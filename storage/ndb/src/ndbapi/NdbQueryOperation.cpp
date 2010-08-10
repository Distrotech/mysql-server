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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/


#include <ndb_global.h>
#include <NdbQueryBuilder.hpp>
#include "NdbQueryBuilderImpl.hpp"

#include "NdbQueryOperationImpl.hpp"

#include <signaldata/TcKeyReq.hpp>
#include <signaldata/TcKeyRef.hpp>
#include <signaldata/ScanTab.hpp>
#include <signaldata/QueryTree.hpp>
#include <signaldata/DbspjErr.hpp>

#include "AttributeHeader.hpp"
#include "NdbRecord.hpp"
#include "NdbRecAttr.hpp"
#include "TransporterFacade.hpp"
#include "NdbApiSignal.hpp"
#include "NdbTransaction.hpp"
#include "NdbInterpretedCode.hpp"
#include "NdbScanFilter.hpp"

#include <Bitmask.hpp>

#if 0
#define DEBUG_CRASH() assert(false)
#else
#define DEBUG_CRASH()
#endif

//#define TEST_SCANREQ

/* Various error codes that are not specific to NdbQuery. */
STATIC_CONST(Err_TupleNotFound = 626);
STATIC_CONST(Err_MemoryAlloc = 4000);
STATIC_CONST(Err_SendFailed = 4002);
STATIC_CONST(Err_UnknownColumn = 4004);
STATIC_CONST(Err_ReceiveFromNdbFailed = 4008);
STATIC_CONST(Err_NodeFailCausedAbort = 4028);
STATIC_CONST(Err_WrongFieldLength = 4209);
STATIC_CONST(Err_MixRecAttrAndRecord = 4284);
STATIC_CONST(Err_InvalidRangeNo = 4286);
STATIC_CONST(Err_DifferentTabForKeyRecAndAttrRec = 4287);
STATIC_CONST(Err_KeyIsNULL = 4316);
STATIC_CONST(Err_FinaliseNotCalled = 4519);
STATIC_CONST(Err_InterpretedCodeWrongTab = 4524);

/* A 'void' index for a tuple in internal parent / child correlation structs .*/
static const Uint16 tupleNotFound = 0xffff;

/** Set to true to trace incomming signals.*/
const bool traceSignals = false;

/**
 * A class for accessing the correlation data at the end of a tuple (for 
 * scan queries). These data have the following layout:
 *
 * Word 0: AttributeHeader
 * Word 1: Id of receiver for root operation (where the ancestor tuple of this
 *         tuple will go).
 * Word 2, upper halfword: tuple id of parent tuple.
 * Word 2, lower halfword: tuple id of this tuple.
 *
 * Both tuple identifiers are unique within this batch and root fragment.
 * With these identifiers, it is possible to relate a tuple to its parent and 
 * children. That way, results for child operations can be updated correctly
 * when the application iterates over the results of the root scan operation.
 */
class CorrelationData
{
public:
  static const Uint32 wordCount = 3;

  explicit CorrelationData(const Uint32* tupleData, Uint32 tupleLength):
    m_corrPart(tupleData + tupleLength - wordCount)
  {
    assert(tupleLength >= wordCount);
    assert(AttributeHeader(m_corrPart[0]).getAttributeId() 
           == AttributeHeader::READ_ANY_VALUE);
    assert(AttributeHeader(m_corrPart[0]).getByteSize() == 2*sizeof(Uint32));
    assert(getTupleId()<tupleNotFound);
    assert(getParentTupleId()<tupleNotFound);
  }

  Uint32 getRootReceiverId() const
  { return m_corrPart[1];}

  Uint16 getTupleId() const
  { return m_corrPart[2] & 0xffff;}

  Uint16 getParentTupleId() const
  { return m_corrPart[2] >> 16;}

private:
  const Uint32* const m_corrPart;
}; // class CorrelationData
 
/**
 * If a query has a scan operation as its root, then that scan will normally 
 * read from several fragments of its target table. Each such root fragment
 * scan, along with any child lookup operations that are spawned from it,
 * runs independently, in the sense that:
 * - The API will know when it has received all data from a fragment for a 
 *   given batch and all child operations spawned from it.
 * - When one fragment is complete (for a batch) the API will make these data
 *   avaliable to the application, even if other fragments are not yet complete.
 * - The tuple identifiers that are used for matching children with parents are
 *   only guaranteed to be unique within one batch, operation, and root 
 *   operation fragment. Tuples derived from different root fragments must 
 *   thus be kept apart.
 * 
 * This class manages the state of one such read operation, from one particular
 * fragment of the target table of the root operation. If the root operation
 * is a lookup, then there will be only one instance of this class.
 */
class NdbRootFragment {
public:
  explicit NdbRootFragment();

  /**
   * Initialize object.
   * @param query Enclosing query.
   * @param fragNo This object manages state for reading from the fragNo'th 
   * fragment that the root operation accesses.
   */
  void init(NdbQueryImpl& query, Uint32 fragNo); 

  Uint32 getFragNo() const
  { return m_fragNo; }

  /**
   * Prepare for receiving another batch.
   */
  void reset();

  void incrOutstandingResults(Int32 delta)
  {
    m_outstandingResults += delta;
  }

  void clearOutstandingResults()
  {
    m_outstandingResults = 0;
  }

  void setConfReceived();

  /** 
   * The root operation will read from a number of fragments of a table.
   * This method checks if all results for the current batch has been 
   * received for a given fragment. This includes both results for the root
   * operation and any child operations. Note that child operations may access
   * other fragments; the fragment number only refers to what 
   * the root operation does.
   *
   * @return True if current batch is complete for this fragment.
   */
  bool isFragBatchComplete() const
  { 
    assert(m_fragNo!=voidFragNo);
    return m_confReceived && m_outstandingResults==0; 
  }

  /**
   * Get the result stream that handles results dervied from this root 
   * fragment for a particular operation.
   * @param operationNo The id of the operation.
   * @return The result stream for this root fragment.
   */
  NdbResultStream& getResultStream(Uint32 operationNo) const
  { return m_query->getQueryOperation(operationNo).getResultStream(m_fragNo); }
  
  /**
   * @return True if there are no more batches to be received for this fragment.
   */
  bool finalBatchReceived() const;

  /**
   * @return True if there are no more results from this root fragment (for 
   * the current batch).
   */
  bool isEmpty() const;

private:
  STATIC_CONST( voidFragNo = 0xffffffff);

  /** Enclosing query.*/
  NdbQueryImpl* m_query;

  /** Number of the root operation fragment.*/
  Uint32 m_fragNo;

  /**
   * The number of outstanding TCKEYREF or TRANSID_AI 
   * messages for the fragment. This includes both messages related to the
   * root operation and any descendant operation that was instantiated as
   * a consequence of tuples found by the root operation.
   * This number may temporarily be negative if e.g. TRANSID_AI arrives before 
   * SCAN_TABCONF. 
   */
  Int32 m_outstandingResults;

  /**
   * This is an array with one element for each fragment that the root
   * operation accesses (i.e. one for a lookup, all for a table scan).
   *
   * Each element is true iff a SCAN_TABCONF (for that fragment) or 
   * TCKEYCONF message has been received */
  bool m_confReceived;
}; //NdbRootFragment


/** 
 * This class manages the subset of result data for one operation that is 
 * derived from one fragment of the root operation. Note that the result tuples
 * may come from any fragment, but they all have initial ancestors from the 
 * same fragment of the root operation.  
 * For each operation there will thus be one NdbResultStream for each fragment
 * that the root operation reads from (one in the case of lookups.)
 * This class has an NdbReceiver object for processing tuples as well as 
 * structures for correlating child and parent tuples.
 */
class NdbResultStream {
  friend bool NdbRootFragment::isEmpty() const;
public:

  /** Outcome when asking for a new now from a scan.*/
  enum ScanRowResult
  {
    /** Undefined value, for variable initialization etc..*/
    ScanRowResult_void,
    /** There was a new row.*/
    ScanRowResult_gotRow,
    /** There are no more rows.*/
    ScanRowResult_endOfScan,
    /** There are no more rows in the current batch, but there may be more 
     * in subsequent batches.*/
    ScanRowResult_endOfBatch
  };

  /**
   * @param operation The operation for which we will receive results.
   * @param rootFragNo 0..n-1 when the root operation reads from n fragments.
   */
  explicit NdbResultStream(NdbQueryOperationImpl& operation, Uint32 rootFragNo);

  ~NdbResultStream();

  /** 
   * Prepare for receiving first results. 
   * @return possible error code. 
   */
  int prepare();

  /** Prepare for receiving next batch of scan results. */
  void reset();
    
  /**
   * 0..n-1 if the root operation reads from n fragments. This stream holds data
   * derived from one of those fragments.
   */
  Uint32 getRootFragNo() const
  { return m_rootFragNo; }

  NdbReceiver& getReceiver()
  { return m_receiver; }

  const NdbReceiver& getReceiver() const
  { return m_receiver; }

  Uint32 getRowCount() const
  { return m_rowCount; }

  /**
   * Process an incomming tuple for this stream. Extract parent and own tuple 
   * ids and pass it on to m_receiver.
   *
   * @param ptr buffer holding tuple.
   * @param len buffer length.
   */
  void execTRANSID_AI(const Uint32 *ptr, Uint32 len);

  /** A complete batch has been received for a fragment on this NdbResultStream,
   *  Update whatever required before the appl. are allowed to navigate the result.
   */ 
  void handleBatchComplete();

  /**
   * Get the next row for an operation that is either a scan itself or a 
   * descendant of a scan.
   * @param parentId Tuple id of parent tuple (or tupleNotFound if this is the
   * root operation.
   */
  ScanRowResult getNextScanRow(Uint16 parentId);

  /** For debugging.*/
  friend NdbOut& operator<<(NdbOut& out, const NdbResultStream&);

private:
  /** This stream handles results derived from the m_rootFragNo'th 
   * fragment of the root operation.*/
  const Uint32 m_rootFragNo;

  /** The receiver object that unpacks transid_AI messages.*/
  NdbReceiver m_receiver;

  /** Max #rows which this stream may recieve in its buffer structures */
  Uint32 m_maxRows;

  /** The number of transid_AI messages received.*/
  Uint32 m_rowCount;

  /** Operation to which this resultStream belong.*/
  NdbQueryOperationImpl& m_operation;

  /** This is the state of the iterator used by getNextScanRow().*/
  enum
  {
    /** The first row has not been fetched yet.*/
    Iter_notStarted,
    /** Get next row from child operation, and keep the current tuple for 
     * this operation..*/
    Iter_stepChild,
    /** Get next row from this operation.*/
    Iter_stepSelf,
    /** Last row for current batch has been returned.*/
    Iter_finished
  } m_iterState;

  /** This is the tuple id of the current parent tuple (when iterating using 
   * getNextScanRow())*/
  Uint16 m_currentParentId;
  
  /** This is the current row number (when iterating using getNextScanRow())*/
  Uint16 m_currentRow; // FIXME: Use NdbReciver::m_current_row instead???

  /**
   * TupleSet contain two logically distinct set of information:
   *
   *  - Child/Parent correlation set required to correlate
   *    child tuples with its parents. Child/Tuple pairs are indexed
   *    by tuple number which is the same as the order in which tuples
   *    appear in the NdbReceiver buffers.
   *
   *  - A HashMap on 'm_parentId' used to locate tuples correlated
   *    to a parent tuple. Indexes by hashing the parentId such that:
   *     - [hash(parentId)].m_hash_head will then index the first
   *       TupleSet entry potential containing the parentId to locate.
   *     - .m_hash_next in the indexed TupleSet may index the next TupleSet 
   *       to considder.
   *       
   * Both the child/parent correlation set and the parentId HashMap has been
   * folded into the same structure on order to reduce number of objects 
   * being dynamically allocated. 
   * As an advantage this results in an autoscaling of the hash bucket size .
   *
   * Structure is only present if 'isScanQuery'
   */
  class TupleSet {
  public:
                        // Tuple ids are unique within this batch and stream
    Uint16 m_parentId;  // Id of parent tuple which this tuple is correlated with
    Uint16 m_tupleId;   // Id of this tuple

    Uint16 m_hash_head; // Index of first item in TupleSet[] matching a hashed parentId.
    Uint16 m_hash_next; // 'next' index matching 

    explicit TupleSet()
    {}

  private:
    /** No copying.*/
    TupleSet(const TupleSet&);
    TupleSet& operator=(const TupleSet&);
  };

  TupleSet* m_tupleSet;

  void clearParentChildMap();

  void setParentChildMap(Uint16 parentId,
                         Uint16 tupleId, 
                         Uint16 tupleNo);

  Uint16 getTupleId(Uint16 tupleNo) const
  { return m_tupleSet[tupleNo].m_tupleId; }

  Uint16 findTupleWithParentId(Uint16 parentId);

  Uint16 findNextTuple();

  /** No copying.*/
  NdbResultStream(const NdbResultStream&);
  NdbResultStream& operator=(const NdbResultStream&);
}; //class NdbResultStream

////////////////////////////////////////////////
/////////  NdbResultStream methods ///////////
////////////////////////////////////////////////

NdbResultStream::NdbResultStream(NdbQueryOperationImpl& operation, Uint32 rootFragNo):
  m_rootFragNo(rootFragNo),
  m_receiver(operation.getQuery().getNdbTransaction().getNdb(), &operation),  // FIXME? Use Ndb recycle lists
  m_maxRows(0),
  m_rowCount(0),
  m_operation(operation),
  m_iterState(Iter_notStarted),
  m_currentParentId(tupleNotFound),
  m_currentRow(tupleNotFound),
  m_tupleSet(NULL)
{};

NdbResultStream::~NdbResultStream() { 
  delete[] m_tupleSet; 
}

int  // Return 0 if ok, else errorcode
NdbResultStream::prepare()
{
  /* Parent / child correlation is only relevant for scan type queries
   * Don't create m_parentTupleId[] and m_childTupleIdx[] for lookups!
   * Neither is these structures required for operations not having respective
   * child or parent operations.
   */
  if (m_operation.getQueryDef().isScanQuery())
  {
    m_maxRows  = m_operation.getMaxBatchRows();
    m_tupleSet = new TupleSet[m_maxRows];
    if (unlikely(m_tupleSet==NULL))
      return Err_MemoryAlloc;

    clearParentChildMap();
  }
  else
    m_maxRows = 1;

  return 0;
} //NdbResultStream::prepare


void
NdbResultStream::reset()
{
  assert (m_operation.getQueryDef().isScanQuery());

  // Root scan-operation need a ScanTabConf to complete
  m_rowCount = 0;
  m_iterState = Iter_notStarted;
  m_currentRow = 0;

  clearParentChildMap();

  m_receiver.prepareSend();
} //NdbResultStream::reset


void
NdbResultStream::clearParentChildMap()
{
  assert (m_operation.getQueryDef().isScanQuery());
  for (Uint32 i=0; i<m_maxRows; i++)
  {
    m_tupleSet[i].m_parentId = tupleNotFound;
    m_tupleSet[i].m_tupleId  = tupleNotFound;
    m_tupleSet[i].m_hash_head = tupleNotFound;
  }
}

void
NdbResultStream::setParentChildMap(Uint16 parentId,
                                   Uint16 tupleId, 
                                   Uint16 tupleNo)
{
  assert (m_operation.getQueryDef().isScanQuery());
  assert (tupleNo < m_maxRows);
  for (Uint32 i = 0; i < tupleNo; i++)
  {
    // Check that tuple id is unique.
    assert (m_tupleSet[i].m_tupleId != tupleId); 
  }
  m_tupleSet[tupleNo].m_parentId = parentId;
  m_tupleSet[tupleNo].m_tupleId  = tupleId;

  /* Insert parentId in HashMap */
  if (parentId != tupleNotFound)
  {
    const Uint16 hash = (parentId % m_maxRows);
    m_tupleSet[tupleNo].m_hash_next = m_tupleSet[hash].m_hash_head;
    m_tupleSet[hash].m_hash_head  = tupleNo;
  }
}

Uint16
NdbResultStream::findTupleWithParentId(Uint16 parentId)
{
  assert (m_operation.getQueryDef().isScanQuery());
  m_currentParentId = parentId;

  const Uint16 hash = (parentId % m_maxRows);
  m_currentRow = m_tupleSet[hash].m_hash_head;
  while (m_currentRow != tupleNotFound)
  {
    assert(m_currentRow < m_maxRows);
    if (m_tupleSet[m_currentRow].m_parentId == parentId)
      return m_currentRow;

    m_currentRow = m_tupleSet[m_currentRow].m_hash_next;
  }
  return tupleNotFound;
}

Uint16
NdbResultStream::findNextTuple()
{
  assert (m_operation.getQueryDef().isScanQuery());

  while (m_currentRow != tupleNotFound)
  {
    assert(m_currentRow < m_maxRows);
    m_currentRow = m_tupleSet[m_currentRow].m_hash_next;

    if (m_currentRow != tupleNotFound &&
        m_tupleSet[m_currentRow].m_parentId == m_currentParentId)
      return m_currentRow;
  }
  return tupleNotFound;
}

void
NdbResultStream::execTRANSID_AI(const Uint32 *ptr, Uint32 len)
{
  if (m_operation.getQueryDef().isScanQuery())
  {
    const CorrelationData correlData(ptr, len);

    assert(m_operation.getRoot().getResultStream(m_rootFragNo)
           .m_receiver.getId() == correlData.getRootReceiverId());

    m_receiver.execTRANSID_AI(ptr, len - CorrelationData::wordCount);

    /**
     * Keep correlation data between parent and child tuples.
     * Since tuples may arrive in any order, we cannot match
     * parent and child until all tuples (for this batch and 
     * root fragment) have arrived.
     */
    setParentChildMap(m_operation.getParentOperation()!=NULL ? correlData.getParentTupleId() : tupleNotFound,
                      correlData.getTupleId(),
                      m_rowCount);
  }
  else
  {
    // Lookup query.
    m_receiver.execTRANSID_AI(ptr, len);
  }
  m_rowCount++;
} // NdbResultStream::execTRANSID_AI()


