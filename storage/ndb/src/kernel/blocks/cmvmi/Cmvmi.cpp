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

#include "Cmvmi.hpp"

#include <Configuration.hpp>
#include <kernel_types.h>
#include <TransporterRegistry.hpp>
#include <NdbOut.hpp>
#include <NdbMem.h>
#include <NdbTick.h>

#include <SignalLoggerManager.hpp>
#include <FastScheduler.hpp>

#define DEBUG(x) { ndbout << "CMVMI::" << x << endl; }

#include <signaldata/TestOrd.hpp>
#include <signaldata/EventReport.hpp>
#include <signaldata/TamperOrd.hpp>
#include <signaldata/StartOrd.hpp>
#include <signaldata/CloseComReqConf.hpp>
#include <signaldata/SetLogLevelOrd.hpp>
#include <signaldata/EventSubscribeReq.hpp>
#include <signaldata/DumpStateOrd.hpp>
#include <signaldata/DisconnectRep.hpp>
#include <signaldata/EnableCom.hpp>
#include <signaldata/RouteOrd.hpp>
#include <signaldata/DbinfoScan.hpp>

#include <EventLogger.hpp>
#include <TimeQueue.hpp>

#include <NdbSleep.h>
#include <SafeCounter.hpp>

#define ZREPORT_MEMORY_USAGE 1000

// Used here only to print event reports on stdout/console.
extern EventLogger * g_eventLogger;
extern int simulate_error_during_shutdown;

// Index pages used by ACC instances
Uint32 g_acc_pages_used[1 + MAX_NDBMT_LQH_WORKERS];

extern void mt_set_section_chunk_size();

Cmvmi::Cmvmi(Block_context& ctx) :
  SimulatedBlock(CMVMI, ctx)
  ,subscribers(subscriberPool)
{
  BLOCK_CONSTRUCTOR(Cmvmi);

  Uint32 long_sig_buffer_size;
  const ndb_mgm_configuration_iterator * p = 
    m_ctx.m_config.getOwnConfigIterator();
  ndbrequire(p != 0);

  ndb_mgm_get_int_parameter(p, CFG_DB_LONG_SIGNAL_BUFFER,  
			    &long_sig_buffer_size);

  /* Ensure that aligned allocation will result in 64-bit
   * aligned offset for theData
   */
  STATIC_ASSERT((sizeof(SectionSegment) % 8) == 0);
  STATIC_ASSERT((offsetof(SectionSegment, theData) % 8) == 0); 

  long_sig_buffer_size= long_sig_buffer_size / sizeof(SectionSegment);
  g_sectionSegmentPool.setSize(long_sig_buffer_size,
                               true,true,true,CFG_DB_LONG_SIGNAL_BUFFER);

  mt_set_section_chunk_size();

  // Add received signals
  addRecSignal(GSN_CONNECT_REP, &Cmvmi::execCONNECT_REP);
  addRecSignal(GSN_DISCONNECT_REP, &Cmvmi::execDISCONNECT_REP);

  addRecSignal(GSN_NDB_TAMPER,  &Cmvmi::execNDB_TAMPER, true);
  addRecSignal(GSN_SET_LOGLEVELORD,  &Cmvmi::execSET_LOGLEVELORD);
  addRecSignal(GSN_EVENT_REP,  &Cmvmi::execEVENT_REP);
  addRecSignal(GSN_STTOR,  &Cmvmi::execSTTOR);
  addRecSignal(GSN_READ_CONFIG_REQ,  &Cmvmi::execREAD_CONFIG_REQ);
  addRecSignal(GSN_CLOSE_COMREQ,  &Cmvmi::execCLOSE_COMREQ);
  addRecSignal(GSN_ENABLE_COMREQ,  &Cmvmi::execENABLE_COMREQ);
  addRecSignal(GSN_OPEN_COMREQ,  &Cmvmi::execOPEN_COMREQ);
  addRecSignal(GSN_TEST_ORD,  &Cmvmi::execTEST_ORD);

  addRecSignal(GSN_TAMPER_ORD,  &Cmvmi::execTAMPER_ORD);
  addRecSignal(GSN_STOP_ORD,  &Cmvmi::execSTOP_ORD);
  addRecSignal(GSN_START_ORD,  &Cmvmi::execSTART_ORD);
  addRecSignal(GSN_EVENT_SUBSCRIBE_REQ, 
               &Cmvmi::execEVENT_SUBSCRIBE_REQ);

  addRecSignal(GSN_DUMP_STATE_ORD, &Cmvmi::execDUMP_STATE_ORD);

  addRecSignal(GSN_TESTSIG, &Cmvmi::execTESTSIG);
  addRecSignal(GSN_NODE_START_REP, &Cmvmi::execNODE_START_REP, true);

  addRecSignal(GSN_CONTINUEB, &Cmvmi::execCONTINUEB);
  addRecSignal(GSN_ROUTE_ORD, &Cmvmi::execROUTE_ORD);
  addRecSignal(GSN_DBINFO_SCANREQ, &Cmvmi::execDBINFO_SCANREQ);
  
  subscriberPool.setSize(5);
  
  const ndb_mgm_configuration_iterator * db = m_ctx.m_config.getOwnConfigIterator();
  for(unsigned j = 0; j<LogLevel::LOGLEVEL_CATEGORIES; j++){
    Uint32 logLevel;
    if(!ndb_mgm_get_int_parameter(db, CFG_MIN_LOGLEVEL+j, &logLevel)){
      clogLevel.setLogLevel((LogLevel::EventCategory)j, 
			    logLevel);
    }
  }
  
  ndb_mgm_configuration_iterator * iter = m_ctx.m_config.getClusterConfigIterator();
  for(ndb_mgm_first(iter); ndb_mgm_valid(iter); ndb_mgm_next(iter)){
    jam();
    Uint32 nodeId;
    Uint32 nodeType;

    ndbrequire(!ndb_mgm_get_int_parameter(iter,CFG_NODE_ID, &nodeId));
    ndbrequire(!ndb_mgm_get_int_parameter(iter,CFG_TYPE_OF_SECTION,&nodeType));

    switch(nodeType){
    case NodeInfo::DB:
      c_dbNodes.set(nodeId);
      break;
    case NodeInfo::API:
    case NodeInfo::MGM:
      break;
    default:
      ndbrequire(false);
    }
    setNodeInfo(nodeId).m_type = nodeType;
  }

  setNodeInfo(getOwnNodeId()).m_connected = true;
  setNodeInfo(getOwnNodeId()).m_version = ndbGetOwnVersion();
  setNodeInfo(getOwnNodeId()).m_mysql_version = NDB_MYSQL_VERSION_D;

  c_memusage_report_frequency = 0;

  m_start_time = NdbTick_CurrentMillisecond() / 1000; // seconds

  bzero(g_acc_pages_used, sizeof(g_acc_pages_used));

}

Cmvmi::~Cmvmi()
{
  m_shared_page_pool.clear();
}

#ifdef ERROR_INSERT
NodeBitmask c_error_9000_nodes_mask;
extern Uint32 MAX_RECEIVED_SIGNALS;
#endif

void Cmvmi::execNDB_TAMPER(Signal* signal) 
{
  jamEntry();
  SET_ERROR_INSERT_VALUE(signal->theData[0]);
  if(ERROR_INSERTED(9999)){
    CRASH_INSERTION(9999);
  }

  if(ERROR_INSERTED(9998)){
    while(true) NdbSleep_SecSleep(1);
  }

  if(ERROR_INSERTED(9997)){
    ndbrequire(false);
  }

#ifndef NDB_WIN32
  if(ERROR_INSERTED(9996)){
    simulate_error_during_shutdown= SIGSEGV;
    ndbrequire(false);
  }

  if(ERROR_INSERTED(9995)){
    simulate_error_during_shutdown= SIGSEGV;
    kill(getpid(), SIGABRT);
  }
#endif

#ifdef ERROR_INSERT
  if (signal->theData[0] == 9003)
  {
    if (MAX_RECEIVED_SIGNALS < 1024)
    {
      MAX_RECEIVED_SIGNALS = 1024;
    }
    else
    {
      MAX_RECEIVED_SIGNALS = 1 + (rand() % 128);
    }
    ndbout_c("MAX_RECEIVED_SIGNALS: %d", MAX_RECEIVED_SIGNALS);
    CLEAR_ERROR_INSERT_VALUE;
  }
#endif
}//execNDB_TAMPER()

void Cmvmi::execSET_LOGLEVELORD(Signal* signal) 
{
  SetLogLevelOrd * const llOrd = (SetLogLevelOrd *)&signal->theData[0];
  LogLevel::EventCategory category;
  Uint32 level;
  jamEntry();

  for(unsigned int i = 0; i<llOrd->noOfEntries; i++){
    category = (LogLevel::EventCategory)(llOrd->theData[i] >> 16);
    level = llOrd->theData[i] & 0xFFFF;
    
    clogLevel.setLogLevel(category, level);
  }
}//execSET_LOGLEVELORD()

void Cmvmi::execEVENT_REP(Signal* signal) 
{
  //-----------------------------------------------------------------------
  // This message is sent to report any types of events in NDB.
  // Based on the log level they will be either ignored or
  // reported. Currently they are printed, but they will be
  // transferred to the management server for further distribution
  // to the graphical management interface.
  //-----------------------------------------------------------------------
  EventReport * const eventReport = (EventReport *)&signal->theData[0]; 
  Ndb_logevent_type eventType = eventReport->getEventType();
  Uint32 nodeId= eventReport->getNodeId();
  if (nodeId == 0)
  {
    nodeId= refToNode(signal->getSendersBlockRef());

    if (nodeId == 0)
    {
      /* Event reporter supplied no node id,
       * assume it was local
       */
      nodeId= getOwnNodeId();
    }

    eventReport->setNodeId(nodeId);
  }

  jamEntry();
  
  /**
   * If entry is not found
   */
  Uint32 threshold;
  LogLevel::EventCategory eventCategory;
  Logger::LoggerLevel severity;  
  EventLoggerBase::EventTextFunction textF;
  if (EventLoggerBase::event_lookup(eventType,eventCategory,threshold,severity,textF))
    return;
  
  SubscriberPtr ptr;
  for(subscribers.first(ptr); ptr.i != RNIL; subscribers.next(ptr)){
    if(ptr.p->logLevel.getLogLevel(eventCategory) < threshold){
      continue;
    }
    
    sendSignal(ptr.p->blockRef, GSN_EVENT_REP, signal, signal->length(), JBB);
  }
  
  if(clogLevel.getLogLevel(eventCategory) < threshold){
    return;
  }

  // Print the event info
  g_eventLogger->log(eventReport->getEventType(), 
                     signal->theData, signal->getLength(), 0, 0);
  
  return;
}//execEVENT_REP()

