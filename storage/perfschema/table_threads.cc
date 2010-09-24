/* Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "my_global.h"
#include "my_pthread.h"
#include "table_threads.h"
#include "sql_parse.h"
#include "pfs_instr_class.h"
#include "pfs_instr.h"

THR_LOCK table_threads::m_table_lock;

static const TABLE_FIELD_TYPE field_types[]=
{
  {
    { C_STRING_WITH_LEN("THREAD_ID") },
    { C_STRING_WITH_LEN("int(11)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("NAME") },
    { C_STRING_WITH_LEN("varchar(128)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("TYPE") },
    { C_STRING_WITH_LEN("varchar(10)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("PROCESSLIST_ID") },
    { C_STRING_WITH_LEN("int(11)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("PROCESSLIST_USER") },
    { C_STRING_WITH_LEN("varchar(16)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("PROCESSLIST_HOST") },
    { C_STRING_WITH_LEN("varchar(60)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("PROCESSLIST_DB") },
    { C_STRING_WITH_LEN("varchar(64)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("PROCESSLIST_COMMAND") },
    { C_STRING_WITH_LEN("varchar(16)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("PROCESSLIST_TIME") },
    { C_STRING_WITH_LEN("bigint(20)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("PROCESSLIST_STATE") },
    { C_STRING_WITH_LEN("varchar(64)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("PROCESSLIST_INFO") },
    { C_STRING_WITH_LEN("longtext") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("PARENT_THREAD_ID") },
    { C_STRING_WITH_LEN("int(11)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("ROLE") },
    { C_STRING_WITH_LEN("varchar(64)") },
    { NULL, 0}
  },
  {
    { C_STRING_WITH_LEN("INSTRUMENTED") },
    { C_STRING_WITH_LEN("enum(\'YES\',\'NO\')") },
    { NULL, 0}
  }
};

TABLE_FIELD_DEF
table_threads::m_field_def=
{ 14, field_types };

PFS_engine_table_share
table_threads::m_share=
{
  { C_STRING_WITH_LEN("THREADS") },
  &pfs_updatable_acl,
  &table_threads::create,
  NULL, /* write_row */
  NULL, /* delete_all_rows */
  NULL, /* get_row_count */
  1000, /* records */
  sizeof(PFS_simple_index), /* ref length */
  &m_table_lock,
  &m_field_def,
  false /* checked */
};

PFS_engine_table* table_threads::create()
{
  return new table_threads();
}

table_threads::table_threads()
  : PFS_engine_table(& m_share, & m_pos),
  m_row_exists(false), m_pos(0), m_next_pos(0)
{}

void table_threads::reset_position(void)
{
  m_pos.m_index= 0;
  m_next_pos.m_index= 0;
}

int table_threads::rnd_next()
{
  PFS_thread *pfs;

  for (m_pos.set_at(&m_next_pos);
       m_pos.m_index < thread_max;
       m_pos.next())
  {
    pfs= &thread_array[m_pos.m_index];
    if (pfs->m_lock.is_populated())
    {
      make_row(pfs);
      m_next_pos.set_after(&m_pos);
      return 0;
    }
  }

  return HA_ERR_END_OF_FILE;
}

int table_threads::rnd_pos(const void *pos)
{
  PFS_thread *pfs;

  set_position(pos);
  DBUG_ASSERT(m_pos.m_index < thread_max);
  pfs= &thread_array[m_pos.m_index];
  if (pfs->m_lock.is_populated())
  {
    make_row(pfs);
    return 0;
  }

  return HA_ERR_RECORD_DELETED;
}

void table_threads::make_row(PFS_thread *pfs)
{
  pfs_lock lock;
  PFS_thread_class *safe_class;

  m_row_exists= false;

  /* Protect this reader against thread termination */
  pfs->m_lock.begin_optimistic_lock(&lock);

  safe_class= sanitize_thread_class(pfs->m_class);
  if (unlikely(safe_class == NULL))
    return;

  m_row.m_thread_internal_id= pfs->m_thread_internal_id;
  m_row.m_parent_thread_internal_id= pfs->m_parent_thread_internal_id;
  m_row.m_thread_id= pfs->m_thread_id;
  m_row.m_name= safe_class->m_name;
  m_row.m_name_length= safe_class->m_name_length;
  memcpy(m_row.m_username, pfs->m_username, pfs->m_username_length);
  m_row.m_username_length= pfs->m_username_length;
  memcpy(m_row.m_hostname, pfs->m_hostname, pfs->m_hostname_length);
  m_row.m_hostname_length= pfs->m_hostname_length;
  memcpy(m_row.m_dbname, pfs->m_dbname, pfs->m_dbname_length);
  m_row.m_dbname_length= pfs->m_dbname_length;
  m_row.m_command= pfs->m_command;
  m_row.m_start_time= pfs->m_start_time;
  /* FIXME: need to copy it ? */
  m_row.m_processlist_state_ptr= pfs->m_processlist_state_ptr;
  m_row.m_processlist_state_length= pfs->m_processlist_state_length;
  /* FIXME: need to copy it ? */
  m_row.m_processlist_info_ptr= pfs->m_processlist_info_ptr;
  m_row.m_processlist_info_length= pfs->m_processlist_info_length;
  m_row.m_enabled_ptr= &pfs->m_enabled;

  if (pfs->m_lock.end_optimistic_lock(& lock))
    m_row_exists= true;
}

