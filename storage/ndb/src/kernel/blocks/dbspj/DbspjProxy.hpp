/* Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef NDB_DBSPJ_PROXY_HPP
#define NDB_DBSPJ_PROXY_HPP

#include <LocalProxy.hpp>

class DbspjProxy : public LocalProxy {
public:
  DbspjProxy(Block_context& ctx);
  virtual ~DbspjProxy();
  BLOCK_DEFINES(DbspjProxy);

  virtual void loadWorkers() { tc_loadWorkers(); }
protected:
  virtual SimulatedBlock* newWorker(Uint32 instanceNo);

};

#endif