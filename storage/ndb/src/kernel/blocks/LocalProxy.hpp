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

#ifndef NDB_LOCAL_PROXY_HPP
#define NDB_LOCAL_PROXY_HPP

#include <pc.hpp>
#include <SimulatedBlock.hpp>
#include <Bitmask.hpp>
#include <DLFifoList.hpp>
#include <signaldata/ReadConfig.hpp>
#include <signaldata/NdbSttor.hpp>
#include <signaldata/ReadNodesConf.hpp>
#include <signaldata/NodeFailRep.hpp>
#include <signaldata/NFCompleteRep.hpp>
#include <signaldata/CreateTrigImpl.hpp>
#include <signaldata/DropTrigImpl.hpp>

/*
 * Proxy blocks for MT LQH.
 *
 * The LQH proxy is the LQH block seen by other nodes and blocks,
 * unless by-passed for efficiency.  Real LQH instances (workers)
 * run behind it.  The instance number is 1 + worker index.
 *
 * There are also proxies and workers for ACC, TUP, TUX, BACKUP,
 * RESTORE, and PGMAN.  Proxy classes are subclasses of LocalProxy.
 * Workers with same instance number (one from each class) run in
 * same thread.
 *
 * After LQH workers there is an optional extra worker.  It runs
 * in the thread of the main block (i.e. the proxy).  Its instance
 * number is fixed as 1 + MaxLqhWorkers (currently 5) i.e. it skips
 * over any unused LQH instance numbers.
 */

class LocalProxy : public SimulatedBlock {
public:
  LocalProxy(BlockNumber blockNumber, Block_context& ctx);
  virtual ~LocalProxy();
  BLOCK_DEFINES(LocalProxy);

protected:
  enum { MaxLqhWorkers = MAX_NDBMT_LQH_WORKERS };
  enum { MaxExtraWorkers = 1 };
  enum { MaxWorkers = MaxLqhWorkers + MaxExtraWorkers };
  typedef Bitmask<(MaxWorkers+31)/32> WorkerMask;
  Uint32 c_lqhWorkers;
  Uint32 c_extraWorkers;
  Uint32 c_workers;
  // no gaps - extra worker has index c_lqhWorkers (not MaxLqhWorkers)
  SimulatedBlock* c_worker[MaxWorkers];

  virtual SimulatedBlock* newWorker(Uint32 instanceNo) = 0;
  virtual void loadWorkers();

  // get worker block by index (not by instance)

  SimulatedBlock* workerBlock(Uint32 i) {
    ndbrequire(i < c_workers);
    ndbrequire(c_worker[i] != 0);
    return c_worker[i];
  }

  SimulatedBlock* extraWorkerBlock() {
    return workerBlock(c_lqhWorkers);
  }

  // get worker block reference by index (not by instance)

  BlockReference workerRef(Uint32 i) {
    return numberToRef(number(), workerInstance(i), getOwnNodeId());
  }

  BlockReference extraWorkerRef() {
    ndbrequire(c_workers == c_lqhWorkers + 1);
    Uint32 i = c_lqhWorkers;
    return workerRef(i);
  }

  // convert between worker index and worker instance

  Uint32 workerInstance(Uint32 i) {
    ndbrequire(i < c_workers);
    Uint32 ino;
    if (i < c_lqhWorkers)
      ino = 1 + i;
    else
      ino = 1 + MaxLqhWorkers;
    return ino;
  }

  Uint32 workerIndex(Uint32 ino) {
    ndbrequire(ino != 0);
    Uint32 i;
    if (ino != 1 + MaxLqhWorkers)
      i = ino - 1;
    else
      i = c_lqhWorkers;
    ndbrequire(i < c_workers);
    return i;
  }

  // support routines and classes ("Ss" = signal state)

  typedef void (LocalProxy::*SsFUNC)(Signal*, Uint32 ssId);

  struct SsCommon {
    Uint32 m_ssId;      // unique id in SsPool (below)
    SsFUNC m_sendREQ;   // from proxy to worker
    SsFUNC m_sendCONF;  // from proxy to caller
    Uint32 m_worker;    // current worker
    Uint32 m_error;
    static const char* name() { return "UNDEF"; }
    SsCommon() {
      m_ssId = 0;
      m_sendREQ = 0;
      m_sendCONF = 0;
      m_worker = 0;
      m_error = 0;
    }
  };

