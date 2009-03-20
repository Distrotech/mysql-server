/* Copyright (C) 2003 MySQL AB

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

#include <NDBT.hpp>
#include <NDBT_Test.hpp>
#include <NdbRestarter.hpp>

#define CHECKNOTNULL(p) if ((p) == NULL) {          \
    ndbout << "Error at line " << __LINE__ << endl; \
    ERR(trans->getNdbError());                      \
    trans->close();                                 \
    return NDBT_FAILED; }

#define CHECKEQUAL(v, e) if ((e) != (v)) {            \
    ndbout << "Error at line " << __LINE__ <<         \
      " expected " << v << endl;                      \
    ERR(trans->getNdbError());                        \
    trans->close();                                   \
    return NDBT_FAILED; }


/* Setup memory as a long Varchar with 2 bytes of
 * length information
 */
Uint32 setLongVarchar(char* where, const char* what, Uint32 sz)
{
  where[0]=sz & 0xff;
  where[1]=(sz >> 8) & 0xff;
  memcpy(&where[2], what, sz);
  return (sz + 2);
}


/* Activate the given error insert in TC block
 * This is used for error insertion where a TCKEYREQ
 * is required to activate the error
 */
int activateErrorInsert(NdbTransaction* trans, 
                        const NdbRecord* record,
                        const NdbDictionary::Table* tab,
                        const char* buf,
                        NdbRestarter* restarter, 
                        Uint32 val)
{
  if (restarter->insertErrorInAllNodes(val) != 0){
    g_err << "error insert (val) failed" << endl;
    return NDBT_FAILED;
  }

  NdbOperation* insert= trans->getNdbOperation(tab);
  
  CHECKNOTNULL(insert);

  CHECKEQUAL(0, insert->insertTuple());

  CHECKEQUAL(0, insert->equal((Uint32) 0, 
                              NdbDictionary::getValuePtr
                              (record,
                               buf,
                               0)));
  CHECKEQUAL(0, insert->setValue(1,
                                 NdbDictionary::getValuePtr
                                 (record,
                                  buf,
                                  1)));

  CHECKEQUAL(0, trans->execute(NdbTransaction::NoCommit));

  CHECKEQUAL(0, trans->getNdbError().code);

  return NDBT_OK;
}
    
/* Test for correct behaviour using primary key operations
 * when an NDBD node's SegmentedSection pool is exhausted.
 */
