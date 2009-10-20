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

#ifndef VMSignal_H
#define VMSignal_H

#include <ndb_global.h>
#include <ndb_limits.h>
#include <kernel_types.h>

#include <ErrorReporter.hpp>
#include <NodeBitmask.hpp>

#include <RefConvert.hpp>
#include <TransporterDefinitions.hpp>

extern void getSections(Uint32 secCount, SegmentedSectionPtr ptr[3]);

struct SectionHandle
{
  SectionHandle (class SimulatedBlock*);
  SectionHandle (class SimulatedBlock*, Uint32 ptrI);
  SectionHandle (class SimulatedBlock*, class Signal*);
  ~SectionHandle ();

  Uint32 m_cnt;
  SegmentedSectionPtr m_ptr[3];

  bool getSection(SegmentedSectionPtr & ptr, Uint32 sectionNo);
  void clear() { m_cnt = 0;}

  SimulatedBlock* m_block;
};

/**
 * Struct used when sending to multiple blocks
 */
struct NodeReceiverGroup {
  NodeReceiverGroup();
  NodeReceiverGroup(Uint32 blockRef);
  NodeReceiverGroup(Uint32 blockNo, const NodeBitmask &);
  NodeReceiverGroup(Uint32 blockNo, const NdbNodeBitmask &);
  NodeReceiverGroup(Uint32 blockNo, const class SignalCounter &);
  
  NodeReceiverGroup& operator=(BlockReference ref);
  
  Uint32 m_block;
  NodeBitmask m_nodes;
};

template <unsigned T> struct SignalT
{
  SignalT() { bzero(&header, sizeof(header)); }
  Uint32 m_sectionPtrI[3];
  SignalHeader header;
  union {
    Uint32 theData[T];
    Uint64 dummyAlign;
  };
};

/**
 * class used for passing argumentes to blocks
 */
class Signal {
  friend class SimulatedBlock;
  friend class APZJobBuffer;
  friend class FastScheduler;
public:
  Signal();
  
  Uint32 getLength() const;
  Uint32 getTrace() const;
  Uint32 getSendersBlockRef() const;

  const Uint32* getDataPtr() const ;
  Uint32* getDataPtrSend() ;
  
  void setTrace(Uint32);

  Uint32 getNoOfSections() const;

  /**
   * Old depricated methods...
   */
  Uint32 length() const { return getLength();}
  BlockReference senderBlockRef() const { return getSendersBlockRef();}

private:
  void setLength(Uint32);
  
public:
#define VMS_DATA_SIZE \
  (MAX_ATTRIBUTES_IN_TABLE + MAX_TUPLE_SIZE_IN_WORDS + MAX_KEY_SIZE_IN_WORDS)

#if VMS_DATA_SIZE > 8192
#error "VMSignal buffer is too small"
#endif
  
  Uint32 m_sectionPtrI[3];
  SignalHeader header; // 28 bytes
  union {
    Uint32 theData[8192];  // 8192 32-bit words -> 32K Bytes
    Uint64 dummyAlign;
  };
  void garbage_register();
};

inline
Uint32
Signal::getLength() const {
  return header.theLength;
}

inline
Uint32
Signal::getTrace() const {
  return header.theTrace;
}

inline
Uint32
Signal::getSendersBlockRef() const {
  return header.theSendersBlockRef;
}

inline
const Uint32* 
Signal::getDataPtr() const { 
  return &theData[0];
}

inline
Uint32* 
Signal::getDataPtrSend() { 
  return &theData[0];
}

inline
void
Signal::setLength(Uint32 len){
  header.theLength = len;
}

inline
void
Signal::setTrace(Uint32 t){
  header.theTrace = t;
}

inline
Uint32 
Signal::getNoOfSections() const {
  return header.m_noOfSections;
}

inline
NodeReceiverGroup::NodeReceiverGroup() : m_block(0){
  m_nodes.clear();
}

inline
NodeReceiverGroup::NodeReceiverGroup(Uint32 blockRef){
  m_nodes.clear();
  m_block = refToBlock(blockRef);
  m_nodes.set(refToNode(blockRef));
}

inline
NodeReceiverGroup::NodeReceiverGroup(Uint32 blockNo, 
				     const NodeBitmask & nodes)
{
  m_block = blockNo;
  m_nodes = nodes;
}

inline
NodeReceiverGroup::NodeReceiverGroup(Uint32 blockNo, 
				     const NdbNodeBitmask & nodes)
{
  m_block = blockNo;
  m_nodes = nodes;
}

#include "SignalCounter.hpp"

inline
NodeReceiverGroup::NodeReceiverGroup(Uint32 blockNo, 
				     const SignalCounter & nodes){
  m_block = blockNo;
  m_nodes = nodes.m_nodes;
}

inline
NodeReceiverGroup& 
NodeReceiverGroup::operator=(BlockReference blockRef){
  m_nodes.clear();
  m_block = refToBlock(blockRef);
  m_nodes.set(refToNode(blockRef));
  return * this;
}

inline
SectionHandle::SectionHandle(SimulatedBlock* b)
  : m_cnt(0), 
    m_block(b)
{
}

inline
SectionHandle::SectionHandle(SimulatedBlock* b, Signal* s)
  : m_cnt(s->header.m_noOfSections),
    m_block(b)
{
  Uint32 * ptr = s->m_sectionPtrI;
  Uint32 ptr0 = * ptr++;
  Uint32 ptr1 = * ptr++;
  Uint32 ptr2 = * ptr++;

  m_ptr[0].i = ptr0;
  m_ptr[1].i = ptr1;
  m_ptr[2].i = ptr2;

  getSections(m_cnt, m_ptr);

  s->header.m_noOfSections = 0;
}

inline
SectionHandle::SectionHandle(SimulatedBlock* b, Uint32 ptr)
  : m_cnt(1),
    m_block(b)
{
  m_ptr[0].i = ptr;
  getSections(1, m_ptr);
}

inline
bool
SectionHandle::getSection(SegmentedSectionPtr& ptr, Uint32 no)
{
  if (likely(no < m_cnt))
  {
    ptr = m_ptr[no];
    return true;
  }

  return false;
}

#endif
