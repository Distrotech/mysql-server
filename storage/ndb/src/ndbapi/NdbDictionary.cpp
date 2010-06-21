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

#include <NdbDictionary.hpp>
#include "NdbDictionaryImpl.hpp"
#include <NdbOut.hpp>
#include <signaldata/CreateHashMap.hpp>
#include <NdbBlob.hpp>
C_MODE_START
#include <decimal.h>
C_MODE_END

/* NdbRecord static helper methods */

NdbDictionary::RecordType
NdbDictionary::getRecordType(const NdbRecord* record)
{
  return NdbDictionaryImpl::getRecordType(record);
}

const char*
NdbDictionary::getRecordTableName(const NdbRecord* record)
{
  return NdbDictionaryImpl::getRecordTableName(record);
}

const char*
NdbDictionary::getRecordIndexName(const NdbRecord* record)
{
  return NdbDictionaryImpl::getRecordIndexName(record);
}

bool
NdbDictionary::getFirstAttrId(const NdbRecord* record,
                              Uint32& firstAttrId)
{
  return NdbDictionaryImpl::getNextAttrIdFrom(record,
                                              0,
                                              firstAttrId);
}

bool
NdbDictionary::getNextAttrId(const NdbRecord* record,
                             Uint32& attrId)
{
  return NdbDictionaryImpl::getNextAttrIdFrom(record, 
                                              attrId+1,
                                              attrId);
}

bool
NdbDictionary::getOffset(const NdbRecord* record,
                         Uint32 attrId,
                         Uint32& offset)
{
  return NdbDictionaryImpl::getOffset(record, attrId, offset);
}

bool
NdbDictionary::getNullBitOffset(const NdbRecord* record,
                                Uint32 attrId,
                                Uint32& nullbit_byte_offset,
                                Uint32& nullbit_bit_in_byte)
{
  return NdbDictionaryImpl::getNullBitOffset(record,
                                             attrId,
                                             nullbit_byte_offset,
                                             nullbit_bit_in_byte);
}


const char*
NdbDictionary::getValuePtr(const NdbRecord* record,
            const char* row,
            Uint32 attrId)
{
  return NdbDictionaryImpl::getValuePtr(record, row, attrId);
}

char*
NdbDictionary::getValuePtr(const NdbRecord* record,
            char* row,
            Uint32 attrId)
{
  return NdbDictionaryImpl::getValuePtr(record, row, attrId);
}

bool
NdbDictionary::isNull(const NdbRecord* record,
       const char* row,
       Uint32 attrId)
{
  return NdbDictionaryImpl::isNull(record, row, attrId);
}

int
NdbDictionary::setNull(const NdbRecord* record,
        char* row,
        Uint32 attrId,
        bool value)
{
  return NdbDictionaryImpl::setNull(record, row, attrId, value);
}

Uint32
NdbDictionary::getRecordRowLength(const NdbRecord* record)
{
  return NdbDictionaryImpl::getRecordRowLength(record);
}

const unsigned char* 
NdbDictionary::getEmptyBitmask()
{
  return (const unsigned char*) NdbDictionaryImpl::m_emptyMask;
}

/* --- */

NdbDictionary::ObjectId::ObjectId()
  : m_impl(* new NdbDictObjectImpl(NdbDictionary::Object::TypeUndefined))
{
}

NdbDictionary::ObjectId::~ObjectId()
{
  NdbDictObjectImpl * tmp = &m_impl;  
  delete tmp;
}

NdbDictionary::Object::Status
NdbDictionary::ObjectId::getObjectStatus() const {
  return m_impl.m_status;
}

int 
NdbDictionary::ObjectId::getObjectVersion() const {
  return m_impl.m_version;
}

int 
NdbDictionary::ObjectId::getObjectId() const {
  return m_impl.m_id;
}

/*****************************************************************
 * Column facade
 */
NdbDictionary::Column::Column(const char * name) 
  : m_impl(* new NdbColumnImpl(* this))
{
  setName(name);
}

NdbDictionary::Column::Column(const NdbDictionary::Column & org)
  : m_impl(* new NdbColumnImpl(* this))
{
  m_impl = org.m_impl;
}

NdbDictionary::Column::Column(NdbColumnImpl& impl)
  : m_impl(impl)
{
}

NdbDictionary::Column::~Column(){
  NdbColumnImpl * tmp = &m_impl;  
  if(this != tmp){
    delete tmp;
  }
}

NdbDictionary::Column&
NdbDictionary::Column::operator=(const NdbDictionary::Column& column)
{
  m_impl = column.m_impl;
  
  return *this;
}

int
NdbDictionary::Column::setName(const char * name){
  return !m_impl.m_name.assign(name);
}

const char* 
NdbDictionary::Column::getName() const {
  return m_impl.m_name.c_str();
}

void
NdbDictionary::Column::setType(Type t){
  m_impl.init(t);
}

NdbDictionary::Column::Type 
NdbDictionary::Column::getType() const {
  return m_impl.m_type;
}

void 
NdbDictionary::Column::setPrecision(int val){
  m_impl.m_precision = val;
}

int 
NdbDictionary::Column::getPrecision() const {
  return m_impl.m_precision;
}

void 
NdbDictionary::Column::setScale(int val){
  m_impl.m_scale = val;
}

int 
NdbDictionary::Column::getScale() const{
  return m_impl.m_scale;
}

void
NdbDictionary::Column::setLength(int length){
  m_impl.m_length = length;
}

int 
NdbDictionary::Column::getLength() const{
  return m_impl.m_length;
}

void
NdbDictionary::Column::setInlineSize(int size)
{
  m_impl.m_precision = size;
}

void
NdbDictionary::Column::setCharset(CHARSET_INFO* cs)
{
  m_impl.m_cs = cs;
}

CHARSET_INFO*
NdbDictionary::Column::getCharset() const
{
  return m_impl.m_cs;
}

int
NdbDictionary::Column::getCharsetNumber() const
{
  return m_impl.m_cs->number;
}

int
NdbDictionary::Column::getInlineSize() const
{
  return m_impl.m_precision;
}

void
NdbDictionary::Column::setPartSize(int size)
{
  m_impl.m_scale = size;
}

int
NdbDictionary::Column::getPartSize() const
{
  return m_impl.m_scale;
}

void
NdbDictionary::Column::setStripeSize(int size)
{
  m_impl.m_length = size;
}

int
NdbDictionary::Column::getStripeSize() const
{
  return m_impl.m_length;
}

int 
NdbDictionary::Column::getSize() const{
  return m_impl.m_attrSize;
}

void 
NdbDictionary::Column::setNullable(bool val){
  m_impl.m_nullable = val;
}

bool 
NdbDictionary::Column::getNullable() const {
  return m_impl.m_nullable;
}

void 
NdbDictionary::Column::setPrimaryKey(bool val){
  m_impl.m_pk = val;
}

bool 
NdbDictionary::Column::getPrimaryKey() const {
  return m_impl.m_pk;
}

void 
NdbDictionary::Column::setPartitionKey(bool val){
  m_impl.m_distributionKey = val;
}

bool 
NdbDictionary::Column::getPartitionKey() const{
  return m_impl.m_distributionKey;
}

const NdbDictionary::Table * 
NdbDictionary::Column::getBlobTable() const {
  NdbTableImpl * t = m_impl.m_blobTable;
  if (t)
    return t->m_facade;
  return 0;
}

void 
NdbDictionary::Column::setAutoIncrement(bool val){
  m_impl.m_autoIncrement = val;
}

bool 
NdbDictionary::Column::getAutoIncrement() const {
  return m_impl.m_autoIncrement;
}

void
NdbDictionary::Column::setAutoIncrementInitialValue(Uint64 val){
  m_impl.m_autoIncrementInitialValue = val;
}

/*
* setDefaultValue() with only one const char * parameter is reserved
* for backward compatible api consideration, but is broken.
*/
int
NdbDictionary::Column::setDefaultValue(const char* defaultValue)
{
  return -1;
}

/*
  The significant length of a column can't easily be calculated before
  the column type is fully defined, so the length of the default value 
  is passed in as a parameter explicitly.
*/
int
NdbDictionary::Column::setDefaultValue(const void* defaultValue, unsigned int n)
{
  if (defaultValue == NULL)
    return m_impl.m_defaultValue.assign(NULL, 0);

  return m_impl.m_defaultValue.assign(defaultValue, n);
}

const void*
NdbDictionary::Column::getDefaultValue(unsigned int* len) const
{
  if (len)
   *len = m_impl.m_defaultValue.length();

  return m_impl.m_defaultValue.get_data();
}

int
NdbDictionary::Column::getColumnNo() const {
  return m_impl.m_column_no;
}

int
NdbDictionary::Column::getAttrId() const {
  return m_impl.m_attrId;
}

bool
NdbDictionary::Column::equal(const NdbDictionary::Column & col) const {
  return m_impl.equal(col.m_impl);
}

int
NdbDictionary::Column::getSizeInBytes() const 
{
  return m_impl.m_attrSize * m_impl.m_arraySize;
}

void
NdbDictionary::Column::setArrayType(ArrayType type)
{
  m_impl.m_arrayType = type;
}

NdbDictionary::Column::ArrayType
NdbDictionary::Column::getArrayType() const
{
  return (ArrayType)m_impl.m_arrayType;
}

void
NdbDictionary::Column::setStorageType(StorageType type)
{
  m_impl.m_storageType = type;
}

NdbDictionary::Column::StorageType
NdbDictionary::Column::getStorageType() const
{
  return (StorageType)m_impl.m_storageType;
}

int
NdbDictionary::Column::getBlobVersion() const
{
  return m_impl.getBlobVersion();
}

void
NdbDictionary::Column::setBlobVersion(int blobVersion)
{
  m_impl.setBlobVersion(blobVersion);
}

void 
NdbDictionary::Column::setDynamic(bool val){
  m_impl.m_dynamic = val;
}

bool 
NdbDictionary::Column::getDynamic() const {
  return m_impl.m_dynamic;
}

bool
NdbDictionary::Column::getIndexSourced() const {
  return m_impl.m_indexSourced;
}

/*****************************************************************
 * Table facade
 */
NdbDictionary::Table::Table(const char * name)
  : m_impl(* new NdbTableImpl(* this)) 
{
  setName(name);
}

NdbDictionary::Table::Table(const NdbDictionary::Table & org)
  : Object(org), m_impl(* new NdbTableImpl(* this))
{
  m_impl.assign(org.m_impl);
}

NdbDictionary::Table::Table(NdbTableImpl & impl)
  : m_impl(impl)
{
}

NdbDictionary::Table::~Table(){
  NdbTableImpl * tmp = &m_impl;  
  if(this != tmp){
    delete tmp;
  }
}

NdbDictionary::Table&
NdbDictionary::Table::operator=(const NdbDictionary::Table& table)
{
  m_impl.assign(table.m_impl);
  
  m_impl.m_facade = this;
  return *this;
}

int
NdbDictionary::Table::setName(const char * name){
  return m_impl.setName(name);
}

const char * 
NdbDictionary::Table::getName() const {
  return m_impl.getName();
}

const char *
NdbDictionary::Table::getMysqlName() const {
  return m_impl.getMysqlName();
}

int
NdbDictionary::Table::getTableId() const {
  return m_impl.m_id;
}

int
NdbDictionary::Table::addColumn(const Column & c){
  NdbColumnImpl* col = new NdbColumnImpl;
  if (col ==  NULL)
  {
    errno = ENOMEM;
    return -1;
  }
  (* col) = NdbColumnImpl::getImpl(c);
  if (m_impl.m_columns.push_back(col))
  {
    return -1;
  }
  if (m_impl.buildColumnHash())
  {
    return -1;
  }
  return 0;
}

