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

#include <ndb_global.h>

#include "SimulatedBlock.hpp"
#include <NdbOut.hpp>
#include <GlobalData.hpp>
#include <Emulator.hpp>
#include <WatchDog.hpp>
#include <ErrorHandlingMacros.hpp>
#include <TimeQueue.hpp>
#include <TransporterRegistry.hpp>
#include <SignalLoggerManager.hpp>
#include <FastScheduler.hpp>
#include "ndbd_malloc.hpp"
#include <signaldata/EventReport.hpp>
#include <signaldata/ContinueFragmented.hpp>
#include <signaldata/NodeStateSignalData.hpp>
#include <signaldata/FsRef.hpp>
#include <signaldata/SignalDroppedRep.hpp>
#include <signaldata/LocalRouteOrd.hpp>
#include <DebuggerNames.hpp>
#include "LongSignal.hpp"

#include <Properties.hpp>
#include "Configuration.hpp"
#include <AttributeDescriptor.hpp>
#include <NdbSqlUtil.hpp>

#include <EventLogger.hpp>
extern EventLogger * g_eventLogger;

#define ljamEntry() jamEntryLine(30000 + __LINE__)
#define ljam() jamLine(30000 + __LINE__)

//
// Constructor, Destructor
//
SimulatedBlock::SimulatedBlock(BlockNumber blockNumber,
			       struct Block_context & ctx) 
  : theNodeId(globalData.ownId),
    theNumber(blockNumber),
    theReference(numberToRef(blockNumber, globalData.ownId)),
    m_ctx(ctx),
    m_global_page_pool(globalData.m_global_page_pool),
    m_shared_page_pool(globalData.m_shared_page_pool),
    c_fragmentInfoHash(c_fragmentInfoPool),
    c_linearFragmentSendList(c_fragmentSendPool),
    c_segmentedFragmentSendList(c_fragmentSendPool),
    c_mutexMgr(* this),
    c_counterMgr(* this)
{
  NewVarRef = 0;
  
  globalData.setBlock(blockNumber, this);
  c_fragmentIdCounter = 1;
  c_fragSenderRunning = false;
  
#ifdef VM_TRACE_TIME
  clearTimes();
#endif

  for(GlobalSignalNumber i = 0; i<=MAX_GSN; i++)
    theExecArray[i] = 0;

  installSimulatedBlockFunctions();
  UpgradeStartup::installEXEC(this);

  CLEAR_ERROR_INSERT_VALUE;

#ifdef VM_TRACE
  m_global_variables = new Ptr<void> * [1];
  m_global_variables[0] = 0;
  m_global_variables_save = 0;
#endif
}

void
SimulatedBlock::initCommon()
{
  Uint32 count = 10;
  this->getParam("FragmentSendPool", &count);
  c_fragmentSendPool.setSize(count);

  count = 10;
  this->getParam("FragmentInfoPool", &count);
  c_fragmentInfoPool.setSize(count);

  count = 10;
  this->getParam("FragmentInfoHash", &count);
  c_fragmentInfoHash.setSize(count);

  count = 5;
  this->getParam("ActiveMutexes", &count);
  c_mutexMgr.setSize(count);
  
  count = 5;
  this->getParam("ActiveCounters", &count);
  c_counterMgr.setSize(count);
}

SimulatedBlock::~SimulatedBlock()
{
  freeBat();
#ifdef VM_TRACE_TIME
  printTimes(stdout);
#endif

#ifdef VM_TRACE
  delete [] m_global_variables;
#endif
}

void 
SimulatedBlock::installSimulatedBlockFunctions(){
  ExecFunction * a = theExecArray;
  a[GSN_NODE_STATE_REP] = &SimulatedBlock::execNODE_STATE_REP;
  a[GSN_CHANGE_NODE_STATE_REQ] = &SimulatedBlock::execCHANGE_NODE_STATE_REQ;
  a[GSN_NDB_TAMPER] = &SimulatedBlock::execNDB_TAMPER;
  a[GSN_SIGNAL_DROPPED_REP] = &SimulatedBlock::execSIGNAL_DROPPED_REP;
  a[GSN_CONTINUE_FRAGMENTED]= &SimulatedBlock::execCONTINUE_FRAGMENTED;
  a[GSN_UTIL_CREATE_LOCK_REF]   = &SimulatedBlock::execUTIL_CREATE_LOCK_REF;
  a[GSN_UTIL_CREATE_LOCK_CONF]  = &SimulatedBlock::execUTIL_CREATE_LOCK_CONF;
  a[GSN_UTIL_DESTROY_LOCK_REF]  = &SimulatedBlock::execUTIL_DESTORY_LOCK_REF;
  a[GSN_UTIL_DESTROY_LOCK_CONF] = &SimulatedBlock::execUTIL_DESTORY_LOCK_CONF;
  a[GSN_UTIL_LOCK_REF]    = &SimulatedBlock::execUTIL_LOCK_REF;
  a[GSN_UTIL_LOCK_CONF]   = &SimulatedBlock::execUTIL_LOCK_CONF;
  a[GSN_UTIL_UNLOCK_REF]  = &SimulatedBlock::execUTIL_UNLOCK_REF;
  a[GSN_UTIL_UNLOCK_CONF] = &SimulatedBlock::execUTIL_UNLOCK_CONF;
  a[GSN_FSOPENREF]    = &SimulatedBlock::execFSOPENREF;
  a[GSN_FSCLOSEREF]   = &SimulatedBlock::execFSCLOSEREF;
  a[GSN_FSWRITEREF]   = &SimulatedBlock::execFSWRITEREF;
  a[GSN_FSREADREF]    = &SimulatedBlock::execFSREADREF;
  a[GSN_FSREMOVEREF]  = &SimulatedBlock::execFSREMOVEREF;
  a[GSN_FSSYNCREF]    = &SimulatedBlock::execFSSYNCREF;
  a[GSN_FSAPPENDREF]  = &SimulatedBlock::execFSAPPENDREF;
  a[GSN_NODE_START_REP] = &SimulatedBlock::execNODE_START_REP;
  a[GSN_API_START_REP] = &SimulatedBlock::execAPI_START_REP;
  a[GSN_LOCAL_ROUTE_ORD] = &SimulatedBlock::execLOCAL_ROUTE_ORD;
}

void
SimulatedBlock::addRecSignalImpl(GlobalSignalNumber gsn, 
				 ExecFunction f, bool force){
  if(gsn > MAX_GSN || (!force &&  theExecArray[gsn] != 0)){
    char errorMsg[255];
    BaseString::snprintf(errorMsg, 255, 
 	     "GSN %d(%d))", gsn, MAX_GSN); 
    ERROR_SET(fatal, NDBD_EXIT_ILLEGAL_SIGNAL, errorMsg, errorMsg);
  }
  theExecArray[gsn] = f;
}

void
SimulatedBlock::signal_error(Uint32 gsn, Uint32 len, Uint32 recBlockNo, 
			     const char* filename, int lineno) const 
{
  char objRef[255];
  BaseString::snprintf(objRef, 255, "%s:%d", filename, lineno);
  char probData[255];
  BaseString::snprintf(probData, 255, 
	   "Signal (GSN: %d, Length: %d, Rec Block No: %d)", 
	   gsn, len, recBlockNo);
  
  ErrorReporter::handleError(NDBD_EXIT_BLOCK_BNR_ZERO,
			     probData, 
			     objRef);
}


extern class SectionSegmentPool g_sectionSegmentPool;

#define check_sections(signal, cnt, cnt2) do { if (unlikely(cnt)) { handle_invalid_sections_in_send_signal(signal); } else if (unlikely(cnt2 == 0 && (signal->header.m_fragmentInfo != 0 && signal->header.m_fragmentInfo != 3))) { handle_invalid_fragmentInfo(signal); } } while(0)

void
SimulatedBlock::handle_invalid_sections_in_send_signal(Signal* signal) const
{
  //Uint32 cnt = signal->header.m_noOfSections;
#if defined VM_TRACE || defined ERROR_INSERT
  ErrorReporter::handleError(NDBD_EXIT_BLOCK_BNR_ZERO,
			     "Unhandled sections in sendSignal",
			     "");
#else
  infoEvent("Unhandled sections in sendSignal!!");
#endif
}

void
SimulatedBlock::handle_lingering_sections_after_execute(Signal* signal) const
{
  //Uint32 cnt = signal->header.m_noOfSections;
#if defined VM_TRACE || defined ERROR_INSERT
  ErrorReporter::handleError(NDBD_EXIT_BLOCK_BNR_ZERO,
			     "Unhandled sections after execute",
			     "");
#else
  infoEvent("Unhandled sections after execute");
#endif
}

void
SimulatedBlock::handle_lingering_sections_after_execute(SectionHandle* handle) const
{
  //Uint32 cnt = signal->header.m_noOfSections;
#if defined VM_TRACE || defined ERROR_INSERT
  ErrorReporter::handleError(NDBD_EXIT_BLOCK_BNR_ZERO,
			     "Unhandled sections(handle) after execute",
			     "");
#else
  infoEvent("Unhandled sections(handle) after execute");
#endif
}

void
SimulatedBlock::handle_invalid_fragmentInfo(Signal* signal) const
{
#if defined VM_TRACE || defined ERROR_INSERT
  ErrorReporter::handleError(NDBD_EXIT_BLOCK_BNR_ZERO,
			     "Incorrect header->m_fragmentInfo in sendSignal()",
			     "");
#else
  signal->header.m_fragmentInfo = 0;
  infoEvent("Incorrect header->m_fragmentInfo in sendSignal");
#endif
}

void 
SimulatedBlock::sendSignal(BlockReference ref, 
			   GlobalSignalNumber gsn, 
			   Signal* signal, 
			   Uint32 length, 
			   JobBufferLevel jobBuffer) const {

  BlockNumber sendBnr = number();
  BlockReference sendBRef = reference();
  
  Uint32 noOfSections = signal->header.m_noOfSections;
  Uint32 recBlock = refToBlock(ref);
  Uint32 recNode   = refToNode(ref);
  Uint32 ourProcessor         = globalData.ownId;
  
  signal->header.theLength = length;
  signal->header.theVerId_signalNumber = gsn;
  signal->header.theReceiversBlockNumber = recBlock;
  signal->header.m_noOfSections = 0;

  check_sections(signal, noOfSections, 0);
  
  Uint32 tSignalId = signal->header.theSignalId;
  
  if ((length == 0) || length > 25 || (recBlock == 0)) {
    signal_error(gsn, length, recBlock, __FILE__, __LINE__);
    return;
  }//if
#ifdef VM_TRACE
  if(globalData.testOn){
    Uint16 proc = 
      (recNode == 0 ? globalData.ownId : recNode);
    signal->header.theSendersBlockRef = sendBRef;
    globalSignalLoggers.sendSignal(signal->header, 
				   jobBuffer, 
				   &signal->theData[0],
				   proc);
  }
#endif
  
  if(recNode == ourProcessor || recNode == 0) {
    signal->header.theSendersSignalId = tSignalId;
    signal->header.theSendersBlockRef = sendBRef;
    globalScheduler.execute(signal, jobBuffer, recBlock,
			    gsn);
    return;
  } else { 
    // send distributed Signal
    SignalHeader sh;

    Uint32 tTrace = signal->getTrace();
    
    sh.theVerId_signalNumber   = gsn;
    sh.theReceiversBlockNumber = recBlock;
    sh.theSendersBlockRef      = sendBnr;
    sh.theLength               = length;
    sh.theTrace                = tTrace;
    sh.theSignalId             = tSignalId;
    sh.m_noOfSections          = 0;
    sh.m_fragmentInfo          = 0;
    
#ifdef TRACE_DISTRIBUTED
    ndbout_c("send: %s(%d) to (%s, %d)",
	     getSignalName(gsn), gsn, getBlockName(recBlock),
	     recNode);
#endif
    SendStatus ss = globalTransporterRegistry.prepareSend(&sh, jobBuffer, 
							  &signal->theData[0],
							  recNode, 
							  (LinearSectionPtr*)0);
    
    ndbrequire(ss == SEND_OK || ss == SEND_BLOCKED || ss == SEND_DISCONNECTED);
  }
  return;
}

