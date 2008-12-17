/* Copyright (C) 2003-2008 MySQL AB

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

#include <ndb_global.h>

#include "MgmtSrvr.hpp"
#include "ndb_mgmd_error.h"
#include "Services.hpp"
#include "ConfigManager.hpp"

#include <NdbOut.hpp>
#include <NdbApiSignal.hpp>
#include <kernel_types.h>
#include <GlobalSignalNumbers.h>
#include <signaldata/TestOrd.hpp>
#include <signaldata/TamperOrd.hpp>
#include <signaldata/StartOrd.hpp>
#include <signaldata/ApiVersion.hpp>
#include <signaldata/ResumeReq.hpp>
#include <signaldata/SetLogLevelOrd.hpp>
#include <signaldata/EventSubscribeReq.hpp>
#include <signaldata/EventReport.hpp>
#include <signaldata/DumpStateOrd.hpp>
#include <signaldata/BackupSignalData.hpp>
#include <signaldata/NFCompleteRep.hpp>
#include <signaldata/NodeFailRep.hpp>
#include <signaldata/AllocNodeId.hpp>
#include <signaldata/SchemaTrans.hpp>
#include <signaldata/CreateNodegroup.hpp>
#include <signaldata/DropNodegroup.hpp>
#include <signaldata/DbinfoScan.hpp>
#include <NdbSleep.h>
#include <EventLogger.hpp>
#include <DebuggerNames.hpp>
#include <ndb_version.h>

#include <SocketServer.hpp>
#include <NdbConfig.h>

#include <NdbAutoPtr.hpp>

#include <ndberror.h>

#include <mgmapi.h>
#include <mgmapi_configuration.hpp>
#include <mgmapi_config_parameters.h>

#include <SignalSender.hpp>

#include <ndbinfo.h>
#include <AttributeHeader.hpp>

int g_errorInsert;
#define ERROR_INSERTED(x) (g_errorInsert == x)

#define INIT_SIGNAL_SENDER(ss,nodeId) \
  SignalSender ss(theFacade); \
  ss.lock(); /* lock will be released on exit */ \
  {\
    int result = okToSendTo(nodeId, true);\
    if (result != 0) {\
      return result;\
    }\
  }

extern "C" my_bool opt_core;

static void require(bool v)
{
  if(!v)
  {
    if (opt_core)
      abort();
    else
      exit(-1);
  }
}


void *
MgmtSrvr::logLevelThread_C(void* m)
{
  MgmtSrvr *mgm = (MgmtSrvr*)m;
  mgm->logLevelThreadRun();
  return 0;
}

extern EventLogger * g_eventLogger;

#ifdef NOT_USED
static NdbOut&
operator<<(NdbOut& out, const LogLevel & ll)
{
  out << "[LogLevel: ";
  for(size_t i = 0; i<LogLevel::LOGLEVEL_CATEGORIES; i++)
    out << ll.getLogLevel((LogLevel::EventCategory)i) << " ";
  out << "]";
  return out;
}
#endif

void
MgmtSrvr::logLevelThreadRun() 
{
  while (!_isStopThread) 
  {
    Vector<NodeId> failed_started_nodes;
    Vector<EventSubscribeReq> failed_log_level_requests;

    /**
     * Handle started nodes
     */
    m_started_nodes.lock();
    if (m_started_nodes.size() > 0)
    {
      // calculate max log level
      EventSubscribeReq req;
      {
        LogLevel tmp;
        m_event_listner.lock();
        for(int i = m_event_listner.m_clients.size() - 1; i >= 0; i--)
          tmp.set_max(m_event_listner[i].m_logLevel);
        m_event_listner.unlock();
        req = tmp;
      }
      req.blockRef = _ownReference;
      while (m_started_nodes.size() > 0)
      {
        Uint32 node = m_started_nodes[0];
        m_started_nodes.erase(0, false);
        m_started_nodes.unlock();

        if (setEventReportingLevelImpl(node, req))
        {
          failed_started_nodes.push_back(node);
        }
        else
        {
          SetLogLevelOrd ord;
          ord = m_nodeLogLevel[node];
          setNodeLogLevelImpl(node, ord);
        }
        m_started_nodes.lock();
      }
    }
    m_started_nodes.unlock();
    
    m_log_level_requests.lock();
    while (m_log_level_requests.size() > 0)
    {
      EventSubscribeReq req = m_log_level_requests[0];
      m_log_level_requests.erase(0, false);
      m_log_level_requests.unlock();

      if(req.blockRef == 0)
      {
        req.blockRef = _ownReference;
        if (setEventReportingLevelImpl(0, req))
        {
          failed_log_level_requests.push_back(req);
        }
      } 
      else 
      {
        SetLogLevelOrd ord;
        ord = req;
        if (setNodeLogLevelImpl(req.blockRef, ord))
        {
          failed_log_level_requests.push_back(req);
        }
      }
      m_log_level_requests.lock();
    }      
    m_log_level_requests.unlock();

    if(!ERROR_INSERTED(10000))
      m_event_listner.check_listeners();

    Uint32 sleeptime = _logLevelThreadSleep;
    if (failed_started_nodes.size())
    {
      m_started_nodes.lock();
      for (Uint32 i = 0; i<failed_started_nodes.size(); i++)
        m_started_nodes.push_back(failed_started_nodes[i], false);
      m_started_nodes.unlock();
      failed_started_nodes.clear();
      sleeptime = 100;
    }

    if (failed_log_level_requests.size())
    {
      m_log_level_requests.lock();
      for (Uint32 i = 0; i<failed_log_level_requests.size(); i++)
        m_log_level_requests.push_back(failed_log_level_requests[i], false);
      m_log_level_requests.unlock();
      failed_log_level_requests.clear();
      sleeptime = 100;
    }

    NdbSleep_MilliSleep(sleeptime);
  }
}


bool
MgmtSrvr::setEventLogFilter(int severity, int enable)
{
  Logger::LoggerLevel level = (Logger::LoggerLevel)severity;
  if (enable > 0) {
    g_eventLogger->enable(level);
  } else if (enable == 0) {
    g_eventLogger->disable(level);
  } else if (g_eventLogger->isEnable(level)) {
    g_eventLogger->disable(level);
  } else {
    g_eventLogger->enable(level);
  }
  return g_eventLogger->isEnable(level);
}

bool 
MgmtSrvr::isEventLogFilterEnabled(int severity) 
{
  return g_eventLogger->isEnable((Logger::LoggerLevel)severity);
}

int MgmtSrvr::translateStopRef(Uint32 errCode)
{
  switch(errCode){
  case StopRef::NodeShutdownInProgress:
    return NODE_SHUTDOWN_IN_PROGESS;
    break;
  case StopRef::SystemShutdownInProgress:
    return SYSTEM_SHUTDOWN_IN_PROGRESS;
    break;
  case StopRef::NodeShutdownWouldCauseSystemCrash:
    return NODE_SHUTDOWN_WOULD_CAUSE_SYSTEM_CRASH;
    break;
  case StopRef::UnsupportedNodeShutdown:
    return UNSUPPORTED_NODE_SHUTDOWN;
    break;
  }
  return 4999;
}


MgmtSrvr::MgmtSrvr(const MgmtOpts& opts,
                   const char* connect_str) :
  m_opts(opts),
  _blockNumber(-1),
  _ownNodeId(0),
  m_port(0),
  m_local_config(NULL),
  _ownReference(0),
  m_config_manager(NULL),
  m_need_restart(false),
  theFacade(NULL),
  _isStopThread(false),
  _logLevelThreadSleep(500),
  m_event_listner(this),
  m_master_node(0),
  _logLevelThread(NULL)
{
  DBUG_ENTER("MgmtSrvr::MgmtSrvr");

  m_local_config_mutex= NdbMutex_Create();
  m_node_id_mutex = NdbMutex_Create();
  if (!m_local_config_mutex || !m_node_id_mutex)
  {
    g_eventLogger->error("Failed to create MgmtSrvr mutexes");
    require(false);
  }

  /* Init node arrays */
  for(Uint32 i = 0; i<MAX_NODES; i++) {
    nodeTypes[i] = (enum ndb_mgm_node_type)-1;
    m_connect_address[i].s_addr= 0;
  }

  /* Setup clusterlog as client[0] in m_event_listner */
  {
    Ndb_mgmd_event_service::Event_listener se;
    my_socket_invalidate(&(se.m_socket));
    for(size_t t = 0; t<LogLevel::LOGLEVEL_CATEGORIES; t++){
      se.m_logLevel.setLogLevel((LogLevel::EventCategory)t, 7);
    }
    se.m_logLevel.setLogLevel(LogLevel::llError, 15);
    se.m_logLevel.setLogLevel(LogLevel::llConnection, 8);
    se.m_logLevel.setLogLevel(LogLevel::llBackup, 15);
    m_event_listner.m_clients.push_back(se);
    m_event_listner.m_logLevel = se.m_logLevel;
  }

  DBUG_VOID_RETURN;
}


static bool
create_directory(const char* dir)
{
#ifdef __WIN__
  if (CreateDirectory(dir, NULL) == 0)
  {
    g_eventLogger->warning("Failed to create directory '%s', error: %d",
                           dir, GetLastError());
    return false;
  }
#else
  if (mkdir(dir, S_IRUSR | S_IWUSR | S_IXUSR ) != 0)
  {
    g_eventLogger->warning("Failed to create directory '%s', error: %d",
                           dir, errno);
    return false;
  }
#endif
  return true;
}


/*
  check_configdir

  Make sure configdir exist and try to create it if not

*/

const char*
MgmtSrvr::check_configdir() const
{
  if (m_opts.configdir &&
      strcmp(m_opts.configdir, MYSQLCLUSTERDIR) != 0)
  {
    // Specified on commmand line
    if (access(m_opts.configdir, F_OK))
    {
      g_eventLogger->error("Directory '%s' specified with --configdir " \
                           "does not exist. Either create it or pass " \
                           "the path to an already existing directory.",
                           m_opts.configdir);
      return NULL;
    }
    return m_opts.configdir;
  }
  else
  {
    // Compiled in path MYSQLCLUSTERDIR
    if (access(MYSQLCLUSTERDIR, F_OK))
    {
      g_eventLogger->info("The default config directory '%s' "            \
                          "does not exist. Trying to create it...",
                          MYSQLCLUSTERDIR);

      if (!create_directory(MYSQLCLUSTERDIR) ||
          access(MYSQLCLUSTERDIR, F_OK))
      {
        g_eventLogger->error("Could not create directory '%s'. "        \
                             "Either create it manually or "            \
                             "specify a different directory with "      \
                             "--configdir=<path>",
                             MYSQLCLUSTERDIR);
        return NULL;
      }

      g_eventLogger->info("Sucessfully created config directory");
    }
    return MYSQLCLUSTERDIR;
  }
}


bool
MgmtSrvr::init()
{
  DBUG_ENTER("MgmtSrvr::init");

  const char* configdir;
  if (!(configdir= check_configdir()))
    DBUG_RETURN(false);

  if (!(m_config_manager= new ConfigManager(m_opts, configdir)))
  {
    g_eventLogger->error("Failed to create ConfigManager");
    DBUG_RETURN(false);
  }

  if (m_config_manager->add_config_change_subscriber(this) < 0)
  {
    g_eventLogger->error("Failed to add MgmtSrvr as config change subscriber");
    DBUG_RETURN(false);
  }

  if (!m_config_manager->init())
  {
    DBUG_RETURN(false);
  }

  /* 'config_changed' should have been called from 'init' */
  require(m_local_config);

  if (m_opts.print_full_config)
  {
    print_config();
    DBUG_RETURN(false);
  }

  assert(_ownNodeId);

  /* Reserve the node id with ourself */
  NodeId nodeId= _ownNodeId;
  int error_code;
  BaseString error_string;
  if (!alloc_node_id(&nodeId, NDB_MGM_NODE_TYPE_MGM,
                     0, /* client_addr */
                     error_code, error_string,
                     0 /* log_event */ ))
  {
    g_eventLogger->error("INTERNAL ERROR: Could not allocate nodeid: %d, " \
                         "error: %d, '%s'",
                         _ownNodeId, error_code, error_string.c_str());
    DBUG_RETURN(false);
  }

  if (nodeId != _ownNodeId)
  {
    g_eventLogger->error("INTERNAL ERROR: Nodeid %d allocated " \
                         "when %d was requested",
                         nodeId, _ownNodeId);
    DBUG_RETURN(false);
  }

  DBUG_RETURN(true);
}


bool
MgmtSrvr::start_transporter(const Config* config)
{
  DBUG_ENTER("MgmtSrvr::start_transporter");

  theFacade= new TransporterFacade(0);
  if (theFacade == 0)
  {
    g_eventLogger->error("Could not create TransporterFacade.");
    DBUG_RETURN(false);
  }

  if (theFacade->start_instance(_ownNodeId,
                                config->m_configValues) < 0)
  {
    g_eventLogger->error("Failed to start transporter");
    delete theFacade;
    theFacade = 0;
    DBUG_RETURN(false);
  }

  assert(_blockNumber == -1); // Blocknumber shouldn't been allocated yet

  /*
    Register ourself at TransporterFacade to be able to receive signals
    and to be notified when a database process has died.
  */
  if ((_blockNumber= theFacade->open(this,
                                     signalReceivedNotification,
                                     nodeStatusNotification)) == -1)
  {
    g_eventLogger->error("Failed to open block in TransporterFacade");
    theFacade->stop_instance();
    delete theFacade;
    theFacade = 0;
    DBUG_RETURN(false);
  }

  _ownReference = numberToRef(_blockNumber, _ownNodeId);

  /*
    set api reg req frequency quite high:

    100 ms interval to make sure we have fairly up-to-date
    info from the nodes.  This to make sure that this info
    is not dependent on heartbeat settings in the
    configuration
  */
  theFacade->theClusterMgr->set_max_api_reg_req_interval(100);

  DBUG_RETURN(true);
}