const NdbDictionary::Column*
NdbDictionary::Table::getColumn(const char * name) const {
  return m_impl.getColumn(name);
}

const NdbDictionary::Column* 
NdbDictionary::Table::getColumn(const int attrId) const {
  return m_impl.getColumn(attrId);
}

NdbDictionary::Column*
NdbDictionary::Table::getColumn(const char * name) 
{
  return m_impl.getColumn(name);
}

NdbDictionary::Column* 
NdbDictionary::Table::getColumn(const int attrId)
{
  return m_impl.getColumn(attrId);
}

void
NdbDictionary::Table::setLogging(bool val){
  m_impl.m_logging = val;
}

bool 
NdbDictionary::Table::getLogging() const {
  return m_impl.m_logging;
}

void
NdbDictionary::Table::setFragmentType(FragmentType ft){
  m_impl.m_fragmentType = ft;
}

NdbDictionary::Object::FragmentType 
NdbDictionary::Table::getFragmentType() const {
  return m_impl.m_fragmentType;
}

void 
NdbDictionary::Table::setKValue(int kValue){
  m_impl.m_kvalue = kValue;
}

int
NdbDictionary::Table::getKValue() const {
  return m_impl.m_kvalue;
}

void 
NdbDictionary::Table::setMinLoadFactor(int lf){
  m_impl.m_minLoadFactor = lf;
}

int 
NdbDictionary::Table::getMinLoadFactor() const {
  return m_impl.m_minLoadFactor;
}

void 
NdbDictionary::Table::setMaxLoadFactor(int lf){
  m_impl.m_maxLoadFactor = lf;  
}

int 
NdbDictionary::Table::getMaxLoadFactor() const {
  return m_impl.m_maxLoadFactor;
}

int
NdbDictionary::Table::getNoOfColumns() const {
  return m_impl.m_columns.size();
}

int
NdbDictionary::Table::getNoOfAutoIncrementColumns() const {
  return m_impl.m_noOfAutoIncColumns;
}

int
NdbDictionary::Table::getNoOfPrimaryKeys() const {
  return m_impl.m_noOfKeys;
}

void
NdbDictionary::Table::setMaxRows(Uint64 maxRows)
{
  m_impl.m_max_rows = maxRows;
}

Uint64
NdbDictionary::Table::getMaxRows() const
{
  return m_impl.m_max_rows;
}

void
NdbDictionary::Table::setMinRows(Uint64 minRows)
{
  m_impl.m_min_rows = minRows;
}

Uint64
NdbDictionary::Table::getMinRows() const
{
  return m_impl.m_min_rows;
}

void
NdbDictionary::Table::setDefaultNoPartitionsFlag(Uint32 flag)
{
  m_impl.m_default_no_part_flag = flag;
}

Uint32
NdbDictionary::Table::getDefaultNoPartitionsFlag() const
{
  return m_impl.m_default_no_part_flag;
}

const char*
NdbDictionary::Table::getPrimaryKey(int no) const {
  int count = 0;
  for (unsigned i = 0; i < m_impl.m_columns.size(); i++) {
    if (m_impl.m_columns[i]->m_pk) {
      if (count++ == no)
        return m_impl.m_columns[i]->m_name.c_str();
    }
  }
  return 0;
}

const void* 
NdbDictionary::Table::getFrmData() const {
  return m_impl.getFrmData();
}

Uint32
NdbDictionary::Table::getFrmLength() const {
  return m_impl.getFrmLength();
}

enum NdbDictionary::Table::SingleUserMode
NdbDictionary::Table::getSingleUserMode() const
{
  return (enum SingleUserMode)m_impl.m_single_user_mode;
}

void
NdbDictionary::Table::setSingleUserMode(enum NdbDictionary::Table::SingleUserMode mode)
{
  m_impl.m_single_user_mode = (Uint8)mode;
}

#if 0
int
NdbDictionary::Table::setTablespaceNames(const void *data, Uint32 len)
{
  return m_impl.setTablespaceNames(data, len);
}

const void*
NdbDictionary::Table::getTablespaceNames()
{
  return m_impl.getTablespaceNames();
}

Uint32
NdbDictionary::Table::getTablespaceNamesLen() const
{
  return m_impl.getTablespaceNamesLen();
}
#endif

void
NdbDictionary::Table::setLinearFlag(Uint32 flag)
{
  m_impl.m_linear_flag = flag;
}

bool
NdbDictionary::Table::getLinearFlag() const
{
  return m_impl.m_linear_flag;
}

void
NdbDictionary::Table::setFragmentCount(Uint32 count)
{
  m_impl.setFragmentCount(count);
}

Uint32
NdbDictionary::Table::getFragmentCount() const
{
  return m_impl.getFragmentCount();
}

int
NdbDictionary::Table::setFrm(const void* data, Uint32 len){
  return m_impl.setFrm(data, len);
}

const Uint32*
NdbDictionary::Table::getFragmentData() const {
  return m_impl.getFragmentData();
}

Uint32
NdbDictionary::Table::getFragmentDataLen() const {
  return m_impl.getFragmentDataLen();
}

int
NdbDictionary::Table::setFragmentData(const Uint32* data, Uint32 cnt)
{
  return m_impl.setFragmentData(data, cnt);
}

const Int32*
NdbDictionary::Table::getRangeListData() const {
  return m_impl.getRangeListData();
}

Uint32
NdbDictionary::Table::getRangeListDataLen() const {
  return m_impl.getRangeListDataLen();
}

int
NdbDictionary::Table::setRangeListData(const Int32* data, Uint32 len)
{
  return m_impl.setRangeListData(data, len);
}

Uint32
NdbDictionary::Table::getFragmentNodes(Uint32 fragmentId, 
                                       Uint32* nodeIdArrayPtr,
                                       Uint32 arraySize) const
{
  return m_impl.getFragmentNodes(fragmentId, nodeIdArrayPtr, arraySize);
}

NdbDictionary::Object::Status
NdbDictionary::Table::getObjectStatus() const {
  return m_impl.m_status;
}

void
NdbDictionary::Table::setStatusInvalid() const {
  m_impl.m_status = NdbDictionary::Object::Invalid;
}

int 
NdbDictionary::Table::getObjectVersion() const {
  return m_impl.m_version;
}

int 
NdbDictionary::Table::getObjectId() const {
  return m_impl.m_id;
}

bool
NdbDictionary::Table::equal(const NdbDictionary::Table & col) const {
  return m_impl.equal(col.m_impl);
}

int
NdbDictionary::Table::getRowSizeInBytes() const {  
  int sz = 0;
  for(int i = 0; i<getNoOfColumns(); i++){
    const NdbDictionary::Column * c = getColumn(i);
    sz += (c->getSizeInBytes()+ 3) / 4;
  }
  return sz * 4;
}

int
NdbDictionary::Table::getReplicaCount() const {  
  return m_impl.m_replicaCount;
}

bool
NdbDictionary::Table::getTemporary() const {
  return m_impl.m_temporary;
}

void
NdbDictionary::Table::setTemporary(bool val) {
  m_impl.m_temporary = val;
}

int
NdbDictionary::Table::createTableInDb(Ndb* pNdb, bool equalOk) const {  
  const NdbDictionary::Table * pTab = 
    pNdb->getDictionary()->getTable(getName());
  if(pTab != 0 && equal(* pTab))
    return 0;
  if(pTab != 0 && !equal(* pTab))
    return -1;
  return pNdb->getDictionary()->createTable(* this);
}

bool
NdbDictionary::Table::getTablespace(Uint32 *id, Uint32 *version) const 
{
  if (m_impl.m_tablespace_id == RNIL)
    return false;
  if (id)
    *id= m_impl.m_tablespace_id;
  if (version)
    *version= m_impl.m_version;
  return true;
}

const char *
NdbDictionary::Table::getTablespaceName() const 
{
  return m_impl.m_tablespace_name.c_str();
}

int
NdbDictionary::Table::setTablespaceName(const char * name){
  m_impl.m_tablespace_id = ~0;
  m_impl.m_tablespace_version = ~0;
  return !m_impl.m_tablespace_name.assign(name);
}

int
NdbDictionary::Table::setTablespace(const NdbDictionary::Tablespace & ts){
  m_impl.m_tablespace_id = NdbTablespaceImpl::getImpl(ts).m_id;
  m_impl.m_tablespace_version = ts.getObjectVersion();
  return !m_impl.m_tablespace_name.assign(ts.getName());
}

bool
NdbDictionary::Table::getHashMap(Uint32 *id, Uint32 *version) const
{
  if (m_impl.m_hash_map_id == RNIL)
    return false;
  if (id)
    *id= m_impl.m_hash_map_id;
  if (version)
    *version= m_impl.m_hash_map_version;
  return true;
}

int
NdbDictionary::Table::setHashMap(const NdbDictionary::HashMap& hm)
{
  m_impl.m_hash_map_id = hm.getObjectId();
  m_impl.m_hash_map_version = hm.getObjectVersion();
  return 0;
}

void
NdbDictionary::Table::setRowChecksumIndicator(bool val){
  m_impl.m_row_checksum = val;
}

bool 
NdbDictionary::Table::getRowChecksumIndicator() const {
  return m_impl.m_row_checksum;
}

void
NdbDictionary::Table::setRowGCIIndicator(bool val){
  m_impl.m_row_gci = val;
}

bool 
NdbDictionary::Table::getRowGCIIndicator() const {
  return m_impl.m_row_gci;
}

void
NdbDictionary::Table::setForceVarPart(bool val){
  m_impl.m_force_var_part = val;
}

bool 
NdbDictionary::Table::getForceVarPart() const {
  return m_impl.m_force_var_part;
}

bool
NdbDictionary::Table::hasDefaultValues() const {
  return m_impl.m_has_default_values;
}

const NdbRecord*
NdbDictionary::Table::getDefaultRecord() const {
  return m_impl.m_ndbrecord;
}

int
NdbDictionary::Table::aggregate(NdbError& error)
{
  return m_impl.aggregate(error);
}

int
NdbDictionary::Table::validate(NdbError& error)
{
  return m_impl.validate(error);
}

Uint32
NdbDictionary::Table::getPartitionId(Uint32 hashValue) const
{
  switch (m_impl.m_fragmentType){
  case NdbDictionary::Object::FragAllSmall:
  case NdbDictionary::Object::FragAllMedium:
  case NdbDictionary::Object::FragAllLarge:
  case NdbDictionary::Object::FragSingle:
  case NdbDictionary::Object::DistrKeyLin:
  {
    Uint32 fragmentId = hashValue & m_impl.m_hashValueMask;
    if(fragmentId < m_impl.m_hashpointerValue) 
      fragmentId = hashValue & ((m_impl.m_hashValueMask << 1) + 1);
    return fragmentId;
  }
  case NdbDictionary::Object::DistrKeyHash:
  {
    Uint32 cnt = m_impl.m_fragmentCount;
    return hashValue % (cnt ? cnt : 1);
  }
  case NdbDictionary::Object::HashMapPartition:
  {
    Uint32 cnt = m_impl.m_hash_map.size();
    return m_impl.m_hash_map[hashValue % cnt];
  }
  default:
    return 0;
  }
}

/*****************************************************************
 * Index facade
 */

NdbDictionary::Index::Index(const char * name)
  : m_impl(* new NdbIndexImpl(* this))
{
  setName(name);
}