int testSegmentedSectionPk(NDBT_Context* ctx, NDBT_Step* step){
  /*
   * Signal type       Exhausted @              How
   * -----------------------------------------------------
   * Long TCKEYREQ     Initial import           Consume + send
   * Long TCKEYREQ     Initial import, not first
   *                     TCKEYREQ in batch      Consume + send
   * Long TCKEYREQ     Initial import, not last
   *                     TCKEYREQ in batch      Consume + send
   * No testing of short TCKEYREQ variants as they cannot be
   * generated in mysql-5.1-telco-6.4+
   */

  /* We just run on one table */
  if (strcmp(ctx->getTab()->getName(), "WIDE_2COL") != 0)
    return NDBT_OK;

  const Uint32 maxRowBytes= NDB_MAX_TUPLE_SIZE_IN_WORDS * sizeof(Uint32);
  const Uint32 srcBuffBytes= NDBT_Tables::MaxVarTypeKeyBytes;
  const Uint32 maxAttrBytes= NDBT_Tables::MaxKeyMaxVarTypeAttrBytes;
  char smallKey[50];
  char srcBuff[srcBuffBytes];
  char smallRowBuf[maxRowBytes];
  char bigKeyRowBuf[maxRowBytes];
  char bigAttrRowBuf[maxRowBytes];

  /* Small key for hinting to same TC */
  Uint32 smallKeySize= setLongVarchar(&smallKey[0],
                                      "ShortKey",
                                      8);

  /* Large value source */
  memset(srcBuff, 'B', srcBuffBytes);

  const NdbRecord* record= ctx->getTab()->getDefaultRecord();

  /* Setup buffers
   * Small row buffer with small key and small data
   */ 
  setLongVarchar(NdbDictionary::getValuePtr(record,
                                            smallRowBuf,
                                            0),
                 "ShortKey",
                 8);
  NdbDictionary::setNull(record, smallRowBuf, 0, false);

  setLongVarchar(NdbDictionary::getValuePtr(record,
                                            smallRowBuf,
                                            1),
                 "ShortData",
                 9);
  NdbDictionary::setNull(record, smallRowBuf, 1, false);

  /* Big key buffer with big key and small data*/
  setLongVarchar(NdbDictionary::getValuePtr(record,
                                            bigKeyRowBuf,
                                            0),
                 &srcBuff[0],
                 srcBuffBytes);
  NdbDictionary::setNull(record, bigKeyRowBuf, 0, false);

  setLongVarchar(NdbDictionary::getValuePtr(record,
                                            bigKeyRowBuf,
                                            1),
                 "ShortData",
                 9);
  NdbDictionary::setNull(record, bigKeyRowBuf, 1, false);

  /* Big AttrInfo buffer with small key and big data */
  setLongVarchar(NdbDictionary::getValuePtr(record,
                                            bigAttrRowBuf,
                                            0),
                 "ShortKey", 
                 8);
  NdbDictionary::setNull(record, bigAttrRowBuf, 0, false);

  setLongVarchar(NdbDictionary::getValuePtr(record,
                                            bigAttrRowBuf,
                                            1),
                 &srcBuff[0],
                 maxAttrBytes);
  NdbDictionary::setNull(record, bigAttrRowBuf, 1, false);

  NdbRestarter restarter;
  Ndb* pNdb= GETNDB(step);

  /* Start a transaction on a specific node */
  NdbTransaction* trans= pNdb->startTransaction(ctx->getTab(),
                                                &smallKey[0],
                                                smallKeySize);
  CHECKNOTNULL(trans);

  /* Activate error insert 8065 in this transaction, consumes
   * all but 10 SectionSegments
   */
  CHECKEQUAL(NDBT_OK, activateErrorInsert(trans, 
                                          record, 
                                          ctx->getTab(),
                                          smallRowBuf, 
                                          &restarter, 
                                          8065));

  /* Ok, now the chosen TC's node should have only 10 
   * SegmentedSection buffers = ~ 60 words * 10 = 2400 bytes
   * Let's try an insert with a key bigger than that
   * Since it's part of the same transaction, it'll go via
   * the same TC.
   */
  const NdbOperation* bigInsert = trans->insertTuple(record, bigKeyRowBuf);

  CHECKNOTNULL(bigInsert);

  CHECKEQUAL(-1, trans->execute(NdbTransaction::NoCommit));

  /* ZGET_DATABUF_ERR expected */
  CHECKEQUAL(218, trans->getNdbError().code)

  trans->close();
  
  /* Ok, now a long TCKEYREQ to the same TC - this
   * has slightly different abort handling since no other
   * operations exist in this new transaction.
   * We also change it so that import overflow occurs 
   * on the AttrInfo section
   */
  /* Start transaction on the same node */
  CHECKNOTNULL(trans= pNdb->startTransaction(ctx->getTab(),
                                             &smallKey[0],
                                             smallKeySize));


  CHECKNOTNULL(bigInsert = trans->insertTuple(record, bigAttrRowBuf));

  CHECKEQUAL(-1,trans->execute(NdbTransaction::NoCommit));

  /* ZGET_DATABUF_ERR expected */
  CHECKEQUAL(218, trans->getNdbError().code);

  trans->close();

  /* Ok, now a long TCKEYREQ where we run out of SegmentedSections
   * on the first TCKEYREQ, but there are other TCKEYREQs following
   * in the same batch.  Check that abort handling is correct
   */
    /* Start transaction on the same node */
  CHECKNOTNULL(trans= pNdb->startTransaction(ctx->getTab(),
                                             &smallKey[0],
                                             smallKeySize));
  /* First op in batch, will cause overflow */
  CHECKNOTNULL(bigInsert = trans->insertTuple(record, bigAttrRowBuf));
  
  /* Second op in batch, what happens to it? */
  const NdbOperation* secondOp;
  CHECKNOTNULL(secondOp = trans->insertTuple(record, bigAttrRowBuf));


  CHECKEQUAL(-1,trans->execute(NdbTransaction::NoCommit));

  /* ZGET_DATABUF_ERR expected */
  CHECKEQUAL(218, trans->getNdbError().code);

  trans->close();

  /* Now try with a 'short' TCKEYREQ, generated using the old Api 
   * with a big key value
   */
  /* Start transaction on the same node */
  CHECKNOTNULL(trans= pNdb->startTransaction(ctx->getTab(),
                                             &smallKey[0],
                                             smallKeySize));
  
  NdbOperation* bigInsertOldApi;
  CHECKNOTNULL(bigInsertOldApi= trans->getNdbOperation(ctx->getTab()));

  CHECKEQUAL(0, bigInsertOldApi->insertTuple());
  CHECKEQUAL(0, bigInsertOldApi->equal((Uint32)0, 
                                       NdbDictionary::getValuePtr
                                       (record,
                                        bigKeyRowBuf,
                                        0)));
  CHECKEQUAL(0, bigInsertOldApi->setValue(1, 
                                          NdbDictionary::getValuePtr
                                          (record,
                                           bigKeyRowBuf,
                                           1)));

  CHECKEQUAL(-1, trans->execute(NdbTransaction::NoCommit));

  /* ZGET_DATABUF_ERR expected */
  CHECKEQUAL(218, trans->getNdbError().code)

  trans->close();

  /* Now try with a 'short' TCKEYREQ, generated using the old Api 
   * with a big data value
   */
  CHECKNOTNULL(trans= pNdb->startTransaction(ctx->getTab(),
                                             &smallKey[0],
                                             smallKeySize));
  
  CHECKNOTNULL(bigInsertOldApi= trans->getNdbOperation(ctx->getTab()));

  CHECKEQUAL(0, bigInsertOldApi->insertTuple());
  CHECKEQUAL(0, bigInsertOldApi->equal((Uint32)0, 
                                       NdbDictionary::getValuePtr
                                       (record,
                                        bigAttrRowBuf,
                                        0)));
  CHECKEQUAL(0, bigInsertOldApi->setValue(1, 
                                          NdbDictionary::getValuePtr
                                          (record,
                                           bigAttrRowBuf,
                                           1)));

  CHECKEQUAL(-1, trans->execute(NdbTransaction::NoCommit));

  /* ZGET_DATABUF_ERR expected */
  CHECKEQUAL(218, trans->getNdbError().code)

  trans->close();

  /* Finished with error insert, cleanup the error insertion
   * Error insert 8068 will free the hoarded segments
   */
  CHECKNOTNULL(trans= pNdb->startTransaction(ctx->getTab(),
                                             &smallKey[0],
                                             smallKeySize));

  CHECKEQUAL(NDBT_OK, activateErrorInsert(trans, 
                                          record, 
                                          ctx->getTab(),
                                          smallRowBuf, 
                                          &restarter, 
                                          8068));

  trans->execute(NdbTransaction::Rollback);
  
  CHECKEQUAL(0, trans->getNdbError().code);

  trans->close();

  return NDBT_OK;
}
  