  // run workers sequentially
  struct SsSequential : SsCommon {
    SsSequential() {}
  };
  void sendREQ(Signal*, SsSequential& ss);
  void recvCONF(Signal*, SsSequential& ss);
  void recvREF(Signal*, SsSequential& ss, Uint32 error);
  // for use in sendREQ
  void skipReq(SsSequential& ss);
  void skipConf(SsSequential& ss);
  // for use in sendCONF
  bool firstReply(const SsSequential& ss);
  bool lastReply(const SsSequential& ss);

  // run workers in parallel
  struct SsParallel : SsCommon {
    WorkerMask m_workerMask;
    bool m_extraLast;   // run extra after LQH workers
    Uint32 m_extraSent;
    SsParallel() {
      m_extraLast = false;
      m_extraSent = 0;
    }
  };
  void sendREQ(Signal*, SsParallel& ss);
  void recvCONF(Signal*, SsParallel& ss);
  void recvREF(Signal*, SsParallel& ss, Uint32 error);
  // for use in sendREQ
  void skipReq(SsParallel& ss);
  void skipConf(SsParallel& ss);
  // for use in sendCONF
  bool firstReply(const SsParallel& ss);
  bool lastReply(const SsParallel& ss);
  bool lastExtra(Signal* signal, SsParallel& ss);
  // set all or given bits in worker mask
  void setMask(SsParallel& ss);
  void setMask(SsParallel& ss, const WorkerMask& mask);

  /*
   * Ss instances are seized from a pool.  Each pool is simply an array
   * of Ss instances.  Usually poolSize is 1.  Some signals need a few
   * more but the heavy stuff (query/DML) by-passes the proxy.
   *
   * Each Ss instance has a unique Uint32 ssId.  If there are multiple
   * instances then ssId must be computable from signal data.  One option
   * often is to use a generated ssId and set it as senderData,
   */

  template <class Ss>
  struct SsPool {
    Ss m_pool[Ss::poolSize];
    Uint32 m_usage;
    SsPool() {
      m_usage = 0;
    }
  };

  Uint32 c_ssIdSeq;

  // convenient for adding non-zero high bit
  enum { SsIdBase = (1u << 31) };

  template <class Ss>
  Ss* ssSearch(Uint32 ssId)
  {
    SsPool<Ss>& sp = Ss::pool(this);
    Ss* ssptr = 0;
    for (Uint32 i = 0; i < Ss::poolSize; i++) {
      if (sp.m_pool[i].m_ssId == ssId) {
        ssptr = &sp.m_pool[i];
        break;
      }
    }
    return ssptr;
  }

  template <class Ss>
  Ss& ssSeize() {
    const Uint32 base = SsIdBase;
    const Uint32 mask = SsIdBase - 1;
    Uint32 ssId = base | c_ssIdSeq;
    c_ssIdSeq = (c_ssIdSeq + 1) & mask;
    return ssSeize<Ss>(ssId);
  }

  template <class Ss>
  Ss& ssSeize(Uint32 ssId) {
    SsPool<Ss>& sp = Ss::pool(this);
    ndbrequire(sp.m_usage < Ss::poolSize);
    ndbrequire(ssId != 0);
    Ss* ssptr;
    // check for duplicate
    ssptr = ssSearch<Ss>(ssId);
    ndbrequire(ssptr == 0);
    // search for free
    ssptr = ssSearch<Ss>(0);
    ndbrequire(ssptr != 0);
    // set methods, clear bitmasks, etc
    new (ssptr) Ss;
    ssptr->m_ssId = ssId;
    sp.m_usage++;
    D("ssSeize" << V(sp.m_usage) << hex << V(ssId) << " " << Ss::name());
    return *ssptr;
  }

  template <class Ss>
  Ss& ssFind(Uint32 ssId) {
    SsPool<Ss>& sp = Ss::pool(this);
    ndbrequire(ssId != 0);
    Ss* ssptr = ssSearch<Ss>(ssId);
    ndbrequire(ssptr != 0);
    return *ssptr;
  }