NdbDictionary::Index::Index(NdbIndexImpl & impl)
  : m_impl(impl) 
{
}

NdbDictionary::Index::~Index(){
  NdbIndexImpl * tmp = &m_impl;  
  if(this != tmp){
    delete tmp;
  }
}

int
NdbDictionary::Index::setName(const char * name){
  return m_impl.setName(name);
}

const char * 
NdbDictionary::Index::getName() const {
  return m_impl.getName();
}

int
NdbDictionary::Index::setTable(const char * table){
  return m_impl.setTable(table);
}

const char * 
NdbDictionary::Index::getTable() const {
  return m_impl.getTable();
}

unsigned
NdbDictionary::Index::getNoOfColumns() const {
  return m_impl.m_columns.size();
}

int
NdbDictionary::Index::getNoOfIndexColumns() const {
  return m_impl.m_columns.size();
}

const NdbDictionary::Column *
NdbDictionary::Index::getColumn(unsigned no) const {
  if(no < m_impl.m_columns.size())
    return m_impl.m_columns[no];
  return NULL;
}

const char *
NdbDictionary::Index::getIndexColumn(int no) const {
  const NdbDictionary::Column* col = getColumn(no);

  if (col)
    return col->getName();
  else
    return NULL;
}

const NdbRecord*
NdbDictionary::Index::getDefaultRecord() const {
  return m_impl.m_table->m_ndbrecord;
}

int
NdbDictionary::Index::addColumn(const Column & c){
  NdbColumnImpl* col = new NdbColumnImpl;
  if (col == NULL)
  {
    errno = ENOMEM;
    return -1;
  }
  (* col) = NdbColumnImpl::getImpl(c);

  col->m_indexSourced=true;
  
  /* Remove defaults from indexed columns */
  col->m_defaultValue.clear();

  if (m_impl.m_columns.push_back(col))
  {
    return -1;
  }
  return 0;
}

int
NdbDictionary::Index::addColumnName(const char * name){
  const Column c(name);
  return addColumn(c);
}

int
NdbDictionary::Index::addIndexColumn(const char * name){
  const Column c(name);
  return addColumn(c);
}

int
NdbDictionary::Index::addColumnNames(unsigned noOfNames, const char ** names){
  for(unsigned i = 0; i < noOfNames; i++) {
    const Column c(names[i]);
    if (addColumn(c))
    {
      return -1;
    }
  }
  return 0;
}

int
NdbDictionary::Index::addIndexColumns(int noOfNames, const char ** names){
  for(int i = 0; i < noOfNames; i++) {
    const Column c(names[i]);
    if (addColumn(c))
    {
      return -1;
    }
  }
  return 0;
}

void
NdbDictionary::Index::setType(NdbDictionary::Index::Type t){
  m_impl.m_type = (NdbDictionary::Object::Type)t;
}

NdbDictionary::Index::Type
NdbDictionary::Index::getType() const {
  return (NdbDictionary::Index::Type)m_impl.m_type;
}

void
NdbDictionary::Index::setLogging(bool val){
  m_impl.m_logging = val;
}

bool
NdbDictionary::Index::getTemporary() const {
  return m_impl.m_temporary;
}

void
NdbDictionary::Index::setTemporary(bool val){
  m_impl.m_temporary = val;
}

bool 
NdbDictionary::Index::getLogging() const {
  return m_impl.m_logging;
}

NdbDictionary::Object::Status
NdbDictionary::Index::getObjectStatus() const {
  return m_impl.m_table->m_status;
}

int 
NdbDictionary::Index::getObjectVersion() const {
  return m_impl.m_table->m_version;
}

int 
NdbDictionary::Index::getObjectId() const {
  return m_impl.m_table->m_id;
}

/*****************************************************************
 * OptimizeTableHandle facade
 */
NdbDictionary::OptimizeTableHandle::OptimizeTableHandle()
  : m_impl(* new NdbOptimizeTableHandleImpl(* this))
{}

NdbDictionary::OptimizeTableHandle::OptimizeTableHandle(NdbOptimizeTableHandleImpl & impl)
  : m_impl(impl)
{}

NdbDictionary::OptimizeTableHandle::~OptimizeTableHandle()
{
  NdbOptimizeTableHandleImpl * tmp = &m_impl;
  if(this != tmp){
    delete tmp;
  }
}

int
NdbDictionary::OptimizeTableHandle::next()
{
  return m_impl.next();
}

int
NdbDictionary::OptimizeTableHandle::close()
{
  int result = m_impl.close();
  return result;
}

/*****************************************************************
 * OptimizeIndexHandle facade
 */
NdbDictionary::OptimizeIndexHandle::OptimizeIndexHandle()
  : m_impl(* new NdbOptimizeIndexHandleImpl(* this))
{}

NdbDictionary::OptimizeIndexHandle::OptimizeIndexHandle(NdbOptimizeIndexHandleImpl & impl)
  : m_impl(impl)
{}

NdbDictionary::OptimizeIndexHandle::~OptimizeIndexHandle()
{
  NdbOptimizeIndexHandleImpl * tmp = &m_impl;
  if(this != tmp){
    delete tmp;
  }
}

int
NdbDictionary::OptimizeIndexHandle::next()
{
  return m_impl.next();
}

int
NdbDictionary::OptimizeIndexHandle::close()
{
  int result = m_impl.close();
  return result;
}

/*****************************************************************
 * Event facade
 */
NdbDictionary::Event::Event(const char * name)
  : m_impl(* new NdbEventImpl(* this))
{
  setName(name);
}

NdbDictionary::Event::Event(const char * name, const Table& table)
  : m_impl(* new NdbEventImpl(* this))
{
  setName(name);
  setTable(table);
}

NdbDictionary::Event::Event(NdbEventImpl & impl)
  : m_impl(impl) 
{
}

NdbDictionary::Event::~Event()
{
  NdbEventImpl * tmp = &m_impl;
  if(this != tmp){
    delete tmp;
  }
}

int
NdbDictionary::Event::setName(const char * name)
{
  return m_impl.setName(name);
}

const char *
NdbDictionary::Event::getName() const
{
  return m_impl.getName();
}

void 
NdbDictionary::Event::setTable(const Table& table)
{
  m_impl.setTable(table);
}

const NdbDictionary::Table *
NdbDictionary::Event::getTable() const
{
  return m_impl.getTable();
}

int
NdbDictionary::Event::setTable(const char * table)
{
  return m_impl.setTable(table);
}

const char*
NdbDictionary::Event::getTableName() const
{
  return m_impl.getTableName();
}

void
NdbDictionary::Event::addTableEvent(const TableEvent t)
{
  m_impl.addTableEvent(t);
}

bool
NdbDictionary::Event::getTableEvent(const TableEvent t) const
{
  return m_impl.getTableEvent(t);
}

void
NdbDictionary::Event::setDurability(EventDurability d)
{
  m_impl.setDurability(d);
}

NdbDictionary::Event::EventDurability
NdbDictionary::Event::getDurability() const
{
  return m_impl.getDurability();
}

void
NdbDictionary::Event::setReport(EventReport r)
{
  m_impl.setReport(r);
}

NdbDictionary::Event::EventReport
NdbDictionary::Event::getReport() const
{
  return m_impl.getReport();
}

void
NdbDictionary::Event::addColumn(const Column & c){
  NdbColumnImpl* col = new NdbColumnImpl;
  (* col) = NdbColumnImpl::getImpl(c);
  m_impl.m_columns.push_back(col);
}

void
NdbDictionary::Event::addEventColumn(unsigned attrId)
{
  m_impl.m_attrIds.push_back(attrId);
}

void
NdbDictionary::Event::addEventColumn(const char * name)
{
  const Column c(name);
  addColumn(c);
}

void
NdbDictionary::Event::addEventColumns(int n, const char ** names)
{
  for (int i = 0; i < n; i++)
    addEventColumn(names[i]);
}

int NdbDictionary::Event::getNoOfEventColumns() const
{
  return m_impl.getNoOfEventColumns();
}

const NdbDictionary::Column *
NdbDictionary::Event::getEventColumn(unsigned no) const
{
  return m_impl.getEventColumn(no);
}

void NdbDictionary::Event::mergeEvents(bool flag)
{
  m_impl.m_mergeEvents = flag;
}

NdbDictionary::Object::Status
NdbDictionary::Event::getObjectStatus() const
{
  return m_impl.m_status;
}

int 
NdbDictionary::Event::getObjectVersion() const
{
  return m_impl.m_version;
}

int 
NdbDictionary::Event::getObjectId() const {
  return m_impl.m_id;
}

void NdbDictionary::Event::print()
{
  m_impl.print();
}

/*****************************************************************
 * Tablespace facade
 */
NdbDictionary::Tablespace::Tablespace()
  : m_impl(* new NdbTablespaceImpl(* this))
{
}

NdbDictionary::Tablespace::Tablespace(NdbTablespaceImpl & impl)
  : m_impl(impl) 
{
}

NdbDictionary::Tablespace::Tablespace(const NdbDictionary::Tablespace & org)
  : Object(org), m_impl(* new NdbTablespaceImpl(* this))
{
  m_impl.assign(org.m_impl);
}

NdbDictionary::Tablespace::~Tablespace(){
  NdbTablespaceImpl * tmp = &m_impl;  
  if(this != tmp){
    delete tmp;
  }
}

void 
NdbDictionary::Tablespace::setName(const char * name){
  m_impl.m_name.assign(name);
}

const char * 
NdbDictionary::Tablespace::getName() const {
  return m_impl.m_name.c_str();
}

void
NdbDictionary::Tablespace::setAutoGrowSpecification
(const NdbDictionary::AutoGrowSpecification& spec){
  m_impl.m_grow_spec = spec;
}
const NdbDictionary::AutoGrowSpecification& 
NdbDictionary::Tablespace::getAutoGrowSpecification() const {
  return m_impl.m_grow_spec;
}

void
NdbDictionary::Tablespace::setExtentSize(Uint32 sz){
  m_impl.m_extent_size = sz;
}

Uint32
NdbDictionary::Tablespace::getExtentSize() const {
  return m_impl.m_extent_size;
}

void
NdbDictionary::Tablespace::setDefaultLogfileGroup(const char * name){
  m_impl.m_logfile_group_id = ~0;
  m_impl.m_logfile_group_version = ~0;
  m_impl.m_logfile_group_name.assign(name);
}

void
NdbDictionary::Tablespace::setDefaultLogfileGroup
(const NdbDictionary::LogfileGroup & lg){
  m_impl.m_logfile_group_id = NdbLogfileGroupImpl::getImpl(lg).m_id;
  m_impl.m_logfile_group_version = lg.getObjectVersion();
  m_impl.m_logfile_group_name.assign(lg.getName());
}

const char * 
NdbDictionary::Tablespace::getDefaultLogfileGroup() const {
  return m_impl.m_logfile_group_name.c_str();
}

Uint32
NdbDictionary::Tablespace::getDefaultLogfileGroupId() const {
  return m_impl.m_logfile_group_id;
}

NdbDictionary::Object::Status
NdbDictionary::Tablespace::getObjectStatus() const {
  return m_impl.m_status;
}

int 
NdbDictionary::Tablespace::getObjectVersion() const {
  return m_impl.m_version;
}

int 
NdbDictionary::Tablespace::getObjectId() const {
  return m_impl.m_id;
}

/*****************************************************************
 * LogfileGroup facade
 */
NdbDictionary::LogfileGroup::LogfileGroup()
  : m_impl(* new NdbLogfileGroupImpl(* this))
{
}

