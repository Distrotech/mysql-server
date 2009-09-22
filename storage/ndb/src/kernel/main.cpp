/* 
   Copyright (C) 2003-2008 MySQL AB, 2009 Sun Microsystems, Inc.
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
#include <ndb_opts.h>
#include <kernel/NodeBitmask.hpp>

#include "ndbd.hpp"
#include "angel.hpp"

#include "Configuration.hpp"
#include "vm/SimBlockList.hpp"
#include "ThreadConfig.hpp"
#include <SignalLoggerManager.hpp>
#include <NdbOut.hpp>
#include <NdbDaemon.h>
#include <NdbSleep.h>
#include <NdbConfig.h>
#include <WatchDog.hpp>
#include <NdbAutoPtr.hpp>
#include <Properties.hpp>

#include <EventLogger.hpp>
extern EventLogger * g_eventLogger;

static int opt_daemon, opt_no_daemon, opt_foreground,
  opt_initial, opt_no_start, opt_initialstart, opt_verbose;
static const char* opt_nowait_nodes = 0;
static const char* opt_bind_address = 0;

extern NdbNodeBitmask g_nowait_nodes;

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
    GET_BOOL, NO_ARG, IF_WIN(0,1), 0, 0, 0, 0, 0 },
  { "nodaemon", 256,
    "Do not start ndbd as daemon, provided for testing purposes",
    (uchar**) &opt_no_daemon, (uchar**) &opt_no_daemon, 0,
    GET_BOOL, NO_ARG, IF_WIN(1,0), 0, 0, 0, 0, 0 },
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
    "Perform a partial initial start of the cluster.  "
    "Each node should be started with this option, as well as --nowait-nodes",
    (uchar**) &opt_initialstart, (uchar**) &opt_initialstart, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 },
  { "bind-address", 256,
    "Local bind address",
    (uchar**) &opt_bind_address, (uchar**) &opt_bind_address, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { "verbose", 'v',
    "Write more log messages",
    (uchar**) &opt_verbose, (uchar**) &opt_verbose, 0,
    GET_BOOL, NO_ARG, 0, 0, 1, 0, 0, 0 },
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

extern int g_ndb_init_need_monotonic;

int main(int argc, char** argv)
{
  g_ndb_init_need_monotonic = 1;
  NDB_INIT(argv[0]);

  ndbSetOwnVersion();

  // Print to stdout/console
  g_eventLogger->createConsoleHandler();
  g_eventLogger->setCategory("ndbd");

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

  // Turn on debug printouts if --verbose
  if (opt_verbose)
    g_eventLogger->enable(Logger::LL_DEBUG);

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
    theConfig->fetch_configuration(opt_connect_str, opt_bind_address);
  }

  my_setwd(NdbConfig_get_path(0), MYF(0));

#ifndef NDB_WIN32
  if (!opt_foreground)
  {
    if (angel_run(opt_connect_str,
                  opt_bind_address,
                  opt_initialstart,
                  opt_daemon))
      return 1;
    // ndbd continues here
  }
  else
    g_eventLogger->info("Ndb started in foreground");
#else
  g_eventLogger->info("Ndb started");
#endif

  int res = ndbd_run(opt_foreground);
  ndbd_exit(res);
  return res;
}