void
Cmvmi::execEVENT_SUBSCRIBE_REQ(Signal * signal){
  EventSubscribeReq * subReq = (EventSubscribeReq *)&signal->theData[0];
  Uint32 senderRef = signal->getSendersBlockRef();
  SubscriberPtr ptr;
  jamEntry();
  DBUG_ENTER("Cmvmi::execEVENT_SUBSCRIBE_REQ");

  /**
   * Search for subcription
   */
  for(subscribers.first(ptr); ptr.i != RNIL; subscribers.next(ptr)){
    if(ptr.p->blockRef == subReq->blockRef)
      break;
  }
  
  if(ptr.i == RNIL){
    /**
     * Create a new one
     */
    if(subscribers.seize(ptr) == false){
      sendSignal(senderRef, GSN_EVENT_SUBSCRIBE_REF, signal, 1, JBB);
      return;
    }
    ptr.p->logLevel.clear();
    ptr.p->blockRef = subReq->blockRef;    
  }
  
  if(subReq->noOfEntries == 0){
    /**
     * Cancel subscription
     */
    subscribers.release(ptr.i);
  } else {
    /**
     * Update subscription
     */
    LogLevel::EventCategory category;
    Uint32 level = 0;
    for(Uint32 i = 0; i<subReq->noOfEntries; i++){
      category = (LogLevel::EventCategory)(subReq->theData[i] >> 16);
      level = subReq->theData[i] & 0xFFFF;
      ptr.p->logLevel.setLogLevel(category, level);
      DBUG_PRINT("info",("entry %d: level=%d, category= %d", i, level, category));
    }
  }
  
  signal->theData[0] = ptr.i;
  sendSignal(senderRef, GSN_EVENT_SUBSCRIBE_CONF, signal, 1, JBB);
  DBUG_VOID_RETURN;
}

void
Cmvmi::cancelSubscription(NodeId nodeId){
  
  SubscriberPtr ptr;
  subscribers.first(ptr);
  
  while(ptr.i != RNIL){
    Uint32 i = ptr.i;
    BlockReference blockRef = ptr.p->blockRef;
    
    subscribers.next(ptr);
    
    if(refToNode(blockRef) == nodeId){
      subscribers.release(i);
    }
  }
}

void Cmvmi::sendSTTORRY(Signal* signal)
{
  jam();
  signal->theData[3] = 1;
  signal->theData[4] = 3;
  signal->theData[5] = 8;
  signal->theData[6] = 255;
  sendSignal(NDBCNTR_REF, GSN_STTORRY, signal, 7, JBB);
}//Cmvmi::sendSTTORRY


static Uint32 f_accpages = 0;
extern Uint32 compute_acc_32kpages(const ndb_mgm_configuration_iterator * p);

void 
Cmvmi::execREAD_CONFIG_REQ(Signal* signal)
{
  jamEntry();

  const ReadConfigReq * req = (ReadConfigReq*)signal->getDataPtr();

  Uint32 ref = req->senderRef;
  Uint32 senderData = req->senderData;

  const ndb_mgm_configuration_iterator * p = 
    m_ctx.m_config.getOwnConfigIterator();
  ndbrequire(p != 0);

  Uint64 page_buffer = 64*1024*1024;
  ndb_mgm_get_int64_parameter(p, CFG_DB_DISK_PAGE_BUFFER_MEMORY, &page_buffer);
  
  Uint32 pages = 0;
  pages += Uint32(page_buffer / GLOBAL_PAGE_SIZE); // in pages
  Uint32 restore_instances = 1;
  if (isNdbMtLqh())
    restore_instances = getLqhWorkers();
  pages += LCP_RESTORE_BUFFER * restore_instances;
  m_global_page_pool.setSize(pages + 64, true);

  {
    void* ptr = m_ctx.m_mm.get_memroot();
    m_shared_page_pool.set((GlobalPage*)ptr, ~0);
  }

  f_accpages = compute_acc_32kpages(p);

  ReadConfigConf * conf = (ReadConfigConf*)signal->getDataPtrSend();
  conf->senderRef = reference();
  conf->senderData = senderData;
  sendSignal(ref, GSN_READ_CONFIG_CONF, signal, 
	     ReadConfigConf::SignalLength, JBB);

  c_memusage_report_frequency = 0;
  ndb_mgm_get_int_parameter(p, CFG_DB_MEMREPORT_FREQUENCY, 
                            &c_memusage_report_frequency);
}

void Cmvmi::execSTTOR(Signal* signal)
{
  Uint32 theStartPhase  = signal->theData[1];

  jamEntry();
  if (theStartPhase == 1){
    jam();

    if(m_ctx.m_config.lockPagesInMainMemory() == 1)
    {
      jam();
      /**
       * Notify watchdog that we're locking memory...
       *   which can be equally "heavy" as allocating it
       */
      refresh_watch_dog(9);
      int res = NdbMem_MemLockAll(0);
      if (res != 0)
      {
        char buf[100];
        BaseString::snprintf(buf, sizeof(buf), 
                             "Failed to memlock pages, error: %d (%s)",
                             errno, strerror(errno));
        g_eventLogger->warning("%s", buf);
        warningEvent("%s", buf);
      }
    }
    
    /**
     * Install "normal" watchdog value
     */
    {
      Uint32 db_watchdog_interval = 0;
      const ndb_mgm_configuration_iterator * p = 
        m_ctx.m_config.getOwnConfigIterator();
      ndb_mgm_get_int_parameter(p, CFG_DB_WATCHDOG_INTERVAL, 
                                &db_watchdog_interval);
      ndbrequire(db_watchdog_interval);
      update_watch_dog_timer(db_watchdog_interval);
    }

    /**
     * Start auto-mem reporting
     */
    signal->theData[0] = ZREPORT_MEMORY_USAGE;
    signal->theData[1] = 0;
    signal->theData[2] = 0;
    signal->theData[3] = 0;
    execCONTINUEB(signal);
    
    sendSTTORRY(signal);
    return;
  } else if (theStartPhase == 3) {
    jam();
    globalData.activateSendPacked = 1;
    sendSTTORRY(signal);
  } else if (theStartPhase == 8){
#ifdef ERROR_INSERT
    if (ERROR_INSERTED(9004))
    {
      Uint32 len = signal->getLength();
      Uint32 db = c_dbNodes.find(0);
      if (db == getOwnNodeId())
        db = c_dbNodes.find(db);
      Uint32 i = c_error_9000_nodes_mask.find(0);
      Uint32 tmp[25];
      memcpy(tmp, signal->theData, sizeof(tmp));
      signal->theData[0] = i;
      sendSignal(calcQmgrBlockRef(db),GSN_API_FAILREQ, signal, 1, JBA);
      ndbout_c("stopping %u using %u", i, db);
      CLEAR_ERROR_INSERT_VALUE;
      memcpy(signal->theData, tmp, sizeof(tmp));
      sendSignalWithDelay(reference(), GSN_STTOR,
                          signal, 100, len);
      return;
    }
#endif
    globalData.theStartLevel = NodeState::SL_STARTED;
    sendSTTORRY(signal);
  }
}

void Cmvmi::execCLOSE_COMREQ(Signal* signal)
{
  // Close communication with the node and halt input/output from 
  // other blocks than QMGR
  
  CloseComReqConf * const closeCom = (CloseComReqConf *)&signal->theData[0];

  const BlockReference userRef = closeCom->xxxBlockRef;
  Uint32 requestType = closeCom->requestType;
  Uint32 failNo = closeCom->failNo;
//  Uint32 noOfNodes = closeCom->noOfNodes;
  
  jamEntry();
  for (unsigned i = 0; i < MAX_NODES; i++)
  {
    if(NodeBitmask::get(closeCom->theNodes, i))
    {
      jam();

      //-----------------------------------------------------
      // Report that the connection to the node is closed
      //-----------------------------------------------------
      signal->theData[0] = NDB_LE_CommunicationClosed;
      signal->theData[1] = i;
      sendSignal(CMVMI_REF, GSN_EVENT_REP, signal, 2, JBB);
      
      globalTransporterRegistry.setIOState(i, HaltIO);
      globalTransporterRegistry.do_disconnect(i);
    }
  }
  if (requestType != CloseComReqConf::RT_NO_REPLY)
  {
    ndbassert((requestType == CloseComReqConf::RT_API_FAILURE) ||
              ((requestType == CloseComReqConf::RT_NODE_FAILURE) &&
               (failNo != 0)));
    jam();
    CloseComReqConf* closeComConf = (CloseComReqConf *)signal->getDataPtrSend();
    closeComConf->xxxBlockRef = userRef;
    closeComConf->requestType = requestType;
    closeComConf->failNo = failNo;

    /* Note assumption that noOfNodes and theNodes
     * bitmap is not trampled above 
     * signals received from the remote node.
     */
    sendSignal(QMGR_REF, GSN_CLOSE_COMCONF, signal, 19, JBA);
  }
}

