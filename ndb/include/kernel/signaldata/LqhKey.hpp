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

#ifndef LQH_KEY_H
#define LQH_KEY_H

#include "SignalData.hpp"

class LqhKeyReq {
  /**
   * Reciver(s)
   */
  friend class Dblqh;         // Reciver

  /**
   * Sender(s)
   */
  friend class Dbtc;      

  /**
   * For printing
   */
  friend bool printLQHKEYREQ(FILE * output, const Uint32 * theData, Uint32 len, Uint16 receiverBlockNo);

public:
  STATIC_CONST( FixedSignalLength = 11 );

private:

  /**
   * DATA VARIABLES
   */
//-------------------------------------------------------------
// Unconditional part. First 10 words
//-------------------------------------------------------------
  UintR clientConnectPtr;	// DATA 0
  UintR attrLen;	        // DATA 1
  UintR hashValue;		// DATA 2
  UintR requestInfo;   		// DATA 3
  UintR tcBlockref;		// DATA 4
  UintR tableSchemaVersion;	// DATA 5
  UintR fragmentData;		// DATA 6
  UintR transId1;		// DATA 7
  UintR transId2;		// DATA 8
  UintR savePointId;            // DATA 9
  union {
    /**
     * When sent from  TC -> LQH this variable contains scanInfo
     * When send from LQH -> LQH this variable contains noFiredTriggers
     */
    UintR noFiredTriggers;	// DATA 10 
    Uint32 scanInfo;            // DATA 10
  };

//-------------------------------------------------------------
// Variable sized key part. Those will be placed to
// pack the signal in an appropriate manner.
//-------------------------------------------------------------
  UintR variableData[10];           // DATA 11 - 21

  static UintR getAttrLen(const UintR & scanInfoAttrLen);
  static UintR getScanTakeOverFlag(const UintR & scanInfoAttrLen);
  static UintR getStoredProcFlag(const UintR & scanData);
  static UintR getDistributionKey(const UintR & scanData);
  
  static UintR getTableId(const UintR & tableSchemaVersion);
  static UintR getSchemaVersion(const UintR & tableSchemaVersion);

  static UintR getFragmentId(const UintR & fragmentData);
  static UintR getNextReplicaNodeId(const UintR & fragmentData);

  static Uint8 getLockType(const UintR & requestInfo);
  static Uint8 getDirtyFlag(const UintR & requestInfo);
  static Uint8 getInterpretedFlag(const UintR & requestInfo);
  static Uint8 getSimpleFlag(const UintR & requestInfo);
  static Uint8 getOperation(const UintR & requestInfo);
  static Uint8 getSeqNoReplica(const UintR & requestInfo);
  static Uint8 getLastReplicaNo(const UintR & requestInfo);
  static Uint8 getAIInLqhKeyReq(const UintR & requestInfo);
  static UintR getKeyLen(const UintR & requestInfo);
  static UintR getSameClientAndTcFlag(const UintR & requestInfo);
  static UintR getReturnedReadLenAIFlag(const UintR & requestInfo);
  static UintR getApplicationAddressFlag(const UintR & requestInfo);
  static UintR getMarkerFlag(const UintR & requestInfo);

  /**
   * Setters
   */

  static void setAttrLen(UintR & scanInfoAttrLen, UintR val);
  static void setScanTakeOverFlag(UintR & scanInfoAttrLen, UintR val);
  static void setStoredProcFlag(UintR & scanData, UintR val);
  static void setDistributionKey(UintR & scanData, UintR val);
  
  static void setTableId(UintR & tableSchemaVersion, UintR val);
  static void setSchemaVersion(UintR & tableSchemaVersion, UintR val);

  static void setFragmentId(UintR & fragmentData, UintR val);
  static void setNextReplicaNodeId(UintR & fragmentData, UintR val);

  static void setLockType(UintR & requestInfo, UintR val);
  static void setDirtyFlag(UintR & requestInfo, UintR val);
  static void setInterpretedFlag(UintR & requestInfo, UintR val);
  static void setSimpleFlag(UintR & requestInfo, UintR val);
  static void setOperation(UintR & requestInfo, UintR val);
  static void setSeqNoReplica(UintR & requestInfo, UintR val);
  static void setLastReplicaNo(UintR & requestInfo, UintR val);
  static void setAIInLqhKeyReq(UintR & requestInfo, UintR val);
  static void setKeyLen(UintR & requestInfo, UintR val);
  static void setSameClientAndTcFlag(UintR & requestInfo, UintR val);
  static void setReturnedReadLenAIFlag(UintR & requestInfo, UintR val);
  static void setApplicationAddressFlag(UintR & requestInfo, UintR val);
  static void setMarkerFlag(UintR & requestInfo, UintR val);
};

