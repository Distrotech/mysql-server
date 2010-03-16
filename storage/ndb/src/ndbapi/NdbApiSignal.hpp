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

/**********************************************************************
 * Name:		NdbApiSignal.H
 * Include:	
 * Link:		
 * Author:		UABMNST Mona Natterkvist UAB/B/SD
 * Date:		97----
 * Version:	0.1
 * Description:	Interface between TIS and NDB
 * Documentation:
 * Adjust:		971204  UABMNST   First version.
 * Adjust:         000705  QABANAB   Changes in Protocol2
 * Comment:	
 *****************************************************************************/
#ifndef NdbApiSignal_H
#define NdbApiSignal_H

#include <kernel_types.h>
#include "TransporterFacade.hpp"
#include <TransporterDefinitions.hpp>
#include "Ndb.hpp"

#define CAST_PTR(X,Y) reinterpret_cast<X*>(Y)
#define CAST_CONSTPTR(X,Y) reinterpret_cast<const X*>(Y)

#include <signaldata/GetTabInfo.hpp>
#include <signaldata/DictTabInfo.hpp>
#include <signaldata/CreateTable.hpp>
#include <signaldata/CreateIndx.hpp>
#include <signaldata/CreateEvnt.hpp>
#include <signaldata/SumaImpl.hpp>
#include <signaldata/DropTable.hpp>
#include <signaldata/AlterTable.hpp>
#include <signaldata/DropIndx.hpp>
#include <signaldata/ListTables.hpp>
#include <signaldata/DropFilegroup.hpp>
#include <signaldata/CreateFilegroup.hpp>
#include <signaldata/WaitGCP.hpp>
#include <signaldata/SchemaTrans.hpp>
#include <signaldata/CreateHashMap.hpp>
#include <signaldata/ApiRegSignalData.hpp>
#include <signaldata/ArbitSignalData.hpp>

/**
 * A NdbApiSignal : public SignalHeader
 *
 * Stores the address to theData in theSignalId
 */
class NdbApiSignal : public SignalHeader
 {
public:  
  			NdbApiSignal(Ndb* ndb);
  			NdbApiSignal(BlockReference ref);
  			NdbApiSignal(const NdbApiSignal &);
                        NdbApiSignal(const SignalHeader &header)
			  : SignalHeader(header), theNextSignal(0), theRealData(0) {};
  			~NdbApiSignal();

  void                  set(Uint8  trace,
			    Uint16 receiversBlockNumber,
			    Uint16 signalNumber,
			    Uint32 length);

  
  void 			setData(Uint32 aWord, Uint32 aDataNo);  
  Uint32 		readData(Uint32 aDataNo) const; // Read word in signal
  
  int 			setSignal(int NdbSignalType);  	// Set signal header  
  int 			readSignalNumber();    		// Read signal number  
  Uint32             	getLength() const;
  void	             	setLength(Uint32 aLength);
  void 			next(NdbApiSignal* anApiSignal);  
  NdbApiSignal* 	next();
 
   const Uint32 *       getDataPtr() const;
         Uint32 *       getDataPtrSend();
   STATIC_CONST(        MaxSignalWords = 25);

  NodeId                get_sender_node();

  /**
   * Fragmentation
   */
  bool isFragmented() const { return m_fragmentInfo != 0;}
  bool isFirstFragment() const { return m_fragmentInfo <= 1;}
  bool isLastFragment() const { 
    return m_fragmentInfo == 0 || m_fragmentInfo == 3; 
  }
  
  Uint32 getFragmentId() const { 
    return (m_fragmentInfo == 0 ? 0 : getDataPtr()[theLength - 1]); 
  }
  
private:
  void setDataPtr(Uint32 *);
  
  friend class NdbTransaction;
  friend class NdbScanReceiver;
  friend class Table;
  friend class TransporterFacade;
  void copyFrom(const NdbApiSignal * src);

  /**
   * Only used when creating a signal in the api
   */
  union {
    Uint32 theData[MaxSignalWords];
    WaitGCPReq _WaitGCPReq;
    GetTabInfoReq _GetTabInfoReq;
    ListTablesReq _ListTablesReq;
    CreateTableReq _CreateTableReq;
    AlterTableReq _AlterTableReq;
    CreateIndxReq _CreateIndxReq;
    DropTableReq _DropTableReq;
    DropIndxReq _DropIndxReq;
    CreateFileReq _CreateFileReq;
    CreateFilegroupReq _CreateFilegroupReq;
    DropFilegroupReq _DropFilegroupReq;
    DropFileReq _DropFileReq;
    SubStartReq _SubStartReq;
    SubStopReq _SubStopReq;
    CreateEvntReq _CreateEvntReq;
    DropEvntReq _DropEvntReq;
    SubGcpCompleteAck _SubGcpCompleteAck;
    CreateHashMapReq _CreateHashMapReq;
    SchemaTransBeginReq _SchemaTransBeginReq;
    SchemaTransEndReq _SchemaTransEndReq;
    ApiRegReq _ApiRegReq;
    ArbitSignalData _ArbitSignalData;
  };
  NdbApiSignal *theNextSignal;
  Uint32 *theRealData;
};
/**********************************************************************
NodeId get_sender_node
Remark:        Get the node id of the sender
***********************************************************************/
inline
NodeId
NdbApiSignal::get_sender_node()
{
  return refToNode(theSendersBlockRef);
}