NdbDictionary::LogfileGroup::LogfileGroup(NdbLogfileGroupImpl & impl)
  : m_impl(impl) 
{
}

NdbDictionary::LogfileGroup::LogfileGroup(const NdbDictionary::LogfileGroup & org)
  : Object(org), m_impl(* new NdbLogfileGroupImpl(* this)) 
{
  m_impl.assign(org.m_impl);
}

NdbDictionary::LogfileGroup::~LogfileGroup(){
  NdbLogfileGroupImpl * tmp = &m_impl;  
  if(this != tmp){
    delete tmp;
  }
}

void 
NdbDictionary::LogfileGroup::setName(const char * name){
  m_impl.m_name.assign(name);
}

const char * 
NdbDictionary::LogfileGroup::getName() const {
  return m_impl.m_name.c_str();
}

void
NdbDictionary::LogfileGroup::setUndoBufferSize(Uint32 sz){
  m_impl.m_undo_buffer_size = sz;
}

Uint32
NdbDictionary::LogfileGroup::getUndoBufferSize() const {
  return m_impl.m_undo_buffer_size;
}

void
NdbDictionary::LogfileGroup::setAutoGrowSpecification
(const NdbDictionary::AutoGrowSpecification& spec){
  m_impl.m_grow_spec = spec;
}
const NdbDictionary::AutoGrowSpecification& 
NdbDictionary::LogfileGroup::getAutoGrowSpecification() const {
  return m_impl.m_grow_spec;
}

Uint64 NdbDictionary::LogfileGroup::getUndoFreeWords() const {
  return m_impl.m_undo_free_words;
}

NdbDictionary::Object::Status
NdbDictionary::LogfileGroup::getObjectStatus() const {
  return m_impl.m_status;
}

int 
NdbDictionary::LogfileGroup::getObjectVersion() const {
  return m_impl.m_version;
}

int 
NdbDictionary::LogfileGroup::getObjectId() const {
  return m_impl.m_id;
}

/*****************************************************************
 * Datafile facade
 */
NdbDictionary::Datafile::Datafile()
  : m_impl(* new NdbDatafileImpl(* this))
{
}

NdbDictionary::Datafile::Datafile(NdbDatafileImpl & impl)
  : m_impl(impl) 
{
}

NdbDictionary::Datafile::Datafile(const NdbDictionary::Datafile & org)
  : Object(org), m_impl(* new NdbDatafileImpl(* this)) 
{
  m_impl.assign(org.m_impl);
}

NdbDictionary::Datafile::~Datafile(){
  NdbDatafileImpl * tmp = &m_impl;  
  if(this != tmp){
    delete tmp;
  }
}

void 
NdbDictionary::Datafile::setPath(const char * path){
  m_impl.m_path.assign(path);
}

const char * 
NdbDictionary::Datafile::getPath() const {
  return m_impl.m_path.c_str();
}

void 
NdbDictionary::Datafile::setSize(Uint64 sz){
  m_impl.m_size = sz;
}

Uint64
NdbDictionary::Datafile::getSize() const {
  return m_impl.m_size;
}

Uint64
NdbDictionary::Datafile::getFree() const {
  return m_impl.m_free;
}

int
NdbDictionary::Datafile::setTablespace(const char * tablespace){
  m_impl.m_filegroup_id = ~0;
  m_impl.m_filegroup_version = ~0;
  return !m_impl.m_filegroup_name.assign(tablespace);
}

int
NdbDictionary::Datafile::setTablespace(const NdbDictionary::Tablespace & ts){
  m_impl.m_filegroup_id = NdbTablespaceImpl::getImpl(ts).m_id;
  m_impl.m_filegroup_version = ts.getObjectVersion();
  return !m_impl.m_filegroup_name.assign(ts.getName());
}

const char *
NdbDictionary::Datafile::getTablespace() const {
  return m_impl.m_filegroup_name.c_str();
}

void
NdbDictionary::Datafile::getTablespaceId(NdbDictionary::ObjectId* dst) const 
{
  if (dst)
  {
    NdbDictObjectImpl::getImpl(* dst).m_id = m_impl.m_filegroup_id;
    NdbDictObjectImpl::getImpl(* dst).m_version = m_impl.m_filegroup_version;
  }
}

NdbDictionary::Object::Status
NdbDictionary::Datafile::getObjectStatus() const {
  return m_impl.m_status;
}

int 
NdbDictionary::Datafile::getObjectVersion() const {
  return m_impl.m_version;
}

int 
NdbDictionary::Datafile::getObjectId() const {
  return m_impl.m_id;
}

/*****************************************************************
 * Undofile facade
 */
NdbDictionary::Undofile::Undofile()
  : m_impl(* new NdbUndofileImpl(* this))
{
}

NdbDictionary::Undofile::Undofile(NdbUndofileImpl & impl)
  : m_impl(impl) 
{
}

NdbDictionary::Undofile::Undofile(const NdbDictionary::Undofile & org)
  : Object(org), m_impl(* new NdbUndofileImpl(* this))
{
  m_impl.assign(org.m_impl);
}

NdbDictionary::Undofile::~Undofile(){
  NdbUndofileImpl * tmp = &m_impl;  
  if(this != tmp){
    delete tmp;
  }
}

void 
NdbDictionary::Undofile::setPath(const char * path){
  m_impl.m_path.assign(path);
}

const char * 
NdbDictionary::Undofile::getPath() const {
  return m_impl.m_path.c_str();
}

void 
NdbDictionary::Undofile::setSize(Uint64 sz){
  m_impl.m_size = sz;
}

Uint64
NdbDictionary::Undofile::getSize() const {
  return m_impl.m_size;
}

void 
NdbDictionary::Undofile::setLogfileGroup(const char * logfileGroup){
  m_impl.m_filegroup_id = ~0;
  m_impl.m_filegroup_version = ~0;
  m_impl.m_filegroup_name.assign(logfileGroup);
}

void 
NdbDictionary::Undofile::setLogfileGroup
(const NdbDictionary::LogfileGroup & ts){
  m_impl.m_filegroup_id = NdbLogfileGroupImpl::getImpl(ts).m_id;
  m_impl.m_filegroup_version = ts.getObjectVersion();
  m_impl.m_filegroup_name.assign(ts.getName());
}

const char *
NdbDictionary::Undofile::getLogfileGroup() const {
  return m_impl.m_filegroup_name.c_str();
}

void
NdbDictionary::Undofile::getLogfileGroupId(NdbDictionary::ObjectId * dst)const 
{
  if (dst)
  {
    NdbDictObjectImpl::getImpl(* dst).m_id = m_impl.m_filegroup_id;
    NdbDictObjectImpl::getImpl(* dst).m_version = m_impl.m_filegroup_version;
  }
}

NdbDictionary::Object::Status
NdbDictionary::Undofile::getObjectStatus() const {
  return m_impl.m_status;
}

int 
NdbDictionary::Undofile::getObjectVersion() const {
  return m_impl.m_version;
}

int 
NdbDictionary::Undofile::getObjectId() const {
  return m_impl.m_id;
}

/*****************************************************************
 * HashMap facade
 */
NdbDictionary::HashMap::HashMap()
  : m_impl(* new NdbHashMapImpl(* this))
{
}

NdbDictionary::HashMap::HashMap(NdbHashMapImpl & impl)
  : m_impl(impl)
{
}

NdbDictionary::HashMap::HashMap(const NdbDictionary::HashMap & org)
  : Object(org), m_impl(* new NdbHashMapImpl(* this))
{
  m_impl.assign(org.m_impl);
}

NdbDictionary::HashMap::~HashMap(){
  NdbHashMapImpl * tmp = &m_impl;
  if(this != tmp){
    delete tmp;
  }
}

void
NdbDictionary::HashMap::setName(const char * path)
{
  m_impl.m_name.assign(path);
}

const char *
NdbDictionary::HashMap::getName() const
{
  return m_impl.m_name.c_str();
}

void
NdbDictionary::HashMap::setMap(const Uint32* map, Uint32 len)
{
  m_impl.m_map.assign(map, len);
}

Uint32
NdbDictionary::HashMap::getMapLen() const
{
  return m_impl.m_map.size();
}

int
NdbDictionary::HashMap::getMapValues(Uint32* dst, Uint32 len) const
{
  if (len != getMapLen())
    return -1;

  memcpy(dst, m_impl.m_map.getBase(), sizeof(Uint32) * len);
  return 0;
}

bool
NdbDictionary::HashMap::equal(const NdbDictionary::HashMap & obj) const
{
  return m_impl.m_map.equal(obj.m_impl.m_map);
}

NdbDictionary::Object::Status
NdbDictionary::HashMap::getObjectStatus() const {
  return m_impl.m_status;
}

int
NdbDictionary::HashMap::getObjectVersion() const {
  return m_impl.m_version;
}

int
NdbDictionary::HashMap::getObjectId() const {
  return m_impl.m_id;
}

int
NdbDictionary::Dictionary::getDefaultHashMap(NdbDictionary::HashMap& dst,
                                             Uint32 fragments)
{
  BaseString tmp;
  tmp.assfmt("DEFAULT-HASHMAP-%u-%u",
             NDB_DEFAULT_HASHMAP_BUCKTETS, fragments);

  return getHashMap(dst, tmp.c_str());
}

int
NdbDictionary::Dictionary::getHashMap(NdbDictionary::HashMap& dst,
                                      const char * name)
{
  return m_impl.m_receiver.get_hashmap(NdbHashMapImpl::getImpl(dst), name);
}

int
NdbDictionary::Dictionary::getHashMap(NdbDictionary::HashMap& dst,
                                      const NdbDictionary::Table* tab)
{
  if (tab == 0 ||
      tab->getFragmentType() != NdbDictionary::Object::HashMapPartition)
  {
    return -1;
  }
  return
    m_impl.m_receiver.get_hashmap(NdbHashMapImpl::getImpl(dst),
                                  NdbTableImpl::getImpl(*tab).m_hash_map_id);
}

int
NdbDictionary::Dictionary::initDefaultHashMap(NdbDictionary::HashMap& dst,
                                              Uint32 fragments)
{
  BaseString tmp;
  tmp.assfmt("DEFAULT-HASHMAP-%u-%u",
             NDB_DEFAULT_HASHMAP_BUCKTETS, fragments);

  dst.setName(tmp.c_str());

  Vector<Uint32> map;
  for (Uint32 i = 0; i<NDB_DEFAULT_HASHMAP_BUCKTETS; i++)
  {
    map.push_back(i % fragments);
  }

  dst.setMap(map.getBase(), map.size());
  return 0;
}

