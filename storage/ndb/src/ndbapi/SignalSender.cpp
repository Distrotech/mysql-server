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

#include "SignalSender.hpp"
#include <NdbSleep.h>
#include <SignalLoggerManager.hpp>
#include <signaldata/NFCompleteRep.hpp>
#include <signaldata/NodeFailRep.hpp>
#include <signaldata/TestOrd.hpp>


SimpleSignal::SimpleSignal(bool dealloc){
  memset(this, 0, sizeof(* this));
  deallocSections = dealloc;
}

SimpleSignal::~SimpleSignal(){
  if(!deallocSections)
    return;
  if(ptr[0].p != 0) delete []ptr[0].p;
  if(ptr[1].p != 0) delete []ptr[1].p;
  if(ptr[2].p != 0) delete []ptr[2].p;
}

void 
SimpleSignal::set(class SignalSender& ss,
		  Uint8  trace, Uint16 recBlock, Uint16 gsn, Uint32 len){
  
  header.theTrace                = trace;
  header.theReceiversBlockNumber = recBlock;
  header.theVerId_signalNumber   = gsn;
  header.theLength               = len;
  header.theSendersBlockRef      = refToBlock(ss.getOwnRef());
}

void
SimpleSignal::print(FILE * out) const {
  fprintf(out, "---- Signal ----------------\n");
  SignalLoggerManager::printSignalHeader(out, header, 0, 0, false);
  SignalLoggerManager::printSignalData(out, header, theData);
  for(Uint32 i = 0; i<header.m_noOfSections; i++){
    Uint32 len = ptr[i].sz;
    fprintf(out, " --- Section %d size=%d ---\n", i, len);
    Uint32 * signalData = ptr[i].p;
    while(len >= 7){
      fprintf(out, 
              " H\'%.8x H\'%.8x H\'%.8x H\'%.8x H\'%.8x H\'%.8x H\'%.8x\n",
              signalData[0], signalData[1], signalData[2], signalData[3], 
              signalData[4], signalData[5], signalData[6]);
      len -= 7;
      signalData += 7;
    }
    if(len > 0){
      fprintf(out, " H\'%.8x", signalData[0]);
      for(Uint32 i = 1; i<len; i++)
        fprintf(out, " H\'%.8x", signalData[i]);
      fprintf(out, "\n");
    }
  }
}

SignalSender::SignalSender(TransporterFacade *facade, int blockNo)
  : m_lock(0)
{
  m_cond = NdbCondition_Create();
  theFacade = facade;
  lock();
  m_blockNo = theFacade->open(this, execSignal, execNodeStatus, blockNo);
  unlock();
  assert(m_blockNo > 0);
}

SignalSender::SignalSender(Ndb_cluster_connection* connection)
{
  m_cond = NdbCondition_Create();
  theFacade = connection->m_impl.m_transporter_facade;
  lock();
  m_blockNo = theFacade->open(this, execSignal, execNodeStatus, -1);
  unlock();
  assert(m_blockNo > 0);
}

SignalSender::~SignalSender(){
  int i;
  if (m_lock)
    unlock();
  theFacade->close(m_blockNo,0);
  // free these _after_ closing theFacade to ensure that
  // we delete all signals
  for (i= m_jobBuffer.size()-1; i>= 0; i--)
    delete m_jobBuffer[i];
  for (i= m_usedBuffer.size()-1; i>= 0; i--)
    delete m_usedBuffer[i];
  NdbCondition_Destroy(m_cond);
}

int SignalSender::lock()
{
  if (NdbMutex_Lock(theFacade->theMutexPtr))
    return -1;
  m_lock= 1;
  return 0;
}

int SignalSender::unlock()
{
  if (NdbMutex_Unlock(theFacade->theMutexPtr))
    return -1;
  m_lock= 0;
  return 0;
}

Uint32
SignalSender::getOwnRef() const {
  return numberToRef(m_blockNo, theFacade->ownId());
}

