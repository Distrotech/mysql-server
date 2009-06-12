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

/*

Retreived T
treeSpec: 000f0002 00060001 00000008 00000007 00000001 00020002 babe0003 00080001 00000003 00000007 00000001 00000001 00000002 00020001 00020000 
paramSpec: 00050001 00000001 00010000 00000001 fff00006 00050001 00000001 00020000 00000001 fff00006 
^C
 */


#include <ndb_global.h>
#include <ndb_opts.h>
#include <NDBT.hpp>
#include <NdbApi.hpp>
#include <NdbSleep.h>
#include <HugoOperations.hpp>
#include <kernel/signaldata/QueryTree.hpp>
#include <kernel/AttributeHeader.hpp>

typedef uchar* gptr;
static Uint32 storeCompactList(Uint32 * dst, const Vector<Uint32> & src);

static void
push_back(Vector<Uint32>& dst, const Uint32 * src, Uint32 len)
{
  for (Uint32 i = 0; i < len; i++)
    dst.push_back(src[i]);
}

static void
dump(const char * head, const Vector<Uint32> & src, const char * tail = 0)
{
  if (head)
    printf("%s", head);
  for (Uint32 i = 0; i<src.size(); i++)
    printf("%.8x ", src[i]);
  if (tail)
    printf("%s", tail);
}

static int _scan = 0;
static const char* _dbname = "TEST_DB";

