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

#include <ndb_global.h>
#include <my_pthread.h>
#include <ndb_opts.h>
#include <ndb_version.h>
#include "Configuration.hpp"
#include <ConfigRetriever.hpp>
#include <TransporterRegistry.hpp>

#include "vm/SimBlockList.hpp"
#include "ThreadConfig.hpp"
#include <SignalLoggerManager.hpp>
#include <NdbOut.hpp>
#include <NdbMain.h>
#include <NdbDaemon.h>
#include <NdbSleep.h>
#include <NdbConfig.h>
#include <WatchDog.hpp>

#include <LogLevel.hpp>
#include <EventLogger.hpp>
#include <Logger.hpp>
#include <NdbEnv.h>

#include <NdbAutoPtr.hpp>

#include <Properties.hpp>

#include <mgmapi_debug.h>

#if defined NDB_SOLARIS // ok
#include <sys/processor.h> // For system informatio
#endif

extern EventLogger * g_eventLogger;
extern NdbMutex * theShutdownMutex;

static int opt_daemon, opt_no_daemon, opt_foreground,
  opt_initial, opt_no_start, opt_initialstart;
static const char* opt_nowait_nodes = 0;
static const char* opt_bind_address = 0;

extern NdbNodeBitmask g_nowait_nodes;

void catchsigs(bool ignore); // for process signal handling

#define MAX_FAILED_STARTUPS 3
// Flag set by child through SIGUSR1 to signal a failed startup
static bool failed_startup_flag = false;
// Counter for consecutive failed startups
static Uint32 failed_startups = 0;
extern "C" void handler_shutdown(int signum);  // for process signal handling
extern "C" void handler_error(int signum);  // for process signal handling
extern "C" void handler_sigusr1(int signum);  // child signalling failed restart

// Shows system information
void systemInfo(const Configuration & conf,
		const LogLevel & ll); 

// These are used already before fork if fetch_configuration() fails
// (e.g. Unable to alloc node id).  Set them to something reasonable.
static FILE *child_info_file_r= stdin;
static FILE *child_info_file_w= stdout;

static void writeChildInfo(const char *token, int val)
{
  fprintf(child_info_file_w, "%s=%d\n", token, val);
  fflush(child_info_file_w);
}

void childReportSignal(int signum)
{
  writeChildInfo("signal", signum);
}

void childReportError(int error)
{
  writeChildInfo("error", error);
}

void childExit(int code, Uint32 currentStartPhase)
{
#ifndef NDB_WIN
  writeChildInfo("sphase", currentStartPhase);
  writeChildInfo("exit", code);
  fprintf(child_info_file_w, "\n");
  fclose(child_info_file_r);
  fclose(child_info_file_w);
  exit(code);
#else
  {
    Configuration* theConfig = globalEmulatorData.theConfiguration;
    theConfig->closeConfiguration(true);
    switch(code){
    case NRT_Default:
        g_eventLogger->info("Angel shutting down");
        reportShutdown(theConfig, 0, 0, currentStartPhase);
        exit(0);
        break;
    case NRT_NoStart_Restart:
        theConfig->setInitialStart(false);
        globalData.theRestartFlag = initial_state;
        break;
    case NRT_NoStart_InitialStart:
        theConfig->setInitialStart(true);
        globalData.theRestartFlag = initial_state;
        break;
    case NRT_DoStart_InitialStart:
        theConfig->setInitialStart(true);
        globalData.theRestartFlag = perform_start;
        break;
    default:
        if(theConfig->stopOnError()){
          /**
           * Error shutdown && stopOnError()
           */
          reportShutdown(theConfig, 1, 0, currentStartPhase);
          exit(0);
        }
        // Fall-through
    case NRT_DoStart_Restart:
        theConfig->setInitialStart(false);
        globalData.theRestartFlag = perform_start;
        break;
    }
    char buf[80];
    BaseString::snprintf(buf, sizeof(buf), "WIN_NDBD_CFG=%d %d %d",
                         theConfig->getInitialStart(),
                         globalData.theRestartFlag, globalData.ownId);
    _putenv(buf);

    char exe[MAX_PATH];
    GetModuleFileName(0,exe,sizeof(exe));

    STARTUPINFO sinfo;
    ZeroMemory(&sinfo, sizeof(sinfo));
    sinfo.cb= sizeof(STARTUPINFO);
    sinfo.dwFlags= STARTF_USESHOWWINDOW;
    sinfo.wShowWindow= SW_HIDE;

    PROCESS_INFORMATION pinfo;
    if(reportShutdown(theConfig, 0, 1, currentStartPhase)) {
      g_eventLogger->error("unable to shutdown");
      exit(1);
    }
    g_eventLogger->info("Ndb has terminated.  code=%d", code);
    if(code==NRT_NoStart_Restart)
      globalTransporterRegistry.disconnectAll();
    g_eventLogger->info("Ndb has terminated.  Restarting");
    if(CreateProcess(exe, GetCommandLine(), NULL, NULL, TRUE, 0, NULL, NULL,
      &sinfo, &pinfo) == 0)
    {
      g_eventLogger->error("Angel was unable to create child ndbd process"
        " error: %d", GetLastError());
    }
  }
#endif
}

