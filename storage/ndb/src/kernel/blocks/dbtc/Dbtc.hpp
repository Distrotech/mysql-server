/*
   Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.

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

#ifndef DBTC_H
#define DBTC_H

#include <ndb_limits.h>
#include <pc.hpp>
#include <SimulatedBlock.hpp>
#include <DLHashTable.hpp>
#include <SLList.hpp>
#include <DLList.hpp>
#include <DLFifoList.hpp>
#include <DataBuffer.hpp>
#include <Bitmask.hpp>
#include <AttributeList.hpp>
#include <signaldata/AttrInfo.hpp>
#include <signaldata/LqhTransConf.hpp>
#include <signaldata/LqhKey.hpp>
#include <signaldata/TrigAttrInfo.hpp>
#include <signaldata/TcIndx.hpp>
#include <signaldata/TransIdAI.hpp>
#include <signaldata/EventReport.hpp>
#include <trigger_definitions.h>
#include <SignalCounter.hpp>

#ifdef DBTC_C
/*
 * 2.2 LOCAL SYMBOLS
 * -----------------
 */
#define Z8NIL 255
#define ZAPI_CONNECT_FILESIZE 20
#define ZCLOSED 2
#define ZCOMMITING 0			 /* VALUE FOR TRANSTATUS        */
#define ZCOMMIT_SETUP 2
#define ZCONTINUE_ABORT_080 4
#define ZGCP_FILESIZE 10
#define ZINTSPH1 1
#define ZINTSPH2 2
#define ZINTSPH3 3
#define ZINTSPH6 6
#define ZLASTPHASE 255
#define ZNODEBUF_FILESIZE 2000
#define ZNR_OF_SEIZE 10
#define ZSCANREC_FILE_SIZE 100
#define ZSCAN_FRAGREC_FILE_SIZE 400
#define ZSCAN_OPREC_FILE_SIZE 400
#define ZSPH1 1
#define ZTABREC_FILESIZE 16
#define ZTAKE_OVER_ACTIVE 1
#define ZTAKE_OVER_IDLE 0
#define ZTC_CONNECT_FILESIZE 200
#define ZTCOPCONF_SIZE 6

// ----------------------------------------
// Error Codes for Scan
// ----------------------------------------
#define ZNO_CONCURRENCY_ERROR 242
#define ZTOO_HIGH_CONCURRENCY_ERROR 244
#define ZNO_SCANREC_ERROR 245
#define ZNO_FRAGMENT_ERROR 246
#define ZSCAN_AI_LEN_ERROR 269
#define ZSCAN_LQH_ERROR 270
#define ZSCAN_FRAG_LQH_ERROR 274

#define ZSCANTIME_OUT_ERROR 296
#define ZSCANTIME_OUT_ERROR2 297

// ----------------------------------------
// Error Codes for transactions
// ----------------------------------------
#define ZSTATE_ERROR 202
#define ZLENGTH_ERROR 207 // Also Scan
#define ZERO_KEYLEN_ERROR 208
#define ZSIGNAL_ERROR 209
#define ZGET_ATTRBUF_ERROR 217 // Also Scan
#define ZGET_DATAREC_ERROR 218
#define ZMORE_AI_IN_TCKEYREQ_ERROR 220
#define ZCOMMITINPROGRESS 230
#define ZROLLBACKNOTALLOWED 232
#define ZNO_FREE_TC_CONNECTION 233 // Also Scan
#define ZABORTINPROGRESS 237
#define ZPREPAREINPROGRESS 238
#define ZWRONG_SCHEMA_VERSION_ERROR 241 // Also Scan
#define ZSCAN_NODE_ERROR 250
#define ZTRANS_STATUS_ERROR 253
#define ZTIME_OUT_ERROR 266
#define ZSIMPLE_READ_WITHOUT_AI 271
#define ZNO_AI_WITH_UPDATE 272
#define ZSEIZE_API_COPY_ERROR 275
#define ZSCANINPROGRESS 276
#define ZABORT_ERROR 277
#define ZCOMMIT_TYPE_ERROR 278

#define ZNO_FREE_TC_MARKER 279
#define ZNODE_SHUTDOWN_IN_PROGRESS 280
#define ZCLUSTER_SHUTDOWN_IN_PROGRESS 281
#define ZWRONG_STATE 282
#define ZCLUSTER_IN_SINGLEUSER_MODE 299

#define ZDROP_TABLE_IN_PROGRESS 283
#define ZNO_SUCH_TABLE 284
#define ZUNKNOWN_TABLE_ERROR 285
#define ZNODEFAIL_BEFORE_COMMIT 286
#define ZINDEX_CORRUPT_ERROR 287
#define ZSCAN_FRAGREC_ERROR 291

// ----------------------------------------
// Seize error
// ----------------------------------------
#define ZNO_FREE_API_CONNECTION 219
#define ZSYSTEM_NOT_STARTED_ERROR 203

// ----------------------------------------
// Release errors
// ----------------------------------------
#define ZINVALID_CONNECTION 229


#define ZNOT_FOUND 626
#define ZALREADYEXIST 630
#define ZNOTUNIQUE 893

#define ZINVALID_KEY 290
#define ZUNLOCKED_IVAL_TOO_HIGH 294
#define ZUNLOCKED_OP_HAS_BAD_STATE 295 
#define ZBAD_DIST_KEY 298
#endif

class Dbtc: public SimulatedBlock {
public:
  enum ConnectionState {
    CS_CONNECTED = 0,
    CS_DISCONNECTED = 1,
    CS_STARTED = 2,
    CS_RECEIVING = 3,
    CS_PREPARED = 4,
    CS_START_PREPARING = 5,
    CS_REC_PREPARING = 6,
    CS_RESTART = 7,
    CS_ABORTING = 8,
    CS_COMPLETING = 9,
    CS_COMPLETE_SENT = 10,
    CS_PREPARE_TO_COMMIT = 11,
    CS_COMMIT_SENT = 12,
    CS_START_COMMITTING = 13,
    CS_COMMITTING = 14,
    CS_REC_COMMITTING = 15,
    CS_WAIT_ABORT_CONF = 16,
    CS_WAIT_COMPLETE_CONF = 17,
    CS_WAIT_COMMIT_CONF = 18,
    CS_FAIL_ABORTING = 19,
    CS_FAIL_ABORTED = 20,
    CS_FAIL_PREPARED = 21,
    CS_FAIL_COMMITTING = 22,
    CS_FAIL_COMMITTED = 23,
    CS_FAIL_COMPLETED = 24,
    CS_START_SCAN = 25
  };

  enum OperationState {
    OS_CONNECTED = 1,
    OS_OPERATING = 2,
    OS_PREPARED = 3,
    OS_COMMITTING = 4,
    OS_COMMITTED = 5,
    OS_COMPLETING = 6,
    OS_COMPLETED = 7,
    OS_RESTART = 8,
    OS_ABORTING = 9,
    OS_ABORT_SENT = 10,
    OS_TAKE_OVER = 11,
    OS_WAIT_DIH = 12,
    OS_WAIT_KEYINFO = 13,
    OS_WAIT_ATTR = 14,
    OS_WAIT_COMMIT_CONF = 15,
    OS_WAIT_ABORT_CONF = 16,
    OS_WAIT_COMPLETE_CONF = 17,
    OS_WAIT_SCAN = 18
  };

  enum AbortState {
    AS_IDLE = 0,
    AS_ACTIVE = 1
  };

  enum HostState {
    HS_ALIVE = 0,
    HS_DEAD = 1
  };

  enum LqhTransState {
    LTS_IDLE = 0,
    LTS_ACTIVE = 1
  };

  enum FailState {
    FS_IDLE = 0,
    FS_LISTENING = 1,
    FS_COMPLETING = 2
  };

  enum SystemStartState {
    SSS_TRUE = 0,
    SSS_FALSE = 1
  };

  enum TimeOutCheckState {
    TOCS_TRUE = 0,
    TOCS_FALSE = 1
  };

  enum ReturnSignal {
    RS_NO_RETURN = 0,
    RS_TCKEYCONF = 1,
    RS_TC_COMMITCONF = 3,
    RS_TCROLLBACKCONF = 4,
    RS_TCROLLBACKREP = 5
  };
  
  enum IndexOperationState {
    IOS_NOOP = 0,
    IOS_INDEX_ACCESS = 1,
    IOS_INDEX_ACCESS_WAIT_FOR_TCKEYCONF = 2,
    IOS_INDEX_ACCESS_WAIT_FOR_TRANSID_AI = 3,
    IOS_INDEX_OPERATION = 4
  };
  
  enum IndexState {
    IS_BUILDING = 0,          // build in progress, start state at create
    IS_ONLINE = 1             // ready to use
  };

  /* Sub states of IndexOperation while waiting for TransId_AI
   * from index table lookup
   */
  enum IndexTransIdAIState {
    ITAS_WAIT_HEADER   = 0,     // Initial state
    ITAS_WAIT_FRAGID   = 1,     // Waiting for fragment id word
    ITAS_WAIT_KEY      = 2,     // Waiting for (more) key information
    ITAS_ALL_RECEIVED  = 3,     // All TransIdAI info received
    ITAS_WAIT_KEY_FAIL = 4     // Failed collecting key
  };
  

