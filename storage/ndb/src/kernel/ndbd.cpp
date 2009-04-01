/* Copyright (C) 2003-2008 MySQL AB, 2009 Sun Microsystems, Inc.

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

#include <NdbEnv.h>
#include <NdbConfig.h>
#include <NdbSleep.h>
#include <NdbAutoPtr.hpp>

#include "vm/SimBlockList.hpp"
#include "vm/WatchDog.hpp"
#include "vm/ThreadConfig.hpp"
#include "vm/Configuration.hpp"

#include "ndbd.hpp"

#include <ConfigRetriever.hpp>
#include <LogLevel.hpp>

#if defined NDB_SOLARIS
#include <sys/processor.h>
#endif

#include <EventLogger.hpp>
extern EventLogger * g_eventLogger;

static void
systemInfo(const Configuration & config, const LogLevel & logLevel)
{
#ifdef NDB_WIN32
  int processors = 0;
  int speed;
  SYSTEM_INFO sinfo;
  GetSystemInfo(&sinfo);
  processors = sinfo.dwNumberOfProcessors;
  HKEY hKey;
  if(ERROR_SUCCESS==RegOpenKeyEx
     (HKEY_LOCAL_MACHINE,
      TEXT("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
      0, KEY_READ, &hKey)) {
    DWORD dwMHz;
    DWORD cbData = sizeof(dwMHz);
    if(ERROR_SUCCESS==RegQueryValueEx(hKey,
				      "~MHz", 0, 0, (LPBYTE)&dwMHz, &cbData)) {
      speed = int(dwMHz);
    }
    RegCloseKey(hKey);
  }
#elif defined NDB_SOLARIS
  // Search for at max 16 processors among the first 256 processor ids
  processor_info_t pinfo; memset(&pinfo, 0, sizeof(pinfo));
  int pid = 0;
  while(processors < 16 && pid < 256){
    if(!processor_info(pid++, &pinfo))
      processors++;
  }
  speed = pinfo.pi_clock;
#endif

  if(logLevel.getLogLevel(LogLevel::llStartUp) > 0){
    g_eventLogger->info("NDB Cluster -- DB node %d", globalData.ownId);
    g_eventLogger->info("%s --", NDB_VERSION_STRING);
    if (config.get_mgmd_host())
      g_eventLogger->info("Configuration fetched at %s port %d",
                          config.get_mgmd_host(), config.get_mgmd_port());
#ifdef NDB_SOLARIS
    g_eventLogger->info("NDB is running on a machine with %d processor(s) at %d MHz",
                        processor, speed);
#endif
  }
  if(logLevel.getLogLevel(LogLevel::llStartUp) > 3){
    Uint32 t = config.timeBetweenWatchDogCheck();
    g_eventLogger->info("WatchDog timer is set to %d ms", t);
  }
}


static int
init_global_memory_manager(EmulatorData &ed, Uint32 *watchCounter)
{
  const ndb_mgm_configuration_iterator * p =
    ed.theConfiguration->getOwnConfigIterator();
  if (p == 0)
  {
    abort();
  }

  Uint64 shared_mem = 8*1024*1024;
  ndb_mgm_get_int64_parameter(p, CFG_DB_SGA, &shared_mem);
  Uint32 shared_pages = Uint32(shared_mem /= GLOBAL_PAGE_SIZE);

  Uint32 tupmem = 0;
  if (ndb_mgm_get_int_parameter(p, CFG_TUP_PAGE, &tupmem))
  {
    g_eventLogger->alert("Failed to get CFG_TUP_PAGE parameter from "
                        "config, exiting.");
    return -1;
  }

  if (tupmem)
  {
    Resource_limit rl;
    rl.m_min = tupmem;
    rl.m_max = tupmem;
    rl.m_resource_id = RG_DATAMEM;
    ed.m_mem_manager->set_resource_limit(rl);
  }

  Uint32 maxopen = 4 * 4; // 4 redo parts, max 4 files per part
  Uint32 filebuffer = NDB_FILE_BUFFER_SIZE;
  Uint32 filepages = (filebuffer / GLOBAL_PAGE_SIZE) * maxopen;

  if (filepages)
  {
    Resource_limit rl;
    rl.m_min = filepages;
    rl.m_max = filepages;
    rl.m_resource_id = RG_FILE_BUFFERS;
    ed.m_mem_manager->set_resource_limit(rl);
  }

  Uint32 jbpages = compute_jb_pages(&ed);
  if (jbpages)
  {
    Resource_limit rl;
    rl.m_min = jbpages;
    rl.m_max = jbpages;
    rl.m_resource_id = RG_JOBBUFFER;
    ed.m_mem_manager->set_resource_limit(rl);
  }

  Uint32 sbpages = 0;
  if (globalTransporterRegistry.get_using_default_send_buffer() == false)
  {
    Uint64 mem = globalTransporterRegistry.get_total_max_send_buffer();
    sbpages = Uint32((mem + GLOBAL_PAGE_SIZE - 1) / GLOBAL_PAGE_SIZE);
    Resource_limit rl;
    rl.m_min = sbpages;
    rl.m_max = sbpages;
    rl.m_resource_id = RG_TRANSPORTER_BUFFERS;
    ed.m_mem_manager->set_resource_limit(rl);
  }

  if (shared_pages + tupmem + filepages + jbpages + sbpages)
  {
    Resource_limit rl;
    rl.m_min = 0;
    rl.m_max = shared_pages + tupmem + filepages + jbpages + sbpages;
    rl.m_resource_id = 0;
    ed.m_mem_manager->set_resource_limit(rl);
  }

  if (!ed.m_mem_manager->init(watchCounter))
  {
    struct ndb_mgm_param_info dm;
    struct ndb_mgm_param_info sga;
    size_t size;

    size = sizeof(ndb_mgm_param_info);
    ndb_mgm_get_db_parameter_info(CFG_DB_DATA_MEM, &dm, &size);
    size = sizeof(ndb_mgm_param_info);
    ndb_mgm_get_db_parameter_info(CFG_DB_SGA, &sga, &size);

    g_eventLogger->alert("Malloc (%lld bytes) for %s and %s failed, exiting",
                         Uint64(shared_mem + tupmem) * GLOBAL_PAGE_SIZE,
                         dm.m_name, sga.m_name);
    return -1;
  }

  return 0;                     // Success
}


static int
get_multithreaded_config(EmulatorData& ed)
{
  // multithreaded is compiled in ndbd/ndbmtd for now
  globalData.isNdbMt = SimulatedBlock::isMultiThreaded();
  if (!globalData.isNdbMt) {
    ndbout << "NDBMT: non-mt" << endl;
    return 0;
  }

  ndb_mgm_configuration * conf = ed.theConfiguration->getClusterConfig();
  if (conf == 0)
  {
    abort();
  }

  ndb_mgm_configuration_iterator * p =
    ndb_mgm_create_configuration_iterator(conf, CFG_SECTION_NODE);
  if (ndb_mgm_find(p, CFG_NODE_ID, globalData.ownId))
  {
    abort();
  }

  Uint32 mtthreads = 0;
  ndb_mgm_get_int_parameter(p, CFG_DB_MT_THREADS, &mtthreads);
  ndbout << "NDBMT: MaxNoOfExecutionThreads=" << mtthreads << endl;

  globalData.isNdbMtLqh = true;

  {
    Uint32 classic = 0;
    ndb_mgm_get_int_parameter(p, CFG_NDBMT_CLASSIC, &classic);
    if (classic)
      globalData.isNdbMtLqh = false;

    const char* p = NdbEnv_GetEnv("NDB_MT_LQH", (char*)0, 0);
    if (p != 0)
    {
      if (strstr(p, "NOPLEASE") != 0)
        globalData.isNdbMtLqh = false;
      else
        globalData.isNdbMtLqh = true;
    }
  }

  if (!globalData.isNdbMtLqh)
    return 0;

  Uint32 threads = 0;
  switch(mtthreads){
  case 0:
  case 1:
  case 2:
  case 3:
    threads = 1; // TC + receiver + SUMA + LQH
    break;
  case 4:
  case 5:
  case 6:
    threads = 2; // TC + receiver + SUMA + 2 * LQH
    break;
  default:
    threads = 4; // TC + receiver + SUMA + 4 * LQH
  }

  ndb_mgm_get_int_parameter(p, CFG_NDBMT_LQH_THREADS, &threads);
  Uint32 workers = threads;
  ndb_mgm_get_int_parameter(p, CFG_NDBMT_LQH_WORKERS, &workers);

#ifdef VM_TRACE
  // testing
  {
    const char* p;
    p = NdbEnv_GetEnv("NDBMT_LQH_WORKERS", (char*)0, 0);
    if (p != 0)
      workers = atoi(p);
    p = NdbEnv_GetEnv("NDBMT_LQH_THREADS", (char*)0, 0);
    if (p != 0)
      threads = atoi(p);
  }
#endif

  ndbout << "NDBMT: workers=" << workers
         << " threads=" << threads << endl;

  assert(workers != 0 && workers <= MAX_NDBMT_LQH_WORKERS);
  assert(threads != 0 && threads <= MAX_NDBMT_LQH_THREADS);
  assert(workers % threads == 0);

  globalData.ndbMtLqhWorkers = workers;
  globalData.ndbMtLqhThreads = threads;
  return 0;
}


extern FILE *child_info_file_r;
extern FILE *child_info_file_w;

static void
writeChildInfo(const char *token, int val)
{
  fprintf(child_info_file_w, "%s=%d\n", token, val);
  fflush(child_info_file_w);
}

void
childReportSignal(int signum)
{
  writeChildInfo("signal", signum);
}

void
childReportError(int error)
{
  writeChildInfo("error", error);
}

void
childExit(int code, Uint32 currentStartPhase)
{
#ifndef NDB_WIN
  writeChildInfo("sphase", currentStartPhase);
  writeChildInfo("exit", code);
  fprintf(child_info_file_w, "\n");
  fclose(child_info_file_r);
  fclose(child_info_file_w);
  _exit(code);
#else
  {
    Configuration* theConfig=globalEmulatorData.theConfiguration;
    theConfig->closeConfiguration(true);
    switch (code) {
    case NRT_Default:
      g_eventLogger->info("Angel shutting down");
      reportShutdown(theConfig, 0, 0, currentStartPhase);
      _exit(0);
      break;
    case NRT_NoStart_Restart:
      theConfig->setInitialStart(false);
      globalData.theRestartFlag=initial_state;
      break;
    case NRT_NoStart_InitialStart:
      theConfig->setInitialStart(true);
      globalData.theRestartFlag=initial_state;
      break;
    case NRT_DoStart_InitialStart:
      theConfig->setInitialStart(true);
      globalData.theRestartFlag=perform_start;
      break;
    default:
      if (theConfig->stopOnError())
      {
        /**
         * Error shutdown && stopOnError()
         */
        reportShutdown(theConfig, 1, 0, currentStartPhase);
        _exit(0);
      }
      // Fall-through
    case NRT_DoStart_Restart:
      theConfig->setInitialStart(false);
      globalData.theRestartFlag=perform_start;
      break;
    }
    char buf[80];
    BaseString::snprintf(buf, sizeof (buf), "WIN_NDBD_CFG=%d %d %d",
                         theConfig->getInitialStart(),
                         globalData.theRestartFlag, globalData.ownId);
    _putenv(buf);

    char exe[MAX_PATH];
    GetModuleFileName(0, exe, sizeof (exe));

    STARTUPINFO sinfo;
    ZeroMemory(&sinfo, sizeof (sinfo));
    sinfo.cb=sizeof (STARTUPINFO);
    sinfo.dwFlags=STARTF_USESHOWWINDOW;
    sinfo.wShowWindow=SW_HIDE;

    PROCESS_INFORMATION pinfo;
    if (reportShutdown(theConfig, 0, 1, currentStartPhase))
    {
      g_eventLogger->error("unable to shutdown");
      _exit(1);
    }
    g_eventLogger->info("Ndb has terminated.  code=%d", code);
    if (code == NRT_NoStart_Restart)
      globalTransporterRegistry.disconnectAll();
    g_eventLogger->info("Ndb has terminated.  Restarting");
    if (CreateProcess(exe, GetCommandLine(), NULL, NULL, TRUE, 0, NULL, NULL,
                      &sinfo, &pinfo) == 0)
    {
      g_eventLogger->error("Angel was unable to create child ndbd process"
                           " error: %d", GetLastError());
    }
  }
