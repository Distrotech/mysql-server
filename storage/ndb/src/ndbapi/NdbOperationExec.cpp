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
#include <NdbOperation.hpp>
#include <NdbTransaction.hpp>
#include <NdbBlob.hpp>
#include "NdbApiSignal.hpp"
#include <Ndb.hpp>
#include <NdbRecAttr.hpp>
#include "NdbUtil.hpp"
#include "NdbInterpretedCode.hpp"

#include "Interpreter.hpp"
#include <AttributeHeader.hpp>
#include <signaldata/TcKeyReq.hpp>
#include <signaldata/TcKeyRef.hpp>
#include <signaldata/KeyInfo.hpp>
#include <signaldata/AttrInfo.hpp>
#include <signaldata/ScanTab.hpp>

#include <ndb_version.h>

#include "API.hpp"
#include <NdbOut.hpp>



void
NdbOperation::setLastFlag(NdbApiSignal* signal, Uint32 lastFlag)
{
  TcKeyReq * const req = CAST_PTR(TcKeyReq, signal->getDataPtrSend());
  TcKeyReq::setExecuteFlag(req->requestInfo, lastFlag);
}

/******************************************************************************
int doSend()

Return Value:   Return >0 : send was succesful, returns number of signals sent
                Return -1: In all other case.   
Parameters:     aProcessorId: Receiving processor node
Remark:         Sends the TCKEYREQ signal and optional KEYINFO and ATTRINFO 
                signals.
******************************************************************************/
int
NdbOperation::doSend(int aNodeId, Uint32 lastFlag)
{
  int tReturnCode;
  int tSignalCount = 0;
  assert(theTCREQ != NULL);
  setLastFlag(theTCREQ, lastFlag);

  if (m_attribute_record != NULL)
  {
    /* NdbRecord send - single long signal */
    tReturnCode= doSendNdbRecord(aNodeId);
    if (tReturnCode == -1)
      return -1;

    tSignalCount= 1;
  }
  else
  {
    /* Old Api send - signal train */
    TransporterFacade *tp = theNdb->theImpl->m_transporter_facade;
    tReturnCode = tp->sendSignal(theTCREQ, aNodeId);
    tSignalCount++;
    if (tReturnCode == -1) {
      return -1;
    }
    NdbApiSignal *tSignal = theTCREQ->next();
    while (tSignal != NULL) {
      NdbApiSignal* tnextSignal = tSignal->next();
      tReturnCode = tp->sendSignal(tSignal, aNodeId);
      tSignal = tnextSignal;
      if (tReturnCode == -1) {
        return -1;
      }
      tSignalCount++;
    }//while
    tSignal = theFirstATTRINFO;
    while (tSignal != NULL) {
      NdbApiSignal* tnextSignal = tSignal->next();
      tReturnCode = tp->sendSignal(tSignal, aNodeId);
      tSignal = tnextSignal;
      if (tReturnCode == -1) {
        return -1;
      }
      tSignalCount++;
    }//while
  }

  /* Todo : Consider calling NdbOperation::postExecuteRelease()
   * Ideally it should be called outside TP mutex, so not added
   * here yet
   */

  theNdbCon->OpSent();
  return tSignalCount;
}//NdbOperation::doSend()


int
NdbOperation::doSendNdbRecord(int aNodeId)
{
 /*
  * Send a long signal to the kernel.
  * KeyInfo and AttrInfo for NdbRecord are always sent as
  * separate sections, with none in the TCKEYREQ
  */
  TransporterFacade *tp = theNdb->theImpl->m_transporter_facade;

  Uint32 numSecs= 1;
  GenericSectionPtr secs[2];
  SignalSectionIterator keyInfoIter(theTCREQ->next());
  SignalSectionIterator attrInfoIter(theFirstATTRINFO);
  
  /* KeyInfo - always present for TCKEY/INDXREQ*/
  secs[0].sz= theTupKeyLen;
  secs[0].sectionIter= &keyInfoIter;

  /* AttrInfo - not always needed (e.g. Delete) */
  if (theTotalCurrAI_Len != 0)
  {
    secs[1].sz= theTotalCurrAI_Len;
    secs[1].sectionIter= &attrInfoIter;
    numSecs++;
  }

  return tp->sendSignal(theTCREQ, aNodeId, &secs[0], numSecs);
}