void 
NdbResultStream::handleBatchComplete()
{
  /* Now we have received all tuples for all operations. 
   * Set correct #rows received in the NdbReceiver.
   */
  getReceiver().m_result_rows = getRowCount();
}

NdbResultStream::ScanRowResult
NdbResultStream::getNextScanRow(Uint16 parentId)
{
  const bool isRoot = 
    m_operation.getQueryOperationDef().getQueryOperationIx() == 0;

  /* Restart iteration if parent id has changed.*/
  if (!isRoot && m_currentParentId != parentId)
  {
    assert (parentId != tupleNotFound);
    m_currentParentId = parentId;
    m_iterState = Iter_notStarted;
  }        
  assert(m_iterState != Iter_finished);

  /* Loop until we either find a suitable set of rows for this operation
   * and its descendants, or until we decide that there are no more matches
   * for this batch and root fragment.
   */
  while (true)
  {
    if (m_iterState == Iter_notStarted)
    {
      // Fetch first row for this stream
      if (isRoot)
      {
        m_currentRow = 0;
        if (getReceiver().m_result_rows == 0)
        {
          m_iterState = Iter_finished;
        }
        else
        {
          m_iterState = Iter_stepSelf;
        }
      }
      else
      {
        assert (parentId != tupleNotFound);
        m_currentParentId = parentId;
        if (findTupleWithParentId(parentId) == tupleNotFound)
        {
          m_iterState = Iter_finished;
        }
        else
        {
          m_iterState = Iter_stepSelf;
        }
      }
    }
    else if (m_iterState == Iter_stepSelf)
    {
      // Fetch next row for this stream
      if (isRoot)
      {
        m_currentRow++;
        if (m_currentRow >= getReceiver().m_result_rows)
        {
          m_iterState = Iter_finished;
        }
      }
      else
      {
        if (findNextTuple() == tupleNotFound)
        {
          m_iterState = Iter_finished;
        }
      } 
    }

    if (m_iterState == Iter_finished)
    {
      if (isRoot)
      {
        return ScanRowResult_endOfScan;
      }
      else
      {
#if 0
        return ScanRowResult_endOfBatch;
#else
        // FIXME!!! (Handle left outer join properly.)
        m_operation.nullifyResult();
        return ScanRowResult_endOfScan;
#endif
      }
    }

    bool childRowsOk = true;
    Uint32 childNo = 0;
    Uint32 childrenWithRows = 0;
    /* Call recursively for the children of this operation.*/
    while (childRowsOk && childNo < m_operation.getNoOfChildOperations())
    {
      NdbQueryOperationImpl& child = m_operation.getChildOperation(childNo);
      
      /* If we fetched a new row for this operation, then we should get a
       * new row for each child also.
       * If we did not fetch a new row for this operation, then we should only
       * get new rows for the child which has a scan desecendant (if there
       * is such a child.)*/
      if (m_iterState == Iter_stepSelf 
          || child.getQueryOperationDef().hasScanDescendant())
      {
        switch(child.getResultStream(m_rootFragNo)
               .getNextScanRow(getTupleId(m_currentRow)))
        {
        case ScanRowResult_gotRow:
          childrenWithRows++;
          break;
        case ScanRowResult_endOfScan:
          if (child.getQueryOperationDef().getMatchType() 
              != NdbQueryOptions::MatchAll)
          {
            /* This must be an inner join.*/
            childRowsOk = false;
          }
          break;
        case ScanRowResult_endOfBatch:
          childRowsOk = false;
          break;
        default:
          assert(false);
        }
      }
      childNo++;
    }
    if (m_iterState != Iter_stepSelf && childrenWithRows == 0)
    {
      /* A row with "NULL" children should have been issued on the last call.
       * We should fetch the next row for this operation.*/
      childRowsOk = false;
    }

    if (!childRowsOk || childrenWithRows == 0)
    {
      // Fetch a new row for this operation next time.
      m_iterState = Iter_stepSelf;
    }
    else
    {
      // Keep iterating over child rows.
      m_iterState = Iter_stepChild;
    }

    if (childRowsOk)
    {
      getReceiver().setCurrentRow(m_currentRow);
      m_operation.fetchRow(*this);
      return ScanRowResult_gotRow;
    }
  } // while(true)
} //NdbResultStream::getNextScanRow()


///////////////////////////////////////////
/////////  NdbRootFragment methods ///////////
///////////////////////////////////////////
NdbRootFragment::NdbRootFragment():
  m_query(NULL),
  m_fragNo(voidFragNo),
  m_outstandingResults(0),
  m_confReceived(false)
{
}


void NdbRootFragment::init(NdbQueryImpl& query, Uint32 fragNo)
{
  assert(m_fragNo==voidFragNo);
  m_query = &query;
  m_fragNo = fragNo;
}

void NdbRootFragment::reset()
{
  assert(m_fragNo!=voidFragNo);
  assert(m_outstandingResults == 0);
  assert(m_confReceived);
  m_confReceived = false;
}

void NdbRootFragment::setConfReceived()
{ 
  /* For a query with a lookup root, there may be more than one TCKEYCONF
     message. For a scan, there should only be one SCAN_TABCONF per root
     fragment. 
  */
  assert(!m_query->getQueryDef().isScanQuery() || !m_confReceived);
  m_confReceived = true; 
}

bool NdbRootFragment::finalBatchReceived() const
{
  return getResultStream(0).getReceiver().m_tcPtrI==RNIL;
}


bool  NdbRootFragment::isEmpty() const
{ 
  if (m_query->getQueryDef().isScanQuery())
  {
    return m_query->getQueryOperation(0U).getResultStream(m_fragNo)
      .m_iterState == NdbResultStream::Iter_finished;
  }
  else
  {
    return !m_query->getQueryOperation(0U).getReceiver(m_fragNo).nextResult();
  }
}


///////////////////////////////////////////
/////////  NdbQuery API methods ///////////
///////////////////////////////////////////

NdbQuery::NdbQuery(NdbQueryImpl& impl):
  m_impl(impl)
{}

NdbQuery::~NdbQuery()
{}

Uint32
NdbQuery::getNoOfOperations() const
{
  return m_impl.getNoOfOperations();
}

NdbQueryOperation*
NdbQuery::getQueryOperation(Uint32 index) const
{
  return &m_impl.getQueryOperation(index).getInterface();
}

NdbQueryOperation*
NdbQuery::getQueryOperation(const char* ident) const
{
  NdbQueryOperationImpl* op = m_impl.getQueryOperation(ident);
  return (op) ? &op->getInterface() : NULL;
}

Uint32
NdbQuery::getNoOfParameters() const
{
  return m_impl.getNoOfParameters();
}

const NdbParamOperand*
NdbQuery::getParameter(const char* name) const
{
  return m_impl.getParameter(name);
}

const NdbParamOperand*
NdbQuery::getParameter(Uint32 num) const
{
  return m_impl.getParameter(num);
}

int
NdbQuery::setBound(const NdbRecord *keyRecord,
                   const NdbIndexScanOperation::IndexBound *bound)
{
  const int error = m_impl.setBound(keyRecord,bound);
  if (unlikely(error)) {
    m_impl.setErrorCodeAbort(error);
    return -1;
  } else {
    return 0;
  }
}

NdbQuery::NextResultOutcome
NdbQuery::nextResult(bool fetchAllowed, bool forceSend)
{
  return m_impl.nextResult(fetchAllowed, forceSend);
}

void
NdbQuery::close(bool forceSend)
{
  m_impl.close(forceSend);
}

NdbTransaction*
NdbQuery::getNdbTransaction() const
{
  return &m_impl.getNdbTransaction();
}

const NdbError& 
NdbQuery::getNdbError() const {
  return m_impl.getNdbError();
};

int NdbQuery::isPrunable(bool& prunable) const
{
  return m_impl.isPrunable(prunable);
}

NdbQueryOperation::NdbQueryOperation(NdbQueryOperationImpl& impl)
  :m_impl(impl)
{}
NdbQueryOperation::~NdbQueryOperation()
{}

Uint32
NdbQueryOperation::getNoOfParentOperations() const
{
  return m_impl.getNoOfParentOperations();
}

NdbQueryOperation*
NdbQueryOperation::getParentOperation(Uint32 i) const
{
  return &m_impl.getParentOperation(i).getInterface();
}

Uint32 
NdbQueryOperation::getNoOfChildOperations() const
{
  return m_impl.getNoOfChildOperations();
}

NdbQueryOperation* 
NdbQueryOperation::getChildOperation(Uint32 i) const
{
  return &m_impl.getChildOperation(i).getInterface();
}

const NdbQueryOperationDef&
NdbQueryOperation::getQueryOperationDef() const
{
  return m_impl.getQueryOperationDef().getInterface();
}

NdbQuery& 
NdbQueryOperation::getQuery() const {
  return m_impl.getQuery().getInterface();
};

NdbRecAttr*
NdbQueryOperation::getValue(const char* anAttrName,
			    char* resultBuffer)
{
  return m_impl.getValue(anAttrName, resultBuffer);
}

NdbRecAttr*
NdbQueryOperation::getValue(Uint32 anAttrId, 
			    char* resultBuffer)
{
  return m_impl.getValue(anAttrId, resultBuffer);
}

NdbRecAttr*
NdbQueryOperation::getValue(const NdbDictionary::Column* column, 
			    char* resultBuffer)
{
  if (unlikely(column==NULL)) {
    m_impl.getQuery().setErrorCode(QRY_REQ_ARG_IS_NULL);
    return NULL;
  }
  return m_impl.getValue(NdbColumnImpl::getImpl(*column), resultBuffer);
}

int
NdbQueryOperation::setResultRowBuf (
                       const NdbRecord *rec,
                       char* resBuffer,
                       const unsigned char* result_mask)
{
  if (unlikely(rec==0 || resBuffer==0)) {
    m_impl.getQuery().setErrorCode(QRY_REQ_ARG_IS_NULL);
    return -1;
  }
  return m_impl.setResultRowBuf(rec, resBuffer, result_mask);
}

int
NdbQueryOperation::setResultRowRef (
                       const NdbRecord* rec,
                       const char* & bufRef,
                       const unsigned char* result_mask)
{
  // FIXME: Errors must be set in the NdbError object owned by this operation.
  if (unlikely(rec==0)) {
    m_impl.getQuery().setErrorCode(QRY_REQ_ARG_IS_NULL);
    return -1;
  }
  return m_impl.setResultRowRef(rec, bufRef, result_mask);
}

bool
NdbQueryOperation::isRowNULL() const
{
  return m_impl.isRowNULL();
}

bool
NdbQueryOperation::isRowChanged() const
{
  return m_impl.isRowChanged();
}

int
NdbQueryOperation::setOrdering(NdbQueryOptions::ScanOrdering ordering)
{
  return m_impl.setOrdering(ordering);
}

NdbQueryOptions::ScanOrdering
NdbQueryOperation::getOrdering() const
{
  return m_impl.getOrdering();
}

int NdbQueryOperation::setInterpretedCode(const NdbInterpretedCode& code) const
{
  return m_impl.setInterpretedCode(code);
}

/////////////////////////////////////////////////
/////////  NdbQueryParamValue methods ///////////
/////////////////////////////////////////////////

enum Type
{
  Type_NULL,
  Type_raw,        // Raw data formated according to bound Column format.
  Type_raw_shrink, // As Type_raw, except short VarChar has to be shrinked.
  Type_string,     // '\0' terminated C-type string, char/varchar data only
  Type_Uint16,
  Type_Uint32,
  Type_Uint64,
  Type_Double
};

NdbQueryParamValue::NdbQueryParamValue(Uint16 val) : m_type(Type_Uint16)
{ m_value.uint16 = val; }

NdbQueryParamValue::NdbQueryParamValue(Uint32 val) : m_type(Type_Uint32)
{ m_value.uint32 = val; }

NdbQueryParamValue::NdbQueryParamValue(Uint64 val) : m_type(Type_Uint64)
{ m_value.uint64 = val; }

NdbQueryParamValue::NdbQueryParamValue(double val) : m_type(Type_Double)
{ m_value.dbl = val; }

// C-type string, terminated by '\0'
NdbQueryParamValue::NdbQueryParamValue(const char* val) : m_type(Type_string)
{ m_value.string = val; }

// Raw data
NdbQueryParamValue::NdbQueryParamValue(const void* val, bool shrinkVarChar)
 : m_type(shrinkVarChar ? Type_raw_shrink : Type_raw)
{ m_value.raw = val; }

// NULL-value, also used as optional end marker 
NdbQueryParamValue::NdbQueryParamValue() : m_type(Type_NULL)
{}


int NdbQueryParamValue::getValue(const NdbParamOperandImpl& param,
                                 const void*& addr, Uint32& len,
                                 bool& is_null) const
{
  const NdbColumnImpl* const column  = param.getColumn();
  Uint32 maxSize = column->getSizeInBytes();
  is_null = false;

  // Fetch parameter value and length.
  // Rudimentary typecheck of paramvalue: At least length should be as expected:
  //  - Extend with more types if required
  //  - Considder to add simple type conversion, ex: Int64 -> Int32
  //  - Or 
  //     -- Represent all exact numeric as Int64 and convert to 'smaller' int
  //     -- Represent all floats as Double and convert to smaller floats
  //
  switch(m_type)
  {
    case Type_NULL:
      addr = NULL;
      len  = 0;
      is_null = true;
      return 0;
    case Type_Uint16:
      if (unlikely(column->getType() != NdbDictionary::Column::Smallint &&
                   column->getType() != NdbDictionary::Column::Smallunsigned))
        return QRY_PARAMETER_HAS_WRONG_TYPE;
      addr = &m_value;
      len = static_cast<Uint32>(sizeof(m_value.uint16));
      DBUG_ASSERT(len == maxSize);
      break;
    case Type_Uint32:
      if (unlikely(column->getType() != NdbDictionary::Column::Int &&
                   column->getType() != NdbDictionary::Column::Unsigned))
        return QRY_PARAMETER_HAS_WRONG_TYPE;
      addr = &m_value;
      len = static_cast<Uint32>(sizeof(m_value.uint32));
      DBUG_ASSERT(len == maxSize);
      break;
    case Type_Uint64:
      if (unlikely(column->getType() != NdbDictionary::Column::Bigint &&
                   column->getType() != NdbDictionary::Column::Bigunsigned))
        return QRY_PARAMETER_HAS_WRONG_TYPE;
      addr = &m_value;
      len = static_cast<Uint32>(sizeof(m_value.uint64));
      DBUG_ASSERT(len == maxSize);
      break;
    case Type_Double:
      if (unlikely(column->getType() != NdbDictionary::Column::Double))
        return QRY_PARAMETER_HAS_WRONG_TYPE;
      addr = &m_value;
      len = static_cast<Uint32>(sizeof(m_value.dbl));
      DBUG_ASSERT(len == maxSize);
      break;
    case Type_string:
      if (unlikely(column->getType() != NdbDictionary::Column::Char &&
                   column->getType() != NdbDictionary::Column::Varchar &&
                   column->getType() != NdbDictionary::Column::Longvarchar))
        return QRY_PARAMETER_HAS_WRONG_TYPE;
      addr = m_value.string;
      len  = static_cast<Uint32>(strlen(m_value.string));
      if (unlikely(len > maxSize))
        return QRY_CHAR_PARAMETER_TRUNCATED;
      break;

    case Type_raw:
      // 'Raw' data is readily formated according to the bound column 
      if (likely(column->m_arrayType == NDB_ARRAYTYPE_FIXED))
      {
        len  = maxSize;
        addr = m_value.raw;
      }
      else if (column->m_arrayType == NDB_ARRAYTYPE_SHORT_VAR)
      {
        len  = *((Uint8*)(m_value.raw));
        addr = ((Uint8*)m_value.raw)+1;

        DBUG_ASSERT(column->getType() == NdbDictionary::Column::Varchar ||
                    column->getType() == NdbDictionary::Column::Varbinary);
        if (unlikely(len > static_cast<Uint32>(column->getLength())))
          return QRY_CHAR_PARAMETER_TRUNCATED;
      }
      else if (column->m_arrayType == NDB_ARRAYTYPE_MEDIUM_VAR)
      {
        len  = uint2korr((Uint8*)m_value.raw);
        addr = ((Uint8*)m_value.raw)+2;

        DBUG_ASSERT(column->getType() == NdbDictionary::Column::Longvarchar ||
                    column->getType() == NdbDictionary::Column::Longvarbinary);
        if (unlikely(len > static_cast<Uint32>(column->getLength())))
          return QRY_CHAR_PARAMETER_TRUNCATED;
      }
      else
      {
        DBUG_ASSERT(0);
      }
      break;

    case Type_raw_shrink:
      // Only short VarChar can be shrinked
      if (unlikely(column->m_arrayType != NDB_ARRAYTYPE_SHORT_VAR))
        return QRY_PARAMETER_HAS_WRONG_TYPE;

      DBUG_ASSERT(column->getType() == NdbDictionary::Column::Varchar ||
                  column->getType() == NdbDictionary::Column::Varbinary);

      len  = uint2korr((Uint8*)m_value.raw);
      addr = ((Uint8*)m_value.raw)+2;

      if (unlikely(len > static_cast<Uint32>(column->getLength())))
        return QRY_CHAR_PARAMETER_TRUNCATED;
      break;

    default:
      assert(false);
  }
  return 0;
} // NdbQueryParamValue::getValue

///////////////////////////////////////////
/////////  NdbQueryImpl methods ///////////
///////////////////////////////////////////

