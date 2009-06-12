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

#include "Emulator.hpp"
#include <FastScheduler.hpp>
#include <SignalLoggerManager.hpp>
#include <TransporterRegistry.hpp>
#include <TimeQueue.hpp>

#include "Configuration.hpp"
#include "WatchDog.hpp"
#include "ThreadConfig.hpp"
#include "SimBlockList.hpp"

#include <NodeState.hpp>
#include "ndbd_malloc_impl.hpp"

#include <NdbMem.h>
#include <NdbMutex.h>
#include <NdbSleep.h>

#include <EventLogger.hpp>

void childExit(int code, Uint32 currentStartPhase);
void childAbort(int code, Uint32 currentStartPhase);

extern "C" {
  extern void (* ndb_new_handler)();
}
extern EventLogger * g_eventLogger;
#ifndef NDB_WIN
extern my_bool opt_core;
#endif
// instantiated and updated in NdbcntrMain.cpp
extern Uint32 g_currentStartPhase;

/**
 * Declare the global variables 
 */

#ifndef NO_EMULATED_JAM
/*
  This is the jam buffer used for non-threaded ndbd (but present also
  in threaded ndbd to allow sharing of object files among the two
  binaries).
 */
EmulatedJamBuffer theEmulatedJamBuffer;
#endif

   GlobalData globalData;

   TimeQueue globalTimeQueue;
   FastScheduler globalScheduler;
   extern TransporterRegistry globalTransporterRegistry;

#ifdef VM_TRACE
   SignalLoggerManager globalSignalLoggers;
#endif

EmulatorData globalEmulatorData;
NdbMutex * theShutdownMutex = 0;
int simulate_error_during_shutdown= 0;

EmulatorData::EmulatorData(){
  theConfiguration = 0;
  theWatchDog      = 0;
  theThreadConfig  = 0;
  theSimBlockList  = 0;
  theShutdownMutex = 0;
  m_socket_server = 0;
  m_mem_manager = 0;
}

void
ndb_new_handler_impl(){
  ERROR_SET(fatal, NDBD_EXIT_MEMALLOC, "New handler", "");
}

void
EmulatorData::create(){
  /*
    Global jam() buffer, for non-multithreaded operation.
    For multithreaded ndbd, each thread will set a local jam buffer later.
  */
  NdbThread_SetTlsKey(NDB_THREAD_TLS_JAM, (void *)&theEmulatedJamBuffer);

  NdbMem_Create();

  theConfiguration = new Configuration();
  theWatchDog      = new WatchDog();
  theThreadConfig  = new ThreadConfig();
  theSimBlockList  = new SimBlockList();
  m_socket_server  = new SocketServer();
  m_mem_manager    = new Ndbd_mem_manager();

  theShutdownMutex = NdbMutex_Create();

  ndb_new_handler = ndb_new_handler_impl;
}

void
EmulatorData::destroy(){
  if(theConfiguration)
    delete theConfiguration; theConfiguration = 0;
  if(theWatchDog)
    delete theWatchDog; theWatchDog = 0;
  if(theThreadConfig)
    delete theThreadConfig; theThreadConfig = 0;
  if(theSimBlockList)
    delete theSimBlockList; theSimBlockList = 0;
  if(m_socket_server)
    delete m_socket_server; m_socket_server = 0;
  NdbMutex_Destroy(theShutdownMutex);
  if (m_mem_manager)
    delete m_mem_manager; m_mem_manager = 0;
  
  NdbMem_Destroy();
}