void 
SimulatedBlock::sendSignal(NodeReceiverGroup rg, 
			   GlobalSignalNumber gsn, 
			   Signal* signal, 
			   Uint32 length, 
			   JobBufferLevel jobBuffer) const {

  Uint32 noOfSections = signal->header.m_noOfSections;
  Uint32 tSignalId = signal->header.theSignalId;
  Uint32 tTrace = signal->getTrace();
  
  Uint32 ourProcessor = globalData.ownId;
  Uint32 recBlock = rg.m_block;
  
  signal->header.theLength = length;
  signal->header.theVerId_signalNumber = gsn;
  signal->header.theReceiversBlockNumber = recBlock;
  signal->header.theSendersSignalId = tSignalId;
  signal->header.theSendersBlockRef = reference();
  signal->header.m_noOfSections = 0;

  check_sections(signal, noOfSections, 0);

  if ((length == 0) || (length > 25) || (recBlock == 0)) {
    signal_error(gsn, length, recBlock, __FILE__, __LINE__);
    return;
  }//if

  SignalHeader sh;
  
  sh.theVerId_signalNumber   = gsn;
  sh.theReceiversBlockNumber = recBlock;
  sh.theSendersBlockRef      = number();
  sh.theLength               = length;
  sh.theTrace                = tTrace;
  sh.theSignalId             = tSignalId;
  sh.m_noOfSections          = 0;
  sh.m_fragmentInfo          = 0;

  /**
   * Check own node
   */
  if(rg.m_nodes.get(0) || rg.m_nodes.get(ourProcessor)){
#ifdef VM_TRACE
    if(globalData.testOn){
      globalSignalLoggers.sendSignal(signal->header, 
				     jobBuffer, 
				     &signal->theData[0],
				     ourProcessor);
    }
#endif
    globalScheduler.execute(signal, jobBuffer, recBlock, gsn);
    
    rg.m_nodes.clear((Uint32)0);
    rg.m_nodes.clear(ourProcessor);
  }

  /**
   * Do the big loop
   */
  Uint32 recNode = 0;
  while(!rg.m_nodes.isclear()){
    recNode = rg.m_nodes.find(recNode + 1);
    rg.m_nodes.clear(recNode);
#ifdef VM_TRACE
    if(globalData.testOn){
      globalSignalLoggers.sendSignal(signal->header, 
				     jobBuffer, 
				     &signal->theData[0],
				     recNode);
    }
#endif

#ifdef TRACE_DISTRIBUTED
    ndbout_c("send: %s(%d) to (%s, %d)",
	     getSignalName(gsn), gsn, getBlockName(recBlock),
	     recNode);
#endif

    SendStatus ss = globalTransporterRegistry.prepareSend(&sh, jobBuffer, 
							  &signal->theData[0],
							  recNode,
							  (LinearSectionPtr*)0);
    ndbrequire(ss == SEND_OK || ss == SEND_BLOCKED || ss == SEND_DISCONNECTED);
  }

  return;
}

bool import(Ptr<SectionSegment> & first, const Uint32 * src, Uint32 len);

void 
SimulatedBlock::sendSignal(BlockReference ref, 
			   GlobalSignalNumber gsn, 
			   Signal* signal, 
			   Uint32 length, 
			   JobBufferLevel jobBuffer,
			   LinearSectionPtr ptr[3],
			   Uint32 noOfSections) const {
  
  BlockNumber sendBnr = number();
  BlockReference sendBRef = reference();
  
  Uint32 recBlock = refToBlock(ref);
  Uint32 recNode   = refToNode(ref);
  Uint32 ourProcessor         = globalData.ownId;
  
  check_sections(signal, signal->header.m_noOfSections, noOfSections);
  
  signal->header.theLength = length;
  signal->header.theVerId_signalNumber = gsn;
  signal->header.theReceiversBlockNumber = recBlock;
  signal->header.m_noOfSections = noOfSections;

  Uint32 tSignalId = signal->header.theSignalId;
  Uint32 tFragInfo = signal->header.m_fragmentInfo;
  
  if ((length == 0) || (length + noOfSections > 25) || (recBlock == 0)) {
    signal_error(gsn, length, recBlock, __FILE__, __LINE__);
    return;
  }//if
#ifdef VM_TRACE
  if(globalData.testOn){
    Uint16 proc = 
      (recNode == 0 ? globalData.ownId : recNode);
    signal->header.theSendersBlockRef = sendBRef;
    globalSignalLoggers.sendSignal(signal->header, 
				   jobBuffer, 
				   &signal->theData[0],
				   proc,
                                   ptr, noOfSections);
  }
#endif
  
  if(recNode == ourProcessor || recNode == 0) {
    signal->header.theSendersSignalId = tSignalId;
    signal->header.theSendersBlockRef = sendBRef;

    /**
     * We have to copy the data
     */
    Ptr<SectionSegment> segptr[3];
    for(Uint32 i = 0; i<noOfSections; i++){
      ndbrequire(import(segptr[i], ptr[i].p, ptr[i].sz));
      signal->theData[length+i] = segptr[i].i;
    }
    
    globalScheduler.execute(signal, jobBuffer, recBlock,
			    gsn);
    signal->header.m_noOfSections = 0;
    return;
  } else { 
    // send distributed Signal
    SignalHeader sh;

    Uint32 tTrace = signal->getTrace();
    Uint32 noOfSections = signal->header.m_noOfSections;
    
    sh.theVerId_signalNumber   = gsn;
    sh.theReceiversBlockNumber = recBlock;
    sh.theSendersBlockRef      = sendBnr;
    sh.theLength               = length;
    sh.theTrace                = tTrace;
    sh.theSignalId             = tSignalId;
    sh.m_noOfSections          = noOfSections;
    sh.m_fragmentInfo          = tFragInfo;
    
#ifdef TRACE_DISTRIBUTED
    ndbout_c("send: %s(%d) to (%s, %d)",
	     getSignalName(gsn), gsn, getBlockName(recBlock),
	     recNode);
#endif

    SendStatus ss = globalTransporterRegistry.prepareSend(&sh, jobBuffer, 
							  &signal->theData[0],
							  recNode, 
							  ptr);
    ndbrequire(ss == SEND_OK || ss == SEND_BLOCKED || ss == SEND_DISCONNECTED);
  }

  signal->header.m_noOfSections = 0;
  signal->header.m_fragmentInfo = 0;
  return;
}

void 
SimulatedBlock::sendSignal(NodeReceiverGroup rg, 
			   GlobalSignalNumber gsn, 
			   Signal* signal, 
			   Uint32 length, 
			   JobBufferLevel jobBuffer,
			   LinearSectionPtr ptr[3],
			   Uint32 noOfSections) const {
  
  Uint32 tSignalId = signal->header.theSignalId;
  Uint32 tTrace    = signal->getTrace();
  Uint32 tFragInfo = signal->header.m_fragmentInfo;
  
  Uint32 ourProcessor = globalData.ownId;
  Uint32 recBlock = rg.m_block;
  
  check_sections(signal, signal->header.m_noOfSections, noOfSections);
  
  signal->header.theLength = length;
  signal->header.theVerId_signalNumber = gsn;
  signal->header.theReceiversBlockNumber = recBlock;
  signal->header.theSendersSignalId = tSignalId;
  signal->header.theSendersBlockRef = reference();
  signal->header.m_noOfSections = noOfSections;
  
  if ((length == 0) || (length + noOfSections > 25) || (recBlock == 0)) {
    signal_error(gsn, length, recBlock, __FILE__, __LINE__);
    return;
  }//if

  SignalHeader sh;
  sh.theVerId_signalNumber   = gsn;
  sh.theReceiversBlockNumber = recBlock;
  sh.theSendersBlockRef      = number();
  sh.theLength               = length;
  sh.theTrace                = tTrace;
  sh.theSignalId             = tSignalId;
  sh.m_noOfSections          = noOfSections;
  sh.m_fragmentInfo          = tFragInfo;

  /**
   * Check own node
   */
  if(rg.m_nodes.get(0) || rg.m_nodes.get(ourProcessor)){
#ifdef VM_TRACE
    if(globalData.testOn){
      globalSignalLoggers.sendSignal(signal->header, 
				     jobBuffer, 
				     &signal->theData[0],
				     ourProcessor,
                                     ptr, noOfSections);
    }
#endif
    /**
     * We have to copy the data
     */
    Ptr<SectionSegment> segptr[3];
    for(Uint32 i = 0; i<noOfSections; i++){
      ndbrequire(import(segptr[i], ptr[i].p, ptr[i].sz));
      signal->theData[length+i] = segptr[i].i;
    }
    globalScheduler.execute(signal, jobBuffer, recBlock, gsn);
    
    rg.m_nodes.clear((Uint32)0);
    rg.m_nodes.clear(ourProcessor);
  }
  
  /**
   * Do the big loop
   */
  Uint32 recNode = 0;
  while(!rg.m_nodes.isclear()){
    recNode = rg.m_nodes.find(recNode + 1);
    rg.m_nodes.clear(recNode);
    
#ifdef VM_TRACE
    if(globalData.testOn){
      globalSignalLoggers.sendSignal(signal->header, 
				     jobBuffer, 
				     &signal->theData[0],
				     recNode,
                                     ptr, noOfSections);
    }
#endif
    
#ifdef TRACE_DISTRIBUTED
    ndbout_c("send: %s(%d) to (%s, %d)",
	     getSignalName(gsn), gsn, getBlockName(recBlock),
	     recNode);
#endif

    SendStatus ss = globalTransporterRegistry.prepareSend(&sh, jobBuffer, 
							  &signal->theData[0],
							  recNode,
							  ptr);
    ndbrequire(ss == SEND_OK || ss == SEND_BLOCKED || ss == SEND_DISCONNECTED);
  }
  
  signal->header.m_noOfSections = 0;
  signal->header.m_fragmentInfo = 0;
  
  return;
}

void
SimulatedBlock::sendSignal(BlockReference ref,
			   GlobalSignalNumber gsn,
			   Signal* signal,
			   Uint32 length,
			   JobBufferLevel jobBuffer,
			   SectionHandle* sections) const {

  Uint32 noOfSections = sections->m_cnt;
  BlockNumber sendBnr = number();
  BlockReference sendBRef = reference();

  Uint32 recBlock = refToBlock(ref);
  Uint32 recNode   = refToNode(ref);
  Uint32 ourProcessor         = globalData.ownId;

  check_sections(signal, signal->header.m_noOfSections, noOfSections);

  signal->header.theLength = length;
  signal->header.theVerId_signalNumber = gsn;
  signal->header.theReceiversBlockNumber = recBlock;
  signal->header.m_noOfSections = noOfSections;

  Uint32 tSignalId = signal->header.theSignalId;
  Uint32 tFragInfo = signal->header.m_fragmentInfo;

  if ((length == 0) || (length + noOfSections > 25) || (recBlock == 0)) {
    signal_error(gsn, length, recBlock, __FILE__, __LINE__);
    return;
  }//if
#ifdef VM_TRACE
  if(globalData.testOn){
    Uint16 proc =
      (recNode == 0 ? globalData.ownId : recNode);
    signal->header.theSendersBlockRef = sendBRef;
    globalSignalLoggers.sendSignal(signal->header,
				   jobBuffer,
				   &signal->theData[0],
				   proc,
                                   sections->m_ptr, noOfSections);
  }
#endif

  if(recNode == ourProcessor || recNode == 0) {
    signal->header.theSendersSignalId = tSignalId;
    signal->header.theSendersBlockRef = sendBRef;

    /**
     * We have to copy the data
     */
    Uint32 * dst = signal->theData + length;
    * dst ++ = sections->m_ptr[0].i;
    * dst ++ = sections->m_ptr[1].i;
    * dst ++ = sections->m_ptr[2].i;

    globalScheduler.execute(signal, jobBuffer, recBlock, gsn);
  } else {
    // send distributed Signal
    SignalHeader sh;

    Uint32 tTrace = signal->getTrace();

    sh.theVerId_signalNumber   = gsn;
    sh.theReceiversBlockNumber = recBlock;
    sh.theSendersBlockRef      = sendBnr;
    sh.theLength               = length;
    sh.theTrace                = tTrace;
    sh.theSignalId             = tSignalId;
    sh.m_noOfSections          = noOfSections;
    sh.m_fragmentInfo          = tFragInfo;

#ifdef TRACE_DISTRIBUTED
    ndbout_c("send: %s(%d) to (%s, %d)",
	     getSignalName(gsn), gsn, getBlockName(recBlock),
	     recNode);
#endif

    SendStatus ss = globalTransporterRegistry.prepareSend(&sh, jobBuffer,
							  &signal->theData[0],
							  recNode,
							  g_sectionSegmentPool,
							  sections->m_ptr);
    ndbrequire(ss == SEND_OK || ss == SEND_BLOCKED || ss == SEND_DISCONNECTED);
    ::releaseSections(noOfSections, sections->m_ptr);
  }

  signal->header.m_noOfSections = 0;
  signal->header.m_fragmentInfo = 0;
  sections->m_cnt = 0;
  return;
}