  /*
   * In some cases it may not be known if this is first request.
   * This situation should be avoided by adding signal data or
   * by keeping state in the proxy instance.
   */
  template <class Ss>
  Ss& ssFindSeize(Uint32 ssId, bool* found) {
    SsPool<Ss>& sp = Ss::pool(this);
    ndbrequire(ssId != 0);
    Ss* ssptr = ssSearch<Ss>(ssId);
    if (ssptr != 0) {
      *found = true;
      return *ssptr;
    }
    *found = false;
    return ssSeize<Ss>(ssId);
  }

  template <class Ss>
  void ssRelease(Uint32 ssId) {
    SsPool<Ss>& sp = Ss::pool(this);
    ndbrequire(sp.m_usage != 0);
    ndbrequire(ssId != 0);
    D("ssRelease" << V(sp.m_usage) << hex << V(ssId) << " " << Ss::name());
    Ss* ssptr = ssSearch<Ss>(ssId);
    ndbrequire(ssptr != 0);
    ssptr->m_ssId = 0;
    ndbrequire(sp.m_usage > 0);
    sp.m_usage--;
  }

  template <class Ss>
  void ssRelease(Ss& ss) {
    ssRelease<Ss>(ss.m_ssId);
  }

  /*
   * In some cases handle pool full via delayed signal.
   * wl4391_todo maybe use CONTINUEB and guard against infinite loop.
   */
  template <class Ss>
  bool ssQueue(Signal* signal) {
    SsPool<Ss>& sp = Ss::pool(this);
    if (sp.m_usage < Ss::poolSize)
      return false;
    ndbrequire(signal->getNoOfSections() == 0);
    GlobalSignalNumber gsn = signal->header.theVerId_signalNumber & 0xFFFF;
    sendSignalWithDelay(reference(), gsn,
                        signal, 10, signal->length());
    return true;
  }

  // system info

  Uint32 c_typeOfStart;
  Uint32 c_masterNodeId;

  struct Node {
    Uint32 m_nodeId;
    bool m_alive;
    Node() {
      m_nodeId = 0;
      m_alive = false;
    }
    Uint32 nextList;
    union {
    Uint32 prevList;
    Uint32 nextPool;
    };
  };
  typedef Ptr<Node> NodePtr;
  ArrayPool<Node> c_nodePool;
  DLFifoList<Node> c_nodeList;

  // GSN_READ_CONFIG_REQ
  struct Ss_READ_CONFIG_REQ : SsSequential {
    ReadConfigReq m_req;
    Ss_READ_CONFIG_REQ() {
      m_sendREQ = &LocalProxy::sendREAD_CONFIG_REQ;
      m_sendCONF = &LocalProxy::sendREAD_CONFIG_CONF;
    }
    enum { poolSize = 1 };
    static SsPool<Ss_READ_CONFIG_REQ>& pool(LocalProxy* proxy) {
      return proxy->c_ss_READ_CONFIG_REQ;
    }
  };
  SsPool<Ss_READ_CONFIG_REQ> c_ss_READ_CONFIG_REQ;
  void execREAD_CONFIG_REQ(Signal*);
  void sendREAD_CONFIG_REQ(Signal*, Uint32 ssId);
  void execREAD_CONFIG_CONF(Signal*);
  void sendREAD_CONFIG_CONF(Signal*, Uint32 ssId);

  // GSN_STTOR
  struct Ss_STTOR : SsParallel {
    Uint32 m_reqlength;
    Uint32 m_reqdata[25];
    Uint32 m_conflength;
    Uint32 m_confdata[25];
    Ss_STTOR() {
      m_sendREQ = &LocalProxy::sendSTTOR;
      m_sendCONF = &LocalProxy::sendSTTORRY;
    }
    enum { poolSize = 1 };
    static SsPool<Ss_STTOR>& pool(LocalProxy* proxy) {
      return proxy->c_ss_STTOR;
    }
  };
  SsPool<Ss_STTOR> c_ss_STTOR;
  void execSTTOR(Signal*);
  virtual void callSTTOR(Signal*);
  void backSTTOR(Signal*);
  void sendSTTOR(Signal*, Uint32 ssId);
  void execSTTORRY(Signal*);
  void sendSTTORRY(Signal*, Uint32 ssId);

