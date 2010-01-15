/* Copyright (C) 2007-2008 MySQL AB, 2009 Sun Microsystems Inc.
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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include "Dbinfo.hpp"

#include <signaldata/NodeFailRep.hpp>
#include <signaldata/ReadNodesConf.hpp>

#include <signaldata/DumpStateOrd.hpp>
#include <signaldata/DbinfoScan.hpp>
#include <signaldata/TransIdAI.hpp>

Uint32 dbinfo_blocks[] = { DBACC, DBTUP, BACKUP, DBTC, SUMA, DBUTIL,
                           TRIX, DBTUX, DBDICT, CMVMI, DBLQH, LGMAN, 0};

Dbinfo::Dbinfo(Block_context& ctx) :
  SimulatedBlock(DBINFO, ctx),
  c_nodes(c_nodePool)
{
  BLOCK_CONSTRUCTOR(Dbinfo);

  STATIC_ASSERT(sizeof(DbinfoScanCursor) == sizeof(Ndbinfo::ScanCursor));

  c_nodePool.setSize(MAX_NDB_NODES);

  /* Add Received Signals */
  addRecSignal(GSN_STTOR, &Dbinfo::execSTTOR);
  addRecSignal(GSN_DUMP_STATE_ORD, &Dbinfo::execDUMP_STATE_ORD);
  addRecSignal(GSN_READ_CONFIG_REQ, &Dbinfo::execREAD_CONFIG_REQ, true);

  addRecSignal(GSN_DBINFO_SCANREQ, &Dbinfo::execDBINFO_SCANREQ);
  addRecSignal(GSN_DBINFO_SCANCONF, &Dbinfo::execDBINFO_SCANCONF);

  addRecSignal(GSN_READ_NODESCONF, &Dbinfo::execREAD_NODESCONF);
  addRecSignal(GSN_NODE_FAILREP, &Dbinfo::execNODE_FAILREP);
  addRecSignal(GSN_INCL_NODEREQ, &Dbinfo::execINCL_NODEREQ);

}

Dbinfo::~Dbinfo()
{
  /* Nothing */
}

BLOCK_FUNCTIONS(Dbinfo)

void Dbinfo::execSTTOR(Signal *signal)
{
  jamEntry();

  const Uint32 startphase  = signal->theData[1];

  if (startphase == 3) 
  {
    jam();
    signal->theData[0] = reference();
    sendSignal(NDBCNTR_REF, GSN_READ_NODESREQ, signal, 1, JBB);
    return;
  }//if

  sendSTTORRY(signal);
  return;
}

void Dbinfo::execREAD_CONFIG_REQ(Signal *signal)
{
  jamEntry();
  const ReadConfigReq * req = (ReadConfigReq*)signal->getDataPtr();
  Uint32 ref = req->senderRef;
  Uint32 senderData = req->senderData;

  /* In the future, do something sensible here. */

  ReadConfigConf * conf = (ReadConfigConf*)signal->getDataPtrSend();
  conf->senderRef = reference();
  conf->senderData = senderData;
  sendSignal(ref, GSN_READ_CONFIG_CONF, signal,
	     ReadConfigConf::SignalLength, JBB);
}

void Dbinfo::sendSTTORRY(Signal* signal)
{
  signal->theData[0] = 0;
  signal->theData[3] = 1;
  signal->theData[4] = 3;
  signal->theData[5] = 255; // No more start phases from missra
  sendSignal(NDBCNTR_REF, GSN_STTORRY, signal, 6, JBB);
}

void Dbinfo::execDUMP_STATE_ORD(Signal* signal)
{
  jamEntry();

  switch(signal->theData[0])
  {
  case DumpStateOrd::DbinfoListTables:
    jam();
    ndbout_c("--- BEGIN NDB$INFO.TABLES ---");
    for(int i = 0; i < Ndbinfo::getNumTables(); i++)
    {
      const Ndbinfo::Table& tab = Ndbinfo::getTable(i);
      ndbout_c("%d,%s", i, tab.m.name);
    }
    ndbout_c("--- END NDB$INFO.TABLES ---");
    break;

  case DumpStateOrd::DbinfoListColumns:
    jam();
    ndbout_c("--- BEGIN NDB$INFO.COLUMNS ---");
    for(int i = 0; i < Ndbinfo::getNumTables(); i++)
    {
      const Ndbinfo::Table& tab = Ndbinfo::getTable(i);

      for(int j = 0; j < tab.m.ncols; j++)
        ndbout_c("%d,%d,%s,%d", i, j,
                 tab.col[j].name, tab.col[j].coltype);
    }
    ndbout_c("--- END NDB$INFO.COLUMNS ---");
    break;

  };
}