#endif
}

void
childAbort(int code, Uint32 currentStartPhase)
{
#ifndef NDB_WIN
  writeChildInfo("sphase", currentStartPhase);
  writeChildInfo("exit", code);
  fprintf(child_info_file_w, "\n");
  fclose(child_info_file_r);
  fclose(child_info_file_w);
#ifndef NDB_WIN32
  signal(SIGABRT, SIG_DFL);
#endif
  abort();
#else
  childExit(code, currentStartPhase);
#endif
}

extern "C"
void
handler_shutdown(int signum){
  g_eventLogger->info("Received signal %d. Performing stop.", signum);
  childReportError(0);
  childReportSignal(signum);
  globalData.theRestartFlag = perform_stop;
}

extern NdbMutex * theShutdownMutex;

extern "C"
void
handler_error(int signum){
  // only let one thread run shutdown
  static long thread_id= 0;

  if (thread_id != 0 && thread_id == my_thread_id())
  {
    // Shutdown thread received signal
#ifndef NDB_WIN32
	signal(signum, SIG_DFL);
    kill(getpid(), signum);
#endif
    while(true)
      NdbSleep_MilliSleep(10);
  }
  if(theShutdownMutex && NdbMutex_Trylock(theShutdownMutex) != 0)
    while(true)
      NdbSleep_MilliSleep(10);
  thread_id= my_thread_id();
  g_eventLogger->info("Received signal %d. Running error handler.", signum);
  childReportSignal(signum);
  // restart the system
  char errorData[64], *info= 0;
#ifdef HAVE_STRSIGNAL
  info= strsignal(signum);
#endif
  BaseString::snprintf(errorData, sizeof(errorData), "Signal %d received; %s", signum,
		       info ? info : "No text for signal available");
  ERROR_SET_SIGNAL(fatal, NDBD_EXIT_OS_SIGNAL_RECEIVED, errorData, __FILE__);
}


