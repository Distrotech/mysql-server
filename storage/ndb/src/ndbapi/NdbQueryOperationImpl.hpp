/*
   Copyright (C) 2009 Sun Microsystems Inc
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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef NdbQueryOperationImpl_H
#define NdbQueryOperationImpl_H

#include "NdbQueryOperation.hpp"
#include "NdbQueryBuilderImpl.hpp"
#include "NdbIndexScanOperation.hpp"
#include <NdbError.hpp>
#include <ndb_limits.h>
#include <Vector.hpp>

// Forward declarations
class NdbTableImpl;
class NdbIndexImpl;
class NdbApiSignal;
class NdbResultStream;
class NdbParamOperand;
class NdbTransaction;
class NdbReceiver;
class NdbOut;
class NdbRootFragment;

/** This class is the internal implementation of the interface defined by
 * NdbQuery. This class should thus not be visible to the application 
 * programmer. @see NdbQuery.*/
class NdbQueryImpl {

  /* NdbQueryOperations are allowed to access it containing query */
  friend class NdbQueryOperationImpl;
  friend class ReceiverIdIterator;
  
  /** For debugging.*/
  friend NdbOut& operator<<(NdbOut& out, const class NdbQueryOperationImpl&);

public:
  /** Factory method which instantiate a query from its definition.
   (There is no public constructor.)*/
  static NdbQueryImpl* buildQuery(NdbTransaction& trans, 
                                  const NdbQueryDefImpl& queryDef);

  /** Return number of operations in query.*/
  Uint32 getNoOfOperations() const;

  /**
   * return number of leaf-operations
   */
  Uint32 getNoOfLeafOperations() const;

  // Get a specific NdbQueryOperation instance by ident specified
  // when the NdbQueryOperationDef was created.
  NdbQueryOperationImpl& getQueryOperation(Uint32 ident) const;
  NdbQueryOperationImpl* getQueryOperation(const char* ident) const;
  // Consider to introduce these as convenient shortcuts
//NdbQueryOperationDefImpl& getQueryOperationDef(Uint32 ident) const;
//NdbQueryOperationDefImpl* getQueryOperationDef(const char* ident) const;

  /** Return number of parameter operands in query.*/
  Uint32 getNoOfParameters() const;
  const NdbParamOperand* getParameter(const char* name) const;
  const NdbParamOperand* getParameter(Uint32 num) const;

  /** Get the next tuple(s) from the global cursor on the query.
   * @param fetchAllowed If true, the method may block while waiting for more
   * results to arrive. Otherwise, the method will return immediately if no more
   * results are buffered in the API.
   * @param forceSend FIXME: Describe this.
   * @return 
   * -  NextResult_error (-1):       if unsuccessful,<br>
   * -  NextResult_gotRow (0):       if another tuple was received, and<br> 
   * -  NextResult_scanComplete (1): if there are no more tuples to scan.
   * -  NextResult_bufferEmpty (2):  if there are no more cached records 
   *                                 in NdbApi
   * @see NdbQueryOperation::nextResult()
   */ 
  NdbQuery::NextResultOutcome nextResult(bool fetchAllowed, bool forceSend);

  /** Close query: 
   *  - Release datanode resources,
   *  - Discard pending result sets,
   *  - optionaly dealloc NdbQuery structures
   */
  int close(bool forceSend);

  /** Deallocate 'this' NdbQuery object and its NdbQueryOperation objects.
   *  If not already closed, it will also ::close() the NdbQuery.
   */
  void release();

  NdbTransaction& getNdbTransaction() const
  { return m_transaction; }

  const NdbError& getNdbError() const;

  void setErrorCode(int aErrorCode);

  /** Set an error code and initiate transaction abort.*/
  void setErrorCodeAbort(int aErrorCode);

  /** Assign supplied parameter values to the parameter placeholders
   *  created when the query was defined.
   *  Values are *copied* into this NdbQueryImpl object:
   *  Memory location used as source for parameter values don't have
   *  to be valid after this assignment.
   */
  int assignParameters(const NdbQueryParamValue paramValues[]);

