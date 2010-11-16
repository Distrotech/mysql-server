/*
   Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.

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

#ifndef trp_client_hpp
#define trp_client_hpp

#include <ndb_global.h>
#include <NdbCondition.h>

struct trp_node;
class NdbApiSignal;
struct LinearSectionPtr;
struct GenericSectionPtr;

class trp_client
{
  friend class TransporterFacade;
public:
  trp_client();
  virtual ~trp_client();

  virtual void trp_deliver_signal(const NdbApiSignal *,
                                  const LinearSectionPtr ptr[3]) = 0;
  virtual void trp_node_status(Uint32, Uint32 event) = 0;

  int open(class TransporterFacade*, int blockNo = -1);
  void close();

  void start_poll();
  void do_poll(Uint32);
  void complete_poll();
  void wakeup();

  void forceSend(int val = 1);

  int sendSignal(NdbApiSignal * signal, Uint32 nodeId);
  int sendSignal(NdbApiSignal*, Uint32, const LinearSectionPtr p[3], Uint32 s);
  int sendSignal(NdbApiSignal*, Uint32, const GenericSectionPtr p[3], Uint32 s);
  int sendFragmentedSignal(NdbApiSignal*, Uint32,
			   const LinearSectionPtr ptr[3], Uint32 secs);
  int sendFragmentedSignal(NdbApiSignal*, Uint32,
                           const GenericSectionPtr ptr[3], Uint32 secs);

  const trp_node & getNodeInfo(Uint32 i) const;

  void lock();
  void unlock();
private:
  Uint32 m_blockNo;
  TransporterFacade * m_facade;

  /**
   * This is used for polling
   */
  struct PollQueue
  {
    bool m_locked;
    bool m_poll_owner;
    trp_client *m_prev;
    trp_client *m_next;
    NdbCondition * m_condition;
  } m_poll;
  void cond_wait(Uint32 timeout, NdbMutex*);
  void cond_signal();
};

inline
void
trp_client::wakeup()
{
  if (m_poll.m_locked == true && m_poll.m_poll_owner == false)
    cond_signal();
  else if (m_poll.m_poll_owner)
    assert(m_poll.m_locked);
}

class PollGuard
{
public:
  PollGuard(class NdbImpl&);
  ~PollGuard() { unlock_and_signal(); }
  int wait_n_unlock(int wait_time, Uint32 nodeId, Uint32 state,
                    bool forceSend= false);
  int wait_for_input_in_loop(int wait_time, bool forceSend);
  void wait_for_input(int wait_time);
  int wait_scan(int wait_time, Uint32 nodeId, bool forceSend);
  void unlock_and_signal();
private:
  class trp_client* m_clnt;
  class NdbWaiter *m_waiter;
};

#include "TransporterFacade.hpp"

inline
void
trp_client::lock()
{
  assert(m_poll.m_locked == false);
  NdbMutex_Lock(m_facade->theMutexPtr);
  m_poll.m_locked = true;
}

inline
void
trp_client::unlock()
{
  assert(m_poll.m_locked == true);
  NdbMutex_Unlock(m_facade->theMutexPtr);
  m_poll.m_locked = false;
}

inline
int
trp_client::sendSignal(NdbApiSignal * signal, Uint32 nodeId)
{
  return m_facade->sendSignal(signal, nodeId);
}

inline
int
trp_client::sendSignal(NdbApiSignal * signal, Uint32 nodeId,
                       const LinearSectionPtr ptr[3], Uint32 secs)
{
  return m_facade->sendSignal(signal, nodeId, ptr, secs);
}

inline
int
trp_client::sendSignal(NdbApiSignal * signal, Uint32 nodeId,
                       const GenericSectionPtr ptr[3], Uint32 secs)
{
  return m_facade->sendSignal(signal, nodeId, ptr, secs);
}

inline
int
trp_client::sendFragmentedSignal(NdbApiSignal * signal, Uint32 nodeId,
                                 const LinearSectionPtr ptr[3], Uint32 secs)
{
  return m_facade->sendFragmentedSignal(signal, nodeId, ptr, secs);
}

inline
int
trp_client::sendFragmentedSignal(NdbApiSignal * signal, Uint32 nodeId,
                                 const GenericSectionPtr ptr[3], Uint32 secs)
{
  return m_facade->sendFragmentedSignal(signal, nodeId, ptr, secs);
}

#endif
