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


#include "NdbQueryOperationImpl.hpp"
#include <ndb_global.h>
#include "NdbQueryBuilder.hpp"
#include "NdbQueryBuilderImpl.hpp"
#include "signaldata/TcKeyReq.hpp"
#include "signaldata/ScanTab.hpp"
#include "signaldata/QueryTree.hpp"

#include "AttributeHeader.hpp"
#include "NdbRecord.hpp"
#include "NdbRecAttr.hpp"
#include "TransporterFacade.hpp"
#include "NdbApiSignal.hpp"
#include "NdbTransaction.hpp"
#include "NdbInterpretedCode.hpp"
#include "NdbScanFilter.hpp"

#if 0
#define DEBUG_CRASH() assert(false)
#else
#define DEBUG_CRASH()
#endif

//#define TEST_SCANREQ

/* Various error codes that are not specific to NdbQuery. */
STATIC_CONST(Err_MemoryAlloc = 4000);
STATIC_CONST(Err_SendFailed = 4002);
STATIC_CONST(Err_UnknownColumn = 4004);
STATIC_CONST(Err_ReceiveFromNdbFailed = 4008);
STATIC_CONST(Err_NodeFailCausedAbort = 4028);
STATIC_CONST(Err_MixRecAttrAndRecord = 4284);
STATIC_CONST(Err_DifferentTabForKeyRecAndAttrRec = 4287);
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
  { m_outstandingResults += delta;}

  void setConfReceived()
  { 
    assert(!m_confReceived);
    m_confReceived = true; 
  }

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
  bool isEmpty() const
  { return !m_query->getQueryOperation(0U).getReceiver(m_fragNo).nextResult(); }

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
public:
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
   * Find the number of the child tuple for a given tuple number and child 
   * operation.
   * @param childOperationNo Number of child operation. 0..n-1 if there are n 
   * child operations.
   * @param tupleNo The number of the tuple 0..n-1 if there are n result tuples
   * for this operation in this batch derived from the m_rootFragNo'th fragment 
   * of the root operation.
   * @return Tuple number of corresponding tuple from child operation. 
   * 'tupleNotFound' if there was no result from the child operation.
   */
  Uint16 getChildTupleNo(Uint32 childOperationNo, Uint16 tupleNo) const;
    
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

  /** Fix parent-child references when a complete batch has been received
   * for a given fragment.
   */
  void buildChildTupleLinks();

  /** For debugging.*/
  friend NdbOut& operator<<(NdbOut& out, const NdbResultStream&);

private:
  /** A map from tuple Id to tuple number.*/
  class TupleIdMap {
  public:
    explicit TupleIdMap():m_vector(){}

    void put(Uint16 tupleId, Uint16 tupleNum);

    Uint16 get(Uint16 tupleId) const;

    void clear()
    { m_vector.clear(); }

  private:
    struct Pair{
      /** Tuple id, unique within this batch and stream.*/
      Uint16 m_tupleId;
      /** Tuple sequence number, among tuples received in this stream.*/
      Uint16 m_tupleNum;
    };
    Vector<Pair> m_vector;

    /** No copying.*/
    TupleIdMap(const TupleIdMap&);
    TupleIdMap& operator=(const TupleIdMap&);
  }; // class TupleIdMap

  /** This stream handles results derived from the from the m_rootFragNo'th 
   * fragment of the root operation.*/
  const Uint32 m_rootFragNo;

  /** The receiver object that unpacks transid_AI messages.*/
  NdbReceiver m_receiver;

  /** The number of transid_AI messages received.*/
  Uint32 m_rowCount;

  /** A map from tuple Id to tuple number.*/
  TupleIdMap m_tupIdToTupNumMap;

  /** Operation to which this resultStream belong.*/
  NdbQueryOperationImpl& m_operation;

  /** One-dimensional array. For each tuple, this array holds the tuple id of 
   * the parent tuple.
   */
  Uint16* m_parentTupleId;

  /** 
   * This is a two dimensional array with a row for each result tuple and a 
   * column for each child operation. Each entry holds the tuple number 
   * of the corresponding result tuple for a child operation. (By tuple number
   * we mean the order in which tuples appear in NdbReceiver buffers.)
   * 
   * This table is populated by buildChildTupleLinks(). It is used when calling
   * nextResult(), to fetch the right result tuples for descendant operations.
   */
  Uint16* m_childTupleIdx;

  /**
   * Bind a tuple to a child operation tuple for a given child operation. 
   *
   * @param childOpNo Sequence number of child operation.
   * @param tupleNo Sequence number of tuple.
   * @param childTupleNo Sequence number of child tuple.
   */
  void setChildTupleNo(Uint32 childOpNo, 
                       Uint16 tupleNo, 
                       Uint16 childTupleNo);

  /** No copying.*/
  NdbResultStream(const NdbResultStream&);
  NdbResultStream& operator=(const NdbResultStream&);
}; //class NdbResultStream

////////////////////////////////////////////////
/////////  NdbResultStream methods ///////////
////////////////////////////////////////////////

void 
NdbResultStream::TupleIdMap::put(Uint16 tupleId, Uint16 num){
  const Pair p = {tupleId, num};
  m_vector.push_back(p);
}

Uint16
NdbResultStream::TupleIdMap::get(Uint16 tupleId) const {
  for(Uint32 i=0; i<m_vector.size(); i++){
    if(m_vector[i].m_tupleId == tupleId){
      return m_vector[i].m_tupleNum;
    }
  }
  return tupleNotFound;
}

NdbResultStream::NdbResultStream(NdbQueryOperationImpl& operation, Uint32 rootFragNo):
  m_rootFragNo(rootFragNo),
  m_receiver(operation.getQuery().getNdbTransaction().getNdb(), &operation),  // FIXME? Use Ndb recycle lists
  m_rowCount(0),
  m_tupIdToTupNumMap(),
  m_operation(operation),
  m_parentTupleId(NULL),
  m_childTupleIdx(NULL)
{};

NdbResultStream::~NdbResultStream() { 
  delete[] m_childTupleIdx; 
  delete[] m_parentTupleId; 
}

int  // Return 0 if ok, else errorcode
NdbResultStream::prepare()
{
  /* Parent / child correlation is only relevant for scan type queries
   * Don't create m_parentTupleId[] and m_childTupleIdx[] for lookups!
   * Neither is these structures required for operations not having respective
   * child or parent operations.
   */
  if (m_operation.getQueryDef().isScanQuery()) {

    const size_t batchRows = m_operation.getQuery().getMaxBatchRows();
    if (m_operation.getNoOfParentOperations()>0) {
      assert (m_operation.getNoOfParentOperations()==1);
      m_parentTupleId = new Uint16[batchRows];
      if (unlikely(m_parentTupleId==NULL))
        return Err_MemoryAlloc;
    }

    if (m_operation.getNoOfChildOperations()>0) {
      const size_t correlatedChildren =  batchRows
                                     * m_operation.getNoOfChildOperations();
      m_childTupleIdx = new Uint16[correlatedChildren];
      if (unlikely(m_childTupleIdx==NULL))
        return Err_MemoryAlloc;

      for (unsigned i=0; i<correlatedChildren; i++) {
        m_childTupleIdx[i] = tupleNotFound;
      }
    }
  }

  return 0;
} //NdbResultStream::prepare