bool
MgmtSrvr::start_mgm_service(const Config* config)
{
  DBUG_ENTER("MgmtSrvr::start_mgm_service");

  assert(m_port == 0);
  {
    // Find the portnumber to use for mgm service
    ConfigIter iter(config, CFG_SECTION_NODE);

    if(iter.find(CFG_NODE_ID, _ownNodeId) != 0){
      g_eventLogger->error("Could not find node %d in config", _ownNodeId);
      DBUG_RETURN(false);
    }

    unsigned type;
    if(iter.get(CFG_TYPE_OF_SECTION, &type) != 0 ||
       type != NODE_TYPE_MGM){
      g_eventLogger->error("Node %d is not defined as management server",
                           _ownNodeId);
      DBUG_RETURN(false);
    }

    if(iter.get(CFG_MGM_PORT, &m_port) != 0){
      g_eventLogger->error("PortNumber not defined for node %d", _ownNodeId);
      DBUG_RETURN(false);
    }
  }

  unsigned short port= m_port;
  DBUG_PRINT("info", ("Using port %d", port));
  if (port == 0)
  {
    g_eventLogger->error("Could not find out which port to use"\
                        " for management service");
    DBUG_RETURN(false);
  }

  {
    int count= 5; // no of retries for tryBind
    while(!m_socket_server.tryBind(port, m_opts.bind_address))
    {
      if (--count > 0)
      {
	NdbSleep_SecSleep(1);
	continue;
      }
      g_eventLogger->error("Unable to bind management service port: %s:%d!\n"
                           "Please check if the port is already used,\n"
                           "(perhaps a ndb_mgmd is already running),\n"
                           "and if you are executing on the correct computer",
                           (m_opts.bind_address ? m_opts.bind_address : "*"),
                           port);
      DBUG_RETURN(false);
    }
  }

  {
    MgmApiService * mapi = new MgmApiService(*this);
    if (mapi == NULL)
    {
      g_eventLogger->error("Could not allocate MgmApiService");
      DBUG_RETURN(false);
    }

    if(!m_socket_server.setup(mapi, &port, m_opts.bind_address))
    {
      delete mapi; // Will be deleted by SocketServer in all other cases
      g_eventLogger->error("Unable to setup management service port: %s:%d!\n"
                           "Please check if the port is already used,\n"
                           "(perhaps a ndb_mgmd is already running),\n"
                           "and if you are executing on the correct computer",
                           (m_opts.bind_address ? m_opts.bind_address : "*"),
                           port);
      DBUG_RETURN(false);
    }

    if (port != m_port)
    {
      g_eventLogger->error("Couldn't start management service on the "\
                           "requested port: %d. Got port: %d instead",
                          m_port, port);
      DBUG_RETURN(false);
    }
  }

  m_socket_server.startServer();

  g_eventLogger->info("Id: %d, Command port: %s:%d",
                      _ownNodeId,
                      m_opts.bind_address ? m_opts.bind_address : "*",
                      port);
  DBUG_RETURN(true);
}


bool
MgmtSrvr::start()
{
  DBUG_ENTER("MgmtSrvr::start");

  Guard g(m_local_config_mutex);

  /* Start transporter */
  if(!start_transporter(m_local_config))
  {
    g_eventLogger->error("Failed to start transporter!");
    DBUG_RETURN(false);
  }

  /* Start mgm service */
  if (!start_mgm_service(m_local_config))
  {
    g_eventLogger->error("Failed to start mangement service!");
    DBUG_RETURN(false);
  }

  /* Use local MGM port for TransporterRegistry */
  if(!connect_to_self())
  {
    g_eventLogger->error("Failed to connect to ourself!");
    DBUG_RETURN(false);
  }

  /* Start config manager */
  m_config_manager->set_facade(theFacade);
  if (!m_config_manager->start())
  {
    g_eventLogger->error("Failed to start ConfigManager");
    DBUG_RETURN(false);
  }

  /* Loglevel thread */
  assert(_isStopThread == false);
  _logLevelThread = NdbThread_Create(logLevelThread_C,
				     (void**)this,
				     32768,
				     "MgmtSrvr_Loglevel",
				     NDB_THREAD_PRIO_LOW);

  DBUG_RETURN(true);
}


void
MgmtSrvr::setClusterLog(const Config* config)
{
  BaseString logdest;

  g_eventLogger->close();

  DBUG_ASSERT(_ownNodeId);

  ConfigIter iter(config, CFG_SECTION_NODE);
  require(iter.find(CFG_NODE_ID, _ownNodeId) == 0);

  // Update DataDir from config
  const char *datadir;
  require(iter.get(CFG_NODE_DATADIR, &datadir) == 0);
  NdbConfig_SetPath(datadir);

  // Get log destination from config
  const char *value;
  if(iter.get(CFG_LOG_DESTINATION, &value) == 0){
    logdest.assign(value);
  }

  if(logdest.length() == 0 || logdest == "") {
    // No LogDestination set, use default settings
    char *clusterLog= NdbConfig_ClusterLogFileName(_ownNodeId);
    logdest.assfmt("FILE:filename=%s,maxsize=1000000,maxfiles=6",
		   clusterLog);
    free(clusterLog);
  }

  int err= 0;
  char errStr[100]= {0};
  if(!g_eventLogger->addHandler(logdest, &err, sizeof(errStr), errStr)) {
    ndbout << "Warning: could not add log destination '"
           << logdest.c_str() << "'. Reason: ";
    if(err)
      ndbout << strerror(err);
    if(err && errStr[0]!='\0')
      ndbout << ", ";
    if(errStr[0]!='\0')
      ndbout << errStr;
    ndbout << endl;
  }

  if (m_opts.non_interactive)
    g_eventLogger->createConsoleHandler();

  if (m_opts.verbose)
    g_eventLogger->enable(Logger::LL_DEBUG);
}


void
MgmtSrvr::config_changed(NodeId node_id, const Config* new_config)
{
  DBUG_ENTER("MgmtSrvr::config_changed");

  Guard g(m_local_config_mutex);

  // Don't allow nodeid to change, once it's been set
  require(_ownNodeId == 0 || _ownNodeId == node_id);

  _ownNodeId= node_id;

  // TODO Magnus, Copy information about dynamic ports from
  // new to old or save that info elsewhere

  delete m_local_config;
  m_local_config= new Config(new_config); // Copy
  require(m_local_config);

  /* Rebuild node arrays */
  ConfigIter iter(m_local_config, CFG_SECTION_NODE);
  for(Uint32 i = 0; i<MAX_NODES; i++) {

    m_connect_address[i].s_addr= 0;

    if (iter.first())
      continue;

    if (iter.find(CFG_NODE_ID, i) == 0){
      unsigned type;
      require(iter.get(CFG_TYPE_OF_SECTION, &type) == 0);

      switch(type){
      case NODE_TYPE_DB:
        nodeTypes[i] = NDB_MGM_NODE_TYPE_NDB;
        break;
      case NODE_TYPE_API:
        nodeTypes[i] = NDB_MGM_NODE_TYPE_API;
        break;
      case NODE_TYPE_MGM:
        nodeTypes[i] = NDB_MGM_NODE_TYPE_MGM;
        break;
      default:
        break;
      }
    }
    else
    {
      nodeTypes[i] = (enum ndb_mgm_node_type)-1;
    }

  }

  // Setup cluster log
  setClusterLog(m_local_config);

  if (theFacade)
  {
    if (!theFacade->configure(_ownNodeId,
                              m_local_config->m_configValues))
    {
      g_eventLogger->warning("Could not reconfigure everything online, "
                             "this node need a restart");
      m_need_restart= true;
    }
  }

  DBUG_VOID_RETURN;
}


bool
MgmtSrvr::getPackedConfig(UtilBuffer& pack_buf)
{
  return m_config_manager->get_packed_config(pack_buf);
}


MgmtSrvr::~MgmtSrvr()
{

  /* Stop config manager */
  if (m_config_manager != 0)
  {
    m_config_manager->stop();
    delete m_config_manager;
    m_config_manager= 0;
  }

  /* Stop log level thread */
  void* res = 0;
  _isStopThread = true;

  if (_logLevelThread != NULL) {
    NdbThread_WaitFor(_logLevelThread, &res);
    NdbThread_Destroy(&_logLevelThread);
  }

  /* Stop mgm service, don't allow new connections */
  m_socket_server.stopServer();

  /* Stop all active session */
  m_socket_server.stopSessions(true);

  // Stop transporter
  if(theFacade != 0){
    theFacade->stop_instance();
    delete theFacade;
    theFacade = 0;
  }

  delete m_local_config;

  NdbMutex_Destroy(m_local_config_mutex);
  NdbMutex_Destroy(m_node_id_mutex);
}


//****************************************************************************
//****************************************************************************

int MgmtSrvr::okToSendTo(NodeId nodeId, bool unCond) 
{
  if(nodeId == 0 || getNodeType(nodeId) != NDB_MGM_NODE_TYPE_NDB)
    return WRONG_PROCESS_TYPE;
  // Check if we have contact with it
  if(unCond){
    if(theFacade->theClusterMgr->getNodeInfo(nodeId).m_api_reg_conf)
      return 0;
  }
  else if (theFacade->get_node_alive(nodeId) == true)
    return 0;
  return NO_CONTACT_WITH_PROCESS;
}

void
MgmtSrvr::report_unknown_signal(SimpleSignal *signal)
{
  signal->print();
  g_eventLogger->error("Unknown signal received. SignalNumber: "
                       "%i from (%d, 0x%x)",
                       signal->readSignalNumber(),
                       refToNode(signal->header.theSendersBlockRef),
                       refToBlock(signal->header.theSendersBlockRef));
  assert(false);
}

/*****************************************************************************
 * Starting and stopping database nodes
 ****************************************************************************/

int 
MgmtSrvr::start(int nodeId)
{
  INIT_SIGNAL_SENDER(ss,nodeId);
  
  SimpleSignal ssig;
  StartOrd* const startOrd = CAST_PTR(StartOrd, ssig.getDataPtrSend());
  ssig.set(ss,TestOrd::TraceAPI, CMVMI, GSN_START_ORD, StartOrd::SignalLength);
  startOrd->restartInfo = 0;
  
  return ss.sendSignal(nodeId, &ssig) == SEND_OK ? 0 : SEND_OR_RECEIVE_FAILED;
}

/*****************************************************************************
 * Version handling
 *****************************************************************************/

int 
MgmtSrvr::versionNode(int nodeId, Uint32 &version, Uint32& mysql_version,
		      const char **address)
{
  version= 0;
  mysql_version = 0;
  if (getOwnNodeId() == nodeId)
  {
    /**
     * If we're inquiring about our own node id,
     * We know what version we are (version implies connected for mgm)
     * but would like to find out from elsewhere what address they're using
     * to connect to us. This means that secondary mgm servers
     * can list ip addresses for mgm servers.
     *
     * If we don't get an address (i.e. no db nodes),
     * we get the address from the configuration.
     */
    sendVersionReq(nodeId, version, mysql_version, address);
    version= NDB_VERSION;
    mysql_version = NDB_MYSQL_VERSION_D;
    if(!*address)
    {
      Guard g(m_local_config_mutex);
      ConfigIter iter(m_local_config, CFG_SECTION_NODE);
      unsigned tmp= 0;
      for(iter.first();iter.valid();iter.next())
      {
	if(iter.get(CFG_NODE_ID, &tmp)) require(false);
	if((unsigned)nodeId!=tmp)
	  continue;
	if(iter.get(CFG_NODE_HOST, address)) require(false);
	break;
      }
    }
  }
  else if (getNodeType(nodeId) == NDB_MGM_NODE_TYPE_NDB)
  {
    ClusterMgr::Node node= theFacade->theClusterMgr->getNodeInfo(nodeId);
    if(node.connected)
    {
      version= node.m_info.m_version;
      mysql_version = node.m_info.m_mysql_version;
    }
    *address= get_connect_address(nodeId);
  }
  else if (getNodeType(nodeId) == NDB_MGM_NODE_TYPE_API ||
	   getNodeType(nodeId) == NDB_MGM_NODE_TYPE_MGM)
  {
    return sendVersionReq(nodeId, version, mysql_version, address);
  }

  return 0;
}


int
MgmtSrvr::sendVersionReq(int v_nodeId,
			 Uint32 &version,
			 Uint32& mysql_version,
			 const char **address)
{
  SignalSender ss(theFacade);
  ss.lock();

  SimpleSignal ssig;
  ApiVersionReq* req = CAST_PTR(ApiVersionReq, ssig.getDataPtrSend());
  req->senderRef = ss.getOwnRef();
  req->nodeId = v_nodeId;
  ssig.set(ss, TestOrd::TraceAPI, QMGR,
           GSN_API_VERSION_REQ, ApiVersionReq::SignalLength);

  NodeId nodeId;
  int do_send = 1;
  while(true)
  {
    if (do_send)
    {
      nodeId = ss.get_an_alive_node();
      if (nodeId == 0)
      {
        return NO_CONTACT_WITH_DB_NODES;
      }

      if (ss.sendSignal(nodeId, &ssig) != SEND_OK)
      {
        return SEND_OR_RECEIVE_FAILED;
      }

      do_send = 0;
    }

    SimpleSignal *signal = ss.waitFor();

    switch (signal->readSignalNumber()) {
    case GSN_API_VERSION_CONF: {
      const ApiVersionConf * const conf =
	CAST_CONSTPTR(ApiVersionConf, signal->getDataPtr());

      assert((int) conf->nodeId == v_nodeId);

      version = conf->version;
      mysql_version = conf->mysql_version;
      if (version < NDBD_SPLIT_VERSION)
	mysql_version = 0;
      struct in_addr in;
      in.s_addr= conf->inet_addr;
      *address= inet_ntoa(in);

      return 0;
    }

    case GSN_NF_COMPLETEREP:{
      const NFCompleteRep * const rep =
	CAST_CONSTPTR(NFCompleteRep, signal->getDataPtr());
      if (rep->failedNodeId == nodeId)
	do_send = 1; // retry with other node
      continue;
    }

    case GSN_NODE_FAILREP:{
      const NodeFailRep * const rep =
	CAST_CONSTPTR(NodeFailRep, signal->getDataPtr());
      if (NdbNodeBitmask::get(rep->theNodes,nodeId))
	do_send = 1; // retry with other node
      continue;
    }
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      // Ignore
      continue;
    default:
      report_unknown_signal(signal);
      return SEND_OR_RECEIVE_FAILED;
    }
  }

  // Should never come here
  require(false);
  return -1;
}


int MgmtSrvr::sendStopMgmd(NodeId nodeId,
			   bool abort,
			   bool stop,
			   bool restart,
			   bool nostart,
			   bool initialStart)
{
  const char* hostname;
  Uint32 port;
  BaseString connect_string;

  {
    Guard g(m_local_config_mutex);
    {
      ConfigIter iter(m_local_config, CFG_SECTION_NODE);

      if(iter.first())                       return SEND_OR_RECEIVE_FAILED;
      if(iter.find(CFG_NODE_ID, nodeId))     return SEND_OR_RECEIVE_FAILED;
      if(iter.get(CFG_NODE_HOST, &hostname)) return SEND_OR_RECEIVE_FAILED;
    }
    {
      ConfigIter iter(m_local_config, CFG_SECTION_NODE);

      if(iter.first())                   return SEND_OR_RECEIVE_FAILED;
      if(iter.find(CFG_NODE_ID, nodeId)) return SEND_OR_RECEIVE_FAILED;
      if(iter.get(CFG_MGM_PORT, &port))  return SEND_OR_RECEIVE_FAILED;
    }
    if( strlen(hostname) == 0 )
      return SEND_OR_RECEIVE_FAILED;

  }
  connect_string.assfmt("%s:%u",hostname,port);

  DBUG_PRINT("info",("connect string: %s",connect_string.c_str()));

  NdbMgmHandle h= ndb_mgm_create_handle();
  if ( h && connect_string.length() > 0 )
  {
    ndb_mgm_set_connectstring(h,connect_string.c_str());
    if(ndb_mgm_connect(h,1,0,0))
    {
      DBUG_PRINT("info",("failed ndb_mgm_connect"));
      ndb_mgm_destroy_handle(&h);
      return SEND_OR_RECEIVE_FAILED;
    }
    if(!restart)
    {
      if(ndb_mgm_stop(h, 1, (const int*)&nodeId) < 0)
      {
        ndb_mgm_destroy_handle(&h);
        return SEND_OR_RECEIVE_FAILED;
      }
    }
    else
    {
      int nodes[1];
      nodes[0]= (int)nodeId;
      if(ndb_mgm_restart2(h, 1, nodes, initialStart, nostart, abort) < 0)
      {
        ndb_mgm_destroy_handle(&h);
        return SEND_OR_RECEIVE_FAILED;
      }
    }
  }
  ndb_mgm_destroy_handle(&h);

  return 0;
}