void childAbort(int code, Uint32 currentStartPhase)
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
  childExit(code,currentStartPhase);
#endif
}

static int insert(const char * pair, Properties & p)
{
  BaseString tmp(pair);
  
  tmp.trim(" \t\n\r");
  Vector<BaseString> split;
  tmp.split(split, ":=", 2);
  if(split.size() != 2)
    return -1;
  p.put(split[0].trim().c_str(), split[1].trim().c_str()); 
  return 0;
}

static int readChildInfo(Properties &info)
{
  fclose(child_info_file_w);
  char buf[128];
  while (fgets(buf,sizeof(buf),child_info_file_r))
    insert(buf,info);
  fclose(child_info_file_r);
  return 0;
}

static bool get_int_property(Properties &info,
			     const char *token, Uint32 *int_val)
{
  const char *str_val= 0;
  if (!info.get(token, &str_val))
    return false;
  char *endptr;
  long int tmp= strtol(str_val, &endptr, 10);
  if (str_val == endptr)
    return false;
  *int_val = tmp;
  return true;
}

int reportShutdown(class Configuration *config, int error_exit, int restart, Uint32 sphase= 256)
{
  Uint32 error= 0, signum= 0;
#ifndef NDB_WIN
  Properties info;
  readChildInfo(info);

  get_int_property(info, "signal", &signum);
  get_int_property(info, "error", &error);
  get_int_property(info, "sphase", &sphase);
#endif
  Uint32 length, theData[25];
  EventReport *rep = (EventReport *)theData;

  rep->setNodeId(globalData.ownId);
  if (restart)
    theData[1] =                                    1      |
      (globalData.theRestartFlag == initial_state ? 2 : 0) |
      (config->getInitialStart()                  ? 4 : 0);
  else
    theData[1] = 0;

  if (error_exit == 0)
  {
    rep->setEventType(NDB_LE_NDBStopCompleted);
    theData[2] = signum;
    length = 3;
  }
  else
  {
    rep->setEventType(NDB_LE_NDBStopForced);
    theData[2] = signum;
    theData[3] = error;
    theData[4] = sphase;
    theData[5] = 0; // extra
    length = 6;
  }

  { // Log event
    const EventReport * const eventReport = (EventReport *)&theData[0];
    g_eventLogger->log(eventReport->getEventType(), theData, length,
                       eventReport->getNodeId(), 0);
  }

  for (unsigned n = 0; n < config->m_mgmds.size(); n++)
  {
    NdbMgmHandle h = ndb_mgm_create_handle();
    if (h == 0 ||
	ndb_mgm_set_connectstring(h, config->m_mgmds[n].c_str()) ||
	ndb_mgm_connect(h,
			1, //no_retries
			0, //retry_delay_in_seconds
			0  //verbose
			))
      goto handle_error;

    {
      if (ndb_mgm_report_event(h, theData, length))
	goto handle_error;
    }
    goto do_next;

handle_error:
    if (h)
    {
      BaseString tmp(ndb_mgm_get_latest_error_msg(h));
      tmp.append(" : ");
      tmp.append(ndb_mgm_get_latest_error_desc(h));
      g_eventLogger->warning("Unable to report shutdown reason to %s: %s",
                             config->m_mgmds[n].c_str(), tmp.c_str());
    }
    else
    {
      g_eventLogger->error("Unable to report shutdown reason to %s",
                           config->m_mgmds[n].c_str());
    }
do_next:
    if (h)
    {
      ndb_mgm_disconnect(h);
      ndb_mgm_destroy_handle(&h);
    }
  }
  return 0;
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
  shared_mem /= GLOBAL_PAGE_SIZE;

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

  Uint32 jbpages = compute_jb_pages(&ed);;
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
    sbpages = (mem + GLOBAL_PAGE_SIZE - 1) / GLOBAL_PAGE_SIZE;
    Resource_limit rl;
    rl.m_min = sbpages;
    rl.m_max = sbpages;
    rl.m_resource_id = RG_TRANSPORTER_BUFFERS;
    ed.m_mem_manager->set_resource_limit(rl);
  }

  if (shared_mem + tupmem + filepages + jbpages + sbpages)
  {
    Resource_limit rl;
    rl.m_min = 0;
    rl.m_max = shared_mem + tupmem + filepages + jbpages + sbpages;
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

  const ndb_mgm_configuration_iterator * p =
    ed.theConfiguration->getOwnConfigIterator();
  if (p == 0)
  {
    abort();
  }

  Uint32 mtthreads = 0;
  ndb_mgm_get_int_parameter(p, CFG_DB_MT_THREADS, &mtthreads);
  ndbout << "NDBMT: MaxNoOfExecutionThreads=" << mtthreads << endl;

  globalData.isNdbMtLqh = true;

  /**
   * TODO add config for mt-classic
   */
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


static struct my_option my_long_options[] =
{
  NDB_STD_OPTS("ndbd"),
  { "initial", 256,
    "Perform initial start of ndbd, including cleaning the file system. "
    "Consult documentation before using this",
    (uchar**) &opt_initial, (uchar**) &opt_initial, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 },
  { "nostart", 'n',
    "Don't start ndbd immediately. Ndbd will await command from ndb_mgmd",
    (uchar**) &opt_no_start, (uchar**) &opt_no_start, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 },
  { "daemon", 'd', "Start ndbd as daemon (default)",
    (uchar**) &opt_daemon, (uchar**) &opt_daemon, 0,
    GET_BOOL, NO_ARG, 1, 0, 0, 0, 0, 0 },
  { "nodaemon", 256,
    "Do not start ndbd as daemon, provided for testing purposes",
    (uchar**) &opt_no_daemon, (uchar**) &opt_no_daemon, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 },
  { "foreground", 256,
    "Run real ndbd in foreground, provided for debugging purposes"
    " (implies --nodaemon)",
    (uchar**) &opt_foreground, (uchar**) &opt_foreground, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 },
  { "nowait-nodes", 256,
    "Nodes that will not be waited for during start",
    (uchar**) &opt_nowait_nodes, (uchar**) &opt_nowait_nodes, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { "initial-start", 256,
    "Perform initial start",
    (uchar**) &opt_initialstart, (uchar**) &opt_initialstart, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 },
  { "bind-address", 256,
    "Local bind address",
    (uchar**) &opt_bind_address, (uchar**) &opt_bind_address, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};

const char *load_default_groups[]= { "mysql_cluster", "ndbd", 0 };

static void short_usage_sub(void)
{
  ndb_short_usage_sub(my_progname, NULL);
}

static void usage()
{
  ndb_usage(short_usage_sub, load_default_groups, my_long_options);
}

extern "C" void ndbSetOwnVersion();

int main(int argc, char** argv)
{
  NDB_INIT(argv[0]);

  ndbSetOwnVersion();

  // Print to stdout/console
  g_eventLogger->createConsoleHandler();
  g_eventLogger->setCategory("ndbd");
  g_eventLogger->enable(Logger::LL_ON, Logger::LL_INFO);
  g_eventLogger->enable(Logger::LL_ON, Logger::LL_CRITICAL);
  g_eventLogger->enable(Logger::LL_ON, Logger::LL_ERROR);
  g_eventLogger->enable(Logger::LL_ON, Logger::LL_WARNING);

  // Turn on max loglevel for startup messages
  g_eventLogger->m_logLevel.setLogLevel(LogLevel::llStartUp, 15);

  ndb_opt_set_usage_funcs("ndbd", short_usage_sub, usage);
  load_defaults("my",load_default_groups,&argc,&argv);

#ifndef DBUG_OFF
  opt_debug= "d:t:O,/tmp/ndbd.trace";
#endif

  int ho_error;
  if ((ho_error=handle_options(&argc, &argv, my_long_options,
                               ndb_std_get_one_option)))
    exit(ho_error);

  if (opt_no_daemon || opt_foreground) {
    // --nodaemon or --forground implies --daemon=0
    opt_daemon= 0;
  }

  DBUG_PRINT("info", ("no_start=%d", opt_no_start));
  DBUG_PRINT("info", ("initial=%d", opt_initial));
  DBUG_PRINT("info", ("daemon=%d", opt_daemon));
  DBUG_PRINT("info", ("foreground=%d", opt_foreground));
  DBUG_PRINT("info", ("connect_str=%s", opt_connect_str));

  if (opt_nowait_nodes)
  {
    int res = g_nowait_nodes.parseMask(opt_nowait_nodes);
    if(res == -2 || (res > 0 && g_nowait_nodes.get(0)))
    {
      g_eventLogger->error("Invalid nodeid specified in nowait-nodes: %s",
                           opt_nowait_nodes);
      exit(-1);
    }
    else if (res < 0)
    {
      g_eventLogger->error("Unable to parse nowait-nodes argument: %s",
                           opt_nowait_nodes);
      exit(-1);
    }
  }

  globalEmulatorData.create();

  Configuration* theConfig = globalEmulatorData.theConfiguration;
  if(!theConfig->init(opt_no_start, opt_initial,
                      opt_initialstart, opt_daemon)){
    g_eventLogger->error("Failed to init Configuration");
    exit(-1);
  }
  char*cfg= getenv("WIN_NDBD_CFG");
  if(cfg) {
    int x,y,z;
    if(3!=sscanf(cfg,"%d %d %d",&x,&y,&z)) {
      g_eventLogger->error("internal error: couldn't find 3 parameters");
      exit(1);
    }
    theConfig->setInitialStart(x);
    globalData.theRestartFlag= (restartStates)y;
    globalData.ownId= z;
  }
  { // Do configuration
#ifndef NDB_WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
    theConfig->fetch_configuration(opt_connect_str, opt_bind_address);
  }

  my_setwd(NdbConfig_get_path(0), MYF(0));

  if (opt_daemon) {
    // Become a daemon
    char *lockfile= NdbConfig_PidFileName(globalData.ownId);
    char *logfile=  NdbConfig_StdoutFileName(globalData.ownId);
    NdbAutoPtr<char> tmp_aptr1(lockfile), tmp_aptr2(logfile);

#ifndef NDB_WIN32
    if (NdbDaemon_Make(lockfile, logfile, 0) == -1)
    {
      ndbout << "Cannot become daemon: " << NdbDaemon_ErrorText << endl;
      return 1;
    }
#endif
  }

#ifndef NDB_WIN32
  signal(SIGUSR1, handler_sigusr1);

  pid_t child = -1;
  while (!opt_foreground)
  {
    // setup reporting between child and parent
    int filedes[2];
    if (pipe(filedes))
    {
      g_eventLogger->error("pipe() failed with errno=%d (%s)",
                           errno, strerror(errno));
      return 1;
    }
    else
    {
      if (!(child_info_file_w= fdopen(filedes[1],"w")))
      {
        g_eventLogger->error("fdopen() failed with errno=%d (%s)",
                             errno, strerror(errno));
      }
      if (!(child_info_file_r= fdopen(filedes[0],"r")))
      {
        g_eventLogger->error("fdopen() failed with errno=%d (%s)",
                             errno, strerror(errno));
      }
    }

    if ((child = fork()) <= 0)
      break; // child or error

    /**
     * Parent
     */

    catchsigs(true);

    /**
     * We no longer need the mgm connection in this process
     * (as we are the angel, not ndb)
     *
     * We don't want to purge any allocated resources (nodeid), so
     * we set that option to false
     */
    theConfig->closeConfiguration(false);

    int status = 0, error_exit = 0, signum = 0;
    while(waitpid(child, &status, 0) != child);
    if(WIFEXITED(status)){
      switch(WEXITSTATUS(status)){
      case NRT_Default:
        g_eventLogger->info("Angel shutting down");
	reportShutdown(theConfig, 0, 0);
	exit(0);
	break;
      case NRT_NoStart_Restart:
	theConfig->setInitialStart(false);
	globalData.theRestartFlag = initial_state;
	break;
      case NRT_NoStart_InitialStart:
	theConfig->setInitialStart(true);
	globalData.theRestartFlag = initial_state;
	break;
      case NRT_DoStart_InitialStart:
	theConfig->setInitialStart(true);
	globalData.theRestartFlag = perform_start;
	break;
      default:
	error_exit = 1;
	if(theConfig->stopOnError()){
	  /**
	   * Error shutdown && stopOnError()
	   */
	  reportShutdown(theConfig, error_exit, 0);
	  exit(0);
	}
	// Fall-through
      case NRT_DoStart_Restart:
	theConfig->setInitialStart(false);
	globalData.theRestartFlag = perform_start;
	break;
      }
    } else {
      error_exit = 1;
      if (WIFSIGNALED(status))
      {
	signum = WTERMSIG(status);
	childReportSignal(signum);
      }
      else
      {
	signum = 127;
        g_eventLogger->info("Unknown exit reason. Stopped.");
      }
      if(theConfig->stopOnError()){
	/**
	 * Error shutdown && stopOnError()
	 */
	reportShutdown(theConfig, error_exit, 0);
	exit(0);
      }
    }

    if (!failed_startup_flag)
    {
      // Reset the counter for consecutive failed startups
      failed_startups = 0;
    }
    else if (failed_startups >= MAX_FAILED_STARTUPS && !theConfig->stopOnError())
    {
      /**
       * Error shutdown && stopOnError()
       */
      g_eventLogger->alert("Ndbd has failed %u consecutive startups. "
                           "Not restarting", failed_startups);
      reportShutdown(theConfig, error_exit, 0);
      exit(0);
    }
    failed_startup_flag = false;
    reportShutdown(theConfig, error_exit, 1);
    g_eventLogger->info("Ndb has terminated (pid %d) restarting", child);
    theConfig->fetch_configuration(opt_connect_str, opt_bind_address);
  }

  if (child >= 0)
    g_eventLogger->info("Angel pid: %d ndb pid: %d", getppid(), getpid());
  else if (child > 0)
    g_eventLogger->info("Ndb pid: %d", getpid());
  else
    g_eventLogger->info("Ndb started in foreground");
#else
  g_eventLogger->info("Ndb started");
#endif
  theConfig->setupConfiguration();
  systemInfo(* theConfig, * theConfig->m_logLevel); 
  
  NdbThread* pWatchdog = globalEmulatorData.theWatchDog->doStart();

  if (get_multithreaded_config(globalEmulatorData))
    return -1;

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
  
  catchsigs(false);
   
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
    exit(-1);
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
    exit(-1);
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


void 
systemInfo(const Configuration & config, const LogLevel & logLevel){
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
#elif defined NDB_SOLARIS // ok
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
#ifdef NDB_SOLARIS // ok
    g_eventLogger->info("NDB is running on a machine with %d processor(s) at %d MHz",
                        processor, speed);
#endif
  }
  if(logLevel.getLogLevel(LogLevel::llStartUp) > 3){
    Uint32 t = config.timeBetweenWatchDogCheck();
    g_eventLogger->info("WatchDog timer is set to %d ms", t);
  }

}

#define handler_register(signum, handler, ignore)\
{\
  if (ignore) {\
    if(signum != SIGCHLD)\
      signal(signum, SIG_IGN);\
  } else\
    signal(signum, handler);\
}

void 
catchsigs(bool ignore){
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
    handler_register(signals_shutdown[i], handler_shutdown, ignore);
  for(i = 0; i < sizeof(signals_error)/sizeof(signals_error[0]); i++)
    handler_register(signals_error[i], handler_error, ignore);
  for(i = 0; i < sizeof(signals_ignore)/sizeof(signals_ignore[0]); i++)
    handler_register(signals_ignore[i], SIG_IGN, ignore);
#ifdef SIGTRAP
  if (!opt_foreground)
    handler_register(SIGTRAP, handler_error, ignore);
#endif
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

extern "C"
void 
handler_sigusr1(int signum)
{
  if (!failed_startup_flag)
  {
    failed_startups++;
    failed_startup_flag = true;
  }
  g_eventLogger->info("Angel received ndbd startup failure count %u.", failed_startups);
}