  int setBound(const NdbRecord *keyRecord,
               const NdbIndexScanOperation::IndexBound *bound);

  /** Prepare for execution. 
   *  @return possible error code.
   */
  int prepareSend();

  /** Send prepared signals from this NdbQuery to start execution
   *  @return #signals sent, -1 if error.
   */
  int doSend(int aNodeId, bool lastFlag);

  NdbQuery& getInterface()
  { return m_interface; }

  /** Get next query in same transaction.*/
  NdbQueryImpl* getNext() const
  { return m_next; }

  void setNext(NdbQueryImpl* next)
  { m_next = next; }

  /** Get the (transaction independent) definition of this query. */
  const NdbQueryDefImpl& getQueryDef() const
  { return m_queryDef; }

  /** Process TCKEYCONF message. Return true if query is complete. */
  bool execTCKEYCONF();

  /** Process SCAN_TABCONF w/ EndOfData which is a 'Close Scan Reply'. */
  void execCLOSE_SCAN_REP(int errorCode, bool needClose);

  /** Determines if query has completed and may be garbage collected
   *  A query is not considder complete until the client has 
   *  called the ::close() or ::release() method on it.
   */
  bool hasCompleted() const
  { return (m_state == Closed); 
  }
  
  /** 
   * Mark this query as the first query or operation in a new transaction.
   * This should only be called for queries where root operation is a lookup.
   */
  void setStartIndicator()
  { 
    assert(!m_queryDef.isScanQuery());
    m_startIndicator = true; 
  }

  /** 
   * Mark this query as the last query or operation in a transaction, after
   * which the transaction should be committed. This should only be called 
   * for queries where root operation is a lookup.
   */
  void setCommitIndicator()
  {  
    assert(!m_queryDef.isScanQuery());
    m_commitIndicator = true; 
  }

  /**
   * Check if this is a pruned range scan. A range scan is pruned if the ranges
   * are such that only a subset of the fragments need to be scanned for 
   * matching tuples.
   *
   * @param pruned This will be set to true if the operation is a pruned range 
   * scan.
   * @return 0 if ok, -1 in case of error (call getNdbError() for details.)
   */
  int isPrunable(bool& pruned);

  /** Get the number of fragments to be read for the root operation.*/
  Uint32 getRootFragCount() const
  { return m_rootFragCount; }

private:
  /** Possible return values from NdbQueryImpl::awaitMoreResults. 
   * A subset of the integer values also matches those returned
   * from PoolGuard::wait_scan().
   */
  enum FetchResult{
    FetchResult_gotError = -4,  // There is an error avail in 'm_error.code'
    FetchResult_sendFail = -3,
    FetchResult_nodeFail = -2,
    FetchResult_timeOut = -1,
    FetchResult_ok = 0,
    FetchResult_noMoreData = 1,
    FetchResult_noMoreCache = 2
  };

  /** A stack of NdbRootFragment pointers.
   *  NdbRootFragments which are 'BatchComplete' are pushed on this stack by 
   *  the receiver thread, and later pop'ed into the application thread when 
   *  it need more results to process.
   *  Due to this shared usage, the PollGuard mutex must be set before 
   *  accessing SharedFragStack.
   */
  class SharedFragStack{
  public:
    explicit SharedFragStack();

    ~SharedFragStack() {
      delete[] m_array;
    }

    // Prepare internal datastructures.
    // Return 0 if ok, else errorcode
    int prepare(int capacity);

    NdbRootFragment* pop() { 
      return m_current>=0 ? m_array[m_current--] : NULL;
    }
    
    void push(NdbRootFragment& frag);

    int size() const {
      return (m_current+1);
    }

    void clear() {
      m_current = -1;
    }

    /** Possible error received from TC / datanodes. */
    int m_errorCode;

  private:
    /** Capacity of stack.*/
    int m_capacity;

    /** Index of current top of stack.*/
    int m_current;
    NdbRootFragment** m_array;