void Cmvmi::execOPEN_COMREQ(Signal* signal)
{
  // Connect to the specifed NDB node, only QMGR allowed communication 
  // so far with the node

  const BlockReference userRef = signal->theData[0];
  Uint32 tStartingNode = signal->theData[1];
  Uint32 tData2 = signal->theData[2];
  jamEntry();

  const Uint32 len = signal->getLength();
  if(len == 2)
  {
#ifdef ERROR_INSERT
    if (! ((ERROR_INSERTED(9000) || ERROR_INSERTED(9002)) 
	   && c_error_9000_nodes_mask.get(tStartingNode)))
#endif
    {
      globalTransporterRegistry.do_connect(tStartingNode);
      globalTransporterRegistry.setIOState(tStartingNode, HaltIO);
      
      //-----------------------------------------------------
      // Report that the connection to the node is opened
      //-----------------------------------------------------
      signal->theData[0] = NDB_LE_CommunicationOpened;
      signal->theData[1] = tStartingNode;
      sendSignal(CMVMI_REF, GSN_EVENT_REP, signal, 2, JBB);
      //-----------------------------------------------------
    }
  } else {
    for(unsigned int i = 1; i < MAX_NODES; i++ ) 
    {
      jam();
      if (i != getOwnNodeId() && getNodeInfo(i).m_type == tData2)
      {
	jam();

#ifdef ERROR_INSERT
	if ((ERROR_INSERTED(9000) || ERROR_INSERTED(9002))
	    && c_error_9000_nodes_mask.get(i))
	  continue;
#endif
	globalTransporterRegistry.do_connect(i);
	globalTransporterRegistry.setIOState(i, HaltIO);
	
	signal->theData[0] = NDB_LE_CommunicationOpened;
	signal->theData[1] = i;
	sendSignal(CMVMI_REF, GSN_EVENT_REP, signal, 2, JBB);
      }
    }
  }
  
  if (userRef != 0) {
    jam(); 
    signal->theData[0] = tStartingNode;
    signal->theData[1] = tData2;
    sendSignal(userRef, GSN_OPEN_COMCONF, signal, len - 1,JBA);
  }
}

void Cmvmi::execENABLE_COMREQ(Signal* signal)
{
  jamEntry();
  const EnableComReq *enableComReq = (const EnableComReq *)signal->getDataPtr();

  /* Need to copy out signal data to not clobber it with sendSignal(). */
  Uint32 senderRef = enableComReq->m_senderRef;
  Uint32 senderData = enableComReq->m_senderData;
  Uint32 nodes[NodeBitmask::Size];
  MEMCOPY_NO_WORDS(nodes, enableComReq->m_nodeIds, NodeBitmask::Size);

  /* Enable communication with all our NDB blocks to these nodes. */
  Uint32 search_from = 0;
  for (;;)
  {
    Uint32 tStartingNode = NodeBitmask::find(nodes, search_from);
    if (tStartingNode == NodeBitmask::NotFound)
      break;
    search_from = tStartingNode + 1;

    globalTransporterRegistry.setIOState(tStartingNode, NoHalt);
    setNodeInfo(tStartingNode).m_connected = true;

    //-----------------------------------------------------
    // Report that the version of the node
    //-----------------------------------------------------
    signal->theData[0] = NDB_LE_ConnectedApiVersion;
    signal->theData[1] = tStartingNode;
    signal->theData[2] = getNodeInfo(tStartingNode).m_version;
    signal->theData[3] = getNodeInfo(tStartingNode).m_mysql_version;

    sendSignal(CMVMI_REF, GSN_EVENT_REP, signal, 4, JBB);
    //-----------------------------------------------------
  }

  EnableComConf *enableComConf = (EnableComConf *)signal->getDataPtrSend();
  enableComConf->m_senderRef = reference();
  enableComConf->m_senderData = senderData;
  MEMCOPY_NO_WORDS(enableComConf->m_nodeIds, nodes, NodeBitmask::Size);
  sendSignal(senderRef, GSN_ENABLE_COMCONF, signal,
             EnableComConf::SignalLength, JBA);
}

void Cmvmi::execDISCONNECT_REP(Signal *signal)
{
  const DisconnectRep * const rep = (DisconnectRep *)&signal->theData[0];
  const Uint32 hostId = rep->nodeId;

  jamEntry();

  setNodeInfo(hostId).m_connected = false;
  setNodeInfo(hostId).m_connectCount++;
  const NodeInfo::NodeType type = getNodeInfo(hostId).getType();
  ndbrequire(type != NodeInfo::INVALID);

  sendSignal(QMGR_REF, GSN_DISCONNECT_REP, signal, 
             DisconnectRep::SignalLength, JBA);
  
  cancelSubscription(hostId);

  signal->theData[0] = NDB_LE_Disconnected;
  signal->theData[1] = hostId;
  sendSignal(CMVMI_REF, GSN_EVENT_REP, signal, 2, JBB);
}
 
void Cmvmi::execCONNECT_REP(Signal *signal){
  const Uint32 hostId = signal->theData[0];
  jamEntry();
  
  const NodeInfo::NodeType type = (NodeInfo::NodeType)getNodeInfo(hostId).m_type;
  ndbrequire(type != NodeInfo::INVALID);
  globalData.m_nodeInfo[hostId].m_version = 0;
  globalData.m_nodeInfo[hostId].m_mysql_version = 0;
  
  /**
   * Inform QMGR that client has connected
   */
  signal->theData[0] = hostId;
  sendSignal(QMGR_REF, GSN_CONNECT_REP, signal, 1, JBA);

  /* Automatically subscribe events for MGM nodes.
   */
  if(type == NodeInfo::MGM)
  {
    jam();
    globalTransporterRegistry.setIOState(hostId, NoHalt);
  }

  //------------------------------------------
  // Also report this event to the Event handler
  //------------------------------------------
  signal->theData[0] = NDB_LE_Connected;
  signal->theData[1] = hostId;
  signal->header.theLength = 2;
  
  execEVENT_REP(signal);
}

#ifdef VM_TRACE
void
modifySignalLogger(bool allBlocks, BlockNumber bno, 
                   TestOrd::Command cmd, 
                   TestOrd::SignalLoggerSpecification spec){
  SignalLoggerManager::LogMode logMode;

  /**
   * Mapping between SignalLoggerManager::LogMode and 
   *                 TestOrd::SignalLoggerSpecification
   */
  switch(spec){
  case TestOrd::InputSignals:
    logMode = SignalLoggerManager::LogIn;
    break;
  case TestOrd::OutputSignals:
    logMode = SignalLoggerManager::LogOut;
    break;
  case TestOrd::InputOutputSignals:
    logMode = SignalLoggerManager::LogInOut;
    break;
  default:
    return;
    break;
  }
  
  switch(cmd){
  case TestOrd::On:
    globalSignalLoggers.logOn(allBlocks, bno, logMode);
    break;
  case TestOrd::Off:
    globalSignalLoggers.logOff(allBlocks, bno, logMode);
    break;
  case TestOrd::Toggle:
    globalSignalLoggers.logToggle(allBlocks, bno, logMode);
    break;
  case TestOrd::KeepUnchanged:
    // Do nothing
    break;
  }
  globalSignalLoggers.flushSignalLog();
}
#endif

void
Cmvmi::execTEST_ORD(Signal * signal){
  jamEntry();
  
#ifdef VM_TRACE
  TestOrd * const testOrd = (TestOrd *)&signal->theData[0];

  TestOrd::Command cmd;

  {
    /**
     * Process Trace command
     */
    TestOrd::TraceSpecification traceSpec;

    testOrd->getTraceCommand(cmd, traceSpec);
    unsigned long traceVal = traceSpec;
    unsigned long currentTraceVal = globalSignalLoggers.getTrace();
    switch(cmd){
    case TestOrd::On:
      currentTraceVal |= traceVal;
      break;
    case TestOrd::Off:
      currentTraceVal &= (~traceVal);
      break;
    case TestOrd::Toggle:
      currentTraceVal ^= traceVal;
      break;
    case TestOrd::KeepUnchanged:
      // Do nothing
      break;
    }
    globalSignalLoggers.setTrace(currentTraceVal);
  }
  
  {
    /**
     * Process Log command
     */
    TestOrd::SignalLoggerSpecification logSpec;
    BlockNumber bno;
    unsigned int loggers = testOrd->getNoOfSignalLoggerCommands();
    
    if(loggers == (unsigned)~0){ // Apply command to all blocks
      testOrd->getSignalLoggerCommand(0, bno, cmd, logSpec);
      modifySignalLogger(true, bno, cmd, logSpec);
    } else {
      for(unsigned int i = 0; i<loggers; i++){
        testOrd->getSignalLoggerCommand(i, bno, cmd, logSpec);
        modifySignalLogger(false, bno, cmd, logSpec);
      }
    }
  }

  {
    /**
     * Process test command
     */
    testOrd->getTestCommand(cmd);
    switch(cmd){
    case TestOrd::On:{
      SET_GLOBAL_TEST_ON;
    }
    break;
    case TestOrd::Off:{
      SET_GLOBAL_TEST_OFF;
    }
    break;
    case TestOrd::Toggle:{
      TOGGLE_GLOBAL_TEST_FLAG;
    }
    break;
    case TestOrd::KeepUnchanged:
      // Do nothing
      break;
    }
    globalSignalLoggers.flushSignalLog();
  }

#endif
}

void Cmvmi::execSTOP_ORD(Signal* signal) 
{
  jamEntry();
  globalData.theRestartFlag = perform_stop;
}//execSTOP_ORD()