/* Test for correct behaviour using unique key operations
 * when an NDBD node's SegmentedSection pool is exhausted.
 */
int testSegmentedSectionIx(NDBT_Context* ctx, NDBT_Step* step){
  /* 
   * Signal type       Exhausted @              How
   * -----------------------------------------------------
   * Long TCINDXREQ    Initial import           Consume + send 
   * Long TCINDXREQ    Build second TCKEYREQ    Consume + send short
   *                                             w. long base key
   */
  /* We will generate : 
   *   10 SS left : 
   *     Long IndexReq with too long Key/AttrInfo
   *    5 SS left :
   *     Long IndexReq read with short Key + Attrinfo to long 
   *       base table Key
   */
  /* We just run on one table */
  if (strcmp(ctx->getTab()->getName(), "WIDE_2COL_IX") != 0)
    return NDBT_OK;

  const char* indexName= "WIDE_2COL_IX$NDBT_IDX0";
  const Uint32 maxRowBytes= NDB_MAX_TUPLE_SIZE_IN_WORDS * sizeof(Uint32);
  const Uint32 srcBuffBytes= NDBT_Tables::MaxVarTypeKeyBytes;
  const Uint32 maxIndexKeyBytes= NDBT_Tables::MaxKeyMaxVarTypeAttrBytesIndex;
  /* We want to use 6 Segmented Sections, each of 60 32-bit words, including
   * a 2 byte length overhead
   * (We don't want to use 10 Segmented Sections as in some scenarios TUP 
   *  uses Segmented Sections when sending results, and if we use TUP on
   *  the same node, the exhaustion will occur in TUP, which is not what
   *  we're testing)
   */
  const Uint32 mediumPrimaryKeyBytes= (6* 60 * 4) - 2;
  char smallKey[50];
  char srcBuff[srcBuffBytes];
  char smallRowBuf[maxRowBytes];
  char bigKeyIxBuf[maxRowBytes];
  char bigAttrIxBuf[maxRowBytes];
  char bigKeyRowBuf[maxRowBytes];
  char resultSpace[maxRowBytes];

  /* Small key for hinting to same TC */
  Uint32 smallKeySize= setLongVarchar(&smallKey[0],
                                      "ShortKey",
                                      8);

  /* Large value source */
  memset(srcBuff, 'B', srcBuffBytes);

  Ndb* pNdb= GETNDB(step);

  const NdbRecord* baseRecord= ctx->getTab()->getDefaultRecord();
  const NdbRecord* ixRecord= pNdb->
    getDictionary()->getIndex(indexName,
                              ctx->getTab()->getName())->getDefaultRecord();

  /* Setup buffers
   * Small row buffer with short key and data in base table record format
   */ 
  setLongVarchar(NdbDictionary::getValuePtr(baseRecord,
                                            smallRowBuf,
                                            0),
                 "ShortKey",
                 8);
  NdbDictionary::setNull(baseRecord, smallRowBuf, 0, false);

  setLongVarchar(NdbDictionary::getValuePtr(baseRecord,
                                            smallRowBuf,
                                            1),
                 "ShortData",
                 9);
  NdbDictionary::setNull(baseRecord, smallRowBuf, 1, false);

  /* Big index key buffer
   * Big index key (normal row attribute) in index record format
   * Index's key is attrid 1 from the base table
   * This could get confusing !
   */
  
  setLongVarchar(NdbDictionary::getValuePtr(ixRecord,
                                            bigKeyIxBuf,
                                            1),
                 &srcBuff[0],
                 maxIndexKeyBytes);
  NdbDictionary::setNull(ixRecord, bigKeyIxBuf, 1, false);

  /* Big AttrInfo buffer
   * Small key and large attrinfo in base table record format */
  setLongVarchar(NdbDictionary::getValuePtr(baseRecord,
                                            bigAttrIxBuf,
                                            0),
                 "ShortIXKey", 
                 10);

  NdbDictionary::setNull(baseRecord, bigAttrIxBuf, 0, false);

  setLongVarchar(NdbDictionary::getValuePtr(baseRecord,
                                            bigAttrIxBuf,
                                            1),
                 &srcBuff[0],
                 maxIndexKeyBytes);
  NdbDictionary::setNull(baseRecord, bigAttrIxBuf, 1, false);

  /* Big key row buffer 
   * Medium sized key and small attrinfo (index key) in
   * base table record format
   */
  setLongVarchar(NdbDictionary::getValuePtr(baseRecord,
                                            bigKeyRowBuf,
                                            0),
                 &srcBuff[0], 
                 mediumPrimaryKeyBytes);

  NdbDictionary::setNull(baseRecord, bigKeyRowBuf, 0, false);

  setLongVarchar(NdbDictionary::getValuePtr(baseRecord,
                                            bigKeyRowBuf,
                                            1),
                 "ShortIXKey",
                 10);
  NdbDictionary::setNull(baseRecord, bigKeyRowBuf, 1, false);


  /* Start a transaction on a specific node */
  NdbTransaction* trans= pNdb->startTransaction(ctx->getTab(),
                                                &smallKey[0],
                                                smallKeySize);
  /* Insert a row in the base table with a big PK, and
   * small data (Unique IX key).  This is used later to lookup
   * a big PK and cause overflow when reading TRANSID_AI in TC.
   */
  CHECKNOTNULL(trans->insertTuple(baseRecord,
                                  bigKeyRowBuf));

  CHECKEQUAL(0, trans->execute(NdbTransaction::Commit));

  NdbRestarter restarter;
  /* Start a transaction on a specific node */
  trans= pNdb->startTransaction(ctx->getTab(),
                                &smallKey[0],
                                smallKeySize);
  CHECKNOTNULL(trans);

  /* Activate error insert 8065 in this transaction, consumes
   * all but 10 SectionSegments
   */
  CHECKEQUAL(NDBT_OK, activateErrorInsert(trans, 
                                          baseRecord, 
                                          ctx->getTab(),
                                          smallRowBuf, 
                                          &restarter, 
                                          8065));

  /* Ok, now the chosen TC's node should have only 10 
   * SegmentedSection buffers = ~ 60 words * 10 = 2400 bytes
   * Let's try an index read with an index key bigger than that
   * Since it's part of the same transaction, it'll go via
   * the same TC.
   */
  const NdbOperation* bigRead= trans->readTuple(ixRecord,
                                                bigKeyIxBuf,
                                                baseRecord,
                                                resultSpace);

  CHECKNOTNULL(bigRead);

  CHECKEQUAL(-1, trans->execute(NdbTransaction::NoCommit));

  /* ZGET_DATABUF_ERR expected */
  CHECKEQUAL(218, trans->getNdbError().code)

  trans->close();

  
  /* Ok, now a long TCINDXREQ to the same TC - this
   * has slightly different abort handling since no other
   * operations exist in this new transaction.
   */
  /* Start a transaction on a specific node */
  CHECKNOTNULL(trans= pNdb->startTransaction(ctx->getTab(),
                                             &smallKey[0],
                                             smallKeySize));

  CHECKNOTNULL(trans->readTuple(ixRecord,
                                bigKeyIxBuf,
                                baseRecord,
                                resultSpace));
  
  CHECKEQUAL(-1, trans->execute(NdbTransaction::NoCommit));

  /* ZGET_DATABUF_ERR expected */
  CHECKEQUAL(218, trans->getNdbError().code);
  
  trans->close();

  /* Now a TCINDXREQ that overflows, but is not the last in the
   * batch, what happens to the other TCINDXREQ in the batch?
   */
  CHECKNOTNULL(trans= pNdb->startTransaction(ctx->getTab(),
                                             &smallKey[0],
                                             smallKeySize));

  CHECKNOTNULL(trans->readTuple(ixRecord,
                                bigKeyIxBuf,
                                baseRecord,
                                resultSpace));
  /* Another read */
  CHECKNOTNULL(trans->readTuple(ixRecord,
                                bigKeyIxBuf,
                                baseRecord,
                                resultSpace));
  
  CHECKEQUAL(-1, trans->execute(NdbTransaction::NoCommit));

  /* ZGET_DATABUF_ERR expected */
  CHECKEQUAL(218, trans->getNdbError().code);
  
  trans->close();


  /* Next we read a tuple with a large primary key via the unique
   * index.  The index read itself should be fine, but
   * pulling in the base table PK will cause abort due to overflow
   * handling TRANSID_AI
   */
  /* Start a transaction on a specific node */
  CHECKNOTNULL(trans= pNdb->startTransaction(ctx->getTab(),
                                             &smallKey[0],
                                             smallKeySize));

  /* Activate error insert 8066 in this transaction, consumes
   * all but 5 SectionSegments
   */
  CHECKEQUAL(NDBT_OK, activateErrorInsert(trans, 
                                          baseRecord, 
                                          ctx->getTab(),
                                          smallRowBuf, 
                                          &restarter, 
                                          8066));
  CHECKEQUAL(0, trans->execute(NdbTransaction::NoCommit));
  
  CHECKNOTNULL(bigRead= trans->readTuple(ixRecord,
                                         bigAttrIxBuf,
                                         baseRecord,
                                         resultSpace));

  CHECKEQUAL(-1, trans->execute(NdbTransaction::NoCommit));

  /* ZGET_DATABUF_ERR expected */
  CHECKEQUAL(218, trans->getNdbError().code)

  trans->close();

  /* Finished with error insert, cleanup the error insertion */
  CHECKNOTNULL(trans= pNdb->startTransaction(ctx->getTab(),
                                             &smallKey[0],
                                             smallKeySize));

  CHECKEQUAL(NDBT_OK, activateErrorInsert(trans, 
                                          baseRecord, 
                                          ctx->getTab(),
                                          smallRowBuf, 
                                          &restarter, 
                                          8068));

  trans->execute(NdbTransaction::Rollback);
  
  CHECKEQUAL(0, trans->getNdbError().code);

  trans->close();

  return NDBT_OK;
}