    // No copying.
    SharedFragStack(const SharedFragStack&);
    SharedFragStack& operator=(const SharedFragStack&);
  }; // class SharedFragStack

  class OrderedFragSet{
  public:
    explicit OrderedFragSet();

    ~OrderedFragSet(); 

    // Prepare internal datastructures.
    // Return 0 if ok, else errorcode
    int prepare(NdbQueryOptions::ScanOrdering ordering, 
                int size, 
                const NdbRecord* keyRecord,
                const NdbRecord* resultRecord);

    /** Get the root fragment from which to read the next row.*/
    NdbRootFragment* getCurrent() const;

    /** Re-organize the fragments after a row has been consumed. This is 
     * needed to remove fragements that has been needed, and to re-sort 
     * fragments if doing a sorted scan.*/
    void reorganize();

    /** Add a complete fragment that has been received.*/
    void add(NdbRootFragment& frag);

    /** Reset object to an empty state.*/
    void clear();

    /** Get a fragment where all rows have been consumed. (This method is 
     * not idempotent - the fragment is removed from the set. 
     * @return Emptied fragment (or NULL if there are no more emptied 
     * fragments).
     */
    NdbRootFragment* getEmpty();

  private:

    /** Max no of fragments.*/
    int m_capacity;
    /** Number of fragments in 'm_activeFrags'.*/
    int m_activeFragCount;
    /** Number of fragments in 'm_emptiedFrags'. */
    int m_emptiedFragCount;
    /** Number of fragments where the final batch has been received
     * and consumed.*/
    int m_finalFragCount;
    /** Ordering of index scan result.*/
    NdbQueryOptions::ScanOrdering m_ordering;
    /** Needed for comparing records when ordering results.*/
    const NdbRecord* m_keyRecord;
    /** Needed for comparing records when ordering results.*/
    const NdbRecord* m_resultRecord;
    /** Fragments where some tuples in the current batch has not yet been 
     * consumed.*/
    NdbRootFragment** m_activeFrags;
    /** Fragments where all tuples in the current batch have been consumed, 
     * but where there are more batches to fetch.*/
    NdbRootFragment** m_emptiedFrags;
    // No copying.
    OrderedFragSet(const OrderedFragSet&);
    OrderedFragSet& operator=(const OrderedFragSet&);

    /** For sorting fragment reads according to index value of first record. 
     * Also f1<f2 if f2 has reached end of data and f1 has not.
     * @return 1 if f1>f2, 0 if f1==f2, -1 if f1<f2.*/
    int compare(const NdbRootFragment& frag1,
                const NdbRootFragment& frag2) const;

    /** For debugging purposes.*/
    bool verifySortOrder() const;
  }; // class OrderedFragSet

  /** The interface that is visible to the application developer.*/
  NdbQuery m_interface;

  enum {        // State of NdbQuery in API
    Initial,    // Constructed object, assiciated with a defined query
    Defined,    // Parameter values has been assigned
    Prepared,   // KeyInfo & AttrInfo prepared for execution
    Executing,  // Signal with exec. req. sent to TC
    EndOfData,  // All results rows consumed
    Closed,     // Query has been ::close()'ed 
    Failed,     
    Destructed
  } m_state;

  enum {        // Assumed state of query cursor in TC block
    Inactive,   // Execution not started at TC
    Active
  } m_tcState;

  /** Next query in same transaction.*/
  NdbQueryImpl* m_next;
  /** Definition of this query.*/
  const NdbQueryDefImpl& m_queryDef;

  /** Possible error status of this query.*/
  NdbError m_error;
  /** Transaction in which this query instance executes.*/
  NdbTransaction& m_transaction;

  /** Scan queries creates their own sub transaction which they
   *  execute within.
   *  Has same transId, Ndb*, ++ as the 'real' transaction above.
   */
  NdbTransaction* m_scanTransaction;

  /** The operations constituting this query.*/
  NdbQueryOperationImpl *m_operations;  // 'Array of ' OperationImpls
  Uint32 m_countOperations;             // #elements in above array

