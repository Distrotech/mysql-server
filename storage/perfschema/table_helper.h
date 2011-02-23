/* Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.

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

#ifndef PFS_TABLE_HELPER_H
#define PFS_TABLE_HELPER_H

#include "pfs_column_types.h"
#include "pfs_stat.h"
#include "pfs_timer.h"
#include "pfs_engine_table.h"
#include "pfs_instr_class.h"

/**
  @file storage/perfschema/table_helper.h
  Performance schema table helpers (declarations).
*/

/**
  @addtogroup Performance_schema_tables
  @{
*/

/** Namespace, internal views used within table setup_instruments. */
struct PFS_instrument_view_constants
{
  static const uint FIRST_VIEW= 1;
  static const uint VIEW_MUTEX= 1;
  static const uint VIEW_RWLOCK= 2;
  static const uint VIEW_COND= 3;
  static const uint VIEW_FILE= 4;
  static const uint VIEW_TABLE= 5;
  static const uint VIEW_SOCKET= 6;
  static const uint LAST_VIEW= 6;
};

/** Namespace, internal views used within object summaries. */
struct PFS_object_view_constants
{
  static const uint FIRST_VIEW= 1;
  static const uint VIEW_TABLE= 1;
  static const uint LAST_VIEW= 1;

  /* Future use */
  static const uint VIEW_EVENT= 2;
  static const uint VIEW_PROCEDURE= 3;
  static const uint VIEW_FUNCTION= 4;
};

/** Row fragment for column EVENT_NAME. */
struct PFS_event_name_row
{
  /** Column EVENT_NAME. */
  const char *m_name;
  /** Length in bytes of @c m_name. */
  uint m_name_length;

  /** Build a row from a memory buffer. */
  inline void make_row(PFS_instr_class *pfs)
  {
    m_name= pfs->m_name;
    m_name_length= pfs->m_name_length;
  }

  /** Set a table field from the row. */
  inline void set_field(Field *f)
  {
    PFS_engine_table::set_field_varchar_utf8(f, m_name, m_name_length);
  }
};

/** Row fragment for columns OBJECT_TYPE, SCHEMA_NAME, OBJECT_NAME. */
struct PFS_object_row
{
  /** Column OBJECT_TYPE. */
  enum_object_type m_object_type;
  /** Column SCHEMA_NAME. */
  char m_schema_name[NAME_LEN];
  /** Length in bytes of @c m_schema_name. */
  uint m_schema_name_length;
  /** Column OBJECT_NAME. */
  char m_object_name[NAME_LEN];
  /** Length in bytes of @c m_object_name. */
  uint m_object_name_length;

  /** Build a row from a memory buffer. */
  int make_row(PFS_table_share *pfs);
  /** Set a table field from the row. */
  void set_field(uint index, Field *f);
};

/** Row fragment for columns OBJECT_TYPE, SCHEMA_NAME, OBJECT_NAME, INDEX_NAME. */
struct PFS_index_row
{
  PFS_object_row m_object_row;
  /** Column INDEX_NAME. */
  char m_index_name[NAME_LEN];
  /** Length in bytes of @c m_index_name. */
  uint m_index_name_length;

  /** Build a row from a memory buffer. */
  int make_row(PFS_table_share *pfs, uint table_index);
  /** Set a table field from the row. */
  void set_field(uint index, Field *f);
};

/** Row fragment for single statistics columns (COUNT, SUM, MIN, AVG, MAX) */
struct PFS_stat_row
{
  /** Column COUNT_STAR. */
  ulonglong m_count;
  /** Column SUM_TIMER_WAIT. */
  ulonglong m_sum;
  /** Column MIN_TIMER_WAIT. */
  ulonglong m_min;
  /** Column AVG_TIMER_WAIT. */
  ulonglong m_avg;
  /** Column MAX_TIMER_WAIT. */
  ulonglong m_max;

  /** Build a row with timer fields from a memory buffer. */
  inline void set(time_normalizer *normalizer, const PFS_single_stat *stat)
  {
    m_count= stat->m_count;

    if (m_count)
    {
      m_sum= normalizer->wait_to_pico(stat->m_sum);
      m_min= normalizer->wait_to_pico(stat->m_min);
      m_max= normalizer->wait_to_pico(stat->m_max);
      m_avg= normalizer->wait_to_pico(stat->m_sum / m_count);
    }
    else
    {
      m_sum= 0;
      m_min= 0;
      m_avg= 0;
      m_max= 0;
    }
  }