/*
 * Common method for handeling all STOP_REQ signalling that
 * is used by Stopping, Restarting and Single user commands
 *
 * In the event that we need to stop a mgmd, we create a mgm
 * client connection to that mgmd and stop it that way.
 * This allows us to stop mgm servers when there isn't any real
 * distributed communication up.
 *
 * node_ids.size()==0 means to stop all DB nodes.
 *                    MGM nodes will *NOT* be stopped.
 *
 * If we work out we should be stopping or restarting ourselves,
 * we return <0 in stopSelf for restart, >0 for stop
 * and 0 for do nothing.
 */

int MgmtSrvr::sendSTOP_REQ(const Vector<NodeId> &node_ids,
			   NdbNodeBitmask &stoppedNodes,
			   Uint32 singleUserNodeId,
			   bool abort,
			   bool stop,
			   bool restart,
			   bool nostart,
			   bool initialStart,
                           int* stopSelf)
{
  int error = 0;
  DBUG_ENTER("MgmtSrvr::sendSTOP_REQ");
  DBUG_PRINT("enter", ("no of nodes: %d  singleUseNodeId: %d  "
                       "abort: %d  stop: %d  restart: %d  "
                       "nostart: %d  initialStart: %d",
                       node_ids.size(), singleUserNodeId,
                       abort, stop, restart, nostart, initialStart));

  stoppedNodes.clear();

  SignalSender ss(theFacade);
  ss.lock(); // lock will be released on exit

  SimpleSignal ssig;
  StopReq* const stopReq = CAST_PTR(StopReq, ssig.getDataPtrSend());
  ssig.set(ss, TestOrd::TraceAPI, NDBCNTR, GSN_STOP_REQ, StopReq::SignalLength);

  NdbNodeBitmask notstarted;
  for (Uint32 i = 0; i<node_ids.size(); i++)
  {
    Uint32 nodeId = node_ids[i];
    ClusterMgr::Node node = theFacade->theClusterMgr->getNodeInfo(nodeId);
    if (node.m_state.startLevel != NodeState::SL_STARTED)
      notstarted.set(nodeId);
  }
  
  stopReq->requestInfo = 0;
  stopReq->apiTimeout = 5000;
  stopReq->transactionTimeout = 1000;
  stopReq->readOperationTimeout = 1000;
  stopReq->operationTimeout = 1000;
  stopReq->senderData = 12;
  stopReq->senderRef = ss.getOwnRef();
  if (singleUserNodeId)
  {
    stopReq->singleuser = 1;
    stopReq->singleUserApi = singleUserNodeId;
    StopReq::setSystemStop(stopReq->requestInfo, false);
    StopReq::setPerformRestart(stopReq->requestInfo, false);
    StopReq::setStopAbort(stopReq->requestInfo, false);
  }
  else
  {
    stopReq->singleuser = 0;
    StopReq::setSystemStop(stopReq->requestInfo, stop);
    StopReq::setPerformRestart(stopReq->requestInfo, restart);
    StopReq::setStopAbort(stopReq->requestInfo, abort);
    StopReq::setNoStart(stopReq->requestInfo, nostart);
    StopReq::setInitialStart(stopReq->requestInfo, initialStart);
  }

  // send the signals
  NdbNodeBitmask nodes;
  NodeId nodeId= 0;
  int use_master_node= 0;
  int do_send= 0;
  *stopSelf= 0;
  NdbNodeBitmask nodes_to_stop;
  {
    for (unsigned i= 0; i < node_ids.size(); i++)
    {
      nodeId= node_ids[i];
      ndbout << "asked to stop " << nodeId << endl;

      if ((getNodeType(nodeId) != NDB_MGM_NODE_TYPE_MGM)
          &&(getNodeType(nodeId) != NDB_MGM_NODE_TYPE_NDB))
        DBUG_RETURN(WRONG_PROCESS_TYPE);

      if (getNodeType(nodeId) != NDB_MGM_NODE_TYPE_MGM)
        nodes_to_stop.set(nodeId);
      else if (nodeId != getOwnNodeId())
      {
        error= sendStopMgmd(nodeId, abort, stop, restart,
                            nostart, initialStart);
        if (error == 0)
          stoppedNodes.set(nodeId);
      }
      else
      {
        ndbout << "which is me" << endl;
        *stopSelf= (restart)? -1 : 1;
        stoppedNodes.set(nodeId);
      }
    }
  }
  int no_of_nodes_to_stop= nodes_to_stop.count();
  if (node_ids.size())
  {
    if (no_of_nodes_to_stop)
    {
      do_send= 1;
      if (no_of_nodes_to_stop == 1)
      {
        nodeId= nodes_to_stop.find(0);
      }
      else // multi node stop, send to master
      {
        use_master_node= 1;
        nodes_to_stop.copyto(NdbNodeBitmask::Size, stopReq->nodes);
        StopReq::setStopNodes(stopReq->requestInfo, 1);
      }
    }
  }
  else
  {
    nodeId= 0;
    while(getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB))
    {
      if(okToSendTo(nodeId, true) == 0)
      {
	SendStatus result = ss.sendSignal(nodeId, &ssig);
	if (result == SEND_OK)
	  nodes.set(nodeId);
      }
    }
  }

  // now wait for the replies
  while (!nodes.isclear() || do_send)
  {
    if (do_send)
    {
      int r;
      assert(nodes.count() == 0);
      if (use_master_node)
        nodeId= m_master_node;
      if ((r= okToSendTo(nodeId, true)) != 0)
      {
        bool next;
        if (!use_master_node)
          DBUG_RETURN(r);
        m_master_node= nodeId= 0;
        while((next= getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB)) == true &&
              (r= okToSendTo(nodeId, true)) != 0);
        if (!next)
          DBUG_RETURN(NO_CONTACT_WITH_DB_NODES);
      }
      if (ss.sendSignal(nodeId, &ssig) != SEND_OK)
        DBUG_RETURN(SEND_OR_RECEIVE_FAILED);
      nodes.set(nodeId);
      do_send= 0;
    }
    SimpleSignal *signal = ss.waitFor();
    int gsn = signal->readSignalNumber();
    switch (gsn) {
    case GSN_STOP_REF:{
      const StopRef * const ref = CAST_CONSTPTR(StopRef, signal->getDataPtr());
      const NodeId nodeId = refToNode(signal->header.theSendersBlockRef);
#ifdef VM_TRACE
      ndbout_c("Node %d refused stop", nodeId);
#endif
      assert(nodes.get(nodeId));
      nodes.clear(nodeId);
      if (ref->errorCode == StopRef::MultiNodeShutdownNotMaster)
      {
        assert(use_master_node);
        m_master_node= ref->masterNodeId;
        do_send= 1;
        continue;
      }
      error = translateStopRef(ref->errorCode);
      break;
    }
    case GSN_STOP_CONF:{
#ifdef NOT_USED
      const StopConf * const ref = CAST_CONSTPTR(StopConf, signal->getDataPtr());
#endif
      const NodeId nodeId = refToNode(signal->header.theSendersBlockRef);
#ifdef VM_TRACE
      ndbout_c("Node %d single user mode", nodeId);
#endif
      assert(nodes.get(nodeId));
      if (singleUserNodeId != 0)
      {
        stoppedNodes.set(nodeId);
      }
      else
      {
        assert(no_of_nodes_to_stop > 1);
        stoppedNodes.bitOR(nodes_to_stop);
      }
      nodes.clear(nodeId);
      break;
    }
    case GSN_NF_COMPLETEREP:{
      const NFCompleteRep * const rep =
	CAST_CONSTPTR(NFCompleteRep, signal->getDataPtr());
#ifdef VM_TRACE
      ndbout_c("sendSTOP_REQ Node %d fail completed", rep->failedNodeId);
#endif
      nodes.clear(rep->failedNodeId); // clear the failed node
      if (singleUserNodeId == 0)
        stoppedNodes.set(rep->failedNodeId);
      break;
    }
    case GSN_NODE_FAILREP:{
      const NodeFailRep * const rep =
	CAST_CONSTPTR(NodeFailRep, signal->getDataPtr());
      NdbNodeBitmask mask;
      mask.assign(NdbNodeBitmask::Size, rep->theNodes);
      mask.bitANDC(notstarted);
      nodes.bitANDC(mask);
      
      if (singleUserNodeId == 0)
	stoppedNodes.bitOR(mask);
      break;
    }
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      continue;
    default:
      report_unknown_signal(signal);
#ifdef VM_TRACE
      ndbout_c("Unknown signal %d", gsn);
#endif
      DBUG_RETURN(SEND_OR_RECEIVE_FAILED);
    }
  }
  if (error && *stopSelf)
  {
    *stopSelf= 0;
  }
  DBUG_RETURN(error);
}

/*
 * Stop one nodes
 */

int MgmtSrvr::stopNodes(const Vector<NodeId> &node_ids,
                        int *stopCount, bool abort, int* stopSelf)
{
  /*
    verify that no nodes are starting before stopping as this would cause
    the starting node to shutdown
  */
  if (!abort && check_nodes_starting())
    return OPERATION_NOT_ALLOWED_START_STOP;

  NdbNodeBitmask nodes;
  int ret= sendSTOP_REQ(node_ids,
                        nodes,
                        0,
                        abort,
                        false,
                        false,
                        false,
                        false,
                        stopSelf);
  if (stopCount)
    *stopCount= nodes.count();
  return ret;
}

int MgmtSrvr::shutdownMGM(int *stopCount, bool abort, int *stopSelf)
{
  NodeId nodeId = 0;
  int error;

  while(getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_MGM))
  {
    if(nodeId==getOwnNodeId())
      continue;
    error= sendStopMgmd(nodeId, abort, true, false,
                        false, false);
    if (error == 0)
      *stopCount++;
  }

  *stopSelf= 1;
  *stopCount++;

  return 0;
}

/*
 * Perform DB nodes shutdown.
 * MGM servers are left in their current state
 */

int MgmtSrvr::shutdownDB(int * stopCount, bool abort)
{
  NdbNodeBitmask nodes;
  Vector<NodeId> node_ids;

  int tmp;

  int ret = sendSTOP_REQ(node_ids,
			 nodes,
			 0,
			 abort,
			 true,
			 false,
			 false,
			 false,
                         &tmp);
  if (stopCount)
    *stopCount = nodes.count();
  return ret;
}

/*
 * Enter single user mode on all live nodes
 */

int MgmtSrvr::enterSingleUser(int * stopCount, Uint32 singleUserNodeId)
{
  if (getNodeType(singleUserNodeId) != NDB_MGM_NODE_TYPE_API)
    return NODE_NOT_API_NODE;
  NdbNodeBitmask nodes;
  Vector<NodeId> node_ids;
  int stopSelf;
  int ret = sendSTOP_REQ(node_ids,
			 nodes,
			 singleUserNodeId,
			 false,
			 false,
			 false,
			 false,
			 false,
                         &stopSelf);
  if (stopCount)
    *stopCount = nodes.count();
  return ret;
}

/*
 * Perform node restart
 */

int MgmtSrvr::check_nodes_stopping()
{
  NodeId nodeId = 0;
  ClusterMgr::Node node;
  while(getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB))
  {
    node = theFacade->theClusterMgr->getNodeInfo(nodeId);
    if((node.m_state.startLevel == NodeState::SL_STOPPING_1) || 
       (node.m_state.startLevel == NodeState::SL_STOPPING_2) || 
       (node.m_state.startLevel == NodeState::SL_STOPPING_3) || 
       (node.m_state.startLevel == NodeState::SL_STOPPING_4))
      return 1;
  }
  return 0;
}

int MgmtSrvr::check_nodes_starting()
{
  NodeId nodeId = 0;
  ClusterMgr::Node node;
  while(getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB))
  {
    node = theFacade->theClusterMgr->getNodeInfo(nodeId);
    if((node.m_state.startLevel == NodeState::SL_STARTING))
      return 1;
  }
  return 0;
}

int MgmtSrvr::restartNodes(const Vector<NodeId> &node_ids,
                           int * stopCount, bool nostart,
                           bool initialStart, bool abort,
                           int *stopSelf)
{
  /*
    verify that no nodes are starting before stopping as this would cause
    the starting node to shutdown
  */
  if (!abort && check_nodes_starting())
    return OPERATION_NOT_ALLOWED_START_STOP;

  NdbNodeBitmask nodes;
  int ret= sendSTOP_REQ(node_ids,
                        nodes,
                        0,
                        abort,
                        false,
                        true,
                        true,
                        initialStart,
                        stopSelf);

  if (ret)
    return ret;

  if (stopCount)
    *stopCount = nodes.count();
  
  // start up the nodes again
  NDB_TICKS waitTime = 12000;
  NDB_TICKS maxTime = NdbTick_CurrentMillisecond() + waitTime;
  for (unsigned i = 0; i < node_ids.size(); i++)
  {
    NodeId nodeId= node_ids[i];
    enum ndb_mgm_node_status s;
    s = NDB_MGM_NODE_STATUS_NO_CONTACT;
#ifdef VM_TRACE
    ndbout_c("Waiting for %d not started", nodeId);
#endif
    while (s != NDB_MGM_NODE_STATUS_NOT_STARTED && waitTime > 0)
    {
      Uint32 startPhase = 0, version = 0, dynamicId = 0, nodeGroup = 0;
      Uint32 mysql_version = 0;
      Uint32 connectCount = 0;
      bool system;
      const char *address= NULL;
      status(nodeId, &s, &version, &mysql_version, &startPhase, 
             &system, &dynamicId, &nodeGroup, &connectCount, &address);
      NdbSleep_MilliSleep(100);  
      waitTime = (maxTime - NdbTick_CurrentMillisecond());
    }
  }

  if (nostart)
    return 0;

  /*
    verify that no nodes are stopping before starting as this would cause
    the starting node to shutdown
  */
  int retry= 600*10;
  for (;check_nodes_stopping();)
  {
    if (--retry)
      break;
    NdbSleep_MilliSleep(100);
  }

  /*
    start the nodes
  */
  for (unsigned i = 0; i < node_ids.size(); i++)
  {
    (void) start(node_ids[i]);
  }
  return 0;
}

/*
 * Perform restart of all DB nodes
 */