  /** Current global cursor position. Refers the current NdbQueryOperation which
   *  should be advanced to 'next' position for producing a new global set of results.
   */
  Uint32 m_globalCursor;

  /** Number of root fragments not yet completed within the current batch.
   *  Only access w/ PollGuard mutex as it is also updated by receiver threa 
   */
  Uint32 m_pendingFrags;  // BEWARE: protect with PollGuard mutex

  /** Number of fragments to be read by the root operation. (1 if root 
   * operation is a lookup)*/
  Uint32 m_rootFragCount;

  /**
   * This is an array with one element for each fragment that the root
   * operation accesses (i.e. one for a lookup, all for a table scan).
   * It keeps the state of the read operation on that fragment, and on
   * any child operation instance derived from it.
   */
  NdbRootFragment* m_rootFrags;

  /** Root fragments that the application is currently iterating over. Only 
   * accessed by application thread.
   */
  OrderedFragSet m_applFrags;

  /** Root frgaments that have received a complete batch. Shared between 
   *  application thread and receiving thread. Access should be mutex protected.
   */
  SharedFragStack m_fullFrags;  // BEWARE: protect with PollGuard mutex

  /** Number of root fragments for which confirmation for the final batch 
   * (with tcPtrI=RNIL) has been received. Observe that even if 
   * m_finalBatchFrags==m_rootFragCount, all tuples for the final batches may
   * still not have been received (i.e. m_pendingFrags>0).
   */
  Uint32 m_finalBatchFrags; // BEWARE: protect with PollGuard mutex

  /** Number of IndexBounds set by API (index scans only) */
  Uint32 m_num_bounds;

  /** 
   * Number of fields in the shortest bound (for an index scan root).
   * Will be 0xffffffff if no bound has been set. 
   */
  Uint32 m_shortestBound;

  /**
   * Signal building section:
   */
  Uint32Buffer m_attrInfo;  // ATTRINFO: QueryTree + serialized parameters
  Uint32Buffer m_keyInfo;   // KEYINFO:  Lookup key or scan bounds

  /** True if this query starts a new transaction. */
  bool m_startIndicator;

  /** True if the transaction should be committed after executing this query.*/
  bool m_commitIndicator;

  /** This field tells if the root operation is a prunable range scan. A range 
   * scan is pruned if the ranges are such that only a subset of the fragments 
   * need to be scanned for matching tuples. (Currently, pushed scans can only 
   * be pruned if is there is a single range that maps to a single fragment.
   */
  enum {
    /** Call NdbQueryOperationDef::checkPrunable() to determine prunability.*/
    Prune_Unknown, 
    Prune_Yes, // The root is a prunable range scan.
    Prune_No   // The root is not a prunable range scan.
  } m_prunability;

  /** If m_prunability==Prune_Yes, this is the hash value of the single 
   * fragment that should be scanned.*/
  Uint32 m_pruneHashVal;

  // Only constructable from factory ::buildQuery();
  explicit NdbQueryImpl(
             NdbTransaction& trans,
             const NdbQueryDefImpl& queryDef);

  ~NdbQueryImpl();

  /** Release resources after scan has returned last available result */
  void postFetchRelease();

  /** Navigate to the next result from the root operation. */
  NdbQuery::NextResultOutcome nextRootResult(bool fetchAllowed, bool forceSend);

  /** Send SCAN_NEXTREQ signal to fetch another batch from a scan query
   * @return 0 if send succeeded, -1 otherwise.
   */
  int sendFetchMore(NdbRootFragment& emptyFrag, bool forceSend);

  /** Wait for more scan results which already has been REQuested to arrive.
   * @return 0 if some rows did arrive, a negative value if there are errors (in m_error.code),
   * and 1 of there are no more rows to receive.
   */
  FetchResult awaitMoreResults(bool forceSend);

  /** Check if we have received an error from TC, or datanodes.
   * @return 'true' if an error is pending, 'false' otherwise.
   */
  bool hasReceivedError();                                   // Need mutex lock

