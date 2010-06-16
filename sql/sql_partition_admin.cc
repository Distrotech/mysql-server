/* Copyright (c) 2010 Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "sql_parse.h"                      // check_access,
                                            // check_merge_table_access
#include "sql_table.h"                      // mysql_alter_table, etc.
#include "sql_alter_table.h"                // Alter_table_statement
#include "sql_partition_admin.h"            // Alter_table_exchange_*
#include "sql_partition.h"                  // struct partition_info, etc.
#include "sql_handler.h"                    // mysql_ha_rm_tables
#include "sql_base.h"                       // open_and_lock_tables, etc
#include "lock.h"                           // MYSQL_OPEN_TAKE_UPGRADABLE_MDL
#include "debug_sync.h"                     // DEBUG_SYNC

#ifndef WITH_PARTITION_STORAGE_ENGINE

bool Partition_statement_unsupported::execute(THD *)
{
  DBUG_ENTER("Partition_statement_unsupported::execute");
  /* error, partitioning support not compiled in... */
  my_error(ER_FEATURE_DISABLED, MYF(0), "partitioning",
           "--with-plugin-partition");
  DBUG_RETURN(TRUE);
}

#else

bool Alter_table_exchange_partition_statement::execute(THD *thd)
{
  /* Moved from mysql_execute_command */
  LEX *lex= thd->lex;
  /* first SELECT_LEX (have special meaning for many of non-SELECTcommands) */
  SELECT_LEX *select_lex= &lex->select_lex;
  /* first table of first SELECT_LEX */
  TABLE_LIST *first_table= (TABLE_LIST*) select_lex->table_list.first;
  /*
    Code in mysql_alter_table() may modify its HA_CREATE_INFO argument,
    so we have to use a copy of this structure to make execution
    prepared statement- safe. A shallow copy is enough as no memory
    referenced from this structure will be modified.
    @todo move these into constructor...
  */
  HA_CREATE_INFO create_info(lex->create_info);
  Alter_info alter_info(lex->alter_info, thd->mem_root);
  ulong priv_needed= ALTER_ACL | DROP_ACL | INSERT_ACL | CREATE_ACL;

  DBUG_ENTER("Alter_table_exchange_partition_statement::execute");

  if (thd->is_fatal_error) /* out of memory creating a copy of alter_info */
    DBUG_RETURN(TRUE);

  /* Must be set in the parser */
  DBUG_ASSERT(select_lex->db);
  /* also check the table to be exchanged with the partition */
  DBUG_ASSERT(alter_info.flags & ALTER_EXCHANGE_PARTITION);

  if (check_access(thd, priv_needed, first_table->db,
                   &first_table->grant.privilege,
                   &first_table->grant.m_internal,
                   0, 0) ||
      check_access(thd, priv_needed, first_table->next_local->db,
                   &first_table->next_local->grant.privilege,
                   &first_table->next_local->grant.m_internal,
                   0, 0))
    DBUG_RETURN(TRUE);

  if (check_grant(thd, priv_needed, first_table, FALSE, UINT_MAX, FALSE))
    DBUG_RETURN(TRUE);

  /* Not allowed with EXCHANGE PARTITION */
  DBUG_ASSERT(!create_info.data_file_name && !create_info.index_file_name);

  thd->enable_slow_log= opt_log_slow_admin_statements;
  DBUG_RETURN(exchange_partition(thd, first_table, &alter_info));
}


