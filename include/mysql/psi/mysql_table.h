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

#ifndef MYSQL_TABLE_H
#define MYSQL_TABLE_H

/**
  @file mysql/psi/mysql_table.h
  Instrumentation helpers for table io.
*/

#include "mysql/psi/psi.h"

/**
  @defgroup Table_instrumentation Table Instrumentation
  @ingroup Instrumentation_interface
  @{
*/

#ifdef HAVE_PSI_INTERFACE
  #define MYSQL_START_TABLE_WAIT(PSI, OP, INDEX, FLAGS) \
    inline_mysql_start_table_wait(PSI, OP, INDEX, FLAGS, __FILE__, __LINE__)
#else
  #define MYSQL_START_TABLE_WAIT(PSI, OP, INDEX, FLAGS) \
    NULL
#endif

#ifdef HAVE_PSI_INTERFACE
  #define MYSQL_END_TABLE_WAIT(L) \
    inline_mysql_end_table_wait(L)
#else
  #define MYSQL_END_TABLE_WAIT(L) \
    do {} while (0)
#endif

#ifdef HAVE_PSI_INTERFACE
static inline struct PSI_table_locker *
inline_mysql_start_table_wait(struct PSI_table *psi, enum PSI_table_operation op,
                              uint index, ulong flags,
                              const char *src_file, int src_line)
{
  struct PSI_table_locker *locker= NULL;
  if (likely(PSI_server && psi))
  {
    locker= PSI_server->get_thread_table_locker(psi, op, flags);
    if (likely(locker != NULL))
      PSI_server->start_table_wait(locker, index, src_file, src_line);
  }
  return locker;
}

static inline void
inline_mysql_end_table_wait(struct PSI_table_locker *locker)
{
  if (likely(locker != NULL))
    PSI_server->end_table_wait(locker);
}
#endif

/** @} (end of group Table_instrumentation) */

#endif