Uint32 Dbinfo::find_next_node(Uint32 node) const
{
  node++;
  while(!c_aliveNodes.get(node) &&
        node < MAX_NDB_NODES)
     node++;

  if (node == MAX_NDB_NODES)
    return 0;
  return node;
}

Uint32 Dbinfo::find_next_block(Uint32 block) const
{
  int i = 0;
  // Find current blocks position
  while (dbinfo_blocks[i] != block &&
         dbinfo_blocks[i] != 0)
    i++;

  // Make sure current block was found
  ndbrequire(dbinfo_blocks[i]);

  // Return the next block(which might be 0)
  return dbinfo_blocks[++i];
}

bool Dbinfo::find_next(Ndbinfo::ScanCursor* cursor) const
{
  Uint32 node = refToNode(cursor->currRef);
  Uint32 block = refToBlock(cursor->currRef);
  const Uint32 instance = refToInstance(cursor->currRef);
  ndbrequire(instance == 0);

  if (block)
  {
    jam();
    if (block == DBINFO)
    {
      jam();

      // Starting scan on this node
      ndbrequire(node == getOwnNodeId());

      // Start on first block
      cursor->currRef = numberToRef(dbinfo_blocks[0], node);
      return true;
    }

    // Find next block
    block = find_next_block(block);
    if (block)
    {
      jam();
      cursor->currRef = numberToRef(block, node);
      return true;
    }
  }

  node = find_next_node(node);
  if (node)
  {
    jam();
    block = DBINFO;
    cursor->currRef = numberToRef(block, node);
    return true;
  }

  // No more nodes -> done
  cursor->currRef = 0;
  return false;
}