/**
  @brief Checks that the tables will be able to be used for EXCHANGE PARTITION.
  @param table      Non partitioned table.
  @param part_table Partitioned table.

  @retval FALSE if OK, otherwise error is reported and TRUE is returned.
*/
static bool check_exchange_partition(TABLE *table, TABLE *part_table)
{
  DBUG_ENTER("check_exchange_partition");

  /* Both tables must exist */
  if (!part_table || !table)
  {
    my_error(ER_CHECK_NO_SUCH_TABLE, MYF(0));
    DBUG_RETURN(TRUE);
  }

  /* The first table must be partitioned, and the second must not */
  if (!part_table->part_info)
  {
    my_error(ER_PARTITION_MGMT_ON_NONPARTITIONED, MYF(0));
    DBUG_RETURN(TRUE);
  }
  if (table->part_info)
  {
    my_error(ER_PARTITION_EXCHANGE_PART_TABLE, MYF(0),
             table->s->table_name.str);
    DBUG_RETURN(TRUE);
  }

  if (part_table->file->ht != partition_hton)
  {
    /*
      Only allowed on partitioned tables throught the generic ha_partition
      handler, i.e not yet for native partitioning (NDB).
    */
    my_error(ER_PARTITION_MGMT_ON_NONPARTITIONED, MYF(0));
    DBUG_RETURN(TRUE);
  }

  if (table->file->ht != part_table->part_info->default_engine_type)
  {
    my_error(ER_MIX_HANDLER_ERROR, MYF(0));
    DBUG_RETURN(TRUE);
  }

  /* Verify that table is not tmp table, partitioned tables cannot be tmp. */
  if (table->s->tmp_table != NO_TMP_TABLE)
  {
    my_error(ER_PARTITION_EXCHANGE_TEMP_TABLE, MYF(0),
             table->s->table_name.str);
    DBUG_RETURN(TRUE);
  }
  DBUG_RETURN(FALSE);
}


/**
  @brief Compare table structure/options between a non partitioned table
  and a specific partition of a partitioned table.

  @param thd        Thread object.
  @param table      Non partitioned table.
  @param part_table Partitioned table.
  @param part_elem  Partition element to use for partition specific compare.
*/
static bool compare_table_with_partition(THD *thd, TABLE *table,
                                         TABLE *part_table,
                                         partition_element *part_elem)
{
  HA_CREATE_INFO table_create_info, part_create_info;
  Alter_info part_alter_info;
  Alter_table_change_level alter_change_level;
  /* Unused */
  KEY *key_info_buffer;
  uint *index_drop_buffer, index_drop_count;
  uint *index_add_buffer, index_add_count;
  uint candidate_key_count= 0;
  DBUG_ENTER("compare_table_with_partition");

  alter_change_level= ALTER_TABLE_METADATA_ONLY;
  bzero(&part_create_info, sizeof(HA_CREATE_INFO));
  bzero(&table_create_info, sizeof(HA_CREATE_INFO));

  update_create_info_from_table(&table_create_info, table);
  /* get the current auto_increment value */
  table->file->update_create_info(&table_create_info);
  if (mysql_prepare_alter_table(thd, part_table, &part_create_info,
                                &part_alter_info))
  {
    my_error(ER_TABLES_DIFFERENT_METADATA, MYF(0));
    DBUG_RETURN(TRUE);
  }
  /* db_type is not set in prepare_alter_table */
  part_create_info.db_type= part_table->part_info->default_engine_type;
  /*
    Since we exchange the partition with the table, allow exchanging
    auto_increment value as well.
  */
  part_create_info.auto_increment_value=
                                table_create_info.auto_increment_value;

  /*
    NOTE: ha_blackhole does not support check_if_compatible_data,
    so this always fail for blackhole tables.
    ha_myisam compares pointers to verify that DATA/INDEX DIRECTORY is
    the same, so any table using data/index_file_name will fail.
  */
  if (mysql_compare_tables(table, &part_alter_info, &part_create_info,
                           0, &alter_change_level, &key_info_buffer,
                           &index_drop_buffer, &index_drop_count,
                           &index_add_buffer, &index_add_count,
                           &candidate_key_count, TRUE))
  {
    my_error(ER_TABLES_DIFFERENT_METADATA, MYF(0));
    DBUG_RETURN(TRUE);
  }

  DEBUG_SYNC(thd, "swap_partition_after_compare_tables");
  if (alter_change_level != ALTER_TABLE_METADATA_ONLY)
  {
    my_error(ER_TABLES_DIFFERENT_METADATA, MYF(0));
    DBUG_RETURN(TRUE);
  }

  if (table_create_info.avg_row_length != part_create_info.avg_row_length)
  {
    my_error(ER_PARTITION_EXCHANGE_DIFFERENT_OPTION, MYF(0),
             "AVG_ROW_LENGTH");
    DBUG_RETURN(TRUE);
  }

  if (table_create_info.table_options != part_create_info.table_options)
  {
    my_error(ER_PARTITION_EXCHANGE_DIFFERENT_OPTION, MYF(0),
             "TABLE OPTION");
    DBUG_RETURN(TRUE);
  }

  if (table->s->table_charset != part_table->s->table_charset)
  {
    my_error(ER_PARTITION_EXCHANGE_DIFFERENT_OPTION, MYF(0),
             "CHARACTER SET");
    DBUG_RETURN(TRUE);
  }

  /*
    NOTE: We do not support update of frm-file, i.e. change
    max/min_rows, data/index_file_name etc.
    The workaround is to use REORGANIZE PARTITION to rewrite
    the frm file and then use EXCHANGE PARTITION when they are the same.
  */
  if (compare_partition_options(&table_create_info, part_elem))
    DBUG_RETURN(TRUE);

  DBUG_RETURN(FALSE);
}