int table_threads::read_row_values(TABLE *table,
                                   unsigned char *buf,
                                   Field **fields,
                                   bool read_all)
{
  Field *f;

  if (unlikely(! m_row_exists))
    return HA_ERR_RECORD_DELETED;

  /* Set the null bits */
  DBUG_ASSERT(table->s->null_bytes == 2);
  buf[0]= 0;
  buf[1]= 0;

  for (; (f= *fields) ; fields++)
  {
    if (read_all || bitmap_is_set(table->read_set, f->field_index))
    {
      switch(f->field_index)
      {
      case 0: /* THREAD_ID */
        set_field_ulong(f, m_row.m_thread_internal_id);
        break;
      case 1: /* NAME */
        set_field_varchar_utf8(f, m_row.m_name, m_row.m_name_length);
        break;
      case 2: /* TYPE */
        if (m_row.m_thread_id != 0)
          set_field_varchar_utf8(f, "FOREGROUND", 10);
        else
          set_field_varchar_utf8(f, "BACKGROUND", 10);
        break;
      case 3: /* PROCESSLIST_ID */
        if (m_row.m_thread_id != 0)
          set_field_ulong(f, m_row.m_thread_id);
        else
          f->set_null();
        break;
      case 4: /* PROCESSLIST_USER */
        if (m_row.m_username_length > 0)
          set_field_varchar_utf8(f, m_row.m_username,
                                 m_row.m_username_length);
        else
          f->set_null();
        break;
      case 5: /* PROCESSLIST_HOST */
        if (m_row.m_hostname_length > 0)
          set_field_varchar_utf8(f, m_row.m_hostname,
                                 m_row.m_hostname_length);
        else
          f->set_null();
        break;
      case 6: /* PROCESSLIST_DB */
        if (m_row.m_dbname_length > 0)
          set_field_varchar_utf8(f, m_row.m_dbname,
                                 m_row.m_dbname_length);
        else
          f->set_null();
        break;
      case 7: /* PROCESSLIST_COMMAND */
        if (m_row.m_thread_id != 0)
          set_field_varchar_utf8(f, command_name[m_row.m_command].str,
                                 command_name[m_row.m_command].length);
        else
          f->set_null();
        break;
      case 8: /* PROCESSLIST_TIME */
        if (m_row.m_start_time)
          set_field_ulonglong(f, my_time(0) - m_row.m_start_time);
        else
          f->set_null();
        break;
      case 9: /* PROCESSLIST_STATE */
        if (m_row.m_processlist_state_length > 0)
          set_field_varchar_utf8(f, m_row.m_processlist_state_ptr,
                                 m_row.m_processlist_state_length);
        else
          f->set_null();
        break;
      case 10: /* PROCESSLIST_INFO */
        if (m_row.m_processlist_info_length > 0)
          set_field_longtext_utf8(f, m_row.m_processlist_info_ptr,
                                  m_row.m_processlist_info_length);
        else
          f->set_null();
        break;
      case 11: /* PARENT_THREAD_ID */
        if (m_row.m_parent_thread_internal_id != 0)
          set_field_ulong(f, m_row.m_parent_thread_internal_id);
        else
          f->set_null();
        break;
      case 12: /* ROLE */
        f->set_null();
        break;
      case 13: /* INSTRUMENTED */
        set_field_enum(f, (*m_row.m_enabled_ptr) ? ENUM_YES : ENUM_NO);
        break;
      default:
        DBUG_ASSERT(false);
      }
    }
  }
  return 0;
}

int table_threads::update_row_values(TABLE *table,
                                     const unsigned char *old_buf,
                                     unsigned char *new_buf,
                                     Field **fields)
{
  Field *f;
  enum_yes_no value;

  for (; (f= *fields) ; fields++)
  {
    if (bitmap_is_set(table->write_set, f->field_index))
    {
      switch(f->field_index)
      {
      case 0: /* THREAD_ID */
      case 1: /* NAME */
      case 2: /* TYPE */
      case 3: /* PROCESSLIST_ID */
      case 4: /* PROCESSLIST_USER */
      case 5: /* PROCESSLIST_HOST */
      case 6: /* PROCESSLIST_DB */
      case 7: /* PROCESSLIST_COMMAND */
      case 8: /* PROCESSLIST_TIME */
      case 9: /* PROCESSLIST_STATE */
      case 10: /* PROCESSLIST_INFO */
      case 11: /* PARENT_THREAD_ID */
      case 12: /* ROLE */
        my_error(ER_WRONG_PERFSCHEMA_USAGE, MYF(0));
        return HA_ERR_WRONG_COMMAND;
      case 13: /* INSTRUMENTED */
        value= (enum_yes_no) get_field_enum(f);
        *m_row.m_enabled_ptr= (value == ENUM_YES) ? true : false;
        break;
      default:
        DBUG_ASSERT(false);
      }
    }
  }
  return 0;
}