NdbQueryImpl::NdbQueryImpl(NdbTransaction& trans, 
                           const NdbQueryDefImpl& queryDef):
  m_interface(*this),
  m_state(Initial),
  m_tcState(Inactive),
  m_next(NULL),
  m_queryDef(queryDef),
  m_error(),
  m_transaction(trans),
  m_scanTransaction(NULL),
  m_operations(0),
  m_countOperations(0),
  m_scanCount(0),
  m_pendingFrags(0),
  m_rootFragCount(0),
  m_rootFrags(NULL),
  m_applFrags(),
  m_fullFrags(),
  m_finalBatchFrags(0),
  m_num_bounds(0),
  m_shortestBound(0xffffffff),
  m_attrInfo(),
  m_keyInfo(),
  m_startIndicator(false),
  m_commitIndicator(false),
  m_prunability(Prune_No),
  m_pruneHashVal(0)
{
  // Allocate memory for all m_operations[] in a single chunk
  m_countOperations = queryDef.getNoOfOperations();
  Uint32  size = m_countOperations * 
    static_cast<Uint32>(sizeof(NdbQueryOperationImpl));
  m_operations = static_cast<NdbQueryOperationImpl*> (::operator new(size));
  assert (m_operations);

  // Then; use placement new to construct each individual 
  // NdbQueryOperationImpl object in m_operations
  for (Uint32 i=0; i<m_countOperations; ++i)
  {
    const NdbQueryOperationDefImpl& def = queryDef.getQueryOperation(i);
    new(&m_operations[i]) NdbQueryOperationImpl(*this, def);
  }

  // Serialized QueryTree definition is first part of ATTRINFO.
  m_attrInfo.append(queryDef.getSerialized());
}

NdbQueryImpl::~NdbQueryImpl()
{
 
  // Do this to check that m_queryDef still exists.
  assert(getNoOfOperations() == m_queryDef.getNoOfOperations());

  // NOTE: m_operations[] was allocated as a single memory chunk with
  // placement new construction of each operation.
  // Requires explicit call to d'tor of each operation before memory is free'ed.
  if (m_operations != NULL) {
    for (int i=m_countOperations-1; i>=0; --i)
    { m_operations[i].~NdbQueryOperationImpl();
    }
    ::operator delete(m_operations);
    m_operations = NULL;
  }
  delete[] m_rootFrags;
  m_rootFrags = NULL;
  m_state = Destructed;
}

void
NdbQueryImpl::postFetchRelease()
{
  if (m_operations != NULL) {
    for (unsigned i=0; i<m_countOperations; i++)
    { m_operations[i].postFetchRelease();
    }
  }
}


//static
NdbQueryImpl*
NdbQueryImpl::buildQuery(NdbTransaction& trans, 
                         const NdbQueryDefImpl& queryDef)
{
  if (queryDef.getNoOfOperations()==0) {
    trans.setErrorCode(QRY_HAS_ZERO_OPERATIONS);
    return NULL;
  }

  NdbQueryImpl* const query = new NdbQueryImpl(trans, queryDef);
  if (query==NULL) {
    trans.setOperationErrorCodeAbort(Err_MemoryAlloc);
  }
  assert(query->m_state==Initial);
  return query;
}


/** Assign supplied parameter values to the parameter placeholders
 *  Created when the query was defined.
 *  Values are *copied* into this NdbQueryImpl object:
 *  Memory location used as source for parameter values don't have
 *  to be valid after this assignment.
 */
int
NdbQueryImpl::assignParameters(const NdbQueryParamValue paramValues[])
{
  /**
   * Immediately build the serialized parameter representation in order 
   * to avoid storing param values elsewhere until query is executed.
   * Also calculates prunable property, and possibly its hashValue.
   */
  // Build explicit key/filter/bounds for root operation, possibly refering paramValues
  const int error = getRoot().prepareKeyInfo(m_keyInfo, paramValues);
  if (unlikely(error != 0))
    return error;

  // Serialize parameter values for the other (non-root) operations
  // (No need to serialize for root (i==0) as root key is part of keyInfo above)
  for (Uint32 i=1; i<getNoOfOperations(); ++i)
  {
    if (getQueryDef().getQueryOperation(i).getNoOfParameters() > 0)
    {
      const int error = getQueryOperation(i).serializeParams(paramValues);
      if (unlikely(error != 0))
        return error;
    }
  }
  assert(m_state<Defined);
  m_state = Defined;
  return 0;
} // NdbQueryImpl::assignParameters


static int
insert_bound(Uint32Buffer& keyInfo, const NdbRecord *key_record,
                                              Uint32 column_index,
                                              const char *row,
                                              Uint32 bound_type)
{
  char buf[NdbRecord::Attr::SHRINK_VARCHAR_BUFFSIZE];
  const NdbRecord::Attr *column= &key_record->columns[column_index];

  bool is_null= column->is_null(row);
  Uint32 len= 0;
  const void *aValue= row+column->offset;

  if (!is_null)
  {
    bool len_ok;
    /* Support for special mysqld varchar format in keys. */
    if (column->flags & NdbRecord::IsMysqldShrinkVarchar)
    {
      len_ok= column->shrink_varchar(row, len, buf);
      aValue= buf;
    }
    else
    {
      len_ok= column->get_var_length(row, len);
    }
    if (!len_ok) {
      return Err_WrongFieldLength;
    }
  }

  AttributeHeader ah(column->index_attrId, len);
  keyInfo.append(bound_type);
  keyInfo.append(ah.m_value);
  keyInfo.append(aValue,len);

  return 0;
}


int
NdbQueryImpl::setBound(const NdbRecord *key_record,
                       const NdbIndexScanOperation::IndexBound *bound)
{
  m_prunability = Prune_Unknown;
  if (unlikely(bound==NULL))
    return QRY_REQ_ARG_IS_NULL;

  assert (getRoot().getQueryOperationDef().getType() 
          == NdbQueryOperationDef::OrderedIndexScan);
  int startPos = m_keyInfo.getSize();
//assert (startPos == 0);  // Assumed by ::checkPrunable

  // We don't handle both NdbQueryIndexBound defined in ::scanIndex()
  // in combination with a later ::setBound(NdbIndexScanOperation::IndexBound)
//assert (m_bound.lowKeys==0 && m_bound.highKeys==0);

  if (unlikely(bound->range_no > NdbIndexScanOperation::MaxRangeNo))
  {
 // setErrorCodeAbort(4286);
    return Err_InvalidRangeNo;
  }
  assert (bound->range_no == m_num_bounds);
  m_num_bounds++;

  Uint32 key_count= bound->low_key_count;
  Uint32 common_key_count= key_count;
  if (key_count < bound->high_key_count)
    key_count= bound->high_key_count;
  else
    common_key_count= bound->high_key_count;

  if (m_shortestBound > common_key_count)
  {
    m_shortestBound = common_key_count;
  }
  /* Has the user supplied an open range (no bounds)? */
  const bool openRange= ((bound->low_key == NULL || bound->low_key_count == 0) && 
                         (bound->high_key == NULL || bound->high_key_count == 0));
  if (likely(!openRange))
  {
    /* If low and high key pointers are the same and key counts are
     * the same, we send as an Eq bound to save bandwidth.
     * This will not send an EQ bound if :
     *   - Different numbers of high and low keys are EQ
     *   - High and low keys are EQ, but use different ptrs
     */
    const bool isEqRange= 
      (bound->low_key == bound->high_key) &&
      (bound->low_key_count == bound->high_key_count) &&
      (bound->low_inclusive && bound->high_inclusive); // Does this matter?

    if (isEqRange)
    {
      /* Using BoundEQ will result in bound being sent only once */
      for (unsigned j= 0; j<key_count; j++)
      {
        const int error=
          insert_bound(m_keyInfo, key_record, key_record->key_indexes[j],
                                bound->low_key, NdbIndexScanOperation::BoundEQ);
        if (unlikely(error))
          return error;
      }
    }
    else
    {
      /* Distinct upper and lower bounds, must specify them independently */
      /* Note :  Protocol allows individual columns to be specified as EQ
       * or some prefix of columns.  This is not currently supported from
       * NDBAPI.
       */
      for (unsigned j= 0; j<key_count; j++)
      {
        Uint32 bound_type;
        /* If key is part of lower bound */
        if (bound->low_key && j<bound->low_key_count)
        {
          /* Inclusive if defined, or matching rows can include this value */
          bound_type= bound->low_inclusive  || j+1 < bound->low_key_count ?
            NdbIndexScanOperation::BoundLE : NdbIndexScanOperation::BoundLT;
          const int error=
            insert_bound(m_keyInfo, key_record, key_record->key_indexes[j],
                                  bound->low_key, bound_type);
          if (unlikely(error))
            return error;
        }
        /* If key is part of upper bound */
        if (bound->high_key && j<bound->high_key_count)
        {
          /* Inclusive if defined, or matching rows can include this value */
          bound_type= bound->high_inclusive  || j+1 < bound->high_key_count ?
            NdbIndexScanOperation::BoundGE : NdbIndexScanOperation::BoundGT;
          const int error=
            insert_bound(m_keyInfo, key_record, key_record->key_indexes[j],
                                  bound->high_key, bound_type);
          if (unlikely(error))
            return error;
        }
      }
    }
  }
  else
  {
    /* Open range - all rows must be returned.
     * To encode this, we'll request all rows where the first
     * key column value is >= NULL
     */
    AttributeHeader ah(key_record->columns[0].index_attrId, 0);
    m_keyInfo.append(NdbIndexScanOperation::BoundLE);
    m_keyInfo.append(ah.m_value);
  }

  Uint32 length = m_keyInfo.getSize()-startPos;
  if (unlikely(m_keyInfo.isMemoryExhausted())) {
    return Err_MemoryAlloc;
  } else if (unlikely(length > 0xFFFF)) {
    return QRY_DEFINITION_TOO_LARGE; // Query definition too large.
  } else if (likely(length > 0)) {
    m_keyInfo.put(startPos, m_keyInfo.get(startPos) | (length << 16) | (bound->range_no << 4));
  }

#ifdef TRACE_SERIALIZATION
  ndbout << "Serialized KEYINFO w/ bounds for indexScan root : ";
  for (Uint32 i = startPos; i < m_keyInfo.getSize(); i++) {
    char buf[12];
    sprintf(buf, "%.8x", m_keyInfo.get(i));
    ndbout << buf << " ";
  }
  ndbout << endl;
#endif

  assert(m_state<=Defined);
  m_state = Defined;
  return 0;
} // NdbQueryImpl::setBound()


Uint32
NdbQueryImpl::getNoOfOperations() const
{
  return m_countOperations;
}

Uint32
NdbQueryImpl::getNoOfLeafOperations() const
{
  return getQueryOperation(Uint32(0)).getNoOfLeafOperations();
}

NdbQueryOperationImpl&
NdbQueryImpl::getQueryOperation(Uint32 index) const
{
  assert(index<m_countOperations);
  return m_operations[index];
}

NdbQueryOperationImpl*
NdbQueryImpl::getQueryOperation(const char* ident) const
{
  for(Uint32 i = 0; i<m_countOperations; i++){
    if(strcmp(m_operations[i].getQueryOperationDef().getName(), ident) == 0){
      return &m_operations[i];
    }
  }
  return NULL;
}

Uint32
NdbQueryImpl::getNoOfParameters() const
{
  return 0;  // FIXME
}

const NdbParamOperand*
NdbQueryImpl::getParameter(const char* name) const
{
  return NULL; // FIXME
}

const NdbParamOperand*
NdbQueryImpl::getParameter(Uint32 num) const
{
  return NULL; // FIXME
}

NdbQuery::NextResultOutcome
NdbQueryImpl::nextResult(bool fetchAllowed, bool forceSend)
{
  if (unlikely(m_state < Executing || m_state >= Closed)) {
    assert (m_state >= Initial && m_state < Destructed);
    if (m_state == Failed)
      setErrorCode(QRY_IN_ERROR_STATE);
    else
      setErrorCode(QRY_ILLEGAL_STATE);
    DEBUG_CRASH();
    return NdbQuery::NextResult_error;
  }

  while (m_state != EndOfData) // Or until return when 'gotRow'
  {
    /* To minimize lock contention, each query has two separate root fragment 
     * conatiners (m_fullFrags and m_applFrags). m_applFrags is only
     * accessed by the application thread, so it is safe to use it without 
     * locks.
     */

    if (unlikely(m_applFrags.getCurrent()==NULL))
    {
      /* m_applFrags is empty, so we cannot get more results without 
       * possibly blocking.
       */
      if (fetchAllowed)
      {
        /* fetchMoreResults() will either copy fragments that are already
         * complete (under mutex protection), or block until more data arrives.
         */
        const FetchResult fetchResult = fetchMoreResults(forceSend);
        switch (fetchResult) {
        case FetchResult_otherError:
          assert (m_error.code != 0);
          setErrorCode(m_error.code);
          return NdbQuery::NextResult_error;
        case FetchResult_sendFail:
          // FIXME: copy semantics from NdbScanOperation.
          setErrorCode(Err_NodeFailCausedAbort); // Node fail
          return NdbQuery::NextResult_error;
        case FetchResult_nodeFail:
          setErrorCode(Err_NodeFailCausedAbort); // Node fail
          return NdbQuery::NextResult_error;
        case FetchResult_timeOut:
          setErrorCode(Err_ReceiveFromNdbFailed); // Timeout
          return NdbQuery::NextResult_error;
        case FetchResult_ok:
          break;
        case FetchResult_scanComplete:
          getRoot().nullifyResult();
          return NdbQuery::NextResult_scanComplete;
        default:
          assert(false);
        }
      } else { 
        // There are no more cached records in NdbApi
        return NdbQuery::NextResult_bufferEmpty; 
      }
    }

    NdbQueryOperationImpl& root = getRoot();
    NdbResultStream& rootStream = m_applFrags.getCurrent()->getResultStream(0);
    bool gotRow = false;

    /* Make results from root operation available to the user.*/
    if (m_queryDef.isScanQuery()) {
      gotRow = (rootStream.getNextScanRow(tupleNotFound) 
                == NdbResultStream::ScanRowResult_gotRow);

      /* In case we are doing an ordered index scan, reorder the root fragments
       * such that we get the next record from the right fragment.
       */
      m_applFrags.reorder();
    }
    else // Lookup query
    {
      gotRow = root.fetchLookupResults();
    }

    if (likely(gotRow))  // Row might have been eliminated by apply of inner join
      return NdbQuery::NextResult_gotRow;
  }

  assert (m_state == EndOfData);
  return NdbQuery::NextResult_scanComplete;
} //NdbQueryImpl::nextResult


NdbQueryImpl::FetchResult
NdbQueryImpl::fetchMoreResults(bool forceSend){
  assert(m_applFrags.getCurrent() == NULL);

  /* Check if there are any more completed fragments available.*/
  if(m_queryDef.isScanQuery()){
    
    assert (m_state==Executing);
    assert (m_scanTransaction);

    Ndb* const ndb = m_transaction.getNdb();
    TransporterFacade* const facade = ndb->theImpl->m_transporter_facade;

    /* This part needs to be done under mutex due to synchronization with 
     * receiver thread. */
    PollGuard poll_guard(facade,
                         &ndb->theImpl->theWaiter,
                         ndb->theNdbBlockNumber);

    while (likely(m_error.code==0))
    {
      /* m_fullFrags contains any fragments that are complete (for this batch)
       * but have not yet been moved (under mutex protection) to 
       * m_applFrags.*/
      if (m_fullFrags.size()==0) {
        if (isBatchComplete()) {
          // Request another scan batch, may already be at EOF
          const int sent = sendFetchMore(m_transaction.getConnectedNodeId());
          if (sent==0) {  // EOF reached?
            m_state = EndOfData;
            postFetchRelease();
            return FetchResult_scanComplete;
          } else if (unlikely(sent<0)) {
            return FetchResult_sendFail;
          }
        } //if (isBatchComplete...

        /* More results are on the way, so we wait for them.*/
        const FetchResult waitResult = static_cast<FetchResult>
          (poll_guard.wait_scan(3*facade->m_waitfor_timeout, 
                                m_transaction.getConnectedNodeId(), 
                                forceSend));
        if(waitResult != FetchResult_ok){
          return waitResult;
        }
      } // if (m_fullFrags.top()==NULL)

      NdbRootFragment* frag;
      while ((frag=m_fullFrags.pop()) != NULL) {
        m_applFrags.add(*frag);
      }

      if (m_applFrags.getCurrent() != NULL) {
        return FetchResult_ok;
      }

      /* Getting here is not an error. PollGuard::wait_scan() will return
       * when a complete batch (for a fragment) is available for *any* active 
       * scan in this transaction. So we must wait again for the next arriving 
       * batch.
       */
    } // while(likely(m_error.code==0))

    // 'while' terminated by m_error.code
    assert (m_error.code);
    return FetchResult_otherError;

  } else { // is a Lookup query
    /* The root operation is a lookup. Lookups are guaranteed to be complete
     * before NdbTransaction::execute() returns. Therefore we do not set
     * the lock, because we know that the signal receiver thread will not
     * be accessing  m_fullFrags at this time.
     */
    NdbRootFragment* frag = m_fullFrags.pop();
    if (frag==NULL)
    {
      /* Getting here means that either:
       *  - No results was returned (TCKEYREF)
       *  - or, the application called nextResult() twice for a lookup query.
       */
      m_state = EndOfData;
      postFetchRelease();
      return FetchResult_scanComplete;
    }
    else
    {
      /* Move fragment from receiver thread's container to application 
       *  thread's container.*/
      m_applFrags.add(*frag);
      assert(m_fullFrags.pop()==NULL); // Only one stream for lookups.
      assert(m_applFrags.getCurrent()->getResultStream(0)
             .getReceiver().hasResults());
      return FetchResult_ok;
    }
  } // if(m_queryDef.isScanQuery())
} //NdbQueryImpl::fetchMoreResults

void 
NdbQueryImpl::handleBatchComplete(Uint32 fragNo)
{
  assert(m_rootFrags[fragNo].isFragBatchComplete());
  for (Uint32 i = 0; i<getNoOfOperations(); i++) {
    m_operations[i].m_resultStreams[fragNo]->handleBatchComplete();
  }
}
  