int
NdbDictionary::Dictionary::prepareHashMap(const Table& oldTableF,
                                          Table& newTableF)
{
  if (!hasSchemaTrans())
  {
    return -1;
  }

  const NdbTableImpl& oldTable = NdbTableImpl::getImpl(oldTableF);
  NdbTableImpl& newTable = NdbTableImpl::getImpl(newTableF);

  if (oldTable.getFragmentType() == NdbDictionary::Object::HashMapPartition)
  {
    HashMap oldmap;
    if (getHashMap(oldmap, &oldTable) == -1)
    {
      return -1;
    }

    if (oldmap.getObjectVersion() != (int)oldTable.m_hash_map_version)
    {
      return -1;
    }

    HashMap newmapF;
    NdbHashMapImpl& newmap = NdbHashMapImpl::getImpl(newmapF);
    newmap.assign(NdbHashMapImpl::getImpl(oldmap));

    Uint32 oldcnt = oldTable.getFragmentCount();
    Uint32 newcnt = newTable.getFragmentCount();
    if (newcnt == 0)
    {
      /**
       * reorg...we don't know how many fragments new table should have
       *   create if exist a default map...which will "know" how many fragments there are
       */
      ObjectId tmp;
      int ret = m_impl.m_receiver.create_hashmap(NdbHashMapImpl::getImpl(newmapF),
                                                 &NdbDictObjectImpl::getImpl(tmp),
                                                 CreateHashMapReq::CreateDefault |
                                                 CreateHashMapReq::CreateIfNotExists);
      if (ret)
      {
        return ret;
      }

      HashMap hm;
      ret = m_impl.m_receiver.get_hashmap(NdbHashMapImpl::getImpl(hm), tmp.getObjectId());
      if (ret)
      {
        return ret;
      }
      Uint32 zero = 0;
      Vector<Uint32> values;
      values.fill(hm.getMapLen() - 1, zero);
      hm.getMapValues(values.getBase(), values.size());
      for (Uint32 i = 0; i<hm.getMapLen(); i++)
      {
        if (values[i] > newcnt)
          newcnt = values[i];
      }
      newcnt++; // Loop will find max val, cnt = max + 1
      if (newcnt < oldcnt)
      {
        /**
         * drop partition is currently not supported...
         *   and since this is a "reorg" (newcnt == 0) we silently change it to a nop
         */
        newcnt = oldcnt;
      }
      newTable.setFragmentCount(newcnt);
    }

    for (Uint32 i = 0; i<newmap.m_map.size(); i++)
    {
      Uint32 newval = i % newcnt;
      if (newval >= oldcnt)
      {
        newmap.m_map[i] = newval;
      }
    }

    /**
     * Check if this accidently became a "default" map
     */
    HashMap def;
    if (getDefaultHashMap(def, newcnt) == 0)
    {
      if (def.equal(newmapF))
      {
        newTable.m_hash_map_id = def.getObjectId();
        newTable.m_hash_map_version = def.getObjectVersion();
        return 0;
      }
    }

    initDefaultHashMap(def, newcnt);
    if (def.equal(newmapF))
    {
      ObjectId tmp;
      if (createHashMap(def, &tmp) == -1)
      {
        return -1;
      }
      newTable.m_hash_map_id = tmp.getObjectId();
      newTable.m_hash_map_version = tmp.getObjectVersion();
      return 0;
    }

    int cnt = 0;
retry:
    if (cnt == 0)
    {
      newmap.m_name.assfmt("HASHMAP-%u-%u-%u",
                           NDB_DEFAULT_HASHMAP_BUCKTETS,
                           oldcnt,
                           newcnt);
    }
    else
    {
      newmap.m_name.assfmt("HASHMAP-%u-%u-%u-#%u",
                           NDB_DEFAULT_HASHMAP_BUCKTETS,
                           oldcnt,
                           newcnt,
                           cnt);

    }

    if (getHashMap(def, newmap.getName()) == 0)
    {
      if (def.equal(newmap))
      {
        newTable.m_hash_map_id = def.getObjectId();
        newTable.m_hash_map_version = def.getObjectVersion();
        return 0;
      }
      cnt++;
      goto retry;
    }

    ObjectId tmp;
    if (createHashMap(newmapF, &tmp) == -1)
    {
      return -1;
    }
    newTable.m_hash_map_id = tmp.getObjectId();
    newTable.m_hash_map_version = tmp.getObjectVersion();
    return 0;
  }
  assert(false); // NOT SUPPORTED YET
  return -1;
}

/*****************************************************************
 * Dictionary facade
 */
NdbDictionary::Dictionary::Dictionary(Ndb & ndb)
  : m_impl(* new NdbDictionaryImpl(ndb, *this))
{
}

NdbDictionary::Dictionary::Dictionary(NdbDictionaryImpl & impl)
  : m_impl(impl) 
{
}
NdbDictionary::Dictionary::~Dictionary(){
  NdbDictionaryImpl * tmp = &m_impl;  
  if(this != tmp){
    delete tmp;
  }
}

// do op within trans if no trans exists
#define DO_TRANS(ret, action) \
  { \
    bool trans = hasSchemaTrans(); \
    if ((trans || (ret = beginSchemaTrans()) == 0) && \
        (ret = (action)) == 0 && \
        (trans || (ret = endSchemaTrans()) == 0)) \
      ; \
    else if (!trans) { \
      NdbError save_error = m_impl.m_error; \
      (void)endSchemaTrans(SchemaTransAbort); \
      m_impl.m_error = save_error; \
    } \
  }

int 
NdbDictionary::Dictionary::createTable(const Table & t)
{
  int ret;
  DO_TRANS(
    ret,
    m_impl.createTable(NdbTableImpl::getImpl(t))
  );
  return ret;
}

int
NdbDictionary::Dictionary::optimizeTable(const Table &t,
                                         OptimizeTableHandle &h){
  DBUG_ENTER("NdbDictionary::Dictionary::optimzeTable");
  DBUG_RETURN(m_impl.optimizeTable(NdbTableImpl::getImpl(t), 
                                   NdbOptimizeTableHandleImpl::getImpl(h)));
}

int
NdbDictionary::Dictionary::optimizeIndex(const Index &ind,
                                         NdbDictionary::OptimizeIndexHandle &h)
{
  DBUG_ENTER("NdbDictionary::Dictionary::optimzeIndex");
  DBUG_RETURN(m_impl.optimizeIndex(NdbIndexImpl::getImpl(ind),
                                   NdbOptimizeIndexHandleImpl::getImpl(h)));
}

int
NdbDictionary::Dictionary::dropTable(Table & t)
{
  int ret;
  DO_TRANS(
    ret,
    m_impl.dropTable(NdbTableImpl::getImpl(t))
  );
  return ret;
}

int
NdbDictionary::Dictionary::dropTableGlobal(const Table & t)
{
  int ret;
  DO_TRANS(
    ret,
    m_impl.dropTableGlobal(NdbTableImpl::getImpl(t))
  );
  return ret;
}

int
NdbDictionary::Dictionary::dropTable(const char * name)
{
  int ret;
  DO_TRANS(
    ret,
    m_impl.dropTable(name)
  );
  return ret;
}

bool
NdbDictionary::Dictionary::supportedAlterTable(const Table & f,
					       const Table & t)
{
  return m_impl.supportedAlterTable(NdbTableImpl::getImpl(f),
                                    NdbTableImpl::getImpl(t));
}

int
NdbDictionary::Dictionary::alterTable(const Table & f, const Table & t)
{
  int ret;
  DO_TRANS(
    ret,
    m_impl.alterTable(NdbTableImpl::getImpl(f),
                      NdbTableImpl::getImpl(t))
  );
  return ret;
}

int
NdbDictionary::Dictionary::alterTableGlobal(const Table & f,
                                            const Table & t)
{
  int ret;
  DO_TRANS(
    ret,
    m_impl.alterTableGlobal(NdbTableImpl::getImpl(f),
                            NdbTableImpl::getImpl(t))
  );
  return ret;
}

const NdbDictionary::Table * 
NdbDictionary::Dictionary::getTable(const char * name, void **data) const
{
  NdbTableImpl * t = m_impl.getTable(name, data);
  if(t)
    return t->m_facade;
  return 0;
}

const NdbDictionary::Index * 
NdbDictionary::Dictionary::getIndexGlobal(const char * indexName,
                                          const Table &ndbtab) const
{
  NdbIndexImpl * i = m_impl.getIndexGlobal(indexName,
                                           NdbTableImpl::getImpl(ndbtab));
  if(i)
    return i->m_facade;
  return 0;
}

const NdbDictionary::Index *
NdbDictionary::Dictionary::getIndexGlobal(const char * indexName,
                                          const char * tableName) const
{
  NdbIndexImpl * i = m_impl.getIndexGlobal(indexName,
                                           tableName);
  if(i)
    return i->m_facade;
  return 0;
}

const NdbDictionary::Table * 
NdbDictionary::Dictionary::getTableGlobal(const char * name) const
{
  NdbTableImpl * t = m_impl.getTableGlobal(name);
  if(t)
    return t->m_facade;
  return 0;
}

int
NdbDictionary::Dictionary::removeIndexGlobal(const Index &ndbidx,
                                             int invalidate) const
{
  return m_impl.releaseIndexGlobal(NdbIndexImpl::getImpl(ndbidx), invalidate);
}

int
NdbDictionary::Dictionary::removeTableGlobal(const Table &ndbtab,
                                             int invalidate) const
{
  return m_impl.releaseTableGlobal(NdbTableImpl::getImpl(ndbtab), invalidate);
}

NdbRecord *
NdbDictionary::Dictionary::createRecord(const Table *table,
                                        const RecordSpecification *recSpec,
                                        Uint32 length,
                                        Uint32 elemSize,
                                        Uint32 flags)
{
  /* We want to obtain a global reference to the Table object */
  NdbTableImpl* impl=&NdbTableImpl::getImpl(*table);
  Ndb* myNdb= &m_impl.m_ndb;

  /* Temporarily change Ndb object to use table's database 
   * and schema 
   */
  BaseString currentDb(myNdb->getDatabaseName());
  BaseString currentSchema(myNdb->getDatabaseSchemaName());

  myNdb->setDatabaseName
    (Ndb::getDatabaseFromInternalName(impl->m_internalName.c_str()).c_str());
  myNdb->setDatabaseSchemaName
    (Ndb::getSchemaFromInternalName(impl->m_internalName.c_str()).c_str());

  /* Get global ref to table.  This is released below, or when the
   * NdbRecord is released
   */
  const Table* globalTab= getTableGlobal(impl->m_externalName.c_str());

  /* Restore Ndb object's DB and Schema */
  myNdb->setDatabaseName(currentDb.c_str());
  myNdb->setDatabaseSchemaName(currentSchema.c_str());

  if (globalTab == NULL)
    /* An error is set on the dictionary */
    return NULL;
  
  NdbTableImpl* globalTabImpl= &NdbTableImpl::getImpl(*globalTab);
  
  assert(impl->m_id == globalTabImpl->m_id);
  if (table_version_major(impl->m_version) != 
      table_version_major(globalTabImpl->m_version))
  {
    removeTableGlobal(*globalTab, false); // Don't invalidate
    m_impl.m_error.code= 241; //Invalid schema object version
    return NULL;
  }

  NdbRecord* result= m_impl.createRecord(globalTabImpl,
                                         recSpec,
                                         length,
                                         elemSize,
                                         flags,
                                         false); // Not default NdbRecord

  if (!result)
  {
    removeTableGlobal(*globalTab, false); // Don't invalidate
  }
  return result;
}