static struct my_option my_long_options[] =
{
  NDB_STD_OPTS("spj_test"),
  { "database", 'd', "Name of database table is in",
    (uchar**) &_dbname, (uchar**) &_dbname, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { "scan", 's', "Table scan followed by key lookup",
    (uchar**) &_scan, (uchar**) &_scan, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};

class ha_ndbcluster
{
public:
  static Uint32 getTransPtr(NdbTransaction* trans){
    return trans->theTCConPtr;
  }
};

class NdbScanFilterImpl
{
public:
  static void add(NdbOperation* op, const Uint32 * src, Uint32 len) {
    op->insertATTRINFOloop(src, len);
  }
  static void add2(NdbScanOperation* op, const Uint32 * src, Uint32 len) {
    op->insertATTRINFOData_NdbRecord((const char*)src, 4*len);
  }
  static void setIsLinkedFlag(NdbOperation* op){
    op->m_isLinked = true;
  }
  static Uint32 getOpPtr(const NdbOperation* op, int recNo=0){
    return op->ptr2int(recNo);
  }
  static Uint32 getTransPtr(NdbScanOperation* op){
    return ha_ndbcluster::getTransPtr(op->theNdbCon);
  }
};


class ResultSet{
public:
  ResultSet(NdbOperation* pOp, int recNo){
    for(int i=0; i<attrCount; i++){
      mRecAttrs[i] = pOp->getValue(attrNames[i], NULL, recNo);
    }
  }

  void print() const{
    for(int i=0; i<attrCount; i++){
      ndbout << attrNames[i] << "=" << mRecAttrs[i]->u_32_value() << endl;
    }
  }
private:
  static const int attrCount = 6;
  static const char* const attrNames[attrCount];
  const NdbRecAttr * mRecAttrs[attrCount];
};


const char* const ResultSet::attrNames[ResultSet::attrCount] = {
  "a", "b", "a0", "b0", "c0", "c1" 
};
/*
class LookupQuery{
  friend class LookupOp;
public:
  explicit LookupQuery(Ndb& ndb, const NdbDictionary::Table* pTab);
private:
  const NdbDictionary::Table* pTab;
  HugoOperations hugoOps;
  NdbTransaction* pTrans;
  NdbOperation* pOp;
  Vector<Uint32> treeSpec;
  Vector<Uint32> paramSpec;
  Vector<Uint32> attrList;
};

LookupQuery::LookupQuery(Ndb& ndb, const NdbDictionary::Table * pTab, 
			 int depth):
  pTab(pTab),
  hugoOps(*pTab),
  pTrans(MyNdb.startTransaction()),
  pOp(pTrans->getNdbOperation(pTab)){
  pOp->readTuple(NdbOperation::LM_Dirty);
  pOp->equal("a", 1);
  pOp->equal("b", 1);
}
*/

class LookupOp{
public:
  explicit LookupOp(const NdbOperation* pOp,
		    bool final,
		    int nodeNo);
  void serializeOp(Vector<Uint32>& vec) const;
  void serializeParam(Vector<Uint32>& vec) const;
  int getOpLen() const {return QueryNode::getLength(qn.len);}
  int getParamLen() const {return QueryNodeParameters::getLength(p.len);}
private:
  union {
    QN_LookupNode qn;
    Uint32 queryData[25];
  };
  union {
    QN_LookupParameters p;
    Uint32 paramData[25];
  };
};

LookupOp::LookupOp(const NdbOperation* pOp, 
		   bool final, 
		   int nodeNo){
  const NdbDictionary::Table * const pTab = pOp->getTable();
  qn.tableId = pTab->getObjectId();
  qn.tableVersion = pTab->getObjectVersion();

  Uint32 optPos = 0;
  if(final){
    qn.requestInfo = 0;
  }else{
    qn.requestInfo = DABits::NI_LINKED_ATTR;
  }

  if(nodeNo>0){
    qn.requestInfo |= DABits::NI_HAS_PARENT | DABits::NI_KEY_LINKED;
    Vector<Uint32> parent;
    parent.push_back(nodeNo-1); // Number...
    optPos = storeCompactList(qn.optional, parent);
  }

  Vector<Uint32> attrList;
  // Projection for link from child...
  attrList.push_back(2); // a0
  attrList.push_back(3); // b0

  if(nodeNo>0){
    qn.optional[optPos++] = attrList.size(); // length of KeyPattern
    for (Uint32 i = 1; i <= attrList.size(); i++){
      qn.optional[optPos++] = QueryPattern::col(attrList.size()-i);// KeyPattern
    }
  }

  if(!final){
    optPos += storeCompactList(qn.optional+optPos, attrList);
  }
    
  // Set length of qn1.
  QueryNode::setOpLen(qn.len, QueryNode::QN_LOOKUP,
		      QN_LookupNode::NodeSize + optPos);
  
  // Has projection for child op.
  p.requestInfo = DABits::PI_ATTR_LIST;
  // Define a receiver object for this op.
  p.resultData = NdbScanFilterImpl::getOpPtr(pOp, nodeNo);
  //p1.resultData = 0x10000; // NdbScanFilterImpl::getOpPtr(pOp);
  // Define a result projection to include *all* attributes.
  p.optional[0] = 1; // Length of user projecttion
  AttributeHeader::init(p.optional + 1, AttributeHeader::READ_ALL,
			pTab->getNoOfColumns());
  // Set length of oprional part (projection)
  QueryNodeParameters::setOpLen(p.len, QueryNodeParameters::QN_LOOKUP, p.NodeSize + 2);
}

void LookupOp::serializeOp(Vector<Uint32>& vec) const{
  push_back(vec, queryData, QueryNode::getLength(qn.len));
}

void LookupOp::serializeParam(Vector<Uint32>& vec) const{
  push_back(vec, paramData, QueryNodeParameters::getLength(p.len));
}






/**
 * SQL:
 drop table if exists T;
 create table T (a int, b int, a0 int not null, b0 int not null,
 c0 int unsigned not null, c1 int unsigned not null, primary key(a,b))
 engine = ndb;

 insert into T values (1,1,2,3,1,1);
 insert into T values (3,2,4,5,1,1);
 insert into T values (4,5,1,1,1,1);

*/

int spjTest(int argc, char** argv){
  NDB_INIT(argv[0]);

  const char *load_default_groups[]= { "mysql_cluster",0 };
  load_defaults("my",load_default_groups,&argc,&argv);
  int ho_error;
#ifndef DBUG_OFF
  opt_debug= "d:t:O,/tmp/ndb_desc.trace";
#endif
  if ((ho_error=handle_options(&argc, &argv, my_long_options,
			       ndb_std_get_one_option)))
    return NDBT_ProgramExit(NDBT_WRONGARGS);

  Ndb_cluster_connection con(opt_connect_str);
  if(con.connect(12, 5, 1) != 0)
  {
    ndbout << "Unable to connect to management server." << endl;
    return NDBT_ProgramExit(NDBT_FAILED);
  }

  int res = con.wait_until_ready(30,30);
  if (res != 0)
  {
    ndbout << "Cluster nodes not ready in 30 seconds." << endl;
    return NDBT_ProgramExit(NDBT_FAILED);
  }

  Ndb MyNdb(&con, _dbname);
  if(MyNdb.init() != 0){
    ERR(MyNdb.getNdbError());
    return NDBT_ProgramExit(NDBT_FAILED);
  }

  const NdbDictionary::Dictionary * dict= MyNdb.getDictionary();
  const NdbDictionary::Table * pTab = dict->getTable(argv[0]);
  if (pTab == 0)
  {
    ndbout_c("Failed to retreive table: \"%s\"", argv[0]);
  }
  else
  {
    ndbout_c("Retreived %s", argv[0]);
  }

  if (_scan == 0)
  {
    /**
       SELECT t1.*, t2.*
       FROM T t1 LEFT OUTER JOIN T t2 ON t2.a = t1.b0 AND t2.b = t1.a0
       WHERE t1.a = 1 and t1.b = 1;

       sh> test_spj T
    */

    HugoOperations hugoOps(* pTab);
    NdbTransaction* pTrans = MyNdb.startTransaction();
    NdbOperation* pOp = pTrans->getNdbOperation(pTab);
    pOp->readTuple(NdbOperation::LM_Dirty);
    pOp->equal("a", 11);
    pOp->equal("b", 3);

    Vector<Uint32> treeSpec;
    Vector<Uint32> paramSpec;

    union {
      QueryTree qt;
      Uint32 _data3[1];
    };
    /*LookupOp look1(pOp, false, 0);
      LookupOp look2(pOp, true, 1);*/

    const int nNodes = 10;
    LookupOp* node[nNodes];
    int totLen = 0;
    for(int i=0; i<nNodes; i++){
      node[i] = new LookupOp(pOp, i==nNodes-1,i);
      totLen += node[i]->getOpLen();
    }

    QueryTree::setCntLen(qt.cnt_len, nNodes,
                         1 +
                         totLen);

    push_back(treeSpec, _data3, 1);
    
    for(int i=0; i<nNodes; i++){
      node[i]->serializeOp(treeSpec);
    }

    dump("treeSpec: ", treeSpec, "\n");

    for(int i=0; i<nNodes; i++){
      node[i]->serializeParam(paramSpec);
    }

    dump("paramSpec: ", paramSpec, "\n");

    NdbScanFilterImpl::add(pOp, treeSpec.getBase(), treeSpec.size());
    NdbScanFilterImpl::add(pOp, paramSpec.getBase(), paramSpec.size());
    NdbScanFilterImpl::setIsLinkedFlag(pOp);

    const ResultSet* rSet[nNodes];
    for(int i=0; i<nNodes; i++){
      rSet[i] = new ResultSet(pOp, i);
    }
    pTrans->execute(NoCommit);
     
    /*if (pTrans->getNdbError().classification == NdbError::NoDataFound){
      ndbout << "No data found" << endl;
    }else{
      ndbout << "myRecAttrs[2]->u_32_value()=" 
		<< myRecAttrs[2]->u_32_value() << endl;
		}*/
    for(int i=0; i<nNodes; i++){
      rSet[i]->print();
    }

  }
  else if (_scan != 0)
  {
    /**
       SELECT t1.*, t2.*
       FROM T t1 LEFT OUTER JOIN T t2 ON t2.a = t1.b0 AND t2.b = t1.a0;

       sh> test_spj -s T
    */
    NdbTransaction* pTrans = MyNdb.startTransaction();
    NdbScanOperation* pOp = pTrans->scanTable(pTab->getDefaultRecord(),
                                              NdbOperation::LM_Dirty);
    Vector<Uint32> treeSpec;
    Vector<Uint32> paramSpec;
    Vector<Uint32> attrList;

    union {
      QN_ScanFragNode qn1;
      Uint32 _data1[25];
    };
    qn1.requestInfo = DABits::NI_LINKED_ATTR;
    qn1.tableId = pTab->getObjectId();
    qn1.tableVersion = pTab->getObjectVersion();

    // Linked ATTR
#if 0
    for (int i = 0; i<pTab->getNoOfColumns(); i++)
    {
      if (pTab->getColumn(i)->getPrimaryKey())
      {
        attrList.push_back(i);
      }
    }
#else
    attrList.push_back(2); // a0
    attrList.push_back(3); // b0
#endif

    Uint32 len0 = storeCompactList(qn1.optional, attrList);
    QueryNode::setOpLen(qn1.len, QueryNode::QN_SCAN_FRAG,
                        QN_LookupNode::NodeSize + len0);

    union {
      QN_LookupParameters p1;
      Uint32 _data4[25];
    };
    p1.requestInfo = DABits::PI_ATTR_LIST;
    p1.resultData = 0x10000; //NdbScanFilterImpl::getTransPtr(pOp);
    p1.optional[0] = 2; // Length of user projecttion
    AttributeHeader::init(p1.optional + 1, AttributeHeader::READ_ALL,
                          pTab->getNoOfColumns());
    /**
     * correlation value
     */
    AttributeHeader::init(p1.optional + 2, AttributeHeader::READ_ANY_VALUE,
                          0);
    QueryNodeParams::setOpLen(p1.len, QueryNodeParams::QN_SCAN_FRAG, p1.NodeSize + 3);


    union {
      QN_LookupNode qn2;
      Uint32 _data2[25];
    };
    qn2.requestInfo = DABits::NI_HAS_PARENT | DABits::NI_KEY_LINKED;
    qn2.tableId = pTab->getObjectId();
    qn2.tableVersion = pTab->getObjectVersion();
    Vector<Uint32> parent;
    parent.push_back(0); // Number...
    Uint32 len1 = storeCompactList(qn2.optional, parent);
    qn2.optional[len1] = attrList.size(); // length of KeyPattern
    for (Uint32 i = 1; i <= attrList.size(); i++)
      qn2.optional[len1+i] = QueryPattern::col(attrList.size()-i);// KeyPattern

    QueryNode::setOpLen(qn2.len, QueryNode::QN_LOOKUP,
                        QN_LookupNode::NodeSize + len1 + 1 + attrList.size());

    union {
      QN_LookupParameters p2;
      Uint32 _data5[25];
    };
    p2.requestInfo = DABits::PI_ATTR_LIST;
    p2.resultData = 0x20000; //NdbScanFilterImpl::getTransPtr(pOp);
    p2.optional[0] = 2; // Length of user projection
    AttributeHeader::init(p2.optional+1, AttributeHeader::READ_ALL,
                          pTab->getNoOfColumns());
    /**
     * correlation value
     */
    AttributeHeader::init(p2.optional+2, AttributeHeader::READ_ANY_VALUE, 0);
    QueryNodeParams::setOpLen(p2.len, QueryNodeParams::QN_LOOKUP, p2.NodeSize + 3);


    union {
      QueryTree qt;
      Uint32 _data3[1];
    };
    QueryTree::setCntLen(qt.cnt_len, 2,
                         1 +
                         QueryNode::getLength(qn1.len) +
                         QueryNode::getLength(qn2.len));

    push_back(treeSpec, _data3, 1);
    push_back(treeSpec, _data1, QueryNode::getLength(qn1.len));
    push_back(treeSpec, _data2, QueryNode::getLength(qn2.len));

    dump("treeSpec: ", treeSpec, "\n");

    push_back(paramSpec, _data4, QueryNodeParams::getLength(p1.len));
    push_back(paramSpec, _data5, QueryNodeParams::getLength(p2.len));

    dump("paramSpec: ", paramSpec, "\n");

    NdbScanFilterImpl::add2(pOp, treeSpec.getBase(), treeSpec.size());
    NdbScanFilterImpl::add2(pOp, paramSpec.getBase(), paramSpec.size());
    NdbScanFilterImpl::setIsLinkedFlag(pOp);

    pTrans->execute(NoCommit);
    while (true) NdbSleep_SecSleep(1);
    while (pOp->nextResult() == 0);
  }
  return NDBT_ProgramExit(NDBT_OK);
}


int main(int argc, char** argv){
  return spjTest(argc, argv);
}
/**
 * Store list of 16-bit integers put into 32-bit integers
 * adding a count as first 16-bit
 */
static
Uint32
storeCompactList(Uint32 * dst, const Vector<Uint32> & src)
{
  Uint32 cnt = src.size();
  if (cnt)
  {
    dst[0] = cnt | (src[0] << 16);
    for (Uint32 i = 1; i+1 < cnt; i += 2)
    {
      dst[1 + (i - 1)/2] = src[i] | (src[i+1] << 16);
    }
    Uint32 len = 1 + (cnt - 1) / 2;
    if ((cnt & 1) == 0)
    {
      dst[len++] = src[cnt-1] | (0xBABE << 16);
    }

    return len;
  }

  return 0;
}

template class Vector<Uint32>;