const ClusterMgr::Node & 
SignalSender::getNodeInfo(Uint16 nodeId) const {
  return theFacade->theClusterMgr->getNodeInfo(nodeId);
}

Uint32
SignalSender::getNoOfConnectedNodes() const {
  return theFacade->theClusterMgr->getNoOfConnectedNodes();
}


NodeBitmask
SignalSender::broadcastSignal(NodeBitmask mask,
                              SimpleSignal& sig,
                              Uint16 recBlock, Uint16 gsn,
                              Uint32 len)
{
  sig.set(*this, TestOrd::TraceAPI, recBlock, gsn, len);

  NodeBitmask result;
  for(Uint32 i = 0; i < MAX_NODES; i++)
  {
    if(mask.get(i) && sendSignal(i, &sig) == SEND_OK)
      result.set(i);
  }
  return result;
}


SendStatus
SignalSender::sendSignal(Uint16 nodeId,
                         SimpleSignal& sig,
                         Uint16 recBlock, Uint16 gsn,
                         Uint32 len)
{
  sig.set(*this, TestOrd::TraceAPI, recBlock, gsn, len);
  return sendSignal(nodeId, &sig);
}


int
SignalSender::sendFragmentedSignal(Uint16 nodeId,
                                   SimpleSignal& sig,
                                   Uint16 recBlock, Uint16 gsn,
                                   Uint32 len)
{
  sig.set(*this, TestOrd::TraceAPI, recBlock, gsn, len);
  if (nodeId == theFacade->ownId())
  {
    // No need to fragment when sending to own node
    return sendSignal(nodeId, &sig);
  }

  return theFacade->sendFragmentedSignal((NdbApiSignal*)&sig.header,
                                         nodeId,
                                         &sig.ptr[0],
                                         sig.header.m_noOfSections);
}


template<class T>
SimpleSignal *
SignalSender::waitFor(Uint32 timeOutMillis, T & t)
{
  SimpleSignal * s = t.check(m_jobBuffer);
  if(s != 0){
    if (m_usedBuffer.push_back(s))
    {
      return 0;
    }
    return s;
  }

  /* Remove old signals from usedBuffer */
  for (unsigned i= 0; i < m_usedBuffer.size(); i++)
    delete m_usedBuffer[i];
  m_usedBuffer.clear();

  NDB_TICKS now = NdbTick_CurrentMillisecond();
  NDB_TICKS stop = now + timeOutMillis;
  Uint32 wait = (timeOutMillis == 0 ? 10 : timeOutMillis);
  do {
    NdbCondition_WaitTimeout(m_cond,
			     theFacade->theMutexPtr, 
			     wait);
    
    
    SimpleSignal * s = t.check(m_jobBuffer);
    if(s != 0){
      if (m_usedBuffer.push_back(s))
      {
        return 0;
      }
      return s;
    }
    
    now = NdbTick_CurrentMillisecond();
    wait = (Uint32)(timeOutMillis == 0 ? 10 : stop - now);
  } while(stop > now || timeOutMillis == 0);
  
  return 0;
} 

class WaitForAny {
public:
  WaitForAny() {}
  SimpleSignal * check(Vector<SimpleSignal*> & m_jobBuffer){
    if(m_jobBuffer.size() > 0){
      SimpleSignal * s = m_jobBuffer[0];
      m_jobBuffer.erase(0);
      return s;
    }
    return 0;
  }
};
  
SimpleSignal *
SignalSender::waitFor(Uint32 timeOutMillis){
  
  WaitForAny w;
  return waitFor(timeOutMillis, w);
}

#include <NdbApiSignal.hpp>