void 
NdbQueryImpl::closeSingletonScans()
{
  assert(!getQueryDef().isScanQuery());
  for(Uint32 i = 0; i<getNoOfOperations(); i++){
    NdbQueryOperationImpl& operation = getQueryOperation(i);
    NdbResultStream& resultStream = *operation.m_resultStreams[0];
    /** Now we have received all tuples for all operations. We can thus call
     *  execSCANOPCONF() with the right row count.
     */
    resultStream.getReceiver()
      .execSCANOPCONF(RNIL, 0, resultStream.getRowCount());
  }
  /* nextResult() will later move it from m_fullFrags to m_applFrags
   * under mutex protection.
   */
  if (getRoot().m_resultStreams[0]->getReceiver().hasResults()) {
    m_fullFrags.push(m_rootFrags[0]);
  }
} //NdbQueryImpl::closeSingletonScans

int
NdbQueryImpl::close(bool forceSend)
{
  int res = 0;

  assert (m_state >= Initial && m_state < Destructed);
  Ndb* const ndb = m_transaction.getNdb();

  if (m_tcState != Inactive)
  {
    /* We have started a scan, but we have not yet received the last batch
     * for all root fragments. We must therefore close the scan to release 
     * the scan context at TC.*/
    res = closeTcCursor(forceSend);
  }

  // Throw any pending results
  m_fullFrags.clear();
  m_applFrags.clear();

  if (m_scanTransaction != NULL)
  {
    assert (m_state != Closed);
    assert (m_scanTransaction->m_scanningQuery == this);
    m_scanTransaction->m_scanningQuery = NULL;
    ndb->closeTransaction(m_scanTransaction);
    ndb->theRemainingStartTransactions--;  // Compensate; m_scanTransaction was not a real Txn
    m_scanTransaction = NULL;
  }

  postFetchRelease();
  m_state = Closed;  // Even if it was previously 'Failed' it is closed now!
  return res;
} //NdbQueryImpl::close


void
NdbQueryImpl::release()
{ 
  assert (m_state >= Initial && m_state < Destructed);
  if (m_state != Closed) {
    close(true);  // Ignore any errors, explicit ::close() first if errors are of interest
  }

  delete this;
}

void
NdbQueryImpl::setErrorCode(int aErrorCode)
{
  assert (aErrorCode!=0);
  m_error.code = aErrorCode;
  m_transaction.theErrorLine = 0;
  m_transaction.theErrorOperation = NULL;
  //if (m_abortOption != AO_IgnoreError)
  m_transaction.setOperationErrorCode(aErrorCode);
  m_state = Failed;
}

void
NdbQueryImpl::setErrorCodeAbort(int aErrorCode)
{
  assert (aErrorCode!=0);
  m_error.code = aErrorCode;
  m_transaction.theErrorLine = 0;
  m_transaction.theErrorOperation = NULL;
  m_transaction.setOperationErrorCodeAbort(aErrorCode);
  m_state = Failed;
}


bool 
NdbQueryImpl::execTCKEYCONF()
{
  if (traceSignals) {
    ndbout << "NdbQueryImpl::execTCKEYCONF()" << endl;
  }
  assert(!getQueryDef().isScanQuery());

  // We will get 1 + #leaf-nodes TCKEYCONF for a lookup...
  m_rootFrags[0].setConfReceived();
  m_rootFrags[0].incrOutstandingResults(-1);

  bool ret = false;
  if (m_rootFrags[0].isFragBatchComplete()) { 
    /* If this root fragment is complete, verify that the query is also 
     * complete for this batch.
     */
    ret = incrementPendingFrags(-1);
    assert(ret);
  }

  if (traceSignals) {
    ndbout << "NdbQueryImpl::execTCKEYCONF(): returns:" << ret
           << ", m_pendingFrags=" << m_pendingFrags
           << ", *getRoot().m_resultStreams[0]=" 
           << *getRoot().m_resultStreams[0]
           << endl;
  }
  return ret;
}

void 
NdbQueryImpl::execCLOSE_SCAN_REP(bool needClose)
{
  if (traceSignals)
  {
    ndbout << "NdbQueryImpl::execCLOSE_SCAN_REP()" << endl;
  }
  assert(m_finalBatchFrags < getRootFragCount());
  m_pendingFrags = 0;
  if(!needClose)
  {
    m_finalBatchFrags = getRootFragCount();
  }
}


bool 
NdbQueryImpl::incrementPendingFrags(int increment)
{
  m_pendingFrags += increment;
  assert(m_pendingFrags < 1<<15); // Check against underflow.
  if (traceSignals) {
    ndbout << "NdbQueryImpl::incrementPendingFrags(" << increment << "): "
           << ", pendingFrags=" << m_pendingFrags <<  endl;
  }

  if (m_pendingFrags==0) {
    if (!getQueryDef().isScanQuery()) {
      closeSingletonScans();
    }
    return true;
  } else {
    return false;
  }
}

int
NdbQueryImpl::prepareSend()
{
  if (unlikely(m_state != Defined)) {
    assert (m_state >= Initial && m_state < Destructed);
    if (m_state == Failed) 
      setErrorCodeAbort(QRY_IN_ERROR_STATE);
    else
      setErrorCodeAbort(QRY_ILLEGAL_STATE);
    DEBUG_CRASH();
    return -1;
  }

  assert (m_pendingFrags==0);

  // Determine execution parameters 'batch size'.
  // May be user specified (TODO), and/or,  limited/specified by config values
  //
  if (getQueryDef().isScanQuery())
  {
    /* For the first batch, we read from all fragments for both ordered 
     * and unordered scans.*/
    m_pendingFrags = m_rootFragCount 
      = getRoot().getQueryOperationDef().getTable().getFragmentCount();
    
    Ndb* const ndb = m_transaction.getNdb();

    /** Scan operations need a own sub-transaction object associated with each 
     *  query.
     */
    ndb->theRemainingStartTransactions++; // Compensate; does not start a real Txn
    NdbTransaction *scanTxn = ndb->hupp(&m_transaction);
    if (scanTxn==NULL) {
      ndb->theRemainingStartTransactions--;
      m_transaction.setOperationErrorCodeAbort(ndb->getNdbError().code);
      return -1;
    }
    scanTxn->theMagicNumber = 0x37412619;
    scanTxn->m_scanningQuery = this;
    this->m_scanTransaction = scanTxn;
  }
  else  // Lookup query
  {
    m_pendingFrags = m_rootFragCount = 1;
  }

  // Some preparation for later batchsize calculations pr. (sub) scan
  getRoot().calculateBatchedRows();
  getRoot().setBatchedRows(1);

  // 1. Build receiver structures for each QueryOperation.
  // 2. Fill in parameters (into ATTRINFO) for QueryTree.
  //    (Has to complete *after* ::prepareReceiver() as QueryTree params
  //     refer receiver id's.)
  //
  for (Uint32 i = 0; i < m_countOperations; i++) {
    int error;
    if (unlikely((error = m_operations[i].prepareReceiver()) != 0)
              || (error = m_operations[i].prepareAttrInfo(m_attrInfo)) != 0) {
      setErrorCodeAbort(error);
      return -1;
    }
  }

  if (unlikely(m_attrInfo.isMemoryExhausted() || m_keyInfo.isMemoryExhausted())) {
    setErrorCodeAbort(Err_MemoryAlloc);
    return -1;
  }

  if (unlikely(m_attrInfo.getSize() > ScanTabReq::MaxTotalAttrInfo  ||
               m_keyInfo.getSize()  > ScanTabReq::MaxTotalAttrInfo)) {
    setErrorCodeAbort(4257); // TODO: find a more suitable errorcode, 
    return -1;
  }

  // Setup m_applStreams and m_fullStreams for receiving results
  const NdbRecord* keyRec = NULL;
  if(getRoot().getQueryOperationDef().getIndex()!=NULL)
  {
    /* keyRec is needed for comparing records when doing ordered index scans.*/
    keyRec = getRoot().getQueryOperationDef().getIndex()->getDefaultRecord();
    assert(keyRec!=NULL);
  }
  int error;
  if (unlikely((error = m_applFrags.prepare(getRoot().getOrdering(),
                                              m_pendingFrags, 
                                              keyRec,
                                              getRoot().m_ndbRecord)) != 0)
            || (error = m_fullFrags.prepare(m_pendingFrags)) != 0) {
    setErrorCodeAbort(error);
    return -1;
  }

  /**
   * Allocate and initialize fragment state variables.
   */
  m_rootFrags = new NdbRootFragment[m_rootFragCount];
  if(m_rootFrags == NULL)
  {
    setErrorCodeAbort(Err_MemoryAlloc);
    return -1;
  }
  else
  {
    for(Uint32 i = 0; i<m_rootFragCount; i++)
    {
      m_rootFrags[i].init(*this, i); // Set fragment number.
    }
  }

#ifdef TRACE_SERIALIZATION
  ndbout << "Serialized ATTRINFO : ";
  for(Uint32 i = 0; i < m_attrInfo.getSize(); i++){
    char buf[12];
    sprintf(buf, "%.8x", m_attrInfo.get(i));
    ndbout << buf << " ";
  }
  ndbout << endl;
#endif

  m_state = Prepared;
  return 0;
} // NdbQueryImpl::prepareSend



/** This iterator is used for inserting a sequence of receiver ids 
 * for the initial batch of a scan into a section via a GenericSectionPtr.*/
class InitialReceiverIdIterator: public GenericSectionIterator
{
public:
  
  InitialReceiverIdIterator(const NdbQueryImpl& query)
    :m_query(query),
     m_currFragNo(0)
  {}
  
  virtual ~InitialReceiverIdIterator() {};
  
  /**
   * Get next batch of receiver ids. 
   * @param sz This will be set to the number of receiver ids that have been
   * put in the buffer (0 if end has been reached.)
   * @return Array of receiver ids (or NULL if end reached.
   */
  virtual const Uint32* getNextWords(Uint32& sz);

  virtual void reset()
  { m_currFragNo = 0;};
  
private:
  /** 
   * Size of internal receiver id buffer. This value is arbitrary, but 
   * a larger buffer would mean fewer calls to getNextWords(), possibly
   * improving efficiency.
   */
  static const Uint32 bufSize = 16;
  /** The query with the scan root operation that we list receiver ids for.*/
  const NdbQueryImpl& m_query;
  /** The next fragment numnber to be processed. (Range for 0 to no of 
   * fragments.)*/
  Uint32 m_currFragNo;
  /** Buffer for storing one batch of receiver ids.*/
  Uint32 m_receiverIds[bufSize];
};

const Uint32* InitialReceiverIdIterator::getNextWords(Uint32& sz)
{
  sz = 0;
  /**
   * For the initial batch, we want to retrieve one batch for each fragment
   * whether it is a sorted scan or not.
   */
  if (m_currFragNo >= m_query.getRootFragCount())
  {
    return NULL;
  }
  else
  {
    const NdbQueryOperationImpl& root = m_query.getQueryOperation(0U);
    while (sz < bufSize && 
           m_currFragNo < m_query.getRootFragCount())
    {
      m_receiverIds[sz] = root.getReceiver(m_currFragNo).getId();
      sz++;
      m_currFragNo++;
    }
    return m_receiverIds;
  }
}
  

/******************************************************************************
int doSend()

Return Value:   Return >0 : send was succesful, returns number of signals sent
                Return -1: In all other case.   
Parameters:     nodeId: Receiving processor node
Remark:         Send a TCKEYREQ or SCAN_TABREQ (long) signal depending of 
                the query being either a lookup or scan type. 
                KEYINFO and ATTRINFO are included as part of the long signal
******************************************************************************/
int
NdbQueryImpl::doSend(int nodeId, bool lastFlag)
{
  if (unlikely(m_state != Prepared)) {
    assert (m_state >= Initial && m_state < Destructed);
    if (m_state == Failed) 
      setErrorCodeAbort(QRY_IN_ERROR_STATE);
    else
      setErrorCodeAbort(QRY_ILLEGAL_STATE);
    DEBUG_CRASH();
    return -1;
  }

  Ndb& ndb = *m_transaction.getNdb();
  TransporterFacade *tp = ndb.theImpl->m_transporter_facade;

  const NdbQueryOperationImpl& root = getRoot();
  const NdbQueryOperationDefImpl& rootDef = root.getQueryOperationDef();
  const NdbTableImpl* const rootTable = rootDef.getIndex()
    ? rootDef.getIndex()->getIndexTable()
    : &rootDef.getTable();

  Uint32 tTableId = rootTable->m_id;
  Uint32 tSchemaVersion = rootTable->m_version;

  if (rootDef.isScanOperation())
  {
    Uint32 scan_flags = 0;  // TODO: Specify with ScanOptions::SO_SCANFLAGS

    bool tupScan = (scan_flags & NdbScanOperation::SF_TupScan);
    bool rangeScan = false;

    bool dummy;
    const int error = isPrunable(dummy);
    if (unlikely(error != 0))
      return error;

    /* Handle IndexScan specifics */
    if ( (int) rootTable->m_indexType ==
         (int) NdbDictionary::Index::OrderedIndex )
    {
      rangeScan = true;
      tupScan = false;
    }
    const Uint32 descending = 
      root.getOrdering()==NdbQueryOptions::ScanOrdering_descending ? 1 : 0;
    assert(descending==0 || (int) rootTable->m_indexType ==
           (int) NdbDictionary::Index::OrderedIndex);

    assert (root.getMaxBatchRows() > 0);

    NdbApiSignal tSignal(&ndb);
    tSignal.setSignal(GSN_SCAN_TABREQ);

    ScanTabReq * const scanTabReq = CAST_PTR(ScanTabReq, tSignal.getDataPtrSend());
    Uint32 reqInfo = 0;

    const Uint64 transId = m_scanTransaction->getTransactionId();

    scanTabReq->apiConnectPtr = m_scanTransaction->theTCConPtr;
    scanTabReq->buddyConPtr = m_scanTransaction->theBuddyConPtr; // 'buddy' refers 'real-transaction'->theTCConPtr
    scanTabReq->spare = 0;  // Unused in later protocoll versions
    scanTabReq->tableId = tTableId;
    scanTabReq->tableSchemaVersion = tSchemaVersion;
    scanTabReq->storedProcId = 0xFFFF;
    scanTabReq->transId1 = (Uint32) transId;
    scanTabReq->transId2 = (Uint32) (transId >> 32);

    Uint32 batchRows = root.getMaxBatchRows();
    Uint32 batchByteSize, firstBatchRows;
    NdbReceiver::calculate_batch_size(tp,
                                      root.m_ndbRecord,
                                      root.m_firstRecAttr,
                                      0, // Key size.
                                      getRootFragCount(),
                                      batchRows,
                                      batchByteSize,
                                      firstBatchRows);
    assert (batchRows==root.getMaxBatchRows());
    ScanTabReq::setScanBatch(reqInfo, batchRows);
    scanTabReq->batch_byte_size = batchByteSize;
    scanTabReq->first_batch_size = firstBatchRows;

    ScanTabReq::setViaSPJFlag(reqInfo, 1);
    ScanTabReq::setParallelism(reqInfo, getRootFragCount());
    ScanTabReq::setRangeScanFlag(reqInfo, rangeScan);
    ScanTabReq::setDescendingFlag(reqInfo, descending);
    ScanTabReq::setTupScanFlag(reqInfo, tupScan);
    ScanTabReq::setNoDiskFlag(reqInfo, !root.diskInUserProjection());

    // Assume LockMode LM_ReadCommited, set related lock flags
    ScanTabReq::setLockMode(reqInfo, false);  // not exclusive
    ScanTabReq::setHoldLockFlag(reqInfo, false);
    ScanTabReq::setReadCommittedFlag(reqInfo, true);

//  m_keyInfo = (scan_flags & NdbScanOperation::SF_KeyInfo) ? 1 : 0;

    // If scan is pruned, use optional 'distributionKey' to hold hashvalue
    if (m_prunability == Prune_Yes)
    {
//    printf("Build pruned SCANREQ, w/ hashValue:%d\n", hashValue);
      ScanTabReq::setDistributionKeyFlag(reqInfo, 1);
      scanTabReq->distributionKey= m_pruneHashVal;
      tSignal.setLength(ScanTabReq::StaticLength + 1);
    } else {
      tSignal.setLength(ScanTabReq::StaticLength);
    }
    scanTabReq->requestInfo = reqInfo;

    /**
     * Then send the signal:
     *
     * SCANTABREQ always has 2 mandatory sections and an optional
     * third section
     * Section 0 : List of receiver Ids NDBAPI has allocated 
     *             for the scan
     * Section 1 : ATTRINFO section
     * Section 2 : Optional KEYINFO section
     */
    GenericSectionPtr secs[3];
    InitialReceiverIdIterator receiverIdIter(*this);
    LinearSectionIterator attrInfoIter(m_attrInfo.addr(), m_attrInfo.getSize());
    LinearSectionIterator keyInfoIter(m_keyInfo.addr(), m_keyInfo.getSize());
 
    secs[0].sectionIter= &receiverIdIter;
    secs[0].sz= getRootFragCount();

    secs[1].sectionIter= &attrInfoIter;
    secs[1].sz= m_attrInfo.getSize();

    Uint32 numSections= 2;
    if (m_keyInfo.getSize() > 0)
    {
      secs[2].sectionIter= &keyInfoIter;
      secs[2].sz= m_keyInfo.getSize();
      numSections= 3;
    }

    /* Send Fragmented as SCAN_TABREQ can be large */
    const int res = tp->sendFragmentedSignal(&tSignal, nodeId, secs, numSections);
    if (unlikely(res == -1))
    {
      setErrorCodeAbort(Err_SendFailed);  // Error: 'Send to NDB failed'
      return FetchResult_sendFail;
    }
    m_tcState = Active;

  } else {  // Lookup query

    NdbApiSignal tSignal(&ndb);
    tSignal.setSignal(GSN_TCKEYREQ);

    TcKeyReq * const tcKeyReq = CAST_PTR(TcKeyReq, tSignal.getDataPtrSend());

    const Uint64 transId = m_transaction.getTransactionId();
    tcKeyReq->apiConnectPtr   = m_transaction.theTCConPtr;
    tcKeyReq->apiOperationPtr = root.getIdOfReceiver();
    tcKeyReq->tableId = tTableId;
    tcKeyReq->tableSchemaVersion = tSchemaVersion;
    tcKeyReq->transId1 = (Uint32) transId;
    tcKeyReq->transId2 = (Uint32) (transId >> 32);

    Uint32 attrLen = 0;
    tcKeyReq->setAttrinfoLen(attrLen, 0); // Not required for long signals.
    tcKeyReq->setAPIVersion(attrLen, NDB_VERSION);
    tcKeyReq->attrLen = attrLen;

    Uint32 reqInfo = 0;
    Uint32 interpretedFlag= root.hasInterpretedCode() && 
                            rootDef.getType() == NdbQueryOperationDef::PrimaryKeyAccess;

    TcKeyReq::setOperationType(reqInfo, NdbOperation::ReadRequest);
    TcKeyReq::setViaSPJFlag(reqInfo, true);
    TcKeyReq::setKeyLength(reqInfo, 0);            // This is a long signal
    TcKeyReq::setAIInTcKeyReq(reqInfo, 0);         // Not needed
    TcKeyReq::setInterpretedFlag(reqInfo, interpretedFlag);
    TcKeyReq::setStartFlag(reqInfo, m_startIndicator);
    TcKeyReq::setExecuteFlag(reqInfo, lastFlag);
    TcKeyReq::setNoDiskFlag(reqInfo, !root.diskInUserProjection());
    TcKeyReq::setAbortOption(reqInfo, NdbOperation::AO_IgnoreError);

    TcKeyReq::setDirtyFlag(reqInfo, true);
    TcKeyReq::setSimpleFlag(reqInfo, true);
    TcKeyReq::setCommitFlag(reqInfo, m_commitIndicator);
    tcKeyReq->requestInfo = reqInfo;

    tSignal.setLength(TcKeyReq::StaticLength);

/****
    // Unused optional part located after TcKeyReq::StaticLength
    tcKeyReq->scanInfo = 0;
    tcKeyReq->distrGroupHashValue = 0;
    tcKeyReq->distributionKeySize = 0;
    tcKeyReq->storedProcId = 0xFFFF;
***/

/**** TODO ... maybe - from NdbOperation::prepareSendNdbRecord(AbortOption ao)
    Uint8 abortOption= (ao == DefaultAbortOption) ?
      (Uint8) m_abortOption : (Uint8) ao;
  
    m_abortOption= theSimpleIndicator && theOperationType==ReadRequest ?
      (Uint8) AO_IgnoreError : (Uint8) abortOption;

    TcKeyReq::setAbortOption(reqInfo, m_abortOption);
    TcKeyReq::setCommitFlag(tcKeyReq->requestInfo, theCommitIndicator);
*****/

    LinearSectionPtr secs[2];
    secs[TcKeyReq::KeyInfoSectionNum].p= m_keyInfo.addr();
    secs[TcKeyReq::KeyInfoSectionNum].sz= m_keyInfo.getSize();
    Uint32 numSections= 1;

    if (m_attrInfo.getSize() > 0)
    {
      secs[TcKeyReq::AttrInfoSectionNum].p= m_attrInfo.addr();
      secs[TcKeyReq::AttrInfoSectionNum].sz= m_attrInfo.getSize();
      numSections= 2;
    }

    const int res = tp->sendSignal(&tSignal, nodeId, secs, numSections);
    if (unlikely(res == -1))
    {
      setErrorCodeAbort(Err_SendFailed);  // Error: 'Send to NDB failed'
      return FetchResult_sendFail;
    }
    m_transaction.OpSent();
    m_rootFrags[0].incrOutstandingResults(1 + getNoOfOperations() +
                                          getNoOfLeafOperations());
  } // if

  // Shrink memory footprint by removing structures not required after ::execute()
  m_keyInfo.releaseExtend();
  m_attrInfo.releaseExtend();

  // TODO: Release m_interpretedCode now?

  /* Todo : Consider calling NdbOperation::postExecuteRelease()
   * Ideally it should be called outside TP mutex, so not added
   * here yet
   */

  m_state = Executing;
  return 1;
} // NdbQueryImpl::doSend()