  /**--------------------------------------------------------------------------
   * LOCAL SYMBOLS PER 'SYMBOL-VALUED' VARIABLE
   *
   *
   *            NSYMB ZAPI_CONNECT_FILESIZE = 20
   *            NSYMB ZTC_CONNECT_FILESIZE  = 200
   *            NSYMB ZHOST_FILESIZE        = 16  
   *            NSYMB ZDATABUF_FILESIZE     = 4000
   *            NSYMB ZATTRBUF_FILESIZE     = 4000 
   *            NSYMB ZGCP_FILESIZE         = 10 
   *
   *
   *  ABORTED CODES
   *  TPHASE    NSYMB ZSPH1 = 1
   *            NSYMB ZLASTPHASE = 255
   *
   * 
   * LQH_TRANS
   *       NSYMB ZTRANS_ABORTED = 1 
   *       NSYMB ZTRANS_PREPARED = 2 
   *       NSYMB ZTRANS_COMMITTED = 3
   *       NSYMB ZCOMPLETED_LQH_TRANS = 4
   *       NSYMB ZTRANS_COMPLETED = 5 
   *
   * 
   * TAKE OVER 
   *       NSYMB ZTAKE_OVER_IDLE = 0
   *       NSYMB ZTAKE_OVER_ACTIVE = 1
   * 
   * ATTRBUF (ATTRBUF_RECORD)
   *          NSYMB ZINBUF_DATA_LEN = 24
   *          NSYMB ZINBUF_NEXTFREE = 25    (NOT USED )
   *          NSYMB ZINBUF_PREV = 26   
   *          NSYMB ZINBUF_NEXT = 27    
   -------------------------------------------------------------------------*/
  /*
    2.3 RECORDS AND FILESIZES
    -------------------------
  */
  /* **************************************************************** */
  /* ---------------------------------------------------------------- */
  /* ------------------- TRIGGER AND INDEX DATA --------------------- */
  /* ---------------------------------------------------------------- */
  /* **************************************************************** */
  /* ********* DEFINED TRIGGER DATA ********* */
  /* THIS RECORD FORMS LISTS OF ACTIVE        */
  /* TRIGGERS FOR EACH TABLE.                 */
  /* THE RECORDS ARE MANAGED BY A TRIGGER     */
  /* POOL WHERE A TRIGGER RECORD IS SEIZED    */
  /* WHEN A TRIGGER IS ACTIVATED AND RELEASED */
  /* WHEN THE TRIGGER IS DEACTIVATED.         */
  /* **************************************** */
  struct TcDefinedTriggerData {
    TcDefinedTriggerData() {}
    /**
     * Trigger id, used to identify the trigger
     */
    UintR triggerId;
    
    /**
     * Trigger type, defines what the trigger is used for
     */
    TriggerType::Value triggerType;
    
    /**
     * Trigger type, defines what the trigger is used for
     */
    TriggerEvent::Value triggerEvent;
    
    /**
     * Next ptr (used in pool/list)
     */
    union {
      Uint32 nextPool;
      Uint32 nextList;
    };
    
    /**
     * Index id, only used by secondary_index triggers.  This is same as
     * index table id in DICT.
     **/
    union {
      Uint32 indexId; // For unique index trigger
      Uint32 tableId; // For reorg trigger
    };

    /**
     * Prev pointer (used in list)
     */
    Uint32 prevList;

    Uint32 oldTriggerIds[2]; // For upgrade :(

    inline void print(NdbOut & s) const { 
      s << "[DefinedTriggerData = " << triggerId << "]"; 
    }
  };
  typedef Ptr<TcDefinedTriggerData> DefinedTriggerPtr;
  
  /**
   * Pool of trigger data record
   */
  ArrayPool<TcDefinedTriggerData> c_theDefinedTriggerPool;

  /**
   * The list of active triggers
   */  
  DLList<TcDefinedTriggerData> c_theDefinedTriggers;

  typedef DataBuffer<11> AttributeBuffer;
 
  AttributeBuffer::DataBufferPool c_theAttributeBufferPool;

  UintR c_transactionBufferSpace;


  /* ********** FIRED TRIGGER DATA ********** */
  /* THIS RECORD FORMS LISTS OF FIRED         */
  /* TRIGGERS FOR A TRANSACTION.              */
  /* THE RECORDS ARE MANAGED BY A TRIGGER     */
  /* POOL WHERE A TRIGGER RECORD IS SEIZED    */
  /* WHEN A TRIGGER IS ACTIVATED AND RELEASED */
  /* WHEN THE TRIGGER IS DEACTIVATED.         */
  /* **************************************** */
  struct TcFiredTriggerData {
    TcFiredTriggerData() {}

    /**
     * Trigger id, used to identify the trigger
     **/
    Uint32 triggerId;
    
    /**
     * The operation that fired the trigger
     */
    Uint32 fireingOperation;

    /**
     * The fragment id of the firing operation. This will be appended
     * to the Primary Key such that the record can be found even in the
     * case of user defined partitioning.
     */
    Uint32 fragId;

    /**
     * Used for scrapping in case of node failure
     */
    Uint32 nodeId;

    /**
     * Trigger type, defines what the trigger is used for
     */
    TriggerType::Value triggerType;

    /**
     * Trigger type, defines what the trigger is used for
     */
    TriggerEvent::Value triggerEvent;

    /**
     * Trigger attribute info, primary key value(s)
     */
    AttributeBuffer::Head keyValues;
    
    /**
     * Trigger attribute info, attribute value(s) before operation
     */
    AttributeBuffer::Head beforeValues;
    
    /**
     * Trigger attribute info, attribute value(s) after operation
     */
    AttributeBuffer::Head afterValues;

    /**
     * Next ptr (used in pool/list)
     */
    union {
      Uint32 nextPool;
      Uint32 nextList;
      Uint32 nextHash;
    };
    
    /**
     * Prev pointer (used in list)
     */
    union {
      Uint32 prevList;
      Uint32 prevHash;
    };
    
    inline void print(NdbOut & s) const { 
      s << "[FiredTriggerData = " << triggerId << "]"; 
    }

    inline Uint32 hashValue() const {
      return fireingOperation ^ nodeId;
    }

    inline bool equal(const TcFiredTriggerData & rec) const {
      return fireingOperation == rec.fireingOperation && nodeId == rec.nodeId;
    }
  };
  typedef Ptr<TcFiredTriggerData> FiredTriggerPtr;
  
  /**
   * Pool of trigger data record
   */
  ArrayPool<TcFiredTriggerData> c_theFiredTriggerPool;
  DLHashTable<TcFiredTriggerData> c_firedTriggerHash;
  AttributeBuffer::DataBufferPool c_theTriggerAttrInfoPool;

  Uint32 c_maxNumberOfDefinedTriggers;
  Uint32 c_maxNumberOfFiredTriggers;

  struct AttrInfoRecord {
    /**
     * Pre-allocated AttrInfo signal
     */
    AttrInfo attrInfo;

    /**
     * Next ptr (used in pool/list)
     */
    union {
      Uint32 nextPool;
      Uint32 nextList;
    };
    /**
     * Prev pointer (used in list)
     */
    Uint32 prevList;
  };


  /* ************* INDEX DATA *************** */
  /* THIS RECORD FORMS LISTS OF ACTIVE        */
  /* INDEX FOR EACH TABLE.                    */
  /* THE RECORDS ARE MANAGED BY A INDEX       */
  /* POOL WHERE AN INDEX RECORD IS SEIZED     */
  /* WHEN AN INDEX IS CREATED AND RELEASED    */
  /* WHEN THE INDEX IS DROPPED.               */
  /* **************************************** */
  struct TcIndexData {
    TcIndexData() {}

    /**
     *  IndexState
     */
    IndexState indexState;

    /**
     * Index id, same as index table id in DICT
     */
    Uint32 indexId;
    
    /**
     * Index attribute list.  Only the length is used in v21x.
     */
    IndexAttributeList attributeList;

    /**
     * Primary table id, the primary table to be indexed
     */
    Uint32 primaryTableId;

    /**
     *  Primary key position in secondary table
     */
    Uint32 primaryKeyPos;

    /**
     * Next ptr (used in pool/list)
     */
    union {
      Uint32 nextPool;
      Uint32 nextList;
    };
    /**
     * Prev pointer (used in list)
     */
    Uint32 prevList;
  };
  
  typedef Ptr<TcIndexData> TcIndexDataPtr;
  
  /**
   * Pool of index data record
   */
  ArrayPool<TcIndexData> c_theIndexPool;
  
  /**
   * The list of defined indexes
   */  
  DLList<TcIndexData> c_theIndexes;
  UintR c_maxNumberOfIndexes;

  struct TcIndexOperation {
    TcIndexOperation() :
      indexOpState(IOS_NOOP),
      pendingKeyInfo(0),
      keyInfoSectionIVal(RNIL),
      pendingAttrInfo(0),
      attrInfoSectionIVal(RNIL),
      transIdAIState(ITAS_WAIT_HEADER),
      pendingTransIdAI(0),
      transIdAISectionIVal(RNIL),
      indexReadTcConnect(RNIL)
    {}

    ~TcIndexOperation()
    {
    }
    
    // Index data
    Uint32 indexOpId;
    IndexOperationState indexOpState; // Used to mark on-going TcKeyReq
    Uint32 pendingKeyInfo;
    Uint32 keyInfoSectionIVal;
    Uint32 pendingAttrInfo;
    Uint32 attrInfoSectionIVal;
    IndexTransIdAIState transIdAIState;
    Uint32 pendingTransIdAI;
    Uint32 transIdAISectionIVal; // For accumulating TransId_AI
    Uint32 fragmentId;
    
    TcKeyReq tcIndxReq;
    UintR connectionIndex;
    UintR indexReadTcConnect; //
    
    /**
     * Next ptr (used in pool/list)
     */
    union {
      Uint32 nextPool;
      Uint32 nextList;
    };
    /**
     * Prev pointer (used in list)
     */
    Uint32 prevList;
  };
  
  typedef Ptr<TcIndexOperation> TcIndexOperationPtr;
  
  /**
   * Pool of index data record
   */
  ArrayPool<TcIndexOperation> c_theIndexOperationPool;

  UintR c_maxNumberOfIndexOperations;   

  /************************** API CONNECT RECORD ***********************
   * The API connect record contains the connection record to which the
   * application connects.  
   *
   * The application can send one operation at a time.  It can send a 
   * new operation immediately after sending the previous operation. 
   * Thereby several operations can be active in one transaction within TC. 
   * This is achieved by using the API connect record. 
   * Each active operation is handled by the TC connect record. 
   * As soon as the TC connect record has sent the     
   * request to the LQH it is ready to receive new operations. 
   * The LQH connect record takes care of waiting for an operation to      
   * complete.  
   * When an operation has completed on the LQH connect record, 
   * a new operation can be started on this LQH connect record. 
   *******************************************************************
   *                                                                   
   *       API CONNECT RECORD ALIGNED TO BE 256 BYTES                  
   ********************************************************************/
  
  /*******************************************************************>*/
  // We break out the API Timer for optimisation on scanning rather than
  // on fast access.
  /*******************************************************************>*/
  inline void setApiConTimer(Uint32 apiConPtrI, Uint32 value, Uint32 line){
    c_apiConTimer[apiConPtrI] = value;
    c_apiConTimer_line[apiConPtrI] = line;
  }

  inline Uint32 getApiConTimer(Uint32 apiConPtrI) const {
    return c_apiConTimer[apiConPtrI];
  }
  UintR* c_apiConTimer;
  UintR* c_apiConTimer_line;

