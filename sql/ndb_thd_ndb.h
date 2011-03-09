/*
   Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifndef NDB_THD_NDB_H
#define NDB_THD_NDB_H

#include <my_global.h>
#include <my_base.h>          // ha_rows
#include <sql_list.h>         // List<>
#include <hash.h>             // HASH

#include "ndb_share.h"

#include <kernel/ndb_limits.h> // MAX_NDB_NODES

/*
  Place holder for ha_ndbcluster thread specific data
*/

enum THD_NDB_OPTIONS
{
  TNO_NO_LOG_SCHEMA_OP=  1 << 0,
  /*
    In participating mysqld, do not try to acquire global schema
    lock, as one other mysqld already has the lock.
  */
  TNO_NO_LOCK_SCHEMA_OP= 1 << 1
  /*
    Skip drop of ndb table in delete_table.  Used when calling
    mysql_rm_table_part2 in "show tables", as we do not want to
    remove ndb tables "by mistake".  The table should not exist
    in ndb in the first place.
  */
  ,TNO_NO_NDB_DROP_TABLE=    1 << 2
};

enum THD_NDB_TRANS_OPTIONS
{
  TNTO_INJECTED_APPLY_STATUS= 1 << 0
  ,TNTO_NO_LOGGING=           1 << 1
  ,TNTO_TRANSACTIONS_OFF=     1 << 2
};

class Thd_ndb 
{
  THD* m_thd;

  Thd_ndb(THD*);
  ~Thd_ndb();
public:
  static Thd_ndb* seize(THD*);
  static void release(Thd_ndb* thd_ndb);

  void init_open_tables();

  class Ndb_cluster_connection *connection;
  class Ndb *ndb;
  class ha_ndbcluster *m_handler;
  ulong count;
  uint lock_count;
  uint start_stmt_count;
  uint save_point_count;
  class NdbTransaction *trans;
  bool m_error;
  bool m_slow_path;
  bool m_force_send;

  uint32 options;
  uint32 trans_options;
  List<NDB_SHARE> changed_tables;
  HASH open_tables;
  /*
    This is a memroot used to buffer rows for batched execution.
    It is reset after every execute().
  */
  MEM_ROOT m_batch_mem_root;
  /*
    Estimated pending batched execution bytes, once this is > BATCH_FLUSH_SIZE
    we execute() to flush the rows buffered in m_batch_mem_root.
  */
  uint m_unsent_bytes;
  uint m_batch_size;

  uint m_execute_count;
  uint m_max_violation_count;
  uint m_old_violation_count;
  uint m_conflict_fn_usage_count;

  uint m_scan_count;
  uint m_pruned_scan_count;
  
  uint m_transaction_no_hint_count[MAX_NDB_NODES];
  uint m_transaction_hint_count[MAX_NDB_NODES];

  NdbTransaction *global_schema_lock_trans;
  uint global_schema_lock_count;
  uint global_schema_lock_error;
  uint schema_locks_count; // Number of global schema locks taken by thread
  bool has_required_global_schema_lock(const char* func);

  unsigned m_connect_count;
  bool valid_ndb(void);
  bool recycle_ndb(THD* thd);
};

#endif