/** This iterator is used for inserting a sequence of receiver ids for scan
 * batches other than the first batch  into a section via a GenericSectionPtr.
*/
class ReceiverIdIterator: public GenericSectionIterator
{
public:
  
  ReceiverIdIterator(const NdbQueryImpl& query)
    :m_query(query),
     m_currFragNo(0)
  {}

  virtual ~ReceiverIdIterator() {}
  
  virtual void reset()
  { m_currFragNo = 0; };
  
  /**
   * Get next batch of receiver ids. 
   * @param sz This will be set to the number of receiver ids that have been
   * put in the buffer (0 if end has been reached.)
   * @return Array of receiver ids (or NULL if end reached.
   */
  virtual const Uint32* getNextWords(Uint32& sz);

  /** Get the fragment number of the buffEntryNo'th entry in the internal
   * buffer.
   */
  Uint32 getRootFragNo(Uint32 buffEntryNo) const
  { 
    assert(buffEntryNo < bufSize);
    return m_rootFragNos[buffEntryNo]; 
  }

private:
  /** 
   * Size of internal receiver id buffer. This value is arbitrary, but 
   * a larger buffer would mean fewer calls to getNextWords(), possibly
   * improving efficiency.
   */
  static const Uint32 bufSize = 16;
  /** The query with the scan root operation that we list receiver ids for.*/
  const NdbQueryImpl& m_query;
  /** The next fragment numnber to be processed. (Range for 0 to [no of 
   * fragments] - 1.)*/
  Uint32 m_currFragNo;
  /** Buffer for storing one batch of receiver ids.*/
  Uint32 m_receiverIds[bufSize];
  /** Buffer of fragment numbers for the root operation. (Fragments are 
   * numbered from 0 to [no of fragments] - 1)*/
  Uint32 m_rootFragNos[bufSize];
};


const Uint32* ReceiverIdIterator::getNextWords(Uint32& sz)
{
  sz = 0;
  if (m_query.getRoot().getOrdering() == NdbQueryOptions::ScanOrdering_unordered)
  {
    /* For unordered scans, ask for a new batch for each fragment.*/
    while (m_currFragNo < m_query.getRootFragCount()
           && sz < bufSize)
    {
      const Uint32 tcPtrI = 
        m_query.getRoot().getReceiver(m_currFragNo).m_tcPtrI;
      if (tcPtrI != RNIL) // Check if we have received the final batch.
      {
        m_receiverIds[sz] = tcPtrI;
        m_rootFragNos[sz++] = m_currFragNo;
      }
      m_currFragNo++;
    }
  }
  else if (m_currFragNo == 0)
  {
    /* For ordred scans we must have records buffered for each (non-finished)
     * root fragment at all times, in order to find the lowest remaining 
     * record. When one root fragment is empty, we must block the scan ask 
     * for a new batch for that particular fragment.
     * Note that getEmpty() will never return a fragment that is complete 
     * (meaning that the last batch has been received).
     */
    const NdbRootFragment* const emptyFrag = m_query.m_applFrags.getEmpty();
    if(emptyFrag!=NULL)
    {
      sz = 1;
      m_receiverIds[0] = emptyFrag->getResultStream(0).getReceiver().m_tcPtrI;
      assert(m_receiverIds[0] != RNIL);
      m_rootFragNos[0] = emptyFrag->getFragNo();
    }
    /**
     * Set it to one to make sure that the next call to getNextWords() will 
     * return NULL, since we only want one fragment at a time.
     */
    m_currFragNo = 1;
  }
  
  return sz == 0 ? NULL : m_receiverIds;
}

/******************************************************************************
int sendFetchMore() - Fetch another scan batch, optionaly closing the scan
                
                Request another batch of rows to be retrieved from the scan.
                Transporter mutex is locked before this method is called. 

Return Value:   Return >0 : send was succesful, returns number of fragments
                having pending scans batches.
                Return =0 : No more rows is available -> EOF
                Return -1: In all other case.   
Parameters:     nodeId: Receiving processor node
Remark:
******************************************************************************/
int
NdbQueryImpl::sendFetchMore(int nodeId)
{
  Uint32 sent = 0;
  assert(getRoot().m_resultStreams!=NULL);
  assert(m_pendingFrags==0);

  ReceiverIdIterator receiverIdIter(*this);

  Uint32 idBuffSize = 0;
  receiverIdIter.getNextWords(idBuffSize);

  /* Iterate over the fragments for which we will reqest a new batch.*/
  while (idBuffSize > 0)
  {
    sent += idBuffSize;
    m_pendingFrags += idBuffSize;
  
    for (Uint32 i = 0; i<idBuffSize; i++)
    {
      NdbRootFragment* const emptyFrag = 
        m_rootFrags + receiverIdIter.getRootFragNo(i);
      emptyFrag->reset();
  
      for (unsigned op=0; op<m_countOperations; op++) 
      {
        emptyFrag->getResultStream(op).reset();
      }
    }
    receiverIdIter.getNextWords(idBuffSize);
  }
  receiverIdIter.reset();

//printf("::sendFetchMore, to nodeId:%d, sent:%d\n", nodeId, sent);
  if (sent==0)
  {
    assert (m_finalBatchFrags == getRootFragCount());
    return 0;
  }

  assert (m_finalBatchFrags+m_pendingFrags <= getRootFragCount());

  Ndb& ndb = *m_transaction.getNdb();
  NdbApiSignal tSignal(&ndb);
  tSignal.setSignal(GSN_SCAN_NEXTREQ);
  ScanNextReq * const scanNextReq = CAST_PTR(ScanNextReq, tSignal.getDataPtrSend());

  assert (m_scanTransaction);
  const Uint64 transId = m_scanTransaction->getTransactionId();

  scanNextReq->apiConnectPtr = m_scanTransaction->theTCConPtr;
  scanNextReq->stopScan = 0;
  scanNextReq->transId1 = (Uint32) transId;
  scanNextReq->transId2 = (Uint32) (transId >> 32);
  tSignal.setLength(ScanNextReq::SignalLength);

  GenericSectionPtr secs[1];
  secs[ScanNextReq::ReceiverIdsSectionNum].sectionIter = &receiverIdIter;
  secs[ScanNextReq::ReceiverIdsSectionNum].sz = sent;

  TransporterFacade* tp = ndb.theImpl->m_transporter_facade;
  const int res = tp->sendSignal(&tSignal, nodeId, secs, 1);
  if (unlikely(res == -1)) {
    setErrorCodeAbort(Err_SendFailed);  // Error: 'Send to NDB failed'
    return FetchResult_sendFail;
  }

  return sent;
} // NdbQueryImpl::sendFetchMore()

int
NdbQueryImpl::closeTcCursor(bool forceSend)
{
  assert (m_queryDef.isScanQuery());

  Ndb* const ndb = m_transaction.getNdb();
  TransporterFacade* const facade = ndb->theImpl->m_transporter_facade;

  /* This part needs to be done under mutex due to synchronization with 
   * receiver thread.
   */
  PollGuard poll_guard(facade,
                       &ndb->theImpl->theWaiter,
                       ndb->theNdbBlockNumber);

  /* Wait for outstanding scan results from current batch fetch */
  while (m_error.code==0 && !isBatchComplete())
  {
    const FetchResult waitResult = static_cast<FetchResult>
          (poll_guard.wait_scan(3*facade->m_waitfor_timeout, 
                                m_transaction.getConnectedNodeId(), 
                                forceSend));
    switch (waitResult) {
    case FetchResult_ok:
      break;
    case FetchResult_nodeFail:
      setErrorCode(Err_NodeFailCausedAbort);  // Node fail
      return -1;
    case FetchResult_timeOut:
      setErrorCode(Err_ReceiveFromNdbFailed); // Timeout
      return -1;
    default:
      assert(false);
    }
  } // while

  assert(m_pendingFrags==0 || m_error.code != 0);
  m_error.code = 0;  // Ignore possible errorcode caused by previous fetching

  /* Discard pending result in order to not confuse later counting of m_finalBatchFrags */
  m_fullFrags.clear();

  if (m_finalBatchFrags<getRootFragCount())  // TC has an open scan cursor.
  {
    /* Send SCANREQ(close) */
    const int error = sendClose(m_transaction.getConnectedNodeId());
    if (unlikely(error))
      return error;

    /* Wait for close to be confirmed: */
    while (m_pendingFrags > 0)
    {
      const FetchResult waitResult = static_cast<FetchResult>
            (poll_guard.wait_scan(3*facade->m_waitfor_timeout, 
                                  m_transaction.getConnectedNodeId(), 
                                  forceSend));
      switch (waitResult) {
      case FetchResult_ok:
        if (unlikely(m_error.code))   // Close request itself failed, keep error
        {
          setErrorCode(m_error.code);
          return -1;
        }
        NdbRootFragment* frag;
        while ((frag=m_fullFrags.pop()) != NULL)
        {
          if (frag->finalBatchReceived())
          {
            // This was the final batch for that root fragment.
            m_finalBatchFrags++;
          }
        }
        break;
      case FetchResult_nodeFail:
        setErrorCode(Err_NodeFailCausedAbort);  // Node fail
        return -1;
      case FetchResult_timeOut:
        setErrorCode(Err_ReceiveFromNdbFailed); // Timeout
        return -1;
      default:
        assert(false);
      }
    } // while
  } // if
  assert(m_finalBatchFrags == getRootFragCount());

  return 0;
} //NdbQueryImpl::closeTcCursor

int
NdbQueryImpl::sendClose(int nodeId)
{
  assert(m_finalBatchFrags < getRootFragCount());

  m_pendingFrags = getRootFragCount() - m_finalBatchFrags;
  assert(m_pendingFrags > 0);
  assert(m_pendingFrags < 1<<15); // Check against underflow.

  Ndb& ndb = *m_transaction.getNdb();
  NdbApiSignal tSignal(&ndb);
  tSignal.setSignal(GSN_SCAN_NEXTREQ);
  ScanNextReq * const scanNextReq = CAST_PTR(ScanNextReq, tSignal.getDataPtrSend());

  assert (m_scanTransaction);
  const Uint64 transId = m_scanTransaction->getTransactionId();

  scanNextReq->apiConnectPtr = m_scanTransaction->theTCConPtr;
  scanNextReq->stopScan = true;
  scanNextReq->transId1 = (Uint32) transId;
  scanNextReq->transId2 = (Uint32) (transId >> 32);
  tSignal.setLength(ScanNextReq::SignalLength);

  TransporterFacade* tp = ndb.theImpl->m_transporter_facade;
  return tp->sendSignal(&tSignal, nodeId);

} // NdbQueryImpl::sendClose()


bool 
NdbQueryImpl::isBatchComplete() const {
#ifndef NDEBUG
  if (!m_error.code)
  {
    Uint32 count = 0;
    for(Uint32 i = 0; i < getRootFragCount(); i++){
      if(!m_rootFrags[i].isFragBatchComplete()){
        count++;
      }
    }
    assert(count == m_pendingFrags);
  }
#endif
  return m_pendingFrags == 0;
}


int NdbQueryImpl::isPrunable(bool& prunable)
{
  if (m_prunability == Prune_Unknown)
  {
    const int error = getRoot().getQueryOperationDef()
      .checkPrunable(m_keyInfo, m_shortestBound, prunable, m_pruneHashVal);
    if (unlikely(error != 0))
    {
      prunable = false;
      setErrorCodeAbort(error);
      return -1;
    }
    m_prunability = prunable ? Prune_Yes : Prune_No;
  }
  prunable = (m_prunability == Prune_Yes);
  return 0;
}


/****************
 * NdbQueryImpl::FragStack methods.
 ***************/

NdbQueryImpl::FragStack::FragStack():
  m_capacity(0),
  m_current(-1),
  m_array(NULL){
}

int
NdbQueryImpl::FragStack::prepare(int capacity)
{
  assert(m_array==NULL);
  assert(m_capacity==0);
  if (capacity > 0) 
  { m_capacity = capacity;
    m_array = new NdbRootFragment*[capacity];
    if (unlikely(m_array==NULL))
      return Err_MemoryAlloc;
  }
  return 0;
}

void
NdbQueryImpl::FragStack::push(NdbRootFragment& frag){
  m_current++;
  assert(m_current<m_capacity);
  m_array[m_current] = &frag; 
}

/****************
 * NdbQueryImpl::OrderedFragSet methods.
 ***************/

NdbQueryImpl::OrderedFragSet::OrderedFragSet():
  m_capacity(0),
  m_size(0),
  m_completedFrags(0),
  m_ordering(NdbQueryOptions::ScanOrdering_void),
  m_keyRecord(NULL),
  m_resultRecord(NULL),
  m_array(NULL)
{
}

int
NdbQueryImpl::OrderedFragSet::prepare(NdbQueryOptions::ScanOrdering ordering, 
                                      int capacity,                
                                      const NdbRecord* keyRecord,
                                      const NdbRecord* resultRecord)
{
  assert(m_array==NULL);
  assert(m_capacity==0);
  assert(ordering!=NdbQueryOptions::ScanOrdering_void);
  
  if (capacity > 0) 
  { m_capacity = capacity;
    m_array = new NdbRootFragment*[capacity];
    if (unlikely(m_array==NULL))
      return Err_MemoryAlloc;
    bzero(m_array, capacity * sizeof(NdbRootFragment*));
  }
  m_ordering = ordering;
  m_keyRecord = keyRecord;
  m_resultRecord = resultRecord;
  return 0;
}