  // GSN_NDB_STTOR
  struct Ss_NDB_STTOR : SsParallel {
    NdbSttor m_req;
    enum { m_reqlength = sizeof(NdbSttor) >> 2 };
    Ss_NDB_STTOR() {
      m_sendREQ = &LocalProxy::sendNDB_STTOR;
      m_sendCONF = &LocalProxy::sendNDB_STTORRY;
    }
    enum { poolSize = 1 };
    static SsPool<Ss_NDB_STTOR>& pool(LocalProxy* proxy) {
      return proxy->c_ss_NDB_STTOR;
    }
  };
  SsPool<Ss_NDB_STTOR> c_ss_NDB_STTOR;
  void execNDB_STTOR(Signal*);
  virtual void callNDB_STTOR(Signal*);
  void backNDB_STTOR(Signal*);
  void sendNDB_STTOR(Signal*, Uint32 ssId);
  void execNDB_STTORRY(Signal*);
  void sendNDB_STTORRY(Signal*, Uint32 ssId);

  // GSN_READ_NODESREQ
  struct Ss_READ_NODES_REQ {
    GlobalSignalNumber m_gsn; // STTOR or NDB_STTOR
    Ss_READ_NODES_REQ() {
      m_gsn = 0;
    }
  };
  Ss_READ_NODES_REQ c_ss_READ_NODESREQ;
  void sendREAD_NODESREQ(Signal*);
  void execREAD_NODESCONF(Signal*);
  void execREAD_NODESREF(Signal*);

  // GSN_NODE_FAILREP
  struct Ss_NODE_FAILREP : SsParallel {
    NodeFailRep m_req;
    // REQ sends NdbNodeBitmask but CONF sends nodeId at a time
    NdbNodeBitmask m_waitFor[MaxWorkers];
    Ss_NODE_FAILREP() {
      m_sendREQ = &LocalProxy::sendNODE_FAILREP;
      m_sendCONF = &LocalProxy::sendNF_COMPLETEREP;
    }
    // some blocks do not reply
    static bool noReply(BlockNumber blockNo) {
      return
        blockNo == BACKUP;
    }
    enum { poolSize = 1 };
    static SsPool<Ss_NODE_FAILREP>& pool(LocalProxy* proxy) {
      return proxy->c_ss_NODE_FAILREP;
    }
  };
  SsPool<Ss_NODE_FAILREP> c_ss_NODE_FAILREP;
  void execNODE_FAILREP(Signal*);
  void sendNODE_FAILREP(Signal*, Uint32 ssId);
  void execNF_COMPLETEREP(Signal*);
  void sendNF_COMPLETEREP(Signal*, Uint32 ssId);

  // GSN_INCL_NODEREQ
  struct Ss_INCL_NODEREQ : SsParallel {
    // future-proof by allocating max length
    struct Req {
      Uint32 senderRef;
      Uint32 inclNodeId;
      Uint32 word[23];
    };
    struct Conf {
      Uint32 inclNodeId;
      Uint32 senderRef;
    };
    Uint32 m_reqlength;
    Req m_req;
    Ss_INCL_NODEREQ() {
      m_sendREQ = &LocalProxy::sendINCL_NODEREQ;
      m_sendCONF = &LocalProxy::sendINCL_NODECONF;
    }
    enum { poolSize = 1 };
    static SsPool<Ss_INCL_NODEREQ>& pool(LocalProxy* proxy) {
      return proxy->c_ss_INCL_NODEREQ;
    }
  };
  SsPool<Ss_INCL_NODEREQ> c_ss_INCL_NODEREQ;
  void execINCL_NODEREQ(Signal*);
  void sendINCL_NODEREQ(Signal*, Uint32 ssId);
  void execINCL_NODECONF(Signal*);
  void sendINCL_NODECONF(Signal*, Uint32 ssId);

  // GSN_DUMP_STATE_ORD
  struct Ss_DUMP_STATE_ORD : SsParallel {
    Uint32 m_reqlength;
    Uint32 m_reqdata[25];
    Ss_DUMP_STATE_ORD() {
      m_sendREQ = &LocalProxy::sendDUMP_STATE_ORD;
      m_sendCONF = 0;
    }
    enum { poolSize = 1 };
    static SsPool<Ss_DUMP_STATE_ORD>& pool(LocalProxy* proxy) {
      return proxy->c_ss_DUMP_STATE_ORD;
    }
  };
  SsPool<Ss_DUMP_STATE_ORD> c_ss_DUMP_STATE_ORD;
  void execDUMP_STATE_ORD(Signal*);
  void sendDUMP_STATE_ORD(Signal*, Uint32 ssId);

