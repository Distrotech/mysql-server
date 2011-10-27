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

#ifndef NDB_NDBAPI_UTIL_H
#define NDB_NDBAPI_UTIL_H

#include <my_global.h>

#include <ndbapi/NdbRecAttr.hpp>
#include <ndbapi/NdbBlob.hpp>
#include <ndbapi/NdbDictionary.hpp>

union NdbValue
{
  const NdbRecAttr *rec;
  NdbBlob *blob;
  void *ptr;
};

int get_ndb_blobs_value(struct TABLE* table, NdbValue* value_array,
                        uchar*& buffer, uint& buffer_size,
                        my_ptrdiff_t ptrdiff);

char *ndb_pack_varchar(const NdbDictionary::Column *col,
                       char *buf, const char *str, int sz);

#endif