NdbRecord *
NdbDictionary::Dictionary::createRecord(const Index *index,
                                        const Table *table,
                                        const RecordSpecification *recSpec,
                                        Uint32 length,
                                        Uint32 elemSize,
                                        Uint32 flags)
{
  /* We want to obtain a global reference to the Index's underlying
   * table object
   */
  NdbTableImpl* tabImpl=&NdbTableImpl::getImpl(*table);
  Ndb* myNdb= &m_impl.m_ndb;

  /* Temporarily change Ndb object to use table's database 
   * and schema.  Index's database and schema are not 
   * useful for finding global table reference
   */
  BaseString currentDb(myNdb->getDatabaseName());
  BaseString currentSchema(myNdb->getDatabaseSchemaName());

  myNdb->setDatabaseName
    (Ndb::getDatabaseFromInternalName(tabImpl->m_internalName.c_str()).c_str());
  myNdb->setDatabaseSchemaName
    (Ndb::getSchemaFromInternalName(tabImpl->m_internalName.c_str()).c_str());

  /* Get global ref to index.  This is released below, or when the
   * NdbRecord object is released
   */
  const Index* globalIndex= getIndexGlobal(index->getName(), *table);

  /* Restore Ndb object's DB and Schema */
  myNdb->setDatabaseName(currentDb.c_str());
  myNdb->setDatabaseSchemaName(currentSchema.c_str());

  if (globalIndex == NULL)
    /* An error is set on the dictionary */
    return NULL;
  
  NdbIndexImpl* indexImpl= &NdbIndexImpl::getImpl(*index);
  NdbIndexImpl* globalIndexImpl= &NdbIndexImpl::getImpl(*globalIndex);
  
  assert(indexImpl->m_id == globalIndexImpl->m_id);
  
  if (table_version_major(indexImpl->m_version) != 
      table_version_major(globalIndexImpl->m_version))
  {
    removeIndexGlobal(*globalIndex, false); // Don't invalidate
    m_impl.m_error.code= 241; //Invalid schema object version
    return NULL;
  }

  NdbRecord* result= m_impl.createRecord(globalIndexImpl->m_table,
                                         recSpec,
                                         length,
                                         elemSize,
                                         flags,
                                         false); // Not default NdbRecord

  if (!result)
  {
    removeIndexGlobal(*globalIndex, false); // Don't invalidate
  }
  return result;
}

NdbRecord *
NdbDictionary::Dictionary::createRecord(const Index *index,
                                        const RecordSpecification *recSpec,
                                        Uint32 length,
                                        Uint32 elemSize,
                                        Uint32 flags)
{
  const NdbDictionary::Table *table= getTable(index->getTable());
  if (!table)
    return NULL;
  return createRecord(index,
                      table,
                      recSpec,
                      length,
                      elemSize,
                      flags);
}

void 
NdbDictionary::Dictionary::releaseRecord(NdbRecord *rec)
{
  m_impl.releaseRecord_impl(rec);
}

void NdbDictionary::Dictionary::putTable(const NdbDictionary::Table * table)
{
 NdbDictionary::Table  *copy_table = new NdbDictionary::Table;
  *copy_table = *table;
  m_impl.putTable(&NdbTableImpl::getImpl(*copy_table));
}

void NdbDictionary::Dictionary::set_local_table_data_size(unsigned sz)
{
  m_impl.m_local_table_data_size= sz;
}

const NdbDictionary::Table * 
NdbDictionary::Dictionary::getTable(const char * name) const
{
  return getTable(name, 0);
}

const NdbDictionary::Table *
NdbDictionary::Dictionary::getBlobTable(const NdbDictionary::Table* table,
                                        const char* col_name)
{
  const NdbDictionary::Column* col = table->getColumn(col_name);
  if (col == NULL) {
    m_impl.m_error.code = 4318;
    return NULL;
  }
  return getBlobTable(table, col->getColumnNo());
}

const NdbDictionary::Table *
NdbDictionary::Dictionary::getBlobTable(const NdbDictionary::Table* table,
                                        Uint32 col_no)
{
  return m_impl.getBlobTable(NdbTableImpl::getImpl(*table), col_no);
}

void
NdbDictionary::Dictionary::invalidateTable(const char * name){
  DBUG_ENTER("NdbDictionaryImpl::invalidateTable");
  NdbTableImpl * t = m_impl.getTable(name);
  if(t)
    m_impl.invalidateObject(* t);
  DBUG_VOID_RETURN;
}

void
NdbDictionary::Dictionary::invalidateTable(const Table *table){
  NdbTableImpl &t = NdbTableImpl::getImpl(*table);
  m_impl.invalidateObject(t);
}

void
NdbDictionary::Dictionary::removeCachedTable(const char * name){
  NdbTableImpl * t = m_impl.getTable(name);
  if(t)
    m_impl.removeCachedObject(* t);
}

void
NdbDictionary::Dictionary::removeCachedTable(const Table *table){
  NdbTableImpl &t = NdbTableImpl::getImpl(*table);
  m_impl.removeCachedObject(t);
}

int
NdbDictionary::Dictionary::createIndex(const Index & ind, bool offline)
{
  int ret;
  DO_TRANS(
    ret,
    m_impl.createIndex(NdbIndexImpl::getImpl(ind), offline)
  );
  return ret;
}

int
NdbDictionary::Dictionary::createIndex(const Index & ind, const Table & tab,
                                       bool offline)
{
  int ret;
  DO_TRANS(
    ret,
    m_impl.createIndex(NdbIndexImpl::getImpl(ind),
                       NdbTableImpl::getImpl(tab),
		       offline)
  );
  return ret;
}

int 
NdbDictionary::Dictionary::dropIndex(const char * indexName,
				     const char * tableName)
{
  int ret;
  DO_TRANS(
    ret,
    m_impl.dropIndex(indexName, tableName)
  );
  return ret;
}

int 
NdbDictionary::Dictionary::dropIndexGlobal(const Index &ind)
{
  int ret;
  DO_TRANS(
    ret,
    m_impl.dropIndexGlobal(NdbIndexImpl::getImpl(ind))
  );
  return ret;
}

const NdbDictionary::Index * 
NdbDictionary::Dictionary::getIndex(const char * indexName,
				    const char * tableName) const
{
  NdbIndexImpl * i = m_impl.getIndex(indexName, tableName);
  if(i)
    return i->m_facade;
  return 0;
}

void
NdbDictionary::Dictionary::invalidateIndex(const Index *index){
  DBUG_ENTER("NdbDictionary::Dictionary::invalidateIndex");
  NdbIndexImpl &i = NdbIndexImpl::getImpl(*index);
  assert(i.m_table != 0);
  m_impl.invalidateObject(* i.m_table);
  DBUG_VOID_RETURN;
}

void
NdbDictionary::Dictionary::invalidateIndex(const char * indexName,
                                           const char * tableName){
  DBUG_ENTER("NdbDictionaryImpl::invalidateIndex");
  NdbIndexImpl * i = m_impl.getIndex(indexName, tableName);
  if(i) {
    assert(i->m_table != 0);
    m_impl.invalidateObject(* i->m_table);
  }
  DBUG_VOID_RETURN;
}

int
NdbDictionary::Dictionary::forceGCPWait()
{
  return m_impl.forceGCPWait();
}

void
NdbDictionary::Dictionary::removeCachedIndex(const Index *index){
  DBUG_ENTER("NdbDictionary::Dictionary::removeCachedIndex");
  NdbIndexImpl &i = NdbIndexImpl::getImpl(*index);
  assert(i.m_table != 0);
  m_impl.removeCachedObject(* i.m_table);
  DBUG_VOID_RETURN;
}

void
NdbDictionary::Dictionary::removeCachedIndex(const char * indexName,
					     const char * tableName){
  NdbIndexImpl * i = m_impl.getIndex(indexName, tableName);
  if(i) {
    assert(i->m_table != 0);
    m_impl.removeCachedObject(* i->m_table);
  }
}

const NdbDictionary::Table *
NdbDictionary::Dictionary::getIndexTable(const char * indexName, 
					 const char * tableName) const
{
  NdbIndexImpl * i = m_impl.getIndex(indexName, tableName);
  NdbTableImpl * t = m_impl.getTable(tableName);
  if(i && t) {
    NdbTableImpl * it = m_impl.getIndexTable(i, t);
    return it->m_facade;
  }
  return 0;
}


int
NdbDictionary::Dictionary::createEvent(const Event & ev)
{
  return m_impl.createEvent(NdbEventImpl::getImpl(ev));
}

int 
NdbDictionary::Dictionary::dropEvent(const char * eventName, int force)
{
  return m_impl.dropEvent(eventName, force);
}

const NdbDictionary::Event *
NdbDictionary::Dictionary::getEvent(const char * eventName)
{
  NdbEventImpl * t = m_impl.getEvent(eventName);
  if(t)
    return t->m_facade;
  return 0;
}

int
NdbDictionary::Dictionary::listEvents(List& list)
{
  // delegate to overloaded const function for same semantics
  const NdbDictionary::Dictionary * const cthis = this;
  return cthis->NdbDictionary::Dictionary::listEvents(list);
}

int
NdbDictionary::Dictionary::listEvents(List& list) const
{
  return m_impl.listEvents(list);
}

int
NdbDictionary::Dictionary::listObjects(List& list, Object::Type type)
{
  // delegate to overloaded const function for same semantics
  const NdbDictionary::Dictionary * const cthis = this;
  return cthis->NdbDictionary::Dictionary::listObjects(list, type);
}

int
NdbDictionary::Dictionary::listObjects(List& list, Object::Type type) const
{
  // delegate to variant with FQ names param
  return listObjects(list, type, 
                     m_impl.m_ndb.usingFullyQualifiedNames());
}

int
NdbDictionary::Dictionary::listObjects(List& list, Object::Type type,
                                       bool fullyQualified) const
{
  return m_impl.listObjects(list, type, 
                            fullyQualified);
}

int
NdbDictionary::Dictionary::listIndexes(List& list, const char * tableName)
{
  // delegate to overloaded const function for same semantics
  const NdbDictionary::Dictionary * const cthis = this;
  return cthis->NdbDictionary::Dictionary::listIndexes(list, tableName);
}

int
NdbDictionary::Dictionary::listIndexes(List& list,
				       const char * tableName) const
{
  const NdbDictionary::Table* tab= getTable(tableName);
  if(tab == 0)
  {
    return -1;
  }
  return m_impl.listIndexes(list, tab->getTableId());
}

int
NdbDictionary::Dictionary::listIndexes(List& list,
				       const NdbDictionary::Table &table) const
{
  return m_impl.listIndexes(list, table.getTableId());
}


const struct NdbError & 
NdbDictionary::Dictionary::getNdbError() const {
  return m_impl.getNdbError();
}

int
NdbDictionary::Dictionary::getWarningFlags() const
{
  return m_impl.m_warn;
}

// printers

void
pretty_print_string(NdbOut& out, 
                    const NdbDictionary::NdbDataPrintFormat &f,
                    const char *type, bool is_binary,
                    const void *aref, unsigned sz)
{
  const unsigned char* ref = (const unsigned char*)aref;
  int i, len, printable= 1;
  // trailing zeroes are not printed
  for (i=sz-1; i >= 0; i--)
    if (ref[i] == 0) sz--;
    else break;
  if (!is_binary)
  {
    // trailing spaces are not printed
    for (i=sz-1; i >= 0; i--)
      if (ref[i] == 32) sz--;
      else break;
  }
  if (is_binary && f.hex_format)
  {
    if (sz == 0)
    {
      out.print("0x0");
      return;
    }
    out.print("0x");
    for (len = 0; len < (int)sz; len++)
      out.print("%02X", (int)ref[len]);
    return;
  }
  if (sz == 0) return; // empty

  for (len=0; len < (int)sz && ref[i] != 0; len++)
    if (printable && !isprint((int)ref[i]))
      printable= 0;

  if (printable)
    out.print("%.*s", len, ref);
  else
  {
    out.print("0x");
    for (i=0; i < len; i++)
      out.print("%02X", (int)ref[i]);
  }
  if (len != (int)sz)
  {
    out.print("[");
    for (i= len+1; ref[i] != 0; i++)
    out.print("%u]",len-i);
    assert((int)sz > i);
    pretty_print_string(out,f,type,is_binary,ref+i,sz-i);
  }
}