  // GSN_NDB_TAMPER
  struct Ss_NDB_TAMPER : SsParallel {
    Uint32 m_errorInsert;
    Ss_NDB_TAMPER() {
      m_sendREQ = &LocalProxy::sendNDB_TAMPER;
      m_sendCONF = 0;
    }
    enum { poolSize = 1 };
    static SsPool<Ss_NDB_TAMPER>& pool(LocalProxy* proxy) {
      return proxy->c_ss_NDB_TAMPER;
    }
  };
  SsPool<Ss_NDB_TAMPER> c_ss_NDB_TAMPER;
  void execNDB_TAMPER(Signal*);
  void sendNDB_TAMPER(Signal*, Uint32 ssId);

  // GSN_TIME_SIGNAL
  struct Ss_TIME_SIGNAL : SsParallel {
    Ss_TIME_SIGNAL() {
      m_sendREQ = &LocalProxy::sendTIME_SIGNAL;
      m_sendCONF = 0;
    }
    enum { poolSize = 1 };
    static SsPool<Ss_TIME_SIGNAL>& pool(LocalProxy* proxy) {
      return proxy->c_ss_TIME_SIGNAL;
    }
  };
  SsPool<Ss_TIME_SIGNAL> c_ss_TIME_SIGNAL;
  void execTIME_SIGNAL(Signal*);
  void sendTIME_SIGNAL(Signal*, Uint32 ssId);

  // GSN_CREATE_TRIG_IMPL_REQ
  struct Ss_CREATE_TRIG_IMPL_REQ : SsParallel {
    CreateTrigImplReq m_req;
    Ss_CREATE_TRIG_IMPL_REQ() {
      m_sendREQ = &LocalProxy::sendCREATE_TRIG_IMPL_REQ;
      m_sendCONF = &LocalProxy::sendCREATE_TRIG_IMPL_CONF;
    }
    enum { poolSize = 3 };
    static SsPool<Ss_CREATE_TRIG_IMPL_REQ>& pool(LocalProxy* proxy) {
      return proxy->c_ss_CREATE_TRIG_IMPL_REQ;
    }
  };
  SsPool<Ss_CREATE_TRIG_IMPL_REQ> c_ss_CREATE_TRIG_IMPL_REQ;
  void execCREATE_TRIG_IMPL_REQ(Signal*);
  void sendCREATE_TRIG_IMPL_REQ(Signal*, Uint32 ssId);
  void execCREATE_TRIG_IMPL_CONF(Signal*);
  void execCREATE_TRIG_IMPL_REF(Signal*);
  void sendCREATE_TRIG_IMPL_CONF(Signal*, Uint32 ssId);

  // GSN_DROP_TRIG_IMPL_REQ
  struct Ss_DROP_TRIG_IMPL_REQ : SsParallel {
    DropTrigImplReq m_req;
    Ss_DROP_TRIG_IMPL_REQ() {
      m_sendREQ = &LocalProxy::sendDROP_TRIG_IMPL_REQ;
      m_sendCONF = &LocalProxy::sendDROP_TRIG_IMPL_CONF;
    }
    enum { poolSize = 3 };
    static SsPool<Ss_DROP_TRIG_IMPL_REQ>& pool(LocalProxy* proxy) {
      return proxy->c_ss_DROP_TRIG_IMPL_REQ;
    }
  };
  SsPool<Ss_DROP_TRIG_IMPL_REQ> c_ss_DROP_TRIG_IMPL_REQ;
  void execDROP_TRIG_IMPL_REQ(Signal*);
  void sendDROP_TRIG_IMPL_REQ(Signal*, Uint32 ssId);
  void execDROP_TRIG_IMPL_CONF(Signal*);
  void execDROP_TRIG_IMPL_REF(Signal*);
  void sendDROP_TRIG_IMPL_CONF(Signal*, Uint32 ssId);
};

#endif