void
NdbResultStream::reset()
{
  assert (m_operation.getQueryDef().isScanQuery());

  // Root scan-operation need a ScanTabConf to complete
  m_rowCount = 0;

  if (m_childTupleIdx!=NULL) {
    const size_t correlatedChildren =  m_operation.getQuery().getMaxBatchRows()
                                   * m_operation.getNoOfChildOperations();
    for (unsigned i=0; i<correlatedChildren; i++) {
      m_childTupleIdx[i] = tupleNotFound;
    }
  }

  m_tupIdToTupNumMap.clear();

  m_receiver.prepareSend();
} //NdbResultStream::reset


void
NdbResultStream::setChildTupleNo(Uint32 childOpNo, 
                                 Uint16 tupleNo, 
                                 Uint16 childTupleNo)
{
  assert (tupleNo < m_operation.getQuery().getMaxBatchRows());
  const Uint32 ix = (tupleNo*m_operation.getNoOfChildOperations()) + childOpNo;
  m_childTupleIdx[ix] = childTupleNo;
}

Uint16
NdbResultStream::getChildTupleNo(Uint32 childOpNo, Uint16 tupleNo) const
{
  assert (tupleNo < m_operation.getQuery().getMaxBatchRows());
  const Uint32 ix = (tupleNo*m_operation.getNoOfChildOperations()) + childOpNo;
  return m_childTupleIdx[ix];
}


void
NdbResultStream::execTRANSID_AI(const Uint32 *ptr, Uint32 len)
{
  if(m_operation.getQueryDef().isScanQuery())
  {
    const CorrelationData correlData(ptr, len);

    assert(m_operation.getRoot().getResultStream(m_rootFragNo)
           .m_receiver.getId() == correlData.getRootReceiverId());

    m_receiver.execTRANSID_AI(ptr, len - CorrelationData::wordCount);

    /**
     * We need to keep this number, such that we can later find the child
     * tuples of this tuple. Since tuples may arrive in any order, we
     * cannot match this tuple with its children until all tuples (for this 
     * batch and root fragment) have arrived.
     */
    m_tupIdToTupNumMap.put(correlData.getTupleId(), m_rowCount);

    if (m_parentTupleId)
    {
      /**
       * We need to keep this number, such that we can later find the parent
       * tuple of this tuple. Since tuples may arrive in any order, we
       * cannot match parent and child until all tuples (for this batch and 
       * root fragment) have arrived.
       */
      m_parentTupleId[m_rowCount] = correlData.getParentTupleId();
    } 
    else 
    {
      // This must be the root operation.
      assert (m_operation.getNoOfParentOperations()==0);
    }
  }
  else
  {
    // Lookup query.
    m_receiver.execTRANSID_AI(ptr, len);
  }
  m_rowCount++;
} // NdbResultStream::execTRANSID_AI()


void 
NdbResultStream::buildChildTupleLinks()
{
  /* Now we have received all tuples for all operations. 
   * Set correct #rows received in the NdbReceiver.
   */
  getReceiver().m_result_rows = getRowCount();

  if (m_operation.getNoOfParentOperations()>0) {
    assert(m_operation.getNoOfParentOperations()==1);
    NdbQueryOperationImpl* parent = &m_operation.getParentOperation(0);

    /* Find the number of this operation in its parent's list of children.*/
    Uint32 childOpNo = 0;
    while(childOpNo < parent->getNoOfChildOperations() &&
          &m_operation != &parent->getChildOperation(childOpNo)){
      childOpNo++;
    }
    assert(childOpNo < parent->getNoOfChildOperations());

    /* Make references from parent tuple to child tuple. These will be
     * used by nextResult() to fetch the proper children when iterating
     * over the result of a scan with children.
     */
    NdbResultStream& parentStream = parent->getResultStream(m_rootFragNo);
    for (Uint32 tupNo = 0; tupNo<getRowCount(); tupNo++) {
      /* Get the number (index) of the parent tuple among those tuples 
       * received for the parent operation within this stream and batch.
       */
      const Uint32 parentTupNo = 
        parentStream.m_tupIdToTupNumMap.get(m_parentTupleId[tupNo]);
      // Verify that the parent tuple exists.
      assert(parentTupNo != tupleNotFound);

      /* Verify that no child tuple has been set for this parent tuple
       * and child operation yet.
       */
      assert(parentStream.getChildTupleNo(childOpNo, parentTupNo) 
             == tupleNotFound);
      /* Set this tuple as the child of its parent tuple*/
      parentStream.setChildTupleNo(childOpNo, parentTupNo, tupNo);
    }
  } 
} //NdbResultStream::buildChildTupleLinks



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