  struct ApiConnectRecord {
    ApiConnectRecord(ArrayPool<TcFiredTriggerData> & firedTriggerPool,
		     ArrayPool<TcIndexOperation> & seizedIndexOpPool):
      m_special_op_flags(0),
      theFiredTriggers(firedTriggerPool),
      theSeizedIndexOperations(seizedIndexOpPool) 
    {}
    
    //---------------------------------------------------
    // First 16 byte cache line. Hot variables.
    //---------------------------------------------------
    ConnectionState apiConnectstate;
    UintR transid[2];
    UintR firstTcConnect;
    
    //---------------------------------------------------
    // Second 16 byte cache line. Hot variables.
    //---------------------------------------------------
    UintR lqhkeyconfrec;
    UintR cachePtr;
    UintR currSavePointId;
    UintR counter;
    
    //---------------------------------------------------
    // Third 16 byte cache line. First and second cache
    // line plus this will be enough for copy API records.
    // Variables used in late phases.
    //---------------------------------------------------
    UintR nextGcpConnect;
    UintR prevGcpConnect;
    UintR gcpPointer;
    UintR ndbapiConnect;
    
    //---------------------------------------------------
    // Fourth 16 byte cache line. Only used in late phases.
    // Plus 4 bytes of error handling.
    //---------------------------------------------------
    UintR nextApiConnect;
    BlockReference ndbapiBlockref;
    UintR apiCopyRecord;
    Uint64 globalcheckpointid;
    
    //---------------------------------------------------
    // Second 64 byte cache line starts. First 16 byte
    // cache line in this one. Variables primarily used
    // in early phase.
    //---------------------------------------------------
    UintR lastTcConnect;
    UintR lqhkeyreqrec;
    Uint32 buddyPtr;
    union {
      UintR apiScanRec;
      UintR commitAckMarker;
    };

    Uint32 no_commit_ack_markers;
    ReturnSignal returnsignal;
    AbortState abortState;

    Uint8 indexOpReturn;
    Uint8 triggerPending; // Used to mark waiting for a CONTINUEB

    Uint8 m_exec_flag;
    Uint8 m_special_op_flags; // Used to mark on-going TcKeyReq as indx table

    Uint8 takeOverRec;
    Uint8 currentReplicaNo;
    Uint8 m_commit_ack_marker_received;

    Uint8 tckeyrec; // Changed from R

    Uint8 tcindxrec;
    Uint8 apiFailState; // Changed R
    Uint8 timeOutCounter;
    Uint8 singleUserMode;
    
    Uint16 returncode;
    Uint16 takeOverInd;
    //---------------------------------------------------
    // Error Handling variables. If cache line 32 bytes
    // ensures that cache line is still only read in
    // early phases.
    //---------------------------------------------------
    UintR currentTcConnect;
    BlockReference tcBlockref;
    UintR failureNr;
    
    //---------------------------------------------------
    // Second 64 byte cache line. Third 16 byte cache line
    // in this one. Variables primarily used in early phase
    // and checked in late phase.
    // Fourth cache line is the tcSendArray that is used
    // when two and three operations are responded to in
    // parallel. The first two entries in tcSendArray is
    // part of the third cache line.
    //---------------------------------------------------
    //---------------------------------------------------
    // timeOutCounter is used waiting for ABORTCONF, COMMITCONF
    // and COMPLETECONF
    //---------------------------------------------------
    UintR tcSendArray[6];
    NdbNodeBitmask m_transaction_nodes; 
    
    // Trigger data
    
    /**
     * The list of fired triggers
     */  
    DLFifoList<TcFiredTriggerData> theFiredTriggers;
    
    
    // Index data
    
    UintR noIndexOp;     // No outstanding index ops

    // Index op return context
    UintR indexOp;
    UintR clientData;
    Uint32 errorData;
    UintR attrInfoLen;
    Uint32 immediateTriggerId;  // Id of trigger op being fired NOW
    
    UintR accumulatingIndexOp;
    UintR executingIndexOp;
    UintR tcIndxSendArray[6];
    DLList<TcIndexOperation> theSeizedIndexOperations;

#ifdef ERROR_INSERT
    Uint32 continueBCount;  // ERROR_INSERT 8082
#endif
  };
  
  typedef Ptr<ApiConnectRecord> ApiConnectRecordPtr;


  /************************** TC CONNECT RECORD ************************/
  /* *******************************************************************/
  /* TC CONNECT RECORD KEEPS ALL INFORMATION TO CARRY OUT A TRANSACTION*/
  /* THE TRANSACTION CONTROLLER ESTABLISHES CONNECTIONS TO DIFFERENT   */
  /* BLOCKS TO CARRY OUT THE TRANSACTION. THERE CAN BE SEVERAL RECORDS */
  /* PER ACTIVE TRANSACTION. THE TC CONNECT RECORD COOPERATES WITH THE */
  /* API CONNECT RECORD FOR COMMUNICATION WITH THE API AND WITH THE    */
  /* LQH CONNECT RECORD FOR COMMUNICATION WITH THE LQH'S INVOLVED IN   */
  /* THE TRANSACTION. TC CONNECT RECORD IS PERMANENTLY CONNECTED TO A  */
  /* RECORD IN DICT AND ONE IN DIH. IT CONTAINS A LIST OF ACTIVE LQH   */
  /* CONNECT RECORDS AND A LIST OF STARTED BUT NOT ACTIVE LQH CONNECT  */
  /* RECORDS. IT DOES ALSO CONTAIN A LIST OF ALL OPERATIONS THAT ARE   */
  /* EXECUTED WITH THE TC CONNECT RECORD.                              */
  /*******************************************************************>*/
  /*       TC_CONNECT RECORD ALIGNED TO BE 128 BYTES                   */
  /*******************************************************************>*/
  struct TcConnectRecord {
    //---------------------------------------------------
    // First 16 byte cache line. Those variables are only
    // used in error cases.
    //---------------------------------------------------
    UintR  tcOprec;      /* TC OPREC of operation being taken over       */
    Uint16 failData[4];  /* Failed nodes when taking over an operation   */
    UintR  nextTcFailHash;
    
    //---------------------------------------------------
    // Second 16 byte cache line. Those variables are used
    // from LQHKEYCONF to sending COMMIT and COMPLETED.
    //---------------------------------------------------
    UintR lastLqhCon;        /* Connect record in last replicas Lqh record   */
    Uint16 lastLqhNodeId;    /* Node id of last replicas Lqh                 */
    Uint16 m_execAbortOption;/* TcKeyReq::ExecuteAbortOption */
    UintR  commitAckMarker;  /* CommitMarker I value */
    
    //---------------------------------------------------
    // Third 16 byte cache line. The hottest variables.
    //---------------------------------------------------
    OperationState tcConnectstate;         /* THE STATE OF THE CONNECT*/
    UintR apiConnect;                      /* POINTER TO API CONNECT RECORD */
    UintR nextTcConnect;                   /* NEXT TC RECORD*/
    Uint8 dirtyOp;
    Uint8 opSimple;   
    Uint8 lastReplicaNo;     /* NUMBER OF THE LAST REPLICA IN THE OPERATION */
    Uint8 noOfNodes;         /* TOTAL NUMBER OF NODES IN OPERATION          */
    Uint8 operation;         /* OPERATION TYPE                              */
                             /* 0 = READ REQUEST                            */
                             /* 1 = UPDATE REQUEST                          */
                             /* 2 = INSERT REQUEST                          */
                             /* 3 = DELETE REQUEST                          */
    Uint8 m_special_op_flags; // See ApiConnectRecord::SpecialOpFlags
    enum SpecialOpFlags {
      SOF_NORMAL = 0,
      SOF_INDEX_TABLE_READ = 1,       // Read index table
      SOF_INDEX_BASE_TABLE_ACCESS = 2,// Execute on "real" table
      SOF_REORG_TRIGGER_BASE = 4,
      SOF_REORG_TRIGGER = 4 | 16,     // A reorg trigger
      SOF_REORG_MOVING = 8,           // A record that should be moved
      SOF_TRIGGER = 16,               // A trigger
      SOF_REORG_COPY = 32,
      SOF_REORG_DELETE = 64
    };
    
    static inline bool isIndexOp(Uint8 flags) {
      return
        flags == SOF_INDEX_TABLE_READ ||
        flags == SOF_INDEX_BASE_TABLE_ACCESS;
    }

    //---------------------------------------------------
    // Fourth 16 byte cache line. The mildly hot variables.
    // tcNodedata expands 4 Bytes into the next cache line
    // with indexes almost never used.
    //---------------------------------------------------
    UintR clientData;           /* SENDERS OPERATION POINTER              */
    UintR prevTcConnect;        /* DOUBLY LINKED LIST OF TC CONNECT RECORDS*/
    UintR savePointId;

    Uint16 tcNodedata[4];
    /* Instance key to send to LQH.  Receiver maps it to actual instance. */
    Uint16 lqhInstanceKey;
    
    // Trigger data
    FiredTriggerPtr accumulatingTriggerData;
    UintR noFiredTriggers;
    UintR noReceivedTriggers;
    UintR triggerExecutionCount;
    UintR triggeringOperation;
    UintR savedState[LqhKeyConf::SignalLength];
    
    // Index data
    UintR indexOp;
    UintR currentTriggerId;
    UintR attrInfoLen;
  };
  
  friend struct TcConnectRecord;
  
  typedef Ptr<TcConnectRecord> TcConnectRecordPtr;
  
  // ********************** CACHE RECORD **************************************
  //---------------------------------------------------------------------------
  // This record is used between reception of TCKEYREQ and sending of LQHKEYREQ
  // It is separatedso as to improve the cache hit rate and also to minimise 
  // the necessary memory storage in NDB Cluster.
  //---------------------------------------------------------------------------

  struct CacheRecord {
    /* Fields used by TCKEYREQ/TCINDXREQ/SCANTABREQ */
      Uint32 keyInfoSectionI;   /* KeyInfo section I-val */
      Uint32 attrInfoSectionI;  /* AttrInfo section I-val */

      // TODO : Consider using section length + other vars for this 
      UintR  currReclenAi;      /* AttrInfo words received so far */
      Uint16 attrlength;        /* Total AttrInfo length */
      Uint16 save1;             /* KeyInfo words received so far */
      Uint16 keylen;            /* KEY LENGTH SENT BY REQUEST SIGNAL */
    
      /* Distribution information */
      // TODO : Consider placing this info into other records
      Uint8  distributionKeyIndicator;
      Uint8  viaSPJFlag;        /* Send request via the SPJ block.*/
      UintR  distributionKey;
    /* End of fields used by TCKEYREQ/TCINDXREQ/SCANTABREQ */
    