void Dbinfo::execDBINFO_SCANREQ(Signal *signal)
{
  jamEntry();
  DbinfoScanReq* req_ptr = (DbinfoScanReq*)signal->getDataPtrSend();
  const Uint32 senderRef = signal->header.theSendersBlockRef;

  // Copy signal on stack
  DbinfoScanReq req = *req_ptr;

  const Uint32 resultData = req.resultData;
  const Uint32 transId0 = req.transId[0];
  const Uint32 transId1 = req.transId[1];
  const Uint32 resultRef = req.resultRef;

  // Validate tableId
  const Uint32 tableId = req.tableId;
  if (tableId >= (Uint32)Ndbinfo::getNumTables())
  {
    jam();
    DbinfoScanRef *ref= (DbinfoScanRef*)signal->getDataPtrSend();
    ref->resultData = resultData;
    ref->transId[0] = transId0;
    ref->transId[1] = transId1;
    ref->resultRef = resultRef;
    ref->errorCode= DbinfoScanRef::NoTable;
    sendSignal(senderRef, GSN_DBINFO_SCANREF, signal,
               DbinfoScanRef::SignalLength, JBB);
    return;
  }

  // TODO Check all scan parameters

  Ndbinfo::ScanCursor* cursor =
    (Ndbinfo::ScanCursor*)DbinfoScan::getCursorPtr(&req);
  Uint32 signal_length = signal->getLength();
  if (signal_length == DbinfoScanReq::SignalLength)
  {
    // Initialize cursor
    jam();
    cursor->senderRef = senderRef;
    cursor->saveSenderRef = 0;
    cursor->currRef = 0;
    cursor->saveCurrRef = 0;
    // Reset all data holders
    memset(cursor->data, 0, sizeof(cursor->data));
    cursor->flags = 0;
    cursor->totalRows = 0;
    cursor->totalBytes = 0;
    req.cursor_sz = Ndbinfo::ScanCursor::Length;
    signal_length += req.cursor_sz;
  }
  ndbrequire(signal_length ==
             DbinfoScanReq::SignalLength + Ndbinfo::ScanCursor::Length);
  ndbrequire(req.cursor_sz == Ndbinfo::ScanCursor::Length);

  switch(tableId)
  {
  case Ndbinfo::TABLES_TABLEID:
  {
    jam();

    Ndbinfo::Ratelimit rl;
    Uint32 tableId = cursor->data[0];

    while(tableId < (Uint32)Ndbinfo::getNumTables())
    {
      jam();
      const Ndbinfo::Table& tab = Ndbinfo::getTable(tableId);
      Ndbinfo::Row row(signal, req);
      row.write_uint32(tableId);
      row.write_string(tab.m.name);
      row.write_string(tab.m.comment);
      ndbinfo_send_row(signal, req, row, rl);

      tableId++;

      if (rl.need_break(req))
      {
        jam();
        ndbinfo_send_scan_break(signal, req, rl, tableId);
        return;
      }
    }

    // All tables sent
    req.cursor_sz = 0; // Close cursor
    ndbinfo_send_scan_conf(signal, req, rl);
    return;

    break;
  }

  case Ndbinfo::COLUMNS_TABLEID:
  {
    jam();

    Ndbinfo::Ratelimit rl;
    Uint32 tableId = cursor->data[0];
    Uint32 columnId = cursor->data[1];

    while(tableId < (Uint32)Ndbinfo::getNumTables())
    {
      jam();
      const Ndbinfo::Table& tab = Ndbinfo::getTable(tableId);
      while(columnId < (Uint32)tab.m.ncols)
      {
        jam();
        Ndbinfo::Row row(signal, req);
        row.write_uint32(tableId);
        row.write_uint32(columnId);
        row.write_string(tab.col[columnId].name);
        row.write_uint32(tab.col[columnId].coltype);
        row.write_string(tab.col[columnId].comment);
        ndbinfo_send_row(signal, req, row, rl);

        assert(columnId < 256);
        columnId++;

        if(rl.need_break(req))
        {
          jam();
          ndbinfo_send_scan_break(signal, req, rl, tableId, columnId);
          return;
        }
      }
      columnId = 0;
      tableId++;
    }

    // All tables and columns sent
    req.cursor_sz = 0; // Close cursor
    ndbinfo_send_scan_conf(signal, req, rl);

    break;
  }

  default:
  {
    jam();

    ndbassert(tableId > 1);

    //printSignalHeader(stdout, signal->header, 99, 98, true);
    //printDBINFO_SCAN(stdout, signal->theData, signal->getLength(), 0);

    if (Ndbinfo::ScanCursor::getHasMoreData(cursor->flags) ||
        find_next(cursor))
    {
      jam();
      ndbrequire(cursor->currRef);

      // CONF or REF should be sent back here
      cursor->senderRef = reference();

      // Send SCANREQ
      MEMCOPY_NO_WORDS(req_ptr,
                       &req, signal_length);
      sendSignal(cursor->currRef,
                 GSN_DBINFO_SCANREQ,
                 signal, signal_length, JBB);
    }
    else
    {
      // Scan is done, send SCANCONF back to caller
      jam();
      DbinfoScanConf *apiconf= (DbinfoScanConf*)signal->getDataPtrSend();
      MEMCOPY_NO_WORDS(apiconf, &req, DbinfoScanConf::SignalLength);
      // Set cursor_sz back to 0 to indicate end of scan
      apiconf->cursor_sz = 0;
      sendSignal(resultRef, GSN_DBINFO_SCANCONF, signal,
                 DbinfoScanConf::SignalLength, JBB);
    }
    break;
  }
  }
}

void Dbinfo::execDBINFO_SCANCONF(Signal *signal)
{
  const DbinfoScanConf* conf_ptr= (const DbinfoScanConf*)signal->getDataPtr();
  // Copy signal on stack
  const DbinfoScanConf conf= *conf_ptr;

  jamEntry();

  //printDBINFO_SCAN(stdout, signal->theData, signal->getLength(), 0);

  Uint32 signal_length = signal->getLength();
  ndbrequire(signal_length ==
             DbinfoScanReq::SignalLength+Ndbinfo::ScanCursor::Length);
  ndbrequire(conf.cursor_sz == Ndbinfo::ScanCursor::Length);

  // Validate tableId
  const Uint32 tableId= conf.tableId;
  ndbassert(tableId < (Uint32)Ndbinfo::getNumTables());

  const Uint32 resultRef = conf.resultRef;

  // Copy cursor on stack
  ndbrequire(conf.cursor_sz);
  Ndbinfo::ScanCursor* cursor =
    (Ndbinfo::ScanCursor*)DbinfoScan::getCursorPtr(&conf);

  if (Ndbinfo::ScanCursor::getHasMoreData(cursor->flags) || conf.returnedRows)
  {
    // Rate limit break, pass through to API
    jam();
    ndbrequire(cursor->currRef);
    DbinfoScanConf *apiconf = (DbinfoScanConf*) signal->getDataPtrSend();
    MEMCOPY_NO_WORDS(apiconf, &conf, signal_length);
    sendSignal(resultRef, GSN_DBINFO_SCANCONF, signal, signal_length, JBB);
    return;
  }

  if (find_next(cursor))
  {
    jam();
    ndbrequire(cursor->currRef);

    // CONF or REF should be sent back here
    cursor->senderRef = reference();

    // Send SCANREQ
    MEMCOPY_NO_WORDS(signal->getDataPtrSend(),
                     &conf, signal_length);
    sendSignal(cursor->currRef,
               GSN_DBINFO_SCANREQ,
               signal, signal_length, JBB);
    return;
  }

  // Scan is done, send SCANCONF back to caller
  jam();
  DbinfoScanConf *apiconf = (DbinfoScanConf*) signal->getDataPtrSend();
  MEMCOPY_NO_WORDS(apiconf, &conf, DbinfoScanConf::SignalLength);

  // Set cursor_sz back to 0 to indicate end of scan
  apiconf->cursor_sz = 0;
  sendSignal(resultRef, GSN_DBINFO_SCANCONF, signal,
             DbinfoScanConf::SignalLength, JBB);
  return;
}