/**
  @brief Exchange partition/table with ddl log.

  @details How to handle a crash in the middle of the rename (break on error):
  1) register in ddl_log that we are going to exchange swap_table with part.
  2) do the first rename (swap_table -> tmp-name) and sync the ddl_log.
  3) do the second rename (part -> swap_table) and sync the ddl_log.
  4) do the last rename (tmp-name -> part).
  5) mark the entry done.

  Recover by:
    5) is done, All completed. Nothing to recover.
    4) is done see 3). (No mark or sync in the ddl_log...)
    3) is done -> try rename part -> tmp-name (ignore failure) goto 2).
    2) is done -> try rename swap_table -> part (ignore failure) goto 1).
    1) is done -> try rename tmp-name -> swap_table (ignore failure).
    before 1) Nothing to recover...

  @param thd        Thread handle
  @param name       name of table/partition 1 (to be exchanged with 2)
  @param from_name  name of table/partition 2 (to be exchanged with 1)
  @param tmp_name   temporary name to use while exchaning
  @param ht         handlerton of the table/partitions

  @return Operation status
    @retval TRUE    Error
    @retval FALSE   Success

  @note ha_heap always succeeds in rename (since it is created upon usage).
  This is OK when to recover from a crash since all heap are empty and the
  recover is done early in the startup of the server (right before
  read_init_file which can populate the tables).

  And if no crash we can trust the syncs in the ddl_log.

  What about if the rename is put into a background thread? That will cause
  corruption and is avoided by the exlusive metadata lock.
*/
static bool exchange_name_with_ddl_log(THD *thd,
                                       const char *name,
                                       const char *from_name,
                                       const char *tmp_name,
                                       handlerton *ht)
{
  DDL_LOG_ENTRY exchange_entry;
  DDL_LOG_MEMORY_ENTRY *log_entry= NULL;
  DDL_LOG_MEMORY_ENTRY *exec_log_entry= NULL;
  bool error= TRUE;
  bool error_set= FALSE;
  handler *file= NULL;
  DBUG_ENTER("exchange_name_with_ddl_log");

  if (!(file= get_new_handler(NULL, thd->mem_root, ht)))
  {
    mem_alloc_error(sizeof(handler));
    DBUG_RETURN(TRUE);
  }

  /* prepare the action entry */
  exchange_entry.entry_type=   DDL_LOG_ENTRY_CODE;
  exchange_entry.action_type=  DDL_LOG_EXCHANGE_ACTION;
  exchange_entry.next_entry=   0;
  exchange_entry.name=         name;
  exchange_entry.from_name=    from_name;
  exchange_entry.tmp_name=     tmp_name;
  exchange_entry.handler_name= ha_resolve_storage_engine_name(ht);
  exchange_entry.phase=        EXCH_PHASE_NAME_TO_TEMP;

  mysql_mutex_lock(&LOCK_gdl);
  /*
    write to the ddl log what to do by:
    1) write the action entry (i.e. which names to be exchanged)
    2) write the execution entry with a link to the action entry
  */
  DBUG_EXECUTE_IF("exchange_partition_fail_1", goto err_no_action_written;);
  DBUG_EXECUTE_IF("exchange_partition_abort_1", abort(););
  if (write_ddl_log_entry(&exchange_entry, &log_entry))
    goto err_no_action_written;

  DBUG_EXECUTE_IF("exchange_partition_fail_2", goto err_no_execute_written;);
  DBUG_EXECUTE_IF("exchange_partition_abort_2", abort(););
  if (write_execute_ddl_log_entry(log_entry->entry_pos, FALSE, &exec_log_entry))
    goto err_no_execute_written;
  /* ddl_log is written and synced */

  mysql_mutex_unlock(&LOCK_gdl);
  /*
    Execute the name exchange.
    Do one rename, increase the phase, update the action entry and sync.
    In case of errors in the ddl_log we must fail and let the ddl_log try
    to revert the changes, since otherwise it could revert the command after
    we sent OK to the client.
  */
  /* call rename table from table to tmp-name */
  DBUG_EXECUTE_IF("exchange_partition_fail_3",
                  my_error(ER_ERROR_ON_RENAME, MYF(0),
                           name, tmp_name);
                  error_set= TRUE;
                  goto err_rename;);
  DBUG_EXECUTE_IF("exchange_partition_abort_3", abort(););
  if (file->ha_rename_table(name, tmp_name))
  {
    my_error(ER_ERROR_ON_RENAME, MYF(0),
             name, tmp_name);
    error_set= TRUE;
    goto err_rename;
  }
  DBUG_EXECUTE_IF("exchange_partition_fail_4", goto err_rename;);
  DBUG_EXECUTE_IF("exchange_partition_abort_4", abort(););
  if (deactivate_ddl_log_entry(log_entry->entry_pos))
    goto err_rename;

  /* call rename table from partition to table */
  DBUG_EXECUTE_IF("exchange_partition_fail_5",
                  my_error(ER_ERROR_ON_RENAME, MYF(0),
                           from_name, name);
                  error_set= TRUE;
                  goto err_rename;);
  DBUG_EXECUTE_IF("exchange_partition_abort_5", abort(););
  if (file->ha_rename_table(from_name, name))
  {
    my_error(ER_ERROR_ON_RENAME, MYF(0),
             from_name, name);
    error_set= TRUE;
    goto err_rename;
  }
  DBUG_EXECUTE_IF("exchange_partition_fail_6", goto err_rename;);
  DBUG_EXECUTE_IF("exchange_partition_abort_6", abort(););
  if (deactivate_ddl_log_entry(log_entry->entry_pos))
    goto err_rename;

  /* call rename table from tmp-nam to partition */
  DBUG_EXECUTE_IF("exchange_partition_fail_7",
                  my_error(ER_ERROR_ON_RENAME, MYF(0),
                           tmp_name, from_name);
                  error_set= TRUE;
                  goto err_rename;);
  DBUG_EXECUTE_IF("exchange_partition_abort_7", abort(););
  if (file->ha_rename_table(tmp_name, from_name))
  {
    my_error(ER_ERROR_ON_RENAME, MYF(0),
             tmp_name, from_name);
    error_set= TRUE;
    goto err_rename;
  }
  DBUG_EXECUTE_IF("exchange_partition_fail_8", goto err_rename;);
  DBUG_EXECUTE_IF("exchange_partition_abort_8", abort(););
  if (deactivate_ddl_log_entry(log_entry->entry_pos))
    goto err_rename;

  /* The exchange is complete and ddl_log is deactivated */
  DBUG_EXECUTE_IF("exchange_partition_fail_9", goto err_rename;);
  DBUG_EXECUTE_IF("exchange_partition_abort_9", abort(););
  /* all OK */
  error= FALSE;
  delete file;
  DBUG_RETURN(error);
err_rename:
  /*
    Nothing to do if any of these commands fails :( the commands itselfs
    will log to the error log about the failures...
  */
  /* execute the ddl log entry to revert the renames */
  (void) execute_ddl_log_entry(current_thd, log_entry->entry_pos);
  mysql_mutex_lock(&LOCK_gdl);
  /* mark the execute log entry done */
  (void) write_execute_ddl_log_entry(0, TRUE, &exec_log_entry);
  /* release the execute log entry */
  (void) release_ddl_log_memory_entry(exec_log_entry);
err_no_execute_written:
  /* release the action log entry */
  (void) release_ddl_log_memory_entry(log_entry);
err_no_action_written:
  mysql_mutex_unlock(&LOCK_gdl);
  delete file;
  if (!error_set)
    my_error(ER_DDL_LOG_ERROR, MYF(0));
  DBUG_RETURN(error);
}