/**
 * Request Info
 *
 * k = Key len                - 10 Bits (0-9) max 1023
 * l = Last Replica No        - 2  Bits -> Max 3 (10-11)
 * t = Lock type              - 3  Bits -> Max 7 (12-14)
 * p = Application Addr. Ind  - 1  Bit (15)
 * d = Dirty indicator        - 1  Bit (16)
 * i = Interpreted indicator  - 1  Bit (17)
 * s = Simple indicator       - 1  Bit (18)
 * o = Operation              - 3  Bits (19-21)
 * r = Sequence replica       - 2  Bits (22-23)
 * a = Attr Info in LQHKEYREQ - 3  Bits (24-26)
 * c = Same client and tc     - 1  Bit (27)
 * u = Read Len Return Ind    - 1  Bit (28)
 * m = Commit ack marker      - 1  Bit (29)
 * - = Unused                 - 2  Bits (30-31)
 *
 *           1111111111222222222233
 * 01234567890123456789012345678901
 * kkkkkkkkkklltttpdisooorraaacum--
 */

#define RI_KEYLEN_SHIFT      (0)
#define RI_KEYLEN_MASK       (1023)
#define RI_LAST_REPL_SHIFT   (10)
#define RI_LAST_REPL_MASK    (3)
#define RI_LOCK_TYPE_SHIFT   (12)
#define RI_LOCK_TYPE_MASK    (7)
#define RI_APPL_ADDR_SHIFT   (15)
#define RI_DIRTY_SHIFT       (16)
#define RI_INTERPRETED_SHIFT (17)
#define RI_SIMPLE_SHIFT      (18)
#define RI_OPERATION_SHIFT   (19)
#define RI_OPERATION_MASK    (7)
#define RI_SEQ_REPLICA_SHIFT (22)
#define RI_SEQ_REPLICA_MASK  (3)
#define RI_AI_IN_THIS_SHIFT  (24)
#define RI_AI_IN_THIS_MASK   (7)
#define RI_SAME_CLIENT_SHIFT (27)
#define RI_RETURN_AI_SHIFT   (28)
#define RI_MARKER_SHIFT      (29)

/**
 * Scan Info
 *
 * a = Attr Len                 - 16 Bits -> max 65535 (0-15)
 * p = Stored Procedure Ind     -  1 Bit (16)
 * d = Distribution key         -  8 Bit  -> max 255 (17-24)
 * t = Scan take over indicator -  1 Bit (25)
 *
 *           1111111111222222222233
 * 01234567890123456789012345678901
 * aaaaaaaaaaaaaaaapddddddddt             
 */

#define SI_ATTR_LEN_MASK     (65535)
#define SI_ATTR_LEN_SHIFT    (0)
#define SI_STORED_PROC_SHIFT (16)
#define SI_DISTR_KEY_MASK    (255)
#define SI_DISTR_KEY_SHIFT   (17)
#define SI_SCAN_TO_SHIFT     (25)
#define SI_SCAN_INFO_MASK    (63)
#define SI_SCAN_INFO_SHIFT   (26)

inline 
UintR
LqhKeyReq::getAttrLen(const UintR & scanData)
{
  return (scanData >> SI_ATTR_LEN_SHIFT) & SI_ATTR_LEN_MASK;
}

inline 
Uint32
LqhKeyReq::getScanTakeOverFlag(const UintR & scanData)
{
  return (scanData >> SI_SCAN_TO_SHIFT) & 1;
}

inline
UintR
LqhKeyReq::getStoredProcFlag(const UintR & scanData){
  return (scanData >> SI_STORED_PROC_SHIFT) & 1;
}

inline
UintR
LqhKeyReq::getDistributionKey(const UintR & scanData){
  return (scanData >> SI_DISTR_KEY_SHIFT) & SI_DISTR_KEY_MASK;
}

inline 
UintR LqhKeyReq::getTableId(const UintR & tableSchemaVersion)
{
 return tableSchemaVersion & 0xFFFF;
}

inline 
UintR LqhKeyReq::getSchemaVersion(const UintR & tableSchemaVersion)
{
  return tableSchemaVersion >> 16;
}

inline 
UintR LqhKeyReq::getFragmentId(const UintR & fragmentData)
{
  return fragmentData & 0xFFFF;
}

inline 
UintR LqhKeyReq::getNextReplicaNodeId(const UintR & fragmentData)
{
 return fragmentData >> 16;
}

inline 
Uint8 LqhKeyReq::getLastReplicaNo(const UintR & requestInfo)
{
 return (requestInfo >> RI_LAST_REPL_SHIFT) & RI_LAST_REPL_MASK;
}

