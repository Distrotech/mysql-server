/* Copyright (c) 2010, 2013, Oracle and/or its affiliates. All rights reserved.

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

/*
  Minimal code to be able to link a unit test.
*/

#include "my_global.h"
#include "m_ctype.h"

volatile bool ready_to_exit= false;

uint lower_case_table_names= 0;
CHARSET_INFO *files_charset_info= NULL;

extern "C" void compute_md5_hash(char *, const char *, int)
{
}