/***************************************************************************
int prepareSend(Uint32 aTC_ConnectPtr,
                Uint64 aTransactionId)

Return Value:   Return 0 : preparation of send was succesful.
                Return -1: In all other case.   
Parameters:     aTC_ConnectPtr: the Connect pointer to TC.
		aTransactionId:	the Transaction identity of the transaction.
Remark:         Puts the the data into TCKEYREQ signal and optional KEYINFO and ATTRINFO signals.
***************************************************************************/
int
NdbOperation::prepareSend(Uint32 aTC_ConnectPtr, 
			  Uint64 aTransId,
			  AbortOption ao)
{
  Uint32 tTransId1, tTransId2;
  Uint32 tReqInfo;
  Uint8 tInterpretInd = theInterpretIndicator;
  Uint8 tDirtyIndicator = theDirtyIndicator;
  Uint32 tTotalCurrAI_Len = theTotalCurrAI_Len;
  theErrorLine = 0;

  if (tInterpretInd != 1) {
    OperationType tOpType = theOperationType;
    OperationStatus tStatus = theStatus;
    if ((tOpType == UpdateRequest) ||
	(tOpType == InsertRequest) ||
	(tOpType == WriteRequest)) {
      if (tStatus != SetValue) {
        setErrorCodeAbort(4116);
        return -1;
      }//if
    } else if ((tOpType == ReadRequest) || (tOpType == ReadExclusive) ||
	       (tOpType == DeleteRequest)) {
      if (tStatus != GetValue) {
        setErrorCodeAbort(4116);
        return -1;
      } 
      else if(unlikely(tDirtyIndicator && tTotalCurrAI_Len == 0))
      {
	getValue(NdbDictionary::Column::FRAGMENT);
	tTotalCurrAI_Len = theTotalCurrAI_Len;
	assert(theTotalCurrAI_Len);
      }
      else if (tOpType != DeleteRequest)
      {
	assert(tOpType == ReadRequest || tOpType == ReadExclusive);
        tTotalCurrAI_Len = repack_read(tTotalCurrAI_Len);
      }
    } else {
      setErrorCodeAbort(4005);      
      return -1;
    }//if
  } else {
    if (prepareSendInterpreted() == -1) {
      return -1;
    }//if
    tTotalCurrAI_Len = theTotalCurrAI_Len;
  }//if
  
//-------------------------------------------------------------
// We start by filling in the first 9 unconditional words of the
// TCKEYREQ signal.
//-------------------------------------------------------------
  TcKeyReq * const tcKeyReq = CAST_PTR(TcKeyReq, theTCREQ->getDataPtrSend());

  Uint32 tTableId = m_accessTable->m_id;
  Uint32 tSchemaVersion = m_accessTable->m_version;
  
  tcKeyReq->apiConnectPtr      = aTC_ConnectPtr;
  tcKeyReq->apiOperationPtr    = ptr2int();
  // Check if too much attrinfo have been defined
  if (tTotalCurrAI_Len > TcKeyReq::MaxTotalAttrInfo){
    setErrorCodeAbort(4257);
    return -1;
  }
  Uint32 TattrLen = 0;
  tcKeyReq->setAttrinfoLen(TattrLen, tTotalCurrAI_Len);
  tcKeyReq->setAPIVersion(TattrLen, NDB_VERSION);
  tcKeyReq->attrLen            = TattrLen;

  tcKeyReq->tableId            = tTableId;
  tcKeyReq->tableSchemaVersion = tSchemaVersion;
  tTransId1 = (Uint32) aTransId;
  tTransId2 = (Uint32) (aTransId >> 32);
  
  Uint8 tSimpleIndicator = theSimpleIndicator;
  Uint8 tCommitIndicator = theCommitIndicator;
  Uint8 tStartIndicator = theStartIndicator;
  Uint8 tInterpretIndicator = theInterpretIndicator;
  Uint8 tNoDisk = m_no_disk_flag;

  /**
   * A dirty read, can not abort the transaction
   */
  Uint8 tReadInd = (theOperationType == ReadRequest);
  Uint8 tDirtyState = tReadInd & tDirtyIndicator;

  tcKeyReq->transId1           = tTransId1;
  tcKeyReq->transId2           = tTransId2;
  
  tReqInfo = 0;
  if (tTotalCurrAI_Len <= TcKeyReq::MaxAttrInfo) {
    tcKeyReq->setAIInTcKeyReq(tReqInfo, tTotalCurrAI_Len);
  } else {
    tcKeyReq->setAIInTcKeyReq(tReqInfo, TcKeyReq::MaxAttrInfo);
  }//if

  tcKeyReq->setSimpleFlag(tReqInfo, tSimpleIndicator);
  tcKeyReq->setCommitFlag(tReqInfo, tCommitIndicator);
  tcKeyReq->setStartFlag(tReqInfo, tStartIndicator);
  tcKeyReq->setInterpretedFlag(tReqInfo, tInterpretIndicator);
  tcKeyReq->setNoDiskFlag(tReqInfo, tNoDisk);

  OperationType tOperationType = theOperationType;
  Uint32 tTupKeyLen = theTupKeyLen;
  Uint8 abortOption = (ao == DefaultAbortOption) ? (Uint8) m_abortOption : (Uint8) ao;

  tcKeyReq->setDirtyFlag(tReqInfo, tDirtyIndicator);
  tcKeyReq->setOperationType(tReqInfo, tOperationType);
  tcKeyReq->setKeyLength(tReqInfo, tTupKeyLen);
  
  // A dirty read is always ignore error
  abortOption = tDirtyState ? (Uint8) AO_IgnoreError : (Uint8) abortOption;
  tcKeyReq->setAbortOption(tReqInfo, abortOption);
  m_abortOption = abortOption;
  
  Uint8 tDistrKeyIndicator = theDistrKeyIndicator_;
  Uint8 tScanIndicator = theScanInfo & 1;

  tcKeyReq->setDistributionKeyFlag(tReqInfo, tDistrKeyIndicator);
  tcKeyReq->setScanIndFlag(tReqInfo, tScanIndicator);

  tcKeyReq->requestInfo  = tReqInfo;

//-------------------------------------------------------------
// The next step is to fill in the upto three conditional words.
//-------------------------------------------------------------
  Uint32* tOptionalDataPtr = &tcKeyReq->scanInfo;
  Uint32 tDistrGHIndex = tScanIndicator;
  Uint32 tDistrKeyIndex = tDistrGHIndex;

  Uint32 tScanInfo = theScanInfo;
  Uint32 tDistrKey = theDistributionKey;

  tOptionalDataPtr[0] = tScanInfo;
  tOptionalDataPtr[tDistrKeyIndex] = tDistrKey;

//-------------------------------------------------------------
// The next is step is to compress the key data part of the
// TCKEYREQ signal.
//-------------------------------------------------------------
  Uint32 tKeyIndex = tDistrKeyIndex + tDistrKeyIndicator;
  Uint32* tKeyDataPtr = &tOptionalDataPtr[tKeyIndex];
  Uint32 Tdata1 = tcKeyReq->keyInfo[0];
  Uint32 Tdata2 = tcKeyReq->keyInfo[1];
  Uint32 Tdata3 = tcKeyReq->keyInfo[2];
  Uint32 Tdata4 = tcKeyReq->keyInfo[3];
  Uint32 Tdata5;

  tKeyDataPtr[0] = Tdata1;
  tKeyDataPtr[1] = Tdata2;
  tKeyDataPtr[2] = Tdata3;
  tKeyDataPtr[3] = Tdata4;
  if (tTupKeyLen > 4) {
    Tdata1 = tcKeyReq->keyInfo[4];
    Tdata2 = tcKeyReq->keyInfo[5];
    Tdata3 = tcKeyReq->keyInfo[6];
    Tdata4 = tcKeyReq->keyInfo[7];

    tKeyDataPtr[4] = Tdata1;
    tKeyDataPtr[5] = Tdata2;
    tKeyDataPtr[6] = Tdata3;
    tKeyDataPtr[7] = Tdata4;
  }//if
//-------------------------------------------------------------
// Finally we also compress the ATTRINFO part of the signal.
// We optimise by using the if-statement for sending KEYINFO
// signals to calculating the new Attrinfo Index.
//-------------------------------------------------------------
  Uint32 tAttrInfoIndex;  

  if (tTupKeyLen > TcKeyReq::MaxKeyInfo) {
    /**
     *	Set transid, TC connect ptr and length in the KEYINFO signals
     */
    NdbApiSignal* tSignal = theTCREQ->next();
    Uint32 remainingKey = tTupKeyLen - TcKeyReq::MaxKeyInfo;
    do {
      Uint32* tSigDataPtr = tSignal->getDataPtrSend();
      NdbApiSignal* tnextSignal = tSignal->next();
      tSigDataPtr[0] = aTC_ConnectPtr;
      tSigDataPtr[1] = tTransId1;
      tSigDataPtr[2] = tTransId2;
      if (remainingKey > KeyInfo::DataLength) {
	// The signal is full
	tSignal->setLength(KeyInfo::MaxSignalLength);
	remainingKey -= KeyInfo::DataLength;
      }
      else {
	// Last signal
	tSignal->setLength(KeyInfo::HeaderLength + remainingKey);
	remainingKey = 0;
      }
      tSignal = tnextSignal;
    } while (tSignal != NULL);
    tAttrInfoIndex = tKeyIndex + TcKeyReq::MaxKeyInfo;
  } else {
    tAttrInfoIndex = tKeyIndex + tTupKeyLen;
  }//if

//-------------------------------------------------------------
// Perform the Attrinfo packing in the TCKEYREQ signal started
// above.
//-------------------------------------------------------------
  Uint32* tAIDataPtr = &tOptionalDataPtr[tAttrInfoIndex];
  Tdata1 = tcKeyReq->attrInfo[0];
  Tdata2 = tcKeyReq->attrInfo[1];
  Tdata3 = tcKeyReq->attrInfo[2];
  Tdata4 = tcKeyReq->attrInfo[3];
  Tdata5 = tcKeyReq->attrInfo[4];

  theTCREQ->setLength(tcKeyReq->getAIInTcKeyReq(tReqInfo) +
                      tAttrInfoIndex + TcKeyReq::StaticLength);

  tAIDataPtr[0] = Tdata1;
  tAIDataPtr[1] = Tdata2;
  tAIDataPtr[2] = Tdata3;
  tAIDataPtr[3] = Tdata4;
  tAIDataPtr[4] = Tdata5;

/***************************************************
*  Send the ATTRINFO signals.
***************************************************/
  if (tTotalCurrAI_Len > 5) {
    // Set the last signal's length.
    NdbApiSignal* tSignal = theFirstATTRINFO;
    theCurrentATTRINFO->setLength(theAI_LenInCurrAI);
    do {
      Uint32* tSigDataPtr = tSignal->getDataPtrSend();
      NdbApiSignal* tnextSignal = tSignal->next();
      tSigDataPtr[0] = aTC_ConnectPtr;
      tSigDataPtr[1] = tTransId1;
      tSigDataPtr[2] = tTransId2;
      tSignal = tnextSignal;
    } while (tSignal != NULL);
  }//if
  theStatus = WaitResponse;
  theReceiver.prepareSend();
  return 0;
}//NdbOperation::prepareSend()