int MgmtSrvr::restartDB(bool nostart, bool initialStart,
                        bool abort, int * stopCount)
{
  NdbNodeBitmask nodes;
  Vector<NodeId> node_ids;
  int tmp;

  int ret = sendSTOP_REQ(node_ids,
			 nodes,
			 0,
			 abort,
			 true,
			 true,
			 true,
			 initialStart,
                         &tmp);

  if (ret)
    return ret;

  if (stopCount)
    *stopCount = nodes.count();

#ifdef VM_TRACE
    ndbout_c("Stopped %d nodes", nodes.count());
#endif
  /**
   * Here all nodes were correctly stopped,
   * so we wait for all nodes to be contactable
   */
  NDB_TICKS waitTime = 12000;
  NodeId nodeId = 0;
  NDB_TICKS maxTime = NdbTick_CurrentMillisecond() + waitTime;

  while(getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB)) {
    if (!nodes.get(nodeId))
      continue;
    enum ndb_mgm_node_status s;
    s = NDB_MGM_NODE_STATUS_NO_CONTACT;
#ifdef VM_TRACE
    ndbout_c("Waiting for %d not started", nodeId);
#endif
    while (s != NDB_MGM_NODE_STATUS_NOT_STARTED && waitTime > 0) {
      Uint32 startPhase = 0, version = 0, dynamicId = 0, nodeGroup = 0;
      Uint32 mysql_version = 0;
      Uint32 connectCount = 0;
      bool system;
      const char *address;
      status(nodeId, &s, &version, &mysql_version, &startPhase, 
	     &system, &dynamicId, &nodeGroup, &connectCount, &address);
      NdbSleep_MilliSleep(100);  
      waitTime = (maxTime - NdbTick_CurrentMillisecond());
    }
  }
  
  if(nostart)
    return 0;
  
  /**
   * Now we start all database nodes (i.e. we make them non-idle)
   * We ignore the result we get from the start command.
   */
  nodeId = 0;
  while(getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB)) {
    if (!nodes.get(nodeId))
      continue;
    int result;
    result = start(nodeId);
    g_eventLogger->debug("Started node %d with result %d", nodeId, result);
    /**
     * Errors from this call are deliberately ignored.
     * Maybe the user only wanted to restart a subset of the nodes.
     * It is also easy for the user to check which nodes have 
     * started and which nodes have not.
     */
  }
  
  return 0;
}

int
MgmtSrvr::exitSingleUser(int * stopCount, bool abort)
{
  NodeId nodeId = 0;
  int count = 0;

  SignalSender ss(theFacade);
  ss.lock(); // lock will be released on exit

  SimpleSignal ssig;
  ResumeReq* const resumeReq = 
    CAST_PTR(ResumeReq, ssig.getDataPtrSend());

  ssig.set(ss,TestOrd::TraceAPI, NDBCNTR, GSN_RESUME_REQ, 
	   ResumeReq::SignalLength);
  resumeReq->senderData = 12;
  resumeReq->senderRef = ss.getOwnRef();

  while(getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB)){
    if(okToSendTo(nodeId, true) == 0){
      SendStatus result = ss.sendSignal(nodeId, &ssig);
      if (result == SEND_OK)
	count++;
    }
  }

  if(stopCount != 0)
    * stopCount = count;

  return 0;
}

/*****************************************************************************
 * Status
 ****************************************************************************/

#include <ClusterMgr.hpp>

void
MgmtSrvr::updateStatus()
{
  theFacade->theClusterMgr->forceHB();
}

int 
MgmtSrvr::status(int nodeId, 
                 ndb_mgm_node_status * _status, 
		 Uint32 * version,
		 Uint32 * mysql_version,
		 Uint32 * _phase, 
		 bool * _system,
		 Uint32 * dynamic,
		 Uint32 * nodegroup,
		 Uint32 * connectCount,
		 const char **address)
{
  if (getNodeType(nodeId) == NDB_MGM_NODE_TYPE_API ||
      getNodeType(nodeId) == NDB_MGM_NODE_TYPE_MGM) {
    versionNode(nodeId, *version, *mysql_version, address);
  } else {
    *address= get_connect_address(nodeId);
  }

  const ClusterMgr::Node node = 
    theFacade->theClusterMgr->getNodeInfo(nodeId);

  if(!node.connected){
    * _status = NDB_MGM_NODE_STATUS_NO_CONTACT;
    return 0;
  }
  
  if (getNodeType(nodeId) == NDB_MGM_NODE_TYPE_NDB) {
    * version = node.m_info.m_version;
    * mysql_version = node.m_info.m_mysql_version;
  }

  * dynamic = node.m_state.dynamicId;
  * nodegroup = node.m_state.nodeGroup;
  * connectCount = node.m_info.m_connectCount;
  
  switch(node.m_state.startLevel){
  case NodeState::SL_CMVMI:
    * _status = NDB_MGM_NODE_STATUS_NOT_STARTED;
    * _phase = 0;
    return 0;
    break;
  case NodeState::SL_STARTING:
    * _status     = NDB_MGM_NODE_STATUS_STARTING;
    * _phase = node.m_state.starting.startPhase;
    return 0;
    break;
  case NodeState::SL_STARTED:
    * _status = NDB_MGM_NODE_STATUS_STARTED;
    * _phase = 0;
    return 0;
    break;
  case NodeState::SL_STOPPING_1:
    * _status = NDB_MGM_NODE_STATUS_SHUTTING_DOWN;
    * _phase = 1;
    * _system = node.m_state.stopping.systemShutdown != 0;
    return 0;
    break;
  case NodeState::SL_STOPPING_2:
    * _status = NDB_MGM_NODE_STATUS_SHUTTING_DOWN;
    * _phase = 2;
    * _system = node.m_state.stopping.systemShutdown != 0;
    return 0;
    break;
  case NodeState::SL_STOPPING_3:
    * _status = NDB_MGM_NODE_STATUS_SHUTTING_DOWN;
    * _phase = 3;
    * _system = node.m_state.stopping.systemShutdown != 0;
    return 0;
    break;
  case NodeState::SL_STOPPING_4:
    * _status = NDB_MGM_NODE_STATUS_SHUTTING_DOWN;
    * _phase = 4;
    * _system = node.m_state.stopping.systemShutdown != 0;
    return 0;
    break;
  case NodeState::SL_SINGLEUSER:
    * _status = NDB_MGM_NODE_STATUS_SINGLEUSER;
    * _phase  = 0;
    return 0;
    break;
  default:
    * _status = NDB_MGM_NODE_STATUS_UNKNOWN;
    * _phase = 0;
    return 0;
  }
  
  return -1;
}

int 
MgmtSrvr::setEventReportingLevelImpl(int nodeId_arg, 
				     const EventSubscribeReq& ll)
{
  SignalSender ss(theFacade);
  NdbNodeBitmask nodes;
  nodes.clear();
  while (1)
  {
    Uint32 nodeId, max;
    ss.lock();
    SimpleSignal ssig;
    EventSubscribeReq * dst = 
      CAST_PTR(EventSubscribeReq, ssig.getDataPtrSend());
    ssig.set(ss,TestOrd::TraceAPI, CMVMI, GSN_EVENT_SUBSCRIBE_REQ,
             EventSubscribeReq::SignalLength);
    *dst = ll;

    if (nodeId_arg == 0)
    {
      // all nodes
      nodeId = 1;
      max = MAX_NDB_NODES;
    }
    else
    {
      // only one node
      max = nodeId = nodeId_arg;
    }
    // first make sure nodes are sendable
    for(; nodeId <= max; nodeId++)
    {
      if (nodeTypes[nodeId] != NODE_TYPE_DB)
        continue;
      if (okToSendTo(nodeId, true))
      {
        if (theFacade->theClusterMgr->getNodeInfo(nodeId).connected  == false)
        {
          // node not connected we can safely skip this one
          continue;
        }
        // api_reg_conf not recevied yet, need to retry
        return SEND_OR_RECEIVE_FAILED;
      }
    }

    if (nodeId_arg == 0)
    {
      // all nodes
      nodeId = 1;
      max = MAX_NDB_NODES;
    }
    else
    {
      // only one node
      max = nodeId = nodeId_arg;
    }
    // now send to all sendable nodes nodes
    // note, lock is held, so states have not changed
    for(; (Uint32) nodeId <= max; nodeId++)
    {
      if (nodeTypes[nodeId] != NODE_TYPE_DB)
        continue;
      if (theFacade->theClusterMgr->getNodeInfo(nodeId).connected  == false)
        continue; // node is not connected, skip
      if (ss.sendSignal(nodeId, &ssig) == SEND_OK)
        nodes.set(nodeId);
      else if (max == nodeId)
      {
        return SEND_OR_RECEIVE_FAILED;
      }
    }
    break;
  }

  if (nodes.isclear())
  {
    return SEND_OR_RECEIVE_FAILED;
  }
  
  int error = 0;
  while (!nodes.isclear())
  {
    Uint32 nodeId;
    SimpleSignal *signal = ss.waitFor();
    int gsn = signal->readSignalNumber();
    nodeId = refToNode(signal->header.theSendersBlockRef);
    switch (gsn) {
    case GSN_EVENT_SUBSCRIBE_CONF:{
      nodes.clear(nodeId);
      break;
    }
    case GSN_EVENT_SUBSCRIBE_REF:{
      nodes.clear(nodeId);
      error = 1;
      break;
    }
      // Since sending okToSend(true), 
      // there is no guarantee that NF_COMPLETEREP will come
      // i.e listen also to NODE_FAILREP
    case GSN_NODE_FAILREP: {
      const NodeFailRep * const rep =
	CAST_CONSTPTR(NodeFailRep, signal->getDataPtr());
      NdbNodeBitmask mask;
      mask.assign(NdbNodeBitmask::Size, rep->theNodes);
      nodes.bitANDC(mask);
      break;
    }
      
    case GSN_NF_COMPLETEREP:{
      const NFCompleteRep * const rep =
	CAST_CONSTPTR(NFCompleteRep, signal->getDataPtr());
      nodes.clear(rep->failedNodeId);
      break;
    }
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      continue;
    default:
      report_unknown_signal(signal);
      return SEND_OR_RECEIVE_FAILED;
    }
  }
  if (error)
    return SEND_OR_RECEIVE_FAILED;
  return 0;
}

//****************************************************************************
//****************************************************************************
int 
MgmtSrvr::setNodeLogLevelImpl(int nodeId, const SetLogLevelOrd & ll)
{
  INIT_SIGNAL_SENDER(ss,nodeId);

  SimpleSignal ssig;
  ssig.set(ss,TestOrd::TraceAPI, CMVMI, GSN_SET_LOGLEVELORD,
	   SetLogLevelOrd::SignalLength);
  SetLogLevelOrd* const dst = CAST_PTR(SetLogLevelOrd, ssig.getDataPtrSend());
  *dst = ll;
  
  return ss.sendSignal(nodeId, &ssig) == SEND_OK ? 0 : SEND_OR_RECEIVE_FAILED;
}

//****************************************************************************
//****************************************************************************

int 
MgmtSrvr::insertError(int nodeId, int errorNo) 
{
  int block;

  if (errorNo < 0) {
    return INVALID_ERROR_NUMBER;
  }

  SignalSender ss(theFacade);
  ss.lock(); /* lock will be released on exit */

  if(getNodeType(nodeId) == NDB_MGM_NODE_TYPE_NDB)
  {
    block= CMVMI;
  }
  else if(nodeId == _ownNodeId)
  {
    g_errorInsert= errorNo;
    return 0;
  }
  else if(getNodeType(nodeId) == NDB_MGM_NODE_TYPE_MGM)
    block= _blockNumber;
  else
    return WRONG_PROCESS_TYPE;

  SimpleSignal ssig;
  ssig.set(ss,TestOrd::TraceAPI, block, GSN_TAMPER_ORD, 
	   TamperOrd::SignalLength);
  TamperOrd* const tamperOrd = CAST_PTR(TamperOrd, ssig.getDataPtrSend());
  tamperOrd->errorNo = errorNo;

  return ss.sendSignal(nodeId, &ssig) == SEND_OK ? 0 : SEND_OR_RECEIVE_FAILED;
}


int
MgmtSrvr::startSchemaTrans(SignalSender& ss, NodeId & out_nodeId,
                           Uint32 transId, Uint32 & out_transKey)
{
  SimpleSignal ssig;

  ssig.set(ss, 0, DBDICT, GSN_SCHEMA_TRANS_BEGIN_REQ,
           SchemaTransBeginReq::SignalLength);

  SchemaTransBeginReq* req =
    CAST_PTR(SchemaTransBeginReq, ssig.getDataPtrSend());

  req->clientRef =  ss.getOwnRef();
  req->transId = transId;
  req->requestInfo = 0;

  NodeId nodeId = ss.get_an_alive_node();

retry:
  if (ss.get_node_alive(nodeId) == false)
  {
    nodeId = ss.get_an_alive_node();
  }

  if (ss.sendSignal(nodeId, &ssig) != SEND_OK)
  {
    return SEND_OR_RECEIVE_FAILED;
  }

  while (true)
  {
    SimpleSignal *signal = ss.waitFor();
    int gsn = signal->readSignalNumber();
    switch (gsn) {
    case GSN_SCHEMA_TRANS_BEGIN_CONF: {
      const SchemaTransBeginConf * conf =
        CAST_CONSTPTR(SchemaTransBeginConf, signal->getDataPtr());
      out_transKey = conf->transKey;
      out_nodeId = nodeId;
      return 0;
    }
    case GSN_SCHEMA_TRANS_BEGIN_REF: {
      const SchemaTransBeginRef * ref =
        CAST_CONSTPTR(SchemaTransBeginRef, signal->getDataPtr());

      switch(ref->errorCode){
      case SchemaTransBeginRef::NotMaster:
        nodeId = ref->masterNodeId;
        // Fall-through
      case SchemaTransBeginRef::Busy:
      case SchemaTransBeginRef::BusyWithNR:
        goto retry;
      default:
        return ref->errorCode;
      }
    }
    case GSN_NF_COMPLETEREP:
      // ignore
      break;
    case GSN_NODE_FAILREP:{
      const NodeFailRep * const rep =
        CAST_CONSTPTR(NodeFailRep, signal->getDataPtr());
      if (NdbNodeBitmask::get(rep->theNodes, nodeId))
      {
        nodeId++;
        goto retry;
      }
      break;
    }
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      break;
    default:
      report_unknown_signal(signal);
      return SEND_OR_RECEIVE_FAILED;
    }
  }
}

int
MgmtSrvr::endSchemaTrans(SignalSender& ss, NodeId nodeId,
                         Uint32 transId, Uint32 transKey,
                         Uint32 flags)
{
  SimpleSignal ssig;

  ssig.set(ss, 0, DBDICT, GSN_SCHEMA_TRANS_END_REQ,
           SchemaTransEndReq::SignalLength);

  SchemaTransEndReq* req =
    CAST_PTR(SchemaTransEndReq, ssig.getDataPtrSend());

  req->clientRef =  ss.getOwnRef();
  req->transId = transId;
  req->requestInfo = 0;
  req->transKey = transKey;
  req->flags = flags;

  if (ss.sendSignal(nodeId, &ssig) != SEND_OK)
  {
    return SEND_OR_RECEIVE_FAILED;
  }

  while (true)
  {
    SimpleSignal *signal = ss.waitFor();
    int gsn = signal->readSignalNumber();
    switch (gsn) {
    case GSN_SCHEMA_TRANS_END_CONF: {
      return 0;
    }
    case GSN_SCHEMA_TRANS_END_REF: {
      const SchemaTransEndRef * ref =
        CAST_CONSTPTR(SchemaTransEndRef, signal->getDataPtr());
      return ref->errorCode;
    }
    case GSN_NF_COMPLETEREP:
      // ignore
      break;
    case GSN_NODE_FAILREP:{
      const NodeFailRep * const rep =
        CAST_CONSTPTR(NodeFailRep, signal->getDataPtr());
      if (NdbNodeBitmask::get(rep->theNodes, nodeId))
      {
        return -1;
      }
      break;
    }
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      break;
    default:
      report_unknown_signal(signal);
      return SEND_OR_RECEIVE_FAILED;
    }
  }
}