NdbRootFragment* 
NdbQueryImpl::OrderedFragSet::getCurrent()
{ 
  if(m_ordering==NdbQueryOptions::ScanOrdering_unordered){
    while(m_size>0 && m_array[m_size-1]->isEmpty())
    {
      m_size--;
    }
    if(m_size>0)
    {
      return m_array[m_size-1];
    }
    else
    {
      return NULL;
    }
  }
  else
  {
    assert(verifySortOrder());
    // Results should be ordered.
    if(m_size+m_completedFrags < m_capacity) 
    {
      // Waiting for the first batch for all fragments to arrive.
      return NULL;
    }
    if(m_size==0 || m_array[0]->isEmpty()) 
    {      
      // Waiting for a new batch for a fragment.
      return NULL;
    }
    else
    {
      return m_array[0];
    }
  }
}

void 
NdbQueryImpl::OrderedFragSet::reorder()
{
  if(m_ordering!=NdbQueryOptions::ScanOrdering_unordered && m_size>0)
  {
    if(m_array[0]->finalBatchReceived() &&
       m_array[0]->isEmpty())
    {
      m_completedFrags++;
      m_size--;
      memmove(m_array, m_array+1, m_size * sizeof(NdbRootFragment*));
      assert(verifySortOrder());
    }
    else if(m_size>1)
    {
      /* There are more data to be read from m_array[0]. Move it to its proper
       * place.*/
      int first = 1;
      int last = m_size;
      /* Use binary search to find the largest record that is smaller than or
       * equal to m_array[0] */
      int middle = (first+last)/2;
      while(first<last)
      {
        assert(middle<m_size);
        switch(compare(*m_array[0], *m_array[middle]))
        {
        case -1:
          last = middle;
          break;
        case 0:
          last = first = middle;
          break;
        case 1:
          first = middle + 1;
          break;
        }
        middle = (first+last)/2;
      }
      if(middle>0)
      {
        NdbRootFragment* const oldTop = m_array[0];
        memmove(m_array, m_array+1, (middle-1) * sizeof(NdbRootFragment*));
        m_array[middle-1] = oldTop;
      }
      assert(verifySortOrder());
    }
  }
}

void 
NdbQueryImpl::OrderedFragSet::add(NdbRootFragment& frag)
{
  assert(&frag!=NULL);
  if(m_ordering==NdbQueryOptions::ScanOrdering_unordered)
  {
    assert(m_size<m_capacity);
    m_array[m_size++] = &frag;
  }
  else
  {
    if(m_size+m_completedFrags < m_capacity)
    {
      if(!frag.isEmpty())
      {
        // Frag is non-empty.
        int current = 0;
        // Insert the new frag such that the array remains sorted.
        while(current<m_size && compare(frag, *m_array[current])==1)
        {
          current++;
        }
        memmove(m_array+current+1,
                m_array+current,
                (m_size - current) * sizeof(NdbRootFragment*));
        m_array[current] = &frag;
        m_size++;
        assert(m_size <= m_capacity);
        assert(verifySortOrder());
      }
      else
      {
        // First batch is empty, therefore it should also be the final batch. 
        assert(frag.finalBatchReceived());
        m_completedFrags++;
      }
    }
    else
    {
      // This is not the first batch, so the frag should be here already.

      /* A Frag may only be emptied when it hold the record with the 
       * currently lowest sort order. It must hance become member no 0 in
       * m_array before it can be emptied. Then we will ask for a new batch
       * for that particular frag.*/
      assert(&frag==m_array[0]);
      // Move current frag 0 to its proper place.
      reorder();
    }
  }
}

NdbRootFragment* 
NdbQueryImpl::OrderedFragSet::getEmpty() const
{
  // This method is not applicable to unordered scans.
  assert(m_ordering!=NdbQueryOptions::ScanOrdering_unordered);
  // The first frag should be empty when calling this method.
  assert(m_size==0 || m_array[0]->isEmpty());
  assert(verifySortOrder());
  if(m_completedFrags==m_capacity)
  {
    assert(m_size==0);
    // All frags are complete.
    return NULL;
  }
  assert(!m_array[0]->finalBatchReceived());
  return m_array[0];
}

bool 
NdbQueryImpl::OrderedFragSet::verifySortOrder() const
{
  for(int i = 0; i<m_size-2; i++)
  {
    if(compare(*m_array[i], *m_array[i+1])==1)
    {
      assert(false);
      return false;
    }
  }
  return true;
}


/**
 * Compare frags such that f1<f2 if f1 is empty but f2 is not.
 * - Othewise compare record contents.
 * @return -1 if frag1<frag2, 0 if frag1 == frag2, otherwise 1.
*/
int
NdbQueryImpl::OrderedFragSet::compare(const NdbRootFragment& frag1,
                                      const NdbRootFragment& frag2) const
{
  assert(m_ordering!=NdbQueryOptions::ScanOrdering_unordered);

  /* f1<f2 if f1 is empty but f2 is not.*/  
  if(frag1.isEmpty())
  {
    if(!frag2.isEmpty())
    {
      return -1;
    }
    else
    {
      return 0;
    }
  }
  
  /* Neither stream is empty so we must compare records.*/
  return compare_ndbrecord(&frag1.getResultStream(0).getReceiver(), 
                           &frag2.getResultStream(0).getReceiver(),
                           m_keyRecord,
                           m_resultRecord,
                           m_ordering 
                           == NdbQueryOptions::ScanOrdering_descending,
                           false);
}



////////////////////////////////////////////////////
/////////  NdbQueryOperationImpl methods ///////////
////////////////////////////////////////////////////

NdbQueryOperationImpl::NdbQueryOperationImpl(
           NdbQueryImpl& queryImpl,
           const NdbQueryOperationDefImpl& def):
  m_interface(*this),
  m_magic(MAGIC),
  m_queryImpl(queryImpl),
  m_operationDef(def),
  m_parent(NULL),
  m_children(def.getNoOfChildOperations()),
  m_maxBatchRows(0),   // >0: User specified prefered value, ==0: Use default CFG values
  m_resultStreams(NULL),
  m_params(),
  m_bufferSize(0),
  m_batchBuffer(NULL),
  m_resultBuffer(NULL),
  m_resultRef(NULL),
  m_isRowNull(true),
  m_ndbRecord(NULL),
  m_read_mask(NULL),
  m_firstRecAttr(NULL),
  m_lastRecAttr(NULL),
  m_ordering(NdbQueryOptions::ScanOrdering_unordered),
  m_interpretedCode(NULL),
  m_diskInUserProjection(false)
{ 
  // Fill in operations parent refs, and append it as child of its parent
  const NdbQueryOperationDefImpl* parent = def.getParentOperation();
  if (parent != NULL)
  { 
    const Uint32 ix = parent->getQueryOperationIx();
    assert (ix < m_queryImpl.getNoOfOperations());
    m_parent = &m_queryImpl.getQueryOperation(ix);
    m_parent->m_children.push_back(this);
  }
  if (def.getType()==NdbQueryOperationDef::OrderedIndexScan)
  {  
    const NdbQueryOptions::ScanOrdering defOrdering = 
      static_cast<const NdbQueryIndexScanOperationDefImpl&>(def).getOrdering();
    if (defOrdering != NdbQueryOptions::ScanOrdering_void)
    {
      // Use value from definition, if one was set.
      m_ordering = defOrdering;
    }
  }
}

NdbQueryOperationImpl::~NdbQueryOperationImpl()
{
  // We expect ::postFetchRelease to have deleted fetch related structures when fetch completed.
  // Either by fetching through last row, or calling ::close() which forcefully terminates fetch
  assert (m_batchBuffer == NULL);
  assert (m_resultStreams == NULL);
  assert (m_firstRecAttr == NULL);
  assert (m_interpretedCode == NULL);
} //NdbQueryOperationImpl::~NdbQueryOperationImpl()

/**
 * Release what we want need anymore after last available row has been 
 * returned from datanodes.
 */ 
void
NdbQueryOperationImpl::postFetchRelease()
{
  if (m_batchBuffer) {
#ifndef NDEBUG // Buffer overrun check activated.
    { const Uint32 bufLen = m_bufferSize*m_queryImpl.getRootFragCount();
      assert(m_batchBuffer[bufLen+0] == 'a' &&
             m_batchBuffer[bufLen+1] == 'b' &&
             m_batchBuffer[bufLen+2] == 'c' &&
             m_batchBuffer[bufLen+3] == 'd');
    }
#endif
    delete[] m_batchBuffer;
    m_batchBuffer = NULL;
  }

  if (m_resultStreams) {
    for(Uint32 i = 0; i<getQuery().getRootFragCount(); i ++){
      delete m_resultStreams[i];
    }
    delete[] m_resultStreams;
    m_resultStreams = NULL;
  }

  Ndb* const ndb = m_queryImpl.getNdbTransaction().getNdb();
  NdbRecAttr* recAttr = m_firstRecAttr;
  while (recAttr != NULL) {
    NdbRecAttr* saveRecAttr = recAttr;
    recAttr = recAttr->next();
    ndb->releaseRecAttr(saveRecAttr);
  }
  m_firstRecAttr = NULL;

  // Set API exposed info to indicate NULL-row
  m_isRowNull = true;
  if (m_resultRef!=NULL) {
    *m_resultRef = NULL;
  }

  // TODO: Consider if interpretedCode can be deleted imm. after ::doSend
  delete m_interpretedCode;
  m_interpretedCode = NULL;
} //NdbQueryOperationImpl::postFetchRelease()


Uint32
NdbQueryOperationImpl::getNoOfParentOperations() const
{
  return (m_parent) ? 1 : 0;
}

NdbQueryOperationImpl&
NdbQueryOperationImpl::getParentOperation(Uint32 i) const
{
  assert(i==0 && m_parent!=NULL);
  return *m_parent;
}
NdbQueryOperationImpl*
NdbQueryOperationImpl::getParentOperation() const
{
  return m_parent;
}

Uint32 
NdbQueryOperationImpl::getNoOfChildOperations() const
{
  return m_children.size();
}

NdbQueryOperationImpl&
NdbQueryOperationImpl::getChildOperation(Uint32 i) const
{
  return *m_children[i];
}

Int32 NdbQueryOperationImpl::getNoOfDescendantOperations() const
{
  Int32 children = 0;

  for (unsigned i = 0; i < getNoOfChildOperations(); i++)
    children += 1 + getChildOperation(i).getNoOfDescendantOperations();

  return children;
}

Uint32
NdbQueryOperationImpl::getNoOfLeafOperations() const
{
  if (getNoOfChildOperations() == 0)
  {
    return 1;
  }
  else
  {
    Uint32 sum = 0;
    for (unsigned i = 0; i < getNoOfChildOperations(); i++)
      sum += getChildOperation(i).getNoOfLeafOperations();

    return sum;
  }
}

NdbRecAttr*
NdbQueryOperationImpl::getValue(
                            const char* anAttrName,
                            char* resultBuffer)
{
  const NdbColumnImpl* const column 
    = m_operationDef.getTable().getColumn(anAttrName);
  if(unlikely(column==NULL)){
    getQuery().setErrorCodeAbort(Err_UnknownColumn);
    return NULL;
  } else {
    return getValue(*column, resultBuffer);
  }
}

NdbRecAttr*
NdbQueryOperationImpl::getValue(
                            Uint32 anAttrId, 
                            char* resultBuffer)
{
  const NdbColumnImpl* const column 
    = m_operationDef.getTable().getColumn(anAttrId);
  if(unlikely(column==NULL)){
    getQuery().setErrorCodeAbort(Err_UnknownColumn);
    return NULL;
  } else {
    return getValue(*column, resultBuffer);
  }
}

NdbRecAttr*
NdbQueryOperationImpl::getValue(
                            const NdbColumnImpl& column, 
                            char* resultBuffer)
{
  if (unlikely(getQuery().m_state != NdbQueryImpl::Defined)) {
    int state = getQuery().m_state;
    assert (state >= NdbQueryImpl::Initial && state < NdbQueryImpl::Destructed);

    if (state == NdbQueryImpl::Failed) 
      getQuery().setErrorCode(QRY_IN_ERROR_STATE);
    else
      getQuery().setErrorCode(QRY_ILLEGAL_STATE);
    DEBUG_CRASH();
    return NULL;
  }
  Ndb* const ndb = getQuery().getNdbTransaction().getNdb();
  NdbRecAttr* const recAttr = ndb->getRecAttr();
  if(unlikely(recAttr == NULL)) {
    getQuery().setErrorCodeAbort(Err_MemoryAlloc);
    return NULL;
  }
  if(unlikely(recAttr->setup(&column, resultBuffer))) {
    ndb->releaseRecAttr(recAttr);
    getQuery().setErrorCodeAbort(Err_MemoryAlloc);
    return NULL;
  }
  // Append to tail of list.
  if(m_firstRecAttr == NULL){
    m_firstRecAttr = recAttr;
  }else{
    m_lastRecAttr->next(recAttr);
  }
  m_lastRecAttr = recAttr;
  assert(recAttr->next()==NULL);
  return recAttr;
}

int
NdbQueryOperationImpl::setResultRowBuf (
                       const NdbRecord *rec,
                       char* resBuffer,
                       const unsigned char* result_mask)
{
  if (unlikely(getQuery().m_state != NdbQueryImpl::Defined)) {
    int state = getQuery().m_state;
    assert (state >= NdbQueryImpl::Initial && state < NdbQueryImpl::Destructed);

    if (state == NdbQueryImpl::Failed) 
      getQuery().setErrorCode(QRY_IN_ERROR_STATE);
    else
      getQuery().setErrorCode(QRY_ILLEGAL_STATE);
    DEBUG_CRASH();
    return -1;
  }
  if (rec->tableId != 
      static_cast<Uint32>(m_operationDef.getTable().getTableId())){
    /* The key_record and attribute_record in primary key operation do not 
       belong to the same table.*/
    getQuery().setErrorCode(Err_DifferentTabForKeyRecAndAttrRec);
    return -1;
  }
  if (unlikely(m_ndbRecord != NULL)) {
    getQuery().setErrorCode(QRY_RESULT_ROW_ALREADY_DEFINED);
    return -1;
  }
  m_ndbRecord = rec;
  m_read_mask = result_mask;
  m_resultBuffer = resBuffer;
  assert(m_batchBuffer==NULL);
  return 0;
}

int
NdbQueryOperationImpl::setResultRowRef (
                       const NdbRecord* rec,
                       const char* & bufRef,
                       const unsigned char* result_mask)
{
  m_resultRef = &bufRef;
  *m_resultRef = NULL; // No result row yet
  return setResultRowBuf(rec, NULL, result_mask);
}

void 
NdbQueryOperationImpl::fetchRow(NdbResultStream& resultStream)
{
  const char* buff = resultStream.getReceiver().get_row();
  assert(buff!=NULL || (m_firstRecAttr==NULL && m_ndbRecord==NULL));

  m_isRowNull = false;
  if (m_firstRecAttr != NULL)
  {
    NdbRecAttr* recAttr = m_firstRecAttr;
    Uint32 posInRow = 0;
    while (recAttr != NULL)
    {
      const char *attrData = NULL;
      Uint32 attrSize = 0;
      const int retVal1 = resultStream.getReceiver()
        .getScanAttrData(attrData, attrSize, posInRow);
      assert(retVal1==0);
      assert(attrData!=NULL);
      const bool retVal2 = recAttr
        ->receive_data(reinterpret_cast<const Uint32*>(attrData), attrSize);
      assert(retVal2);
      recAttr = recAttr->next();
    }
  }
  if (m_ndbRecord != NULL)
  {
    if (m_resultRef!=NULL)
    {
      // Set application pointer to point into internal buffer.
      *m_resultRef = buff;
    }
    else
    {
      assert(m_resultBuffer!=NULL);
      // Copy result to buffer supplied by application.
      memcpy(m_resultBuffer, buff, 
             resultStream.getReceiver().m_record.m_ndb_record->m_row_size);
    }
  }
} // NdbQueryOperationImpl::fetchRow


bool 
NdbQueryOperationImpl::fetchLookupResults()
{
  NdbResultStream& resultStream = *m_resultStreams[0];

  if (resultStream.getRowCount() == 0)
  {
    /* This operation gave no result for the current parent tuple.*/
    nullifyResult();
    return false;
  }
  else
  {
    /* Call recursively for the children of this operation.*/
    for (Uint32 i = 0; i<getNoOfChildOperations(); i++)
    {
      NdbQueryOperationImpl& child = getChildOperation(i);
      bool nullRow = !child.fetchLookupResults();
      if (nullRow &&
          child.m_operationDef.getMatchType() != NdbQueryOptions::MatchAll)
      {
        // If a NULL row is returned from a child which is not outer joined, 
        // parent row may be eliminate also.
        (void)resultStream.getReceiver().get_row();  // Get and throw 
        nullifyResult();
        return false;
      }
    }
    fetchRow(resultStream);
    return true;
  }
} //NdbQueryOperationImpl::fetchLookupResults

void 
NdbQueryOperationImpl::nullifyResult()
{
  if (!m_isRowNull)
  {
    /* This operation gave no result for the current row.*/ 
    m_isRowNull = true;
    if (m_resultRef!=NULL)
    {
      // Set the pointer supplied by the application to NULL.
      *m_resultRef = NULL;
    }
    /* We should not give any results for the descendants either.*/
    for (Uint32 i = 0; i<getNoOfChildOperations(); i++)
    {
      getChildOperation(i).nullifyResult();
   }
  }
} // NdbQueryOperationImpl::nullifyResult

bool
NdbQueryOperationImpl::isRowNULL() const
{
  return m_isRowNull;
}

bool
NdbQueryOperationImpl::isRowChanged() const
{
  // Will be true until scan linked with scan is implemented.
  return true;
}

static bool isSetInMask(const unsigned char* mask, int bitNo)
{
  return mask[bitNo>>3] & 1<<(bitNo&7);
}