void
SignalSender::execSignal(void* signalSender, 
			 const NdbApiSignal* signal,
			 const struct LinearSectionPtr ptr[3]){
  SimpleSignal * s = new SimpleSignal(true);
  s->header = * signal;
  memcpy(&s->theData[0], signal->getDataPtr(), 4 * s->header.theLength);
  for(Uint32 i = 0; i<s->header.m_noOfSections; i++){
    s->ptr[i].p = new Uint32[ptr[i].sz];
    s->ptr[i].sz = ptr[i].sz;
    memcpy(s->ptr[i].p, ptr[i].p, 4 * ptr[i].sz);
  }
  SignalSender * ss = (SignalSender*)signalSender;
  ss->m_jobBuffer.push_back(s);
  NdbCondition_Signal(ss->m_cond);
}
  
void 
SignalSender::execNodeStatus(void* signalSender, 
			     Uint32 nodeId, 
			     bool alive, 
			     bool nfCompleted){
  if (alive) {
    // node connected
    return;
  }

  SimpleSignal * s = new SimpleSignal(true);
  SignalSender * ss = (SignalSender*)signalSender;

  // node disconnected
  if(nfCompleted)
  {
    // node shutdown complete
    s->header.theVerId_signalNumber = GSN_NF_COMPLETEREP;
    NFCompleteRep *rep = (NFCompleteRep *)s->getDataPtrSend();
    rep->blockNo = 0;
    rep->nodeId = 0;
    rep->failedNodeId = nodeId;
    rep->unused = 0;
    rep->from = 0;
  }
  else
  {
    // node failure
    s->header.theVerId_signalNumber = GSN_NODE_FAILREP;
    NodeFailRep *rep = (NodeFailRep *)s->getDataPtrSend();
    rep->failNo = 0;
    rep->masterNodeId = 0;
    rep->noOfNodes = 1;
    NdbNodeBitmask::clear(rep->theNodes);

    // Mark ndb nodes as failed in bitmask
    const ClusterMgr::Node node= ss->getNodeInfo(nodeId);
    if (node.m_info.getType() ==  NodeInfo::DB)
      NdbNodeBitmask::set(rep->theNodes, nodeId);
  }

  ss->m_jobBuffer.push_back(s);
  NdbCondition_Signal(ss->m_cond);
}


template<class T>
NodeId
SignalSender::find_node(const NodeBitmask& mask, T & t)
{
  unsigned n= 0;
  do {
     n= mask.find(n+1);

     if (n == NodeBitmask::NotFound)
       return 0;

    assert(n < MAX_NODES);

  } while (!t.found_ok(*this, getNodeInfo(n)));

  return n;
}


class FindConfirmedNode {
public:
  bool found_ok(const SignalSender& ss, const ClusterMgr::Node & node){
    return node.is_confirmed();
  }
};


NodeId
SignalSender::find_confirmed_node(const NodeBitmask& mask)
{
  FindConfirmedNode f;
  return find_node(mask, f);
}


class FindConnectedNode {
public:
  bool found_ok(const SignalSender& ss, const ClusterMgr::Node & node){
    return node.is_connected();
  }
};


NodeId
SignalSender::find_connected_node(const NodeBitmask& mask)
{
  FindConnectedNode f;
  return find_node(mask, f);
}


class FindAliveNode {
public:
  bool found_ok(const SignalSender& ss, const ClusterMgr::Node & node){
    return node.m_alive;
  }
};


NodeId
SignalSender::find_alive_node(const NodeBitmask& mask)
{
  FindAliveNode f;
  return find_node(mask, f);
}


#if __SUNPRO_CC != 0x560
template SimpleSignal* SignalSender::waitFor<WaitForAny>(unsigned, WaitForAny&);
template NodeId SignalSender::find_node<FindConfirmedNode>(const NodeBitmask&,
                                                           FindConfirmedNode&);
template NodeId SignalSender::find_node<FindAliveNode>(const NodeBitmask&,
                                                       FindAliveNode&);
template NodeId SignalSender::find_node<FindConnectedNode>(const NodeBitmask&,
                                                           FindConnectedNode&);
#endif
template class Vector<SimpleSignal*>;
  