int
MgmtSrvr::createNodegroup(int *nodes, int count, int *ng)
{
  int res;
  SignalSender ss(theFacade);
  ss.lock();

  Uint32 transId = rand();
  Uint32 transKey;
  NodeId nodeId;

  if ((res = startSchemaTrans(ss, nodeId, transId, transKey)))
  {
    return res;
  }

  SimpleSignal ssig;
  ssig.set(ss, 0, DBDICT, GSN_CREATE_NODEGROUP_REQ,
           CreateNodegroupReq::SignalLength);

  CreateNodegroupReq* req =
    CAST_PTR(CreateNodegroupReq, ssig.getDataPtrSend());

  req->transId = transId;
  req->transKey = transKey;
  req->nodegroupId = RNIL;
  req->senderData = 77;
  req->senderRef = ss.getOwnRef();
  bzero(req->nodes, sizeof(req->nodes));

  if (ng)
  {
    if (* ng != -1)
    {
      req->nodegroupId = * ng;
    }
  }
  for (int i = 0; i<count && i<(int)NDB_ARRAY_SIZE(req->nodes); i++)
  {
    req->nodes[i] = nodes[i];
  }

  if (ss.sendSignal(nodeId, &ssig) != SEND_OK)
  {
    return SEND_OR_RECEIVE_FAILED;
  }

  bool wait = true;
  while (wait)
  {
    SimpleSignal *signal = ss.waitFor();
    int gsn = signal->readSignalNumber();
    switch (gsn) {
    case GSN_CREATE_NODEGROUP_CONF: {
      const CreateNodegroupConf * conf =
        CAST_CONSTPTR(CreateNodegroupConf, signal->getDataPtr());

      if (ng)
      {
        * ng = conf->nodegroupId;
      }

      wait = false;
      break;
    }
    case GSN_CREATE_NODEGROUP_REF:{
      const CreateNodegroupRef * ref =
        CAST_CONSTPTR(CreateNodegroupRef, signal->getDataPtr());
      endSchemaTrans(ss, nodeId, transId, transKey,
                     SchemaTransEndReq::SchemaTransAbort);
      return ref->errorCode;
    }
    case GSN_NF_COMPLETEREP:
      // ignore
      break;
    case GSN_NODE_FAILREP:{
      const NodeFailRep * const rep =
        CAST_CONSTPTR(NodeFailRep, signal->getDataPtr());
      if (NdbNodeBitmask::get(rep->theNodes, nodeId))
      {
        return SchemaTransBeginRef::Nodefailure;
      }
      break;
    }
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      break;
    default:
      report_unknown_signal(signal);
      return SEND_OR_RECEIVE_FAILED;
    }
  }

  return endSchemaTrans(ss, nodeId, transId, transKey, 0);
}

int
MgmtSrvr::dropNodegroup(int ng)
{
  int res;
  SignalSender ss(theFacade);
  ss.lock();

  Uint32 transId = rand();
  Uint32 transKey;
  NodeId nodeId;

  if ((res = startSchemaTrans(ss, nodeId, transId, transKey)))
  {
    return res;
  }

  SimpleSignal ssig;
  ssig.set(ss, 0, DBDICT, GSN_DROP_NODEGROUP_REQ, DropNodegroupReq::SignalLength);

  DropNodegroupReq* req =
    CAST_PTR(DropNodegroupReq, ssig.getDataPtrSend());

  req->transId = transId;
  req->transKey = transKey;
  req->nodegroupId = ng;
  req->senderData = 77;
  req->senderRef = ss.getOwnRef();

  if (ss.sendSignal(nodeId, &ssig) != SEND_OK)
  {
    return SEND_OR_RECEIVE_FAILED;
  }

  bool wait = true;
  while (wait)
  {
    SimpleSignal *signal = ss.waitFor();
    int gsn = signal->readSignalNumber();
    switch (gsn) {
    case GSN_DROP_NODEGROUP_CONF: {
      wait = false;
      break;
    }
    case GSN_DROP_NODEGROUP_REF:
    {
      const DropNodegroupRef * ref =
        CAST_CONSTPTR(DropNodegroupRef, signal->getDataPtr());
      endSchemaTrans(ss, nodeId, transId, transKey,
                     SchemaTransEndReq::SchemaTransAbort);
      return ref->errorCode;
    }
    case GSN_NF_COMPLETEREP:
      // ignore
      break;
    case GSN_NODE_FAILREP:{
      const NodeFailRep * const rep =
        CAST_CONSTPTR(NodeFailRep, signal->getDataPtr());
      if (NdbNodeBitmask::get(rep->theNodes, nodeId))
      {
        return SchemaTransBeginRef::Nodefailure;
      }
      break;
    }
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      break;
    default:
      report_unknown_signal(signal);
      return SEND_OR_RECEIVE_FAILED;
    }
  }

  return endSchemaTrans(ss, nodeId, transId, transKey, 0);
}


//****************************************************************************
//****************************************************************************

int 
MgmtSrvr::setTraceNo(int nodeId, int traceNo)
{
  if (traceNo < 0) {
    return INVALID_TRACE_NUMBER;
  }

  INIT_SIGNAL_SENDER(ss,nodeId);

  SimpleSignal ssig;
  ssig.set(ss,TestOrd::TraceAPI, CMVMI, GSN_TEST_ORD, TestOrd::SignalLength);
  TestOrd* const testOrd = CAST_PTR(TestOrd, ssig.getDataPtrSend());
  testOrd->clear();
  // Assume TRACE command causes toggling. Not really defined... ? TODO
  testOrd->setTraceCommand(TestOrd::Toggle, 
			   (TestOrd::TraceSpecification)traceNo);

  return ss.sendSignal(nodeId, &ssig) == SEND_OK ? 0 : SEND_OR_RECEIVE_FAILED;
}

//****************************************************************************
//****************************************************************************

int 
MgmtSrvr::getBlockNumber(const BaseString &blockName) 
{
  short bno = getBlockNo(blockName.c_str());
  if(bno != 0)
    return bno;
  return -1;
}

//****************************************************************************
//****************************************************************************

int 
MgmtSrvr::setSignalLoggingMode(int nodeId, LogMode mode, 
			       const Vector<BaseString>& blocks)
{
  INIT_SIGNAL_SENDER(ss,nodeId);

  // Convert from MgmtSrvr format...

  TestOrd::Command command;
  if (mode == Off) {
    command = TestOrd::Off;
  }
  else {
    command = TestOrd::On;
  }

  TestOrd::SignalLoggerSpecification logSpec;
  switch (mode) {
  case In:
    logSpec = TestOrd::InputSignals;
    break;
  case Out:
    logSpec = TestOrd::OutputSignals;
    break;
  case InOut:
    logSpec = TestOrd::InputOutputSignals;
    break;
  case Off:
    // In MgmtSrvr interface it's just possible to switch off all logging, both
    // "in" and "out" (this should probably be changed).
    logSpec = TestOrd::InputOutputSignals;
    break;
  default:
    ndbout_c("Unexpected value %d, MgmtSrvr::setSignalLoggingMode, line %d",
	     (unsigned)mode, __LINE__);
    assert(false);
    return -1;
  }

  SimpleSignal ssig;
  ssig.set(ss,TestOrd::TraceAPI, CMVMI, GSN_TEST_ORD, TestOrd::SignalLength);

  TestOrd* const testOrd = CAST_PTR(TestOrd, ssig.getDataPtrSend());
  testOrd->clear();
  
  if (blocks.size() == 0 || blocks[0] == "ALL") {
    // Logg command for all blocks
    testOrd->addSignalLoggerCommand(command, logSpec);
  } else {
    for(unsigned i = 0; i < blocks.size(); i++){
      int blockNumber = getBlockNumber(blocks[i]);
      if (blockNumber == -1) {
        return INVALID_BLOCK_NAME;
      }
      testOrd->addSignalLoggerCommand(blockNumber, command, logSpec);
    } // for
  } // else

  return ss.sendSignal(nodeId, &ssig) == SEND_OK ? 0 : SEND_OR_RECEIVE_FAILED;
}

/*****************************************************************************
 * Signal tracing
 *****************************************************************************/
int MgmtSrvr::startSignalTracing(int nodeId)
{
  INIT_SIGNAL_SENDER(ss,nodeId);
  
  SimpleSignal ssig;
  ssig.set(ss,TestOrd::TraceAPI, CMVMI, GSN_TEST_ORD, TestOrd::SignalLength);

  TestOrd* const testOrd = CAST_PTR(TestOrd, ssig.getDataPtrSend());
  testOrd->clear();
  testOrd->setTestCommand(TestOrd::On);

  return ss.sendSignal(nodeId, &ssig) == SEND_OK ? 0 : SEND_OR_RECEIVE_FAILED;
}

int 
MgmtSrvr::stopSignalTracing(int nodeId) 
{
  INIT_SIGNAL_SENDER(ss,nodeId);

  SimpleSignal ssig;
  ssig.set(ss,TestOrd::TraceAPI, CMVMI, GSN_TEST_ORD, TestOrd::SignalLength);
  TestOrd* const testOrd = CAST_PTR(TestOrd, ssig.getDataPtrSend());
  testOrd->clear();
  testOrd->setTestCommand(TestOrd::Off);

  return ss.sendSignal(nodeId, &ssig) == SEND_OK ? 0 : SEND_OR_RECEIVE_FAILED;
}


/*****************************************************************************
 * Dump state
 *****************************************************************************/

int
MgmtSrvr::dumpState(int nodeId, const char* args)
{
  // Convert the space separeted args 
  // string to an int array
  Uint32 args_array[25];
  Uint32 numArgs = 0;

  char buf[10];  
  int b  = 0;
  memset(buf, 0, 10);
  for (size_t i = 0; i <= strlen(args); i++){
    if (args[i] == ' ' || args[i] == 0){
      args_array[numArgs] = atoi(buf);
      numArgs++;
      memset(buf, 0, 10);
      b = 0;
    } else {
      buf[b] = args[i];
      b++;
    }    
  }
  
  return dumpState(nodeId, args_array, numArgs);
}

int
MgmtSrvr::dumpState(int nodeId, const Uint32 args[], Uint32 no)
{
  INIT_SIGNAL_SENDER(ss,nodeId);

  const Uint32 len = no > 25 ? 25 : no;
  
  SimpleSignal ssig;
  DumpStateOrd * const dumpOrd = 
    CAST_PTR(DumpStateOrd, ssig.getDataPtrSend());
  ssig.set(ss,TestOrd::TraceAPI, CMVMI, GSN_DUMP_STATE_ORD, len);
  for(Uint32 i = 0; i<25; i++){
    if (i < len)
      dumpOrd->args[i] = args[i];
    else
      dumpOrd->args[i] = 0;
  }
  
  return ss.sendSignal(nodeId, &ssig) == SEND_OK ? 0 : SEND_OR_RECEIVE_FAILED;
}


//****************************************************************************
//****************************************************************************

const char* MgmtSrvr::getErrorText(int errorCode, char *buf, int buf_sz)
{
  ndb_error_string(errorCode, buf, buf_sz);
  buf[buf_sz-1]= 0;
  return buf;
}

void MgmtSrvr::execDBINFO_SCANREQ(NdbApiSignal* signal)
{
#if 1
  (void)signal;
#else
  DbinfoScanReq req= *(DbinfoScanReq*) signal->getDataPtr();

  const Uint32 tableId= req.tableId;
  const Uint32 senderRef= req.senderRef;
  const Uint32 apiTxnId= req.apiTxnId;

  DbinfoScanReq *oreq= (DbinfoScanReq*)signal->getDataPtrSend();

  memcpy(signal->getDataPtrSend(),&req,signal->getLength()*sizeof(Uint32));

  char buf[1024];
  struct dbinfo_row r;
  struct dbinfo_ratelimit rl;
  int i;
  int startid= 0;

  switch(req.tableId)
  {
  case 3:
//  case NDBINFO_LOGDESTINATION_TABLEID:
/*    dbinfo_ratelimit_init(&rl, &req);

    if(!(req.requestInfo & DbinfoScanReq::StartScan))
      startid= req.cur_item;

    for(i=startid;
        dbinfo_ratelimit_continue(&rl)
          && 3+getLogger()->getHandlerCount();
        i++)
    {
      dbinfo_write_row_init(&r, buf, sizeof(buf));

      LogHandler* lh= getLogger()->getHandler(i);
      if(lh)
      {
        BaseString lparams;
        const char *s;
        lh->getParams(lparams);
        dbinfo_write_row_column_uint32(&r, getOwnNodeId());
        s= lh->handler_type();
        dbinfo_write_row_column(&r, s, strlen(s));
        s= lparams.c_str();
        dbinfo_write_row_column(&r, s, strlen(s));
        dbinfo_write_row_column_uint32(&r, lh->getCurrentSize());
        dbinfo_write_row_column_uint32(&r, lh->getMaxSize());
        dbinfo_send_row(signal,r,rl,apiTxnId,senderRef);
      }
    }
    if(!dbinfo_ratelimit_continue(&rl) && i < number_ndbinfo_tables)
    {
      dbinfo_ratelimit_sendconf(signal,req,rl,i);
    }
    else
    {
      DbinfoScanConf *conf= (DbinfoScanConf*)signal->getDataPtrSend();
      conf->tableId= req.tableId;
      conf->senderRef= req.senderRef;
      conf->apiTxnId= req.apiTxnId;
      conf->requestInfo= 0;
      sendSignal(req.senderRef, GSN_DBINFO_SCANCONF, signal,
                 DbinfoScanConf::SignalLength, JBB);
    }
*/    break;
  }
#endif
}

void
MgmtSrvr::handleReceivedSignal(NdbApiSignal* signal)
{
  int gsn = signal->readSignalNumber();

  switch (gsn) {
  case GSN_EVENT_REP:
  {
    eventReport(signal->getDataPtr(), signal->getLength());
    break;
  }

  case GSN_NF_COMPLETEREP:
    break;
  case GSN_NODE_FAILREP:
    break;

  case GSN_TAMPER_ORD:
    ndbout << "TAMPER ORD" << endl;
    break;
  case GSN_API_REGCONF:
  case GSN_TAKE_OVERTCCONF:
    break;

  case GSN_DBINFO_SCANREQ:
    execDBINFO_SCANREQ(signal);
    break;

  default:
    g_eventLogger->error("Unknown signal received. SignalNumber: "
                         "%i from (%d, 0x%x)",
                         gsn,
                         refToNode(signal->theSendersBlockRef),
                         refToBlock(signal->theSendersBlockRef));
    assert(false);
  }
}


void
MgmtSrvr::signalReceivedNotification(void* mgmtSrvr,
                                     NdbApiSignal* signal,
				     LinearSectionPtr ptr[3])
{
  ((MgmtSrvr*)mgmtSrvr)->handleReceivedSignal(signal);
}