Uint32
NdbOperation::repack_read(Uint32 len)
{
  Uint32 i;
  Uint32 maxId = 0, check = 0;
  Uint32 save = len;
  Bitmask<MAXNROFATTRIBUTESINWORDS> mask;
  NdbApiSignal *tSignal = theFirstATTRINFO;
  TcKeyReq * const tcKeyReq = CAST_PTR(TcKeyReq, theTCREQ->getDataPtrSend());
  Uint32 cols = m_currentTable->m_columns.size();

  /* Build bitmask for ATTRINFO in TCKEYREQ signal */
  Uint32 *ptr = tcKeyReq->attrInfo;
  for (i = 0; len && i < 5; i++, len--)
  {
    AttributeHeader tmp(* ptr++);
    Uint32 id = tmp.getAttributeId();
    if (id >= NDB_MAX_ATTRIBUTES_IN_TABLE)
    {
      // Dont support == fallback
      return save;
    }
    mask.set(id);
    /* check==0 if cols are in ascending attrId order
     * as (id-maxId) == 0
     */
    maxId = (id > maxId) ? id : maxId;
    check |= (id - maxId);
  }

  /* Build bitmask for ATTRINFO in extra ATTRINFO signals */
  Uint32 cnt = 0;
  while (len)
  {
    cnt++;
    assert(tSignal);
    ptr = tSignal->getDataPtrSend() + AttrInfo::HeaderLength;
    for (i = 0; len && i<AttrInfo::DataLength; i++, len--)
    {
      AttributeHeader tmp(* ptr++);
      Uint32 id = tmp.getAttributeId();
      if (id >= NDB_MAX_ATTRIBUTES_IN_TABLE)
      {
        // Dont support == fallback
        return save;
      }
      
      mask.set(id);

      /* check==0 if cols are in ascending attrId order
       * as (id-maxId) == 0
       */
      maxId = (id > maxId) ? id : maxId;
      check |= (id - maxId);
    }
    tSignal = tSignal->next();
  }
  const Uint32 newlen = 1 + (maxId >> 5);
  const bool all = cols == save;
  if (check == 0)
  {
    /* AttrInfos are in ascending order, ok to use READ_ALL
     * or READ_PACKED
     * (Correct NdbRecAttrs will be used when data is received)
     */
    assert(1+ MAXNROFATTRIBUTESINWORDS <= TcKeyReq::MaxAttrInfo);
    
    theNdb->releaseSignals(cnt, theFirstATTRINFO, theCurrentATTRINFO);
    theFirstATTRINFO = 0;
    theCurrentATTRINFO = 0;
    ptr = tcKeyReq->attrInfo;
    if (all)
    {
      AttributeHeader::init(ptr, AttributeHeader::READ_ALL, cols);
      return 1;
    }
    else  
    {
      AttributeHeader::init(ptr, AttributeHeader::READ_PACKED, 4*newlen);
      memcpy(ptr + 1, &mask, 4*MAXNROFATTRIBUTESINWORDS);
      return 1+newlen;
    }
  }
  
  return save;
}