int
NdbQueryOperationImpl::serializeProject(Uint32Buffer& attrInfo)
{
  Uint32 startPos = attrInfo.getSize();
  attrInfo.append(0U);  // Temp write firste 'length' word, update later

  /**
   * If the columns in the projections are specified as 
   * in NdbRecord format, attrId are assumed to be ordered ascending.
   * In this form the projection spec. can be packed as
   * a single bitmap.
   */
  if (m_ndbRecord != NULL) {
    Bitmask<MAXNROFATTRIBUTESINWORDS> readMask;
    Uint32 requestedCols= 0;
    Uint32 maxAttrId= 0;

    for (Uint32 i= 0; i<m_ndbRecord->noOfColumns; i++)
    {
      const NdbRecord::Attr* const col= &m_ndbRecord->columns[i];
      Uint32 attrId= col->attrId;

      if (m_read_mask == NULL || isSetInMask(m_read_mask, i))
      { if (attrId > maxAttrId)
          maxAttrId= attrId;

        readMask.set(attrId);
        requestedCols++;

        const NdbColumnImpl* const column = getQueryOperationDef().getTable()
          .getColumn(col->column_no);
        if (column->getStorageType() == NDB_STORAGETYPE_DISK)
        {
          m_diskInUserProjection = true;
        }
      }
    }

    // Test for special case, get all columns:
    if (requestedCols == (unsigned)m_operationDef.getTable().getNoOfColumns()) {
      Uint32 ah;
      AttributeHeader::init(&ah, AttributeHeader::READ_ALL, requestedCols);
      attrInfo.append(ah);
    } else if (requestedCols > 0) {
      /* Serialize projection as a bitmap.*/
      const Uint32 wordCount = 1+maxAttrId/32; // Size of mask.
      Uint32* dst = attrInfo.alloc(wordCount+1);
      AttributeHeader::init(dst, 
                            AttributeHeader::READ_PACKED, 4*wordCount);
      memcpy(dst+1, &readMask, 4*wordCount);
    }
  } // if (m_ndbRecord...)

  /** Projection is specified in RecAttr format.
   *  This may also be combined with the NdbRecord format.
   */
  const NdbRecAttr* recAttr = m_firstRecAttr;
  /* Serialize projection as a list of Attribute id's.*/
  while (recAttr) {
    Uint32 ah;
    AttributeHeader::init(&ah,
                          recAttr->attrId(),
                          0);
    attrInfo.append(ah);
    if (recAttr->getColumn()->getStorageType() == NDB_STORAGETYPE_DISK)
    {
      m_diskInUserProjection = true;
    }
    recAttr = recAttr->next();
  }

  bool withCorrelation = getRoot().getQueryDef().isScanQuery();
  if (withCorrelation) {
    Uint32 ah;
    AttributeHeader::init(&ah, AttributeHeader::READ_ANY_VALUE, 0);
    attrInfo.append(ah);
  }

  // Size of projection in words.
  Uint32 length = attrInfo.getSize() - startPos - 1 ;
  attrInfo.put(startPos, length);
  return 0;
} // NdbQueryOperationImpl::serializeProject


int NdbQueryOperationImpl::serializeParams(const NdbQueryParamValue* paramValues)
{
  if (unlikely(paramValues == NULL))
  {
    return QRY_NEED_PARAMETER;
  }

  const NdbQueryOperationDefImpl& def = getQueryOperationDef();
  for (Uint32 i=0; i<def.getNoOfParameters(); i++)
  {
    const NdbParamOperandImpl& paramDef = def.getParameter(i);
    const NdbQueryParamValue& paramValue = paramValues[paramDef.getParamIx()];

    /**
     *  Add parameter value to serialized data.
     *  Each value has a Uint32 length field (in bytes), followed by
     *  the actuall value. Allocation is in Uint32 units with unused bytes
     *  zero padded.
     **/
    const void* addr;
    Uint32 len;
    bool null;
    const int error = paramValue.getValue(paramDef,addr,len,null);
    if (unlikely(error))
      return error;
    if (unlikely(null))
      return QRY_NEED_PARAMETER;

    m_params.append(len);          // paramValue length in #bytes
    m_params.append(addr,len);

    if(unlikely(m_params.isMemoryExhausted())){
      return Err_MemoryAlloc;
    }
  }
  return 0;
} // NdbQueryOperationImpl::serializeParams

int
NdbQueryOperationImpl::calculateBatchedRows(NdbQueryOperationImpl* scanParent)
{
  if (m_operationDef.isScanOperation())
    scanParent = this;

  for (Uint32 i = 0; i < m_children.size(); i++)
    m_children[i]->calculateBatchedRows(scanParent);

#ifdef TEST_SCANREQ
    m_maxBatchRows = 1;  // To force usage of SCAN_NEXTREQ even for small scans resultsets
#endif

  if (scanParent!=NULL)
  {
    Ndb& ndb = *getQuery().getNdbTransaction().getNdb();
    TransporterFacade *tp = ndb.theImpl->m_transporter_facade;

    // Calculate batchsize for query as minimum batchRows for all m_operations[].
    // Ignore calculated 'batchByteSize' and 'firstBatchRows' here - Recalculated
    // when building signal after max-batchRows has been determined.
    Uint32 batchByteSize, firstBatchRows;
    NdbReceiver::calculate_batch_size(tp,
                                      m_ndbRecord,
                                      m_firstRecAttr,
                                      0, // Key size.
                                      getQuery().getRootFragCount(),  // scanParent->getFragCount()
                                      scanParent->m_maxBatchRows,
                                      batchByteSize,
                                      firstBatchRows);
    assert (scanParent->m_maxBatchRows>0);
    assert (firstBatchRows==scanParent->m_maxBatchRows);
    m_maxBatchRows = firstBatchRows; // First guess, ::setBatchedRows() updates later  
  }
  else
    m_maxBatchRows = 1;

  return 0;
} // NdbQueryOperationImpl::calculateBatchedRows


void
NdbQueryOperationImpl::setBatchedRows(Uint32 batchedRows)
{
  if (m_operationDef.isScanOperation())
    batchedRows = this->m_maxBatchRows;

  for (Uint32 i = 0; i < m_children.size(); i++)
    m_children[i]->setBatchedRows(batchedRows);

  m_maxBatchRows = batchedRows;
}


int 
NdbQueryOperationImpl::prepareReceiver()
{
  const Uint32 rowSize = 
    NdbReceiver::ndbrecord_rowsize(m_ndbRecord, m_firstRecAttr,0,false);
  m_bufferSize = rowSize * getMaxBatchRows();
//ndbout "m_bufferSize=" << m_bufferSize << endl;

  if (m_bufferSize > 0) { // 0 bytes in batch if no result requested
    Uint32 bufLen = m_bufferSize*m_queryImpl.getRootFragCount();
#ifdef NDEBUG
    m_batchBuffer = new char[bufLen];
    if (unlikely(m_batchBuffer == NULL)) {
      return Err_MemoryAlloc;
    }
#else
    /* To be able to check for buffer overrun.*/
    m_batchBuffer = new char[bufLen+4];
    if (unlikely(m_batchBuffer == NULL)) {
      return Err_MemoryAlloc;
    }
    m_batchBuffer[bufLen+0] = 'a';
    m_batchBuffer[bufLen+1] = 'b';
    m_batchBuffer[bufLen+2] = 'c';
    m_batchBuffer[bufLen+3] = 'd';
#endif
  }

  // Construct receiver streams and prepare them for receiving scan result
  assert(m_resultStreams==NULL);
  assert(m_queryImpl.getRootFragCount() > 0);
  m_resultStreams = new NdbResultStream*[m_queryImpl.getRootFragCount()];
  if (unlikely(m_resultStreams == NULL)) {
    return Err_MemoryAlloc;
  }
  for(Uint32 i = 0; i<m_queryImpl.getRootFragCount(); i++) {
    m_resultStreams[i] = NULL;  // Init to legal contents for d'tor
  }
  for(Uint32 i = 0; i<m_queryImpl.getRootFragCount(); i++) {
    m_resultStreams[i] = new NdbResultStream(*this, i);
    if (unlikely(m_resultStreams[i] == NULL)) {
      return Err_MemoryAlloc;
    }
    const int error = m_resultStreams[i]->prepare();
    if (unlikely(error)) {
      return error;
    }

    m_resultStreams[i]->getReceiver().init(NdbReceiver::NDB_QUERY_OPERATION, 
                                        false, this);
    m_resultStreams[i]->getReceiver()
      .do_setup_ndbrecord(m_ndbRecord,
                          getMaxBatchRows(), 
                          0 /*key_size*/, 
                          0 /*read_range_no*/, 
                          rowSize,
                          &m_batchBuffer[m_bufferSize*i],
                          0);
    m_resultStreams[i]->getReceiver().prepareSend();
  }

  return 0;
}//NdbQueryOperationImpl::prepareReceiver

int 
NdbQueryOperationImpl::prepareAttrInfo(Uint32Buffer& attrInfo)
{
  // ::prepareReceiver() need to complete first:
  assert (m_resultStreams != NULL);

  const NdbQueryOperationDefImpl& def = getQueryOperationDef();

  /**
   * Serialize parameters refered by this NdbQueryOperation.
   * Params for the complete NdbQuery is collected in a single
   * serializedParams chunk. Each operations params are 
   * proceeded by 'length' for this operation.
   */
  if (def.getType() == NdbQueryOperationDef::UniqueIndexAccess)
  {
    // Reserve memory for LookupParameters, fill in contents later when
    // 'length' and 'requestInfo' has been calculated.
    Uint32 startPos = attrInfo.getSize();
    attrInfo.alloc(QN_LookupParameters::NodeSize);
    Uint32 requestInfo = 0;

    if (m_params.getSize() > 0)
    {
      // parameter values has been serialized as part of NdbTransaction::createQuery()
      // Only need to append it to rest of the serialized arguments
      requestInfo |= DABits::PI_KEY_PARAMS;
      attrInfo.append(m_params);
    }

    QN_LookupParameters* param = reinterpret_cast<QN_LookupParameters*>(attrInfo.addr(startPos));
    if (unlikely(param==NULL))
       return Err_MemoryAlloc;

    param->requestInfo = requestInfo;
    param->resultData = getIdOfReceiver();
    Uint32 length = attrInfo.getSize() - startPos;
    if (unlikely(length > 0xFFFF)) {
      return QRY_DEFINITION_TOO_LARGE; //Query definition too large.
    }
    QueryNodeParameters::setOpLen(param->len,
                                  QueryNodeParameters::QN_LOOKUP,
                                  length);

#ifdef __TRACE_SERIALIZATION
    ndbout << "Serialized params for index node " 
           << m_operationDef.getQueryOperationId()-1 << " : ";
    for(Uint32 i = startPos; i < attrInfo.getSize(); i++){
      char buf[12];
      sprintf(buf, "%.8x", attrInfo.get(i));
      ndbout << buf << " ";
    }
    ndbout << endl;
#endif
  } // if (UniqueIndexAccess ...

  // Reserve memory for LookupParameters, fill in contents later when
  // 'length' and 'requestInfo' has been calculated.
  Uint32 startPos = attrInfo.getSize();
  Uint32 requestInfo = 0;
  bool isRoot = (def.getQueryOperationIx()==0);

  QueryNodeParameters::OpType paramType =
       !def.isScanOperation() ? QueryNodeParameters::QN_LOOKUP
           : (isRoot) ? QueryNodeParameters::QN_SCAN_FRAG 
                      : QueryNodeParameters::QN_SCAN_INDEX;

  if (paramType == QueryNodeParameters::QN_SCAN_INDEX)
    attrInfo.alloc(QN_ScanIndexParameters::NodeSize);
  else if (paramType == QueryNodeParameters::QN_SCAN_FRAG)
    attrInfo.alloc(QN_ScanFragParameters::NodeSize);
  else
    attrInfo.alloc(QN_LookupParameters::NodeSize);

  // SPJ block assume PARAMS to be supplied before ATTR_LIST
  if (m_params.getSize() > 0 &&
      def.getType() != NdbQueryOperationDef::UniqueIndexAccess)
  {
    // parameter values has been serialized as part of NdbTransaction::createQuery()
    // Only need to append it to rest of the serialized arguments
    requestInfo |= DABits::PI_KEY_PARAMS;
    attrInfo.append(m_params);    
  }

  if (hasInterpretedCode())
  {
    requestInfo |= DABits::PI_ATTR_INTERPRET;
    const int error= prepareInterpretedCode(attrInfo);
    if (unlikely(error)) 
    {
      return error;
    }
  }

  requestInfo |= DABits::PI_ATTR_LIST;
  const int error = serializeProject(attrInfo);
  if (unlikely(error)) {
    return error;
  }

  if (diskInUserProjection())
  {
    requestInfo |= DABits::PI_DISK_ATTR;
  }

  Uint32 length = attrInfo.getSize() - startPos;
  if (unlikely(length > 0xFFFF)) {
    return QRY_DEFINITION_TOO_LARGE; //Query definition too large.
  }

  if (paramType == QueryNodeParameters::QN_SCAN_INDEX)
  {
    QN_ScanIndexParameters* param = reinterpret_cast<QN_ScanIndexParameters*>(attrInfo.addr(startPos)); 
    if (unlikely(param==NULL))
      return Err_MemoryAlloc;

    Ndb& ndb = *m_queryImpl.getNdbTransaction().getNdb();
    TransporterFacade *tp = ndb.theImpl->m_transporter_facade;

    Uint32 batchRows = getMaxBatchRows();
    Uint32 batchByteSize, firstBatchRows;
    NdbReceiver::calculate_batch_size(tp,
                                      m_ndbRecord,
                                      m_firstRecAttr,
                                      0, // Key size.
                                      m_queryImpl.getRootFragCount(),
                                      batchRows,
                                      batchByteSize,
                                      firstBatchRows);
    assert (batchRows==getMaxBatchRows());

    requestInfo |= QN_ScanIndexParameters::SIP_PARALLEL; // FIXME: SPJ always assume. SIP_PARALLEL
    param->requestInfo = requestInfo; 
    param->batchSize = ((Uint16)batchByteSize << 16) | (Uint16)firstBatchRows;
    param->resultData = getIdOfReceiver();
    QueryNodeParameters::setOpLen(param->len, paramType, length);
  }
  else if (paramType == QueryNodeParameters::QN_SCAN_FRAG)
  {
    QN_ScanFragParameters* param = reinterpret_cast<QN_ScanFragParameters*>(attrInfo.addr(startPos)); 
    if (unlikely(param==NULL))
      return Err_MemoryAlloc;

    param->requestInfo = requestInfo;
    param->resultData = getIdOfReceiver();
    QueryNodeParameters::setOpLen(param->len, paramType, length);
  }
  else
  {
    assert(paramType == QueryNodeParameters::QN_LOOKUP);
    QN_LookupParameters* param = reinterpret_cast<QN_LookupParameters*>(attrInfo.addr(startPos)); 
    if (unlikely(param==NULL))
      return Err_MemoryAlloc;

    param->requestInfo = requestInfo;
    param->resultData = getIdOfReceiver();
    QueryNodeParameters::setOpLen(param->len, paramType, length);
  }

#ifdef __TRACE_SERIALIZATION
  ndbout << "Serialized params for node " 
         << m_operationDef.getQueryOperationId() << " : ";
  for(Uint32 i = startPos; i < attrInfo.getSize(); i++){
    char buf[12];
    sprintf(buf, "%.8x", attrInfo.get(i));
    ndbout << buf << " ";
  }
  ndbout << endl;
#endif

  // Parameter values was appended to AttrInfo, shrink param buffer
  // to reduce memory footprint.
  m_params.releaseExtend();

  return 0;
} // NdbQueryOperationImpl::prepareAttrInfo


int 
NdbQueryOperationImpl::prepareKeyInfo(
                     Uint32Buffer& keyInfo,
                     const NdbQueryParamValue* actualParam)
{
  assert(this == &getRoot()); // Should only be called for root operation.
#ifdef TRACE_SERIALIZATION
  int startPos = keyInfo.getSize();
#endif

  const NdbQueryOperationDefImpl::IndexBound* bounds = m_operationDef.getBounds();
  if (bounds)
  {
    const int error = prepareIndexKeyInfo(keyInfo, bounds, actualParam);
    if (unlikely(error))
      return error;
  }

  const NdbQueryOperandImpl* const* keys = m_operationDef.getKeyOperands();
  if (keys)
  {
    const int error = prepareLookupKeyInfo(keyInfo, keys, actualParam);
    if (unlikely(error))
      return error;
  }

  if (unlikely(keyInfo.isMemoryExhausted())) {
    return Err_MemoryAlloc;
  }

#ifdef TRACE_SERIALIZATION
  ndbout << "Serialized KEYINFO for NdbQuery root : ";
  for (Uint32 i = startPos; i < keyInfo.getSize(); i++) {
    char buf[12];
    sprintf(buf, "%.8x", keyInfo.get(i));
    ndbout << buf << " ";
  }
  ndbout << endl;
#endif

  return 0;
} // NdbQueryOperationImpl::prepareKeyInfo


static int
formatAttr(const NdbColumnImpl* column,
           const void* &value, Uint32& len,
           char* buffer, Uint32 buflen)
{
  // Check that column->shrink_varchar() not specified, only used by mySQL
  // assert (!(column->flags & NdbDictionary::RecMysqldShrinkVarchar));

  switch (column->getArrayType()) {
    case NdbDictionary::Column::ArrayTypeFixed:
      break;
    case NdbDictionary::Column::ArrayTypeShortVar:
      if (unlikely(len > 0xFF || len+1 > buflen))
        return QRY_CHAR_OPERAND_TRUNCATED;
      buffer[0] = (unsigned char)len;
      memcpy(buffer+1, value, len);
      len+=1;
      value = buffer;
      break;
    case NdbDictionary::Column::ArrayTypeMediumVar:
      if (unlikely(len > 0xFFFF || len+2 > buflen))
        return QRY_CHAR_OPERAND_TRUNCATED;
      buffer[0] = (unsigned char)(len & 0xFF);
      buffer[1] = (unsigned char)(len >> 8);
      memcpy(buffer+2, value, len);
      len+=2;
      value = buffer;
      break;
    default:
      assert(false);
  }
  return 0;
} // static formatAttr