void
SimulatedBlock::sendSignal(NodeReceiverGroup rg,
			   GlobalSignalNumber gsn,
			   Signal* signal,
			   Uint32 length,
			   JobBufferLevel jobBuffer,
			   SectionHandle * sections) const {

  Uint32 noOfSections = sections->m_cnt;
  Uint32 tSignalId = signal->header.theSignalId;
  Uint32 tTrace    = signal->getTrace();
  Uint32 tFragInfo = signal->header.m_fragmentInfo;

  Uint32 ourProcessor = globalData.ownId;
  Uint32 recBlock = rg.m_block;

  check_sections(signal, signal->header.m_noOfSections, noOfSections);

  signal->header.theLength = length;
  signal->header.theVerId_signalNumber = gsn;
  signal->header.theReceiversBlockNumber = recBlock;
  signal->header.theSendersSignalId = tSignalId;
  signal->header.theSendersBlockRef = reference();
  signal->header.m_noOfSections = noOfSections;

  if ((length == 0) || (length + noOfSections > 25) || (recBlock == 0)) {
    signal_error(gsn, length, recBlock, __FILE__, __LINE__);
    return;
  }//if

  SignalHeader sh;
  sh.theVerId_signalNumber   = gsn;
  sh.theReceiversBlockNumber = recBlock;
  sh.theSendersBlockRef      = number();
  sh.theLength               = length;
  sh.theTrace                = tTrace;
  sh.theSignalId             = tSignalId;
  sh.m_noOfSections          = noOfSections;
  sh.m_fragmentInfo          = tFragInfo;

  /**
   * Check own node
   */
  bool release = true;
  if(rg.m_nodes.get(0) || rg.m_nodes.get(ourProcessor))
  {
    release = false;
#ifdef VM_TRACE
    if(globalData.testOn){
      globalSignalLoggers.sendSignal(signal->header,
				     jobBuffer,
				     &signal->theData[0],
				     ourProcessor,
                                     sections->m_ptr, noOfSections);
    }
#endif
    /**
     * We have to copy the data
     */
    Uint32 * dst = signal->theData + length;
    * dst ++ = sections->m_ptr[0].i;
    * dst ++ = sections->m_ptr[1].i;
    * dst ++ = sections->m_ptr[2].i;
    globalScheduler.execute(signal, jobBuffer, recBlock, gsn);

    rg.m_nodes.clear((Uint32)0);
    rg.m_nodes.clear(ourProcessor);
  }

  /**
   * Do the big loop
   */
  Uint32 recNode = 0;
  while(!rg.m_nodes.isclear()){
    recNode = rg.m_nodes.find(recNode + 1);
    rg.m_nodes.clear(recNode);

#ifdef VM_TRACE
    if(globalData.testOn){
      globalSignalLoggers.sendSignal(signal->header,
				     jobBuffer,
				     &signal->theData[0],
				     recNode,
                                     sections->m_ptr, noOfSections);
    }
#endif

#ifdef TRACE_DISTRIBUTED
    ndbout_c("send: %s(%d) to (%s, %d)",
	     getSignalName(gsn), gsn, getBlockName(recBlock),
	     recNode);
#endif

    SendStatus ss = globalTransporterRegistry.prepareSend(&sh, jobBuffer,
							  &signal->theData[0],
							  recNode,
							  g_sectionSegmentPool,
							  sections->m_ptr);
    ndbrequire(ss == SEND_OK || ss == SEND_BLOCKED || ss == SEND_DISCONNECTED);
  }

  if (release)
  {
    ::releaseSections(noOfSections, sections->m_ptr);
  }

  sections->m_cnt = 0;
  signal->header.m_noOfSections = 0;
  signal->header.m_fragmentInfo = 0;

  return;
}

void
SimulatedBlock::sendSignalWithDelay(BlockReference ref, 
				    GlobalSignalNumber gsn,
				    Signal* signal,
				    Uint32 delayInMilliSeconds, 
				    Uint32 length) const {
  
  BlockNumber bnr = refToBlock(ref);

  check_sections(signal, signal->header.m_noOfSections, 0);
  
  signal->header.theLength = length;
  signal->header.theSendersSignalId = signal->header.theSignalId;
  signal->header.theVerId_signalNumber = gsn;
  signal->header.theReceiversBlockNumber = bnr;
  signal->header.theSendersBlockRef = reference();

#ifdef VM_TRACE
  {
    if(globalData.testOn){
      globalSignalLoggers.sendSignalWithDelay(delayInMilliSeconds,
					      signal->header,
					      0,
					      &signal->theData[0], 
					      globalData.ownId);
    }
  }
#endif
  globalTimeQueue.insert(signal, bnr, gsn, delayInMilliSeconds);

  // befor 2nd parameter to globalTimeQueue.insert
  // (Priority)theSendSig[sigIndex].jobBuffer
}

void
SimulatedBlock::sendSignalWithDelay(BlockReference ref,
				    GlobalSignalNumber gsn,
				    Signal* signal,
				    Uint32 delayInMilliSeconds,
				    Uint32 length,
				    SectionHandle * sections) const {

  Uint32 noOfSections = sections->m_cnt;
  BlockNumber bnr = refToBlock(ref);

  //BlockNumber sendBnr = number();
  BlockReference sendBRef = reference();

  if (bnr == 0) {
    bnr_error();
  }//if

  check_sections(signal, signal->header.m_noOfSections, noOfSections);

  signal->header.theLength = length;
  signal->header.theSendersSignalId = signal->header.theSignalId;
  signal->header.theSendersBlockRef = sendBRef;
  signal->header.theVerId_signalNumber = gsn;
  signal->header.theReceiversBlockNumber = bnr;
  signal->header.m_noOfSections = noOfSections;

  Uint32 * dst = signal->theData + length;
  * dst ++ = sections->m_ptr[0].i;
  * dst ++ = sections->m_ptr[1].i;
  * dst ++ = sections->m_ptr[2].i;

#ifdef VM_TRACE
  {
    if(globalData.testOn){
      globalSignalLoggers.sendSignalWithDelay(delayInMilliSeconds,
					      signal->header,
					      0,
					      &signal->theData[0],
					      globalData.ownId);
    }
  }
#endif
  globalTimeQueue.insert(signal, bnr, gsn, delayInMilliSeconds);

  signal->header.m_noOfSections = 0;
  signal->header.m_fragmentInfo = 0;
  sections->m_cnt = 0;
}

void
SimulatedBlock::releaseSections(SectionHandle& handle)
{
  ::releaseSections(handle.m_cnt, handle.m_ptr);
  handle.m_cnt = 0;
}

class SectionSegmentPool& 
SimulatedBlock::getSectionSegmentPool(){
  return g_sectionSegmentPool;
}

NewVARIABLE *
SimulatedBlock::allocateBat(int batSize){
  NewVARIABLE* bat = NewVarRef;
  bat = (NewVARIABLE*)realloc(bat, batSize * sizeof(NewVARIABLE));
  NewVarRef = bat;
  theBATSize = batSize;
  return bat;
}

void
SimulatedBlock::freeBat(){
  if(NewVarRef != 0){
    free(NewVarRef);
    NewVarRef = 0;
  }
}

const NewVARIABLE *
SimulatedBlock::getBat(Uint16 blockNo){
  SimulatedBlock * sb = globalData.getBlock(blockNo);
  if(sb == 0)
    return 0;
  return sb->NewVarRef;
}

Uint16
SimulatedBlock::getBatSize(Uint16 blockNo){
  SimulatedBlock * sb = globalData.getBlock(blockNo);
  if(sb == 0)
    return 0;
  return sb->theBATSize;
}

void* SimulatedBlock::allocRecord(const char * type, size_t s, size_t n, bool clear, Uint32 paramId)
{
  return allocRecordAligned(type, s, n, 0, 0, clear, paramId);
}

void* 
SimulatedBlock::allocRecordAligned(const char * type, size_t s, size_t n, void **unaligned_buffer, Uint32 align, bool clear, Uint32 paramId)
{

  void * p = NULL;
  Uint32 over_alloc = unaligned_buffer ? (align - 1) : 0;
  size_t size = n*s + over_alloc;
  Uint64 real_size = (Uint64)((Uint64)n)*((Uint64)s) + over_alloc;
  refresh_watch_dog(9);
  if (real_size > 0){
#ifdef VM_TRACE_MEM
    ndbout_c("%s::allocRecord(%s, %u, %u) = %llu bytes", 
	     getBlockName(number()), 
	     type,
	     s,
	     n,
	     real_size);
#endif
    if( real_size == (Uint64)size )
      p = ndbd_malloc(size);
    if (p == NULL){
      char buf1[255];
      char buf2[255];
      struct ndb_mgm_param_info param_info;
      size_t size = sizeof(ndb_mgm_param_info);

      if(0 != paramId && 0 == ndb_mgm_get_db_parameter_info(paramId, &param_info, &size)) {
        BaseString::snprintf(buf1, sizeof(buf1), "%s could not allocate memory for parameter %s", 
	         getBlockName(number()), param_info.m_name);
      } else {
        BaseString::snprintf(buf1, sizeof(buf1), "%s could not allocate memory for %s", 
	         getBlockName(number()), type);
      }
      BaseString::snprintf(buf2, sizeof(buf2), "Requested: %ux%u = %llu bytes", 
	       (Uint32)s, (Uint32)n, (Uint64)real_size);
      ERROR_SET(fatal, NDBD_EXIT_MEMALLOC, buf1, buf2);
    }

    if(clear){
      char * ptr = (char*)p;
      const Uint32 chunk = 128 * 1024;
      while(size > chunk){
	refresh_watch_dog(9);
	memset(ptr, 0, chunk);
	ptr += chunk;
	size -= chunk;
      }
      refresh_watch_dog(9);
      memset(ptr, 0, size);
    }
    if (unaligned_buffer)
    {
      *unaligned_buffer = p;
      p = (void *)(((UintPtr)p + over_alloc) & ~(UintPtr)(over_alloc));
#ifdef VM_TRACE
      g_eventLogger->info("'%s' (%u) %llu %llu, alignment correction %u bytes",
                          type, align, (Uint64)p, (Uint64)p+n*s,
                          (Uint32)((UintPtr)p - (UintPtr)*unaligned_buffer));
#endif
    }
  }
  return p;
}

void 
SimulatedBlock::deallocRecord(void ** ptr, 
			      const char * type, size_t s, size_t n){
  (void)type;

  if(* ptr != 0){
      ndbd_free(* ptr, n*s);
    * ptr = 0;
  }
}

void
SimulatedBlock::refresh_watch_dog(Uint32 place)
{
  globalData.incrementWatchDogCounter(place);
}

void
SimulatedBlock::update_watch_dog_timer(Uint32 interval)
{
  extern EmulatorData globalEmulatorData;
  globalEmulatorData.theWatchDog->setCheckInterval(interval);
}

void
SimulatedBlock::progError(int line, int err_code, const char* extra) const {
  jamLine(line);

  const char *aBlockName = getBlockName(number(), "VM Kernel");

  // Pack status of interesting config variables 
  // so that we can print them in error.log
  int magicStatus = 
    (m_ctx.m_config.stopOnError()<<1) + 
    (m_ctx.m_config.getInitialStart()<<2) + 
    (m_ctx.m_config.getDaemonMode()<<3);
  

  /* Add line number to block name */
  char buf[100];
  BaseString::snprintf(&buf[0], 100, "%s (Line: %d) 0x%.8x", 
	   aBlockName, line, magicStatus);

  ErrorReporter::handleError(err_code, extra, buf);

}