    /* TCKEYREQ/TCINDXREQ only fields */
      UintR  schemaVersion;/* SCHEMA VERSION USED IN TRANSACTION         */
      UintR  tableref;     /* POINTER TO THE TABLE IN WHICH THE FRAGMENT EXISTS*/
      Uint16 apiVersionNo;
    
      UintR  fragmentid;   /* THE COMPUTED FRAGMENT ID                     */
      UintR  hashValue;    /* THE HASH VALUE USED TO LOCATE FRAGMENT       */
    
      Uint8  m_special_hash; // collation or distribution key
      Uint8  m_no_hash;      // Hash not required for LQH (special variant)
      Uint8  m_no_disk_flag; 
      Uint8  m_op_queue;
      Uint8  lenAiInTckeyreq;  /* LENGTH OF ATTRIBUTE INFORMATION IN TCKEYREQ */
    
      Uint8  fragmentDistributionKey;  /* DIH generation no */
    
      /**
       * EXECUTION MODE OF OPERATION                    
       * 0 = NORMAL EXECUTION, 1 = INTERPRETED EXECUTION
       */
      Uint8  opExec;     
    
      /* Use of Long signals */
      Uint8  isLongTcKeyReq;   /* Incoming TcKeyReq used long signal */
      Uint8  useLongLqhKeyReq; /* Outgoing LqhKeyReq should be long */
    
      UintR  nextCacheRec;
      Uint32 scanInfo;
    
      Uint32 scanTakeOverInd;
      Uint32 unlockNodeId;     /* NodeId for unlock operation */
    /* End of TCKEYREQ/TCINDXREQ only fields */

  };
  
  typedef Ptr<CacheRecord> CacheRecordPtr;
  
  /* ************************ HOST RECORD ********************************** */
  /********************************************************/
  /* THIS RECORD CONTAINS ALIVE-STATUS ON ALL NODES IN THE*/
  /* SYSTEM                                               */
  /********************************************************/
  /*       THIS RECORD IS ALIGNED TO BE 128 BYTES.        */
  /********************************************************/
  struct HostRecord {
    HostState hostStatus;
    LqhTransState lqhTransStatus;
    bool  inPackedList;
    UintR noOfPackedWordsLqh;
    UintR packedWordsLqh[26];
    UintR noOfWordsTCKEYCONF;
    UintR packedWordsTCKEYCONF[30];
    UintR noOfWordsTCINDXCONF;
    UintR packedWordsTCINDXCONF[30];
    BlockReference hostLqhBlockRef;

    enum NodeFailBits
    {
      NF_TAKEOVER          = 0x1,
      NF_CHECK_SCAN        = 0x2,
      NF_CHECK_TRANSACTION = 0x4,
      NF_BLOCK_HANDLE      = 0x8,
      NF_NODE_FAIL_BITS    = 0xF // All bits...
    };
    Uint32 m_nf_bits;
    NdbNodeBitmask m_lqh_trans_conf;
  }; /* p2c: size = 128 bytes */
  
  typedef Ptr<HostRecord> HostRecordPtr;
  
  /* *********** TABLE RECORD ********************************************* */

  /********************************************************/
  /* THIS RECORD CONTAINS THE CURRENT SCHEMA VERSION OF   */
  /* ALL TABLES IN THE SYSTEM.                            */
  /********************************************************/
  struct TableRecord {
    TableRecord() {}
    Uint32 currentSchemaVersion;
    Uint16 m_flags;
    Uint8 tableType;
    Uint8 singleUserMode;

    enum {
      TR_ENABLED      = 1 << 0,
      TR_DROPPING     = 1 << 1,
      TR_STORED_TABLE = 1 << 2,
      TR_PREPARED     = 1 << 3
      ,TR_USER_DEFINED_PARTITIONING = 1 << 4
    };
    Uint8 get_enabled()     const { return (m_flags & TR_ENABLED)      != 0; }
    Uint8 get_dropping()    const { return (m_flags & TR_DROPPING)     != 0; }
    Uint8 get_storedTable() const { return (m_flags & TR_STORED_TABLE) != 0; }
    Uint8 get_prepared()    const { return (m_flags & TR_PREPARED)     != 0; }
    void set_enabled(Uint8 f)     { f ? m_flags |= (Uint16)TR_ENABLED      : m_flags &= ~(Uint16)TR_ENABLED; }
    void set_dropping(Uint8 f)    { f ? m_flags |= (Uint16)TR_DROPPING     : m_flags &= ~(Uint16)TR_DROPPING; }
    void set_storedTable(Uint8 f) { f ? m_flags |= (Uint16)TR_STORED_TABLE : m_flags &= ~(Uint16)TR_STORED_TABLE; }
    void set_prepared(Uint8 f)    { f ? m_flags |= (Uint16)TR_PREPARED : m_flags &= ~(Uint16)TR_PREPARED; }

    Uint8 get_user_defined_partitioning() const {
      return (m_flags & TR_USER_DEFINED_PARTITIONING) != 0;
    }

    void set_user_defined_partitioning(Uint8 f) {
      f ?
        m_flags |= (Uint16)TR_USER_DEFINED_PARTITIONING :
        m_flags &= ~(Uint16)TR_USER_DEFINED_PARTITIONING;
    }

    Uint8 noOfKeyAttr;
    Uint8 hasCharAttr;
    Uint8 noOfDistrKeys;
    Uint8 hasVarKeys;

    bool checkTable(Uint32 schemaVersion) const {
      return !get_dropping() &&
	((/** normal transaction path */
          get_enabled() && table_version_major(schemaVersion) == table_version_major(currentSchemaVersion)) 
         ||
         (/** 
           * unique index is relaxed for DbUtil and transactions ongoing
           * while index is created
           */
          get_prepared() && schemaVersion == currentSchemaVersion &&
          DictTabInfo::isUniqueIndex(tableType)));
    }
    
    Uint32 getErrorCode(Uint32 schemaVersion) const;
  };
  typedef Ptr<TableRecord> TableRecordPtr;

  /**
   * There is max 16 ScanFragRec's for 
   * each scan started in TC. Each ScanFragRec is used by
   * a scan fragment "process" that scans one fragment at a time. 
   * It will receive max 16 tuples in each request
   */
  struct ScanFragRec {
    ScanFragRec(){ 
      stopFragTimer();
      lqhBlockref = 0;
      scanFragState = IDLE;
      scanRec = RNIL;
    }
    /**
     * ScanFragState      
     *  WAIT_GET_PRIMCONF : Waiting for DIGETPRIMCONF when starting a new 
     *   fragment scan
     *  LQH_ACTIVE : The scan process has sent a command to LQH and is
     *   waiting for the response
     *  LQH_ACTIVE_CLOSE : The scan process has sent close to LQH and is
     *   waiting for the response
     *  DELIVERED : The result have been delivered, this scan frag process 
     *   are waiting for a SCAN_NEXTREQ to tell us to continue scanning
     *  RETURNING_FROM_DELIVERY : SCAN_NEXTREQ received and continuing scan
     *   soon 
     *  QUEUED_FOR_DELIVERY : Result queued in TC and waiting for delivery
     *   to API
     *  COMPLETED : The fragment scan processes has completed and finally
     *    sent a SCAN_PROCCONF
     */
    enum ScanFragState {
      IDLE = 0,
      WAIT_GET_PRIMCONF = 1,
      LQH_ACTIVE = 2,
      DELIVERED = 4,
      QUEUED_FOR_DELIVERY = 6,
      COMPLETED = 7
    };
    // Timer for checking timeout of this fragment scan
    Uint32  scanFragTimer;

    // Id of the current scanned fragment
    Uint32 scanFragId;

    // Blockreference of LQH 
    BlockReference lqhBlockref;

    // getNodeInfo.m_connectCount, set at seize used so that
    // I don't accidently kill a starting node
    Uint32 m_connectCount;

    // State of this fragment scan
    ScanFragState scanFragState;

    // Id of the ScanRecord this fragment scan belongs to
    Uint32 scanRec;

    // The value of fragmentCompleted in the last received SCAN_FRAGCONF
    Uint8 m_scan_frag_conf_status;

    inline void startFragTimer(Uint32 timeVal){
      scanFragTimer = timeVal;
    }
    inline void stopFragTimer(void){
      scanFragTimer = 0;
    }

    Uint32 m_ops;
    Uint32 m_chksum;
    Uint32 m_apiPtr;
    Uint32 m_totalLen;
    union {
      Uint32 nextPool;
      Uint32 nextList;
    };
    Uint32 prevList;
  };
  
  typedef Ptr<ScanFragRec> ScanFragRecPtr;
  typedef LocalDLList<ScanFragRec> ScanFragList;

