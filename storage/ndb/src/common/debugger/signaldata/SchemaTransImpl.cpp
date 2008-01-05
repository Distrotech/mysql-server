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

#include <signaldata/SchemaTransImpl.hpp>
#include <signaldata/DictSignal.hpp>
#include <signaldata/SignalData.hpp>
#include <SignalLoggerManager.hpp>
#include <DebuggerNames.hpp>

bool
printSCHEMA_TRANS_IMPL_REQ(FILE* output, const Uint32* theData, Uint32 len, Uint16 rbn)
{
  const SchemaTransImplReq* sig = (const SchemaTransImplReq*)theData;
  const Uint32 phaseInfo = sig->phaseInfo;
  Uint32 mode = SchemaTransImplReq::getMode(phaseInfo);
  Uint32 phase = SchemaTransImplReq::getPhase(phaseInfo);
  Uint32 gsn = SchemaTransImplReq::getGsn(phaseInfo);
  const Uint32 requestInfo = sig->requestInfo;
  Uint32 opExtra = DictSignal::getRequestExtra(requestInfo);
  const Uint32 operationInfo = sig->operationInfo;
  Uint32 opIndex = SchemaTransImplReq::getOpIndex(operationInfo);
  Uint32 opDepth = SchemaTransImplReq::getOpDepth(operationInfo);
  const Uint32 iteratorInfo = sig->iteratorInfo;
  Uint32 listId = SchemaTransImplReq::getListId(iteratorInfo);
  Uint32 listIndex = SchemaTransImplReq::getListIndex(iteratorInfo);
  Uint32 itRepeat = SchemaTransImplReq::getItRepeat(iteratorInfo);
  fprintf(output, " senderRef: 0x%x", sig->senderRef);
  fprintf(output, " transKey: %u", sig->transKey);
  fprintf(output, " opKey: %u", sig->opKey);
  fprintf(output, "\n");
  fprintf(output, " mode: %u [%s] phase: %u [%s]",
          mode, DictSignal::getTransModeName(mode),
          phase, DictSignal::getTransPhaseName(phase));
  fprintf(output, "\n");
  fprintf(output, " requestInfo: 0x%x", requestInfo);
  fprintf(output, " opExtra: %u", opExtra);
  fprintf(output, " requestFlags: [%s]",
          DictSignal::getRequestFlagsText(requestInfo));
  fprintf(output, "\n");
  fprintf(output, " opIndex: %u", opIndex);
  fprintf(output, " opDepth: %u", opDepth);
  fprintf(output, "\n");
  fprintf(output, " listId: %u", listId);
  fprintf(output, " listIndex: %u", listIndex);
  fprintf(output, " itRepeat: %u", itRepeat);
  fprintf(output, "\n");
  fprintf(output, " clientRef: 0x%x", sig->clientRef);
  fprintf(output, " transId: 0x%x", sig->transId);
  fprintf(output, "\n");
  const Uint32 fixed_len = SchemaTransImplReq::SignalLength;
  if (len > fixed_len) {
    fprintf(output, "piggy-backed: %u %s\n", gsn, getSignalName(gsn));
    const Uint32* pb_data = &theData[fixed_len];
    const Uint32 pb_len = len - fixed_len;
    switch (gsn) {
      // internal operation signals
    case GSN_CREATE_TAB_REQ:
      printCREATE_TAB_REQ(output, pb_data, pb_len, rbn);
      break;
    case GSN_DROP_TAB_REQ:
      printDROP_TAB_REQ(output, pb_data, pb_len, rbn);
      break;
    case GSN_ALTER_TAB_REQ:
      printALTER_TAB_REQ(output, pb_data, pb_len, rbn);
      break;
    case GSN_CREATE_TRIG_IMPL_REQ:
      printCREATE_TRIG_IMPL_REQ(output, pb_data, pb_len, rbn);
      break;
    case GSN_DROP_TRIG_IMPL_REQ:
      printDROP_TRIG_IMPL_REQ(output, pb_data, pb_len, rbn);
      break;
    case GSN_ALTER_TRIG_IMPL_REQ:
      printALTER_TRIG_IMPL_REQ(output, pb_data, pb_len, rbn);
      break;
    case GSN_CREATE_INDX_IMPL_REQ:
      printCREATE_INDX_IMPL_REQ(output, pb_data, pb_len, rbn);
      break;
    case GSN_DROP_INDX_IMPL_REQ:
      printDROP_INDX_IMPL_REQ(output, pb_data, pb_len, rbn);
      break;
    case GSN_ALTER_INDX_IMPL_REQ:
      printALTER_INDX_IMPL_REQ(output, pb_data, pb_len, rbn);
      break;
    case GSN_BUILD_INDX_IMPL_REQ:
      printBUILD_INDX_IMPL_REQ(output, pb_data, pb_len, rbn);
      break;
    default:
    {
      Uint32 i;
      for (i = 0; i < len - fixed_len; i++) {
        if (i > 0 && i % 7 == 0)
          fprintf(output, "\n");
        fprintf(output, " H'%08x", theData[fixed_len + i]);
      }
      fprintf(output, "\n");
    }
    break;
    }
  }
  return true;
}

bool
printSCHEMA_TRANS_IMPL_CONF(FILE* output, const Uint32* theData, Uint32 len, Uint16 rbn)
{
  const SchemaTransImplConf* sig = (const SchemaTransImplConf*)theData;
  fprintf(output, " senderRef: 0x%x", sig->senderRef);
  fprintf(output, " transKey: %u", sig->transKey);
  fprintf(output, " itFlags: 0x%x", sig->itFlags);
  fprintf(output, "\n");
  return true;
}

bool
printSCHEMA_TRANS_IMPL_REF(FILE* output, const Uint32* theData, Uint32 len, Uint16 rbn)
{
  const SchemaTransImplRef* sig = (const SchemaTransImplRef*)theData;
  fprintf(output, " senderRef: 0x%x", sig->senderRef);
  fprintf(output, " transKey: %u", sig->transKey);
  fprintf(output, "\n");
  fprintf(output, " errorCode: %u", sig->errorCode);
  fprintf(output, " errorLine: %u", sig->errorLine);
  fprintf(output, "\n");
  return true;
}
