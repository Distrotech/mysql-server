/* Copyright (C) 2009 Sun Microsystems, Inc.

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

#include "mysql_priv.h"

extern "C" {
  void sql_alloc_error_handler(void)
  {
    sql_print_error("%s", ER(ER_OUT_OF_RESOURCES));

    THD *thd= current_thd;
    if (thd && !thd->is_error())
    {
      /*
        This thread is Out Of Memory.
        An OOM condition is a fatal error.
        It should not be caught by error handlers in stored procedures.
        Also, recording that SQL condition in the condition area could
        cause more memory allocations, which in turn could raise more
        OOM conditions, causing recursion in the error handling code itself.
        As a result, my_error() should not be invoked, and the
        thread diagnostics area is set to an error status directly.
        Note that Diagnostics_area::set_error_status() is safe,
        since it does not call any memory allocation routines.
        The visible result for a client application will be:
        - a query fails with an ER_OUT_OF_RESOURCES error,
        returned in the error packet.
        - SHOW ERROR/SHOW WARNINGS may be empty.
      */
      thd->stmt_da->set_error_status(thd,
                                     ER_OUT_OF_RESOURCES,
                                     ER(ER_OUT_OF_RESOURCES),
                                     NULL);
    }
  }
}
