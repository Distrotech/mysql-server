/* Copyright (c) 2010, Oracle and/or its affiliates.

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

#ifndef RPL_INFO_FACTORY_H
#define RPL_INFO_FACTORY_H

#include "rpl_mi.h"
#include "rpl_rli.h"

class Rpl_info_factory
{
  public:

  bool static create(uint mi_option, Master_info **mi,
                     uint rli_option, Relay_log_info **rli);
  bool static create_mi(uint rli_option, Master_info **rli);
  bool static create_rli(uint rli_option, bool is_slave_recovery,
                         Relay_log_info **rli);
};
#endif
