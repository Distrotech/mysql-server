/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

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
#include "NdbApiSignal.hpp"
#include <Ndb.hpp>
#include <NdbRecAttr.hpp>
#include "NdbUtil.hpp"

#include "Interpreter.hpp"
#include <AttributeHeader.hpp>
#include <signaldata/TcKeyReq.hpp>
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
  theNdbCon->OpSent();
  return tSignalCount;
}//NdbOperation::doSend()

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
NdbOperation::prepareSend(Uint32 aTC_ConnectPtr, Uint64 aTransId)
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

  Uint32 tTableId = m_currentTable->m_id;
  Uint32 tSchemaVersion = m_currentTable->m_version;
  
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

//-------------------------------------------------------------
// Simple state is set if start and commit is set and it is
// a read request. Otherwise it is set to zero.
//-------------------------------------------------------------
  Uint8 tReadInd = (theOperationType == ReadRequest);
  Uint8 tSimpleState = tReadInd & tSimpleIndicator;

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
  Uint8 abortOption =
    m_abortOption != -1 ? m_abortOption : theNdbCon->m_abortOption;

  tcKeyReq->setDirtyFlag(tReqInfo, tDirtyIndicator);
  tcKeyReq->setOperationType(tReqInfo, tOperationType);
  tcKeyReq->setKeyLength(tReqInfo, tTupKeyLen);
  
  // A simple read is always ignore error
  abortOption = tSimpleIndicator ? (Uint8) AO_IgnoreError : abortOption;
  tcKeyReq->setAbortOption(tReqInfo, abortOption);
  
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
  if (theStatus == ExecInterpretedValue) {
    if (insertATTRINFO(Interpreter::EXIT_OK) != -1) {
//-------------------------------------------------------------------------
// Since we read the total length before inserting the last entry in the
// signals we need to add one to the total length.
//-------------------------------------------------------------------------

      theInterpretedSize = (tTotalCurrAI_Len + 1) -
       (tInitReadSize + 5);

    } else {
      return -1;
    }//if
  } else if (theStatus == FinalGetValue) {

    theFinalReadSize = tTotalCurrAI_Len -
      (tInitReadSize + theInterpretedSize + theFinalUpdateSize + 5);

  } else if (theStatus == SetValueInterpreted) {

    theFinalUpdateSize = tTotalCurrAI_Len -
       (tInitReadSize + theInterpretedSize + 5);

  } else if (theStatus == SubroutineEnd) {

    theSubroutineSize = tTotalCurrAI_Len -
      (tInitReadSize + theInterpretedSize + 
         theFinalUpdateSize + theFinalReadSize + 5);

  } else if (theStatus == GetValue) {
    theInitialReadSize = tTotalCurrAI_Len - 5;
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
    tSignal->setData(((tAddress << 16) + tReadData), tNdbCall->theSignalAddress);
      
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
  Prepares TCKEYREQ and (if needed) KEYINFO and ATTRINFO signals, for
  operations using NdbRecord.
*/
int
NdbOperation::prepareSendNdbRecord(Uint32 aTC_ConnectPtr, Uint64 aTransId)
{
  Uint32 *keyInfoPtr, *attrInfoPtr;
  Uint32 remain;
  int res;

  assert(theStatus==UseNdbRecord);
  /* Not yet support for NdbRecord with interpreted operations. */
  assert(!theInterpretIndicator);

  const NdbRecord *key_rec= thePKRec;
  const char *key_row= thePKRow;
  const NdbRecord *result_rec, *upd_rec;
  const char *updRow;
  const Uint32 *result_mask= theReadMask;

  TcKeyReq *tcKeyReq= CAST_PTR(TcKeyReq, theTCREQ->getDataPtrSend());
  Uint32 hdrSize= fillTcKeyReqHdr(tcKeyReq, aTC_ConnectPtr, aTransId, key_rec);
  keyInfoPtr= theTCREQ->getDataPtrSend() + hdrSize;

  // Fill in keyinfo (in TCKEYREQ signal, spilling into KEYINFO signals)
  remain= TcKeyReq::MaxKeyInfo;
  theTotalNrOfKeyWordInSignal= 0;
  for (Uint32 i= 0; i<key_rec->noOfColumns; i++)
  {
    const NdbRecord::Attr *col;

    col= &key_rec->columns[i];
    /* 
       This is a special case for insert, which allows extra columns in the key
       NdbRecord, since it uses only a single record.
    */
    if(!(col->flags&NdbRecord::IsPK))
      continue;

    switch (col->type)
    {
      case NdbRecord::AttrNotNULL:
        res= insertKEYINFO_NdbRecord(aTC_ConnectPtr, aTransId,
                                     &key_row[col->offset],
                                     col->maxSize, &keyInfoPtr, &remain);
        if(res)
          return res;
        break;

      default:
        assert(false);
    }
  }

  /* 
     Now the total keyinfo size has been computed, inside
     insertKEYINFO_NdbRecord().
  */
  TcKeyReq::setKeyLength(tcKeyReq->requestInfo, theTupKeyLen);

  // Fill in attrinfo (in TCKEYREQ signal, spilling into ATTRINFO signals)
  remain= TcKeyReq::MaxAttrInfo;
  attrInfoPtr= theTCREQ->getDataPtrSend() + hdrSize +
    (theTupKeyLen > TcKeyReq::MaxKeyInfo ? TcKeyReq::MaxKeyInfo : theTupKeyLen);

  OperationType tOpType= theOperationType;
  if ((tOpType == InsertRequest) || (tOpType == WriteRequest) ||
      (tOpType == UpdateRequest))
  {
    upd_rec= theUpdRec;
    updRow= theUpdRow;
    for (Uint32 i= 0; i<upd_rec->noOfColumns; i++)
    {
      const NdbRecord::Attr *col;

      col= &upd_rec->columns[i];
      Uint32 attrId= col->attrId;

      if (result_mask)
      {
        if (!(result_mask[attrId>>5] & (1<<(attrId&31))))
          continue;
      }

      switch (col->type)
      {
        case NdbRecord::AttrNotNULL:
          res= insertATTRINFOHdr_NdbRecord(aTC_ConnectPtr, aTransId,
                                           attrId, col->maxSize,
                                           &attrInfoPtr, &remain);
          if(res)
            return res;
          res= insertATTRINFOData_NdbRecord(aTC_ConnectPtr, aTransId,
                                            &updRow[col->offset], col->maxSize,
                                            &attrInfoPtr, &remain);
          if(res)
            return res;
          break;

        default:
          assert(false);
      }
    }
  }
  else if (tOpType == ReadRequest)
  {
    result_rec= theReceiver.theNdbRecord;
    for (Uint32 i= 0; i<result_rec->noOfColumns; i++)
    {
      const NdbRecord::Attr *col;

      col= &result_rec->columns[i];
      Uint32 attrId= col->attrId;

      if (result_mask)
      {
        if (!(result_mask[attrId>>5] & (1<<(attrId&31))))
          continue;
      }

      res= insertATTRINFOHdr_NdbRecord(aTC_ConnectPtr, aTransId,
                                       attrId, 0,
                                       &attrInfoPtr, &remain);
      if(res)
        return res;
    }
  }
  Uint32 signalLength= hdrSize +
    (theTupKeyLen > TcKeyReq::MaxKeyInfo ?
         TcKeyReq::MaxKeyInfo : theTupKeyLen) +
    (theTotalCurrAI_Len > TcKeyReq::MaxAttrInfo ?
         TcKeyReq::MaxAttrInfo : theTotalCurrAI_Len);
  theTCREQ->setLength(signalLength);


  /* Check if too much attrinfo have been defined. */
  if (theTotalCurrAI_Len > TcKeyReq::MaxTotalAttrInfo){
    setErrorCodeAbort(4257);
    return -1;
  }
  TcKeyReq::setAttrinfoLen(tcKeyReq->attrLen, theTotalCurrAI_Len);
  TcKeyReq::setAIInTcKeyReq(tcKeyReq->requestInfo, 
                            theTotalCurrAI_Len < TcKeyReq::MaxAttrInfo ?
                                theTotalCurrAI_Len : TcKeyReq::MaxAttrInfo);

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
                              Uint64 transId,
                              const NdbRecord *rec)
{
  Uint32 hdrLen;
  UintR *hdrPtr;

  tcKeyReq->apiConnectPtr= connectPtr;
  tcKeyReq->apiOperationPtr= ptr2int();

  UintR attrLen= 0;
  TcKeyReq::setAPIVersion(attrLen, NDB_VERSION);
  /* We will setAttrinfoLen() later when AttrInfo has been written. */
  tcKeyReq->attrLen= attrLen;

  tcKeyReq->tableId= rec->tableId;

  UintR reqInfo= 0;
  TcKeyReq::setSimpleFlag(reqInfo, theSimpleIndicator);
  TcKeyReq::setCommitFlag(reqInfo, theCommitIndicator);
  TcKeyReq::setStartFlag(reqInfo, theStartIndicator);
  TcKeyReq::setInterpretedFlag(reqInfo, theInterpretIndicator);
  TcKeyReq::setNoDiskFlag(reqInfo, m_no_disk_flag);
  TcKeyReq::setDirtyFlag(reqInfo, theDirtyIndicator);
  TcKeyReq::setOperationType(reqInfo, theOperationType);
  Uint8 abortOption=
    m_abortOption != -1 ? m_abortOption : theNdbCon->m_abortOption;
  TcKeyReq::setAbortOption
    (reqInfo, theSimpleIndicator ? (Uint8)AO_IgnoreError : abortOption);
  TcKeyReq::setDistributionKeyFlag(reqInfo, theDistrKeyIndicator_);
  TcKeyReq::setScanIndFlag(reqInfo, theScanInfo & 1);
  /* We will setAIInTcKeyReq() and setKeyLength() later. */
  tcKeyReq->requestInfo= reqInfo;

  tcKeyReq->tableSchemaVersion= rec->tableVersion;
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
  Return 0 on success, -1 on error.
*/
int
NdbOperation::allocKeyInfo(Uint32 connectPtr, Uint64 transId,
                           Uint32 **dstPtr, Uint32 *remain)
{
  NdbApiSignal *tSignal;
  KeyInfo *keyInfo;

  tSignal= theNdb->getSignal();
  if (tSignal == NULL)
  {
    setErrorCodeAbort(4000);
    return -1;
  }
  keyInfo= (struct KeyInfo *)(tSignal->getDataPtrSend());
  if (tSignal->setSignal(m_keyInfoGSN) == -1)
  {
    setErrorCodeAbort(4001);
    return -1;
  }
  tSignal->next(NULL);
  keyInfo->connectPtr= connectPtr;
  keyInfo->transId[0]= (Uint32)transId;
  keyInfo->transId[1]= (Uint32)(transId>>32);
  if (theTCREQ->next() != NULL)
  {
    theLastKEYINFO->setLength(KeyInfo::MaxSignalLength);
    theLastKEYINFO->next(tSignal);
  }
  else
  {
    theTCREQ->next(tSignal);
  }
  theLastKEYINFO= tSignal;
  *remain= KeyInfo::DataLength;
  *dstPtr= &(keyInfo->keyData[0]);
  return 0;
}

/*
  Link a new ATTRINFO signal into the operation.
  Return 0 on success, -1 on error.
*/
int
NdbOperation::allocAttrInfo(Uint32 connectPtr, Uint64 transId,
                            Uint32 **dstPtr, Uint32 *remain)
{
  NdbApiSignal *tSignal;
  AttrInfo *attrInfo;

  tSignal= theNdb->getSignal();
  if (tSignal == NULL)
  {
    setErrorCodeAbort(4000);
    return -1;
  }
  attrInfo= (struct AttrInfo *)(tSignal->getDataPtrSend());
  if (tSignal->setSignal(m_attrInfoGSN) == -1)
  {
    setErrorCodeAbort(4001);
    return -1;
  }
  tSignal->next(NULL);
  attrInfo->connectPtr= connectPtr;
  attrInfo->transId[0]= (Uint32)transId;
  attrInfo->transId[1]= (Uint32)(transId>>32);
  if (theFirstATTRINFO != NULL)
  {
    theCurrentATTRINFO->setLength(AttrInfo::MaxSignalLength);
    theCurrentATTRINFO->next(tSignal);
  }
  else
  {
    theFirstATTRINFO= tSignal;
  }
  theCurrentATTRINFO= tSignal;
  *remain= AttrInfo::DataLength;
  *dstPtr= &(attrInfo->attrData[0]);

  return 0;
}

int
NdbOperation::insertKEYINFO_NdbRecord(Uint32 connectPtr,
                                      Uint64 transId,
                                      const char *value,
                                      Uint32 size,
                                      Uint32 **dstPtr,
                                      Uint32 *remain)
{
  theTupKeyLen+= (size+3)/4;

  while (size > *remain*4)
  {
    if (*remain)
    {
      memcpy(*dstPtr, value, *remain*4);
      value+= *remain*4;
      size-= *remain*4;
    }
    int res= allocKeyInfo(connectPtr, transId, dstPtr, remain);
    if(res)
      return res;
  }

  memcpy(*dstPtr, value, size);
  if((size%4) != 0)
    memset(((char *)*dstPtr)+size, 0, 3-(size%4));
  Uint32 sizeInWords= (size+3)/4;
  *dstPtr+= sizeInWords;
  *remain-= sizeInWords;
  if (theTCREQ->next() != NULL)
    theLastKEYINFO->setLength(KeyInfo::MaxSignalLength - *remain);

  return 0;
}

int
NdbOperation::insertATTRINFOHdr_NdbRecord(Uint32 connectPtr,
                                          Uint64 transId,
                                          Uint32 attrId,
                                          Uint32 attrLen,
                                          Uint32 **dstPtr,
                                          Uint32 *remain)
{
  theTotalCurrAI_Len++;
  if (! *remain)
  {
    int res= allocAttrInfo(connectPtr, transId, dstPtr, remain);
    if (res)
      return res;
  }
  Uint32 ah;
  AttributeHeader::init(&ah, attrId, attrLen);
  *(*dstPtr)++= ah;
  (*remain)--;
  if (theFirstATTRINFO != NULL)
    theCurrentATTRINFO->setLength(AttrInfo::MaxSignalLength - *remain);

  return 0;
}

int
NdbOperation::insertATTRINFOData_NdbRecord(Uint32 connectPtr,
                                           Uint64 transId,
                                           const char *value,
                                           Uint32 size,
                                           Uint32 **dstPtr,
                                           Uint32 *remain)
{
  theTotalCurrAI_Len+= (size+3)/4;

  while (size > *remain*4)
  {
    if (*remain)
    {
      memcpy(*dstPtr, value, *remain*4);
      value+= *remain*4;
      size-= *remain*4;
    }
    int res= allocAttrInfo(connectPtr, transId, dstPtr, remain);
    if (res)
      return res;
  }

  memcpy(*dstPtr, value, size);
  if((size%4) != 0)
    memset(((char *)*dstPtr)+size, 0, 3-(size%4));
  Uint32 sizeInWords= (size+3)/4;
  *dstPtr+= sizeInWords;
  *remain-= sizeInWords;
  if (theFirstATTRINFO != NULL)
    theCurrentATTRINFO->setLength(AttrInfo::MaxSignalLength - *remain);

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

  AbortOption ao = (AbortOption)
    (m_abortOption != -1 ? m_abortOption : theNdbCon->m_abortOption);
  theReceiver.m_received_result_length = ~0;

  theStatus = Finished;
  // blobs want this
  if (m_abortOption != AO_IgnoreError)
  {
    theNdbCon->theReturnStatus = NdbTransaction::ReturnFailure;
  }
  theError.code = aSignal->readData(4);
  theNdbCon->setOperationErrorCodeAbort(aSignal->readData(4), ao);

  if(theOperationType != ReadRequest || !theSimpleIndicator) // not simple read
    return theNdbCon->OpCompleteFailure(ao, m_abortOption != AO_IgnoreError);
  
  /**
   * If TCKEYCONF has arrived
   *   op has completed (maybe trans has completed)
   */
  if(theReceiver.m_expected_result_length)
  {
    return theNdbCon->OpCompleteFailure(AbortOnError);
  }
  
  return -1;
}


void
NdbOperation::handleFailedAI_ElemLen()
{
  NdbRecAttr* tRecAttr = theReceiver.theFirstRecAttr;
  while (tRecAttr != NULL) {
    tRecAttr->setNULL();
    tRecAttr = tRecAttr->next();
  }//while
}//NdbOperation::handleFailedAI_ElemLen()




