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

#ifndef RPL_INFO_TABLE_H
#define RPL_INFO_TABLE_H

#include "rpl_info_handler.h"
#include "rpl_info_table_access.h"

class Rpl_info_table : public Rpl_info_handler
{
public:
  Rpl_info_table(uint nparam, uint param_field_id, const char* param_schema,
                 const char *param_table);
  virtual ~Rpl_info_table();
private:
  /*
    This property identifies the name of the schema where a
    replication table is created.
  */
  char str_schema[FN_REFLEN];
  /*
    This property identifies the name of a replication
    table.
  */
  char str_table[FN_REFLEN];
  /*
    This property indentifies the id/position of the field that is
    used as primary key.
  */
  uint field_idx;
  /*
    This is a flag that identifies when a table should be filled
    up with default values.
  */
  bool use_default;
  /*
    This property represents a description of the repository.
    Speciffically, "schema"."table".
  */
  char description[2 * FN_REFLEN];

  /*
    This is a pointer to a class that facilitates manipulation
    of replication tables.
  */
  Rpl_info_table_access *access;

  int do_init_info();
  int do_check_info();
  void do_end_info();
  int do_flush_info(const bool force);
  int do_reset_info();

  int do_prepare_info_for_read();
  int do_prepare_info_for_write();
  bool do_set_info(const int pos, const char *value);
  bool do_set_info(const int pos, const int value);
  bool do_set_info(const int pos, const ulong value);
  bool do_set_info(const int pos, const float value);
  bool do_set_info(const int pos, const Server_ids *value);
  bool do_get_info(const int pos, char *value, const size_t size,
                   const char *default_value);
  bool do_get_info(const int pos, int *value,
                   const int default_value);
  bool do_get_info(const int pos, ulong *value,
                   const ulong default_value);
  bool do_get_info(const int pos, float *value,
                   const float default_value);
  bool do_get_info(const int pos, Server_ids *value,
                   const Server_ids *default_value);
  char* do_get_description_info();

  Rpl_info_table& operator=(const Rpl_info_table& info);
  Rpl_info_table(const Rpl_info_table& info);
};

#endif /* RPL_INFO_TABLE_H */