/***************************************************************************
int prepareSendInterpreted()

Make preparations to send an interpreted operation.
Return Value:   Return 0 : succesful.
                Return -1: In all other case.   
***************************************************************************/
int
NdbOperation::prepareSendInterpreted()
{
  Uint32 tTotalCurrAI_Len = theTotalCurrAI_Len;
  Uint32 tInitReadSize = theInitialReadSize;
  assert (theStatus != UseNdbRecord); // Should never get here for NdbRecord.
  if (theStatus == ExecInterpretedValue) {
    if (insertATTRINFO(Interpreter::EXIT_OK) != -1) {
//-------------------------------------------------------------------------
// Since we read the total length before inserting the last entry in the
// signals we need to add one to the total length.
//-------------------------------------------------------------------------

      theInterpretedSize = (tTotalCurrAI_Len + 1) -
        (tInitReadSize + AttrInfo::SectionSizeInfoLength);

    } else {
      return -1;
    }//if
  } else if (theStatus == FinalGetValue) {

    theFinalReadSize = tTotalCurrAI_Len -
      (tInitReadSize + theInterpretedSize + theFinalUpdateSize 
       + AttrInfo::SectionSizeInfoLength);

  } else if (theStatus == SetValueInterpreted) {

    theFinalUpdateSize = tTotalCurrAI_Len -
       (tInitReadSize + theInterpretedSize 
        + AttrInfo::SectionSizeInfoLength);

  } else if (theStatus == SubroutineEnd) {

    theSubroutineSize = tTotalCurrAI_Len -
      (tInitReadSize + theInterpretedSize + 
       theFinalUpdateSize + theFinalReadSize 
       + AttrInfo::SectionSizeInfoLength);

  } else if (theStatus == GetValue) {
    theInitialReadSize = tTotalCurrAI_Len - AttrInfo::SectionSizeInfoLength;
  } else {
    setErrorCodeAbort(4116);
    return -1;
  }

  /*
    Fix jumps by patching in the correct address for the corresponding label.
  */
  while (theFirstBranch != NULL) {
    Uint32 tRelAddress;
    Uint32 tLabelAddress = 0;
    int     tAddress = -1;
    NdbBranch* tNdbBranch = theFirstBranch;
    Uint32 tBranchLabel = tNdbBranch->theBranchLabel;
    NdbLabel* tNdbLabel = theFirstLabel;
    if (tBranchLabel >= theNoOfLabels) {
      setErrorCodeAbort(4221);
      return -1;
    }//if

    // Find the label address
    while (tNdbLabel != NULL) {
      for(tLabelAddress = 0; tLabelAddress<16; tLabelAddress++){
	const Uint32 labelNo = tNdbLabel->theLabelNo[tLabelAddress];
	if(tBranchLabel == labelNo){
	  tAddress = tNdbLabel->theLabelAddress[tLabelAddress];
	  break;
	}
      }
      
      if(tAddress != -1)
	break;
      tNdbLabel = tNdbLabel->theNext;
    }//while
    if (tAddress == -1) {
//-------------------------------------------------------------------------
// We were unable to find any label which the branch refers to. This means
// that the application have not programmed the interpreter program correctly.
//-------------------------------------------------------------------------
      setErrorCodeAbort(4222);
      return -1;
    }//if
    if (tNdbLabel->theSubroutine[tLabelAddress] != tNdbBranch->theSubroutine) {
      setErrorCodeAbort(4224);
      return -1;
    }//if
    // Now it is time to update the signal data with the relative branch jump.
    if (tAddress < int(tNdbBranch->theBranchAddress)) {
      tRelAddress = (tNdbBranch->theBranchAddress - tAddress) << 16;
      
      // Indicate backward jump direction
      tRelAddress = tRelAddress + (1 << 31);

    } else if (tAddress > int(tNdbBranch->theBranchAddress)) {
      tRelAddress = (tAddress - tNdbBranch->theBranchAddress) << 16;
    } else {
       setErrorCodeAbort(4223);
       return -1;
    }//if

    NdbApiSignal* tSignal = tNdbBranch->theSignal;
    Uint32 tReadData = tSignal->readData(tNdbBranch->theSignalAddress);
    tSignal->setData((tRelAddress + tReadData), tNdbBranch->theSignalAddress);
      
    theFirstBranch = theFirstBranch->theNext;
    theNdb->releaseNdbBranch(tNdbBranch);
  }//while

  while (theFirstCall != NULL) {
    Uint32 tSubroutineCount = 0;
    int     tAddress = -1;
    NdbSubroutine* tNdbSubroutine;
    NdbCall* tNdbCall = theFirstCall;
    if (tNdbCall->theSubroutine >= theNoOfSubroutines) {
      setErrorCodeAbort(4221);
      return -1;
    }//if
// Find the subroutine address
    tNdbSubroutine = theFirstSubroutine;
    while (tNdbSubroutine != NULL) {
      tSubroutineCount += 16;
      if (tNdbCall->theSubroutine < tSubroutineCount) {
// Subroutine Found
        Uint32 tSubroutineAddress = tNdbCall->theSubroutine - (tSubroutineCount - 16);
        tAddress = tNdbSubroutine->theSubroutineAddress[tSubroutineAddress];
        break;
      }//if
      tNdbSubroutine = tNdbSubroutine->theNext;
    }//while
    if (tAddress == -1) {
      setErrorCodeAbort(4222);
      return -1;
    }//if
// Now it is time to update the signal data with the relative branch jump.
    NdbApiSignal* tSignal = tNdbCall->theSignal;
    Uint32 tReadData = tSignal->readData(tNdbCall->theSignalAddress);
    tSignal->setData(((tAddress << 16) + (tReadData & 0xffff)), 
                     tNdbCall->theSignalAddress);

    theFirstCall = theFirstCall->theNext;
    theNdb->releaseNdbCall(tNdbCall);
  }//while
  
  Uint32 tInitialReadSize = theInitialReadSize;
  Uint32 tInterpretedSize = theInterpretedSize;
  Uint32 tFinalUpdateSize = theFinalUpdateSize;
  Uint32 tFinalReadSize   = theFinalReadSize;
  Uint32 tSubroutineSize  = theSubroutineSize;
  if (theOperationType != OpenScanRequest &&
      theOperationType != OpenRangeScanRequest) {
    TcKeyReq * const tcKeyReq = CAST_PTR(TcKeyReq, theTCREQ->getDataPtrSend());

    tcKeyReq->attrInfo[0] = tInitialReadSize;
    tcKeyReq->attrInfo[1] = tInterpretedSize;
    tcKeyReq->attrInfo[2] = tFinalUpdateSize;
    tcKeyReq->attrInfo[3] = tFinalReadSize;
    tcKeyReq->attrInfo[4] = tSubroutineSize;
  } else {
    // If a scan is defined we use the first ATTRINFO instead of TCKEYREQ.
    theFirstATTRINFO->setData(tInitialReadSize, 4);
    theFirstATTRINFO->setData(tInterpretedSize, 5);
    theFirstATTRINFO->setData(tFinalUpdateSize, 6);
    theFirstATTRINFO->setData(tFinalReadSize, 7);
    theFirstATTRINFO->setData(tSubroutineSize, 8);  
  }//if
  theReceiver.prepareSend();
  return 0;
}//NdbOperation::prepareSendInterpreted()