  /**
   * Each scan allocates one ScanRecord to store information 
   * about the current scan
   *
   */
  struct ScanRecord {
    ScanRecord() {}
    /** NOTE! This is the old comment for ScanState. - MASV
     *       STATE TRANSITIONS OF SCAN_STATE. SCAN_STATE IS THE STATE 
     *       VARIABLE OF THE RECEIVE AND DELIVERY PROCESS.
     *       THE PROCESS HAS THREE STEPS IT GOES THROUGH.  
     *       1) THE INITIAL STATES WHEN RECEIVING DATA FOR THE SCAN.         
     *          - WAIT_SCAN_TAB_INFO                                         
     *          - WAIT_AI                                                    
     *          - WAIT_FRAGMENT_COUNT                                        
     *       2) THE EXECUTION STATES WHEN THE SCAN IS PERFORMED.             
     *          - SCAN_NEXT_ORDERED                                          
     *          - DELIVERED                                                  
     *          - QUEUED_DELIVERED                                           
     *       3) THE CLOSING STATE WHEN THE SCAN PROCESS IS CLOSING UP 
     *          EVERYTHING.        
     *          - CLOSING_SCAN                                               
     *       INITIAL START WHEN SCAN_TABREQ RECEIVED                         
     *       -> WAIT_SCAN_TAB_INFO (IF ANY SCAN_TABINFO TO BE RECEIVED)      
     *       -> WAIT_AI (IF NO SCAN_TAB_INFO BUT ATTRINFO IS RECEIVED)       
     *       -> WAIT_FRAGMENT_COUNT (IF NEITHER SCAN_TABINFO OR ATTRINFO 
     *                               RECEIVED)       
     *                                                                       
     *       WAIT_SCAN_TAB_INFO TRANSITIONS:                                 
     *       -> WAIT_SCAN_TABINFO (WHEN MORE SCAN_TABINFO RECEIVED)          
     *       -> WAIT_AI (WHEN ATTRINFO RECEIVED AFTER RECEIVING ALL 
     *                    SCAN_TABINFO)        
     *       -> WAIT_FRAGMENT_COUNT (WHEN NO ATTRINFO RECEIVED AFTER 
     *                               RECEIVING ALL SCAN_TABINFO )            
     *       WAIT_AI TRANSITIONS:                                            
     *       -> WAIT_AI (WHEN MORE ATTRINFO RECEIVED)                        
     *       -> WAIT_FRAGMENT_COUNT (WHEN ALL ATTRINFO RECEIVED)             
     *                                                                       
     *       WAIT_FRAGMENT_COUNT TRANSITIONS:                                
     *       -> SCAN_NEXT_ORDERED                                            
     *                                                                       
     *       SCAN_NEXT_ORDERED TRANSITIONS:                                  
     *       -> DELIVERED (WHEN FIRST SCAN_FRAGCONF ARRIVES WITH OPERATIONS 
     *                     TO REPORT IN IT)                                  
     *       -> CLOSING_SCAN (WHEN SCAN IS CLOSED BY SCAN_NEXTREQ OR BY SOME 
     *                        ERROR)      
     *
     *       DELIVERED TRANSITIONS:
     *       -> SCAN_NEXT_ORDERED (IF SCAN_NEXTREQ ARRIVES BEFORE ANY NEW 
     *                             OPERATIONS TO REPORT ARRIVES)
     *       -> QUEUED_DELIVERED (IF NEW OPERATION TO REPORT ARRIVES BEFORE 
     *                            SCAN_NEXTREQ)
     *       -> CLOSING_SCAN (WHEN SCAN IS CLOSED BY SCAN_NEXTREQ OR BY SOME 
     *                        ERROR)      
     *   
     *       QUEUED_DELIVERED TRANSITIONS:  
     *       -> DELIVERED (WHEN SCAN_NEXTREQ ARRIVES AND QUEUED OPERATIONS 
     *                     TO REPORT ARE SENT TO THE APPLICATION)
     *       -> CLOSING_SCAN (WHEN SCAN IS CLOSED BY SCAN_NEXTREQ OR BY 
     *                        SOME ERROR)      
     */
    enum ScanState {
      IDLE = 0,
      WAIT_SCAN_TAB_INFO = 1,
      WAIT_AI = 2,
      WAIT_FRAGMENT_COUNT = 3,
      RUNNING = 4,
      CLOSING_SCAN = 5
    };

    // State of this scan
    ScanState scanState;
    
    DLList<ScanFragRec>::Head m_running_scan_frags;  // Currently in LQH
    union { Uint32 m_queued_count; Uint32 scanReceivedOperations; };
    DLList<ScanFragRec>::Head m_queued_scan_frags;   // In TC !sent to API
    DLList<ScanFragRec>::Head m_delivered_scan_frags;// Delivered to API
    
    // Id of the next fragment to be scanned. Used by scan fragment 
    // processes when they are ready for the next fragment
    Uint32 scanNextFragId;
    
    // Total number of fragments in the table we are scanning
    Uint32 scanNoFrag;

    // Index of next ScanRecords when in free list
    Uint32 nextScan;

    // Length of expected attribute information
    union { Uint32 scanAiLength; Uint32 m_booked_fragments_count; };

    Uint32 scanKeyLen;

    // Reference to ApiConnectRecord
    Uint32 scanApiRec;

    // Reference to TcConnectRecord
    Uint32 scanTcrec;

    // Number of scan frag processes that belong to this scan 
    Uint32 scanParallel;

    // Schema version used by this scan
    Uint32 scanSchemaVersion;

    // Index of stored procedure belonging to this scan
    Uint32 scanStoredProcId;

    // The index of table that is scanned
    Uint32 scanTableref;
    Uint32 m_scan_cookie;

    // Number of operation records per scanned fragment
    // Number of operations in first batch
    // Max number of bytes per batch
    union {
      Uint16 first_batch_size_rows;
      Uint16 batch_size_rows;
    };
    Uint32 batch_byte_size;
    Uint32 m_scan_block_no;

    Uint32 scanRequestInfo; // ScanFrag format

    // Close is ordered
    bool m_close_scan_req;
    // All SCAN_FRAGCONS should be passed on to the API as SCAN_TABCONFS.
    // This is needed to correctly propagate 'node masks' when scanning via the
    // SPJ block.
    bool m_pass_all_confs;

    /**
     * Send opcount/total len as different words
     */
    bool m_4word_conf;
  };
  typedef Ptr<ScanRecord> ScanRecordPtr;
  
  /*************************************************************************>*/
  /*                     GLOBAL CHECKPOINT INFORMATION RECORD                */
  /*                                                                         */
  /*       THIS RECORD IS USED TO STORE THE GLOBALCHECKPOINT NUMBER AND A 
   *       COUNTER DURING THE COMPLETION PHASE OF THE TRANSACTION            */
  /*************************************************************************>*/
  /*                                                                         */
  /*       GCP RECORD ALIGNED TO BE 32 BYTES                                 */
  /*************************************************************************>*/
  struct GcpRecord {
    Uint16 gcpUnused0;
    Uint16 gcpNomoretransRec;
    UintR gcpUnused1[2];	/* p2c: Not used */
    UintR firstApiConnect;
    UintR lastApiConnect;
    UintR nextGcp;
    Uint64 gcpId;
  }; /* p2c: size = 32 bytes */
  
  typedef Ptr<GcpRecord> GcpRecordPtr;

  /*************************************************************************>*/
  /*               TC_FAIL_RECORD                                            */
  /*       THIS RECORD IS USED WHEN HANDLING TAKE OVER OF ANOTHER FAILED 
   *       TC NODE.       */
  /*************************************************************************>*/
  struct TcFailRecord {
    Uint16 queueList[MAX_NDB_NODES];
    Uint8 takeOverProcState[MAX_NDB_NODES];
    UintR completedTakeOver;
    UintR currentHashIndexTakeOver;
    FailState failStatus;
    Uint16 queueIndex;
    Uint16 takeOverNode;
  }; /* p2c: size = 64 bytes */
  
  typedef Ptr<TcFailRecord> TcFailRecordPtr;

public:
  Dbtc(Block_context&, Uint32 instanceNumber = 0);
  virtual ~Dbtc();

private:
  BLOCK_DEFINES(Dbtc);

  // Transit signals
  void execPACKED_SIGNAL(Signal* signal);
  void execABORTED(Signal* signal);
  void execATTRINFO(Signal* signal);
  void execCONTINUEB(Signal* signal);
  void execKEYINFO(Signal* signal);
  void execSCAN_NEXTREQ(Signal* signal);
  void execSCAN_PROCREQ(Signal* signal);
  void execSCAN_PROCCONF(Signal* signal);
  void execTAKE_OVERTCREQ(Signal* signal);
  void execTAKE_OVERTCCONF(Signal* signal);
  void execLQHKEYREF(Signal* signal);
  void execTRANSID_AI_R(Signal* signal);
  void execKEYINFO20_R(Signal* signal);
  void execROUTE_ORD(Signal* signal);
  // Received signals
  void execDUMP_STATE_ORD(Signal* signal);
  void execDBINFO_SCANREQ(Signal* signal);
  void execSEND_PACKED(Signal* signal);
  void execCOMPLETED(Signal* signal);
  void execCOMMITTED(Signal* signal);
  void execDIGETNODESREF(Signal* signal);
  void execDIH_SCAN_GET_NODES_REF(Signal* signal);
  void execDIH_SCAN_GET_NODES_CONF(Signal* signal);
  void execDIVERIFYCONF(Signal* signal);
  void execDIH_SCAN_TAB_REF(Signal* signal);
  void execDIH_SCAN_TAB_CONF(Signal* signal);
  void execGCP_NOMORETRANS(Signal* signal);
  void execLQHKEYCONF(Signal* signal);
  void execNDB_STTOR(Signal* signal);
  void execREAD_NODESCONF(Signal* signal);
  void execREAD_NODESREF(Signal* signal);
  void execSTTOR(Signal* signal);
  void execTC_COMMITREQ(Signal* signal);
  void execTC_CLOPSIZEREQ(Signal* signal);
  void execTCGETOPSIZEREQ(Signal* signal);
  void execTCKEYREQ(Signal* signal);
  void execTCRELEASEREQ(Signal* signal);
  void execTCSEIZEREQ(Signal* signal);
  void execTCROLLBACKREQ(Signal* signal);
  void execTC_HBREP(Signal* signal);
  void execTC_SCHVERREQ(Signal* signal);
  void execTAB_COMMITREQ(Signal* signal);
  void execSCAN_TABREQ(Signal* signal);
  void execSCAN_TABINFO(Signal* signal);
  void execSCAN_FRAGCONF(Signal* signal);
  void execSCAN_FRAGREF(Signal* signal);
  void execREAD_CONFIG_REQ(Signal* signal);
  void execLQH_TRANSCONF(Signal* signal);
  void execCOMPLETECONF(Signal* signal);
  void execCOMMITCONF(Signal* signal);
  void execABORTCONF(Signal* signal);
  void execNODE_FAILREP(Signal* signal);
  void execINCL_NODEREQ(Signal* signal);
  void execTIME_SIGNAL(Signal* signal);
  void execAPI_FAILREQ(Signal* signal);
  void execSCAN_HBREP(Signal* signal);

  void execABORT_ALL_REQ(Signal* signal);

  void execCREATE_TRIG_IMPL_REQ(Signal* signal);
  void execDROP_TRIG_IMPL_REQ(Signal* signal);
  void execFIRE_TRIG_ORD(Signal* signal);
  void execTRIG_ATTRINFO(Signal* signal);
  void execCREATE_INDX_IMPL_REQ(Signal* signal);
  void execDROP_INDX_IMPL_REQ(Signal* signal);
  void execTCINDXREQ(Signal* signal);
  void execINDXKEYINFO(Signal* signal);
  void execINDXATTRINFO(Signal* signal);
  void execALTER_INDX_IMPL_REQ(Signal* signal);
  void execSIGNAL_DROPPED_REP(Signal* signal);


  // Index table lookup
  void execTCKEYCONF(Signal* signal);
  void execTCKEYREF(Signal* signal);
  void execTRANSID_AI(Signal* signal);
  void execTCROLLBACKREP(Signal* signal);