  void setFetchTerminated(int aErrorCode, bool needClose);   // Need mutex lock

  /** Close cursor on TC */
  int closeTcCursor(bool forceSend);

  /** Send SCAN_NEXTREQ(close) signal to close cursor on TC and datanodes.
   *  @return #signals sent, -1 if error.
   */
  int sendClose(int nodeId);

  /** Close scan receivers used for lookups. (Since scans and lookups should
   * have the same semantics for nextResult(), lookups use scan-type 
   * NdbReceiver objects.)
   */
  void closeSingletonScans();

  const NdbQuery& getInterface() const
  { return m_interface; }

  NdbQueryOperationImpl& getRoot() const 
  { return getQueryOperation(0U); }

  /** Count number of completed root fragments within this batch. 
   *  @param increment Change in count of completed root frgaments.
   *  @return True if batch is complete.
   */
  bool incrementPendingFrags(int increment);

  /** A complete batch has been received for a given root fragment
   *  Update whatever required before the appl. is allowed to navigate the result.
   */ 
  void handleBatchComplete(Uint32 rootFragNo);
}; // class NdbQueryImpl


/** This class contains data members for NdbQueryOperation, such that these
 *  do not need to exposed in NdbQueryOperation.hpp. This class may be 
 *  changed without forcing the customer to recompile his application.
 */
class NdbQueryOperationImpl {

  /** For debugging.*/
  friend NdbOut& operator<<(NdbOut& out, const NdbQueryOperationImpl&);

  friend class NdbQueryImpl;

public:
  Uint32 getNoOfParentOperations() const;
  NdbQueryOperationImpl& getParentOperation(Uint32 i) const;
  NdbQueryOperationImpl* getParentOperation() const;

  Uint32 getNoOfChildOperations() const;
  NdbQueryOperationImpl& getChildOperation(Uint32 i) const;

  /** A shorthand for getting the root operation. */
  NdbQueryOperationImpl& getRoot() const
  { return m_queryImpl.getRoot(); }

  const NdbQueryDefImpl& getQueryDef() const
  { return m_queryImpl.getQueryDef(); }

  const NdbQueryOperationDefImpl& getQueryOperationDef() const
  { return m_operationDef; }

  // Get the entire query object which this operation is part of
  NdbQueryImpl& getQuery() const
  { return m_queryImpl; }

  NdbRecAttr* getValue(const char* anAttrName, char* resultBuffer);
  NdbRecAttr* getValue(Uint32 anAttrId, char* resultBuffer);
  NdbRecAttr* getValue(const NdbColumnImpl&, char* resultBuffer);

  int setResultRowBuf (const NdbRecord *rec,
                       char* resBuffer,
                       const unsigned char* result_mask);

  int setResultRowRef (const NdbRecord* rec,
                       const char* & bufRef,
                       const unsigned char* result_mask);

  NdbQuery::NextResultOutcome firstResult();

  NdbQuery::NextResultOutcome nextResult(bool fetchAllowed, bool forceSend);

  bool isRowNULL() const;    // Row associated with Operation is NULL value?

  bool isRowChanged() const; // Prev ::nextResult() on NdbQuery retrieved a new
                             // value for this NdbQueryOperation

  /** Process result data for this operation. Return true if batch complete.*/
  bool execTRANSID_AI(const Uint32* ptr, Uint32 len);
  
  /** Process absence of result data for this operation. (Only used when the 
   * root operation is a lookup.)
   * @return true if query complete.*/
  bool execTCKEYREF(const NdbApiSignal* aSignal);

  /** Called once per complete (within batch) fragment when a SCAN_TABCONF 
   * signal is received.
   * @param tcPtrI not in use.
   * @param rowCount Number of rows for this fragment, including all rows from 
   * descendant lookup operations.
   * @param receiver The receiver object that shall process the results.*/
  bool execSCAN_TABCONF(Uint32 tcPtrI, Uint32 rowCount, Uint32 nodeMask,
                        NdbReceiver* receiver); 