static void
catchsigs(bool foreground){
#if !defined NDB_WIN32

  static const int signals_shutdown[] = {
#ifdef SIGBREAK
    SIGBREAK,
#endif
    SIGHUP,
    SIGINT,
#if defined SIGPWR
    SIGPWR,
#elif defined SIGINFO
    SIGINFO,
#endif
    SIGQUIT,
    SIGTERM,
#ifdef SIGTSTP
    SIGTSTP,
#endif
    SIGTTIN,
    SIGTTOU
  };

  static const int signals_error[] = {
    SIGABRT,
    SIGALRM,
#ifdef SIGBUS
    SIGBUS,
#endif
    SIGCHLD,
    SIGFPE,
    SIGILL,
#ifdef SIGIO
    SIGIO,
#endif
#ifdef SIGPOLL
    SIGPOLL,
#endif
    SIGSEGV
  };

  static const int signals_ignore[] = {
    SIGPIPE
  };

  size_t i;
  for(i = 0; i < sizeof(signals_shutdown)/sizeof(signals_shutdown[0]); i++)
    signal(signals_shutdown[i], handler_shutdown);
  for(i = 0; i < sizeof(signals_error)/sizeof(signals_error[0]); i++)
    signal(signals_error[i], handler_error);
  for(i = 0; i < sizeof(signals_ignore)/sizeof(signals_ignore[0]); i++)
    signal(signals_ignore[i], SIG_IGN);

#ifdef SIGTRAP
  if (!foreground)
    signal(SIGTRAP, handler_error);
#endif

#endif
}


