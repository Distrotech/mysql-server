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

#ifndef SIGNAL_COUNTER_HPP
#define SIGNAL_COUNTER_HPP

#include <NodeBitmask.hpp>
#include <ErrorReporter.hpp>

class SignalCounter {
private:
  Uint32 m_count;
  NdbNodeBitmask m_nodes;

public:
  SignalCounter() { clearWaitingFor();}
  void clearWaitingFor();
  
  /**
   * When sending to different node
   */
  void setWaitingFor(Uint32 nodeId);
  void clearWaitingFor(Uint32 nodeId);
  void forceClearWaitingFor(Uint32 nodeId);
  
  bool isWaitingFor(Uint32 nodeId) const;
  bool done() const;

  const char * getText() const;

  SignalCounter& operator=(const NdbNodeBitmask & bitmask);
  SignalCounter& operator=(const NodeReceiverGroup& rg) { 
    return (* this) = rg.m_nodes;
  }

  /**
   * When sending to same node
   */
  SignalCounter& operator=(Uint32 count);
  SignalCounter& operator--(int);
  SignalCounter& operator++(int);
  SignalCounter& operator+=(Uint32);
  Uint32 getCount() const;
};

inline
void 
SignalCounter::setWaitingFor(Uint32 nodeId) {
  if(!m_nodes.get(nodeId)){
    m_nodes.set(nodeId);
    m_count++;
    return;
  }
  ErrorReporter::handleAssert("SignalCounter::set", __FILE__, __LINE__);
}

inline
bool
SignalCounter::isWaitingFor(Uint32 nodeId) const {
  return m_nodes.get(nodeId);
}

inline
bool
SignalCounter::done() const {
  return m_count == 0;
}

inline
Uint32
SignalCounter::getCount() const {
  return m_count;
}

inline
void
SignalCounter::clearWaitingFor(Uint32 nodeId) {
  if(m_nodes.get(nodeId) && m_count > 0){
    m_count--;
    m_nodes.clear(nodeId);
    return;
  }
  ErrorReporter::handleAssert("SignalCounter::clear", __FILE__, __LINE__);
}

inline
void
SignalCounter::clearWaitingFor(){
  m_count = 0;
  m_nodes.clear();
}

inline
void
SignalCounter::forceClearWaitingFor(Uint32 nodeId){
  if(isWaitingFor(nodeId)){
    clearWaitingFor(nodeId);
  }
}

inline
SignalCounter&
SignalCounter::operator=(Uint32 count){
  m_count = count;
  m_nodes.clear();
  return * this;
}

inline
SignalCounter&
SignalCounter::operator--(int){
  if(m_count > 0){
    m_count--;
    return * this;
  }
  ErrorReporter::handleAssert("SignalCounter::operator--", __FILE__, __LINE__);
  return * this;
}

inline
SignalCounter&
SignalCounter::operator++(int){
  m_count++;
  return * this;
}

inline
SignalCounter&
SignalCounter::operator+=(Uint32 n){
  m_count += n;
  return * this;
}

inline
const char *
SignalCounter::getText() const {
  static char buf[255];
  static char nodes[NodeBitmask::TextLength+1];
  snprintf(buf, sizeof(buf), "[SignalCounter: m_count=%d %s]", m_count, m_nodes.getText(nodes));
  return buf;
}

inline
SignalCounter&
SignalCounter::operator=(const NdbNodeBitmask & bitmask){
  m_nodes = bitmask;
  m_count = bitmask.count();
  return * this;
}

#endif