  void execCREATE_TAB_REQ(Signal* signal);
  void execPREP_DROP_TAB_REQ(Signal* signal);
  void execDROP_TAB_REQ(Signal* signal);
  void checkWaitDropTabFailedLqh(Signal*, Uint32 nodeId, Uint32 tableId);
  void execALTER_TAB_REQ(Signal* signal);
  void set_timeout_value(Uint32 timeOut);
  void set_appl_timeout_value(Uint32 timeOut);
  void set_no_parallel_takeover(Uint32);
  void updateBuddyTimer(ApiConnectRecordPtr);

  // Statement blocks
  void updatePackedList(Signal* signal, HostRecord* ahostptr, 
			Uint16 ahostIndex);
  void clearTcNodeData(Signal* signal, 
                       UintR TLastLqhIndicator,
                       UintR Tstart);
  void errorReport(Signal* signal, int place);
  void warningReport(Signal* signal, int place);
  void printState(Signal* signal, int place);
  int seizeTcRecord(Signal* signal);
  int seizeCacheRecord(Signal* signal);
  void TCKEY_abort(Signal* signal, int place);
  void copyFromToLen(UintR* sourceBuffer, UintR* destBuffer, UintR copyLen);
  void reportNodeFailed(Signal* signal, Uint32 nodeId);
  void sendPackedTCKEYCONF(Signal* signal,
                           HostRecord * ahostptr,
                           UintR hostId);
  void sendPackedTCINDXCONF(Signal* signal,
                            HostRecord * ahostptr,
                            UintR hostId);
  void sendPackedSignalLqh(Signal* signal, HostRecord * ahostptr);
  Uint32 sendCommitLqh(Signal* signal,
                       TcConnectRecord * const regTcPtr);
  Uint32 sendCompleteLqh(Signal* signal,
                         TcConnectRecord * const regTcPtr);
  void sendTCKEY_FAILREF(Signal* signal, ApiConnectRecord *);
  void sendTCKEY_FAILCONF(Signal* signal, ApiConnectRecord *);
  void routeTCKEY_FAILREFCONF(Signal* signal, const ApiConnectRecord *, 
			      Uint32 gsn, Uint32 len);
  void execTCKEY_FAILREFCONF_R(Signal* signal);
  void checkStartTimeout(Signal* signal);
  void checkStartFragTimeout(Signal* signal);
  void timeOutFoundFragLab(Signal* signal, Uint32 TscanConPtr);
  void timeOutLoopStartFragLab(Signal* signal, Uint32 TscanConPtr);
  int  releaseAndAbort(Signal* signal);
  void findApiConnectFail(Signal* signal);
  void findTcConnectFail(Signal* signal, Uint32 instanceKey);
  void initApiConnectFail(Signal* signal);
  void initTcConnectFail(Signal* signal, Uint32 instanceKey);
  void initTcFail(Signal* signal);
  void releaseTakeOver(Signal* signal);
  void setupFailData(Signal* signal);
  void updateApiStateFail(Signal* signal);
  void updateTcStateFail(Signal* signal, Uint32 instanceKey);
  void handleApiFailState(Signal* signal, UintR anApiConnectptr);
  void handleFailedApiNode(Signal* signal,
                           UintR aFailedNode,
                           UintR anApiConnectPtr);
  void handleScanStop(Signal* signal, UintR aFailedNode);
  void initScanTcrec(Signal* signal);
  Uint32 initScanrec(ScanRecordPtr,  const class ScanTabReq*,
		   const UintR scanParallel, 
		   const UintR noOprecPerFrag,
                   const Uint32 aiLength,
                   const Uint32 keyLength);
  void initScanfragrec(Signal* signal);
  void releaseScanResources(Signal*, ScanRecordPtr, bool not_started = false);
  ScanRecordPtr seizeScanrec(Signal* signal);
  void sendScanFragReq(Signal*, ScanRecord*, ScanFragRec*, bool);
  void sendScanTabConf(Signal* signal, ScanRecordPtr);
  void close_scan_req(Signal*, ScanRecordPtr, bool received_req);
  void close_scan_req_send_conf(Signal*, ScanRecordPtr);
  
  void checkGcp(Signal* signal);
  void commitGciHandling(Signal* signal, Uint64 Tgci);
  void copyApi(Ptr<ApiConnectRecord> dst, Ptr<ApiConnectRecord> src);
  void DIVER_node_fail_handling(Signal* signal, Uint64 Tgci);
  void gcpTcfinished(Signal* signal, Uint64 gci);
  void handleGcp(Signal* signal, Ptr<ApiConnectRecord>);
  void hash(Signal* signal);
  bool handle_special_hash(Uint32 dstHash[4], 
                           const Uint32* src, Uint32 srcLen, 
                           Uint32 tabPtrI, bool distr);
  
  void initApiConnect(Signal* signal);
  void initApiConnectRec(Signal* signal, 
			 ApiConnectRecord * const regApiPtr,
			 bool releaseIndexOperations = false);
  void initgcp(Signal* signal);
  void inithost(Signal* signal);
  void initialiseScanrec(Signal* signal);
  void initialiseScanFragrec(Signal* signal);
  void initialiseScanOprec(Signal* signal);
  void initTable(Signal* signal);
  void initialiseTcConnect(Signal* signal);
  void linkApiToGcp(Ptr<GcpRecord>, Ptr<ApiConnectRecord>);
  void linkGciInGcilist(Ptr<GcpRecord>);
  void linkTcInConnectionlist(Signal* signal);
  void releaseAbortResources(Signal* signal);
  void releaseApiCon(Signal* signal, UintR aApiConnectPtr);
  void releaseApiConCopy(Signal* signal);
  void releaseApiConnectFail(Signal* signal);
  void releaseAttrinfo();
  void releaseKeys();
  void releaseDirtyRead(Signal*, ApiConnectRecordPtr, TcConnectRecord*);
  void releaseDirtyWrite(Signal* signal);
  void releaseTcCon();
  void releaseTcConnectFail(Signal* signal);
  void releaseTransResources(Signal* signal);
  void seizeApiConnect(Signal* signal);
  void seizeApiConnectCopy(Signal* signal);
  void seizeApiConnectFail(Signal* signal);
  void crash_gcp(Uint32 line);
  void seizeGcp(Ptr<GcpRecord> & dst, Uint64 gci);
  void seizeTcConnect(Signal* signal);
  void seizeTcConnectFail(Signal* signal);
  Ptr<ApiConnectRecord> sendApiCommit(Signal* signal);
  bool sendAttrInfoTrain(Signal* signal,
                         UintR TBRef,
                         Uint32 connectPtr,
                         Uint32 offset,
                         Uint32 attrInfoIVal);
  void sendContinueTimeOutControl(Signal* signal, Uint32 TapiConPtr);
  void sendlqhkeyreq(Signal* signal, 
                     BlockReference TBRef);
  void sendSystemError(Signal* signal, int line);
  void sendtckeyconf(Signal* signal, UintR TcommitFlag);
  void sendTcIndxConf(Signal* signal, UintR TcommitFlag);
  void unlinkApiConnect(Ptr<GcpRecord>, Ptr<ApiConnectRecord>);
  void unlinkGcp(Ptr<GcpRecord>);
  void unlinkReadyTcCon(Signal* signal);
  void handleFailedOperation(Signal* signal,
			     const LqhKeyRef * const lqhKeyRef, 
			     bool gotLqhKeyRef);
  void markOperationAborted(ApiConnectRecord * const regApiPtr,
			    TcConnectRecord * const regTcPtr);
  void clearCommitAckMarker(ApiConnectRecord * const regApiPtr,
			    TcConnectRecord * const regTcPtr);
  // Trigger and index handling
  int saveINDXKEYINFO(Signal* signal,
                      TcIndexOperation* indexOp,
                      const Uint32 *src, 
                      Uint32 len);
  bool receivedAllINDXKEYINFO(TcIndexOperation* indexOp);
  int saveINDXATTRINFO(Signal* signal,
                        TcIndexOperation* indexOp,
                        const Uint32 *src, 
                        Uint32 len);
  bool receivedAllINDXATTRINFO(TcIndexOperation* indexOp);
  bool  saveTRANSID_AI(Signal* signal,
		       TcIndexOperation* indexOp, 
                       const Uint32 *src,
                       Uint32 len);
  bool receivedAllTRANSID_AI(TcIndexOperation* indexOp);
  void readIndexTable(Signal* signal, 
		      ApiConnectRecord* regApiPtr,
		      TcIndexOperation* indexOp);
  void executeIndexOperation(Signal* signal, 
			     ApiConnectRecord* regApiPtr,
			     TcIndexOperation* indexOp);
  bool seizeIndexOperation(ApiConnectRecord* regApiPtr,
			   TcIndexOperationPtr& indexOpPtr);
  void releaseIndexOperation(ApiConnectRecord* regApiPtr,
			     TcIndexOperation* indexOp);
  void releaseAllSeizedIndexOperations(ApiConnectRecord* regApiPtr);
  void setupIndexOpReturn(ApiConnectRecord* regApiPtr,
			  TcConnectRecord* regTcPtr);

  void saveTriggeringOpState(Signal* signal,
			     TcConnectRecord* trigOp);
  void restoreTriggeringOpState(Signal* signal, 
				TcConnectRecord* trigOp);
  void continueTriggeringOp(Signal* signal, 
			    TcConnectRecord* trigOp);

  void scheduleFiredTrigger(ApiConnectRecordPtr* transPtr, 
                            TcConnectRecordPtr* opPtr);
  void executeTriggers(Signal* signal, ApiConnectRecordPtr* transPtr);
  void executeTrigger(Signal* signal,
                      TcFiredTriggerData* firedTriggerData,
                      ApiConnectRecordPtr* transPtr,
                      TcConnectRecordPtr* opPtr);
  void executeIndexTrigger(Signal* signal,
                           TcDefinedTriggerData* definedTriggerData,
                           TcFiredTriggerData* firedTriggerData,
                           ApiConnectRecordPtr* transPtr,
                           TcConnectRecordPtr* opPtr);
  bool appendAttrDataToSection(Uint32& sectionIVal,
                               DataBuffer<11>& values,
                               bool withHeaders,
                               Uint32& attrId,
                               bool& hasNull);
  void insertIntoIndexTable(Signal* signal, 
                            TcFiredTriggerData* firedTriggerData, 
                            ApiConnectRecordPtr* transPtr,
                            TcConnectRecordPtr* opPtr,
                            TcIndexData* indexData,
                            bool holdOperation = false);
  void deleteFromIndexTable(Signal* signal, 
                            TcFiredTriggerData* firedTriggerData, 
                            ApiConnectRecordPtr* transPtr,
                            TcConnectRecordPtr* opPtr,
                            TcIndexData* indexData,
                            bool holdOperation = false);