void 
SimulatedBlock::infoEvent(const char * msg, ...) const {
  if(msg == 0)
    return;
  
  SignalT<25> signalT;
  signalT.theData[0] = NDB_LE_InfoEvent;
  char * buf = (char *)(signalT.theData+1);
  
  va_list ap;
  va_start(ap, msg);
  BaseString::vsnprintf(buf, 96, msg, ap); // 96 = 100 - 4
  va_end(ap);
  
  int len = strlen(buf) + 1;
  if(len > 96){
    len = 96;
    buf[95] = 0;
  }

  /**
   * Init and put it into the job buffer
   */
  memset(&signalT.header, 0, sizeof(SignalHeader));
  
  const Signal * signal = globalScheduler.getVMSignals();
  Uint32 tTrace = signal->header.theTrace;
  Uint32 tSignalId = signal->header.theSignalId;
  
  signalT.header.theVerId_signalNumber   = GSN_EVENT_REP;
  signalT.header.theReceiversBlockNumber = CMVMI;
  signalT.header.theSendersBlockRef      = reference();
  signalT.header.theTrace                = tTrace;
  signalT.header.theSignalId             = tSignalId;
  signalT.header.theLength               = ((len+3)/4)+1;
  
  globalScheduler.execute(&signalT.header, JBB, signalT.theData,
                          signalT.m_sectionPtrI);
}

void 
SimulatedBlock::warningEvent(const char * msg, ...) const {
  if(msg == 0)
    return;

  SignalT<25> signalT;
  signalT.theData[0] = NDB_LE_WarningEvent;
  char * buf = (char *)(signalT.theData+1);
  
  va_list ap;
  va_start(ap, msg);
  BaseString::vsnprintf(buf, 96, msg, ap); // 96 = 100 - 4
  va_end(ap);
  
  int len = strlen(buf) + 1;
  if(len > 96){
    len = 96;
    buf[95] = 0;
  }

  /**
   * Init and put it into the job buffer
   */
  memset(&signalT.header, 0, sizeof(SignalHeader));
  
  const Signal * signal = globalScheduler.getVMSignals();
  Uint32 tTrace = signal->header.theTrace;
  Uint32 tSignalId = signal->header.theSignalId;
  
  signalT.header.theVerId_signalNumber   = GSN_EVENT_REP;
  signalT.header.theReceiversBlockNumber = CMVMI;
  signalT.header.theSendersBlockRef      = reference();
  signalT.header.theTrace                = tTrace;
  signalT.header.theSignalId             = tSignalId;
  signalT.header.theLength               = ((len+3)/4)+1;

  globalScheduler.execute(&signalT.header, JBB, signalT.theData,
                          signalT.m_sectionPtrI);
}

void
SimulatedBlock::execNODE_STATE_REP(Signal* signal){
  const NodeStateRep * const  rep = (NodeStateRep *)&signal->theData[0];
  
  this->theNodeState = rep->nodeState;
}

void
SimulatedBlock::execCHANGE_NODE_STATE_REQ(Signal* signal){
  const ChangeNodeStateReq * const  req = 
    (ChangeNodeStateReq *)&signal->theData[0];
  
  this->theNodeState = req->nodeState;
  const Uint32 senderData = req->senderData;
  const BlockReference senderRef = req->senderRef;

  /**
   * Pack return signal
   */
  ChangeNodeStateConf * const  conf = 
    (ChangeNodeStateConf *)&signal->theData[0];
  
  conf->senderData = senderData;

  sendSignal(senderRef, GSN_CHANGE_NODE_STATE_CONF, signal, 
	     ChangeNodeStateConf::SignalLength, JBB);
}

void
SimulatedBlock::execNDB_TAMPER(Signal * signal){
  if (signal->getLength() == 1)
  {
    SET_ERROR_INSERT_VALUE(signal->theData[0]);
  }
  else
  {
    SET_ERROR_INSERT_VALUE2(signal->theData[0], signal->theData[1]);
  }
}

void
SimulatedBlock::execSIGNAL_DROPPED_REP(Signal * signal){
  char msg[64];
  const SignalDroppedRep * const rep = (SignalDroppedRep *)&signal->theData[0];
  snprintf(msg, sizeof(msg), "%s GSN: %u (%u,%u)", getBlockName(number()),
	   rep->originalGsn, rep->originalLength,rep->originalSectionCount);
  ErrorReporter::handleError(NDBD_EXIT_OUT_OF_LONG_SIGNAL_MEMORY,
			     msg,
			     __FILE__,
			     NST_ErrorHandler);
}

void
SimulatedBlock::execCONTINUE_FRAGMENTED(Signal * signal){
  ljamEntry();

  ContinueFragmented * sig = (ContinueFragmented*)signal->getDataPtrSend();
  ndbrequire(signal->getSendersBlockRef() == reference()); /* Paranoia */

  switch (sig->type)
  {
  case ContinueFragmented::CONTINUE_SENDING :
  {
    ljam();
    Ptr<FragmentSendInfo> fragPtr;
    
    c_segmentedFragmentSendList.first(fragPtr);  
    for(; !fragPtr.isNull();){
      ljam();
      Ptr<FragmentSendInfo> copyPtr = fragPtr;
      c_segmentedFragmentSendList.next(fragPtr);
      
      sendNextSegmentedFragment(signal, * copyPtr.p);
      if(copyPtr.p->m_status == FragmentSendInfo::SendComplete){
        ljam();
        if(copyPtr.p->m_callback.m_callbackFunction != 0) {
          ljam();
          execute(signal, copyPtr.p->m_callback, 0);
        }//if
        c_segmentedFragmentSendList.release(copyPtr);
      }
    }
    
    c_linearFragmentSendList.first(fragPtr);  
    for(; !fragPtr.isNull();){
      ljam(); 
      Ptr<FragmentSendInfo> copyPtr = fragPtr;
      c_linearFragmentSendList.next(fragPtr);
      
      sendNextLinearFragment(signal, * copyPtr.p);
      if(copyPtr.p->m_status == FragmentSendInfo::SendComplete){
        ljam();
        if(copyPtr.p->m_callback.m_callbackFunction != 0) {
          ljam();
          execute(signal, copyPtr.p->m_callback, 0);
        }//if
        c_linearFragmentSendList.release(copyPtr);
      }
    }
    
    if(c_segmentedFragmentSendList.isEmpty() && 
       c_linearFragmentSendList.isEmpty()){
      ljam();
      c_fragSenderRunning = false;
      return;
    }
    
    sig->type = ContinueFragmented::CONTINUE_SENDING;
    sig->line = __LINE__;
    sendSignal(reference(), GSN_CONTINUE_FRAGMENTED, signal, 2, JBB);
    break;
  }
  case ContinueFragmented::CONTINUE_CLEANUP:
  {
    ljam();
    
    const Uint32 callbackWords = (sizeof(Callback) + 3) >> 2;
    /* Check length of signal */
    ndbassert(signal->getLength() ==
              ContinueFragmented::CONTINUE_CLEANUP_FIXED_WORDS + 
              callbackWords);
    
    Callback cb;
    memcpy(&cb, &sig->cleanup.callbackStart, callbackWords << 2);

    doNodeFailureCleanup(signal,
                         sig->cleanup.failedNodeId,
                         sig->cleanup.resource,
                         sig->cleanup.cursor,
                         sig->cleanup.elementsCleaned,
                         cb);
    break;
  }
  default:
    ndbrequire(false);
  }
}

void
SimulatedBlock::execNODE_START_REP(Signal* signal)
{
}

void
SimulatedBlock::execAPI_START_REP(Signal* signal)
{
}

#ifdef VM_TRACE_TIME
void
SimulatedBlock::clearTimes() {
  for(Uint32 i = 0; i <= MAX_GSN; i++){
    m_timeTrace[i].cnt = 0;
    m_timeTrace[i].sum = 0;
    m_timeTrace[i].sub = 0;
  }
}

void
SimulatedBlock::printTimes(FILE * output){
  fprintf(output, "-- %s --\n", getBlockName(number()));
  Uint64 sum = 0;
  for(Uint32 i = 0; i <= MAX_GSN; i++){
    Uint32 n = m_timeTrace[i].cnt;
    if(n != 0){
      double dn = n;
      
      double avg = m_timeTrace[i].sum;
      double avg2 = avg - m_timeTrace[i].sub;
      
      avg /= dn;
      avg2 /= dn;
      
      fprintf(output, 
	      //name ; cnt ; loc ; acc
	      "%s ; #%d ; %dus ; %dus ; %dms\n",
	      getSignalName(i), n, (Uint32)avg, (Uint32)avg2, 
	      (Uint32)((m_timeTrace[i].sum - m_timeTrace[i].sub + 500)/ 1000));
      
      sum += (m_timeTrace[i].sum - m_timeTrace[i].sub);
    }
  }
  sum = (sum + 500)/ 1000;
  fprintf(output, "-- %s : %d --\n", getBlockName(number()), sum);
  fprintf(output, "\n");
  fflush(output);
}

#endif

void release(SegmentedSectionPtr & ptr);

SimulatedBlock::FragmentInfo::FragmentInfo(Uint32 fragId, Uint32 sender){
  m_fragmentId = fragId;
  m_senderRef = sender;
  m_sectionPtrI[0] = RNIL; 
  m_sectionPtrI[1] = RNIL;
  m_sectionPtrI[2] = RNIL;
}

SimulatedBlock::FragmentSendInfo::FragmentSendInfo()
{
}

bool
SimulatedBlock::assembleFragments(Signal * signal){
  Uint32 sigLen = signal->length() - 1;
  Uint32 fragId = signal->theData[sigLen];
  Uint32 fragInfo = signal->header.m_fragmentInfo;
  Uint32 senderRef = signal->getSendersBlockRef();

  Uint32 *sectionPtr = signal->m_sectionPtrI;
  
  if(fragInfo == 0){
    return true;
  }
  
  const Uint32 secs = signal->header.m_noOfSections;
  const Uint32 * const secNos = &signal->theData[sigLen - secs];
  
  if(fragInfo == 1){
    /**
     * First in train
     */
    Ptr<FragmentInfo> fragPtr;
    if(!c_fragmentInfoHash.seize(fragPtr)){
      ndbrequire(false);
      return false;
    }
    
    new (fragPtr.p)FragmentInfo(fragId, senderRef);
    c_fragmentInfoHash.add(fragPtr);
    
    for(Uint32 i = 0; i<secs; i++){
      Uint32 sectionNo = secNos[i];
      ndbassert(sectionNo < 3);
      fragPtr.p->m_sectionPtrI[sectionNo] = sectionPtr[i];
    }
    
    /**
     * Don't release allocated segments
     */
    signal->header.m_fragmentInfo = 0;
    signal->header.m_noOfSections = 0;
    return false;
  }
  
  FragmentInfo key(fragId, senderRef);
  Ptr<FragmentInfo> fragPtr;
  if(c_fragmentInfoHash.find(fragPtr, key)){
    
    /**
     * FragInfo == 2 or 3
     */
    Uint32 i;
    for(i = 0; i<secs; i++){
      Uint32 sectionNo = secNos[i];
      ndbassert(sectionNo < 3);
      Uint32 sectionPtrI = sectionPtr[i];
      if(fragPtr.p->m_sectionPtrI[sectionNo] != RNIL){
	linkSegments(fragPtr.p->m_sectionPtrI[sectionNo], sectionPtrI);
      } else {
	fragPtr.p->m_sectionPtrI[sectionNo] = sectionPtrI;
      }
    }
    
    /**
     * fragInfo = 2
     */
    if(fragInfo == 2){
      signal->header.m_fragmentInfo = 0;
      signal->header.m_noOfSections = 0;
      return false;
    }
    
    /**
     * fragInfo = 3
     */
    for(i = 0; i<3; i++){
      Uint32 ptrI = fragPtr.p->m_sectionPtrI[i];
      if(ptrI != RNIL){
	signal->m_sectionPtrI[i] = ptrI;
      } else {
	break;
      }
    }

    signal->setLength(sigLen - secs);
    signal->header.m_noOfSections = i;
    signal->header.m_fragmentInfo = 0;

    c_fragmentInfoHash.release(fragPtr);
    return true;
  }
  
  /**
   * Unable to find fragment
   */
  ndbrequire(false);
  return false;
}

