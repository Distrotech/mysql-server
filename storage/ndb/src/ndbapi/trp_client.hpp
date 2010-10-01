/*
   Copyright (C) 2003 MySQL AB
    All rights reserved. Use is subject to license terms.

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

#ifndef trp_client_hpp
#define trp_client_hpp

#include <ndb_global.h>

class NdbApiSignal;
struct LinearSectionPtr;

class trp_client
{
public:
  virtual ~trp_client() {}

  virtual void trp_deliver_signal(const NdbApiSignal *,
                                  const LinearSectionPtr ptr[3]) = 0;
  virtual void trp_node_status(Uint32, bool nodeAlive, bool nfComplete) = 0;
};

#endif