void
Cmvmi::execSTART_ORD(Signal* signal) {
  StartOrd * const startOrd = (StartOrd *)&signal->theData[0];
  jamEntry();
  
  Uint32 tmp = startOrd->restartInfo;
  if(StopReq::getPerformRestart(tmp)){
    jam();
    /**
     *
     */
    NdbRestartType type = NRT_Default;
    if(StopReq::getNoStart(tmp) && StopReq::getInitialStart(tmp))
      type = NRT_NoStart_InitialStart;
    if(StopReq::getNoStart(tmp) && !StopReq::getInitialStart(tmp))
      type = NRT_NoStart_Restart;
    if(!StopReq::getNoStart(tmp) && StopReq::getInitialStart(tmp))
      type = NRT_DoStart_InitialStart;
    if(!StopReq::getNoStart(tmp)&&!StopReq::getInitialStart(tmp))
      type = NRT_DoStart_Restart;
    NdbShutdown(NST_Restart, type);
  }

  if(globalData.theRestartFlag == system_started){
    jam();
    /**
     * START_ORD received when already started(ignored)
     */
    //ndbout << "START_ORD received when already started(ignored)" << endl;
    return;
  }
  
  if(globalData.theRestartFlag == perform_stop){
    jam();
    /**
     * START_ORD received when stopping(ignored)
     */
    //ndbout << "START_ORD received when stopping(ignored)" << endl;
    return;
  }
  
  if(globalData.theStartLevel == NodeState::SL_NOTHING)
  {
    jam();

    for(unsigned int i = 1; i < MAX_NODES; i++ )
    {
      if (getNodeInfo(i).m_type == NodeInfo::MGM)
      {
        jam();
        globalTransporterRegistry.do_connect(i);
      }
    }

    globalData.theStartLevel = NodeState::SL_CMVMI;
    sendSignal(QMGR_REF, GSN_START_ORD, signal, 1, JBA);
    return ;
  }
  
  if(globalData.theStartLevel == NodeState::SL_CMVMI)
  {
    jam();

    if(m_ctx.m_config.lockPagesInMainMemory() == 2)
    {
      int res = NdbMem_MemLockAll(1);
      if(res != 0)
      {
        char buf[100];
        BaseString::snprintf(buf, sizeof(buf),
                             "Failed to memlock pages, error: %d (%s)",
                             errno, strerror(errno));
        g_eventLogger->warning("%s", buf);
        warningEvent("%s", buf);
      }
      else
      {
        g_eventLogger->info("Locked future allocations");
      }
    }
    
    globalData.theStartLevel  = NodeState::SL_STARTING;
    globalData.theRestartFlag = system_started;
    /**
     * StartLevel 1
     *
     * Do Restart
     */
    
    // Disconnect all nodes as part of the system restart. 
    // We need to ensure that we are starting up
    // without any connected nodes.   
    for(unsigned int i = 1; i < MAX_NODES; i++ )
    {
      if (i != getOwnNodeId() && getNodeInfo(i).m_type != NodeInfo::MGM)
      {
        globalTransporterRegistry.do_disconnect(i);
        globalTransporterRegistry.setIOState(i, HaltIO);
      }
    }

    CRASH_INSERTION(9994);
    
    /**
     * Start running startphases
     */
    sendSignal(NDBCNTR_REF, GSN_START_ORD, signal, 1, JBA);  
    return;
  }
}//execSTART_ORD()

void Cmvmi::execTAMPER_ORD(Signal* signal) 
{
  jamEntry();
  // TODO We should maybe introduce a CONF and REF signal
  // to be able to indicate if we really introduced an error.
#ifdef ERROR_INSERT
  TamperOrd* const tamperOrd = (TamperOrd*)&signal->theData[0];
  signal->theData[2] = 0;
  signal->theData[1] = tamperOrd->errorNo;
  signal->theData[0] = 5;
  sendSignal(DBDIH_REF, GSN_DIHNDBTAMPER, signal, 3,JBB);
#endif

}//execTAMPER_ORD()

#ifdef VM_TRACE
class RefSignalTest {
public:
  enum ErrorCode {
    OK = 0,
    NF_FakeErrorREF = 7
  };
  Uint32 senderRef;
  Uint32 senderData;
  Uint32 errorCode;
};
#endif


static int iii;

static
int
recurse(char * buf, int loops, int arg){
  char * tmp = (char*)alloca(arg);
  printf("tmp = %p\n", tmp);
  for(iii = 0; iii<arg; iii += 1024){
    tmp[iii] = (iii % 23 + (arg & iii));
  }
  
  if(loops == 0)
    return tmp[345];
  else
    return tmp[arg/loops] + recurse(tmp, loops - 1, arg);
}