/*
 Prepares TCKEYREQ and (if needed) KEYINFO and ATTRINFO signals for
 operations using the NdbRecord API
 Executed when the operation is defined for both PK, Unique index
 and scan takeover operations.
 @returns 0 for success
*/
int
NdbOperation::buildSignalsNdbRecord(Uint32 aTC_ConnectPtr, 
                                    Uint64 aTransId)
{
  char buf[NdbRecord::Attr::SHRINK_VARCHAR_BUFFSIZE];
  int res;
  Uint32 no_disk_flag;
  Uint32 *attrinfo_section_sizes_ptr= NULL;

  assert(theStatus==UseNdbRecord);
  /* Interpreted operations not supported with NdbRecord
   * use NdbInterpretedCode instead
   */
  assert(!theInterpretIndicator);

  const NdbRecord *key_rec= m_key_record;
  const char *key_row= m_key_row;
  const NdbRecord *attr_rec= m_attribute_record;
  const char *updRow;

  TcKeyReq *tcKeyReq= CAST_PTR(TcKeyReq, theTCREQ->getDataPtrSend());
  Uint32 hdrSize= fillTcKeyReqHdr(tcKeyReq, aTC_ConnectPtr, aTransId);
  /* No KeyInfo goes in the TCKEYREQ signal - it all goes into 
   * a separate KeyInfo section
   */
  assert(theTCREQ->next() == NULL);
  theKEYINFOptr= NULL;
  keyInfoRemain= 0;

  /* Fill in keyinfo */
  if (!key_rec)
  {
    /* This means that key_row contains the KEYINFO20 data. */
    /* i.e. lock takeover */
    tcKeyReq->tableId= attr_rec->tableId;
    tcKeyReq->tableSchemaVersion= attr_rec->tableVersion;
    res= insertKEYINFO_NdbRecord(key_row, m_keyinfo_length*4);
    if (res)
      return res;
  }
  else
  {
    tcKeyReq->tableId= key_rec->tableId;
    tcKeyReq->tableSchemaVersion= key_rec->tableVersion;
    theTotalNrOfKeyWordInSignal= 0;
    for (Uint32 i= 0; i<key_rec->key_index_length; i++)
    {
      const NdbRecord::Attr *col;

      col= &key_rec->columns[key_rec->key_indexes[i]];

      /*
        A unique index can index a nullable column (the primary key index
        cannot). So we can get NULL here (but it is an error if we do).
      */
      if (col->is_null(key_row))
      {
        setErrorCodeAbort(4316);
        return -1;
      }

      Uint32 length=0;

      bool len_ok;
      const char *src;
      if (col->flags & NdbRecord::IsMysqldShrinkVarchar)
      {
        /* Used to support special varchar format for mysqld keys. */
        len_ok= col->shrink_varchar(key_row, length, buf);
        src= buf;
      }
      else
      {
        len_ok= col->get_var_length(key_row, length);
        src= &key_row[col->offset];
      }

      if (!len_ok)
      {
        /* Hm, corrupt varchar length. */
        setErrorCodeAbort(4209);
        return -1;
      }
      res= insertKEYINFO_NdbRecord(src, length);
      if (res)
        return res;
    }
  }

  /* For long TCKEYREQ, we don't need to set the key length in the
   * header, as it is passed as the length of the KeyInfo section
   */

  /* Fill in attrinfo
   * If ATTRINFO includes interpreted code then the first 5 words are
   * length information for 5 sections.
   * If there is no interpreted code then there's only one section, and
   * no length information
   */
  /* All ATTRINFO goes into a separate ATTRINFO section - none is placed
   * into the TCKEYREQ signal
   */
  assert(theFirstATTRINFO == NULL);
  attrInfoRemain= 0;
  theATTRINFOptr= NULL;

  no_disk_flag= m_no_disk_flag;

  /* If we have an interpreted program then we add 5 words
   * of section length information at the start of the
   * ATTRINFO
   */
  const NdbInterpretedCode *code= m_interpreted_code;
  if (code)
  {
    if (code->m_flags & NdbInterpretedCode::UsesDisk)
      no_disk_flag = 0;

    /* Need to add section lengths info to the signal */
    Uint32 sizes[AttrInfo::SectionSizeInfoLength];
    sizes[0] = 0;               // Initial read.
    sizes[1] = 0;               // Interpreted program
    sizes[2] = 0;               // Final update size.
    sizes[3] = 0;               // Final read size
    sizes[4] = 0;               // Subroutine size

    res = insertATTRINFOData_NdbRecord((const char *)sizes,
                                       sizeof(sizes));
    if (res)
      return res;

    /* So that we can go back to set the actual sizes later... */
    attrinfo_section_sizes_ptr= (theATTRINFOptr - 
                                 AttrInfo::SectionSizeInfoLength);

  }

  OperationType tOpType= theOperationType;

  /* Initial read signal words */
  if (tOpType == ReadRequest || tOpType == ReadExclusive ||
      (tOpType == DeleteRequest && 
       m_attribute_row != NULL)) // Read as part of delete
  {
    Bitmask<MAXNROFATTRIBUTESINWORDS> readMask;
    Uint32 requestedCols= 0;
    Uint32 maxAttrId= 0;
    for (Uint32 i= 0; i<attr_rec->noOfColumns; i++)
    {
      const NdbRecord::Attr *col;

      col= &attr_rec->columns[i];
      Uint32 attrId= col->attrId;

      /* Pseudo columns not allowed for NdbRecord */
      assert(! (attrId & AttributeHeader::PSEUDO));

      if (!BitmaskImpl::get(MAXNROFATTRIBUTESINWORDS,
                            m_read_mask, attrId))
        continue;

      /* Blob head reads are defined as extra GetValues, 
       * processed below, not here.
       */
      if (unlikely(col->flags & NdbRecord::IsBlob))
        continue;

      if (col->flags & NdbRecord::IsDisk)
        no_disk_flag= 0;

      if (attrId > maxAttrId)
        maxAttrId= attrId;

      readMask.set(attrId);
      requestedCols++;
    }
    theReceiver.m_record.m_column_count= requestedCols;

    /* Are there any columns to read via NdbRecord? */
    if (requestedCols > 0)
    {
      bool all= (requestedCols == m_currentTable->m_columns.size());

      if (all)
      {
        res= insertATTRINFOHdr_NdbRecord(AttributeHeader::READ_ALL,
                                         requestedCols);
        if (res)
          return res;
      }
      else
      {
        /* How many bitmask words are significant? */
        Uint32 sigBitmaskWords= (maxAttrId>>5) + 1;
        
        res= insertATTRINFOHdr_NdbRecord(AttributeHeader::READ_PACKED,
                                         sigBitmaskWords << 2);
        if (res)
          return res;
        
        res= insertATTRINFOData_NdbRecord((const char*) &readMask.rep.data[0],
                                          sigBitmaskWords << 2);
        if (res)
          return res;
      }
    }

    /* Handle any additional getValue(). 
     * Note : This includes extra getValue()s to read Blob
     * header + inline data
     * Disk flag set when getValues were processed.
     */
    const NdbRecAttr *ra= theReceiver.theFirstRecAttr;
    while (ra)
    {
      res= insertATTRINFOHdr_NdbRecord(ra->attrId(), 0);
      if(res)
        return res;
      ra= ra->next();
    }
  }

  if (m_use_any_value && 
      (tOpType == DeleteRequest))
  {
    /* Special hack for delete and ANYVALUE pseudo-column
     * We want to be able set the ANYVALUE pseudo-column as
     * part of a delete, but deletes don't allow updates
     * So we perform a 'read' of the column, passing a value.
     * Code in TUP which handles this 'read' will set the
     * value when the read is processed.
     */
    res= insertATTRINFOHdr_NdbRecord(AttributeHeader::ANY_VALUE, 4);
    if(res)
      return res;
    res= insertATTRINFOData_NdbRecord((const char *)(&m_any_value), 4);
    if(res)
      return res;
  }

  /* Interpreted program main signal words */
  if (code)
  {
    /* Record length of Initial Read section */
    attrinfo_section_sizes_ptr[0]= theTotalCurrAI_Len - 
      AttrInfo::SectionSizeInfoLength;

    Uint32 mainProgramWords= code->m_first_sub_instruction_pos ?
      code->m_first_sub_instruction_pos :
      code->m_instructions_length;

    res = insertATTRINFOData_NdbRecord((const char *)code->m_buffer,
                                       mainProgramWords << 2);

    if (res)
      return res;
    
    // Record length of Interpreted program section */
    attrinfo_section_sizes_ptr[1]= mainProgramWords;
  }


  /* Final update signal words */
  if ((tOpType == InsertRequest) || 
      (tOpType == WriteRequest) ||
      (tOpType == UpdateRequest))
  {
    updRow= m_attribute_row;
    NdbBlob *currentBlob= theBlobList;

    for (Uint32 i= 0; i<attr_rec->noOfColumns; i++)
    {
      const NdbRecord::Attr *col;

      col= &attr_rec->columns[i];
      Uint32 attrId= col->attrId;

      /* Pseudo columns not allowed for NdbRecord */
      assert(! (attrId & AttributeHeader::PSEUDO));

      if (!BitmaskImpl::get((NDB_MAX_ATTRIBUTES_IN_TABLE+31)>>5,
                            m_read_mask, attrId))
        continue;

      if (col->flags & NdbRecord::IsDisk)
        no_disk_flag= 0;

      Uint32 length;
      const char *data;

      if (likely(!(col->flags & (NdbRecord::IsBlob|NdbRecord::IsMysqldBitfield))))
      {
        if (col->is_null(updRow))
          length= 0;
        else if (!col->get_var_length(updRow, length))
        {
          /* Hm, corrupt varchar length. */
          setErrorCodeAbort(4209);
          return -1;
        }
        data= &updRow[col->offset];
      }
      else
      {
        if (likely(col->flags & NdbRecord::IsMysqldBitfield))
        {
          /* Mysqld format bitfield. */
          if (col->is_null(updRow))
            length= 0;
          else
          {
            col->get_mysqld_bitfield(updRow, buf);
            data= buf;
            length= col->maxSize;
          }
        }
        else
        {
          NdbBlob *bh= currentBlob;
          currentBlob= currentBlob->theNext;

          /* 
           * Blob column
           * We cannot prepare signals to update the Blob yet, as the 
           * user has not had a chance to specify the data to write yet.
           *
           * Writes to the blob head, inline data and parts are handled
           * by separate operations, injected before and after this one 
           * as part of the blob handling code in NdbTransaction::execute().
           * However, for Insert and Write to non-nullable columns, we must 
           * write some BLOB data here in case the BLOB is non-nullable.  
           * For this purpose, we write data of zero length
           * For nullable columns, we write null data.  This is necessary
           * as it is valid for users to never call setValue() for nullable
           * blobs.
           */
          if (tOpType == UpdateRequest)
            continue; // Do nothing in this operation

          /* 
           * Blob call that sets up a data pointer for blob header data
           * for an 'empty' blob - length zero or null depending on
           * Blob's 'nullability'
           */
          bh->getNullOrEmptyBlobHeadDataPtr(data, length);
        }
      } // if Blob or Bitfield

      res= insertATTRINFOHdr_NdbRecord(attrId, length);
      if(res)
        return res;
      if (length > 0)
      {
        res= insertATTRINFOData_NdbRecord(data, length);
        if(res)
          return res;
      }
    } // for noOfColumns

    /* Now handle any extra setValues passed in */
    if (m_extraSetValues != NULL)
    {
      for (Uint32 i=0; i<m_numExtraSetValues; i++)
      {
        const NdbDictionary::Column *extraCol=m_extraSetValues[i].column;
        const void * pvalue=m_extraSetValues[i].value;

        if (extraCol->getStorageType( )== NDB_STORAGETYPE_DISK)
          no_disk_flag=0;

        Uint32 length;
        
        if (pvalue==NULL)
          length=0;
        else
        { 
          length=extraCol->getSizeInBytes();          
          if (extraCol->getArrayType() != NdbDictionary::Column::ArrayTypeFixed)
          {
            Uint32 lengthInfoBytes;
            if (!NdbSqlUtil::get_var_length((Uint32) extraCol->getType(),
                                            pvalue,
                                            length,
                                            lengthInfoBytes,
                                            length))
            {
              // Length parameter in equal/setValue is incorrect
              setErrorCodeAbort(4209);
              return -1;
            }
          }
        }       

        // Add ATTRINFO
        res= insertATTRINFOHdr_NdbRecord(extraCol->getAttrId(), length);
        if(res)
          return res;

        if(length>0)
        {
          res=insertATTRINFOData_NdbRecord((char*)pvalue, length);
          if(res)
            return res;
        }
      } // for numExtraSetValues
    }// if m_extraSetValues!=null

    /* Don't need these any more */
    m_extraSetValues = NULL;
    m_numExtraSetValues = 0;
  }

  if ((tOpType == InsertRequest) ||
      (tOpType == WriteRequest) ||
      (tOpType == UpdateRequest))
  {
    /* Handle setAnyValue() for all cases except delete */
    if (m_use_any_value)
    {
      res= insertATTRINFOHdr_NdbRecord(AttributeHeader::ANY_VALUE, 4);
      if(res)
        return res;
      res= insertATTRINFOData_NdbRecord((const char *)(&m_any_value), 4);
      if(res)
        return res;
    }
  }

  /* Final read signal words */
  // Not currently used in NdbRecord

  /* Subroutine section signal words */
  if (code)
  {
    /* Even with no subroutine section signal words, we need to set
     * the size of the update section
     */
    Uint32 updateWords= theTotalCurrAI_Len - (AttrInfo::SectionSizeInfoLength + 
                                              attrinfo_section_sizes_ptr[0] +
                                              attrinfo_section_sizes_ptr[1]);
    attrinfo_section_sizes_ptr[2]= updateWords;

    /* Do we have any subroutines ? */
    if (code->m_number_of_subs > 0)
    {
      assert(code->m_first_sub_instruction_pos);

      Uint32 *subroutineStart= 
        &code->m_buffer[ code->m_first_sub_instruction_pos ];
      Uint32 subroutineWords= 
        code->m_instructions_length -
        code->m_first_sub_instruction_pos;

      assert(subroutineWords > 0);

      res = insertATTRINFOData_NdbRecord((const char *)subroutineStart,
                                         subroutineWords << 2);

      if (res)
        return res;

      /* Update section length for subroutine section */
      attrinfo_section_sizes_ptr[4]= subroutineWords;
    }
  }

  /* Check if too much attrinfo have been defined. */
  if (theTotalCurrAI_Len > TcKeyReq::MaxTotalAttrInfo){
    setErrorCodeAbort(4257);
    return -1;
  }

  /* All KeyInfo and AttrInfo is in separate sections
   * Size information for Key and AttrInfo is taken from the section
   * lengths rather than from header information
   */
  theTCREQ->setLength(hdrSize);
  TcKeyReq::setNoDiskFlag(tcKeyReq->requestInfo, no_disk_flag);
  return 0;

}