/**
  @brief Swap places between a partition and a table.

  @details Verify that the tables are compatible (same engine, definition etc),
  if not IGNORE is given, verify that all rows in the table will fit in the
  partition, if all OK, rename table to tmp name, rename partition to table
  and finally rename tmp name to partition.

  1) Take upgradable mdl, open tables and then lock them (inited in parse)
  2) Verify that metadata matches
  3) If not ignore, verify data
  4) Upgrade to exclusive mdl for both tables
  5) Rename table <-> partition
  6) Rely on close_thread_tables to release mdl and table locks

  @param thd            Thread handle
  @param table_list     Table where the partition exists as first table,
                        Table to swap with the partition as second table
  @param alter_info     Contains partition name to swap
  @param ignore         flag to skip verification of partition values

  @note This is a DDL operation so triggers will not be used.
*/
bool Alter_table_exchange_partition_statement::
  exchange_partition(THD *thd, TABLE_LIST *table_list, Alter_info *alter_info)
{
  TABLE *part_table, *swap_table;
  TABLE_LIST *swap_table_list;
  handlerton *table_hton;
  partition_element *part_elem;
  char *partition_name;
  char temp_name[FN_REFLEN+1];
  char part_file_name[FN_REFLEN+1];
  char swap_file_name[FN_REFLEN+1];
  char temp_file_name[FN_REFLEN+1];
  uint swap_part_id;
  uint part_file_name_len;
  Alter_table_prelocking_strategy alter_prelocking_strategy(alter_info);
  MDL_ticket *swap_table_mdl_ticket= NULL;
  MDL_ticket *part_table_mdl_ticket= NULL;
  bool error= TRUE, ignore= m_lex->ignore;
  DBUG_ENTER("mysql_exchange_partition");
  DBUG_ASSERT(alter_info->flags & ALTER_EXCHANGE_PARTITION);

  partition_name= alter_info->partition_names.head();

  /* Clear open tables from the threads table handler cache */
  mysql_ha_rm_tables(thd, table_list);

  /* Don't allow to exchange with log table */
  swap_table_list= table_list->next_local;
  if (check_if_log_table(swap_table_list->db_length, swap_table_list->db,
                         swap_table_list->table_name_length,
                         swap_table_list->table_name, 0))
  {
    my_error(ER_WRONG_USAGE, MYF(0), "PARTITION", "log table");
    DBUG_RETURN(TRUE);
  }

  /*
    Currently no MDL lock that allows both read and write and is upgradeable
    to exclusive, so leave the lock type to TL_WRITE_ALLOW_READ also on the
    partitioned table.

    TODO: add MDL lock that allows both read and write and is upgradable to
    exclusive lock. This would allow to continue using the partitioned table
    also with update/insert/delete while the verification of the swap table
    is running.
  */

  /*
    NOTE: It is not possible to exchange a crashed partition/table since
    we need some info from the engine, which we can only access after open,
    to be able to verify the structure/metadata.
  */
  if (open_and_lock_tables(thd, table_list, FALSE,
                           MYSQL_OPEN_TAKE_UPGRADABLE_MDL,
                           &alter_prelocking_strategy))
    DBUG_RETURN(TRUE);

  part_table= table_list->table;
  swap_table= swap_table_list->table;
  table_hton= swap_table->file->ht;

  if (check_exchange_partition(swap_table, part_table))
    DBUG_RETURN(TRUE);

  thd_proc_info(thd, "verifying table");

  /* Will append the partition name later in part_info->get_part_elem() */
  part_file_name_len= build_table_filename(part_file_name,
                                           sizeof(part_file_name),
                                           table_list->db,
                                           table_list->table_name,
                                           "", 0);
  build_table_filename(swap_file_name,
                       sizeof(swap_file_name),
                       swap_table_list->db,
                       swap_table_list->table_name,
                       "", 0);
  /* create a unique temp name #sqlx-nnnn_nnnn, x for eXchange */
  my_snprintf(temp_name, sizeof(temp_name), "%sx-%lx_%lx",
              tmp_file_prefix, current_pid, thd->thread_id);
  if (lower_case_table_names)
    my_casedn_str(files_charset_info, temp_name);
  build_table_filename(temp_file_name, sizeof(temp_file_name),
                       table_list->next_local->db,
                       temp_name, "", FN_IS_TMP);

  if (!(part_elem= part_table->part_info->get_part_elem(partition_name,
                                                        part_file_name +
                                                          part_file_name_len,
                                                        &swap_part_id)))
  {
    my_error(ER_UNKNOWN_PARTITION, MYF(0), partition_name,
             part_table->alias);
    DBUG_RETURN(TRUE);
  }

  if (swap_part_id == NOT_A_PARTITION_ID)
  {
    DBUG_ASSERT(part_table->part_info->is_sub_partitioned());
    my_error(ER_PARTITION_INSTEAD_OF_SUBPARTITION, MYF(0));
    DBUG_RETURN(TRUE);
  }

  if (compare_table_with_partition(thd, swap_table, part_table, part_elem))
    DBUG_RETURN(TRUE);

  /* Table and partition has same structure/options, OK to exchange */

  if (!ignore)
  {
    if (verify_data_with_partition(swap_table, part_table, swap_part_id))
      DBUG_RETURN(TRUE);
  }

  /*
    Get exclusive mdl lock on both tables, alway the non partitioned table
    first. Remember the tickets for downgrading locks later.
  */
  swap_table_mdl_ticket= swap_table->mdl_ticket;
  part_table_mdl_ticket= part_table->mdl_ticket;

  /*
    No need to set used_partitions to only propagate
    HA_EXTRA_PREPARE_FOR_RENAME to one part since no built in engine uses
    that flag. And the action would probably be to force close all other
    instances which is what we are doing any way.
  */
  if (wait_while_table_is_used(thd, swap_table, HA_EXTRA_PREPARE_FOR_RENAME) ||
      wait_while_table_is_used(thd, part_table, HA_EXTRA_PREPARE_FOR_RENAME))
    goto err;

  DEBUG_SYNC(thd, "swap_partition_after_wait");

  close_all_tables_for_name(thd, swap_table->s, FALSE);
  close_all_tables_for_name(thd, part_table->s, FALSE);

  DEBUG_SYNC(thd, "swap_partition_before_rename");

  if (exchange_name_with_ddl_log(thd, swap_file_name, part_file_name,
                                 temp_file_name, table_hton))
    goto err;

  /*
    Reopen tables under LOCK TABLES. Ignore the return value for now. It's
    better to keep master/slave in consistent state. Alternative would be to
    try to revert the exchange operation and issue error.
  */
  (void) thd->locked_tables_list.reopen_tables(thd);

  if ((error= write_bin_log(thd, TRUE, thd->query(), thd->query_length())))
  {
    /*
      The error is reported in write_bin_log().
      We try to revert to make it easier to keep the master/slave in sync.
    */
    (void) exchange_name_with_ddl_log(thd, part_file_name, swap_file_name,
                                      temp_file_name, table_hton);
  }

err:
  if (thd->locked_tables_mode)
  {
    if (swap_table_mdl_ticket)
      swap_table_mdl_ticket->downgrade_exclusive_lock(MDL_SHARED_NO_READ_WRITE);
    if (part_table_mdl_ticket)
      part_table_mdl_ticket->downgrade_exclusive_lock(MDL_SHARED_NO_READ_WRITE);
  }

  if (!error)
    my_ok(thd);

  // For query cache
  table_list->table= NULL;
  table_list->next_local->table= NULL;
  query_cache_invalidate3(thd, table_list, FALSE);

  DBUG_RETURN(error);
}

#endif /* WITH_PARTITION_STORAGE_ENGINE */