int testSegmentedSectionScan(NDBT_Context* ctx, NDBT_Step* step){
  /* Test that TC handling of segmented section exhaustion is
   * correct
   * Since NDBAPI always send long requests, that is all that
   * we test
   */
    /* We just run on one table */
  if (strcmp(ctx->getTab()->getName(), "WIDE_2COL") != 0)
    return NDBT_OK;

  const Uint32 maxRowBytes= NDB_MAX_TUPLE_SIZE_IN_WORDS * sizeof(Uint32);
  char smallKey[50];
  char smallRowBuf[maxRowBytes];

  Uint32 smallKeySize= setLongVarchar(&smallKey[0],
                                      "ShortKey",
                                      8);

  const NdbRecord* record= ctx->getTab()->getDefaultRecord();

  /* Setup buffers
   * Small row buffer with small key and small data
   */ 
  setLongVarchar(NdbDictionary::getValuePtr(record,
                                            smallRowBuf,
                                            0),
                 "ShortKey",
                 8);
  NdbDictionary::setNull(record, smallRowBuf, 0, false);

  setLongVarchar(NdbDictionary::getValuePtr(record,
                                            smallRowBuf,
                                            1),
                 "ShortData",
                 9);
  NdbDictionary::setNull(record, smallRowBuf, 1, false);

  NdbRestarter restarter;
  Ndb* pNdb= GETNDB(step);

  /* Start a transaction on a specific node */
  NdbTransaction* trans= pNdb->startTransaction(ctx->getTab(),
                                                &smallKey[0],
                                                smallKeySize);
  CHECKNOTNULL(trans);

  /* Activate error insert 8066 in this transaction, consumes
   * all but 5 SectionSegments
   */
  CHECKEQUAL(NDBT_OK, activateErrorInsert(trans, 
                                          record, 
                                          ctx->getTab(),
                                          smallRowBuf, 
                                          &restarter, 
                                          8066));

  /* Ok, now the chosen TC's node should have only 5 
   * SegmentedSection buffers = ~ 60 words * 5 = 1200 bytes
   * A scan will always send 2 long sections (Receiver Ids
   * + AttrInfo), so let's start a scan with > 2400 bytes of
   * ATTRINFO and see what happens
   */
  NdbScanOperation* scan= trans->getNdbScanOperation(ctx->getTab());

  CHECKNOTNULL(scan);

  CHECKEQUAL(0, scan->readTuples());

  /* Create a particularly useless program */
  NdbInterpretedCode prog;

  for (Uint32 w=0; w < 2500; w++)
    CHECKEQUAL(0, prog.load_const_null(1));

  CHECKEQUAL(0, prog.interpret_exit_ok());
  CHECKEQUAL(0, prog.finalise());

  CHECKEQUAL(0, scan->setInterpretedCode(&prog));

  /* Api doesn't seem to wait for result of scan request */
  CHECKEQUAL(0, trans->execute(NdbTransaction::NoCommit));

  CHECKEQUAL(0, trans->getNdbError().code);

  CHECKEQUAL(-1, scan->nextResult());
  
  CHECKEQUAL(217, scan->getNdbError().code);

  trans->close();

  /* Finished with error insert, cleanup the error insertion */
  CHECKNOTNULL(trans= pNdb->startTransaction(ctx->getTab(),
                                             &smallKey[0],
                                             smallKeySize));

  CHECKEQUAL(NDBT_OK, activateErrorInsert(trans, 
                                          record, 
                                          ctx->getTab(),
                                          smallRowBuf, 
                                          &restarter, 
                                          8068));

  CHECKEQUAL(0, trans->execute(NdbTransaction::Rollback));
  
  CHECKEQUAL(0, trans->getNdbError().code);

  trans->close();

  return NDBT_OK;
}