  void executeReorgTrigger(Signal* signal,
                           TcDefinedTriggerData* definedTriggerData,
                           TcFiredTriggerData* firedTriggerData,
                           ApiConnectRecordPtr* transPtr,
                           TcConnectRecordPtr* opPtr);

  void releaseFiredTriggerData(DLFifoList<TcFiredTriggerData>* triggers);
  void abortTransFromTrigger(Signal* signal, const ApiConnectRecordPtr& transPtr, 
                             Uint32 error);
  // Generated statement blocks
  void warningHandlerLab(Signal* signal, int line);
  void systemErrorLab(Signal* signal, int line);
  void sendSignalErrorRefuseLab(Signal* signal);
  void scanTabRefLab(Signal* signal, Uint32 errCode);
  void diFcountReqLab(Signal* signal, ScanRecordPtr);
  void signalErrorRefuseLab(Signal* signal);
  void abort080Lab(Signal* signal);
  void sendKeyInfoTrain(Signal* signal,
                        BlockReference TBRef,
                        Uint32 connectPtr,
                        Uint32 offset,
                        Uint32 keyInfoIVal);
  void abortScanLab(Signal* signal, ScanRecordPtr, Uint32 errCode, 
		    bool not_started = false);
  void sendAbortedAfterTimeout(Signal* signal, int Tcheck);
  void abort010Lab(Signal* signal);
  void abort015Lab(Signal* signal);
  void packLqhkeyreq(Signal* signal, 
                     BlockReference TBRef);
  void packLqhkeyreq040Lab(Signal* signal,
                           BlockReference TBRef);
  void returnFromQueuedDeliveryLab(Signal* signal);
  void startTakeOverLab(Signal* signal);
  void toCompleteHandlingLab(Signal* signal);
  void toCommitHandlingLab(Signal* signal);
  void toAbortHandlingLab(Signal* signal);
  void abortErrorLab(Signal* signal);
  void nodeTakeOverCompletedLab(Signal* signal);
  void ndbsttorry010Lab(Signal* signal);
  void commit020Lab(Signal* signal);
  void complete010Lab(Signal* signal);
  void releaseAtErrorLab(Signal* signal);
  void seizeDatabuferrorLab(Signal* signal);
  void appendToSectionErrorLab(Signal* signal);
  void scanKeyinfoLab(Signal* signal);
  void scanAttrinfoLab(Signal* signal, UintR Tlen);
  void attrinfoDihReceivedLab(Signal* signal);
  void aiErrorLab(Signal* signal);
  void scanReleaseResourcesLab(Signal* signal);
  void scanCompletedLab(Signal* signal);
  void scanError(Signal* signal, ScanRecordPtr, Uint32 errorCode);
  void diverify010Lab(Signal* signal);
  void intstartphase3x010Lab(Signal* signal);
  void sttorryLab(Signal* signal);
  void abortBeginErrorLab(Signal* signal);
  void tabStateErrorLab(Signal* signal);
  void wrongSchemaVersionErrorLab(Signal* signal);
  void noFreeConnectionErrorLab(Signal* signal);
  void tckeyreq050Lab(Signal* signal);
  void timeOutFoundLab(Signal* signal, UintR anAdd, Uint32 errCode);
  void completeTransAtTakeOverLab(Signal* signal, UintR TtakeOverInd);
  void completeTransAtTakeOverDoLast(Signal* signal, UintR TtakeOverInd);
  void completeTransAtTakeOverDoOne(Signal* signal, UintR TtakeOverInd);
  void timeOutLoopStartLab(Signal* signal, Uint32 apiConnectPtr);
  void initialiseRecordsLab(Signal* signal, UintR Tdata0, Uint32, Uint32);
  void tckeyreq020Lab(Signal* signal);
  void intstartphase1x010Lab(Signal* signal);
  void startphase1x010Lab(Signal* signal);

  void lqhKeyConf_checkTransactionState(Signal * signal,
					Ptr<ApiConnectRecord> regApiPtr);

  void checkDropTab(Signal* signal);

  void checkScanActiveInFailedLqh(Signal* signal,
				  Uint32 scanPtrI,
				  Uint32 failedNodeId);
  void checkScanFragList(Signal*, Uint32 failedNodeId, ScanRecord * scanP, 
			 LocalDLList<ScanFragRec>::Head&);

  void nodeFailCheckTransactions(Signal*,Uint32 transPtrI,Uint32 failedNodeId);
  void ndbdFailBlockCleanupCallback(Signal* signal, Uint32 failedNodeId, Uint32 ignoredRc);
  void checkNodeFailComplete(Signal* signal, Uint32 failedNodeId, Uint32 bit);

  void apiFailBlockCleanupCallback(Signal* signal, Uint32 failedNodeId, Uint32 ignoredRc);
  
  // Initialisation
  void initData();
  void initRecords();

protected:
  virtual bool getParam(const char* name, Uint32* count);
  
private:

  // Transit signals


  ApiConnectRecord *apiConnectRecord;
  ApiConnectRecordPtr apiConnectptr;
  UintR capiConnectFilesize;

  TcConnectRecord *tcConnectRecord;
  TcConnectRecordPtr tcConnectptr;
  UintR ctcConnectFilesize;

  CacheRecord *cacheRecord;
  CacheRecordPtr cachePtr;
  UintR ccacheFilesize;

  HostRecord *hostRecord;
  HostRecordPtr hostptr;
  UintR chostFilesize;
  NdbNodeBitmask c_alive_nodes;

  Uint32 c_ongoing_take_over_cnt;
  GcpRecord *gcpRecord;
  UintR cgcpFilesize;

  TableRecord *tableRecord;
  UintR ctabrecFilesize;

  UintR thashValue;
  UintR tdistrHashValue;

  UintR ttransid_ptr;
  UintR cfailure_nr;
  UintR coperationsize;
  UintR ctcTimer;
  UintR cDbHbInterval;

  Uint64 tcheckGcpId;

  // Montonically increasing counters
  struct MonotonicCounters {
    Uint64 cattrinfoCount;
    Uint64 ctransCount;
    Uint64 ccommitCount;
    Uint64 creadCount;
    Uint64 csimpleReadCount;
    Uint64 cwriteCount;
    Uint64 cabortCount;
    Uint64 c_scan_count;
    Uint64 c_range_scan_count;

    // Resource usage counter(not monotonic)
    Uint32 cconcurrentOp;

    MonotonicCounters() :
     cattrinfoCount(0),
     ctransCount(0),
     ccommitCount(0),
     creadCount(0),
     csimpleReadCount(0),
     cwriteCount(0),
     cabortCount(0),
     c_scan_count(0),
     c_range_scan_count(0),
     cconcurrentOp(0) {}

    Uint32 build_event_rep(Signal* signal)
    {
      /*
        Read saved value from CONTINUEB, subtract from
        counter and write to EVENT_REP
      */
      const Uint32 attrinfoCount =    diff(signal, 1, cattrinfoCount);
      const Uint32 transCount =       diff(signal, 3, ctransCount);
      const Uint32 commitCount =      diff(signal, 5, ccommitCount);
      const Uint32 readCount =        diff(signal, 7, creadCount);
      const Uint32 simpleReadCount =  diff(signal, 9, csimpleReadCount);
      const Uint32 writeCount =       diff(signal, 11, cwriteCount);
      const Uint32 abortCount =       diff(signal, 13, cabortCount);
      const Uint32 scan_count =       diff(signal, 15, c_scan_count);
      const Uint32 range_scan_count = diff(signal, 17, c_range_scan_count);

      signal->theData[0] = NDB_LE_TransReportCounters;
      signal->theData[1] = transCount;
      signal->theData[2] = commitCount;
      signal->theData[3] = readCount;
      signal->theData[4] = simpleReadCount;
      signal->theData[5] = writeCount;
      signal->theData[6] = attrinfoCount;
      signal->theData[7] = cconcurrentOp; // Exception that confirms the rule!
      signal->theData[8] = abortCount;
      signal->theData[9] = scan_count;
      signal->theData[10] = range_scan_count;
      return 11;
    }

    Uint32 build_continueB(Signal* signal) const
    {
      /* Save current value of counters to CONTINUEB */
      const Uint64* vars[] = {
        &cattrinfoCount, &ctransCount, &ccommitCount,
        &creadCount, &csimpleReadCount, &cwriteCount,
        &cabortCount, &c_scan_count, &c_range_scan_count };
      const size_t num = sizeof(vars)/sizeof(vars[0]);

      for (size_t i = 0; i < num; i++)
      {
        signal->theData[1+i*2] = Uint32(*vars[i] >> 32);
        signal->theData[1+i*2+1] = Uint32(*vars[i]);
      }
      return 1 + num * 2;
    }
  private:
    Uint32 diff(Signal* signal, size_t pos, Uint64 curr) const
    {
      const Uint64 old =
        (signal->theData[pos+1] | (Uint64(signal->theData[pos]) << 32));
      return (Uint32)(curr - old);
    }
  } c_counters;

  Uint16 cownNodeid;
  Uint16 terrorCode;

  UintR cfirstfreeTcConnect;
  UintR cfirstfreeApiConnectCopy;
  UintR cfirstfreeCacheRec;

  UintR cfirstgcp;
  UintR clastgcp;
  UintR cfirstfreeGcp;
  UintR cfirstfreeScanrec;

  TableRecordPtr tabptr;
  UintR cfirstfreeApiConnectFail;
  UintR cfirstfreeApiConnect;

  BlockReference cdihblockref;
  BlockReference cownref;                   /* OWN BLOCK REFERENCE */

  ApiConnectRecordPtr timeOutptr;

  ScanRecord *scanRecord;
  UintR cscanrecFileSize;

  UnsafeArrayPool<ScanFragRec> c_scan_frag_pool;
  ScanFragRecPtr scanFragptr;

  UintR cscanFragrecFileSize;

  BlockReference cdictblockref;
  BlockReference cerrorBlockref;
  BlockReference clqhblockref;
  BlockReference cndbcntrblockref;

  Uint16 csignalKey;
  Uint16 csystemnodes;
  Uint16 cnodes[4];
  NodeId cmasterNodeId;
  UintR cnoParallelTakeOver;
  TimeOutCheckState ctimeOutCheckFragActive;