/*
  Do final signal preparation before sending.
*/
int
NdbOperation::prepareSendNdbRecord(AbortOption ao)
{
  // There are a number of flags in the TCKEYREQ header that
  // we have to set at this point...they are not correctly 
  // defined before the call to execute().
  TcKeyReq *tcKeyReq=CAST_PTR(TcKeyReq, theTCREQ->getDataPtrSend());
  
  Uint8 abortOption= (ao == DefaultAbortOption) ?
    (Uint8) m_abortOption : (Uint8) ao;
  
  m_abortOption= theSimpleIndicator && theOperationType==ReadRequest ?
    (Uint8) AO_IgnoreError : (Uint8) abortOption;

  TcKeyReq::setAbortOption(tcKeyReq->requestInfo, m_abortOption);
  TcKeyReq::setCommitFlag(tcKeyReq->requestInfo, theCommitIndicator);
  TcKeyReq::setStartFlag(tcKeyReq->requestInfo, theStartIndicator);
  TcKeyReq::setSimpleFlag(tcKeyReq->requestInfo, theSimpleIndicator);
  TcKeyReq::setDirtyFlag(tcKeyReq->requestInfo, theDirtyIndicator);

  theStatus= WaitResponse;
  theReceiver.prepareSend();

  return 0;
}