inline 
Uint8 LqhKeyReq::getLockType(const UintR & requestInfo)
{
  return (requestInfo >> RI_LOCK_TYPE_SHIFT) & RI_LOCK_TYPE_MASK;
}

inline 
Uint8 LqhKeyReq::getDirtyFlag(const UintR & requestInfo)
{
  return (requestInfo >> RI_DIRTY_SHIFT) & 1;
}

inline 
Uint8 LqhKeyReq::getInterpretedFlag(const UintR & requestInfo)
{
  return (requestInfo >> RI_INTERPRETED_SHIFT) & 1;
}

inline 
Uint8 LqhKeyReq::getSimpleFlag(const UintR & requestInfo)
{
  return (requestInfo >> RI_SIMPLE_SHIFT) & 1;
}

inline 
Uint8 LqhKeyReq::getOperation(const UintR & requestInfo)
{
  return (requestInfo >> RI_OPERATION_SHIFT) & RI_OPERATION_MASK;
}

inline 
Uint8 LqhKeyReq::getSeqNoReplica(const UintR & requestInfo)
{
  return (requestInfo >> RI_SEQ_REPLICA_SHIFT) & RI_SEQ_REPLICA_MASK;
}


inline 
Uint8 LqhKeyReq::getAIInLqhKeyReq(const UintR & requestInfo)
{
  return (requestInfo >> RI_AI_IN_THIS_SHIFT) & RI_AI_IN_THIS_MASK;
}

inline 
UintR LqhKeyReq::getKeyLen(const UintR & requestInfo)
{
  return (requestInfo >> RI_KEYLEN_SHIFT) & RI_KEYLEN_MASK;
}

inline 
UintR 
LqhKeyReq::getSameClientAndTcFlag(const UintR & requestInfo)
{
  return (requestInfo >> RI_SAME_CLIENT_SHIFT) & 1;
}

inline 
UintR LqhKeyReq::getReturnedReadLenAIFlag(const UintR & requestInfo)
{
  return (requestInfo >> RI_RETURN_AI_SHIFT) & 1;
}

inline
UintR 
LqhKeyReq::getApplicationAddressFlag(const UintR & requestInfo){
  return (requestInfo >> RI_APPL_ADDR_SHIFT) & 1;
}

inline
void
LqhKeyReq::setAttrLen(UintR & scanInfoAttrLen, UintR val){
  ASSERT_MAX(val, SI_ATTR_LEN_MASK, "LqhKeyReq::setAttrLen");
  scanInfoAttrLen |= (val << SI_ATTR_LEN_SHIFT);
}


inline
void
LqhKeyReq::setScanTakeOverFlag(UintR & scanInfoAttrLen, UintR val){
  ASSERT_BOOL(val, "LqhKeyReq::setScanTakeOverFlag");
  scanInfoAttrLen |= (val << SI_SCAN_TO_SHIFT);
}
inline
void

LqhKeyReq::setStoredProcFlag(UintR & scanData, UintR val){
  ASSERT_BOOL(val, "LqhKeyReq::setStoredProcFlag");
  scanData |= (val << SI_STORED_PROC_SHIFT);
}

inline
void

LqhKeyReq::setDistributionKey(UintR & scanData, UintR val){
  ASSERT_MAX(val, SI_DISTR_KEY_MASK, "LqhKeyReq::setDistributionKey");
  scanData |= (val << SI_DISTR_KEY_SHIFT);
}

#if 0  
inline
void

LqhKeyReq::setTableId(UintR & tableSchemaVersion, UintR val){
  
}
inline
void
LqhKeyReq::setSchemaVersion(UintR & tableSchemaVersion, UintR val);

inline
void
LqhKeyReq::setFragmentId(UintR & fragmentData, UintR val);

inline
void
LqhKeyReq::setNextReplicaNodeId(UintR & fragmentData, UintR val);
#endif

inline
void
LqhKeyReq::setLockType(UintR & requestInfo, UintR val){
  ASSERT_MAX(val, RI_LOCK_TYPE_MASK, "LqhKeyReq::setLockType");
  requestInfo |= (val << RI_LOCK_TYPE_SHIFT);
}

inline
void
LqhKeyReq::setDirtyFlag(UintR & requestInfo, UintR val){
  ASSERT_BOOL(val, "LqhKeyReq::setDirtyFlag");
  requestInfo |= (val << RI_DIRTY_SHIFT);
}

inline
void
LqhKeyReq::setInterpretedFlag(UintR & requestInfo, UintR val){
  ASSERT_BOOL(val, "LqhKeyReq::setInterpretedFlag");
  requestInfo |= (val << RI_INTERPRETED_SHIFT);
}

