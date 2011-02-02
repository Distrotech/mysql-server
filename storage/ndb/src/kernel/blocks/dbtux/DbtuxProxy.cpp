/* Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.

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

#include "DbtuxProxy.hpp"
#include "Dbtux.hpp"

DbtuxProxy::DbtuxProxy(Block_context& ctx) :
  LocalProxy(DBTUX, ctx)
{
  // GSN_ALTER_INDX_IMPL_REQ
  addRecSignal(GSN_ALTER_INDX_IMPL_REQ, &DbtuxProxy::execALTER_INDX_IMPL_REQ);
  addRecSignal(GSN_ALTER_INDX_IMPL_CONF, &DbtuxProxy::execALTER_INDX_IMPL_CONF);
  addRecSignal(GSN_ALTER_INDX_IMPL_REF, &DbtuxProxy::execALTER_INDX_IMPL_REF);
}

DbtuxProxy::~DbtuxProxy()
{
}

SimulatedBlock*
DbtuxProxy::newWorker(Uint32 instanceNo)
{
  return new Dbtux(m_ctx, instanceNo);
}

// GSN_ALTER_INDX_IMPL_REQ

void
DbtuxProxy::execALTER_INDX_IMPL_REQ(Signal* signal)
{
  const AlterIndxImplReq* req = (const AlterIndxImplReq*)signal->getDataPtr();
  Ss_ALTER_INDX_IMPL_REQ& ss = ssSeize<Ss_ALTER_INDX_IMPL_REQ>();
  ss.m_req = *req;
  ndbrequire(signal->getLength() == AlterIndxImplReq::SignalLength);
  sendREQ(signal, ss);
}

void
DbtuxProxy::sendALTER_INDX_IMPL_REQ(Signal* signal, Uint32 ssId,
                                    SectionHandle * handle)
{
  Ss_ALTER_INDX_IMPL_REQ& ss = ssFind<Ss_ALTER_INDX_IMPL_REQ>(ssId);

  AlterIndxImplReq* req = (AlterIndxImplReq*)signal->getDataPtrSend();
  *req = ss.m_req;
  req->senderRef = reference();
  req->senderData = ssId;
  sendSignalNoRelease(workerRef(ss.m_worker), GSN_ALTER_INDX_IMPL_REQ,
                      signal, AlterIndxImplReq::SignalLength, JBB, handle);
}

void
DbtuxProxy::execALTER_INDX_IMPL_CONF(Signal* signal)
{
  const AlterIndxImplConf* conf = (const AlterIndxImplConf*)signal->getDataPtr();
  Uint32 ssId = conf->senderData;
  Ss_ALTER_INDX_IMPL_REQ& ss = ssFind<Ss_ALTER_INDX_IMPL_REQ>(ssId);
  recvCONF(signal, ss);
}

void
DbtuxProxy::execALTER_INDX_IMPL_REF(Signal* signal)
{
  const AlterIndxImplRef* ref = (const AlterIndxImplRef*)signal->getDataPtr();
  Uint32 ssId = ref->senderData;
  Ss_ALTER_INDX_IMPL_REQ& ss = ssFind<Ss_ALTER_INDX_IMPL_REQ>(ssId);
  recvREF(signal, ss, ref->errorCode);
}

void
DbtuxProxy::sendALTER_INDX_IMPL_CONF(Signal* signal, Uint32 ssId)
{
  Ss_ALTER_INDX_IMPL_REQ& ss = ssFind<Ss_ALTER_INDX_IMPL_REQ>(ssId);
  BlockReference dictRef = ss.m_req.senderRef;

  if (!lastReply(ss))
    return;

  if (ss.m_error == 0) {
    jam();
    AlterIndxImplConf* conf = (AlterIndxImplConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->senderData = ss.m_req.senderData;
    sendSignal(dictRef, GSN_ALTER_INDX_IMPL_CONF,
               signal, AlterIndxImplConf::SignalLength, JBB);
  } else {
    AlterIndxImplRef* ref = (AlterIndxImplRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->senderData = ss.m_req.senderData;
    ref->errorCode = ss.m_error;
    sendSignal(dictRef, GSN_ALTER_INDX_IMPL_REF,
               signal, AlterIndxImplRef::SignalLength, JBB);
  }

  ssRelease<Ss_ALTER_INDX_IMPL_REQ>(ssId);
}

BLOCK_FUNCTIONS(DbtuxProxy)