  /** Build a row with count values from a memory buffer. */
  inline void set(const PFS_single_stat *stat)
  {
    m_count= stat->m_count;

    if (m_count)
    {
      m_sum= stat->m_sum;
      m_min= stat->m_min;
      m_max= stat->m_max;
      m_avg= (stat->m_sum / m_count);
    }
    else
    {
      m_sum= 0;
      m_min= 0;
      m_avg= 0;
      m_max= 0;
    }
  }

  /** Set a table field from the row. */
  void set_field(uint index, Field *f)
  {
    switch (index)
    {
      case 0: /* COUNT */
        PFS_engine_table::set_field_ulonglong(f, m_count);
        break;
      case 1: /* SUM */
        PFS_engine_table::set_field_ulonglong(f, m_sum);
        break;
      case 2: /* MIN */
        PFS_engine_table::set_field_ulonglong(f, m_min);
        break;
      case 3: /* AVG */
        PFS_engine_table::set_field_ulonglong(f, m_avg);
        break;
      case 4: /* MAX */
        PFS_engine_table::set_field_ulonglong(f, m_max);
        break;
      default:
        DBUG_ASSERT(false);
    }
  }
};

/** Row fragment for timer and byte count stats. Corresponds to PFS_multi_stat */
struct PFS_multi_stat_row
{
  PFS_stat_row m_waits;
  PFS_stat_row m_bytes;

  /** Build a row with timer and byte count fields from a memory buffer. */
  inline void set(time_normalizer *normalizer, const PFS_multi_stat *stat)
  {
    m_waits.set(normalizer, &stat->m_waits);
    m_bytes.set(&stat->m_bytes);
  }
};

/** Row fragment for table io statistics columns. */
struct PFS_table_io_stat_row
{
  PFS_stat_row m_all;
  PFS_stat_row m_all_read;
  PFS_stat_row m_all_write;
  PFS_stat_row m_fetch;
  PFS_stat_row m_insert;
  PFS_stat_row m_update;
  PFS_stat_row m_delete;

  /** Build a row from a memory buffer. */
  inline void set(time_normalizer *normalizer, const PFS_table_io_stat *stat)
  {
    PFS_single_stat all_read;
    PFS_single_stat all_write;
    PFS_single_stat all;

    m_fetch.set(normalizer, & stat->m_fetch);

    all_read.aggregate(& stat->m_fetch);

    m_insert.set(normalizer, & stat->m_insert);
    m_update.set(normalizer, & stat->m_update);
    m_delete.set(normalizer, & stat->m_delete);

    all_write.aggregate(& stat->m_insert);
    all_write.aggregate(& stat->m_update);
    all_write.aggregate(& stat->m_delete);

    all.aggregate(& all_read);
    all.aggregate(& all_write);

    m_all_read.set(normalizer, & all_read);
    m_all_write.set(normalizer, & all_write);
    m_all.set(normalizer, & all);
  }
};

/** Row fragment for table lock statistics columns. */
struct PFS_table_lock_stat_row
{
  PFS_stat_row m_all;
  PFS_stat_row m_all_read;
  PFS_stat_row m_all_write;
  PFS_stat_row m_read_normal;
  PFS_stat_row m_read_with_shared_locks;
  PFS_stat_row m_read_high_priority;
  PFS_stat_row m_read_no_insert;
  PFS_stat_row m_read_external;
  PFS_stat_row m_write_allow_write;
  PFS_stat_row m_write_concurrent_insert;
  PFS_stat_row m_write_delayed;
  PFS_stat_row m_write_low_priority;
  PFS_stat_row m_write_normal;
  PFS_stat_row m_write_external;

