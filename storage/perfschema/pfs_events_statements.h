/* Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef PFS_EVENTS_STATEMENTS_H
#define PFS_EVENTS_STATEMENTS_H

/**
  @file storage/perfschema/pfs_events_statements.h
  Events statements data structures (declarations).
*/

#include "pfs_column_types.h"
#include "pfs_events.h"

struct PFS_thread;
struct PFS_user_host;
struct PFS_user;
struct PFS_host;

/** A statement record. */
struct PFS_events_statements : public PFS_events
{
  /** Database name. */
  char m_current_schema_name[NAME_LEN];
  /** Length of @c m_current_schema_name. */
  uint m_current_schema_name_length;
  /** SQL_TEXT */
  char m_sqltext[COL_INFO_SIZE];
  /** Length of @ m_info. */
  uint m_sqltext_length;

  /* Lock */
  ulonglong m_lock_time;

  /* Diagnostics area */
  char m_message_text[MYSQL_ERRMSG_SIZE+1];
  uint m_sql_errno;
  char m_sqlstate[SQLSTATE_LENGTH];
  uint m_error_count;
  uint m_warning_count;
  ulonglong m_rows_affected;

  /* Optimizer */
  ulonglong m_rows_sent;
  ulonglong m_rows_examined;
  ulonglong m_created_tmp_disk_tables;
  ulonglong m_created_tmp_tables;
  ulonglong m_select_full_join;
  ulonglong m_select_full_range_join;
  ulonglong m_select_range;
  ulonglong m_select_range_check;
  ulonglong m_select_scan;
  ulonglong m_sort_merge_passes;
  ulonglong m_sort_range;
  ulonglong m_sort_rows;
  ulonglong m_sort_scan;
  ulonglong m_no_index_used;
  ulonglong m_no_good_index_used;
};

void insert_events_statements_history(PFS_thread *thread, PFS_events_statements *statement);
void insert_events_statements_history_long(PFS_events_statements *statement);

extern bool flag_events_statements_current;
extern bool flag_events_statements_history;
extern bool flag_events_statements_history_long;

extern bool events_statements_history_long_full;
extern volatile uint32 events_statements_history_long_index;
extern PFS_events_statements *events_statements_history_long_array;
extern ulong events_statements_history_long_size;

int init_events_statements_history_long(uint events_statements_history_long_sizing);
void cleanup_events_statements_history_long();

void reset_events_statements_current();
void reset_events_statements_history();
void reset_events_statements_history_long();
void reset_events_statements_by_thread();
void reset_events_statements_by_user_host();
void reset_events_statements_by_user();
void reset_events_statements_by_host();
void reset_events_statements_global();
void aggregate_user_host_statements(PFS_user_host *user_host);
void aggregate_user_statements(PFS_user *user);
void aggregate_host_statements(PFS_host *host);

#endif