/*
  Set up the header of the TCKEYREQ signal (except a few length fields,
  which are computed later in prepareSendNdbRecord()).
  Returns the length of the header, used to find the correct placement of
  keyinfo and attrinfo stored within TCKEYREQ.
*/
Uint32
NdbOperation::fillTcKeyReqHdr(TcKeyReq *tcKeyReq,
                              Uint32 connectPtr,
                              Uint64 transId)
{
  Uint32 hdrLen;
  UintR *hdrPtr;

  tcKeyReq->apiConnectPtr= connectPtr;
  tcKeyReq->apiOperationPtr= ptr2int();

  /* With long TCKEYREQ, we do not need to set the attrlength 
   * in the header since it is encoded as the AI section length
   */
  UintR attrLenAPIVer= 0;
  TcKeyReq::setAPIVersion(attrLenAPIVer, NDB_VERSION);
  tcKeyReq->attrLen= attrLenAPIVer;

  UintR reqInfo= 0;
  /* Dirty flag, Commit flag, Start flag, Simple flag set later
   * in prepareSendNdbRecord()
   */
  TcKeyReq::setInterpretedFlag(reqInfo, (m_interpreted_code != NULL));
  /* We will setNoDiskFlag() later when we have checked all columns. */
  TcKeyReq::setOperationType(reqInfo, theOperationType);
  // AbortOption set later in prepareSendNdbRecord()
  TcKeyReq::setDistributionKeyFlag(reqInfo, theDistrKeyIndicator_);
  TcKeyReq::setScanIndFlag(reqInfo, theScanInfo & 1);
  tcKeyReq->requestInfo= reqInfo;

  tcKeyReq->transId1= (Uint32)transId;
  tcKeyReq->transId2= (Uint32)(transId>>32);

  /*
    The next four words are optional, and included or not based on the flags
    passed earlier. At most two of them are possible here.
  */
  hdrLen= 8;
  hdrPtr= &(tcKeyReq->scanInfo);
  if (theScanInfo & 1)
  {
    *hdrPtr++= theScanInfo;
    hdrLen++;
  }
  if (theDistrKeyIndicator_)
  {
    *hdrPtr++= theDistributionKey;
    hdrLen++;
  }

  return hdrLen;
}

/*
  Link a new KEYINFO signal into the operation.
  This is used to store words for the KEYINFO section.
  // TODO : Unify with allocAttrInfo
  Return 0 on success, -1 on error.
*/
int
NdbOperation::allocKeyInfo()
{
  NdbApiSignal *tSignal;

  tSignal= theNdb->getSignal();
  if (tSignal == NULL)
  {
    setErrorCodeAbort(4000);
    return -1;
  }
  tSignal->next(NULL);
  if (theRequest->next() != NULL)
  {
    theLastKEYINFO->setLength(NdbApiSignal::MaxSignalWords);
    theLastKEYINFO->next(tSignal);
  }
  else
  {
    theRequest->next(tSignal);
  }
  theLastKEYINFO= tSignal;
  keyInfoRemain= NdbApiSignal::MaxSignalWords;
  theKEYINFOptr= tSignal->getDataPtrSend();

  return 0;
}

/*
  Link a new signal into the operation.
  This is used to store words for the ATTRINFO section.
  // TODO : Unify with allocKeyInfo
  Return 0 on success, -1 on error.
*/
int
NdbOperation::allocAttrInfo()
{
  NdbApiSignal *tSignal;

  tSignal= theNdb->getSignal();
  if (tSignal == NULL)
  {
    setErrorCodeAbort(4000);
    return -1;
  }
  tSignal->next(NULL);
  if (theFirstATTRINFO != NULL)
  {
    theCurrentATTRINFO->setLength(NdbApiSignal::MaxSignalWords);
    theCurrentATTRINFO->next(tSignal);
  }
  else
  {
    theFirstATTRINFO= tSignal;
  }
  theCurrentATTRINFO= tSignal;
  attrInfoRemain= NdbApiSignal::MaxSignalWords;
  theATTRINFOptr= tSignal->getDataPtrSend();

  return 0;
}