  UintR ctimeOutCheckFragCounter;
  UintR ctimeOutCheckCounter;
  UintR ctimeOutValue;
  UintR ctimeOutCheckDelay;
  Uint32 ctimeOutCheckHeartbeat;
  Uint32 ctimeOutCheckLastHeartbeat;
  Uint32 ctimeOutMissedHeartbeats;
  Uint32 c_appl_timeout_value;

  SystemStartState csystemStart;
  TimeOutCheckState ctimeOutCheckActive;

  BlockReference capiFailRef;
  UintR cpackedListIndex;
  Uint16 cpackedList[MAX_NODES];
  UintR capiConnectClosing[MAX_NODES];
  UintR con_lineNodes;

  UintR treqinfo;
  UintR ttransid1;
  UintR ttransid2;

  UintR tabortInd;

  NodeId tnodeid;
  BlockReference tblockref;

  LqhTransConf::OperationStatus ttransStatus;
  UintR ttcOprec;
  NodeId tfailedNodeId;
  Uint8 tcurrentReplicaNo;
  Uint8 tpad1;

  Uint64 tgci;
  UintR tapplRef;
  UintR tapplOprec;

  UintR tindex;
  UintR tmaxData;
  UintR tmp;

  UintR tnodes;
  BlockReference tusersblkref;
  UintR tuserpointer;
  UintR tloadCode;

  UintR tconfig1;
  UintR tconfig2;

  UintR cdata[32];
  UintR ctransidFailHash[512];
  UintR ctcConnectFailHash[1024];
  
  /**
   * Commit Ack handling
   */
public:
  struct CommitAckMarker {
    CommitAckMarker() {}
    Uint32 transid1;
    Uint32 transid2;
    union { Uint32 nextPool; Uint32 nextHash; };
    Uint32 prevHash;
    Uint32 apiConnectPtr;
    Uint16 apiNodeId;
    NdbNodeBitmask m_commit_ack_marker_nodes; 

    inline bool equal(const CommitAckMarker & p) const {
      return ((p.transid1 == transid1) && (p.transid2 == transid2));
    }
    
    inline Uint32 hashValue() const {
      return transid1;
    }
  };

private:
  typedef Ptr<CommitAckMarker> CommitAckMarkerPtr;
  typedef DLHashTable<CommitAckMarker>::Iterator CommitAckMarkerIterator;
  
  ArrayPool<CommitAckMarker>   m_commitAckMarkerPool;
  DLHashTable<CommitAckMarker> m_commitAckMarkerHash;
  
  void execTC_COMMIT_ACK(Signal* signal);
  void sendRemoveMarkers(Signal*, const CommitAckMarker *);
  void sendRemoveMarker(Signal* signal, 
			NodeId nodeId,
			Uint32 transid1, 
			Uint32 transid2);
  void removeMarkerForFailedAPI(Signal* signal, Uint32 nodeId, Uint32 bucket);

  bool getAllowStartTransaction(Uint32 nodeId, Uint32 table_single_user_mode) const {
    if (unlikely(getNodeState().getSingleUserMode()))
    {
      if (getNodeState().getSingleUserApi() == nodeId || table_single_user_mode)
        return true;
      else
        return false;
    }
    return getNodeState().startLevel < NodeState::SL_STOPPING_2;
  }
  
  void checkAbortAllTimeout(Signal* signal, Uint32 sleepTime);
  struct AbortAllRecord {
    AbortAllRecord(){ clientRef = 0; }
    Uint32 clientData;
    BlockReference clientRef;
    
    Uint32 oldTimeOutValue;
  };
  AbortAllRecord c_abortRec;

  bool validate_filter(Signal*);
  bool match_and_print(Signal*, ApiConnectRecordPtr);

#ifdef ERROR_INSERT
  bool testFragmentDrop(Signal* signal);
#endif

  /************************** API CONNECT RECORD ***********************/
  /* *******************************************************************/
  /* THE API CONNECT RECORD CONTAINS THE CONNECTION RECORD TO WHICH THE*/
  /* APPLICATION CONNECTS. THE APPLICATION CAN SEND ONE OPERATION AT A */
  /* TIME. IT CAN SEND A NEW OPERATION IMMEDIATELY AFTER SENDING THE   */
  /* PREVIOUS OPERATION. THEREBY SEVERAL OPERATIONS CAN BE ACTIVE IN   */
  /* ONE TRANSACTION WITHIN TC. THIS IS ACHIEVED BY USING THE API      */
  /* CONNECT RECORD. EACH ACTIVE OPERATION IS HANDLED BY THE TC        */
  /* CONNECT RECORD. AS SOON AS THE TC CONNECT RECORD HAS SENT THE     */
  /* REQUEST TO THE LQH IT IS READY TO RECEIVE NEW OPERATIONS. THE     */
  /* LQH CONNECT RECORD TAKES CARE OF WAITING FOR AN OPERATION TO      */
  /* COMPLETE. WHEN AN OPERATION HAS COMPLETED ON THE LQH CONNECT      */
  /* RECORD A NEW OPERATION CAN BE STARTED ON THIS LQH CONNECT RECORD. */
  /*******************************************************************>*/
  /*                                                                   */
  /*       API CONNECT RECORD ALIGNED TO BE 256 BYTES                  */
  /*******************************************************************>*/
  /************************** TC CONNECT RECORD ************************/
  /* *******************************************************************/
  /* TC CONNECT RECORD KEEPS ALL INFORMATION TO CARRY OUT A TRANSACTION*/
  /* THE TRANSACTION CONTROLLER ESTABLISHES CONNECTIONS TO DIFFERENT   */
  /* BLOCKS TO CARRY OUT THE TRANSACTION. THERE CAN BE SEVERAL RECORDS */
  /* PER ACTIVE TRANSACTION. THE TC CONNECT RECORD COOPERATES WITH THE */
  /* API CONNECT RECORD FOR COMMUNICATION WITH THE API AND WITH THE    */
  /* LQH CONNECT RECORD FOR COMMUNICATION WITH THE LQH'S INVOLVED IN   */
  /* THE TRANSACTION. TC CONNECT RECORD IS PERMANENTLY CONNECTED TO A  */
  /* RECORD IN DICT AND ONE IN DIH. IT CONTAINS A LIST OF ACTIVE LQH   */
  /* CONNECT RECORDS AND A LIST OF STARTED BUT NOT ACTIVE LQH CONNECT  */
  /* RECORDS. IT DOES ALSO CONTAIN A LIST OF ALL OPERATIONS THAT ARE   */
  /* EXECUTED WITH THE TC CONNECT RECORD.                              */
  /*******************************************************************>*/
  /*       TC_CONNECT RECORD ALIGNED TO BE 128 BYTES                   */
  /*******************************************************************>*/
  UintR cfirstfreeTcConnectFail;

  /* POINTER FOR THE LQH RECORD*/
  /* ************************ HOST RECORD ********************************* */
  /********************************************************/
  /* THIS RECORD CONTAINS ALIVE-STATUS ON ALL NODES IN THE*/
  /* SYSTEM                                               */
  /********************************************************/
  /*       THIS RECORD IS ALIGNED TO BE 8 BYTES.         */
  /********************************************************/
  /* ************************ TABLE RECORD ******************************** */
  /********************************************************/
  /* THIS RECORD CONTAINS THE CURRENT SCHEMA VERSION OF   */
  /* ALL TABLES IN THE SYSTEM.                            */
  /********************************************************/
  /*-------------------------------------------------------------------------*/
  /*       THE TC CONNECTION USED BY THIS SCAN.                              */
  /*-------------------------------------------------------------------------*/
  /*-------------------------------------------------------------------------*/
  /*	LENGTH READ FOR A PARTICULAR SCANNED OPERATION.			     */
  /*-------------------------------------------------------------------------*/
  /*-------------------------------------------------------------------------*/
  /*       REFERENCE TO THE SCAN RECORD FOR THIS SCAN PROCESS.               */
  /*-------------------------------------------------------------------------*/
  /* *********************************************************************** */
  /* ******$                           DATA BUFFER                   ******$ */
  /*                                                                         */
  /*       THIS BUFFER IS USED AS A GENERAL DATA STORAGE.                    */
  /* *********************************************************************** */
  /* *********************************************************************** */
  /* ******$                 ATTRIBUTE INFORMATION RECORD            ******$ */
  /*
    CAN CONTAIN ONE (1) ATTRINFO SIGNAL. ONE SIGNAL CONTAINS 24 ATTR.          
    INFO WORDS. BUT 32 ELEMENTS ARE USED TO MAKE PLEX HAPPY.                   
    SOME OF THE ELEMENTS ARE USED TO THE FOLLOWING THINGS:                     
    DATA LENGHT IN THIS RECORD IS STORED IN THE ELEMENT INDEXED BY             
    ZINBUF_DATA_LEN.                                                           
    NEXT FREE ATTRBUF IS POINTED OUT BY THE ELEMENT INDEXED BY                 
    PREVIOUS ATTRBUF IS POINTED OUT BY THE ELEMENT INDEXED BY ZINBUF_PREV      
    (NOT USED YET).                                                            
    NEXT ATTRBUF IS POINTED OUT BY THE ELEMENT INDEXED BY ZINBUF_NEXT.        
  */
  /* ********************************************************************** */
  /**************************************************************************/
  /*           GLOBAL CHECKPOINT INFORMATION RECORD                         */
  /*                                                                        */
  /*       THIS RECORD IS USED TO STORE THE GCP NUMBER AND A COUNTER        */
  /*                DURING THE COMPLETION PHASE OF THE TRANSACTION          */
  /**************************************************************************/
  /*                                                                        */
  /*       GCP RECORD ALIGNED TO BE 32 BYTES                                */
  /**************************************************************************/
  /**************************************************************************/
  /*                          TC_FAIL_RECORD                                */
  /*  THIS RECORD IS USED WHEN HANDLING TAKE OVER OF ANOTHER FAILED TC NODE.*/
  /**************************************************************************/
  TcFailRecord *tcFailRecord;
  TcFailRecordPtr tcNodeFailptr;
  /**************************************************************************/
  // Temporary variables that are not allowed to use for storage between
  // signals. They
  // can only be used in a signal to transfer values between subroutines.
  // In the long run
  // those variables should be removed and exchanged for stack
  // variable communication.
  /**************************************************************************/

  Uint32 c_gcp_ref;
  Uint32 c_gcp_data;

  Uint32 c_sttor_ref;

#ifdef ERROR_INSERT
  // Used with ERROR_INSERT 8078 + 8079 to check API_FAILREQ handling
  Uint32 c_lastFailedApi;
#endif
};

#endif
