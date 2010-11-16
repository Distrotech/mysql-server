/*
   Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.

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

#ifndef trp_node_hpp
#define trp_node_hpp

#include <ndb_global.h>
#include <kernel/NodeInfo.hpp>
#include <kernel/NodeState.hpp>

struct trp_node
{
  trp_node();
  bool defined;
  bool compatible;    // Version is compatible
  bool nfCompleteRep; // NF Complete Rep has arrived
  bool m_alive;       // Node is alive
  Uint32 minDbVersion;

  NodeInfo  m_info;
  NodeState m_state;

  void set_connected(bool connected) {
    assert(defined);
    m_connected = connected;
  }
  bool is_connected(void) const {
    const bool connected = m_connected;
    // Must be defined if connected
    assert(!connected ||
           (connected && defined));
    return connected;
  }

  void set_confirmed(bool confirmed) {
    assert(is_connected()); // Must be connected to change confirmed
    m_api_reg_conf = confirmed;
  }

  bool is_confirmed(void) const {
    const bool confirmed = m_api_reg_conf;
    assert(!confirmed ||
           (confirmed && is_connected()));
    return confirmed;
  }

private:
  bool m_connected;     // Transporter connected
  bool m_api_reg_conf;// API_REGCONF has arrived
};

#endif