/* Three MySQL defs duplicated here : */ 
static const int MaxMySQLDecimalPrecision= 65;
static const int MaxMySQLDecimalScale= 30;
static const int DigitsPerDigit_t= 9; // (Decimal digits in 2^32)

/* Implications */
/* Space for -, . and \0 */
static const int MaxDecimalStrLen= MaxMySQLDecimalPrecision + 3;
static const int IntPartDigit_ts= 
  ((MaxMySQLDecimalPrecision - 
    MaxMySQLDecimalScale) +
   DigitsPerDigit_t -1) / DigitsPerDigit_t;
static const int FracPartDigit_ts= 
  (MaxMySQLDecimalScale + 
   DigitsPerDigit_t - 1) / DigitsPerDigit_t;
static const int DigitArraySize= IntPartDigit_ts + FracPartDigit_ts;

NdbOut&
NdbDictionary::printFormattedValue(NdbOut& out, 
                                   const NdbDataPrintFormat& format,
                                   const NdbDictionary::Column* c,
                                   const void* val)
{
  if (val == NULL)
  {
    out << format.null_string;
    return out;
  }
  
  const unsigned char* val_p= (const unsigned char*) val;

  uint length = c->getLength();
  Uint32 j;
  {
    const char *fields_optionally_enclosed_by;
    if (format.fields_enclosed_by[0] == '\0')
      fields_optionally_enclosed_by=
        format.fields_optionally_enclosed_by;
    else
      fields_optionally_enclosed_by= "";
    out << format.fields_enclosed_by;

    switch(c->getType()){
    case NdbDictionary::Column::Bigunsigned:
    {
      Uint64 temp;
      memcpy(&temp, val, 8); 
      out << temp;
      break;
    }
    case NdbDictionary::Column::Bit:
    {
      out << format.hex_prefix << "0x";
      {
        const Uint32 *buf = (const Uint32 *)val;
        unsigned int k = (length+31)/32;
        const unsigned int sigbits= length & 31;
        Uint32 wordMask= (1 << sigbits) -1;
        
        /* Skip leading all-0 words */
        while (k > 0)
        {
          const Uint32 v= buf[--k] & wordMask;
          if (v != 0)
            break;
          /* Following words have all bits significant */
          wordMask= ~0;
        }
        
        /* Write first sig word with non-zero bits */
        out.print("%X", (buf[k] & wordMask));
        
        /* Write remaining words (less significant) */
        while (k > 0)
          out.print("%.8X", buf[--k]);
      }
      break;
    }
    case NdbDictionary::Column::Unsigned:
    {
      if (length > 1)
        out << format.start_array_enclosure;
      out << *(const Uint32*)val;
      for (j = 1; j < length; j++)
        out << " " << *(((const Uint32*)val) + j);
      if (length > 1)
        out << format.end_array_enclosure;
      break;
    }
    case NdbDictionary::Column::Mediumunsigned:
      out << (const Uint32) uint3korr(val_p);
      break;
    case NdbDictionary::Column::Smallunsigned:
      out << *((const Uint16*) val);
      break;
    case NdbDictionary::Column::Tinyunsigned:
      out << *((const Uint8*) val);
      break;
    case NdbDictionary::Column::Bigint:
    {
      Int64 temp;
      memcpy(&temp, val, 8);
      out << temp;
      break;
    }
    case NdbDictionary::Column::Int:
      out << *((Int32*)val);
      break;
    case NdbDictionary::Column::Mediumint:
      out << sint3korr(val_p);
      break;
    case NdbDictionary::Column::Smallint:
      out << *((const short*) val);
      break;
    case NdbDictionary::Column::Tinyint:
      out << *((Int8*) val);
      break;
    case NdbDictionary::Column::Binary:
      if (!format.hex_format)
        out << fields_optionally_enclosed_by;
      j = c->getLength();
      pretty_print_string(out,format,"Binary", true, val, j);
      if (!format.hex_format)
        out << fields_optionally_enclosed_by;
      break;
    case NdbDictionary::Column::Char:
      out << fields_optionally_enclosed_by;
      j = c->getLength();
      pretty_print_string(out,format,"Char", false, val, j);
      out << fields_optionally_enclosed_by;
      break;
    case NdbDictionary::Column::Varchar:
    {
      out << fields_optionally_enclosed_by;
      unsigned len = *val_p;
      pretty_print_string(out,format,"Varchar", false, val_p+1,len);
      j = length;
      out << fields_optionally_enclosed_by;
    }
    break;
    case NdbDictionary::Column::Varbinary:
    {
      if (!format.hex_format)
        out << fields_optionally_enclosed_by;
      unsigned len = *val_p;
      pretty_print_string(out,format,"Varbinary", true, val_p+1,len);
      j = length;
      if (!format.hex_format)
        out << fields_optionally_enclosed_by;
    }
    break;
    case NdbDictionary::Column::Float:
    {
      float temp;
      memcpy(&temp, val, sizeof(temp));
      out << temp;
      break;
    }
    case NdbDictionary::Column::Double:
    {
      double temp;
      memcpy(&temp, val, sizeof(temp));
      out << temp;
      break;
    }
    case NdbDictionary::Column::Olddecimal:
    {
      short len = 1 + c->getPrecision() + (c->getScale() > 0);
      out.print("%.*s", len, val_p);
    }
    break;
    case NdbDictionary::Column::Olddecimalunsigned:
    {
      short len = 0 + c->getPrecision() + (c->getScale() > 0);
      out.print("%.*s", len, val_p);
    }
    break;
    case NdbDictionary::Column::Decimal:
    case NdbDictionary::Column::Decimalunsigned:
    {
      int precision= c->getPrecision();
      int scale= c->getScale();
      
      assert(precision <= MaxMySQLDecimalPrecision);
      assert(scale <= MaxMySQLDecimalScale);
      assert(decimal_size(precision, scale) <= DigitArraySize );
      decimal_digit_t buff[ DigitArraySize ];
      decimal_t tmpDec;
      tmpDec.buf= buff;
      tmpDec.len= DigitArraySize;
      decimal_make_zero(&tmpDec);
      int rc;

      const uchar* data= (const uchar*) val_p;
      if ((rc= bin2decimal(data, &tmpDec, precision, scale)))
      {
        out.print("***Error : Bad bin2decimal conversion %d ***",
                  rc);
        break;
      }
      
      /* Get null terminated var-length string representation */
      char decStr[MaxDecimalStrLen];
      assert(decimal_string_size(&tmpDec) <= MaxDecimalStrLen);
      int len= MaxDecimalStrLen;
      if ((rc= decimal2string(&tmpDec, decStr, 
                              &len,
                              0,   // 0 = Var length output length
                              0,   // 0 = Var length fractional part
                              0))) // Filler char for fixed length
      {
        out.print("***Error : bad decimal2string conversion %d ***",
                  rc);
        break;
      }

      out.print("%s", decStr);
      
      break;
    }
      // for dates cut-and-paste from field.cc
    case NdbDictionary::Column::Datetime:
    {
      ulonglong tmp;
      memcpy(&tmp, val, 8);

      long part1,part2,part3;
      part1=(long) (tmp/LL(1000000));
      part2=(long) (tmp - (ulonglong) part1*LL(1000000));
      char buf[40];
      char* pos=(char*) buf+19;
      *pos--=0;
      *pos--= (char) ('0'+(char) (part2%10)); part2/=10; 
      *pos--= (char) ('0'+(char) (part2%10)); part3= (int) (part2 / 10);
      *pos--= ':';
      *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
      *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
      *pos--= ':';
      *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
      *pos--= (char) ('0'+(char) part3);
      *pos--= '/';
      *pos--= (char) ('0'+(char) (part1%10)); part1/=10;
      *pos--= (char) ('0'+(char) (part1%10)); part1/=10;
      *pos--= '-';
      *pos--= (char) ('0'+(char) (part1%10)); part1/=10;
      *pos--= (char) ('0'+(char) (part1%10)); part3= (int) (part1/10);
      *pos--= '-';
      *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
      *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
      *pos--= (char) ('0'+(char) (part3%10)); part3/=10;
      *pos=(char) ('0'+(char) part3);
      out << buf;
    }
    break;
    case NdbDictionary::Column::Date:
    {
      uint32 tmp=(uint32) uint3korr(val_p);
      int part;
      char buf[40];
      char *pos=(char*) buf+10;
      *pos--=0;
      part=(int) (tmp & 31);
      *pos--= (char) ('0'+part%10);
      *pos--= (char) ('0'+part/10);
      *pos--= '-';
      part=(int) (tmp >> 5 & 15);
      *pos--= (char) ('0'+part%10);
      *pos--= (char) ('0'+part/10);
      *pos--= '-';
      part=(int) (tmp >> 9);
      *pos--= (char) ('0'+part%10); part/=10;
      *pos--= (char) ('0'+part%10); part/=10;
      *pos--= (char) ('0'+part%10); part/=10;
      *pos=   (char) ('0'+part);
      out << buf;
    }
    break;
    case NdbDictionary::Column::Time:
    {
      long tmp=(long) sint3korr(val_p);
      int hour=(uint) (tmp/10000);
      int minute=(uint) (tmp/100 % 100);
      int second=(uint) (tmp % 100);
      char buf[40];
      sprintf(buf, "%02d:%02d:%02d", hour, minute, second);
      out << buf;
    }
    break;
    case NdbDictionary::Column::Year:
    {
      uint year = 1900 + *((const Uint8*) val);
      char buf[40];
      sprintf(buf, "%04d", year);
      out << buf;
    }
    break;
    case NdbDictionary::Column::Timestamp:
    {
      time_t time = *(const Uint32*) val;
      out << (uint)time;
    }
    break;
    case NdbDictionary::Column::Blob:
    case NdbDictionary::Column::Text:
    {
      NdbBlob::Head head;
      NdbBlob::unpackBlobHead(head, (const char*) val_p, c->getBlobVersion());
      out << head.length << ":";
      const unsigned char* p = val_p + head.headsize;
      if ((unsigned int) c->getLength() < head.headsize)
        out << "***error***"; // really cannot happen
      else {
        unsigned n = c->getLength() - head.headsize;
        for (unsigned k = 0; k < n && k < head.length; k++) {
          if (c->getType() == NdbDictionary::Column::Blob)
            out.print("%02X", (int)p[k]);
          else
            out.print("%c", (int)p[k]);
        }
      }
      j = length;
    }
    break;
    case NdbDictionary::Column::Longvarchar:
    {
      out << fields_optionally_enclosed_by;
      unsigned len = uint2korr(val_p);
      pretty_print_string(out,format,"Longvarchar", false, 
                          val_p+2,len);
      j = length;
      out << fields_optionally_enclosed_by;
    }
    break;
    case NdbDictionary::Column::Longvarbinary:
    {
      if (!format.hex_format)
        out << fields_optionally_enclosed_by;
      unsigned len = uint2korr(val_p);
      pretty_print_string(out,format,"Longvarbinary", true, 
                          val_p+2,len);
      j = length;
      if (!format.hex_format)
        out << fields_optionally_enclosed_by;
    }
    break;

    default: /* no print functions for the rest, just print type */
      out << "Unable to format type (" 
          << (int) c->getType()
          << ")";
      if (length > 1)
        out << " " << length << " times";
      break;
    }
    out << format.fields_enclosed_by;
  }
  
  return out;
}

