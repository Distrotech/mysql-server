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

#include <Ndb.hpp>
#include <NdbTransaction.hpp>
#include <NdbOperation.hpp>
#include <NdbScanOperation.hpp>
#include "NdbApiSignal.hpp"
#include "TransporterFacade.hpp"
#include "NdbUtil.hpp"
#include "API.hpp"
#include "NdbImpl.hpp"

#include <signaldata/ScanTab.hpp>

#include <NdbOut.hpp>
#include <NdbQueryOperationImpl.hpp>

/***************************************************************************
 * int  receiveSCAN_TABREF(NdbApiSignal* aSignal)
 *
 *  This means the scan could not be started, set status(s) to indicate 
 *  the failure
 *
 ****************************************************************************/
int			
NdbTransaction::receiveSCAN_TABREF(NdbApiSignal* aSignal){
  const ScanTabRef * ref = CAST_CONSTPTR(ScanTabRef, aSignal->getDataPtr());
  
#if TODO_SPJ_NEED_SCAN_TABREF
  printf("NdbTransaction::receiveSCAN_TABREF, apiPtr:%x\n", ref->apiConnectPtr);


  void * tPtr = theNdb->int2void(ref->apiConnectPtr);
  printf("  tPtr:%p\n", tPtr);

//assert(tPtr); // For now
  NdbReceiver* tOp = theNdb->void2rec(tPtr);
  printf("  NdbReceiver::magic:%d\n", tOp->checkMagicNumber());
  printf("  NdbQueryOperationImpl::magic:%d\n", ((NdbQueryOperationImpl*)tPtr)->checkMagicNumber());

  if (tOp && tOp->checkMagicNumber())
  {
    NdbQueryOperationImpl* const queryOpImpl 
      = static_cast<NdbQueryOperationImpl*>(tOp->m_owner);

    printf("  query->magic:%d\n", queryOpImpl->checkMagicNumber());
    if(queryOpImpl->checkMagicNumber())
    {
//    queryOpImpl->execSCAN_TABCONF(tcPtrI, opCount, tOp);
    }
  }
#endif //TODO_SPJ_NEED_SCAN_TABREF

  if(checkState_TransId(&ref->transId1)){
    if (theScanningOp == NULL) {
      printf("FIXME: No active Scanning op, 'REF' ignored - likely a NdbQuery\n");
      return 0;
    }
    theScanningOp->setErrorCode(ref->errorCode);
    theScanningOp->execCLOSE_SCAN_REP();
    if(!ref->closeNeeded){
      return 0;
    }

    /**
     * Setup so that close_impl will actually perform a close
     *   and not "close scan"-optimze it away
     */
    theScanningOp->m_conf_receivers_count++;
    theScanningOp->m_conf_receivers[0] = theScanningOp->m_receivers[0];
    theScanningOp->m_conf_receivers[0]->m_tcPtrI = ~0;
    return 0;
  } else {
#ifdef NDB_NO_DROPPED_SIGNAL
    abort();
#endif
  }

  return -1;
}

/*****************************************************************************
 * int  receiveSCAN_TABCONF(NdbApiSignal* aSignal)
 *
 * Receive SCAN_TABCONF
 * If scanStatus == 0 there is more records to read. Since signals may be 
 * received in any order we have to go through the lists with saved signals 
 * and check if all expected signals are there so that we can start to 
 * execute them.
 *
 * If scanStatus > 0 this indicates that the scan is finished and there are 
 * no more data to be read.
 * 
 *****************************************************************************/
int			
NdbTransaction::receiveSCAN_TABCONF(NdbApiSignal* aSignal, 
				   const Uint32 * ops, Uint32 len)
{
  const ScanTabConf * conf = CAST_CONSTPTR(ScanTabConf, aSignal->getDataPtr());
  if(checkState_TransId(&conf->transId1)){
    
    /*
      If both EndOfData is set and number of operations is 0, close the scan.
    */
    if (conf->requestInfo == ScanTabConf::EndOfData) {
      if (theScanningOp) {
        theScanningOp->execCLOSE_SCAN_REP();
      } else {
        printf("TODO ::receiveSCAN_TABCONF received EOF, len:%d\n", len);
      }
      return 1; // -> Finished
    }

    int scanStatus = 0;
    for(Uint32 i = 0; i<len; i += 3){
      Uint32 ptrI = * ops++;
      Uint32 tcPtrI = * ops++;
      Uint32 info = * ops++;
      
      void * tPtr = theNdb->int2void(ptrI);
      assert(tPtr); // For now
      NdbReceiver* tOp = theNdb->void2rec(tPtr);
      if (tOp && tOp->checkMagicNumber())
      {
        // Check if this is a linked operation.
        if(tOp->getType()==NdbReceiver::NDB_QUERY_OPERATION)
        {
          Uint32 opCount = info;
          NdbQueryOperationImpl* query = tOp->m_query_operation_impl;

          if (query->execSCAN_TABCONF(tcPtrI, opCount, tOp))
            scanStatus = 1; // We have result data, wakeup receiver
        }
        else
        {
          Uint32 opCount  = ScanTabConf::getRows(info);
          Uint32 totalLen = ScanTabConf::getLength(info);

          if (tcPtrI == RNIL && opCount == 0)
            theScanningOp->receiver_completed(tOp);
          else if (tOp->execSCANOPCONF(tcPtrI, totalLen, opCount))
            theScanningOp->receiver_delivered(tOp);

          // Plain Old scans always wakeup after SCAN_TABCONF
          scanStatus = 1;
        }
      }
    } //for
    return scanStatus;
  } else {
#ifdef NDB_NO_DROPPED_SIGNAL
    abort();
#endif
  }
  
  return -1;
}