void
NdbShutdown(NdbShutdownType type,
	    NdbRestartType restartType)
{
  if(type == NST_ErrorInsert)
  {
    type = NST_Restart;
    restartType = (NdbRestartType)
      globalEmulatorData.theConfiguration->getRestartOnErrorInsert();
    if(restartType == NRT_Default)
    {
      type = NST_ErrorHandler;
      globalEmulatorData.theConfiguration->stopOnError(true);
    }
  }
  
  if((type == NST_ErrorHandlerSignal) || // Signal handler has already locked mutex
     (NdbMutex_Trylock(theShutdownMutex) == 0)){
    globalData.theRestartFlag = perform_stop;

    bool restart = false;

    if((type != NST_Normal && 
	globalEmulatorData.theConfiguration->stopOnError() == false) ||
       type == NST_Restart) 
    {
      restart  = true;
    }


    const char * shutting = "shutting down";
    if(restart)
    {
      shutting = "restarting";
    }
    
    switch(type){
    case NST_Normal:
      g_eventLogger->info("Shutdown initiated");
      break;
    case NST_Watchdog:
      g_eventLogger->info("Watchdog %s system", shutting);
      break;
    case NST_ErrorHandler:
      g_eventLogger->info("Error handler %s system", shutting);
      break;
    case NST_ErrorHandlerSignal:
      g_eventLogger->info("Error handler signal %s system", shutting);
      break;
    case NST_ErrorHandlerStartup:
      g_eventLogger->info("Error handler startup %s system", shutting);
      break;
    case NST_Restart:
      g_eventLogger->info("Restarting system");
      break;
    default:
      g_eventLogger->info("Error handler %s system (unknown type: %u)",
                          shutting, (unsigned)type);
      type = NST_ErrorHandler;
      break;
    }
    
    const char * exitAbort = 0;
#ifndef NDB_WIN
    if (opt_core)
      exitAbort = "aborting";
    else
#endif
      exitAbort = "exiting";
    
    if(type == NST_Watchdog)
    {
      /**
       * Very serious, don't attempt to free, just die!!
       */
      g_eventLogger->info("Watchdog shutdown completed - %s", exitAbort);
#ifndef NDB_WIN
      if (opt_core)
      {
	childAbort(-1,g_currentStartPhase);
      }
      else
#endif
      {
	childExit(-1,g_currentStartPhase);
      }
    }
    
#ifndef NDB_WIN32
    if (simulate_error_during_shutdown) 
    {
      kill(getpid(), simulate_error_during_shutdown);
      while(true)
	NdbSleep_MilliSleep(10);
    }
#endif

    globalEmulatorData.theWatchDog->doStop();
    
#ifdef VM_TRACE
    FILE * outputStream = globalSignalLoggers.setOutputStream(0);
    if(outputStream != 0)
      fclose(outputStream);
#endif
    

#ifndef NDB_WIN32
#define UNLOAD (type == NST_ErrorInsert && opt_core)
#else
#define UNLOAD (0)
#endif
    /**
     * Don't touch transporter here (yet)
     *   cause with ndbmtd, there are locks and nasty stuff
     *   and we don't know which we are holding...
     */
#if NOT_YET
    
    /**
     * Stop all transporter connection attempts and accepts
     */
    globalEmulatorData.m_socket_server->stopServer();
    globalEmulatorData.m_socket_server->stopSessions();
    globalTransporterRegistry.stop_clients();

    /**
     * Stop transporter communication with other nodes
     */
    globalTransporterRegistry.stopSending();
    globalTransporterRegistry.stopReceiving();
    
    /**
     * Remove all transporters
     */
    globalTransporterRegistry.removeAll();
#endif
    
    if(UNLOAD)
    {
      globalEmulatorData.theSimBlockList->unload();    
      NdbMutex_Unlock(theShutdownMutex);
      globalEmulatorData.destroy();
    }
    
    if(type != NST_Normal && type != NST_Restart)
    {
      // Signal parent that error occured during startup
#ifndef NDB_WIN
      if (type == NST_ErrorHandlerStartup)
	kill(getppid(), SIGUSR1);
#endif
      g_eventLogger->info("Error handler shutdown completed - %s", exitAbort);
#ifndef NDB_WIN
      if (opt_core)
      {
	childAbort(-1,g_currentStartPhase);
      }
      else
#endif
      {
	childExit(-1,g_currentStartPhase);
      }
    }
    
    /**
     * This is a normal restart, depend on angel
     */
    if(type == NST_Restart){
      childExit(restartType,g_currentStartPhase);
    }
    
    g_eventLogger->info("Shutdown completed - exiting");
  } 
  else 
  {
    /**
     * Shutdown is already in progress
     */
    
    /** 
     * If this is the watchdog, kill system the hard way
     */
    if (type== NST_Watchdog)
    {
      g_eventLogger->info("Watchdog is killing system the hard way");
#if defined VM_TRACE
      childAbort(-1,g_currentStartPhase);
#else
      childExit(-1,g_currentStartPhase);
#endif
    }
    
    while(true)
      NdbSleep_MilliSleep(10);
  }
}