int testDropSignalFragments(NDBT_Context* ctx, NDBT_Step* step){
  /* Segmented section exhaustion results in dropped signals
   * Fragmented signals split one logical signal over multiple
   * physical signals (to cope with the MAX_SIGNAL_LENGTH=32kB
   * limitation).
   * This testcase checks that when individual signals comprising
   * a fragmented signal (in this case SCANTABREQ) are dropped, the
   * system behaves correctly.
   * Correct behaviour is to behave in the same way as if the signal
   * was not fragmented, and for SCANTABREQ, to return a temporary
   * resource error.
   */
  NdbRestarter restarter;
  Ndb* pNdb= GETNDB(step);

  /* SEND > ((2 * MAX_SEND_MESSAGE_BYTESIZE) + SOME EXTRA) 
   * This way we get at least 3 fragments
   * However, as this is generally > 64kB, it's too much AttrInfo for
   * a ScanTabReq, so the 'success' case returns error 874
   */
  const Uint32 PROG_WORDS= 16500; 

  struct SubCase
  {
    Uint32 errorInsertCode;
    int expectedRc;
  };
  const Uint32 numSubCases= 5;
  const SubCase cases[numSubCases]= 
  /* Error insert   Scanrc */
    {{          0,     874},  // Normal, success which gives too much AI error
     {       8074,     217},  // Drop first fragment -> error 217
     {       8075,     217},  // Drop middle fragment(s) -> error 217
     {       8076,     217},  // Drop last fragment -> error 217
     {       8077,     217}}; // Drop all fragments -> error 217
  const Uint32 numIterations= 50;
  
  Uint32 buff[ PROG_WORDS + 10 ]; // 10 extra for final 'return' etc.

  for (Uint32 iteration=0; iteration < (numIterations * numSubCases); iteration++)
  {
    /* Start a transaction */
    NdbTransaction* trans= pNdb->startTransaction();
    CHECKNOTNULL(trans);

    SubCase subcase= cases[iteration % numSubCases];

    Uint32 errorInsertVal= subcase.errorInsertCode;
    // printf("Inserting error : %u\n", errorInsertVal);
    CHECKEQUAL(0, restarter.insertErrorInAllNodes(errorInsertVal));

    NdbScanOperation* scan= trans->getNdbScanOperation(ctx->getTab());
    
    CHECKNOTNULL(scan);
    
    CHECKEQUAL(0, scan->readTuples());
    
    /* Create a large program, to give a large SCANTABREQ */
    NdbInterpretedCode prog(ctx->getTab(), buff, PROG_WORDS + 10);
    
    for (Uint32 w=0; w < PROG_WORDS; w++)
      CHECKEQUAL(0, prog.load_const_null(1));
    
    CHECKEQUAL(0, prog.interpret_exit_ok());
    CHECKEQUAL(0, prog.finalise());
    
    CHECKEQUAL(0, scan->setInterpretedCode(&prog));
    
    /* Api doesn't seem to wait for result of scan request */
    CHECKEQUAL(0, trans->execute(NdbTransaction::NoCommit));
    
    CHECKEQUAL(0, trans->getNdbError().code);

    CHECKEQUAL(-1, scan->nextResult());
    
    int expectedResult= subcase.expectedRc;
    CHECKEQUAL(expectedResult, scan->getNdbError().code);

    scan->close();
    
    trans->close();
  }

  restarter.insertErrorInAllNodes(0);

  return NDBT_OK;
}


NDBT_TESTSUITE(testLimits);

TESTCASE("ExhaustSegmentedSectionPk",
         "Test behaviour at Segmented Section exhaustion for PK"){
  INITIALIZER(testSegmentedSectionPk);
}

TESTCASE("ExhaustSegmentedSectionIX",
         "Test behaviour at Segmented Section exhaustion for Unique index"){
  INITIALIZER(testSegmentedSectionIx);
}
TESTCASE("ExhaustSegmentedSectionScan",
         "Test behaviour at Segmented Section exhaustion for Scan"){
  INITIALIZER(testSegmentedSectionScan);
}

TESTCASE("DropSignalFragments",
         "Test behaviour of Segmented Section exhaustion with fragmented signals"){
  INITIALIZER(testDropSignalFragments);
}

NDBT_TESTSUITE_END(testLimits);

int main(int argc, const char** argv){
  ndb_init();
  NDBT_TESTSUITE_INSTANCE(testLimits);
  return testLimits.execute(argc, argv);
}