#define check_block(block,val) \
(((val) >= DumpStateOrd::_ ## block ## Min) && ((val) <= DumpStateOrd::_ ## block ## Max))

void
Cmvmi::execDUMP_STATE_ORD(Signal* signal)
{
  Uint32 val = signal->theData[0];
  if (val >= DumpStateOrd::OneBlockOnly)
  {
    if (check_block(Backup, val))
    {
      sendSignal(BACKUP_REF, GSN_DUMP_STATE_ORD, signal, signal->length(), JBB);
    }
    else if (check_block(TC, val))
    {
    }
    else if (check_block(LQH, val))
    {
      sendSignal(DBLQH_REF, GSN_DUMP_STATE_ORD, signal, signal->length(), JBB);
    }
    return;
  }

  sendSignal(QMGR_REF, GSN_DUMP_STATE_ORD,    signal, signal->length(), JBB);
  sendSignal(NDBCNTR_REF, GSN_DUMP_STATE_ORD, signal, signal->length(), JBB);
  sendSignal(DBTC_REF, GSN_DUMP_STATE_ORD,    signal, signal->length(), JBB);
  sendSignal(DBDIH_REF, GSN_DUMP_STATE_ORD,   signal, signal->length(), JBB);
  sendSignal(DBDICT_REF, GSN_DUMP_STATE_ORD,  signal, signal->length(), JBB);
  sendSignal(DBLQH_REF, GSN_DUMP_STATE_ORD,   signal, signal->length(), JBB);
  sendSignal(DBTUP_REF, GSN_DUMP_STATE_ORD,   signal, signal->length(), JBB);
  sendSignal(DBACC_REF, GSN_DUMP_STATE_ORD,   signal, signal->length(), JBB);
  sendSignal(NDBFS_REF, GSN_DUMP_STATE_ORD,   signal, signal->length(), JBB);
  sendSignal(BACKUP_REF, GSN_DUMP_STATE_ORD,  signal, signal->length(), JBB);
  sendSignal(DBUTIL_REF, GSN_DUMP_STATE_ORD,  signal, signal->length(), JBB);
  sendSignal(SUMA_REF, GSN_DUMP_STATE_ORD,    signal, signal->length(), JBB);
  sendSignal(TRIX_REF, GSN_DUMP_STATE_ORD,    signal, signal->length(), JBB);
  sendSignal(DBTUX_REF, GSN_DUMP_STATE_ORD,   signal, signal->length(), JBB);
  sendSignal(LGMAN_REF, GSN_DUMP_STATE_ORD,   signal, signal->length(), JBB);
  sendSignal(TSMAN_REF, GSN_DUMP_STATE_ORD,   signal, signal->length(), JBB);
  sendSignal(PGMAN_REF, GSN_DUMP_STATE_ORD,   signal, signal->length(), JBB);
  sendSignal(DBINFO_REF,GSN_DUMP_STATE_ORD,   signal, signal->length(), JBB);

  /**
   *
   * Here I can dump CMVMI state if needed
   */
  if(signal->theData[0] == 13){
#if 0
    int loop = 100;
    int len = (10*1024*1024);
    if(signal->getLength() > 1)
      loop = signal->theData[1];
    if(signal->getLength() > 2)
      len = signal->theData[2];
    
    ndbout_c("recurse(%d loop, %dkb per recurse)", loop, len/1024);
    int a = recurse(0, loop, len);
    ndbout_c("after...%d", a);
#endif
  }

  DumpStateOrd * const & dumpState = (DumpStateOrd *)&signal->theData[0];
  Uint32 arg = dumpState->args[0];
  if (arg == DumpStateOrd::CmvmiDumpConnections){
    for(unsigned int i = 1; i < MAX_NODES; i++ ){
      const char* nodeTypeStr = "";
      switch(getNodeInfo(i).m_type){
      case NodeInfo::DB:
	nodeTypeStr = "DB";
	break;
      case NodeInfo::API:
	nodeTypeStr = "API";
	break;
      case NodeInfo::MGM:
	nodeTypeStr = "MGM";
	break;
      case NodeInfo::INVALID:
	nodeTypeStr = 0;
	break;
      default:
	nodeTypeStr = "<UNKNOWN>";
      }

      if(nodeTypeStr == 0)
	continue;

      infoEvent("Connection to %d (%s) %s", 
                i, 
                nodeTypeStr,
                globalTransporterRegistry.getPerformStateString(i));
    }
  }
  
  if (arg == DumpStateOrd::CmvmiDumpSubscriptions)
  {
    SubscriberPtr ptr;
    subscribers.first(ptr);  
    g_eventLogger->info("List subscriptions:");
    while(ptr.i != RNIL)
    {
      g_eventLogger->info("Subscription: %u, nodeId: %u, ref: 0x%x",
                          ptr.i,  refToNode(ptr.p->blockRef), ptr.p->blockRef);
      for(Uint32 i = 0; i < LogLevel::LOGLEVEL_CATEGORIES; i++)
      {
        Uint32 level = ptr.p->logLevel.getLogLevel((LogLevel::EventCategory)i);
        g_eventLogger->info("Category %u Level %u", i, level);
      }
      subscribers.next(ptr);
    }
  }

  if (arg == DumpStateOrd::CmvmiDumpLongSignalMemory){
    infoEvent("Cmvmi: g_sectionSegmentPool size: %d free: %d",
	      g_sectionSegmentPool.getSize(),
	      g_sectionSegmentPool.getNoOfFree());
  }

  if (dumpState->args[0] == DumpStateOrd::DumpPageMemory)
  {
    const Uint32 len = signal->getLength();
    if (len == 1)
    {
      // Start dumping resource limits
      signal->theData[1] = 0;
      signal->theData[2] = ~0;
      sendSignal(reference(), GSN_DUMP_STATE_ORD, signal, 3, JBB);

      // Dump data and index memory
      reportDMUsage(signal, 0);
      reportIMUsage(signal, 0);
      return;
    }

    if (len == 2)
    {
      // Dump data and index memory to specific ref
      Uint32 result_ref = signal->theData[1];
      reportDMUsage(signal, 0, result_ref);
      reportIMUsage(signal, 0, result_ref);
      return;
    }

    Uint32 id = signal->theData[1];
    Resource_limit rl;
    if (m_ctx.m_mm.get_resource_limit(id, rl))
    {
      if (rl.m_min || rl.m_curr || rl.m_max)
      {
        infoEvent("Resource %d min: %d max: %d curr: %d",
                  id, rl.m_min, rl.m_max, rl.m_curr);
      }

      signal->theData[0] = 1000;
      signal->theData[1] = id+1;
      signal->theData[2] = ~0;
      sendSignal(reference(), GSN_DUMP_STATE_ORD, signal, 3, JBB);
    }
    return;
  }
  if (arg == DumpStateOrd::CmvmiSchedulerExecutionTimer)
  {
    Uint32 exec_time = signal->theData[1];
    globalEmulatorData.theConfiguration->schedulerExecutionTimer(exec_time);
  }
  if (arg == DumpStateOrd::CmvmiSchedulerSpinTimer)
  {
    Uint32 spin_time = signal->theData[1];
    globalEmulatorData.theConfiguration->schedulerSpinTimer(spin_time);
  } 
  if (arg == DumpStateOrd::CmvmiRealtimeScheduler)
  {
    bool realtime_on = signal->theData[1];
    globalEmulatorData.theConfiguration->realtimeScheduler(realtime_on);
  }
  if (arg == DumpStateOrd::CmvmiExecuteLockCPU)
  {
    Uint32 exec_cpu_id = signal->theData[1];
    globalEmulatorData.theConfiguration->executeLockCPU(exec_cpu_id);
  }
  if (arg == DumpStateOrd::CmvmiMaintLockCPU)
  {
    Uint32 maint_cpu_id = signal->theData[1];
    globalEmulatorData.theConfiguration->maintLockCPU(maint_cpu_id);
  }
  if (arg == DumpStateOrd::CmvmiSetRestartOnErrorInsert)
  {
    if(signal->getLength() == 1)
    {
      Uint32 val = (Uint32)NRT_NoStart_Restart;
      const ndb_mgm_configuration_iterator * p = 
	m_ctx.m_config.getOwnConfigIterator();
      ndbrequire(p != 0);
      
      if(!ndb_mgm_get_int_parameter(p, CFG_DB_STOP_ON_ERROR_INSERT, &val))
      {
        m_ctx.m_config.setRestartOnErrorInsert(val);
      }
    }
    else
    {
      m_ctx.m_config.setRestartOnErrorInsert(signal->theData[1]);
    }
  }

  if (arg == DumpStateOrd::CmvmiTestLongSigWithDelay) {
    unsigned i;
    Uint32 testType = dumpState->args[1];
    Uint32 loopCount = dumpState->args[2];
    Uint32 print = dumpState->args[3];
    const unsigned len0 = 11;
    const unsigned len1 = 123;
    Uint32 sec0[len0];
    Uint32 sec1[len1];
    for (i = 0; i < len0; i++)
      sec0[i] = i;
    for (i = 0; i < len1; i++)
      sec1[i] = 16 * i;
    Uint32* sig = signal->getDataPtrSend();
    sig[0] = reference();
    sig[1] = testType;
    sig[2] = 0;
    sig[3] = print;
    sig[4] = loopCount;
    sig[5] = len0;
    sig[6] = len1;
    sig[7] = 0;
    LinearSectionPtr ptr[3];
    ptr[0].p = sec0;
    ptr[0].sz = len0;
    ptr[1].p = sec1;
    ptr[1].sz = len1;
    sendSignal(reference(), GSN_TESTSIG, signal, 8, JBB, ptr, 2);
  }

  if (arg == DumpStateOrd::CmvmiTestLongSig)
  {
    /* Forward as GSN_TESTSIG to self */
    Uint32 numArgs= signal->length() - 1;
    memmove(signal->getDataPtrSend(), 
            signal->getDataPtrSend() + 1, 
            numArgs << 2);
    sendSignal(reference(), GSN_TESTSIG, signal, numArgs, JBB);
  }

#ifdef ERROR_INSERT
  if (arg == 9000 || arg == 9002)
  {
    SET_ERROR_INSERT_VALUE(arg);
    for (Uint32 i = 1; i<signal->getLength(); i++)
      c_error_9000_nodes_mask.set(signal->theData[i]);
  }
  
  if (arg == 9001)
  {
    CLEAR_ERROR_INSERT_VALUE;
    if (signal->getLength() == 1 || signal->theData[1])
    {
      for (Uint32 i = 0; i<MAX_NODES; i++)
      {
	if (c_error_9000_nodes_mask.get(i))
	{
	  signal->theData[0] = 0;
	  signal->theData[1] = i;
	  EXECUTE_DIRECT(CMVMI, GSN_OPEN_COMREQ, signal, 2);
	}
      }
    }
    c_error_9000_nodes_mask.clear();
  }

  if (arg == 9004 && signal->getLength() == 2)
  {
    SET_ERROR_INSERT_VALUE(9004);
    c_error_9000_nodes_mask.clear();
    c_error_9000_nodes_mask.set(signal->theData[1]);
  }

  if (arg == 9004 && signal->getLength() == 2)
  {
    SET_ERROR_INSERT_VALUE(9004);
    c_error_9000_nodes_mask.clear();
    c_error_9000_nodes_mask.set(signal->theData[1]);
  }
#endif

#ifdef VM_TRACE
#if 0
  {
    SafeCounterManager mgr(* this); mgr.setSize(1);
    SafeCounterHandle handle;

    {
      SafeCounter tmp(mgr, handle);
      tmp.init<RefSignalTest>(CMVMI, GSN_TESTSIG, /* senderData */ 13);
      tmp.setWaitingFor(3);
      ndbrequire(!tmp.done());
      ndbout_c("Allocted");
    }
    ndbrequire(!handle.done());
    {
      SafeCounter tmp(mgr, handle);
      tmp.clearWaitingFor(3);
      ndbrequire(tmp.done());
      ndbout_c("Deallocted");
    }
    ndbrequire(handle.done());
  }
#endif
#endif

  if (arg == 9999)
  {
    Uint32 delay = 1000;
    switch(signal->getLength()){
    case 1:
      break;
    case 2:
      delay = signal->theData[1];
      break;
    default:{
      Uint32 dmin = signal->theData[1];
      Uint32 dmax = signal->theData[2];
      delay = dmin + (rand() % (dmax - dmin));
      break;
    }
    }
    
    signal->theData[0] = 9999;
    if (delay == 0)
    {
      execNDB_TAMPER(signal);
    }
    else if (delay < 10)
    {
      sendSignal(reference(), GSN_NDB_TAMPER, signal, 1, JBB);
    }
    else
    {
      sendSignalWithDelay(reference(), GSN_NDB_TAMPER, signal, delay, 1);
    }
  }
}//Cmvmi::execDUMP_STATE_ORD()


void Cmvmi::execDBINFO_SCANREQ(Signal *signal)
{
  DbinfoScanReq req= *(DbinfoScanReq*)signal->theData;
  const Ndbinfo::ScanCursor* cursor =
    (Ndbinfo::ScanCursor*)DbinfoScan::getCursorPtr(&req);
  Ndbinfo::Ratelimit rl;

  jamEntry();

  switch(req.tableId){
  case Ndbinfo::TRANSPORTERS_TABLEID:
  {
    jam();
    Uint32 rnode = cursor->data[0];
    if (rnode == 0)
      rnode++; // Skip node 0

    while(rnode < MAX_NODES)
    {
      switch(getNodeInfo(rnode).m_type)
      {
      default:
      {
        jam();
        Ndbinfo::Row row(signal, req);
        row.write_uint32(getOwnNodeId()); // Node id
        row.write_uint32(rnode); // Remote node id
        row.write_uint32(globalTransporterRegistry.getPerformState(rnode)); // State
        ndbinfo_send_row(signal, req, row, rl);
       break;
      }

      case NodeInfo::INVALID:
        jam();
       break;
      }

      rnode++;
      if (rl.need_break(req))
      {
        jam();
        ndbinfo_send_scan_break(signal, req, rl, rnode);
        return;
      }
    }
    break;
  }

  case Ndbinfo::RESOURCES_TABLEID:
  {
    jam();
    Uint32 resource_id = cursor->data[0];
    Resource_limit resource_limit;

    while(m_ctx.m_mm.get_resource_limit(resource_id, resource_limit))
    {
      jam();
      Ndbinfo::Row row(signal, req);
      row.write_uint32(getOwnNodeId()); // Node id
      row.write_uint32(resource_id);

      row.write_uint32(resource_limit.m_min);
      row.write_uint32(resource_limit.m_curr);
      row.write_uint32(resource_limit.m_max);
      row.write_uint32(0); //TODO
      ndbinfo_send_row(signal, req, row, rl);
      resource_id++;

      if (rl.need_break(req))
      {
        jam();
        ndbinfo_send_scan_break(signal, req, rl, resource_id);
        return;
      }
    }
    break;
  }

  case Ndbinfo::NODES_TABLEID:
  {
    jam();
    const NodeState& nodeState = getNodeState();
    const Uint32 start_level = nodeState.startLevel;
    const NDB_TICKS uptime = (NdbTick_CurrentMillisecond()/1000) - m_start_time;

    Ndbinfo::Row row(signal, req);
    row.write_uint32(getOwnNodeId()); // Node id

    row.write_uint64(uptime);         // seconds
    row.write_uint32(start_level);
    row.write_uint32(start_level == NodeState::SL_STARTING ?
                     nodeState.starting.startPhase : 0);
    ndbinfo_send_row(signal, req, row, rl);
    break;
  }

  case Ndbinfo::POOLS_TABLEID:
  {
    jam();

    Resource_limit res_limit;
    m_ctx.m_mm.get_resource_limit(RG_DATAMEM, res_limit);

    const Uint32 tup_pages_used = res_limit.m_curr - f_accpages;
    const Uint32 tup_pages_total = res_limit.m_min - f_accpages;

    Ndbinfo::pool_entry pools[] =
    {
      { "Data memory",
        tup_pages_used,
        tup_pages_total,
        sizeof(GlobalPage),
        0,
        { CFG_DB_DATA_MEM,0,0,0 }},
      { NULL, 0,0,0,0,{ 0,0,0,0 }}
    };

    static const size_t num_config_params =
      sizeof(pools[0].config_params)/sizeof(pools[0].config_params[0]);
    Uint32 pool = cursor->data[0];
    BlockNumber bn = blockToMain(number());
    while(pools[pool].poolname)
    {
      jam();
      Ndbinfo::Row row(signal, req);
      row.write_uint32(getOwnNodeId());
      row.write_uint32(bn);           // block number
      row.write_uint32(instance());   // block instance
      row.write_string(pools[pool].poolname);

      row.write_uint64(pools[pool].used);
      row.write_uint64(pools[pool].total);
      row.write_uint64(pools[pool].used_hi);
      row.write_uint64(pools[pool].entry_size);
      for (size_t i = 0; i < num_config_params; i++)
        row.write_uint32(pools[pool].config_params[i]);
      ndbinfo_send_row(signal, req, row, rl);
      pool++;
      if (rl.need_break(req))
      {
        jam();
        ndbinfo_send_scan_break(signal, req, rl, pool);
        return;
      }
    }
  }

  default:
    break;
  }

  ndbinfo_send_scan_conf(signal, req, rl);
}


void
Cmvmi::execNODE_START_REP(Signal* signal)
{
#ifdef ERROR_INSERT
  if (ERROR_INSERTED(9002) && signal->theData[0] == getOwnNodeId())
  {
    signal->theData[0] = 9001;
    execDUMP_STATE_ORD(signal);
  }
#endif
}

BLOCK_FUNCTIONS(Cmvmi)

void
Cmvmi::startFragmentedSend(Signal* signal,
                           Uint32 variant,
                           Uint32 numSigs,
                           NodeReceiverGroup rg)
{
  Uint32* sigData = signal->getDataPtrSend();
  const Uint32 sigLength = 6;
  const Uint32 sectionWords = 240;
  Uint32 sectionData[ sectionWords ];
  
  for (Uint32 i = 0; i < sectionWords; i++)
    sectionData[ i ] = i;
  
  const Uint32 secCount = 1; 
  LinearSectionPtr ptr[3];
  ptr[0].sz = sectionWords;
  ptr[0].p = &sectionData[0];

  for (Uint32 i = 0; i < numSigs; i++)
  {
    sigData[0] = variant;
    sigData[1] = 31;
    sigData[2] = 0;
    sigData[3] = 1; // print
    sigData[4] = 0;
    sigData[5] = sectionWords;
    
    if ((i & 1) == 0)
    {
      DEBUG("Starting linear fragmented send (" << i + 1
            << "/" << numSigs << ")");

      /* Linear send */
      /* Todo : Avoid reading from invalid stackptr in CONTINUEB */
      sendFragmentedSignal(rg,
                           GSN_TESTSIG,
                           signal,
                           sigLength,
                           JBB,
                           ptr,
                           secCount,
                           TheEmptyCallback,
                           90); // messageSize
    }
    else
    {
      /* Segmented send */
      DEBUG("Starting segmented fragmented send (" << i + 1
            << "/" << numSigs << ")");
      Ptr<SectionSegment> segPtr;
      ndbrequire(import(segPtr, sectionData, sectionWords));
      SectionHandle handle(this, segPtr.i);
      
      sendFragmentedSignal(rg,
                           GSN_TESTSIG,
                           signal,
                           sigLength,
                           JBB,
                           &handle,
                           TheEmptyCallback,
                           90); // messageSize
    }
  }
}

void
Cmvmi::testNodeFailureCleanupCallback(Signal* signal, Uint32 data, Uint32 elementsCleaned)
{
  DEBUG("testNodeFailureCleanupCallback");
  DEBUG("Data : " << data 
        << " elementsCleaned : " << elementsCleaned);

  debugPrintFragmentCounts();

  Uint32 variant = data & 0xffff;
  Uint32 testType = (data >> 16) & 0xffff;

  DEBUG("Sending trigger(" << testType 
        << ") variant " << variant 
        << " to self to cleanup any fragments that arrived "
        << "before send was cancelled");

  Uint32* sigData = signal->getDataPtrSend();
  sigData[0] = variant;
  sigData[1] = testType;
  sendSignal(reference(), GSN_TESTSIG, signal, 2, JBB);
  
  return; 
}

void 
Cmvmi::testFragmentedCleanup(Signal* signal, SectionHandle* handle, Uint32 testType, Uint32 variant)
{
  DEBUG("TestType " << testType << " variant " << variant);
  debugPrintFragmentCounts();

  /* Variants : 
   *     Local fragmented send   Multicast fragmented send
   * 0 : Immediate cleanup       Immediate cleanup
   * 1 : Continued cleanup       Immediate cleanup
   * 2 : Immediate cleanup       Continued cleanup
   * 3 : Continued cleanup       Continued cleanup
   */
  const Uint32 NUM_VARIANTS = 4;
  if (variant >= NUM_VARIANTS)
  {
    DEBUG("Unsupported variant");
    releaseSections(*handle);
    return;
  }

  /* Test from ndb_mgm with
   * <node(s)> DUMP 2605 0 30 
   * 
   * Use
   * <node(s)> DUMP 2605 0 39 to get fragment resource usage counts
   * Use
   * <node(s)> DUMP 2601 to get segment usage counts in clusterlog
   */
  if (testType == 30)
  {
    /* Send the first fragment of a fragmented signal to self
     * Receiver will allocate assembly hash entries
     * which must be freed when node failure cleanup
     * executes later
     */
    const Uint32 sectionWords = 240;
    Uint32 sectionData[ sectionWords ];

    for (Uint32 i = 0; i < sectionWords; i++)
      sectionData[ i ] = i;

    const Uint32 secCount = 1; 
    LinearSectionPtr ptr[3];
    ptr[0].sz = sectionWords;
    ptr[0].p = &sectionData[0];

    /* Send signal with testType == 31 */
    NodeReceiverGroup me(reference());
    Uint32* sigData = signal->getDataPtrSend();
    const Uint32 sigLength = 6;
    const Uint32 numPartialSigs = 4; 
    /* Not too many as CMVMI's fragInfo hash is limited size */
    // TODO : Consider making it debug-larger to get 
    // more coverage on CONTINUEB path

    for (Uint32 i = 0; i < numPartialSigs; i++)
    {
      /* Fill in messy TESTSIG format */
      sigData[0] = variant;
      sigData[1] = 31;
      sigData[2] = 0;
      sigData[3] = 0; // print
      sigData[4] = 0;
      sigData[5] = sectionWords;
      
      FragmentSendInfo fsi;
      
      DEBUG("Sending first fragment to self");
      sendFirstFragment(fsi,
                        me,
                        GSN_TESTSIG,
                        signal,
                        sigLength,
                        JBB,
                        ptr,
                        secCount,
                        90); // FragmentLength

      DEBUG("Cancelling remainder to free internal section");
      fsi.m_status = FragmentSendInfo::SendCancelled;
      sendNextLinearFragment(signal, fsi);
    };

    /* Ok, now send short signal with testType == 32
     * to trigger 'remote-side' actions in middle of
     * multiple fragment assembly
     */
    sigData[0] = variant;
    sigData[1] = 32;

    DEBUG("Sending node fail trigger to self");
    sendSignal(me, GSN_TESTSIG, signal, 2, JBB);
    return;
  }

  if (testType == 31)
  {
    /* Just release sections - execTESTSIG() has shown sections received */
    releaseSections(*handle);
    return;
  }

  if (testType == 32)
  {
    /* 'Remote side' trigger to clean up fragmented signal resources */
    BlockReference senderRef = signal->getSendersBlockRef();
    Uint32 sendingNode = refToNode(senderRef);
    
    /* Start sending some linear and fragmented responses to the
     * sender, to exercise frag-send cleanup code when we execute
     * node-failure later
     */
    DEBUG("Starting fragmented send using continueB back to self");

    NodeReceiverGroup sender(senderRef);
    startFragmentedSend(signal, variant, 6, sender);

    debugPrintFragmentCounts();

    Uint32 cbData= (((Uint32) 33) << 16) | variant;
    Callback cb = { safe_cast(&Cmvmi::testNodeFailureCleanupCallback),
                    cbData };

    Callback* cbPtr = NULL;

    bool passCallback = variant & 1;

    if (passCallback)
    {
      DEBUG("Running simBlock failure code WITH CALLBACK for node " 
            << sendingNode);
      cbPtr = &cb;
    }
    else
    {
      DEBUG("Running simBlock failure code IMMEDIATELY (no callback) for node "
            << sendingNode);
      cbPtr = &TheEmptyCallback;
    }

    Uint32 elementsCleaned = simBlockNodeFailure(signal, sendingNode, *cbPtr);
    
    DEBUG("Elements cleaned by call : " << elementsCleaned);

    debugPrintFragmentCounts();

    if (! passCallback)
    {
      DEBUG("Variant " << variant << " manually executing callback");
      /* We call the callback inline here to continue processing */
      testNodeFailureCleanupCallback(signal, 
                                     cbData,
                                     elementsCleaned);
    }

    return;
  }

  if (testType == 33)
  {
    /* Original side - receive cleanup trigger from 'remote' side
     * after node failure cleanup performed there.  We may have
     * fragments it managed to send before the cleanup completed
     * so we'll get rid of them.
     * This would not be necessary in reality as this node would
     * be failed
     */
    Uint32 sendingNode = refToNode(signal->getSendersBlockRef());
    DEBUG("Running simBlock failure code for node " << sendingNode);

    Uint32 elementsCleaned = simBlockNodeFailure(signal, sendingNode);

    DEBUG("Elements cleaned : " << elementsCleaned);

    /* Should have no fragment resources in use now */
    ndbrequire(debugPrintFragmentCounts() == 0);

    /* Now use ReceiverGroup to multicast a fragmented signal to
     * all database nodes
     */
    DEBUG("Starting to send fragmented continueB to all nodes inc. self : ");
    NodeReceiverGroup allNodes(CMVMI, c_dbNodes);
    
    unsigned nodeId = 0;
    while((nodeId = c_dbNodes.find(nodeId+1)) != BitmaskImpl::NotFound)
    {
      DEBUG("Node " << nodeId);
    }

    startFragmentedSend(signal, variant, 8, allNodes);

    debugPrintFragmentCounts();

    Uint32 cbData= (((Uint32) 34) << 16) | variant;
    Callback cb = { safe_cast(&Cmvmi::testNodeFailureCleanupCallback),
                    cbData };
    
    Callback* cbPtr = NULL;
    
    bool passCallback = variant & 2;

    if (passCallback)
    {
      DEBUG("Running simBlock failure code for self WITH CALLBACK (" 
            << getOwnNodeId() << ")");
      cbPtr= &cb;
    }
    else
    {
      DEBUG("Running simBlock failure code for self IMMEDIATELY (no callback) ("
            << getOwnNodeId() << ")");
      cbPtr= &TheEmptyCallback;
    }
    

    /* Fragmented signals being sent will have this node removed
     * from their receiver group, but will keep sending to the 
     * other node(s).
     * Other node(s) should therefore receive the complete signals.
     * We will then receive only the first fragment of each of 
     * the signals which must be removed later.
     */
    elementsCleaned = simBlockNodeFailure(signal, getOwnNodeId(), *cbPtr);

    DEBUG("Elements cleaned : " << elementsCleaned);
    
    debugPrintFragmentCounts();

    /* Callback will send a signal to self to clean up fragments that 
     * were sent to self before the send was cancelled.  
     * (Again, unnecessary in a 'real' situation
     */
    if (!passCallback)
    {
      DEBUG("Variant " << variant << " manually executing callback");

      testNodeFailureCleanupCallback(signal,
                                     cbData,
                                     elementsCleaned);
    }

    return;
  }
  
  if (testType == 34)
  {
    /* Cleanup fragments which were sent before send was cancelled. */
    Uint32 elementsCleaned = simBlockNodeFailure(signal, getOwnNodeId());
    
    DEBUG("Elements cleaned " << elementsCleaned);
    
    /* All FragInfo should be clear, may still be sending some
     * to other node(s)
     */
    debugPrintFragmentCounts();

    DEBUG("Variant " << variant << " completed.");
    
    if (++variant < NUM_VARIANTS)
    {
      DEBUG("Re-executing with variant " << variant);
      Uint32* sigData = signal->getDataPtrSend();
      sigData[0] = variant;
      sigData[1] = 30;
      sendSignal(reference(), GSN_TESTSIG, signal, 2, JBB);
    }
//    else
//    {
//      // Infinite loop to test for leaks
//       DEBUG("Back to zero");
//       Uint32* sigData = signal->getDataPtrSend();
//       sigData[0] = 0;
//       sigData[1] = 30;
//       sendSignal(reference(), GSN_TESTSIG, signal, 2, JBB);
//    }
  }
}

static Uint32 g_print;
static LinearSectionPtr g_test[3];

/* See above for how to generate TESTSIG using DUMP 2603
 * (e.g. : <All/NodeId> DUMP 2603 <TestId> <LoopCount> <Print>
 *   LoopCount : How many times test should loop (0-n)
 *   Print : Whether signals should be printed : 0=no 1=yes
 * 
 * TestIds
 *   20 : Test sendDelayed with 1 milli delay, LoopCount times
 *   1-16 : See vm/testLongSig.cpp
 */
void
Cmvmi::execTESTSIG(Signal* signal){
  Uint32 i;
  /**
   * Test of SafeCounter
   */
  jamEntry();

  if(!assembleFragments(signal)){
    jam();
    return;
  }

  Uint32 ref = signal->theData[0];
  Uint32 testType = signal->theData[1];
  Uint32 fragmentLength = signal->theData[2];
  g_print = signal->theData[3];
//  Uint32 returnCount = signal->theData[4];
  Uint32 * secSizes = &signal->theData[5];

  SectionHandle handle(this, signal);
  
  if(g_print){
    SignalLoggerManager::printSignalHeader(stdout, 
					   signal->header,
					   0,
					   getOwnNodeId(),
					   true);
    ndbout_c("-- Fixed section --");    
    for(i = 0; i<signal->length(); i++){
      fprintf(stdout, "H'0x%.8x ", signal->theData[i]);
      if(((i + 1) % 6) == 0)
	fprintf(stdout, "\n");
    }
    fprintf(stdout, "\n");
    
    for(i = 0; i<handle.m_cnt; i++){
      SegmentedSectionPtr ptr(0,0,0);
      ndbout_c("-- Section %d --", i);
      handle.getSection(ptr, i);
      ndbrequire(ptr.p != 0);
      print(ptr, stdout);
      ndbrequire(ptr.sz == secSizes[i]);
    }
  }

  /**
   * Validate length:s
   */
  for(i = 0; i<handle.m_cnt; i++){
    SegmentedSectionPtr ptr;
    handle.getSection(ptr, i);
    ndbrequire(ptr.p != 0);
    ndbrequire(ptr.sz == secSizes[i]);
  }

  /**
   * Testing send with delay.
   */
  if (testType == 20) {
    if (signal->theData[4] == 0) {
      releaseSections(handle);
      return;
    }
    signal->theData[4]--;
    sendSignalWithDelay(reference(), GSN_TESTSIG, signal, 100, 8, &handle);
    return;
  }

  if (g_print)
    ndbout_c("TestType=%u signal->theData[4]=%u, sendersBlockRef=%u ref=%u\n",
             testType, signal->theData[4], signal->getSendersBlockRef(), ref);

  NodeReceiverGroup rg(CMVMI, c_dbNodes);

  /**
   * Testing SimulatedBlock fragment assembly cleanup
   */
  if ((testType >= 30) &&
      (testType < 40))
  {
    testFragmentedCleanup(signal, &handle, testType, ref);
    return;
  }

  if(signal->getSendersBlockRef() == ref){
    /**
     * Signal from API (not via NodeReceiverGroup)
     */
    if((testType % 2) == 1){
      signal->theData[4] = 1; // No further signals after this
    } else {
      // Change testType to UniCast, and set loopCount to the
      // number of nodes.
      signal->theData[1] --;
      signal->theData[4] = rg.m_nodes.count();
    }
  }
  
  switch(testType){
  case 1:
    /* Unicast to self */
    sendSignal(ref, GSN_TESTSIG,  signal, signal->length(), JBB,
	       &handle);
    break;
  case 2:
    /* Multicast to all nodes */
    sendSignal(rg, GSN_TESTSIG,  signal, signal->length(), JBB,
	       &handle);
    break;
  case 3:
  case 4:{
    LinearSectionPtr ptr[3];
    const Uint32 secs = handle.m_cnt;
    for(i = 0; i<secs; i++){
      SegmentedSectionPtr sptr(0,0,0);
      handle.getSection(sptr, i);
      ptr[i].sz = sptr.sz;
      ptr[i].p = new Uint32[sptr.sz];
      copy(ptr[i].p, sptr);
    }
    
    if(testType == 3){
      /* Unicast linear sections to self */
      sendSignal(ref, GSN_TESTSIG, signal, signal->length(), JBB, ptr, secs);
    } else {
      /* Boradcast linear sections to all nodes */
      sendSignal(rg, GSN_TESTSIG, signal, signal->length(), JBB, ptr, secs);
    }
    for(Uint32 i = 0; i<secs; i++){
      delete[] ptr[i].p;
    }
    releaseSections(handle);
    break;
  }
  /* Send fragmented segmented sections direct send */
  case 5:
  case 6:{
    
    NodeReceiverGroup tmp;
    if(testType == 5){
      /* Unicast */
      tmp  = ref;
    } else {
      /* Multicast */
      tmp = rg;
    }
    
    FragmentSendInfo fragSend;
    sendFirstFragment(fragSend,
		      tmp,
		      GSN_TESTSIG,
		      signal,
		      signal->length(),
		      JBB,
		      &handle,
		      false, // Release sections on send
                      fragmentLength);

    int count = 1;
    while(fragSend.m_status != FragmentSendInfo::SendComplete){
      count++;
      if(g_print)
	ndbout_c("Sending fragment %d", count);
      sendNextSegmentedFragment(signal, fragSend);
    }
    break;
  }
  /* Send fragmented linear sections direct send */
  case 7:
  case 8:{
    LinearSectionPtr ptr[3];
    const Uint32 secs = handle.m_cnt;
    for(i = 0; i<secs; i++){
      SegmentedSectionPtr sptr(0,0,0);
      handle.getSection(sptr, i);
      ptr[i].sz = sptr.sz;
      ptr[i].p = new Uint32[sptr.sz];
      copy(ptr[i].p, sptr);
    }

    NodeReceiverGroup tmp;
    if(testType == 7){
      /* Unicast */
      tmp  = ref;
    } else {
      /* Multicast */
      tmp = rg;
    }

    FragmentSendInfo fragSend;
    sendFirstFragment(fragSend,
		      tmp,
		      GSN_TESTSIG,
		      signal,
		      signal->length(),
		      JBB,
		      ptr,
		      secs,
		      fragmentLength);
    
    int count = 1;
    while(fragSend.m_status != FragmentSendInfo::SendComplete){
      count++;
      if(g_print)
	ndbout_c("Sending fragment %d", count);
      sendNextLinearFragment(signal, fragSend);
    }
    
    for(i = 0; i<secs; i++){
      delete[] ptr[i].p;
    }
    releaseSections(handle);
    break;
  }
  /* Test fragmented segmented send with callback */
  case 9:
  case 10:{

    Callback m_callBack;
    m_callBack.m_callbackFunction = 
      safe_cast(&Cmvmi::sendFragmentedComplete);
    
    if(testType == 9){
      /* Unicast */
      m_callBack.m_callbackData = 9;
      sendFragmentedSignal(ref,
			   GSN_TESTSIG, signal, signal->length(), JBB, 
			   &handle,
			   m_callBack,
			   fragmentLength);
    } else {
      /* Multicast */
      m_callBack.m_callbackData = 10;
      sendFragmentedSignal(rg,
			   GSN_TESTSIG, signal, signal->length(), JBB, 
			   &handle,
			   m_callBack,
			   fragmentLength);
    }
    break;
  }
  /* Test fragmented linear send with callback */
  case 11:
  case 12:{

    const Uint32 secs = handle.m_cnt;
    memset(g_test, 0, sizeof(g_test));
    for(i = 0; i<secs; i++){
      SegmentedSectionPtr sptr(0,0,0);
      handle.getSection(sptr, i);
      g_test[i].sz = sptr.sz;
      g_test[i].p = new Uint32[sptr.sz];
      copy(g_test[i].p, sptr);
    }
    
    releaseSections(handle);
    
    Callback m_callBack;
    m_callBack.m_callbackFunction = 
      safe_cast(&Cmvmi::sendFragmentedComplete);
    
    if(testType == 11){
      /* Unicast */
      m_callBack.m_callbackData = 11;
      sendFragmentedSignal(ref,
			   GSN_TESTSIG, signal, signal->length(), JBB, 
			   g_test, secs,
			   m_callBack,
			   fragmentLength);
    } else {
      /* Multicast */
      m_callBack.m_callbackData = 12;
      sendFragmentedSignal(rg,
			   GSN_TESTSIG, signal, signal->length(), JBB, 
			   g_test, secs,
			   m_callBack,
			   fragmentLength);
    }
    break;
  }
  /* Send fragmented segmented sections direct send no-release */
  case 13:
  case 14:{
    NodeReceiverGroup tmp;
    if(testType == 13){
      /* Unicast */
      tmp  = ref;
    } else {
      /* Multicast */
      tmp = rg;
    }
    
    FragmentSendInfo fragSend;
    sendFirstFragment(fragSend,
		      tmp,
		      GSN_TESTSIG,
		      signal,
		      signal->length(),
		      JBB,
		      &handle,
		      true, // Don't release sections
                      fragmentLength);

    int count = 1;
    while(fragSend.m_status != FragmentSendInfo::SendComplete){
      count++;
      if(g_print)
	ndbout_c("Sending fragment %d", count);
      sendNextSegmentedFragment(signal, fragSend);
    }

    if (g_print)
      ndbout_c("Free sections : %u\n", g_sectionSegmentPool.getNoOfFree());
    releaseSections(handle);
    //handle.clear(); // Use instead of releaseSections to Leak sections 
    break;
  }
  /* Loop decrementing signal->theData[9] */
  case 15:{
    releaseSections(handle);
    ndbrequire(signal->getNoOfSections() == 0);
    Uint32 loop = signal->theData[9];
    if(loop > 0){
      signal->theData[9] --;
      sendSignal(CMVMI_REF, GSN_TESTSIG, signal, signal->length(), JBB);
      return;
    }
    sendSignal(ref, GSN_TESTSIG, signal, signal->length(), JBB);
    return;
  }
  case 16:{
    releaseSections(handle);
    Uint32 count = signal->theData[8];
    signal->theData[10] = count * rg.m_nodes.count();
    for(i = 0; i<count; i++){
      sendSignal(rg, GSN_TESTSIG, signal, signal->length(), JBB); 
    }
    return;
  }

  default:
    ndbrequire(false);
  }
  return;
}

void
Cmvmi::sendFragmentedComplete(Signal* signal, Uint32 data, Uint32 returnCode){
  if(g_print)
    ndbout_c("sendFragmentedComplete: %d", data);
  if(data == 11 || data == 12){
    for(Uint32 i = 0; i<3; i++){
      if(g_test[i].p != 0)
	delete[] g_test[i].p;
    }
  }
}


static Uint32
calc_percent(Uint32 used, Uint32 total)
{
  return (total ? (used * 100)/total : 0);
}


static Uint32
sum_array(const Uint32 array[], unsigned sz)
{
  Uint32 sum = 0;
  for (unsigned i = 0; i < sz; i++)
    sum += array[i];
  return sum;
}


/*
  Check if any of the given thresholds has been
  passed since last

  Returns:
   -1       no threshold passed
   0 - 100  threshold passed
*/

static int
check_threshold(Uint32 last, Uint32 now)
{
  assert(last <= 100 && now <= 100);

  static const Uint32 thresholds[] = { 100, 99, 90, 80, 0 };

  Uint32 passed;
  for (size_t i = 0; i < NDB_ARRAY_SIZE(thresholds); i++)
  {
    if (now >= thresholds[i])
    {
      passed = thresholds[i];
      break;
    }
  }
  assert(passed <= 100);

  if (passed == last)
    return -1; // Already reported this level

  return passed;
}


void
Cmvmi::execCONTINUEB(Signal* signal)
{
  switch(signal->theData[0]){
  case ZREPORT_MEMORY_USAGE:
  {
    jam();
    Uint32 cnt = signal->theData[1];
    Uint32 tup_percent_last = signal->theData[2];
    Uint32 acc_percent_last = signal->theData[3];

    {
      // Data memory threshold
      Resource_limit rl;
      m_ctx.m_mm.get_resource_limit(RG_DATAMEM, rl);

      const Uint32 tup_pages_used = rl.m_curr - f_accpages;
      const Uint32 tup_pages_total = rl.m_min - f_accpages;
      const Uint32 tup_percent_now = calc_percent(tup_pages_used,
                                                  tup_pages_total);

      int passed;
      if ((passed = check_threshold(tup_percent_last, tup_percent_now)) != -1)
      {
        jam();
        reportDMUsage(signal, tup_percent_now >= tup_percent_last ? 1 : -1);
        tup_percent_last = passed;
      }
    }

    {
      // Index memory threshold
      const Uint32 acc_pages_used =
        sum_array(g_acc_pages_used, NDB_ARRAY_SIZE(g_acc_pages_used));
      const Uint32 acc_pages_total = f_accpages * 4;
      const Uint32 acc_percent_now = calc_percent(acc_pages_used,
                                                  acc_pages_total);

      int passed;
      if ((passed = check_threshold(acc_percent_last, acc_percent_now)) != -1)
      {
        jam();
        reportIMUsage(signal, acc_percent_now >= acc_percent_last ? 1 : -1);
        acc_percent_last = passed;
      }
    }

    // Index and data memory report frequency
    if(c_memusage_report_frequency &&
       cnt + 1 == c_memusage_report_frequency)
    {
      jam();
      reportDMUsage(signal, 0);
      reportIMUsage(signal, 0);
      cnt = 0;
    }
    else
    {
      jam();
      cnt++;
    }
    signal->theData[0] = ZREPORT_MEMORY_USAGE;
    signal->theData[1] = cnt; // seconds since last report
    signal->theData[2] = tup_percent_last; // last reported threshold for TUP
    signal->theData[3] = acc_percent_last; // last reported threshold for ACC
    sendSignalWithDelay(reference(), GSN_CONTINUEB, signal, 1000, 4);
    return;
  }
  }
}

void
Cmvmi::reportDMUsage(Signal* signal, int incDec, BlockReference ref)
{
  Resource_limit rl;
  m_ctx.m_mm.get_resource_limit(RG_DATAMEM, rl);

  const Uint32 tup_pages_used = rl.m_curr - f_accpages;
  const Uint32 tup_pages_total = rl.m_min - f_accpages;

  signal->theData[0] = NDB_LE_MemoryUsage;
  signal->theData[1] = incDec;
  signal->theData[2] = sizeof(GlobalPage);
  signal->theData[3] = tup_pages_used;
  signal->theData[4] = tup_pages_total;
  signal->theData[5] = DBTUP;
  sendSignal(ref, GSN_EVENT_REP, signal, 6, JBB);
}


void
Cmvmi::reportIMUsage(Signal* signal, int incDec, BlockReference ref)
{
  const Uint32 acc_pages_used =
    sum_array(g_acc_pages_used, NDB_ARRAY_SIZE(g_acc_pages_used));

  signal->theData[0] = NDB_LE_MemoryUsage;
  signal->theData[1] = incDec;
  signal->theData[2] = 8192;
  signal->theData[3] = acc_pages_used;
  signal->theData[4] = f_accpages * 4;
  signal->theData[5] = DBACC;
  sendSignal(ref, GSN_EVENT_REP, signal, 6, JBB);
}


/**
 * execROUTE_ORD
 * Allows other blocks to route signals as if they
 * came from Cmvmi
 * Useful in ndbmtd for synchronising signals w.r.t
 * external signals received from other nodes which
 * arrive from the same thread that runs CMVMI.
 */
void
Cmvmi::execROUTE_ORD(Signal* signal)
{
  jamEntry();
  if(!assembleFragments(signal)){
    jam();
    return;
  }

  SectionHandle handle(this, signal);

  RouteOrd* ord = (RouteOrd*)signal->getDataPtr();
  Uint32 dstRef = ord->dstRef;
  Uint32 srcRef = ord->srcRef;
  Uint32 gsn = ord->gsn;
  /* ord->cnt ignored */

  Uint32 nodeId = refToNode(dstRef);
  
  if (likely((nodeId == 0) ||
             getNodeInfo(nodeId).m_connected))
  {
    jam();
    Uint32 secCount = handle.m_cnt;
    ndbrequire(secCount >= 1 && secCount <= 3);

    jamLine(secCount);

    /**
     * Put section 0 in signal->theData
     */
    Uint32 sigLen = handle.m_ptr[0].sz;
    ndbrequire(sigLen <= 25);
    copy(signal->theData, handle.m_ptr[0]);

    SegmentedSectionPtr save = handle.m_ptr[0];
    for (Uint32 i = 0; i < secCount - 1; i++)
      handle.m_ptr[i] = handle.m_ptr[i+1];
    handle.m_cnt--;

    sendSignal(dstRef, gsn, signal, sigLen, JBB, &handle);

    handle.m_cnt = 1;
    handle.m_ptr[0] = save;
    releaseSections(handle);
    return ;
  }

  releaseSections(handle);
  warningEvent("Unable to route GSN: %d from %x to %x",
	       gsn, srcRef, dstRef);
}