/**
 * doCleanupFragInfo
 * Iterate over block's Fragment assembly hash, looking
 * for in-assembly fragments from the failed node
 * Release these
 * Returns after each scanned bucket to avoid consuming
 * too much time.
 *
 * Parameters
 *   failedNodeId    : Node id of failed node
 *   cursor          : Hash bucket to start iteration from
 *   rtUnitsUsed     : Total rt units used
 *   elementsCleaned : Number of elements cleaned
 *
 * Updates
 *   cursor          : Hash bucket to continue iteration from
 *   rtUnitsUsed     : += units used
 *   elementsCleaned : += elements cleaned
 * 
 * Returns
 *   true  if all FragInfo structs cleaned up
 *   false if more to do 
 */
bool
SimulatedBlock::doCleanupFragInfo(Uint32 failedNodeId,
                                  Uint32& cursor,
                                  Uint32& rtUnitsUsed,
                                  Uint32& elementsCleaned)
{
  ljam();
  DLHashTable<FragmentInfo>::Iterator iter;
  
  c_fragmentInfoHash.next(cursor, iter);

  const Uint32 startBucket = iter.bucket;

  while (!iter.isNull() &&
         (iter.bucket == startBucket))
  {
    ljam();

    Ptr<FragmentInfo> curr = iter.curr;
    c_fragmentInfoHash.next(iter);

    FragmentInfo* fragInfo = curr.p;
    
    if (refToNode(fragInfo->m_senderRef) == failedNodeId)
    {
      ljam();
      /* We were assembling a fragmented signal from the
       * failed node, discard the partially assembled
       * sections and free the FragmentInfo hash entry
       */
      for(Uint32 s = 0; s<3; s++)
      {
        if (fragInfo->m_sectionPtrI[s] != RNIL)
        {
          ljam();
          SegmentedSectionPtr ssptr;
          getSection(ssptr, fragInfo->m_sectionPtrI[s]);
          release(ssptr);
        }
      }
      
      /* Release FragmentInfo hash element */
      c_fragmentInfoHash.release(curr);

      elementsCleaned++;
      rtUnitsUsed+=3;
    }
      
    rtUnitsUsed++;
  } // while
   
  cursor = iter.bucket;
  return iter.isNull();
}

bool
SimulatedBlock::doCleanupFragSend(Uint32 failedNodeId,
                                  Uint32& cursor,
                                  Uint32& rtUnitsUsed,
                                  Uint32& elementsCleaned)
{
  ljam();
  
  Ptr<FragmentSendInfo> fragPtr;
  const Uint32 NumSendLists = 2;
  ndbrequire(cursor < NumSendLists);

  DLList<FragmentSendInfo>* fragSendLists[ NumSendLists ] =
    { &c_segmentedFragmentSendList,
      &c_linearFragmentSendList };
  
  DLList<FragmentSendInfo>* list = fragSendLists[ cursor ];
  
  list->first(fragPtr);  
  for(; !fragPtr.isNull();){
    ljam();
    Ptr<FragmentSendInfo> copyPtr = fragPtr;
    list->next(fragPtr);
    rtUnitsUsed++;

    NodeReceiverGroup& rg = copyPtr.p->m_nodeReceiverGroup;
    
    if (rg.m_nodes.get(failedNodeId))
    {
      ljam();
      /* Fragmented signal is being sent to node */
      rg.m_nodes.clear(failedNodeId);
      
      if (rg.m_nodes.isclear())
      {
        ljam();
        /* No other nodes in receiver group - send
         * is cancelled
         * Will be cleaned up in the usual CONTINUE_FRAGMENTED
         * handling code.
         */
        copyPtr.p->m_status = FragmentSendInfo::SendCancelled;
      }
      elementsCleaned++;
    }
  }

  /* Next time we'll do the next list */
  cursor++;
  
  return (cursor == NumSendLists);
}


Uint32
SimulatedBlock::doNodeFailureCleanup(Signal* signal,
                                     Uint32 failedNodeId,
                                     Uint32 resource,
                                     Uint32 cursor,
                                     Uint32 elementsCleaned,
                                     Callback& cb)
{
  ljam();
  const bool userCallback = (cb.m_callbackFunction != 0);
  const Uint32 maxRtUnits = userCallback ?
#ifdef VM_TRACE
    2 :
#else
    16 :
#endif 
    ~0; /* Must complete all processing in this call */
  
  Uint32 rtUnitsUsed = 0;

  /* Loop over resources, cleaning them up */
  do
  {
    bool resourceDone= false;
    switch(resource) {
    case ContinueFragmented::RES_FRAGSEND:
    {
      ljam();
      resourceDone = doCleanupFragSend(failedNodeId, cursor,
                                       rtUnitsUsed, elementsCleaned);
      break;
    }
    case ContinueFragmented::RES_FRAGINFO:
    {
      ljam();
      resourceDone = doCleanupFragInfo(failedNodeId, cursor, 
                                       rtUnitsUsed, elementsCleaned);
      break;
    }
    case ContinueFragmented::RES_LAST:
    {
      ljam();
      /* Node failure processing complete, execute user callback if provided */
      if (userCallback)
        execute(signal, cb, elementsCleaned);
      
      return elementsCleaned;
    }
    default:
      ndbrequire(false);
    }

    /* Did we complete cleaning up this resource? */
    if (resourceDone)
    {
      resource++;
      cursor= 0;
    }

  } while (rtUnitsUsed <= maxRtUnits);
  
  ljam();

  /* Not yet completed failure handling.
   * Must have exhausted RT units.  
   * Update cursor and re-invoke
   */
  ndbassert(userCallback);
  
  /* Send signal to continue processing */
  
  ContinueFragmented * sig = (ContinueFragmented*)signal->getDataPtrSend();
  sig->type = ContinueFragmented::CONTINUE_CLEANUP;
  sig->cleanup.failedNodeId = failedNodeId;
  sig->cleanup.resource = resource;
  sig->cleanup.cursor = cursor;
  sig->cleanup.elementsCleaned= elementsCleaned;
  Uint32 callbackWords = (sizeof(Callback) + 3) >> 2;
  Uint32 sigLen = ContinueFragmented::CONTINUE_CLEANUP_FIXED_WORDS + 
    callbackWords;
  ndbassert(sigLen <= 25); // Should be STATIC_ASSERT
  memcpy(&sig->cleanup.callbackStart, &cb, callbackWords << 2);
  
  sendSignal(reference(), GSN_CONTINUE_FRAGMENTED, signal, sigLen, JBB);

  return elementsCleaned;
}
  
Uint32
SimulatedBlock::simBlockNodeFailure(Signal* signal,
                                    Uint32 failedNodeId, 
                                    Callback& cb)
{
  ljam();
  return doNodeFailureCleanup(signal, failedNodeId, 0, 0, 0, cb);
}

Uint32
SimulatedBlock::debugPrintFragmentCounts()
{
  const char* blockName = getBlockName(theNumber);
  DLHashTable<FragmentInfo>::Iterator iter;
  Uint32 fragmentInfoCount = 0;
  c_fragmentInfoHash.first(iter);
  
  while(!iter.isNull())
  {
    fragmentInfoCount++;
    c_fragmentInfoHash.next(iter);
  }
  
  Ptr<FragmentSendInfo> ptr;
  Uint32 linSendInfoCount = 0;

  c_linearFragmentSendList.first(ptr);
  
  while (!ptr.isNull())
  {
    linSendInfoCount++;
    c_linearFragmentSendList.next(ptr);
  }
  
  Uint32 segSendInfoCount = 0;
  c_segmentedFragmentSendList.first(ptr);
  
  while (!ptr.isNull())
  {
    segSendInfoCount++;
    c_segmentedFragmentSendList.next(ptr);
  }

  ndbout_c("%s : Fragment assembly hash entry count : %d", 
           blockName,
           fragmentInfoCount);

  ndbout_c("%s : Linear fragment send list size : %d", 
           blockName,
           linSendInfoCount);

  ndbout_c("%s : Segmented fragment send list size : %d", 
           blockName,
           segSendInfoCount);

  return fragmentInfoCount + 
    linSendInfoCount +
    segSendInfoCount;
}


bool
SimulatedBlock::sendFirstFragment(FragmentSendInfo & info,
				  NodeReceiverGroup rg, 
				  GlobalSignalNumber gsn, 
				  Signal* signal, 
				  Uint32 length, 
				  JobBufferLevel jbuf,
				  SectionHandle* sections,
				  Uint32 messageSize){
  
  Uint32 noSections = sections->m_cnt;
  SegmentedSectionPtr * ptr = sections->m_ptr;

  info.m_sectionPtr[0].m_segmented.i = RNIL;
  info.m_sectionPtr[1].m_segmented.i = RNIL;
  info.m_sectionPtr[2].m_segmented.i = RNIL;
  
  Uint32 totalSize = 0;
  switch(noSections){
  case 3:
    info.m_sectionPtr[2].m_segmented.i = ptr[2].i;
    info.m_sectionPtr[2].m_segmented.p = ptr[2].p;
    totalSize += ptr[2].sz;
  case 2:
    info.m_sectionPtr[1].m_segmented.i = ptr[1].i;
    info.m_sectionPtr[1].m_segmented.p = ptr[1].p;
    totalSize += ptr[1].sz;
  case 1:
    info.m_sectionPtr[0].m_segmented.i = ptr[0].i;
    info.m_sectionPtr[0].m_segmented.p = ptr[0].p;
    totalSize += ptr[0].sz;
  }

  if(totalSize <= messageSize + SectionSegment::DataLength){
    /**
     * Send signal directly
     */
    sendSignal(rg, gsn, signal, length, jbuf, sections);
    info.m_status = FragmentSendInfo::SendComplete;
    return true;
  }

  /**
   * Consume sections
   */
  sections->m_cnt = 0;
    
  /**
   * Setup info object
   */
  info.m_status = FragmentSendInfo::SendNotComplete;
  info.m_prio = (Uint8)jbuf;
  info.m_gsn = gsn;
  info.m_fragInfo = 1;
  info.m_messageSize = messageSize;
  info.m_fragmentId = c_fragmentIdCounter++;
  info.m_nodeReceiverGroup = rg;
  info.m_callback.m_callbackFunction = 0;
  
  Ptr<SectionSegment> tmp;
  if(!import(tmp, &signal->theData[0], length)){
    ndbrequire(false);
    return false;
  }
  info.m_theDataSection.p = &tmp.p->theData[0];
  info.m_theDataSection.sz = length;
  tmp.p->theData[length] = tmp.i;
  
  sendNextSegmentedFragment(signal, info);
  
  if(c_fragmentIdCounter == 0){
    /**
     * Fragment id 0 is invalid
     */
    c_fragmentIdCounter = 1;
  }

  return true;
}

#if 0
#define lsout(x) x
#else
#define lsout(x)
#endif