inline
void
LqhKeyReq::setSimpleFlag(UintR & requestInfo, UintR val){
  ASSERT_BOOL(val, "LqhKeyReq::setSimpleFlag");
  requestInfo |= (val << RI_SIMPLE_SHIFT);
}

inline
void
LqhKeyReq::setOperation(UintR & requestInfo, UintR val){
  ASSERT_MAX(val, RI_OPERATION_MASK, "LqhKeyReq::setOperation");
  requestInfo |= (val << RI_OPERATION_SHIFT);
}

inline
void
LqhKeyReq::setSeqNoReplica(UintR & requestInfo, UintR val){
  ASSERT_MAX(val, RI_SEQ_REPLICA_MASK, "LqhKeyReq::setSeqNoReplica");
  requestInfo |= (val << RI_SEQ_REPLICA_SHIFT);
}

inline
void
LqhKeyReq::setLastReplicaNo(UintR & requestInfo, UintR val){
  ASSERT_MAX(val, RI_LAST_REPL_MASK, "LqhKeyReq::setLastReplicaNo");
  requestInfo |= (val << RI_LAST_REPL_SHIFT);
}

inline
void
LqhKeyReq::setAIInLqhKeyReq(UintR & requestInfo, UintR val){
  ASSERT_MAX(val, RI_AI_IN_THIS_MASK, "LqhKeyReq::setAIInLqhKeyReq");
  requestInfo |= (val << RI_AI_IN_THIS_SHIFT);
}

inline
void
LqhKeyReq::setKeyLen(UintR & requestInfo, UintR val){
  ASSERT_MAX(val, RI_KEYLEN_MASK, "LqhKeyReq::setKeyLen");
  requestInfo |= (val << RI_KEYLEN_SHIFT);
}

inline
void
LqhKeyReq::setSameClientAndTcFlag(UintR & requestInfo, UintR val){
  ASSERT_BOOL(val, "LqhKeyReq::setSameClientAndTcFlag");
  requestInfo |= (val << RI_SAME_CLIENT_SHIFT);
}

inline
void
LqhKeyReq::setReturnedReadLenAIFlag(UintR & requestInfo, UintR val){
  ASSERT_BOOL(val, "LqhKeyReq::setReturnedReadLenAIFlag");
  requestInfo |= (val << RI_RETURN_AI_SHIFT);
}

inline
void
LqhKeyReq::setApplicationAddressFlag(UintR & requestInfo, UintR val){
  ASSERT_BOOL(val, "LqhKeyReq::setApplicationAddressFlag");
  requestInfo |= (val << RI_APPL_ADDR_SHIFT);
}

/**** */

inline
void
LqhKeyReq::setMarkerFlag(UintR & requestInfo, UintR val){
  ASSERT_BOOL(val, "LqhKeyReq::setMarkerFlag");
  requestInfo |= (val << RI_MARKER_SHIFT);
}

inline
UintR 
LqhKeyReq::getMarkerFlag(const UintR & requestInfo){
  return (requestInfo >> RI_MARKER_SHIFT) & 1;
}

class LqhKeyConf {
  /**
   * Reciver(s)
   */
  friend class Dbtc;

  /**
   * Sender(s)
   */
  friend class Dblqh;

  // Sent in a packed signal
  friend class PackedSignal;
  /**
   * For printing
   */
  friend bool printPACKED_SIGNAL(FILE * output, const Uint32 * theData, Uint32 len, Uint16 receiverBlockNo);
  friend bool printLQHKEYCONF(FILE * output, const Uint32 * theData, Uint32 len, Uint16 receiverBlockNo);

public:
  STATIC_CONST( SignalLength = 7 );

private:

  /**
   * DATA VARIABLES
   */
  Uint32 connectPtr;
  Uint32 opPtr;
  Uint32 userRef;
  Uint32 readLen;
  Uint32 transId1;
  Uint32 transId2;
  Uint32 noFiredTriggers;
};

class LqhKeyRef {
  /**
   * Reciver(s)
   */
  friend class Dbtc;      

  /**
   * Sender(s)
   */
  friend class Dblqh;

  /**
   * For printing
   */
  friend bool printLQHKEYREF(FILE * output, const Uint32 * theData, Uint32 len, Uint16 receiverBlockNo);

public:
  STATIC_CONST( SignalLength = 5 );

private:

  /**
   * DATA VARIABLES
   */
  Uint32 userRef;
  Uint32 connectPtr;
  Uint32 errorCode;
  Uint32 transId1;
  Uint32 transId2;
};

#endif