void
MgmtSrvr::handleStatus(NodeId nodeId, bool alive, bool nfComplete)
{
  DBUG_ENTER("MgmtSrvr::handleStatus");
  DBUG_PRINT("enter",("nodeid: %d, alive: %d, nfComplete: %d",
                      nodeId, alive, nfComplete));

  Uint32 theData[25];
  EventReport *rep = (EventReport *)theData;

  theData[1] = nodeId;
  if (alive) {
    if (nodeTypes[nodeId] == NODE_TYPE_DB)
    {
      m_started_nodes.push_back(nodeId);
    }
    rep->setEventType(NDB_LE_Connected);
  } else {
    rep->setEventType(NDB_LE_Disconnected);
    if(nfComplete)
    {
      DBUG_VOID_RETURN;
    }
  }
  rep->setNodeId(_ownNodeId);
  eventReport(theData, 1);
  DBUG_VOID_RETURN;
}


void
MgmtSrvr::nodeStatusNotification(void* mgmSrv, Uint32 nodeId,
				 bool alive, bool nfComplete)
{
  ((MgmtSrvr*)mgmSrv)->handleStatus(nodeId, alive, nfComplete);
}


enum ndb_mgm_node_type 
MgmtSrvr::getNodeType(NodeId nodeId) const 
{
  if(nodeId >= MAX_NODES)
    return (enum ndb_mgm_node_type)-1;
  
  return nodeTypes[nodeId];
}

const char *MgmtSrvr::get_connect_address(Uint32 node_id)
{
  if (m_connect_address[node_id].s_addr == 0 &&
      theFacade && theFacade->theTransporterRegistry &&
      theFacade->theClusterMgr &&
      getNodeType(node_id) == NDB_MGM_NODE_TYPE_NDB) 
  {
    const ClusterMgr::Node &node=
      theFacade->theClusterMgr->getNodeInfo(node_id);
    if (node.connected)
    {
      m_connect_address[node_id]=
	theFacade->theTransporterRegistry->get_connect_address(node_id);
    }
  }
  return inet_ntoa(m_connect_address[node_id]);  
}

void
MgmtSrvr::get_connected_nodes(NodeBitmask &connected_nodes) const
{
  if (theFacade && theFacade->theClusterMgr)
  {
    for(Uint32 i = 0; i < MAX_NDB_NODES; i++)
    {
      if (getNodeType(i) == NDB_MGM_NODE_TYPE_NDB)
      {
	const ClusterMgr::Node &node= theFacade->theClusterMgr->getNodeInfo(i);
	connected_nodes.bitOR(node.m_state.m_connected_nodes);
      }
    }
  }
}

int
MgmtSrvr::alloc_node_id_req(NodeId free_node_id, enum ndb_mgm_node_type type)
{
  SignalSender ss(theFacade);
  ss.lock(); // lock will be released on exit

  SimpleSignal ssig;
  AllocNodeIdReq* req = CAST_PTR(AllocNodeIdReq, ssig.getDataPtrSend());
  ssig.set(ss, TestOrd::TraceAPI, QMGR, GSN_ALLOC_NODEID_REQ,
	   AllocNodeIdReq::SignalLength);
  
  req->senderRef = ss.getOwnRef();
  req->senderData = 19;
  req->nodeId = free_node_id;
  req->nodeType = type;

  int do_send = 1;
  NodeId nodeId = 0;
  while (1)
  {
    if (nodeId == 0)
    {
      bool next;
      while((next = getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB)) == true &&
            theFacade->get_node_alive(nodeId) == false);
      if (!next)
        return NO_CONTACT_WITH_DB_NODES;
      do_send = 1;
    }
    if (do_send)
    {
      if (ss.sendSignal(nodeId, &ssig) != SEND_OK) {
        return SEND_OR_RECEIVE_FAILED;
      }
      do_send = 0;
    }
    
    SimpleSignal *signal = ss.waitFor();

    int gsn = signal->readSignalNumber();
    switch (gsn) {
    case GSN_ALLOC_NODEID_CONF:
    {
#ifdef NOT_USED
      const AllocNodeIdConf * const conf =
        CAST_CONSTPTR(AllocNodeIdConf, signal->getDataPtr());
#endif
      return 0;
    }
    case GSN_ALLOC_NODEID_REF:
    {
      const AllocNodeIdRef * const ref =
        CAST_CONSTPTR(AllocNodeIdRef, signal->getDataPtr());
      if (ref->errorCode == AllocNodeIdRef::NotMaster ||
          ref->errorCode == AllocNodeIdRef::Busy ||
          ref->errorCode == AllocNodeIdRef::NodeFailureHandlingNotCompleted)
      {
        do_send = 1;
        nodeId = refToNode(ref->masterRef);
	if (!theFacade->get_node_alive(nodeId))
	  nodeId = 0;
        if (ref->errorCode != AllocNodeIdRef::NotMaster)
        {
          /* sleep for a while (100ms) before retrying */
          NdbSleep_MilliSleep(100);  
        }
        continue;
      }
      return ref->errorCode;
    }
    case GSN_NF_COMPLETEREP:
    {
      const NFCompleteRep * const rep =
        CAST_CONSTPTR(NFCompleteRep, signal->getDataPtr());
#ifdef VM_TRACE
      ndbout_c("Node %d fail completed", rep->failedNodeId);
#endif
      if (rep->failedNodeId == nodeId)
      {
	do_send = 1;
        nodeId = 0;
      }
      continue;
    }
    case GSN_NODE_FAILREP:{
      // ignore NF_COMPLETEREP will come
      continue;
    }
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      continue;
    default:
      report_unknown_signal(signal);
      return SEND_OR_RECEIVE_FAILED;
    }
  }
  return 0;
}

bool
MgmtSrvr::alloc_node_id(NodeId * nodeId,
			enum ndb_mgm_node_type type,
			struct sockaddr *client_addr, 
			int &error_code, BaseString &error_string,
                        int log_event)
{
  DBUG_ENTER("MgmtSrvr::alloc_node_id");
  DBUG_PRINT("enter", ("nodeid: %d  type: %d  client_addr: 0x%ld",
		       *nodeId, type, (long) client_addr));
  if (m_opts.no_nodeid_checks) {
    if (*nodeId == 0) {
      error_string.appfmt("no-nodeid-checks set in management server.\n"
			  "node id must be set explicitly in connectstring");
      error_code = NDB_MGM_ALLOCID_CONFIG_MISMATCH;
      DBUG_RETURN(false);
    }
    DBUG_RETURN(true);
  }

  Guard g(m_node_id_mutex);
  int no_mgm= 0;
  NodeBitmask connected_nodes(m_reserved_nodes);
  get_connected_nodes(connected_nodes);
  {
    for(Uint32 i = 0; i < MAX_NODES; i++)
      if (getNodeType(i) == NDB_MGM_NODE_TYPE_MGM)
	no_mgm++;
  }
  bool found_matching_id= false;
  bool found_matching_type= false;
  bool found_free_node= false;
  unsigned id_found= 0;
  const char *config_hostname= 0;
  struct in_addr config_addr= {0};
  int r_config_addr= -1;
  unsigned type_c= 0;

  {
    Guard guard_config(m_local_config_mutex);
    ConfigIter iter(m_local_config, CFG_SECTION_NODE);
    for(iter.first(); iter.valid(); iter.next())
    {
      unsigned curr_nodeid = 0;
      require(!iter.get(CFG_NODE_ID, &curr_nodeid));
      if (*nodeId && *nodeId != curr_nodeid)
        continue;
      found_matching_id = true;

      require(!iter.get(CFG_TYPE_OF_SECTION, &type_c));
      if(type_c != (unsigned)type)
        continue;
      found_matching_type = true;

      if (connected_nodes.get(curr_nodeid))
        continue;
      found_free_node = true;

      require(!iter.get(CFG_NODE_HOST, &config_hostname));
      if (config_hostname && config_hostname[0] == 0)
        config_hostname = 0;
      else if (client_addr)
      {
        // check hostname compatibility
        const void *tmp_in = &(((sockaddr_in*)client_addr)->sin_addr);
        if((r_config_addr= Ndb_getInAddr(&config_addr, config_hostname)) != 0 ||
           memcmp(&config_addr, tmp_in, sizeof(config_addr)) != 0)
        {
          struct in_addr tmp_addr;
          if(Ndb_getInAddr(&tmp_addr, "localhost") != 0 ||
             memcmp(&tmp_addr, tmp_in, sizeof(config_addr)) != 0)
          {
            // not localhost
            continue;
          }

          // connecting through localhost
          // check if config_hostname is local
          if (!SocketServer::tryBind(0,config_hostname))
            continue;
        }
      }
      else
      {
        // client_addr == 0
        if (!SocketServer::tryBind(0,config_hostname))
          continue;
      }

      if (*nodeId != 0 ||
          type != NDB_MGM_NODE_TYPE_MGM ||
          no_mgm == 1)  // any match is ok
      {
        if (config_hostname == 0 &&
            *nodeId == 0 &&
            type != NDB_MGM_NODE_TYPE_MGM)
        {
          if (!id_found) // only set if not set earlier
            id_found = curr_nodeid;
          continue; /* continue looking for a nodeid with specified hostname */
        }
        assert(id_found == 0);
        id_found = curr_nodeid;
        break;
      }

      if (id_found) // mgmt server may only have one match
      {
        error_string.appfmt("Ambiguous node id's %d and %d. "
                            "Suggest specifying node id in connectstring, "
                            "or specifying unique host names in config file.",
                            id_found, curr_nodeid);
        error_code = NDB_MGM_ALLOCID_CONFIG_MISMATCH;
        DBUG_RETURN(false);
      }

      if (config_hostname == 0)
      {
        error_string.appfmt("Ambiguity for node id %d. "
                            "Suggest specifying node id in connectstring, "
                            "or specifying unique host names in config file, "
                            "or specifying just one mgmt server in "
                            "config file.",
                            curr_nodeid);
        error_code = NDB_MGM_ALLOCID_CONFIG_MISMATCH;
        DBUG_RETURN(false);
      }
      id_found = curr_nodeid; // mgmt server matched, check for more matches
    }
  }

  if (id_found && client_addr != 0)
  {
    int res = alloc_node_id_req(id_found, type);
    unsigned save_id_found = id_found;
    switch (res)
    {
    case 0:
      // ok continue
      break;
    case NO_CONTACT_WITH_DB_NODES:
      // ok continue
      break;
    default:
      // something wrong
      id_found = 0;
      break;

    }
    if (id_found == 0)
    {
      char buf[128];
      ndb_error_string(res, buf, sizeof(buf));
      error_string.appfmt("Cluster refused allocation of id %d. Error: %d (%s).",
			  save_id_found, res, buf);
      g_eventLogger->warning("Cluster refused allocation of id %d. "
                             "Connection from ip %s. "
                             "Returned error string \"%s\"", save_id_found,
                             inet_ntoa(((struct sockaddr_in *)
                                        (client_addr))->sin_addr),
                             error_string.c_str());
      DBUG_RETURN(false);
    }
  }

  if (id_found)
  {
    *nodeId= id_found;
    DBUG_PRINT("info", ("allocating node id %d",*nodeId));
    {
      int r= 0;
      if (client_addr)
	m_connect_address[id_found]=
	  ((struct sockaddr_in *)client_addr)->sin_addr;
      else if (config_hostname)
	r= Ndb_getInAddr(&(m_connect_address[id_found]), config_hostname);
      else {
	char name[256];
	r= gethostname(name, sizeof(name));
	if (r == 0) {
	  name[sizeof(name)-1]= 0;
	  r= Ndb_getInAddr(&(m_connect_address[id_found]), name);
	}
      }
      if (r)
	m_connect_address[id_found].s_addr= 0;
    }
    m_reserved_nodes.set(id_found);
    if (theFacade && id_found != theFacade->ownId())
    {
      /**
       * Make sure we're ready to accept connections from this node
       */
      theFacade->lock_mutex();
      theFacade->doConnect(id_found);
      theFacade->unlock_mutex();
    }
    
    char tmp_str[128];
    m_reserved_nodes.getText(tmp_str);
    g_eventLogger->info("Mgmt server state: nodeid %d reserved for ip %s, "
                        "m_reserved_nodes %s.",
                        id_found, get_connect_address(id_found), tmp_str);
    DBUG_RETURN(true);
  }

  if (found_matching_type && !found_free_node) {
    // we have a temporary error which might be due to that 
    // we have got the latest connect status from db-nodes.  Force update.
    updateStatus();
  }

  BaseString type_string, type_c_string;
  {
    const char *alias, *str;
    alias= ndb_mgm_get_node_type_alias_string(type, &str);
    type_string.assfmt("%s(%s)", alias, str);
    alias= ndb_mgm_get_node_type_alias_string((enum ndb_mgm_node_type)type_c,
					      &str);
    type_c_string.assfmt("%s(%s)", alias, str);
  }

  if (*nodeId == 0)
  {
    if (found_matching_id)
    {
      if (found_matching_type)
      {
	if (found_free_node)
        {
	  error_string.appfmt("Connection done from wrong host ip %s.",
			      (client_addr)?
                              inet_ntoa(((struct sockaddr_in *)
					 (client_addr))->sin_addr):"");
          error_code = NDB_MGM_ALLOCID_ERROR;
        }
	else
        {
	  error_string.appfmt("No free node id found for %s.",
			      type_string.c_str());
          error_code = NDB_MGM_ALLOCID_ERROR;
        }
      }
      else
      {
	error_string.appfmt("No %s node defined in config file.",
			    type_string.c_str());
        error_code = NDB_MGM_ALLOCID_CONFIG_MISMATCH;
      }
    }
    else
    {
      error_string.append("No nodes defined in config file.");
      error_code = NDB_MGM_ALLOCID_CONFIG_MISMATCH;
    }
  }
  else
  {
    if (found_matching_id)
    {
      if (found_matching_type)
      {
	if (found_free_node)
        {
	  // have to split these into two since inet_ntoa overwrites itself
	  error_string.appfmt("Connection with id %d done from wrong host ip %s,",
			      *nodeId, inet_ntoa(((struct sockaddr_in *)
						  (client_addr))->sin_addr));
	  error_string.appfmt(" expected %s(%s).", config_hostname,
			      r_config_addr ?
			      "lookup failed" : inet_ntoa(config_addr));
          error_code = NDB_MGM_ALLOCID_CONFIG_MISMATCH;
	}
        else
        {
	  error_string.appfmt("Id %d already allocated by another node.",
			      *nodeId);
          error_code = NDB_MGM_ALLOCID_ERROR;
        }
      }
      else
      {
	error_string.appfmt("Id %d configured as %s, connect attempted as %s.",
			    *nodeId, type_c_string.c_str(),
			    type_string.c_str());
        error_code = NDB_MGM_ALLOCID_CONFIG_MISMATCH;
      }
    }
    else
    {
      error_string.appfmt("No node defined with id=%d in config file.",
			  *nodeId);
      error_code = NDB_MGM_ALLOCID_CONFIG_MISMATCH;
    }
  }

  if (log_event || error_code == NDB_MGM_ALLOCID_CONFIG_MISMATCH)
  {
    g_eventLogger->warning("Allocate nodeid (%d) failed. Connection from ip %s."
			   " Returned error string \"%s\"",
			   *nodeId,
			   client_addr != 0
			   ? inet_ntoa(((struct sockaddr_in *)
					(client_addr))->sin_addr)
			   : "<none>",
			   error_string.c_str());

    NodeBitmask connected_nodes2;
    get_connected_nodes(connected_nodes2);
    BaseString tmp_connected, tmp_not_connected;
    for(Uint32 i = 0; i < MAX_NODES; i++)
    {
      if (connected_nodes2.get(i))
      {
	if (!m_reserved_nodes.get(i))
	  tmp_connected.appfmt(" %d", i);
      }
      else if (m_reserved_nodes.get(i))
      {
	tmp_not_connected.appfmt(" %d", i);
      }
    }
    if (tmp_connected.length() > 0)
      g_eventLogger->info("Mgmt server state: node id's %s connected but not reserved", 
			  tmp_connected.c_str());
    if (tmp_not_connected.length() > 0)
      g_eventLogger->info("Mgmt server state: node id's %s not connected but reserved",
			  tmp_not_connected.c_str());
  }
  DBUG_RETURN(false);
}