void
SimulatedBlock::sendNextSegmentedFragment(Signal* signal,
					  FragmentSendInfo & info){
  
  if (unlikely(info.m_status == FragmentSendInfo::SendCancelled))
  {
    /* Send was cancelled - all dest. nodes have failed
     * since send was started
     *
     * Free any sections still to be sent
     */
    Uint32 secCount = 0;
    SegmentedSectionPtr ssptr[3];
    for (Uint32 s = 0; s < 3; s++)
    {
      Uint32 sectionI = info.m_sectionPtr[s].m_segmented.i;
      if (sectionI != RNIL)
      {
        getSection(ssptr[secCount], sectionI);
        info.m_sectionPtr[s].m_segmented.i = RNIL;
        info.m_sectionPtr[s].m_segmented.p = NULL;
        secCount++;
      }
    }
    
    ::releaseSections(secCount, ssptr);
    
    /* Free inline signal data storage section */
    Uint32 inlineDataI = info.m_theDataSection.p[info.m_theDataSection.sz];
    g_sectionSegmentPool.release(inlineDataI);
    
    info.m_status = FragmentSendInfo::SendComplete;
    return;
  }

  /**
   * Store "theData"
   */
  const Uint32 sigLen = info.m_theDataSection.sz;
  memcpy(&signal->theData[0], info.m_theDataSection.p, 4 * sigLen);
  
  Uint32 sz = 0; 
  Uint32 maxSz = info.m_messageSize;
  
  Int32 secNo = 2;
  Uint32 secCount = 0;
  Uint32 * secNos = &signal->theData[sigLen];
  
  SectionHandle sections(this);
  SegmentedSectionPtr *ptr = sections.m_ptr;

  enum { Unknown = 0, Full = 1 } loop = Unknown;
  for(; secNo >= 0 && secCount < 3; secNo--){
    Uint32 ptrI = info.m_sectionPtr[secNo].m_segmented.i;
    if(ptrI == RNIL)
      continue;
    
    info.m_sectionPtr[secNo].m_segmented.i = RNIL;
    
    SectionSegment * ptrP = info.m_sectionPtr[secNo].m_segmented.p;
    const Uint32 size = ptrP->m_sz;
    
    ptr[secCount].i = ptrI;
    ptr[secCount].p = ptrP;
    ptr[secCount].sz = size;
    secNos[secCount] = secNo;
    secCount++;
    
    const Uint32 sizeLeft = maxSz - sz;
    if(size <= sizeLeft){
      /**
       * The section fits
       */
      sz += size;
      lsout(ndbout_c("section %d saved as %d", secNo, secCount-1));
      continue;
    }
    
    const Uint32 overflow = size - sizeLeft; // > 0
    if(overflow <= SectionSegment::DataLength){
      /**
       * Only one segment left to send
       *   send even if sizeLeft <= size
       */
      lsout(ndbout_c("section %d saved as %d but full over: %d", 
		     secNo, secCount-1, overflow));
      secNo--;
      break;
    }

    // size >= 61
    if(sizeLeft < SectionSegment::DataLength){
      /**
       * Less than one segment left (space)
       *   dont bother sending
       */
      secCount--;
      info.m_sectionPtr[secNo].m_segmented.i = ptrI;
      loop = Full;
      lsout(ndbout_c("section %d not saved", secNo));
      break;
    }
    
    /**
     * Split list
     * 1) Find place to split
     * 2) Rewrite header (the part that will be sent)
     * 3) Write new header (for remaining part)
     * 4) Store new header on FragmentSendInfo - record
     */
    // size >= 61 && sizeLeft >= 60
    Uint32 sum = SectionSegment::DataLength;
    Uint32 prevPtrI = ptrI;
    ptrI = ptrP->m_nextSegment;
    const Uint32 fill = sizeLeft - SectionSegment::DataLength;
    while(sum < fill){
      prevPtrI = ptrI;
      ptrP = g_sectionSegmentPool.getPtr(ptrI);
      ptrI = ptrP->m_nextSegment;
      sum += SectionSegment::DataLength;
    }
    
    /**
     * Rewrite header w.r.t size and last
     */
    Uint32 prev = secCount - 1;
    const Uint32 last = ptr[prev].p->m_lastSegment;
    ptr[prev].p->m_lastSegment = prevPtrI;
    ptr[prev].p->m_sz = sum;
    ptr[prev].sz = sum;
      
    /**
     * Write "new" list header
     */
    ptrP = g_sectionSegmentPool.getPtr(ptrI);
    ptrP->m_lastSegment = last;
    ptrP->m_sz = size - sum;
    
    /**
     * And store it on info-record
     */
    info.m_sectionPtr[secNo].m_segmented.i = ptrI;
    info.m_sectionPtr[secNo].m_segmented.p = ptrP;
    
    loop = Full;
    lsout(ndbout_c("section %d split into %d", secNo, prev));
    break;
  }
  
  lsout(ndbout_c("loop: %d secNo: %d secCount: %d sz: %d", 
		 loop, secNo, secCount, sz));
  
  /**
   * Store fragment id
   */
  secNos[secCount] = info.m_fragmentId;
  
  Uint32 fragInfo = info.m_fragInfo;
  info.m_fragInfo = 2;
  switch(loop){
  case Unknown:
    if(secNo >= 0){
      lsout(ndbout_c("Unknown - Full"));
      /**
       * Not finished
       */
      break;
    }
    // Fall through
    lsout(ndbout_c("Unknown - Done"));
    info.m_status = FragmentSendInfo::SendComplete;
    ndbassert(fragInfo == 2);
    fragInfo = 3;
  case Full:
    break;
  }
  
  signal->header.m_fragmentInfo = fragInfo;
  signal->header.m_noOfSections = 0;
  sections.m_cnt = secCount;
  
  sendSignal(info.m_nodeReceiverGroup,
	     info.m_gsn,
	     signal, 
	     sigLen + secCount + 1,
	     (JobBufferLevel)info.m_prio,
	     &sections);
  
  if(fragInfo == 3){
    /**
     * This is the last signal
     */
    g_sectionSegmentPool.release(info.m_theDataSection.p[sigLen]);
  }
}

bool
SimulatedBlock::sendFirstFragment(FragmentSendInfo & info,
				  NodeReceiverGroup rg, 
				  GlobalSignalNumber gsn, 
				  Signal* signal, 
				  Uint32 length, 
				  JobBufferLevel jbuf,
				  LinearSectionPtr ptr[3],
				  Uint32 noOfSections,
				  Uint32 messageSize){
  
  check_sections(signal, signal->header.m_noOfSections, noOfSections);
  
  info.m_sectionPtr[0].m_linear.p = NULL;
  info.m_sectionPtr[1].m_linear.p = NULL;
  info.m_sectionPtr[2].m_linear.p = NULL;
  
  Uint32 totalSize = 0;
  switch(noOfSections){
  case 3:
    info.m_sectionPtr[2].m_linear = ptr[2];
    totalSize += ptr[2].sz;
  case 2:
    info.m_sectionPtr[1].m_linear = ptr[1];
    totalSize += ptr[1].sz;
  case 1:
    info.m_sectionPtr[0].m_linear = ptr[0];
    totalSize += ptr[0].sz;
  }

  if(totalSize <= messageSize + SectionSegment::DataLength){
    /**
     * Send signal directly
     */
    sendSignal(rg, gsn, signal, length, jbuf, ptr, noOfSections);
    info.m_status = FragmentSendInfo::SendComplete;
    
    /**
     * Indicate to sendLinearSignalFragment
     *   that we'r already done
     */
    return true;
  }

  /**
   * Setup info object
   */
  info.m_status = FragmentSendInfo::SendNotComplete;
  info.m_prio = (Uint8)jbuf;
  info.m_gsn = gsn;
  info.m_messageSize = messageSize;
  info.m_fragInfo = 1;
  info.m_fragmentId = c_fragmentIdCounter++;
  info.m_nodeReceiverGroup = rg;
  info.m_callback.m_callbackFunction = 0;

  Ptr<SectionSegment> tmp;
  if(!import(tmp, &signal->theData[0], length)){
    ndbrequire(false);
    return false;
  }

  info.m_theDataSection.p = &tmp.p->theData[0];
  info.m_theDataSection.sz = length;
  tmp.p->theData[length] = tmp.i;
  
  sendNextLinearFragment(signal, info);

  if(c_fragmentIdCounter == 0){
    /**
     * Fragment id 0 is invalid
     */
    c_fragmentIdCounter = 1;
  }
  
  return true;
}

void
SimulatedBlock::sendNextLinearFragment(Signal* signal,
				       FragmentSendInfo & info){
  
  if (unlikely(info.m_status == FragmentSendInfo::SendCancelled))
  {
    /* Send was cancelled - all dest. nodes have failed
     * since send was started
     */
    /* Free inline signal data storage section */
    Uint32 inlineDataI = info.m_theDataSection.p[info.m_theDataSection.sz];
    g_sectionSegmentPool.release(inlineDataI);
    
    info.m_status = FragmentSendInfo::SendComplete;
    return;
  }

  /**
   * Store "theData"
   */
  const Uint32 sigLen = info.m_theDataSection.sz;
  memcpy(&signal->theData[0], info.m_theDataSection.p, 4 * sigLen);
  
  Uint32 sz = 0; 
  Uint32 maxSz = info.m_messageSize;
  
  Int32 secNo = 2;
  Uint32 secCount = 0;
  Uint32 * secNos = &signal->theData[sigLen];
  LinearSectionPtr signalPtr[3];
  
  enum { Unknown = 0, Full = 2 } loop = Unknown;
  for(; secNo >= 0 && secCount < 3; secNo--){
    Uint32 * ptrP = info.m_sectionPtr[secNo].m_linear.p;
    if(ptrP == NULL)
      continue;
    
    info.m_sectionPtr[secNo].m_linear.p = NULL;
    const Uint32 size = info.m_sectionPtr[secNo].m_linear.sz;
    
    signalPtr[secCount].p = ptrP;
    signalPtr[secCount].sz = size;
    secNos[secCount] = secNo;
    secCount++;
    
    const Uint32 sizeLeft = maxSz - sz;
    if(size <= sizeLeft){
      /**
       * The section fits
       */
      sz += size;
      lsout(ndbout_c("section %d saved as %d", secNo, secCount-1));
      continue;
    }
    
    const Uint32 overflow = size - sizeLeft; // > 0
    if(overflow <= SectionSegment::DataLength){
      /**
       * Only one segment left to send
       *   send even if sizeLeft <= size
       */
      lsout(ndbout_c("section %d saved as %d but full over: %d", 
		     secNo, secCount-1, overflow));
      secNo--;
      break;
    }

    // size >= 61
    if(sizeLeft < SectionSegment::DataLength){
      /**
       * Less than one segment left (space)
       *   dont bother sending
       */
      secCount--;
      info.m_sectionPtr[secNo].m_linear.p = ptrP;
      loop = Full;
      lsout(ndbout_c("section %d not saved", secNo));
      break;
    }
    
    /**
     * Split list
     * 1) Find place to split
     * 2) Rewrite header (the part that will be sent)
     * 3) Write new header (for remaining part)
     * 4) Store new header on FragmentSendInfo - record
     */
    Uint32 sum = sizeLeft;
    sum /= SectionSegment::DataLength;
    sum *= SectionSegment::DataLength;
    
    /**
     * Rewrite header w.r.t size
     */
    Uint32 prev = secCount - 1;
    signalPtr[prev].sz = sum;
    
    /**
     * Write/store "new" header
     */
    info.m_sectionPtr[secNo].m_linear.p = ptrP + sum;
    info.m_sectionPtr[secNo].m_linear.sz = size - sum;
    
    loop = Full;
    lsout(ndbout_c("section %d split into %d", secNo, prev));
    break;
  }
  
  lsout(ndbout_c("loop: %d secNo: %d secCount: %d sz: %d", 
		 loop, secNo, secCount, sz));
  
  /**
   * Store fragment id
   */
  secNos[secCount] = info.m_fragmentId;
  
  Uint32 fragInfo = info.m_fragInfo;
  info.m_fragInfo = 2;
  switch(loop){
  case Unknown:
    if(secNo >= 0){
      lsout(ndbout_c("Unknown - Full"));
      /**
       * Not finished
       */
      break;
    }
    // Fall through
    lsout(ndbout_c("Unknown - Done"));
    info.m_status = FragmentSendInfo::SendComplete;
    ndbassert(fragInfo == 2);
    fragInfo = 3;
  case Full:
    break;
  }
  
  signal->header.m_noOfSections = 0;
  signal->header.m_fragmentInfo = fragInfo;
  
  sendSignal(info.m_nodeReceiverGroup,
	     info.m_gsn,
	     signal, 
	     sigLen + secCount + 1,
	     (JobBufferLevel)info.m_prio,
	     signalPtr,
	     secCount);
  
  if(fragInfo == 3){
    /**
     * This is the last signal
     */
    g_sectionSegmentPool.release(info.m_theDataSection.p[sigLen]);
  }
}

