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

#ifndef SIGNAL_SENDER_HPP
#define SIGNAL_SENDER_HPP

#include <ndb_global.h>
#include "TransporterFacade.hpp"
#include <Vector.hpp>

struct SimpleSignal {
public:
  SimpleSignal(bool dealloc = false);
  ~SimpleSignal();
  
  void set(class SignalSender&,
	   Uint8  trace, Uint16 recBlock, Uint16 gsn, Uint32 len);
  
  struct SignalHeader header;
  Uint32 theData[25];
  LinearSectionPtr ptr[3];

  int readSignalNumber() const {return header.theVerId_signalNumber; }
  Uint32 *getDataPtrSend() { return theData; }
  const Uint32 *getDataPtr() const { return theData; }

  /**
   * Fragmentation
   */
  bool isFragmented() const { return header.m_fragmentInfo != 0;}
  bool isFirstFragment() const { return header.m_fragmentInfo <= 1;}
  bool isLastFragment() const { 
    return header.m_fragmentInfo == 0 || header.m_fragmentInfo == 3; 
  }


  Uint32 getFragmentId() const {
    return (header.m_fragmentInfo == 0 ? 0 : getDataPtr()[header.theLength - 1]);
  }

  void print(FILE * out = stdout);
private:
  bool deallocSections;
};

class SignalSender {
public:
  SignalSender(TransporterFacade *facade, int blockNo = -1);
  SignalSender(Ndb_cluster_connection* connection);
  virtual ~SignalSender();
  
  int lock();
  int unlock();

  Uint32 getOwnRef() const;
  const ClusterMgr::Node &getNodeInfo(Uint16 nodeId) const;
  Uint32 getNoOfConnectedNodes() const;

  NodeId find_confirmed_node(const NodeBitmask& mask);
  NodeId find_connected_node(const NodeBitmask& mask);
  NodeId find_alive_node(const NodeBitmask& mask);

  SendStatus sendSignal(Uint16 nodeId, const SimpleSignal *);
  SendStatus sendSignal(Uint16 nodeId, SimpleSignal& sig,
                        Uint16 recBlock, Uint16 gsn, Uint32 len);
  int sendFragmentedSignal(Uint16 nodeId, SimpleSignal& sig,
                           Uint16 recBlock, Uint16 gsn, Uint32 len);
  NodeBitmask broadcastSignal(NodeBitmask mask, SimpleSignal& sig,
                              Uint16 recBlock, Uint16 gsn, Uint32 len);

  SimpleSignal * waitFor(Uint32 timeOutMillis = 0);
  SimpleSignal * waitFor(Uint16 nodeId, Uint32 timeOutMillis = 0);
  SimpleSignal * waitFor(Uint16 nodeId, Uint16 gsn, Uint32 timeOutMillis = 0);  

  Uint32 get_an_alive_node() const { return theFacade->get_an_alive_node(); }
  Uint32 getAliveNode() const { return get_an_alive_node(); }
  bool get_node_alive(NodeId n) const { return theFacade->get_node_alive(n); }

private:
  int m_blockNo;
  TransporterFacade * theFacade;
  
  static void execSignal(void* signalSender, 
			 NdbApiSignal* signal, 
			 struct LinearSectionPtr ptr[3]);
  
  static void execNodeStatus(void* signalSender, Uint32 nodeId, 
			     bool alive, bool nfCompleted);
  
  int m_lock;
  struct NdbCondition * m_cond;
  Vector<SimpleSignal *> m_jobBuffer;
  Vector<SimpleSignal *> m_usedBuffer;

  template<class T>
  SimpleSignal * waitFor(Uint32 timeOutMillis, T & t);

  template<class T>
  NodeId find_node(const NodeBitmask& mask, T & t);
};

#endif