/**
 * Maintain bitmap of active nodes
 */

void Dbinfo::execREAD_NODESCONF(Signal* signal)
{
  jamEntry();
  ReadNodesConf * conf = (ReadNodesConf *)signal->getDataPtr();

  c_aliveNodes.clear();

  Uint32 count = 0;
  for (Uint32 i = 0; i<MAX_NDB_NODES; i++) {
    jam();
    if(NdbNodeBitmask::get(conf->allNodes, i)){
      jam();
      count++;

      NodePtr node;
      ndbrequire(c_nodes.seize(node));

      node.p->nodeId = i;
      if(NdbNodeBitmask::get(conf->inactiveNodes, i)) {
        jam();
	node.p->alive = 0;
      } else {
        jam();
	node.p->alive = 1;
	c_aliveNodes.set(i);
      }//if
    }//if
  }//for
  c_masterNodeId = conf->masterNodeId;
  ndbrequire(count == conf->noOfNodes);
  sendSTTORRY(signal);
}

void Dbinfo::execINCL_NODEREQ(Signal* signal)
{
  jamEntry();

  const Uint32 senderRef = signal->theData[0];
  const Uint32 inclNode  = signal->theData[1];

  NodePtr node;
  for(c_nodes.first(node); node.i != RNIL; c_nodes.next(node)) {
    jam();
    const Uint32 nodeId = node.p->nodeId;
    if(inclNode == nodeId){
      jam();

      ndbrequire(node.p->alive == 0);
      ndbrequire(!c_aliveNodes.get(nodeId));

      node.p->alive = 1;
      c_aliveNodes.set(nodeId);

      break;
    }//if
  }//for
  signal->theData[0] = inclNode;
  signal->theData[1] = reference();
  sendSignal(senderRef, GSN_INCL_NODECONF, signal, 2, JBB);
}

void Dbinfo::execNODE_FAILREP(Signal* signal)
{
  jamEntry();

  NodeFailRep * rep = (NodeFailRep*)signal->getDataPtr();

  bool doStuff = false;

  NodeId new_master_node_id = rep->masterNodeId;
  Uint32 theFailedNodes[NdbNodeBitmask::Size];
  for (Uint32 i = 0; i < NdbNodeBitmask::Size; i++)
    theFailedNodes[i] = rep->theNodes[i];

  c_masterNodeId = new_master_node_id;

  NodePtr nodePtr;
  for(c_nodes.first(nodePtr); nodePtr.i != RNIL; c_nodes.next(nodePtr)) {
    jam();
    if(NdbNodeBitmask::get(theFailedNodes, nodePtr.p->nodeId)){
      if(nodePtr.p->alive){
	jam();
	ndbrequire(c_aliveNodes.get(nodePtr.p->nodeId));
	doStuff = true;
      } else {
        jam();
	ndbrequire(!c_aliveNodes.get(nodePtr.p->nodeId));
      }//if
      nodePtr.p->alive = 0;
      c_aliveNodes.clear(nodePtr.p->nodeId);

      Uint32 elementsCleaned = simBlockNodeFailure(signal, nodePtr.p->nodeId); // No callback
      ndbassert(elementsCleaned == 0); // DbInfo should have no distributed frag signals
      (void) elementsCleaned; // Remove compiler warning
    }//if
  }//for

  if(!doStuff){
    jam();
    return;
  }//if

#ifdef DEBUG_ABORT
  ndbout_c("****************** Node fail rep ******************");
#endif

  // DO STUFF TO HANDLE NF
}