void
SimulatedBlock::sendFragmentedSignal(BlockReference ref, 
				     GlobalSignalNumber gsn, 
				     Signal* signal, 
				     Uint32 length, 
				     JobBufferLevel jbuf,
				     SectionHandle* sections,
				     Callback & c,
				     Uint32 messageSize){
  bool res = true;
  Ptr<FragmentSendInfo> tmp;
  res = c_segmentedFragmentSendList.seize(tmp);
  ndbrequire(res);
  
  res = sendFirstFragment(* tmp.p,
			  NodeReceiverGroup(ref),
			  gsn,
			  signal,
			  length,
			  jbuf,
			  sections,
			  messageSize);
  ndbrequire(res);
  
  if(tmp.p->m_status == FragmentSendInfo::SendComplete){
    c_segmentedFragmentSendList.release(tmp);
    if(c.m_callbackFunction != 0)
      execute(signal, c, 0);
    return;
  }
  tmp.p->m_callback = c;

  if(!c_fragSenderRunning){
    c_fragSenderRunning = true;
    ContinueFragmented * sig = (ContinueFragmented*)signal->getDataPtrSend();
    sig->type = ContinueFragmented::CONTINUE_SENDING;
    sig->line = __LINE__;
    sendSignal(reference(), GSN_CONTINUE_FRAGMENTED, signal, 2, JBB);
  }
}

void
SimulatedBlock::sendFragmentedSignal(NodeReceiverGroup rg, 
				     GlobalSignalNumber gsn, 
				     Signal* signal, 
				     Uint32 length, 
				     JobBufferLevel jbuf,
				     SectionHandle * sections,
				     Callback & c,
				     Uint32 messageSize){
  bool res = true;
  Ptr<FragmentSendInfo> tmp;
  res = c_segmentedFragmentSendList.seize(tmp);
  ndbrequire(res);
  
  res = sendFirstFragment(* tmp.p,
			  rg,
			  gsn,
			  signal,
			  length,
			  jbuf,
			  sections,
			  messageSize);
  ndbrequire(res);
  
  if(tmp.p->m_status == FragmentSendInfo::SendComplete){
    c_segmentedFragmentSendList.release(tmp);
    if(c.m_callbackFunction != 0)
      execute(signal, c, 0);
    return;
  }
  tmp.p->m_callback = c;

  if(!c_fragSenderRunning){
    c_fragSenderRunning = true;
    ContinueFragmented * sig = (ContinueFragmented*)signal->getDataPtrSend();
    sig->type = ContinueFragmented::CONTINUE_SENDING;
    sig->line = __LINE__;
    sendSignal(reference(), GSN_CONTINUE_FRAGMENTED, signal, 2, JBB);
  }
}

SimulatedBlock::Callback SimulatedBlock::TheEmptyCallback = {0, 0};
void
SimulatedBlock::TheNULLCallbackFunction(class Signal*, Uint32, Uint32)
{ abort(); /* should never be called */ }
SimulatedBlock::Callback SimulatedBlock::TheNULLCallback =
{ &SimulatedBlock::TheNULLCallbackFunction, 0 };

void
SimulatedBlock::sendFragmentedSignal(BlockReference ref, 
				     GlobalSignalNumber gsn, 
				     Signal* signal, 
				     Uint32 length, 
				     JobBufferLevel jbuf,
				     LinearSectionPtr ptr[3],
				     Uint32 noOfSections,
				     Callback & c,
				     Uint32 messageSize){
  bool res = true;
  Ptr<FragmentSendInfo> tmp;
  res = c_linearFragmentSendList.seize(tmp);
  ndbrequire(res);

  res = sendFirstFragment(* tmp.p, 
			  NodeReceiverGroup(ref),
			  gsn,
			  signal,
			  length,
			  jbuf,
			  ptr,
			  noOfSections,
			  messageSize);
  ndbrequire(res);
  
  if(tmp.p->m_status == FragmentSendInfo::SendComplete){
    c_linearFragmentSendList.release(tmp);
    if(c.m_callbackFunction != 0)
      execute(signal, c, 0);
    return;
  }
  tmp.p->m_callback = c;
  
  if(!c_fragSenderRunning){
    c_fragSenderRunning = true;
    ContinueFragmented * sig = (ContinueFragmented*)signal->getDataPtrSend();
    sig->type = ContinueFragmented::CONTINUE_SENDING;
    sig->line = __LINE__;
    sendSignal(reference(), GSN_CONTINUE_FRAGMENTED, signal, 2, JBB);
  }
}

void
SimulatedBlock::sendFragmentedSignal(NodeReceiverGroup rg, 
				     GlobalSignalNumber gsn, 
				     Signal* signal, 
				     Uint32 length, 
				     JobBufferLevel jbuf,
				     LinearSectionPtr ptr[3],
				     Uint32 noOfSections,
				     Callback & c,
				     Uint32 messageSize){
  bool res = true;
  Ptr<FragmentSendInfo> tmp;
  res = c_linearFragmentSendList.seize(tmp);
  ndbrequire(res);

  res = sendFirstFragment(* tmp.p, 
			  rg,
			  gsn,
			  signal,
			  length,
			  jbuf,
			  ptr,
			  noOfSections,
			  messageSize);
  ndbrequire(res);
  
  if(tmp.p->m_status == FragmentSendInfo::SendComplete){
    c_linearFragmentSendList.release(tmp);
    if(c.m_callbackFunction != 0)
      execute(signal, c, 0);
    return;
  }
  tmp.p->m_callback = c;
  
  if(!c_fragSenderRunning){
    c_fragSenderRunning = true;
    ContinueFragmented * sig = (ContinueFragmented*)signal->getDataPtrSend();
    sig->type = ContinueFragmented::CONTINUE_SENDING;
    sig->line = __LINE__;
    sendSignal(reference(), GSN_CONTINUE_FRAGMENTED, signal, 2, JBB);
  }
}

NodeInfo &
SimulatedBlock::setNodeInfo(NodeId nodeId) {
  ndbrequire(nodeId > 0 && nodeId < MAX_NODES);
  return globalData.m_nodeInfo[nodeId];
}

void 
SimulatedBlock::execUTIL_CREATE_LOCK_REF(Signal* signal){
  ljamEntry();
  c_mutexMgr.execUTIL_CREATE_LOCK_REF(signal);
}

void SimulatedBlock::execUTIL_CREATE_LOCK_CONF(Signal* signal){
  ljamEntry();
  c_mutexMgr.execUTIL_CREATE_LOCK_CONF(signal);
}

void SimulatedBlock::execUTIL_DESTORY_LOCK_REF(Signal* signal){
  ljamEntry();
  c_mutexMgr.execUTIL_DESTORY_LOCK_REF(signal);
}

void SimulatedBlock::execUTIL_DESTORY_LOCK_CONF(Signal* signal){
  ljamEntry();
  c_mutexMgr.execUTIL_DESTORY_LOCK_CONF(signal);
}

void SimulatedBlock::execUTIL_LOCK_REF(Signal* signal){
  ljamEntry();
  c_mutexMgr.execUTIL_LOCK_REF(signal);
}

void SimulatedBlock::execUTIL_LOCK_CONF(Signal* signal){
  ljamEntry();
  c_mutexMgr.execUTIL_LOCK_CONF(signal);
}

void SimulatedBlock::execUTIL_UNLOCK_REF(Signal* signal){
  ljamEntry();
  c_mutexMgr.execUTIL_UNLOCK_REF(signal);
}

void SimulatedBlock::execUTIL_UNLOCK_CONF(Signal* signal){
  ljamEntry();
  c_mutexMgr.execUTIL_UNLOCK_CONF(signal);
}

void
SimulatedBlock::ignoreMutexUnlockCallback(Signal* signal, 
					  Uint32 ptrI, Uint32 retVal){
  c_mutexMgr.release(ptrI);
}

void 
UpgradeStartup::installEXEC(SimulatedBlock* block){
  SimulatedBlock::ExecFunction * a = block->theExecArray;
  switch(block->number()){
  case QMGR:
    a[UpgradeStartup::GSN_CM_APPCHG] = &SimulatedBlock::execUPGRADE;
    break;
  case CNTR:
    a[UpgradeStartup::GSN_CNTR_MASTERREF] = &SimulatedBlock::execUPGRADE;
    a[UpgradeStartup::GSN_CNTR_MASTERCONF] = &SimulatedBlock::execUPGRADE;
    break;
  }
}

void
SimulatedBlock::execUPGRADE(Signal* signal){
  Uint32 gsn = signal->header.theVerId_signalNumber;
  switch(gsn){
  case UpgradeStartup::GSN_CM_APPCHG:
    UpgradeStartup::execCM_APPCHG(* this, signal);
    break;
  case UpgradeStartup::GSN_CNTR_MASTERREF:
    UpgradeStartup::execCNTR_MASTER_REPLY(* this, signal);
    break;
  case UpgradeStartup::GSN_CNTR_MASTERCONF:
    UpgradeStartup::execCNTR_MASTER_REPLY(* this, signal);
    break;
  }
}

void
SimulatedBlock::fsRefError(Signal* signal, Uint32 line, const char *msg) 
{
  const FsRef *fsRef = (FsRef*)signal->getDataPtr();
  Uint32 errorCode = fsRef->errorCode;
  Uint32 osErrorCode = fsRef->osErrorCode;
  char msg2[100];

  sprintf(msg2, "%s: %s. OS errno: %u", getBlockName(number()), msg, osErrorCode);

  progError(line, errorCode, msg2);
}

void
SimulatedBlock::execFSWRITEREF(Signal* signal) 
{
  fsRefError(signal, __LINE__, "File system write failed");
}

void
SimulatedBlock::execFSREADREF(Signal* signal) 
{
  fsRefError(signal, __LINE__, "File system read failed");
}

void
SimulatedBlock::execFSCLOSEREF(Signal* signal) 
{
  fsRefError(signal, __LINE__, "File system close failed");
}

void
SimulatedBlock::execFSOPENREF(Signal* signal) 
{
  fsRefError(signal, __LINE__, "File system open failed");
}

void
SimulatedBlock::execFSREMOVEREF(Signal* signal) 
{
  fsRefError(signal, __LINE__, "File system remove failed");
}

void
SimulatedBlock::execFSSYNCREF(Signal* signal) 
{
  fsRefError(signal, __LINE__, "File system sync failed");
}

void
SimulatedBlock::execFSAPPENDREF(Signal* signal) 
{
  fsRefError(signal, __LINE__, "File system append failed");
}

#ifdef VM_TRACE
static Ptr<void> * m_empty_global_variables[] = { 0 };
void
SimulatedBlock::disable_global_variables()
{
  m_global_variables_save = m_global_variables;
  m_global_variables = m_empty_global_variables;
}

void
SimulatedBlock::enable_global_variables()
{
  if (m_global_variables == m_empty_global_variables)
  {
    m_global_variables = m_global_variables_save;
  }
}

void
SimulatedBlock::clear_global_variables(){
  Ptr<void> ** tmp = m_global_variables;
  while(* tmp != 0){
    (* tmp)->i = RNIL;
    (* tmp)->p = 0;
    tmp++;
  }
}

void
SimulatedBlock::init_globals_list(void ** tmp, size_t cnt){
  m_global_variables = new Ptr<void> * [cnt+1];
  for(size_t i = 0; i<cnt; i++){
    m_global_variables[i] = (Ptr<void>*)tmp[i];
  }
  m_global_variables[cnt] = 0;
}

#endif

#include "KeyDescriptor.hpp"

Uint32
SimulatedBlock::xfrm_key(Uint32 tab, const Uint32* src, 
			 Uint32 *dst, Uint32 dstSize,
			 Uint32 keyPartLen[MAX_ATTRIBUTES_IN_INDEX]) const
{
  const KeyDescriptor * desc = g_key_descriptor_pool.getPtr(tab);
  const Uint32 noOfKeyAttr = desc->noOfKeyAttr;

  Uint32 i = 0;
  Uint32 srcPos = 0;
  Uint32 dstPos = 0;
  while (i < noOfKeyAttr) 
  {
    const KeyDescriptor::KeyAttr& keyAttr = desc->keyAttr[i];
    Uint32 dstWords =
      xfrm_attr(keyAttr.attributeDescriptor, keyAttr.charsetInfo,
                src, srcPos, dst, dstPos, dstSize);
    keyPartLen[i++] = dstWords;
    if (unlikely(dstWords == 0))
      return 0;
  }

  if (0)
  {
    for(Uint32 i = 0; i<dstPos; i++)
    {
      printf("%.8x ", dst[i]);
    }
    printf("\n");
  }
  return dstPos;
}