bool NdbRootFragment::finalBatchReceived() const
{
  return getResultStream(0).getReceiver().m_tcPtrI==RNIL;
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
NdbQuery::setBound(const NdbIndexScanOperation::IndexBound *bound)
{
  const int error = m_impl.setBound(bound);
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
                       char* resBuffer,
                       const unsigned char* result_mask)
{
  if (unlikely(resBuffer==0)) {
    m_impl.getQuery().setErrorCode(QRY_REQ_ARG_IS_NULL);
    return -1;
  }
  return m_impl.setResultRowBuf(resBuffer, result_mask);
}

int
NdbQueryOperation::setResultRowRef (
                       const char* & bufRef,
                       const unsigned char* result_mask)
{
  return m_impl.setResultRowRef(bufRef, result_mask);
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
NdbQueryOperation::setOrdering(NdbScanOrdering ordering)
{
  return m_impl.setOrdering(ordering);
}

NdbScanOrdering
NdbQueryOperation::getOrdering() const
{
  return m_impl.getOrdering();
}

int NdbQueryOperation::setInterpretedCode(NdbInterpretedCode& code) const
{
  return m_impl.setInterpretedCode(code);
}

/////////////////////////////////////////////////
/////////  NdbQueryParamValue methods ///////////
/////////////////////////////////////////////////

enum Type
{
  Type_NULL,
  Type_raw,     // Raw data formated according to NdbRecord::Attr spec. 
  Type_string,  // '\0' terminated C-type string, char/varchar data only
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

// Raw data in NdbRecord::Attr format.
NdbQueryParamValue::NdbQueryParamValue(const void* val) : m_type(Type_raw)
{ m_value.raw = val; }

// NULL-value, also used as optional end marker 
NdbQueryParamValue::NdbQueryParamValue() : m_type(Type_NULL)
{}

int NdbQueryParamValue::getValue(const NdbParamOperandImpl& param,
                                 const void*& addr, size_t& len,
                                 bool& is_null) const
{
  const NdbRecord::Attr* attr = param.getRecordAttr();
  is_null = false;

  // Fetch parameter value and length.
  // Rudimentary typecheck of paramvalue: At least length should be as expected:
  switch(m_type)
  {
    case Type_NULL:
      addr = NULL;
      len  = 0;
      is_null = true;
      return 0;
    case Type_Uint16:
      addr = &m_value;
      len = sizeof(m_value.uint16);
      if (unlikely(len != attr->maxSize))
        return QRY_PARAMETER_HAS_WRONG_TYPE;
      break;
    case Type_Uint32:
      addr = &m_value;
      len = sizeof(m_value.uint32);
      if (unlikely(len != attr->maxSize))
        return QRY_PARAMETER_HAS_WRONG_TYPE;
      break;
    case Type_Uint64:
      addr = &m_value;
      len = sizeof(m_value.uint64);
      if (unlikely(len != attr->maxSize))
        return QRY_PARAMETER_HAS_WRONG_TYPE;
      break;
    case Type_Double:
      addr = &m_value;
      len = sizeof(m_value.dbl);
      if (unlikely(len != attr->maxSize))
        return QRY_PARAMETER_HAS_WRONG_TYPE;
      break;
    case Type_string:
      addr = m_value.string;
      len  = strlen(m_value.string);
      if (unlikely(len > attr->maxSize))
        return QRY_CHAR_PARAMETER_TRUNCATED;
      break;
    case Type_raw:
    {
      if (attr->flags & NdbRecord::IsVar1ByteLen)
      {
        if (attr->flags & NdbRecord::IsMysqldShrinkVarchar)
        {
          len  = uint2korr((Uint8*)m_value.raw);
          addr = ((Uint8*)m_value.raw)+2;
        }
        else
        {
          len  = *((Uint8*)(m_value.raw));
          addr = ((Uint8*)m_value.raw)+1;
        }
        if (unlikely(len > attr->maxSize))
          return QRY_CHAR_PARAMETER_TRUNCATED;
      }
      else if (attr->flags & NdbRecord::IsVar2ByteLen)
      {
        len  = uint2korr((Uint8*)m_value.raw);
        addr = ((Uint8*)m_value.raw)+2;
        if (unlikely(len > attr->maxSize))
          return QRY_CHAR_PARAMETER_TRUNCATED;
      }
      else
      {
        len  = attr->maxSize;
        addr = m_value.raw;
      }
      break;
    }
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
  m_pendingFrags(0),
  m_rootFragCount(0),
  m_rootFrags(NULL),
  m_maxBatchRows(0),
  m_applFrags(),
  m_fullFrags(),
  m_finalBatchFrags(0),
  m_num_bounds(0),
  m_previous_range_num(0),
  m_attrInfo(),
  m_keyInfo()
{
  // Allocate memory for all m_operations[] in a single chunk
  m_countOperations = queryDef.getNoOfOperations();
  size_t  size = m_countOperations * sizeof(NdbQueryOperationImpl);
  m_operations = static_cast<NdbQueryOperationImpl*> (malloc(size));
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
  // NOTE: m_operations[] was allocated as a single memory chunk with
  // placement new construction of each operation.
  // Requires explicit call to d'tor of each operation before memory is free'ed.
  if (m_operations != NULL) {
    for (int i=m_countOperations-1; i>=0; --i)
    { m_operations[i].~NdbQueryOperationImpl();
    }
    free(m_operations);
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
insert_bound(Uint32Buffer& keyInfo, const NdbRecord::Attr& keyAttr,
                                    const char* row,
                                    Uint32 bound_type)
{
  char buf[NdbRecord::Attr::SHRINK_VARCHAR_BUFFSIZE];

  bool is_null= keyAttr.is_null(row);
  Uint32 len= 0;
  const void *aValue= row+keyAttr.offset;

  if (!is_null)
  {
    bool len_ok;
    /* Support for special mysqld varchar format in keys. */
    if (keyAttr.flags & NdbRecord::IsMysqldShrinkVarchar)
    {
      len_ok= keyAttr.shrink_varchar(row, len, buf);
      aValue= buf;
    }
    else
    {
      len_ok= keyAttr.get_var_length(row, len);
    }
    if (!len_ok) {
      return 4209;
    }
  }

  AttributeHeader ah(keyAttr.index_attrId, len);
  keyInfo.append(bound_type);
  keyInfo.append(ah.m_value);
  keyInfo.append(aValue,len);

  return 0;
}


int
NdbQueryImpl::setBound(const NdbIndexScanOperation::IndexBound *bound)
{
  if (unlikely(bound==NULL))
    return QRY_REQ_ARG_IS_NULL;

  const NdbQueryOperationDefImpl& rootDef = getRoot().getQueryOperationDef();

  assert (rootDef.getType() == NdbQueryOperationDefImpl::OrderedIndexScan);
  const NdbRecord* key_record = rootDef.getKeyRecord();

  int startPos = m_keyInfo.getSize();

  if (unlikely(bound->range_no > NdbIndexScanOperation::MaxRangeNo))
  {
 // setErrorCodeAbort(4286);
    return 4286;
  }
  assert (bound->range_no == m_num_bounds);
  m_num_bounds++;

  Uint32 key_count= bound->low_key_count;
  Uint32 common_key_count= key_count;
  if (key_count < bound->high_key_count)
    key_count= bound->high_key_count;
  else
    common_key_count= bound->high_key_count;

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
        const NdbRecord::Attr& keyAttr= key_record->columns[key_record->key_indexes[j]];
        const int error=
          insert_bound(m_keyInfo, keyAttr, bound->low_key, NdbIndexScanOperation::BoundEQ);
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
        const NdbRecord::Attr& keyAttr = key_record->columns[key_record->key_indexes[j]];
        Uint32 bound_type;
        /* If key is part of lower bound */
        if (bound->low_key && j<bound->low_key_count)
        {
          /* Inclusive if defined, or matching rows can include this value */
          bound_type= bound->low_inclusive  || j+1 < bound->low_key_count ?
            NdbIndexScanOperation::BoundLE : NdbIndexScanOperation::BoundLT;
          const int error=
            insert_bound(m_keyInfo, keyAttr, bound->low_key, bound_type);
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
            insert_bound(m_keyInfo, keyAttr, bound->high_key, bound_type);
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

  size_t length = m_keyInfo.getSize()-startPos;
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

  if (m_state == EndOfData) {
    return NdbQuery::NextResult_scanComplete;
  }

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
        for (unsigned i = 0; i<getNoOfOperations(); i++) {
          m_operations[i].m_isRowNull = true;
        }
        return NdbQuery::NextResult_scanComplete;
      default:
        assert(false);
      }
    } else { 
      // There are no more cached records in NdbApi
      return NdbQuery::NextResult_bufferEmpty; 
    }
  }

  /* Make results from root operation available to the user.*/
  NdbQueryOperationImpl& root = getRoot();
  NdbResultStream* resultStream = &m_applFrags.getCurrent()->getResultStream(0);
  const Uint32 rowNo = resultStream->getReceiver().getCurrentRow();
  const char* const rootBuff = resultStream->getReceiver().get_row();
  assert(rootBuff!=NULL || 
         (root.m_firstRecAttr==NULL && root.m_ndbRecord==NULL));
  root.m_isRowNull = false;
  if (root.m_firstRecAttr != NULL) {
    root.fetchRecAttrResults(resultStream->getRootFragNo());
  }
  if (root.m_ndbRecord != NULL) {
    if (root.m_resultRef!=NULL) {
      // Set application pointer to point into internal buffer.
      *root.m_resultRef = rootBuff;
    } else { 
      // Copy result to buffer supplied by application.
      memcpy(root.m_resultBuffer, rootBuff, 
             resultStream->getReceiver().m_record.m_ndb_record
             ->m_row_size);
    }
  }
  if (m_queryDef.isScanQuery()) {
    for (Uint32 i = 0; i<root.getNoOfChildOperations(); i++) {
      /* For each child, fetch the right row.*/
      root.getChildOperation(i)
        .updateChildResult(resultStream->getRootFragNo(), 
                           resultStream->getChildTupleNo(i,rowNo));
    }
    /* In case we are doing an ordered index scan, reorder the root fragments
     * such that we get the next record from the right fragment.*/
    m_applFrags.reorder();
  } else { // Lookup query
    /* Fetch results for all non-root lookups also.*/
    for (Uint32 i = 1; i<getNoOfOperations(); i++) {
      NdbQueryOperationImpl& operation = getQueryOperation(i);
      NdbResultStream* resultStream = operation.m_resultStreams[0];

      assert(resultStream->getRowCount()<=1);
      operation.m_isRowNull = (resultStream->getRowCount()==0);

      // Check if there was a result for this operation.
      if (operation.m_isRowNull==false) {
        const char* buff = resultStream->getReceiver().get_row();

        if (operation.m_firstRecAttr != NULL) {
          operation.fetchRecAttrResults(0);
        }
        if (operation.m_ndbRecord != NULL) {
          if(operation.m_resultRef!=NULL){
            // Set application pointer to point into internal buffer.
            *operation.m_resultRef = buff;
          }else{
            // Copy result to buffer supplied by application.
            memcpy(operation.m_resultBuffer, buff, 
                   resultStream
                    ->getReceiver().m_record.m_ndb_record
                    ->m_row_size);
          }
        }
      } else {
        // This operation gave no results.
        if (operation.m_resultRef!=NULL) {
          // Set application pointer to NULL.
          *operation.m_resultRef = NULL;
        }
      }
    }
  }

  return NdbQuery::NextResult_gotRow;
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
      if(m_fullFrags.top()==NULL){
        if(getRoot().isBatchComplete()){
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

      /* FIXME: Uncomment the assert below. See 
         https://intranet.mysql.com/secure/mailarchive
         /mail.php?folder=113&mail=18566 
         for and example of a query that causes it to fail.
      */
      // Assert: No spurious wakeups w/ neither resultdata, nor EOF:
      //assert (m_fullFrags.top()!=NULL || getRoot().isBatchComplete() || m_error.code);
      /* Move full fragments from receiver thread's container to application 
       *  thread's container.*/
      while (m_fullFrags.top()!=NULL) {
        m_applFrags.add(*m_fullFrags.top());
        m_fullFrags.pop();
      }

      if (m_applFrags.getCurrent() != NULL) {
        return FetchResult_ok;
      }

      /* FIXME: Uncomment the assert below. See 
         https://intranet.mysql.com/secure/mailarchive
         /mail.php?folder=113&mail=18566 
         for and example of a query that causes it to fail.
      */
      // Only expect to end up here if another ::sendFetchMore() is required
      // assert (getRoot().isBatchComplete() || m_error.code);
    } // while(likely(m_error.code==0))

    // 'while' terminated by m_error.code
    assert (m_error.code);
    return FetchResult_otherError;

  } else { // is a Lookup query
    /* The root operation is a lookup. Lookups are guaranteed to be complete
     * before NdbTransaction::execute() returns. Therefore we do not set
     * the lock, because we know that the signal receiver thread will not
     * be accessing  m_fullFrags at this time.*/
    if(m_fullFrags.top()==NULL){
      /* Getting here means that either:
       *  - No results was returned (TCKEYREF)
       *  - or, the application called nextResult() twice for a lookup query.
       */
      m_state = EndOfData;
      postFetchRelease();
      return FetchResult_scanComplete;
    }else{
      /* Move fragment from receiver thread's container to application 
       *  thread's container.*/
      m_applFrags.add(*m_fullFrags.pop());
      assert(m_fullFrags.top()==NULL); // Only one stream for lookups.
      assert(m_applFrags.getCurrent()->getResultStream(0)
             .getReceiver().hasResults());
      return FetchResult_ok;
    }
  }
} //NdbQueryImpl::fetchMoreResults

void 
NdbQueryImpl::buildChildTupleLinks(Uint32 fragNo)
{
  assert(m_rootFrags[fragNo].isFragBatchComplete());
  for (Uint32 i = 0; i<getNoOfOperations(); i++) {
    m_operations[i].m_resultStreams[fragNo]->buildChildTupleLinks();
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

  if (m_tcState != Inactive && m_finalBatchFrags < getRootFragCount())
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
NdbQueryImpl::setErrorCodeAbort(int aErrorCode)
{
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

  m_rootFrags[0].setConfReceived();

  // Result rows counted on root operation only.
  // Initially we assume all child results to be returned.
  m_rootFrags[0].incrOutstandingResults(1+getRoot().countAllChildOperations());

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
    TransporterFacade *tp = ndb->theImpl->m_transporter_facade;

    Uint32 batchRows = m_maxBatchRows; // >0: User specified prefered value, ==0: Use default CFG values

#ifdef TEST_SCANREQ
    batchRows = 1;  // To force usage of SCAN_NEXTREQ even for small scans resultsets
#endif

    // Calculate batchsize for query as minimum batchRows for all m_operations[].
    // Ignore calculated 'batchByteSize' and 'firstBatchRows' here - Recalculated
    // when building signal after max-batchRows has been determined.
    for (Uint32 i = 0; i < m_countOperations; i++) {
      Uint32 batchByteSize, firstBatchRows;
      NdbReceiver::calculate_batch_size(tp,
                                    m_operations[i].m_ndbRecord,
                                    m_operations[i].m_firstRecAttr,
                                    0, // Key size.
                                    m_pendingFrags,
                                    batchRows,
                                    batchByteSize,
                                    firstBatchRows);
      assert (batchRows>0);
      assert (firstBatchRows==batchRows);
    }
    m_maxBatchRows = batchRows;
    /**
     * Tuples are indentified with 16 bit unsigned integers, and tupleNotFound
     * (=0xffff) is used for representing unknown tuples.
     */
    assert(batchRows < tupleNotFound);

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
    m_maxBatchRows = 1;
  }

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

  // Setup m_applFrags and m_fullFrags for receiving results
  const NdbRecord* keyRec = getRoot().getQueryOperationDef().getKeyRecord();
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
NdbQueryImpl::doSend(int nodeId, bool lastFlag)  // TODO: Use 'lastFlag'
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

  const NdbQueryOperationDefImpl& rootDef = getRoot().getQueryOperationDef();

  const NdbTableImpl* const rootTable = rootDef.getKeyRecord()
    ? rootDef.getKeyRecord()->table
    : &rootDef.getTable();

  Uint32 tTableId = rootTable->m_id;
  Uint32 tSchemaVersion = rootTable->m_version;

  if (rootDef.isScanOperation())
  {
    Uint32 scan_flags = 0;  // TODO: Specify with ScanOptions::SO_SCANFLAGS

    bool tupScan = (scan_flags & NdbScanOperation::SF_TupScan);
    bool rangeScan = false;

    bool   isPruned;
    Uint32 hashValue;
    const int error = rootDef.checkPrunable(m_keyInfo, isPruned, hashValue);
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
      getRoot().getOrdering()==NdbScanOrdering_descending ? 1 : 0;
    assert(descending==0 || (int) rootTable->m_indexType ==
           (int) NdbDictionary::Index::OrderedIndex);

    assert (m_maxBatchRows > 0);

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

    Uint32 batchRows = m_maxBatchRows;
    Uint32 batchByteSize, firstBatchRows;
    NdbReceiver::calculate_batch_size(tp,
                                      getRoot().m_ndbRecord,
                                      getRoot().m_firstRecAttr,
                                      0, // Key size.
                                      getRootFragCount(),
                                      batchRows,
                                      batchByteSize,
                                      firstBatchRows);
    assert (batchRows==m_maxBatchRows);
    ScanTabReq::setScanBatch(reqInfo, batchRows);
    scanTabReq->batch_byte_size = batchByteSize;
    scanTabReq->first_batch_size = firstBatchRows;

    ScanTabReq::setViaSPJFlag(reqInfo, 1);
    ScanTabReq::setParallelism(reqInfo, getRootFragCount());
    ScanTabReq::setRangeScanFlag(reqInfo, rangeScan);
    ScanTabReq::setDescendingFlag(reqInfo, descending);
    ScanTabReq::setTupScanFlag(reqInfo, tupScan);

    // Assume LockMode LM_ReadCommited, set related lock flags
    ScanTabReq::setLockMode(reqInfo, false);  // not exclusive
    ScanTabReq::setHoldLockFlag(reqInfo, false);
    ScanTabReq::setReadCommittedFlag(reqInfo, true);

//  m_keyInfo = (scan_flags & NdbScanOperation::SF_KeyInfo) ? 1 : 0;

    // If scan is pruned, use optional 'distributionKey' to hold hashvalue
    if (isPruned)
    {
//    printf("Build pruned SCANREQ, w/ hashValue:%d\n", hashValue);
      ScanTabReq::setDistributionKeyFlag(reqInfo, 1);
      scanTabReq->distributionKey= hashValue;
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
    LinearSectionPtr secs[3];
    Uint32 receivers[64];  // TODO: 64 is a temp hack
 
    const NdbQueryOperationImpl& queryOp = getRoot();
    for(Uint32 i = 0; i<getRootFragCount(); i++){
      receivers[i] = queryOp.getReceiver(i).getId();
    }

    secs[0].p= receivers;
    secs[0].sz= getRootFragCount();

    secs[1].p= m_attrInfo.addr();
    secs[1].sz= m_attrInfo.getSize();

    Uint32 numSections= 2;
    if (m_keyInfo.getSize() > 0)
    {
      secs[2].p= m_keyInfo.addr();
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
    tcKeyReq->apiOperationPtr = getRoot().getIdOfReceiver();
    tcKeyReq->tableId = tTableId;
    tcKeyReq->tableSchemaVersion = tSchemaVersion;
    tcKeyReq->transId1 = (Uint32) transId;
    tcKeyReq->transId2 = (Uint32) (transId >> 32);

    Uint32 attrLen = 0;
    tcKeyReq->setAttrinfoLen(attrLen, 0); // Not required for long signals.
    tcKeyReq->setAPIVersion(attrLen, NDB_VERSION);
    tcKeyReq->attrLen = attrLen;

    Uint32 reqInfo = 0;
    TcKeyReq::setOperationType(reqInfo, NdbOperation::ReadRequest);
    TcKeyReq::setViaSPJFlag(reqInfo, true);
    TcKeyReq::setKeyLength(reqInfo, 0);            // This is a long signal
    TcKeyReq::setAIInTcKeyReq(reqInfo, 0);         // Not needed
    TcKeyReq::setInterpretedFlag(reqInfo, false);  // Encoded in QueryTree

    // TODO: Set these flags less forcefully
    TcKeyReq::setStartFlag(reqInfo, true);         // TODO, must implememt
    TcKeyReq::setExecuteFlag(reqInfo, true);       // TODO, must implement
    TcKeyReq::setNoDiskFlag(reqInfo, true);
    TcKeyReq::setAbortOption(reqInfo, NdbOperation::AO_IgnoreError);

    TcKeyReq::setDirtyFlag(reqInfo, true);
    TcKeyReq::setSimpleFlag(reqInfo, true);
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
  } // if

  // Shrink memory footprint by removing structures not required after ::execute()
  m_keyInfo.releaseExtend();
  m_attrInfo.releaseExtend();

  /* Todo : Consider calling NdbOperation::postExecuteRelease()
   * Ideally it should be called outside TP mutex, so not added
   * here yet
   */

  m_state = Executing;
  return 1;
} // NdbQueryImpl::doSend()


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
  NdbQueryOperationImpl& root = getRoot();
  Uint32 receivers[64];  // TODO: 64 is a temp hack

  assert (root.m_resultStreams!=NULL);
  assert(m_pendingFrags==0);
  if(root.getOrdering() == NdbScanOrdering_unordered)
  {
    for(unsigned i = 0; i<getRootFragCount(); i++)
    {
      const Uint32 tcPtrI = root.getReceiver(i).m_tcPtrI;
      if (tcPtrI != RNIL) // Check if we have received the final batch.
      {
        receivers[sent++] = tcPtrI;
        m_pendingFrags++;

        m_rootFrags[i].reset();

        for (unsigned op=0; op<m_countOperations; op++) 
        {
          m_operations[op].m_resultStreams[i]->reset();
        }
      }
    }
  }
  else
  {
    /* For ordred scans we must have records buffered for each (non-finished)
     * root fragment at all times, in order to find the lowest remaining record.
     * When one root fragment is empty, we must block the scan ask for a new 
     * batch for that particular fragment.
     */
    NdbRootFragment* const emptyFrag 
      = m_applFrags.getEmpty();
    if(emptyFrag!=NULL)
    {
      receivers[0] = emptyFrag->getResultStream(0).getReceiver().m_tcPtrI;
      sent = 1;
      m_pendingFrags = 1;

      emptyFrag->reset();

      for (unsigned op=0; op<m_countOperations; op++) 
      {
        emptyFrag->getResultStream(op).reset();
      }
    }
  }

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

  LinearSectionPtr secs[1];
  secs[ScanNextReq::ReceiverIdsSectionNum].p = receivers;
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
  while (!getRoot().isBatchComplete() && m_error.code==0)
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
  assert(m_pendingFrags==0);

  m_error.code = 0;  // Ignore possible errorcode caused by previous fetching


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
        while(m_fullFrags.top()!=NULL)
        {
          if(m_fullFrags.top()->finalBatchReceived())
          {
            // This was the final batch for that root fragment.
            m_finalBatchFrags++;
          }
          m_fullFrags.pop();
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

NdbQueryImpl::OrderedFragSet::OrderedFragSet():
  m_capacity(0),
  m_size(0),
  m_completedFrags(0),
  m_ordering(NdbScanOrdering_void),
  m_keyRecord(NULL),
  m_resultRecord(NULL),
  m_array(NULL)
{
}

int
NdbQueryImpl::OrderedFragSet::prepare(NdbScanOrdering ordering, 
                                      int capacity,                
                                      const NdbRecord* keyRecord,
                                      const NdbRecord* resultRecord)
{
  assert(m_array==NULL);
  assert(m_capacity==0);
  assert(ordering!=NdbScanOrdering_void);
  
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
  if(m_ordering==NdbScanOrdering_unordered){
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
  if(m_ordering!=NdbScanOrdering_unordered && m_size>0)
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
  if(m_ordering==NdbScanOrdering_unordered)
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
  assert(m_ordering!=NdbScanOrdering_unordered);
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
  assert(m_ordering!=NdbScanOrdering_unordered);

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
                           == NdbScanOrdering_descending,
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
  m_parents(def.getNoOfParentOperations()),
  m_children(def.getNoOfChildOperations()),
  m_resultStreams(NULL),
  m_params(),
  m_batchBuffer(NULL),
  m_resultBuffer(NULL),
  m_resultRef(NULL),
  m_isRowNull(true),
  m_ndbRecord(NULL),
  m_read_mask(NULL),
  m_firstRecAttr(NULL),
  m_lastRecAttr(NULL),
  m_ordering(NdbScanOrdering_unordered),
  m_interpretedCode(NULL)
{ 
  // Fill in operations parent refs, and append it as child of its parents
  for (Uint32 p=0; p<def.getNoOfParentOperations(); ++p)
  { 
    const NdbQueryOperationDefImpl& parent = def.getParentOperation(p);
    const Uint32 ix = parent.getQueryOperationIx();
    assert (ix < m_queryImpl.getNoOfOperations());
    m_parents.push_back(&m_queryImpl.getQueryOperation(ix));
    m_queryImpl.getQueryOperation(ix).m_children.push_back(this);
  }
  if(def.getType()==NdbQueryOperationDefImpl::OrderedIndexScan)
  {  
    const NdbScanOrdering defOrdering = 
      static_cast<const NdbQueryIndexScanOperationDefImpl&>(def).getOrdering();
    if(defOrdering != NdbScanOrdering_void)
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
    { const size_t bufLen = m_batchByteSize*m_queryImpl.getRootFragCount();
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

  delete m_interpretedCode;
  m_interpretedCode = NULL;
} //NdbQueryOperationImpl::postFetchRelease()


Uint32
NdbQueryOperationImpl::getNoOfParentOperations() const
{
  return m_parents.size();
}

NdbQueryOperationImpl&
NdbQueryOperationImpl::getParentOperation(Uint32 i) const
{
  return *m_parents[i];
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

Uint32 NdbQueryOperationImpl::countAllChildOperations() const
{
  Uint32 children = 0;

  for (unsigned i = 0; i < getNoOfChildOperations(); i++)
    children += 1 + getChildOperation(i).countAllChildOperations();

  return children;
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
  m_ndbRecord = &m_operationDef.getTblRecord();
  m_read_mask = result_mask;
  m_resultBuffer = resBuffer;
  assert(m_batchBuffer==NULL);
  return 0;
}

int
NdbQueryOperationImpl::setResultRowRef (
                       const char* & bufRef,
                       const unsigned char* result_mask)
{
  m_resultRef = &bufRef;
  return setResultRowBuf(NULL, result_mask);
}

void
NdbQueryOperationImpl::fetchRecAttrResults(Uint32 rootFragNo)
{
  NdbRecAttr* recAttr = m_firstRecAttr;
  Uint32 posInRow = 0;
  while(recAttr != NULL){
    const char *attrData = NULL;
    Uint32 attrSize = 0;
    const int retVal1 = m_resultStreams[rootFragNo]->getReceiver()
      .getScanAttrData(attrData, attrSize, posInRow);
    assert(retVal1==0);
    assert(attrData!=NULL);
    const bool retVal2 = recAttr
      ->receive_data(reinterpret_cast<const Uint32*>(attrData), attrSize);
    assert(retVal2);
    recAttr = recAttr->next();
  }
}

void 
NdbQueryOperationImpl::updateChildResult(Uint32 rootFragNo, Uint32 rowNo)
{
  if (rowNo==tupleNotFound) {
    /* This operation gave no result for the current parent tuple.*/ 
    m_isRowNull = true;
    if(m_resultRef!=NULL){
      // Set the pointer supplied by the application to NULL.
      *m_resultRef = NULL;
    }
    /* We should not give any results for the descendants either.*/
    for(Uint32 i = 0; i<getNoOfChildOperations(); i++){
      getChildOperation(i).updateChildResult(0, tupleNotFound);
    }
  }else{
    /* Pick the proper row for a lookup that is a descentdant of the scan.
     * We iterate linearly over the results of the root scan operation, but
     * for the descendant we must use the m_childTupleIdx index to pick the
     * tuple that corresponds to the current parent tuple.*/
    m_isRowNull = false;
    NdbResultStream& resultStream = *m_resultStreams[rootFragNo];
    assert(rowNo < resultStream.getReceiver().m_result_rows);
    /* Use random rather than sequential access on receiver, since we
    * iterate over results using an indexed structure.*/
    resultStream.getReceiver().setCurrentRow(rowNo);
    const char* buff = resultStream.getReceiver().get_row();
    if (m_firstRecAttr != NULL) {
      fetchRecAttrResults(rootFragNo);
    }
    if (m_ndbRecord != NULL) {
      if(m_resultRef!=NULL){
        // Set application pointer to point into internal buffer.
        *m_resultRef = buff;
      }else{
        assert(m_resultBuffer!=NULL);
        // Copy result to buffer supplied by application.
        memcpy(m_resultBuffer, buff, 
               resultStream.getReceiver().m_record.m_ndb_record->m_row_size);
      }
    }
    /* Call recursively for the children of this operation.*/
    for(Uint32 i = 0; i<getNoOfChildOperations(); i++){
      getChildOperation(i).updateChildResult(rootFragNo, 
                                             resultStream
                                               .getChildTupleNo(i, rowNo));
    }
  }
}

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
NdbQueryOperationImpl::serializeProject(Uint32Buffer& attrInfo) const
{
  size_t startPos = attrInfo.getSize();
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
      const NdbRecord::Attr *col= &m_ndbRecord->columns[i];
      Uint32 attrId= col->attrId;

      if (m_read_mask == NULL || isSetInMask(m_read_mask, i))
      { if (attrId > maxAttrId)
          maxAttrId= attrId;

        readMask.set(attrId);
        requestedCols++;
      }
    }

    // Test for special case, get all columns:
    if (requestedCols == m_operationDef.getTblRecord().noOfColumns) {
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
    recAttr = recAttr->next();
  }

  bool withCorrelation = getRoot().getQueryDef().isScanQuery();
  if (withCorrelation) {
    Uint32 ah;
    AttributeHeader::init(&ah, AttributeHeader::READ_ANY_VALUE, 0);
    attrInfo.append(ah);
  }

  // Size of projection in words.
  size_t length = attrInfo.getSize() - startPos - 1 ;
  attrInfo.put(startPos, length);
  return 0;
} // NdbQueryOperationImpl::serializeProject


int NdbQueryOperationImpl::serializeParams(const NdbQueryParamValue paramValues[])
{
  if (unlikely(paramValues == NULL))
  {
    return QRY_NEED_PARAMETER;
  }

  const NdbQueryOperationDefImpl& def = getQueryOperationDef();
  for (Uint32 i=0; i<def.getNoOfParameters(); i++)
  {
    const NdbParamOperandImpl& paramOp = def.getParameter(i);
    const NdbQueryParamValue& paramValue = paramValues[paramOp.getParamIx()];

    /**
     *  Add parameter value to serialized data.
     *  Each value has a Uint32 length field (in bytes), followed by
     *  the actuall value. Allocation is in Uint32 units with unused bytes
     *  zero padded.
     **/
    const void* addr;
    size_t len;
    bool null;
    const int error = paramValue.getValue(paramOp,addr,len,null);
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
NdbQueryOperationImpl::prepareReceiver()
{
  const Uint32 rowSize = 
    NdbReceiver::ndbrecord_rowsize(m_ndbRecord, m_firstRecAttr,0,false);
  m_batchByteSize = rowSize * m_queryImpl.getMaxBatchRows();
//ndbout "m_batchByteSize=" << m_batchByteSize << endl;

  if (m_batchByteSize > 0) { // 0 bytes in batch if no result requested
    size_t bufLen = m_batchByteSize*m_queryImpl.getRootFragCount();
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
                          m_queryImpl.getMaxBatchRows(), 
                          0 /*key_size*/, 
                          0 /*read_range_no*/, 
                          rowSize,
                          &m_batchBuffer[m_batchByteSize*i],
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
  if (def.getType() == NdbQueryOperationDefImpl::UniqueIndexAccess)
  {
    // Reserve memory for LookupParameters, fill in contents later when
    // 'length' and 'requestInfo' has been calculated.
    size_t startPos = attrInfo.getSize();
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
    size_t length = attrInfo.getSize() - startPos;
    if (unlikely(length > 0xFFFF)) {
      return QRY_DEFINITION_TOO_LARGE; //Query definition too large.
    } else {
      QueryNodeParameters::setOpLen(param->len,
                                    def.isScanOperation()
                                      ?QueryNodeParameters::QN_SCAN_FRAG
                                      :QueryNodeParameters::QN_LOOKUP,
				    length);
    }
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
  size_t startPos = attrInfo.getSize();
  attrInfo.alloc(QN_LookupParameters::NodeSize);
  Uint32 requestInfo = 0;

  // SPJ block assume PARAMS to be supplied before ATTR_LIST
  if (m_params.getSize() > 0 &&
      def.getType() == NdbQueryOperationDefImpl::PrimaryKeyAccess)
  {
    // parameter values has been serialized as part of NdbTransaction::createQuery()
    // Only need to append it to rest of the serialized arguments
    requestInfo |= DABits::PI_KEY_PARAMS;
    attrInfo.append(m_params);    
  }

  if(m_interpretedCode!=NULL && m_interpretedCode->m_instructions_length>0)
  {
    requestInfo |= DABits::PI_ATTR_INTERPRET;
    const int error = getRoot().prepareScanFilter(attrInfo);
    if (unlikely(error != 0)) 
    {
      return error;
    }
  }

  requestInfo |= DABits::PI_ATTR_LIST;
  const int error = serializeProject(attrInfo);
  if (unlikely(error)) {
    return error;
  }

  QN_LookupParameters* param = reinterpret_cast<QN_LookupParameters*>(attrInfo.addr(startPos)); 
  if (unlikely(param==NULL))
     return Err_MemoryAlloc;

  param->requestInfo = requestInfo;
  param->resultData = getIdOfReceiver();
  size_t length = attrInfo.getSize() - startPos;
  if (unlikely(length > 0xFFFF)) {
    return QRY_DEFINITION_TOO_LARGE; //Query definition too large.
  } else {
    QueryNodeParameters::setOpLen(param->len,
                                  def.isScanOperation()
                                    ?QueryNodeParameters::QN_SCAN_FRAG
                                    :QueryNodeParameters::QN_LOOKUP,
                                  length);
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
formatAttr(const NdbRecord::Attr& attr,
           const void*& value, size_t& len,
           char* buffer, size_t buflen)
{
  const int varsize = NdbRecord::IsVar1ByteLen | NdbRecord::IsVar2ByteLen;
  if (likely(!(attr.flags & varsize)))
  {
    if (unlikely(len != attr.maxSize))
      return QRY_OPERAND_HAS_WRONG_TYPE;
    return 0;
  }

  if (attr.flags & NdbRecord::IsVar1ByteLen)
  {
    if (unlikely(len > 0xFF || len+1 > buflen))
      return QRY_CHAR_OPERAND_TRUNCATED;
    buffer[0] = (unsigned char)len;
    memcpy(buffer+1, value, len);
    len+=1;
    value = buffer;
  }
  else  // IsVar2ByteLen
  {
    assert (attr.flags & NdbRecord::IsVar2ByteLen);
    if (unlikely(len > 0xFFFF || len+2 > buflen))
      return QRY_CHAR_OPERAND_TRUNCATED;
    buffer[0] = (unsigned char)(len & 0xFF);
    buffer[1] = (unsigned char)(len >> 8);
    memcpy(buffer+2, value, len);
    len+=2;
    value = buffer;
  }

  return 0;
} // static formatAttr


static int
appendBound(Uint32Buffer& keyInfo,
            const NdbRecord::Attr& keyAttr,
            const NdbQueryOperandImpl* bound,
            NdbIndexScanOperation::BoundType type, 
            const NdbQueryParamValue actualParam[]) 
{
  size_t len = 0;
  const void* boundValue = NULL;

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
      return 4316;  // Error: 'Key attributes are not allowed to be NULL attributes'
    break;
  }
  case NdbQueryOperandImpl::Linked:    // Root operation cannot have linked operands.
  default:
    assert(false);
  }
    
  char tmp[NDB_MAX_KEY_SIZE];
  const int error = formatAttr(keyAttr, boundValue, len, tmp, sizeof(tmp));
  if (unlikely(error))
    return error;

  AttributeHeader ah(keyAttr.index_attrId, len);

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

  const NdbRecord* keyRecord = m_operationDef.getKeyRecord();
  const unsigned key_count = 
     (bounds->lowKeys >= bounds->highKeys) ? bounds->lowKeys : bounds->highKeys;

  assert (key_count <= keyRecord->key_index_length);
  for (unsigned keyNo = 0; keyNo < key_count; keyNo++)
  {
    NdbIndexScanOperation::BoundType bound_type;
    const NdbRecord::Attr& keyAttr = keyRecord->columns[keyRecord->key_indexes[keyNo]];

    /* If upper and lower limit is equal, a single BoundEQ is sufficient */
    if (bounds->low[keyNo] == bounds->high[keyNo])
    {
      /* Inclusive if defined, or matching rows can include this value */
      bound_type= NdbIndexScanOperation::BoundEQ;
      int error = appendBound(keyInfo, keyAttr, bounds->low[keyNo], bound_type, actualParam);
      if (unlikely(error))
        return error;
    }
    else
    {
      /* If key is part of lower bound */
      if (keyNo < bounds->lowKeys)
      {
        /* Inclusive if defined, or matching rows can include this value */
        bound_type= bounds->lowIncl  || keyNo+1 < bounds->lowKeys ?
            NdbIndexScanOperation::BoundLE : NdbIndexScanOperation::BoundLT;

        int error = appendBound(keyInfo, keyAttr, bounds->low[keyNo], bound_type, actualParam);
        if (unlikely(error))
          return error;
      }

      /* If key is part of upper bound */
      if (keyNo < bounds->highKeys)
      {
        /* Inclusive if defined, or matching rows can include this value */
        bound_type= bounds->highIncl  || keyNo+1 < bounds->highKeys ?
            NdbIndexScanOperation::BoundGE : NdbIndexScanOperation::BoundGT;

        int error = appendBound(keyInfo, keyAttr, bounds->high[keyNo], bound_type, actualParam);
        if (unlikely(error))
          return error;
      }
    }
  }

  size_t length = keyInfo.getSize()-startPos;
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
#ifdef TRACE_SERIALIZATION
  int startPos = keyInfo.getSize();
#endif

  const NdbRecord* keyRecord = m_operationDef.getKeyRecord();
  const int keyCount = keyRecord->key_index_length;

  for (int keyNo = 0; keyNo<keyCount; keyNo++)
  {
    size_t len = 0;
    const void* boundValue = NULL;
    const NdbRecord::Attr& keyAttr = keyRecord->columns[keyRecord->key_indexes[keyNo]];

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
        return 4316;  // Error: 'Key attributes are not allowed to be NULL attributes'
      break;
    }
    case NdbQueryOperandImpl::Linked:    // Root operation cannot have linked operands.
    default:
      assert(false);
    }

    char tmp[NDB_MAX_KEY_SIZE];

    const int error = formatAttr(keyAttr, boundValue, len, tmp, sizeof(tmp));
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
      m_queryImpl.buildChildTupleLinks(rootFragNo);
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

  // Compensate for children results not produced.
  // (TCKEYCONF assumed all child results to be materialized)
  getQuery().m_rootFrags[0]
    .incrOutstandingResults(-countAllChildOperations()-1);

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

  if (getQuery().m_rootFrags[fragNo].finalBatchReceived())
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
    m_queryImpl.buildChildTupleLinks(fragNo);
    /* nextResult() will later move it from m_fullFrags to m_applFrags
     * under mutex protection.*/
    m_queryImpl.m_fullFrags.push(rootFrag);
    // Don't awake before we have data, or query batch completed.
    ret = resultStream.getReceiver().hasResults() || isBatchComplete();
  }
  if (traceSignals) {
    ndbout << "NdbQueryOperationImpl::execSCAN_TABCONF():, returns:" << ret
           << ", tcPtrI=" << tcPtrI << " rowCount=" << rowCount 
           << " *this=" << *this << endl;
  }
  return ret;
} //NdbQueryOperationImpl::execSCAN_TABCONF

int
NdbQueryOperationImpl::setOrdering(NdbScanOrdering ordering)
{
  if(getQueryOperationDef().getType()
     !=NdbQueryOperationDefImpl::OrderedIndexScan)
  {
    getQuery().setErrorCode(QRY_WRONG_OPERATION_TYPE);
    return -1;
  }

  if(static_cast<const NdbQueryIndexScanOperationDefImpl&>
       (getQueryOperationDef())
     .getOrdering() !=NdbScanOrdering_void)
  {
    getQuery().setErrorCode(QRY_SCAN_ORDER_ALREADY_SET);
    return -1;
  }
  
  m_ordering = ordering;
  return 0;
} // NdbQueryOperationImpl::setOrdering()

int NdbQueryOperationImpl::setInterpretedCode(NdbInterpretedCode& code)
{
  const NdbTableImpl& table = getQueryOperationDef().getTable();
  // Check if operation and interpreter code use the same table
  if (unlikely(table.getTableId() != code.getTable()->getTableId()
               || table_version_major(table.getObjectVersion()) != 
               table_version_major(code.getTable()->getObjectVersion())))
  {
    getQuery().setErrorCodeAbort(Err_InterpretedCodeWrongTab);
    return -1;
  }

  if (likely(getQueryOperationDef().isScanOperation()))
  {
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
    const int retVal = m_interpretedCode->copy(code);

    if (unlikely(retVal!=0))
    {
      getQuery().setErrorCodeAbort(retVal);
      return -1;
    }
  }
  else
  {
    // Lookup operation
    assert(m_interpretedCode==NULL);
    getQuery().setErrorCodeAbort(QRY_WRONG_OPERATION_TYPE);
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


int
NdbQueryOperationImpl::prepareScanFilter(Uint32Buffer& attrInfo) const
{
  assert(getQueryOperationDef().isScanOperation());
  // There should be no subroutines in a scan filter.
  assert(m_interpretedCode->m_first_sub_instruction_pos==0);

  if ((m_interpretedCode->m_flags & NdbInterpretedCode::Finalised) == 0)
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
}


Uint32 
NdbQueryOperationImpl::getIdOfReceiver() const {
  return m_resultStreams[0]->getReceiver().getId();
}

bool 
NdbQueryOperationImpl::isBatchComplete() const {
  assert(m_resultStreams!=NULL);
  assert(this == &getRoot());
#ifndef NDEBUG
  Uint32 count = 0;
  for(Uint32 i = 0; i < m_queryImpl.getRootFragCount(); i++){
    if(!m_queryImpl.m_rootFrags[i].isFragBatchComplete()){
      count++;
    }
  }
  assert(count == getQuery().m_pendingFrags);
#endif
  return getQuery().m_pendingFrags == 0;
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
  for(unsigned int i = 0; i<op.getNoOfParentOperations(); i++){
    out << "  m_parents[" << i << "]" << &op.getParentOperation(i); 
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
template class Vector<NdbResultStream::TupleIdMap::Pair>;