static int
appendBound(Uint32Buffer& keyInfo,
            NdbIndexScanOperation::BoundType type, const NdbQueryOperandImpl* bound,
            const NdbQueryParamValue* actualParam) 
{
  Uint32 len = 0;
  const void* boundValue = NULL;

  assert (bound);
  switch(bound->getKind()){
  case NdbQueryOperandImpl::Const:
  {
    const NdbConstOperandImpl* constOp = static_cast<const NdbConstOperandImpl*>(bound);
    boundValue = constOp->getAddr();
    len = constOp->getSizeInBytes();
    break;
  }
  case NdbQueryOperandImpl::Param:
  {
    const NdbParamOperandImpl* const paramOp 
      = static_cast<const NdbParamOperandImpl*>(bound);
    int paramNo = paramOp->getParamIx();
    assert(actualParam != NULL);

    bool null;
    const int error = actualParam[paramNo].getValue(*paramOp,boundValue,len,null);
    if (unlikely(error))
      return error;
    if (unlikely(null))
      return Err_KeyIsNULL;
    break;
  }
  case NdbQueryOperandImpl::Linked:    // Root operation cannot have linked operands.
  default:
    assert(false);
  }
    
  char tmp[NDB_MAX_KEY_SIZE];
  const NdbColumnImpl* column = bound->getColumn();

  int error = 
    formatAttr(column, boundValue, len, tmp, static_cast<Uint32>(sizeof(tmp)));
  if (unlikely(error))
    return error;

  AttributeHeader ah(column->m_attrId, len);

  keyInfo.append(type);
  keyInfo.append(ah.m_value);
  keyInfo.append(boundValue,len);

  return 0;
} // static appendBound()


int
NdbQueryOperationImpl::prepareIndexKeyInfo(
                     Uint32Buffer& keyInfo,
                     const NdbQueryOperationDefImpl::IndexBound* bounds,
                     const NdbQueryParamValue* actualParam)
{
  int startPos = keyInfo.getSize();
  assert (startPos == 0);  // Assumed by ::checkPrunable

  const unsigned key_count = 
     (bounds->lowKeys >= bounds->highKeys) ? bounds->lowKeys : bounds->highKeys;

  for (unsigned keyNo = 0; keyNo < key_count; keyNo++)
  {
    NdbIndexScanOperation::BoundType bound_type;

    /* If upper and lower limit is equal, a single BoundEQ is sufficient */
    if (keyNo < bounds->lowKeys  &&
        keyNo < bounds->highKeys &&
        bounds->low[keyNo] == bounds->high[keyNo])
    {
      /* Inclusive if defined, or matching rows can include this value */
      bound_type= NdbIndexScanOperation::BoundEQ;
      int error = appendBound(keyInfo, bound_type, bounds->low[keyNo], actualParam);
      if (unlikely(error))
        return error;

    } else {

      /* If key is part of lower bound */
      if (keyNo < bounds->lowKeys)
      {
        /* Inclusive if defined, or matching rows can include this value */
        bound_type= bounds->lowIncl  || keyNo+1 < bounds->lowKeys ?
            NdbIndexScanOperation::BoundLE : NdbIndexScanOperation::BoundLT;

        int error = appendBound(keyInfo, bound_type, bounds->low[keyNo], actualParam);
        if (unlikely(error))
          return error;
      }

      /* If key is part of upper bound */
      if (keyNo < bounds->highKeys)
      {
        /* Inclusive if defined, or matching rows can include this value */
        bound_type= bounds->highIncl  || keyNo+1 < bounds->highKeys ?
            NdbIndexScanOperation::BoundGE : NdbIndexScanOperation::BoundGT;

        int error = appendBound(keyInfo, bound_type, bounds->high[keyNo], actualParam);
        if (unlikely(error))
          return error;
      }
    }
  }

  Uint32 length = keyInfo.getSize()-startPos;
  if (unlikely(keyInfo.isMemoryExhausted())) {
    return Err_MemoryAlloc;
  } else if (unlikely(length > 0xFFFF)) {
    return QRY_DEFINITION_TOO_LARGE; // Query definition too large.
  } else if (likely(length > 0)) {
    keyInfo.put(startPos, keyInfo.get(startPos) | (length << 16));
  }

  return 0;
} // NdbQueryOperationImpl::prepareIndexKeyInfo


int
NdbQueryOperationImpl::prepareLookupKeyInfo(
                     Uint32Buffer& keyInfo,
                     const NdbQueryOperandImpl* const keys[],
                     const NdbQueryParamValue* actualParam)
{
  const int keyCount = m_operationDef.getIndex()!=NULL ? 
    static_cast<int>(m_operationDef.getIndex()->getNoOfColumns()) :
    m_operationDef.getTable().getNoOfPrimaryKeys();

  for (int keyNo = 0; keyNo<keyCount; keyNo++)
  {
    Uint32 len = 0;
    const void* boundValue = NULL;

    switch(keys[keyNo]->getKind()){
    case NdbQueryOperandImpl::Const:
    {
      const NdbConstOperandImpl* const constOp 
        = static_cast<const NdbConstOperandImpl*>(keys[keyNo]);
      boundValue = constOp->getAddr();
      len = constOp->getSizeInBytes();
      break;
    }
    case NdbQueryOperandImpl::Param:
    {
      const NdbParamOperandImpl* const paramOp 
        = static_cast<const NdbParamOperandImpl*>(keys[keyNo]);
      int paramNo = paramOp->getParamIx();
      assert(actualParam != NULL);

      bool null;
      const int error = actualParam[paramNo].getValue(*paramOp,boundValue,len,null);
      if (unlikely(error))
        return error;
      if (unlikely(null))
        return Err_KeyIsNULL;
      break;
    }
    case NdbQueryOperandImpl::Linked:    // Root operation cannot have linked operands.
    default:
      assert(false);
    }

    char tmp[NDB_MAX_KEY_SIZE];
    const NdbColumnImpl* column = keys[keyNo]->getColumn();

    int error = 
      formatAttr(column, boundValue, len, tmp, static_cast<Uint32>(sizeof(tmp)));
    if (unlikely(error))
      return error;

    keyInfo.append(boundValue,len);
  }

  if (unlikely(keyInfo.isMemoryExhausted())) {
    return Err_MemoryAlloc;
  }

  return 0;
} // NdbQueryOperationImpl::prepareLookupKeyInfo


bool 
NdbQueryOperationImpl::execTRANSID_AI(const Uint32* ptr, Uint32 len){
  if (traceSignals) {
    ndbout << "NdbQueryOperationImpl::execTRANSID_AI()" << endl;
  }
  bool ret = false;
  NdbRootFragment* rootFrag = NULL;

  if(getQueryDef().isScanQuery()){
    const Uint32 receiverId = CorrelationData(ptr, len).getRootReceiverId();
    
    /** receiverId holds the Id of the receiver of the corresponding stream
     * of the root operation. We can thus find the correct root fragment 
     * number.
     */
    Uint32 rootFragNo;
    for(rootFragNo = 0; 
        rootFragNo<getQuery().getRootFragCount() && 
          getRoot().m_resultStreams[rootFragNo]->getReceiver().getId() 
          != receiverId; 
        rootFragNo++);
    assert(rootFragNo<getQuery().getRootFragCount());

    // Process result values.
    m_resultStreams[rootFragNo]->execTRANSID_AI(ptr, len);

    rootFrag = &getQuery().m_rootFrags[rootFragNo];
    rootFrag->incrOutstandingResults(-1);

    if (rootFrag->isFragBatchComplete()) {
      m_queryImpl.incrementPendingFrags(-1);
      m_queryImpl.handleBatchComplete(rootFragNo);

      /* nextResult() will later move it from m_fullFrags to m_applFrags
       * under mutex protection.*/
      assert(!rootFrag->isEmpty());
      m_queryImpl.m_fullFrags.push(*rootFrag);
      // Wake up appl thread when we have data, or entire query batch completed.
      ret = true;
    }
  } else { // Lookup query
    // The root operation is a lookup.
    m_resultStreams[0]->execTRANSID_AI(ptr, len);

    rootFrag = &getQuery().m_rootFrags[0];
    rootFrag->incrOutstandingResults(-1);

    if (rootFrag->isFragBatchComplete()) {
      ret = m_queryImpl.incrementPendingFrags(-1);
      assert(ret); // The query should be complete now.
    }
  } // end lookup

  if (traceSignals) {
    ndbout << "NdbQueryOperationImpl::execTRANSID_AI(): returns:" << ret
           << ", *this=" << *this <<  endl;
  }
  return ret;
} //NdbQueryOperationImpl::execTRANSID_AI


bool 
NdbQueryOperationImpl::execTCKEYREF(NdbApiSignal* aSignal){
  if (traceSignals) {
    ndbout << "NdbQueryOperationImpl::execTCKEYREF()" <<  endl;
  }

  /* The SPJ block does not forward TCKEYREFs for trees with scan roots.*/
  assert(!getQueryDef().isScanQuery());

  const TcKeyRef* ref = CAST_CONSTPTR(TcKeyRef, aSignal->getDataPtr());
  if (!getQuery().m_transaction.checkState_TransId(ref->transId))
  {
#ifdef NDB_NO_DROPPED_SIGNAL
    abort();
#endif
    return false;
  }

  // Suppress 'TupleNotFound' status for child operations.
  if (&getRoot() == this || ref->errorCode != Err_TupleNotFound)
  {
    getQuery().setErrorCode(ref->errorCode);
    if (aSignal->getLength() == TcKeyRef::SignalLength)
    {
      // Signal may contain additional error data
      getQuery().m_error.details = (char *)ref->errorData;
    }
  }

  if (ref->errorCode != DbspjErr::NodeFailure)
  {
    // Compensate for children results not produced.
    // (doSend() assumed all child results to be materialized)
    Uint32 cnt = 0;
    cnt += 1; // self
    cnt += getNoOfDescendantOperations();
    if (getNoOfChildOperations() > 0)
    {
      cnt += getNoOfLeafOperations();
    }
    getQuery().m_rootFrags[0].incrOutstandingResults(- Int32(cnt));
  }
  else
  {
    // consider frag-batch complete
    getQuery().m_rootFrags[0].clearOutstandingResults();
  }

  bool ret = false;
  if (getQuery().m_rootFrags[0].isFragBatchComplete()) { 
    ret = m_queryImpl.incrementPendingFrags(-1);
    assert(ret); // The query should be complete now.
  } 

  if (traceSignals) {
    ndbout << "NdbQueryOperationImpl::execTCKEYREF(): returns:" << ret
           << ", *getRoot().m_resultStreams[0] {" 
           << *getRoot().m_resultStreams[0] << "}"
           << ", *this=" << *this <<  endl;
  }
  return ret;
} //NdbQueryOperationImpl::execTCKEYREF

bool
NdbQueryOperationImpl::execSCAN_TABCONF(Uint32 tcPtrI, 
                                        Uint32 rowCount,
                                        NdbReceiver* receiver)
{
  if (traceSignals) {
    ndbout << "NdbQueryOperationImpl::execSCAN_TABCONF()" << endl;
  }
  assert(checkMagicNumber());
  // For now, only the root operation may be a scan.
  assert(&getRoot() == this);
  assert(m_operationDef.isScanOperation());
  Uint32 fragNo;
  // Find root fragment number.
  for(fragNo = 0; 
      fragNo<getQuery().getRootFragCount() && 
        &getRoot().m_resultStreams[fragNo]
        ->getReceiver() != receiver; 
      fragNo++);
  assert(fragNo<getQuery().getRootFragCount());

  NdbRootFragment& rootFrag = getQuery().m_rootFrags[fragNo];
  rootFrag.setConfReceived();
  rootFrag.incrOutstandingResults(rowCount);

  // Handle for SCAN_NEXTREQ, RNIL -> EOF
  NdbResultStream& resultStream = *m_resultStreams[fragNo];
  resultStream.getReceiver().m_tcPtrI = tcPtrI;  

  if (rootFrag.finalBatchReceived())
  {
    m_queryImpl.m_finalBatchFrags++;
  }
  if(traceSignals){
    ndbout << "  resultStream(root) {" << resultStream << "}" << endl;
  }

  bool ret = false;
  if (rootFrag.isFragBatchComplete()) {
    /* This fragment is now complete*/
    m_queryImpl.incrementPendingFrags(-1);
    m_queryImpl.handleBatchComplete(fragNo);

    /* nextResult() will later move it from m_fullFrags to m_applFrags
     * under mutex protection.*/
    m_queryImpl.m_fullFrags.push(rootFrag);
    // Don't awake before we have data, or query batch completed.
    ret = resultStream.getReceiver().hasResults() || 
      m_queryImpl.isBatchComplete();
  }
  if (traceSignals) {
    ndbout << "NdbQueryOperationImpl::execSCAN_TABCONF():, returns:" << ret
           << ", tcPtrI=" << tcPtrI << " rowCount=" << rowCount 
           << " *this=" << *this << endl;
  }
  return ret;
} //NdbQueryOperationImpl::execSCAN_TABCONF

int
NdbQueryOperationImpl::setOrdering(NdbQueryOptions::ScanOrdering ordering)
{
  if (getQueryOperationDef().getType() != NdbQueryOperationDef::OrderedIndexScan)
  {
    getQuery().setErrorCode(QRY_WRONG_OPERATION_TYPE);
    return -1;
  }

  if(static_cast<const NdbQueryIndexScanOperationDefImpl&>
       (getQueryOperationDef())
     .getOrdering() != NdbQueryOptions::ScanOrdering_void)
  {
    getQuery().setErrorCode(QRY_SCAN_ORDER_ALREADY_SET);
    return -1;
  }
  
  m_ordering = ordering;
  return 0;
} // NdbQueryOperationImpl::setOrdering()

int NdbQueryOperationImpl::setInterpretedCode(const NdbInterpretedCode& code)
{
  if (code.m_instructions_length == 0)
  {
    return 0;
  }

  const NdbTableImpl& table = getQueryOperationDef().getTable();
  // Check if operation and interpreter code use the same table
  if (unlikely(table.getTableId() != code.getTable()->getTableId()
               || table_version_major(table.getObjectVersion()) != 
               table_version_major(code.getTable()->getObjectVersion())))
  {
    getQuery().setErrorCodeAbort(Err_InterpretedCodeWrongTab);
    return -1;
  }

  // Allocate an interpreted code object if we do not have one already.
  if (likely(m_interpretedCode == NULL))
  {
    m_interpretedCode = new NdbInterpretedCode();

    if (unlikely(m_interpretedCode==NULL))
    {
      getQuery().setErrorCodeAbort(Err_MemoryAlloc);
      return -1;
    }
  }

  /* 
   * Make a deep copy, such that 'code' can be destroyed when this method 
   * returns.
   */
  const int error = m_interpretedCode->copy(code);
  if (unlikely(error))
  {
    getQuery().setErrorCodeAbort(error);
    return -1;
  }
  return 0;
} // NdbQueryOperationImpl::setInterpretedCode()


NdbResultStream& 
NdbQueryOperationImpl::getResultStream(Uint32 rootFragNo) const
{
  assert(rootFragNo < getQuery().getRootFragCount());
  return *m_resultStreams[rootFragNo];
}

bool
NdbQueryOperationImpl::hasInterpretedCode() const
{
  return m_interpretedCode && m_interpretedCode->m_instructions_length > 0;
} // NdbQueryOperationImpl::hasInterpretedCode

int
NdbQueryOperationImpl::prepareInterpretedCode(Uint32Buffer& attrInfo) const
{
  // There should be no subroutines in a filter.
  assert(m_interpretedCode->m_first_sub_instruction_pos==0);

  if (unlikely((m_interpretedCode->m_flags & NdbInterpretedCode::Finalised) 
               == 0))
  {
    //  NdbInterpretedCode::finalise() not called.
    return Err_FinaliseNotCalled;
  }

  assert(m_interpretedCode->m_instructions_length > 0);
  assert(m_interpretedCode->m_instructions_length <= 0xffff);

  // Allocate space for program and length field.
  Uint32* const buffer = 
    attrInfo.alloc(1+m_interpretedCode->m_instructions_length);
  if(unlikely(buffer==NULL))
  {
    return Err_MemoryAlloc;
  }

  buffer[0] = m_interpretedCode->m_instructions_length;
  memcpy(buffer+1, 
         m_interpretedCode->m_buffer, 
         m_interpretedCode->m_instructions_length * sizeof(Uint32));
  return 0;
} // NdbQueryOperationImpl::prepareInterpretedCode


Uint32 
NdbQueryOperationImpl::getIdOfReceiver() const {
  return m_resultStreams[0]->getReceiver().getId();
}


const NdbReceiver& 
NdbQueryOperationImpl::getReceiver(Uint32 recNo) const {
  assert(recNo<getQuery().getRootFragCount());
  assert(m_resultStreams!=NULL);
  return m_resultStreams[recNo]->getReceiver();
}


/** For debugging.*/
NdbOut& operator<<(NdbOut& out, const NdbQueryOperationImpl& op){
  out << "[ this: " << &op
      << "  m_magic: " << op.m_magic;
  if (op.getParentOperation()){
    out << "  m_parent" << op.getParentOperation(); 
  }
  for(unsigned int i = 0; i<op.getNoOfChildOperations(); i++){
    out << "  m_children[" << i << "]" << &op.getChildOperation(i); 
  }
  out << "  m_queryImpl: " << &op.m_queryImpl;
  out << "  m_operationDef: " << &op.m_operationDef;
  for(Uint32 i = 0; i<op.m_queryImpl.getRootFragCount(); i++){
    out << "  m_resultStream[" << i << "]{" << *op.m_resultStreams[i] << "}";
  }
  out << " m_isRowNull " << op.m_isRowNull;
  out << " ]";
  return out;
}

NdbOut& operator<<(NdbOut& out, const NdbResultStream& stream){
  out << " m_rowCount: " << stream.m_rowCount;
  return out;
}

 
// Compiler settings require explicit instantiation.
template class Vector<NdbQueryOperationImpl*>;
