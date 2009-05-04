/* Copyright (C) 2003 MySQL AB

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


#include <pc.hpp>
#define DBSPJ_C
#include "Dbspj.hpp"
#include <ndb_limits.h>

#define DEBUG(x) { ndbout << "SPJ::" << x << endl; }


Dbspj::Dbspj(Block_context& ctx, Uint32 instanceNumber):
  SimulatedBlock(DBSPJ, ctx, instanceNumber),
  m_scan_request_hash(m_request_pool),
  m_lookup_request_hash(m_request_pool)
{
  BLOCK_CONSTRUCTOR(Dbspj);

  addRecSignal(GSN_DUMP_STATE_ORD, &Dbspj::execDUMP_STATE_ORD);
  addRecSignal(GSN_READ_CONFIG_REQ, &Dbspj::execREAD_CONFIG_REQ);
  addRecSignal(GSN_STTOR, &Dbspj::execSTTOR);

  /**
   * Signals from TC
   */
  addRecSignal(GSN_LQHKEYREQ, &Dbspj::execLQHKEYREQ);
  addRecSignal(GSN_SCAN_FRAGREQ, &Dbspj::execSCAN_FRAGREQ);
  addRecSignal(GSN_SCAN_NEXTREQ, &Dbspj::execSCAN_NEXTREQ);

  /**
   * Signals from LQH
   */
  addRecSignal(GSN_LQHKEYREF, &Dbspj::execLQHKEYREF);
  addRecSignal(GSN_LQHKEYCONF, &Dbspj::execLQHKEYCONF);
  addRecSignal(GSN_SCAN_FRAGREF, &Dbspj::execSCAN_FRAGREF);
  addRecSignal(GSN_SCAN_FRAGCONF, &Dbspj::execSCAN_FRAGCONF);
  addRecSignal(GSN_TRANSID_AI, &Dbspj::execTRANSID_AI);

  ndbout << "Instantiating DBSPJ instanceNo=" << instanceNumber << endl;
}//Dbspj::Dbspj()

Dbspj::~Dbspj() 
{
}//Dbspj::~Dbspj()


BLOCK_FUNCTIONS(Dbspj)