NdbDictionary::NdbDataPrintFormat::NdbDataPrintFormat()
{
  fields_terminated_by= ";";
  start_array_enclosure= "[";
  end_array_enclosure= "]";
  fields_enclosed_by= "";
  fields_optionally_enclosed_by= "\"";
  lines_terminated_by= "\n";
  hex_prefix= "H'";
  null_string= "[NULL]";
  hex_format= 0;
}
NdbDictionary::NdbDataPrintFormat::~NdbDataPrintFormat() {};


NdbOut&
operator<<(NdbOut& out, const NdbDictionary::Column& col)
{
  const CHARSET_INFO *cs = col.getCharset();
  const char *csname = cs ? cs->name : "?";
  out << col.getName() << " ";
  switch (col.getType()) {
  case NdbDictionary::Column::Tinyint:
    out << "Tinyint";
    break;
  case NdbDictionary::Column::Tinyunsigned:
    out << "Tinyunsigned";
    break;
  case NdbDictionary::Column::Smallint:
    out << "Smallint";
    break;
  case NdbDictionary::Column::Smallunsigned:
    out << "Smallunsigned";
    break;
  case NdbDictionary::Column::Mediumint:
    out << "Mediumint";
    break;
  case NdbDictionary::Column::Mediumunsigned:
    out << "Mediumunsigned";
    break;
  case NdbDictionary::Column::Int:
    out << "Int";
    break;
  case NdbDictionary::Column::Unsigned:
    out << "Unsigned";
    break;
  case NdbDictionary::Column::Bigint:
    out << "Bigint";
    break;
  case NdbDictionary::Column::Bigunsigned:
    out << "Bigunsigned";
    break;
  case NdbDictionary::Column::Float:
    out << "Float";
    break;
  case NdbDictionary::Column::Double:
    out << "Double";
    break;
  case NdbDictionary::Column::Olddecimal:
    out << "Olddecimal(" << col.getPrecision() << "," << col.getScale() << ")";
    break;
  case NdbDictionary::Column::Olddecimalunsigned:
    out << "Olddecimalunsigned(" << col.getPrecision() << "," << col.getScale() << ")";
    break;
  case NdbDictionary::Column::Decimal:
    out << "Decimal(" << col.getPrecision() << "," << col.getScale() << ")";
    break;
  case NdbDictionary::Column::Decimalunsigned:
    out << "Decimalunsigned(" << col.getPrecision() << "," << col.getScale() << ")";
    break;
  case NdbDictionary::Column::Char:
    out << "Char(" << col.getLength() << ";" << csname << ")";
    break;
  case NdbDictionary::Column::Varchar:
    out << "Varchar(" << col.getLength() << ";" << csname << ")";
    break;
  case NdbDictionary::Column::Binary:
    out << "Binary(" << col.getLength() << ")";
    break;
  case NdbDictionary::Column::Varbinary:
    out << "Varbinary(" << col.getLength() << ")";
    break;
  case NdbDictionary::Column::Datetime:
    out << "Datetime";
    break;
  case NdbDictionary::Column::Date:
    out << "Date";
    break;
  case NdbDictionary::Column::Blob:
    out << "Blob(" << col.getInlineSize() << "," << col.getPartSize()
        << "," << col.getStripeSize() << ")";
    break;
  case NdbDictionary::Column::Text:
    out << "Text(" << col.getInlineSize() << "," << col.getPartSize()
        << "," << col.getStripeSize() << ";" << csname << ")";
    break;
  case NdbDictionary::Column::Time:
    out << "Time";
    break;
  case NdbDictionary::Column::Year:
    out << "Year";
    break;
  case NdbDictionary::Column::Timestamp:
    out << "Timestamp";
    break;
  case NdbDictionary::Column::Undefined:
    out << "Undefined";
    break;
  case NdbDictionary::Column::Bit:
    out << "Bit(" << col.getLength() << ")";
    break;
  case NdbDictionary::Column::Longvarchar:
    out << "Longvarchar(" << col.getLength() << ";" << csname << ")";
    break;
  case NdbDictionary::Column::Longvarbinary:
    out << "Longvarbinary(" << col.getLength() << ")";
    break;
  default:
    out << "Type" << (Uint32)col.getType();
    break;
  }
  // show unusual (non-MySQL) array size
  if (col.getLength() != 1) {
    switch (col.getType()) {
    case NdbDictionary::Column::Char:
    case NdbDictionary::Column::Varchar:
    case NdbDictionary::Column::Binary:
    case NdbDictionary::Column::Varbinary:
    case NdbDictionary::Column::Blob:
    case NdbDictionary::Column::Text:
    case NdbDictionary::Column::Bit:
    case NdbDictionary::Column::Longvarchar:
    case NdbDictionary::Column::Longvarbinary:
      break;
    default:
      out << " [" << col.getLength() << "]";
      break;
    }
  }

  if (col.getPrimaryKey())
    out << " PRIMARY KEY";
  else if (! col.getNullable())
    out << " NOT NULL";
  else
    out << " NULL";

  if(col.getDistributionKey())
    out << " DISTRIBUTION KEY";

  switch (col.getArrayType()) {
  case NDB_ARRAYTYPE_FIXED:
    out << " AT=FIXED";
    break;
  case NDB_ARRAYTYPE_SHORT_VAR:
    out << " AT=SHORT_VAR";
    break;
  case NDB_ARRAYTYPE_MEDIUM_VAR:
    out << " AT=MEDIUM_VAR";
    break;
  default:
    out << " AT=" << (int)col.getArrayType() << "?";
    break;
  }

  switch (col.getStorageType()) {
  case NDB_STORAGETYPE_MEMORY:
    out << " ST=MEMORY";
    break;
  case NDB_STORAGETYPE_DISK:
    out << " ST=DISK";
    break;
  default:
    out << " ST=" << (int)col.getStorageType() << "?";
    break;
  }

  if (col.getAutoIncrement())
    out << " AUTO_INCR";

  switch (col.getType()) {
  case NdbDictionary::Column::Blob:
  case NdbDictionary::Column::Text:
    out << " BV=" << col.getBlobVersion();
    out << " BT=" << ((col.getBlobTable() != 0) ? col.getBlobTable()->getName() : "<none>");
    break;
  default:
    break;
  }

  if(col.getDynamic())
    out << " DYNAMIC";

  const void *default_data = col.getDefaultValue();

  if (default_data != NULL)
  {
    NdbDictionary::NdbDataPrintFormat f;

    /* Display binary field defaults as hex */
    f.hex_format = 1;

    out << " DEFAULT ";
    
    NdbDictionary::printFormattedValue(out,
                                       f,
                                       &col,
                                       default_data);
  }
  
  return out;
}

int
NdbDictionary::Dictionary::createLogfileGroup(const LogfileGroup & lg,
					      ObjectId * obj)
{
  int ret;
  DO_TRANS(ret,
           m_impl.createLogfileGroup(NdbLogfileGroupImpl::getImpl(lg),
                                     obj ?
                                     & NdbDictObjectImpl::getImpl(* obj) : 0));
  return ret;
}

int
NdbDictionary::Dictionary::dropLogfileGroup(const LogfileGroup & lg)
{
  int ret;
  DO_TRANS(ret,
           m_impl.dropLogfileGroup(NdbLogfileGroupImpl::getImpl(lg)));
  return ret;
}

NdbDictionary::LogfileGroup
NdbDictionary::Dictionary::getLogfileGroup(const char * name)
{
  NdbDictionary::LogfileGroup tmp;
  m_impl.m_receiver.get_filegroup(NdbLogfileGroupImpl::getImpl(tmp), 
				  NdbDictionary::Object::LogfileGroup, name);
  return tmp;
}

int
NdbDictionary::Dictionary::createTablespace(const Tablespace & lg,
					    ObjectId * obj)
{
  int ret;
  DO_TRANS(ret,
           m_impl.createTablespace(NdbTablespaceImpl::getImpl(lg),
                                   obj ?
                                   & NdbDictObjectImpl::getImpl(* obj) : 0));
  return ret;
}

int
NdbDictionary::Dictionary::dropTablespace(const Tablespace & lg)
{
  int ret;
  DO_TRANS(ret,
           m_impl.dropTablespace(NdbTablespaceImpl::getImpl(lg)));
  return ret;
}

NdbDictionary::Tablespace
NdbDictionary::Dictionary::getTablespace(const char * name)
{
  NdbDictionary::Tablespace tmp;
  m_impl.m_receiver.get_filegroup(NdbTablespaceImpl::getImpl(tmp), 
				  NdbDictionary::Object::Tablespace, name);
  return tmp;
}

NdbDictionary::Tablespace
NdbDictionary::Dictionary::getTablespace(Uint32 tablespaceId)
{
  NdbDictionary::Tablespace tmp;
  m_impl.m_receiver.get_filegroup(NdbTablespaceImpl::getImpl(tmp), 
				  NdbDictionary::Object::Tablespace,
                                  tablespaceId);
  return tmp;
}

int
NdbDictionary::Dictionary::createDatafile(const Datafile & df, 
					  bool force,
					  ObjectId * obj)
{
  int ret;
  DO_TRANS(ret,
           m_impl.createDatafile(NdbDatafileImpl::getImpl(df),
                                 force,
                                 obj ? & NdbDictObjectImpl::getImpl(* obj): 0));
  return ret;
}

int
NdbDictionary::Dictionary::dropDatafile(const Datafile& df)
{
  int ret;
  DO_TRANS(ret,
           m_impl.dropDatafile(NdbDatafileImpl::getImpl(df)));
  return ret;
}

NdbDictionary::Datafile
NdbDictionary::Dictionary::getDatafile(Uint32 node, const char * path)
{
  NdbDictionary::Datafile tmp;
  m_impl.m_receiver.get_file(NdbDatafileImpl::getImpl(tmp),
			     NdbDictionary::Object::Datafile,
			     node ? (int)node : -1, path);
  return tmp;
}

int
NdbDictionary::Dictionary::createUndofile(const Undofile & df, 
					  bool force,
					  ObjectId * obj)
{
  int ret;
  DO_TRANS(ret,
           m_impl.createUndofile(NdbUndofileImpl::getImpl(df),
                                 force,
                                 obj ? & NdbDictObjectImpl::getImpl(* obj): 0));
  return ret;
}

int
NdbDictionary::Dictionary::dropUndofile(const Undofile& df)
{
  int ret;
  DO_TRANS(ret,
           m_impl.dropUndofile(NdbUndofileImpl::getImpl(df)));
  return ret;
}

NdbDictionary::Undofile
NdbDictionary::Dictionary::getUndofile(Uint32 node, const char * path)
{
  NdbDictionary::Undofile tmp;
  m_impl.m_receiver.get_file(NdbUndofileImpl::getImpl(tmp),
			     NdbDictionary::Object::Undofile,
			     node ? (int)node : -1, path);
  return tmp;
}

int
NdbDictionary::Dictionary::beginSchemaTrans()
{
  return m_impl.beginSchemaTrans();
}

int
NdbDictionary::Dictionary::endSchemaTrans(Uint32 flags)
{
  return m_impl.endSchemaTrans(flags);
}

bool
NdbDictionary::Dictionary::hasSchemaTrans() const
{
  return m_impl.hasSchemaTrans();
}

int
NdbDictionary::Dictionary::createHashMap(const HashMap& map, ObjectId * dst)
{
  ObjectId tmp;
  if (dst == 0)
    dst = &tmp;

  int ret;
  DO_TRANS(ret,
           m_impl.m_receiver.create_hashmap(NdbHashMapImpl::getImpl(map),
                                            &NdbDictObjectImpl::getImpl(*dst),
                                            0));
  return ret;
}