bool
MgmtSrvr::getNextNodeId(NodeId * nodeId, enum ndb_mgm_node_type type) const 
{
  NodeId tmp = * nodeId;

  tmp++;
  while(nodeTypes[tmp] != type && tmp < MAX_NODES)
    tmp++;
  
  if(tmp == MAX_NODES){
    return false;
  }

  * nodeId = tmp;
  return true;
}

#include "Services.hpp"

void
MgmtSrvr::eventReport(const Uint32 * theData, Uint32 len)
{
  const EventReport * const eventReport = (EventReport *)&theData[0];
  
  NodeId nodeId = eventReport->getNodeId();
  Ndb_logevent_type type = eventReport->getEventType();
  // Log event
  g_eventLogger->log(type, theData, len, nodeId, 
                     &m_event_listner[0].m_logLevel);
  m_event_listner.log(type, theData, len, nodeId);
}

/***************************************************************************
 * Backup
 ***************************************************************************/

int
MgmtSrvr::startBackup(Uint32& backupId, int waitCompleted, Uint32 input_backupId)
{
  SignalSender ss(theFacade);
  ss.lock(); // lock will be released on exit

  NodeId nodeId = m_master_node;
  if (okToSendTo(nodeId, false) != 0)
  {
    bool next;
    nodeId = m_master_node = 0;
    while((next = getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB)) == true &&
          okToSendTo(nodeId, false) != 0);
    if(!next)
      return NO_CONTACT_WITH_DB_NODES;
  }

  SimpleSignal ssig;
  BackupReq* req = CAST_PTR(BackupReq, ssig.getDataPtrSend());
  /*
   * Single-threaded backup.  Set instance key 1.  In the kernel
   * this maps to main instance 0 or worker instance 1 (if MT LQH).
   */
  BlockNumber backupBlockNo = numberToBlock(BACKUP, 1);
  if(input_backupId > 0)
  {
    ssig.set(ss, TestOrd::TraceAPI, backupBlockNo, GSN_BACKUP_REQ,
	     BackupReq::SignalLength);
    req->inputBackupId = input_backupId;
  }
  else
    ssig.set(ss, TestOrd::TraceAPI, backupBlockNo, GSN_BACKUP_REQ,
	     BackupReq::SignalLength - 1);
  
  req->senderData = 19;
  req->backupDataLen = 0;
  assert(waitCompleted < 3);
  req->flags = waitCompleted & 0x3;

  BackupEvent event;
  int do_send = 1;
  while (1) {
    if (do_send)
    {
      if (ss.sendSignal(nodeId, &ssig) != SEND_OK) {
	return SEND_OR_RECEIVE_FAILED;
      }
      if (waitCompleted == 0)
	return 0;
      do_send = 0;
    }
    SimpleSignal *signal = ss.waitFor();

    int gsn = signal->readSignalNumber();
    switch (gsn) {
    case GSN_BACKUP_CONF:{
      const BackupConf * const conf = 
	CAST_CONSTPTR(BackupConf, signal->getDataPtr());
      event.Event = BackupEvent::BackupStarted;
      event.Started.BackupId = conf->backupId;
      event.Nodes = conf->nodes;
#ifdef VM_TRACE
      ndbout_c("Backup(%d) master is %d", conf->backupId,
	       refToNode(signal->header.theSendersBlockRef));
#endif
      backupId = conf->backupId;
      if (waitCompleted == 1)
	return 0;
      // wait for next signal
      break;
    }
    case GSN_BACKUP_COMPLETE_REP:{
      const BackupCompleteRep * const rep = 
	CAST_CONSTPTR(BackupCompleteRep, signal->getDataPtr());
#ifdef VM_TRACE
      ndbout_c("Backup(%d) completed", rep->backupId);
#endif
      event.Event = BackupEvent::BackupCompleted;
      event.Completed.BackupId = rep->backupId;
    
      event.Completed.NoOfBytes = rep->noOfBytesLow;
      event.Completed.NoOfLogBytes = rep->noOfLogBytes;
      event.Completed.NoOfRecords = rep->noOfRecordsLow;
      event.Completed.NoOfLogRecords = rep->noOfLogRecords;
      event.Completed.stopGCP = rep->stopGCP;
      event.Completed.startGCP = rep->startGCP;
      event.Nodes = rep->nodes;

      if (signal->header.theLength >= BackupCompleteRep::SignalLength)
      {
        event.Completed.NoOfBytes += ((Uint64)rep->noOfBytesHigh) << 32;
        event.Completed.NoOfRecords += ((Uint64)rep->noOfRecordsHigh) << 32;
      }

      backupId = rep->backupId;
      return 0;
    }
    case GSN_BACKUP_REF:{
      const BackupRef * const ref = 
	CAST_CONSTPTR(BackupRef, signal->getDataPtr());
      if(ref->errorCode == BackupRef::IAmNotMaster){
	m_master_node = nodeId = refToNode(ref->masterRef);
#ifdef VM_TRACE
	ndbout_c("I'm not master resending to %d", nodeId);
#endif
	do_send = 1; // try again
	if (!theFacade->get_node_alive(nodeId))
	  m_master_node = nodeId = 0;
	continue;
      }
      event.Event = BackupEvent::BackupFailedToStart;
      event.FailedToStart.ErrorCode = ref->errorCode;
      return ref->errorCode;
    }
    case GSN_BACKUP_ABORT_REP:{
      const BackupAbortRep * const rep = 
	CAST_CONSTPTR(BackupAbortRep, signal->getDataPtr());
      event.Event = BackupEvent::BackupAborted;
      event.Aborted.Reason = rep->reason;
      event.Aborted.BackupId = rep->backupId;
      event.Aborted.ErrorCode = rep->reason;
#ifdef VM_TRACE
      ndbout_c("Backup %d aborted", rep->backupId);
#endif
      return rep->reason;
    }
    case GSN_NF_COMPLETEREP:{
      const NFCompleteRep * const rep =
	CAST_CONSTPTR(NFCompleteRep, signal->getDataPtr());
#ifdef VM_TRACE
      ndbout_c("Node %d fail completed", rep->failedNodeId);
#endif
      if (rep->failedNodeId == nodeId ||
	  waitCompleted == 1)
	return 1326;
      // wait for next signal
      // master node will report aborted backup
      break;
    }
    case GSN_NODE_FAILREP:{
      const NodeFailRep * const rep =
	CAST_CONSTPTR(NodeFailRep, signal->getDataPtr());
      if (NdbNodeBitmask::get(rep->theNodes,nodeId) ||
	  waitCompleted == 1)
	return 1326;
      // wait for next signal
      // master node will report aborted backup
      break;
    }
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      continue;
    default:
      report_unknown_signal(signal);
      return SEND_OR_RECEIVE_FAILED;
    }
  }
}

int 
MgmtSrvr::abortBackup(Uint32 backupId)
{
  SignalSender ss(theFacade);
  ss.lock(); // lock will be released on exit

  bool next;
  NodeId nodeId = 0;
  while((next = getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB)) == true &&
	theFacade->get_node_alive(nodeId) == false);
  
  if(!next){
    return NO_CONTACT_WITH_DB_NODES;
  }
  
  SimpleSignal ssig;

  AbortBackupOrd* ord = CAST_PTR(AbortBackupOrd, ssig.getDataPtrSend());
  ssig.set(ss, TestOrd::TraceAPI, BACKUP, GSN_ABORT_BACKUP_ORD, 
	   AbortBackupOrd::SignalLength);
  
  ord->requestType = AbortBackupOrd::ClientAbort;
  ord->senderData = 19;
  ord->backupId = backupId;
  
  return ss.sendSignal(nodeId, &ssig) == SEND_OK ? 0 : SEND_OR_RECEIVE_FAILED;
}


MgmtSrvr::Allocated_resources::Allocated_resources(MgmtSrvr &m)
  : m_mgmsrv(m)
{
  m_reserved_nodes.clear();
  m_alloc_timeout= 0;
}

MgmtSrvr::Allocated_resources::~Allocated_resources()
{
  Guard g(m_mgmsrv.m_node_id_mutex);
  if (!m_reserved_nodes.isclear()) {
    m_mgmsrv.m_reserved_nodes.bitANDC(m_reserved_nodes); 
    // node has been reserved, force update signal to ndb nodes
    m_mgmsrv.updateStatus();

    char tmp_str[128];
    m_mgmsrv.m_reserved_nodes.getText(tmp_str);
    g_eventLogger->info("Mgmt server state: nodeid %d freed, m_reserved_nodes %s.",
                        get_nodeid(), tmp_str);
  }
}

void
MgmtSrvr::Allocated_resources::reserve_node(NodeId id, NDB_TICKS timeout)
{
  m_reserved_nodes.set(id);
  m_alloc_timeout= NdbTick_CurrentMillisecond() + timeout;
}

bool
MgmtSrvr::Allocated_resources::is_timed_out(NDB_TICKS tick)
{
  if (m_alloc_timeout && tick > m_alloc_timeout)
  {
    g_eventLogger->info("Mgmt server state: nodeid %d timed out.",
                        get_nodeid());
    return true;
  }
  return false;
}

NodeId
MgmtSrvr::Allocated_resources::get_nodeid() const
{
  for(Uint32 i = 0; i < MAX_NODES; i++)
  {
    if (m_reserved_nodes.get(i))
      return i;
  }
  return 0;
}

int
MgmtSrvr::setDbParameter(int node, int param, const char * value,
			 BaseString& msg)
{

  Guard g(m_local_config_mutex);

  /**
   * Check parameter
   */
  ConfigIter iter(m_local_config, CFG_SECTION_NODE);
  if(iter.first() != 0){
    msg.assign("Unable to find node section (iter.first())");
    return -1;
  }

  Uint32 type = NODE_TYPE_DB + 1;
  if(node != 0){
    if(iter.find(CFG_NODE_ID, node) != 0){
      msg.assign("Unable to find node (iter.find())");
      return -1;
    }
    if(iter.get(CFG_TYPE_OF_SECTION, &type) != 0){
      msg.assign("Unable to get node type(iter.get(CFG_TYPE_OF_SECTION))");
      return -1;
    }
  } else {
    do {
      if(iter.get(CFG_TYPE_OF_SECTION, &type) != 0){
	msg.assign("Unable to get node type(iter.get(CFG_TYPE_OF_SECTION))");
	return -1;
      }
      if(type == NODE_TYPE_DB)
	break;
    } while(iter.next() == 0);
  }

  if(type != NODE_TYPE_DB){
    msg.assfmt("Invalid node type or no such node (%d %d)", 
	       type, NODE_TYPE_DB);
    return -1;
  }

  int p_type;
  unsigned val_32;
  Uint64 val_64;
  const char * val_char;
  do {
    p_type = 0;
    if(iter.get(param, &val_32) == 0){
      val_32 = atoi(value);
      break;
    }

    p_type++;
    if(iter.get(param, &val_64) == 0){
      val_64 = strtoll(value, 0, 10);
      break;
    }
    p_type++;
    if(iter.get(param, &val_char) == 0){
      val_char = value;
      break;
    }
    msg.assign("Could not get parameter");
    return -1;
  } while(0);

  bool res = false;
  do {
    int ret = iter.get(CFG_TYPE_OF_SECTION, &type);
    assert(ret == 0);

    if(type != NODE_TYPE_DB)
      continue;

    Uint32 node;
    ret = iter.get(CFG_NODE_ID, &node);
    assert(ret == 0);

    ConfigValues::Iterator i2(m_local_config->m_configValues->m_config, 
			      iter.m_config);
    switch(p_type){
    case 0:
      res = i2.set(param, val_32);
      ndbout_c("Updating node %d param: %d to %d",  node, param, val_32);
      break;
    case 1:
      res = i2.set(param, val_64);
      ndbout_c("Updating node %d param: %d to %u",  node, param, val_32);
      break;
    case 2:
      res = i2.set(param, val_char);
      ndbout_c("Updating node %d param: %d to %s",  node, param, val_char);
      break;
    default:
      require(false);
    }
    assert(res);
  } while(node == 0 && iter.next() == 0);

  msg.assign("Success");
  return 0;
}


int
MgmtSrvr::setConnectionDbParameter(int node1, 
				   int node2,
				   int param,
				   int value,
				   BaseString& msg)
{
  DBUG_ENTER("MgmtSrvr::setConnectionDbParameter");

  Uint32 current_value,new_value;
  Guard g(m_local_config_mutex);
  ConfigIter iter(m_local_config, CFG_SECTION_CONNECTION);

  if(iter.first() != 0){
    msg.assign("Unable to find connection section (iter.first())");
    DBUG_RETURN(-1);
  }

  for(;iter.valid();iter.next()) {
    Uint32 n1,n2;
    iter.get(CFG_CONNECTION_NODE_1, &n1);
    iter.get(CFG_CONNECTION_NODE_2, &n2);
    if((n1 == (unsigned)node1 && n2 == (unsigned)node2)
       || (n1 == (unsigned)node2 && n2 == (unsigned)node1))
      break;
  }
  if(!iter.valid()) {
    msg.assign("Unable to find connection between nodes");
    DBUG_RETURN(-2);
  }

  if(iter.get(param, &current_value) != 0) {
    msg.assign("Unable to get current value of parameter");
    DBUG_RETURN(-3);
  }

  ConfigValues::Iterator i2(m_local_config->m_configValues->m_config,
			    iter.m_config);

  if(i2.set(param, (unsigned)value) == false) {
    msg.assign("Unable to set new value of parameter");
    DBUG_RETURN(-4);
  }

  // TODO Magnus, in theory this new config should be saved on
  // nodes, but it's probably a better idea to save this
  // dynamic information elsewhere instead.

  if(iter.get(param, &new_value) != 0) {
    msg.assign("Unable to get parameter after setting it.");
    DBUG_RETURN(-5);
  }

  msg.assfmt("%u -> %u",current_value,new_value);
  DBUG_RETURN(1);
}


