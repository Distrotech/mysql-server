/*
   Copyright (C) 2009 Sun Microsystems Inc.
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

#include "NdbInfo.hpp"
#include "SignalSender.hpp"

#include <AttributeHeader.hpp>
#include <signaldata/DbinfoScan.hpp>
#include <signaldata/TransIdAI.hpp>
#include <signaldata/NFCompleteRep.hpp>

#define CAST_PTR(X,Y) static_cast<X*>(static_cast<void*>(Y))
#define CAST_CONSTPTR(X,Y) static_cast<const X*>(static_cast<const void*>(Y))

NdbInfoScanOperation::NdbInfoScanOperation(const NdbInfo& info,
                                           Ndb_cluster_connection* connection,
                                           const NdbInfo::Table* table,
                                           Uint32 max_rows, Uint32 max_bytes) :
  m_info(info),
  m_state(Undefined),
  m_connection(connection),
  m_signal_sender(NULL),
  m_table(table),
  m_node_id(0),
  m_max_rows(max_rows),
  m_max_bytes(max_bytes),
  m_result_data(0x37),
  m_received_rows(0)
{
}

bool
NdbInfoScanOperation::init(Uint32 id)
{
  DBUG_ENTER("NdbInfoScanoperation::init");
  if (m_state != Undefined)
    DBUG_RETURN(false);

  m_signal_sender = new SignalSender(m_connection);
  if (!m_signal_sender)
    DBUG_RETURN(false);

  m_transid0 = id;
  m_transid1 = m_table->getTableId();
  m_result_ref = m_signal_sender->getOwnRef();

  for (unsigned i = 0; i < m_table->columns(); i++)
    m_recAttrs.push_back(NULL);

  m_state = Initial;
  DBUG_RETURN(true);

}

NdbInfoScanOperation::~NdbInfoScanOperation()
{
  close();
  delete m_signal_sender;
}

int
NdbInfoScanOperation::readTuples()
{
  if (m_state != Initial)
    return NdbInfo::ERR_WrongState;

  m_state = Prepared;
  return 0;
}

const NdbInfoRecAttr *
NdbInfoScanOperation::getValue(const char * anAttrName)
{
  if (m_state != Prepared)
    return NULL;

  const NdbInfo::Column* column = m_table->getColumn(anAttrName);
  if (!column)
    return NULL;
  return getValue(column->m_column_id);
}

const NdbInfoRecAttr *
NdbInfoScanOperation::getValue(Uint32 anAttrId)
{
  if (m_state != Prepared)
    return NULL;

  if (anAttrId >= m_recAttrs.size())
    return NULL;

  NdbInfoRecAttr *recAttr = new NdbInfoRecAttr;
  m_recAttrs[anAttrId] = recAttr;
  return recAttr;
}

int NdbInfoScanOperation::execute()
{
  DBUG_ENTER("NdbInfoScanOperation::execute");
  DBUG_PRINT("info", ("name: '%s', id: %d",
             m_table->getName(), m_table->getTableId()));

  if (m_state != Prepared)
    DBUG_RETURN(-1);

  assert(m_cursor.size() == 0);

  m_signal_sender->lock();
  int ret = sendDBINFO_SCANREQ();
  m_signal_sender->unlock();

  m_state = MoreData;

  DBUG_RETURN(ret);
}

int
NdbInfoScanOperation::sendDBINFO_SCANREQ(void)
{
  DBUG_ENTER("NdbInfoScanOperation::sendDBINFO_SCANREQ");

  if (m_node_id == 0)
  {
    m_node_id = m_signal_sender->get_an_alive_node();
    if(m_node_id == 0)
      DBUG_RETURN(NdbInfo::ERR_ClusterFailure);
  }

  SimpleSignal ss;
  DbinfoScanReq * req = CAST_PTR(DbinfoScanReq, ss.getDataPtrSend());

  // API Identifiers
  req->resultData = m_result_data;
  req->transId[0] = m_transid0;
  req->transId[1] = m_transid1;
  req->resultRef = m_result_ref;

  // Scan parameters
  req->tableId = m_table->getTableId();
  req->colBitmap[0] = ~0;
  req->colBitmap[1] = ~0;
  req->requestInfo = 0;
  req->maxRows = m_max_rows;
  req->maxBytes = m_max_bytes;

  // Scan result
  req->returnedRows = 0;

  // Cursor data
  Uint32* cursor_ptr = DbinfoScan::getCursorPtrSend(req);
  for (unsigned i = 0; i < m_cursor.size(); i++)
  {
    *cursor_ptr = m_cursor[i];
    DBUG_PRINT("info", ("cursor[%u]: 0x%x", i, m_cursor[i]));
    cursor_ptr++;
  }
  req->cursor_sz = m_cursor.size();
  m_cursor.clear();

  assert(m_node_id);
  Uint32 len = DbinfoScanReq::SignalLength + req->cursor_sz;
  if (m_signal_sender->sendSignal(m_node_id, ss, DBINFO,
                                  GSN_DBINFO_SCANREQ, len) != SEND_OK)
    DBUG_RETURN(NdbInfo::ERR_ClusterFailure);

  DBUG_RETURN(0);
}

int NdbInfoScanOperation::receive(void)
{
  DBUG_ENTER("NdbInfoScanOperation::receive");
  while (true)
  {
    const SimpleSignal* sig = m_signal_sender->waitFor();
    if (!sig)
      DBUG_RETURN(-1);
    //sig->print();

    int sig_number = sig->readSignalNumber();
    switch (sig_number) {

    case GSN_DBINFO_TRANSID_AI:
    {
      int ret = execDBINFO_TRANSID_AI(sig);
      if (ret == 0)
        continue;
      DBUG_RETURN(ret); // More data
      break;
    }

    case GSN_DBINFO_SCANCONF:
    {
      int ret = execDBINFO_SCANCONF(sig);
      if (ret > 0)
        continue; // Wait for more data
      DBUG_RETURN(ret);
      break;
    }

    case GSN_DBINFO_SCANREF:
    {
      int ret = execDBINFO_SCANREF(sig);
      if (ret == 0)
        continue;
      DBUG_RETURN(ret);
      break;
    }

    case GSN_NODE_FAILREP:
      // Ignore and wait for NF_COMPLETEREP
      break;

    case GSN_NF_COMPLETEREP:
      DBUG_RETURN(-3);
      break;

    case GSN_SUB_GCP_COMPLETE_REP:
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      // ignore
      break;

    default:
      DBUG_PRINT("error", ("Got unexpected signal: %d", sig_number));
      assert(false);
      break;
    }
  }
  assert(false); // Should never come here
  DBUG_RETURN(-1);
}

int
NdbInfoScanOperation::nextResult()
{
  DBUG_ENTER("NdbInfoScanOperation::nextResult");

  switch(m_state)
  {
  case MoreData:
  {
    m_signal_sender->lock();
    int ret = receive();
    m_signal_sender->unlock();
    DBUG_RETURN(ret);
    break;
  }
  case End:
    DBUG_RETURN(0); // EOF
    break;
  default:
    DBUG_RETURN(-1);
    break;
  }
}

void
NdbInfoScanOperation::close()
{
  DBUG_ENTER("NdbInfoScanOperation::close");

  for (unsigned i = 0; i < m_recAttrs.size(); i++)
  {
    if (m_recAttrs[i])
    {
      delete m_recAttrs[i];
      m_recAttrs[i] = NULL;
    }
  }

  DBUG_VOID_RETURN;
}

int
NdbInfoScanOperation::execDBINFO_TRANSID_AI(const SimpleSignal * signal)
{
  DBUG_ENTER("NdbInfoScanOperation::execDBINFO_TRANSID_AI");
  const TransIdAI* transid =
          CAST_CONSTPTR(TransIdAI, signal->getDataPtr());
  if (transid->connectPtr != m_result_data ||
      transid->transId[0] != m_transid0 ||
      transid->transId[1] != m_transid1)
  {
    // Drop signal that belongs to previous scan
    DBUG_RETURN(0);
  }
  m_received_rows++;

  const Uint32* start = signal->ptr[0].p;
  const Uint32* end = start + signal->ptr[0].sz;

  DBUG_PRINT("info", ("start: %p, end: %p", start, end));
  for (unsigned col = 0; col < m_table->columns(); col++)
  {

    // Read attribute header
    const AttributeHeader ah(*start);
    const Uint32 len = ah.getByteSize();
    DBUG_PRINT("info", ("col: %u, len: %u", col, len));

    // Step past attribute header
    start += ah.getHeaderSize();

    NdbInfoRecAttr* attr = m_recAttrs[col];
    if (attr)
    {
      // Update NdbInfoRecAttr pointer and length
      attr->m_data = (const char*)start;
      attr->m_len = len;
    }

    // Step to next attribute header
    start += ah.getDataSize();

    // No reading beyond end of signal size
    assert(start <= end);
  }
  DBUG_RETURN(1);
}

int
NdbInfoScanOperation::execDBINFO_SCANCONF(const SimpleSignal * sig)
{
  DBUG_ENTER("NdbInfoScanOperation::execDBINFO_SCANCONF");
  const DbinfoScanConf* conf =
          CAST_CONSTPTR(DbinfoScanConf, sig->getDataPtr());

  if (conf->resultData != m_result_data ||
      conf->transId[0] != m_transid0 ||
      conf->transId[1] != m_transid1 ||
      conf->resultRef != m_result_ref)
  {
    // Drop signal that belongs to previous scan
    DBUG_RETURN(1); // Continue waiting
  }
  const Uint32 tableId = conf->tableId;
  assert(tableId == m_table->getTableId());

  // Assert all scan settings is unchanged
  assert(conf->colBitmap[0] == (Uint32)~0);
  assert(conf->colBitmap[1] == (Uint32)~0);
  assert(conf->requestInfo == 0);
  assert(conf->maxRows == m_max_rows);
  assert(conf->maxBytes == m_max_bytes);

  // Save cursor data
  assert(m_cursor.size() == 0);
  const Uint32* cursor_ptr = DbinfoScan::getCursorPtr(conf);
  for (unsigned i = 0; i < conf->cursor_sz; i++)
  {
    m_cursor.push_back(*cursor_ptr);
    DBUG_PRINT("info", ("cursor[%u]: 0x%x", i, m_cursor[i]));
    cursor_ptr++;
  }
  assert(conf->cursor_sz == m_cursor.size());

  if (conf->cursor_sz)
  {
    DBUG_PRINT("info", ("Request more data"));
    int err = sendDBINFO_SCANREQ();
    if (err != 0)
    {
      DBUG_PRINT("info", ("Failed to reuqest more data"));
      m_state = Error;
      DBUG_RETURN(err);
    }

    m_state = MoreData;
    DBUG_RETURN(1);
  }

  m_state = End;
  DBUG_RETURN(0); // EOF
}

int
NdbInfoScanOperation::execDBINFO_SCANREF(const SimpleSignal * signal)
{
  DBUG_ENTER("NdbInfoScanOperation::execDBINFO_SCANREF");
  const DbinfoScanRef* ref =
          CAST_CONSTPTR(DbinfoScanRef, signal->getDataPtr());

  if (ref->resultData != m_result_data ||
      ref->transId[0] != m_transid0 ||
      ref->transId[1] != m_transid1 ||
      ref->resultRef != m_result_ref)
  {
    // Drop signal that belongs to previous scan
    DBUG_RETURN(0); // Continue waiting
  }

  m_state = Error;
  DBUG_RETURN(ref->errorCode);
}

template class Vector<NdbInfoRecAttr*>;