  const NdbQueryOperation& getInterface() const
  { return m_interface; }
  NdbQueryOperation& getInterface()
  { return m_interface; }

  /** Define result ordering for ordered index scan. It is an error to call
   * this method on an operation that is not a scan, or to call it if an
   * ordering was already set on the operation defintion by calling 
   * NdbQueryOperationDef::setOrdering().
   * @param ordering The desired ordering of results.
   * @return 0 if ok, -1 in case of error (call getNdbError() for details.)
   */
  int setOrdering(NdbQueryOptions::ScanOrdering ordering);

  NdbQueryOptions::ScanOrdering getOrdering() const
  { return m_ordering; }

   /** 
   * Set the number of fragments to be scanned in parallel for the root 
   * operation of this query. This only applies to table scans and non-sorted
   * scans of ordered indexes.
   * @return 0 if ok, -1 in case of error (call getNdbError() for details.)
   */
  int setParallelism(Uint32 parallelism);

  /** Set the batch size (max rows per batch) for this operation. This
   * only applies to scan operations, as lookup operations always will
   * have the same batch size as its parent operation, or 1 if it is the
   * root operation.
   * @param batchSize Batch size (in number of rows). A value of 0 means
   * use the default batch size.
   * @return 0 if ok, -1 in case of error (call getNdbError() for details.)
   */
  int setBatchSize(Uint32 batchSize);

  /**
   * Set the NdbInterpretedCode needed for defining a scan filter for 
   * this operation. 
   * It is an error to call this method on a lookup operation.
   * @param code The interpreted code. This object is copied internally, 
   * meaning that 'code' may be destroyed as soon as this method returns.
   * @return 0 if ok, -1 in case of error (call getNdbError() for details.)
   */
  int setInterpretedCode(const NdbInterpretedCode& code);
  bool hasInterpretedCode() const;

  NdbResultStream& getResultStream(Uint32 rootFragNo) const;

  const NdbReceiver& getReceiver(Uint32 rootFragNo) const;

  /** Verify magic number.*/
  bool checkMagicNumber() const
  { return m_magic == MAGIC; }

  /** Get the maximal number of rows that may be returned in a single 
   *  SCANREQ to the SPJ block.
   */
  Uint32 getMaxBatchRows() const
  { return m_maxBatchRows; }

private:

  STATIC_CONST (MAGIC = 0xfade1234);

  /** Interface for the application developer.*/
  NdbQueryOperation m_interface;
  /** For verifying pointers to this class.*/
  const Uint32 m_magic;
  /** NdbQuery to which this operation belongs. */
  NdbQueryImpl& m_queryImpl;
  /** The (transaction independent ) definition from which this instance
   * was created.*/
  const NdbQueryOperationDefImpl& m_operationDef;

  /* MAYBE: replace m_children with navigation via m_operationDef.getChildOperation().*/
  /** Parent of this operation.*/
  NdbQueryOperationImpl* m_parent;
  /** Children of this operation.*/
  Vector<NdbQueryOperationImpl*> m_children;

  /** Max rows (per resultStream) in a scan batch.*/
  Uint32 m_maxBatchRows;

  /** For processing results from this operation (Array of).*/
  NdbResultStream** m_resultStreams;
  /** Buffer for parameters in serialized format */
  Uint32Buffer m_params;

  /** Buffer size allocated for *each* ResultStream/Receiver when 
   *  fetching results.*/
  Uint32 m_bufferSize;
  /** Internally allocated temp. buffer for *all* m_resultStreams[]
   *  when receiving a batch. (m_bufferSize x #ResultStreams) */
  char* m_batchBuffer;
  /** User specified buffer for final storage of result.*/
  char* m_resultBuffer;
  /** User specified pointer to application pointer that should be 
   * set to point to the current row inside m_batchBuffer
   * @see NdbQueryOperationImpl::setResultRowRef */
  const char** m_resultRef;
  /** True if this operation gave no result for the current row.*/
  bool m_isRowNull;

