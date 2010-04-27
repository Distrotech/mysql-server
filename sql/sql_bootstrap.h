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


#ifndef SQL_BOOTSTRAP_H
#define SQL_BOOTSTRAP_H

/**
  The maximum size of a bootstrap query.
  Increase this size if parsing a longer query during bootstrap is necessary.
  The longest query in use currently is:
    INSERT INTO time_zone_transition ..., 8059 characters
*/
#define MAX_BOOTSTRAP_QUERY_SIZE 10000
/**
  The maximum size of a bootstrap query, expressed in a single line.
  Do not increase this size, use the multiline syntax with 'GO' instead.
*/
#define MAX_BOOTSTRAP_LINE_SIZE 10000

#define READ_BOOTSTRAP_EOF 1
#define READ_BOOTSTRAP_ERROR 2

typedef void *fgets_input_t;
typedef char * (*fgets_fn_t)(char *, size_t, fgets_input_t);

int read_bootstrap_query(char *query, int *query_length,
                         fgets_input_t input, fgets_fn_t fgets_fn);

#endif


