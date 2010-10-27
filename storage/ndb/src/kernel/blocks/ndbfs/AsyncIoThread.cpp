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

#include "AsyncIoThread.hpp"
#include "AsyncFile.hpp"
#include <ErrorHandlingMacros.hpp>
#include <kernel_types.h>
#include <NdbThread.h>
#include <signaldata/FsRef.hpp>
#include <signaldata/FsOpenReq.hpp>
#include <signaldata/FsReadWriteReq.hpp>
#include "Ndbfs.hpp"
#include <NdbSleep.h>

#include <EventLogger.hpp>
extern EventLogger * g_eventLogger;

AsyncIoThread::AsyncIoThread(class Ndbfs& fs, AsyncFile* file)
  : m_fs(fs)
{
  m_current_file = file;
  if (file)
  {
    theMemoryChannelPtr = &theMemoryChannel;
  }
  else
  {
    theMemoryChannelPtr = &m_fs.theToThreads;
  }
  theReportTo = &m_fs.theFromThreads;
}

static int numAsyncFiles = 0;

extern "C"
void *
runAsyncIoThread(void* arg)
{
  ((AsyncIoThread*)arg)->run();
  return (NULL);
}


struct NdbThread*
AsyncIoThread::doStart()
{
  // Stacksize for filesystem threads
#if !defined(DBUG_OFF) && defined (__hpux)
  // Empirical evidence indicates at least 32k
  const NDB_THREAD_STACKSIZE stackSize = 32768;
#else
  // Otherwise an 8k stack should be enough
  const NDB_THREAD_STACKSIZE stackSize = 8192;
#endif

  char buf[16];
  numAsyncFiles++;
  BaseString::snprintf(buf, sizeof(buf), "AsyncIoThread%d", numAsyncFiles);

  theStartMutexPtr = NdbMutex_Create();
  theStartConditionPtr = NdbCondition_Create();
  NdbMutex_Lock(theStartMutexPtr);
  theStartFlag = false;

  theThreadPtr = NdbThread_Create(runAsyncIoThread,
                                  (void**)this,
                                  stackSize,
                                  buf,
                                  NDB_THREAD_PRIO_MEAN);

  if (theThreadPtr == 0)
  {
    ERROR_SET(fatal, NDBD_EXIT_MEMALLOC,
              "","Could not allocate file system thread");
  }

  do
  {
    NdbCondition_Wait(theStartConditionPtr,
                      theStartMutexPtr);
  }
  while (theStartFlag == false);

  NdbMutex_Unlock(theStartMutexPtr);
  NdbMutex_Destroy(theStartMutexPtr);
  NdbCondition_Destroy(theStartConditionPtr);

  return theThreadPtr;
}

void
AsyncIoThread::shutdown()
{
  void *status;
  Request request;
  request.action = Request::end;
  this->theMemoryChannelPtr->writeChannel( &request );
  NdbThread_WaitFor(theThreadPtr, &status);
  NdbThread_Destroy(&theThreadPtr);
}

void
AsyncIoThread::dispatch(Request *request)
{
  assert(m_current_file);
  assert(m_current_file->getThread() == this);
  assert(theMemoryChannelPtr == &theMemoryChannel);
  theMemoryChannelPtr->writeChannel(request);
}

void
AsyncIoThread::run()
{
  Request *request;

  // Create theMemoryChannel in the thread that will wait for it
  NdbMutex_Lock(theStartMutexPtr);
  theStartFlag = true;
  NdbMutex_Unlock(theStartMutexPtr);
  NdbCondition_Signal(theStartConditionPtr);

  while (1)
  {
    request = theMemoryChannelPtr->readChannel();
    if (!request || request->action == Request::end)
    {
      DEBUG(ndbout_c("Nothing read from Memory Channel in AsyncFile"));
      theStartFlag = false;
      return;
    }//if

    AsyncFile * file = request->file;
    m_current_request= request;
    switch (request->action) {
    case Request::open:
      file->openReq(request);
      break;
    case Request::close:
      file->closeReq(request);
      break;
    case Request::closeRemove:
      file->closeReq(request);
      file->removeReq(request);
      break;
    case Request::readPartial:
    case Request::read:
      file->readReq(request);
      break;
    case Request::readv:
      file->readvReq(request);
      break;
    case Request::write:
      file->writeReq(request);
      break;
    case Request::writev:
      file->writevReq(request);
      break;
    case Request::writeSync:
      file->writeReq(request);
      file->syncReq(request);
      break;
    case Request::writevSync:
      file->writevReq(request);
      file->syncReq(request);
      break;
    case Request::sync:
      file->syncReq(request);
      break;
    case Request::append:
      file->appendReq(request);
      break;
    case Request::append_synch:
      file->appendReq(request);
      file->syncReq(request);
      break;
    case Request::rmrf:
      file->rmrfReq(request, file->theFileName.c_str(),
                    request->par.rmrf.own_directory);
      break;
    case Request::end:
      theStartFlag = false;
      return;
    case Request::allocmem:
    {
      allocMemReq(request);
      break;
    }
    case Request::buildindx:
      buildIndxReq(request);
      break;
    case Request::suspend:
      if (request->par.suspend.milliseconds)
      {
        g_eventLogger->debug("Suspend %s %u ms",
                             file->theFileName.c_str(),
                             request->par.suspend.milliseconds);
        NdbSleep_MilliSleep(request->par.suspend.milliseconds);
        continue;
      }
      else
      {
        g_eventLogger->debug("Suspend %s",
                             file->theFileName.c_str());
        theStartFlag = false;
        return;
      }
    default:
      DEBUG(ndbout_c("Invalid Request"));
      abort();
      break;
    }//switch
    m_last_request = request;
    m_current_request = 0;

    // No need to signal as ndbfs only uses tryRead
    theReportTo->writeChannelNoSignal(request);
    m_fs.wakeup();
  }
}

void
AsyncIoThread::allocMemReq(Request* request)
{
  bool res = request->par.alloc.ctx->m_mm.init(0);
  if (res == true)
    request->error = 0;
  else
    request->error = 1;
  
}

void
AsyncIoThread::buildIndxReq(Request* request)
{
  mt_BuildIndxReq req;
  memcpy(&req, &request->par.build.m_req, sizeof(req));
  req.mem_buffer = request->file->m_page_ptr.p;
  req.buffer_size = request->file->m_page_cnt * sizeof(GlobalPage);
  request->error = (* req.func_ptr)(&req);
}