  /** Result record & optional bitmask to disable read of selected cols.*/
  const NdbRecord* m_ndbRecord;
  const unsigned char* m_read_mask;

  /** Head & tail of NdbRecAttr list defined by this operation.
    * Used for old-style result retrieval (using getValue()).*/
  NdbRecAttr* m_firstRecAttr;
  NdbRecAttr* m_lastRecAttr;

  /** Ordering of scan results (only applies to ordered index scans.)*/
  NdbQueryOptions::ScanOrdering m_ordering;

  /** A scan filter is mapped to an interpeter code program, which is stored
   * here. (This field is NULL if no scan filter has been defined.)*/
  NdbInterpretedCode* m_interpretedCode;

  /** True if this operation reads from any disk column. */
  bool m_diskInUserProjection;

  /** Number of scan fragments to read in parallel.
   */
  Uint32 m_parallelism;
  
  explicit NdbQueryOperationImpl(NdbQueryImpl& queryImpl, 
                                 const NdbQueryOperationDefImpl& def);
  ~NdbQueryOperationImpl();

  /** A complete batch has been received for a given root fragment
   *  Update whatever required before the appl. is allowed to navigate the result.
   */ 
  void handleBatchComplete(Uint32 rootFragNo);

  /** Copy NdbRecAttr and/or NdbRecord results from stream into appl. buffers */
  void fetchRow(const NdbResultStream& resultStream);

  /** Set result for this operation and all its descendand child 
   *  operations to NULL.
   */
  void nullifyResult();

  /** Release resources after scan has returned last available result */
  void postFetchRelease();

  /** Count number of descendant operations (excluding the operation itself) */
  Int32 getNoOfDescendantOperations() const;

  /**
   * Count number of leaf operations below/including self
   */
  Uint32 getNoOfLeafOperations() const;

  /** Serialize parameter values.
   *  @return possible error code.*/
  int serializeParams(const NdbQueryParamValue* paramValues);

  int serializeProject(Uint32Buffer& attrInfo);

  Uint32 calculateBatchedRows(NdbQueryOperationImpl* closestScan);
  void setBatchedRows(Uint32 batchedRows);

  /** Construct and prepare receiver streams for result processing. */
  int prepareReceiver();

  /** Prepare ATTRINFO for execution. (Add execution params++)
   *  @return possible error code.*/
  int prepareAttrInfo(Uint32Buffer& attrInfo);

  /**
   * Expand keys and bounds for the root operation into the KEYINFO section.
   * @param keyInfo Actuall KEYINFO section the key / bounds are 
   *                put into
   * @param actualParam Instance values for NdbParamOperands.
   * Returns: 0 if OK, or possible an errorcode.
   */
  int prepareKeyInfo(Uint32Buffer& keyInfo,
                     const NdbQueryParamValue* actualParam);

  int prepareLookupKeyInfo(
                     Uint32Buffer& keyInfo,
                     const NdbQueryOperandImpl* const keys[],
                     const NdbQueryParamValue* actualParam);

  int prepareIndexKeyInfo(
                     Uint32Buffer& keyInfo,
                     const NdbQueryOperationDefImpl::IndexBound* bounds,
                     const NdbQueryParamValue* actualParam);

  /** Return I-value (for putting in object map) for a receiver pointing back 
   * to this object. TCKEYCONF is processed by first looking up an 
   * NdbReceiver instance in the object map, and then following 
   * NdbReceiver::m_query_operation_impl here.*/
  Uint32 getIdOfReceiver() const;
  
  /** 
   * If the operation has a scan filter, append the corresponding
   * interpreter code to a buffer.
   * @param attrInfo The buffer to which the code should be appended.
   * @return possible error code */
  int prepareInterpretedCode(Uint32Buffer& attrInfo) const;

  /** Returns true if this operation reads from any disk column. */
  bool diskInUserProjection() const
  { return m_diskInUserProjection; }

}; // class NdbQueryOperationImpl


#endif