int
ndbd_run(bool foreground)
{

  if (get_multithreaded_config(globalEmulatorData))
    return -1;

  Configuration* theConfig = globalEmulatorData.theConfiguration;
  theConfig->setupConfiguration();
  systemInfo(* theConfig, * theConfig->m_logLevel);

  NdbThread* pWatchdog = globalEmulatorData.theWatchDog->doStart();

  {
    /*
     * Memory allocation can take a long time for large memory.
     *
     * So we want the watchdog to monitor the process of initial allocation.
     */
    Uint32 watchCounter;
    watchCounter = 9;           //  Means "doing allocation"
    globalEmulatorData.theWatchDog->registerWatchedThread(&watchCounter, 0);
    if (init_global_memory_manager(globalEmulatorData, &watchCounter))
      return 1;
    globalEmulatorData.theWatchDog->unregisterWatchedThread(0);
  }

  globalEmulatorData.theThreadConfig->init(&globalEmulatorData);

#ifdef VM_TRACE
  // Create a signal logger before block constructors
  char *buf= NdbConfig_SignalLogFileName(globalData.ownId);
  NdbAutoPtr<char> tmp_aptr(buf);
  FILE * signalLog = fopen(buf, "a");
  globalSignalLoggers.setOwnNodeId(globalData.ownId);
  globalSignalLoggers.setOutputStream(signalLog);
#if 1 // to log startup
  { const char* p = NdbEnv_GetEnv("NDB_SIGNAL_LOG", (char*)0, 0);
    if (p != 0) {
      char buf[200];
      BaseString::snprintf(buf, sizeof(buf), "BLOCK=%s", p);
      for (char* q = buf; *q != 0; q++) *q = toupper(toascii(*q));
      globalSignalLoggers.log(SignalLoggerManager::LogInOut, buf);
      globalData.testOn = 1;
      assert(signalLog != 0);
      fprintf(signalLog, "START\n");
      fflush(signalLog);
    }
  }
#endif
#endif

  // Load blocks (both main and workers)
  globalEmulatorData.theSimBlockList->load(globalEmulatorData);

  // Set thread concurrency for Solaris' light weight processes
  int status;
  status = NdbThread_SetConcurrencyLevel(30);
  assert(status == 0);

  catchsigs(foreground);

  /**
   * Do startup
   */

  ErrorReporter::setErrorHandlerShutdownType(NST_ErrorHandlerStartup);

  switch(globalData.theRestartFlag){
  case initial_state:
    globalEmulatorData.theThreadConfig->doStart(NodeState::SL_CMVMI);
    break;
  case perform_start:
    globalEmulatorData.theThreadConfig->doStart(NodeState::SL_CMVMI);
    globalEmulatorData.theThreadConfig->doStart(NodeState::SL_STARTING);
    break;
  default:
    assert("Illegal state globalData.theRestartFlag" == 0);
  }

  globalTransporterRegistry.startSending();
  globalTransporterRegistry.startReceiving();
  if (!globalTransporterRegistry.start_service(*globalEmulatorData.m_socket_server)){
    ndbout_c("globalTransporterRegistry.start_service() failed");
    _exit(-1);
  }

  // Re-use the mgm handle as a transporter
  if(!globalTransporterRegistry.connect_client(
		 theConfig->get_config_retriever()->get_mgmHandlePtr()))
      ERROR_SET(fatal, NDBD_EXIT_INVALID_CONFIG,
		"Connection to mgmd terminated before setup was complete",
		"StopOnError missing");

  NdbThread* pTrp = globalTransporterRegistry.start_clients();
  if (pTrp == 0)
  {
    ndbout_c("globalTransporterRegistry.start_clients() failed");
    _exit(-1);
  }

  NdbThread* pSockServ = globalEmulatorData.m_socket_server->startServer();

  globalEmulatorData.theConfiguration->addThread(pTrp, SocketClientThread);
  globalEmulatorData.theConfiguration->addThread(pWatchdog, WatchDogThread);
  globalEmulatorData.theConfiguration->addThread(pSockServ, SocketServerThread);

  //  theConfig->closeConfiguration();
  {
    NdbThread *pThis = NdbThread_CreateObject(0);
    Uint32 inx = globalEmulatorData.theConfiguration->addThread(pThis,
                                                                MainThread);
    globalEmulatorData.theThreadConfig->ipControlLoop(pThis, inx);
    globalEmulatorData.theConfiguration->removeThreadId(inx);
  }
  NdbShutdown(NST_Normal);

  return NRT_Default;
}