int
MgmtSrvr::getConnectionDbParameter(int node1, 
				   int node2,
				   int param,
				   int *value,
				   BaseString& msg)
{

  DBUG_ENTER("MgmtSrvr::getConnectionDbParameter");
  Guard g(m_local_config_mutex);
  ConfigIter iter(m_local_config, CFG_SECTION_CONNECTION);

  if(iter.first() != 0){
    msg.assign("Unable to find connection section (iter.first())");
    DBUG_RETURN(-1);
  }

  for(;iter.valid();iter.next()) {
    Uint32 n1=0,n2=0;
    iter.get(CFG_CONNECTION_NODE_1, &n1);
    iter.get(CFG_CONNECTION_NODE_2, &n2);
    if((n1 == (unsigned)node1 && n2 == (unsigned)node2)
       || (n1 == (unsigned)node2 && n2 == (unsigned)node1))
      break;
  }
  if(!iter.valid()) {
    msg.assign("Unable to find connection between nodes");
    DBUG_RETURN(-1);
  }

  if(iter.get(param, (Uint32*)value) != 0) {
    msg.assign("Unable to get current value of parameter");
    DBUG_RETURN(-1);
  }

  msg.assfmt("%d",*value);

  DBUG_RETURN(1);
}


void MgmtSrvr::transporter_connect(NDB_SOCKET_TYPE sockfd)
{
  if (theFacade->get_registry()->connect_server(sockfd))
  {
    /**
     * Force an update_connections() so that the
     * ClusterMgr and TransporterFacade is up to date
     * with the new connection.
     * Important for correct node id reservation handling
     */
    NdbMutex_Lock(theFacade->theMutexPtr);
    theFacade->get_registry()->update_connections();
    NdbMutex_Unlock(theFacade->theMutexPtr);
  }
}

bool MgmtSrvr::connect_to_self()
{
  BaseString buf;
  NdbMgmHandle mgm_handle= ndb_mgm_create_handle();

  buf.assfmt("%s:%u",
             m_opts.bind_address ? m_opts.bind_address : "localhost",
             m_port);
  ndb_mgm_set_connectstring(mgm_handle, buf.c_str());

  if(ndb_mgm_connect(mgm_handle, 0, 0, 0) < 0)
  {
    g_eventLogger->warning("%d %s",
                           ndb_mgm_get_latest_error(mgm_handle),
                           ndb_mgm_get_latest_error_desc(mgm_handle));
    ndb_mgm_destroy_handle(&mgm_handle);
    return false;
  }
  // TransporterRegistry now owns the handle and will destroy it.
  theFacade->get_registry()->set_mgm_handle(mgm_handle);

  return true;
}

Logger* MgmtSrvr::getLogger()
{
  return g_eventLogger;
}

int MgmtSrvr::ndbinfo(BaseString table_name, Vector<BaseString> *cols, Vector<BaseString> *rows)
{
  int r= ENOENT;

  if(m_ndbinfo_table_names.size()==0 || table_name=="TABLES" || table_name=="COLUMNS")
  {
    Vector<BaseString> tmp;
    Vector<BaseString> tmp2;

    /* Refresh NDBINFO metadata cache */
    r= ndbinfo(0, &tmp, &tmp2);
    r= ndbinfo(1, &tmp, &tmp2);

    if(table_name=="TABLES")
      return ndbinfo(0, cols, rows);
    if(table_name=="COLUMNS")
      return ndbinfo(1, cols, rows);

  }

  for(Uint32 i = 2; i<m_ndbinfo_table_names.size(); i++)
  {
    if(table_name == m_ndbinfo_table_names[i])
      return ndbinfo(i, cols, rows);
  }

  return r;
}

int MgmtSrvr::ndbinfo(Uint32 tableId, 
                      Vector<BaseString> *cols, Vector<BaseString> *rows)
{
  SignalSender ss(theFacade);
  ss.lock();

  SimpleSignal ssig;
  DbinfoScanReq *req= CAST_PTR(DbinfoScanReq, ssig.getDataPtrSend());
  req->tableId= tableId;
  req->senderRef= ss.getOwnRef();
  req->apiTxnId= 1;
  req->requestInfo= DbinfoScanReq::AllColumns | DbinfoScanReq::StartScan;
  req->colBitmapLo= ~0;
  req->colBitmapHi= ~0;
  req->maxRows= 2;
  req->maxBytes= 0;
  req->rows_total= 0;
  req->word_total= 0;

  ssig.set(ss, TestOrd::TraceAPI, DBINFO, GSN_DBINFO_SCANREQ,
           DbinfoScanReq::SignalLength);

  NodeId nodeId = m_master_node;
  if (okToSendTo(nodeId, false) != 0)
  {
    bool next;
    nodeId = m_master_node = 0;
    while((next = getNextNodeId(&nodeId, NDB_MGM_NODE_TYPE_NDB)) == true &&
          okToSendTo(nodeId, false) != 0);
    if(!next)
      return NO_CONTACT_WITH_DB_NODES;
  }

  int do_send= 1;

  int ncols;
  if(m_ndbinfo_column_types.size() >= tableId+1)
  {
    ncols= m_ndbinfo_column_types[tableId].size();
    *cols= m_ndbinfo_column_names[tableId];
  }
  else
  {
    if(tableId==0)
      ncols= 3;
    else // tableid = 1
      ncols= 4;
  }

  while(true)
  {
    if(do_send)
    {
      if(ss.sendSignal(nodeId, &ssig) != SEND_OK)
        return SEND_OR_RECEIVE_FAILED;

      do_send= 0;
    }

    SimpleSignal *signal= ss.waitFor();

    int gsn= signal->readSignalNumber();

    int len;
    BaseString b, rowstr;
    int i;
    char *row;
    Uint32 rowsz;
    Uint32 rec_tableid;
    int rec_colid;
    DbinfoScanConf *conf;
    Uint32 coltype;

    switch(gsn)
    {
    case GSN_DBINFO_TRANSID_AI:
      row= (char*)signal->ptr[0].p;
      rowsz= signal->ptr[0].sz;

      rowstr.clear();

      for(i=0; i<ncols; i++)
      {
        AttributeHeader ah(*(Uint32*)row);
        row+=ah.getHeaderSize()*sizeof(Uint32);
        len= ah.getByteSize();

        len= ah.getByteSize();

        if(tableId==0)
        {
          if(i==0)
            rec_tableid= *(Uint32*)row;
          if(i==1)
          {
            b.assign(row,len);
            m_ndbinfo_table_names.set(b, rec_tableid, b);
            b.clear();
          }
        }

        if(tableId==1)
        {
          if(i==0)
            rec_tableid= *(Uint32*)row;
          if(i==1)
            rec_colid= *(Uint32*)row;
          if(i==2)
          {
            if(m_ndbinfo_column_names.size() <= rec_tableid)
            {
              Vector<BaseString> v;
              m_ndbinfo_column_names.fill(rec_tableid+1, v);
            }
            b.assign(row,len);
            m_ndbinfo_column_names[rec_tableid].set(b,(unsigned)rec_colid,b);
            b.clear();
          }
          if(i==3)
          {
            if(m_ndbinfo_column_types.size() <= rec_tableid)
            {
              Vector<Uint32> v;
              m_ndbinfo_column_types.fill(rec_tableid+1, v);
            }
            coltype= (strncmp("BIGINT",row,len)==0)?2:1;

            m_ndbinfo_column_types[rec_tableid].set(coltype,
                                                    (unsigned)rec_colid,
                                                    coltype);
          }
        }
        
        if(m_ndbinfo_column_types.size() > tableId
           && m_ndbinfo_column_types[tableId].size() > (unsigned)i)
        {
          switch(m_ndbinfo_column_types[tableId][i])
          {
          case NDBINFO_TYPE_NUMBER:
            rowstr.appfmt("%u",*(Uint32*)row);
            break;
          case NDBINFO_TYPE_STRING:
            b.assign(row,len);
            for(char*c= (char*)b.c_str(); *c!='\0'; c++)
              if(*c=='\n')
                *c= ' ';
            rowstr.append(b);
            b.clear();
            break;
          }

          if(i!=ncols-1)
            rowstr.append(",");
        }

        row+=len;

      }

      rows->push_back(rowstr);
      rowstr.clear();

      break;
    case GSN_DBINFO_SCANCONF:
      conf= (DbinfoScanConf*) signal->getDataPtr();

      if(conf->requestInfo & DbinfoScanConf::MoreData)
      {
        memcpy(req,conf,signal->header.theLength*sizeof(Uint32));
        req->requestInfo &= ~(DbinfoScanReq::StartScan);
        ssig.set(ss, TestOrd::TraceAPI, req->cursor.cur_block, 
                 GSN_DBINFO_SCANREQ, DbinfoScanReq::SignalLengthWithCursor);
        nodeId= req->cursor.cur_node;

        do_send= 1;
        continue;
      }
      else
      {
        return 0;
      }
      break;
    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      // Ignore;
      break;
    default:
      report_unknown_signal(signal);
      return SEND_OR_RECEIVE_FAILED;
    }
  }

  return 0;
}


bool
MgmtSrvr::change_config(Config& new_config, BaseString& msg)
{
  SignalSender ss(theFacade);
  ss.lock();

  SimpleSignal ssig;
  UtilBuffer buf;
  new_config.pack(buf);
  ssig.ptr[0].p = (Uint32*)buf.get_data();
  ssig.ptr[0].sz = (buf.length() + 3) / 4;
  ssig.header.m_noOfSections = 1;

  ConfigChangeReq *req= CAST_PTR(ConfigChangeReq, ssig.getDataPtrSend());
  req->length = buf.length();

  NodeBitmask mgm_nodes;
  ss.getNodes(mgm_nodes, NodeInfo::MGM);

  NodeId nodeId= ss.find_confirmed_node(mgm_nodes);
  if (nodeId == 0)
  {
    msg = "INTERNAL ERROR Could not find any mgmd!";
    return false;
  }

  if (ss.sendSignal(nodeId, ssig,
                    MGM_CONFIG_MAN, GSN_CONFIG_CHANGE_REQ,
                    ConfigChangeReq::SignalLength) != SEND_OK)
  {
    msg.assfmt("Could not start configuration change, send to "
               "node %d failed", nodeId);
    return false;
  }
  mgm_nodes.clear(nodeId);

  bool done = false;
  while(!done)
  {
    SimpleSignal *signal= ss.waitFor();

    switch(signal->readSignalNumber()){
    case GSN_CONFIG_CHANGE_CONF:
      done= true;
      break;
    case GSN_CONFIG_CHANGE_REF:
    {
      const ConfigChangeRef * const ref =
        CAST_CONSTPTR(ConfigChangeRef, signal->getDataPtr());
      g_eventLogger->debug("Got CONFIG_CHANGE_REF, error: %d", ref->errorCode);
      switch(ref->errorCode)
      {
      case ConfigChangeRef::NotMaster:{
        // Retry with next node if any
        NodeId nodeId= ss.find_confirmed_node(mgm_nodes);
        if (nodeId == 0)
        {
          msg = "INTERNAL ERROR Could not find any mgmd!";
          return false;
        }

        if (ss.sendSignal(nodeId, ssig,
                          MGM_CONFIG_MAN, GSN_CONFIG_CHANGE_REQ,
                          ConfigChangeReq::SignalLength) != SEND_OK)
        {
          msg.assfmt("Could not start configuration change, send to "
                     "node %d failed", nodeId);
          return false;
        }
        mgm_nodes.clear(nodeId);
        break;
      }

      default:
        msg = ConfigChangeRef::errorMessage(ref->errorCode);
        return false;
      }

      break;
    }

    case GSN_API_REGCONF:
    case GSN_TAKE_OVERTCCONF:
      // Ignore;
      break;

    default:
      report_unknown_signal(signal);
      return false;

    }
  }

  g_eventLogger->info("Config change completed");

  return 0;
}


void
MgmtSrvr::print_config(const char* section_filter, NodeId nodeid_filter,
                       const char* param_filter,
                       NdbOut& out)
{
  Guard g(m_local_config_mutex);
  m_local_config->print(section_filter, nodeid_filter,
                        param_filter, out);
}


bool
MgmtSrvr::reload_config(const char* config_filename, bool mycnf,
                        BaseString& msg)
{
  if (config_filename && mycnf)
  {
    msg = "ERROR: Both mycnf and config_filename is not supported";
    return false;
  }

  if (config_filename)
  {
    if (m_opts.mycnf)
    {
      msg.assfmt("ERROR: Can't switch to use config.ini '%s' when "
                 "node was started from my.cnf", config_filename);
      return false;
    }
  }
  else
  {
    if (mycnf)
    {
      // Reload from my.cnf
      if (!m_opts.mycnf)
      {
        if (m_opts.config_filename)
        {
          msg.assfmt("ERROR: Can't switch to use my.cnf when "
                     "node was started from '%s'", m_opts.config_filename);
          return false;
        }
      }
    }
    else
    {
      /* No config file name supplied and not told to use mycnf */
      if (m_opts.config_filename)
      {
        g_eventLogger->info("No config file name supplied, using '%s'",
                            m_opts.config_filename);
        config_filename = m_opts.config_filename;
      }
      else
      {
        msg = "ERROR: Neither config file name or mycnf available";
        return false;
      }
    }
  }

  Config* new_conf_ptr;
  if ((new_conf_ptr= ConfigManager::load_config(config_filename,
                                                mycnf, msg)) == NULL)
    return false;
  Config new_conf(new_conf_ptr);

  {
    Guard g(m_local_config_mutex);

    /* Copy the necessary values from old to new config */
    if (!new_conf.setGeneration(m_local_config->getGeneration()) ||
        !new_conf.setName(m_local_config->getName()) ||
        !new_conf.setPrimaryMgmNode(m_local_config->getPrimaryMgmNode()))
    {
      msg = "Failed to initialize reloaded config";
      return false;
    }
  }

  if (!change_config(new_conf, msg))
    return false;
  return true;
}


void
MgmtSrvr::show_variables(NdbOut& out)
{
  out << "daemon: " << yes_no(m_opts.daemon) << endl;
  out << "non_interactive: " << yes_no(m_opts.non_interactive) << endl;
  out << "interactive: " << yes_no(m_opts.interactive) << endl;
  out << "config_filename: " << str_null(m_opts.config_filename) << endl;
  out << "mycnf: " << yes_no(m_opts.mycnf) << endl;
  out << "bind_address: " << str_null(m_opts.bind_address) << endl;
  out << "no_nodeid_checks: " << yes_no(m_opts.no_nodeid_checks) << endl;
  out << "print_full_config: " << yes_no(m_opts.print_full_config) << endl;
  out << "configdir: " << str_null(m_opts.configdir) << endl;
  out << "verbose: " << yes_no(m_opts.verbose) << endl;
  out << "reload: " << yes_no(m_opts.reload) << endl;

  out << "nodeid: " << _ownNodeId << endl;
  out << "blocknumber: " << hex <<_blockNumber << endl;
  out << "own_reference: " << hex << _ownReference << endl;
  out << "port: " << m_port << endl;
  out << "need_restart: " << m_need_restart << endl;
  out << "is_stop_thread: " << _isStopThread << endl;
  out << "log_level_thread_sleep: " << _logLevelThreadSleep << endl;
  out << "master_node: " << m_master_node << endl;
}



template class MutexVector<NodeId>;
template class MutexVector<Ndb_mgmd_event_service::Event_listener>;
template class Vector<EventSubscribeReq>;
template class MutexVector<EventSubscribeReq>;

template class Vector< Vector<BaseString> >;