  /** Build a row from a memory buffer. */
  inline void set(time_normalizer *normalizer, const PFS_table_lock_stat *stat)
  {
    PFS_single_stat all_read;
    PFS_single_stat all_write;
    PFS_single_stat all;

    m_read_normal.set(normalizer, & stat->m_stat[PFS_TL_READ]);
    m_read_with_shared_locks.set(normalizer, & stat->m_stat[PFS_TL_READ_WITH_SHARED_LOCKS]);
    m_read_high_priority.set(normalizer, & stat->m_stat[PFS_TL_READ_HIGH_PRIORITY]);
    m_read_no_insert.set(normalizer, & stat->m_stat[PFS_TL_READ_NO_INSERT]);
    m_read_external.set(normalizer, & stat->m_stat[PFS_TL_READ_EXTERNAL]);

    all_read.aggregate(& stat->m_stat[PFS_TL_READ]);
    all_read.aggregate(& stat->m_stat[PFS_TL_READ_WITH_SHARED_LOCKS]);
    all_read.aggregate(& stat->m_stat[PFS_TL_READ_HIGH_PRIORITY]);
    all_read.aggregate(& stat->m_stat[PFS_TL_READ_NO_INSERT]);
    all_read.aggregate(& stat->m_stat[PFS_TL_READ_EXTERNAL]);

    m_write_allow_write.set(normalizer, & stat->m_stat[PFS_TL_WRITE_ALLOW_WRITE]);
    m_write_concurrent_insert.set(normalizer, & stat->m_stat[PFS_TL_WRITE_CONCURRENT_INSERT]);
    m_write_delayed.set(normalizer, & stat->m_stat[PFS_TL_WRITE_DELAYED]);
    m_write_low_priority.set(normalizer, & stat->m_stat[PFS_TL_WRITE_LOW_PRIORITY]);
    m_write_normal.set(normalizer, & stat->m_stat[PFS_TL_WRITE]);
    m_write_external.set(normalizer, & stat->m_stat[PFS_TL_WRITE_EXTERNAL]);

    all_write.aggregate(& stat->m_stat[PFS_TL_WRITE_ALLOW_WRITE]);
    all_write.aggregate(& stat->m_stat[PFS_TL_WRITE_CONCURRENT_INSERT]);
    all_write.aggregate(& stat->m_stat[PFS_TL_WRITE_DELAYED]);
    all_write.aggregate(& stat->m_stat[PFS_TL_WRITE_LOW_PRIORITY]);
    all_write.aggregate(& stat->m_stat[PFS_TL_WRITE]);
    all_write.aggregate(& stat->m_stat[PFS_TL_WRITE_EXTERNAL]);

    all.aggregate(& all_read);
    all.aggregate(& all_write);

    m_all_read.set(normalizer, & all_read);
    m_all_write.set(normalizer, & all_write);
    m_all.set(normalizer, & all);
  }
};

/** Row fragment for socket io statistics columns. */
struct PFS_socket_io_stat_row
{
  PFS_multi_stat_row m_recv;
  PFS_multi_stat_row m_send;
  PFS_multi_stat_row m_recvfrom;
  PFS_multi_stat_row m_sendto;
  PFS_multi_stat_row m_recvmsg;
  PFS_multi_stat_row m_sendmsg;
  PFS_multi_stat_row m_misc;
  PFS_multi_stat_row m_all_read;
  PFS_multi_stat_row m_all_write;
  PFS_multi_stat_row m_all;

  inline void set(time_normalizer *normalizer, const PFS_socket_io_stat *stat)
  {
    PFS_multi_stat all_read;
    PFS_multi_stat all_write;
    PFS_multi_stat all;

    /* Combine receive operations */
    m_recv.set(normalizer, &stat->m_recv);
    m_recvfrom.set(normalizer, &stat->m_recvfrom);
    m_recvmsg.set(normalizer, &stat->m_recvmsg);

    all_read.aggregate(&stat->m_recv);
    all_read.aggregate(&stat->m_recvfrom);
    all_read.aggregate(&stat->m_recvmsg);

    /* Combine send operations */
    m_send.set(normalizer, &stat->m_send);
    m_sendto.set(normalizer, &stat->m_sendto);
    m_sendmsg.set(normalizer, &stat->m_sendmsg);

    all_write.aggregate(&stat->m_send);
    all_write.aggregate(&stat->m_sendto);
    all_write.aggregate(&stat->m_sendmsg);

    /* Combine row values for miscellaneous socket operations */
    m_misc.set(normalizer, &stat->m_misc);
    
    /* Combine timer stats for all operations */
    all.aggregate(&all_read);
    all.aggregate(&all_write);
    all.aggregate(&stat->m_misc);

    m_all_read.set(normalizer, &all_read);
    m_all_write.set(normalizer, &all_write);
    m_all.set(normalizer, &all);
  }
};

void set_field_object_type(Field *f, enum_object_type object_type);

/** @} */

#endif