/**********************************************************************
void getLength
Remark:        Get the length of the signal.
******************************************************************************/
inline
Uint32
NdbApiSignal::getLength() const{
  return theLength;
}

/**********************************************************************
void setLength
Parameters:    aLength: Signal length
Remark:        Set the length in the signal.
******************************************************************************/
inline
void
NdbApiSignal::setLength(Uint32 aLength){
  theLength = aLength;
}

/**********************************************************************
void next(NdbApiSignal* aSignal);

Parameters:     aSignal: Signal object.
Remark:         Insert signal rear in a linked list.   
*****************************************************************************/
inline
void 
NdbApiSignal::next(NdbApiSignal* aSignal){
  theNextSignal = aSignal;
}
/**********************************************************************
NdbApiSignal* next();

Return Value:   Return theNext signal object if the next was successful.
                Return NULL: In all other case.  
Remark:         Read the theNext in signal.   
*****************************************************************************/
inline
NdbApiSignal* 
NdbApiSignal::next(){
  return theNextSignal;
}
/**********************************************************************
int readSignalNo();

Return Value:    Return the signalNumber. 
Remark:          Read signal number 
*****************************************************************************/
inline
int		
NdbApiSignal::readSignalNumber()
{
  return (int)theVerId_signalNumber;
}
/**********************************************************************
Uint32 readData(Uint32 aDataNo);

Return Value:   Return Data word in a signal.
                Return -1: In all other case.
                aDataNo: Data number in signal.
Remark:         Return the dataWord information in a signal for a dataNo.  
******************************************************************************/
inline
Uint32
NdbApiSignal::readData(Uint32 aDataNo) const {
  return getDataPtr()[aDataNo-1];
}
/**********************************************************************
int setData(Uint32 aWord, int aDataNo);

Return Value:   Return 0 : setData was successful.
                Return -1: In all other case.  
Parameters:     aWord: Data word.
                aDataNo: Data number in signal.
Remark:         Set Data word in signal 1 - 25  
******************************************************************************/
inline
void
NdbApiSignal::setData(Uint32 aWord, Uint32 aDataNo){
  getDataPtrSend()[aDataNo -1] = aWord;
}

/**
 * Return pointer to data structure
 */
inline
const Uint32 *
NdbApiSignal::getDataPtr() const {
  return theRealData;
}

inline
Uint32 *
NdbApiSignal::getDataPtrSend(){
  return (Uint32*)&theData[0];
}

inline
void
NdbApiSignal::setDataPtr(Uint32 * ptr){
  theRealData = ptr;
}

#endif