int
NdbOperation::insertKEYINFO_NdbRecord(const char *value,
                                      Uint32 byteSize)
{
  /* Words are added to a list of signal objects linked from 
   * theRequest->next()
   * The list of objects is then used to form the KeyInfo
   * section of the TCKEYREQ/TCINDXREQ/SCANTABREQ long signal
   * No separate KeyInfo signal train is sent.
   */
  theTupKeyLen+= (byteSize+3)/4;

  while (byteSize > keyInfoRemain*4)
  {
    /* Need to link in extra objects */
    if (keyInfoRemain)
    {
      /* Fill remaining words in this object */
      assert(theKEYINFOptr != NULL);
      memcpy(theKEYINFOptr, value, keyInfoRemain*4);
      value+= keyInfoRemain*4;
      byteSize-= keyInfoRemain*4;
    }

    /* Link new object in */
    int res= allocKeyInfo();
    if(res)
      return res;
  }

  assert(theRequest->next() != NULL);
  assert(theLastKEYINFO != NULL);

  /* Remaining words fit in this object */
  assert(theKEYINFOptr != NULL);
  memcpy(theKEYINFOptr, value, byteSize);
  if((byteSize%4) != 0)
    memset(((char *)theKEYINFOptr)+byteSize, 0, 4-(byteSize%4));
  Uint32 sizeInWords= (byteSize+3)/4;
  theKEYINFOptr+= sizeInWords;
  keyInfoRemain-= sizeInWords;

  theLastKEYINFO->setLength(NdbApiSignal::MaxSignalWords 
                            - keyInfoRemain);

  return 0;
}

int
NdbOperation::insertATTRINFOHdr_NdbRecord(Uint32 attrId,
                                          Uint32 attrLen)
{
  /* Words are added to a list of Signal objects pointed to
   * by theFirstATTRINFO
   * This list is then used to form the ATTRINFO
   * section of the TCKEYREQ/TCINDXREQ/SCANTABREQ long signal
   * No ATTRINFO signal train is sent.
   */

  theTotalCurrAI_Len++;

  if (! attrInfoRemain)
  {
    /* Need to link in an extra object to store this
     * word
     */
    int res= allocAttrInfo();
    if (res)
      return res;
  }

  /* Word fits in remaining space */
  Uint32 ah;
  AttributeHeader::init(&ah, attrId, attrLen);
  assert(theFirstATTRINFO != NULL);
  assert(theCurrentATTRINFO != NULL);
  assert(theATTRINFOptr != NULL);

  *(theATTRINFOptr++)= ah;
  attrInfoRemain--;

  theCurrentATTRINFO->setLength(NdbApiSignal::MaxSignalWords
                                - attrInfoRemain);

  return 0;
}

int
NdbOperation::insertATTRINFOData_NdbRecord(const char *value,
                                           Uint32 byteSize)
{
  /* Words are added to a list of Signal objects pointed to
   * by theFirstATTRINFO
   * This list is then used to form the ATTRINFO
   * section of the TCKEYREQ long signal
   * No ATTRINFO signal train is sent.
   */
  theTotalCurrAI_Len+= (byteSize+3)/4;

  while (byteSize > attrInfoRemain*4)
  {
    /* Need to link in extra objects */
    if (attrInfoRemain)
    {
      /* Fill remaining space in current object */
      memcpy(theATTRINFOptr, value, attrInfoRemain*4);
      value+= attrInfoRemain*4;
      byteSize-= attrInfoRemain*4;
    }

    int res= allocAttrInfo();
    if (res)
      return res;
  }

  /* Remaining words fit in current signal */
  assert(theFirstATTRINFO != NULL);
  assert(theCurrentATTRINFO != NULL);
  assert(theATTRINFOptr != NULL);

  memcpy(theATTRINFOptr, value, byteSize);
  if((byteSize%4) != 0)
    memset(((char *)theATTRINFOptr)+byteSize, 0, 4-(byteSize%4));
  Uint32 sizeInWords= (byteSize+3)/4;
  theATTRINFOptr+= sizeInWords;
  attrInfoRemain-= sizeInWords;

  theCurrentATTRINFO->setLength(NdbApiSignal::MaxSignalWords 
                                - attrInfoRemain);

  return 0;
}

int
NdbOperation::checkState_TransId(NdbApiSignal* aSignal)
{
  Uint64 tRecTransId, tCurrTransId;
  Uint32 tTmp1, tTmp2;

  if (theStatus != WaitResponse) {
#ifdef NDB_NO_DROPPED_SIGNAL
    abort();
#endif
    return -1;
  }//if

  tTmp1 = aSignal->readData(2);
  tTmp2 = aSignal->readData(3);

  tRecTransId = (Uint64)tTmp1 + ((Uint64)tTmp2 << 32);
  tCurrTransId = theNdbCon->getTransactionId();
  if (tCurrTransId != tRecTransId) {
#ifdef NDB_NO_DROPPED_SIGNAL
    abort();
#endif
    return -1;
  }//if
  return 0;
}//NdbOperation::checkState_TransId()

/***************************************************************************
int receiveTCKEYREF( NdbApiSignal* aSignal)

Return Value:   Return 0 : send was succesful.
                Return -1: In all other case.   
Parameters:     aSignal: the signal object that contains the TCKEYREF signal from TC.
Remark:         Handles the reception of the TCKEYREF signal.
***************************************************************************/
int
NdbOperation::receiveTCKEYREF( NdbApiSignal* aSignal)
{
  if (checkState_TransId(aSignal) == -1) {
    return -1;
  }//if

  setErrorCode(aSignal->readData(4));
  if (aSignal->getLength() == TcKeyRef::SignalLength)
  {
    // Signal may contain additional error data
    theError.details = (char *) aSignal->readData(5);
  }

  theStatus = Finished;
  theReceiver.m_received_result_length = ~0;

  // not dirty read
  if(! (theOperationType == ReadRequest && theDirtyIndicator))
  {
    theNdbCon->OpCompleteFailure(this);
    return -1;
  }
  
  /**
   * If TCKEYCONF has arrived
   *   op has completed (maybe trans has completed)
   */
  if(theReceiver.m_expected_result_length)
  {
    return theNdbCon->OpCompleteFailure(this);
  }
  
  return -1;
}