Uint32
SimulatedBlock::xfrm_attr(Uint32 attrDesc, CHARSET_INFO* cs,
                          const Uint32* src, Uint32 & srcPos,
                          Uint32* dst, Uint32 & dstPos, Uint32 dstSize) const
{
  Uint32 array = 
    AttributeDescriptor::getArrayType(attrDesc);
  Uint32 srcBytes = 
    AttributeDescriptor::getSizeInBytes(attrDesc);

  Uint32 srcWords = ~0;
  Uint32 dstWords = ~0;
  uchar* dstPtr = (uchar*)&dst[dstPos];
  const uchar* srcPtr = (const uchar*)&src[srcPos];
  
  if (cs == NULL)
  {
    jam();
    Uint32 len;
    LINT_INIT(len);
    switch(array){
    case NDB_ARRAYTYPE_SHORT_VAR:
      len = 1 + srcPtr[0];
      break;
    case NDB_ARRAYTYPE_MEDIUM_VAR:
      len = 2 + srcPtr[0] + (srcPtr[1] << 8);
      break;
#ifndef VM_TRACE
    default:
      abort();
#endif
    case NDB_ARRAYTYPE_FIXED:
      len = srcBytes;
    }
    srcWords = (len + 3) >> 2;
    dstWords = srcWords;
    memcpy(dstPtr, srcPtr, dstWords << 2);
    
    if (0)
    {
      ndbout_c("srcPos: %d dstPos: %d len: %d srcWords: %d dstWords: %d",
               srcPos, dstPos, len, srcWords, dstWords);
      
      for(Uint32 i = 0; i<srcWords; i++)
        printf("%.8x ", src[srcPos + i]);
      printf("\n");
    }
  } 
  else
  {
    jam();
    Uint32 typeId =
      AttributeDescriptor::getType(attrDesc);
    Uint32 lb, len;
    bool ok = NdbSqlUtil::get_var_length(typeId, srcPtr, srcBytes, lb, len);
    if (unlikely(!ok))
      return 0;
    Uint32 xmul = cs->strxfrm_multiply;
    if (xmul == 0)
      xmul = 1;
    /*
     * Varchar end-spaces are ignored in comparisons.  To get same hash
     * we blank-pad to maximum length via strnxfrm.
     */
    Uint32 dstLen = xmul * (srcBytes - lb);
    ndbrequire(dstLen <= ((dstSize - dstPos) << 2));
    int n = NdbSqlUtil::strnxfrm_bug7284(cs, dstPtr, dstLen, srcPtr + lb, len);
    if (unlikely(n == -1))
      return 0;
    while ((n & 3) != 0) 
    {
      dstPtr[n++] = 0;
    }
    dstWords = (n >> 2);
    srcWords = (lb + len + 3) >> 2; 
  }

  dstPos += dstWords;
  srcPos += srcWords;
  return dstWords;
}

Uint32
SimulatedBlock::create_distr_key(Uint32 tableId,
				 Uint32 *data, 
				 const Uint32 
				 keyPartLen[MAX_ATTRIBUTES_IN_INDEX]) const 
{
  const KeyDescriptor* desc = g_key_descriptor_pool.getPtr(tableId);
  const Uint32 noOfKeyAttr = desc->noOfKeyAttr;
  Uint32 noOfDistrKeys = desc->noOfDistrKeys;
  
  Uint32 *src = data;
  Uint32 *dst = data;
  Uint32 i = 0;
  Uint32 dstPos = 0;
  
  if(keyPartLen)
  {
    while (i < noOfKeyAttr && noOfDistrKeys) 
    {
      Uint32 attr = desc->keyAttr[i].attributeDescriptor;
      Uint32 len = keyPartLen[i];
      if(AttributeDescriptor::getDKey(attr))
      {
	noOfDistrKeys--;
	memmove(dst+dstPos, src, len << 2);
	dstPos += len;
      }
      src += len;
      i++;
    }
  }
  else
  {
    while (i < noOfKeyAttr && noOfDistrKeys) 
    {
      Uint32 attr = desc->keyAttr[i].attributeDescriptor;
      Uint32 len = AttributeDescriptor::getSizeInWords(attr);
      ndbrequire(AttributeDescriptor::getArrayType(attr) == NDB_ARRAYTYPE_FIXED);
      if(AttributeDescriptor::getDKey(attr))
      {
	noOfDistrKeys--;
	memmove(dst+dstPos, src, len << 2);
	dstPos += len;
      }
      src += len;
      i++;
    }
  }
  return dstPos;
}

CArray<KeyDescriptor> g_key_descriptor_pool;

void
SimulatedBlock::sendRoutedSignal(RoutePath path[], Uint32 pathcnt,
                                 Uint32 dst[],
                                 Uint32 dstcnt,
                                 Uint32 gsn,
                                 Signal * signal,
                                 Uint32 sigLen,
                                 JobBufferLevel prio,
                                 SectionHandle * userhandle)
{
  ndbrequire(pathcnt > 0); // don't support (now) directly multi-cast
  pathcnt--; // first hop is made from here


  Uint32 len = LocalRouteOrd::StaticLen + (2 * pathcnt) + dstcnt;
  ndbrequire(len <= 25);

  SectionHandle handle(this, signal);
  if (userhandle)
  {
    ljam();
    handle.m_cnt = userhandle->m_cnt;
    for (Uint32 i = 0; i<handle.m_cnt; i++)
      handle.m_ptr[i] = userhandle->m_ptr[i];
    userhandle->m_cnt = 0;
  }

  if (len + sigLen > 25)
  {
    ljam();

    /**
     * we need to store theData in a section
     */
    ndbrequire(handle.m_cnt < 3);
    handle.m_ptr[2] = handle.m_ptr[1];
    handle.m_ptr[1] = handle.m_ptr[0];
    Ptr<SectionSegment> tmp;
    ndbrequire(import(tmp, signal->theData, sigLen));
    handle.m_ptr[0].p = tmp.p;
    handle.m_ptr[0].i = tmp.i;
    handle.m_ptr[0].sz = sigLen;
    handle.m_cnt ++;
  }
  else
  {
    ljam();
    memmove(signal->theData + len, signal->theData, 4 * sigLen);
    len += sigLen;
  }

  LocalRouteOrd * ord = (LocalRouteOrd*)signal->getDataPtrSend();
  ord->cnt = (pathcnt << 16) | (dstcnt);
  ord->gsn = gsn;
  ord->prio = Uint32(prio);

  Uint32 * dstptr = ord->path;
  for (Uint32 i = 1; i <= pathcnt; i++)
  {
    ndbrequire(refToNode(path[i].ref) == 0 ||
               refToNode(path[i].ref) == getOwnNodeId());

    * dstptr++ = path[i].ref;
    * dstptr++ = Uint32(path[i].prio);
  }

  for (Uint32 i = 0; i<dstcnt; i++)
  {
    ndbrequire(refToNode(dst[i]) == 0 ||
               refToNode(dst[i]) == getOwnNodeId());

    * dstptr++ = dst[i];
  }

  sendSignal(path[0].ref, GSN_LOCAL_ROUTE_ORD, signal, len,
             path[0].prio, &handle);
}

void
SimulatedBlock::execLOCAL_ROUTE_ORD(Signal* signal)
{
  ljamEntry();

  if (!assembleFragments(signal))
  {
    ljam();
    return;
  }

  if (ERROR_INSERTED(1001))
  {
    /**
     * This NDBCNTR error code 1001
     */
    ljam();
    SectionHandle handle(this, signal);
    sendSignalWithDelay(reference(), GSN_LOCAL_ROUTE_ORD, signal, 200, 
                        signal->getLength(), &handle);
    return;
  }

  LocalRouteOrd* ord = (LocalRouteOrd*)signal->getDataPtr();
  Uint32 pathcnt = ord->cnt >> 16;
  Uint32 dstcnt = ord->cnt & 0xFFFF;
  Uint32 sigLen = signal->getLength();

  if (pathcnt == 0)
  {
    /**
     * Send to final destination(s);
     */
    ljam();
    Uint32 gsn = ord->gsn;
    Uint32 prio = ord->prio;
    memcpy(signal->theData+25, ord->path, 4*dstcnt);
    SectionHandle handle(this, signal);
    if (sigLen > LocalRouteOrd::StaticLen + dstcnt)
    {
      ljam();
      /**
       * Data is at end of this...
       */
      memmove(signal->theData,
              signal->theData + LocalRouteOrd::StaticLen + dstcnt,
              4 * (sigLen - (LocalRouteOrd::StaticLen + dstcnt)));
      sigLen = sigLen - (LocalRouteOrd::StaticLen + dstcnt);
    }
    else
    {
      ljam();
      /**
       * Put section 0 in signal->theData
       */
      sigLen = handle.m_ptr[0].sz;
      ndbrequire(sigLen <= 25);
      copy(signal->theData, handle.m_ptr[0]);
      release(handle.m_ptr[0]);

      for (Uint32 i = 0; i < handle.m_cnt - 1; i++)
        handle.m_ptr[i] = handle.m_ptr[i+1];
      handle.m_cnt--;
    }

    /*
     * The extra if-statement is as sendSignalNoRelease will copy sections
     *   which is not necessary is only sending to one destination
     */
    if (dstcnt > 1)
    {
      jam();
      /** 6.3 has not yet impl. sendSignalNoRelease */
#if NDB_VERRSION_D >= NDB_MAKE_VERSION(6,4,0)
      for (Uint32 i = 0; i<dstcnt; i++)
      {
        ljam();
        sendSignalNoRelease(signal->theData[25+i], gsn, signal, sigLen,
                            JobBufferLevel(prio), &handle);
      }
      releaseSections(handle);
#else
      if (handle.m_cnt == 0)
      {
        ljam();
        for (Uint32 i = 0; i<dstcnt; i++)
        {
          ljam();
          sendSignal(signal->theData[25+i], gsn, signal, sigLen,
                     JobBufferLevel(prio));
        }
      }
      else
      {
        /**
         * This path is (out of laziness?) not implemented in 6.3
         */
        ndbrequire(false);
      }
#endif
    }
    else
    {
      jam();
      sendSignal(signal->theData[25+0], gsn, signal, sigLen,
                 JobBufferLevel(prio), &handle);
    }
  }
  else
  {
    /**
     * Reroute
     */
    ljam();
    SectionHandle handle(this, signal);
    Uint32 ref = ord->path[0];
    Uint32 prio = ord->path[1];
    Uint32 len = sigLen - 2;
    ord->cnt = ((pathcnt - 1) << 16) | dstcnt;
    memmove(ord->path, ord->path+2, 4 * (len - LocalRouteOrd::StaticLen));
    sendSignal(ref, GSN_LOCAL_ROUTE_ORD, signal, len,
               JobBufferLevel(prio), &handle);
  }
}


bool
SimulatedBlock::checkNodeFailSequence(Signal* signal)
{
  Uint32 ref = signal->getSendersBlockRef();

  /**
   * Make sure that a signal being part of node-failure handling
   *   from a remote node, does not get to us before we got the NODE_FAILREP
   *   (this to avoid tricky state handling)
   *
   * To ensure this, we send the signal via QMGR (GSN_COMMIT_FAILREQ)
   *   and NDBCNTR (which sends NODE_FAILREP)
   *
   * The extra time should be negilable
   *
   * Note, make an exception for signals sent by our self
   *       as they are only sent as a consequence of NODE_FAILREP
   */
  if (ref == reference() ||
      (refToNode(ref) == getOwnNodeId() &&
       refToMain(ref) == NDBCNTR))
  {
    ljam();
    return true;
  }

  RoutePath path[2];
  path[0].ref = QMGR_REF;
  path[0].prio = JBB;
  path[1].ref = NDBCNTR_REF;
  path[1].prio = JBB;

  Uint32 dst[1];
  dst[0] = reference();

  SectionHandle handle(this, signal);
  Uint32 gsn = signal->header.theVerId_signalNumber;
  Uint32 len = signal->getLength();

  sendRoutedSignal(path, 2, dst, 1, gsn, signal, len, JBB, &handle);
  return false;
}

void
SimulatedBlock::setup_wakeup()
{
#ifdef NDBD_MULTITHREADED
#else
  globalTransporterRegistry.setup_wakeup_socket();
#endif
}

void
SimulatedBlock::wakeup()
{
#ifdef NDBD_MULTITHREADED
  mt_wakeup(this);
#else
  globalTransporterRegistry.wakeup();
#endif
}
