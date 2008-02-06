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

#include <ndb_global.h>
#include <my_sys.h>

#define DBDICT_C
#include "Dbdict.hpp"
#include "diskpage.hpp"

#include <ndb_limits.h>
#include <NdbOut.hpp>
#include <OutputStream.hpp>
#include <Properties.hpp>
#include <Configuration.hpp>
#include <SectionReader.hpp>
#include <SimpleProperties.hpp>
#include <AttributeHeader.hpp>
#include <KeyDescriptor.hpp>
#include <signaldata/DictSchemaInfo.hpp>
#include <signaldata/DictTabInfo.hpp>
#include <signaldata/DropTabFile.hpp>

#include <signaldata/EventReport.hpp>
#include <signaldata/FsCloseReq.hpp>
#include <signaldata/FsConf.hpp>
#include <signaldata/FsOpenReq.hpp>
#include <signaldata/FsReadWriteReq.hpp>
#include <signaldata/FsRef.hpp>
#include <signaldata/GetTabInfo.hpp>
#include <signaldata/GetTableId.hpp>
#include <signaldata/HotSpareRep.hpp>
#include <signaldata/NFCompleteRep.hpp>
#include <signaldata/NodeFailRep.hpp>
#include <signaldata/ReadNodesConf.hpp>
#include <signaldata/RelTabMem.hpp>
#include <signaldata/WaitGCP.hpp>
#include <signaldata/ListTables.hpp>

#include <signaldata/CreateTrig.hpp>
#include <signaldata/DropTrig.hpp>
#include <signaldata/CreateIndx.hpp>
#include <signaldata/DropIndx.hpp>
#include <signaldata/BuildIndx.hpp>

#include <signaldata/DropFilegroup.hpp>
#include <signaldata/CreateFilegroup.hpp>
#include <signaldata/CreateFilegroupImpl.hpp>

#include <signaldata/CreateEvnt.hpp>
#include <signaldata/UtilPrepare.hpp>
#include <signaldata/UtilExecute.hpp>
#include <signaldata/UtilRelease.hpp>
#include <signaldata/SumaImpl.hpp> 

#include <signaldata/LqhFrag.hpp>

#include <signaldata/DiAddTab.hpp>
#include <signaldata/DihStartTab.hpp>

#include <signaldata/DropTable.hpp>
#include <signaldata/DropTab.hpp>
#include <signaldata/PrepDropTab.hpp>

#include <signaldata/CreateTable.hpp>
#include <signaldata/AlterTable.hpp>
#include <signaldata/AlterTab.hpp>
#include <signaldata/CreateFragmentation.hpp>
#include <signaldata/CreateTab.hpp>
#include <NdbSleep.h>
#include <signaldata/ApiBroadcast.hpp>

#include <signaldata/DropObj.hpp>
#include <signaldata/CreateObj.hpp>
#include <signaldata/BackupLockTab.hpp>
#include <SLList.hpp>

#include <EventLogger.hpp>
extern EventLogger g_eventLogger;

#include <signaldata/SchemaTrans.hpp>
#include <DebuggerNames.hpp>

// TransporterCallback.cpp
extern void release(SegmentedSectionPtr&);

#define ZNOT_FOUND 626
#define ZALREADYEXIST 630

//#define EVENT_PH2_DEBUG
//#define EVENT_PH3_DEBUG
//#define EVENT_DEBUG

static const char EVENT_SYSTEM_TABLE_NAME[] = "sys/def/NDB$EVENTS_0";

#define EVENT_TRACE \
//  ndbout_c("Event debug trace: File: %s Line: %u", __FILE__, __LINE__)

#define DIV(x,y) (((x)+(y)-1)/(y))
#define WORDS2PAGES(x) DIV(x, (ZSIZE_OF_PAGES_IN_WORDS - ZPAGE_HEADER_SIZE))
#include <ndb_version.h>

static
struct {
  Uint32 m_gsn_user_req;
  Uint32 m_gsn_req;
  Uint32 m_gsn_ref;
  Uint32 m_gsn_conf;
  void (Dbdict::* m_trans_commit_start)(Signal*, Dbdict::SchemaTransaction*);
  void (Dbdict::* m_trans_commit_complete)(Signal*,Dbdict::SchemaTransaction*);
  void (Dbdict::* m_trans_abort_start)(Signal*, Dbdict::SchemaTransaction*);
  void (Dbdict::* m_trans_abort_complete)(Signal*, Dbdict::SchemaTransaction*);

  void (Dbdict::* m_prepare_start)(Signal*, Dbdict::SchemaOperation*);
  void (Dbdict::* m_prepare_complete)(Signal*, Dbdict::SchemaOperation*);
  void (Dbdict::* m_commit)(Signal*, Dbdict::SchemaOperation*);
  void (Dbdict::* m_commit_start)(Signal*, Dbdict::SchemaOperation*);
  void (Dbdict::* m_commit_complete)(Signal*, Dbdict::SchemaOperation*);
  void (Dbdict::* m_abort)(Signal*, Dbdict::SchemaOperation*);
  void (Dbdict::* m_abort_start)(Signal*, Dbdict::SchemaOperation*);
  void (Dbdict::* m_abort_complete)(Signal*, Dbdict::SchemaOperation*);
  
} f_dict_op[] = {
  /**
   * Create filegroup
   */
  { 
    GSN_CREATE_FILEGROUP_REQ,
    GSN_CREATE_OBJ_REQ, GSN_CREATE_OBJ_REF, GSN_CREATE_OBJ_CONF,
    0, 0, 0, 0,
    &Dbdict::create_fg_prepare_start, &Dbdict::create_fg_prepare_complete,
    &Dbdict::createObj_commit,
    0, 0,
    &Dbdict::createObj_abort,
    &Dbdict::create_fg_abort_start, &Dbdict::create_fg_abort_complete,
  }

  /**
   * Create file
   */
  ,{ 
    GSN_CREATE_FILE_REQ,
    GSN_CREATE_OBJ_REQ, GSN_CREATE_OBJ_REF, GSN_CREATE_OBJ_CONF,
    0, 0, 0, 0,
    &Dbdict::create_file_prepare_start, &Dbdict::create_file_prepare_complete,
    &Dbdict::createObj_commit,
    &Dbdict::create_file_commit_start, 0,
    &Dbdict::createObj_abort,
    &Dbdict::create_file_abort_start, &Dbdict::create_file_abort_complete,
  }

  /**
   * Drop file
   */
  ,{ 
    GSN_DROP_FILE_REQ,
    GSN_DROP_OBJ_REQ, GSN_DROP_OBJ_REF, GSN_DROP_OBJ_CONF,
    0, 0, 0, 0,
    &Dbdict::drop_file_prepare_start, 0,
    &Dbdict::dropObj_commit,
    &Dbdict::drop_file_commit_start, &Dbdict::drop_file_commit_complete,
    &Dbdict::dropObj_abort,
    &Dbdict::drop_file_abort_start, 0
  }

  /**
   * Drop filegroup
   */
  ,{ 
    GSN_DROP_FILEGROUP_REQ,
    GSN_DROP_OBJ_REQ, GSN_DROP_OBJ_REF, GSN_DROP_OBJ_CONF,
    0, 0, 0, 0,
    &Dbdict::drop_fg_prepare_start, 0,
    &Dbdict::dropObj_commit,
    &Dbdict::drop_fg_commit_start, &Dbdict::drop_fg_commit_complete,
    &Dbdict::dropObj_abort,
    &Dbdict::drop_fg_abort_start, 0
  }

  /**
   * Drop undofile
   */
  ,{ 
    GSN_DROP_FILE_REQ,
    GSN_DROP_OBJ_REQ, GSN_DROP_OBJ_REF, GSN_DROP_OBJ_CONF,
    0, 0, 0, 0,
    &Dbdict::drop_undofile_prepare_start, 0,
    0,
    0, &Dbdict::drop_undofile_commit_complete,
    0, 0, 0
  }
};

Uint32
alter_obj_inc_schema_version(Uint32 old)
{
   return (old & 0x00FFFFFF) + ((old + 0x1000000) & 0xFF000000);
}

#if 0
static
Uint32
alter_obj_dec_schema_version(Uint32 old)
{
  return (old & 0x00FFFFFF) + ((old - 0x1000000) & 0xFF000000);
}
#endif

static
Uint32
create_obj_inc_schema_version(Uint32 old)
{
  return (old + 0x00000001) & 0x00FFFFFF;
}

/* **************************************************************** */
/* ---------------------------------------------------------------- */
/* MODULE:          GENERAL MODULE -------------------------------- */
/* ---------------------------------------------------------------- */
/*                                                                  */
/* This module contains general stuff. Mostly debug signals and     */
/* general signals that go into a specific module after checking a  */
/* state variable. Also general subroutines used by many.           */
/* ---------------------------------------------------------------- */
/* **************************************************************** */

/* ---------------------------------------------------------------- */
// This signal is used to dump states of various variables in the
// block by command.
/* ---------------------------------------------------------------- */
void
Dbdict::execDUMP_STATE_ORD(Signal* signal)
{
  jamEntry();

#ifdef VM_TRACE
  if(signal->theData[0] == 1222){
    const Uint32 tab = signal->theData[1];
    PrepDropTabReq* req = (PrepDropTabReq*)signal->getDataPtr();
    req->senderRef = reference();
    req->senderData = 1222;
    req->tableId = tab;
    sendSignal(DBLQH_REF, GSN_PREP_DROP_TAB_REQ, signal,
	       PrepDropTabReq::SignalLength, JBB);
  }

  if(signal->theData[0] == 1223){
    const Uint32 tab = signal->theData[1];
    PrepDropTabReq* req = (PrepDropTabReq*)signal->getDataPtr();
    req->senderRef = reference();
    req->senderData = 1222;
    req->tableId = tab;
    sendSignal(DBTC_REF, GSN_PREP_DROP_TAB_REQ, signal,
	       PrepDropTabReq::SignalLength, JBB);
  }

  if(signal->theData[0] == 1224){
    const Uint32 tab = signal->theData[1];
    PrepDropTabReq* req = (PrepDropTabReq*)signal->getDataPtr();
    req->senderRef = reference();
    req->senderData = 1222;
    req->tableId = tab;
    sendSignal(DBDIH_REF, GSN_PREP_DROP_TAB_REQ, signal,
	       PrepDropTabReq::SignalLength, JBB);
  }

  if(signal->theData[0] == 1225){
    const Uint32 tab = signal->theData[1];
    const Uint32 ver = signal->theData[2];
    TableRecordPtr tabRecPtr;
    c_tableRecordPool.getPtr(tabRecPtr, tab);
    DropTableReq * req = (DropTableReq*)signal->getDataPtr();
    req->senderData = 1225;
    req->senderRef = numberToRef(1,1);
    req->tableId = tab;
    req->tableVersion = tabRecPtr.p->tableVersion + ver;
    sendSignal(DBDICT_REF, GSN_DROP_TABLE_REQ, signal,
	       DropTableReq::SignalLength, JBB);
  }
#endif  
#define MEMINFO(x, y) infoEvent(x ": %d %d", y.getSize(), y.getNoOfFree())
  if(signal->theData[0] == 1226){
    MEMINFO("c_obj_pool", c_obj_pool);
    MEMINFO("c_opRecordPool", c_opRecordPool);
    MEMINFO("c_rope_pool", c_rope_pool);
  }

  if (signal->theData[0] == 1227)
  {
    DLHashTable<DictObject>::Iterator iter;
    bool ok = c_obj_hash.first(iter);
    for(; ok; ok = c_obj_hash.next(iter))
    {
      Rope name(c_rope_pool, iter.curr.p->m_name);
      char buf[1024];
      name.copy(buf);
      ndbout_c("%s m_ref_count: %d", buf, iter.curr.p->m_ref_count); 
      if (iter.curr.p->m_trans_key != 0)
        ndbout_c("- m_trans_key: %u m_op_ref_count: %u",
                 iter.curr.p->m_trans_key, iter.curr.p->m_op_ref_count);
    }
  }    
  
  return;
}//Dbdict::execDUMP_STATE_ORD()

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
// CONTINUEB is used when a real-time break is needed for long
// processes.
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
void Dbdict::execCONTINUEB(Signal* signal) 
{
  jamEntry();
  switch (signal->theData[0]) {
  case ZPACK_TABLE_INTO_PAGES :
    jam();
    packTableIntoPages(signal);
    break;

  case ZSEND_GET_TAB_RESPONSE :
    jam();
    sendGetTabResponse(signal);
    break;

#ifdef VM_TRACE
  case 6103: // search for it
    jam();
    {
      Uint32* data = &signal->theData[0];
      Uint32 masterRef = data[1];
      memmove(&data[0], &data[2], SchemaTransImplConf::SignalLength << 2);
      sendSignal(masterRef, GSN_SCHEMA_TRANS_IMPL_CONF, signal,
                 SchemaTransImplConf::SignalLength, JBB);
    }
    break;
#endif

  default :
    ndbrequire(false);
    break;
  }//switch
  return;
}//execCONTINUEB()

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
// Routine to handle pack table into pages.
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

void Dbdict::packTableIntoPages(Signal* signal)
{
  const Uint32 tableId= signal->theData[1];
  const Uint32 type= signal->theData[2];
  const Uint32 pageId= signal->theData[3];

  PageRecordPtr pagePtr;
  c_pageRecordArray.getPtr(pagePtr, pageId);

  memset(&pagePtr.p->word[0], 0, 4 * ZPAGE_HEADER_SIZE);
  LinearWriter w(&pagePtr.p->word[ZPAGE_HEADER_SIZE], 
		 ZMAX_PAGES_OF_TABLE_DEFINITION * ZSIZE_OF_PAGES_IN_WORDS);
  w.first();
  switch((DictTabInfo::TableType)type) {
  case DictTabInfo::SystemTable:
  case DictTabInfo::UserTable:
  case DictTabInfo::UniqueHashIndex:
  case DictTabInfo::HashIndex:
  case DictTabInfo::UniqueOrderedIndex:
  case DictTabInfo::OrderedIndex:{
    jam();
    TableRecordPtr tablePtr;
    c_tableRecordPool.getPtr(tablePtr, tableId);
    packTableIntoPages(w, tablePtr, signal);
    break;
  }
  case DictTabInfo::Tablespace:
  case DictTabInfo::LogfileGroup:{
    FilegroupPtr fg_ptr;
    ndbrequire(c_filegroup_hash.find(fg_ptr, tableId));
    const Uint32 free_hi= signal->theData[4];
    const Uint32 free_lo= signal->theData[5];
    packFilegroupIntoPages(w, fg_ptr, free_hi, free_lo);
    break;
  }
  case DictTabInfo::Datafile:{
    FilePtr fg_ptr;
    ndbrequire(c_file_hash.find(fg_ptr, tableId));
    const Uint32 free_extents= signal->theData[4];
    packFileIntoPages(w, fg_ptr, free_extents);
    break;
  }
  case DictTabInfo::Undofile:{
    FilePtr fg_ptr;
    ndbrequire(c_file_hash.find(fg_ptr, tableId));
    packFileIntoPages(w, fg_ptr, 0);
    break;
  }
  case DictTabInfo::UndefTableType:
  case DictTabInfo::HashIndexTrigger:
  case DictTabInfo::SubscriptionTrigger:
  case DictTabInfo::ReadOnlyConstraint:
  case DictTabInfo::IndexTrigger:
    ndbrequire(false);
  }
  
  Uint32 wordsOfTable = w.getWordsUsed();
  Uint32 pagesUsed = WORDS2PAGES(wordsOfTable);
  pagePtr.p->word[ZPOS_CHECKSUM] = 
    computeChecksum(&pagePtr.p->word[0], pagesUsed * ZSIZE_OF_PAGES_IN_WORDS);
  
  switch (c_packTable.m_state) {
  case PackTable::PTS_IDLE:
    ndbrequire(false);
    break;
  case PackTable::PTS_GET_TAB:
    jam();
    c_retrieveRecord.retrievedNoOfPages = pagesUsed;
    c_retrieveRecord.retrievedNoOfWords = wordsOfTable;
    sendGetTabResponse(signal);
    return;
    break;
  }//switch
  ndbrequire(false);
  return;
}//packTableIntoPages()

void
Dbdict::packTableIntoPages(SimpleProperties::Writer & w,
			       TableRecordPtr tablePtr,
			       Signal* signal){
  
  union {
    char tableName[MAX_TAB_NAME_SIZE];
    char frmData[MAX_FRM_DATA_SIZE];
    char rangeData[16*MAX_NDB_PARTITIONS];
    char ngData[2*MAX_NDB_PARTITIONS];
    char tsData[2*2*MAX_NDB_PARTITIONS];
    char defaultValue[MAX_ATTR_DEFAULT_VALUE_SIZE];
    char attributeName[MAX_ATTR_NAME_SIZE];
  };
  ConstRope r(c_rope_pool, tablePtr.p->tableName);
  r.copy(tableName);
  w.add(DictTabInfo::TableName, tableName);
  w.add(DictTabInfo::TableId, tablePtr.i);
  w.add(DictTabInfo::TableVersion, tablePtr.p->tableVersion);
  w.add(DictTabInfo::NoOfKeyAttr, tablePtr.p->noOfPrimkey);
  w.add(DictTabInfo::NoOfAttributes, tablePtr.p->noOfAttributes);
  w.add(DictTabInfo::NoOfNullable, tablePtr.p->noOfNullAttr);
  w.add(DictTabInfo::NoOfVariable, (Uint32)0);
  w.add(DictTabInfo::KeyLength, tablePtr.p->tupKeyLength);
  
  w.add(DictTabInfo::TableLoggedFlag, 
	!!(tablePtr.p->m_bits & TableRecord::TR_Logged));
  w.add(DictTabInfo::RowGCIFlag, 
	!!(tablePtr.p->m_bits & TableRecord::TR_RowGCI));
  w.add(DictTabInfo::RowChecksumFlag, 
	!!(tablePtr.p->m_bits & TableRecord::TR_RowChecksum));
  w.add(DictTabInfo::TableTemporaryFlag, 
	!!(tablePtr.p->m_bits & TableRecord::TR_Temporary));
  w.add(DictTabInfo::ForceVarPartFlag,
	!!(tablePtr.p->m_bits & TableRecord::TR_ForceVarPart));
  
  w.add(DictTabInfo::MinLoadFactor, tablePtr.p->minLoadFactor);
  w.add(DictTabInfo::MaxLoadFactor, tablePtr.p->maxLoadFactor);
  w.add(DictTabInfo::TableKValue, tablePtr.p->kValue);
  w.add(DictTabInfo::FragmentTypeVal, tablePtr.p->fragmentType);
  w.add(DictTabInfo::TableTypeVal, tablePtr.p->tableType);
  w.add(DictTabInfo::MaxRowsLow, tablePtr.p->maxRowsLow);
  w.add(DictTabInfo::MaxRowsHigh, tablePtr.p->maxRowsHigh);
  w.add(DictTabInfo::DefaultNoPartFlag, tablePtr.p->defaultNoPartFlag);
  w.add(DictTabInfo::LinearHashFlag, tablePtr.p->linearHashFlag);
  w.add(DictTabInfo::FragmentCount, tablePtr.p->fragmentCount);
  w.add(DictTabInfo::MinRowsLow, tablePtr.p->minRowsLow);
  w.add(DictTabInfo::MinRowsHigh, tablePtr.p->minRowsHigh);
  w.add(DictTabInfo::SingleUserMode, tablePtr.p->singleUserMode);

  if(signal)
  {
    /* This branch is run at GET_TABINFOREQ */

    Uint32 * theData = signal->getDataPtrSend();
    CreateFragmentationReq * const req = (CreateFragmentationReq*)theData;
    req->senderRef = 0;
    req->senderData = RNIL;
    req->fragmentationType = tablePtr.p->fragmentType;
    req->noOfFragments = 0;
    req->primaryTableId = tablePtr.i;
    EXECUTE_DIRECT(DBDICT, GSN_CREATE_FRAGMENTATION_REQ, signal,
                   CreateFragmentationReq::SignalLength);
    ndbrequire(signal->theData[0] == 0);
    Uint16 *data = (Uint16*)&signal->theData[25];
    Uint32 count = 2 + (1 + data[0]) * data[1];
    w.add(DictTabInfo::ReplicaDataLen, 2*count);
    for (Uint32 i = 0; i < count; i++)
      data[i] = htons(data[i]);
    w.add(DictTabInfo::ReplicaData, data, 2*count);
  }
  else
  {
    /* This part is run at CREATE_TABLEREQ, ALTER_TABLEREQ */
    ;
  }
  
  if (tablePtr.p->primaryTableId != RNIL)
  {
    jam();
    TableRecordPtr primTab;
    c_tableRecordPool.getPtr(primTab, tablePtr.p->primaryTableId);
    ConstRope r2(c_rope_pool, primTab.p->tableName);
    r2.copy(tableName);
    w.add(DictTabInfo::PrimaryTable, tableName);
    w.add(DictTabInfo::PrimaryTableId, tablePtr.p->primaryTableId);
    w.add(DictTabInfo::IndexState, tablePtr.p->indexState);
    w.add(DictTabInfo::CustomTriggerId, tablePtr.p->triggerId);
  }

  ConstRope frm(c_rope_pool, tablePtr.p->frmData);
  frm.copy(frmData);
  w.add(DictTabInfo::FrmLen, frm.size());
  w.add(DictTabInfo::FrmData, frmData, frm.size());

  {
    jam();
    ConstRope ts(c_rope_pool, tablePtr.p->tsData);
    ts.copy(tsData);
    w.add(DictTabInfo::TablespaceDataLen, ts.size());
    w.add(DictTabInfo::TablespaceData, tsData, ts.size());

    ConstRope ng(c_rope_pool, tablePtr.p->ngData);
    ng.copy(ngData);
    w.add(DictTabInfo::FragmentDataLen, ng.size());
    w.add(DictTabInfo::FragmentData, ngData, ng.size());

    ConstRope range(c_rope_pool, tablePtr.p->rangeData);
    range.copy(rangeData);
    w.add(DictTabInfo::RangeListDataLen, range.size());
    w.add(DictTabInfo::RangeListData, rangeData, range.size());
  }

  if(tablePtr.p->m_tablespace_id != RNIL)
  {
    w.add(DictTabInfo::TablespaceId, tablePtr.p->m_tablespace_id);
    FilegroupPtr tsPtr;
    ndbrequire(c_filegroup_hash.find(tsPtr, tablePtr.p->m_tablespace_id));
    w.add(DictTabInfo::TablespaceVersion, tsPtr.p->m_version);
  }

  AttributeRecordPtr attrPtr;
  LocalDLFifoList<AttributeRecord> list(c_attributeRecordPool, 
				    tablePtr.p->m_attributes);
  for(list.first(attrPtr); !attrPtr.isNull(); list.next(attrPtr)){
    jam();

    ConstRope name(c_rope_pool, attrPtr.p->attributeName);
    name.copy(attributeName);

    w.add(DictTabInfo::AttributeName, attributeName);
    w.add(DictTabInfo::AttributeId, attrPtr.p->attributeId);
    w.add(DictTabInfo::AttributeKeyFlag, attrPtr.p->tupleKey > 0);
    
    const Uint32 desc = attrPtr.p->attributeDescriptor;
    const Uint32 attrType = AttributeDescriptor::getType(desc);
    const Uint32 attrSize = AttributeDescriptor::getSize(desc);
    const Uint32 arraySize = AttributeDescriptor::getArraySize(desc);
    const Uint32 arrayType = AttributeDescriptor::getArrayType(desc);
    const Uint32 nullable = AttributeDescriptor::getNullable(desc);
    const Uint32 DKey = AttributeDescriptor::getDKey(desc);
    const Uint32 disk= AttributeDescriptor::getDiskBased(desc);
    const Uint32 dynamic= AttributeDescriptor::getDynamic(desc);
    

    // AttributeType deprecated
    w.add(DictTabInfo::AttributeSize, attrSize);
    w.add(DictTabInfo::AttributeArraySize, arraySize);
    w.add(DictTabInfo::AttributeArrayType, arrayType);
    w.add(DictTabInfo::AttributeNullableFlag, nullable);
    w.add(DictTabInfo::AttributeDynamic, dynamic);
    w.add(DictTabInfo::AttributeDKey, DKey);
    w.add(DictTabInfo::AttributeExtType, attrType);
    w.add(DictTabInfo::AttributeExtPrecision, attrPtr.p->extPrecision);
    w.add(DictTabInfo::AttributeExtScale, attrPtr.p->extScale);
    w.add(DictTabInfo::AttributeExtLength, attrPtr.p->extLength);
    w.add(DictTabInfo::AttributeAutoIncrement, 
	  (Uint32)attrPtr.p->autoIncrement);

    if(disk)
      w.add(DictTabInfo::AttributeStorageType, (Uint32)NDB_STORAGETYPE_DISK);
    else
      w.add(DictTabInfo::AttributeStorageType, (Uint32)NDB_STORAGETYPE_MEMORY);
    
    ConstRope def(c_rope_pool, attrPtr.p->defaultValue);
    def.copy(defaultValue);
    w.add(DictTabInfo::AttributeDefaultValue, defaultValue);
    
    w.add(DictTabInfo::AttributeEnd, 1);
  }
  
  w.add(DictTabInfo::TableEnd, 1);
}

void
Dbdict::packFilegroupIntoPages(SimpleProperties::Writer & w,
			       FilegroupPtr fg_ptr,
			       const Uint32 undo_free_hi,
			       const Uint32 undo_free_lo){
  
  DictFilegroupInfo::Filegroup fg; fg.init();
  ConstRope r(c_rope_pool, fg_ptr.p->m_name);
  r.copy(fg.FilegroupName);

  fg.FilegroupId = fg_ptr.p->key;
  fg.FilegroupType = fg_ptr.p->m_type;
  fg.FilegroupVersion = fg_ptr.p->m_version;

  switch(fg.FilegroupType){
  case DictTabInfo::Tablespace:
    //fg.TS_DataGrow = group.m_grow_spec;
    fg.TS_ExtentSize = fg_ptr.p->m_tablespace.m_extent_size;
    fg.TS_LogfileGroupId = fg_ptr.p->m_tablespace.m_default_logfile_group_id;
    FilegroupPtr lfg_ptr;
    ndbrequire(c_filegroup_hash.find(lfg_ptr, fg.TS_LogfileGroupId));
    fg.TS_LogfileGroupVersion = lfg_ptr.p->m_version;
    break;
  case DictTabInfo::LogfileGroup:
    fg.LF_UndoBufferSize = fg_ptr.p->m_logfilegroup.m_undo_buffer_size;
    fg.LF_UndoFreeWordsHi= undo_free_hi;
    fg.LF_UndoFreeWordsLo= undo_free_lo;
    //fg.LF_UndoGrow = ;
    break;
  default:
    ndbrequire(false);
  }
  
  SimpleProperties::UnpackStatus s;
  s = SimpleProperties::pack(w, 
			     &fg,
			     DictFilegroupInfo::Mapping, 
			     DictFilegroupInfo::MappingSize, true);
  
  ndbrequire(s == SimpleProperties::Eof);
}

void
Dbdict::packFileIntoPages(SimpleProperties::Writer & w,
			  FilePtr f_ptr, const Uint32 free_extents){
  
  DictFilegroupInfo::File f; f.init();
  ConstRope r(c_rope_pool, f_ptr.p->m_path);
  r.copy(f.FileName);

  f.FileType = f_ptr.p->m_type;
  f.FilegroupId = f_ptr.p->m_filegroup_id;; //group.m_id;
  f.FileSizeHi = (f_ptr.p->m_file_size >> 32);
  f.FileSizeLo = (f_ptr.p->m_file_size & 0xFFFFFFFF);
  f.FileFreeExtents= free_extents;
  f.FileId =  f_ptr.p->key;
  f.FileVersion = f_ptr.p->m_version;

  FilegroupPtr lfg_ptr;
  ndbrequire(c_filegroup_hash.find(lfg_ptr, f.FilegroupId));
  f.FilegroupVersion = lfg_ptr.p->m_version;

  SimpleProperties::UnpackStatus s;
  s = SimpleProperties::pack(w, 
			     &f,
			     DictFilegroupInfo::FileMapping, 
			     DictFilegroupInfo::FileMappingSize, true);
  
  ndbrequire(s == SimpleProperties::Eof);
}

void
Dbdict::execCREATE_FRAGMENTATION_REQ(Signal* signal)
{
  const CreateFragmentationReq* req =
    (const CreateFragmentationReq*)signal->getDataPtr();

  if (req->primaryTableId == RNIL) {
    jam();
    EXECUTE_DIRECT(DBDIH, GSN_CREATE_FRAGMENTATION_REQ, signal,
                   CreateFragmentationReq::SignalLength);
    return;
  }

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, req->primaryTableId);
  if (tablePtr.p->tabState == TableRecord::DEFINED ||
      tablePtr.p->tabState == TableRecord::BACKUP_ONGOING) {
    jam();
    EXECUTE_DIRECT(DBDIH, GSN_CREATE_FRAGMENTATION_REQ, signal,
                   CreateFragmentationReq::SignalLength);
    return;
  }

  DictObjectPtr obj_ptr;
  c_obj_pool.getPtr(obj_ptr, tablePtr.p->m_obj_ptr_i);

  SchemaOpPtr op_ptr;
  findDictObjectOp(op_ptr, obj_ptr);
  ndbrequire(!op_ptr.isNull());
  OpRecPtr oprec_ptr = op_ptr.p->m_oprec_ptr;
  ndbrequire(memcmp(oprec_ptr.p->m_opType, "CTa", 4) == 0);

  Uint32 *theData = &signal->theData[0];
  const OpSection& fragSection =
    getOpSection(op_ptr, CreateTabReq::FRAGMENTATION);
  copyOut(fragSection, &theData[25], ZNIL);
  theData[0] = 0;
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
// The routines to handle responses from file system.
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

/* ---------------------------------------------------------------- */
// A file was successfully closed.
/* ---------------------------------------------------------------- */
void Dbdict::execFSCLOSECONF(Signal* signal) 
{
  FsConnectRecordPtr fsPtr;
  FsConf * const fsConf = (FsConf *)&signal->theData[0];
  jamEntry();
  c_fsConnectRecordPool.getPtr(fsPtr, fsConf->userPointer);
  switch (fsPtr.p->fsState) {
  case FsConnectRecord::CLOSE_WRITE_SCHEMA:
    jam();
    closeWriteSchemaConf(signal, fsPtr);
    break;
  case FsConnectRecord::CLOSE_READ_SCHEMA:
    jam();
    closeReadSchemaConf(signal, fsPtr);
    break;
  case FsConnectRecord::CLOSE_READ_TAB_FILE:
    jam();
    closeReadTableConf(signal, fsPtr);
    break;
  case FsConnectRecord::CLOSE_WRITE_TAB_FILE:
    jam();
    closeWriteTableConf(signal, fsPtr);
    break;
  case FsConnectRecord::OPEN_READ_SCHEMA2:
    openSchemaFile(signal, 1, fsPtr.i, false, false);
    break;
  case FsConnectRecord::OPEN_READ_TAB_FILE2:
    openTableFile(signal, 1, fsPtr.i, c_readTableRecord.tableId, false);
    break;
  default:
    jamLine((fsPtr.p->fsState & 0xFFF));
    ndbrequire(false);
    break;
  }//switch
}//execFSCLOSECONF()


/* ---------------------------------------------------------------- */
// A file was successfully opened.
/* ---------------------------------------------------------------- */
void Dbdict::execFSOPENCONF(Signal* signal) 
{
  FsConnectRecordPtr fsPtr;
  jamEntry();
  FsConf * const fsConf = (FsConf *)&signal->theData[0];
  c_fsConnectRecordPool.getPtr(fsPtr, fsConf->userPointer);

  Uint32 filePointer = fsConf->filePointer;
  fsPtr.p->filePtr = filePointer;
  switch (fsPtr.p->fsState) {
  case FsConnectRecord::OPEN_WRITE_SCHEMA:
    jam();
    fsPtr.p->fsState = FsConnectRecord::WRITE_SCHEMA;
    writeSchemaFile(signal, filePointer, fsPtr.i);
    break;
  case FsConnectRecord::OPEN_READ_SCHEMA1:
    jam();
    fsPtr.p->fsState = FsConnectRecord::READ_SCHEMA1;
    readSchemaFile(signal, filePointer, fsPtr.i);
    break;
  case FsConnectRecord::OPEN_READ_SCHEMA2:
    jam();
    fsPtr.p->fsState = FsConnectRecord::READ_SCHEMA2;
    readSchemaFile(signal, filePointer, fsPtr.i);
    break;
  case FsConnectRecord::OPEN_READ_TAB_FILE1:
    jam();
    fsPtr.p->fsState = FsConnectRecord::READ_TAB_FILE1;
    readTableFile(signal, filePointer, fsPtr.i);
    break;
  case FsConnectRecord::OPEN_READ_TAB_FILE2:
    jam();
    fsPtr.p->fsState = FsConnectRecord::READ_TAB_FILE2;
    readTableFile(signal, filePointer, fsPtr.i);
    break;
  case FsConnectRecord::OPEN_WRITE_TAB_FILE:
    jam();
    fsPtr.p->fsState = FsConnectRecord::WRITE_TAB_FILE;
    writeTableFile(signal, filePointer, fsPtr.i);
    break;
  default:
    jamLine((fsPtr.p->fsState & 0xFFF));
    ndbrequire(false);
    break;
  }//switch
}//execFSOPENCONF()

/* ---------------------------------------------------------------- */
// An open file was refused.
/* ---------------------------------------------------------------- */
void Dbdict::execFSOPENREF(Signal* signal) 
{
  jamEntry();
  FsRef * const fsRef = (FsRef *)&signal->theData[0];
  FsConnectRecordPtr fsPtr;
  c_fsConnectRecordPool.getPtr(fsPtr, fsRef->userPointer);
  switch (fsPtr.p->fsState) {
  case FsConnectRecord::OPEN_READ_SCHEMA1:
    jam();
    openReadSchemaRef(signal, fsPtr);
    return;
  case FsConnectRecord::OPEN_READ_TAB_FILE1:
    jam();
    openReadTableRef(signal, fsPtr);
    return;
  default:
    break;
  }//switch
  {
    char msg[100];
    sprintf(msg, "File system open failed during FsConnectRecord state %d", (Uint32)fsPtr.p->fsState);
    fsRefError(signal,__LINE__,msg);
  }
}//execFSOPENREF()

/* ---------------------------------------------------------------- */
// A file was successfully read.
/* ---------------------------------------------------------------- */
void Dbdict::execFSREADCONF(Signal* signal) 
{
  jamEntry();
  FsConf * const fsConf = (FsConf *)&signal->theData[0];
  FsConnectRecordPtr fsPtr;
  c_fsConnectRecordPool.getPtr(fsPtr, fsConf->userPointer);
  switch (fsPtr.p->fsState) {
  case FsConnectRecord::READ_SCHEMA1:
  case FsConnectRecord::READ_SCHEMA2:
    readSchemaConf(signal ,fsPtr);
    break;
  case FsConnectRecord::READ_TAB_FILE1:
    if(ERROR_INSERTED(6007)){
      jam();
      FsRef * const fsRef = (FsRef *)&signal->theData[0];
      fsRef->userPointer = fsConf->userPointer;
      fsRef->setErrorCode(fsRef->errorCode, NDBD_EXIT_AFS_UNKNOWN);
      fsRef->osErrorCode = ~0; // Indicate local error
      execFSREADREF(signal);
      return;
    }//Testing how DICT behave if read of file 1 fails (Bug#28770)
  case FsConnectRecord::READ_TAB_FILE2:
    jam();
    readTableConf(signal ,fsPtr);
    break;
  default:
    jamLine((fsPtr.p->fsState & 0xFFF));
    ndbrequire(false);
    break;
  }//switch
}//execFSREADCONF()

/* ---------------------------------------------------------------- */
// A read file was refused.
/* ---------------------------------------------------------------- */
void Dbdict::execFSREADREF(Signal* signal) 
{
  jamEntry();
  FsRef * const fsRef = (FsRef *)&signal->theData[0];
  FsConnectRecordPtr fsPtr;
  c_fsConnectRecordPool.getPtr(fsPtr, fsRef->userPointer);
  switch (fsPtr.p->fsState) {
  case FsConnectRecord::READ_SCHEMA1:
    jam();
    readSchemaRef(signal, fsPtr);
    return;
  case FsConnectRecord::READ_TAB_FILE1:
    jam();
    readTableRef(signal, fsPtr);
    return;
  default:
    break;
  }//switch
  {
    char msg[100];
    sprintf(msg, "File system read failed during FsConnectRecord state %d", (Uint32)fsPtr.p->fsState);
    fsRefError(signal,__LINE__,msg);
  }
}//execFSREADREF()

/* ---------------------------------------------------------------- */
// A file was successfully written.
/* ---------------------------------------------------------------- */
void Dbdict::execFSWRITECONF(Signal* signal) 
{
  FsConf * const fsConf = (FsConf *)&signal->theData[0];
  FsConnectRecordPtr fsPtr;
  jamEntry();
  c_fsConnectRecordPool.getPtr(fsPtr, fsConf->userPointer);
  switch (fsPtr.p->fsState) {
  case FsConnectRecord::WRITE_TAB_FILE:
    writeTableConf(signal, fsPtr);
    break;
  case FsConnectRecord::WRITE_SCHEMA:
    jam();
    writeSchemaConf(signal, fsPtr);
    break;
  default:
    jamLine((fsPtr.p->fsState & 0xFFF));
    ndbrequire(false);
    break;
  }//switch
}//execFSWRITECONF()


/* ---------------------------------------------------------------- */
// Routines to handle Read/Write of Table Files
/* ---------------------------------------------------------------- */
void
Dbdict::writeTableFile(Signal* signal, Uint32 tableId, 
		       SegmentedSectionPtr tabInfoPtr, Callback* callback){
  
  ndbrequire(c_writeTableRecord.tableWriteState == WriteTableRecord::IDLE);
  
  Uint32 pages = WORDS2PAGES(tabInfoPtr.sz);
  c_writeTableRecord.no_of_words = tabInfoPtr.sz;
  c_writeTableRecord.tableWriteState = WriteTableRecord::TWR_CALLBACK;
  c_writeTableRecord.m_callback = * callback;

  c_writeTableRecord.pageId = 0;
  ndbrequire(pages == 1);
  
  PageRecordPtr pageRecPtr;
  c_pageRecordArray.getPtr(pageRecPtr, c_writeTableRecord.pageId);
  copy(&pageRecPtr.p->word[ZPAGE_HEADER_SIZE], tabInfoPtr);
  
  memset(&pageRecPtr.p->word[0], 0, 4 * ZPAGE_HEADER_SIZE);
  pageRecPtr.p->word[ZPOS_CHECKSUM] = 
    computeChecksum(&pageRecPtr.p->word[0], 
		    pages * ZSIZE_OF_PAGES_IN_WORDS);
  
  startWriteTableFile(signal, tableId);
}

// SchemaTrans variant
void
Dbdict::writeTableFile(Signal* signal, Uint32 tableId,
                       OpSection tabInfoSec, Callback* callback)
{
  ndbrequire(c_writeTableRecord.tableWriteState == WriteTableRecord::IDLE);

  {
    const Uint32 size = tabInfoSec.getSize();
    const Uint32 pages = WORDS2PAGES(size);

    c_writeTableRecord.no_of_words = size;
    c_writeTableRecord.tableWriteState = WriteTableRecord::TWR_CALLBACK;
    c_writeTableRecord.m_callback = * callback;

    c_writeTableRecord.pageId = 0;
    ndbrequire(pages == 1);

    PageRecordPtr pageRecPtr;
    c_pageRecordArray.getPtr(pageRecPtr, c_writeTableRecord.pageId);

    Uint32* dst = &pageRecPtr.p->word[ZPAGE_HEADER_SIZE];
    Uint32 dstSize = ZSIZE_OF_PAGES_IN_WORDS - ZPAGE_HEADER_SIZE;
    bool ok = copyOut(tabInfoSec, dst, dstSize);
    ndbrequire(ok);

    memset(&pageRecPtr.p->word[0], 0, 4 * ZPAGE_HEADER_SIZE);
    pageRecPtr.p->word[ZPOS_CHECKSUM] =
      computeChecksum(&pageRecPtr.p->word[0],
                      pages * ZSIZE_OF_PAGES_IN_WORDS);
  }

  startWriteTableFile(signal, tableId);
}

void Dbdict::startWriteTableFile(Signal* signal, Uint32 tableId)
{
  FsConnectRecordPtr fsPtr;
  c_writeTableRecord.tableId = tableId;
  c_fsConnectRecordPool.getPtr(fsPtr, getFsConnRecord());
  fsPtr.p->fsState = FsConnectRecord::OPEN_WRITE_TAB_FILE;
  openTableFile(signal, 0, fsPtr.i, tableId, true);
  c_writeTableRecord.noOfTableFilesHandled = 0;
}//Dbdict::startWriteTableFile()

void Dbdict::openTableFile(Signal* signal,
                           Uint32 fileNo,
                           Uint32 fsConPtr,
                           Uint32 tableId,
                           bool   writeFlag) 
{
  FsOpenReq * const fsOpenReq = (FsOpenReq *)&signal->theData[0];

  fsOpenReq->userReference = reference();
  fsOpenReq->userPointer = fsConPtr;
  if (writeFlag) {
    jam();
    fsOpenReq->fileFlags = 
      FsOpenReq::OM_WRITEONLY | 
      FsOpenReq::OM_TRUNCATE | 
      FsOpenReq::OM_CREATE | 
      FsOpenReq::OM_SYNC;
  } else {
    jam();
    fsOpenReq->fileFlags = FsOpenReq::OM_READONLY;
  }//if
  fsOpenReq->fileNumber[3] = 0; // Initialise before byte changes
  FsOpenReq::setVersion(fsOpenReq->fileNumber, 1);
  FsOpenReq::setSuffix(fsOpenReq->fileNumber, FsOpenReq::S_TABLELIST);
  FsOpenReq::v1_setDisk(fsOpenReq->fileNumber, (fileNo + 1));
  FsOpenReq::v1_setTable(fsOpenReq->fileNumber, tableId);
  FsOpenReq::v1_setFragment(fsOpenReq->fileNumber, (Uint32)-1);
  FsOpenReq::v1_setS(fsOpenReq->fileNumber, 0);
  FsOpenReq::v1_setP(fsOpenReq->fileNumber, 255);
/* ---------------------------------------------------------------- */
// File name : D1/DBDICT/T0/S1.TableList
// D1 means Disk 1 (set by fileNo + 1)
// T0 means table id = 0
// S1 means tableVersion 1
// TableList indicates that this is a file for a table description.
/* ---------------------------------------------------------------- */
  sendSignal(NDBFS_REF, GSN_FSOPENREQ, signal, FsOpenReq::SignalLength, JBA);
}//openTableFile()

void Dbdict::writeTableFile(Signal* signal, Uint32 filePtr, Uint32 fsConPtr) 
{
  FsReadWriteReq * const fsRWReq = (FsReadWriteReq *)&signal->theData[0];

  fsRWReq->filePointer = filePtr;
  fsRWReq->userReference = reference();
  fsRWReq->userPointer = fsConPtr;
  fsRWReq->operationFlag = 0; // Initialise before bit changes
  FsReadWriteReq::setSyncFlag(fsRWReq->operationFlag, 1);
  FsReadWriteReq::setFormatFlag(fsRWReq->operationFlag, 
                                FsReadWriteReq::fsFormatArrayOfPages);
  fsRWReq->varIndex = ZBAT_TABLE_FILE;
  fsRWReq->numberOfPages = WORDS2PAGES(c_writeTableRecord.no_of_words);
  fsRWReq->data.arrayOfPages.varIndex = c_writeTableRecord.pageId;
  fsRWReq->data.arrayOfPages.fileOffset = 0; // Write to file page 0
  sendSignal(NDBFS_REF, GSN_FSWRITEREQ, signal, 8, JBA);
}//writeTableFile()

void Dbdict::writeTableConf(Signal* signal,
                                FsConnectRecordPtr fsPtr)
{
  fsPtr.p->fsState = FsConnectRecord::CLOSE_WRITE_TAB_FILE;
  closeFile(signal, fsPtr.p->filePtr, fsPtr.i);
  return;
}//Dbdict::writeTableConf()

void Dbdict::closeWriteTableConf(Signal* signal,
                                 FsConnectRecordPtr fsPtr)
{
  c_writeTableRecord.noOfTableFilesHandled++;
  if (c_writeTableRecord.noOfTableFilesHandled < 2) {
    jam();
    fsPtr.p->fsState = FsConnectRecord::OPEN_WRITE_TAB_FILE;
    openTableFile(signal, 1, fsPtr.i, c_writeTableRecord.tableId, true);
    return;
  } 
  ndbrequire(c_writeTableRecord.noOfTableFilesHandled == 2);
  c_fsConnectRecordPool.release(fsPtr);
  WriteTableRecord::TableWriteState state = c_writeTableRecord.tableWriteState;
  c_writeTableRecord.tableWriteState = WriteTableRecord::IDLE;
  switch (state) {
  case WriteTableRecord::IDLE:
  case WriteTableRecord::WRITE_ADD_TABLE_MASTER :
  case WriteTableRecord::WRITE_ADD_TABLE_SLAVE :
  case WriteTableRecord::WRITE_RESTART_FROM_MASTER :
  case WriteTableRecord::WRITE_RESTART_FROM_OWN :
    ndbrequire(false);
    break;
  case WriteTableRecord::TWR_CALLBACK:
    jam();
    execute(signal, c_writeTableRecord.m_callback, 0);
    return;
  }
  ndbrequire(false);
}//Dbdict::closeWriteTableConf()

void Dbdict::startReadTableFile(Signal* signal, Uint32 tableId)
{
  //globalSignalLoggers.log(number(), "startReadTableFile");
  ndbrequire(!c_readTableRecord.inUse);
  
  FsConnectRecordPtr fsPtr;
  c_fsConnectRecordPool.getPtr(fsPtr, getFsConnRecord());
  c_readTableRecord.inUse = true;
  c_readTableRecord.tableId = tableId;
  fsPtr.p->fsState = FsConnectRecord::OPEN_READ_TAB_FILE1;
  openTableFile(signal, 0, fsPtr.i, tableId, false);
}//Dbdict::startReadTableFile()

void Dbdict::openReadTableRef(Signal* signal,
                              FsConnectRecordPtr fsPtr) 
{
  fsPtr.p->fsState = FsConnectRecord::OPEN_READ_TAB_FILE2;
  openTableFile(signal, 1, fsPtr.i, c_readTableRecord.tableId, false);
  return;
}//Dbdict::openReadTableConf()

void Dbdict::readTableFile(Signal* signal, Uint32 filePtr, Uint32 fsConPtr) 
{
  FsReadWriteReq * const fsRWReq = (FsReadWriteReq *)&signal->theData[0];

  fsRWReq->filePointer = filePtr;
  fsRWReq->userReference = reference();
  fsRWReq->userPointer = fsConPtr;
  fsRWReq->operationFlag = 0; // Initialise before bit changes
  FsReadWriteReq::setSyncFlag(fsRWReq->operationFlag, 0);
  FsReadWriteReq::setFormatFlag(fsRWReq->operationFlag, 
                                FsReadWriteReq::fsFormatArrayOfPages);
  fsRWReq->varIndex = ZBAT_TABLE_FILE;
  fsRWReq->numberOfPages = WORDS2PAGES(c_readTableRecord.no_of_words);
  fsRWReq->data.arrayOfPages.varIndex = c_readTableRecord.pageId;
  fsRWReq->data.arrayOfPages.fileOffset = 0; // Write to file page 0
  sendSignal(NDBFS_REF, GSN_FSREADREQ, signal, 8, JBA);
}//readTableFile()

void Dbdict::readTableConf(Signal* signal,
			   FsConnectRecordPtr fsPtr)
{
  /* ---------------------------------------------------------------- */
  // Verify the data read from disk
  /* ---------------------------------------------------------------- */
  bool crashInd;
  if (fsPtr.p->fsState == FsConnectRecord::READ_TAB_FILE1) {
    jam();
    crashInd = false;
  } else {
    jam();
    crashInd = true;
  }//if

  PageRecordPtr tmpPagePtr;
  c_pageRecordArray.getPtr(tmpPagePtr, c_readTableRecord.pageId);
  Uint32 sz = 
    WORDS2PAGES(c_readTableRecord.no_of_words)*ZSIZE_OF_PAGES_IN_WORDS;
  Uint32 chk = computeChecksum((const Uint32*)tmpPagePtr.p, sz);
  
  ndbrequire((chk == 0) || !crashInd);
  if(chk != 0){
    jam();
    ndbrequire(fsPtr.p->fsState == FsConnectRecord::READ_TAB_FILE1);
    readTableRef(signal, fsPtr);
    return;
  }//if
  
  fsPtr.p->fsState = FsConnectRecord::CLOSE_READ_TAB_FILE;
  closeFile(signal, fsPtr.p->filePtr, fsPtr.i);
  return;
}//Dbdict::readTableConf()

void Dbdict::readTableRef(Signal* signal,
                          FsConnectRecordPtr fsPtr)
{
  /**
   * First close corrupt file
   */
  fsPtr.p->fsState = FsConnectRecord::OPEN_READ_TAB_FILE2;
  closeFile(signal, fsPtr.p->filePtr, fsPtr.i);
  return;
}//Dbdict::readTableRef()

void Dbdict::closeReadTableConf(Signal* signal,
                                FsConnectRecordPtr fsPtr)
{
  c_fsConnectRecordPool.release(fsPtr);
  c_readTableRecord.inUse = false;
  
  execute(signal, c_readTableRecord.m_callback, 0);
  return;
}//Dbdict::closeReadTableConf()

/* ---------------------------------------------------------------- */
// Routines to handle Read/Write of Schema Files
/* ---------------------------------------------------------------- */
NdbOut& operator<<(NdbOut& out, const SchemaFile::TableEntry entry);

void
Dbdict::updateSchemaState(Signal* signal, Uint32 tableId, 
			  SchemaFile::TableEntry* te, Callback* callback,
                          bool savetodisk, bool dicttrans)
{
  jam();
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  SchemaFile::TableEntry * tableEntry = getTableEntry(xsf, tableId);

  if (!dicttrans) {
    /*
     * Old code may not zero transId, leaving the entry not visible
     * to tabinfo request (old code may also skip updateSchemaState).
   */
    te->m_transId = 0;
  }

  D("updateSchemaState" << V(tableId));
  D("old:" << *tableEntry);
  D("new:" << *te);
  
  SchemaFile::TableState newState = 
    (SchemaFile::TableState)te->m_tableState;
  SchemaFile::TableState oldState = 
    (SchemaFile::TableState)tableEntry->m_tableState;
  
  Uint32 newVersion = te->m_tableVersion;
  Uint32 oldVersion = tableEntry->m_tableVersion;

  bool ok = false;
  switch (newState){
  case SchemaFile::CREATE_PARSED:
    jam();
    ok = true;
    ndbrequire(create_obj_inc_schema_version(oldVersion) == newVersion);
    ndbrequire(oldState == SchemaFile::INIT ||
               oldState == SchemaFile::DROP_TABLE_COMMITTED);
    break;
  case SchemaFile::DROP_PARSED:
    ok = true;
    break;
  case SchemaFile::ALTER_PARSED:
    // wl3600_todo
    ok = true;
    break;
  case SchemaFile::ADD_STARTED:
    jam();
    ok = true;
    if (dicttrans) {
      ndbrequire(oldVersion == newVersion);
      ndbrequire(oldState == SchemaFile::CREATE_PARSED);
      break;
    }
    ndbrequire(create_obj_inc_schema_version(oldVersion) == newVersion);
    ndbrequire(oldState == SchemaFile::INIT ||
	       oldState == SchemaFile::DROP_TABLE_COMMITTED);
    break;
  case SchemaFile::TABLE_ADD_COMMITTED:
    jam();
    ok = true;
    ndbrequire(newVersion == oldVersion);
    ndbrequire(oldState == SchemaFile::ADD_STARTED ||
	       oldState == SchemaFile::DROP_TABLE_STARTED);
    break;
  case SchemaFile::ALTER_TABLE_COMMITTED:
    jam();
    ok = true;
    ndbrequire(alter_obj_inc_schema_version(oldVersion) == newVersion);
    ndbrequire(oldState == SchemaFile::TABLE_ADD_COMMITTED ||
	       oldState == SchemaFile::ALTER_TABLE_COMMITTED);
    break;
  case SchemaFile::DROP_TABLE_STARTED:
    jam();
  case SchemaFile::DROP_TABLE_COMMITTED:
    jam();
    ok = true;
    break;
  case SchemaFile::TEMPORARY_TABLE_COMMITTED:
    jam();
    ndbrequire(oldState == SchemaFile::ADD_STARTED ||
               oldState == SchemaFile::TEMPORARY_TABLE_COMMITTED);
    ok = true;
    break;
  case SchemaFile::INIT:
    jam();
    ok = true;
    if (dicttrans) {
      ndbrequire((oldState == SchemaFile::CREATE_PARSED));
      break;
    }
    ndbrequire((oldState == SchemaFile::ADD_STARTED));
  }//if
  ndbrequire(ok);
  
  * tableEntry = * te;
  computeChecksum(xsf, tableId / NDB_SF_PAGE_ENTRIES);

  if (savetodisk)
  {
    ndbrequire(c_writeSchemaRecord.inUse == false);
    c_writeSchemaRecord.inUse = true;
    
    c_writeSchemaRecord.pageId = c_schemaRecord.schemaPage;
    c_writeSchemaRecord.newFile = false;
    c_writeSchemaRecord.firstPage = tableId / NDB_SF_PAGE_ENTRIES;
    c_writeSchemaRecord.noOfPages = 1;
    c_writeSchemaRecord.m_callback = * callback;
    
    startWriteSchemaFile(signal);
  }
  else
  {
    jam();
    if (callback != 0) {
      jam();
      execute(signal, *callback, 0);
    }
  }
}

void Dbdict::startWriteSchemaFile(Signal* signal)
{
  FsConnectRecordPtr fsPtr;
  c_fsConnectRecordPool.getPtr(fsPtr, getFsConnRecord());
  fsPtr.p->fsState = FsConnectRecord::OPEN_WRITE_SCHEMA;
  openSchemaFile(signal, 0, fsPtr.i, true, c_writeSchemaRecord.newFile);
  c_writeSchemaRecord.noOfSchemaFilesHandled = 0;
}//Dbdict::startWriteSchemaFile()

void Dbdict::openSchemaFile(Signal* signal,
                            Uint32 fileNo,
                            Uint32 fsConPtr,
                            bool writeFlag,
                            bool newFile)
{
  FsOpenReq * const fsOpenReq = (FsOpenReq *)&signal->theData[0];
  fsOpenReq->userReference = reference();
  fsOpenReq->userPointer = fsConPtr;
  if (writeFlag) {
    jam();
    fsOpenReq->fileFlags = 
      FsOpenReq::OM_WRITEONLY | 
      FsOpenReq::OM_SYNC;
    if (newFile)
      fsOpenReq->fileFlags |=
        FsOpenReq::OM_TRUNCATE | 
        FsOpenReq::OM_CREATE;
  } else {
    jam();
    fsOpenReq->fileFlags = FsOpenReq::OM_READONLY;
  }//if
  fsOpenReq->fileNumber[3] = 0; // Initialise before byte changes
  FsOpenReq::setVersion(fsOpenReq->fileNumber, 1);
  FsOpenReq::setSuffix(fsOpenReq->fileNumber, FsOpenReq::S_SCHEMALOG);
  FsOpenReq::v1_setDisk(fsOpenReq->fileNumber, (fileNo + 1));
  FsOpenReq::v1_setTable(fsOpenReq->fileNumber, (Uint32)-1);
  FsOpenReq::v1_setFragment(fsOpenReq->fileNumber, (Uint32)-1);
  FsOpenReq::v1_setS(fsOpenReq->fileNumber, (Uint32)-1);
  FsOpenReq::v1_setP(fsOpenReq->fileNumber, 0);
/* ---------------------------------------------------------------- */
// File name : D1/DBDICT/P0.SchemaLog
// D1 means Disk 1 (set by fileNo + 1). Writes to both D1 and D2
// SchemaLog indicates that this is a file giving a list of current tables.
/* ---------------------------------------------------------------- */
  sendSignal(NDBFS_REF, GSN_FSOPENREQ, signal, FsOpenReq::SignalLength, JBA);
}//openSchemaFile()

void Dbdict::writeSchemaFile(Signal* signal, Uint32 filePtr, Uint32 fsConPtr) 
{
  FsReadWriteReq * const fsRWReq = (FsReadWriteReq *)&signal->theData[0];

  // check write record
  WriteSchemaRecord & wr = c_writeSchemaRecord;
  ndbrequire(wr.pageId == (wr.pageId != 0) * NDB_SF_MAX_PAGES);
  ndbrequire(wr.noOfPages != 0);
  ndbrequire(wr.firstPage + wr.noOfPages <= NDB_SF_MAX_PAGES);

  fsRWReq->filePointer = filePtr;
  fsRWReq->userReference = reference();
  fsRWReq->userPointer = fsConPtr;
  fsRWReq->operationFlag = 0; // Initialise before bit changes
  FsReadWriteReq::setSyncFlag(fsRWReq->operationFlag, 1);
  FsReadWriteReq::setFormatFlag(fsRWReq->operationFlag, 
                                FsReadWriteReq::fsFormatArrayOfPages);
  fsRWReq->varIndex = ZBAT_SCHEMA_FILE;
  fsRWReq->numberOfPages = wr.noOfPages;
  // Write from memory page
  fsRWReq->data.arrayOfPages.varIndex = wr.pageId + wr.firstPage;
  fsRWReq->data.arrayOfPages.fileOffset = wr.firstPage;
  sendSignal(NDBFS_REF, GSN_FSWRITEREQ, signal, 8, JBA);
}//writeSchemaFile()

void Dbdict::writeSchemaConf(Signal* signal,
                                FsConnectRecordPtr fsPtr)
{
  fsPtr.p->fsState = FsConnectRecord::CLOSE_WRITE_SCHEMA;
  closeFile(signal, fsPtr.p->filePtr, fsPtr.i);
  return;
}//Dbdict::writeSchemaConf()

void Dbdict::closeFile(Signal* signal, Uint32 filePtr, Uint32 fsConPtr) 
{
  FsCloseReq * const fsCloseReq = (FsCloseReq *)&signal->theData[0];
  fsCloseReq->filePointer = filePtr;
  fsCloseReq->userReference = reference();
  fsCloseReq->userPointer = fsConPtr;
  FsCloseReq::setRemoveFileFlag(fsCloseReq->fileFlag, false);
  sendSignal(NDBFS_REF, GSN_FSCLOSEREQ, signal, FsCloseReq::SignalLength, JBA);
  return;
}//closeFile()

void Dbdict::closeWriteSchemaConf(Signal* signal,
                                     FsConnectRecordPtr fsPtr)
{
  c_writeSchemaRecord.noOfSchemaFilesHandled++;
  if (c_writeSchemaRecord.noOfSchemaFilesHandled < 2) {
    jam();
    fsPtr.p->fsState = FsConnectRecord::OPEN_WRITE_SCHEMA;
    openSchemaFile(signal, 1, fsPtr.i, true, c_writeSchemaRecord.newFile);
    return;
  } 
  ndbrequire(c_writeSchemaRecord.noOfSchemaFilesHandled == 2);
  
  c_fsConnectRecordPool.release(fsPtr);

  c_writeSchemaRecord.inUse = false;
  execute(signal, c_writeSchemaRecord.m_callback, 0);
  return;
}//Dbdict::closeWriteSchemaConf()

void Dbdict::startReadSchemaFile(Signal* signal)
{
  //globalSignalLoggers.log(number(), "startReadSchemaFile");
  FsConnectRecordPtr fsPtr;
  c_fsConnectRecordPool.getPtr(fsPtr, getFsConnRecord());
  fsPtr.p->fsState = FsConnectRecord::OPEN_READ_SCHEMA1;
  openSchemaFile(signal, 0, fsPtr.i, false, false);
}//Dbdict::startReadSchemaFile()

void Dbdict::openReadSchemaRef(Signal* signal,
                               FsConnectRecordPtr fsPtr) 
{
  fsPtr.p->fsState = FsConnectRecord::OPEN_READ_SCHEMA2;
  openSchemaFile(signal, 1, fsPtr.i, false, false);
}//Dbdict::openReadSchemaRef()

void Dbdict::readSchemaFile(Signal* signal, Uint32 filePtr, Uint32 fsConPtr) 
{
  FsReadWriteReq * const fsRWReq = (FsReadWriteReq *)&signal->theData[0];

  // check read record
  ReadSchemaRecord & rr = c_readSchemaRecord;
  ndbrequire(rr.pageId == (rr.pageId != 0) * NDB_SF_MAX_PAGES);
  ndbrequire(rr.noOfPages != 0);
  ndbrequire(rr.firstPage + rr.noOfPages <= NDB_SF_MAX_PAGES);

  fsRWReq->filePointer = filePtr;
  fsRWReq->userReference = reference();
  fsRWReq->userPointer = fsConPtr;
  fsRWReq->operationFlag = 0; // Initialise before bit changes
  FsReadWriteReq::setSyncFlag(fsRWReq->operationFlag, 0);
  FsReadWriteReq::setFormatFlag(fsRWReq->operationFlag, 
                                FsReadWriteReq::fsFormatArrayOfPages);
  fsRWReq->varIndex = ZBAT_SCHEMA_FILE;
  fsRWReq->numberOfPages = rr.noOfPages;
  fsRWReq->data.arrayOfPages.varIndex = rr.pageId + rr.firstPage;
  fsRWReq->data.arrayOfPages.fileOffset = rr.firstPage;
  sendSignal(NDBFS_REF, GSN_FSREADREQ, signal, 8, JBA);
}//readSchemaFile()

void Dbdict::readSchemaConf(Signal* signal,
                            FsConnectRecordPtr fsPtr)
{
/* ---------------------------------------------------------------- */
// Verify the data read from disk
/* ---------------------------------------------------------------- */
  bool crashInd;
  if (fsPtr.p->fsState == FsConnectRecord::READ_SCHEMA1) {
    jam();
    crashInd = false;
  } else {
    jam();
    crashInd = true;
  }//if

  ReadSchemaRecord & rr = c_readSchemaRecord;
  XSchemaFile * xsf = &c_schemaFile[rr.pageId != 0];

  if (rr.schemaReadState == ReadSchemaRecord::INITIAL_READ_HEAD) {
    jam();
    ndbrequire(rr.firstPage == 0);
    SchemaFile * sf = &xsf->schemaPage[0];
    Uint32 noOfPages;
    if (sf->NdbVersion < NDB_SF_VERSION_5_0_6) {
      jam();
      const Uint32 pageSize_old = 32 * 1024;
      noOfPages = pageSize_old / NDB_SF_PAGE_SIZE - 1;
    } else {
      noOfPages = sf->FileSize / NDB_SF_PAGE_SIZE - 1;
    }
    rr.schemaReadState = ReadSchemaRecord::INITIAL_READ;
    if (noOfPages != 0) {
      rr.firstPage = 1;
      rr.noOfPages = noOfPages;
      readSchemaFile(signal, fsPtr.p->filePtr, fsPtr.i);
      return;
    }
  }

  SchemaFile * sf0 = &xsf->schemaPage[0];
  xsf->noOfPages = sf0->FileSize / NDB_SF_PAGE_SIZE;

  if (sf0->NdbVersion < NDB_SF_VERSION_5_0_6 &&
      ! convertSchemaFileTo_5_0_6(xsf)) {
    jam();
    ndbrequire(! crashInd);
    ndbrequire(fsPtr.p->fsState == FsConnectRecord::READ_SCHEMA1);
    readSchemaRef(signal, fsPtr);
    return;
  }

  for (Uint32 n = 0; n < xsf->noOfPages; n++) {
    SchemaFile * sf = &xsf->schemaPage[n];
    bool ok = false;
    const char *reason;
    if (memcmp(sf->Magic, NDB_SF_MAGIC, sizeof(sf->Magic)) != 0)
    { jam(); reason = "magic code"; }
    else if (sf->FileSize == 0)
    { jam(); reason = "file size == 0"; }
    else if (sf->FileSize % NDB_SF_PAGE_SIZE != 0)
    { jam(); reason = "invalid size multiple"; }
    else if (sf->FileSize != sf0->FileSize)
    { jam(); reason = "invalid size"; }
    else if (sf->PageNumber != n)
    { jam(); reason = "invalid page number"; }
    else if (computeChecksum((Uint32*)sf, NDB_SF_PAGE_SIZE_IN_WORDS) != 0)
    { jam(); reason = "invalid checksum"; }
    else
      ok = true;

    if (!ok)
    {
      char reason_msg[128];
      snprintf(reason_msg, sizeof(reason_msg),
               "schema file corrupt, page %u (%s, "
               "sz=%u sz0=%u pn=%u)",
               n, reason, sf->FileSize, sf0->FileSize, sf->PageNumber);
      if (crashInd)
        progError(__LINE__, NDBD_EXIT_SR_SCHEMAFILE, reason_msg);
      ndbrequireErr(fsPtr.p->fsState == FsConnectRecord::READ_SCHEMA1,
                    NDBD_EXIT_SR_SCHEMAFILE);
      jam();
      infoEvent("primary %s, trying backup", reason_msg);
      readSchemaRef(signal, fsPtr);
      return;
    }
  }

  fsPtr.p->fsState = FsConnectRecord::CLOSE_READ_SCHEMA;
  closeFile(signal, fsPtr.p->filePtr, fsPtr.i);
  return;
}//Dbdict::readSchemaConf()

void Dbdict::readSchemaRef(Signal* signal,
                           FsConnectRecordPtr fsPtr)
{
  /**
   * First close corrupt file
   */
  fsPtr.p->fsState = FsConnectRecord::OPEN_READ_SCHEMA2;
  closeFile(signal, fsPtr.p->filePtr, fsPtr.i);
  return;
}

void Dbdict::closeReadSchemaConf(Signal* signal,
                                 FsConnectRecordPtr fsPtr)
{
  c_fsConnectRecordPool.release(fsPtr);
  ReadSchemaRecord::SchemaReadState state = c_readSchemaRecord.schemaReadState;
  c_readSchemaRecord.schemaReadState = ReadSchemaRecord::IDLE;

  switch(state) {
  case ReadSchemaRecord::INITIAL_READ :
    jam();
    {
      // write back both copies
      
      ndbrequire(c_writeSchemaRecord.inUse == false);
      XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.oldSchemaPage != 0 ];
      Uint32 noOfPages =
        (c_tableRecordPool.getSize() + NDB_SF_PAGE_ENTRIES - 1) /
        NDB_SF_PAGE_ENTRIES;
      resizeSchemaFile(xsf, noOfPages);

      c_writeSchemaRecord.inUse = true;
      c_writeSchemaRecord.pageId = c_schemaRecord.oldSchemaPage;
      c_writeSchemaRecord.newFile = true;
      c_writeSchemaRecord.firstPage = 0;
      c_writeSchemaRecord.noOfPages = xsf->noOfPages;

      c_writeSchemaRecord.m_callback.m_callbackFunction = 
        safe_cast(&Dbdict::initSchemaFile_conf);

      startWriteSchemaFile(signal);
    }
    break;

  default :
    ndbrequire(false);
    break;

  }//switch
}//Dbdict::closeReadSchemaConf()

bool
Dbdict::convertSchemaFileTo_5_0_6(XSchemaFile * xsf)
{
  const Uint32 pageSize_old = 32 * 1024;
  Uint32 page_old[pageSize_old >> 2];
  SchemaFile * sf_old = (SchemaFile *)page_old;

  if (xsf->noOfPages * NDB_SF_PAGE_SIZE != pageSize_old)
    return false;
  SchemaFile * sf0 = &xsf->schemaPage[0];
  memcpy(sf_old, sf0, pageSize_old);

  // init max number new pages needed
  xsf->noOfPages = (sf_old->NoOfTableEntries + NDB_SF_PAGE_ENTRIES - 1) /
                   NDB_SF_PAGE_ENTRIES;
  initSchemaFile(xsf, 0, xsf->noOfPages, true);

  Uint32 noOfPages = 1;
  Uint32 n, i, j;
  for (n = 0; n < xsf->noOfPages; n++) {
    jam();
    for (i = 0; i < NDB_SF_PAGE_ENTRIES; i++) {
      j = n * NDB_SF_PAGE_ENTRIES + i;
      if (j >= sf_old->NoOfTableEntries)
        continue;
      const SchemaFile::TableEntry_old & te_old = sf_old->TableEntries_old[j];
      if (te_old.m_tableState == SchemaFile::INIT ||
          te_old.m_tableState == SchemaFile::DROP_TABLE_COMMITTED ||
          te_old.m_noOfPages == 0)
        continue;
      SchemaFile * sf = &xsf->schemaPage[n];
      SchemaFile::TableEntry & te = sf->TableEntries[i];
      te.m_tableState = te_old.m_tableState;
      te.m_tableVersion = te_old.m_tableVersion;
      te.m_tableType = te_old.m_tableType;
      te.m_info_words = te_old.m_noOfPages * ZSIZE_OF_PAGES_IN_WORDS -
                        ZPAGE_HEADER_SIZE;
      te.m_gcp = te_old.m_gcp;
      if (noOfPages < n)
        noOfPages = n;
    }
  }
  xsf->noOfPages = noOfPages;
  initSchemaFile(xsf, 0, xsf->noOfPages, false);

  return true;
}

/* **************************************************************** */
/* ---------------------------------------------------------------- */
/* MODULE:          INITIALISATION MODULE ------------------------- */
/* ---------------------------------------------------------------- */
/*                                                                  */
/* This module contains initialisation of data at start/restart.    */
/* ---------------------------------------------------------------- */
/* **************************************************************** */

Dbdict::Dbdict(Block_context& ctx):
  SimulatedBlock(DBDICT, ctx),
  c_attributeRecordHash(c_attributeRecordPool),
  c_file_hash(c_file_pool),
  c_filegroup_hash(c_filegroup_pool),
  c_obj_hash(c_obj_pool),
  c_schemaOpHash(c_schemaOpPool),
  c_schemaTransHash(c_schemaTransPool),
  c_schemaTransList(c_schemaTransPool),
  c_schemaTransCount(0),
  c_txHandleHash(c_txHandlePool),
  c_opCreateEvent(c_opRecordPool),
  c_opSubEvent(c_opRecordPool),
  c_opDropEvent(c_opRecordPool),
  c_opSignalUtil(c_opRecordPool),
  c_schemaOperation(c_opRecordPool),
  c_Trans(c_opRecordPool),
  c_opCreateObj(c_schemaOperation),
  c_opDropObj(c_schemaOperation),
  c_opRecordSequence(0)
#ifdef VM_TRACE
  ,debugOut(*new NullOutputStream())
#endif
{
  BLOCK_CONSTRUCTOR(Dbdict);
  
  // Transit signals
  addRecSignal(GSN_DUMP_STATE_ORD, &Dbdict::execDUMP_STATE_ORD);
  addRecSignal(GSN_GET_TABINFOREQ, &Dbdict::execGET_TABINFOREQ);
  addRecSignal(GSN_GET_TABLEID_REQ, &Dbdict::execGET_TABLEDID_REQ);
  addRecSignal(GSN_GET_TABINFO_CONF, &Dbdict::execGET_TABINFO_CONF);
  addRecSignal(GSN_CONTINUEB, &Dbdict::execCONTINUEB);

  addRecSignal(GSN_CREATE_TABLE_REQ, &Dbdict::execCREATE_TABLE_REQ);
  addRecSignal(GSN_CREATE_TAB_REQ, &Dbdict::execCREATE_TAB_REQ);
  addRecSignal(GSN_CREATE_TAB_REF, &Dbdict::execCREATE_TAB_REF);
  addRecSignal(GSN_CREATE_TAB_CONF, &Dbdict::execCREATE_TAB_CONF);
  addRecSignal(GSN_CREATE_FRAGMENTATION_REQ, &Dbdict::execCREATE_FRAGMENTATION_REQ);
  addRecSignal(GSN_CREATE_FRAGMENTATION_REF, &Dbdict::execCREATE_FRAGMENTATION_REF);
  addRecSignal(GSN_CREATE_FRAGMENTATION_CONF, &Dbdict::execCREATE_FRAGMENTATION_CONF);
  addRecSignal(GSN_DIADDTABCONF, &Dbdict::execDIADDTABCONF);
  addRecSignal(GSN_DIADDTABREF, &Dbdict::execDIADDTABREF);
  addRecSignal(GSN_ADD_FRAGREQ, &Dbdict::execADD_FRAGREQ);
  addRecSignal(GSN_TAB_COMMITCONF, &Dbdict::execTAB_COMMITCONF);
  addRecSignal(GSN_TAB_COMMITREF, &Dbdict::execTAB_COMMITREF);
  addRecSignal(GSN_ALTER_TABLE_REQ, &Dbdict::execALTER_TABLE_REQ);
  addRecSignal(GSN_ALTER_TAB_REF, &Dbdict::execALTER_TAB_REF);
  addRecSignal(GSN_ALTER_TAB_CONF, &Dbdict::execALTER_TAB_CONF);

  // Index signals
  addRecSignal(GSN_CREATE_INDX_REQ, &Dbdict::execCREATE_INDX_REQ);
  addRecSignal(GSN_CREATE_INDX_IMPL_CONF, &Dbdict::execCREATE_INDX_IMPL_CONF);
  addRecSignal(GSN_CREATE_INDX_IMPL_REF, &Dbdict::execCREATE_INDX_IMPL_REF);

  addRecSignal(GSN_ALTER_INDX_REQ, &Dbdict::execALTER_INDX_REQ);
  addRecSignal(GSN_ALTER_INDX_CONF, &Dbdict::execALTER_INDX_CONF);
  addRecSignal(GSN_ALTER_INDX_REF, &Dbdict::execALTER_INDX_REF);
  addRecSignal(GSN_ALTER_INDX_IMPL_CONF, &Dbdict::execALTER_INDX_IMPL_CONF);
  addRecSignal(GSN_ALTER_INDX_IMPL_REF, &Dbdict::execALTER_INDX_IMPL_REF);

  addRecSignal(GSN_CREATE_TABLE_CONF, &Dbdict::execCREATE_TABLE_CONF);
  addRecSignal(GSN_CREATE_TABLE_REF, &Dbdict::execCREATE_TABLE_REF);

  addRecSignal(GSN_DROP_INDX_REQ, &Dbdict::execDROP_INDX_REQ);
  addRecSignal(GSN_DROP_INDX_IMPL_CONF, &Dbdict::execDROP_INDX_IMPL_CONF);
  addRecSignal(GSN_DROP_INDX_IMPL_REF, &Dbdict::execDROP_INDX_IMPL_REF);

  addRecSignal(GSN_DROP_TABLE_CONF, &Dbdict::execDROP_TABLE_CONF);
  addRecSignal(GSN_DROP_TABLE_REF, &Dbdict::execDROP_TABLE_REF);

  addRecSignal(GSN_BUILDINDXREQ, &Dbdict::execBUILDINDXREQ);
  addRecSignal(GSN_BUILDINDXCONF, &Dbdict::execBUILDINDXCONF);
  addRecSignal(GSN_BUILDINDXREF, &Dbdict::execBUILDINDXREF);
  addRecSignal(GSN_BUILD_INDX_IMPL_CONF, &Dbdict::execBUILD_INDX_IMPL_CONF);
  addRecSignal(GSN_BUILD_INDX_IMPL_REF, &Dbdict::execBUILD_INDX_IMPL_REF);

  // Util signals
  addRecSignal(GSN_UTIL_PREPARE_CONF, &Dbdict::execUTIL_PREPARE_CONF);
  addRecSignal(GSN_UTIL_PREPARE_REF,  &Dbdict::execUTIL_PREPARE_REF);

  addRecSignal(GSN_UTIL_EXECUTE_CONF, &Dbdict::execUTIL_EXECUTE_CONF);
  addRecSignal(GSN_UTIL_EXECUTE_REF,  &Dbdict::execUTIL_EXECUTE_REF);

  addRecSignal(GSN_UTIL_RELEASE_CONF, &Dbdict::execUTIL_RELEASE_CONF);
  addRecSignal(GSN_UTIL_RELEASE_REF,  &Dbdict::execUTIL_RELEASE_REF);

  // Event signals
  addRecSignal(GSN_CREATE_EVNT_REQ,  &Dbdict::execCREATE_EVNT_REQ);
  addRecSignal(GSN_CREATE_EVNT_CONF, &Dbdict::execCREATE_EVNT_CONF);
  addRecSignal(GSN_CREATE_EVNT_REF,  &Dbdict::execCREATE_EVNT_REF);

  addRecSignal(GSN_CREATE_SUBID_CONF, &Dbdict::execCREATE_SUBID_CONF);
  addRecSignal(GSN_CREATE_SUBID_REF,  &Dbdict::execCREATE_SUBID_REF);

  addRecSignal(GSN_SUB_CREATE_CONF, &Dbdict::execSUB_CREATE_CONF);
  addRecSignal(GSN_SUB_CREATE_REF,  &Dbdict::execSUB_CREATE_REF);

  addRecSignal(GSN_SUB_START_REQ,  &Dbdict::execSUB_START_REQ);
  addRecSignal(GSN_SUB_START_CONF,  &Dbdict::execSUB_START_CONF);
  addRecSignal(GSN_SUB_START_REF,  &Dbdict::execSUB_START_REF);

  addRecSignal(GSN_SUB_STOP_REQ,  &Dbdict::execSUB_STOP_REQ);
  addRecSignal(GSN_SUB_STOP_CONF,  &Dbdict::execSUB_STOP_CONF);
  addRecSignal(GSN_SUB_STOP_REF,  &Dbdict::execSUB_STOP_REF);

  addRecSignal(GSN_DROP_EVNT_REQ,  &Dbdict::execDROP_EVNT_REQ);

  addRecSignal(GSN_SUB_REMOVE_REQ, &Dbdict::execSUB_REMOVE_REQ);
  addRecSignal(GSN_SUB_REMOVE_CONF, &Dbdict::execSUB_REMOVE_CONF);
  addRecSignal(GSN_SUB_REMOVE_REF,  &Dbdict::execSUB_REMOVE_REF);

  // Trigger signals
  addRecSignal(GSN_CREATE_TRIG_REQ, &Dbdict::execCREATE_TRIG_REQ);
  addRecSignal(GSN_CREATE_TRIG_CONF, &Dbdict::execCREATE_TRIG_CONF);
  addRecSignal(GSN_CREATE_TRIG_REF, &Dbdict::execCREATE_TRIG_REF);
  addRecSignal(GSN_DROP_TRIG_REQ, &Dbdict::execDROP_TRIG_REQ);
  addRecSignal(GSN_DROP_TRIG_CONF, &Dbdict::execDROP_TRIG_CONF);
  addRecSignal(GSN_DROP_TRIG_REF, &Dbdict::execDROP_TRIG_REF);
  // Impl
  addRecSignal(GSN_CREATE_TRIG_IMPL_CONF, &Dbdict::execCREATE_TRIG_IMPL_CONF);
  addRecSignal(GSN_CREATE_TRIG_IMPL_REF, &Dbdict::execCREATE_TRIG_IMPL_REF);
  addRecSignal(GSN_DROP_TRIG_IMPL_CONF, &Dbdict::execDROP_TRIG_IMPL_CONF);
  addRecSignal(GSN_DROP_TRIG_IMPL_REF, &Dbdict::execDROP_TRIG_IMPL_REF);

  // Received signals
  addRecSignal(GSN_HOT_SPAREREP, &Dbdict::execHOT_SPAREREP);
  addRecSignal(GSN_GET_SCHEMA_INFOREQ, &Dbdict::execGET_SCHEMA_INFOREQ);
  addRecSignal(GSN_SCHEMA_INFO, &Dbdict::execSCHEMA_INFO);
  addRecSignal(GSN_SCHEMA_INFOCONF, &Dbdict::execSCHEMA_INFOCONF);
  addRecSignal(GSN_DICTSTARTREQ, &Dbdict::execDICTSTARTREQ);
  addRecSignal(GSN_READ_NODESCONF, &Dbdict::execREAD_NODESCONF);
  addRecSignal(GSN_FSOPENCONF, &Dbdict::execFSOPENCONF);
  addRecSignal(GSN_FSOPENREF, &Dbdict::execFSOPENREF, true);
  addRecSignal(GSN_FSCLOSECONF, &Dbdict::execFSCLOSECONF);
  addRecSignal(GSN_FSWRITECONF, &Dbdict::execFSWRITECONF);
  addRecSignal(GSN_FSREADCONF, &Dbdict::execFSREADCONF);
  addRecSignal(GSN_FSREADREF, &Dbdict::execFSREADREF, true);
  addRecSignal(GSN_LQHFRAGCONF, &Dbdict::execLQHFRAGCONF);
  addRecSignal(GSN_LQHADDATTCONF, &Dbdict::execLQHADDATTCONF);
  addRecSignal(GSN_LQHADDATTREF, &Dbdict::execLQHADDATTREF);
  addRecSignal(GSN_LQHFRAGREF, &Dbdict::execLQHFRAGREF);
  addRecSignal(GSN_NDB_STTOR, &Dbdict::execNDB_STTOR);
  addRecSignal(GSN_READ_CONFIG_REQ, &Dbdict::execREAD_CONFIG_REQ, true);
  addRecSignal(GSN_STTOR, &Dbdict::execSTTOR);
  addRecSignal(GSN_TC_SCHVERCONF, &Dbdict::execTC_SCHVERCONF);
  addRecSignal(GSN_NODE_FAILREP, &Dbdict::execNODE_FAILREP);
  addRecSignal(GSN_INCL_NODEREQ, &Dbdict::execINCL_NODEREQ);
  addRecSignal(GSN_API_FAILREQ, &Dbdict::execAPI_FAILREQ);

  addRecSignal(GSN_WAIT_GCP_REF, &Dbdict::execWAIT_GCP_REF);
  addRecSignal(GSN_WAIT_GCP_CONF, &Dbdict::execWAIT_GCP_CONF);

  addRecSignal(GSN_LIST_TABLES_REQ, &Dbdict::execLIST_TABLES_REQ);

  addRecSignal(GSN_DROP_TABLE_REQ, &Dbdict::execDROP_TABLE_REQ);
  
  addRecSignal(GSN_PREP_DROP_TAB_REQ, &Dbdict::execPREP_DROP_TAB_REQ);
  addRecSignal(GSN_PREP_DROP_TAB_REF, &Dbdict::execPREP_DROP_TAB_REF);
  addRecSignal(GSN_PREP_DROP_TAB_CONF, &Dbdict::execPREP_DROP_TAB_CONF);
  
  addRecSignal(GSN_DROP_TAB_REF, &Dbdict::execDROP_TAB_REF);
  addRecSignal(GSN_DROP_TAB_CONF, &Dbdict::execDROP_TAB_CONF);

  addRecSignal(GSN_CREATE_FILE_REQ, &Dbdict::execCREATE_FILE_REQ);
  addRecSignal(GSN_CREATE_FILEGROUP_REQ, &Dbdict::execCREATE_FILEGROUP_REQ);

  addRecSignal(GSN_DROP_FILE_REQ, &Dbdict::execDROP_FILE_REQ);
  addRecSignal(GSN_DROP_FILE_REF, &Dbdict::execDROP_FILE_REF);
  addRecSignal(GSN_DROP_FILE_CONF, &Dbdict::execDROP_FILE_CONF);

  addRecSignal(GSN_DROP_FILEGROUP_REQ, &Dbdict::execDROP_FILEGROUP_REQ);
  addRecSignal(GSN_DROP_FILEGROUP_REF, &Dbdict::execDROP_FILEGROUP_REF);
  addRecSignal(GSN_DROP_FILEGROUP_CONF, &Dbdict::execDROP_FILEGROUP_CONF);
  
  addRecSignal(GSN_CREATE_OBJ_REQ, &Dbdict::execCREATE_OBJ_REQ);
  addRecSignal(GSN_CREATE_OBJ_REF, &Dbdict::execCREATE_OBJ_REF);
  addRecSignal(GSN_CREATE_OBJ_CONF, &Dbdict::execCREATE_OBJ_CONF);
  addRecSignal(GSN_DROP_OBJ_REQ, &Dbdict::execDROP_OBJ_REQ);
  addRecSignal(GSN_DROP_OBJ_REF, &Dbdict::execDROP_OBJ_REF);
  addRecSignal(GSN_DROP_OBJ_CONF, &Dbdict::execDROP_OBJ_CONF);

  addRecSignal(GSN_CREATE_FILE_REF, &Dbdict::execCREATE_FILE_REF);
  addRecSignal(GSN_CREATE_FILE_CONF, &Dbdict::execCREATE_FILE_CONF);
  addRecSignal(GSN_CREATE_FILEGROUP_REF, &Dbdict::execCREATE_FILEGROUP_REF);
  addRecSignal(GSN_CREATE_FILEGROUP_CONF, &Dbdict::execCREATE_FILEGROUP_CONF);

  addRecSignal(GSN_BACKUP_LOCK_TAB_REQ, &Dbdict::execBACKUP_LOCK_TAB_REQ);

  addRecSignal(GSN_DICT_COMMIT_REQ, &Dbdict::execDICT_COMMIT_REQ);
  addRecSignal(GSN_DICT_COMMIT_REF, &Dbdict::execDICT_COMMIT_REF);
  addRecSignal(GSN_DICT_COMMIT_CONF, &Dbdict::execDICT_COMMIT_CONF);

  addRecSignal(GSN_DICT_ABORT_REQ, &Dbdict::execDICT_ABORT_REQ);
  addRecSignal(GSN_DICT_ABORT_REF, &Dbdict::execDICT_ABORT_REF);
  addRecSignal(GSN_DICT_ABORT_CONF, &Dbdict::execDICT_ABORT_CONF);
  addRecSignal(GSN_SCHEMA_TRANS_BEGIN_REQ, &Dbdict::execSCHEMA_TRANS_BEGIN_REQ);
  addRecSignal(GSN_SCHEMA_TRANS_BEGIN_CONF, &Dbdict::execSCHEMA_TRANS_BEGIN_CONF);
  addRecSignal(GSN_SCHEMA_TRANS_BEGIN_REF, &Dbdict::execSCHEMA_TRANS_BEGIN_REF);
  addRecSignal(GSN_SCHEMA_TRANS_END_REQ, &Dbdict::execSCHEMA_TRANS_END_REQ);
  addRecSignal(GSN_SCHEMA_TRANS_END_CONF, &Dbdict::execSCHEMA_TRANS_END_CONF);
  addRecSignal(GSN_SCHEMA_TRANS_END_REF, &Dbdict::execSCHEMA_TRANS_END_REF);
  addRecSignal(GSN_SCHEMA_TRANS_IMPL_REQ, &Dbdict::execSCHEMA_TRANS_IMPL_REQ);
  addRecSignal(GSN_SCHEMA_TRANS_IMPL_CONF, &Dbdict::execSCHEMA_TRANS_IMPL_CONF);
  addRecSignal(GSN_SCHEMA_TRANS_IMPL_REF, &Dbdict::execSCHEMA_TRANS_IMPL_REF);

  addRecSignal(GSN_DICT_LOCK_REQ, &Dbdict::execDICT_LOCK_REQ);
  addRecSignal(GSN_DICT_UNLOCK_ORD, &Dbdict::execDICT_UNLOCK_ORD);
}//Dbdict::Dbdict()

Dbdict::~Dbdict() 
{
}//Dbdict::~Dbdict()

BLOCK_FUNCTIONS(Dbdict)

void Dbdict::initCommonData() 
{
/* ---------------------------------------------------------------- */
// Initialise all common variables.
/* ---------------------------------------------------------------- */
  initRetrieveRecord(0, 0, 0);
  initSchemaRecord();
  initRestartRecord();
  initSendSchemaRecord();
  initReadTableRecord();
  initWriteTableRecord();
  initReadSchemaRecord();
  initWriteSchemaRecord();

  c_masterNodeId = ZNIL;
  c_numberNode = 0;
  c_noNodesFailed = 0;
  c_failureNr = 0;
  c_packTable.m_state = PackTable::PTS_IDLE;
  c_startPhase = 0;
  c_restartType = 255; //Ensure not used restartType
  c_tabinfoReceived = 0;
  c_initialStart = false;
  c_systemRestart = false;
  c_initialNodeRestart = false;
  c_nodeRestart = false;
}//Dbdict::initCommonData()

void Dbdict::initRecords() 
{
  initNodeRecords();
  initPageRecords();
  initTableRecords();
  initTriggerRecords();
}//Dbdict::initRecords()

void Dbdict::initSendSchemaRecord() 
{
  c_sendSchemaRecord.noOfWords = (Uint32)-1;
  c_sendSchemaRecord.pageId = RNIL;
  c_sendSchemaRecord.noOfWordsCurrentlySent = 0;
  c_sendSchemaRecord.noOfSignalsSentSinceDelay = 0;
  c_sendSchemaRecord.inUse = false;
  //c_sendSchemaRecord.sendSchemaState = SendSchemaRecord::IDLE;
}//initSendSchemaRecord()

void Dbdict::initReadTableRecord() 
{
  c_readTableRecord.no_of_words= 0;
  c_readTableRecord.pageId = RNIL;
  c_readTableRecord.tableId = ZNIL;
  c_readTableRecord.inUse = false;
}//initReadTableRecord()

void Dbdict::initWriteTableRecord() 
{
  c_writeTableRecord.no_of_words= 0;
  c_writeTableRecord.pageId = RNIL;
  c_writeTableRecord.noOfTableFilesHandled = 3;
  c_writeTableRecord.tableId = ZNIL;
  c_writeTableRecord.tableWriteState = WriteTableRecord::IDLE;
}//initWriteTableRecord()

void Dbdict::initReadSchemaRecord() 
{
  c_readSchemaRecord.pageId = RNIL;
  c_readSchemaRecord.schemaReadState = ReadSchemaRecord::IDLE;
}//initReadSchemaRecord()

void Dbdict::initWriteSchemaRecord() 
{
  c_writeSchemaRecord.inUse = false;
  c_writeSchemaRecord.pageId = RNIL;
  c_writeSchemaRecord.noOfSchemaFilesHandled = 3;
}//initWriteSchemaRecord()

void Dbdict::initRetrieveRecord(Signal* signal, Uint32 i, Uint32 returnCode) 
{
  c_retrieveRecord.busyState = false;
  c_retrieveRecord.blockRef = 0;
  c_retrieveRecord.m_senderData = RNIL;
  c_retrieveRecord.tableId = RNIL;
  c_retrieveRecord.currentSent = 0;
  c_retrieveRecord.retrievedNoOfPages = 0;
  c_retrieveRecord.retrievedNoOfWords = 0;
  c_retrieveRecord.m_useLongSig = false;
}//initRetrieveRecord()

void Dbdict::initSchemaRecord() 
{
  c_schemaRecord.schemaPage = RNIL;
  c_schemaRecord.oldSchemaPage = RNIL;
}//Dbdict::initSchemaRecord()

void Dbdict::initRestartRecord() 
{
  c_restartRecord.gciToRestart = 0;
  c_restartRecord.activeTable = ZNIL;
  c_restartRecord.m_pass = 0;
}//Dbdict::initRestartRecord()

void Dbdict::initNodeRecords() 
{
  jam();
  for (unsigned i = 1; i < MAX_NDB_NODES; i++) {
    NodeRecordPtr nodePtr;
    c_nodes.getPtr(nodePtr, i);
    nodePtr.p->hotSpare = false;
    nodePtr.p->nodeState = NodeRecord::API_NODE;
  }//for
}//Dbdict::initNodeRecords()

void Dbdict::initPageRecords() 
{
  c_retrieveRecord.retrievePage =  ZMAX_PAGES_OF_TABLE_DEFINITION;
  ndbrequire(ZNUMBER_OF_PAGES >= (ZMAX_PAGES_OF_TABLE_DEFINITION + 1));
  c_schemaRecord.schemaPage = 0;
  c_schemaRecord.oldSchemaPage = NDB_SF_MAX_PAGES;
}//Dbdict::initPageRecords()

void Dbdict::initTableRecords() 
{
  TableRecordPtr tablePtr;
  while (1) {
    jam();
    refresh_watch_dog();
    c_tableRecordPool.seize(tablePtr);
    if (tablePtr.i == RNIL) {
      jam();
      break;
    }//if
    initialiseTableRecord(tablePtr);
  }//while
}//Dbdict::initTableRecords()

void Dbdict::initialiseTableRecord(TableRecordPtr tablePtr) 
{
  new (tablePtr.p) TableRecord();
  tablePtr.p->activePage = RNIL;
  tablePtr.p->filePtr[0] = RNIL;
  tablePtr.p->filePtr[1] = RNIL;
  tablePtr.p->firstPage = RNIL;
  tablePtr.p->tableId = tablePtr.i;
  tablePtr.p->tableVersion = (Uint32)-1;
  tablePtr.p->tabState = TableRecord::NOT_DEFINED;
  tablePtr.p->tabReturnState = TableRecord::TRS_IDLE;
  tablePtr.p->fragmentType = DictTabInfo::AllNodesSmallTable;
  tablePtr.p->gciTableCreated = 0;
  tablePtr.p->noOfAttributes = ZNIL;
  tablePtr.p->noOfNullAttr = 0;
  tablePtr.p->fragmentCount = 0;
  /*
    tablePtr.p->lh3PageIndexBits = 0;
    tablePtr.p->lh3DistrBits = 0;
    tablePtr.p->lh3PageBits = 6;
  */
  tablePtr.p->kValue = 6;
  tablePtr.p->localKeyLen = 1;
  tablePtr.p->maxLoadFactor = 80;
  tablePtr.p->minLoadFactor = 70;
  tablePtr.p->noOfPrimkey = 1;
  tablePtr.p->tupKeyLength = 1;
  tablePtr.p->maxRowsLow = 0;
  tablePtr.p->maxRowsHigh = 0;
  tablePtr.p->defaultNoPartFlag = true;
  tablePtr.p->linearHashFlag = true;
  tablePtr.p->m_bits = 0;
  tablePtr.p->minRowsLow = 0;
  tablePtr.p->minRowsHigh = 0;
  tablePtr.p->singleUserMode = 0;
  tablePtr.p->tableType = DictTabInfo::UserTable;
  tablePtr.p->primaryTableId = RNIL;
  // volatile elements
  tablePtr.p->indexState = TableRecord::IS_UNDEFINED;
  tablePtr.p->triggerId = RNIL;
  tablePtr.p->buildTriggerId = RNIL;
}//Dbdict::initialiseTableRecord()

void Dbdict::initTriggerRecords()
{
  TriggerRecordPtr triggerPtr;
  while (1) {
    jam();
    refresh_watch_dog();
    c_triggerRecordPool.seize(triggerPtr);
    if (triggerPtr.i == RNIL) {
      jam();
      break;
    }//if
    initialiseTriggerRecord(triggerPtr);
  }//while
}

void Dbdict::initialiseTriggerRecord(TriggerRecordPtr triggerPtr)
{
  new (triggerPtr.p) TriggerRecord();
  triggerPtr.p->triggerState = TriggerRecord::TS_NOT_DEFINED;
  triggerPtr.p->triggerId = RNIL;
  triggerPtr.p->tableId = RNIL;
  triggerPtr.p->attributeMask.clear();
  triggerPtr.p->indexId = RNIL;
}

Uint32 Dbdict::getFsConnRecord() 
{
  FsConnectRecordPtr fsPtr;
  c_fsConnectRecordPool.seize(fsPtr);
  ndbrequire(fsPtr.i != RNIL);
  fsPtr.p->filePtr = (Uint32)-1;
  fsPtr.p->ownerPtr = RNIL;
  fsPtr.p->fsState = FsConnectRecord::IDLE;
  return fsPtr.i;
}//Dbdict::getFsConnRecord()

/*
 * Search schemafile for free entry.  Its index is used as 'logical id'
 * of new disk-stored object.
 */
Uint32 Dbdict::getFreeObjId(Uint32 minId)
{
  const XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  Uint32 noOfPages = xsf->noOfPages;
  Uint32 n, i;
  for (n = 0; n < noOfPages; n++) {
    jam();
    const SchemaFile * sf = &xsf->schemaPage[n];
    for (i = 0; i < NDB_SF_PAGE_ENTRIES; i++) {
      const SchemaFile::TableEntry& te = sf->TableEntries[i];
      if (te.m_tableState == (Uint32)SchemaFile::INIT ||
          te.m_tableState == (Uint32)SchemaFile::DROP_TABLE_COMMITTED) {
        // minId is obsolete anyway
        if (minId <= n * NDB_SF_PAGE_ENTRIES + i)
          return n * NDB_SF_PAGE_ENTRIES + i;
      }
    }
  }
  return RNIL;
}

Uint32 Dbdict::getFreeTableRecord(Uint32 primaryTableId) 
{
  Uint32 minId = (primaryTableId == RNIL ? 0 : primaryTableId + 1);
  Uint32 i = getFreeObjId(minId);
  if (i == RNIL) {
    jam();
    return RNIL;
  }
  if (i >= c_tableRecordPool.getSize()) {
    jam();
    return RNIL;
  }
  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, i);
  ndbrequire(tablePtr.p->tabState == TableRecord::NOT_DEFINED);
  initialiseTableRecord(tablePtr);
  tablePtr.p->tabState = TableRecord::DEFINING;
  return i;
}

Uint32 Dbdict::getFreeTriggerRecord()
{
  const Uint32 size = c_triggerRecordPool.getSize();
  TriggerRecordPtr triggerPtr;
  for (triggerPtr.i = 0; triggerPtr.i < size; triggerPtr.i++) {
    jam();
    c_triggerRecordPool.getPtr(triggerPtr);
    if (triggerPtr.p->triggerState == TriggerRecord::TS_NOT_DEFINED) {
      jam();
      initialiseTriggerRecord(triggerPtr);
      return triggerPtr.i;
    }
  }
  return RNIL;
}

/* **************************************************************** */
/* ---------------------------------------------------------------- */
/* MODULE:          START/RESTART HANDLING ------------------------ */
/* ---------------------------------------------------------------- */
/*                                                                  */
/* This module contains the code that is common for all             */
/* start/restart types.                                             */
/* ---------------------------------------------------------------- */
/* **************************************************************** */

/* ---------------------------------------------------------------- */
// This is sent as the first signal during start/restart.
/* ---------------------------------------------------------------- */
void Dbdict::execSTTOR(Signal* signal) 
{
  jamEntry();
  c_startPhase = signal->theData[1];
  switch (c_startPhase) {
  case 1:
    break;
  case 3:
    c_restartType = signal->theData[7];         /* valid if 3 */
    ndbrequire(c_restartType == NodeState::ST_INITIAL_START ||
               c_restartType == NodeState::ST_SYSTEM_RESTART ||
               c_restartType == NodeState::ST_INITIAL_NODE_RESTART ||
               c_restartType == NodeState::ST_NODE_RESTART);
    break;
  }
  sendSTTORRY(signal);
}//execSTTOR()

void Dbdict::sendSTTORRY(Signal* signal)
{
  signal->theData[0] = 0;       /* garbage SIGNAL KEY */
  signal->theData[1] = 0;       /* garbage SIGNAL VERSION NUMBER  */
  signal->theData[2] = 0;       /* garbage */
  signal->theData[3] = 1;       /* first wanted start phase */
  signal->theData[4] = 3;       /* get type of start */
  signal->theData[5] = ZNOMOREPHASES;
  sendSignal(NDBCNTR_REF, GSN_STTORRY, signal, 6, JBB);
}

/* ---------------------------------------------------------------- */
// We receive information about sizes of records.
/* ---------------------------------------------------------------- */
void Dbdict::execREAD_CONFIG_REQ(Signal* signal) 
{
  const ReadConfigReq * req = (ReadConfigReq*)signal->getDataPtr();
  Uint32 ref = req->senderRef;
  Uint32 senderData = req->senderData;
  ndbrequire(req->noOfParameters == 0);

  jamEntry();
 
  const ndb_mgm_configuration_iterator * p = 
    m_ctx.m_config.getOwnConfigIterator();
  ndbrequire(p != 0);
  
  Uint32 attributesize, tablerecSize;
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_DB_NO_TRIGGERS, 
					&c_maxNoOfTriggers));
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_DICT_ATTRIBUTE,&attributesize));
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_DICT_TABLE, &tablerecSize));

  c_attributeRecordPool.setSize(attributesize);
  c_attributeRecordHash.setSize(64);
  c_fsConnectRecordPool.setSize(ZFS_CONNECT_SIZE);
  c_nodes.setSize(MAX_NDB_NODES);
  c_pageRecordArray.setSize(ZNUMBER_OF_PAGES);
  c_schemaPageRecordArray.setSize(2 * NDB_SF_MAX_PAGES);
  c_tableRecordPool.setSize(tablerecSize);
  g_key_descriptor_pool.setSize(tablerecSize);
  c_triggerRecordPool.setSize(c_maxNoOfTriggers);

  c_opSectionBufferPool.setSize(1024); // units OpSectionSegmentSize
  c_schemaOpPool.setSize(256);
  c_schemaOpHash.setSize(256);
  c_schemaTransPool.setSize(5);
  c_schemaTransHash.setSize(2);
  c_txHandlePool.setSize(2);
  c_txHandleHash.setSize(2);

  c_obj_pool.setSize(tablerecSize+c_maxNoOfTriggers);
  c_obj_hash.setSize((tablerecSize+c_maxNoOfTriggers+1)/2);
  m_dict_lock_pool.setSize(MAX_NDB_NODES);

  Pool_context pc;
  pc.m_block = this;

  c_file_hash.setSize(16);
  c_filegroup_hash.setSize(16);

  c_file_pool.init(RT_DBDICT_FILE, pc);
  c_filegroup_pool.init(RT_DBDICT_FILEGROUP, pc);

  // new OpRec pools
  c_createTableRecPool.setSize(32);
  c_dropTableRecPool.setSize(32);
  c_alterTableRecPool.setSize(32);
  c_createTriggerRecPool.setSize(32);
  c_dropTriggerRecPool.setSize(32);
  c_createIndexRecPool.setSize(32);
  c_dropIndexRecPool.setSize(32);
  c_alterIndexRecPool.setSize(32);
  c_buildIndexRecPool.setSize(32);
  
  c_opRecordPool.setSize(256);   // XXX need config params
  c_opCreateEvent.setSize(2);
  c_opSubEvent.setSize(2);
  c_opDropEvent.setSize(2);
  c_opSignalUtil.setSize(8);

  // Initialize schema file copies
  c_schemaFile[0].schemaPage =
    (SchemaFile*)c_schemaPageRecordArray.getPtr(0 * NDB_SF_MAX_PAGES);
  c_schemaFile[0].noOfPages = 0;
  c_schemaFile[1].schemaPage =
    (SchemaFile*)c_schemaPageRecordArray.getPtr(1 * NDB_SF_MAX_PAGES);
  c_schemaFile[1].noOfPages = 0;

  c_schemaOperation.setSize(8);
  //c_opDropObj.setSize(8);
  c_Trans.setSize(8);

  Uint32 rps = 0;
  rps += tablerecSize * (MAX_TAB_NAME_SIZE + MAX_FRM_DATA_SIZE);
  rps += attributesize * (MAX_ATTR_NAME_SIZE + MAX_ATTR_DEFAULT_VALUE_SIZE);
  rps += c_maxNoOfTriggers * MAX_TAB_NAME_SIZE;
  rps += (10 + 10) * MAX_TAB_NAME_SIZE;

  Uint32 sm = 5;
  ndb_mgm_get_int_parameter(p, CFG_DB_STRING_MEMORY, &sm);
  if (sm == 0)
    sm = 5;
  
  Uint32 sb = 0;
  if (sm < 100)
  {
    sb = (rps * sm) / 100;
  }
  else
  {
    sb = sm;
  }
  
  c_rope_pool.setSize(sb/28 + 100);
  
  // Initialize BAT for interface to file system
  NewVARIABLE* bat = allocateBat(2);
  bat[0].WA = &c_schemaPageRecordArray.getPtr(0)->word[0];
  bat[0].nrr = 2 * NDB_SF_MAX_PAGES;
  bat[0].ClusterSize = NDB_SF_PAGE_SIZE;
  bat[0].bits.q = NDB_SF_PAGE_SIZE_IN_WORDS_LOG2;
  bat[0].bits.v = 5;  // 32 bits per element
  bat[1].WA = &c_pageRecordArray.getPtr(0)->word[0];
  bat[1].nrr = ZNUMBER_OF_PAGES;
  bat[1].ClusterSize = ZSIZE_OF_PAGES_IN_WORDS * 4;
  bat[1].bits.q = ZLOG_SIZE_OF_PAGES_IN_WORDS; // 2**13 = 8192 elements
  bat[1].bits.v = 5;  // 32 bits per element

  initCommonData();
  initRecords();

  ReadConfigConf * conf = (ReadConfigConf*)signal->getDataPtrSend();
  conf->senderRef = reference();
  conf->senderData = senderData;
  sendSignal(ref, GSN_READ_CONFIG_CONF, signal, 
	     ReadConfigConf::SignalLength, JBB);

  {
    Ptr<DictObject> ptr;
    SLList<DictObject> objs(c_obj_pool);
    while(objs.seize(ptr))
      new (ptr.p) DictObject();
    objs.release();
  }

#ifdef VM_TRACE
  {
    FILE* debugFile = globalSignalLoggers.getOutputStream();
    if (debugFile != NULL)
      debugOut = *new NdbOut(*new FileOutputStream(debugFile));
  }
#endif
}//execSIZEALT_REP()

#ifdef VM_TRACE
bool
Dbdict::debugOutOn() const
{
  SignalLoggerManager::LogMode mask = SignalLoggerManager::LogInOut;
  return globalSignalLoggers.logMatch(DBDICT, mask);
}
#endif

/* ---------------------------------------------------------------- */
// Start phase signals sent by CNTR. We reply with NDB_STTORRY when
// we completed this phase.
/* ---------------------------------------------------------------- */
void Dbdict::execNDB_STTOR(Signal* signal) 
{
  jamEntry();
  c_startPhase = signal->theData[2];
  const Uint32 restartType = signal->theData[3];
  if (restartType == NodeState::ST_INITIAL_START) {
    jam();
    c_initialStart = true;
  } else if (restartType == NodeState::ST_SYSTEM_RESTART) {
    jam();
    c_systemRestart = true;
  } else if (restartType == NodeState::ST_INITIAL_NODE_RESTART) {
    jam();
    c_initialNodeRestart = true;
  } else if (restartType == NodeState::ST_NODE_RESTART) {
    jam();
    c_nodeRestart = true;
  } else {
    ndbrequire(false);
  }//if
  switch (c_startPhase) {
  case 1:
    jam();
    initSchemaFile(signal);
    break;
  case 3:
    jam();
    signal->theData[0] = reference();
    sendSignal(NDBCNTR_REF, GSN_READ_NODESREQ, signal, 1, JBB);
    break;
  case 6:
    jam();
    c_initialStart = false;
    c_systemRestart = false;
    c_initialNodeRestart = false;
    c_nodeRestart = false;
    sendNDB_STTORRY(signal);
    break;
  case 7:
    // uses c_restartType
    if(restartType == NodeState::ST_SYSTEM_RESTART &&
       c_masterNodeId == getOwnNodeId()){
      rebuildIndexes(signal, 0);
      return;
    }
    sendNDB_STTORRY(signal);
    break;
  default:
    jam();
    sendNDB_STTORRY(signal);
    break;
  }//switch
}//execNDB_STTOR()

void Dbdict::sendNDB_STTORRY(Signal* signal) 
{
  signal->theData[0] = reference();
  sendSignal(NDBCNTR_REF, GSN_NDB_STTORRY, signal, 1, JBB);
  return;
}//sendNDB_STTORRY()

/* ---------------------------------------------------------------- */
// We receive the information about which nodes that are up and down.
/* ---------------------------------------------------------------- */
void Dbdict::execREAD_NODESCONF(Signal* signal) 
{
  jamEntry();

  ReadNodesConf * const readNodes = (ReadNodesConf *)&signal->theData[0];
  c_numberNode   = readNodes->noOfNodes;
  c_masterNodeId = readNodes->masterNodeId;

  c_noNodesFailed = 0;
  c_aliveNodes.clear();
  for (unsigned i = 1; i < MAX_NDB_NODES; i++) {
    jam();
    NodeRecordPtr nodePtr;
    c_nodes.getPtr(nodePtr, i);

    if (NdbNodeBitmask::get(readNodes->allNodes, i)) {
      jam();
      nodePtr.p->nodeState = NodeRecord::NDB_NODE_ALIVE;
      if (NdbNodeBitmask::get(readNodes->inactiveNodes, i)) {
	jam();
	/**-------------------------------------------------------------------
	 *
	 * THIS NODE IS DEFINED IN THE CLUSTER BUT IS NOT ALIVE CURRENTLY.
	 * WE ADD THE NODE TO THE SET OF FAILED NODES AND ALSO SET THE
	 * BLOCKSTATE TO BUSY TO AVOID ADDING TABLES WHILE NOT ALL NODES ARE
	 * ALIVE.
	 *------------------------------------------------------------------*/
        nodePtr.p->nodeState = NodeRecord::NDB_NODE_DEAD;
	c_noNodesFailed++;
      } else {
	c_aliveNodes.set(i);
      }
    }//if
  }//for
  sendNDB_STTORRY(signal);
}//execREAD_NODESCONF()

/* ---------------------------------------------------------------- */
// HOT_SPAREREP informs DBDICT about which nodes that have become
// hot spare nodes.
/* ---------------------------------------------------------------- */
void Dbdict::execHOT_SPAREREP(Signal* signal) 
{
  Uint32 hotSpareNodes = 0;
  jamEntry();
  HotSpareRep * const hotSpare = (HotSpareRep*)&signal->theData[0];
  for (unsigned i = 1; i < MAX_NDB_NODES; i++) {
    if (NdbNodeBitmask::get(hotSpare->theHotSpareNodes, i)) {
      NodeRecordPtr nodePtr;
      c_nodes.getPtr(nodePtr, i);
      nodePtr.p->hotSpare = true;
      hotSpareNodes++;
    }//if
  }//for
  ndbrequire(hotSpareNodes == hotSpare->noHotSpareNodes);
  c_noHotSpareNodes = hotSpareNodes;
  return;
}//execHOT_SPAREREP()

void Dbdict::initSchemaFile(Signal* signal) 
{
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  xsf->noOfPages = (c_tableRecordPool.getSize() + NDB_SF_PAGE_ENTRIES - 1)
                   / NDB_SF_PAGE_ENTRIES;
  initSchemaFile(xsf, 0, xsf->noOfPages, true);
  // init alt copy too for INR
  XSchemaFile * oldxsf = &c_schemaFile[c_schemaRecord.oldSchemaPage != 0];
  oldxsf->noOfPages = xsf->noOfPages;
  memcpy(&oldxsf->schemaPage[0], &xsf->schemaPage[0], xsf->schemaPage[0].FileSize);
  
  if (c_initialStart || c_initialNodeRestart) {    
    jam();
    ndbrequire(c_writeSchemaRecord.inUse == false);
    c_writeSchemaRecord.inUse = true;
    c_writeSchemaRecord.pageId = c_schemaRecord.schemaPage;
    c_writeSchemaRecord.newFile = true;
    c_writeSchemaRecord.firstPage = 0;
    c_writeSchemaRecord.noOfPages = xsf->noOfPages;

    c_writeSchemaRecord.m_callback.m_callbackFunction = 
      safe_cast(&Dbdict::initSchemaFile_conf);
    
    startWriteSchemaFile(signal);
  } else if (c_systemRestart || c_nodeRestart) {
    jam();
    ndbrequire(c_readSchemaRecord.schemaReadState == ReadSchemaRecord::IDLE);
    c_readSchemaRecord.pageId = c_schemaRecord.oldSchemaPage;
    c_readSchemaRecord.firstPage = 0;
    c_readSchemaRecord.noOfPages = 1;
    c_readSchemaRecord.schemaReadState = ReadSchemaRecord::INITIAL_READ_HEAD;
    startReadSchemaFile(signal);
  } else {
    ndbrequire(false);
  }//if
}//Dbdict::initSchemaFile()

void
Dbdict::initSchemaFile_conf(Signal* signal, Uint32 callbackData, Uint32 rv){
  jam();
  sendNDB_STTORRY(signal);
}

void
Dbdict::activateIndexes(Signal* signal, Uint32 i)
{
  if (i == 0)
    D("activateIndexes start");

  Uint32 requestFlags = 0;

  switch (c_restartType) {
  case NodeState::ST_SYSTEM_RESTART:
    // activate in a distributed trans but do not build yet
    if (c_masterNodeId != getOwnNodeId()) {
      D("activateIndexes not master");
      goto out;
    }
    requestFlags |= DictSignal::RF_NO_BUILD;
    break;
  case NodeState::ST_NODE_RESTART:
  case NodeState::ST_INITIAL_NODE_RESTART:
    // activate on this node in a local trans
    requestFlags |= DictSignal::RF_LOCAL_TRANS;
    requestFlags |= DictSignal::RF_NO_BUILD;
    break;
  default:
    ndbrequire(false);
    break;
  }

  TableRecordPtr indexPtr;
  indexPtr.i = i;
  for (; indexPtr.i < c_tableRecordPool.getSize(); indexPtr.i++)
  {
    c_tableRecordPool.getPtr(indexPtr);

    if (indexPtr.p->tabState != TableRecord::DEFINED)
    {
      continue;
    }

    if (!indexPtr.p->isIndex())
    {
      continue;
    }

    if ((requestFlags & DictSignal::RF_LOCAL_TRANS) &&
        indexPtr.p->indexState != TableRecord::IS_ONLINE)
    {
      jam();
      continue;
    }

    // wl3600_todo use simple schema trans when implemented
    D("activateIndexes i=" << indexPtr.i);

    TxHandlePtr tx_ptr;
    seizeTxHandle(tx_ptr);
    ndbrequire(!tx_ptr.isNull());

    tx_ptr.p->m_requestInfo = 0;
    tx_ptr.p->m_requestInfo |= requestFlags;
    tx_ptr.p->m_userData = indexPtr.i;

    Callback c = {
      safe_cast(&Dbdict::activateIndex_fromBeginTrans),
      tx_ptr.p->tx_key
    };
    tx_ptr.p->m_callback = c;
    beginSchemaTrans(signal, tx_ptr);
    return;
  }

  D("activateIndexes done");
#ifdef VM_TRACE
  check_consistency();
#endif
out:
  signal->theData[0] = reference();
  sendSignal(c_restartRecord.returnBlockRef, GSN_DICTSTARTCONF,
	     signal, 1, JBB);
}

void
Dbdict::activateIndex_fromBeginTrans(Signal* signal, Uint32 tx_key, Uint32 ret)
{
  D("activateIndex_fromBeginTrans" << V(tx_key) << V(ret));

  ndbrequire(ret == 0); //wl3600_todo

  TxHandlePtr tx_ptr;
  findTxHandle(tx_ptr, tx_key);
  ndbrequire(!tx_ptr.isNull());

  TableRecordPtr indexPtr;
  indexPtr.i = tx_ptr.p->m_userData;
  c_tableRecordPool.getPtr(indexPtr);

  AlterIndxReq* req = (AlterIndxReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, AlterIndxImplReq::AlterIndexOnline);
  DictSignal::addRequestFlagsGlobal(requestInfo, tx_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = tx_ptr.p->tx_key;
  req->transId = tx_ptr.p->m_transId;
  req->transKey = tx_ptr.p->m_transKey;
  req->requestInfo = requestInfo;
  req->indexId = indexPtr.i;
  req->indexVersion = indexPtr.p->tableVersion;

  Callback c = {
    safe_cast(&Dbdict::activateIndex_fromAlterIndex),
    tx_ptr.p->tx_key
  };
  tx_ptr.p->m_callback = c;

  sendSignal(reference(), GSN_ALTER_INDX_REQ, signal,
             AlterIndxReq::SignalLength, JBB);
}

void
Dbdict::activateIndex_fromAlterIndex(Signal* signal, Uint32 tx_key, Uint32 ret)
{
  D("activateIndex_fromAlterIndex" << V(tx_key) << V(ret));

  ndbrequire(ret == 0); // wl3600_todo

  TxHandlePtr tx_ptr;
  findTxHandle(tx_ptr, tx_key);
  ndbrequire(!tx_ptr.isNull());

  if (ret != 0)
    setError(tx_ptr.p->m_error, ret, __LINE__);

  Callback c = {
    safe_cast(&Dbdict::activateIndex_fromEndTrans),
    tx_ptr.p->tx_key
  };
  tx_ptr.p->m_callback = c;

  Uint32 flags = 0;
  if (hasError(tx_ptr.p->m_error))
    flags |= SchemaTransEndReq::SchemaTransAbort;
  endSchemaTrans(signal, tx_ptr, flags);
}

void
Dbdict::activateIndex_fromEndTrans(Signal* signal, Uint32 tx_key, Uint32 ret)
{
  D("activateIndex_fromEndTrans" << V(tx_key) << V(ret));

  ndbrequire(ret == 0); //wl3600_todo

  TxHandlePtr tx_ptr;
  findTxHandle(tx_ptr, tx_key);
  ndbrequire(!tx_ptr.isNull());

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, tx_ptr.p->m_userData);

  char indexName[MAX_TAB_NAME_SIZE];
  {
    DictObjectPtr obj_ptr;
    c_obj_pool.getPtr(obj_ptr, indexPtr.p->m_obj_ptr_i);
    Rope name(c_rope_pool, obj_ptr.p->m_name);
    name.copy(indexName);
  }

  ErrorInfo error = tx_ptr.p->m_error;
  if (!hasError(error))
  {
    jam();
    infoEvent("DICT: activate index %u done (%s)",
	      indexPtr.i, indexName);
  }
  else
  {
    jam();
    warningEvent("DICT: activate index %u error: code=%u line=%u node=%u (%s)",
		 indexPtr.i,
		 error.errorCode, error.errorLine, error.errorNodeId,
		 indexName);
  }

  releaseTxHandle(tx_ptr);
  activateIndexes(signal, indexPtr.i + 1);
}

void
Dbdict::rebuildIndexes(Signal* signal, Uint32 i)
{
  if (i == 0)
    D("rebuildIndexes start");

  TableRecordPtr indexPtr;
  indexPtr.i = i;
  for (; indexPtr.i < c_tableRecordPool.getSize(); indexPtr.i++) {
    c_tableRecordPool.getPtr(indexPtr);
    if (indexPtr.p->tabState != TableRecord::DEFINED)
      continue;
    if (!indexPtr.p->isIndex())
      continue;

    // wl3600_todo use simple schema trans when implemented
    D("rebuildIndexes i=" << indexPtr.i);

    TxHandlePtr tx_ptr;
    seizeTxHandle(tx_ptr);
    ndbrequire(!tx_ptr.isNull());

    Uint32 requestInfo = 0;
    if (indexPtr.p->m_bits & TableRecord::TR_Logged) {
      // only sets index online - the flag propagates to trans and ops
      requestInfo |= DictSignal::RF_NO_BUILD;
    }
    tx_ptr.p->m_requestInfo = requestInfo;
    tx_ptr.p->m_userData = indexPtr.i;

    Callback c = {
      safe_cast(&Dbdict::rebuildIndex_fromBeginTrans),
      tx_ptr.p->tx_key
    };
    tx_ptr.p->m_callback = c;
    beginSchemaTrans(signal, tx_ptr);
    return;
  }

  D("rebuildIndexes done");
  sendNDB_STTORRY(signal);
}

void
Dbdict::rebuildIndex_fromBeginTrans(Signal* signal, Uint32 tx_key, Uint32 ret)
{
  D("rebuildIndex_fromBeginTrans" << V(tx_key) << V(ret));

  ndbrequire(ret == 0); //wl3600_todo

  TxHandlePtr tx_ptr;
  findTxHandle(tx_ptr, tx_key);
  ndbrequire(!tx_ptr.isNull());

  TableRecordPtr indexPtr;
  indexPtr.i = tx_ptr.p->m_userData;
  c_tableRecordPool.getPtr(indexPtr);

  BuildIndxReq* req = (BuildIndxReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, BuildIndxReq::MainOp);
  DictSignal::addRequestFlagsGlobal(requestInfo, tx_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = tx_ptr.p->tx_key;
  req->transId = tx_ptr.p->m_transId;
  req->transKey = tx_ptr.p->m_transKey;
  req->requestInfo = requestInfo;
  req->buildId = 0;
  req->buildKey = 0;
  req->tableId = indexPtr.p->primaryTableId;
  req->indexId = indexPtr.i;
  req->indexType = indexPtr.p->tableType;
  req->parallelism = 16;

  Callback c = {
    safe_cast(&Dbdict::rebuildIndex_fromBuildIndex),
    tx_ptr.p->tx_key
  };
  tx_ptr.p->m_callback = c;

  sendSignal(reference(), GSN_BUILDINDXREQ, signal,
             BuildIndxReq::SignalLength, JBB);
}

void
Dbdict::rebuildIndex_fromBuildIndex(Signal* signal, Uint32 tx_key, Uint32 ret)
{
  D("rebuildIndex_fromBuildIndex" << V(tx_key) << V(ret));

  ndbrequire(ret == 0); //wl3600_todo

  TxHandlePtr tx_ptr;
  findTxHandle(tx_ptr, tx_key);
  ndbrequire(!tx_ptr.isNull());

  if (ret != 0)
    setError(tx_ptr.p->m_error, ret, __LINE__);

  Callback c = {
    safe_cast(&Dbdict::rebuildIndex_fromEndTrans),
    tx_ptr.p->tx_key
  };
  tx_ptr.p->m_callback = c;

  Uint32 flags = 0;
  if (hasError(tx_ptr.p->m_error))
    flags |= SchemaTransEndReq::SchemaTransAbort;
  endSchemaTrans(signal, tx_ptr, flags);
}

void
Dbdict::rebuildIndex_fromEndTrans(Signal* signal, Uint32 tx_key, Uint32 ret)
{
  D("rebuildIndex_fromEndTrans" << V(tx_key) << V(ret));

  ndbrequire(ret == 0); //wl3600_todo

  TxHandlePtr tx_ptr;
  findTxHandle(tx_ptr, tx_key);
  ndbrequire(!tx_ptr.isNull());

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, tx_ptr.p->m_userData);

  const char* actionName;
  {
    Uint32 requestInfo = tx_ptr.p->m_requestInfo;
    bool noBuild = (requestInfo & DictSignal::RF_NO_BUILD);
    actionName = !noBuild ? "rebuild" : "online";
  }

  char indexName[MAX_TAB_NAME_SIZE];
  {
    DictObjectPtr obj_ptr;
    c_obj_pool.getPtr(obj_ptr, indexPtr.p->m_obj_ptr_i);
    Rope name(c_rope_pool, obj_ptr.p->m_name);
    name.copy(indexName);
  }

  ErrorInfo error = tx_ptr.p->m_error;
  if (!hasError(error)) {
    jam();
    infoEvent(
        "DICT: %s index %u done (%s)",
        actionName, indexPtr.i, indexName);
  } else {
    jam();
    warningEvent(
        "DICT: %s index %u error: code=%u line=%u node=%u (%s)",
        actionName,
        indexPtr.i, error.errorCode, error.errorLine, error.errorNodeId,
        indexName);
  }

  Uint32 i = tx_ptr.p->m_userData;
  releaseTxHandle(tx_ptr);

  rebuildIndexes(signal, i + 1);
}

/* **************************************************************** */
/* ---------------------------------------------------------------- */
/* MODULE:          SYSTEM RESTART MODULE ------------------------- */
/* ---------------------------------------------------------------- */
/*                                                                  */
/* This module contains code specific for system restart            */
/* ---------------------------------------------------------------- */
/* **************************************************************** */

/* ---------------------------------------------------------------- */
// DIH asks DICT to read in table data from disk during system
// restart. DIH also asks DICT to send information about which
// tables that should be started as part of this system restart.
// DICT will also activate the tables in TC as part of this process.
/* ---------------------------------------------------------------- */
void Dbdict::execDICTSTARTREQ(Signal* signal) 
{
  jamEntry();
  c_restartRecord.gciToRestart = signal->theData[0];
  c_restartRecord.returnBlockRef = signal->theData[1];
  if (c_nodeRestart || c_initialNodeRestart) {
    jam();   

    CRASH_INSERTION(6000);

    BlockReference dictRef = calcDictBlockRef(c_masterNodeId);
    signal->theData[0] = getOwnNodeId();
    sendSignal(dictRef, GSN_GET_SCHEMA_INFOREQ, signal, 1, JBB);
    return;
  }
  ndbrequire(c_systemRestart);
  ndbrequire(c_masterNodeId == getOwnNodeId());

  c_schemaRecord.m_callback.m_callbackData = 0;
  c_schemaRecord.m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::masterRestart_checkSchemaStatusComplete);

  c_restartRecord.m_pass = 0;
  c_restartRecord.activeTable = 0;
  c_schemaRecord.schemaPage = c_schemaRecord.oldSchemaPage; // ugly
  checkSchemaStatus(signal);
}//execDICTSTARTREQ()

void
Dbdict::masterRestart_checkSchemaStatusComplete(Signal* signal,
						Uint32 callbackData,
						Uint32 returnCode){

  c_schemaRecord.schemaPage = 0; // ugly
  XSchemaFile * oldxsf = &c_schemaFile[c_schemaRecord.oldSchemaPage != 0];
  ndbrequire(oldxsf->noOfPages != 0);

  LinearSectionPtr ptr[3];
  ptr[0].p = (Uint32*)&oldxsf->schemaPage[0];
  ptr[0].sz = oldxsf->noOfPages * NDB_SF_PAGE_SIZE_IN_WORDS;

  c_sendSchemaRecord.m_SCHEMAINFO_Counter = c_aliveNodes;
  NodeReceiverGroup rg(DBDICT, c_aliveNodes);

  rg.m_nodes.clear(getOwnNodeId());
  Callback c = { 0, 0 };
  sendFragmentedSignal(rg,
		       GSN_SCHEMA_INFO,
		       signal, 
		       1, //SchemaInfo::SignalLength,
		       JBB,
		       ptr,
		       1,
		       c);

  XSchemaFile * newxsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  newxsf->noOfPages = oldxsf->noOfPages;
  memcpy(&newxsf->schemaPage[0], &oldxsf->schemaPage[0],
         oldxsf->noOfPages * NDB_SF_PAGE_SIZE);

  signal->theData[0] = getOwnNodeId();
  sendSignal(reference(), GSN_SCHEMA_INFOCONF, signal, 1, JBB);
}

void 
Dbdict::execGET_SCHEMA_INFOREQ(Signal* signal){

  const Uint32 ref = signal->getSendersBlockRef();
  //const Uint32 senderData = signal->theData[0];
  
  ndbrequire(c_sendSchemaRecord.inUse == false);
  c_sendSchemaRecord.inUse = true;

  LinearSectionPtr ptr[3];
  
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  ndbrequire(xsf->noOfPages != 0);
  
  ptr[0].p = (Uint32*)&xsf->schemaPage[0];
  ptr[0].sz = xsf->noOfPages * NDB_SF_PAGE_SIZE_IN_WORDS;

  Callback c = { safe_cast(&Dbdict::sendSchemaComplete), 0 };
  sendFragmentedSignal(ref,
		       GSN_SCHEMA_INFO,
		       signal, 
		       1, //GetSchemaInfoConf::SignalLength,
		       JBB,
		       ptr,
		       1,
		       c);
}//Dbdict::execGET_SCHEMA_INFOREQ()

void
Dbdict::sendSchemaComplete(Signal * signal, 
			   Uint32 callbackData,
			   Uint32 returnCode){
  ndbrequire(c_sendSchemaRecord.inUse == true);
  c_sendSchemaRecord.inUse = false;

}


/* ---------------------------------------------------------------- */
// We receive the schema info from master as part of all restarts
// except the initial start where no tables exists.
/* ---------------------------------------------------------------- */
void Dbdict::execSCHEMA_INFO(Signal* signal) 
{
  jamEntry();
  if(!assembleFragments(signal)){
    jam();
    return;
  }

  if(getNodeState().getNodeRestartInProgress()){
    CRASH_INSERTION(6001);
  }

  SectionHandle handle(this, signal);
  SegmentedSectionPtr schemaDataPtr;
  handle.getSection(schemaDataPtr, 0);

  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  ndbrequire(schemaDataPtr.sz % NDB_SF_PAGE_SIZE_IN_WORDS == 0);
  xsf->noOfPages = schemaDataPtr.sz / NDB_SF_PAGE_SIZE_IN_WORDS;
  copy((Uint32*)&xsf->schemaPage[0], schemaDataPtr);
  releaseSections(handle);
  
  SchemaFile * sf0 = &xsf->schemaPage[0];
  if (sf0->NdbVersion < NDB_SF_VERSION_5_0_6) {
    bool ok = convertSchemaFileTo_5_0_6(xsf);
    ndbrequire(ok);
  }
    
  validateChecksum(xsf);

  XSchemaFile * oldxsf = &c_schemaFile[c_schemaRecord.oldSchemaPage != 0];
  resizeSchemaFile(xsf, oldxsf->noOfPages);

  ndbrequire(signal->getSendersBlockRef() != reference());
    
  /* ---------------------------------------------------------------- */
  // Synchronise our view on data with other nodes in the cluster.
  // This is an important part of restart handling where we will handle
  // cases where the table have been added but only partially, where
  // tables have been deleted but not completed the deletion yet and
  // other scenarios needing synchronisation.
  /* ---------------------------------------------------------------- */
  c_schemaRecord.m_callback.m_callbackData = 0;
  c_schemaRecord.m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::restart_checkSchemaStatusComplete);

  c_restartRecord.m_pass= 0;
  c_restartRecord.activeTable = 0;
  checkSchemaStatus(signal);
}//execSCHEMA_INFO()

void
Dbdict::restart_checkSchemaStatusComplete(Signal * signal, 
					  Uint32 callbackData,
					  Uint32 returnCode)
{
  jam();
  D("restart_checkSchemaStatusComplete");

  ndbrequire(c_writeSchemaRecord.inUse == false);
  c_writeSchemaRecord.inUse = true;
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  c_writeSchemaRecord.pageId = c_schemaRecord.schemaPage;
  c_writeSchemaRecord.newFile = true;
  c_writeSchemaRecord.firstPage = 0;
  c_writeSchemaRecord.noOfPages = xsf->noOfPages;
  c_writeSchemaRecord.m_callback.m_callbackData = 0;
  c_writeSchemaRecord.m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::restart_writeSchemaConf);
  
  for(Uint32 i = 0; i<xsf->noOfPages; i++)
    computeChecksum(xsf, i);  

  startWriteSchemaFile(signal);
}

void
Dbdict::restart_writeSchemaConf(Signal * signal, 
				Uint32 callbackData,
				Uint32 returnCode)
{
  jam();
  D("restart_writeSchemaConf");

  if(c_systemRestart){
    jam();
    signal->theData[0] = getOwnNodeId();
    sendSignal(calcDictBlockRef(c_masterNodeId), GSN_SCHEMA_INFOCONF,
	       signal, 1, JBB);
    return;
  }
  
  ndbrequire(c_nodeRestart || c_initialNodeRestart);
  activateIndexes(signal, 0);
  return;
}

void Dbdict::execSCHEMA_INFOCONF(Signal* signal) 
{
  jamEntry();
  ndbrequire(signal->getNoOfSections() == 0);

/* ---------------------------------------------------------------- */
// This signal is received in the master as part of system restart
// from all nodes (including the master) after they have synchronised
// their data with the master node's schema information.
/* ---------------------------------------------------------------- */
  const Uint32 nodeId = signal->theData[0];
  c_sendSchemaRecord.m_SCHEMAINFO_Counter.clearWaitingFor(nodeId);

  if (!c_sendSchemaRecord.m_SCHEMAINFO_Counter.done()){
    jam();
    return;
  }//if
  activateIndexes(signal, 0);
}//execSCHEMA_INFOCONF()

static bool 
checkSchemaStatus(Uint32 tableType, Uint32 pass)
{
  switch(tableType){
  case DictTabInfo::UndefTableType:
    return true;
  case DictTabInfo::HashIndexTrigger:
  case DictTabInfo::SubscriptionTrigger:
  case DictTabInfo::ReadOnlyConstraint:
  case DictTabInfo::IndexTrigger:
    return false;
  case DictTabInfo::LogfileGroup:
    return pass == 0 || pass == 9 || pass == 10;
  case DictTabInfo::Tablespace:
    return pass == 1 || pass == 8 || pass == 11;
  case DictTabInfo::Datafile:
  case DictTabInfo::Undofile:
    return pass == 2 || pass == 7 || pass == 12;
  case DictTabInfo::SystemTable:
  case DictTabInfo::UserTable:
    return /* pass == 3 || pass == 6 || */ pass == 13;
  case DictTabInfo::UniqueHashIndex:
  case DictTabInfo::HashIndex:
  case DictTabInfo::UniqueOrderedIndex:
  case DictTabInfo::OrderedIndex:
    return /* pass == 4 || pass == 5 || */ pass == 14;
  }
  
  return false;
}

static const Uint32 CREATE_OLD_PASS = 4;
static const Uint32 DROP_OLD_PASS = 9;
static const Uint32 CREATE_NEW_PASS = 14;
static const Uint32 LAST_PASS = 14;

NdbOut&
operator<<(NdbOut& out, const SchemaFile::TableEntry entry)
{
  out << "[";
  out << " state: " << entry.m_tableState;
  out << " version: " << hex << entry.m_tableVersion << dec;
  out << " type: " << entry.m_tableType;
  out << " words: " << entry.m_info_words;
  out << " gcp: " << entry.m_gcp;
  out << " trans: " << entry.m_transId;
  out << " ]";
  return out;
}

/**
 * Pass 0  Create old LogfileGroup
 * Pass 1  Create old Tablespace
 * Pass 2  Create old Datafile/Undofile
 * Pass 3  Create old Table           // NOT DONE DUE TO DIH
 * Pass 4  Create old Index           // NOT DONE DUE TO DIH
 
 * Pass 5  Drop old Index             // NOT DONE DUE TO DIH
 * Pass 6  Drop old Table             // NOT DONE DUE TO DIH
 * Pass 7  Drop old Datafile/Undofile
 * Pass 8  Drop old Tablespace
 * Pass 9  Drop old Logfilegroup
 
 * Pass 10 Create new LogfileGroup
 * Pass 11 Create new Tablespace
 * Pass 12 Create new Datafile/Undofile
 * Pass 13 Create new Table
 * Pass 14 Create new Index
 */

void Dbdict::checkSchemaStatus(Signal* signal) 
{
  XSchemaFile * newxsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  XSchemaFile * oldxsf = &c_schemaFile[c_schemaRecord.oldSchemaPage != 0];
  ndbrequire(newxsf->noOfPages == oldxsf->noOfPages);
  const Uint32 noOfEntries = newxsf->noOfPages * NDB_SF_PAGE_ENTRIES;

  for (; c_restartRecord.activeTable < noOfEntries;
       c_restartRecord.activeTable++) {
    jam();

    Uint32 tableId = c_restartRecord.activeTable;
    SchemaFile::TableEntry *newEntry = getTableEntry(newxsf, tableId);
    SchemaFile::TableEntry *oldEntry = getTableEntry(oldxsf, tableId);
    SchemaFile::TableState newSchemaState = 
      (SchemaFile::TableState)newEntry->m_tableState;
    SchemaFile::TableState oldSchemaState = 
      (SchemaFile::TableState)oldEntry->m_tableState;

    if (c_restartRecord.activeTable >= c_tableRecordPool.getSize()) {
      jam();
      ndbrequire(newSchemaState == SchemaFile::INIT);
      ndbrequire(oldSchemaState == SchemaFile::INIT);
      continue;
    }//if

    D("checkSchemaStatus" << V(*oldEntry) << V(*newEntry));

//#define PRINT_SCHEMA_RESTART
#ifdef PRINT_SCHEMA_RESTART
    char buf[100];
    snprintf(buf, sizeof(buf), "checkSchemaStatus: pass: %d table: %d", 
             c_restartRecord.m_pass, tableId);
#endif
    
    if (c_restartRecord.m_pass <= CREATE_OLD_PASS)
    {
      if (!::checkSchemaStatus(oldEntry->m_tableType, c_restartRecord.m_pass))
        continue;

      switch(oldSchemaState){
      case SchemaFile::INIT: jam();
      case SchemaFile::DROP_TABLE_COMMITTED: jam();
      case SchemaFile::ADD_STARTED: jam();
      case SchemaFile::DROP_TABLE_STARTED: jam();
      case SchemaFile::TEMPORARY_TABLE_COMMITTED: jam();
	continue;
      case SchemaFile::TABLE_ADD_COMMITTED: jam();
      case SchemaFile::ALTER_TABLE_COMMITTED: jam();
	jam();
#ifdef PRINT_SCHEMA_RESTART
        ndbout_c("%s -> restartCreateTab", buf);
        ndbout << *newEntry << " " << *oldEntry << endl;
#endif
	restartCreateTab(signal, tableId, oldEntry, oldEntry, true);        
        return;
      case SchemaFile::CREATE_PARSED:
        jam();
      case SchemaFile::DROP_PARSED:
        jam();
      case SchemaFile::ALTER_PARSED:
        jam();
        // these are not disk states
        ndbrequire(false);
        continue;
      }
    }

    if (c_restartRecord.m_pass <= DROP_OLD_PASS)
    {
      if (!::checkSchemaStatus(oldEntry->m_tableType, c_restartRecord.m_pass))
        continue;

      switch(oldSchemaState){
      case SchemaFile::INIT: jam();
      case SchemaFile::DROP_TABLE_COMMITTED: jam();
      case SchemaFile::TEMPORARY_TABLE_COMMITTED: jam();
        continue;
      case SchemaFile::ADD_STARTED: jam();
      case SchemaFile::DROP_TABLE_STARTED: jam();
#ifdef PRINT_SCHEMA_RESTART
        ndbout_c("%s -> restartDropTab", buf);
        ndbout << *newEntry << " " << *oldEntry << endl;
#endif
	restartDropTab(signal, tableId, oldEntry, newEntry);
        return;
      case SchemaFile::TABLE_ADD_COMMITTED: jam();
      case SchemaFile::ALTER_TABLE_COMMITTED: jam();
        if (! (* oldEntry == * newEntry))
        {
#ifdef PRINT_SCHEMA_RESTART
          ndbout_c("%s -> restartDropTab", buf);
          ndbout << *newEntry << " " << *oldEntry << endl;
#endif
          restartDropTab(signal, tableId, oldEntry, newEntry);
          return;
        }
        continue;
      case SchemaFile::CREATE_PARSED:
        jam();
      case SchemaFile::DROP_PARSED:
        jam();
      case SchemaFile::ALTER_PARSED:
        jam();
        // these are not disk states
        ndbrequire(false);
        continue;
      }
    }

    if (c_restartRecord.m_pass <= CREATE_NEW_PASS)
    {
      if (!::checkSchemaStatus(newEntry->m_tableType, c_restartRecord.m_pass))
        continue;
      
      switch(newSchemaState){
      case SchemaFile::INIT: jam();
      case SchemaFile::DROP_TABLE_COMMITTED: jam();
      case SchemaFile::TEMPORARY_TABLE_COMMITTED: jam();
        * oldEntry = * newEntry;
        continue;
      case SchemaFile::ADD_STARTED: jam();
      case SchemaFile::DROP_TABLE_STARTED: jam();
        ndbrequire(DictTabInfo::isTable(newEntry->m_tableType) ||
                   DictTabInfo::isIndex(newEntry->m_tableType));
        newEntry->m_tableState = SchemaFile::INIT;
        continue;
      case SchemaFile::TABLE_ADD_COMMITTED: jam();
      case SchemaFile::ALTER_TABLE_COMMITTED: jam();
        if (DictTabInfo::isIndex(newEntry->m_tableType) ||
            DictTabInfo::isTable(newEntry->m_tableType))
        {
          bool file = * oldEntry == *newEntry &&
            (!DictTabInfo::isIndex(newEntry->m_tableType) || c_systemRestart);

#ifdef PRINT_SCHEMA_RESTART          
          ndbout_c("%s -> restartCreateTab (file: %d)", buf, file);
          ndbout << *newEntry << " " << *oldEntry << endl;
#endif
          restartCreateTab(signal, tableId, newEntry, newEntry, file);        
          * oldEntry = * newEntry;
          return;
        }
        else if (! (* oldEntry == *newEntry))
        {
#ifdef PRINT_SCHEMA_RESTART
          ndbout_c("%s -> restartCreateTab", buf);
          ndbout << *newEntry << " " << *oldEntry << endl;
#endif
          restartCreateTab(signal, tableId, oldEntry, newEntry, false);        
          * oldEntry = * newEntry;
          return;
        }
        * oldEntry = * newEntry;
        continue;
      case SchemaFile::CREATE_PARSED:
        jam();
      case SchemaFile::DROP_PARSED:
        jam();
      case SchemaFile::ALTER_PARSED:
        jam();
        // master cannot have active transaction
        ndbrequire(false);
        continue;
      }
    }
  }
  
  c_restartRecord.m_pass++;
  c_restartRecord.activeTable= 0;
  if(c_restartRecord.m_pass <= LAST_PASS)
  {
    checkSchemaStatus(signal);
  }
  else
  {
    execute(signal, c_schemaRecord.m_callback, 0);
  }
}//checkSchemaStatus()

void
Dbdict::restartCreateTab(Signal* signal, Uint32 tableId, 
			 const SchemaFile::TableEntry * old_entry, 
			 const SchemaFile::TableEntry * new_entry, 
			 bool file)
{
  jam();
  D("restartCreateTab");

  switch(new_entry->m_tableType){
  case DictTabInfo::UndefTableType:
  case DictTabInfo::HashIndexTrigger:
  case DictTabInfo::SubscriptionTrigger:
  case DictTabInfo::ReadOnlyConstraint:
  case DictTabInfo::IndexTrigger:
    ndbrequire(false);
  case DictTabInfo::SystemTable:
  case DictTabInfo::UserTable:
  case DictTabInfo::UniqueHashIndex:
  case DictTabInfo::HashIndex:
  case DictTabInfo::UniqueOrderedIndex:
  case DictTabInfo::OrderedIndex:
    break;
  case DictTabInfo::Tablespace:
  case DictTabInfo::LogfileGroup:
  case DictTabInfo::Datafile:
  case DictTabInfo::Undofile:
    restartCreateObj(signal, tableId, old_entry, new_entry, file);
    return;
  }
  
  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;  
  seizeSchemaOp(op_ptr, createTabPtr);
  ndbrequire(!op_ptr.isNull());
  CreateTabReq* impl_req = &createTabPtr.p->m_request;

  impl_req->senderRef = reference();
  impl_req->senderData = op_ptr.p->op_key;
  impl_req->requestType = 0;
  impl_req->tableId = tableId;
  impl_req->tableVersion = 0;
  impl_req->gci = 0;

  if(file && !ERROR_INSERTED(6002)){
    jam();
    
    c_readTableRecord.no_of_words = old_entry->m_info_words;
    c_readTableRecord.pageId = 0;
    c_readTableRecord.m_callback.m_callbackData = op_ptr.p->op_key;
    c_readTableRecord.m_callback.m_callbackFunction = 
      safe_cast(&Dbdict::restartCreateTab_readTableConf);
    
    startReadTableFile(signal, tableId);
    return;
  } else {
    
    ndbrequire(c_masterNodeId != getOwnNodeId());
    
    /**
     * Get from master
     */
    GetTabInfoReq * const req = (GetTabInfoReq *)&signal->theData[0];
    req->senderRef = reference();
    req->senderData = op_ptr.p->op_key;
    req->requestType = GetTabInfoReq::RequestById |
      GetTabInfoReq::LongSignalConf;
    req->tableId = tableId;
    sendSignal(calcDictBlockRef(c_masterNodeId), GSN_GET_TABINFOREQ, signal,
	       GetTabInfoReq::SignalLength, JBB);

    if(ERROR_INSERTED(6002)){
      NdbSleep_MilliSleep(10);
      CRASH_INSERTION(6002);
    }
  }
}

void
Dbdict::restartCreateTab_readTableConf(Signal* signal, 
				       Uint32 op_key,
				       Uint32 ret)
{
  jam();
  D("restartCreateTab_readTableConf");
  
  PageRecordPtr pageRecPtr;
  c_pageRecordArray.getPtr(pageRecPtr, c_readTableRecord.pageId);

  ParseDictTabInfoRecord parseRecord;
  parseRecord.requestType = DictTabInfo::GetTabInfoConf;
  parseRecord.errorCode = 0;
  
  Uint32 sz = c_readTableRecord.no_of_words;
  SimplePropertiesLinearReader r(pageRecPtr.p->word+ZPAGE_HEADER_SIZE, sz);
  handleTabInfoInit(r, &parseRecord);
  if (parseRecord.errorCode != 0)
  {
    char buf[255];
    BaseString::snprintf(buf, sizeof(buf), 
                         "Failed to create table %u during restart,"
                         " error: %u line: %u."
			 " Most likely change of configuration",
			 c_readTableRecord.tableId,
			 parseRecord.errorCode,
                         parseRecord.errorLine);
    progError(__LINE__, 
	      NDBD_EXIT_INVALID_CONFIG,
	      buf);
    ndbrequire(parseRecord.errorCode == 0);
  }

  /* ---------------------------------------------------------------- */
  // We have read the table description from disk as part of system restart.
  // We will also write it back again to ensure that both copies are ok.
  /* ---------------------------------------------------------------- */
  ndbrequire(c_writeTableRecord.tableWriteState == WriteTableRecord::IDLE);
  c_writeTableRecord.no_of_words = c_readTableRecord.no_of_words;
  c_writeTableRecord.pageId = c_readTableRecord.pageId;
  c_writeTableRecord.tableWriteState = WriteTableRecord::TWR_CALLBACK;
  c_writeTableRecord.m_callback.m_callbackData = op_key;
  c_writeTableRecord.m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartCreateTab_writeTableConf);
  startWriteTableFile(signal, c_readTableRecord.tableId);
}

void
Dbdict::execGET_TABINFO_CONF(Signal* signal)
{
  jamEntry();

  if(!assembleFragments(signal)){
    jam();
    return;
  }
  
  GetTabInfoConf * const conf = (GetTabInfoConf*)signal->getDataPtr();

  switch(conf->tableType){
  case DictTabInfo::UndefTableType:
  case DictTabInfo::HashIndexTrigger:
  case DictTabInfo::SubscriptionTrigger:
  case DictTabInfo::ReadOnlyConstraint:
  case DictTabInfo::IndexTrigger:
    ndbrequire(false);
  case DictTabInfo::SystemTable:
  case DictTabInfo::UserTable:
  case DictTabInfo::UniqueHashIndex:
  case DictTabInfo::HashIndex:
  case DictTabInfo::UniqueOrderedIndex:
  case DictTabInfo::OrderedIndex:
    break;
  case DictTabInfo::Tablespace:
  case DictTabInfo::LogfileGroup:
  case DictTabInfo::Datafile:
  case DictTabInfo::Undofile:
    if(refToBlock(conf->senderRef) == TSMAN
       && (refToNode(conf->senderRef) == 0
	   || refToNode(conf->senderRef) == getOwnNodeId()))
    {
      jam();
      FilePtr fg_ptr;
      ndbrequire(c_file_hash.find(fg_ptr, conf->tableId));
      const Uint32 free_extents= conf->freeExtents;
      const Uint32 id= conf->tableId;
      const Uint32 type= conf->tableType;
      const Uint32 data= conf->senderData;
      signal->theData[0]= ZPACK_TABLE_INTO_PAGES;
      signal->theData[1]= id;
      signal->theData[2]= type;
      signal->theData[3]= data;
      signal->theData[4]= free_extents;
      sendSignal(reference(), GSN_CONTINUEB, signal, 5, JBB);
    }
    else if(refToBlock(conf->senderRef) == LGMAN
	    && (refToNode(conf->senderRef) == 0
		|| refToNode(conf->senderRef) == getOwnNodeId()))
    {
      jam();
      FilegroupPtr fg_ptr;
      ndbrequire(c_filegroup_hash.find(fg_ptr, conf->tableId));
      const Uint32 free_hi= conf->freeWordsHi;
      const Uint32 free_lo= conf->freeWordsLo;
      const Uint32 id= conf->tableId;
      const Uint32 type= conf->tableType;
      const Uint32 data= conf->senderData;
      signal->theData[0]= ZPACK_TABLE_INTO_PAGES;
      signal->theData[1]= id;
      signal->theData[2]= type;
      signal->theData[3]= data;
      signal->theData[4]= free_hi;
      signal->theData[5]= free_lo;
      sendSignal(reference(), GSN_CONTINUEB, signal, 6, JBB);
    }
    else
    {
      jam();
      restartCreateObj_getTabInfoConf(signal);
    }
    return;
  }
  
  const Uint32 tableId = conf->tableId;
  const Uint32 senderData = conf->senderData;

  SectionHandle handle(this, signal);
  SegmentedSectionPtr tabInfoPtr;
  handle.getSection(tabInfoPtr, GetTabInfoConf::DICT_TAB_INFO);

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;  
  findSchemaOp(op_ptr, createTabPtr, senderData);
  ndbrequire(!op_ptr.isNull());
  ndbrequire(createTabPtr.p->m_request.tableId == tableId);

  /**
   * Put data into table record
   */
  ParseDictTabInfoRecord parseRecord;
  parseRecord.requestType = DictTabInfo::GetTabInfoConf;
  parseRecord.errorCode = 0;
  
  SimplePropertiesSectionReader r(tabInfoPtr, getSectionSegmentPool());
  handleTabInfoInit(r, &parseRecord);
  ndbrequire(parseRecord.errorCode == 0);

  // save to disk

  ndbrequire(tableId < c_tableRecordPool.getSize());
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  SchemaFile::TableEntry * tableEntry = getTableEntry(xsf, tableId);
  tableEntry->m_info_words= tabInfoPtr.sz;
  
  Callback callback;
  callback.m_callbackData = op_ptr.p->op_key;
  callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartCreateTab_writeTableConf);
  
  writeTableFile(signal, tableId, tabInfoPtr, &callback);

  releaseSections(handle);
}

void
Dbdict::restartCreateTab_writeTableConf(Signal* signal, 
					Uint32 op_key,
					Uint32 ret)
{
  jam();
  D("restartCreateTab_writeTableConf");

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;  
  findSchemaOp(op_ptr, createTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  Callback callback;
  callback.m_callbackData = op_key;
  callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartCreateTab_dihComplete);

  OpSection& fragSection = op_ptr.p->m_section[CreateTabReq::FRAGMENTATION];
  new (&fragSection) OpSection;
  createTab_dih(signal, op_ptr, fragSection, &callback);
}

void
Dbdict::restartCreateTab_dihComplete(Signal* signal, 
				     Uint32 op_key,
				     Uint32 ret)
{
  jam();
  D("restartCreateTab_dihComplete");
  
  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;  
  findSchemaOp(op_ptr, createTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  const CreateTabReq* impl_req = &createTabPtr.p->m_request;

  if (hasError(op_ptr.p->m_error))
  {
    char buf[100];
    BaseString::snprintf(buf, sizeof(buf),
                         "Failed to create table %u during restart,"
                         " error: %u line: %u.",
                         impl_req->tableId,
                         op_ptr.p->m_error.errorCode,
                         op_ptr.p->m_error.errorLine);
    progError(__LINE__, NDBD_EXIT_RESOURCE_ALLOC_ERROR, buf);
  }

  Callback callback;
  callback.m_callbackData = op_key;
  callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartCreateTab_activateComplete);
  
  createTab_activate(signal, op_ptr, &callback);
}

void
Dbdict::restartCreateTab_activateComplete(Signal* signal, 
					  Uint32 op_key,
					  Uint32 ret)
{
  jam();
  D("restartCreateTab_activateComplete");
  
  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;  
  findSchemaOp(op_ptr, createTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  const CreateTabReq* impl_req = &createTabPtr.p->m_request;

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, impl_req->tableId);
  tabPtr.p->tabState = TableRecord::DEFINED;
  
  releaseSchemaOp(op_ptr);

  c_restartRecord.activeTable++;
  checkSchemaStatus(signal);
}

void
Dbdict::restartDropTab(Signal* signal, Uint32 tableId,
                       const SchemaFile::TableEntry * old_entry, 
                       const SchemaFile::TableEntry * new_entry)
{
  switch(old_entry->m_tableType){
  case DictTabInfo::UndefTableType:
  case DictTabInfo::HashIndexTrigger:
  case DictTabInfo::SubscriptionTrigger:
  case DictTabInfo::ReadOnlyConstraint:
  case DictTabInfo::IndexTrigger:
    ndbrequire(false);
  case DictTabInfo::SystemTable:
  case DictTabInfo::UserTable:
  case DictTabInfo::UniqueHashIndex:
  case DictTabInfo::HashIndex:
  case DictTabInfo::UniqueOrderedIndex:
  case DictTabInfo::OrderedIndex:
    break;
  case DictTabInfo::Tablespace:
  case DictTabInfo::LogfileGroup:
  case DictTabInfo::Datafile:
  case DictTabInfo::Undofile:
    restartDropObj(signal, tableId, old_entry);
    return;
  }

  SchemaOpPtr op_ptr;
  DropTableRecPtr dropTabPtr;  
  seizeSchemaOp(op_ptr, dropTabPtr);
  ndbrequire(!op_ptr.isNull());
  DropTabReq* impl_req = &dropTabPtr.p->m_request;

  impl_req->senderRef = reference();
  impl_req->senderData = op_ptr.p->op_key;
  impl_req->requestType = DropTabReq::RestartDropTab;
  impl_req->tableId = tableId;
  impl_req->tableVersion = 0;

  dropTabPtr.p->m_block = 0;
  dropTabPtr.p->m_callback.m_callbackData = op_ptr.p->op_key;
  dropTabPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartDropTab_complete);
  dropTab_nextStep(signal, op_ptr);  
}

void
Dbdict::restartDropTab_complete(Signal* signal, 
				Uint32 op_key,
				Uint32 ret)
{
  jam();

  SchemaOpPtr op_ptr;
  DropTableRecPtr dropTabPtr;  
  findSchemaOp(op_ptr, dropTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  //const DropTabReq* impl_req = &dropTabPtr.p->m_request;

  //@todo check error

  releaseTableObject(c_restartRecord.activeTable);
  releaseSchemaOp(op_ptr);

  c_restartRecord.activeTable++;
  checkSchemaStatus(signal);
}

/**
 * Create Obj during NR/SR
 */
void
Dbdict::restartCreateObj(Signal* signal, 
			 Uint32 tableId, 
			 const SchemaFile::TableEntry * old_entry,
			 const SchemaFile::TableEntry * new_entry,
			 bool file){
  jam();
  
  CreateObjRecordPtr createObjPtr;  
  ndbrequire(c_opCreateObj.seize(createObjPtr));
  
  const Uint32 key = ++c_opRecordSequence;
  createObjPtr.p->key = key;
  c_opCreateObj.add(createObjPtr);
  createObjPtr.p->m_errorCode = 0;
  createObjPtr.p->m_senderRef = reference();
  createObjPtr.p->m_senderData = tableId;
  createObjPtr.p->m_clientRef = reference();
  createObjPtr.p->m_clientData = tableId;
  
  createObjPtr.p->m_obj_id = tableId;
  createObjPtr.p->m_obj_type = new_entry->m_tableType;
  createObjPtr.p->m_obj_version = new_entry->m_tableVersion;

  createObjPtr.p->m_callback.m_callbackData = key;
  createObjPtr.p->m_callback.m_callbackFunction= 
    safe_cast(&Dbdict::restartCreateObj_prepare_start_done);
  
  createObjPtr.p->m_restart= file ? 1 : 2;
  switch(new_entry->m_tableType){
  case DictTabInfo::Tablespace:
  case DictTabInfo::LogfileGroup:
    createObjPtr.p->m_vt_index = 0;
    break;
  case DictTabInfo::Datafile:
  case DictTabInfo::Undofile:
    createObjPtr.p->m_vt_index = 1;
    break;
  default:
    ndbrequire(false);
  }
  
  createObjPtr.p->m_obj_info_ptr_i = RNIL;
  if(file)
  {
    c_readTableRecord.no_of_words = old_entry->m_info_words;
    c_readTableRecord.pageId = 0;
    c_readTableRecord.m_callback.m_callbackData = key;
    c_readTableRecord.m_callback.m_callbackFunction = 
      safe_cast(&Dbdict::restartCreateObj_readConf);
    
    startReadTableFile(signal, tableId);
  }
  else
  {
    /**
     * Get from master
     */
    GetTabInfoReq * const req = (GetTabInfoReq *)&signal->theData[0];
    req->senderRef = reference();
    req->senderData = key;
    req->requestType = GetTabInfoReq::RequestById |
      GetTabInfoReq::LongSignalConf;
    req->tableId = tableId;
    sendSignal(calcDictBlockRef(c_masterNodeId), GSN_GET_TABINFOREQ, signal,
	       GetTabInfoReq::SignalLength, JBB);
  }
}

void
Dbdict::restartCreateObj_getTabInfoConf(Signal* signal)
{
  jam();

  GetTabInfoConf * const conf = (GetTabInfoConf*)signal->getDataPtr();

  const Uint32 objId = conf->tableId;
  const Uint32 senderData = conf->senderData;

  SectionHandle handle(this, signal);
  SegmentedSectionPtr objInfoPtr;
  handle.getSection(objInfoPtr, GetTabInfoConf::DICT_TAB_INFO);
  handle.clear();

  CreateObjRecordPtr createObjPtr;  
  ndbrequire(c_opCreateObj.find(createObjPtr, senderData));
  ndbrequire(createObjPtr.p->m_obj_id == objId);
  
  createObjPtr.p->m_obj_info_ptr_i= objInfoPtr.i;
  
  (this->*f_dict_op[createObjPtr.p->m_vt_index].m_prepare_start)
    (signal, createObjPtr.p);
}

void
Dbdict::restartCreateObj_readConf(Signal* signal,
				  Uint32 callbackData, 
				  Uint32 returnCode)
{
  jam();
  ndbrequire(returnCode == 0);
  CreateObjRecordPtr createObjPtr;  
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  ndbrequire(createObjPtr.p->m_errorCode == 0);

  PageRecordPtr pageRecPtr;
  c_pageRecordArray.getPtr(pageRecPtr, c_readTableRecord.pageId);
  
  Uint32 sz = c_readTableRecord.no_of_words;

  Ptr<SectionSegment> ptr;
  ndbrequire(import(ptr, pageRecPtr.p->word+ZPAGE_HEADER_SIZE, sz));
  createObjPtr.p->m_obj_info_ptr_i= ptr.i;  
  
  if (f_dict_op[createObjPtr.p->m_vt_index].m_prepare_start)
    (this->*f_dict_op[createObjPtr.p->m_vt_index].m_prepare_start)
      (signal, createObjPtr.p);
  else
    execute(signal, createObjPtr.p->m_callback, 0);
}

void
Dbdict::restartCreateObj_prepare_start_done(Signal* signal,
					    Uint32 callbackData, 
					    Uint32 returnCode)
{
  jam();
  ndbrequire(returnCode == 0);
  CreateObjRecordPtr createObjPtr;  
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  ndbrequire(createObjPtr.p->m_errorCode == 0);

  Callback callback;
  callback.m_callbackData = callbackData;
  callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartCreateObj_write_complete);
  
  SegmentedSectionPtr objInfoPtr;
  getSection(objInfoPtr, createObjPtr.p->m_obj_info_ptr_i);

  writeTableFile(signal, createObjPtr.p->m_obj_id, objInfoPtr, &callback);
}

void
Dbdict::restartCreateObj_write_complete(Signal* signal,
					Uint32 callbackData, 
					Uint32 returnCode)
{
  ndbrequire(returnCode == 0);
  CreateObjRecordPtr createObjPtr;  
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  ndbrequire(createObjPtr.p->m_errorCode == 0);
  
  SectionHandle handle(this, createObjPtr.p->m_obj_info_ptr_i);
  releaseSections(handle);
  createObjPtr.p->m_obj_info_ptr_i = RNIL;
  
  createObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartCreateObj_prepare_complete_done);
  
  if (f_dict_op[createObjPtr.p->m_vt_index].m_prepare_complete)
    (this->*f_dict_op[createObjPtr.p->m_vt_index].m_prepare_complete)
      (signal, createObjPtr.p);
  else
    execute(signal, createObjPtr.p->m_callback, 0);
}

void
Dbdict::restartCreateObj_prepare_complete_done(Signal* signal,
					       Uint32 callbackData, 
					       Uint32 returnCode)
{
  jam();
  ndbrequire(returnCode == 0);
  CreateObjRecordPtr createObjPtr;  
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  ndbrequire(createObjPtr.p->m_errorCode == 0);

  createObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartCreateObj_commit_start_done);

  if (f_dict_op[createObjPtr.p->m_vt_index].m_commit_start)
    (this->*f_dict_op[createObjPtr.p->m_vt_index].m_commit_start)
      (signal, createObjPtr.p);
  else
    execute(signal, createObjPtr.p->m_callback, 0);
}

void
Dbdict::restartCreateObj_commit_start_done(Signal* signal,
					   Uint32 callbackData, 
					   Uint32 returnCode)
{
  jam();
  ndbrequire(returnCode == 0);
  CreateObjRecordPtr createObjPtr;  
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  ndbrequire(createObjPtr.p->m_errorCode == 0);

  createObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartCreateObj_commit_complete_done);

  if (f_dict_op[createObjPtr.p->m_vt_index].m_commit_complete)
    (this->*f_dict_op[createObjPtr.p->m_vt_index].m_commit_complete)
      (signal, createObjPtr.p);
  else
    execute(signal, createObjPtr.p->m_callback, 0);
}  


void
Dbdict::restartCreateObj_commit_complete_done(Signal* signal,
					      Uint32 callbackData, 
					      Uint32 returnCode)
{
  jam();
  ndbrequire(returnCode == 0);
  CreateObjRecordPtr createObjPtr;  
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  ndbrequire(createObjPtr.p->m_errorCode == 0);
  
  c_opCreateObj.release(createObjPtr);

  c_restartRecord.activeTable++;
  checkSchemaStatus(signal);
}

/**
 * Drop object during NR/SR
 */
void
Dbdict::restartDropObj(Signal* signal, 
                       Uint32 tableId, 
                       const SchemaFile::TableEntry * entry)
{
  jam();
  
  DropObjRecordPtr dropObjPtr;  
  ndbrequire(c_opDropObj.seize(dropObjPtr));
  
  const Uint32 key = ++c_opRecordSequence;
  dropObjPtr.p->key = key;
  c_opDropObj.add(dropObjPtr);
  dropObjPtr.p->m_errorCode = 0;
  dropObjPtr.p->m_senderRef = reference();
  dropObjPtr.p->m_senderData = tableId;
  dropObjPtr.p->m_clientRef = reference();
  dropObjPtr.p->m_clientData = tableId;
  
  dropObjPtr.p->m_obj_id = tableId;
  dropObjPtr.p->m_obj_type = entry->m_tableType;
  dropObjPtr.p->m_obj_version = entry->m_tableVersion;

  dropObjPtr.p->m_callback.m_callbackData = key;
  dropObjPtr.p->m_callback.m_callbackFunction= 
    safe_cast(&Dbdict::restartDropObj_prepare_start_done);

  ndbout_c("Dropping %d %d", tableId, entry->m_tableType);
  switch(entry->m_tableType){
  case DictTabInfo::Tablespace:
  case DictTabInfo::LogfileGroup:{
    jam();
    Ptr<Filegroup> fg_ptr;
    ndbrequire(c_filegroup_hash.find(fg_ptr, tableId));
    dropObjPtr.p->m_obj_ptr_i = fg_ptr.i;
    dropObjPtr.p->m_vt_index = 3;
    break;
  }
  case DictTabInfo::Datafile:{
    jam();
    Ptr<File> file_ptr;
    dropObjPtr.p->m_vt_index = 2;
    ndbrequire(c_file_hash.find(file_ptr, tableId));
    dropObjPtr.p->m_obj_ptr_i = file_ptr.i;
    break;
  }
  case DictTabInfo::Undofile:{
    jam();    
    Ptr<File> file_ptr;
    dropObjPtr.p->m_vt_index = 4;
    ndbrequire(c_file_hash.find(file_ptr, tableId));
    dropObjPtr.p->m_obj_ptr_i = file_ptr.i;

    /**
     * Undofiles are only removed from logfile groups file list
     *   as drop undofile is currently not supported...
     *   file will be dropped by lgman when dropping filegroup
     */
    dropObjPtr.p->m_callback.m_callbackFunction= 
      safe_cast(&Dbdict::restartDropObj_commit_complete_done);
    
    if (f_dict_op[dropObjPtr.p->m_vt_index].m_commit_complete)
      (this->*f_dict_op[dropObjPtr.p->m_vt_index].m_commit_complete)
        (signal, dropObjPtr.p);
    else
      execute(signal, dropObjPtr.p->m_callback, 0);
    return;
  }
  default:
    jamLine(entry->m_tableType);
    ndbrequire(false);
  }
  
  if (f_dict_op[dropObjPtr.p->m_vt_index].m_prepare_start)
    (this->*f_dict_op[dropObjPtr.p->m_vt_index].m_prepare_start)
      (signal, dropObjPtr.p);
  else
    execute(signal, dropObjPtr.p->m_callback, 0);
}

void
Dbdict::restartDropObj_prepare_start_done(Signal* signal,
                                          Uint32 callbackData, 
                                          Uint32 returnCode)
{
  jam();
  ndbrequire(returnCode == 0);
  DropObjRecordPtr dropObjPtr;  
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  ndbrequire(dropObjPtr.p->m_errorCode == 0);
  
  dropObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartDropObj_prepare_complete_done);
  
  if (f_dict_op[dropObjPtr.p->m_vt_index].m_prepare_complete)
    (this->*f_dict_op[dropObjPtr.p->m_vt_index].m_prepare_complete)
      (signal, dropObjPtr.p);
  else
    execute(signal, dropObjPtr.p->m_callback, 0);
}

void
Dbdict::restartDropObj_prepare_complete_done(Signal* signal,
                                             Uint32 callbackData, 
                                             Uint32 returnCode)
{
  jam();
  ndbrequire(returnCode == 0);
  DropObjRecordPtr dropObjPtr;  
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  ndbrequire(dropObjPtr.p->m_errorCode == 0);
  
  dropObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartDropObj_commit_start_done);
  
  if (f_dict_op[dropObjPtr.p->m_vt_index].m_commit_start)
    (this->*f_dict_op[dropObjPtr.p->m_vt_index].m_commit_start)
      (signal, dropObjPtr.p);
  else
    execute(signal, dropObjPtr.p->m_callback, 0);
}

void
Dbdict::restartDropObj_commit_start_done(Signal* signal,
                                         Uint32 callbackData, 
                                         Uint32 returnCode)
{
  jam();
  ndbrequire(returnCode == 0);
  DropObjRecordPtr dropObjPtr;  
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  ndbrequire(dropObjPtr.p->m_errorCode == 0);
  
  dropObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::restartDropObj_commit_complete_done);

  if (f_dict_op[dropObjPtr.p->m_vt_index].m_commit_complete)
    (this->*f_dict_op[dropObjPtr.p->m_vt_index].m_commit_complete)
      (signal, dropObjPtr.p);
  else
    execute(signal, dropObjPtr.p->m_callback, 0);
}  


void
Dbdict::restartDropObj_commit_complete_done(Signal* signal,
                                            Uint32 callbackData, 
                                            Uint32 returnCode)
{
  jam();
  ndbrequire(returnCode == 0);
  DropObjRecordPtr dropObjPtr;  
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  ndbrequire(dropObjPtr.p->m_errorCode == 0);
  
  c_opDropObj.release(dropObjPtr);

  c_restartRecord.activeTable++;
  checkSchemaStatus(signal);
}

/* **************************************************************** */
/* ---------------------------------------------------------------- */
/* MODULE:          NODE FAILURE HANDLING ------------------------- */
/* ---------------------------------------------------------------- */
/*                                                                  */
/* This module contains the code that is used when nodes            */
/* (kernel/api) fails.                                              */
/* ---------------------------------------------------------------- */
/* **************************************************************** */

/* ---------------------------------------------------------------- */
// We receive a report of an API that failed.
/* ---------------------------------------------------------------- */
void Dbdict::execAPI_FAILREQ(Signal* signal) 
{
  jamEntry();
  Uint32 failedApiNode = signal->theData[0];
  BlockReference retRef = signal->theData[1];

#if 0
  Uint32 userNode = refToNode(c_connRecord.userBlockRef);
  if (userNode == failedApiNode) {
    jam();
    c_connRecord.userBlockRef = (Uint32)-1;
  }//if
#endif

  // sends API_FAILCONF when done
  handleApiFail(signal, failedApiNode, retRef);
}

/* ---------------------------------------------------------------- */
// We receive a report of one or more node failures of kernel nodes.
/* ---------------------------------------------------------------- */
void Dbdict::execNODE_FAILREP(Signal* signal) 
{
  jamEntry();
  NodeFailRep * const nodeFail = (NodeFailRep *)&signal->theData[0];

  c_failureNr    = nodeFail->failNo;
  const Uint32 numberOfFailedNodes  = nodeFail->noOfNodes;
  const bool masterFailed = (c_masterNodeId != nodeFail->masterNodeId);
  c_masterNodeId = nodeFail->masterNodeId;

  c_noNodesFailed += numberOfFailedNodes;
  Uint32 theFailedNodes[NdbNodeBitmask::Size];
  memcpy(theFailedNodes, nodeFail->theNodes, sizeof(theFailedNodes));

  c_counterMgr.execNODE_FAILREP(signal);

  if (masterFailed)
  {
    jam();
    if(c_opRecordPool.getSize() != 
       (c_opRecordPool.getNoOfFree() + 
	c_opSubEvent.get_count() + c_opCreateEvent.get_count() +
	c_opDropEvent.get_count() + c_opSignalUtil.get_count()))
    {
      jam();
      UtilLockReq lockReq;
      lockReq.senderRef = reference();
      lockReq.senderData = 1;
      lockReq.lockId = 0;
      lockReq.requestInfo = UtilLockReq::SharedLock;
      lockReq.extra = DictLockReq::NodeFailureLock;
      m_dict_lock.lock(this, m_dict_lock_pool, &lockReq, 0);
    }
  }
  
  for(unsigned i = 1; i < MAX_NDB_NODES; i++) {
    jam();
    if(NdbNodeBitmask::get(theFailedNodes, i)) {
      jam();
      NodeRecordPtr nodePtr;
      c_nodes.getPtr(nodePtr, i);

      nodePtr.p->nodeState = NodeRecord::NDB_NODE_DEAD;
      NFCompleteRep * const nfCompRep = (NFCompleteRep *)&signal->theData[0];
      nfCompRep->blockNo      = DBDICT;
      nfCompRep->nodeId       = getOwnNodeId();
      nfCompRep->failedNodeId = nodePtr.i;
      sendSignal(DBDIH_REF, GSN_NF_COMPLETEREP, signal, 
		 NFCompleteRep::SignalLength, JBB);
      
      c_aliveNodes.clear(i);
    }//if
  }//for

  /*
   * NODE_FAILREP guarantees that no "in flight" signal from
   * a dead node is accepted, and also that the job buffer contains
   * no such (un-executed) signals.  Therefore no DICT_UNLOCK_ORD
   * from a dead node (leading to master crash) is possible after
   * this clean-up removes the lock record.
   */
  removeStaleDictLocks(signal, theFailedNodes);

}//execNODE_FAILREP()


/* **************************************************************** */
/* ---------------------------------------------------------------- */
/* MODULE:          NODE START HANDLING --------------------------- */
/* ---------------------------------------------------------------- */
/*                                                                  */
/* This module contains the code that is used when kernel nodes     */
/* starts.                                                          */
/* ---------------------------------------------------------------- */
/* **************************************************************** */

/* ---------------------------------------------------------------- */
// Include a starting node in list of nodes to be part of adding
// and dropping tables.
/* ---------------------------------------------------------------- */
void Dbdict::execINCL_NODEREQ(Signal* signal) 
{
  jamEntry();
  NodeRecordPtr nodePtr;
  BlockReference retRef = signal->theData[0];
  nodePtr.i = signal->theData[1];

  ndbrequire(c_noNodesFailed > 0);
  c_noNodesFailed--;

  c_nodes.getPtr(nodePtr);
  ndbrequire(nodePtr.p->nodeState == NodeRecord::NDB_NODE_DEAD);
  nodePtr.p->nodeState = NodeRecord::NDB_NODE_ALIVE;
  signal->theData[0] = nodePtr.i;
  signal->theData[1] = reference();
  sendSignal(retRef, GSN_INCL_NODECONF, signal, 2, JBB);

  c_aliveNodes.set(nodePtr.i);
}//execINCL_NODEREQ()

/* **************************************************************** */
/* ---------------------------------------------------------------- */
/* MODULE:          ADD TABLE HANDLING ---------------------------- */
/* ---------------------------------------------------------------- */
/*                                                                  */
/* This module contains the code that is used when adding a table.  */
/* ---------------------------------------------------------------- */
/* **************************************************************** */

inline
void Dbdict::printTables()
{
  DLHashTable<DictObject>::Iterator iter;
  bool moreTables = c_obj_hash.first(iter);
  printf("OBJECTS IN DICT:\n");
  char name[MAX_TAB_NAME_SIZE];
  while (moreTables) {
    Ptr<DictObject> tablePtr = iter.curr;
    ConstRope r(c_rope_pool, tablePtr.p->m_name);
    r.copy(name);
    printf("%s ", name); 
    moreTables = c_obj_hash.next(iter);
  }
  printf("\n");
}

#define tabRequire(cond, error) \
  if (!(cond)) { \
    jam();    \
    parseP->errorCode = error; parseP->errorLine = __LINE__; \
    parseP->errorKey = it.getKey(); \
    return;   \
  }//if

// handleAddTableFailure(signal, __LINE__, allocatedTable);

Dbdict::DictObject *
Dbdict::get_object(const char * name, Uint32 len, Uint32 hash){
  DictObject key;
  key.m_key.m_name_ptr = name;
  key.m_key.m_name_len = len;
  key.m_key.m_pool = &c_rope_pool;
  key.m_name.m_hash = hash;
  Ptr<DictObject> old_ptr;
  c_obj_hash.find(old_ptr, key);
  return old_ptr.p;
}

//wl3600_todo remove the duplication
bool
Dbdict::get_object(DictObjectPtr& obj_ptr, const char* name, Uint32 len, Uint32 hash)
{
  DictObject key;
  key.m_key.m_name_ptr = name;
  key.m_key.m_name_len = len;
  key.m_key.m_pool = &c_rope_pool;
  key.m_name.m_hash = hash;
  return c_obj_hash.find(obj_ptr, key);
}

void
Dbdict::release_object(Uint32 obj_ptr_i, DictObject* obj_ptr_p){
  Rope name(c_rope_pool, obj_ptr_p->m_name);
  name.erase();

  Ptr<DictObject> ptr = { obj_ptr_p, obj_ptr_i };
  c_obj_hash.release(ptr);
}

void
Dbdict::increase_ref_count(Uint32 obj_ptr_i)
{
  DictObject* ptr = c_obj_pool.getPtr(obj_ptr_i);
  ptr->m_ref_count++;  
}

void
Dbdict::decrease_ref_count(Uint32 obj_ptr_i)
{
  DictObject* ptr = c_obj_pool.getPtr(obj_ptr_i);
  ndbrequire(ptr->m_ref_count);
  ptr->m_ref_count--;  
}

void Dbdict::handleTabInfoInit(SimpleProperties::Reader & it,
			       ParseDictTabInfoRecord * parseP,
			       bool checkExist) 
{
/* ---------------------------------------------------------------- */
// We always start by handling table name since this must be the first
// item in the list. Through the table name we can derive if it is a
// correct name, a new name or an already existing table.
/* ---------------------------------------------------------------- */

  it.first();

  SimpleProperties::UnpackStatus status;
  c_tableDesc.init();
  status = SimpleProperties::unpack(it, &c_tableDesc, 
				    DictTabInfo::TableMapping, 
				    DictTabInfo::TableMappingSize, 
				    true, true);
  
  if(status != SimpleProperties::Break){
    parseP->errorCode = CreateTableRef::InvalidFormat;
    parseP->status    = status;
    parseP->errorKey  = it.getKey();
    parseP->errorLine = __LINE__;
    return;
  }

  if(parseP->requestType == DictTabInfo::AlterTableFromAPI)
  {  
    ndbrequire(!checkExist);
  }
  if(!checkExist)
  {
    ndbrequire(parseP->requestType == DictTabInfo::AlterTableFromAPI);
  }
  
  /* ---------------------------------------------------------------- */
  // Verify that table name is an allowed table name.
  // TODO
  /* ---------------------------------------------------------------- */
  const Uint32 tableNameLength = strlen(c_tableDesc.TableName) + 1;
  const Uint32 name_hash = Rope::hash(c_tableDesc.TableName, tableNameLength);

  if(checkExist){
    jam();
    tabRequire(get_object(c_tableDesc.TableName, tableNameLength) == 0, 
	       CreateTableRef::TableAlreadyExist);
  }
  
  TableRecordPtr tablePtr;
  switch (parseP->requestType) {
  case DictTabInfo::CreateTableFromAPI: {
    jam();
  }
  case DictTabInfo::AlterTableFromAPI:{
    jam();
    tablePtr.i = getFreeTableRecord(c_tableDesc.PrimaryTableId);
    /* ---------------------------------------------------------------- */
    // Check if no free tables existed.
    /* ---------------------------------------------------------------- */
    tabRequire(tablePtr.i != RNIL, CreateTableRef::NoMoreTableRecords);
    
    c_tableRecordPool.getPtr(tablePtr);
    break;
  }
  case DictTabInfo::AddTableFromDict:
  case DictTabInfo::ReadTableFromDiskSR:
  case DictTabInfo::GetTabInfoConf:
  {
/* ---------------------------------------------------------------- */
// Get table id and check that table doesn't already exist
/* ---------------------------------------------------------------- */
    tablePtr.i = c_tableDesc.TableId;
    
    if (parseP->requestType == DictTabInfo::ReadTableFromDiskSR) {
      ndbrequire(tablePtr.i == c_restartRecord.activeTable);
    }//if
    if (parseP->requestType == DictTabInfo::GetTabInfoConf) {
      ndbrequire(tablePtr.i == c_restartRecord.activeTable);
    }//if
    
    c_tableRecordPool.getPtr(tablePtr);
    ndbrequire(tablePtr.p->tabState == TableRecord::NOT_DEFINED);
    
    //Uint32 oldTableVersion = tablePtr.p->tableVersion;
    initialiseTableRecord(tablePtr);
    if (parseP->requestType == DictTabInfo::AddTableFromDict) {
      jam();
      tablePtr.p->tabState = TableRecord::DEFINING;
    }//if

/* ---------------------------------------------------------------- */
// Set table version
/* ---------------------------------------------------------------- */
    Uint32 tableVersion = c_tableDesc.TableVersion;
    tablePtr.p->tableVersion = tableVersion;
    
    break;
  }
  default:
    ndbrequire(false);
    break;
  }//switch
  parseP->tablePtr = tablePtr;
  
  { 
    Rope name(c_rope_pool, tablePtr.p->tableName);
    tabRequire(name.assign(c_tableDesc.TableName, tableNameLength, name_hash),
	       CreateTableRef::OutOfStringBuffer);
  }

  Ptr<DictObject> obj_ptr;
  if (parseP->requestType != DictTabInfo::AlterTableFromAPI) {
    jam();
    ndbrequire(c_obj_hash.seize(obj_ptr));
    new (obj_ptr.p) DictObject;
    obj_ptr.p->m_id = tablePtr.i;
    obj_ptr.p->m_type = c_tableDesc.TableType;
    obj_ptr.p->m_name = tablePtr.p->tableName;
    obj_ptr.p->m_ref_count = 0;
    c_obj_hash.add(obj_ptr);
    tablePtr.p->m_obj_ptr_i = obj_ptr.i;

#if defined VM_TRACE || defined ERROR_INSERT
    ndbout_c("Dbdict: create name=%s,id=%u,obj_ptr_i=%d", 
	     c_tableDesc.TableName, tablePtr.i, tablePtr.p->m_obj_ptr_i);
#endif
  }
  
  // Disallow logging of a temporary table.
  tabRequire(!(c_tableDesc.TableTemporaryFlag && c_tableDesc.TableLoggedFlag),
             CreateTableRef::NoLoggingTemporaryTable);

  tablePtr.p->noOfAttributes = c_tableDesc.NoOfAttributes;
  tablePtr.p->m_bits |= 
    (c_tableDesc.TableLoggedFlag ? TableRecord::TR_Logged : 0);
  tablePtr.p->m_bits |= 
    (c_tableDesc.RowChecksumFlag ? TableRecord::TR_RowChecksum : 0);
  tablePtr.p->m_bits |= 
    (c_tableDesc.RowGCIFlag ? TableRecord::TR_RowGCI : 0);
  tablePtr.p->m_bits |= 
    (c_tableDesc.TableTemporaryFlag ? TableRecord::TR_Temporary : 0);
  tablePtr.p->m_bits |=
    (c_tableDesc.ForceVarPartFlag ? TableRecord::TR_ForceVarPart : 0);
  tablePtr.p->minLoadFactor = c_tableDesc.MinLoadFactor;
  tablePtr.p->maxLoadFactor = c_tableDesc.MaxLoadFactor;
  tablePtr.p->fragmentType = (DictTabInfo::FragmentType)c_tableDesc.FragmentType;
  tablePtr.p->tableType = (DictTabInfo::TableType)c_tableDesc.TableType;
  tablePtr.p->kValue = c_tableDesc.TableKValue;
  tablePtr.p->fragmentCount = c_tableDesc.FragmentCount;
  tablePtr.p->m_tablespace_id = c_tableDesc.TablespaceId; 
  tablePtr.p->maxRowsLow = c_tableDesc.MaxRowsLow; 
  tablePtr.p->maxRowsHigh = c_tableDesc.MaxRowsHigh; 
  tablePtr.p->minRowsLow = c_tableDesc.MinRowsLow;
  tablePtr.p->minRowsHigh = c_tableDesc.MinRowsHigh;
  tablePtr.p->defaultNoPartFlag = c_tableDesc.DefaultNoPartFlag; 
  tablePtr.p->linearHashFlag = c_tableDesc.LinearHashFlag; 
  tablePtr.p->singleUserMode = c_tableDesc.SingleUserMode;
  
  {
    Rope frm(c_rope_pool, tablePtr.p->frmData);
    tabRequire(frm.assign(c_tableDesc.FrmData, c_tableDesc.FrmLen),
	       CreateTableRef::OutOfStringBuffer);
    Rope range(c_rope_pool, tablePtr.p->rangeData);
    tabRequire(range.assign(c_tableDesc.RangeListData,
               c_tableDesc.RangeListDataLen),
	      CreateTableRef::OutOfStringBuffer);
    Rope fd(c_rope_pool, tablePtr.p->ngData);
    tabRequire(fd.assign((const char*)c_tableDesc.FragmentData,
                         c_tableDesc.FragmentDataLen),
	       CreateTableRef::OutOfStringBuffer);
    Rope ts(c_rope_pool, tablePtr.p->tsData);
    tabRequire(ts.assign((const char*)c_tableDesc.TablespaceData,
                         c_tableDesc.TablespaceDataLen),
	       CreateTableRef::OutOfStringBuffer);
  }
  
  c_fragDataLen = c_tableDesc.FragmentDataLen;
  memcpy(c_fragData, c_tableDesc.FragmentData,
         c_tableDesc.FragmentDataLen);

  if(c_tableDesc.PrimaryTableId != RNIL)
  {
    jam();
    tablePtr.p->primaryTableId = c_tableDesc.PrimaryTableId;
    tablePtr.p->indexState = (TableRecord::IndexState)c_tableDesc.IndexState;
    tablePtr.p->triggerId = c_tableDesc.CustomTriggerId;
  }
  else
  {
    jam();
    tablePtr.p->primaryTableId = RNIL;
    tablePtr.p->indexState = TableRecord::IS_UNDEFINED;
    tablePtr.p->triggerId = RNIL;
  }
  tablePtr.p->buildTriggerId = RNIL;
  
  handleTabInfo(it, parseP, c_tableDesc);
  
  if(parseP->errorCode != 0)
  {
    /**
     * Release table
     */
    releaseTableObject(tablePtr.i, checkExist);
    return;
  }

  if (checkExist && tablePtr.p->m_tablespace_id != RNIL)
  {
    /**
     * Increase ref count
     */
    FilegroupPtr ptr;
    ndbrequire(c_filegroup_hash.find(ptr, tablePtr.p->m_tablespace_id));
    increase_ref_count(ptr.p->m_obj_ptr_i);
  }
}//handleTabInfoInit()

void Dbdict::handleTabInfo(SimpleProperties::Reader & it,
			   ParseDictTabInfoRecord * parseP,
			   DictTabInfo::Table &tableDesc)
{
  TableRecordPtr tablePtr = parseP->tablePtr;
  
  SimpleProperties::UnpackStatus status;
  
  Uint32 keyCount = 0;
  Uint32 keyLength = 0;
  Uint32 attrCount = tablePtr.p->noOfAttributes;
  Uint32 nullCount = 0;
  Uint32 nullBits = 0;
  Uint32 noOfCharsets = 0;
  Uint16 charsets[128];
  Uint32 recordLength = 0;
  AttributeRecordPtr attrPtr;
  c_attributeRecordHash.removeAll();

  LocalDLFifoList<AttributeRecord> list(c_attributeRecordPool, 
					tablePtr.p->m_attributes);

  Uint32 counts[] = {0,0,0,0,0};
  
  for(Uint32 i = 0; i<attrCount; i++){
    /**
     * Attribute Name
     */
    DictTabInfo::Attribute attrDesc; attrDesc.init();
    status = SimpleProperties::unpack(it, &attrDesc, 
				      DictTabInfo::AttributeMapping, 
				      DictTabInfo::AttributeMappingSize, 
				      true, true);
    
    if(status != SimpleProperties::Break){
      parseP->errorCode = CreateTableRef::InvalidFormat;
      parseP->status    = status;
      parseP->errorKey  = it.getKey();
      parseP->errorLine = __LINE__;
      return;
    }

    /**
     * Check that attribute is not defined twice
     */
    const size_t len = strlen(attrDesc.AttributeName)+1;
    const Uint32 name_hash = Rope::hash(attrDesc.AttributeName, len);
    {
      AttributeRecord key;
      key.m_key.m_name_ptr = attrDesc.AttributeName;
      key.m_key.m_name_len = len;
      key.attributeName.m_hash = name_hash;
      key.m_key.m_pool = &c_rope_pool;
      Ptr<AttributeRecord> old_ptr;
      c_attributeRecordHash.find(old_ptr, key);
      
      if(old_ptr.i != RNIL){
	parseP->errorCode = CreateTableRef::AttributeNameTwice;
	return;
      }
    }
    
    list.seize(attrPtr);
    if(attrPtr.i == RNIL){
      jam();
      parseP->errorCode = CreateTableRef::NoMoreAttributeRecords;
      return;
    }
    
    new (attrPtr.p) AttributeRecord();
    attrPtr.p->attributeDescriptor = 0x00012255; //Default value
    attrPtr.p->tupleKey = 0;
    
    /**
     * TmpAttrib to Attribute mapping
     */
    {
      Rope name(c_rope_pool, attrPtr.p->attributeName);
      if (!name.assign(attrDesc.AttributeName, len, name_hash))
      {
	jam();
	parseP->errorCode = CreateTableRef::OutOfStringBuffer;
	parseP->errorLine = __LINE__;
	return;
      }
    }
    attrPtr.p->attributeId = i;
    //attrPtr.p->attributeId = attrDesc.AttributeId;
    attrPtr.p->tupleKey = (keyCount + 1) * attrDesc.AttributeKeyFlag;
    
    attrPtr.p->extPrecision = attrDesc.AttributeExtPrecision;
    attrPtr.p->extScale = attrDesc.AttributeExtScale;
    attrPtr.p->extLength = attrDesc.AttributeExtLength;
    // charset in upper half of precision
    unsigned csNumber = (attrPtr.p->extPrecision >> 16);
    if (csNumber != 0) {
      /*
       * A new charset is first accessed here on this node.
       * TODO use separate thread (e.g. via NDBFS) if need to load from file
       */
      CHARSET_INFO* cs = get_charset(csNumber, MYF(0));
      if (cs == NULL) {
        parseP->errorCode = CreateTableRef::InvalidCharset;
        parseP->errorLine = __LINE__;
        return;
      }
      // XXX should be done somewhere in mysql
      all_charsets[cs->number] = cs;
      unsigned i = 0;
      while (i < noOfCharsets) {
        if (charsets[i] == csNumber)
          break;
        i++;
      }
      if (i == noOfCharsets) {
        noOfCharsets++;
        if (noOfCharsets > sizeof(charsets)/sizeof(charsets[0])) {
          parseP->errorCode = CreateTableRef::InvalidFormat;
          parseP->errorLine = __LINE__;
          return;
        }
        charsets[i] = csNumber;
      }
    }

    // compute attribute size and array size
    bool translateOk = attrDesc.translateExtType();
    tabRequire(translateOk, CreateTableRef::Inconsistency);

    if(attrDesc.AttributeArraySize > 65535){
      parseP->errorCode = CreateTableRef::ArraySizeTooBig;
      parseP->status    = status;
      parseP->errorKey  = it.getKey();
      parseP->errorLine = __LINE__;
      return;
    }
    
    // XXX old test option, remove
    if(!attrDesc.AttributeKeyFlag && 
       tablePtr.i > 1 &&
       !tablePtr.p->isIndex())
    {
      //attrDesc.AttributeStorageType= NDB_STORAGETYPE_DISK;
    }

    Uint32 desc = 0;
    AttributeDescriptor::setType(desc, attrDesc.AttributeExtType);
    AttributeDescriptor::setSize(desc, attrDesc.AttributeSize);
    AttributeDescriptor::setArraySize(desc, attrDesc.AttributeArraySize);
    AttributeDescriptor::setArrayType(desc, attrDesc.AttributeArrayType);
    AttributeDescriptor::setNullable(desc, attrDesc.AttributeNullableFlag);
    AttributeDescriptor::setDKey(desc, attrDesc.AttributeDKey);
    AttributeDescriptor::setPrimaryKey(desc, attrDesc.AttributeKeyFlag);
    AttributeDescriptor::setDiskBased(desc, attrDesc.AttributeStorageType == NDB_STORAGETYPE_DISK);
    AttributeDescriptor::setDynamic(desc, attrDesc.AttributeDynamic);
    attrPtr.p->attributeDescriptor = desc;
    attrPtr.p->autoIncrement = attrDesc.AttributeAutoIncrement;
    {
      Rope defaultValue(c_rope_pool, attrPtr.p->defaultValue);
      defaultValue.assign(attrDesc.AttributeDefaultValue);
    }
    
    keyCount += attrDesc.AttributeKeyFlag;
    nullCount += attrDesc.AttributeNullableFlag;
    
    const Uint32 aSz = (1 << attrDesc.AttributeSize);
    Uint32 sz;
    if(aSz != 1)
    {
      sz = ((aSz * attrDesc.AttributeArraySize) + 31) >> 5;
    }    
    else
    {
      sz = 0;
      nullBits += attrDesc.AttributeArraySize;      
    }
    
    if(attrDesc.AttributeArraySize == 0)
    {
      parseP->errorCode = CreateTableRef::InvalidArraySize;
      parseP->status    = status;
      parseP->errorKey  = it.getKey();
      parseP->errorLine = __LINE__;
      return;
    }
    
    recordLength += sz;
    if(attrDesc.AttributeKeyFlag){
      keyLength += sz;

      if(attrDesc.AttributeNullableFlag){
	parseP->errorCode = CreateTableRef::NullablePrimaryKey;
	parseP->status    = status;
	parseP->errorKey  = it.getKey();
	parseP->errorLine = __LINE__;
	return;
      }
    }
    
    c_attributeRecordHash.add(attrPtr);

    int a= AttributeDescriptor::getDiskBased(desc);
    int b= AttributeDescriptor::getArrayType(desc);
    Uint32 pos= 2*(a ? 1 : 0) + (b == NDB_ARRAYTYPE_FIXED ? 0 : 1);
    counts[pos+1]++;

    if(b != NDB_ARRAYTYPE_FIXED && sz == 0)
    {
      parseP->errorCode = CreateTableRef::VarsizeBitfieldNotSupported;
      parseP->status    = status;
      parseP->errorKey  = it.getKey();
      parseP->errorLine = __LINE__;
      return;
    }
    
    if(!it.next())
      break;
    
    if(it.getKey() != DictTabInfo::AttributeName)
      break;
  }//while
  
  tablePtr.p->noOfPrimkey = keyCount;
  tablePtr.p->noOfNullAttr = nullCount;
  tablePtr.p->noOfCharsets = noOfCharsets;
  tablePtr.p->tupKeyLength = keyLength;
  tablePtr.p->noOfNullBits = nullCount + nullBits;

  tabRequire(recordLength<= MAX_TUPLE_SIZE_IN_WORDS, 
	     CreateTableRef::RecordTooBig);
  tabRequire(keyLength <= MAX_KEY_SIZE_IN_WORDS, 
	     CreateTableRef::InvalidPrimaryKeySize);
  tabRequire(keyLength > 0, 
	     CreateTableRef::InvalidPrimaryKeySize);

  if(tablePtr.p->m_tablespace_id != RNIL || counts[3] || counts[4])
  {
    FilegroupPtr tablespacePtr;
    if(!c_filegroup_hash.find(tablespacePtr, tablePtr.p->m_tablespace_id))
    {
      tabRequire(false, CreateTableRef::InvalidTablespace);
    }
    
    if(tablespacePtr.p->m_type != DictTabInfo::Tablespace)
    {
      tabRequire(false, CreateTableRef::NotATablespace);
    }
    
    if(tablespacePtr.p->m_version != tableDesc.TablespaceVersion)
    {
      tabRequire(false, CreateTableRef::InvalidTablespaceVersion);
    }
  }
}//handleTabInfo()

/* ---------------------------------------------------------------- */
// New global checkpoint created.
/* ---------------------------------------------------------------- */
void Dbdict::execWAIT_GCP_CONF(Signal* signal) 
{
#if 0
  TableRecordPtr tablePtr;
  jamEntry();
  WaitGCPConf* const conf = (WaitGCPConf*)&signal->theData[0];
  c_tableRecordPool.getPtr(tablePtr, c_connRecord.connTableId);
  tablePtr.p->gciTableCreated = conf->gcp;
  sendUpdateSchemaState(signal,
                        tablePtr.i,
                        SchemaFile::TABLE_ADD_COMMITTED,
                        c_connRecord.noOfPagesForTable,
                        conf->gcp);
#endif
}//execWAIT_GCP_CONF()

/* ---------------------------------------------------------------- */
// Refused new global checkpoint.
/* ---------------------------------------------------------------- */
void Dbdict::execWAIT_GCP_REF(Signal* signal) 
{
  jamEntry();
  WaitGCPRef* const ref = (WaitGCPRef*)&signal->theData[0];
/* ---------------------------------------------------------------- */
// Error Handling code needed
/* ---------------------------------------------------------------- */
  char buf[32];
  BaseString::snprintf(buf, sizeof(buf), "WAIT_GCP_REF ErrorCode=%d",
		       ref->errorCode);
  progError(__LINE__, NDBD_EXIT_NDBREQUIRE, buf);
}//execWAIT_GCP_REF()

// MODULE: CreateTable

const Dbdict::OpInfo
Dbdict::CreateTableRec::g_opInfo = {
  { 'C', 'T', 'a', 0 },
  GSN_CREATE_TAB_REQ,
  CreateTabReq::SignalLength,
  //
  &Dbdict::createTable_seize,
  &Dbdict::createTable_release,
  //
  &Dbdict::createTable_parse,
  &Dbdict::createTable_subOps,
  &Dbdict::createTable_reply,
  //
  &Dbdict::createTable_prepare,
  &Dbdict::createTable_commit,
  //
  &Dbdict::createTable_abortParse,
  &Dbdict::createTable_abortPrepare
};

bool
Dbdict::createTable_seize(SchemaOpPtr op_ptr)
{
  return seizeOpRec<CreateTableRec>(op_ptr);
}

void
Dbdict::createTable_release(SchemaOpPtr op_ptr)
{
  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);
  if (createTabPtr.p->m_tabInfoPtrI != RNIL) {
    jam();
    SegmentedSectionPtr ss_ptr;
    getSection(ss_ptr, createTabPtr.p->m_tabInfoPtrI);
    ::release(ss_ptr);
    createTabPtr.p->m_tabInfoPtrI = RNIL;
  }
  if (createTabPtr.p->m_fragmentsPtrI != RNIL) {
    jam();
    SegmentedSectionPtr ss_ptr;
    getSection(ss_ptr, createTabPtr.p->m_fragmentsPtrI);
    ::release(ss_ptr);
    createTabPtr.p->m_fragmentsPtrI = RNIL;
  }
  releaseOpRec<CreateTableRec>(op_ptr);
}

void
Dbdict::execCREATE_TABLE_REQ(Signal* signal)
{
  jamEntry();
  if (!assembleFragments(signal)) {
    jam();
    return;
  }
  SectionHandle handle(this, signal);

  const CreateTableReq req_copy =
    *(const CreateTableReq*)signal->getDataPtr();
  const CreateTableReq* req = &req_copy;

  ErrorInfo error;
  do {
    SchemaOpPtr op_ptr;
    CreateTableRecPtr createTabPtr;
    CreateTabReq* impl_req;

    startClientReq(op_ptr, createTabPtr, req, impl_req, error);
    if (hasError(error)) {
      jam();
      break;
    }

    impl_req->tableId = RNIL;
    impl_req->tableVersion = 0;
    impl_req->gci = 0;

    handleClientReq(signal, op_ptr, handle);
    return;
  } while (0);

  releaseSections(handle);

  CreateTableRef* ref = (CreateTableRef*)signal->getDataPtrSend();
  ref->senderRef = reference();
  ref->transId = req->transId;
  ref->clientData = req->clientData;
  getError(error, ref);
  ref->errorStatus = error.errorStatus;
  ref->errorKey = error.errorKey;

  sendSignal(req->clientRef, GSN_CREATE_TABLE_REF, signal,
	     CreateTableRef::SignalLength, JBB);
}

// CreateTable: PARSE

void
Dbdict::createTable_parse(Signal* signal, bool master,
                          SchemaOpPtr op_ptr,
                          SectionHandle& handle, ErrorInfo& error)
{
  D("createTable_parse");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);
  CreateTabReq* impl_req = &createTabPtr.p->m_request;

  if (checkSingleUserMode(trans_ptr.p->m_clientRef)) {
    jam();
    setError(error, CreateTableRef::SingleUser, __LINE__);
    return;
  }

  /*
   * Master parses client DictTabInfo (sec 0) into new table record.
   * DIH is called to create fragmentation.  The table record is
   * then dumped back into DictTabInfo to produce a canonical format.
   * The new DictTabInfo (sec 0) and the fragmentation (sec 1) are
   * sent to all participants when master parse returns.
   */
  if (master)
  {
    jam();

    // parse client DictTabInfo into new TableRecord

    ParseDictTabInfoRecord parseRecord;
    parseRecord.requestType = DictTabInfo::CreateTableFromAPI;
    parseRecord.errorCode = 0;

    SegmentedSectionPtr ss_ptr;
    if (!handle.getSection(ss_ptr, CreateTableReq::DICT_TAB_INFO)) {
      jam();
      setError(error, CreateTableRef::InvalidFormat, __LINE__);
      return;
    }
    SimplePropertiesSectionReader r(ss_ptr, getSectionSegmentPool());

    handleTabInfoInit(r, &parseRecord);
    releaseSections(handle);

    if (parseRecord.errorCode != 0) {
      jam();
      setError(error, parseRecord);
      return;
    }

    TableRecordPtr tabPtr = parseRecord.tablePtr;

    // link operation to object seized in handleTabInfoInit
    {
      DictObjectPtr obj_ptr;
      Uint32 obj_ptr_i = tabPtr.p->m_obj_ptr_i;
      bool ok = findDictObject(op_ptr, obj_ptr, obj_ptr_i);
      ndbrequire(ok);
    }

    // fill in table id and version
    impl_req->tableId = tabPtr.i;
    impl_req->tableVersion = tabPtr.p->tableVersion;

    // create fragmentation via DIH (no changes in DIH)

    CreateFragmentationReq* frag_req =
      (CreateFragmentationReq*)signal->getDataPtrSend();
    frag_req->senderRef = 0; // direct conf
    frag_req->senderData = RNIL;
    frag_req->primaryTableId = tabPtr.p->primaryTableId;
    frag_req->noOfFragments = tabPtr.p->fragmentCount;
    frag_req->fragmentationType = tabPtr.p->fragmentType;

    Uint32* frag_data32 = &signal->theData[25];
    Uint16* frag_data = (Uint16*)frag_data32;
    MEMCOPY_NO_WORDS(frag_data, c_fragData, c_fragDataLen);

    if (tabPtr.p->isOrderedIndex()) {
      jam();
      // ordered index has same fragmentation as the table
      frag_req->primaryTableId = tabPtr.p->primaryTableId;
      frag_req->fragmentationType = DictTabInfo::DistrKeyOrderedIndex;
    } else if (tabPtr.p->isHashIndex()) {
      jam();
      /*
       * Unique hash indexes has same amount of fragments as primary table
       * and distributed in the same manner but has always a normal hash
       * fragmentation.
       */
      frag_req->primaryTableId = tabPtr.p->primaryTableId;
      frag_req->fragmentationType = DictTabInfo::DistrKeyUniqueHashIndex;
    } else {
      jam();
      /*
       * Blob tables come here with primaryTableId != RNIL but we only need
       * it for creating the fragments so we set it to RNIL now that we got
       * what we wanted from it to avoid other side effects.
       */
      tabPtr.p->primaryTableId = RNIL;
    }

    EXECUTE_DIRECT(DBDICT, GSN_CREATE_FRAGMENTATION_REQ, signal,
                   CreateFragmentationReq::SignalLength);
    jamEntry();
    if (signal->theData[0] != 0) {
      jam();
      setError(error, signal->theData[0], __LINE__);
      return;
    }

    // save fragmentation in long signal memory
    {
      // wl3600_todo make a method for this magic stuff
      Uint32 count = 2 + (1 + frag_data[0]) * frag_data[1];

      Ptr<SectionSegment> frag_ptr;
      bool ok = ::import(frag_ptr, frag_data32, (count + 1) / 2);
      ndbrequire(ok);
      createTabPtr.p->m_fragmentsPtrI = frag_ptr.i;

      // save fragment count
      tabPtr.p->fragmentCount = frag_data[1];
    }

    // update table version
    {
      XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
      SchemaFile::TableEntry * tabEntry = getTableEntry(xsf, tabPtr.i);

      impl_req->tableVersion =
        tabPtr.p->tableVersion =
        create_obj_inc_schema_version(tabEntry->m_tableVersion);
    }

    // dump table record back into DictTabInfo
    {
      SimplePropertiesSectionWriter w(getSectionSegmentPool());
      packTableIntoPages(w, tabPtr);

      SegmentedSectionPtr ss_ptr;
      w.getPtr(ss_ptr);
      createTabPtr.p->m_tabInfoPtrI = ss_ptr.i;
    }

    // assign signal sections to send to participants
    {
      SegmentedSectionPtr ss0_ptr;
      SegmentedSectionPtr ss1_ptr;

      getSection(ss0_ptr, createTabPtr.p->m_tabInfoPtrI);
      getSection(ss1_ptr, createTabPtr.p->m_fragmentsPtrI);

      ndbrequire(handle.m_cnt == 0);
      handle.m_ptr[CreateTabReq::DICT_TAB_INFO] = ss0_ptr;
      handle.m_ptr[CreateTabReq::FRAGMENTATION] = ss1_ptr;
      handle.m_cnt = 2;

      // handle owns the memory now
      createTabPtr.p->m_tabInfoPtrI = RNIL;
      createTabPtr.p->m_fragmentsPtrI = RNIL;
    }
  }

  const Uint32 gci = impl_req->gci;
  const Uint32 tableId = impl_req->tableId;
  const Uint32 tableVersion = impl_req->tableVersion;

  SegmentedSectionPtr tabInfoPtr;
  {
    bool ok = handle.getSection(tabInfoPtr, CreateTabReq::DICT_TAB_INFO);
    ndbrequire(ok);
  }

  // wl3600_todo parse the rewritten DictTabInfo in master too
  if (!master)
  {
    jam();
    createTabPtr.p->m_request.tableId = tableId;

    ParseDictTabInfoRecord parseRecord;
    parseRecord.requestType = DictTabInfo::AddTableFromDict;
    parseRecord.errorCode = 0;

    SimplePropertiesSectionReader r(tabInfoPtr, getSectionSegmentPool());

    bool checkExist = true;
    handleTabInfoInit(r, &parseRecord, checkExist);

    if (parseRecord.errorCode != 0)
    {
      jam();
      setError(error, parseRecord);
      return;
    }

    TableRecordPtr tabPtr = parseRecord.tablePtr;

    // link operation to object seized in handleTabInfoInit
    {
      DictObjectPtr obj_ptr;
      Uint32 obj_ptr_i = tabPtr.p->m_obj_ptr_i;
      bool ok = findDictObject(op_ptr, obj_ptr, obj_ptr_i);
      ndbrequire(ok);
    }
  }

  // save sections to DICT memory
  saveOpSection(op_ptr, handle, CreateTabReq::DICT_TAB_INFO);
  saveOpSection(op_ptr, handle, CreateTabReq::FRAGMENTATION);

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, tableId);
  tabPtr.p->packedSize = tabInfoPtr.sz;
  // wl3600_todo verify version on slave
  tabPtr.p->tableVersion = tableVersion;
  tabPtr.p->gciTableCreated = gci;

  if (ERROR_INSERTED(6121)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    setError(error, 9121, __LINE__);
    return;
  }

  // save original schema file entry
  {
    XSchemaFile* xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
    SchemaFile::TableEntry* tableEntry = getTableEntry(xsf, tableId);
    createTabPtr.p->m_orig_entry = *tableEntry;
  }

  // update schema file entry in memory
  {
    SchemaFile::TableEntry& te = createTabPtr.p->m_curr_entry;
    te.init();
    te.m_tableState = SchemaFile::CREATE_PARSED;
    te.m_tableVersion = tableVersion;
    te.m_tableType = tabPtr.p->tableType;
    te.m_info_words = tabInfoPtr.sz;
    te.m_gcp = gci;
    te.m_transId = trans_ptr.p->m_transId;

    bool savetodisk = false;
    updateSchemaState(signal, tableId, &te, (Callback*)0, savetodisk, 1);
  }

  D("createTable_parse: "
    << copyRope<MAX_TAB_NAME_SIZE>(tabPtr.p->tableName)
    << V(tabPtr.p->tableVersion));
}

bool
Dbdict::createTable_subOps(Signal* signal, SchemaOpPtr op_ptr)
{
  D("createTable_subOps" << V(op_ptr.i) << *op_ptr.p);
  // wl3600_todo blobs
  return false;
}

void
Dbdict::createTable_reply(Signal* signal, SchemaOpPtr op_ptr, ErrorInfo error)
{
  jam();
  D("createTable_reply" << V(op_ptr.i) << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);

  if (!hasError(error)) {
    CreateTableConf* conf = (CreateTableConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->clientData = op_ptr.p->m_clientData;
    conf->transId = trans_ptr.p->m_transId;
    conf->tableId = createTabPtr.p->m_request.tableId;
    conf->tableVersion = createTabPtr.p->m_request.tableVersion;

    D(V(conf->tableId) << V(conf->tableVersion));

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_CREATE_TABLE_CONF, signal,
               CreateTableConf::SignalLength, JBB);
  } else {
    jam();
    CreateTableRef* ref = (CreateTableRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->clientData = op_ptr.p->m_clientData;
    ref->transId = trans_ptr.p->m_transId;
    getError(error, ref);
    ref->errorStatus = error.errorStatus;
    ref->errorKey = error.errorKey;

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_CREATE_TABLE_REF, signal,
               CreateTableRef::SignalLength, JBB);
  }
}

// CreateTable: PREPARE

void
Dbdict::createTable_prepare(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);

  Uint32 tableId = createTabPtr.p->m_request.tableId;
  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, tableId);

  D("createTable_prepare" << *op_ptr.p);

  // update schema file entry on disk
  SchemaFile::TableEntry& te = createTabPtr.p->m_curr_entry;
  te.m_tableState = SchemaFile::ADD_STARTED;

  Callback callback;
  callback.m_callbackData = op_ptr.p->op_key;
  callback.m_callbackFunction =
    safe_cast(&Dbdict::createTab_writeSchemaConf1);

  bool savetodisk = !(tabPtr.p->m_bits & TableRecord::TR_Temporary);
  updateSchemaState(signal, tableId, &te, &callback, savetodisk, 1);
}

void
Dbdict::createTab_writeSchemaConf1(Signal* signal,
                                   Uint32 op_key,
                                   Uint32 ret)
{
  jam();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  Callback callback;
  callback.m_callbackData = op_ptr.p->op_key;
  callback.m_callbackFunction =
    safe_cast(&Dbdict::createTab_writeTableConf);

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, createTabPtr.p->m_request.tableId);
  bool savetodisk = !(tabPtr.p->m_bits & TableRecord::TR_Temporary);
  if (savetodisk)
  {
    const OpSection& tabInfoSec =
      getOpSection(op_ptr, CreateTabReq::DICT_TAB_INFO);
    writeTableFile(signal, createTabPtr.p->m_request.tableId,
                   tabInfoSec, &callback);
  }
  else
  {
    execute(signal, callback, 0);
  }
}

void
Dbdict::createTab_writeTableConf(Signal* signal,
				 Uint32 op_key,
				 Uint32 ret)
{
  jam();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  const OpSection& fragSec =
    getOpSection(op_ptr, CreateTabReq::FRAGMENTATION);
  ndbrequire(fragSec.getSize() > 0);

  Callback callback;
  callback.m_callbackData = op_ptr.p->op_key;
  callback.m_callbackFunction =
    safe_cast(&Dbdict::createTab_dihComplete);

  createTab_dih(signal, op_ptr, fragSec, &callback);
}

void
Dbdict::createTab_dih(Signal* signal,
		      SchemaOpPtr op_ptr,
		      OpSection fragSec,
		      Callback * c)
{
  jam();
  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  D("createTab_dih" << *op_ptr.p);

  createTabPtr.p->m_callback = * c;

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, createTabPtr.p->m_request.tableId);

  DiAddTabReq * req = (DiAddTabReq*)signal->getDataPtrSend();
  req->connectPtr = op_ptr.p->op_key;
  req->tableId = tabPtr.i;
  req->fragType = tabPtr.p->fragmentType;
  req->kValue = tabPtr.p->kValue;
  req->noOfReplicas = 0;
  req->loggedTable = !!(tabPtr.p->m_bits & TableRecord::TR_Logged);
  req->tableType = tabPtr.p->tableType;
  req->schemaVersion = tabPtr.p->tableVersion;
  req->primaryTableId = tabPtr.p->primaryTableId;
  req->temporaryTable = !!(tabPtr.p->m_bits & TableRecord::TR_Temporary);
  // no transaction for restart tab (should add one)
  req->schemaTransId = !trans_ptr.isNull() ? trans_ptr.p->m_transId : 0;

  // fragmentation in long signal section
  {
    Uint32 page[1024];
    LinearSectionPtr ptr[3];
    Uint32 noOfSections = 0;

    const Uint32 size = fragSec.getSize();

    // wl3600_todo add ndbrequire on SR, NR
    if (size != 0) {
      jam();
      bool ok = copyOut(fragSec, page, 1024);
      ndbrequire(ok);
      ptr[noOfSections].sz = size;
      ptr[noOfSections].p = page;
      noOfSections++;
    }

    sendSignal(DBDIH_REF, GSN_DIADDTABREQ, signal,
               DiAddTabReq::SignalLength, JBB,
               ptr, noOfSections);
  }
  /**
   * Create KeyDescriptor
   */
  KeyDescriptor* desc= g_key_descriptor_pool.getPtr(tabPtr.i);
  new (desc) KeyDescriptor();

  Uint32 key = 0;
  Ptr<AttributeRecord> attrPtr;
  LocalDLFifoList<AttributeRecord> list(c_attributeRecordPool,
                                        tabPtr.p->m_attributes);
  for(list.first(attrPtr); !attrPtr.isNull(); list.next(attrPtr))
  {
    AttributeRecord* aRec = attrPtr.p;
    if (aRec->tupleKey)
    {
      Uint32 attr = aRec->attributeDescriptor;

      desc->noOfKeyAttr ++;
      desc->keyAttr[key].attributeDescriptor = attr;
      Uint32 csNumber = (aRec->extPrecision >> 16);
      if (csNumber)
      {
        desc->keyAttr[key].charsetInfo = all_charsets[csNumber];
        ndbrequire(all_charsets[csNumber] != 0);
        desc->hasCharAttr = 1;
      }
      else
      {
        desc->keyAttr[key].charsetInfo = 0;
      }
      if (AttributeDescriptor::getDKey(attr))
      {
        desc->noOfDistrKeys ++;
      }
      if (AttributeDescriptor::getArrayType(attr) != NDB_ARRAYTYPE_FIXED)
      {
        desc->noOfVarKeys ++;
      }
      key++;
    }
  }
  ndbrequire(key == tabPtr.p->noOfPrimkey);
}

static
void
calcLHbits(Uint32 * lhPageBits, Uint32 * lhDistrBits,
	   Uint32 fid, Uint32 totalFragments, Dbdict * dict)
{
  Uint32 distrBits = 0;
  Uint32 pageBits = 0;

  Uint32 tmp = 1;
  while (tmp < totalFragments) {
    jamBlock(dict);
    tmp <<= 1;
    distrBits++;
  }//while
#ifdef ndb_classical_lhdistrbits
  if (tmp != totalFragments) {
    tmp >>= 1;
    if ((fid >= (totalFragments - tmp)) && (fid < (tmp - 1))) {
      distrBits--;
    }//if
  }//if
#endif
  * lhPageBits = pageBits;
  * lhDistrBits = distrBits;

}//calcLHbits()

void
Dbdict::execADD_FRAGREQ(Signal* signal)
{
  jamEntry();

  AddFragReq * const req = (AddFragReq*)signal->getDataPtr();

  Uint32 dihPtr = req->dihPtr;
  Uint32 senderData = req->senderData;
  Uint32 tableId = req->tableId;
  Uint32 fragId = req->fragmentId;
  Uint32 node = req->nodeId;
  Uint32 lcpNo = req->nextLCP;
  Uint32 fragCount = req->totalFragments;
  Uint32 requestInfo = req->requestInfo;
  Uint32 startGci = req->startGci;
  Uint32 logPart = req->logPartId;

  ndbrequire(node == getOwnNodeId());

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, senderData);
  ndbrequire(!op_ptr.isNull());

  createTabPtr.p->m_dihAddFragPtr = dihPtr;

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, tableId);

#if 0
  tabPtr.p->gciTableCreated = (startGci > tabPtr.p->gciTableCreated ? startGci:
			       startGci > tabPtr.p->gciTableCreated);
#endif

  /**
   * Calc lh3PageBits
   */
  Uint32 lhDistrBits = 0;
  Uint32 lhPageBits = 0;
  ::calcLHbits(&lhPageBits, &lhDistrBits, fragId, fragCount, this);

  Uint64 maxRows = tabPtr.p->maxRowsLow +
    (((Uint64)tabPtr.p->maxRowsHigh) << 32);
  Uint64 minRows = tabPtr.p->minRowsLow +
    (((Uint64)tabPtr.p->minRowsHigh) << 32);
  maxRows = (maxRows + fragCount - 1) / fragCount;
  minRows = (minRows + fragCount - 1) / fragCount;

  {
    LqhFragReq* req = (LqhFragReq*)signal->getDataPtrSend();
    req->senderData = senderData;
    req->senderRef = reference();
    req->fragmentId = fragId;
    req->requestInfo = requestInfo;
    req->tableId = tableId;
    req->localKeyLength = tabPtr.p->localKeyLen;
    req->maxLoadFactor = tabPtr.p->maxLoadFactor;
    req->minLoadFactor = tabPtr.p->minLoadFactor;
    req->kValue = tabPtr.p->kValue;
    req->lh3DistrBits = 0; //lhDistrBits;
    req->lh3PageBits = 0; //lhPageBits;
    req->noOfAttributes = tabPtr.p->noOfAttributes;
    req->noOfNullAttributes = tabPtr.p->noOfNullBits;
    req->maxRowsLow = maxRows & 0xFFFFFFFF;
    req->maxRowsHigh = maxRows >> 32;
    req->minRowsLow = minRows & 0xFFFFFFFF;
    req->minRowsHigh = minRows >> 32;
    req->schemaVersion = tabPtr.p->tableVersion;
    Uint32 keyLen = tabPtr.p->tupKeyLength;
    req->keyLength = keyLen; // wl-2066 no more "long keys"
    req->nextLCP = lcpNo;

    req->noOfKeyAttr = tabPtr.p->noOfPrimkey;
    req->noOfCharsets = tabPtr.p->noOfCharsets;
    req->checksumIndicator = 1;
    req->GCPIndicator = 1;
    req->startGci = startGci;
    req->tableType = tabPtr.p->tableType;
    req->primaryTableId = tabPtr.p->primaryTableId;
    req->tablespace_id= tabPtr.p->m_tablespace_id;
    req->tablespace_id = tabPtr.p->m_tablespace_id;
    req->logPartId = logPart;
    req->forceVarPartFlag = !!(tabPtr.p->m_bits& TableRecord::TR_ForceVarPart);
    sendSignal(DBLQH_REF, GSN_LQHFRAGREQ, signal,
	       LqhFragReq::SignalLength, JBB);
  }
}

void
Dbdict::execLQHFRAGCONF(Signal * signal)
{
  jamEntry();
  LqhFragConf * const conf = (LqhFragConf*)signal->getDataPtr();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, conf->senderData);
  ndbrequire(!op_ptr.isNull());

  createTabPtr.p->m_lqhFragPtr = conf->lqhFragPtr;

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, createTabPtr.p->m_request.tableId);
  sendLQHADDATTRREQ(signal, op_ptr, tabPtr.p->m_attributes.firstItem);
}

void
Dbdict::execLQHFRAGREF(Signal * signal)
{
  jamEntry();
  LqhFragRef * const ref = (LqhFragRef*)signal->getDataPtr();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, ref->senderData);
  ndbrequire(!op_ptr.isNull());

  setError(op_ptr, ref->errorCode, __LINE__);

  {
    AddFragRef * const ref = (AddFragRef*)signal->getDataPtr();
    ref->dihPtr = createTabPtr.p->m_dihAddFragPtr;
    sendSignal(DBDIH_REF, GSN_ADD_FRAGREF, signal,
	       AddFragRef::SignalLength, JBB);
  }
}

void
Dbdict::sendLQHADDATTRREQ(Signal* signal,
			  SchemaOpPtr op_ptr,
			  Uint32 attributePtrI)
{
  jam();
  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, createTabPtr.p->m_request.tableId);
  LqhAddAttrReq * const req = (LqhAddAttrReq*)signal->getDataPtrSend();
  Uint32 i = 0;
  for(i = 0; i<LqhAddAttrReq::MAX_ATTRIBUTES && attributePtrI != RNIL; i++){
    jam();
    AttributeRecordPtr attrPtr;
    c_attributeRecordPool.getPtr(attrPtr, attributePtrI);
    LqhAddAttrReq::Entry& entry = req->attributes[i];
    entry.attrId = attrPtr.p->attributeId;
    entry.attrDescriptor = attrPtr.p->attributeDescriptor;
    entry.extTypeInfo = 0;
    // charset number passed to TUP, TUX in upper half
    entry.extTypeInfo |= (attrPtr.p->extPrecision & ~0xFFFF);
    if (tabPtr.p->isIndex()) {
      Uint32 primaryAttrId;
      if (attrPtr.p->nextList != RNIL) {
        getIndexAttr(tabPtr, attributePtrI, &primaryAttrId);
      } else {
        primaryAttrId = ZNIL;
        if (tabPtr.p->isOrderedIndex())
          entry.attrId = 0;     // attribute goes to TUP
      }
      entry.attrId |= (primaryAttrId << 16);
    }
    attributePtrI = attrPtr.p->nextList;
  }
  req->lqhFragPtr = createTabPtr.p->m_lqhFragPtr;
  req->senderData = op_ptr.p->op_key;
  req->senderAttrPtr = attributePtrI;
  req->noOfAttributes = i;

  sendSignal(DBLQH_REF, GSN_LQHADDATTREQ, signal,
	     LqhAddAttrReq::HeaderLength + LqhAddAttrReq::EntryLength * i, JBB);
}

void
Dbdict::execLQHADDATTCONF(Signal * signal)
{
  jamEntry();
  LqhAddAttrConf * const conf = (LqhAddAttrConf*)signal->getDataPtr();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, conf->senderData);
  ndbrequire(!op_ptr.isNull());

  const Uint32 fragId = conf->fragId;
  const Uint32 nextAttrPtr = conf->senderAttrPtr;
  if(nextAttrPtr != RNIL){
    jam();
    sendLQHADDATTRREQ(signal, op_ptr, nextAttrPtr);
    return;
  }

  {
    AddFragConf * const conf = (AddFragConf*)signal->getDataPtr();
    conf->dihPtr = createTabPtr.p->m_dihAddFragPtr;
    conf->fragId = fragId;
    sendSignal(DBDIH_REF, GSN_ADD_FRAGCONF, signal,
	       AddFragConf::SignalLength, JBB);
  }
}

void
Dbdict::execLQHADDATTREF(Signal * signal)
{
  jamEntry();
  LqhAddAttrRef * const ref = (LqhAddAttrRef*)signal->getDataPtr();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, ref->senderData);
  ndbrequire(!op_ptr.isNull());

  setError(op_ptr, ref->errorCode, __LINE__);

  {
    AddFragRef * const ref = (AddFragRef*)signal->getDataPtr();
    ref->dihPtr = createTabPtr.p->m_dihAddFragPtr;
    sendSignal(DBDIH_REF, GSN_ADD_FRAGREF, signal,
	       AddFragRef::SignalLength, JBB);
  }
}

void
Dbdict::execDIADDTABCONF(Signal* signal)
{
  jam();

  DiAddTabConf * const conf = (DiAddTabConf*)signal->getDataPtr();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, conf->senderData);
  ndbrequire(!op_ptr.isNull());

  signal->theData[0] = op_ptr.p->op_key;
  signal->theData[1] = reference();
  signal->theData[2] = createTabPtr.p->m_request.tableId;

  if(createTabPtr.p->m_dihAddFragPtr != RNIL){
    jam();

    /**
     * We did perform at least one LQHFRAGREQ
     */
    sendSignal(DBLQH_REF, GSN_TAB_COMMITREQ, signal, 3, JBB);
    return;
  } else {
    /**
     * No local fragment (i.e. no LQHFRAGREQ)
     */
    execute(signal, createTabPtr.p->m_callback, 0);
    return;
    //sendSignal(DBDIH_REF, GSN_TAB_COMMITREQ, signal, 3, JBB);
  }
}

void
Dbdict::execDIADDTABREF(Signal* signal)
{
  jam();

  DiAddTabRef * const ref = (DiAddTabRef*)signal->getDataPtr();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, ref->senderData);
  ndbrequire(!op_ptr.isNull());

  setError(op_ptr, ref->errorCode, __LINE__);
  execute(signal, createTabPtr.p->m_callback, 0);
}

// wl3600_todo split into 2 methods for prepare/commit
void
Dbdict::execTAB_COMMITCONF(Signal* signal)
{
  jamEntry();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, signal->theData[0]);
  ndbrequire(!op_ptr.isNull());
  //const CreateTabReq* impl_req = &createTabPtr.p->m_request;

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, createTabPtr.p->m_request.tableId);

  if (refToBlock(signal->getSendersBlockRef()) == DBLQH) {
    jam();
    // prepare table in DBTC
    signal->theData[0] = tabPtr.i;
    signal->theData[1] = tabPtr.p->tableVersion;
    signal->theData[2] = (Uint32)!!(tabPtr.p->m_bits & TableRecord::TR_Logged);
    signal->theData[3] = reference();
    signal->theData[4] = (Uint32)tabPtr.p->tableType;
    signal->theData[5] = op_ptr.p->op_key;
    signal->theData[6] = (Uint32)tabPtr.p->noOfPrimkey;
    signal->theData[7] = (Uint32)tabPtr.p->singleUserMode;

    sendSignal(DBTC_REF, GSN_TC_SCHVERREQ, signal, 8, JBB);
    return;
  }

  if (refToBlock(signal->getSendersBlockRef()) == DBDIH) {
    jam();
    // commit table in DBTC
    signal->theData[0] = op_ptr.p->op_key;
    signal->theData[1] = reference();
    signal->theData[2] = tabPtr.i;

    sendSignal(DBTC_REF, GSN_TAB_COMMITREQ, signal, 3, JBB);
    return;
  }

  if (refToBlock(signal->getSendersBlockRef()) == DBTC) {
    jam();
    execute(signal, createTabPtr.p->m_callback, 0);
    return;
  }

  ndbrequire(false);
}

void
Dbdict::execTAB_COMMITREF(Signal* signal) {
  jamEntry();
  ndbrequire(false);
}//execTAB_COMMITREF()

void
Dbdict::createTab_dihComplete(Signal* signal,
			      Uint32 op_key,
			      Uint32 ret)
{
  jam();
  D("createTab_dihComplete");

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  //const CreateTabReq* impl_req = &createTabPtr.p->m_request;

  //@todo check for master failed

  if (ERROR_INSERTED(6131)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    setError(op_ptr.p->m_error, 9131, __LINE__);
  }

  if (!hasError(op_ptr.p->m_error)) {
    jam();
    // prepare done
    sendTransConf(signal, op_ptr);
  } else {
    jam();
    sendTransRef(signal, op_ptr);
  }
}

// CreateTable: COMMIT

void
Dbdict::createTable_commit(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);

  Uint32 tableId = createTabPtr.p->m_request.tableId;
  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, tableId);

  D("createTable_commit" << *op_ptr.p);

  bool savetodisk = !(tabPtr.p->m_bits & TableRecord::TR_Temporary);

  SchemaFile::TableEntry& te = createTabPtr.p->m_curr_entry;
  if (savetodisk)
    te.m_tableState = SchemaFile::TABLE_ADD_COMMITTED;
  else
    te.m_tableState = SchemaFile::TEMPORARY_TABLE_COMMITTED;
  te.m_transId = 0;

  Callback callback;
  callback.m_callbackData = op_ptr.p->op_key;
  callback.m_callbackFunction =
    safe_cast(&Dbdict::createTab_writeSchemaConf2);

  updateSchemaState(signal, tabPtr.i, &te, &callback, savetodisk, 1);
}

void
Dbdict::createTab_writeSchemaConf2(Signal* signal,
				   Uint32 op_key,
				   Uint32 ret)
{
  jam();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  Callback c;
  c.m_callbackData = op_ptr.p->op_key;
  c.m_callbackFunction = safe_cast(&Dbdict::createTab_alterComplete);
  createTab_activate(signal, op_ptr, &c);
}

void
Dbdict::createTab_activate(Signal* signal, SchemaOpPtr op_ptr,
			  Callback * c)
{
  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);
  createTabPtr.p->m_callback = * c;

  signal->theData[0] = op_ptr.p->op_key;
  signal->theData[1] = reference();
  signal->theData[2] = createTabPtr.p->m_request.tableId;
  sendSignal(DBDIH_REF, GSN_TAB_COMMITREQ, signal, 3, JBB);
}

void
Dbdict::execTC_SCHVERCONF(Signal* signal)
{
  jamEntry();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, signal->theData[1]);
  ndbrequire(!op_ptr.isNull());

  execute(signal, createTabPtr.p->m_callback, 0);
}

void
Dbdict::createTab_alterComplete(Signal* signal,
				Uint32 op_key,
				Uint32 ret)
{
  jam();

  SchemaOpPtr op_ptr;
  CreateTableRecPtr createTabPtr;
  findSchemaOp(op_ptr, createTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  const CreateTabReq* impl_req = &createTabPtr.p->m_request;

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, impl_req->tableId);
  tabPtr.p->tabState = TableRecord::DEFINED;

  D("createTab_alterComplete" << *op_ptr.p);

  //@todo check error
  //@todo check master failed

  // inform SUMA
  {
    CreateTabConf* conf = (CreateTabConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->senderData = impl_req->tableId; // special usage
#if 0
    signal->header.m_noOfSections = 1;
    SegmentedSectionPtr tabInfoPtr;
    getSection(tabInfoPtr, createTabPtr.p->m_tabInfoPtrI);
    signal->setSection(tabInfoPtr, 0);
#endif
    sendSignal(SUMA_REF, GSN_CREATE_TAB_CONF, signal,
               CreateTabConf::SignalLength, JBB);
#if 0
    signal->header.m_noOfSections = 0;
#endif
  }

  sendTransConf(signal, op_ptr);
}

void
Dbdict::createTable_abortParse(Signal* signal, SchemaOpPtr op_ptr)
{
  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);
  const CreateTabReq* impl_req = &createTabPtr.p->m_request;
  const Uint32 tableId = impl_req->tableId;

  D("createTable_abortParse" << V(tableId) << *op_ptr.p);

  do {
    if (createTabPtr.p->m_abortPrepareDone)
    {
      jam();
      D("done by abort prepare");
      break;
    }

    if (tableId == RNIL) {
      jam();
      D("no table allocated");
      break;
    }

    TableRecordPtr tabPtr;
    c_tableRecordPool.getPtr(tabPtr, tableId);

    ndbrequire(tabPtr.p->tabState == TableRecord::DEFINING);
    tabPtr.p->tabState = TableRecord::NOT_DEFINED;

    // any link was to a new object
    if (hasDictObject(op_ptr)) {
      jam();
      unlinkDictObject(op_ptr);
      releaseTableObject(tableId, true);
    }

    if (createTabPtr.p->m_curr_entry.m_tableState != SchemaFile::INIT)
    {
      // parse was completed so schema entry must be restored
      SchemaFile::TableEntry& te = createTabPtr.p->m_orig_entry;
      bool savetodisk = false;
      updateSchemaState(signal, tableId, &te, (Callback*)0, savetodisk, 1);
    }
  } while (0);

  sendTransConf(signal, op_ptr);
}

void
Dbdict::createTable_abortPrepare(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);
  const CreateTabReq* impl_req = &createTabPtr.p->m_request;
  //const Uint32 tableId = impl_req->tableId;

  D("createTable_abortPrepare" << *op_ptr.p);

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, impl_req->tableId);
  tabPtr.p->tabState = TableRecord::DROPPING;

  // create drop table operation  wl3600_todo must pre-allocate

  SchemaOpPtr& oplnk_ptr = op_ptr.p->m_oplnk_ptr;
  ndbrequire(oplnk_ptr.isNull());
  DropTableRecPtr dropTabPtr;
  seizeSchemaOp(oplnk_ptr, dropTabPtr);
  ndbrequire(!oplnk_ptr.isNull());
  DropTabReq* aux_impl_req = &dropTabPtr.p->m_request;

  aux_impl_req->senderRef = impl_req->senderRef;
  aux_impl_req->senderData = impl_req->senderData;
  aux_impl_req->requestType = DropTabReq::CreateTabDrop;
  aux_impl_req->tableId = impl_req->tableId;
  aux_impl_req->tableVersion = impl_req->tableVersion;

  // link other way too
  oplnk_ptr.p->m_opbck_ptr = op_ptr;

  // wl3600_todo use ref count
  unlinkDictObject(op_ptr);

  dropTabPtr.p->m_block = 0;
  dropTabPtr.p->m_callback.m_callbackData =
    oplnk_ptr.p->op_key;
  dropTabPtr.p->m_callback.m_callbackFunction =
    safe_cast(&Dbdict::createTable_abortLocalConf);

  // invoke the "commit" phase of drop table
  dropTab_nextStep(signal, oplnk_ptr);

  if (tabPtr.p->m_tablespace_id != RNIL) {
    FilegroupPtr ptr;
    ndbrequire(c_filegroup_hash.find(ptr, tabPtr.p->m_tablespace_id));
    decrease_ref_count(ptr.p->m_obj_ptr_i);
  }
}

void
Dbdict::createTable_abortLocalConf(Signal* signal,
                                   Uint32 oplnk_key,
                                   Uint32 ret)
{
  jam();
  D("createTable_abortLocalConf" << V(oplnk_key));

  SchemaOpPtr oplnk_ptr;
  DropTableRecPtr dropTabPtr;
  findSchemaOp(oplnk_ptr, dropTabPtr, oplnk_key);
  ndbrequire(!oplnk_ptr.isNull());

  SchemaOpPtr op_ptr = oplnk_ptr.p->m_opbck_ptr;
  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);
  const CreateTabReq* impl_req = &createTabPtr.p->m_request;
  Uint32 tableId = impl_req->tableId;

  // write schema file  wl3600_todo why not use updateSchemaState

  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  SchemaFile::TableEntry * tableEntry = getTableEntry(xsf, tableId);
  tableEntry->m_tableState = SchemaFile::DROP_TABLE_COMMITTED;
  tableEntry->m_transId = 0;
  computeChecksum(xsf, tableId / NDB_SF_PAGE_ENTRIES);

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, tableId);
  bool savetodisk = !(tablePtr.p->m_bits & TableRecord::TR_Temporary);

  Callback callback = {
    safe_cast(&Dbdict::createTable_abortWriteSchemaConf),
    oplnk_ptr.p->op_key
  };

  // wl3600_todo too much repeated code
  if (savetodisk) {
    ndbrequire(c_writeSchemaRecord.inUse == false);
    c_writeSchemaRecord.inUse = true;

    c_writeSchemaRecord.pageId = c_schemaRecord.schemaPage;
    c_writeSchemaRecord.firstPage = tableId / NDB_SF_PAGE_ENTRIES;
    c_writeSchemaRecord.noOfPages = 1;
    c_writeSchemaRecord.m_callback = callback;
    startWriteSchemaFile(signal);
  } else {
    execute(signal, callback, 0);
  }
}

void
Dbdict::createTable_abortWriteSchemaConf(Signal* signal,
                                         Uint32 oplnk_key,
                                         Uint32 ret)
{
  jam();
  D("createTable_abortWriteSchemaConf" << V(oplnk_key));

  SchemaOpPtr oplnk_ptr;
  DropTableRecPtr dropTabPtr;
  findSchemaOp(oplnk_ptr, dropTabPtr, oplnk_key);
  ndbrequire(!oplnk_ptr.isNull());

  SchemaOpPtr op_ptr = oplnk_ptr.p->m_opbck_ptr;
  CreateTableRecPtr createTabPtr;
  getOpRec(op_ptr, createTabPtr);
  const CreateTabReq* impl_req = &createTabPtr.p->m_request;
  Uint32 tableId = impl_req->tableId;

  releaseTableObject(tableId);

  createTabPtr.p->m_abortPrepareDone = true;
  sendTransConf(signal, op_ptr);
}

// CreateTable: MISC

void
Dbdict::execCREATE_FRAGMENTATION_REF(Signal * signal)
{
  // currently not received
  ndbrequire(false);
}

void
Dbdict::execCREATE_FRAGMENTATION_CONF(Signal* signal)
{
  // currently not received
  ndbrequire(false);
}

void
Dbdict::execCREATE_TAB_REF(Signal* signal)
{
  // no longer received
  ndbrequire(false);
}

void
Dbdict::execCREATE_TAB_CONF(Signal* signal)
{
  // no longer received
  ndbrequire(false);
}

void
Dbdict::execCREATE_TAB_REQ(Signal* signal)
{
  // no longer received
  ndbrequire(false);
}

void Dbdict::execCREATE_TABLE_CONF(Signal* signal)
{
  jamEntry();
  const CreateTableConf* conf = (const CreateTableConf*)signal->getDataPtr();
  handleDictConf(signal, conf);
}

void Dbdict::execCREATE_TABLE_REF(Signal* signal)
{
  jamEntry();
  const CreateTableRef* ref = (const CreateTableRef*)signal->getDataPtr();
  handleDictRef(signal, ref);
}

void Dbdict::releaseTableObject(Uint32 tableId, bool removeFromHash) 
{
  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, tableId);
  if (removeFromHash)
  {
    jam();
    release_object(tablePtr.p->m_obj_ptr_i);
  }
  else
  {
    Rope tmp(c_rope_pool, tablePtr.p->tableName);
    tmp.erase();
  }
  
  {
    Rope tmp(c_rope_pool, tablePtr.p->frmData);
    tmp.erase();
  }

  {
    Rope tmp(c_rope_pool, tablePtr.p->tsData);
    tmp.erase();
  }

  {
    Rope tmp(c_rope_pool, tablePtr.p->ngData);
    tmp.erase();
  }

  {
    Rope tmp(c_rope_pool, tablePtr.p->rangeData);
    tmp.erase();
  }

  tablePtr.p->tabState = TableRecord::NOT_DEFINED;
  
  LocalDLFifoList<AttributeRecord> list(c_attributeRecordPool, 
					tablePtr.p->m_attributes);
  AttributeRecordPtr attrPtr;
  for(list.first(attrPtr); !attrPtr.isNull(); list.next(attrPtr)){
    Rope name(c_rope_pool, attrPtr.p->attributeName);
    Rope def(c_rope_pool, attrPtr.p->defaultValue);
    name.erase();
    def.erase();
  }
  list.release();
}//releaseTableObject()

// CreateTable: END

// MODULE: DropTable

const Dbdict::OpInfo
Dbdict::DropTableRec::g_opInfo = {
  { 'D', 'T', 'a', 0 },
  GSN_DROP_TAB_REQ,
  DropTabReq::SignalLength,
  //
  &Dbdict::dropTable_seize,
  &Dbdict::dropTable_release,
  //
  &Dbdict::dropTable_parse,
  &Dbdict::dropTable_subOps,
  &Dbdict::dropTable_reply,
  //
  &Dbdict::dropTable_prepare,
  &Dbdict::dropTable_commit,
  //
  &Dbdict::dropTable_abortParse,
  &Dbdict::dropTable_abortPrepare
};

bool
Dbdict::dropTable_seize(SchemaOpPtr op_ptr)
{
  return seizeOpRec<DropTableRec>(op_ptr);
}

void
Dbdict::dropTable_release(SchemaOpPtr op_ptr)
{
  releaseOpRec<DropTableRec>(op_ptr);
}

void
Dbdict::execDROP_TABLE_REQ(Signal* signal)
{
  jamEntry();
  if (!assembleFragments(signal)) {
    jam();
    return;
  }
  SectionHandle handle(this, signal);

  const DropTableReq req_copy =
    *(const DropTableReq*)signal->getDataPtr();
  const DropTableReq* req = &req_copy;

  ErrorInfo error;
  do {
    SchemaOpPtr op_ptr;
    DropTableRecPtr dropTabPtr;
    DropTabReq* impl_req;

    startClientReq(op_ptr, dropTabPtr, req, impl_req, error);
    if (hasError(error)) {
      jam();
      break;
    }

    impl_req->tableId = req->tableId;
    impl_req->tableVersion = req->tableVersion;

    handleClientReq(signal, op_ptr, handle);
    return;
  } while (0);

  releaseSections(handle);

  DropTableRef* ref = (DropTableRef*)signal->getDataPtrSend();
  ref->senderRef = reference();
  ref->clientData = req->clientData;
  ref->transId = req->transId;
  ref->tableId = req->tableId;
  ref->tableVersion = req->tableVersion;
  getError(error, ref);

  sendSignal(req->clientRef, GSN_DROP_TABLE_REF, signal,
             DropTableRef::SignalLength, JBB);
}

// DropTable: PARSE

void
Dbdict::dropTable_parse(Signal* signal, bool master,
                        SchemaOpPtr op_ptr,
                        SectionHandle& handle, ErrorInfo& error)
{
  D("dropTable_parse" << V(op_ptr.i) << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  DropTableRecPtr dropTabPtr;
  getOpRec(op_ptr, dropTabPtr);
  DropTabReq* impl_req = &dropTabPtr.p->m_request;
  Uint32 tableId = impl_req->tableId;

  if (checkSingleUserMode(trans_ptr.p->m_clientRef)) {
    jam();
    setError(error, DropTableRef::SingleUser, __LINE__);
    return;
  }

  TableRecordPtr tablePtr;
  if (!(tableId < c_tableRecordPool.getSize())) {
    jam();
    setError(error, DropTableRef::NoSuchTable, __LINE__);
    return;
  }
  c_tableRecordPool.getPtr(tablePtr, impl_req->tableId);

  // check version first (api will retry)
  if (tablePtr.p->tableVersion != impl_req->tableVersion) {
    jam();
    setError(error, DropTableRef::InvalidTableVersion, __LINE__);
    return;
  }

  // check and save original schema file entry
  {
    XSchemaFile* xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
    SchemaFile::TableEntry* te = getTableEntry(xsf, tableId);

    if (te->m_transId != 0 && te->m_transId != trans_ptr.p->m_transId) {
      jam();
      setError(error, DropTableRef::ActiveSchemaTrans, __LINE__);
      return;
    }
    if (te->m_tableState == SchemaFile::DROP_PARSED) {
      jam();
      setError(error, DropTableRef::DropInProgress, __LINE__);
      return;
    }
    dropTabPtr.p->m_orig_entry = *te;
  }

  const TableRecord::TabState tabState = tablePtr.p->tabState;
  bool ok = false;
  switch (tabState) {
  case TableRecord::NOT_DEFINED:
  case TableRecord::DEFINING:
    jam();
    setError(error, DropTableRef::NoSuchTable, __LINE__);
    return;
  case TableRecord::DEFINED:
    jam();
    ok = true;
    break;
  case TableRecord::PREPARE_DROPPING:
  case TableRecord::DROPPING:
    jam();
    setError(error, DropTableRef::DropInProgress, __LINE__);
    return;
  case TableRecord::BACKUP_ONGOING:
    jam();
    setError(error, DropTableRef::BackupInProgress, __LINE__);
    return;
  }
  ndbrequire(ok);

  // link operation to object
  {
    DictObjectPtr obj_ptr;
    Uint32 obj_ptr_i = tablePtr.p->m_obj_ptr_i;
    bool ok = findDictObject(op_ptr, obj_ptr, obj_ptr_i);
    ndbrequire(ok);
  }

  if (ERROR_INSERTED(6121)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    setError(error, 9121, __LINE__);
    return;
  }

  // update schema state in memory
  {
    SchemaFile::TableEntry& te = dropTabPtr.p->m_curr_entry;
    te = dropTabPtr.p->m_orig_entry;
    te.m_tableState = SchemaFile::DROP_PARSED;
    te.m_transId = trans_ptr.p->m_transId;

    bool savetodisk = false;
    updateSchemaState(signal, tableId, &te, (Callback*)0, savetodisk, 1);
  }
}

bool
Dbdict::dropTable_subOps(Signal* signal, SchemaOpPtr op_ptr)
{
  D("dropTable_subOps" << V(op_ptr.i) << *op_ptr.p);
  // wl3600_todo blobs, indexes, events
  return false;
}

void
Dbdict::dropTable_reply(Signal* signal, SchemaOpPtr op_ptr, ErrorInfo error)
{
  jam();
  D("dropTable_reply" << V(op_ptr.i) << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  DropTableRecPtr dropTabPtr;
  getOpRec(op_ptr, dropTabPtr);

  if (!hasError(error)) {
    DropTableConf* conf = (DropTableConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->clientData = op_ptr.p->m_clientData;
    conf->transId = trans_ptr.p->m_transId;
    conf->tableId = dropTabPtr.p->m_request.tableId;
    conf->tableVersion = dropTabPtr.p->m_request.tableVersion;

    D(V(conf->tableId) << V(conf->tableVersion));

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_DROP_TABLE_CONF, signal,
               DropTableConf::SignalLength, JBB);
  } else {
    jam();
    DropTableRef* ref = (DropTableRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->clientData = op_ptr.p->m_clientData;
    ref->transId = trans_ptr.p->m_transId;
    ref->tableId = dropTabPtr.p->m_request.tableId;
    ref->tableVersion = dropTabPtr.p->m_request.tableVersion;
    getError(error, ref);

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_DROP_TABLE_REF, signal,
               DropTableRef::SignalLength, JBB);
  }
}

// DropTable: PREPARE

void
Dbdict::dropTable_prepare(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  DropTableRecPtr dropTabPtr;
  getOpRec(op_ptr, dropTabPtr);

  D("dropTable_prepare" << *op_ptr.p);

  dropTabPtr.p->m_block = 0;

  Mutex mutex(signal, c_mutexMgr, dropTabPtr.p->m_define_backup_mutex);
  Callback c = {
    safe_cast(&Dbdict::dropTable_backup_mutex_locked),
    op_ptr.p->op_key
  };
  bool ok = mutex.lock(c);
  ndbrequire(ok);
  return;
}

void
Dbdict::dropTable_backup_mutex_locked(Signal* signal,
                                      Uint32 op_key,
                                      Uint32 ret)
{
  jamEntry();
  D("dropTable_backup_mutex_locked");

  ndbrequire(ret == 0);

  SchemaOpPtr op_ptr;
  DropTableRecPtr dropTabPtr;
  findSchemaOp(op_ptr, dropTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  const DropTabReq* impl_req = &dropTabPtr.p->m_request;

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, impl_req->tableId);

  Mutex mutex(signal, c_mutexMgr, dropTabPtr.p->m_define_backup_mutex);
  mutex.unlock(); // ignore response

  if (tablePtr.p->tabState == TableRecord::BACKUP_ONGOING)
  {
    jam();
    setError(op_ptr, DropTableRef::BackupInProgress, __LINE__);
    sendTransRef(signal, op_ptr);
    return;
  }

  tablePtr.p->tabState = TableRecord::PREPARE_DROPPING;
  prepDropTab_nextStep(signal, op_ptr);
}

void
Dbdict::prepDropTab_nextStep(Signal* signal, SchemaOpPtr op_ptr)
{
  DropTableRecPtr dropTabPtr;
  getOpRec(op_ptr, dropTabPtr);
  const DropTabReq* impl_req = &dropTabPtr.p->m_request;

  /**
   * No errors currently allowed
   */
  ndbrequire(!hasError(op_ptr.p->m_error));

  Uint32& block = dropTabPtr.p->m_block; // ref
  D("prepDropTab_nextStep" << hex << V(block) << *op_ptr.p);

  switch (block) {
  case 0:
    jam();
    block = DBDICT;
    prepDropTab_writeSchema(signal, op_ptr);
    return;
  case DBDICT:
    jam();
    block = DBLQH;
    break;
  case DBLQH:
    jam();
    block = DBTC;
    break;
  case DBTC:
    jam();
    block = DBDIH;
    break;
  case DBDIH:
    jam();
    prepDropTab_complete(signal, op_ptr);
    return;
  default:
    ndbrequire(false);
    break;
  }

  if (ERROR_INSERTED(6131) &&
      block == DBDIH) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;

    PrepDropTabRef* ref = (PrepDropTabRef*)signal->getDataPtrSend();
    ref->senderRef = numberToRef(block, getOwnNodeId());
    ref->senderData = op_ptr.p->op_key;
    ref->tableId = impl_req->tableId;
    ref->errorCode = 9131;

    sendSignal(reference(), GSN_PREP_DROP_TAB_REF, signal,
               PrepDropTabRef::SignalLength, JBB);
    return;
  }
 

  PrepDropTabReq* prep = (PrepDropTabReq*)signal->getDataPtrSend();
  prep->senderRef = reference();
  prep->senderData = op_ptr.p->op_key;
  prep->tableId = impl_req->tableId;
  prep->requestType = impl_req->requestType;

  BlockReference ref = numberToRef(block, getOwnNodeId());
  sendSignal(ref, GSN_PREP_DROP_TAB_REQ, signal,
             PrepDropTabReq::SignalLength, JBB);
}

void
Dbdict::execPREP_DROP_TAB_REQ(Signal* signal)
{
  // no longer received
  ndbrequire(false);
}

void
Dbdict::prepDropTab_writeSchema(Signal* signal, SchemaOpPtr op_ptr)
{
  jamEntry();

  DropTableRecPtr dropTabPtr;
  getOpRec(op_ptr, dropTabPtr);
  const DropTabReq* impl_req = &dropTabPtr.p->m_request;

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, impl_req->tableId);
  tablePtr.p->tabState = TableRecord::PREPARE_DROPPING;

  /**
   * Modify schema  wl3600_todo use updateSchemaState
   */
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  SchemaFile::TableEntry * tableEntry = getTableEntry(xsf, tablePtr.i);
  SchemaFile::TableState tabState =
    (SchemaFile::TableState)tableEntry->m_tableState;
  ndbrequire(tabState == SchemaFile::DROP_PARSED);
  //ndbrequire(tabState == SchemaFile::TABLE_ADD_COMMITTED ||
             //tabState == SchemaFile::ALTER_TABLE_COMMITTED ||
             //tabState == SchemaFile::TEMPORARY_TABLE_COMMITTED);
  tableEntry->m_tableState = SchemaFile::DROP_TABLE_STARTED;
  computeChecksum(xsf, tablePtr.i / NDB_SF_PAGE_ENTRIES);

  bool savetodisk = !(tablePtr.p->m_bits & TableRecord::TR_Temporary);
  Callback callback;
  callback.m_callbackData = op_ptr.p->op_key;
  callback.m_callbackFunction = safe_cast(&Dbdict::prepDropTab_writeSchemaConf);
  if (savetodisk)
  {
    ndbrequire(c_writeSchemaRecord.inUse == false);
    c_writeSchemaRecord.inUse = true;

    c_writeSchemaRecord.pageId = c_schemaRecord.schemaPage;
    c_writeSchemaRecord.newFile = false;
    c_writeSchemaRecord.firstPage = tablePtr.i / NDB_SF_PAGE_ENTRIES;
    c_writeSchemaRecord.noOfPages = 1;
    c_writeSchemaRecord.m_callback = callback;
    startWriteSchemaFile(signal);
  }
  else
  {
    execute(signal, callback, 0);
  }
}

void
Dbdict::prepDropTab_writeSchemaConf(Signal* signal,
                                    Uint32 op_key,
                                    Uint32 ret)
{
  jam();
  D("prepDropTab_writeSchemaConf");
  prepDropTab_fromLocal(signal, op_key, ret);
}

void
Dbdict::execPREP_DROP_TAB_CONF(Signal * signal)
{
  jamEntry();
  const PrepDropTabConf* conf = (const PrepDropTabConf*)signal->getDataPtr();

  Uint32 nodeId = refToNode(conf->senderRef);
  Uint32 block = refToBlock(conf->senderRef);
  ndbrequire(nodeId == getOwnNodeId() && block != DBDICT);

  prepDropTab_fromLocal(signal, conf->senderData, 0);
}

void
Dbdict::execPREP_DROP_TAB_REF(Signal* signal)
{
  jamEntry();
  const PrepDropTabRef* ref = (const PrepDropTabRef*)signal->getDataPtr();

  Uint32 nodeId = refToNode(ref->senderRef);
  Uint32 block = refToBlock(ref->senderRef);
  ndbrequire(nodeId == getOwnNodeId() && block != DBDICT);

  Uint32 errorCode = ref->errorCode;
  ndbrequire(errorCode != 0);

  if (errorCode == PrepDropTabRef::NoSuchTable && block == DBLQH)
  {
    jam();
    /**
     * Ignore errors:
     * 1) no such table and LQH, it might not exists in different LQH's
     */
    errorCode = 0;
  }
  prepDropTab_fromLocal(signal, ref->senderData, errorCode);
}

void
Dbdict::prepDropTab_fromLocal(Signal* signal, Uint32 op_key, Uint32 errorCode)
{
  SchemaOpPtr op_ptr;
  DropTableRecPtr dropTabPtr;
  findSchemaOp(op_ptr, dropTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  if (errorCode != 0)
  {
    jam();
    setError(op_ptr, errorCode, __LINE__);
  }

  prepDropTab_nextStep(signal, op_ptr);
}

void
Dbdict::prepDropTab_complete(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  D("prepDropTab_complete");

  sendTransConf(signal, op_ptr);
}

// DropTable: COMMIT

void
Dbdict::dropTable_commit(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  DropTableRecPtr dropTabPtr;
  getOpRec(op_ptr, dropTabPtr);

  D("dropTable_commit" << *op_ptr.p);

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, dropTabPtr.p->m_request.tableId);
  tablePtr.p->tabState = TableRecord::DROPPING;

  dropTabPtr.p->m_block = 0;
  dropTabPtr.p->m_callback.m_callbackData =
    op_ptr.p->op_key;
  dropTabPtr.p->m_callback.m_callbackFunction =
    safe_cast(&Dbdict::dropTab_complete);

  if (tablePtr.p->m_tablespace_id != RNIL)
  {
    FilegroupPtr ptr;
    ndbrequire(c_filegroup_hash.find(ptr, tablePtr.p->m_tablespace_id));
    decrease_ref_count(ptr.p->m_obj_ptr_i);
  }

#if defined VM_TRACE || defined ERROR_INSERT
  // from a newer execDROP_TAB_REQ version
  {
    char buf[1024];
    Rope name(c_rope_pool, tablePtr.p->tableName);
    name.copy(buf);
    ndbout_c("Dbdict: drop name=%s,id=%u,obj_id=%u", buf, tablePtr.i,
             tablePtr.p->m_obj_ptr_i);
  }
#endif

  dropTab_nextStep(signal, op_ptr);
}

void
Dbdict::dropTab_nextStep(Signal* signal, SchemaOpPtr op_ptr)
{
  DropTableRecPtr dropTabPtr;
  getOpRec(op_ptr, dropTabPtr);
  const DropTabReq* impl_req = &dropTabPtr.p->m_request;

  /**
   * No errors currently allowed
   */
  ndbrequire(!hasError(op_ptr.p->m_error));

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, impl_req->tableId);

  Uint32& block = dropTabPtr.p->m_block; // ref
  D("dropTab_nextStep" << hex << V(block) << *op_ptr.p);

  switch (block) {
  case 0:
    jam();
    block = DBTC;
    break;
  case DBTC:
    jam();
    if (tablePtr.p->isTable() || tablePtr.p->isHashIndex())
      block = DBACC;
    if (tablePtr.p->isOrderedIndex())
      block = DBTUP;
    break;
  case DBACC:
    jam();
    block = DBTUP;
    break;
  case DBTUP:
    jam();
    if (tablePtr.p->isTable() || tablePtr.p->isHashIndex())
      block = DBLQH;
    if (tablePtr.p->isOrderedIndex())
      block = DBTUX;
    break;
  case DBTUX:
    jam();
    block = DBLQH;
    break;
  case DBLQH:
    jam();
    block = DBDIH;
    break;
  case DBDIH:
    jam();
    execute(signal, dropTabPtr.p->m_callback, 0);
    return;
  default:
    ndbrequire(false);
    break;
  }

  DropTabReq* req = (DropTabReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->tableId = impl_req->tableId;
  req->requestType = impl_req->requestType;

  BlockReference ref = numberToRef(block, getOwnNodeId());
  sendSignal(ref, GSN_DROP_TAB_REQ, signal,
             DropTabReq::SignalLength, JBB);
}

void
Dbdict::execDROP_TAB_CONF(Signal* signal)
{
  jamEntry();
  const DropTabConf* conf = (const DropTabConf*)signal->getDataPtr();

  Uint32 nodeId = refToNode(conf->senderRef);
  Uint32 block = refToBlock(conf->senderRef);
  ndbrequire(nodeId == getOwnNodeId() && block != DBDICT);

  dropTab_fromLocal(signal, conf->senderData);
}

void
Dbdict::execDROP_TAB_REF(Signal* signal)
{
  jamEntry();
  const DropTabRef* ref = (const DropTabRef*)signal->getDataPtr();

  Uint32 nodeId = refToNode(ref->senderRef);
  Uint32 block = refToBlock(ref->senderRef);
  ndbrequire(nodeId == getOwnNodeId() && block != DBDICT);
  ndbrequire(ref->errorCode == DropTabRef::NoSuchTable);

  dropTab_fromLocal(signal, ref->senderData);
}

void
Dbdict::dropTab_fromLocal(Signal* signal, Uint32 op_key)
{
  jamEntry();

  SchemaOpPtr op_ptr;
  DropTableRecPtr dropTabPtr;
  findSchemaOp(op_ptr, dropTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  //const DropTabReq* impl_req = &dropTabPtr.p->m_request;

  D("dropTab_fromLocal" << *op_ptr.p);

  dropTab_nextStep(signal, op_ptr);
}

void
Dbdict::dropTab_complete(Signal* signal,
                         Uint32 op_key,
                         Uint32 ret)
{
  jam();

  SchemaOpPtr op_ptr;
  DropTableRecPtr dropTabPtr;
  findSchemaOp(op_ptr, dropTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  Uint32 tableId = dropTabPtr.p->m_request.tableId;

  /**
   * Write to schema file
   */
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  SchemaFile::TableEntry * tableEntry = getTableEntry(xsf, tableId);
  SchemaFile::TableState tabState =
    (SchemaFile::TableState)tableEntry->m_tableState;
  ndbrequire(tabState == SchemaFile::DROP_TABLE_STARTED);
  tableEntry->m_tableState = SchemaFile::DROP_TABLE_COMMITTED;
  tableEntry->m_transId = 0;
  computeChecksum(xsf, tableId / NDB_SF_PAGE_ENTRIES);

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, tableId);
  bool savetodisk = !(tablePtr.p->m_bits & TableRecord::TR_Temporary);
  Callback callback;
  callback.m_callbackData = op_key;
  callback.m_callbackFunction = safe_cast(&Dbdict::dropTab_writeSchemaConf);
  if (savetodisk)
  {
    ndbrequire(c_writeSchemaRecord.inUse == false);
    c_writeSchemaRecord.inUse = true;

    c_writeSchemaRecord.pageId = c_schemaRecord.schemaPage;
    c_writeSchemaRecord.firstPage = tableId / NDB_SF_PAGE_ENTRIES;
    c_writeSchemaRecord.noOfPages = 1;
    c_writeSchemaRecord.m_callback = callback;
    startWriteSchemaFile(signal);
  }
  else
  {
    execute(signal, callback, 0);
  }
}

void
Dbdict::dropTab_writeSchemaConf(Signal* signal,
                                Uint32 op_key,
                                Uint32 ret)
{
  jam();
  D("dropTab_writeSchemaConf");

  SchemaOpPtr op_ptr;
  DropTableRecPtr dropTabPtr;
  findSchemaOp(op_ptr, dropTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  const DropTabReq* impl_req = &dropTabPtr.p->m_request;
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  unlinkDictObject(op_ptr);
  releaseTableObject(dropTabPtr.p->m_request.tableId);

  // inform SUMA
  {
    DropTabConf* conf = (DropTabConf*)signal->getDataPtrSend();

    // special use of senderRef
    if (trans_ptr.p->m_isMaster) {
      jam();
      conf->senderRef = trans_ptr.p->m_clientRef;
    } else {
      jam();
      conf->senderRef = 0;
    }
    conf->senderData = op_key;
    conf->tableId = impl_req->tableId;

    sendSignal(SUMA_REF, GSN_DROP_TAB_CONF, signal,
               DropTabConf::SignalLength, JBB);
  }

  sendTransConf(signal, trans_ptr);
}

// DropTable: ABORT

void
Dbdict::dropTable_abortParse(Signal* signal, SchemaOpPtr op_ptr)
{
  D("dropTable_abortParse" << *op_ptr.p);

  DropTableRecPtr dropTabPtr;
  getOpRec(op_ptr, dropTabPtr);
  DropTabReq* impl_req = &dropTabPtr.p->m_request;
  Uint32 tableId = impl_req->tableId;

  // update schema state in memory  XXX temp
  if (dropTabPtr.p->m_curr_entry.m_tableState != SchemaFile::INIT)
  {
    XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
    SchemaFile::TableEntry * tableEntry = getTableEntry(xsf, tableId);
    *tableEntry = dropTabPtr.p->m_orig_entry;
    computeChecksum(xsf, tableId / NDB_SF_PAGE_ENTRIES);
  }

  sendTransConf(signal, op_ptr);
}

void
Dbdict::dropTable_abortPrepare(Signal* signal, SchemaOpPtr op_ptr)
{
  D("dropTable_abortPrepare" << *op_ptr.p);

  // no errors currently allowed...
  ndbrequire(false);

  sendTransConf(signal, op_ptr);
}

// DropTable: MISC

void Dbdict::execDROP_TABLE_CONF(Signal* signal)
{
  jamEntry();
  const DropTableConf* conf = (const DropTableConf*)signal->getDataPtr();
  handleDictConf(signal, conf);
}

void Dbdict::execDROP_TABLE_REF(Signal* signal)
{
  jamEntry();
  const DropTableRef* ref = (const DropTableRef*)signal->getDataPtr();
  handleDictRef(signal, ref);
}

// DropTable: END

// MODULE: AlterTable

const Dbdict::OpInfo
Dbdict::AlterTableRec::g_opInfo = {
  { 'A', 'T', 'a', 0 },
  GSN_ALTER_TAB_REQ,
  AlterTabReq::SignalLength,
  //
  &Dbdict::alterTable_seize,
  &Dbdict::alterTable_release,
  //
  &Dbdict::alterTable_parse,
  &Dbdict::alterTable_subOps,
  &Dbdict::alterTable_reply,
  //
  &Dbdict::alterTable_prepare,
  &Dbdict::alterTable_commit,
  //
  &Dbdict::alterTable_abortParse,
  &Dbdict::alterTable_abortPrepare
};

bool
Dbdict::alterTable_seize(SchemaOpPtr op_ptr)
{
  return seizeOpRec<AlterTableRec>(op_ptr);
}

void
Dbdict::alterTable_release(SchemaOpPtr op_ptr)
{
  AlterTableRecPtr alterTabPtr;
  getOpRec(op_ptr, alterTabPtr);
  {
    Rope r(c_rope_pool, alterTabPtr.p->m_oldTableName);
    r.erase();
  }
  {
    Rope r(c_rope_pool, alterTabPtr.p->m_oldFrmData);
    r.erase();
  }
  releaseOpRec<AlterTableRec>(op_ptr);
}

bool
Dbdict::check_ndb_versions() const
{
  Uint32 node = 0;
  Uint32 version = getNodeInfo(getOwnNodeId()).m_version;
  while((node = c_aliveNodes.find(node + 1)) != BitmaskImpl::NotFound)
  {
    if(getNodeInfo(node).m_version != version)
    {
      return false;
    }
  }
  return true;
}

void
Dbdict::execALTER_TABLE_REQ(Signal* signal)
{
  jamEntry();
  if (!assembleFragments(signal)) {
    jam();
    return;
  }
  SectionHandle handle(this, signal);

  const AlterTableReq req_copy =
    *(const AlterTableReq*)signal->getDataPtr();
  const AlterTableReq* req = &req_copy;

  ErrorInfo error;
  do {
    SchemaOpPtr op_ptr;
    AlterTableRecPtr alterTabPtr;
    AlterTabReq* impl_req;

    startClientReq(op_ptr, alterTabPtr, req, impl_req, error);
    if (hasError(error)) {
      jam();
      break;
    }

    impl_req->tableId = req->tableId;
    impl_req->tableVersion = req->tableVersion;
    impl_req->newTableVersion = 0; // set in master parse
    impl_req->gci = 0;
    impl_req->changeMask = req->changeMask;
    impl_req->connectPtr = RNIL; // set from TUP
    impl_req->noOfNewAttr = 0; // set these in master parse
    impl_req->newNoOfCharsets = 0;
    impl_req->newNoOfKeyAttrs = 0;

    handleClientReq(signal, op_ptr, handle);
    return;
  } while (0);

  releaseSections(handle);

  AlterTableRef* ref = (AlterTableRef*)signal->getDataPtrSend();
  ref->senderRef = reference();
  ref->clientData = req->clientData;
  ref->transId = req->transId;
  getError(error, ref);

  sendSignal(req->clientRef, GSN_ALTER_TABLE_REF, signal,
             AlterTableRef::SignalLength, JBB);
}

// AlterTable: PARSE

void
Dbdict::alterTable_parse(Signal* signal, bool master,
                         SchemaOpPtr op_ptr,
                         SectionHandle& handle, ErrorInfo& error)
{
  D("alterTable_parse");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  AlterTableRecPtr alterTabPtr;
  getOpRec(op_ptr, alterTabPtr);
  AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  // check that all db nodes run same version
  if (!check_ndb_versions()) {
    jam();
    setError(error, AlterTableRef::IncompatibleVersions, __LINE__);
    return;
  }

  if (checkSingleUserMode(trans_ptr.p->m_clientRef)) {
    jam();
    setError(error, AlterTableRef::SingleUser, __LINE__);
    return;
  }

  // get table definition
  TableRecordPtr tablePtr;
  if (!(impl_req->tableId < c_tableRecordPool.getSize())) {
    jam();
    setError(error, AlterTableRef::NoSuchTable, __LINE__);
    return;
  }
  c_tableRecordPool.getPtr(tablePtr, impl_req->tableId);

  const TableRecord::TabState tabState = tablePtr.p->tabState;
  bool ok = false;
  switch (tabState) {
  case TableRecord::NOT_DEFINED:
  case TableRecord::DEFINING:
    jam();
    setError(error, AlterTableRef::NoSuchTable, __LINE__);
    return;
  case TableRecord::DEFINED:
    ok = true;
    jam();
    break;
  case TableRecord::BACKUP_ONGOING:
    jam();
    setError(error, AlterTableRef::BackupInProgress, __LINE__);
    return;
  case TableRecord::PREPARE_DROPPING:
  case TableRecord::DROPPING:
    jam();
    setError(error, AlterTableRef::DropInProgress, __LINE__);
    return;
  }
  ndbrequire(ok);

  // save it for abort code
  alterTabPtr.p->m_tablePtr = tablePtr;

  if (tablePtr.p->tableVersion != impl_req->tableVersion) {
    jam();
    setError(error, AlterTableRef::InvalidTableVersion, __LINE__);
    return;
  }

  // parse new table definition into new table record
  TableRecordPtr& newTablePtr = alterTabPtr.p->m_newTablePtr; // ref
  {
    ParseDictTabInfoRecord parseRecord;
    parseRecord.requestType = DictTabInfo::AlterTableFromAPI;
    parseRecord.errorCode = 0;

    SegmentedSectionPtr ptr;
    bool ok = handle.getSection(ptr, AlterTableReq::DICT_TAB_INFO);
    ndbrequire(ok);
    SimplePropertiesSectionReader r(ptr, getSectionSegmentPool());

    handleTabInfoInit(r, &parseRecord, false); // Will not save info

    if (parseRecord.errorCode != 0) {
      jam();
      setError(error, parseRecord);
      return;
    }

    // the new temporary table record seized from pool
    newTablePtr = parseRecord.tablePtr;
  }

  // set the new version now
  impl_req->newTableVersion =
    newTablePtr.p->tableVersion =
    alter_obj_inc_schema_version(tablePtr.p->tableVersion);

  // add attribute stuff
  {
    const Uint32 noOfNewAttr =
      newTablePtr.p->noOfAttributes - tablePtr.p->noOfAttributes;
    const bool addAttrFlag =
      AlterTableReq::getAddAttrFlag(impl_req->changeMask);

    if (!(newTablePtr.p->noOfAttributes >= tablePtr.p->noOfAttributes) ||
        (noOfNewAttr != 0) != addAttrFlag) {
      jam();
      setError(error, AlterTableRef::UnsupportedChange, __LINE__);
      return;
    }

    if (master)
    {
      jam();
      impl_req->noOfNewAttr = noOfNewAttr;
      impl_req->newNoOfCharsets = newTablePtr.p->noOfCharsets;
      impl_req->newNoOfKeyAttrs = newTablePtr.p->noOfPrimkey;
    }
    else
    {
      jam();
      ndbrequire(impl_req->noOfNewAttr == noOfNewAttr);
    }

    LocalDLFifoList<AttributeRecord>
      list(c_attributeRecordPool, newTablePtr.p->m_attributes);
    AttributeRecordPtr attrPtr;
    list.first(attrPtr);
    Uint32 i = 0;
    for (i = 0; i < newTablePtr.p->noOfAttributes; i++) {
      if (i >= tablePtr.p->noOfAttributes) {
        jam();
        Uint32 j = 2 * (i - tablePtr.p->noOfAttributes);
        alterTabPtr.p->m_newAttrData[j + 0] = attrPtr.p->attributeDescriptor;
        alterTabPtr.p->m_newAttrData[j + 1] = attrPtr.p->extPrecision & ~0xFFFF;
      }
      list.next(attrPtr);
    }
  }

  D("alterTable_parse " << V(newTablePtr.i) << hex << V(newTablePtr.p->tableVersion));

  if (ERROR_INSERTED(6121)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    setError(error, 9121, __LINE__);
    return;
  }

  // save original schema file entry
  {
    XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
    SchemaFile::TableEntry * tableEntry = getTableEntry(xsf, impl_req->tableId);
    alterTabPtr.p->m_orig_entry = *tableEntry;
  }

  // master rewrites DictTabInfo (it is re-parsed only on slaves)
  if (master)
  {
    jam();
    releaseSections(handle);
    SimplePropertiesSectionWriter w(getSectionSegmentPool());
    packTableIntoPages(w, alterTabPtr.p->m_newTablePtr);

    SegmentedSectionPtr tabInfoPtr;
    w.getPtr(tabInfoPtr);
    handle.m_ptr[AlterTabReq::DICT_TAB_INFO] = tabInfoPtr;
    handle.m_cnt = 1;
  }

  // save sections
  saveOpSection(op_ptr, handle, AlterTabReq::DICT_TAB_INFO);
}

bool
Dbdict::alterTable_subOps(Signal* signal, SchemaOpPtr op_ptr)
{
  D("alterTable_subOps" << V(op_ptr.i) << *op_ptr.p);
  return false;
}

void
Dbdict::alterTable_reply(Signal* signal, SchemaOpPtr op_ptr, ErrorInfo error)
{
  jam();
  D("alterTable_reply" << V(op_ptr.i) << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  AlterTableRecPtr alterTabPtr;
  getOpRec(op_ptr, alterTabPtr);
  const AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  if (!hasError(error)) {
    AlterTableConf* conf = (AlterTableConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->clientData = op_ptr.p->m_clientData;
    conf->transId = trans_ptr.p->m_transId;
    conf->tableId = impl_req->tableId;
    conf->tableVersion = impl_req->tableVersion;
    conf->newTableVersion = impl_req->newTableVersion;

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_ALTER_TABLE_CONF, signal,
               AlterTableConf::SignalLength, JBB);
  } else {
    jam();
    AlterTableRef* ref = (AlterTableRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->clientData = op_ptr.p->m_clientData;
    ref->transId = trans_ptr.p->m_transId;
    getError(error, ref);
    ref->errorStatus = error.errorStatus;
    ref->errorKey = error.errorKey;

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_ALTER_TABLE_REF, signal,
               AlterTableRef::SignalLength, JBB);
  }
}

// AlterTable: PREPARE

void
Dbdict::alterTable_prepare(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  AlterTableRecPtr alterTabPtr;
  getOpRec(op_ptr, alterTabPtr);
  //const AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  D("alterTable_prepare" << *op_ptr.p);

  Mutex mutex(signal, c_mutexMgr, alterTabPtr.p->m_define_backup_mutex);
  Callback c = {
    safe_cast(&Dbdict::alterTable_backup_mutex_locked),
    op_ptr.p->op_key
  };
  bool ok = mutex.lock(c);
  ndbrequire(ok);
  return;
}

void
Dbdict::alterTable_backup_mutex_locked(Signal* signal,
                                       Uint32 op_key,
                                       Uint32 ret)
{
  jamEntry();
  D("alterTable_backup_mutex_locked");

  ndbrequire(ret == 0);

  SchemaOpPtr op_ptr;
  AlterTableRecPtr alterTabPtr;
  findSchemaOp(op_ptr, alterTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  const AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, impl_req->tableId);

  Mutex mutex(signal, c_mutexMgr, alterTabPtr.p->m_define_backup_mutex);
  mutex.unlock(); // ignore response

  if (tablePtr.p->tabState == TableRecord::BACKUP_ONGOING)
  {
    jam();
    setError(op_ptr, AlterTableRef::BackupInProgress, __LINE__);
    sendTransRef(signal, op_ptr);
    return;
  }

  jam();
  // wl3600_todo fix state
  tablePtr.p->tabState = TableRecord::PREPARE_DROPPING;

  TableRecordPtr newTablePtr = alterTabPtr.p->m_newTablePtr;

  const Uint32 changeMask = impl_req->changeMask;
  Uint32& changeMaskDone = alterTabPtr.p->m_changeMaskDone; // ref

  if (AlterTableReq::getNameFlag(changeMask))
  {
    jam();
    const Uint32 sz = MAX_TAB_NAME_SIZE;
    D("alter name:"
      << " old=" << copyRope<sz>(tablePtr.p->tableName)
      << " new=" << copyRope<sz>(newTablePtr.p->tableName));

    Ptr<DictObject> obj_ptr;
    c_obj_pool.getPtr(obj_ptr, tablePtr.p->m_obj_ptr_i);

    // remove old name from hash
    c_obj_hash.remove(obj_ptr);

    // save old name and replace it by new
    bool ok =
      copyRope<sz>(alterTabPtr.p->m_oldTableName, tablePtr.p->tableName) &&
      copyRope<sz>(tablePtr.p->tableName, newTablePtr.p->tableName);
    ndbrequire(ok);

    // add new name to object hash
    obj_ptr.p->m_name = tablePtr.p->tableName;
    c_obj_hash.add(obj_ptr);

    AlterTableReq::setNameFlag(changeMaskDone, true);
  }

  if (AlterTableReq::getFrmFlag(changeMask))
  {
    jam();
    // save old frm and replace it by new
    const Uint32 sz = MAX_FRM_DATA_SIZE;
    bool ok =
      copyRope<sz>(alterTabPtr.p->m_oldFrmData, tablePtr.p->frmData) &&
      copyRope<sz>(tablePtr.p->frmData, newTablePtr.p->frmData);
    ndbrequire(ok);

    AlterTableReq::setFrmFlag(changeMaskDone, true);
  }

  if (AlterTableReq::getAddAttrFlag(changeMask))
  {
    jam();

    /* Move the column definitions to the real table definitions. */
    LocalDLFifoList<AttributeRecord>
      list(c_attributeRecordPool, tablePtr.p->m_attributes);
    LocalDLFifoList<AttributeRecord>
      newlist(c_attributeRecordPool, newTablePtr.p->m_attributes);

    const Uint32 noOfNewAttr = impl_req->noOfNewAttr;
    ndbrequire(noOfNewAttr > 0);
    Uint32 i;

    /* Move back to find the first column to move. */
    AttributeRecordPtr pPtr;
    ndbrequire(newlist.last(pPtr));
    for (i = 1; i < noOfNewAttr; i++) {
      jam();
      ndbrequire(newlist.prev(pPtr));
    }

    /* Move columns. */
    for (i = 0; i < noOfNewAttr; i++) {
      AttributeRecordPtr qPtr = pPtr;
      newlist.next(pPtr);
      newlist.remove(qPtr);
      list.addLast(qPtr);
    }
    tablePtr.p->noOfAttributes += noOfNewAttr;
  }

  alterTable_toLocal(signal, op_ptr);
}

void
Dbdict::alterTable_toLocal(Signal* signal, SchemaOpPtr op_ptr)
{
  AlterTableRecPtr alterTabPtr;
  getOpRec(op_ptr, alterTabPtr);
  const AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  const Uint32 blockIndex = alterTabPtr.p->m_blockIndex;
  ndbrequire(blockIndex < AlterTableRec::BlockCount);
  const Uint32 blockNo = alterTabPtr.p->m_blockNo[blockIndex];

  const char* const blockName = getBlockName(blockNo);
  D("alterTable_toLocal" << V(blockIndex) << V(blockName));

  AlterTabReq* req = (AlterTabReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->requestType = AlterTabReq::AlterTablePrepare;
  req->tableId = impl_req->tableId;
  req->tableVersion = impl_req->tableVersion;
  req->newTableVersion = impl_req->newTableVersion;
  req->gci = impl_req->gci;
  req->changeMask = impl_req->changeMask;
  req->connectPtr = blockNo == DBTUP ? alterTabPtr.p->m_tupAlterTabPtr : RNIL;
  req->noOfNewAttr = impl_req->noOfNewAttr;
  req->newNoOfCharsets = impl_req->newNoOfCharsets;
  req->newNoOfKeyAttrs = impl_req->newNoOfKeyAttrs;

  Callback c = {
    safe_cast(&Dbdict::alterTable_fromLocal),
    op_ptr.p->op_key
  };
  op_ptr.p->m_callback = c;

  if (ERROR_INSERTED(6131) &&
      blockIndex + 1 == AlterTableRec::BlockCount) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    AlterTabRef* ref = (AlterTabRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->senderData = op_ptr.p->op_key;
    ref->errorCode = 9131;
    sendSignal(reference(), GSN_ALTER_TAB_REF, signal,
               AlterTabRef::SignalLength, JBB);
    return;
  }

  BlockReference blockRef = numberToRef(blockNo, getOwnNodeId());
  const bool sendNewAttrData = req->noOfNewAttr > 0 && blockNo == DBTUP;

  if (!sendNewAttrData) {
    jam();
    sendSignal(blockRef, GSN_ALTER_TAB_REQ, signal,
               AlterTabReq::SignalLength, JBB);
  } else {
    jam();
    LinearSectionPtr ptr[3];
    ptr[0].p = alterTabPtr.p->m_newAttrData;
    ptr[0].sz = 2 * impl_req->noOfNewAttr;
    sendSignal(blockRef, GSN_ALTER_TAB_REQ, signal,
               AlterTabReq::SignalLength, JBB, ptr, 1);
  }
}

void
Dbdict::alterTable_fromLocal(Signal* signal,
                             Uint32 op_key,
                             Uint32 ret)
{
  D("alterTable_fromLocal");

  SchemaOpPtr op_ptr;
  AlterTableRecPtr alterTabPtr;
  findSchemaOp(op_ptr, alterTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  Uint32& blockIndex = alterTabPtr.p->m_blockIndex; //ref
  ndbrequire(blockIndex < AlterTableRec::BlockCount);
  const Uint32 blockNo = alterTabPtr.p->m_blockNo[blockIndex];

  if (ret)
  {
    jam();
    setError(op_ptr, ret, __LINE__);
    sendTransRef(signal, op_ptr);
    return;
  }

  blockIndex += 1;

  // save TUP operation record for commit/abort
  const AlterTabConf* conf = (const AlterTabConf*)signal->getDataPtr();
  if (blockNo == DBTUP)
  {
    jam();
    alterTabPtr.p->m_tupAlterTabPtr = conf->connectPtr;
  }

  if (blockIndex < AlterTableRec::BlockCount)
  {
    jam();
    alterTable_toLocal(signal, op_ptr);
    return;
  }
  else
  {
    jam();
    sendTransConf(signal, op_ptr);
  }
}

// AlterTable: COMMIT

void
Dbdict::alterTable_commit(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  AlterTableRecPtr alterTabPtr;
  getOpRec(op_ptr, alterTabPtr);
  //const AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  D("alterTable_commit" << *op_ptr.p);

  // wl3600_todo hmm there is no prepare to disk

  ndbrequire(signal->getNoOfSections() == 0);

  alterTable_toTupCommit(signal, op_ptr);
}

void
Dbdict::alterTable_toTupCommit(Signal* signal, SchemaOpPtr op_ptr)
{
  D("alterTable_toTupCommit");

  AlterTableRecPtr alterTabPtr;
  getOpRec(op_ptr, alterTabPtr);
  const AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  AlterTabReq* req = (AlterTabReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->requestType = AlterTabReq::AlterTableCommit;
  req->tableId = impl_req->tableId;
  req->tableVersion = impl_req->tableVersion;
  req->newTableVersion = impl_req->newTableVersion;
  req->gci = impl_req->gci;
  req->changeMask = impl_req->changeMask;
  req->connectPtr = alterTabPtr.p->m_tupAlterTabPtr;
  req->noOfNewAttr = impl_req->noOfNewAttr;
  req->newNoOfCharsets = impl_req->newNoOfCharsets;
  req->newNoOfKeyAttrs = impl_req->newNoOfKeyAttrs;

  Callback c = {
    safe_cast(&Dbdict::alterTable_fromTupCommit),
    op_ptr.p->op_key
  };
  op_ptr.p->m_callback = c;

  sendSignal(DBTUP_REF, GSN_ALTER_TAB_REQ, signal,
             AlterTabReq::SignalLength, JBB);
}

void
Dbdict::alterTable_fromTupCommit(Signal* signal,
                                 Uint32 op_key,
                                 Uint32 ret)
{
  D("alterTable_fromTupCommit");

  SchemaOpPtr op_ptr;
  AlterTableRecPtr alterTabPtr;
  findSchemaOp(op_ptr, alterTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  ndbrequire(ret == 0); // Failure during commit is not allowed

  const AlterTabReq* impl_req = &alterTabPtr.p->m_request;
  const Uint32 tableId = impl_req->tableId;
  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, tableId);

  const OpSection& tabInfoSec =
    getOpSection(op_ptr, CreateTabReq::DICT_TAB_INFO);
  const Uint32 size = tabInfoSec.getSize();
  bool savetodisk = !(tablePtr.p->m_bits & TableRecord::TR_Temporary);

  // update table record
  tablePtr.p->packedSize = size;
  tablePtr.p->tableVersion = impl_req->newTableVersion;
  tablePtr.p->gciTableCreated = impl_req->gci;

  SchemaFile::TableEntry tabEntry;
  tabEntry.init();
  tabEntry.m_tableVersion = impl_req->newTableVersion;
  tabEntry.m_tableType = tablePtr.p->tableType;
  if (savetodisk)
    tabEntry.m_tableState = SchemaFile::ALTER_TABLE_COMMITTED;
  else
    tabEntry.m_tableState = SchemaFile::TEMPORARY_TABLE_COMMITTED;
  tabEntry.m_gcp = impl_req->gci;
  tabEntry.m_info_words = size;
  tabEntry.m_transId = 0;

  Callback callback = {
    safe_cast(&Dbdict::alterTab_writeSchemaConf),
    op_ptr.p->op_key
  };

  updateSchemaState(signal, tableId, &tabEntry, &callback, savetodisk);
}

void
Dbdict::alterTab_writeSchemaConf(Signal* signal, Uint32 op_key, Uint32 ret)
{
  SchemaOpPtr op_ptr;
  AlterTableRecPtr alterTabPtr;
  findSchemaOp(op_ptr, alterTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  const AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, impl_req->tableId);
  bool savetodisk = !(tablePtr.p->m_bits & TableRecord::TR_Temporary);

  const OpSection& tabInfoSec =
    getOpSection(op_ptr, CreateTabReq::DICT_TAB_INFO);

  Callback callback = {
    safe_cast(&Dbdict::alterTab_writeTableConf),
    callback.m_callbackData = op_ptr.p->op_key
  };

  if (savetodisk) {
    writeTableFile(signal, impl_req->tableId, tabInfoSec, &callback);
  } else {
    execute(signal, callback, 0);
  }
}

void
Dbdict::alterTab_writeTableConf(Signal* signal,
                                Uint32 op_key,
                                Uint32 ret)
{
  jam();
  SchemaOpPtr op_ptr;
  AlterTableRecPtr alterTabPtr;
  findSchemaOp(op_ptr, alterTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  const AlterTabReq* impl_req = &alterTabPtr.p->m_request;
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  TableRecordPtr tabPtr;
  c_tableRecordPool.getPtr(tabPtr, impl_req->tableId);

  tabPtr.p->tabState = TableRecord::DEFINED;

  // inform Suma so it can send events to any subscribers of the table
  {
    AlterTabReq* req = (AlterTabReq*)signal->getDataPtrSend();

    // special use of senderRef
    if (trans_ptr.p->m_isMaster)
      req->senderRef = trans_ptr.p->m_clientRef;
    else
      req->senderRef = 0;
    req->senderData = op_key;
    req->tableId = impl_req->tableId;
    req->tableVersion = impl_req->tableVersion;
    req->newTableVersion = impl_req->newTableVersion;
    req->gci = tabPtr.p->gciTableCreated;
    req->requestType = 0;
    req->changeMask = impl_req->changeMask;
    req->connectPtr = RNIL;
    req->noOfNewAttr = impl_req->noOfNewAttr;
    req->newNoOfCharsets = impl_req->newNoOfCharsets;
    req->newNoOfKeyAttrs = impl_req->newNoOfKeyAttrs;

    const OpSection& tabInfoSec =
      getOpSection(op_ptr, AlterTabReq::DICT_TAB_INFO);
    SegmentedSectionPtr tabInfoPtr;
    bool ok = copyOut(tabInfoSec, tabInfoPtr);
    ndbrequire(ok);

    SectionHandle handle(this, tabInfoPtr.i);
    sendSignal(SUMA_REF, GSN_ALTER_TAB_REQ, signal,
               AlterTabReq::SignalLength, JBB, &handle);
  }
  
  // older way to notify  wl3600_todo disable to find SUMA problems
  {
    ApiBroadcastRep* api= (ApiBroadcastRep*)signal->getDataPtrSend();
    api->gsn = GSN_ALTER_TABLE_REP;
    api->minVersion = MAKE_VERSION(4,1,15);

    AlterTableRep* rep = (AlterTableRep*)api->theData;
    rep->tableId = tabPtr.p->tableId;
    // wl3600_todo wants old version?
    rep->tableVersion = impl_req->tableVersion;
    rep->changeType = AlterTableRep::CT_ALTERED;

    char oldTableName[MAX_TAB_NAME_SIZE];
    memset(oldTableName, 0, sizeof(oldTableName));
    {
      const RopeHandle& rh =
        AlterTableReq::getNameFlag(impl_req->changeMask)
          ? alterTabPtr.p->m_oldTableName
          : tabPtr.p->tableName;
      ConstRope r(c_rope_pool, rh);
      r.copy(oldTableName);
    }

    LinearSectionPtr ptr[3];
    ptr[0].p = (Uint32*)oldTableName;
    ptr[0].sz = (sizeof(oldTableName) + 3) >> 2;

    sendSignal(QMGR_REF, GSN_API_BROADCAST_REP, signal,
	       ApiBroadcastRep::SignalLength + AlterTableRep::SignalLength,
	       JBB, ptr, 1);
  }

  releaseTableObject(alterTabPtr.p->m_newTablePtr.i, false);
  sendTransConf(signal, op_ptr);
}

// AlterTable: ABORT

void
Dbdict::alterTable_abortParse(Signal* signal, SchemaOpPtr op_ptr)
{
  D("alterTable_abortParse" << *op_ptr.p);

  AlterTableRecPtr alterTabPtr;
  getOpRec(op_ptr, alterTabPtr);
  //const AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  TableRecordPtr& newTablePtr = alterTabPtr.p->m_newTablePtr; // ref
  if (!newTablePtr.isNull()) {
    jam();
    // release the temporary work table
    releaseTableObject(newTablePtr.i, false);
    newTablePtr.setNull();
  }

  TableRecordPtr& tablePtr = alterTabPtr.p->m_tablePtr; // ref
  if (!tablePtr.isNull()) {
    jam();
    // wl3600_todo tabState should be rationalized
    if (tablePtr.p->tabState != TableRecord::BACKUP_ONGOING) {
      jam();
      tablePtr.p->tabState = TableRecord::DEFINED;
    }
    tablePtr.setNull();
  }

  sendTransConf(signal, op_ptr);
}

void
Dbdict::alterTable_abortPrepare(Signal* signal, SchemaOpPtr op_ptr)
{
  D("alterTable_abortPrepare" << *op_ptr.p);

  AlterTableRecPtr alterTabPtr;
  getOpRec(op_ptr, alterTabPtr);
  const AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  // reset DICT memory changes (there was no prepare to disk)
  {
    jam();

    TableRecordPtr tablePtr;
    c_tableRecordPool.getPtr(tablePtr, impl_req->tableId);
    TableRecordPtr newTablePtr = alterTabPtr.p->m_newTablePtr;

    const Uint32 changeMask = alterTabPtr.p->m_changeMaskDone;

    if (AlterTableReq::getNameFlag(changeMask))
    {
      jam();
      const Uint32 sz = MAX_TAB_NAME_SIZE;
      D("reset name:"
        << " new=" << copyRope<sz>(tablePtr.p->tableName)
        << " old=" << copyRope<sz>(alterTabPtr.p->m_oldTableName));

      Ptr<DictObject> obj_ptr;
      c_obj_pool.getPtr(obj_ptr, tablePtr.p->m_obj_ptr_i);

      // remove new name from hash
      c_obj_hash.remove(obj_ptr);

      // copy old name back
      bool ok =
        copyRope<sz>(tablePtr.p->tableName, alterTabPtr.p->m_oldTableName);
      ndbrequire(ok);

      // add old name to object hash
      obj_ptr.p->m_name = tablePtr.p->tableName;
      c_obj_hash.add(obj_ptr);
    }

    if (AlterTableReq::getFrmFlag(changeMask))
    {
      jam();
      const Uint32 sz = MAX_FRM_DATA_SIZE;

      // copy old frm back
      bool ok =
        copyRope<sz>(tablePtr.p->frmData, alterTabPtr.p->m_oldFrmData);
      ndbrequire(ok);
    }

    if (AlterTableReq::getAddAttrFlag(changeMask))
    {
      jam();

      /* Release the extra columns, not to be used anyway. */
      LocalDLFifoList<AttributeRecord>
        list(c_attributeRecordPool, tablePtr.p->m_attributes);

      const Uint32 noOfNewAttr = impl_req->noOfNewAttr;
      ndbrequire(noOfNewAttr > 0);
      Uint32 i;

      for (i= 0; i < noOfNewAttr; i++) {
        AttributeRecordPtr pPtr;
        ndbrequire(list.last(pPtr));
        list.release(pPtr);
      }
    }
  }

  if (alterTabPtr.p->m_blockIndex > 0)
  {
    jam();
    /*
     * Local blocks have only updated table version.
     * Reset it to original in reverse block order.
     */
    alterTable_abortToLocal(signal, op_ptr);
    return;
  }
  else
  {
    jam();
    sendTransConf(signal, op_ptr);
    return;
  }
}

void
Dbdict::alterTable_abortToLocal(Signal* signal, SchemaOpPtr op_ptr)
{
  AlterTableRecPtr alterTabPtr;
  getOpRec(op_ptr, alterTabPtr);
  const AlterTabReq* impl_req = &alterTabPtr.p->m_request;

  const Uint32 blockCount = alterTabPtr.p->m_blockIndex;
  ndbrequire(blockCount != 0 && blockCount <= AlterTableRec::BlockCount);
  const Uint32 blockIndex = blockCount - 1;
  const Uint32 blockNo = alterTabPtr.p->m_blockNo[blockIndex];

  const char* const blockName = getBlockName(blockNo);
  D("alterTable_abortToLocal" << V(blockIndex) << V(blockName));

  AlterTabReq* req = (AlterTabReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->requestType = AlterTabReq::AlterTableRevert;
  req->tableId = impl_req->tableId;
  req->tableVersion = impl_req->tableVersion;
  req->newTableVersion = impl_req->newTableVersion;
  req->gci = impl_req->gci;
  req->changeMask = impl_req->changeMask;
  req->connectPtr = blockNo == DBTUP ? alterTabPtr.p->m_tupAlterTabPtr : RNIL;
  req->noOfNewAttr = impl_req->noOfNewAttr;
  req->newNoOfCharsets = impl_req->newNoOfCharsets;
  req->newNoOfKeyAttrs = impl_req->newNoOfKeyAttrs;

  Callback c = {
    safe_cast(&Dbdict::alterTable_abortFromLocal),
    op_ptr.p->op_key
  };
  op_ptr.p->m_callback = c;

  BlockReference blockRef = numberToRef(blockNo, getOwnNodeId());
  sendSignal(blockRef, GSN_ALTER_TAB_REQ, signal,
             AlterTabReq::SignalLength, JBB);
}

void
Dbdict::alterTable_abortFromLocal(Signal*signal, Uint32 op_key, Uint32 ret)
{
  SchemaOpPtr op_ptr;
  AlterTableRecPtr alterTabPtr;
  findSchemaOp(op_ptr, alterTabPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  const Uint32 blockCount = alterTabPtr.p->m_blockIndex;
  ndbrequire(blockCount != 0 && blockCount <= AlterTableRec::BlockCount);
  const Uint32 blockIndex = blockCount - 1;
  alterTabPtr.p->m_blockIndex = blockIndex;

  ndbrequire(ret == 0); // abort is not allowed to fail

  if (blockIndex > 0)
  {
    jam();
    alterTable_toLocal(signal, op_ptr);
    return;
  }
  else
  {
    jam();
    sendTransConf(signal, op_ptr);
  }
}

// AlterTable: MISC

void
Dbdict::execALTER_TAB_CONF(Signal* signal)
{
  jamEntry();
  const AlterTabConf* conf = (const AlterTabConf*)signal->getDataPtr();
  ndbrequire(refToNode(conf->senderRef) == getOwnNodeId());
  handleDictConf(signal, conf);
}

void
Dbdict::execALTER_TAB_REF(Signal* signal)
{
  jamEntry();
  const AlterTabRef* ref = (const AlterTabRef*)signal->getDataPtr();
  ndbrequire(refToNode(ref->senderRef) == getOwnNodeId());
  handleDictRef(signal, ref);
}

// AlterTable: END

/* **************************************************************** */
/* ---------------------------------------------------------------- */
/* MODULE:          EXTERNAL INTERFACE TO DATA -------------------- */
/* ---------------------------------------------------------------- */
/*                                                                  */
/* This module contains the code that is used by other modules to.  */
/* access the data within DBDICT.                                   */
/* ---------------------------------------------------------------- */
/* **************************************************************** */

void Dbdict::execGET_TABLEDID_REQ(Signal * signal) 
{
  jamEntry();
  ndbrequire(signal->getNoOfSections() == 1);  
  GetTableIdReq const * req = (GetTableIdReq *)signal->getDataPtr();
  Uint32 senderData = req->senderData;
  Uint32 senderRef = req->senderRef;
  Uint32 len = req->len;

  if(len>MAX_TAB_NAME_SIZE)
  {
    jam();
    sendGET_TABLEID_REF((Signal*)signal, 
			(GetTableIdReq *)req, 
			GetTableIdRef::TableNameTooLong);
    return;
  }

  char tableName[MAX_TAB_NAME_SIZE];
  SectionHandle handle(this, signal);
  SegmentedSectionPtr ssPtr;
  handle.getSection(ssPtr,GetTableIdReq::TABLE_NAME);
  copy((Uint32*)tableName, ssPtr);
  releaseSections(handle);
    
  DictObject * obj_ptr_p = get_object(tableName, len);
  if(obj_ptr_p == 0 || !DictTabInfo::isTable(obj_ptr_p->m_type)){
    jam();
    sendGET_TABLEID_REF(signal, 
			(GetTableIdReq *)req, 
			GetTableIdRef::TableNotDefined);
    return;
  }

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, obj_ptr_p->m_id); 
  
  GetTableIdConf * conf = (GetTableIdConf *)req;
  conf->tableId = tablePtr.p->tableId;
  conf->schemaVersion = tablePtr.p->tableVersion;
  conf->senderData = senderData;
  sendSignal(senderRef, GSN_GET_TABLEID_CONF, signal,
	     GetTableIdConf::SignalLength, JBB);  
}


void Dbdict::sendGET_TABLEID_REF(Signal* signal, 
				 GetTableIdReq * req,
				 GetTableIdRef::ErrorCode errorCode) 
{
  GetTableIdRef * const ref = (GetTableIdRef *)req;
  /**
   * The format of GetTabInfo Req/Ref is the same
   */
  BlockReference retRef = req->senderRef;
  ref->err = errorCode;
  sendSignal(retRef, GSN_GET_TABLEID_REF, signal, 
	     GetTableIdRef::SignalLength, JBB);
}

/* ---------------------------------------------------------------- */
// Get a full table description.
/* ---------------------------------------------------------------- */
void Dbdict::execGET_TABINFOREQ(Signal* signal) 
{
  jamEntry();
  if(!assembleFragments(signal)) 
  { 
    return;
  }  

  GetTabInfoReq * const req = (GetTabInfoReq *)&signal->theData[0];
  SectionHandle handle(this, signal);

  /**
   * If I get a GET_TABINFO_REQ from myself
   * it's is a one from the time queue
   */
  bool fromTimeQueue = (signal->senderBlockRef() == reference());
  
  if (c_retrieveRecord.busyState && fromTimeQueue == true) {
    jam();
    
    sendSignalWithDelay(reference(), GSN_GET_TABINFOREQ, signal, 30, 
			signal->length(),
			&handle);
    return;
  }//if

  const Uint32 MAX_WAITERS = 5;
  
  if(c_retrieveRecord.busyState && fromTimeQueue == false)
  {
    jam();
    if(c_retrieveRecord.noOfWaiters < MAX_WAITERS){
      jam();
      c_retrieveRecord.noOfWaiters++;
      
      sendSignalWithDelay(reference(), GSN_GET_TABINFOREQ, signal, 30, 
			  signal->length(),
			  &handle);
      return;
    }
    releaseSections(handle);
    sendGET_TABINFOREF(signal, req, GetTabInfoRef::Busy, __LINE__);
    return;
  }
  
  if(fromTimeQueue){
    jam();
    c_retrieveRecord.noOfWaiters--;
  } 

  const bool useLongSig = (req->requestType & GetTabInfoReq::LongSignalConf);
  const bool byName = (req->requestType & GetTabInfoReq::RequestByName);
  const Uint32 transId = req->schemaTransId;

  Uint32 obj_id = RNIL;
  if (byName) {
    jam();
    ndbrequire(handle.m_cnt == 1);
    const Uint32 len = req->tableNameLen;
    
    if(len > MAX_TAB_NAME_SIZE){
      jam();
      releaseSections(handle);
      sendGET_TABINFOREF(signal, req, GetTabInfoRef::TableNameTooLong, __LINE__);
      return;
    }

    Uint32 tableName[(MAX_TAB_NAME_SIZE + 3) / 4];
    SegmentedSectionPtr ssPtr;
    handle.getSection(ssPtr,GetTabInfoReq::TABLE_NAME);
    copy(tableName, ssPtr);
    
    DictObject * old_ptr_p = get_object((char*)tableName, len);
    if(old_ptr_p)
      obj_id = old_ptr_p->m_id;
  } else {
    jam();
    obj_id = req->tableId;
  }
  releaseSections(handle);

  SchemaFile::TableEntry *objEntry = 0;
  if(obj_id != RNIL){
    XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
    objEntry = getTableEntry(xsf, obj_id);      
  }

  // The table seached for was not found
  if(objEntry == 0){
    jam();
    sendGET_TABINFOREF(signal, req, GetTabInfoRef::TableNotDefined, __LINE__);
    return;
  }//if

  // If istable/index, allow ADD_STARTED (not to ref)

  D("execGET_TABINFOREQ" << V(transId) << " " << *objEntry);

  if (transId != 0 && transId == objEntry->m_transId) {
    jam();
    // see own trans always
  } else {
    if (objEntry->m_transId != 0) {
      jam();
      // cannot see another uncommitted trans
      sendGET_TABINFOREF(signal, req, GetTabInfoRef::TableNotDefined, __LINE__);
      return;
    }
    if (objEntry->m_tableState != SchemaFile::TABLE_ADD_COMMITTED &&
        objEntry->m_tableState != SchemaFile::ALTER_TABLE_COMMITTED &&
        objEntry->m_tableState != SchemaFile::TEMPORARY_TABLE_COMMITTED){
      jam();
      sendGET_TABINFOREF(signal, req, GetTabInfoRef::TableNotDefined, __LINE__);
      return;
    }//if

    if (DictTabInfo::isTable(objEntry->m_tableType) || 
        DictTabInfo::isIndex(objEntry->m_tableType))
    {
      jam();
      TableRecordPtr tabPtr;
      c_tableRecordPool.getPtr(tabPtr, obj_id);
      if (tabPtr.p->tabState != TableRecord::DEFINED &&
          tabPtr.p->tabState != TableRecord::BACKUP_ONGOING)
      {
        jam();
        sendGET_TABINFOREF(signal, req, GetTabInfoRef::TableNotDefined, __LINE__);
        return;
      }
      ndbrequire(objEntry->m_tableState == SchemaFile::TEMPORARY_TABLE_COMMITTED ||
                 !(tabPtr.p->m_bits & TableRecord::TR_Temporary));
    }
  }
  
  c_retrieveRecord.busyState = true;
  c_retrieveRecord.blockRef = req->senderRef;
  c_retrieveRecord.m_senderData = req->senderData;
  c_retrieveRecord.tableId = obj_id;
  c_retrieveRecord.currentSent = 0;
  c_retrieveRecord.m_useLongSig = useLongSig;
  c_retrieveRecord.m_table_type = objEntry->m_tableType;
  c_packTable.m_state = PackTable::PTS_GET_TAB;

  if(objEntry->m_tableType==DictTabInfo::Datafile)
  {
    jam();
    GetTabInfoReq *req= (GetTabInfoReq*)signal->getDataPtrSend();
    req->senderData= c_retrieveRecord.retrievePage;
    req->senderRef= reference();
    req->requestType= GetTabInfoReq::RequestById;
    req->tableId= obj_id;
    req->schemaTransId = 0;

    sendSignal(TSMAN_REF, GSN_GET_TABINFOREQ, signal,
	       GetTabInfoReq::SignalLength, JBB);
  }
  else if(objEntry->m_tableType==DictTabInfo::LogfileGroup)
  {
    jam();
    GetTabInfoReq *req= (GetTabInfoReq*)signal->getDataPtrSend();
    req->senderData= c_retrieveRecord.retrievePage;
    req->senderRef= reference();
    req->requestType= GetTabInfoReq::RequestById;
    req->tableId= obj_id;
    req->schemaTransId = 0;

    sendSignal(LGMAN_REF, GSN_GET_TABINFOREQ, signal,
	       GetTabInfoReq::SignalLength, JBB);
  }
  else
  {
    jam();
    signal->theData[0] = ZPACK_TABLE_INTO_PAGES;
    signal->theData[1] = obj_id;
    signal->theData[2] = objEntry->m_tableType;
    signal->theData[3] = c_retrieveRecord.retrievePage;
    sendSignal(reference(), GSN_CONTINUEB, signal, 4, JBB);
  }
  jam();
}//execGET_TABINFOREQ()

void Dbdict::sendGetTabResponse(Signal* signal) 
{
  PageRecordPtr pagePtr;
  DictTabInfo * const conf = (DictTabInfo *)&signal->theData[0];
  conf->senderRef   = reference();
  conf->senderData  = c_retrieveRecord.m_senderData;
  conf->requestType = DictTabInfo::GetTabInfoConf;
  conf->totalLen    = c_retrieveRecord.retrievedNoOfWords;

  c_pageRecordArray.getPtr(pagePtr, c_retrieveRecord.retrievePage);
  Uint32* pagePointer = (Uint32*)&pagePtr.p->word[0] + ZPAGE_HEADER_SIZE;
  
  if(c_retrieveRecord.m_useLongSig){
    jam();
    GetTabInfoConf* conf = (GetTabInfoConf*)signal->getDataPtr();
    conf->gci = 0;
    conf->tableId = c_retrieveRecord.tableId;
    conf->senderData = c_retrieveRecord.m_senderData;
    conf->totalLen = c_retrieveRecord.retrievedNoOfWords;
    conf->tableType = c_retrieveRecord.m_table_type;

    Callback c = { safe_cast(&Dbdict::initRetrieveRecord), 0 };
    LinearSectionPtr ptr[3];
    ptr[0].p = pagePointer;
    ptr[0].sz = c_retrieveRecord.retrievedNoOfWords;
    sendFragmentedSignal(c_retrieveRecord.blockRef,
			 GSN_GET_TABINFO_CONF,
			 signal, 
			 GetTabInfoConf::SignalLength,
			 JBB,
			 ptr,
			 1,
			 c);
    return;
  }

  ndbrequire(false);
}//sendGetTabResponse()

void Dbdict::sendGET_TABINFOREF(Signal* signal, 
				GetTabInfoReq * req,
				GetTabInfoRef::ErrorCode errorCode,
                                Uint32 errorLine)
{
  jamEntry();
  const GetTabInfoReq req_copy = *req;
  GetTabInfoRef * const ref = (GetTabInfoRef *)&signal->theData[0];

  ref->senderData = req_copy.senderData;
  ref->senderRef = reference();
  ref->requestType = req_copy.requestType;
  ref->tableId = req_copy.tableId;
  ref->schemaTransId = req_copy.schemaTransId;
  ref->errorCode = (Uint32)errorCode;
  ref->errorLine = errorLine;
  
  BlockReference retRef = req_copy.senderRef;
  sendSignal(retRef, GSN_GET_TABINFOREF, signal,
             GetTabInfoRef::SignalLength, JBB);
}

void
Dbdict::execLIST_TABLES_REQ(Signal* signal)
{
  jamEntry();
  ListTablesReq * req = (ListTablesReq*)signal->getDataPtr();
  Uint32 senderRef  = req->senderRef;
  Uint32 senderData = req->senderData;
  // save req flags
  const Uint32 reqTableId = req->getTableId();
  const Uint32 reqTableType = req->getTableType();
  const bool reqListNames = req->getListNames();
  const bool reqListIndexes = req->getListIndexes();
  // init the confs
  ListTablesConf * conf = (ListTablesConf *)signal->getDataPtrSend();
  conf->senderData = senderData;
  conf->counter = 0;
  Uint32 pos = 0;

  DLHashTable<DictObject>::Iterator iter;
  bool ok = c_obj_hash.first(iter);
  for(; ok; ok = c_obj_hash.next(iter)){
    Uint32 type = iter.curr.p->m_type;
    if ((reqTableType != (Uint32)0) && (reqTableType != type))
      continue;

    if (reqListIndexes && !DictTabInfo::isIndex(type))
      continue;

    TableRecordPtr tablePtr;
    if (DictTabInfo::isTable(type) || DictTabInfo::isIndex(type)){
      c_tableRecordPool.getPtr(tablePtr, iter.curr.p->m_id);

      if(reqListIndexes && (reqTableId != tablePtr.p->primaryTableId))
	continue;
      
      conf->tableData[pos] = 0;
      conf->setTableId(pos, tablePtr.i); // id
      conf->setTableType(pos, type); // type
      // state

      if(DictTabInfo::isTable(type)){
	switch (tablePtr.p->tabState) {
	case TableRecord::DEFINING:
	  conf->setTableState(pos, DictTabInfo::StateBuilding);
	  break;
	case TableRecord::PREPARE_DROPPING:
	case TableRecord::DROPPING:
	  conf->setTableState(pos, DictTabInfo::StateDropping);
	  break;
	case TableRecord::DEFINED:
	  conf->setTableState(pos, DictTabInfo::StateOnline);
	  break;
	case TableRecord::BACKUP_ONGOING:
	  conf->setTableState(pos, DictTabInfo::StateBackup);
	  break;
	default:
	  conf->setTableState(pos, DictTabInfo::StateBroken);
	  break;
	}
      }
      if (tablePtr.p->isIndex()) {
	switch (tablePtr.p->indexState) {
	case TableRecord::IS_OFFLINE:
	  conf->setTableState(pos, DictTabInfo::StateOffline);
	  break;
	case TableRecord::IS_BUILDING:
	  conf->setTableState(pos, DictTabInfo::StateBuilding);
	  break;
	case TableRecord::IS_DROPPING:
	  conf->setTableState(pos, DictTabInfo::StateDropping);
	  break;
	case TableRecord::IS_ONLINE:
	  conf->setTableState(pos, DictTabInfo::StateOnline);
	  break;
	default:
	  conf->setTableState(pos, DictTabInfo::StateBroken);
	  break;
	}
      }
      // Logging status
      if (! (tablePtr.p->m_bits & TableRecord::TR_Logged)) {
	conf->setTableStore(pos, DictTabInfo::StoreNotLogged);
      } else {
	conf->setTableStore(pos, DictTabInfo::StorePermanent);
      }
      // Temporary status
      if (tablePtr.p->m_bits & TableRecord::TR_Temporary) {
	conf->setTableTemp(pos, NDB_TEMP_TAB_TEMPORARY);
      } else {
	conf->setTableTemp(pos, NDB_TEMP_TAB_PERMANENT);
      }
      pos++;
    }
    if(DictTabInfo::isTrigger(type)){
      TriggerRecordPtr triggerPtr;
      c_triggerRecordPool.getPtr(triggerPtr, iter.curr.p->m_id);

      conf->tableData[pos] = 0;
      conf->setTableId(pos, triggerPtr.i);
      conf->setTableType(pos, type);
      switch (triggerPtr.p->triggerState) {
      case TriggerRecord::TS_DEFINING:
	conf->setTableState(pos, DictTabInfo::StateBuilding);
	break;
      case TriggerRecord::TS_OFFLINE:
	conf->setTableState(pos, DictTabInfo::StateOffline);
	break;
      case TriggerRecord::TS_ONLINE:
	conf->setTableState(pos, DictTabInfo::StateOnline);
	break;
      default:
	conf->setTableState(pos, DictTabInfo::StateBroken);
	break;
      }
      conf->setTableStore(pos, DictTabInfo::StoreNotLogged);
      pos++;
    }
    if (DictTabInfo::isFilegroup(type)){
      jam();
      conf->tableData[pos] = 0;
      conf->setTableId(pos, iter.curr.p->m_id);
      conf->setTableType(pos, type); // type
      conf->setTableState(pos, DictTabInfo::StateOnline);  // XXX todo
      pos++;
    }
    if (DictTabInfo::isFile(type)){
      jam();
      conf->tableData[pos] = 0;
      conf->setTableId(pos, iter.curr.p->m_id);
      conf->setTableType(pos, type); // type
      conf->setTableState(pos, DictTabInfo::StateOnline); // XXX todo
      pos++;
    }
    
    if (pos >= ListTablesConf::DataLength) {
      sendSignal(senderRef, GSN_LIST_TABLES_CONF, signal,
		 ListTablesConf::SignalLength, JBB);
      conf->counter++;
      pos = 0;
    }
    
    if (! reqListNames)
      continue;
    
    Rope name(c_rope_pool, iter.curr.p->m_name);
    const Uint32 size = name.size();
    conf->tableData[pos] = size;
    pos++;
    if (pos >= ListTablesConf::DataLength) {
      sendSignal(senderRef, GSN_LIST_TABLES_CONF, signal,
		 ListTablesConf::SignalLength, JBB);
      conf->counter++;
      pos = 0;
    }
    Uint32 i = 0;
    char tmp[MAX_TAB_NAME_SIZE];
    name.copy(tmp);
    while (i < size) {
      char* p = (char*)&conf->tableData[pos];
      for (Uint32 j = 0; j < 4; j++) {
	if (i < size)
	  *p++ = tmp[i++];
	else
	  *p++ = 0;
      }
      pos++;
      if (pos >= ListTablesConf::DataLength) {
	sendSignal(senderRef, GSN_LIST_TABLES_CONF, signal,
		   ListTablesConf::SignalLength, JBB);
	conf->counter++;
	pos = 0;
      }
    }
  }
  // last signal must have less than max length
  sendSignal(senderRef, GSN_LIST_TABLES_CONF, signal,
	     ListTablesConf::HeaderLength + pos, JBB);
}

// MODULE: CreateIndex

const Dbdict::OpInfo
Dbdict::CreateIndexRec::g_opInfo = {
  { 'C', 'I', 'n', 0 },
  GSN_CREATE_INDX_IMPL_REQ,
  CreateIndxImplReq::SignalLength,
  //
  &Dbdict::createIndex_seize,
  &Dbdict::createIndex_release,
  //
  &Dbdict::createIndex_parse,
  &Dbdict::createIndex_subOps,
  &Dbdict::createIndex_reply,
  //
  &Dbdict::createIndex_prepare,
  &Dbdict::createIndex_commit,
  //
  &Dbdict::createIndex_abortParse,
  &Dbdict::createIndex_abortPrepare
};

bool
Dbdict::createIndex_seize(SchemaOpPtr op_ptr)
{
  return seizeOpRec<CreateIndexRec>(op_ptr);
}

void
Dbdict::createIndex_release(SchemaOpPtr op_ptr)
{
  releaseOpRec<CreateIndexRec>(op_ptr);
}

void
Dbdict::execCREATE_INDX_REQ(Signal* signal)
{
  jamEntry();
  if (!assembleFragments(signal)) {
    jam();
    return;
  }
  SectionHandle handle(this, signal);

  const CreateIndxReq req_copy =
    *(const CreateIndxReq*)signal->getDataPtr();
  const CreateIndxReq* req = &req_copy;

  ErrorInfo error;
  do {
    SchemaOpPtr op_ptr;
    CreateIndexRecPtr createIndexPtr;
    CreateIndxImplReq* impl_req;

    startClientReq(op_ptr, createIndexPtr, req, impl_req, error);
    if (hasError(error)) {
      jam();
      break;
    }

    impl_req->tableId = req->tableId;
    impl_req->tableVersion = req->tableVersion;
    impl_req->indexType = req->indexType;
    // these are set in sub-operation create table
    impl_req->indexId = RNIL;
    impl_req->indexVersion = 0;

    handleClientReq(signal, op_ptr, handle);
    return;
  } while (0);

  releaseSections(handle);

  CreateIndxRef* ref = (CreateIndxRef*)signal->getDataPtrSend();

  ref->senderRef = reference();
  ref->clientData = req->clientData;
  ref->transId = req->transId;
  getError(error, ref);

  sendSignal(req->clientRef, GSN_CREATE_INDX_REF, signal,
             CreateIndxRef::SignalLength, JBB);
}

// CreateIndex: PARSE

void
Dbdict::createIndex_parse(Signal* signal, bool master,
                          SchemaOpPtr op_ptr,
                          SectionHandle& handle, ErrorInfo& error)
{
  D("createIndex_parse");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  CreateIndexRecPtr createIndexPtr;
  getOpRec(op_ptr, createIndexPtr);
  CreateIndxImplReq* impl_req = &createIndexPtr.p->m_request;

  if (checkSingleUserMode(trans_ptr.p->m_clientRef)) {
    jam();
    setError(error, CreateIndxRef::SingleUser, __LINE__);
    return;
  }

  // save attribute list
  AttributeList& attrList = createIndexPtr.p->m_attrList;
  {
    SegmentedSectionPtr ss_ptr;
    handle.getSection(ss_ptr, CreateIndxReq::ATTRIBUTE_LIST_SECTION);
    SimplePropertiesSectionReader r(ss_ptr, getSectionSegmentPool());
    r.reset(); // undo implicit first()
    if (!r.getWord(&attrList.sz) ||
        r.getSize() != 1 + attrList.sz ||
        attrList.sz == 0 ||
        attrList.sz > MAX_ATTRIBUTES_IN_INDEX ||
        !r.getWords(attrList.id, attrList.sz)) {
      jam();
      setError(error, CreateIndxRef::InvalidIndexType, __LINE__);
      return;
    }
  }

  // save name and some index table properties
  Uint32& bits = createIndexPtr.p->m_bits;
  bits = TableRecord::TR_RowChecksum;
  {
    SegmentedSectionPtr ss_ptr;
    handle.getSection(ss_ptr, CreateIndxReq::INDEX_NAME_SECTION);
    SimplePropertiesSectionReader r(ss_ptr, getSectionSegmentPool());
    DictTabInfo::Table tableDesc;
    tableDesc.init();
    SimpleProperties::UnpackStatus status =
      SimpleProperties::unpack(
          r, &tableDesc,
          DictTabInfo::TableMapping, DictTabInfo::TableMappingSize,
          true, true);
    if (status != SimpleProperties::Eof ||
        tableDesc.TableName[0] == 0)
    {
      ndbassert(false);
      setError(error, CreateIndxRef::InvalidName, __LINE__);
      return;
    }
    const Uint32 bytesize = sizeof(createIndexPtr.p->m_indexName);
    memcpy(createIndexPtr.p->m_indexName, tableDesc.TableName, bytesize);
    if (tableDesc.TableLoggedFlag) {
      jam();
      bits |= TableRecord::TR_Logged;
    }
    if (tableDesc.TableTemporaryFlag) {
      jam();
      bits |= TableRecord::TR_Temporary;
    }
    D("index " << createIndexPtr.p->m_indexName);
  }

  // check primary table
  TableRecordPtr tablePtr;
  {
    if (!(impl_req->tableId < c_tableRecordPool.getSize())) {
      jam();
      setError(error, CreateIndxRef::InvalidPrimaryTable, __LINE__);
      return;
    }
    c_tableRecordPool.getPtr(tablePtr, impl_req->tableId);

    if (!tablePtr.p->isTable()) {
      jam();
      setError(error, CreateIndxRef::InvalidPrimaryTable, __LINE__);
      return;
    }

    DictObjectPtr obj_ptr;
    c_obj_pool.getPtr(obj_ptr, tablePtr.p->m_obj_ptr_i);

    if (obj_ptr.p->m_trans_key == 0) {
      if (tablePtr.p->tabState != TableRecord::DEFINED &&
          tablePtr.p->tabState != TableRecord::BACKUP_ONGOING) {
        jam();
        setError(error, CreateIndxRef::InvalidPrimaryTable, __LINE__);
        return;
      }
    }
    else {
      if (obj_ptr.p->m_trans_key != trans_ptr.p->trans_key) {
        jam();
        setError(error, CreateIndxRef::InvalidPrimaryTable, __LINE__);
        return;
      }
    }
  }

  // check index type and set more properties
  {
    switch (impl_req->indexType) {
    case DictTabInfo::UniqueHashIndex:
      jam();
      createIndexPtr.p->m_fragmentType = DictTabInfo::DistrKeyUniqueHashIndex;
      break;
    case DictTabInfo::OrderedIndex:
      jam();
      if (bits & TableRecord::TR_Logged) {
        jam();
        setError(error, CreateIndxRef::InvalidIndexType, __LINE__);
        return;
      }
      createIndexPtr.p->m_fragmentType = DictTabInfo::DistrKeyOrderedIndex;
      // hash case is computed later from attribute list
      createIndexPtr.p->m_indexKeyLength = MAX_TTREE_NODE_SIZE;
      break;
    default:
      setError(error, CreateIndxRef::InvalidIndexType, __LINE__);
      return;
    }
  }

  // check that the temporary status of index is compatible with table
  {
    if (!(bits & TableRecord::TR_Temporary) &&
        (tablePtr.p->m_bits & TableRecord::TR_Temporary))
    {
      jam();
      setError(error, CreateIndxRef::TableIsTemporary, __LINE__);
      return;
    }
    if ((bits & TableRecord::TR_Temporary) &&
        !(tablePtr.p->m_bits & TableRecord::TR_Temporary))
    {
      // This could be implemented later, but mysqld does currently not detect
      // that the index disappears after SR, and it appears not too useful.
      jam();
      setError(error, CreateIndxRef::TableIsNotTemporary, __LINE__);
      return;
    }
    if ((bits & TableRecord::TR_Temporary) &&
        (bits & TableRecord::TR_Logged))
    {
      jam();
      setError(error, CreateIndxRef::NoLoggingTemporaryIndex, __LINE__);
      return;
    }
  }

  // check attribute list
  AttributeMask& attrMask = createIndexPtr.p->m_attrMask;
  AttributeMap& attrMap = createIndexPtr.p->m_attrMap;
  {
    Uint32 k;
    for (k = 0; k < attrList.sz; k++) {
      jam();
      Uint32 current_id = attrList.id[k];
      AttributeRecordPtr attrPtr;

      // find the attribute
      {
        LocalDLFifoList<AttributeRecord>
          list(c_attributeRecordPool, tablePtr.p->m_attributes);
        list.first(attrPtr);
        while (!attrPtr.isNull()) {
          jam();
          if (attrPtr.p->attributeId == current_id) {
            jam();
            break;
          }
          list.next(attrPtr);
        }
      }
      if (attrPtr.isNull()) {
        jam();
	ndbassert(false);
        setError(error, CreateIndxRef::BadRequestType, __LINE__);
        return;
      }

      // add attribute to mask
      if (attrMask.get(current_id)) {
        jam();
        setError(error, CreateIndxRef::DuplicateAttributes, __LINE__);
        return;
      }
      attrMask.set(current_id);

      // check disk based
      const Uint32 attrDesc = attrPtr.p->attributeDescriptor;
      if (AttributeDescriptor::getDiskBased(attrDesc)) {
        jam();
        setError(error, CreateIndxRef::IndexOnDiskAttributeError, __LINE__);
        return;
      }

      // re-order hash index by attribute id
      Uint32 new_index = k;
      if (impl_req->indexType == DictTabInfo::UniqueHashIndex) {
        jam();
        // map contains (old_index,attr_id) ordered by attr_id
        while (new_index > 0) {
          jam();
          if (current_id >= attrMap[new_index - 1].attr_id) {
            jam();
            break;
          }
          attrMap[new_index] = attrMap[new_index - 1];
          new_index--;
        }
      }
      attrMap[new_index].old_index = k;
      attrMap[new_index].attr_id = current_id;
      attrMap[new_index].attr_ptr_i = attrPtr.i;

      // add key size for hash index (ordered case was handled before)
      if (impl_req->indexType == DictTabInfo::UniqueHashIndex) {
        jam();
        const Uint32 s1 = AttributeDescriptor::getSize(attrDesc);
        const Uint32 s2 = AttributeDescriptor::getArraySize(attrDesc);
        createIndexPtr.p->m_indexKeyLength += ((1 << s1) * s2 + 31) >> 5;
      }
    }
  }

  if (ERROR_INSERTED(6122)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    setError(error, 9122, __LINE__);
    return;
  }
}

bool
Dbdict::createIndex_subOps(Signal* signal, SchemaOpPtr op_ptr)
{
  D("createIndex_subOps" << V(op_ptr.i) << *op_ptr.p);

  CreateIndexRecPtr createIndexPtr;
  getOpRec(op_ptr, createIndexPtr);

  // op to create index table
  if (!createIndexPtr.p->m_sub_create_table) {
    jam();
    Callback c = {
      safe_cast(&Dbdict::createIndex_fromCreateTable),
      op_ptr.p->op_key
    };
    op_ptr.p->m_callback = c;
    createIndex_toCreateTable(signal, op_ptr);
    return true;
  }
  // op to alter index online
  if (!createIndexPtr.p->m_sub_alter_index) {
    jam();
    Callback c = {
      safe_cast(&Dbdict::createIndex_fromAlterIndex),
      op_ptr.p->op_key
    };
    op_ptr.p->m_callback = c;
    createIndex_toAlterIndex(signal, op_ptr);
    return true;
  }
  return false;
}

void
Dbdict::createIndex_toCreateTable(Signal* signal, SchemaOpPtr op_ptr)
{
  D("createIndex_toCreateTable");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  CreateIndexRecPtr createIndexPtr;
  getOpRec(op_ptr, createIndexPtr);

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, createIndexPtr.p->m_request.tableId);
  ndbrequire(tablePtr.i == tablePtr.p->tableId);

  // signal data writer
  Uint32* wbuffer = &c_indexPage.word[0];
  LinearWriter w(wbuffer, sizeof(c_indexPage) >> 2);
  w.first();

  //indexPtr.p->noOfPrimkey = indexPtr.p->noOfAttributes;
  // plus concatenated primary table key attribute
  //indexPtr.p->noOfAttributes += 1;
  //indexPtr.p->noOfNullAttr = 0;

  w.add(DictTabInfo::TableName, createIndexPtr.p->m_indexName);
  { bool flag = createIndexPtr.p->m_bits & TableRecord::TR_Logged;
    w.add(DictTabInfo::TableLoggedFlag, (Uint32)flag);
  }
  { bool flag = createIndexPtr.p->m_bits & TableRecord::TR_Temporary;
    w.add(DictTabInfo::TableTemporaryFlag, (Uint32)flag);
  }
  w.add(DictTabInfo::FragmentTypeVal, createIndexPtr.p->m_fragmentType);
  w.add(DictTabInfo::TableTypeVal, createIndexPtr.p->m_request.indexType);
  { Rope name(c_rope_pool, tablePtr.p->tableName);
    char tableName[MAX_TAB_NAME_SIZE];
    name.copy(tableName);
    w.add(DictTabInfo::PrimaryTable, tableName);
  }
  w.add(DictTabInfo::PrimaryTableId, tablePtr.p->tableId);
  w.add(DictTabInfo::NoOfAttributes, createIndexPtr.p->m_attrList.sz + 1);
  w.add(DictTabInfo::NoOfKeyAttr, createIndexPtr.p->m_attrList.sz);//XXX ordered??
  w.add(DictTabInfo::NoOfNullable, (Uint32)0);
  w.add(DictTabInfo::KeyLength, createIndexPtr.p->m_indexKeyLength);
  w.add(DictTabInfo::SingleUserMode, (Uint32)NDB_SUM_READ_WRITE);

  // write index key attributes

  //const AttributeList& attrList = createIndexPtr.p->m_attrList;
  const AttributeMap& attrMap = createIndexPtr.p->m_attrMap;
  Uint32 k;
  for (k = 0; k < createIndexPtr.p->m_attrList.sz; k++) {
    jam();
    // insert the attributes in the order decided before in attrMap
    // TODO: make sure "old_index" is stored with the table and
    // passed up to NdbDictionary
    AttributeRecordPtr attrPtr;
    c_attributeRecordPool.getPtr(attrPtr, attrMap[k].attr_ptr_i);

    { Rope attrName(c_rope_pool, attrPtr.p->attributeName);
      char attributeName[MAX_ATTR_NAME_SIZE];
      attrName.copy(attributeName);
      w.add(DictTabInfo::AttributeName, attributeName);
    }

    const Uint32 attrDesc = attrPtr.p->attributeDescriptor;
    const bool isNullable = AttributeDescriptor::getNullable(attrDesc);
    const Uint32 arrayType = AttributeDescriptor::getArrayType(attrDesc);
    const Uint32 attrType = AttributeDescriptor::getType(attrDesc);

    w.add(DictTabInfo::AttributeId, k);
    if (createIndexPtr.p->m_request.indexType == DictTabInfo::UniqueHashIndex) {
      jam();
      w.add(DictTabInfo::AttributeKeyFlag, (Uint32)true);
      w.add(DictTabInfo::AttributeNullableFlag, (Uint32)false);
    }
    if (createIndexPtr.p->m_request.indexType == DictTabInfo::OrderedIndex) {
      jam();
      w.add(DictTabInfo::AttributeKeyFlag, (Uint32)false);
      w.add(DictTabInfo::AttributeNullableFlag, (Uint32)isNullable);
    }
    w.add(DictTabInfo::AttributeArrayType, arrayType);
    w.add(DictTabInfo::AttributeExtType, attrType);
    w.add(DictTabInfo::AttributeExtPrecision, attrPtr.p->extPrecision);
    w.add(DictTabInfo::AttributeExtScale, attrPtr.p->extScale);
    w.add(DictTabInfo::AttributeExtLength, attrPtr.p->extLength);
    w.add(DictTabInfo::AttributeEnd, (Uint32)true);
  }

  if (createIndexPtr.p->m_request.indexType == DictTabInfo::UniqueHashIndex) {
    jam();
    Uint32 key_type = NDB_ARRAYTYPE_FIXED;
    AttributeRecordPtr attrPtr;
    LocalDLFifoList<AttributeRecord> list(c_attributeRecordPool,
                                          tablePtr.p->m_attributes);
    // XXX move to parse
    for (list.first(attrPtr); !attrPtr.isNull(); list.next(attrPtr))
    {
      const Uint32 desc = attrPtr.p->attributeDescriptor;
      if (AttributeDescriptor::getPrimaryKey(desc) &&
	  AttributeDescriptor::getArrayType(desc) != NDB_ARRAYTYPE_FIXED)
      {
	key_type = NDB_ARRAYTYPE_MEDIUM_VAR;
	break;
      }
    }

    // write concatenated primary table key attribute i.e. keyinfo
    w.add(DictTabInfo::AttributeName, "NDB$PK");
    w.add(DictTabInfo::AttributeId, createIndexPtr.p->m_attrList.sz);
    w.add(DictTabInfo::AttributeArrayType, key_type);
    w.add(DictTabInfo::AttributeKeyFlag, (Uint32)false);
    w.add(DictTabInfo::AttributeNullableFlag, (Uint32)false);
    w.add(DictTabInfo::AttributeExtType, (Uint32)DictTabInfo::ExtUnsigned);
    w.add(DictTabInfo::AttributeExtLength, tablePtr.p->tupKeyLength+1);
    w.add(DictTabInfo::AttributeEnd, (Uint32)true);
  }
  if (createIndexPtr.p->m_request.indexType == DictTabInfo::OrderedIndex) {
    jam();
    // write index tree node as Uint32 array attribute
    w.add(DictTabInfo::AttributeName, "NDB$TNODE");
    w.add(DictTabInfo::AttributeId, createIndexPtr.p->m_attrList.sz);
    // should not matter but VAR crashes in TUP
    w.add(DictTabInfo::AttributeArrayType, (Uint32)NDB_ARRAYTYPE_FIXED);
    w.add(DictTabInfo::AttributeKeyFlag, (Uint32)true);
    w.add(DictTabInfo::AttributeNullableFlag, (Uint32)false);
    w.add(DictTabInfo::AttributeExtType, (Uint32)DictTabInfo::ExtUnsigned);
    w.add(DictTabInfo::AttributeExtLength, createIndexPtr.p->m_indexKeyLength);
    w.add(DictTabInfo::AttributeEnd, (Uint32)true);
  }
  // finish
  w.add(DictTabInfo::TableEnd, (Uint32)true);

  CreateTableReq* req = (CreateTableReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;

  LinearSectionPtr lsPtr[3];
  lsPtr[0].p = wbuffer;
  lsPtr[0].sz = w.getWordsUsed();
  sendSignal(reference(), GSN_CREATE_TABLE_REQ, signal,
             CreateTableReq::SignalLength, JBB, lsPtr, 1);
}

void
Dbdict::createIndex_fromCreateTable(Signal* signal, Uint32 op_key, Uint32 ret)
{
  jam();
  D("createIndex_fromCreateTable" << dec << V(op_key) << V(ret));

  SchemaOpPtr op_ptr;
  CreateIndexRecPtr createIndexPtr;

  findSchemaOp(op_ptr, createIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  CreateIndxImplReq* impl_req = &createIndexPtr.p->m_request;

  if (ret == 0) {
    const CreateTableConf* conf =
      (const CreateTableConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    impl_req->indexId = conf->tableId;
    impl_req->indexVersion = conf->tableVersion;
    createIndexPtr.p->m_sub_create_table = true;
    createSubOps(signal, op_ptr);
  } else {
    jam();
    const CreateTableRef* ref =
      (const CreateTableRef*)signal->getDataPtr();
    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::createIndex_toAlterIndex(Signal* signal, SchemaOpPtr op_ptr)
{
  D("createIndex_toAlterIndex");

  AlterIndxReq* req = (AlterIndxReq*)signal->getDataPtrSend();

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  CreateIndexRecPtr createIndexPtr;
  getOpRec(op_ptr, createIndexPtr);

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, AlterIndxImplReq::AlterIndexOnline);
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->indexId = createIndexPtr.p->m_request.indexId;
  req->indexVersion = createIndexPtr.p->m_request.indexVersion;

  sendSignal(reference(), GSN_ALTER_INDX_REQ, signal,
             AlterIndxReq::SignalLength, JBB);
}

void
Dbdict::createIndex_fromAlterIndex(Signal* signal, Uint32 op_key, Uint32 ret)
{
  jam();
  D("createIndex_fromAlterIndex");

  SchemaOpPtr op_ptr;
  CreateIndexRecPtr createIndexPtr;

  findSchemaOp(op_ptr, createIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  //Uint32 errorCode = 0;
  if (ret == 0) {
    const AlterIndxConf* conf =
      (const AlterIndxConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    createIndexPtr.p->m_sub_alter_index = true;
    createSubOps(signal, op_ptr);
  } else {
    jam();
    const AlterIndxRef* ref =
      (const AlterIndxRef*)signal->getDataPtr();

    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::createIndex_reply(Signal* signal, SchemaOpPtr op_ptr, ErrorInfo error)
{
  jam();
  D("createIndex_reply");

  SchemaTransPtr& trans_ptr = op_ptr.p->m_trans_ptr;
  CreateIndexRecPtr createIndexPtr;
  getOpRec(op_ptr, createIndexPtr);
  const CreateIndxImplReq* impl_req = &createIndexPtr.p->m_request;

  if (!hasError(error)) {
    CreateIndxConf* conf = (CreateIndxConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->clientData = op_ptr.p->m_clientData;
    conf->transId = trans_ptr.p->m_transId;
    conf->indexId = impl_req->indexId;
    conf->indexVersion = impl_req->indexVersion;

    D(V(conf->indexId) << V(conf->indexVersion));

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_CREATE_INDX_CONF, signal,
               CreateIndxConf::SignalLength, JBB);
  } else {
    jam();
    CreateIndxRef* ref = (CreateIndxRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->clientData = op_ptr.p->m_clientData;
    ref->transId = trans_ptr.p->m_transId;
    getError(error, ref);

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_CREATE_INDX_REF, signal,
               CreateIndxRef::SignalLength, JBB);
  }
}

// CreateIndex: PREPARE

void
Dbdict::createIndex_prepare(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  D("createIndex_prepare");

  sendTransConf(signal, op_ptr);
}

// CreateIndex: COMMIT

void
Dbdict::createIndex_commit(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  D("createIndex_commit");

  sendTransConf(signal, op_ptr);
}

// CreateIndex: ABORT

void
Dbdict::createIndex_abortParse(Signal* signal, SchemaOpPtr op_ptr)
{
  D("createIndex_abortParse" << *op_ptr.p);

  // wl3600_todo probably nothing..

  sendTransConf(signal, op_ptr);
}

void
Dbdict::createIndex_abortPrepare(Signal* signal, SchemaOpPtr op_ptr)
{
  D("createIndex_abortPrepare" << *op_ptr.p);
  // wl3600_todo
  sendTransConf(signal, op_ptr);
}

// CreateIndex: MISC

void
Dbdict::execCREATE_INDX_IMPL_CONF(Signal* signal)
{
  jamEntry();
  const CreateIndxImplConf* conf = (const CreateIndxImplConf*)signal->getDataPtr();
  ndbrequire(refToNode(conf->senderRef) == getOwnNodeId());
  handleDictConf(signal, conf);
}

void
Dbdict::execCREATE_INDX_IMPL_REF(Signal* signal)
{
  jamEntry();
  const CreateIndxImplRef* ref = (const CreateIndxImplRef*)signal->getDataPtr();
  ndbrequire(refToNode(ref->senderRef) == getOwnNodeId());
  handleDictRef(signal, ref);
}

// CreateIndex: END

// MODULE: DropIndex

const Dbdict::OpInfo
Dbdict::DropIndexRec::g_opInfo = {
  { 'D', 'I', 'n', 0 },
  GSN_DROP_INDX_IMPL_REQ,
  DropIndxImplReq::SignalLength,
  //
  &Dbdict::dropIndex_seize,
  &Dbdict::dropIndex_release,
  //
  &Dbdict::dropIndex_parse,
  &Dbdict::dropIndex_subOps,
  &Dbdict::dropIndex_reply,
  //
  &Dbdict::dropIndex_prepare,
  &Dbdict::dropIndex_commit,
  //
  &Dbdict::dropIndex_abortParse,
  &Dbdict::dropIndex_abortPrepare
};

bool
Dbdict::dropIndex_seize(SchemaOpPtr op_ptr)
{
  return seizeOpRec<DropIndexRec>(op_ptr);
}

void
Dbdict::dropIndex_release(SchemaOpPtr op_ptr)
{
  releaseOpRec<DropIndexRec>(op_ptr);
}

void
Dbdict::execDROP_INDX_REQ(Signal* signal)
{
  jamEntry();
  if (!assembleFragments(signal)) {
    jam();
    return;
  }
  SectionHandle handle(this, signal);

  const DropIndxReq req_copy =
    *(const DropIndxReq*)signal->getDataPtr();
  const DropIndxReq* req = &req_copy;

  ErrorInfo error;
  do {
    SchemaOpPtr op_ptr;
    DropIndexRecPtr dropIndexPtr;
    DropIndxImplReq* impl_req;

    startClientReq(op_ptr, dropIndexPtr, req, impl_req, error);
    if (hasError(error)) {
      jam();
      break;
    }

    impl_req->tableId = RNIL;    // wl3600_todo fill in in parse
    impl_req->tableVersion = 0;
    impl_req->indexId = req->indexId;
    impl_req->indexVersion = req->indexVersion;

    handleClientReq(signal, op_ptr, handle);
    return;
  } while (0);

  releaseSections(handle);

  DropIndxRef* ref = (DropIndxRef*)signal->getDataPtrSend();
  ref->senderRef = reference();
  ref->clientData = req->clientData;
  ref->transId = req->transId;
  ref->indexId = req->indexId;
  ref->indexVersion = req->indexVersion;
  getError(error, ref);

  sendSignal(req->clientRef, GSN_DROP_INDX_REF, signal,
             DropIndxRef::SignalLength, JBB);
}

// DropIndex: PARSE

void
Dbdict::dropIndex_parse(Signal* signal, bool master,
                        SchemaOpPtr op_ptr,
                        SectionHandle& handle, ErrorInfo& error)
{
  D("dropIndex_parse" << V(op_ptr.i) << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  DropIndexRecPtr dropIndexPtr;
  getOpRec(op_ptr, dropIndexPtr);
  DropIndxImplReq* impl_req = &dropIndexPtr.p->m_request;

  if (checkSingleUserMode(trans_ptr.p->m_clientRef)) {
    jam();
    setError(error, DropIndxRef::SingleUser, __LINE__);
    return;
  }

  TableRecordPtr indexPtr;
  if (!(impl_req->indexId < c_tableRecordPool.getSize())) {
    jam();
    setError(error, DropIndxRef::IndexNotFound, __LINE__);
    return;
  }
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  // wl3600_todo make possible to drop broken
  if (indexPtr.p->tabState == TableRecord::NOT_DEFINED) {
    jam();
    setError(error, DropIndxRef::IndexNotFound, __LINE__);
    return;
  }

  if (!indexPtr.p->isIndex()) {
    jam();
    setError(error, DropIndxRef::NotAnIndex, __LINE__);
    return;
  }

  if (indexPtr.p->tableVersion != impl_req->indexVersion) {
    jam();
    setError(error, DropIndxRef::InvalidIndexVersion, __LINE__);
    return;
  }

  ndbrequire(indexPtr.p->primaryTableId != RNIL);
  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, indexPtr.p->primaryTableId);

  // master sets primary table, slave verifies it agrees
  if (master)
  {
    jam();
    impl_req->tableId = tablePtr.p->tableId;
    impl_req->tableVersion = tablePtr.p->tableVersion;
  }
  else
  {
    if (impl_req->tableId != tablePtr.p->tableId)
    {
      jam(); // wl3600_todo better error code
      setError(error, DropIndxRef::InvalidIndexVersion, __LINE__);
      return;
    }
    if (impl_req->tableVersion != tablePtr.p->tableVersion)
    {
      jam(); // wl3600_todo better error code
      setError(error, DropIndxRef::InvalidIndexVersion, __LINE__);
      return;
    }
  }

  if (ERROR_INSERTED(6122)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    setError(error, 9122, __LINE__);
    return;
  }
}

bool
Dbdict::dropIndex_subOps(Signal* signal, SchemaOpPtr op_ptr)
{
  D("dropIndex_subOps" << V(op_ptr.i) << *op_ptr.p);

  DropIndexRecPtr dropIndexPtr;
  getOpRec(op_ptr, dropIndexPtr);

  // op to alter index offline
  if (!dropIndexPtr.p->m_sub_alter_index) {
    jam();
    Callback c = {
      safe_cast(&Dbdict::dropIndex_fromAlterIndex),
      op_ptr.p->op_key
    };
    op_ptr.p->m_callback = c;
    dropIndex_toAlterIndex(signal, op_ptr);
    return true;
  }

  // op to drop index table
  if (!dropIndexPtr.p->m_sub_drop_table) {
    jam();
    Callback c = {
      safe_cast(&Dbdict::dropIndex_fromDropTable),
      op_ptr.p->op_key
    };
    op_ptr.p->m_callback = c;
    dropIndex_toDropTable(signal, op_ptr);
    return true;
  }

  return false;
}

void
Dbdict::dropIndex_reply(Signal* signal, SchemaOpPtr op_ptr, ErrorInfo error)
{
  jam();
  D("dropIndex_reply" << V(op_ptr.i) << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  DropIndexRecPtr dropIndexPtr;
  getOpRec(op_ptr, dropIndexPtr);
  const DropIndxImplReq* impl_req = &dropIndexPtr.p->m_request;

  if (!hasError(error)) {
    DropIndxConf* conf = (DropIndxConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->clientData = op_ptr.p->m_clientData;
    conf->transId = trans_ptr.p->m_transId;
    // wl3600_todo why send redundant fields?
    conf->indexId = impl_req->indexId;
    conf->indexVersion = impl_req->indexVersion;

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_DROP_INDX_CONF, signal,
               DropIndxConf::SignalLength, JBB);
  } else {
    jam();
    DropIndxRef* ref = (DropIndxRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->clientData = op_ptr.p->m_clientData;
    ref->transId = trans_ptr.p->m_transId;
    // wl3600_todo why send redundant fields?
    ref->indexId = impl_req->indexId;
    ref->indexVersion = impl_req->indexVersion;
    getError(error, ref);

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_DROP_INDX_REF, signal,
               DropIndxRef::SignalLength, JBB);
  }
}

// DropIndex: PREPARE

void
Dbdict::dropIndex_prepare(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  DropIndexRecPtr dropIndexPtr;
  getOpRec(op_ptr, dropIndexPtr);

  D("dropIndex_prepare" << *op_ptr.p);

  sendTransConf(signal, op_ptr);
}

// DropIndex: COMMIT

void
Dbdict::dropIndex_commit(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  DropIndexRecPtr dropIndexPtr;
  getOpRec(op_ptr, dropIndexPtr);

  D("dropIndex_commit" << *op_ptr.p);

  sendTransConf(signal, op_ptr);
}

// DropIndex: ABORT

void
Dbdict::dropIndex_abortParse(Signal* signal, SchemaOpPtr op_ptr)
{
  D("dropIndex_abortParse" << *op_ptr.p);
  // wl3600_todo
  sendTransConf(signal, op_ptr);
}

void
Dbdict::dropIndex_abortPrepare(Signal* signal, SchemaOpPtr op_ptr)
{
  D("dropIndex_abortPrepare" << *op_ptr.p);
  ndbrequire(false);
  sendTransConf(signal, op_ptr);
}

void
Dbdict::dropIndex_toAlterIndex(Signal* signal, SchemaOpPtr op_ptr)
{
  D("dropIndex_toAlterIndex");

  AlterIndxReq* req = (AlterIndxReq*)signal->getDataPtrSend();

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  DropIndexRecPtr dropIndexPtr;
  getOpRec(op_ptr, dropIndexPtr);

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, AlterIndxImplReq::AlterIndexOffline);
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->indexId = dropIndexPtr.p->m_request.indexId;
  req->indexVersion = dropIndexPtr.p->m_request.indexVersion;

  sendSignal(reference(), GSN_ALTER_INDX_REQ, signal,
             AlterIndxReq::SignalLength, JBB);
}

void
Dbdict::dropIndex_fromAlterIndex(Signal* signal, Uint32 op_key, Uint32 ret)
{
  jam();
  D("dropIndex_fromAlterIndex");

  SchemaOpPtr op_ptr;
  DropIndexRecPtr dropIndexPtr;

  findSchemaOp(op_ptr, dropIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  if (ret == 0) {
    const AlterIndxConf* conf =
      (const AlterIndxConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    dropIndexPtr.p->m_sub_alter_index = true;
    createSubOps(signal, op_ptr);
  } else {
    jam();
    const AlterIndxRef* ref =
      (const AlterIndxRef*)signal->getDataPtr();

    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::dropIndex_toDropTable(Signal* signal, SchemaOpPtr op_ptr)
{
  D("dropIndex_toDropTable");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  DropIndexRecPtr dropIndexPtr;
  getOpRec(op_ptr, dropIndexPtr);
  const DropIndxImplReq* impl_req = &dropIndexPtr.p->m_request;

  DropTableReq* req = (DropTableReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->tableId = impl_req->indexId;
  req->tableVersion = impl_req->indexVersion;

  sendSignal(reference(), GSN_DROP_TABLE_REQ, signal,
             DropTableReq::SignalLength, JBB);
}

void
Dbdict::dropIndex_fromDropTable(Signal* signal, Uint32 op_key, Uint32 ret)
{
  jam();
  D("dropIndex_fromDropTable" << dec << V(op_key) << V(ret));

  SchemaOpPtr op_ptr;
  DropIndexRecPtr dropIndexPtr;

  findSchemaOp(op_ptr, dropIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  if (ret == 0) {
    jam();
    const DropTableConf* conf =
      (const DropTableConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    dropIndexPtr.p->m_sub_drop_table = true;
    createSubOps(signal, op_ptr);
  } else {
    jam();
    const DropTableRef* ref =
      (const DropTableRef*)signal->getDataPtr();
    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

// DropIndex: MISC

void
Dbdict::execDROP_INDX_IMPL_CONF(Signal* signal)
{
  jamEntry();
  const DropIndxImplConf* conf = (const DropIndxImplConf*)signal->getDataPtr();
  ndbrequire(refToNode(conf->senderRef) == getOwnNodeId());
  handleDictConf(signal, conf);
}

void
Dbdict::execDROP_INDX_IMPL_REF(Signal* signal)
{
  jamEntry();
  const DropIndxImplRef* ref = (const DropIndxImplRef*)signal->getDataPtr();
  ndbrequire(refToNode(ref->senderRef) == getOwnNodeId());
  handleDictRef(signal, ref);
}

// DropIndex: END

// MODULE: AlterIndex

const Dbdict::OpInfo
Dbdict::AlterIndexRec::g_opInfo = {
  { 'A', 'I', 'n', 0 },
  GSN_ALTER_INDX_IMPL_REQ,
  AlterIndxImplReq::SignalLength,
  //
  &Dbdict::alterIndex_seize,
  &Dbdict::alterIndex_release,
  //
  &Dbdict::alterIndex_parse,
  &Dbdict::alterIndex_subOps,
  &Dbdict::alterIndex_reply,
  //
  &Dbdict::alterIndex_prepare,
  &Dbdict::alterIndex_commit,
  //
  &Dbdict::alterIndex_abortParse,
  &Dbdict::alterIndex_abortPrepare
};

bool
Dbdict::alterIndex_seize(SchemaOpPtr op_ptr)
{
  return seizeOpRec<AlterIndexRec>(op_ptr);
}

void
Dbdict::alterIndex_release(SchemaOpPtr op_ptr)
{
  releaseOpRec<AlterIndexRec>(op_ptr);
}

void
Dbdict::execALTER_INDX_REQ(Signal* signal)
{
  jamEntry();
  if (!assembleFragments(signal)) {
    jam();
    return;
  }
  SectionHandle handle(this, signal);

  const AlterIndxReq req_copy =
    *(const AlterIndxReq*)signal->getDataPtr();
  const AlterIndxReq* req = &req_copy;

  ErrorInfo error;
  do {
    SchemaOpPtr op_ptr;
    AlterIndexRecPtr alterIndexPtr;
    AlterIndxImplReq* impl_req;

    startClientReq(op_ptr, alterIndexPtr, req, impl_req, error);
    if (hasError(error)) {
      jam();
      break;
    }

    impl_req->tableId = RNIL;    // wl3600_todo fill in in parse
    impl_req->tableVersion = 0;
    impl_req->indexId = req->indexId;
    impl_req->indexVersion = req->indexVersion;
    impl_req->indexType = 0; // fill in parse

    handleClientReq(signal, op_ptr, handle);
    return;
  } while (0);

  releaseSections(handle);

  AlterIndxRef* ref = (AlterIndxRef*)signal->getDataPtrSend();
  ref->senderRef = reference();
  ref->clientData = req->clientData;
  ref->transId = req->transId;
  ref->indexId = req->indexId;
  ref->indexVersion = req->indexVersion;
  getError(error, ref);

  sendSignal(req->clientRef, GSN_ALTER_INDX_REF, signal,
             AlterIndxRef::SignalLength, JBB);
}

const Dbdict::TriggerTmpl
Dbdict::g_hashIndexTriggerTmpl[1] = {
  { "NDB$INDEX_%u_UI",
    {
      TriggerType::SECONDARY_INDEX,
      TriggerActionTime::TA_AFTER,
      TriggerEvent::TE_CUSTOM,
      false, false, true // monitor replicas, monitor all, report all
    }
  }
};

const Dbdict::TriggerTmpl
Dbdict::g_orderedIndexTriggerTmpl[1] = {
  { "NDB$INDEX_%u_CUSTOM",
    {
      TriggerType::ORDERED_INDEX,
      TriggerActionTime::TA_CUSTOM,
      TriggerEvent::TE_CUSTOM,
      true, false, true
    }
  }
};

const Dbdict::TriggerTmpl
Dbdict::g_buildIndexConstraintTmpl[1] = {
  { "NDB$INDEX_%u_BUILD",
    {
      TriggerType::READ_ONLY_CONSTRAINT,
      TriggerActionTime::TA_AFTER,
      TriggerEvent::TE_UPDATE,
      false, true, false
    }
  }
};

// AlterIndex: PARSE

void
Dbdict::alterIndex_parse(Signal* signal, bool master,
                         SchemaOpPtr op_ptr,
                         SectionHandle& handle, ErrorInfo& error)
{
  D("alterIndex_parse" << V(op_ptr.i) << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;

  TableRecordPtr indexPtr;
  if (!(impl_req->indexId < c_tableRecordPool.getSize())) {
    jam();
    setError(error, AlterIndxRef::IndexNotFound, __LINE__);
    return;
  }
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  // wl3600_todo check either DEFINED or visible to transaction
  if (indexPtr.p->tabState == TableRecord::NOT_DEFINED) {
    jam();
    setError(error, AlterIndxRef::IndexNotFound, __LINE__);
    return;
  }

  // check it is an index and set trigger info for sub-ops
  switch (indexPtr.p->tableType) {
  case DictTabInfo::UniqueHashIndex:
    jam();
    alterIndexPtr.p->m_triggerTmpl = g_hashIndexTriggerTmpl;
    break;
  case DictTabInfo::OrderedIndex:
    jam();
    alterIndexPtr.p->m_triggerTmpl = g_orderedIndexTriggerTmpl;
    break;
  default:
    jam();
    setError(error, AlterIndxRef::NotAnIndex, __LINE__);
    return;
  }

  if (master)
  {
    jam();
    impl_req->indexType = indexPtr.p->tableType;
  }
  else
  {
    jam();
    ndbrequire(impl_req->indexType == (Uint32)indexPtr.p->tableType);
  }

  if (indexPtr.p->tableVersion != impl_req->indexVersion) {
    jam();
    setError(error, AlterIndxRef::InvalidIndexVersion, __LINE__);
    return;
  }

  ndbrequire(indexPtr.p->primaryTableId != RNIL);
  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, indexPtr.p->primaryTableId);

  // master sets primary table, participant verifies it agrees
  if (master)
  {
    jam();
    impl_req->tableId = tablePtr.p->tableId;
    impl_req->tableVersion = tablePtr.p->tableVersion;
  }
  else
  {
    if (impl_req->tableId != tablePtr.p->tableId)
    {
      jam(); // wl3600_todo better error code
      setError(error, AlterIndxRef::InvalidIndexVersion, __LINE__);
      return;
    }
    if (impl_req->tableVersion != tablePtr.p->tableVersion)
    {
      jam(); // wl3600_todo better error code
      setError(error, AlterIndxRef::InvalidIndexVersion, __LINE__);
      return;
    }
  }

  // check request type
  Uint32 requestType = impl_req->requestType;
  switch (requestType) {
  case AlterIndxImplReq::AlterIndexOnline:
    jam();
    indexPtr.p->indexState = TableRecord::IS_BUILDING;
    break;
  case AlterIndxImplReq::AlterIndexOffline:
    jam();
    indexPtr.p->indexState = TableRecord::IS_DROPPING;
    break;
  default:
    ndbassert(false);
    setError(error, AlterIndxRef::BadRequestType, __LINE__);
    return;
  }

  // set attribute mask (of primary table attribute ids)
  getIndexAttrMask(indexPtr, alterIndexPtr.p->m_attrMask);

  if (ERROR_INSERTED(6123)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    setError(error, 9123, __LINE__);
    return;
  }
}

bool
Dbdict::alterIndex_subOps(Signal* signal, SchemaOpPtr op_ptr)
{
  D("alterIndex_subOps" << V(op_ptr.i) << *op_ptr.p);

  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;
  Uint32 requestType = impl_req->requestType;

  // ops to create or drop triggers
  if (alterIndexPtr.p->m_sub_trigger == false)
  {
    jam();
    alterIndexPtr.p->m_sub_trigger = true;
    switch (requestType) {
    case AlterIndxImplReq::AlterIndexOnline:
      jam();
      {
        Callback c = {
          safe_cast(&Dbdict::alterIndex_fromCreateTrigger),
          op_ptr.p->op_key
        };
        op_ptr.p->m_callback = c;
        alterIndex_toCreateTrigger(signal, op_ptr);
      }
      break;
    case AlterIndxImplReq::AlterIndexOffline:
      jam();
      {
        Callback c = {
          safe_cast(&Dbdict::alterIndex_fromDropTrigger),
          op_ptr.p->op_key
        };
        op_ptr.p->m_callback = c;
        alterIndex_toDropTrigger(signal, op_ptr);
      }
      break;
    default:
      ndbrequire(false);
      break;
    }
    return true;
  }

  if (requestType == AlterIndxImplReq::AlterIndexOnline &&
      !alterIndexPtr.p->m_sub_build_index) {
    jam();
    Callback c = {
      safe_cast(&Dbdict::alterIndex_fromBuildIndex),
      op_ptr.p->op_key
    };
    op_ptr.p->m_callback = c;
    alterIndex_toBuildIndex(signal, op_ptr);
    return true;
  }

  return false;
}

void
Dbdict::alterIndex_toCreateTrigger(Signal* signal, SchemaOpPtr op_ptr)
{
  D("alterIndex_toCreateTrigger");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;
  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  const TriggerTmpl& triggerTmpl = alterIndexPtr.p->m_triggerTmpl[0];

  CreateTrigReq* req = (CreateTrigReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, CreateTrigReq::CreateTriggerOnline);
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->tableId = impl_req->tableId;
  req->tableVersion = impl_req->tableVersion;
  req->indexId = impl_req->indexId;
  req->indexVersion = impl_req->indexVersion;
  req->triggerNo = 0;

  Uint32 forceTriggerId = indexPtr.p->triggerId;
  D(V(getNodeState().startLevel) << V(NodeState::SL_STARTED));
  if (getNodeState().startLevel == NodeState::SL_STARTED) {
    ndbrequire(forceTriggerId == RNIL);
  }
  req->forceTriggerId = forceTriggerId;

  TriggerInfo::packTriggerInfo(req->triggerInfo, triggerTmpl.triggerInfo);

  req->receiverRef = 0;
  req->attributeMask = alterIndexPtr.p->m_attrMask;

  char triggerName[MAX_TAB_NAME_SIZE];
  sprintf(triggerName, triggerTmpl.nameFormat, impl_req->indexId);

  // name section
  Uint32 buffer[2 + ((MAX_TAB_NAME_SIZE + 3) >> 2)];    // SP string
  LinearWriter w(buffer, sizeof(buffer) >> 2);
  w.reset();
  w.add(DictTabInfo::TableName, triggerName);
  LinearSectionPtr lsPtr[3];
  lsPtr[0].p = buffer;
  lsPtr[0].sz = w.getWordsUsed();

  sendSignal(reference(), GSN_CREATE_TRIG_REQ, signal,
             CreateTrigReq::SignalLength, JBB, lsPtr, 1);
}

void
Dbdict::alterIndex_atCreateTrigger(Signal* signal, SchemaOpPtr op_ptr)
{
  /*
   * Index trigger req has been parsed on this participant.  Connect the
   * index and the trigger on this node.  Trigger number comes in the
   * signal because we have no access to trigger counter in coordinator.
   * There is a counter under index record but it is better to use it
   * for verification.
   */
  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);
  const CreateTrigImplReq* impl_req = &createTriggerPtr.p->m_request;

  TriggerRecordPtr triggerPtr;
  c_triggerRecordPool.getPtr(triggerPtr, impl_req->triggerId);

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  D("alterIndex_atCreateTrigger" << *op_ptr.p);

  triggerPtr.p->indexId = impl_req->indexId;
  indexPtr.p->triggerId = impl_req->triggerId;
}

void
Dbdict::alterIndex_fromCreateTrigger(Signal* signal, Uint32 op_key, Uint32 ret)
{
  jam();

  SchemaOpPtr op_ptr;
  AlterIndexRecPtr alterIndexPtr;

  findSchemaOp(op_ptr, alterIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  //const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;

  D("alterIndex_fromCreateTrigger" << dec << V(op_key) << V(ret));

  if (ret == 0) {
    jam();
    const CreateTrigConf* conf =
      (const CreateTrigConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    createSubOps(signal, op_ptr);
  } else {
    const CreateTrigRef* ref =
      (const CreateTrigRef*)signal->getDataPtr();
    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::alterIndex_toDropTrigger(Signal* signal, SchemaOpPtr op_ptr)
{
  D("alterIndex_toDropTrigger");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  //const TriggerTmpl& triggerTmpl = alterIndexPtr.p->m_triggerTmpl[0];

  DropTrigReq* req = (DropTrigReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, 0);
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->tableId = impl_req->tableId;
  req->tableVersion = impl_req->tableVersion;
  req->indexId = impl_req->indexId;
  req->indexVersion = impl_req->indexVersion;
  req->triggerNo = 0;
  req->triggerId = indexPtr.p->triggerId;

  sendSignal(reference(), GSN_DROP_TRIG_REQ, signal,
             DropTrigReq::SignalLength, JBB);
}

void
Dbdict::alterIndex_atDropTrigger(Signal* signal, SchemaOpPtr op_ptr)
{
  DropTriggerRecPtr dropTriggerPtr;
  getOpRec(op_ptr, dropTriggerPtr);
  const DropTrigImplReq* impl_req = &dropTriggerPtr.p->m_request;

  // the trigger is already deleted

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  D("alterIndex_atDropTrigger" << *op_ptr.p);

  indexPtr.p->triggerId = RNIL;
}

void
Dbdict::alterIndex_fromDropTrigger(Signal* signal, Uint32 op_key, Uint32 ret)
{
  jam();

  SchemaOpPtr op_ptr;
  AlterIndexRecPtr alterIndexPtr;

  findSchemaOp(op_ptr, alterIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  //const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;

  D("alterIndex_fromDropTrigger" << dec << V(op_key) << V(ret));

  if (ret == 0) {
    const DropTrigConf* conf =
      (const DropTrigConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    createSubOps(signal, op_ptr);
  } else {
    jam();
    const DropTrigRef* ref =
      (const DropTrigRef*)signal->getDataPtr();
    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::alterIndex_toBuildIndex(Signal* signal, SchemaOpPtr op_ptr)
{
  D("alterIndex_toBuildIndex");

  BuildIndxReq* req = (BuildIndxReq*)signal->getDataPtrSend();

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, BuildIndxReq::MainOp);
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->buildId = 0; // not used
  req->buildKey = 0; // not used
  req->tableId = impl_req->tableId;
  req->indexId = impl_req->indexId;
  req->indexType = impl_req->indexType;
  req->parallelism = 16;

  sendSignal(reference(), GSN_BUILDINDXREQ, signal,
             BuildIndxReq::SignalLength, JBB);
}

void
Dbdict::alterIndex_fromBuildIndex(Signal* signal, Uint32 op_key, Uint32 ret)
{
  jam();
  D("alterIndex_fromBuildIndex");

  SchemaOpPtr op_ptr;
  AlterIndexRecPtr alterIndexPtr;

  findSchemaOp(op_ptr, alterIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  if (ret == 0) {
    jam();
    const BuildIndxConf* conf =
      (const BuildIndxConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    alterIndexPtr.p->m_sub_build_index = true;
    createSubOps(signal, op_ptr);
  } else {
    jam();
    const BuildIndxRef* ref =
      (const BuildIndxRef*)signal->getDataPtr();

    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::alterIndex_reply(Signal* signal, SchemaOpPtr op_ptr, ErrorInfo error)
{
  D("alterIndex_reply" << V(op_ptr.i) << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;

  if (!hasError(error)) {
    AlterIndxConf* conf = (AlterIndxConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->clientData = op_ptr.p->m_clientData;
    conf->transId = trans_ptr.p->m_transId;
    conf->indexId = impl_req->indexId;
    conf->indexVersion = impl_req->indexVersion;

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_ALTER_INDX_CONF,
               signal, AlterIndxConf::SignalLength, JBB);
  } else {
    jam();
    AlterIndxRef* ref = (AlterIndxRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->clientData = op_ptr.p->m_clientData;
    ref->transId = trans_ptr.p->m_transId;
    // wl3600_todo requestType (fix all at once)
    // wl3600_todo next 2 redundant (fix all at once)
    ref->indexId = impl_req->indexId;
    ref->indexVersion = impl_req->indexVersion;
    getError(error, ref);

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_ALTER_INDX_REF,
               signal, AlterIndxRef::SignalLength, JBB);
  }
}

// AlterIndex: PREPARE

void
Dbdict::alterIndex_prepare(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;
  Uint32 requestType = impl_req->requestType;

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  D("alterIndex_prepare" << *op_ptr.p);

  /*
   * Create table creates index table in all blocks.  DBTC has
   * an additional entry for hash index.  It is created (offline)
   * and dropped here.  wl3600_todo redesign
   */

  switch (indexPtr.p->tableType) {
  case DictTabInfo::UniqueHashIndex:
    {
      Callback c = {
        safe_cast(&Dbdict::alterIndex_fromLocal),
        op_ptr.p->op_key
      };
      op_ptr.p->m_callback = c;

      switch (requestType) {
      case AlterIndxImplReq::AlterIndexOnline:
        alterIndex_toCreateLocal(signal, op_ptr);
        break;
      case AlterIndxImplReq::AlterIndexOffline:
        alterIndex_toDropLocal(signal, op_ptr);
        break;
      default:
        ndbrequire(false);
        break;
      }
    }
    break;
  case DictTabInfo::OrderedIndex:
    sendTransConf(signal, op_ptr);
    break;
  default:
    ndbrequire(false);
    break;
  }
}

void
Dbdict::alterIndex_toCreateLocal(Signal* signal, SchemaOpPtr op_ptr)
{
  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  D("alterIndex_toCreateLocal" << *op_ptr.p);

  CreateIndxImplReq* req = (CreateIndxImplReq*)signal->getDataPtrSend();

  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->requestType = 0;
  req->tableId = impl_req->tableId;
  req->tableVersion = 0; // not used
  req->indexType = indexPtr.p->tableType;
  req->indexId = impl_req->indexId;
  req->indexVersion = 0; // not used

  // attribute list
  getIndexAttrList(indexPtr, alterIndexPtr.p->m_attrList);
  LinearSectionPtr ls_ptr[3];
  ls_ptr[0].p = (Uint32*)&alterIndexPtr.p->m_attrList;
  ls_ptr[0].sz = 1 + alterIndexPtr.p->m_attrList.sz;

  BlockReference ref = DBTC_REF;
  sendSignal(ref, GSN_CREATE_INDX_IMPL_REQ, signal,
             CreateIndxImplReq::SignalLength, JBB, ls_ptr, 1);
}

void
Dbdict::alterIndex_toDropLocal(Signal* signal, SchemaOpPtr op_ptr)
{
  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  D("alterIndex_toDropLocal" << *op_ptr.p);

  DropIndxImplReq* req = (DropIndxImplReq*)signal->getDataPtrSend();

  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->tableId = impl_req->tableId;
  req->tableVersion = 0; // not used
  req->indexId = impl_req->indexId;
  req->indexVersion = 0; // not used

  BlockReference ref = DBTC_REF;
  sendSignal(ref, GSN_DROP_INDX_IMPL_REQ, signal,
             DropIndxImplReq::SignalLength, JBB);
}

void
Dbdict::alterIndex_fromLocal(Signal* signal, Uint32 op_key, Uint32 ret)
{
  SchemaOpPtr op_ptr;
  AlterIndexRecPtr alterIndexPtr;
  findSchemaOp(op_ptr, alterIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  D("alterIndex_fromLocal" << *op_ptr.p << V(ret));

  if (ret == 0) {
    jam();
    alterIndexPtr.p->m_tc_index_done = true;
    sendTransConf(signal, op_ptr);
  } else {
    jam();
    setError(op_ptr, ret, __LINE__);
    sendTransRef(signal, op_ptr);
  }
}

// AlterIndex: COMMIT

void
Dbdict::alterIndex_commit(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);

  D("alterIndex_commit" << *op_ptr.p);

  sendTransConf(signal, op_ptr);
}

// AlterIndex: ABORT

void
Dbdict::alterIndex_abortParse(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;
  Uint32 requestType = impl_req->requestType;
  Uint32 indexId = impl_req->indexId;

  D("alterIndex_abortParse" << *op_ptr.p);

  do {
    if (!(impl_req->indexId < c_tableRecordPool.getSize())) {
      jam();
      D("invalid index id" << V(indexId));
      break;
    }

    TableRecordPtr indexPtr;
    c_tableRecordPool.getPtr(indexPtr, indexId);

    switch (requestType) {
    case AlterIndxImplReq::AlterIndexOnline:
      jam();
      indexPtr.p->indexState = TableRecord::IS_OFFLINE;
      break;
    case AlterIndxImplReq::AlterIndexOffline:
      jam();
      indexPtr.p->indexState = TableRecord::IS_ONLINE;
      break;
    default:
      break;
    }
  } while (0);

  sendTransConf(signal, op_ptr);
}

void
Dbdict::alterIndex_abortPrepare(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  AlterIndexRecPtr alterIndexPtr;
  getOpRec(op_ptr, alterIndexPtr);
  const AlterIndxImplReq* impl_req = &alterIndexPtr.p->m_request;
  Uint32 requestType = impl_req->requestType;

  D("alterIndex_abortPrepare" << *op_ptr.p);

  if (!alterIndexPtr.p->m_tc_index_done) {
    jam();
    sendTransConf(signal, op_ptr);
    return;
  }

  Callback c = {
    safe_cast(&Dbdict::alterIndex_abortFromLocal),
    op_ptr.p->op_key
  };
  op_ptr.p->m_callback = c;

  switch (requestType) {
  case AlterIndxImplReq::AlterIndexOnline:
    jam();
    alterIndex_toDropLocal(signal, op_ptr);
    break;
  case AlterIndxImplReq::AlterIndexOffline:
    jam();
    alterIndex_toCreateLocal(signal, op_ptr);
    break;
  default:
    ndbrequire(false);
    break;
  }
}

void
Dbdict::alterIndex_abortFromLocal(Signal* signal,
                                  Uint32 op_key,
                                  Uint32 ret)
{
  SchemaOpPtr op_ptr;
  AlterIndexRecPtr alterIndexPtr;
  findSchemaOp(op_ptr, alterIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  D("alterIndex_abortFromLocal" << V(ret) << *op_ptr.p);

  if (ret == 0) {
    jam();
    alterIndexPtr.p->m_tc_index_done = false;
    sendTransConf(signal, op_ptr);
  } else {
    // abort is not allowed to fail
    ndbrequire(false);
  }
}

// AlterIndex: MISC

void
Dbdict::execALTER_INDX_CONF(Signal* signal)
{
  jamEntry();
  const AlterIndxConf* conf = (const AlterIndxConf*)signal->getDataPtr();
  handleDictConf(signal, conf);
}

void
Dbdict::execALTER_INDX_REF(Signal* signal)
{
  jamEntry();
  const AlterIndxRef* ref = (const AlterIndxRef*)signal->getDataPtr();
  handleDictRef(signal, ref);
}

void
Dbdict::execALTER_INDX_IMPL_CONF(Signal* signal)
{
  jamEntry();
  const AlterIndxImplConf* conf = (const AlterIndxImplConf*)signal->getDataPtr();
  handleDictConf(signal, conf);
}

void
Dbdict::execALTER_INDX_IMPL_REF(Signal* signal)
{
  jamEntry();
  const AlterIndxImplRef* ref = (const AlterIndxImplRef*)signal->getDataPtr();
  handleDictRef(signal, ref);
}

// AlterIndex: END

// MODULE: BuildIndex

const Dbdict::OpInfo
Dbdict::BuildIndexRec::g_opInfo = {
  { 'B', 'I', 'n', 0 },
  GSN_BUILD_INDX_IMPL_REQ,
  BuildIndxImplReq::SignalLength,
  //
  &Dbdict::buildIndex_seize,
  &Dbdict::buildIndex_release,
  //
  &Dbdict::buildIndex_parse,
  &Dbdict::buildIndex_subOps,
  &Dbdict::buildIndex_reply,
  //
  &Dbdict::buildIndex_prepare,
  &Dbdict::buildIndex_commit,
  //
  &Dbdict::buildIndex_abortParse,
  &Dbdict::buildIndex_abortPrepare
};

bool
Dbdict::buildIndex_seize(SchemaOpPtr op_ptr)
{
  return seizeOpRec<BuildIndexRec>(op_ptr);
}

void
Dbdict::buildIndex_release(SchemaOpPtr op_ptr)
{
  releaseOpRec<BuildIndexRec>(op_ptr);
}

void
Dbdict::execBUILDINDXREQ(Signal* signal)
{
  jamEntry();
  if (!assembleFragments(signal)) {
    jam();
    return;
  }
  SectionHandle handle(this, signal);

  const BuildIndxReq req_copy =
    *(const BuildIndxReq*)signal->getDataPtr();
  const BuildIndxReq* req = &req_copy;

  ErrorInfo error;
  do {
    SchemaOpPtr op_ptr;
    BuildIndexRecPtr buildIndexPtr;
    BuildIndxImplReq* impl_req;

    startClientReq(op_ptr, buildIndexPtr, req, impl_req, error);
    if (hasError(error)) {
      jam();
      break;
    }

    impl_req->buildId = req->buildId; //wl3600_todo remove from client sig
    impl_req->buildKey = req->buildKey;
    impl_req->tableId = req->tableId;
    impl_req->indexId = req->indexId;
    impl_req->indexType = req->indexType;  //wl3600_todo remove from client sig
    impl_req->parallelism = req->parallelism;

    handleClientReq(signal, op_ptr, handle);
    return;
  } while (0);

  releaseSections(handle);

  BuildIndxRef* ref = (BuildIndxRef*)signal->getDataPtrSend();
  ref->senderRef = reference();
  ref->clientData = req->clientData;
  ref->transId = req->transId;
  ref->tableId = req->tableId;
  ref->indexId = req->indexId;
  ref->indexType = req->indexType;
  getError(error, ref);

  sendSignal(req->clientRef, GSN_BUILDINDXREF, signal,
             BuildIndxRef::SignalLength, JBB);
}

// BuildIndex: PARSE

void
Dbdict::buildIndex_parse(Signal* signal, bool master,
                         SchemaOpPtr op_ptr,
                         SectionHandle& handle, ErrorInfo& error)
{
  D("buildIndex_parse");

  BuildIndexRecPtr buildIndexPtr;
  getOpRec(op_ptr, buildIndexPtr);
  BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  // get index
  TableRecordPtr indexPtr;
  if (!(impl_req->indexId < c_tableRecordPool.getSize())) {
    jam();
    setError(error, BuildIndxRef::IndexNotFound, __LINE__);
    return;
  }
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  ndbrequire(indexPtr.p->primaryTableId == impl_req->tableId);

  // get primary table
  TableRecordPtr tablePtr;
  if (!(impl_req->tableId < c_tableRecordPool.getSize())) {
    jam();
    setError(error, BuildIndxRef::IndexNotFound, __LINE__);
    return;
  }
  c_tableRecordPool.getPtr(tablePtr, impl_req->tableId);

  // set attribute lists
  getIndexAttrList(indexPtr, buildIndexPtr.p->m_indexKeyList);
  getTableKeyList(tablePtr, buildIndexPtr.p->m_tableKeyList);

  Uint32 requestType = impl_req->requestType;
  switch (requestType) {
  case BuildIndxReq::MainOp:
  case BuildIndxReq::SubOp:
    break;
  default:
    jam();
    ndbassert(false);
    setError(error, BuildIndxRef::BadRequestType, __LINE__);
    return;
  }

  // set build constraint info and attribute mask
  switch (indexPtr.p->tableType) {
  case DictTabInfo::UniqueHashIndex:
    jam();
    if (requestType == BuildIndxReq::MainOp) {
      buildIndexPtr.p->m_triggerTmpl = g_buildIndexConstraintTmpl;
      buildIndexPtr.p->m_subOpCount = 3;
      buildIndexPtr.p->m_subOpIndex = 0;

      // mask is NDB$PK (last attribute)
      Uint32 attrId = indexPtr.p->noOfAttributes - 1;
      buildIndexPtr.p->m_attrMask.clear();
      buildIndexPtr.p->m_attrMask.set(attrId);
      break;
    }
    /*FALLTHRU*/
  default:
    jam();
    {
      buildIndexPtr.p->m_triggerTmpl = 0;
      buildIndexPtr.p->m_subOpCount = 0;
      buildIndexPtr.p->m_doBuild = true;
    }
    break;
  }

  if (ERROR_INSERTED(6126)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    setError(error, 9126, __LINE__);
    return;
  }
}

bool
Dbdict::buildIndex_subOps(Signal* signal, SchemaOpPtr op_ptr)
{
  D("buildIndex_subOps" << V(op_ptr.i) << *op_ptr.p);

  BuildIndexRecPtr buildIndexPtr;
  getOpRec(op_ptr, buildIndexPtr);
  //const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  if (buildIndexPtr.p->m_subOpIndex < buildIndexPtr.p->m_subOpCount) {
    jam();
    switch (buildIndexPtr.p->m_subOpIndex) {
    case 0:
      jam();
      {
        Callback c = {
          safe_cast(&Dbdict::buildIndex_fromCreateConstraint),
          op_ptr.p->op_key
        };
        op_ptr.p->m_callback = c;
        buildIndex_toCreateConstraint(signal, op_ptr);
        return true;
      }
      break;
    case 1:
      jam();
      // the sub-op that does the actual hash index build
      {
        Callback c = {
          safe_cast(&Dbdict::buildIndex_fromBuildIndex),
          op_ptr.p->op_key
        };
        op_ptr.p->m_callback = c;
        buildIndex_toBuildIndex(signal, op_ptr);
        return true;
      }
      break;
    case 2:
      jam();
      {
        Callback c = {
          safe_cast(&Dbdict::buildIndex_fromDropConstraint),
          op_ptr.p->op_key
        };
        op_ptr.p->m_callback = c;
        buildIndex_toDropConstraint(signal, op_ptr);
        return true;
      }
      break;
    default:
      ndbrequire(false);
      break;
    }
  }

  return false;
}

void
Dbdict::buildIndex_toCreateConstraint(Signal* signal, SchemaOpPtr op_ptr)
{
  D("buildIndex_toCreateConstraint");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  BuildIndexRecPtr buildIndexPtr;
  getOpRec(op_ptr, buildIndexPtr);
  const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  const TriggerTmpl& triggerTmpl = buildIndexPtr.p->m_triggerTmpl[0];

  CreateTrigReq* req = (CreateTrigReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, CreateTrigReq::CreateTriggerOnline);
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->tableId = impl_req->indexId; // constraint is on index table
  req->tableVersion = 0;
  req->indexId = RNIL;
  req->indexVersion = 0;
  req->triggerNo = 0;
  req->forceTriggerId = RNIL;

  TriggerInfo::packTriggerInfo(req->triggerInfo, triggerTmpl.triggerInfo);

  req->receiverRef = 0;
  req->attributeMask = buildIndexPtr.p->m_attrMask;

  char triggerName[MAX_TAB_NAME_SIZE];
  sprintf(triggerName, triggerTmpl.nameFormat, impl_req->indexId);

  // name section
  Uint32 buffer[2 + ((MAX_TAB_NAME_SIZE + 3) >> 2)];    // SP string
  LinearWriter w(buffer, sizeof(buffer) >> 2);
  w.reset();
  w.add(DictTabInfo::TableName, triggerName);
  LinearSectionPtr ls_ptr[3];
  ls_ptr[0].p = buffer;
  ls_ptr[0].sz = w.getWordsUsed();

  sendSignal(reference(), GSN_CREATE_TRIG_REQ, signal,
             CreateTrigReq::SignalLength, JBB, ls_ptr, 1);
}

void
Dbdict::buildIndex_atCreateConstraint(Signal* signal, SchemaOpPtr op_ptr)
{
  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);
  const CreateTrigImplReq* impl_req = &createTriggerPtr.p->m_request;

  TriggerRecordPtr triggerPtr;
  c_triggerRecordPool.getPtr(triggerPtr, impl_req->triggerId);

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->tableId);

  D("buildIndex_atCreateConstraint" << *op_ptr.p);

  ndbrequire(indexPtr.p->buildTriggerId == RNIL);
  indexPtr.p->buildTriggerId = impl_req->triggerId;
}

void
Dbdict::buildIndex_fromCreateConstraint(Signal* signal, Uint32 op_key, Uint32 ret)
{
  jam();

  SchemaOpPtr op_ptr;
  BuildIndexRecPtr buildIndexPtr;

  findSchemaOp(op_ptr, buildIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  //const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  D("buildIndex_fromCreateConstraint" << dec << V(op_key) << V(ret));

  if (ret == 0) {
    jam();
    const CreateTrigConf* conf =
      (const CreateTrigConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    ndbrequire(buildIndexPtr.p->m_subOpIndex < buildIndexPtr.p->m_subOpCount);
    buildIndexPtr.p->m_subOpIndex += 1;
    createSubOps(signal, op_ptr);
  } else {
    const CreateTrigRef* ref =
      (const CreateTrigRef*)signal->getDataPtr();

    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::buildIndex_toBuildIndex(Signal* signal, SchemaOpPtr op_ptr)
{
  D("buildIndex_toBuildIndex");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  BuildIndexRecPtr buildIndexPtr;
  getOpRec(op_ptr, buildIndexPtr);
  const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  BuildIndxReq* req = (BuildIndxReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, BuildIndxReq::SubOp);
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->buildId = 0;
  req->buildKey = 0;
  req->tableId = impl_req->tableId;
  req->indexId = impl_req->indexId;
  req->indexType = impl_req->indexType;
  req->parallelism = impl_req->parallelism;

  sendSignal(reference(), GSN_BUILDINDXREQ, signal,
            BuildIndxReq::SignalLength, JBB);
}

void
Dbdict::buildIndex_fromBuildIndex(Signal* signal, Uint32 op_key, Uint32 ret)
{
  jam();
  D("buildIndex_fromBuildIndex");

  SchemaOpPtr op_ptr;
  BuildIndexRecPtr buildIndexPtr;

  findSchemaOp(op_ptr, buildIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  if (ret == 0) {
    jam();
    const BuildIndxConf* conf =
      (const BuildIndxConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    ndbrequire(buildIndexPtr.p->m_subOpIndex < buildIndexPtr.p->m_subOpCount);
    buildIndexPtr.p->m_subOpIndex += 1;
    createSubOps(signal, op_ptr);
  } else {
    jam();
    const BuildIndxRef* ref =
      (const BuildIndxRef*)signal->getDataPtr();

    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::buildIndex_toDropConstraint(Signal* signal, SchemaOpPtr op_ptr)
{
  D("buildIndex_toDropConstraint");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  BuildIndexRecPtr buildIndexPtr;
  getOpRec(op_ptr, buildIndexPtr);
  const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  const TriggerTmpl& triggerTmpl = buildIndexPtr.p->m_triggerTmpl[0];

  DropTrigReq* req = (DropTrigReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, 0);
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->tableId = impl_req->indexId; // constraint is on index table
  req->tableVersion = 0;
  req->indexId = RNIL;
  req->indexVersion = 0;
  req->triggerNo = 0;
  req->triggerId = RNIL;

  // wl3600_todo use name now, connect by tree walk later

  char triggerName[MAX_TAB_NAME_SIZE];
  sprintf(triggerName, triggerTmpl.nameFormat, impl_req->indexId);

  // name section
  Uint32 buffer[2 + ((MAX_TAB_NAME_SIZE + 3) >> 2)];    // SP string
  LinearWriter w(buffer, sizeof(buffer) >> 2);
  w.reset();
  w.add(DictTabInfo::TableName, triggerName);
  LinearSectionPtr ls_ptr[3];
  ls_ptr[0].p = buffer;
  ls_ptr[0].sz = w.getWordsUsed();

  sendSignal(reference(), GSN_DROP_TRIG_REQ, signal,
             DropTrigReq::SignalLength, JBB, ls_ptr, 1);
}

void
Dbdict::buildIndex_atDropConstraint(Signal* signal, SchemaOpPtr op_ptr)
{
  DropTriggerRecPtr dropTriggerPtr;
  getOpRec(op_ptr, dropTriggerPtr);
  const DropTrigImplReq* impl_req = &dropTriggerPtr.p->m_request;

  // the trigger is already deleted

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->tableId);

  D("buildIndex_atDropConstraint" << *op_ptr.p);

  ndbrequire(indexPtr.p->buildTriggerId != RNIL);
  indexPtr.p->buildTriggerId = RNIL;
}

void
Dbdict::buildIndex_fromDropConstraint(Signal* signal, Uint32 op_key, Uint32 ret)
{
  jam();

  SchemaOpPtr op_ptr;
  BuildIndexRecPtr buildIndexPtr;

  findSchemaOp(op_ptr, buildIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  //const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  D("buildIndex_fromDropConstraint" << dec << V(op_key) << V(ret));

  if (ret == 0) {
    const DropTrigConf* conf =
      (const DropTrigConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    ndbrequire(buildIndexPtr.p->m_subOpIndex < buildIndexPtr.p->m_subOpCount);
    buildIndexPtr.p->m_subOpIndex += 1;
    createSubOps(signal, op_ptr);
  } else {
    jam();
    const DropTrigRef* ref =
      (const DropTrigRef*)signal->getDataPtr();

    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::buildIndex_reply(Signal* signal, SchemaOpPtr op_ptr, ErrorInfo error)
{
  jam();

  SchemaTransPtr& trans_ptr = op_ptr.p->m_trans_ptr;
  BuildIndexRecPtr buildIndexPtr;
  getOpRec(op_ptr, buildIndexPtr);
  const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  D("buildIndex_reply" << V(impl_req->indexId));

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  if (!hasError(error)) {
    BuildIndxConf* conf = (BuildIndxConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->clientData = op_ptr.p->m_clientData;
    conf->transId = trans_ptr.p->m_transId;
    conf->tableId = impl_req->tableId;
    conf->indexId = impl_req->indexId;
    conf->indexType = impl_req->indexType;

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_BUILDINDXCONF, signal,
               BuildIndxConf::SignalLength, JBB);
  } else {
    jam();
    BuildIndxRef* ref = (BuildIndxRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->clientData = op_ptr.p->m_clientData;
    ref->transId = trans_ptr.p->m_transId;
    ref->tableId = impl_req->tableId;
    ref->indexId = impl_req->indexId;
    ref->indexType = impl_req->indexType;
    getError(error, ref);

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_BUILDINDXREF, signal,
               BuildIndxRef::SignalLength, JBB);
  }
}

// BuildIndex: PREPARE

void
Dbdict::buildIndex_prepare(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  BuildIndexRecPtr buildIndexPtr;
  getOpRec(op_ptr, buildIndexPtr);
  //const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  Uint32 requestInfo = op_ptr.p->m_requestInfo;
  bool noBuild = (requestInfo & DictSignal::RF_NO_BUILD);

  D("buildIndex_prepare" << hex << V(requestInfo));

  if (noBuild || !buildIndexPtr.p->m_doBuild) {
    jam();
    sendTransConf(signal, op_ptr);
    return;
  }

  buildIndex_toLocalBuild(signal, op_ptr);
}

void
Dbdict:: buildIndex_toLocalBuild(Signal* signal, SchemaOpPtr op_ptr)
{
  BuildIndexRecPtr buildIndexPtr;
  getOpRec(op_ptr, buildIndexPtr);
  const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  D("buildIndex_toLocalBuild");

  BuildIndxImplReq* req = (BuildIndxImplReq*)signal->getDataPtrSend();

  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->requestType = 0;
  req->transId = trans_ptr.p->m_transId;
  req->buildId = impl_req->buildId;
  req->buildKey = impl_req->buildKey;
  req->tableId = impl_req->tableId;
  req->indexId = impl_req->indexId;
  req->indexType = indexPtr.p->tableType;
  req->parallelism = impl_req->parallelism;

  Callback c = {
    safe_cast(&Dbdict::buildIndex_fromLocalBuild),
    op_ptr.p->op_key
  };
  op_ptr.p->m_callback = c;

  switch (indexPtr.p->tableType) {
  case DictTabInfo::UniqueHashIndex:
    jam();
    {
      LinearSectionPtr ls_ptr[3];
      ls_ptr[0].sz = buildIndexPtr.p->m_indexKeyList.sz;
      ls_ptr[0].p = buildIndexPtr.p->m_indexKeyList.id;
      ls_ptr[1].sz = buildIndexPtr.p->m_tableKeyList.sz;
      ls_ptr[1].p = buildIndexPtr.p->m_tableKeyList.id;

      sendSignal(TRIX_REF, GSN_BUILD_INDX_IMPL_REQ, signal,
                 BuildIndxImplReq::SignalLength, JBB, ls_ptr, 2);
    }
    break;
  case DictTabInfo::OrderedIndex:
    jam();
    {
      sendSignal(DBTUP_REF, GSN_BUILD_INDX_IMPL_REQ, signal,
                 BuildIndxImplReq::SignalLength, JBB);
    }
    break;
  default:
    ndbrequire(false);
    break;
  }
}

void
Dbdict::buildIndex_fromLocalBuild(Signal* signal, Uint32 op_key, Uint32 ret)
{
  SchemaOpPtr op_ptr;
  BuildIndexRecPtr buildIndexPtr;
  findSchemaOp(op_ptr, buildIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  D("buildIndex_fromLocalBuild");

  if (ret == 0) {
    jam();
    sendTransConf(signal, op_ptr);
  } else {
    jam();
    setError(op_ptr, ret, __LINE__);
    sendTransRef(signal, op_ptr);
  }
}

// BuildIndex: COMMIT

void
Dbdict::buildIndex_commit(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  BuildIndexRecPtr buildIndexPtr;
  getOpRec(op_ptr, buildIndexPtr);

  D("buildIndex_commit" << *op_ptr.p);

  buildIndex_toLocalOnline(signal, op_ptr);
}

void
Dbdict::buildIndex_toLocalOnline(Signal* signal, SchemaOpPtr op_ptr)
{
  BuildIndexRecPtr buildIndexPtr;
  getOpRec(op_ptr, buildIndexPtr);
  const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  D("buildIndex_toLocalOnline");

  AlterIndxImplReq* req = (AlterIndxImplReq*)signal->getDataPtrSend();

  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->requestType = impl_req->requestType;
  req->tableId = impl_req->tableId;
  req->tableVersion = 0; // not used
  req->indexId = impl_req->indexId;
  req->indexVersion = 0; // not used

  Callback c = {
    safe_cast(&Dbdict::buildIndex_fromLocalOnline),
    op_ptr.p->op_key
  };
  op_ptr.p->m_callback = c;

  switch (indexPtr.p->tableType) {
  case DictTabInfo::UniqueHashIndex:
    jam();
    {
      sendSignal(DBTC_REF, GSN_ALTER_INDX_IMPL_REQ, signal,
                 AlterIndxImplReq::SignalLength, JBB);
    }
    break;
  case DictTabInfo::OrderedIndex:
    jam();
    {
      sendSignal(DBTUX_REF, GSN_ALTER_INDX_IMPL_REQ, signal,
                 AlterIndxImplReq::SignalLength, JBB);
    }
    break;
  default:
    ndbrequire(false);
    break;
  }
}

void
Dbdict::buildIndex_fromLocalOnline(Signal* signal, Uint32 op_key, Uint32 ret)
{
  SchemaOpPtr op_ptr;
  BuildIndexRecPtr buildIndexPtr;
  findSchemaOp(op_ptr, buildIndexPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  const BuildIndxImplReq* impl_req = &buildIndexPtr.p->m_request;

  TableRecordPtr indexPtr;
  c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);

  D("buildIndex_fromLocalOnline");

  if (ret == 0) {
    jam();
    // set index online
    indexPtr.p->indexState = TableRecord::IS_ONLINE;
    sendTransConf(signal, op_ptr);
  } else {
    //wl3600_todo
    ndbrequire(false);
  }
}

// BuildIndex: ABORT

void
Dbdict::buildIndex_abortParse(Signal* signal, SchemaOpPtr op_ptr)
{
  D("buildIndex_abortParse" << *op_ptr.p);
  // wl3600_todo
  sendTransConf(signal, op_ptr);
}

void
Dbdict::buildIndex_abortPrepare(Signal* signal, SchemaOpPtr op_ptr)
{
  D("buildIndex_abortPrepare" << *op_ptr.p);

  // nothing to do, entire index table will be dropped
  sendTransConf(signal, op_ptr);
}

// BuildIndex: MISC

void
Dbdict::execBUILDINDXCONF(Signal* signal)
{
  jamEntry();
  const BuildIndxConf* conf = (const BuildIndxConf*)signal->getDataPtr();
  handleDictConf(signal, conf);
}

void
Dbdict::execBUILDINDXREF(Signal* signal)
{
  jamEntry();
  const BuildIndxRef* ref = (const BuildIndxRef*)signal->getDataPtr();
  handleDictRef(signal, ref);
}

void
Dbdict::execBUILD_INDX_IMPL_CONF(Signal* signal)
{
  jamEntry();
  const BuildIndxImplConf* conf = (const BuildIndxImplConf*)signal->getDataPtr();
  handleDictConf(signal, conf);
}

void
Dbdict::execBUILD_INDX_IMPL_REF(Signal* signal)
{
  jamEntry();
  const BuildIndxImplRef* ref = (const BuildIndxImplRef*)signal->getDataPtr();
  handleDictRef(signal, ref);
}

// BuildIndex: END

/*****************************************************
 *
 * MODULE: Util signalling
 *
 *****************************************************/

int
Dbdict::sendSignalUtilReq(Callback *pcallback,
			  BlockReference ref, 
			  GlobalSignalNumber gsn, 
			  Signal* signal, 
			  Uint32 length, 
			  JobBufferLevel jbuf,
			  LinearSectionPtr ptr[3],
			  Uint32 noOfSections)
{
  jam();
  EVENT_TRACE;
  OpSignalUtilPtr utilRecPtr;

  // Seize a Util Send record
  if (!c_opSignalUtil.seize(utilRecPtr)) {
    // Failed to allocate util record
    return -1;
  }
  utilRecPtr.p->m_callback = *pcallback;

  // should work for all util signal classes
  UtilPrepareReq *req = (UtilPrepareReq*)signal->getDataPtrSend();
  utilRecPtr.p->m_userData = req->getSenderData();
  req->setSenderData(utilRecPtr.i);

  if (ptr) {
    jam();
    sendSignal(ref, gsn, signal, length, jbuf, ptr, noOfSections);
  } else {
    jam();
    sendSignal(ref, gsn, signal, length, jbuf);
  }

  return 0;
}

int
Dbdict::recvSignalUtilReq(Signal* signal, Uint32 returnCode)
{
  jam();
  EVENT_TRACE;
  UtilPrepareConf * const req = (UtilPrepareConf*)signal->getDataPtr();
  OpSignalUtilPtr utilRecPtr;
  utilRecPtr.i = req->getSenderData();
  if ((utilRecPtr.p = c_opSignalUtil.getPtr(utilRecPtr.i)) == NULL) {
    jam();
    return -1;
  }

  req->setSenderData(utilRecPtr.p->m_userData);
  Callback c = utilRecPtr.p->m_callback;
  c_opSignalUtil.release(utilRecPtr);

  execute(signal, c, returnCode);
  return 0;
}

void Dbdict::execUTIL_PREPARE_CONF(Signal *signal)
{
  jamEntry();
  EVENT_TRACE;
  ndbrequire(recvSignalUtilReq(signal, 0) == 0);
}

void
Dbdict::execUTIL_PREPARE_REF(Signal *signal)
{
  jamEntry();
  EVENT_TRACE;
  ndbrequire(recvSignalUtilReq(signal, 1) == 0);
}

void Dbdict::execUTIL_EXECUTE_CONF(Signal *signal)
{
  jamEntry();
  EVENT_TRACE;
   ndbrequire(recvSignalUtilReq(signal, 0) == 0);
}

void Dbdict::execUTIL_EXECUTE_REF(Signal *signal)
{
  jamEntry();
  EVENT_TRACE;

#ifdef EVENT_DEBUG
  UtilExecuteRef * ref = (UtilExecuteRef *)signal->getDataPtrSend();

  ndbout_c("execUTIL_EXECUTE_REF");
  ndbout_c("senderData %u",ref->getSenderData());
  ndbout_c("errorCode %u",ref->getErrorCode());
  ndbout_c("TCErrorCode %u",ref->getTCErrorCode());
#endif
  
  ndbrequire(recvSignalUtilReq(signal, 1) == 0);
}
void Dbdict::execUTIL_RELEASE_CONF(Signal *signal)
{
  jamEntry();
  EVENT_TRACE;
  ndbrequire(false);
  ndbrequire(recvSignalUtilReq(signal, 0) == 0);
}
void Dbdict::execUTIL_RELEASE_REF(Signal *signal)
{
  jamEntry();
  EVENT_TRACE;
  ndbrequire(false);
  ndbrequire(recvSignalUtilReq(signal, 1) == 0);
}

/**
 * MODULE: Create event
 *
 * Create event in DICT.
 * 
 *
 * Request type in CREATE_EVNT signals:
 *
 * Signalflow see Dbdict.txt
 *
 */

/*****************************************************************
 *
 * Systable stuff
 *
 */

const Uint32 Dbdict::sysTab_NDBEVENTS_0_szs[EVENT_SYSTEM_TABLE_LENGTH] = {
  sizeof(((sysTab_NDBEVENTS_0*)0)->NAME),
  sizeof(((sysTab_NDBEVENTS_0*)0)->EVENT_TYPE),
  sizeof(((sysTab_NDBEVENTS_0*)0)->TABLEID),
  sizeof(((sysTab_NDBEVENTS_0*)0)->TABLEVERSION),
  sizeof(((sysTab_NDBEVENTS_0*)0)->TABLE_NAME),
  sizeof(((sysTab_NDBEVENTS_0*)0)->ATTRIBUTE_MASK),
  sizeof(((sysTab_NDBEVENTS_0*)0)->SUBID),
  sizeof(((sysTab_NDBEVENTS_0*)0)->SUBKEY)
};

void
Dbdict::prepareTransactionEventSysTable (Callback *pcallback,
					 Signal* signal,
					 Uint32 senderData,
					 UtilPrepareReq::OperationTypeValue prepReq)
{
  // find table id for event system table
  DictObject * opj_ptr_p = get_object(EVENT_SYSTEM_TABLE_NAME,
				      sizeof(EVENT_SYSTEM_TABLE_NAME));

  ndbrequire(opj_ptr_p != 0);
  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, opj_ptr_p->m_id);
  ndbrequire(tablePtr.i != RNIL); // system table must exist
  
  Uint32 tableId = tablePtr.p->tableId; /* System table */
  Uint32 noAttr = tablePtr.p->noOfAttributes;
  ndbrequire(noAttr == EVENT_SYSTEM_TABLE_LENGTH);
  
  switch (prepReq) {
  case UtilPrepareReq::Update:
  case UtilPrepareReq::Insert:
  case UtilPrepareReq::Write:
  case UtilPrepareReq::Read:
    jam();
    break;
  case UtilPrepareReq::Delete:
    jam();
    noAttr = 1; // only involves Primary key which should be the first
    break;
  }
  prepareUtilTransaction(pcallback, signal, senderData, tableId, NULL,
		      prepReq, noAttr, NULL, NULL);
}

void
Dbdict::prepareUtilTransaction(Callback *pcallback,
			       Signal* signal,
			       Uint32 senderData,
			       Uint32 tableId,
			       const char* tableName,
			       UtilPrepareReq::OperationTypeValue prepReq,
			       Uint32 noAttr,
			       Uint32 attrIds[],
			       const char *attrNames[])
{
  jam();
  EVENT_TRACE;

  UtilPrepareReq * utilPrepareReq = 
    (UtilPrepareReq *)signal->getDataPtrSend();
  
  utilPrepareReq->setSenderRef(reference());
  utilPrepareReq->setSenderData(senderData);

  const Uint32 pageSizeInWords = 128;
  Uint32 propPage[pageSizeInWords];
  LinearWriter w(&propPage[0],128);
  w.first();
  w.add(UtilPrepareReq::NoOfOperations, 1);
  w.add(UtilPrepareReq::OperationType, prepReq);
  if (tableName) {
    jam();
    w.add(UtilPrepareReq::TableName, tableName);
  } else {
    jam();
    w.add(UtilPrepareReq::TableId, tableId);
  }
  for(Uint32 i = 0; i < noAttr; i++)
    if (tableName) {
      jam();
      w.add(UtilPrepareReq::AttributeName, attrNames[i]);
    } else {
      if (attrIds) {
	jam();
	w.add(UtilPrepareReq::AttributeId, attrIds[i]);
      } else {
	jam();
	w.add(UtilPrepareReq::AttributeId, i);
      }
    }
#ifdef EVENT_DEBUG
  // Debugging
  SimplePropertiesLinearReader reader(propPage, w.getWordsUsed());
  printf("Dict::prepareInsertTransactions: Sent SimpleProperties:\n");
  reader.printAll(ndbout);
#endif

  struct LinearSectionPtr sectionsPtr[UtilPrepareReq::NoOfSections];
  sectionsPtr[UtilPrepareReq::PROPERTIES_SECTION].p = propPage;
  sectionsPtr[UtilPrepareReq::PROPERTIES_SECTION].sz = w.getWordsUsed();

  sendSignalUtilReq(pcallback, DBUTIL_REF, GSN_UTIL_PREPARE_REQ, signal,
		    UtilPrepareReq::SignalLength, JBB, 
		    sectionsPtr, UtilPrepareReq::NoOfSections);
}

/*****************************************************************
 *
 * CREATE_EVNT_REQ has three types RT_CREATE, RT_GET (from user)
 * and RT_DICT_AFTER_GET send from master DICT to slaves
 *
 * This function just dscpaches these to
 *
 * createEvent_RT_USER_CREATE
 * createEvent_RT_USER_GET
 * createEvent_RT_DICT_AFTER_GET
 *
 * repectively
 *
 */

void
Dbdict::execCREATE_EVNT_REQ(Signal* signal)
{
  jamEntry();

#if 0
  {
    SafeCounterHandle handle;
    {
      SafeCounter tmp(c_counterMgr, handle);
      tmp.init<CreateEvntRef>(CMVMI, GSN_DUMP_STATE_ORD, /* senderData */ 13);
      tmp.clearWaitingFor();
      tmp.setWaitingFor(3);
      ndbrequire(!tmp.done());
      ndbout_c("Allocted");
    }
    ndbrequire(!handle.done());
    {
      SafeCounter tmp(c_counterMgr, handle);
      tmp.clearWaitingFor(3);
      ndbrequire(tmp.done());
      ndbout_c("Deallocted");
    }
    ndbrequire(handle.done());
  }
  {
    NodeBitmask nodes;
    nodes.clear();

    nodes.set(2);
    nodes.set(3);
    nodes.set(4);
    nodes.set(5);

    {
      Uint32 i = 0;
      while((i = nodes.find(i)) != NodeBitmask::NotFound){
	ndbout_c("1 Node id = %u", i);
	i++;
      }
    }

    NodeReceiverGroup rg(DBDICT, nodes);
    RequestTracker rt2;
    ndbrequire(rt2.done());
    ndbrequire(!rt2.hasRef());
    ndbrequire(!rt2.hasConf());
    rt2.init<CreateEvntRef>(c_counterMgr, rg, GSN_CREATE_EVNT_REF, 13);

    RequestTracker rt3;
    rt3.init<CreateEvntRef>(c_counterMgr, rg, GSN_CREATE_EVNT_REF, 13);

    ndbrequire(!rt2.done());
    ndbrequire(!rt3.done());

    rt2.reportRef(c_counterMgr, 2);
    rt3.reportConf(c_counterMgr, 2);

    ndbrequire(!rt2.done());
    ndbrequire(!rt3.done());

    rt2.reportConf(c_counterMgr, 3);
    rt3.reportConf(c_counterMgr, 3);

    ndbrequire(!rt2.done());
    ndbrequire(!rt3.done());

    rt2.reportConf(c_counterMgr, 4);
    rt3.reportConf(c_counterMgr, 4);

    ndbrequire(!rt2.done());
    ndbrequire(!rt3.done());

    rt2.reportConf(c_counterMgr, 5);
    rt3.reportConf(c_counterMgr, 5);

    ndbrequire(rt2.done());
    ndbrequire(rt3.done());
  }
#endif

  if (! assembleFragments(signal)) {
    jam();
    return;
  }

  CreateEvntReq *req = (CreateEvntReq*)signal->getDataPtr();
  const CreateEvntReq::RequestType requestType = req->getRequestType();
  const Uint32                     requestFlag = req->getRequestFlag();
  SectionHandle handle(this, signal);

  if (refToBlock(signal->senderBlockRef()) != DBDICT &&
      getOwnNodeId() != c_masterNodeId)
  {
    jam();
    releaseSections(handle);
    
    CreateEvntRef * ref = (CreateEvntRef *)signal->getDataPtrSend();
    ref->setUserRef(reference());
    ref->setErrorCode(CreateEvntRef::NotMaster);
    ref->setErrorLine(__LINE__);
    ref->setErrorNode(reference());
    ref->setMasterNode(c_masterNodeId);
    sendSignal(signal->senderBlockRef(), GSN_CREATE_EVNT_REF, signal,
	       CreateEvntRef::SignalLength2, JBB);
    return;
  }

  OpCreateEventPtr evntRecPtr;
  // Seize a Create Event record
  if (!c_opCreateEvent.seize(evntRecPtr)) {
    // Failed to allocate event record
    jam();
    releaseSections(handle);

    CreateEvntRef * ret = (CreateEvntRef *)signal->getDataPtrSend();
    ret->senderRef = reference();
    ret->setErrorCode(747);
    ret->setErrorLine(__LINE__);
    ret->setErrorNode(reference());
    sendSignal(signal->senderBlockRef(), GSN_CREATE_EVNT_REF, signal,
	       CreateEvntRef::SignalLength, JBB);
    return;
  }

#ifdef EVENT_DEBUG
  ndbout_c("DBDICT::execCREATE_EVNT_REQ from %u evntRecId = (%d)", refToNode(signal->getSendersBlockRef()), evntRecPtr.i);
#endif
  
  ndbrequire(req->getUserRef() == signal->getSendersBlockRef());

  evntRecPtr.p->init(req,this);

  if (requestFlag & (Uint32)CreateEvntReq::RT_DICT_AFTER_GET) {
    jam();
    EVENT_TRACE;
    releaseSections(handle);
    createEvent_RT_DICT_AFTER_GET(signal, evntRecPtr);
    return;
  }
  if (requestType == CreateEvntReq::RT_USER_GET) {
    jam();
    EVENT_TRACE;
    createEvent_RT_USER_GET(signal, evntRecPtr, handle);
    return;
  }
  if (requestType == CreateEvntReq::RT_USER_CREATE) {
    jam();
    EVENT_TRACE;
    createEvent_RT_USER_CREATE(signal, evntRecPtr, handle);
    return;
  }

#ifdef EVENT_DEBUG
  ndbout << "Dbdict.cpp: Dbdict::execCREATE_EVNT_REQ other" << endl;
#endif
  jam();
  releaseSections(handle);
    
  evntRecPtr.p->m_errorCode = 1;
  evntRecPtr.p->m_errorLine = __LINE__;
  evntRecPtr.p->m_errorNode = reference();
  
  createEvent_sendReply(signal, evntRecPtr);
}

/********************************************************************
 *
 * Event creation
 *
 *****************************************************************/

void
Dbdict::createEvent_RT_USER_CREATE(Signal* signal,
				   OpCreateEventPtr evntRecPtr,
				   SectionHandle& handle)
{
  jam();
  DBUG_ENTER("Dbdict::createEvent_RT_USER_CREATE");
  evntRecPtr.p->m_request.setUserRef(signal->senderBlockRef());

#ifdef EVENT_DEBUG
  ndbout << "Dbdict.cpp: Dbdict::execCREATE_EVNT_REQ RT_USER" << endl;
  char buf[128] = {0};
  AttributeMask mask = evntRecPtr.p->m_request.getAttrListBitmask(); 
  mask.getText(buf);
  ndbout_c("mask = %s", buf);
#endif

  // Interpret the long signal

  SegmentedSectionPtr ssPtr;
  // save name and event properties
  ndbrequire(handle.getSection(ssPtr, CreateEvntReq::EVENT_NAME_SECTION));

  SimplePropertiesSectionReader r0(ssPtr, getSectionSegmentPool());
#ifdef EVENT_DEBUG
  r0.printAll(ndbout);
#endif
    // event name
  if ((!r0.first()) ||
      (r0.getValueType() != SimpleProperties::StringValue) ||
      (r0.getValueLen() <= 0)) {
    jam();
    releaseSections(handle);

    evntRecPtr.p->m_errorCode = 1;
    evntRecPtr.p->m_errorLine = __LINE__;
    evntRecPtr.p->m_errorNode = reference();

    createEvent_sendReply(signal, evntRecPtr);
    DBUG_VOID_RETURN;
  }
  r0.getString(evntRecPtr.p->m_eventRec.NAME);
  {
    int len = strlen(evntRecPtr.p->m_eventRec.NAME);
    memset(evntRecPtr.p->m_eventRec.NAME+len, 0, MAX_TAB_NAME_SIZE-len);
#ifdef EVENT_DEBUG
    printf("CreateEvntReq::RT_USER_CREATE; EventName %s, len %u\n",
	   evntRecPtr.p->m_eventRec.NAME, len);
    for(int i = 0; i < MAX_TAB_NAME_SIZE/4; i++)
      printf("H'%.8x ", ((Uint32*)evntRecPtr.p->m_eventRec.NAME)[i]);
    printf("\n");
#endif
  }
  // table name
  if ((!r0.next()) ||
      (r0.getValueType() != SimpleProperties::StringValue) ||
      (r0.getValueLen() <= 0)) {
    jam();
    releaseSections(handle);
    
    evntRecPtr.p->m_errorCode = 1;
    evntRecPtr.p->m_errorLine = __LINE__;
    evntRecPtr.p->m_errorNode = reference();
    
    createEvent_sendReply(signal, evntRecPtr);
    DBUG_VOID_RETURN;
  }
  r0.getString(evntRecPtr.p->m_eventRec.TABLE_NAME);
  {
    int len = strlen(evntRecPtr.p->m_eventRec.TABLE_NAME);
    memset(evntRecPtr.p->m_eventRec.TABLE_NAME+len, 0, MAX_TAB_NAME_SIZE-len);
  }

  releaseSections(handle);
  
  // Send request to SUMA

  CreateSubscriptionIdReq * sumaIdReq =
    (CreateSubscriptionIdReq *)signal->getDataPtrSend();
  
  // make sure we save the original sender for later
  sumaIdReq->senderRef  = reference();
  sumaIdReq->senderData = evntRecPtr.i;
#ifdef EVENT_DEBUG
  ndbout << "sumaIdReq->senderData = " << sumaIdReq->senderData << endl;
#endif
  sendSignal(SUMA_REF, GSN_CREATE_SUBID_REQ, signal, 
	     CreateSubscriptionIdReq::SignalLength, JBB);
  // we should now return in either execCREATE_SUBID_CONF
  // or execCREATE_SUBID_REF
  DBUG_VOID_RETURN;
}

void Dbdict::execCREATE_SUBID_REF(Signal* signal)
{
  jamEntry();
  DBUG_ENTER("Dbdict::execCREATE_SUBID_REF");
  CreateSubscriptionIdRef * const ref =
    (CreateSubscriptionIdRef *)signal->getDataPtr();
  OpCreateEventPtr evntRecPtr;

  evntRecPtr.i = ref->senderData;
  ndbrequire((evntRecPtr.p = c_opCreateEvent.getPtr(evntRecPtr.i)) != NULL);

  if (ref->errorCode)
  {
    evntRecPtr.p->m_errorCode = ref->errorCode;
    evntRecPtr.p->m_errorLine = __LINE__;
  }
  else
  {
    evntRecPtr.p->m_errorCode = 1;
    evntRecPtr.p->m_errorLine = __LINE__;
  }
  evntRecPtr.p->m_errorNode = reference();

  createEvent_sendReply(signal, evntRecPtr);
  DBUG_VOID_RETURN;
}

void Dbdict::execCREATE_SUBID_CONF(Signal* signal)
{
  jamEntry();
  DBUG_ENTER("Dbdict::execCREATE_SUBID_CONF");

  CreateSubscriptionIdConf const * sumaIdConf =
    (CreateSubscriptionIdConf *)signal->getDataPtr();

  Uint32 evntRecId = sumaIdConf->senderData;
  OpCreateEvent *evntRec;

  ndbrequire((evntRec = c_opCreateEvent.getPtr(evntRecId)) != NULL);

  evntRec->m_request.setEventId(sumaIdConf->subscriptionId);
  evntRec->m_request.setEventKey(sumaIdConf->subscriptionKey);

  Callback c = { safe_cast(&Dbdict::createEventUTIL_PREPARE), 0 };

  prepareTransactionEventSysTable(&c, signal, evntRecId,
				  UtilPrepareReq::Insert);
  DBUG_VOID_RETURN;
}

void
Dbdict::createEventComplete_RT_USER_CREATE(Signal* signal,
					   OpCreateEventPtr evntRecPtr){
  jam();
  createEvent_sendReply(signal, evntRecPtr);
}

/*********************************************************************
 *
 * UTIL_PREPARE, UTIL_EXECUTE
 *
 * insert or read systable NDB$EVENTS_0
 */

void interpretUtilPrepareErrorCode(UtilPrepareRef::ErrorCode errorCode,
				   Uint32& error, Uint32& line,
                                   const Dbdict *dict)
{
  DBUG_ENTER("interpretUtilPrepareErrorCode");
  switch (errorCode) {
  case UtilPrepareRef::NO_ERROR:
    jamBlock(dict);
    error = 1;
    line = __LINE__;
    DBUG_VOID_RETURN;
  case UtilPrepareRef::PREPARE_SEIZE_ERROR:
    jamBlock(dict);
    error = 748;
    line = __LINE__;
    DBUG_VOID_RETURN;
  case UtilPrepareRef::PREPARE_PAGES_SEIZE_ERROR:
    jamBlock(dict);
    error = 1;
    line = __LINE__;
    DBUG_VOID_RETURN;
  case UtilPrepareRef::PREPARED_OPERATION_SEIZE_ERROR:
    jamBlock(dict);
    error = 1;
    line = __LINE__;
    DBUG_VOID_RETURN;
  case UtilPrepareRef::DICT_TAB_INFO_ERROR:
    jamBlock(dict);
    error = 1;
    line = __LINE__;
    DBUG_VOID_RETURN;
  case UtilPrepareRef::MISSING_PROPERTIES_SECTION:
    jamBlock(dict);
    error = 1;
    line = __LINE__;
    DBUG_VOID_RETURN;
  default:
    jamBlock(dict);
    error = 1;
    line = __LINE__;
    DBUG_VOID_RETURN;
  }
  DBUG_VOID_RETURN;
}

void 
Dbdict::createEventUTIL_PREPARE(Signal* signal,
				Uint32 callbackData,
				Uint32 returnCode)
{
  jam();
  EVENT_TRACE;
  if (returnCode == 0) {
    UtilPrepareConf* const req = (UtilPrepareConf*)signal->getDataPtr();
    OpCreateEventPtr evntRecPtr;
    jam();
    evntRecPtr.i = req->getSenderData();
    const Uint32 prepareId = req->getPrepareId();
    
    ndbrequire((evntRecPtr.p = c_opCreateEvent.getPtr(evntRecPtr.i)) != NULL);
    
    Callback c = { safe_cast(&Dbdict::createEventUTIL_EXECUTE), 0 };

    switch (evntRecPtr.p->m_requestType) {
    case CreateEvntReq::RT_USER_GET:
      jam();
      executeTransEventSysTable(&c, signal,
				evntRecPtr.i, evntRecPtr.p->m_eventRec,
				prepareId, UtilPrepareReq::Read);
      break;
    case CreateEvntReq::RT_USER_CREATE:
      {
	evntRecPtr.p->m_eventRec.EVENT_TYPE =
          evntRecPtr.p->m_request.getEventType() | evntRecPtr.p->m_request.getReportFlags();
	evntRecPtr.p->m_eventRec.TABLEID  = evntRecPtr.p->m_request.getTableId();
	evntRecPtr.p->m_eventRec.TABLEVERSION=evntRecPtr.p->m_request.getTableVersion();
	AttributeMask m = evntRecPtr.p->m_request.getAttrListBitmask();
	memcpy(evntRecPtr.p->m_eventRec.ATTRIBUTE_MASK, &m,
	       sizeof(evntRecPtr.p->m_eventRec.ATTRIBUTE_MASK));
	evntRecPtr.p->m_eventRec.SUBID  = evntRecPtr.p->m_request.getEventId();
	evntRecPtr.p->m_eventRec.SUBKEY = evntRecPtr.p->m_request.getEventKey();
	DBUG_PRINT("info",
		   ("CREATE: event name: %s table name: %s table id: %u table version: %u",
		    evntRecPtr.p->m_eventRec.NAME,
		    evntRecPtr.p->m_eventRec.TABLE_NAME,
		    evntRecPtr.p->m_eventRec.TABLEID,
		    evntRecPtr.p->m_eventRec.TABLEVERSION));
  
      }
      jam();
      executeTransEventSysTable(&c, signal,
				evntRecPtr.i, evntRecPtr.p->m_eventRec,
				prepareId, UtilPrepareReq::Insert);
      break;
    default:
#ifdef EVENT_DEBUG
      printf("type = %d\n", evntRecPtr.p->m_requestType);
      printf("bet type = %d\n", CreateEvntReq::RT_USER_GET);
      printf("create type = %d\n", CreateEvntReq::RT_USER_CREATE);
#endif
      ndbrequire(false);
    }
  } else { // returnCode != 0
    UtilPrepareRef* const ref = (UtilPrepareRef*)signal->getDataPtr();

    const UtilPrepareRef::ErrorCode errorCode =
      (UtilPrepareRef::ErrorCode)ref->getErrorCode();

    OpCreateEventPtr evntRecPtr;
    evntRecPtr.i = ref->getSenderData();
    ndbrequire((evntRecPtr.p = c_opCreateEvent.getPtr(evntRecPtr.i)) != NULL);

    interpretUtilPrepareErrorCode(errorCode, evntRecPtr.p->m_errorCode,
				  evntRecPtr.p->m_errorLine, this);
    evntRecPtr.p->m_errorNode = reference();

    createEvent_sendReply(signal, evntRecPtr);
  }
}

void Dbdict::executeTransEventSysTable(Callback *pcallback, Signal *signal,
				       const Uint32 ptrI,
				       sysTab_NDBEVENTS_0& m_eventRec,
				       const Uint32 prepareId,
				       UtilPrepareReq::OperationTypeValue prepReq)
{
  jam();
  const Uint32 noAttr = EVENT_SYSTEM_TABLE_LENGTH;
  Uint32 total_len = 0;

  Uint32* attrHdr = signal->theData + 25;
  Uint32* attrPtr = attrHdr;

  Uint32 id=0;
  // attribute 0 event name: Primary Key
  {
    AttributeHeader::init(attrPtr, id, sysTab_NDBEVENTS_0_szs[id]);
    total_len += sysTab_NDBEVENTS_0_szs[id];
    attrPtr++; id++;
  }

  switch (prepReq) {
  case UtilPrepareReq::Read:
    jam();
    EVENT_TRACE;
    // no more
    while ( id < noAttr )
      AttributeHeader::init(attrPtr++, id++, 0);
    ndbrequire(id == (Uint32) noAttr);
    break;
  case UtilPrepareReq::Insert:
    jam();
    EVENT_TRACE;
    while ( id < noAttr ) {
      AttributeHeader::init(attrPtr, id, sysTab_NDBEVENTS_0_szs[id]);
      total_len += sysTab_NDBEVENTS_0_szs[id];
      attrPtr++; id++;
    }
    ndbrequire(id == (Uint32) noAttr);
    break;
  case UtilPrepareReq::Delete:
    ndbrequire(id == 1);
    break;
  default:
    ndbrequire(false);
  }
    
  LinearSectionPtr headerPtr;
  LinearSectionPtr dataPtr;
    
  headerPtr.p = attrHdr;
  headerPtr.sz = noAttr;
    
  dataPtr.p = (Uint32*)&m_eventRec;
  dataPtr.sz = total_len/4;

  ndbrequire((total_len == sysTab_NDBEVENTS_0_szs[0]) ||
	     (total_len == sizeof(sysTab_NDBEVENTS_0)));

#if 0
    printf("Header size %u\n", headerPtr.sz);
    for(int i = 0; i < (int)headerPtr.sz; i++)
      printf("H'%.8x ", attrHdr[i]);
    printf("\n");
    
    printf("Data size %u\n", dataPtr.sz);
    for(int i = 0; i < (int)dataPtr.sz; i++)
      printf("H'%.8x ", dataPage[i]);
    printf("\n");
#endif

  executeTransaction(pcallback, signal, 
		     ptrI,
		     prepareId,
		     id,
		     headerPtr,
		     dataPtr);
}

void Dbdict::executeTransaction(Callback *pcallback,
				Signal* signal, 
				Uint32 senderData,
				Uint32 prepareId,
				Uint32 noAttr,
				LinearSectionPtr headerPtr,
				LinearSectionPtr dataPtr)
{
  jam();
  EVENT_TRACE;

  UtilExecuteReq * utilExecuteReq = 
    (UtilExecuteReq *)signal->getDataPtrSend();

  utilExecuteReq->setSenderRef(reference());
  utilExecuteReq->setSenderData(senderData);
  utilExecuteReq->setPrepareId(prepareId);
  utilExecuteReq->setReleaseFlag(); // must be done after setting prepareId

#if 0
  printf("Header size %u\n", headerPtr.sz);
  for(int i = 0; i < (int)headerPtr.sz; i++)
    printf("H'%.8x ", headerBuffer[i]);
  printf("\n");
  
  printf("Data size %u\n", dataPtr.sz);
  for(int i = 0; i < (int)dataPtr.sz; i++)
    printf("H'%.8x ", dataBuffer[i]);
  printf("\n");
#endif

  struct LinearSectionPtr sectionsPtr[UtilExecuteReq::NoOfSections];
  sectionsPtr[UtilExecuteReq::HEADER_SECTION].p = headerPtr.p;
  sectionsPtr[UtilExecuteReq::HEADER_SECTION].sz = noAttr;
  sectionsPtr[UtilExecuteReq::DATA_SECTION].p = dataPtr.p;
  sectionsPtr[UtilExecuteReq::DATA_SECTION].sz = dataPtr.sz;

  sendSignalUtilReq(pcallback, DBUTIL_REF, GSN_UTIL_EXECUTE_REQ, signal,
		    UtilExecuteReq::SignalLength, JBB,
		    sectionsPtr, UtilExecuteReq::NoOfSections);
}

void Dbdict::parseReadEventSys(Signal* signal, sysTab_NDBEVENTS_0& m_eventRec)
{
  jam();
  SectionHandle handle(this, signal);
  SegmentedSectionPtr headerPtr, dataPtr;

  handle.getSection(headerPtr, UtilExecuteReq::HEADER_SECTION);
  SectionReader headerReader(headerPtr, getSectionSegmentPool());
      
  handle.getSection(dataPtr, UtilExecuteReq::DATA_SECTION);
  SectionReader dataReader(dataPtr, getSectionSegmentPool());
  
  AttributeHeader header;
  Uint32 *dst = (Uint32*)&m_eventRec;

  for (int i = 0; i < EVENT_SYSTEM_TABLE_LENGTH; i++) {
    headerReader.getWord((Uint32 *)&header);
    int sz = header.getDataSize();
    for (int i=0; i < sz; i++)
      dataReader.getWord(dst++);
  }

  ndbrequire( ((char*)dst-(char*)&m_eventRec) == sizeof(m_eventRec) );

  releaseSections(handle);
}
    
void Dbdict::createEventUTIL_EXECUTE(Signal *signal, 
				     Uint32 callbackData,
				     Uint32 returnCode)
{
  jam();
  EVENT_TRACE;
  if (returnCode == 0) {
    // Entry into system table all set
    UtilExecuteConf* const conf = (UtilExecuteConf*)signal->getDataPtr();
    jam();
    OpCreateEventPtr evntRecPtr;
    evntRecPtr.i = conf->getSenderData();
    
    ndbrequire((evntRecPtr.p = c_opCreateEvent.getPtr(evntRecPtr.i)) != NULL);
    OpCreateEvent *evntRec = evntRecPtr.p;
    
    switch (evntRec->m_requestType) {
    case CreateEvntReq::RT_USER_GET: {
      parseReadEventSys(signal, evntRecPtr.p->m_eventRec);

      evntRec->m_request.setEventType(evntRecPtr.p->m_eventRec.EVENT_TYPE);
      evntRec->m_request.setReportFlags(evntRecPtr.p->m_eventRec.EVENT_TYPE);
      evntRec->m_request.setTableId(evntRecPtr.p->m_eventRec.TABLEID);
      evntRec->m_request.setTableVersion(evntRecPtr.p->m_eventRec.TABLEVERSION);
      evntRec->m_request.setAttrListBitmask(*(AttributeMask*)
					    evntRecPtr.p->m_eventRec.ATTRIBUTE_MASK);
      evntRec->m_request.setEventId(evntRecPtr.p->m_eventRec.SUBID);
      evntRec->m_request.setEventKey(evntRecPtr.p->m_eventRec.SUBKEY);

      DBUG_PRINT("info",
		 ("GET: event name: %s table name: %s table id: %u table version: %u",
		  evntRecPtr.p->m_eventRec.NAME,
		  evntRecPtr.p->m_eventRec.TABLE_NAME,
		  evntRecPtr.p->m_eventRec.TABLEID,
		  evntRecPtr.p->m_eventRec.TABLEVERSION));
      
      // find table id for event table
      DictObject* obj_ptr_p = get_object(evntRecPtr.p->m_eventRec.TABLE_NAME);
      if(!obj_ptr_p){
	jam();
	evntRecPtr.p->m_errorCode = 723;
	evntRecPtr.p->m_errorLine = __LINE__;
	evntRecPtr.p->m_errorNode = reference();
	
	createEvent_sendReply(signal, evntRecPtr);
	return;
      }
      
      TableRecordPtr tablePtr;
      c_tableRecordPool.getPtr(tablePtr, obj_ptr_p->m_id);
      evntRec->m_request.setTableId(tablePtr.p->tableId);
      evntRec->m_request.setTableVersion(tablePtr.p->tableVersion);
      
      createEventComplete_RT_USER_GET(signal, evntRecPtr);
      return;
    }
    case CreateEvntReq::RT_USER_CREATE: {
#ifdef EVENT_DEBUG
      printf("create type = %d\n", CreateEvntReq::RT_USER_CREATE);
#endif
      jam();
      createEventComplete_RT_USER_CREATE(signal, evntRecPtr);
      return;
    }
      break;
    default:
      ndbrequire(false);
    }
  } else { // returnCode != 0
    UtilExecuteRef * const ref = (UtilExecuteRef *)signal->getDataPtr();
    OpCreateEventPtr evntRecPtr;
    evntRecPtr.i = ref->getSenderData();
    ndbrequire((evntRecPtr.p = c_opCreateEvent.getPtr(evntRecPtr.i)) != NULL);
    jam();
    evntRecPtr.p->m_errorNode = reference();
    evntRecPtr.p->m_errorLine = __LINE__;

    switch (ref->getErrorCode()) {
    case UtilExecuteRef::TCError:
      switch (ref->getTCErrorCode()) {
      case ZNOT_FOUND:
	jam();
	evntRecPtr.p->m_errorCode = 4710;
	break;
      case ZALREADYEXIST:
	jam();
	evntRecPtr.p->m_errorCode = 746;
	break;
      default:
	jam();
	evntRecPtr.p->m_errorCode = ref->getTCErrorCode();
	break;
      }
      break;
    default:
      jam();
      evntRecPtr.p->m_errorCode = ref->getErrorCode();
      break;
    }
    
    createEvent_sendReply(signal, evntRecPtr);
  }
}

/***********************************************************************
 *
 * NdbEventOperation, reading systable, creating event in suma
 *
 */

void
Dbdict::createEvent_RT_USER_GET(Signal* signal,
				OpCreateEventPtr evntRecPtr,
				SectionHandle& handle){
  jam();
  EVENT_TRACE;
#ifdef EVENT_PH2_DEBUG
  ndbout_c("DBDICT(Coordinator) got GSN_CREATE_EVNT_REQ::RT_USER_GET evntRecPtr.i = (%d), ref = %u", evntRecPtr.i, evntRecPtr.p->m_request.getUserRef());
#endif

  SegmentedSectionPtr ssPtr;

  handle.getSection(ssPtr, 0);

  SimplePropertiesSectionReader r0(ssPtr, getSectionSegmentPool());
#ifdef EVENT_DEBUG
  r0.printAll(ndbout);
#endif
  if ((!r0.first()) ||
      (r0.getValueType() != SimpleProperties::StringValue) ||
      (r0.getValueLen() <= 0)) {
    jam();
    releaseSections(handle);

    evntRecPtr.p->m_errorCode = 1;
    evntRecPtr.p->m_errorLine = __LINE__;
    evntRecPtr.p->m_errorNode = reference();

    createEvent_sendReply(signal, evntRecPtr);
    return;
  }

  r0.getString(evntRecPtr.p->m_eventRec.NAME);
  int len = strlen(evntRecPtr.p->m_eventRec.NAME);
  memset(evntRecPtr.p->m_eventRec.NAME+len, 0, MAX_TAB_NAME_SIZE-len);
  
  releaseSections(handle);
  
  Callback c = { safe_cast(&Dbdict::createEventUTIL_PREPARE), 0 };
  
  prepareTransactionEventSysTable(&c, signal, evntRecPtr.i,
				  UtilPrepareReq::Read);
  /* 
   * Will read systable and fill an OpCreateEventPtr
   * and return below
   */
}

void
Dbdict::createEventComplete_RT_USER_GET(Signal* signal,
					OpCreateEventPtr evntRecPtr){
  jam();

  // Send to oneself and the other DICT's
  CreateEvntReq * req = (CreateEvntReq *)signal->getDataPtrSend();
      
  *req = evntRecPtr.p->m_request;
  req->senderRef = reference();
  req->senderData = evntRecPtr.i;
      
  req->addRequestFlag(CreateEvntReq::RT_DICT_AFTER_GET);
      
#ifdef EVENT_PH2_DEBUG
  ndbout_c("DBDICT(Coordinator) sending GSN_CREATE_EVNT_REQ::RT_DICT_AFTER_GET to DBDICT participants evntRecPtr.i = (%d)", evntRecPtr.i);
#endif

  NodeReceiverGroup rg(DBDICT, c_aliveNodes);
  RequestTracker & p = evntRecPtr.p->m_reqTracker;
  if (!p.init<CreateEvntRef>(c_counterMgr, rg, GSN_CREATE_EVNT_REF, 
			     evntRecPtr.i))
  {
    jam();
    evntRecPtr.p->m_errorCode = 701;
    createEvent_sendReply(signal, evntRecPtr);
    return;
  }

  sendSignal(rg, GSN_CREATE_EVNT_REQ, signal, CreateEvntReq::SignalLength, JBB);
}

void
Dbdict::createEvent_nodeFailCallback(Signal* signal, Uint32 eventRecPtrI,
				     Uint32 returnCode){
  OpCreateEventPtr evntRecPtr;
  c_opCreateEvent.getPtr(evntRecPtr, eventRecPtrI);
  createEvent_sendReply(signal, evntRecPtr);
}

void Dbdict::execCREATE_EVNT_REF(Signal* signal) 
{
  jamEntry();      
  EVENT_TRACE;
  CreateEvntRef * const ref = (CreateEvntRef *)signal->getDataPtr();
  OpCreateEventPtr evntRecPtr;

  evntRecPtr.i = ref->getUserData();

  ndbrequire((evntRecPtr.p = c_opCreateEvent.getPtr(evntRecPtr.i)) != NULL);

#ifdef EVENT_PH2_DEBUG
  ndbout_c("DBDICT(Coordinator) got GSN_CREATE_EVNT_REF evntRecPtr.i = (%d)", evntRecPtr.i);
#endif

  if (ref->errorCode == CreateEvntRef::NF_FakeErrorREF){
    jam();
    evntRecPtr.p->m_reqTracker.ignoreRef(c_counterMgr, refToNode(ref->senderRef));
  } else {
    jam();
    evntRecPtr.p->m_reqTracker.reportRef(c_counterMgr, refToNode(ref->senderRef));
  }
  createEvent_sendReply(signal, evntRecPtr);

  return;
}

void Dbdict::execCREATE_EVNT_CONF(Signal* signal)
{
  jamEntry();
  EVENT_TRACE;
  CreateEvntConf * const conf = (CreateEvntConf *)signal->getDataPtr();
  OpCreateEventPtr evntRecPtr;

  evntRecPtr.i = conf->getUserData();

  ndbrequire((evntRecPtr.p = c_opCreateEvent.getPtr(evntRecPtr.i)) != NULL);

#ifdef EVENT_PH2_DEBUG
  ndbout_c("DBDICT(Coordinator) got GSN_CREATE_EVNT_CONF evntRecPtr.i = (%d)", evntRecPtr.i);
#endif

  evntRecPtr.p->m_reqTracker.reportConf(c_counterMgr, refToNode(conf->senderRef));

  // we will only have a valid tablename if it the master DICT sending this
  // but that's ok
  LinearSectionPtr ptr[1];
  ptr[0].p = (Uint32 *)evntRecPtr.p->m_eventRec.TABLE_NAME;
  ptr[0].sz =
    (strlen(evntRecPtr.p->m_eventRec.TABLE_NAME)+4)/4; // to make sure we have a null

  createEvent_sendReply(signal, evntRecPtr, ptr, 1);
    
  return;
}

/************************************************
 *
 * Participant stuff
 *
 */

void
Dbdict::createEvent_RT_DICT_AFTER_GET(Signal* signal, OpCreateEventPtr evntRecPtr){
  DBUG_ENTER("Dbdict::createEvent_RT_DICT_AFTER_GET");
  jam();
  evntRecPtr.p->m_request.setUserRef(signal->senderBlockRef());
  
#ifdef EVENT_PH2_DEBUG
  ndbout_c("DBDICT(Participant) got CREATE_EVNT_REQ::RT_DICT_AFTER_GET evntRecPtr.i = (%d)", evntRecPtr.i);
#endif

  // the signal comes from the DICT block that got the first user request!
  // This code runs on all DICT nodes, including oneself

  // Seize a Create Event record, the Coordinator will now have two seized
  // but that's ok, it's like a recursion

  CRASH_INSERTION2(6009, getOwnNodeId() != c_masterNodeId);

  SubCreateReq * sumaReq = (SubCreateReq *)signal->getDataPtrSend();
  
  sumaReq->senderRef        = reference(); // reference to DICT
  sumaReq->senderData       = evntRecPtr.i;
  sumaReq->subscriptionId   = evntRecPtr.p->m_request.getEventId();
  sumaReq->subscriptionKey  = evntRecPtr.p->m_request.getEventKey();
  sumaReq->subscriptionType = SubCreateReq::TableEvent;
  if (evntRecPtr.p->m_request.getReportAll())
    sumaReq->subscriptionType|= SubCreateReq::ReportAll;
  if (evntRecPtr.p->m_request.getReportSubscribe())
    sumaReq->subscriptionType|= SubCreateReq::ReportSubscribe;
  sumaReq->tableId          = evntRecPtr.p->m_request.getTableId();
  sumaReq->schemaTransId    = 0;
    
#ifdef EVENT_PH2_DEBUG
  ndbout_c("sending GSN_SUB_CREATE_REQ");
#endif

  sendSignal(SUMA_REF, GSN_SUB_CREATE_REQ, signal,
	     SubCreateReq::SignalLength, JBB);
  DBUG_VOID_RETURN;
}

void Dbdict::execSUB_CREATE_REF(Signal* signal)
{
  jamEntry();
  DBUG_ENTER("Dbdict::execSUB_CREATE_REF");

  SubCreateRef * const ref = (SubCreateRef *)signal->getDataPtr();
  OpCreateEventPtr evntRecPtr;

  evntRecPtr.i = ref->senderData;
  ndbrequire((evntRecPtr.p = c_opCreateEvent.getPtr(evntRecPtr.i)) != NULL);

  if (ref->errorCode == 1415) {
    jam();
    createEvent_sendReply(signal, evntRecPtr);
    DBUG_VOID_RETURN;
  }

  if (ref->errorCode)
  {
    evntRecPtr.p->m_errorCode = ref->errorCode;
    evntRecPtr.p->m_errorLine = __LINE__;
  }
  else
  {
    evntRecPtr.p->m_errorCode = 1;
    evntRecPtr.p->m_errorLine = __LINE__;
  }
  evntRecPtr.p->m_errorNode = reference();

  createEvent_sendReply(signal, evntRecPtr);
  DBUG_VOID_RETURN;
}

void Dbdict::execSUB_CREATE_CONF(Signal* signal)
{
  jamEntry();
  DBUG_ENTER("Dbdict::execSUB_CREATE_CONF");
  EVENT_TRACE;

  SubCreateConf * const sumaConf = (SubCreateConf *)signal->getDataPtr();
  OpCreateEventPtr evntRecPtr;
  evntRecPtr.i = sumaConf->senderData;
  ndbrequire((evntRecPtr.p = c_opCreateEvent.getPtr(evntRecPtr.i)) != NULL);

  createEvent_sendReply(signal, evntRecPtr);

  DBUG_VOID_RETURN;
}

/****************************************************
 *
 * common create reply method
 *
 *******************************************************/

void Dbdict::createEvent_sendReply(Signal* signal,
				   OpCreateEventPtr evntRecPtr,
				   LinearSectionPtr *ptr, int noLSP)
{
  jam();
  EVENT_TRACE;

  // check if we're ready to sent reply
  // if we are the master dict we might be waiting for conf/ref

  if (!evntRecPtr.p->m_reqTracker.done()) {
    jam();
    return; // there's more to come
  }

  if (evntRecPtr.p->m_reqTracker.hasRef()) {
    ptr = NULL; // we don't want to return anything if there's an error
    if (!evntRecPtr.p->hasError()) {
      evntRecPtr.p->m_errorCode = 1;
      evntRecPtr.p->m_errorLine = __LINE__;
      evntRecPtr.p->m_errorNode = reference();
      jam();
    } else 
      jam();
  }

  // reference to API if master DICT
  // else reference to master DICT
  Uint32 senderRef = evntRecPtr.p->m_request.getUserRef();
  Uint32 signalLength;
  Uint32 gsn;

  if (evntRecPtr.p->hasError()) {
    jam();
    EVENT_TRACE;
    CreateEvntRef * ret = (CreateEvntRef *)signal->getDataPtrSend();
    
    ret->setEventId(evntRecPtr.p->m_request.getEventId());
    ret->setEventKey(evntRecPtr.p->m_request.getEventKey());
    ret->setUserData(evntRecPtr.p->m_request.getUserData());
    ret->senderRef = reference();
    ret->setTableId(evntRecPtr.p->m_request.getTableId());
    ret->setTableVersion(evntRecPtr.p->m_request.getTableVersion());
    ret->setEventType(evntRecPtr.p->m_request.getEventType());
    ret->setRequestType(evntRecPtr.p->m_request.getRequestType());

    ret->setErrorCode(evntRecPtr.p->m_errorCode);
    ret->setErrorLine(evntRecPtr.p->m_errorLine);
    ret->setErrorNode(evntRecPtr.p->m_errorNode);

    signalLength = CreateEvntRef::SignalLength;
#ifdef EVENT_PH2_DEBUG
    ndbout_c("DBDICT sending GSN_CREATE_EVNT_REF to evntRecPtr.i = (%d) node = %u ref = %u", evntRecPtr.i, refToNode(senderRef), senderRef);
    ndbout_c("errorCode = %u", evntRecPtr.p->m_errorCode);
    ndbout_c("errorLine = %u", evntRecPtr.p->m_errorLine);
#endif
    gsn = GSN_CREATE_EVNT_REF;

  } else {
    jam();
    EVENT_TRACE;
    CreateEvntConf * evntConf = (CreateEvntConf *)signal->getDataPtrSend();
    
    evntConf->setEventId(evntRecPtr.p->m_request.getEventId());
    evntConf->setEventKey(evntRecPtr.p->m_request.getEventKey());
    evntConf->setUserData(evntRecPtr.p->m_request.getUserData());
    evntConf->senderRef = reference();
    evntConf->setTableId(evntRecPtr.p->m_request.getTableId());
    evntConf->setTableVersion(evntRecPtr.p->m_request.getTableVersion());
    evntConf->setAttrListBitmask(evntRecPtr.p->m_request.getAttrListBitmask());
    evntConf->setEventType(evntRecPtr.p->m_request.getEventType());
    evntConf->setRequestType(evntRecPtr.p->m_request.getRequestType());

    signalLength = CreateEvntConf::SignalLength;
#ifdef EVENT_PH2_DEBUG
    ndbout_c("DBDICT sending GSN_CREATE_EVNT_CONF to evntRecPtr.i = (%d) node = %u ref = %u", evntRecPtr.i, refToNode(senderRef), senderRef);
#endif
    gsn = GSN_CREATE_EVNT_CONF;
  }

  if (ptr) {
    jam();
    sendSignal(senderRef, gsn, signal, signalLength, JBB, ptr, noLSP);
  } else {
    jam();
    sendSignal(senderRef, gsn, signal, signalLength, JBB);
  }

  c_opCreateEvent.release(evntRecPtr);
}

/*************************************************************/

/********************************************************************
 *
 * Start event
 *
 *******************************************************************/

void Dbdict::execSUB_START_REQ(Signal* signal)
{
  jamEntry();

  Uint32 origSenderRef = signal->senderBlockRef();

  if (refToBlock(origSenderRef) != DBDICT &&
      getOwnNodeId() != c_masterNodeId)
  {
    /*
     * Coordinator but not master
     */
    SubStartRef * ref = (SubStartRef *)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->errorCode = SubStartRef::NotMaster;
    ref->m_masterNodeId = c_masterNodeId;
    sendSignal(origSenderRef, GSN_SUB_START_REF, signal,
	       SubStartRef::SignalLength2, JBB);
    return;
  }
  OpSubEventPtr subbPtr;
  Uint32 errCode = 0;

  if (!c_opSubEvent.seize(subbPtr)) {
    errCode = SubStartRef::Busy;
busy:
    jam();
    SubStartRef * ref = (SubStartRef *)signal->getDataPtrSend();

    { // fix
      Uint32 subcriberRef = ((SubStartReq*)signal->getDataPtr())->subscriberRef;
      ref->subscriberRef = subcriberRef;
    }
    jam();
    //      ret->setErrorCode(SubStartRef::SeizeError);
    //      ret->setErrorLine(__LINE__);
    //      ret->setErrorNode(reference());
    ref->senderRef = reference();
    ref->errorCode = errCode;

    sendSignal(origSenderRef, GSN_SUB_START_REF, signal,
	       SubStartRef::SignalLength2, JBB);
    return;
  }

  {
    const SubStartReq* req = (SubStartReq*) signal->getDataPtr();
    subbPtr.p->m_senderRef = req->senderRef;
    subbPtr.p->m_senderData = req->senderData;
    subbPtr.p->m_errorCode = 0;
  }
  
  if (refToBlock(origSenderRef) != DBDICT) {
    /*
     * Coordinator
     */
    jam();
    
    subbPtr.p->m_senderRef = origSenderRef; // not sure if API sets correctly
    NodeReceiverGroup rg(DBDICT, c_aliveNodes);

    RequestTracker & p = subbPtr.p->m_reqTracker;
    if (!p.init<SubStartRef>(c_counterMgr, rg, GSN_SUB_START_REF, subbPtr.i))
    {
      c_opSubEvent.release(subbPtr);
      errCode = SubStartRef::Busy;
      goto busy;
    }
    
    SubStartReq* req = (SubStartReq*) signal->getDataPtrSend();
    
    req->senderRef  = reference();
    req->senderData = subbPtr.i;
    
#ifdef EVENT_PH3_DEBUG
    ndbout_c("DBDICT(Coordinator) sending GSN_SUB_START_REQ to DBDICT participants subbPtr.i = (%d)", subbPtr.i);
#endif

    if (ERROR_INSERTED(6011))
    {
      ndbout_c("sending delayed to self...");
      if (ERROR_INSERTED(6011))
      {
        rg.m_nodes.clear(getOwnNodeId());
      }
      sendSignal(rg, GSN_SUB_START_REQ, signal,
                 SubStartReq::SignalLength2, JBB);
      sendSignalWithDelay(reference(),
                          GSN_SUB_START_REQ,
                          signal, 5000, SubStartReq::SignalLength2);
    }
    else
    {
      sendSignal(rg, GSN_SUB_START_REQ, signal,
                 SubStartReq::SignalLength2, JBB);
    }
    return;
  }
  /*
   * Participant
   */
  ndbrequire(refToBlock(origSenderRef) == DBDICT);

  CRASH_INSERTION(6007);
  
  {
    SubStartReq* req = (SubStartReq*) signal->getDataPtrSend();
    
    req->senderRef = reference();
    req->senderData = subbPtr.i;
    
#ifdef EVENT_PH3_DEBUG
    ndbout_c("DBDICT(Participant) sending GSN_SUB_START_REQ to SUMA subbPtr.i = (%d)", subbPtr.i);
#endif
    sendSignal(SUMA_REF, GSN_SUB_START_REQ, signal, SubStartReq::SignalLength2, JBB);
  }
}

void Dbdict::execSUB_START_REF(Signal* signal)
{
  jamEntry();

  const SubStartRef* ref = (SubStartRef*) signal->getDataPtr();
  Uint32 senderRef  = ref->senderRef;
  Uint32 err = ref->errorCode;

  OpSubEventPtr subbPtr;
  c_opSubEvent.getPtr(subbPtr, ref->senderData);

  if (refToBlock(senderRef) == SUMA) {
    /*
     * Participant
     */
    jam();

#ifdef EVENT_PH3_DEBUG
    ndbout_c("DBDICT(Participant) got GSN_SUB_START_REF = (%d)", subbPtr.i);
#endif

    jam();
    SubStartRef* ref = (SubStartRef*) signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->senderData = subbPtr.p->m_senderData;
    ref->errorCode = err;
    sendSignal(subbPtr.p->m_senderRef, GSN_SUB_START_REF,
	       signal, SubStartRef::SignalLength2, JBB);
    c_opSubEvent.release(subbPtr);
    return;
  }
  /*
   * Coordinator
   */
  ndbrequire(refToBlock(senderRef) == DBDICT);
#ifdef EVENT_PH3_DEBUG
  ndbout_c("DBDICT(Coordinator) got GSN_SUB_START_REF = (%d)", subbPtr.i);
#endif
  if (err == SubStartRef::NF_FakeErrorREF){
    jam();
    subbPtr.p->m_reqTracker.ignoreRef(c_counterMgr, refToNode(senderRef));
  } else {
    jam();
    if (subbPtr.p->m_errorCode == 0)
    {
      subbPtr.p->m_errorCode= err ? err : 1;
    }
    subbPtr.p->m_reqTracker.reportRef(c_counterMgr, refToNode(senderRef));
  }
  completeSubStartReq(signal,subbPtr.i,0);
}

void Dbdict::execSUB_START_CONF(Signal* signal)
{
  jamEntry();

  const SubStartConf* conf = (SubStartConf*) signal->getDataPtr();
  Uint32 senderRef  = conf->senderRef;

  OpSubEventPtr subbPtr;
  c_opSubEvent.getPtr(subbPtr, conf->senderData);

  if (refToBlock(senderRef) == SUMA) {
    /*
     * Participant
     */
    jam();
    SubStartConf* conf = (SubStartConf*) signal->getDataPtrSend();

#ifdef EVENT_PH3_DEBUG
  ndbout_c("DBDICT(Participant) got GSN_SUB_START_CONF = (%d)", subbPtr.i);
#endif

    conf->senderRef = reference();
    conf->senderData = subbPtr.p->m_senderData;

    sendSignal(subbPtr.p->m_senderRef, GSN_SUB_START_CONF,
	       signal, SubStartConf::SignalLength2, JBB);
    c_opSubEvent.release(subbPtr);
    return;
  }
  /*
   * Coordinator
   */
  ndbrequire(refToBlock(senderRef) == DBDICT);
#ifdef EVENT_PH3_DEBUG
  ndbout_c("DBDICT(Coordinator) got GSN_SUB_START_CONF = (%d)", subbPtr.i);
#endif
  subbPtr.p->m_sub_start_conf = *conf;
  subbPtr.p->m_reqTracker.reportConf(c_counterMgr, refToNode(senderRef));
  completeSubStartReq(signal,subbPtr.i,0);
}

/*
 * Coordinator
 */
void Dbdict::completeSubStartReq(Signal* signal,
				 Uint32 ptrI,
				 Uint32 returnCode){
  jam();

  OpSubEventPtr subbPtr;
  c_opSubEvent.getPtr(subbPtr, ptrI);

  if (!subbPtr.p->m_reqTracker.done()){
    jam();
    return;
  }

  if (subbPtr.p->m_reqTracker.hasRef()) {
    jam();
#ifdef EVENT_DEBUG
    ndbout_c("SUB_START_REF");
#endif
    SubStartRef * ref = (SubStartRef *)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->errorCode = subbPtr.p->m_errorCode;
    sendSignal(subbPtr.p->m_senderRef, GSN_SUB_START_REF,
	       signal, SubStartRef::SignalLength, JBB);
    if (subbPtr.p->m_reqTracker.hasConf()) {
      //  stopStartedNodes(signal);
    }
    c_opSubEvent.release(subbPtr);
    return;
  }
#ifdef EVENT_DEBUG
  ndbout_c("SUB_START_CONF");
#endif
  
  SubStartConf* conf = (SubStartConf*)signal->getDataPtrSend();
  * conf = subbPtr.p->m_sub_start_conf;
  sendSignal(subbPtr.p->m_senderRef, GSN_SUB_START_CONF,
	     signal, SubStartConf::SignalLength, JBB);
  c_opSubEvent.release(subbPtr);
}

/********************************************************************
 *
 * Stop event
 *
 *******************************************************************/

void Dbdict::execSUB_STOP_REQ(Signal* signal)
{
  jamEntry();

  Uint32 origSenderRef = signal->senderBlockRef();

  if (refToBlock(origSenderRef) != DBDICT &&
      getOwnNodeId() != c_masterNodeId)
  {
    /*
     * Coordinator but not master
     */
    SubStopRef * ref = (SubStopRef *)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->errorCode = SubStopRef::NotMaster;
    ref->m_masterNodeId = c_masterNodeId;
    sendSignal(origSenderRef, GSN_SUB_STOP_REF, signal,
	       SubStopRef::SignalLength2, JBB);
    return;
  }
  OpSubEventPtr subbPtr;
  Uint32 errCode = 0;
  if (!c_opSubEvent.seize(subbPtr)) {
    errCode = SubStopRef::Busy;
busy:
    SubStopRef * ref = (SubStopRef *)signal->getDataPtrSend();
    jam();
    //      ret->setErrorCode(SubStartRef::SeizeError);
    //      ret->setErrorLine(__LINE__);
    //      ret->setErrorNode(reference());
    ref->senderRef = reference();
    ref->errorCode = errCode;

    sendSignal(origSenderRef, GSN_SUB_STOP_REF, signal,
	       SubStopRef::SignalLength, JBB);
    return;
  }

  {
    const SubStopReq* req = (SubStopReq*) signal->getDataPtr();
    subbPtr.p->m_senderRef = req->senderRef;
    subbPtr.p->m_senderData = req->senderData;
    subbPtr.p->m_errorCode = 0;
  }
  
  if (refToBlock(origSenderRef) != DBDICT) {
    /*
     * Coordinator
     */
    jam();
#ifdef EVENT_DEBUG
    ndbout_c("SUB_STOP_REQ 1");
#endif
    subbPtr.p->m_senderRef = origSenderRef; // not sure if API sets correctly
    NodeReceiverGroup rg(DBDICT, c_aliveNodes);
    RequestTracker & p = subbPtr.p->m_reqTracker;
    if (!p.init<SubStopRef>(c_counterMgr, rg, GSN_SUB_STOP_REF, subbPtr.i))
    {
      jam();
      c_opSubEvent.release(subbPtr);
      errCode = SubStopRef::Busy;
      goto busy;
    }
    
    SubStopReq* req = (SubStopReq*) signal->getDataPtrSend();
    
    req->senderRef  = reference();
    req->senderData = subbPtr.i;
    
    sendSignal(rg, GSN_SUB_STOP_REQ, signal, SubStopReq::SignalLength, JBB);
    return;
  }
  /*
   * Participant
   */
#ifdef EVENT_DEBUG
  ndbout_c("SUB_STOP_REQ 2");
#endif
  ndbrequire(refToBlock(origSenderRef) == DBDICT);

  CRASH_INSERTION(6008);

  {
    SubStopReq* req = (SubStopReq*) signal->getDataPtrSend();
    
    req->senderRef = reference();
    req->senderData = subbPtr.i;
    
    sendSignal(SUMA_REF, GSN_SUB_STOP_REQ, signal, SubStopReq::SignalLength, JBB);
  }
}

void Dbdict::execSUB_STOP_REF(Signal* signal)
{
  jamEntry();
  const SubStopRef* ref = (SubStopRef*) signal->getDataPtr();
  Uint32 senderRef  = ref->senderRef;
  Uint32 err = ref->errorCode;

  OpSubEventPtr subbPtr;
  c_opSubEvent.getPtr(subbPtr, ref->senderData);

  if (refToBlock(senderRef) == SUMA) {
    /*
     * Participant
     */
    jam();
    SubStopRef* ref = (SubStopRef*) signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->senderData = subbPtr.p->m_senderData;
    ref->errorCode = err;
    sendSignal(subbPtr.p->m_senderRef, GSN_SUB_STOP_REF,
	       signal, SubStopRef::SignalLength, JBB);
    c_opSubEvent.release(subbPtr);
    return;
  }
  /*
   * Coordinator
   */
  ndbrequire(refToBlock(senderRef) == DBDICT);
  if (err == SubStopRef::NF_FakeErrorREF){
    jam();
    subbPtr.p->m_reqTracker.ignoreRef(c_counterMgr, refToNode(senderRef));
  } else {
    jam();
    if (subbPtr.p->m_errorCode == 0)
    {
      subbPtr.p->m_errorCode= err ? err : 1;
    }
    subbPtr.p->m_reqTracker.reportRef(c_counterMgr, refToNode(senderRef));
  }
  completeSubStopReq(signal,subbPtr.i,0);
}

void Dbdict::execSUB_STOP_CONF(Signal* signal)
{
  jamEntry();

  const SubStopConf* conf = (SubStopConf*) signal->getDataPtr();
  Uint32 senderRef  = conf->senderRef;

  OpSubEventPtr subbPtr;
  c_opSubEvent.getPtr(subbPtr, conf->senderData);

  if (refToBlock(senderRef) == SUMA) {
    /*
     * Participant
     */
    jam();
    SubStopConf* conf = (SubStopConf*) signal->getDataPtrSend();

    conf->senderRef = reference();
    conf->senderData = subbPtr.p->m_senderData;

    sendSignal(subbPtr.p->m_senderRef, GSN_SUB_STOP_CONF,
	       signal, SubStopConf::SignalLength, JBB);
    c_opSubEvent.release(subbPtr);
    return;
  }
  /*
   * Coordinator
   */
  ndbrequire(refToBlock(senderRef) == DBDICT);
  subbPtr.p->m_sub_stop_conf = *conf;
  subbPtr.p->m_reqTracker.reportConf(c_counterMgr, refToNode(senderRef));
  completeSubStopReq(signal,subbPtr.i,0);
}

/*
 * Coordinator
 */
void Dbdict::completeSubStopReq(Signal* signal,
				Uint32 ptrI,
				Uint32 returnCode){
  OpSubEventPtr subbPtr;
  c_opSubEvent.getPtr(subbPtr, ptrI);

  if (!subbPtr.p->m_reqTracker.done()){
    jam();
    return;
  }

  if (subbPtr.p->m_reqTracker.hasRef()) {
    jam();
#ifdef EVENT_DEBUG
    ndbout_c("SUB_STOP_REF");
#endif
    SubStopRef* ref = (SubStopRef*)signal->getDataPtrSend();

    ref->senderRef  = reference();
    ref->senderData = subbPtr.p->m_senderData;
    ref->errorCode  = subbPtr.p->m_errorCode;

    sendSignal(subbPtr.p->m_senderRef, GSN_SUB_STOP_REF,
	       signal, SubStopRef::SignalLength, JBB);
    if (subbPtr.p->m_reqTracker.hasConf()) {
      //  stopStartedNodes(signal);
    }
    c_opSubEvent.release(subbPtr);
    return;
  }
#ifdef EVENT_DEBUG
  ndbout_c("SUB_STOP_CONF");
#endif
  SubStopConf* conf = (SubStopConf*)signal->getDataPtrSend();
  * conf = subbPtr.p->m_sub_stop_conf;
  sendSignal(subbPtr.p->m_senderRef, GSN_SUB_STOP_CONF,
	     signal, SubStopConf::SignalLength, JBB);
  c_opSubEvent.release(subbPtr);
}

/***************************************************************
 * MODULE: Drop event.
 *
 * Drop event.
 * 
 * TODO
 */

void
Dbdict::execDROP_EVNT_REQ(Signal* signal)
{
  jamEntry();
  DBUG_ENTER("Dbdict::execDROP_EVNT_REQ");

  DropEvntReq *req = (DropEvntReq*)signal->getDataPtr();
  const Uint32 senderRef = signal->senderBlockRef();
  OpDropEventPtr evntRecPtr;
  SectionHandle handle(this, signal);

  if (refToBlock(senderRef) != DBDICT &&
      getOwnNodeId() != c_masterNodeId)
  {
    jam();
    releaseSections(handle);

    DropEvntRef * ref = (DropEvntRef *)signal->getDataPtrSend();
    ref->setUserRef(reference());
    ref->setErrorCode(DropEvntRef::NotMaster);
    ref->setErrorLine(__LINE__);
    ref->setErrorNode(reference());
    ref->setMasterNode(c_masterNodeId);
    sendSignal(senderRef, GSN_DROP_EVNT_REF, signal,
	       DropEvntRef::SignalLength2, JBB);
    return;
  }

  // Seize a Create Event record
  if (!c_opDropEvent.seize(evntRecPtr)) {
    // Failed to allocate event record
    jam();
    releaseSections(handle);
 
    DropEvntRef * ret = (DropEvntRef *)signal->getDataPtrSend();
    ret->setErrorCode(747);
    ret->setErrorLine(__LINE__);
    ret->setErrorNode(reference());
    sendSignal(senderRef, GSN_DROP_EVNT_REF, signal,
	       DropEvntRef::SignalLength, JBB);
    DBUG_VOID_RETURN;
  }

#ifdef EVENT_DEBUG
  ndbout_c("DBDICT::execDROP_EVNT_REQ evntRecId = (%d)", evntRecPtr.i);
#endif

  OpDropEvent* evntRec = evntRecPtr.p;
  evntRec->init(req);

  SegmentedSectionPtr ssPtr;

  handle.getSection(ssPtr, 0);

  SimplePropertiesSectionReader r0(ssPtr, getSectionSegmentPool());
#ifdef EVENT_DEBUG
  r0.printAll(ndbout);
#endif
  // event name
  if ((!r0.first()) ||
      (r0.getValueType() != SimpleProperties::StringValue) ||
      (r0.getValueLen() <= 0)) {
    jam();
    releaseSections(handle);

    evntRecPtr.p->m_errorCode = 1;
    evntRecPtr.p->m_errorLine = __LINE__;
    evntRecPtr.p->m_errorNode = reference();

    dropEvent_sendReply(signal, evntRecPtr);
    DBUG_VOID_RETURN;
  }
  r0.getString(evntRecPtr.p->m_eventRec.NAME);
  {
    int len = strlen(evntRecPtr.p->m_eventRec.NAME);
    memset(evntRecPtr.p->m_eventRec.NAME+len, 0, MAX_TAB_NAME_SIZE-len);
#ifdef EVENT_DEBUG
    printf("DropEvntReq; EventName %s, len %u\n",
	   evntRecPtr.p->m_eventRec.NAME, len);
    for(int i = 0; i < MAX_TAB_NAME_SIZE/4; i++)
      printf("H'%.8x ", ((Uint32*)evntRecPtr.p->m_eventRec.NAME)[i]);
    printf("\n");
#endif
  }
  
  releaseSections(handle);

  Callback c = { safe_cast(&Dbdict::dropEventUTIL_PREPARE_READ), 0 };

  prepareTransactionEventSysTable(&c, signal, evntRecPtr.i,
				  UtilPrepareReq::Read);
  DBUG_VOID_RETURN;
}

void 
Dbdict::dropEventUTIL_PREPARE_READ(Signal* signal,
				   Uint32 callbackData,
				   Uint32 returnCode)
{
  jam();
  EVENT_TRACE;
  if (returnCode != 0) {
    EVENT_TRACE;
    dropEventUtilPrepareRef(signal, callbackData, returnCode);
    return;
  }

  UtilPrepareConf* const req = (UtilPrepareConf*)signal->getDataPtr();
  OpDropEventPtr evntRecPtr;
  evntRecPtr.i = req->getSenderData();
  const Uint32 prepareId = req->getPrepareId();

  ndbrequire((evntRecPtr.p = c_opDropEvent.getPtr(evntRecPtr.i)) != NULL);

  Callback c = { safe_cast(&Dbdict::dropEventUTIL_EXECUTE_READ), 0 };

  executeTransEventSysTable(&c, signal,
			    evntRecPtr.i, evntRecPtr.p->m_eventRec,
			    prepareId, UtilPrepareReq::Read);
}

void 
Dbdict::dropEventUTIL_EXECUTE_READ(Signal* signal,
				   Uint32 callbackData,
				   Uint32 returnCode)
{
  jam();
  EVENT_TRACE;
  if (returnCode != 0) {
    EVENT_TRACE;
    dropEventUtilExecuteRef(signal, callbackData, returnCode);
    return;
  }

  OpDropEventPtr evntRecPtr;
  UtilExecuteConf * const ref = (UtilExecuteConf *)signal->getDataPtr();
  jam();
  evntRecPtr.i = ref->getSenderData();
  ndbrequire((evntRecPtr.p = c_opDropEvent.getPtr(evntRecPtr.i)) != NULL);

  parseReadEventSys(signal, evntRecPtr.p->m_eventRec);

  NodeReceiverGroup rg(DBDICT, c_aliveNodes);
  RequestTracker & p = evntRecPtr.p->m_reqTracker;
  if (!p.init<SubRemoveRef>(c_counterMgr, rg, GSN_SUB_REMOVE_REF,
			    evntRecPtr.i))
  {
    evntRecPtr.p->m_errorCode = 701;
    dropEvent_sendReply(signal, evntRecPtr);
    return;
  }
  
  SubRemoveReq* req = (SubRemoveReq*) signal->getDataPtrSend();

  req->senderRef       = reference();
  req->senderData      = evntRecPtr.i;
  req->subscriptionId  = evntRecPtr.p->m_eventRec.SUBID;
  req->subscriptionKey = evntRecPtr.p->m_eventRec.SUBKEY;

  sendSignal(rg, GSN_SUB_REMOVE_REQ, signal, SubRemoveReq::SignalLength, JBB);
}

/*
 * Participant
 */

void
Dbdict::execSUB_REMOVE_REQ(Signal* signal)
{
  jamEntry();
  DBUG_ENTER("Dbdict::execSUB_REMOVE_REQ");

  Uint32 origSenderRef = signal->senderBlockRef();

  OpSubEventPtr subbPtr;
  if (!c_opSubEvent.seize(subbPtr)) {
    SubRemoveRef * ref = (SubRemoveRef *)signal->getDataPtrSend();
    jam();
    ref->senderRef = reference();
    ref->errorCode = SubRemoveRef::Busy;

    sendSignal(origSenderRef, GSN_SUB_REMOVE_REF, signal,
	       SubRemoveRef::SignalLength, JBB);
    DBUG_VOID_RETURN;
  }

  {
    const SubRemoveReq* req = (SubRemoveReq*) signal->getDataPtr();
    subbPtr.p->m_senderRef = req->senderRef;
    subbPtr.p->m_senderData = req->senderData;
    subbPtr.p->m_errorCode = 0;
  }

  CRASH_INSERTION2(6010, getOwnNodeId() != c_masterNodeId);
  
  SubRemoveReq* req = (SubRemoveReq*) signal->getDataPtrSend();
  req->senderRef = reference();
  req->senderData = subbPtr.i;

  sendSignal(SUMA_REF, GSN_SUB_REMOVE_REQ, signal, SubRemoveReq::SignalLength, JBB);
  DBUG_VOID_RETURN;
}

/*
 * Coordintor/Participant
 */

void
Dbdict::execSUB_REMOVE_REF(Signal* signal)
{
  jamEntry();
  DBUG_ENTER("Dbdict::execSUB_REMOVE_REF");

  const SubRemoveRef* ref = (SubRemoveRef*) signal->getDataPtr();
  Uint32 senderRef = ref->senderRef;
  Uint32 err= ref->errorCode;

  if (refToBlock(senderRef) == SUMA) {
    /*
     * Participant
     */
    jam();
    OpSubEventPtr subbPtr;
    c_opSubEvent.getPtr(subbPtr, ref->senderData);
    if (err == 1407) {
      // conf this since this may occur if a nodefailure has occured
      // earlier so that the systable was not cleared
      SubRemoveConf* conf = (SubRemoveConf*) signal->getDataPtrSend();
      conf->senderRef  = reference();
      conf->senderData = subbPtr.p->m_senderData;
      sendSignal(subbPtr.p->m_senderRef, GSN_SUB_REMOVE_CONF,
		 signal, SubRemoveConf::SignalLength, JBB);
    } else {
      SubRemoveRef* ref = (SubRemoveRef*) signal->getDataPtrSend();
      ref->senderRef = reference();
      ref->senderData = subbPtr.p->m_senderData;
      ref->errorCode = err;
      sendSignal(subbPtr.p->m_senderRef, GSN_SUB_REMOVE_REF,
		 signal, SubRemoveRef::SignalLength, JBB);
    }
    c_opSubEvent.release(subbPtr);
    DBUG_VOID_RETURN;
  }
  /*
   * Coordinator
   */
  ndbrequire(refToBlock(senderRef) == DBDICT);
  OpDropEventPtr eventRecPtr;
  c_opDropEvent.getPtr(eventRecPtr, ref->senderData);
  if (err == SubRemoveRef::NF_FakeErrorREF){
    jam();
    eventRecPtr.p->m_reqTracker.ignoreRef(c_counterMgr, refToNode(senderRef));
  } else {
    jam();
    if (eventRecPtr.p->m_errorCode == 0)
    {
      eventRecPtr.p->m_errorCode= err ? err : 1;
      eventRecPtr.p->m_errorLine= __LINE__;
      eventRecPtr.p->m_errorNode= reference();
    }
    eventRecPtr.p->m_reqTracker.reportRef(c_counterMgr, refToNode(senderRef));
  }
  completeSubRemoveReq(signal,eventRecPtr.i,0);
  DBUG_VOID_RETURN;
}

void
Dbdict::execSUB_REMOVE_CONF(Signal* signal)
{
  jamEntry();
  const SubRemoveConf* conf = (SubRemoveConf*) signal->getDataPtr();
  Uint32 senderRef = conf->senderRef;

  if (refToBlock(senderRef) == SUMA) {
    /*
     * Participant
     */
    jam();
    OpSubEventPtr subbPtr;
    c_opSubEvent.getPtr(subbPtr, conf->senderData);
    SubRemoveConf* conf = (SubRemoveConf*) signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->senderData = subbPtr.p->m_senderData;
    sendSignal(subbPtr.p->m_senderRef, GSN_SUB_REMOVE_CONF,
	       signal, SubRemoveConf::SignalLength, JBB);
    c_opSubEvent.release(subbPtr);
    return;
  }
  /*
   * Coordinator
   */
  ndbrequire(refToBlock(senderRef) == DBDICT);
  OpDropEventPtr eventRecPtr;
  c_opDropEvent.getPtr(eventRecPtr, conf->senderData);
  eventRecPtr.p->m_reqTracker.reportConf(c_counterMgr, refToNode(senderRef));
  completeSubRemoveReq(signal,eventRecPtr.i,0);
}

void
Dbdict::completeSubRemoveReq(Signal* signal, Uint32 ptrI, Uint32 xxx)
{
  OpDropEventPtr evntRecPtr;
  c_opDropEvent.getPtr(evntRecPtr, ptrI);

  if (!evntRecPtr.p->m_reqTracker.done()){
    jam();
    return;
  }

  if (evntRecPtr.p->m_reqTracker.hasRef()) {
    jam();
    if ( evntRecPtr.p->m_errorCode == 0 )
    {
      evntRecPtr.p->m_errorNode = reference();
      evntRecPtr.p->m_errorLine = __LINE__;
      evntRecPtr.p->m_errorCode = 1;
    }
    dropEvent_sendReply(signal, evntRecPtr);
    return;
  }

  Callback c = { safe_cast(&Dbdict::dropEventUTIL_PREPARE_DELETE), 0 };

  prepareTransactionEventSysTable(&c, signal, evntRecPtr.i,
				  UtilPrepareReq::Delete);
}

void 
Dbdict::dropEventUTIL_PREPARE_DELETE(Signal* signal,
				     Uint32 callbackData,
				     Uint32 returnCode)
{
  jam();
  EVENT_TRACE;
  if (returnCode != 0) {
    EVENT_TRACE;
    dropEventUtilPrepareRef(signal, callbackData, returnCode);
    return;
  }

  UtilPrepareConf* const req = (UtilPrepareConf*)signal->getDataPtr();
  OpDropEventPtr evntRecPtr;
  jam();
  evntRecPtr.i = req->getSenderData();
  const Uint32 prepareId = req->getPrepareId();
  
  ndbrequire((evntRecPtr.p = c_opDropEvent.getPtr(evntRecPtr.i)) != NULL);
#ifdef EVENT_DEBUG
  printf("DropEvntUTIL_PREPARE; evntRecPtr.i len %u\n",evntRecPtr.i);
#endif    

  Callback c = { safe_cast(&Dbdict::dropEventUTIL_EXECUTE_DELETE), 0 };

  executeTransEventSysTable(&c, signal,
			    evntRecPtr.i, evntRecPtr.p->m_eventRec,
			    prepareId, UtilPrepareReq::Delete);
}

void 
Dbdict::dropEventUTIL_EXECUTE_DELETE(Signal* signal,
				     Uint32 callbackData,
				     Uint32 returnCode)
{
  jam();
  EVENT_TRACE;
  if (returnCode != 0) {
    EVENT_TRACE;
    dropEventUtilExecuteRef(signal, callbackData, returnCode);
    return;
  }

  OpDropEventPtr evntRecPtr;
  UtilExecuteConf * const ref = (UtilExecuteConf *)signal->getDataPtr();
  jam();
  evntRecPtr.i = ref->getSenderData();
  ndbrequire((evntRecPtr.p = c_opDropEvent.getPtr(evntRecPtr.i)) != NULL);

  dropEvent_sendReply(signal, evntRecPtr);
}

void
Dbdict::dropEventUtilPrepareRef(Signal* signal,
				Uint32 callbackData,
				Uint32 returnCode)
{
  jam();
  EVENT_TRACE;
  UtilPrepareRef * const ref = (UtilPrepareRef *)signal->getDataPtr();
  OpDropEventPtr evntRecPtr;
  evntRecPtr.i = ref->getSenderData();
  ndbrequire((evntRecPtr.p = c_opDropEvent.getPtr(evntRecPtr.i)) != NULL);

  interpretUtilPrepareErrorCode((UtilPrepareRef::ErrorCode)ref->getErrorCode(),
				evntRecPtr.p->m_errorCode,
                                evntRecPtr.p->m_errorLine, this);
  evntRecPtr.p->m_errorNode = reference();

  dropEvent_sendReply(signal, evntRecPtr);
}

void
Dbdict::dropEventUtilExecuteRef(Signal* signal,
				Uint32 callbackData,
				Uint32 returnCode)
{
  jam();
  EVENT_TRACE;
  OpDropEventPtr evntRecPtr;
  UtilExecuteRef * const ref = (UtilExecuteRef *)signal->getDataPtr();
  jam();
  evntRecPtr.i = ref->getSenderData();
  ndbrequire((evntRecPtr.p = c_opDropEvent.getPtr(evntRecPtr.i)) != NULL);
    
  evntRecPtr.p->m_errorNode = reference();
  evntRecPtr.p->m_errorLine = __LINE__;

  switch (ref->getErrorCode()) {
  case UtilExecuteRef::TCError:
    switch (ref->getTCErrorCode()) {
    case ZNOT_FOUND:
      jam();
      evntRecPtr.p->m_errorCode = 4710;
      break;
    default:
      jam();
      evntRecPtr.p->m_errorCode = ref->getTCErrorCode();
      break;
    }
    break;
  default:
    jam();
    evntRecPtr.p->m_errorCode = ref->getErrorCode();
    break;
  }
  dropEvent_sendReply(signal, evntRecPtr);
}

void Dbdict::dropEvent_sendReply(Signal* signal,
				 OpDropEventPtr evntRecPtr)
{
  jam();
  EVENT_TRACE;
  Uint32 senderRef = evntRecPtr.p->m_request.getUserRef();

  if (evntRecPtr.p->hasError()) {
    jam();
    DropEvntRef * ret = (DropEvntRef *)signal->getDataPtrSend();
    
    ret->setUserData(evntRecPtr.p->m_request.getUserData());
    ret->setUserRef(evntRecPtr.p->m_request.getUserRef());

    ret->setErrorCode(evntRecPtr.p->m_errorCode);
    ret->setErrorLine(evntRecPtr.p->m_errorLine);
    ret->setErrorNode(evntRecPtr.p->m_errorNode);

    sendSignal(senderRef, GSN_DROP_EVNT_REF, signal,
	       DropEvntRef::SignalLength, JBB);
  } else {
    jam();
    DropEvntConf * evntConf = (DropEvntConf *)signal->getDataPtrSend();
    
    evntConf->setUserData(evntRecPtr.p->m_request.getUserData());
    evntConf->setUserRef(evntRecPtr.p->m_request.getUserRef());

    sendSignal(senderRef, GSN_DROP_EVNT_CONF, signal,
	       DropEvntConf::SignalLength, JBB);
  }

  c_opDropEvent.release(evntRecPtr);
}

// MODULE: CreateTrigger

const Dbdict::OpInfo
Dbdict::CreateTriggerRec::g_opInfo = {
  { 'C', 'T', 'r', 0 },
  GSN_CREATE_TRIG_IMPL_REQ,
  CreateTrigImplReq::SignalLength,
  //
  &Dbdict::createTrigger_seize,
  &Dbdict::createTrigger_release,
  //
  &Dbdict::createTrigger_parse,
  &Dbdict::createTrigger_subOps,
  &Dbdict::createTrigger_reply,
  //
  &Dbdict::createTrigger_prepare,
  &Dbdict::createTrigger_commit,
  //
  &Dbdict::createTrigger_abortParse,
  &Dbdict::createTrigger_abortPrepare
};

bool
Dbdict::createTrigger_seize(SchemaOpPtr op_ptr)
{
  return seizeOpRec<CreateTriggerRec>(op_ptr);
}

void
Dbdict::createTrigger_release(SchemaOpPtr op_ptr)
{
  releaseOpRec<CreateTriggerRec>(op_ptr);
}

void
Dbdict::execCREATE_TRIG_REQ(Signal* signal)
{
  jamEntry();
  if (!assembleFragments(signal)) {
    jam();
    return;
  }
  SectionHandle handle(this, signal);

  const CreateTrigReq req_copy =
    *(const CreateTrigReq*)signal->getDataPtr();
  const CreateTrigReq* req = &req_copy;

  ErrorInfo error;
  do {
    SchemaOpPtr op_ptr;
    CreateTriggerRecPtr createTriggerPtr;
    CreateTrigImplReq* impl_req;

    startClientReq(op_ptr, createTriggerPtr, req, impl_req, error);
    if (hasError(error)) {
      jam();
      break;
    }

    impl_req->tableId = req->tableId;
    impl_req->tableVersion = req->tableVersion;
    impl_req->indexId = req->indexId;
    impl_req->indexVersion = req->indexVersion;
    impl_req->triggerNo = req->triggerNo;
    impl_req->triggerId = req->forceTriggerId;
    impl_req->triggerInfo = req->triggerInfo;
    impl_req->receiverRef = req->receiverRef;
    impl_req->attributeMask = req->attributeMask;

    handleClientReq(signal, op_ptr, handle);
    return;
  } while (0);

  releaseSections(handle);

  CreateTrigRef* ref = (CreateTrigRef*)signal->getDataPtrSend();

  ref->senderRef = reference();
  ref->clientData = req->clientData;
  ref->transId = req->transId;
  ref->tableId = req->tableId;
  ref->indexId = req->indexId;
  ref->triggerInfo = req->triggerInfo;
  getError(error, ref);

  sendSignal(req->clientRef, GSN_CREATE_TRIG_REF, signal,
             CreateTrigRef::SignalLength, JBB);
}

// CreateTrigger: PARSE

void
Dbdict::createTrigger_parse(Signal* signal, bool master,
                            SchemaOpPtr op_ptr,
                            SectionHandle& handle, ErrorInfo& error)
{
  D("createTrigger_parse" << V(op_ptr.i) << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);
  CreateTrigImplReq* impl_req = &createTriggerPtr.p->m_request;

  // check request type
  Uint32 requestType = impl_req->requestType;
  switch (CreateTrigReq::getOnlineFlag(requestType)) {
  case CreateTrigImplReq::CreateTriggerOnline:
    break;
  case CreateTrigImplReq::CreateTriggerOffline:
    jam();
  default:
    ndbassert(false);
    setError(error, CreateTrigRef::BadRequestType, __LINE__);
    return;
  }

  // check the table
  {
    const Uint32 tableId = impl_req->tableId;
    if (! (tableId < c_tableRecordPool.getSize()))
    {
      jam();
      setError(error, CreateTrigRef::InvalidTable, __LINE__);
      return;
    }
    TableRecordPtr tablePtr;
    c_tableRecordPool.getPtr(tablePtr, tableId);
#if wl3600_todo // no PARSE state
    if (tablePtr.p->tabState != TableRecord::PARSE &&
	tablePtr.p->tabState != TableRecord::DEFINED &&
	tablePtr.p->tabState != TableRecord::BACKUP_ONGOING)
    {
      jam();
      setError(error, CreateTrigRef::InvalidTable, __LINE__);
      return;
    }
#endif
  }

  switch(CreateTrigReq::getEndpointFlag(requestType)){
  case CreateTrigReq::MainTrigger:
    jam();
    createTriggerPtr.p->m_main_op = true;
    break;
  case CreateTrigReq::TriggerDst:
    jam();
  case CreateTrigReq::TriggerSrc:
    jam();
    if (handle.m_cnt)
    {
      jam();
      ndbassert(false);
      setError(error, CreateTrigRef::BadRequestType, __LINE__);
      return;
    }
    createTriggerPtr.p->m_main_op = false;
    createTrigger_parse_endpoint(signal, op_ptr, error);
    return;
  }

  SegmentedSectionPtr ss_ptr;
  handle.getSection(ss_ptr, CreateTrigReq::TRIGGER_NAME_SECTION);
  SimplePropertiesSectionReader r(ss_ptr, getSectionSegmentPool());
  DictTabInfo::Table tableDesc;
  tableDesc.init();
  SimpleProperties::UnpackStatus status =
    SimpleProperties::unpack(r, &tableDesc,
			     DictTabInfo::TableMapping,
			     DictTabInfo::TableMappingSize,
			     true, true);

  if (status != SimpleProperties::Eof ||
      tableDesc.TableName[0] == 0)
  {
    jam();
    ndbassert(false);
    setError(error, CreateTrigRef::InvalidName, __LINE__);
    return;
  }
  const Uint32 bytesize = sizeof(createTriggerPtr.p->m_triggerName);
  memcpy(createTriggerPtr.p->m_triggerName, tableDesc.TableName, bytesize);
  D("trigger " << createTriggerPtr.p->m_triggerName);

  // check name is unique
  if (get_object(createTriggerPtr.p->m_triggerName) != 0) {
    jam();
    D("duplicate trigger name " << createTriggerPtr.p->m_triggerName);
    setError(error, CreateTrigRef::TriggerExists, __LINE__);
    return;
  }

  TriggerRecordPtr triggerPtr;
  if (master)
  {
    jam();
    if (impl_req->triggerId == RNIL)
    {
      jam();
      impl_req->triggerId = getFreeTriggerRecord();
      if (impl_req->triggerId == RNIL)
      {
	jam();
	setError(error, CreateTrigRef::TooManyTriggers, __LINE__);
	return;
      }
      c_triggerRecordPool.getPtr(triggerPtr, impl_req->triggerId);
      ndbrequire(triggerPtr.p->triggerState == TriggerRecord::TS_NOT_DEFINED);
      D("master allocated triggerId " << impl_req->triggerId);
    }
    else
    {
      if (!(impl_req->triggerId < c_triggerRecordPool.getSize()))
      {
	jam();
	setError(error, CreateTrigRef::TooManyTriggers, __LINE__);
	return;
      }
      c_triggerRecordPool.getPtr(triggerPtr, impl_req->triggerId);
      if (triggerPtr.p->triggerState != TriggerRecord::TS_NOT_DEFINED)
      {
	jam();
	setError(error, CreateTrigRef::TriggerExists, __LINE__);
	return;
      }
      D("master forced triggerId " << impl_req->triggerId);
    }
  }
  else
  {
    jam();
    // slave receives trigger id from master
    if (! (impl_req->triggerId < c_triggerRecordPool.getSize()))
    {
      jam();
      setError(error, CreateTrigRef::TooManyTriggers, __LINE__);
      return;
    }
    c_triggerRecordPool.getPtr(triggerPtr, impl_req->triggerId);
    if (triggerPtr.p->triggerState != TriggerRecord::TS_NOT_DEFINED)
    {
      jam();
      setError(error, CreateTrigRef::TriggerExists, __LINE__);
      return;
    }
    D("slave allocated triggerId " << hex << impl_req->triggerId);
  }

  initialiseTriggerRecord(triggerPtr);

  triggerPtr.p->triggerId = impl_req->triggerId;
  triggerPtr.p->tableId = impl_req->tableId;
  triggerPtr.p->indexId = RNIL; // feedback method connects to index
  triggerPtr.p->triggerInfo = impl_req->triggerInfo;
  triggerPtr.p->receiverRef = impl_req->receiverRef;
  triggerPtr.p->attributeMask = impl_req->attributeMask;
  triggerPtr.p->triggerState = TriggerRecord::TS_DEFINING;
  {
    Rope name(c_rope_pool, triggerPtr.p->triggerName);
    if (!name.assign(createTriggerPtr.p->m_triggerName)) {
      jam();
      setError(error, CreateTrigRef::OutOfStringBuffer, __LINE__);
      return;
    }
  }

  // connect to new DictObject
  {
    Ptr<DictObject> obj_ptr;
    seizeDictObject(op_ptr, obj_ptr, triggerPtr.p->triggerName);

    obj_ptr.p->m_id = impl_req->triggerId; // wl3600_todo id
    obj_ptr.p->m_type =
      TriggerInfo::getTriggerType(triggerPtr.p->triggerInfo);
    triggerPtr.p->m_obj_ptr_i = obj_ptr.i;
  }

  {
    TriggerType::Value type =
      TriggerInfo::getTriggerType(triggerPtr.p->triggerInfo);
    switch(type){
    case TriggerType::SECONDARY_INDEX:
      jam();
      createTriggerPtr.p->m_sub_dst = false;
      createTriggerPtr.p->m_sub_src = false;
      break;
    case TriggerType::ORDERED_INDEX:
    case TriggerType::READ_ONLY_CONSTRAINT:
      jam();
      createTriggerPtr.p->m_sub_dst = true; // Only need LQH
      createTriggerPtr.p->m_sub_src = false;
      break;
    default:
    case TriggerType::SUBSCRIPTION:
    case TriggerType::SUBSCRIPTION_BEFORE:
      ndbassert(false);
      setError(error, CreateTrigRef::UnsupportedTriggerType, __LINE__);
      return;
    }
  }

  if (ERROR_INSERTED(6124))
  {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    setError(error, 9124, __LINE__);
    return;
  }

  if (impl_req->indexId != RNIL)
  {
    TableRecordPtr indexPtr;
    c_tableRecordPool.getPtr(indexPtr, impl_req->indexId);
    triggerPtr.p->indexId = impl_req->indexId;
    indexPtr.p->triggerId = impl_req->triggerId;
  }
}

void
Dbdict::createTrigger_parse_endpoint(Signal* signal,
				     SchemaOpPtr op_ptr,
				     ErrorInfo& error)
{
  jam();

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);
  CreateTrigImplReq* impl_req = &createTriggerPtr.p->m_request;

  // check request type
  Uint32 requestType = impl_req->requestType;
  switch(CreateTrigReq::getEndpointFlag(requestType)){
  case CreateTrigReq::TriggerDst:
    jam();
    createTriggerPtr.p->m_block_list[0] = DBTC_REF;
    break;
  case CreateTrigReq::TriggerSrc:
    jam();
    createTriggerPtr.p->m_block_list[0] = DBLQH_REF;
    break;
  default:
    ndbassert(false);
    setError(error, CreateTrigRef::BadRequestType, __LINE__);
    return;
  }

  return;
}

bool
Dbdict::createTrigger_subOps(Signal* signal, SchemaOpPtr op_ptr)
{
  D("createTrigger_subOps" << V(op_ptr.i) << *op_ptr.p);

  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);

  Uint32 requestType =
    DictSignal::getRequestType(createTriggerPtr.p->m_request.requestType);
  switch(CreateTrigReq::getEndpointFlag(requestType)){
  case CreateTrigReq::MainTrigger:
    jam();
    break;
  case CreateTrigReq::TriggerDst:
  case CreateTrigReq::TriggerSrc:
    jam();
    return false;
  }

  /**
   * NOTE create dst before src
   */
  if (!createTriggerPtr.p->m_sub_dst)
  {
    jam();
    createTriggerPtr.p->m_sub_dst = true;
    Callback c = {
      safe_cast(&Dbdict::createTrigger_fromCreateEndpoint),
      op_ptr.p->op_key
    };
    op_ptr.p->m_callback = c;
    createTrigger_toCreateEndpoint(signal, op_ptr,
				   CreateTrigReq::TriggerDst);
    return true;
  }

  if (!createTriggerPtr.p->m_sub_src)
  {
    jam();
    createTriggerPtr.p->m_sub_src = true;
    Callback c = {
      safe_cast(&Dbdict::createTrigger_fromCreateEndpoint),
      op_ptr.p->op_key
    };
    op_ptr.p->m_callback = c;
    createTrigger_toCreateEndpoint(signal, op_ptr,
				   CreateTrigReq::TriggerSrc);
    return true;
  }

  return false;
}

void
Dbdict::createTrigger_toCreateEndpoint(Signal* signal,
				       SchemaOpPtr op_ptr,
				       CreateTrigReq::EndpointFlag endpoint)
{
  D("alterIndex_toCreateTrigger");

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);
  const CreateTrigImplReq* impl_req = &createTriggerPtr.p->m_request;

  CreateTrigReq* req = (CreateTrigReq*)signal->getDataPtrSend();

  Uint32 type = 0;
  CreateTrigReq::setOnlineFlag(type, CreateTrigReq::CreateTriggerOnline);
  CreateTrigReq::setEndpointFlag(type, endpoint);

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, type);
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->tableId = impl_req->tableId;
  req->tableVersion = impl_req->tableVersion;
  req->indexId = impl_req->indexId;
  req->indexVersion = impl_req->indexVersion;
  req->forceTriggerId = impl_req->triggerId;
  req->triggerInfo = impl_req->triggerInfo;

  sendSignal(reference(), GSN_CREATE_TRIG_REQ, signal,
             CreateTrigReq::SignalLength, JBB);
}

void
Dbdict::createTrigger_fromCreateEndpoint(Signal* signal,
					 Uint32 op_key, Uint32 ret)
{
  SchemaOpPtr op_ptr;
  CreateTriggerRecPtr createTriggerPtr;
  findSchemaOp(op_ptr, createTriggerPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  if (ret == 0)
  {
    jam();
    const CreateTrigConf* conf =
      (const CreateTrigConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    createSubOps(signal, op_ptr);
  }
  else
  {
    jam();
    const CreateTrigRef* ref =
      (const CreateTrigRef*)signal->getDataPtr();
    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::createTrigger_reply(Signal* signal, SchemaOpPtr op_ptr, ErrorInfo error)
{
  jam();

  SchemaTransPtr& trans_ptr = op_ptr.p->m_trans_ptr;
  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);
  const CreateTrigImplReq* impl_req = &createTriggerPtr.p->m_request;

  D("createTrigger_reply" << V(impl_req->triggerId) << *op_ptr.p);

  if (!hasError(error)) {
    CreateTrigConf* conf = (CreateTrigConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->clientData = op_ptr.p->m_clientData;
    conf->transId = trans_ptr.p->m_transId;
    conf->tableId = impl_req->tableId;
    conf->indexId = impl_req->indexId;
    conf->triggerId = impl_req->triggerId;
    conf->triggerInfo = impl_req->triggerInfo;

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_CREATE_TRIG_CONF, signal,
               CreateTrigConf::SignalLength, JBB);
  } else {
    jam();
    CreateTrigRef* ref = (CreateTrigRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->clientData = op_ptr.p->m_clientData;
    ref->transId = trans_ptr.p->m_transId;
    ref->tableId = impl_req->tableId;
    ref->indexId = impl_req->indexId;
    ref->triggerInfo =impl_req->triggerInfo;
    getError(error, ref);

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_CREATE_TRIG_REF, signal,
               CreateTrigRef::SignalLength, JBB);
  }
}

// CreateTrigger: PREPARE

void
Dbdict::createTrigger_prepare(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);

  if (createTriggerPtr.p->m_main_op)
  {
    jam();
    sendTransConf(signal, op_ptr);
    return;
  }

  Callback c =  { safe_cast(&Dbdict::createTrigger_prepare_fromLocal),
                  op_ptr.p->op_key
  };

  op_ptr.p->m_callback = c;
  send_create_trig_req(signal, op_ptr);
}

void
Dbdict::createTrigger_prepare_fromLocal(Signal* signal,
                                        Uint32 op_key, Uint32 ret)
{
  SchemaOpPtr op_ptr;
  CreateTriggerRecPtr createTriggerPtr;
  findSchemaOp(op_ptr, createTriggerPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  if (ret == 0)
  {
    jam();
    createTriggerPtr.p->m_created = true;
    sendTransConf(signal, op_ptr);
  }
  else
  {
    jam();
    setError(op_ptr, ret, __LINE__);
    sendTransRef(signal, op_ptr);
  }
}

// CreateTrigger: COMMIT

void
Dbdict::createTrigger_commit(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();

  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);
  const CreateTrigImplReq* impl_req = &createTriggerPtr.p->m_request;

  if (createTriggerPtr.p->m_main_op)
  {
    jam();

    Uint32 triggerId = impl_req->triggerId;
    TriggerRecordPtr triggerPtr;
    c_triggerRecordPool.getPtr(triggerPtr, triggerId);

    triggerPtr.p->triggerState = TriggerRecord::TS_ONLINE;
    unlinkDictObject(op_ptr);
  }

  sendTransConf(signal, op_ptr);
}

// CreateTrigger: ABORT

void
Dbdict::createTrigger_abortParse(Signal* signal, SchemaOpPtr op_ptr)
{
  D("createTrigger_abortParse" << *op_ptr.p);

  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);
  CreateTrigImplReq* impl_req = &createTriggerPtr.p->m_request;
  Uint32 triggerId = impl_req->triggerId;

  if (createTriggerPtr.p->m_main_op)
  {
    jam();

    TriggerRecordPtr triggerPtr;
    c_triggerRecordPool.getPtr(triggerPtr, triggerId);

    if (triggerPtr.p->triggerState == TriggerRecord::TS_DEFINING)
    {
      jam();
      triggerPtr.p->triggerState = TriggerRecord::TS_NOT_DEFINED;
    }

    if (triggerPtr.p->indexId != RNIL)
    {
      TableRecordPtr indexPtr;
      c_tableRecordPool.getPtr(indexPtr, triggerPtr.p->indexId);
      triggerPtr.p->indexId = RNIL;
      indexPtr.p->triggerId = RNIL;
    }

    // ignore Feedback for now (referencing object will be dropped too)

    if (hasDictObject(op_ptr))
    {
      jam();
      releaseDictObject(op_ptr);
    }
  }

  sendTransConf(signal, op_ptr);
}

void
Dbdict::createTrigger_abortPrepare(Signal* signal, SchemaOpPtr op_ptr)
{
  D("createTrigger_abortPrepare" << *op_ptr.p);

  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);
  const CreateTrigImplReq* impl_req = &createTriggerPtr.p->m_request;

  if (createTriggerPtr.p->m_main_op)
  {
    jam();
    sendTransConf(signal, op_ptr);
    return;
  }

  if (createTriggerPtr.p->m_created == false)
  {
    jam();
    sendTransConf(signal, op_ptr);
    return;
  }

  Callback c =  { safe_cast(&Dbdict::createTrigger_abortPrepare_fromLocal),
		  op_ptr.p->op_key
  };
  op_ptr.p->m_callback = c;

  DropTrigImplReq* req = (DropTrigImplReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->requestType = 0;
  req->tableId = impl_req->tableId;
  req->tableVersion = 0; // not used
  req->indexId = impl_req->indexId;
  req->indexVersion = 0; // not used
  req->triggerNo = 0;
  req->triggerId = impl_req->triggerId;
  req->triggerInfo = impl_req->triggerInfo;

  BlockReference ref = createTriggerPtr.p->m_block_list[0];
  sendSignal(ref, GSN_DROP_TRIG_IMPL_REQ, signal,
             DropTrigImplReq::SignalLength, JBB);
}

void
Dbdict::createTrigger_abortPrepare_fromLocal(Signal* signal,
                                             Uint32 op_key, Uint32 ret)
{
  SchemaOpPtr op_ptr;
  CreateTriggerRecPtr createTriggerPtr;
  findSchemaOp(op_ptr, createTriggerPtr, op_key);
  ndbrequire(!op_ptr.isNull());

  ndbrequire(ret == 0); // abort can't fail

  sendTransConf(signal, op_ptr);
}

void
Dbdict::send_create_trig_req(Signal* signal,
                             SchemaOpPtr op_ptr)
{
  CreateTriggerRecPtr createTriggerPtr;
  getOpRec(op_ptr, createTriggerPtr);
  const CreateTrigImplReq* impl_req = &createTriggerPtr.p->m_request;

  TriggerRecordPtr triggerPtr;
  c_triggerRecordPool.getPtr(triggerPtr, impl_req->triggerId);

  D("send_create_trig_req");

  CreateTrigImplReq* req = (CreateTrigImplReq*)signal->getDataPtrSend();

  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->requestType = 0;
  req->tableId = triggerPtr.p->tableId;
  req->tableVersion = 0; // not used
  req->indexId = triggerPtr.p->indexId;
  req->indexVersion = 0; // not used
  req->triggerNo = 0; // not used
  req->triggerId = triggerPtr.p->triggerId;
  req->triggerInfo = triggerPtr.p->triggerInfo;
  req->receiverRef = triggerPtr.p->receiverRef;
  req->attributeMask = triggerPtr.p->attributeMask;

  BlockReference ref = createTriggerPtr.p->m_block_list[0];
  sendSignal(ref, GSN_CREATE_TRIG_IMPL_REQ, signal,
             CreateTrigImplReq::SignalLength, JBB);
}

// CreateTrigger: MISC

void
Dbdict::execCREATE_TRIG_CONF(Signal* signal)
{
  jamEntry();
  const CreateTrigConf* conf = (const CreateTrigConf*)signal->getDataPtr();
  handleDictConf(signal, conf);
}

void
Dbdict::execCREATE_TRIG_REF(Signal* signal)
{
  jamEntry();
  const CreateTrigRef* ref = (const CreateTrigRef*)signal->getDataPtr();
  handleDictRef(signal, ref);
}

void
Dbdict::execCREATE_TRIG_IMPL_CONF(Signal* signal)
{
  jamEntry();
  const CreateTrigImplConf* conf = (const CreateTrigImplConf*)signal->getDataPtr();
  ndbrequire(refToNode(conf->senderRef) == getOwnNodeId());
  handleDictConf(signal, conf);
}

void
Dbdict::execCREATE_TRIG_IMPL_REF(Signal* signal)
{
  jamEntry();
  const CreateTrigImplRef* ref = (const CreateTrigImplRef*)signal->getDataPtr();
  ndbrequire(refToNode(ref->senderRef) == getOwnNodeId());
  handleDictRef(signal, ref);
}

// CreateTrigger: END

// MODULE: DropTrigger

const Dbdict::OpInfo
Dbdict::DropTriggerRec::g_opInfo = {
  { 'D', 'T', 'r', 0 },
  GSN_DROP_TRIG_IMPL_REQ,
  DropTrigImplReq::SignalLength,
  //
  &Dbdict::dropTrigger_seize,
  &Dbdict::dropTrigger_release,
  //
  &Dbdict::dropTrigger_parse,
  &Dbdict::dropTrigger_subOps,
  &Dbdict::dropTrigger_reply,
  //
  &Dbdict::dropTrigger_prepare,
  &Dbdict::dropTrigger_commit,
  //
  &Dbdict::dropTrigger_abortParse,
  &Dbdict::dropTrigger_abortPrepare
};

bool
Dbdict::dropTrigger_seize(SchemaOpPtr op_ptr)
{
  return seizeOpRec<DropTriggerRec>(op_ptr);
}

void
Dbdict::dropTrigger_release(SchemaOpPtr op_ptr)
{
  releaseOpRec<DropTriggerRec>(op_ptr);
}

void
Dbdict::execDROP_TRIG_REQ(Signal* signal)
{
  jamEntry();
  if (!assembleFragments(signal)) {
    jam();
    return;
  }
  SectionHandle handle(this, signal);

  const DropTrigReq req_copy =
    *(const DropTrigReq*)signal->getDataPtr();
  const DropTrigReq* req = &req_copy;

  ErrorInfo error;
  do {
    SchemaOpPtr op_ptr;
    DropTriggerRecPtr dropTriggerPtr;
    DropTrigImplReq* impl_req;

    startClientReq(op_ptr, dropTriggerPtr, req, impl_req, error);
    if (hasError(error)) {
      jam();
      break;
    }

    impl_req->tableId = req->tableId;
    impl_req->tableVersion = req->tableVersion;
    impl_req->indexId = req->indexId;
    impl_req->indexVersion = req->indexVersion;
    impl_req->triggerNo = req->triggerNo;
    impl_req->triggerId = req->triggerId;
    impl_req->triggerInfo = ~(Uint32)0;

    handleClientReq(signal, op_ptr, handle);
    return;
  } while (0);

  releaseSections(handle);

  DropTrigRef* ref = (DropTrigRef*)signal->getDataPtrSend();

  ref->senderRef = reference();
  ref->clientData = req->clientData;
  ref->transId = req->transId;
  ref->tableId = req->tableId;
  ref->indexId = req->indexId;
  getError(error, ref);

  sendSignal(req->clientRef, GSN_DROP_TRIG_REF, signal,
             DropTrigRef::SignalLength, JBB);
}

// DropTrigger: PARSE

void
Dbdict::dropTrigger_parse(Signal* signal, bool master,
                          SchemaOpPtr op_ptr,
                          SectionHandle& handle, ErrorInfo& error)
{
  D("dropTrigger_parse" << V(op_ptr.i) << *op_ptr.p);

  DropTriggerRecPtr dropTriggerPtr;
  getOpRec(op_ptr, dropTriggerPtr);
  DropTrigImplReq* impl_req = &dropTriggerPtr.p->m_request;

  bool reqByName = false;
  if (handle.m_cnt > 0) { // wl3600_todo use requestType
    jam();
    ndbrequire(handle.m_cnt == 1);
    reqByName = true;
  }

  if (reqByName) {
    jam();
    SegmentedSectionPtr ss_ptr;
    handle.getSection(ss_ptr, DropTrigImplReq::TRIGGER_NAME_SECTION);
    SimplePropertiesSectionReader r(ss_ptr, getSectionSegmentPool());
    DictTabInfo::Table tableDesc;
    tableDesc.init();
    SimpleProperties::UnpackStatus status =
      SimpleProperties::unpack(
          r, &tableDesc,
          DictTabInfo::TableMapping, DictTabInfo::TableMappingSize,
          true, true);

    if (status != SimpleProperties::Eof ||
        tableDesc.TableName[0] == 0)
    {
      jam();
      ndbassert(false);
      setError(error, DropTrigRef::InvalidName, __LINE__);
      return;
    }
    const Uint32 bytesize = sizeof(dropTriggerPtr.p->m_triggerName);
    memcpy(dropTriggerPtr.p->m_triggerName, tableDesc.TableName, bytesize);
    D("parsed trigger name: " << dropTriggerPtr.p->m_triggerName);

    // find object by name and link it to operation
    DictObjectPtr obj_ptr;
    if (!findDictObject(op_ptr, obj_ptr, dropTriggerPtr.p->m_triggerName)) {
      jam();
      setError(error, DropTrigRef::TriggerNotFound, __LINE__);
      return;
    }
    if (impl_req->triggerId != RNIL &&
        impl_req->triggerId != obj_ptr.p->m_id) {
      jam();
      // inconsistency in request
      setError(error, DropTrigRef::TriggerNotFound, __LINE__);
      return;
    }
    impl_req->triggerId = obj_ptr.p->m_id;
  }

  // check trigger id from user or via name
  TriggerRecordPtr triggerPtr;
  {
    if (!(impl_req->triggerId < c_triggerRecordPool.getSize())) {
      jam();
      setError(error, DropTrigImplRef::TriggerNotFound, __LINE__);
      return;
    }
    c_triggerRecordPool.getPtr(triggerPtr, impl_req->triggerId);
    // wl3600_todo state check
  }
  
  D("trigger " << copyRope<MAX_TAB_NAME_SIZE>(triggerPtr.p->triggerName));
  impl_req->triggerInfo = triggerPtr.p->triggerInfo;
  Uint32 requestType = impl_req->requestType;

  switch(DropTrigReq::getEndpointFlag(requestType)){
  case DropTrigReq::MainTrigger:
    jam();
    dropTriggerPtr.p->m_main_op = true;
    break;
  case DropTrigReq::TriggerDst:
  case DropTrigReq::TriggerSrc:
    jam();
    if (handle.m_cnt)
    {
      jam();
      ndbassert(false);
      setError(error, DropTrigRef::BadRequestType, __LINE__);
      return;
    }
    dropTriggerPtr.p->m_main_op = false;
    dropTrigger_parse_endpoint(signal, op_ptr, error);
    return;
  }

  // connect to object in request by id
  if (!reqByName) {
    jam();
    DictObjectPtr obj_ptr;
    if (!findDictObject(op_ptr, obj_ptr, triggerPtr.p->m_obj_ptr_i)) {
      jam();
      // broken trigger object wl3600_todo bad error code
      setError(error, DropTrigRef::TriggerNotFound, __LINE__);
      return;
    }
    ndbrequire(obj_ptr.p->m_id == triggerPtr.p->triggerId);

    // fill in name just to be complete
    ConstRope name(c_rope_pool, triggerPtr.p->triggerName);
    name.copy(dropTriggerPtr.p->m_triggerName); //wl3600_todo length check
  }

  // check the table (must match trigger record)
  {
    if (impl_req->tableId != triggerPtr.p->tableId) {
      jam();
      setError(error, DropTrigRef::InvalidTable, __LINE__);
      return;
    }
  }

  // check the index (must match trigger record, maybe both RNIL)
  {
    if (impl_req->indexId != triggerPtr.p->indexId) {
      jam();  // wl3600_todo wrong error code
      setError(error, DropTrigRef::InvalidTable, __LINE__);
      return;
    }
  }

  {
    TriggerType::Value type =
      TriggerInfo::getTriggerType(triggerPtr.p->triggerInfo);
    switch(type){
    case TriggerType::SECONDARY_INDEX:
      jam();
      dropTriggerPtr.p->m_sub_dst = false;
      dropTriggerPtr.p->m_sub_src = false;
      break;
    case TriggerType::ORDERED_INDEX:
    case TriggerType::READ_ONLY_CONSTRAINT:
      jam();
      dropTriggerPtr.p->m_sub_dst = true; // Only need LQH
      dropTriggerPtr.p->m_sub_src = false;
      break;
    default:
    case TriggerType::SUBSCRIPTION:
    case TriggerType::SUBSCRIPTION_BEFORE:
      ndbassert(false);
      setError(error, DropTrigRef::UnsupportedTriggerType, __LINE__);
      return;
    }
  }

  if (ERROR_INSERTED(6124)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    setError(error, 9124, __LINE__);
    return;
  }
}

void
Dbdict::dropTrigger_parse_endpoint(Signal* signal,
				   SchemaOpPtr op_ptr,
				   ErrorInfo& error)
{
  DropTriggerRecPtr dropTriggerPtr;
  getOpRec(op_ptr, dropTriggerPtr);
  DropTrigImplReq* impl_req = &dropTriggerPtr.p->m_request;

  // check request type
  Uint32 requestType = impl_req->requestType;
  switch(DropTrigReq::getEndpointFlag(requestType)){
  case DropTrigReq::TriggerDst:
    jam();
    dropTriggerPtr.p->m_block_list[0] = DBTC_REF;
    break;
  case DropTrigReq::TriggerSrc:
    jam();
    dropTriggerPtr.p->m_block_list[0] = DBLQH_REF;
    break;
  default:
    ndbassert(false);
    setError(error, DropTrigRef::BadRequestType, __LINE__);
    return;
  }
}

bool
Dbdict::dropTrigger_subOps(Signal* signal, SchemaOpPtr op_ptr)
{
  D("dropTrigger_subOps" << V(op_ptr.i) << *op_ptr.p);

  DropTriggerRecPtr dropTriggerPtr;
  getOpRec(op_ptr, dropTriggerPtr);

  Uint32 requestType = dropTriggerPtr.p->m_request.requestType;
  switch(DropTrigReq::getEndpointFlag(requestType)){
  case DropTrigReq::MainTrigger:
    jam();
    break;
  case DropTrigReq::TriggerDst:
  case DropTrigReq::TriggerSrc:
    jam();
    return false;
  }

  /**
   * NOTE drop src before dst
   */
  if (!dropTriggerPtr.p->m_sub_src)
  {
    jam();
    dropTriggerPtr.p->m_sub_src = true;
    Callback c = {
      safe_cast(&Dbdict::dropTrigger_fromDropEndpoint),
      op_ptr.p->op_key
    };
    op_ptr.p->m_callback = c;
    dropTrigger_toDropEndpoint(signal, op_ptr,
			       DropTrigReq::TriggerSrc);
    return true;
  }

  if (!dropTriggerPtr.p->m_sub_dst)
  {
    jam();
    dropTriggerPtr.p->m_sub_dst = true;
    Callback c = {
      safe_cast(&Dbdict::dropTrigger_fromDropEndpoint),
      op_ptr.p->op_key
    };
    op_ptr.p->m_callback = c;
    dropTrigger_toDropEndpoint(signal, op_ptr,
			       DropTrigReq::TriggerDst);
    return true;
  }

  return false;
}

void
Dbdict::dropTrigger_toDropEndpoint(Signal* signal,
				   SchemaOpPtr op_ptr,
				   DropTrigReq::EndpointFlag endpoint)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  DropTriggerRecPtr dropTriggerPtr;
  getOpRec(op_ptr, dropTriggerPtr);
  const DropTrigImplReq* impl_req = &dropTriggerPtr.p->m_request;

  DropTrigReq* req = (DropTrigReq*)signal->getDataPtrSend();

  Uint32 requestType = 0;
  DropTrigReq::setEndpointFlag(requestType, endpoint);

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, requestType);
  DictSignal::addRequestFlagsGlobal(requestInfo, op_ptr.p->m_requestInfo);

  req->clientRef = reference();
  req->clientData = op_ptr.p->op_key;
  req->transId = trans_ptr.p->m_transId;
  req->transKey = trans_ptr.p->trans_key;
  req->requestInfo = requestInfo;
  req->tableId = impl_req->tableId;
  req->tableVersion = impl_req->tableVersion;
  req->indexId = impl_req->indexId;
  req->indexVersion = impl_req->indexVersion;
  req->triggerId = impl_req->triggerId;
  req->triggerNo = 0;

  sendSignal(reference(), GSN_DROP_TRIG_REQ, signal,
             DropTrigReq::SignalLength, JBB);
}

void
Dbdict::dropTrigger_fromDropEndpoint(Signal* signal,
					 Uint32 op_key, Uint32 ret)
{
  SchemaOpPtr op_ptr;
  DropTriggerRecPtr dropTriggerPtr;
  findSchemaOp(op_ptr, dropTriggerPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  if (ret == 0)
  {
    jam();
    const DropTrigConf* conf =
      (const DropTrigConf*)signal->getDataPtr();

    ndbrequire(conf->transId == trans_ptr.p->m_transId);
    createSubOps(signal, op_ptr);
  }
  else
  {
    jam();
    const DropTrigRef* ref =
      (const DropTrigRef*)signal->getDataPtr();
    ErrorInfo error;
    setError(error, ref);
    abortSubOps(signal, op_ptr, error);
  }
}

void
Dbdict::dropTrigger_reply(Signal* signal, SchemaOpPtr op_ptr, ErrorInfo error)
{
  jam();
  SchemaTransPtr& trans_ptr = op_ptr.p->m_trans_ptr;
  DropTriggerRecPtr dropTriggerPtr;
  getOpRec(op_ptr, dropTriggerPtr);
  const DropTrigImplReq* impl_req = &dropTriggerPtr.p->m_request;

  D("dropTrigger_reply" << V(impl_req->triggerId));

  if (!hasError(error)) {
    DropTrigConf* conf = (DropTrigConf*)signal->getDataPtrSend();
    conf->senderRef = reference();
    conf->clientData = op_ptr.p->m_clientData;
    conf->transId = trans_ptr.p->m_transId;
    conf->tableId = impl_req->tableId;
    conf->indexId = impl_req->indexId;
    conf->triggerId = impl_req->triggerId;

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_DROP_TRIG_CONF, signal,
               DropTrigConf::SignalLength, JBB);
  } else {
    jam();
    DropTrigRef* ref = (DropTrigRef*)signal->getDataPtrSend();
    ref->senderRef = reference();
    ref->clientData = op_ptr.p->m_clientData;
    ref->transId = trans_ptr.p->m_transId;
    ref->tableId = impl_req->tableId;
    ref->indexId = impl_req->indexId;
    ref->triggerId = impl_req->triggerId;
    getError(error, ref);

    Uint32 clientRef = op_ptr.p->m_clientRef;
    sendSignal(clientRef, GSN_DROP_TRIG_REF, signal,
               DropTrigRef::SignalLength, JBB);
  }
}

// DropTrigger: PREPARE

void
Dbdict::dropTrigger_prepare(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  D("dropTrigger_prepare");

  /**
   * This could check that triggers actually are present
   */

  sendTransConf(signal, op_ptr);
}

// DropTrigger: COMMIT

void
Dbdict::dropTrigger_commit(Signal* signal, SchemaOpPtr op_ptr)
{
  jam();
  DropTriggerRecPtr dropTriggerPtr;
  getOpRec(op_ptr, dropTriggerPtr);

  if (dropTriggerPtr.p->m_main_op)
  {
    jam();

    Uint32 triggerId = dropTriggerPtr.p->m_request.triggerId;

    TriggerRecordPtr triggerPtr;
    c_triggerRecordPool.getPtr(triggerPtr, triggerId);

    if (triggerPtr.p->indexId != RNIL)
    {
      TableRecordPtr indexPtr;
      c_tableRecordPool.getPtr(indexPtr, triggerPtr.p->indexId);
      triggerPtr.p->indexId = RNIL;
      indexPtr.p->triggerId = RNIL;
    }

    // remove trigger
    releaseDictObject(op_ptr);
    triggerPtr.p->triggerState = TriggerRecord::TS_NOT_DEFINED;

    sendTransConf(signal, op_ptr);
    return;
  }

  Callback c =  { safe_cast(&Dbdict::dropTrigger_commit_fromLocal),
                  op_ptr.p->op_key
  };

  op_ptr.p->m_callback = c;
  send_drop_trig_req(signal, op_ptr);
}

void
Dbdict::dropTrigger_commit_fromLocal(Signal* signal,
                                       Uint32 op_key, Uint32 ret)
{
  SchemaOpPtr op_ptr;
  DropTriggerRecPtr dropTriggerPtr;
  findSchemaOp(op_ptr, dropTriggerPtr, op_key);
  ndbrequire(!op_ptr.isNull());
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  if (ret)
  {
    jam();
    warningEvent("Got error code %u from %x (DROP_TRIG_IMPL_REQ)",
                 ret,
                 dropTriggerPtr.p->m_block_list[0]);
  }

  sendTransConf(signal, op_ptr);
}

void
Dbdict::send_drop_trig_req(Signal* signal, SchemaOpPtr op_ptr)
{
  DropTriggerRecPtr dropTriggerPtr;
  getOpRec(op_ptr, dropTriggerPtr);
  const DropTrigImplReq* impl_req = &dropTriggerPtr.p->m_request;

  DropTrigImplReq* req = (DropTrigImplReq*)signal->getDataPtrSend();

  req->senderRef = reference();
  req->senderData = op_ptr.p->op_key;
  req->requestType = impl_req->requestType;
  req->tableId = impl_req->tableId;
  req->tableVersion = 0; // not used
  req->indexId = impl_req->indexId;
  req->indexVersion = 0; // not used
  req->triggerNo = 0;
  req->triggerId = impl_req->triggerId;
  req->triggerInfo = impl_req->triggerInfo;

  BlockReference ref = dropTriggerPtr.p->m_block_list[0];
  sendSignal(ref, GSN_DROP_TRIG_IMPL_REQ, signal,
             DropTrigImplReq::SignalLength, JBB);
}

// DropTrigger: ABORT

void
Dbdict::dropTrigger_abortParse(Signal* signal, SchemaOpPtr op_ptr)
{
  DropTriggerRecPtr dropTriggerPtr;
  getOpRec(op_ptr, dropTriggerPtr);

  D("dropTrigger_abortParse" << *op_ptr.p);

  if (hasDictObject(op_ptr)) {
    jam();
    unlinkDictObject(op_ptr);
  }
  sendTransConf(signal, op_ptr);
}

void
Dbdict::dropTrigger_abortPrepare(Signal* signal, SchemaOpPtr op_ptr)
{
  D("dropTrigger_abortPrepare" << *op_ptr.p);

  sendTransConf(signal, op_ptr);
}

// DropTrigger: MISC

void
Dbdict::execDROP_TRIG_CONF(Signal* signal)
{
  jamEntry();
  const DropTrigConf* conf = (const DropTrigConf*)signal->getDataPtr();
  handleDictConf(signal, conf);
}

void
Dbdict::execDROP_TRIG_REF(Signal* signal)
{
  jamEntry();
  const DropTrigRef* ref = (const DropTrigRef*)signal->getDataPtr();
  handleDictRef(signal, ref);
}

void
Dbdict::execDROP_TRIG_IMPL_CONF(Signal* signal)
{
  jamEntry();
  const DropTrigImplConf* conf = (const DropTrigImplConf*)signal->getDataPtr();
  ndbrequire(refToNode(conf->senderRef) == getOwnNodeId());
  handleDictConf(signal, conf);
}

void
Dbdict::execDROP_TRIG_IMPL_REF(Signal* signal)
{
  jamEntry();
  const DropTrigImplRef* ref = (const DropTrigImplRef*)signal->getDataPtr();
  ndbrequire(refToNode(ref->senderRef) == getOwnNodeId());
  handleDictRef(signal, ref);
}

// DropTrigger: END


/**
 * MODULE: Support routines for index and trigger.
 */

/*
  This routine is used to set-up the primary key attributes of the unique
  hash index. Since we store fragment id as part of the primary key here
  we insert the pseudo column for getting fragment id first in the array.
  This routine is used as part of the building of the index.
*/

void
Dbdict::getTableKeyList(TableRecordPtr tablePtr, 
			Id_array<MAX_ATTRIBUTES_IN_INDEX+1>& list)
{
  jam();
  list.sz = 0;
  list.id[list.sz++] = AttributeHeader::FRAGMENT;
  LocalDLFifoList<AttributeRecord> alist(c_attributeRecordPool,
                                         tablePtr.p->m_attributes);
  AttributeRecordPtr attrPtr;
  for (alist.first(attrPtr); !attrPtr.isNull(); alist.next(attrPtr)) {
    if (attrPtr.p->tupleKey) {
      list.id[list.sz++] = attrPtr.p->attributeId;
    }
  }
  ndbrequire(list.sz == (uint)(tablePtr.p->noOfPrimkey + 1));
  ndbrequire(list.sz <= MAX_ATTRIBUTES_IN_INDEX + 1);
}

// XXX should store the primary attribute id
void
Dbdict::getIndexAttr(TableRecordPtr indexPtr, Uint32 itAttr, Uint32* id)
{
  jam();

  Uint32 len;
  char name[MAX_ATTR_NAME_SIZE];
  TableRecordPtr tablePtr;
  AttributeRecordPtr attrPtr;

  c_tableRecordPool.getPtr(tablePtr, indexPtr.p->primaryTableId);
  AttributeRecord* iaRec = c_attributeRecordPool.getPtr(itAttr);
  {
    ConstRope tmp(c_rope_pool, iaRec->attributeName);
    tmp.copy(name);
    len = tmp.size();
  }
  LocalDLFifoList<AttributeRecord> alist(c_attributeRecordPool, 
					 tablePtr.p->m_attributes);
  for (alist.first(attrPtr); !attrPtr.isNull(); alist.next(attrPtr)){
    ConstRope tmp(c_rope_pool, attrPtr.p->attributeName);
    if(tmp.compare(name, len) == 0){
      id[0] = attrPtr.p->attributeId;
      return;
    }
  }
  ndbrequire(false);
}

void
Dbdict::getIndexAttrList(TableRecordPtr indexPtr, AttributeList& list)
{
  jam();
  list.sz = 0;
  memset(list.id, 0, sizeof(list.id));
  ndbrequire(indexPtr.p->noOfAttributes >= 2);

  LocalDLFifoList<AttributeRecord> alist(c_attributeRecordPool,
                                         indexPtr.p->m_attributes);
  AttributeRecordPtr attrPtr;
  for (alist.first(attrPtr); !attrPtr.isNull(); alist.next(attrPtr)) {
    // skip last
    AttributeRecordPtr tempPtr = attrPtr;
    if (! alist.next(tempPtr))
      break;
    getIndexAttr(indexPtr, attrPtr.i, &list.id[list.sz++]);
  }
  ndbrequire(indexPtr.p->noOfAttributes == list.sz + 1);
}

void
Dbdict::getIndexAttrMask(TableRecordPtr indexPtr, AttributeMask& mask)
{
  jam();
  mask.clear();
  ndbrequire(indexPtr.p->noOfAttributes >= 2);
  
  AttributeRecordPtr attrPtr, currPtr;
  LocalDLFifoList<AttributeRecord> alist(c_attributeRecordPool, 
					 indexPtr.p->m_attributes);
  
  
  for (alist.first(attrPtr); currPtr = attrPtr, alist.next(attrPtr); ){
    Uint32 id;
    getIndexAttr(indexPtr, currPtr.i, &id);
    mask.set(id);
  }
}

// DICT lock master

const Dbdict::DictLockType*
Dbdict::getDictLockType(Uint32 lockType)
{
  static const DictLockType lt[] = {
    { DictLockReq::NodeRestartLock, "NodeRestart" }
    ,{ DictLockReq::SchemaTransLock, "SchemaTransLock" }
    ,{ DictLockReq::CreateFileLock,  "CreateFile" }
    ,{ DictLockReq::CreateFilegroupLock, "CreateFilegroup" }
    ,{ DictLockReq::DropFileLock,    "DropFile" }
    ,{ DictLockReq::DropFilegroupLock, "DropFilegroup" }
  };
  for (unsigned int i = 0; i < sizeof(lt)/sizeof(lt[0]); i++) {
    if ((Uint32) lt[i].lockType == lockType)
      return &lt[i];
  }
  return NULL;
}

void
Dbdict::sendDictLockInfoEvent(Signal*, const UtilLockReq* req, const char* text)
{
  const Dbdict::DictLockType* lt = getDictLockType(req->extra);
  
  infoEvent("DICT: %s %u for %s",
            text,
            (unsigned)refToNode(req->senderRef), lt->text);
}

void
Dbdict::execDICT_LOCK_REQ(Signal* signal)
{
  jamEntry();
  const DictLockReq* req = (const DictLockReq*)&signal->theData[0];

  UtilLockReq lockReq;
  lockReq.senderRef = req->userRef;
  lockReq.senderData = req->userPtr;
  lockReq.lockId = 0;
  lockReq.requestInfo = 0;
  lockReq.extra = req->lockType;

  const DictLockType* lt = getDictLockType(req->lockType);

  if (req->lockType == DictLockReq::NodeRestartLock)
  {
    jam();
    lockReq.requestInfo |= UtilLockReq::SharedLock;
  }

  // make sure bad request crashes slave, not master (us)
  Uint32 err, res;
  if (getOwnNodeId() != c_masterNodeId) 
  {
    jam();
    err = DictLockRef::NotMaster;
    goto ref;
  }
  
  if (lt == NULL) 
  {
    jam();
    err = DictLockRef::InvalidLockType;
    goto ref;
  }

  if (req->userRef != signal->getSendersBlockRef() ||
      getNodeInfo(refToNode(req->userRef)).m_type != NodeInfo::DB) 
  {
    jam();
    err = DictLockRef::BadUserRef;
    goto ref;
  }

  if (c_aliveNodes.get(refToNode(req->userRef))) 
  {
    jam();
    err = DictLockRef::TooLate;
    goto ref;
  }
  
  res = m_dict_lock.lock(this, m_dict_lock_pool, &lockReq, 0);
  switch(res){
  case 0:
    jam();
    sendDictLockInfoEvent(signal, &lockReq, "locked by node");
    goto conf;
    break;
  case UtilLockRef::OutOfLockRecords:
    jam();
    err = DictLockRef::TooManyRequests;
    goto ref;
    break;
  default:
    jam();
    sendDictLockInfoEvent(signal, &lockReq, "lock request by node");    
    break;
  }
  return;
  
ref:
  {
    DictLockRef* ref = (DictLockRef*)signal->getDataPtrSend();
    ref->userPtr = lockReq.senderData;
    ref->lockType = lockReq.extra;
    ref->errorCode = err;
    
    sendSignal(lockReq.senderRef, GSN_DICT_LOCK_REF, signal,
               DictLockRef::SignalLength, JBB);
  }
  return;
  
conf:
  {
    DictLockConf* conf = (DictLockConf*)signal->getDataPtrSend();
    
    conf->userPtr = lockReq.senderData;
    conf->lockType = lockReq.extra;
    conf->lockPtr = lockReq.senderData;
    
    sendSignal(lockReq.senderRef, GSN_DICT_LOCK_CONF, signal,
               DictLockConf::SignalLength, JBB);
  }
  return;
}

void
Dbdict::execDICT_UNLOCK_ORD(Signal* signal)
{
  jamEntry();
  const DictUnlockOrd* ord = (const DictUnlockOrd*)&signal->theData[0];
  
  DictLockReq req;
  req.userPtr = ord->senderData;
  req.userRef = ord->senderRef;

  if (signal->getLength() < DictUnlockOrd::SignalLength)
  {
    jam();
    req.userPtr = ord->lockPtr;
    req.userRef = signal->getSendersBlockRef();
  }
  
  UtilLockReq lockReq;
  lockReq.senderData = req.userPtr;
  lockReq.senderRef = req.userRef;
  lockReq.extra = DictLockReq::NodeRestartLock; // Should check...
  Uint32 res = dict_lock_unlock(signal, &req);
  switch(res){
  case UtilUnlockRef::OK:
    jam();
    sendDictLockInfoEvent(signal, &lockReq, "unlocked by node");
    return;
  case UtilUnlockRef::NotLockOwner:
    jam();
    sendDictLockInfoEvent(signal, &lockReq, "lock request removed by node");
    return;
  default:
    ndbassert(false);
  }
}

// NF handling

void
Dbdict::removeStaleDictLocks(Signal* signal, const Uint32* theFailedNodes)
{
  LockQueue::Iterator iter;
  if (m_dict_lock.first(this, m_dict_lock_pool, iter))
  {
    do {
      if (NodeBitmask::get(theFailedNodes, 
                           refToNode(iter.m_curr.p->m_req.senderRef)))
      {
        if (iter.m_curr.p->m_req.requestInfo & UtilLockReq::Granted)
        {
          jam();
          sendDictLockInfoEvent(signal, &iter.m_curr.p->m_req, 
                                "remove lock by failed node");
        } 
        else 
        {
          jam();
          sendDictLockInfoEvent(signal, &iter.m_curr.p->m_req, 
                                "remove lock request by failed node");
        }
        DictUnlockOrd* ord = (DictUnlockOrd*)signal->getDataPtrSend();
        ord->senderRef = iter.m_curr.p->m_req.senderRef;
        ord->senderData = iter.m_curr.p->m_req.senderData;
        ord->lockPtr = iter.m_curr.p->m_req.senderData;
        ord->lockType = iter.m_curr.p->m_req.extra;
        sendSignal(reference(), GSN_DICT_UNLOCK_ORD, signal,
                   DictUnlockOrd::SignalLength, JBB);
      }
    } while (m_dict_lock.next(iter));
  }
}

Uint32
Dbdict::dict_lock_trylock(const DictLockReq* _req)
{
  UtilLockReq req;
  const UtilLockReq *lockOwner;
  req.senderData = _req->userPtr;
  req.senderRef = _req->userRef;
  req.extra = _req->lockType;
  req.requestInfo = UtilLockReq::TryLock | UtilLockReq::Notify;

  Uint32 res = m_dict_lock.lock(this, m_dict_lock_pool, &req, &lockOwner);
  switch(res){
  case UtilLockRef::OK:
    jam();
    return 0;
  case UtilLockRef::LockAlreadyHeld:
    jam();
    if (lockOwner->extra == DictLockReq::NodeRestartLock)
    {
      jam();
      return SchemaTransBeginRef::BusyWithNR;
    }
    break;
  case UtilLockRef::OutOfLockRecords:
    jam();
    break;
  case UtilLockRef::InLockQueue:
    jam();
    /**
     * Should not happen with trylock
     */
    ndbassert(false);
    break;
  }
  
  return SchemaTransBeginRef::Busy;
}

Uint32
Dbdict::dict_lock_unlock(Signal* signal, const DictLockReq* _req)
{
  UtilUnlockReq req;
  req.senderData = _req->userPtr;
  req.senderRef = _req->userRef;
  
  Uint32 res = m_dict_lock.unlock(this, m_dict_lock_pool, &req);
  switch(res){
  case UtilUnlockRef::OK:
  case UtilUnlockRef::NotLockOwner:
    break;
  case UtilUnlockRef::NotInLockQueue:
    ndbassert(false);
    return res;
  }

  UtilLockReq lockReq;
  LockQueue::Iterator iter;
  if (m_dict_lock.first(this, m_dict_lock_pool, iter))
  {
    int res;
    while ((res = m_dict_lock.checkLockGrant(iter, &lockReq)) > 0)
    {
      jam();
      /**
       *
       */
      if (res == 2)
      {
        jam();
        DictLockConf* conf = (DictLockConf*)signal->getDataPtrSend();
        conf->userPtr = lockReq.senderData;
        conf->lockPtr = lockReq.senderData;
        conf->lockType = lockReq.extra;
        sendSignal(lockReq.senderRef, GSN_DICT_LOCK_CONF, signal,
                   DictLockConf::SignalLength, JBB);
      }        
      
      if (!m_dict_lock.next(iter))
        break;
    }
  }

  return res;
}

void
Dbdict::execBACKUP_LOCK_TAB_REQ(Signal* signal)
{
  jamEntry();
  const BackupLockTab *req = (const BackupLockTab *)signal->getDataPtrSend();
  Uint32 senderRef = req->m_senderRef;
  Uint32 tableId = req->m_tableId;
  Uint32 lock = req->m_lock_unlock;

  TableRecordPtr tablePtr;
  c_tableRecordPool.getPtr(tablePtr, tableId, true);

  if(lock == BackupLockTab::LOCK_TABLE)
  {
    ndbrequire(tablePtr.p->tabState == TableRecord::DEFINED);
    tablePtr.p->tabState = TableRecord::BACKUP_ONGOING;
  }
  else if(tablePtr.p->tabState == TableRecord::BACKUP_ONGOING)
  {
    tablePtr.p->tabState = TableRecord::DEFINED;
  }

  sendSignal(senderRef, GSN_BACKUP_LOCK_TAB_CONF, signal,
             BackupLockTab::SignalLength, JBB);
}

/* **************************************************************** */
/* ---------------------------------------------------------------- */
/* MODULE:          STORE/RESTORE SCHEMA FILE---------------------- */
/* ---------------------------------------------------------------- */
/*                                                                  */
/* General module used to store the schema file on disk and         */
/* similar function to restore it from disk.                        */
/* ---------------------------------------------------------------- */
/* **************************************************************** */

void
Dbdict::initSchemaFile(XSchemaFile * xsf, Uint32 firstPage, Uint32 lastPage,
                       bool initEntries)
{
  ndbrequire(lastPage <= xsf->noOfPages);
  for (Uint32 n = firstPage; n < lastPage; n++) {
    SchemaFile * sf = &xsf->schemaPage[n];
    if (initEntries)
      memset(sf, 0, NDB_SF_PAGE_SIZE);

    Uint32 ndb_version = NDB_VERSION;
    if (ndb_version < NDB_SF_VERSION_5_0_6)
      ndb_version = NDB_SF_VERSION_5_0_6;

    memcpy(sf->Magic, NDB_SF_MAGIC, sizeof(sf->Magic));
    sf->ByteOrder = 0x12345678;
    sf->NdbVersion =  ndb_version;
    sf->FileSize = xsf->noOfPages * NDB_SF_PAGE_SIZE;
    sf->PageNumber = n;
    sf->CheckSum = 0;
    sf->NoOfTableEntries = NDB_SF_PAGE_ENTRIES;

    computeChecksum(xsf, n);
  }
}

void
Dbdict::resizeSchemaFile(XSchemaFile * xsf, Uint32 noOfPages)
{
  ndbrequire(noOfPages <= NDB_SF_MAX_PAGES);
  if (xsf->noOfPages < noOfPages) {
    jam();
    Uint32 firstPage = xsf->noOfPages;
    xsf->noOfPages = noOfPages;
    initSchemaFile(xsf, 0, firstPage, false);
    initSchemaFile(xsf, firstPage, xsf->noOfPages, true);
  }
  if (xsf->noOfPages > noOfPages) {
    jam();
    Uint32 tableId = noOfPages * NDB_SF_PAGE_ENTRIES;
    while (tableId < xsf->noOfPages * NDB_SF_PAGE_ENTRIES) {
      SchemaFile::TableEntry * te = getTableEntry(xsf, tableId);
      if (te->m_tableState != SchemaFile::INIT &&
          te->m_tableState != SchemaFile::DROP_TABLE_COMMITTED) {
        ndbrequire(false);
      }
      tableId++;
    }
    xsf->noOfPages = noOfPages;
    initSchemaFile(xsf, 0, xsf->noOfPages, false);
  }
}

void
Dbdict::computeChecksum(XSchemaFile * xsf, Uint32 pageNo){ 
  SchemaFile * sf = &xsf->schemaPage[pageNo];
  sf->CheckSum = 0;
  sf->CheckSum = computeChecksum((Uint32*)sf, NDB_SF_PAGE_SIZE_IN_WORDS);
}

bool 
Dbdict::validateChecksum(const XSchemaFile * xsf){
  
  for (Uint32 n = 0; n < xsf->noOfPages; n++) {
    SchemaFile * sf = &xsf->schemaPage[n];
    Uint32 c = computeChecksum((Uint32*)sf, NDB_SF_PAGE_SIZE_IN_WORDS);
    if ( c != 0)
      return false;
  }
  return true;
}

Uint32
Dbdict::computeChecksum(const Uint32 * src, Uint32 len){
  Uint32 ret = 0;
  for(Uint32 i = 0; i<len; i++)
    ret ^= src[i];
  return ret;
}

SchemaFile::TableEntry * 
Dbdict::getTableEntry(XSchemaFile * xsf, Uint32 tableId)
{
  Uint32 n = tableId / NDB_SF_PAGE_ENTRIES;
  Uint32 i = tableId % NDB_SF_PAGE_ENTRIES;
  ndbrequire(n < xsf->noOfPages);

  SchemaFile * sf = &xsf->schemaPage[n];
  return &sf->TableEntries[i];
}

//******************************************
void
Dbdict::execCREATE_FILE_REQ(Signal* signal)
{
  jamEntry();
  
  if(!assembleFragments(signal)){
    jam();
    return;
  }

  SectionHandle handle(this, signal);
  
  CreateFileReq * req = (CreateFileReq*)signal->getDataPtr();
  CreateFileRef * ref = (CreateFileRef*)signal->getDataPtrSend();
  Uint32 senderRef = req->senderRef;
  Uint32 senderData = req->senderData;
  Uint32 type = req->objType;
  Uint32 requestInfo = req->requestInfo;
  
  do {
    if(getOwnNodeId() != c_masterNodeId){
      jam();
      ref->errorCode = CreateFileRef::NotMaster;
      ref->status    = 0;
      ref->errorKey  = 0;
      ref->errorLine = __LINE__;
      break;
    }
    
    if (checkSingleUserMode(senderRef))
    {
      ref->errorCode = CreateFileRef::SingleUser;
      ref->status    = 0;
      ref->errorKey  = 0;
      ref->errorLine = __LINE__;
      break;
    }

    Ptr<SchemaTransaction> trans_ptr;
    if (! c_Trans.seize(trans_ptr)){
      jam();
      ref->errorCode = CreateFileRef::Busy;
      ref->status    = 0;
      ref->errorKey  = 0;
      ref->errorLine = __LINE__;
      break;
    }

    DictLockReq lockReq;
    lockReq.userPtr = trans_ptr.i;
    lockReq.userRef = reference();
    lockReq.lockType = DictLockReq::CreateFileLock;
    if ((ref->errorCode = dict_lock_trylock(&lockReq)))
    {
      jam();
      ref->errorLine = __LINE__;      
      break;
    }

    jam(); 
    const Uint32 trans_key = ++c_opRecordSequence;
    trans_ptr.p->key = trans_key;
    trans_ptr.p->m_senderRef = senderRef;
    trans_ptr.p->m_senderData = senderData;
    trans_ptr.p->m_nodes = c_aliveNodes;
    trans_ptr.p->m_errorCode = 0;
//    trans_ptr.p->m_nodes.clear(); 
//    trans_ptr.p->m_nodes.set(getOwnNodeId());
    c_Trans.add(trans_ptr);
    
    const Uint32 op_key = ++c_opRecordSequence;
    trans_ptr.p->m_op.m_key = op_key;
    trans_ptr.p->m_op.m_vt_index = 1;
    trans_ptr.p->m_op.m_state = DictObjOp::Preparing;
    
    CreateObjReq* create_obj = (CreateObjReq*)signal->getDataPtrSend();
    create_obj->op_key = op_key;
    create_obj->senderRef = reference();
    create_obj->senderData = trans_key;
    create_obj->clientRef = senderRef;
    create_obj->clientData = senderData;
    
    create_obj->objType = type;
    create_obj->requestInfo = requestInfo;

    {
      Uint32 objId = getFreeObjId(0);
      if (objId == RNIL) {
        jam();
        ref->errorCode = CreateFileRef::NoMoreObjectRecords;
        ref->status    = 0;
        ref->errorKey  = 0;
        ref->errorLine = __LINE__;

        dict_lock_unlock(0, &lockReq);
        break;
      }
      
      create_obj->objId = objId;
      trans_ptr.p->m_op.m_obj_id = objId;
      create_obj->gci = 0;
      
      XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
      SchemaFile::TableEntry *objEntry = getTableEntry(xsf, objId);      
      create_obj->objVersion = 
	create_obj_inc_schema_version(objEntry->m_tableVersion);
    }
    
    NodeReceiverGroup rg(DBDICT, trans_ptr.p->m_nodes);
    SafeCounter tmp(c_counterMgr, trans_ptr.p->m_counter);
    tmp.init<CreateObjRef>(rg, GSN_CREATE_OBJ_REF, trans_key);
    sendSignal(rg, GSN_CREATE_OBJ_REQ, signal, 
	       CreateObjReq::SignalLength, JBB,
	       &handle);

    return;
  } while(0);
  
  releaseSections(handle);
  ref->senderData = senderData;
  ref->masterNodeId = c_masterNodeId;
  sendSignal(senderRef, GSN_CREATE_FILE_REF,signal,
	     CreateFileRef::SignalLength, JBB);
}

void
Dbdict::execCREATE_FILEGROUP_REQ(Signal* signal)
{
  jamEntry();
  
  if(!assembleFragments(signal)){
    jam();
    return;
  }
  
  SectionHandle handle(this, signal);

  CreateFilegroupReq * req = (CreateFilegroupReq*)signal->getDataPtr();
  CreateFilegroupRef * ref = (CreateFilegroupRef*)signal->getDataPtrSend();
  Uint32 senderRef = req->senderRef;
  Uint32 senderData = req->senderData;
  Uint32 type = req->objType;
  
  do {
    if(getOwnNodeId() != c_masterNodeId){
      jam();
      ref->errorCode = CreateFilegroupRef::NotMaster;
      ref->status    = 0;
      ref->errorKey  = 0;
      ref->errorLine = __LINE__;
      break;
    }
    
    if (checkSingleUserMode(senderRef))
    {
      ref->errorCode = CreateFilegroupRef::SingleUser;
      ref->status    = 0;
      ref->errorKey  = 0;
      ref->errorLine = __LINE__;
      break;
    }

    Ptr<SchemaTransaction> trans_ptr;
    if (! c_Trans.seize(trans_ptr)){
      jam();
      ref->errorCode = CreateFilegroupRef::Busy;
      ref->status    = 0;
      ref->errorKey  = 0;
      ref->errorLine = __LINE__;
      break;
    }

    DictLockReq lockReq;
    lockReq.userPtr = trans_ptr.i;
    lockReq.userRef = reference();
    lockReq.lockType = DictLockReq::CreateFilegroupLock;
    if ((ref->errorCode = dict_lock_trylock(&lockReq)))
    {
      jam();
      ref->errorLine = __LINE__;      
      break;
    }

    jam(); 
    const Uint32 trans_key = ++c_opRecordSequence;
    trans_ptr.p->key = trans_key;
    trans_ptr.p->m_senderRef = senderRef;
    trans_ptr.p->m_senderData = senderData;
    trans_ptr.p->m_nodes = c_aliveNodes;
    trans_ptr.p->m_errorCode = 0;
    c_Trans.add(trans_ptr);
    
    const Uint32 op_key = ++c_opRecordSequence;
    trans_ptr.p->m_op.m_key = op_key;
    trans_ptr.p->m_op.m_vt_index = 0;
    trans_ptr.p->m_op.m_state = DictObjOp::Preparing;
    
    CreateObjReq* create_obj = (CreateObjReq*)signal->getDataPtrSend();
    create_obj->op_key = op_key;
    create_obj->senderRef = reference();
    create_obj->senderData = trans_key;
    create_obj->clientRef = senderRef;
    create_obj->clientData = senderData;
    
    create_obj->objType = type;
    
    {
      Uint32 objId = getFreeObjId(0);
      if (objId == RNIL) {
        jam();
        ref->errorCode = CreateFilegroupRef::NoMoreObjectRecords;
        ref->status    = 0;
        ref->errorKey  = 0;
        ref->errorLine = __LINE__;

        dict_lock_unlock(0, &lockReq);
        break;
      }
      
      create_obj->objId = objId;
      trans_ptr.p->m_op.m_obj_id = objId;
      create_obj->gci = 0;
      
      XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
      SchemaFile::TableEntry *objEntry = getTableEntry(xsf, objId);      
      create_obj->objVersion = 
	create_obj_inc_schema_version(objEntry->m_tableVersion);
    }
    
    NodeReceiverGroup rg(DBDICT, trans_ptr.p->m_nodes);
    SafeCounter tmp(c_counterMgr, trans_ptr.p->m_counter);
    tmp.init<CreateObjRef>(rg, GSN_CREATE_OBJ_REF, trans_key);
    sendSignal(rg, GSN_CREATE_OBJ_REQ, signal, 
	       CreateObjReq::SignalLength, JBB,
	       &handle);

    return;
  } while(0);
  
  releaseSections(handle);
  ref->senderData = senderData;
  ref->masterNodeId = c_masterNodeId;
  sendSignal(senderRef, GSN_CREATE_FILEGROUP_REF,signal,
	     CreateFilegroupRef::SignalLength, JBB);
}

void
Dbdict::execDROP_FILE_REQ(Signal* signal)
{
  jamEntry();
  
  if(!assembleFragments(signal)){
    jam();
    return;
  }
  
  DropFileReq * req = (DropFileReq*)signal->getDataPtr();
  DropFileRef * ref = (DropFileRef*)signal->getDataPtrSend();
  Uint32 senderRef = req->senderRef;
  Uint32 senderData = req->senderData;
  Uint32 objId = req->file_id;
  Uint32 version = req->file_version;
  
  do {
    if(getOwnNodeId() != c_masterNodeId){
      jam();
      ref->errorCode = DropFileRef::NotMaster;
      ref->errorKey  = 0;
      ref->errorLine = __LINE__;
      break;
    }
    
    if (checkSingleUserMode(senderRef))
    {
      jam();
      ref->errorCode = DropFileRef::SingleUser;
      ref->errorKey  = 0;
      ref->errorLine = __LINE__;
      break;
    }

    Ptr<File> file_ptr;
    if (!c_file_hash.find(file_ptr, objId))
    {
      jam();
      ref->errorCode = DropFileRef::NoSuchFile;
      ref->errorLine = __LINE__;
      break;
    }
    
    if (file_ptr.p->m_version != version)
    {
      jam();
      ref->errorCode = DropFileRef::InvalidSchemaObjectVersion;
      ref->errorLine = __LINE__;
      break;
    }

    Ptr<SchemaTransaction> trans_ptr;
    if (! c_Trans.seize(trans_ptr))
    {
      jam();
      ref->errorCode = DropFileRef::Busy;
      ref->errorLine = __LINE__;
      break;
    }

    DictLockReq lockReq;
    lockReq.userPtr = trans_ptr.i;
    lockReq.userRef = reference();
    lockReq.lockType = DictLockReq::DropFileLock;
    if ((ref->errorCode = dict_lock_trylock(&lockReq)))
    {
      jam();
      ref->errorLine = __LINE__;      
      break;
    }

    jam();
    
    const Uint32 trans_key = ++c_opRecordSequence;
    trans_ptr.p->key = trans_key;
    trans_ptr.p->m_senderRef = senderRef;
    trans_ptr.p->m_senderData = senderData;
    trans_ptr.p->m_nodes = c_aliveNodes;
    trans_ptr.p->m_errorCode = 0;
    c_Trans.add(trans_ptr);
    
    const Uint32 op_key = ++c_opRecordSequence;
    trans_ptr.p->m_op.m_key = op_key;
    trans_ptr.p->m_op.m_vt_index = 2;
    trans_ptr.p->m_op.m_state = DictObjOp::Preparing;
    
    DropObjReq* drop_obj = (DropObjReq*)signal->getDataPtrSend();
    drop_obj->op_key = op_key;
    drop_obj->objVersion = version;
    drop_obj->objId = objId;
    drop_obj->objType = file_ptr.p->m_type;
    trans_ptr.p->m_op.m_obj_id = objId;

    drop_obj->senderRef = reference();
    drop_obj->senderData = trans_key;
    drop_obj->clientRef = senderRef;
    drop_obj->clientData = senderData;

    drop_obj->requestInfo = 0;
    
    NodeReceiverGroup rg(DBDICT, trans_ptr.p->m_nodes);
    SafeCounter tmp(c_counterMgr, trans_ptr.p->m_counter);
    tmp.init<CreateObjRef>(rg, GSN_DROP_OBJ_REF, trans_key);
    sendSignal(rg, GSN_DROP_OBJ_REQ, signal, 
	       DropObjReq::SignalLength, JBB);

    return;
  } while(0);
  
  ref->senderData = senderData;
  ref->masterNodeId = c_masterNodeId;
  sendSignal(senderRef, GSN_DROP_FILE_REF,signal,
	     DropFileRef::SignalLength, JBB);
}

void
Dbdict::execDROP_FILEGROUP_REQ(Signal* signal)
{
  jamEntry();
  
  if(!assembleFragments(signal)){
    jam();
    return;
  }
  
  DropFilegroupReq * req = (DropFilegroupReq*)signal->getDataPtr();
  DropFilegroupRef * ref = (DropFilegroupRef*)signal->getDataPtrSend();
  Uint32 senderRef = req->senderRef;
  Uint32 senderData = req->senderData;
  Uint32 objId = req->filegroup_id;
  Uint32 version = req->filegroup_version;
  
  do {
    if(getOwnNodeId() != c_masterNodeId)
    {
      jam();
      ref->errorCode = DropFilegroupRef::NotMaster;
      ref->errorKey  = 0;
      ref->errorLine = __LINE__;
      break;
    }
    
    if (checkSingleUserMode(senderRef))
    {
      jam();
      ref->errorCode = DropFilegroupRef::SingleUser;
      ref->errorKey  = 0;
      ref->errorLine = __LINE__;
      break;
    }

    Ptr<Filegroup> filegroup_ptr;
    if (!c_filegroup_hash.find(filegroup_ptr, objId))
    {
      jam();
      ref->errorCode = DropFilegroupRef::NoSuchFilegroup;
      ref->errorLine = __LINE__;
      break;
    }

    if (filegroup_ptr.p->m_version != version)
    {
      jam();
      ref->errorCode = DropFilegroupRef::InvalidSchemaObjectVersion;
      ref->errorLine = __LINE__;
      break;
    }
    
    Ptr<SchemaTransaction> trans_ptr;
    if (! c_Trans.seize(trans_ptr))
    {
      jam();
      ref->errorCode = DropFilegroupRef::Busy;
      ref->errorLine = __LINE__;
      break;
    }

    DictLockReq lockReq;
    lockReq.userPtr = trans_ptr.i;
    lockReq.userRef = reference();
    lockReq.lockType = DictLockReq::DropFilegroupLock;
    if ((ref->errorCode = dict_lock_trylock(&lockReq)))
    {
      jam();
      ref->errorLine = __LINE__;      
      break;
    }

    jam();
    
    const Uint32 trans_key = ++c_opRecordSequence;
    trans_ptr.p->key = trans_key;
    trans_ptr.p->m_senderRef = senderRef;
    trans_ptr.p->m_senderData = senderData;
    trans_ptr.p->m_nodes = c_aliveNodes;
    trans_ptr.p->m_errorCode = 0;
    c_Trans.add(trans_ptr);
    
    const Uint32 op_key = ++c_opRecordSequence;
    trans_ptr.p->m_op.m_key = op_key;
    trans_ptr.p->m_op.m_vt_index = 3;
    trans_ptr.p->m_op.m_state = DictObjOp::Preparing;
    
    DropObjReq* drop_obj = (DropObjReq*)signal->getDataPtrSend();
    drop_obj->op_key = op_key;
    drop_obj->objVersion = version;
    drop_obj->objId = objId;
    drop_obj->objType = filegroup_ptr.p->m_type;
    trans_ptr.p->m_op.m_obj_id = objId;
    
    drop_obj->senderRef = reference();
    drop_obj->senderData = trans_key;
    drop_obj->clientRef = senderRef;
    drop_obj->clientData = senderData;
    
    drop_obj->requestInfo = 0;
    
    NodeReceiverGroup rg(DBDICT, trans_ptr.p->m_nodes);
    SafeCounter tmp(c_counterMgr, trans_ptr.p->m_counter);
    tmp.init<CreateObjRef>(rg, GSN_DROP_OBJ_REF, trans_key);
    sendSignal(rg, GSN_DROP_OBJ_REQ, signal, 
	       DropObjReq::SignalLength, JBB);

    return;
  } while(0);
  
  ref->senderData = senderData;
  ref->masterNodeId = c_masterNodeId;
  sendSignal(senderRef, GSN_DROP_FILEGROUP_REF,signal,
	     DropFilegroupRef::SignalLength, JBB);
}

void
Dbdict::execCREATE_OBJ_REF(Signal* signal)
{
  CreateObjRef * const ref = (CreateObjRef*)signal->getDataPtr();
  Ptr<SchemaTransaction> trans_ptr;

  jamEntry();
  ndbrequire(c_Trans.find(trans_ptr, ref->senderData));
  if(ref->errorCode != CreateObjRef::NF_FakeErrorREF){
    jam();
    trans_ptr.p->setErrorCode(ref->errorCode);
  }
  Uint32 node = refToNode(ref->senderRef);
  schemaOperation_reply(signal, trans_ptr.p, node);
}

void
Dbdict::execCREATE_OBJ_CONF(Signal* signal)
{
  Ptr<SchemaTransaction> trans_ptr;
  CreateObjConf * const conf = (CreateObjConf*)signal->getDataPtr();

  jamEntry();
  ndbrequire(c_Trans.find(trans_ptr, conf->senderData));
  schemaOperation_reply(signal, trans_ptr.p, refToNode(conf->senderRef));
}

void
Dbdict::schemaOperation_reply(Signal* signal,
		              SchemaTransaction * trans_ptr_p,
		              Uint32 nodeId)
{
  jam();
  {
    SafeCounter tmp(c_counterMgr, trans_ptr_p->m_counter);
    if(!tmp.clearWaitingFor(nodeId)){
      jam();
      return;
    }
  }
  
  switch(trans_ptr_p->m_op.m_state){
  case DictObjOp::Preparing:{
    if(trans_ptr_p->m_errorCode != 0)
    {
      /**
       * Failed to prepare on atleast one node -> abort on all
       */
      trans_ptr_p->m_op.m_state = DictObjOp::Aborting;
      trans_ptr_p->m_callback.m_callbackData = trans_ptr_p->key;
      trans_ptr_p->m_callback.m_callbackFunction= 
	safe_cast(&Dbdict::trans_abort_start_done);
      
      if(f_dict_op[trans_ptr_p->m_op.m_vt_index].m_trans_abort_start)
      {
        jam();
	(this->*f_dict_op[trans_ptr_p->m_op.m_vt_index].m_trans_abort_start)
	  (signal, trans_ptr_p);
      }
      else
      {
        jam();
	execute(signal, trans_ptr_p->m_callback, 0);
      }
      return;
    }
    
    trans_ptr_p->m_op.m_state = DictObjOp::Prepared;
    trans_ptr_p->m_callback.m_callbackData = trans_ptr_p->key;
    trans_ptr_p->m_callback.m_callbackFunction= 
      safe_cast(&Dbdict::trans_commit_start_done);
    
    if(f_dict_op[trans_ptr_p->m_op.m_vt_index].m_trans_commit_start)
    {
      jam();
      (this->*f_dict_op[trans_ptr_p->m_op.m_vt_index].m_trans_commit_start)
	(signal, trans_ptr_p);
    }
    else
    {
      jam();
      execute(signal, trans_ptr_p->m_callback, 0);
    }
    return;
  }
  case DictObjOp::Committing: {
    ndbrequire(trans_ptr_p->m_errorCode == 0);

    trans_ptr_p->m_op.m_state = DictObjOp::Committed;
    trans_ptr_p->m_callback.m_callbackData = trans_ptr_p->key;
    trans_ptr_p->m_callback.m_callbackFunction= 
      safe_cast(&Dbdict::trans_commit_complete_done);
    
    if(f_dict_op[trans_ptr_p->m_op.m_vt_index].m_trans_commit_complete)
    {
      jam();
      (this->*f_dict_op[trans_ptr_p->m_op.m_vt_index].m_trans_commit_complete)
	(signal, trans_ptr_p);
    }
    else
    {
      jam();
      execute(signal, trans_ptr_p->m_callback, 0);
    }
    return;
  }
  case DictObjOp::Aborting:{
    trans_ptr_p->m_op.m_state = DictObjOp::Committed;
    trans_ptr_p->m_callback.m_callbackData = trans_ptr_p->key;
    trans_ptr_p->m_callback.m_callbackFunction= 
      safe_cast(&Dbdict::trans_abort_complete_done);

    if(f_dict_op[trans_ptr_p->m_op.m_vt_index].m_trans_abort_complete)
    {
      jam();
      (this->*f_dict_op[trans_ptr_p->m_op.m_vt_index].m_trans_abort_complete)
	(signal, trans_ptr_p);
    }
    else
    {
      jam();
      execute(signal, trans_ptr_p->m_callback, 0);
    }
    return;
  }
  case DictObjOp::Defined:
  case DictObjOp::Prepared:
  case DictObjOp::Committed:
  case DictObjOp::Aborted:
    jam();
    break;
  }
  ndbrequire(false);
}

void
Dbdict::trans_commit_start_done(Signal* signal, 
				Uint32 callbackData,
				Uint32 retValue)
{
  Ptr<SchemaTransaction> trans_ptr;

  jam();
  ndbrequire(retValue == 0);
  ndbrequire(c_Trans.find(trans_ptr, callbackData));
  NodeReceiverGroup rg(DBDICT, trans_ptr.p->m_nodes);
  SafeCounter tmp(c_counterMgr, trans_ptr.p->m_counter);
  tmp.init<DictCommitRef>(rg, GSN_DICT_COMMIT_REF, trans_ptr.p->key);
  
  DictCommitReq * const req = (DictCommitReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->senderData = trans_ptr.p->key;
  req->op_key = trans_ptr.p->m_op.m_key;
  sendSignal(rg, GSN_DICT_COMMIT_REQ, signal, DictCommitReq::SignalLength, 
	     JBB);
  trans_ptr.p->m_op.m_state = DictObjOp::Committing;
}

void
Dbdict::trans_commit_complete_done(Signal* signal, 
				   Uint32 callbackData,
				   Uint32 retValue)
{
  Ptr<SchemaTransaction> trans_ptr;

  jam();
  ndbrequire(retValue == 0);
  ndbrequire(c_Trans.find(trans_ptr, callbackData));

  DictLockReq lockReq;
  lockReq.userRef = reference();
  lockReq.userPtr = trans_ptr.i;

  switch(f_dict_op[trans_ptr.p->m_op.m_vt_index].m_gsn_user_req){
  case GSN_CREATE_FILEGROUP_REQ:{
    FilegroupPtr fg_ptr;
    jam();
    ndbrequire(c_filegroup_hash.find(fg_ptr, trans_ptr.p->m_op.m_obj_id));
    
    CreateFilegroupConf * conf = (CreateFilegroupConf*)signal->getDataPtr();
    conf->senderRef = reference();
    conf->senderData = trans_ptr.p->m_senderData;
    conf->filegroupId = fg_ptr.p->key;
    conf->filegroupVersion = fg_ptr.p->m_version;
    
    //@todo check api failed
    sendSignal(trans_ptr.p->m_senderRef, GSN_CREATE_FILEGROUP_CONF, signal, 
	       CreateFilegroupConf::SignalLength, JBB);

    lockReq.lockType = DictLockReq::CreateFilegroupLock;
    break;
  }
  case GSN_CREATE_FILE_REQ:{
    FilePtr f_ptr;
    jam();
    ndbrequire(c_file_hash.find(f_ptr, trans_ptr.p->m_op.m_obj_id));
    CreateFileConf * conf = (CreateFileConf*)signal->getDataPtr();
    conf->senderRef = reference();
    conf->senderData = trans_ptr.p->m_senderData;
    conf->fileId = f_ptr.p->key;
    conf->fileVersion = f_ptr.p->m_version;

    //@todo check api failed
    sendSignal(trans_ptr.p->m_senderRef, GSN_CREATE_FILE_CONF, signal, 
	       CreateFileConf::SignalLength, JBB);

    lockReq.lockType = DictLockReq::CreateFileLock;
    break;
  }
  case GSN_DROP_FILE_REQ:{
    DropFileConf * conf = (DropFileConf*)signal->getDataPtr();
    jam();
    conf->senderRef = reference();
    conf->senderData = trans_ptr.p->m_senderData;
    conf->fileId = trans_ptr.p->m_op.m_obj_id;
    
    //@todo check api failed
    sendSignal(trans_ptr.p->m_senderRef, GSN_DROP_FILE_CONF, signal, 
	       DropFileConf::SignalLength, JBB);

    lockReq.lockType = DictLockReq::DropFileLock;
    break;
  }
  case GSN_DROP_FILEGROUP_REQ:{
    DropFilegroupConf * conf = (DropFilegroupConf*)signal->getDataPtr();
    jam();
    conf->senderRef = reference();
    conf->senderData = trans_ptr.p->m_senderData;
    conf->filegroupId = trans_ptr.p->m_op.m_obj_id;
    
    //@todo check api failed
    sendSignal(trans_ptr.p->m_senderRef, GSN_DROP_FILEGROUP_CONF, signal, 
	       DropFilegroupConf::SignalLength, JBB);

    lockReq.lockType = DictLockReq::DropFilegroupLock;
    break;
  }
  default:
    ndbrequire(false);
  }
  
  dict_lock_unlock(signal, &lockReq);
  c_Trans.release(trans_ptr);
  return;
}

void
Dbdict::trans_abort_start_done(Signal* signal, 
			       Uint32 callbackData,
			       Uint32 retValue)
{
  Ptr<SchemaTransaction> trans_ptr;

  jam();
  ndbrequire(retValue == 0);
  ndbrequire(c_Trans.find(trans_ptr, callbackData));
  
  NodeReceiverGroup rg(DBDICT, trans_ptr.p->m_nodes);
  SafeCounter tmp(c_counterMgr, trans_ptr.p->m_counter);
  ndbrequire(tmp.init<DictAbortRef>(rg, trans_ptr.p->key));
  
  DictAbortReq * const req = (DictAbortReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->senderData = trans_ptr.p->key;
  req->op_key = trans_ptr.p->m_op.m_key;
  
  sendSignal(rg, GSN_DICT_ABORT_REQ, signal, DictAbortReq::SignalLength, JBB);
}

void
Dbdict::trans_abort_complete_done(Signal* signal, 
				  Uint32 callbackData,
				  Uint32 retValue)
{
  Ptr<SchemaTransaction> trans_ptr;

  jam();
  ndbrequire(retValue == 0);
  ndbrequire(c_Trans.find(trans_ptr, callbackData));

  DictLockReq lockReq;
  lockReq.userRef = reference();
  lockReq.userPtr = trans_ptr.i;

  switch(f_dict_op[trans_ptr.p->m_op.m_vt_index].m_gsn_user_req){
  case GSN_CREATE_FILEGROUP_REQ:
  {
    //
    CreateFilegroupRef * ref = (CreateFilegroupRef*)signal->getDataPtr();
    jam();
    ref->senderRef = reference();
    ref->senderData = trans_ptr.p->m_senderData;
    ref->masterNodeId = c_masterNodeId;
    ref->errorCode = trans_ptr.p->m_errorCode;
    ref->errorLine = 0;
    ref->errorKey = 0;
    ref->status = 0;

    //@todo check api failed
    sendSignal(trans_ptr.p->m_senderRef, GSN_CREATE_FILEGROUP_REF, signal, 
	       CreateFilegroupRef::SignalLength, JBB);

    lockReq.lockType = DictLockReq::CreateFilegroupLock;
    break;
  }
  case GSN_CREATE_FILE_REQ:
  {
    CreateFileRef * ref = (CreateFileRef*)signal->getDataPtr();
    jam();
    ref->senderRef = reference();
    ref->senderData = trans_ptr.p->m_senderData;
    ref->masterNodeId = c_masterNodeId;
    ref->errorCode = trans_ptr.p->m_errorCode;
    ref->errorLine = 0;
    ref->errorKey = 0;
    ref->status = 0;
    
    //@todo check api failed
    sendSignal(trans_ptr.p->m_senderRef, GSN_CREATE_FILE_REF, signal, 
	       CreateFileRef::SignalLength, JBB);

    lockReq.lockType = DictLockReq::CreateFileLock;
    break;
  }
  case GSN_DROP_FILE_REQ:
  {
    DropFileRef * ref = (DropFileRef*)signal->getDataPtr();
    jam();
    ref->senderRef = reference();
    ref->senderData = trans_ptr.p->m_senderData;
    ref->masterNodeId = c_masterNodeId;
    ref->errorCode = trans_ptr.p->m_errorCode;
    ref->errorLine = 0;
    ref->errorKey = 0;
    
    //@todo check api failed
    sendSignal(trans_ptr.p->m_senderRef, GSN_DROP_FILE_REF, signal, 
	       DropFileRef::SignalLength, JBB);

    lockReq.lockType = DictLockReq::DropFileLock;
    break;
  }
  case GSN_DROP_FILEGROUP_REQ:
  {
    //
    DropFilegroupRef * ref = (DropFilegroupRef*)signal->getDataPtr();
    jam();
    ref->senderRef = reference();
    ref->senderData = trans_ptr.p->m_senderData;
    ref->masterNodeId = c_masterNodeId;
    ref->errorCode = trans_ptr.p->m_errorCode;
    ref->errorLine = 0;
    ref->errorKey = 0;
    
    //@todo check api failed
    sendSignal(trans_ptr.p->m_senderRef, GSN_DROP_FILEGROUP_REF, signal, 
	       DropFilegroupRef::SignalLength, JBB);

    lockReq.lockType = DictLockReq::DropFilegroupLock;
    break;
  }
  default:
    ndbrequire(false);
  }
  
  dict_lock_unlock(signal, &lockReq);
  c_Trans.release(trans_ptr);
  return;
}

void
Dbdict::execCREATE_OBJ_REQ(Signal* signal)
{
  jamEntry();

  if(!assembleFragments(signal)){
    jam();
    return;
  }

  CreateObjReq * const req = (CreateObjReq*)signal->getDataPtr();
  const Uint32 gci = req->gci;
  const Uint32 objId = req->objId;
  const Uint32 objVersion = req->objVersion;
  const Uint32 objType = req->objType;
  const Uint32 requestInfo = req->requestInfo;

  SegmentedSectionPtr objInfoPtr;
  SectionHandle handle(this, signal);
  ndbrequire(handle.getSection(objInfoPtr, CreateObjReq::DICT_OBJ_INFO));
  handle.clear();
  
  CreateObjRecordPtr createObjPtr;  
  ndbrequire(c_opCreateObj.seize(createObjPtr));
  
  const Uint32 key = req->op_key;
  createObjPtr.p->key = key;
  c_opCreateObj.add(createObjPtr);
  createObjPtr.p->m_errorCode = 0;
  createObjPtr.p->m_senderRef = req->senderRef;
  createObjPtr.p->m_senderData = req->senderData;
  createObjPtr.p->m_clientRef = req->clientRef;
  createObjPtr.p->m_clientData = req->clientData;
  
  createObjPtr.p->m_gci = gci;
  createObjPtr.p->m_obj_id = objId;
  createObjPtr.p->m_obj_type = objType;
  createObjPtr.p->m_obj_version = objVersion;
  createObjPtr.p->m_obj_info_ptr_i = objInfoPtr.i;
  createObjPtr.p->m_obj_ptr_i = RNIL;

  createObjPtr.p->m_callback.m_callbackData = key;
  createObjPtr.p->m_callback.m_callbackFunction= 
    safe_cast(&Dbdict::createObj_prepare_start_done);

  createObjPtr.p->m_restart= 0;
  switch(objType){
  case DictTabInfo::Tablespace:
  case DictTabInfo::LogfileGroup:
    jam();
    createObjPtr.p->m_vt_index = 0;
    break;
  case DictTabInfo::Datafile:
  case DictTabInfo::Undofile:
    /**
     * Use restart code to impl. ForceCreateFile
     */
    if (requestInfo & CreateFileReq::ForceCreateFile)
    {
      jam();
      createObjPtr.p->m_restart= 2;
    }
    jam();
    createObjPtr.p->m_vt_index = 1;
    break;
  default:
    ndbrequire(false);
  }
  
  (this->*f_dict_op[createObjPtr.p->m_vt_index].m_prepare_start)
    (signal, createObjPtr.p);
}

void
Dbdict::execDICT_COMMIT_REQ(Signal* signal)
{
  DictCommitReq* req = (DictCommitReq*)signal->getDataPtr();
  Ptr<SchemaOperation> op;

  jamEntry();
  ndbrequire(c_schemaOperation.find(op, req->op_key));
  (this->*f_dict_op[op.p->m_vt_index].m_commit)(signal, op.p);
}

void
Dbdict::execDICT_ABORT_REQ(Signal* signal)
{
  DictAbortReq* req = (DictAbortReq*)signal->getDataPtr();
  Ptr<SchemaOperation> op;

  jamEntry();
  ndbrequire(c_schemaOperation.find(op, req->op_key));
  (this->*f_dict_op[op.p->m_vt_index].m_abort)(signal, op.p);
}

void
Dbdict::execDICT_COMMIT_REF(Signal* signal)
{
  DictCommitRef * const ref = (DictCommitRef*)signal->getDataPtr();
  Ptr<SchemaTransaction> trans_ptr;

  jamEntry();
  ndbrequire(c_Trans.find(trans_ptr, ref->senderData));
  if(ref->errorCode != DictCommitRef::NF_FakeErrorREF){
    jam();
    trans_ptr.p->setErrorCode(ref->errorCode);
  }
  Uint32 node = refToNode(ref->senderRef);
  schemaOperation_reply(signal, trans_ptr.p, node);
}

void
Dbdict::execDICT_COMMIT_CONF(Signal* signal)
{
  Ptr<SchemaTransaction> trans_ptr;
  DictCommitConf * const conf = (DictCommitConf*)signal->getDataPtr();

  jamEntry();
  ndbrequire(c_Trans.find(trans_ptr, conf->senderData));
  schemaOperation_reply(signal, trans_ptr.p, refToNode(conf->senderRef));
}

void
Dbdict::execDICT_ABORT_REF(Signal* signal)
{
  DictAbortRef * const ref = (DictAbortRef*)signal->getDataPtr();
  Ptr<SchemaTransaction> trans_ptr;

  jamEntry();
  ndbrequire(c_Trans.find(trans_ptr, ref->senderData));
  if(ref->errorCode != DictAbortRef::NF_FakeErrorREF){
    jam();
    trans_ptr.p->setErrorCode(ref->errorCode);
  }
  Uint32 node = refToNode(ref->senderRef);
  schemaOperation_reply(signal, trans_ptr.p, node);
}

void
Dbdict::execDICT_ABORT_CONF(Signal* signal)
{
  DictAbortConf * const conf = (DictAbortConf*)signal->getDataPtr();
  Ptr<SchemaTransaction> trans_ptr;

  jamEntry();
  ndbrequire(c_Trans.find(trans_ptr, conf->senderData));
  schemaOperation_reply(signal, trans_ptr.p, refToNode(conf->senderRef));
}

void
Dbdict::createObj_prepare_start_done(Signal* signal,
				     Uint32 callbackData, 
				     Uint32 returnCode)
{
  jam();
  CreateObjRecordPtr createObjPtr;  
  
  ndbrequire(returnCode == 0);
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  SectionHandle handle(this, createObjPtr.p->m_obj_info_ptr_i);

  if(createObjPtr.p->m_errorCode != 0)
  {
    jam();
    createObjPtr.p->m_obj_info_ptr_i= RNIL;
    releaseSections(handle);
    createObj_prepare_complete_done(signal, callbackData, 0);
    return;
  }
  
  SchemaFile::TableEntry tabEntry;
  bzero(&tabEntry, sizeof(tabEntry));
  tabEntry.m_tableVersion = createObjPtr.p->m_obj_version;
  tabEntry.m_tableType    = createObjPtr.p->m_obj_type;
  tabEntry.m_tableState   = SchemaFile::ADD_STARTED;
  tabEntry.m_gcp          = createObjPtr.p->m_gci;
  tabEntry.m_info_words   = handle.m_ptr[0].sz;
  
  Callback cb;
  cb.m_callbackData = createObjPtr.p->key;
  cb.m_callbackFunction = safe_cast(&Dbdict::createObj_writeSchemaConf1);
  
  updateSchemaState(signal, createObjPtr.p->m_obj_id, &tabEntry, &cb);
  handle.clear();
}

void
Dbdict::createObj_writeSchemaConf1(Signal* signal, 
				   Uint32 callbackData,
				   Uint32 returnCode)
{
  CreateObjRecordPtr createObjPtr;  
  Callback callback;

  jam();
  ndbrequire(returnCode == 0);
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  
  callback.m_callbackData = createObjPtr.p->key;
  callback.m_callbackFunction = safe_cast(&Dbdict::createObj_writeObjConf);
  
  SectionHandle handle(this, createObjPtr.p->m_obj_info_ptr_i);
  writeTableFile(signal, createObjPtr.p->m_obj_id, handle.m_ptr[0], &callback);
  
  releaseSections(handle);
  createObjPtr.p->m_obj_info_ptr_i = RNIL;
}

void
Dbdict::createObj_writeObjConf(Signal* signal, 
			       Uint32 callbackData,
			       Uint32 returnCode)
{
  CreateObjRecordPtr createObjPtr;

  jam();
  ndbrequire(returnCode == 0);
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  createObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::createObj_prepare_complete_done);
  (this->*f_dict_op[createObjPtr.p->m_vt_index].m_prepare_complete)
    (signal, createObjPtr.p);
}

void
Dbdict::createObj_prepare_complete_done(Signal* signal, 
					Uint32 callbackData,
					Uint32 returnCode)
{
  CreateObjRecordPtr createObjPtr;

  jam();
  ndbrequire(returnCode == 0);
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  
  //@todo check for master failed
  
  if(createObjPtr.p->m_errorCode == 0){
    jam();
    
    CreateObjConf * const conf = (CreateObjConf*)signal->getDataPtr();
    conf->senderRef = reference();
    conf->senderData = createObjPtr.p->m_senderData;
    sendSignal(createObjPtr.p->m_senderRef, GSN_CREATE_OBJ_CONF,
	       signal, CreateObjConf::SignalLength, JBB);
    return;
  }
  
  CreateObjRef * const ref = (CreateObjRef*)signal->getDataPtr();
  ref->senderRef = reference();
  ref->senderData = createObjPtr.p->m_senderData;
  ref->errorCode = createObjPtr.p->m_errorCode;
  ref->errorLine = 0;
  ref->errorKey = 0;
  ref->errorStatus = 0;
  
  sendSignal(createObjPtr.p->m_senderRef, GSN_CREATE_OBJ_REF,
	     signal, CreateObjRef::SignalLength, JBB);
}

void
Dbdict::createObj_commit(Signal * signal, SchemaOperation * op)
{
  OpCreateObj * createObj = (OpCreateObj*)op;

  createObj->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::createObj_commit_start_done);
  if (f_dict_op[createObj->m_vt_index].m_commit_start)
  {
    jam();
    (this->*f_dict_op[createObj->m_vt_index].m_commit_start)(signal, createObj);
  }
  else
  {
    jam();
    execute(signal, createObj->m_callback, 0);
  }
}

void
Dbdict::createObj_commit_start_done(Signal* signal, 
				    Uint32 callbackData,
				    Uint32 returnCode)
{
  CreateObjRecordPtr createObjPtr;  
  
  jam();
  ndbrequire(returnCode == 0);
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  
  Uint32 objId = createObjPtr.p->m_obj_id;
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  SchemaFile::TableEntry objEntry = * getTableEntry(xsf, objId);
  objEntry.m_tableState = SchemaFile::TABLE_ADD_COMMITTED;
  
  Callback callback;
  callback.m_callbackData = createObjPtr.p->key;
  callback.m_callbackFunction = 
    safe_cast(&Dbdict::createObj_writeSchemaConf2);
  
  updateSchemaState(signal, createObjPtr.p->m_obj_id, &objEntry, &callback);

}

void
Dbdict::createObj_writeSchemaConf2(Signal* signal, 
				   Uint32 callbackData,
				   Uint32 returnCode)
{
  CreateObjRecordPtr createObjPtr;

  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  createObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::createObj_commit_complete_done);
  if (f_dict_op[createObjPtr.p->m_vt_index].m_commit_complete)
  {
    jam();
    (this->*f_dict_op[createObjPtr.p->m_vt_index].m_commit_complete)
      (signal, createObjPtr.p);
  }
  else
  {
    jam();
    execute(signal, createObjPtr.p->m_callback, 0);
  }
  
}

void
Dbdict::createObj_commit_complete_done(Signal* signal, 
				       Uint32 callbackData,
				       Uint32 returnCode)
{
  CreateObjRecordPtr createObjPtr;

  jam();
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  
  //@todo check error
  //@todo check master failed
  
  DictCommitConf * const conf = (DictCommitConf*)signal->getDataPtr();
  conf->senderRef = reference();
  conf->senderData = createObjPtr.p->m_senderData;
  sendSignal(createObjPtr.p->m_senderRef, GSN_DICT_COMMIT_CONF,
	     signal, DictCommitConf::SignalLength, JBB);
  
  c_opCreateObj.release(createObjPtr);
}

void
Dbdict::createObj_abort(Signal* signal, SchemaOperation* op)
{
  OpCreateObj * createObj = (OpCreateObj*)op;
  
  createObj->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::createObj_abort_start_done);
  if (f_dict_op[createObj->m_vt_index].m_abort_start)
  {
    jam();
    (this->*f_dict_op[createObj->m_vt_index].m_abort_start)(signal, createObj);
  }
  else
  {
    jam();
    execute(signal, createObj->m_callback, 0);
  }
}

void
Dbdict::createObj_abort_start_done(Signal* signal, 
				   Uint32 callbackData,
				   Uint32 returnCode)
{
  CreateObjRecordPtr createObjPtr;

  jam();
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  SchemaFile::TableEntry objEntry = * getTableEntry(xsf, 
						    createObjPtr.p->m_obj_id);
  objEntry.m_tableState = SchemaFile::DROP_TABLE_COMMITTED;
  
  Callback callback;
  callback.m_callbackData = createObjPtr.p->key;
  callback.m_callbackFunction = 
    safe_cast(&Dbdict::createObj_abort_writeSchemaConf);
  
  updateSchemaState(signal, createObjPtr.p->m_obj_id, &objEntry, &callback);
}

void
Dbdict::createObj_abort_writeSchemaConf(Signal* signal, 
					Uint32 callbackData,
					Uint32 returnCode)
{
  CreateObjRecordPtr createObjPtr;

  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));
  createObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::createObj_abort_complete_done);
  
  if (f_dict_op[createObjPtr.p->m_vt_index].m_abort_complete)
  {
    jam();
    (this->*f_dict_op[createObjPtr.p->m_vt_index].m_abort_complete)
      (signal, createObjPtr.p);
  }
  else
  {
    jam();
    execute(signal, createObjPtr.p->m_callback, 0);
  }
}

void
Dbdict::createObj_abort_complete_done(Signal* signal, 
				      Uint32 callbackData,
				      Uint32 returnCode)
{
  CreateObjRecordPtr createObjPtr;

  jam();
  ndbrequire(c_opCreateObj.find(createObjPtr, callbackData));

  DictAbortConf * const conf = (DictAbortConf*)signal->getDataPtr();
  conf->senderRef = reference();
  conf->senderData = createObjPtr.p->m_senderData;
  sendSignal(createObjPtr.p->m_senderRef, GSN_DICT_ABORT_CONF,
	     signal, DictAbortConf::SignalLength, JBB);
  
  c_opCreateObj.release(createObjPtr);
}

void
Dbdict::execDROP_OBJ_REQ(Signal* signal)
{
  jamEntry();

  if(!assembleFragments(signal)){
    jam();
    return;
  }
  
  DropObjReq * const req = (DropObjReq*)signal->getDataPtr();

  const Uint32 objId = req->objId;
  const Uint32 objVersion = req->objVersion;
  const Uint32 objType = req->objType;
  
  DropObjRecordPtr dropObjPtr;  
  ndbrequire(c_opDropObj.seize(dropObjPtr));
  
  const Uint32 key = req->op_key;
  dropObjPtr.p->key = key;
  c_opDropObj.add(dropObjPtr);
  dropObjPtr.p->m_errorCode = 0;
  dropObjPtr.p->m_senderRef = req->senderRef;
  dropObjPtr.p->m_senderData = req->senderData;
  dropObjPtr.p->m_clientRef = req->clientRef;
  dropObjPtr.p->m_clientData = req->clientData;
  
  dropObjPtr.p->m_obj_id = objId;
  dropObjPtr.p->m_obj_type = objType;
  dropObjPtr.p->m_obj_version = objVersion;

  dropObjPtr.p->m_callback.m_callbackData = key;
  dropObjPtr.p->m_callback.m_callbackFunction= 
    safe_cast(&Dbdict::dropObj_prepare_start_done);

  switch(objType){
  case DictTabInfo::Tablespace:
  case DictTabInfo::LogfileGroup:
  {
    Ptr<Filegroup> fg_ptr;
    jam();
    dropObjPtr.p->m_vt_index = 3;
    ndbrequire(c_filegroup_hash.find(fg_ptr, objId));
    dropObjPtr.p->m_obj_ptr_i = fg_ptr.i;
    break;
  
  }
  case DictTabInfo::Datafile:
  {
    Ptr<File> file_ptr;
    jam();
    dropObjPtr.p->m_vt_index = 2;
    ndbrequire(c_file_hash.find(file_ptr, objId));
    dropObjPtr.p->m_obj_ptr_i = file_ptr.i;
    break;
  }
  case DictTabInfo::Undofile:
  {
    jam();
    dropObjPtr.p->m_vt_index = 4;
    return;
  }
  default:
    ndbrequire(false);
  }
  
  signal->header.m_noOfSections = 0;
  (this->*f_dict_op[dropObjPtr.p->m_vt_index].m_prepare_start)
    (signal, dropObjPtr.p);
}

void
Dbdict::dropObj_prepare_start_done(Signal* signal, 
				   Uint32 callbackData,
				   Uint32 returnCode)
{
  DropObjRecordPtr dropObjPtr;
  Callback cb;

  ndbrequire(returnCode == 0);
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));

  cb.m_callbackData = callbackData;
  cb.m_callbackFunction = 
    safe_cast(&Dbdict::dropObj_prepare_writeSchemaConf);

  if(dropObjPtr.p->m_errorCode != 0)
  {
    jam();
    dropObj_prepare_complete_done(signal, callbackData, 0);
    return;
  }
  jam();  
  Uint32 objId = dropObjPtr.p->m_obj_id;
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  SchemaFile::TableEntry objEntry = *getTableEntry(xsf, objId);
  objEntry.m_tableState = SchemaFile::DROP_TABLE_STARTED;
  updateSchemaState(signal, objId, &objEntry, &cb);
}

void
Dbdict::dropObj_prepare_writeSchemaConf(Signal* signal, 
					Uint32 callbackData,
					Uint32 returnCode)
{
  DropObjRecordPtr dropObjPtr;

  ndbrequire(returnCode == 0);
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  dropObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::dropObj_prepare_complete_done);
  if(f_dict_op[dropObjPtr.p->m_vt_index].m_prepare_complete)
  {
    jam();
    (this->*f_dict_op[dropObjPtr.p->m_vt_index].m_prepare_complete)
      (signal, dropObjPtr.p);
  }
  else
  {
    jam();
    execute(signal, dropObjPtr.p->m_callback, 0);
  }
}

void
Dbdict::dropObj_prepare_complete_done(Signal* signal, 
				      Uint32 callbackData,
				      Uint32 returnCode)
{
  DropObjRecordPtr dropObjPtr;

  ndbrequire(returnCode == 0);
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  jam();
  
  //@todo check for master failed
  
  if(dropObjPtr.p->m_errorCode == 0){
    jam();
    
    DropObjConf * const conf = (DropObjConf*)signal->getDataPtr();
    conf->senderRef = reference();
    conf->senderData = dropObjPtr.p->m_senderData;
    sendSignal(dropObjPtr.p->m_senderRef, GSN_DROP_OBJ_CONF,
	       signal, DropObjConf::SignalLength, JBB);
    return;
  }
  
  DropObjRef * const ref = (DropObjRef*)signal->getDataPtr();
  ref->senderRef = reference();
  ref->senderData = dropObjPtr.p->m_senderData;
  ref->errorCode = dropObjPtr.p->m_errorCode;
  
  sendSignal(dropObjPtr.p->m_senderRef, GSN_DROP_OBJ_REF,
	     signal, DropObjRef::SignalLength, JBB);

}

void
Dbdict::dropObj_commit(Signal * signal, SchemaOperation * op)
{
  OpDropObj * dropObj = (OpDropObj*)op;

  dropObj->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::dropObj_commit_start_done);
  if (f_dict_op[dropObj->m_vt_index].m_commit_start)
  {
    jam();
    (this->*f_dict_op[dropObj->m_vt_index].m_commit_start)(signal, dropObj);
  }
  else
  {
    jam();
    execute(signal, dropObj->m_callback, 0);
  }
}

void
Dbdict::dropObj_commit_start_done(Signal* signal, 
				  Uint32 callbackData,
				  Uint32 returnCode)
{
  DropObjRecordPtr dropObjPtr;

  jam();
  ndbrequire(returnCode == 0);
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  
  Uint32 objId = dropObjPtr.p->m_obj_id;
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  SchemaFile::TableEntry objEntry = * getTableEntry(xsf, objId);
  objEntry.m_tableState = SchemaFile::DROP_TABLE_COMMITTED;
  
  Callback callback;
  callback.m_callbackData = dropObjPtr.p->key;
  callback.m_callbackFunction = 
    safe_cast(&Dbdict::dropObj_commit_writeSchemaConf);
  
  updateSchemaState(signal, objId, &objEntry, &callback);
}

void
Dbdict::dropObj_commit_writeSchemaConf(Signal* signal, 
				       Uint32 callbackData,
				       Uint32 returnCode)
{
  DropObjRecordPtr dropObjPtr;

  jam();
  ndbrequire(returnCode == 0);
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  dropObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::dropObj_commit_complete_done);
  
  if(f_dict_op[dropObjPtr.p->m_vt_index].m_commit_complete)
  {
    jam();
    (this->*f_dict_op[dropObjPtr.p->m_vt_index].m_commit_complete)
      (signal, dropObjPtr.p);
  }
  else
  {
    jam();
    execute(signal, dropObjPtr.p->m_callback, 0);
  }
}

void
Dbdict::dropObj_commit_complete_done(Signal* signal, 
				     Uint32 callbackData,
				     Uint32 returnCode)
{
  DropObjRecordPtr dropObjPtr;

  jam();
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  
  //@todo check error
  //@todo check master failed
  
  DictCommitConf * const conf = (DictCommitConf*)signal->getDataPtr();
  conf->senderRef = reference();
  conf->senderData = dropObjPtr.p->m_senderData;
  sendSignal(dropObjPtr.p->m_senderRef, GSN_DICT_COMMIT_CONF,
	     signal, DictCommitConf::SignalLength, JBB);
  c_opDropObj.release(dropObjPtr);
}

void
Dbdict::dropObj_abort(Signal * signal, SchemaOperation * op)
{
  OpDropObj * dropObj = (OpDropObj*)op;

  dropObj->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::dropObj_abort_start_done);
  if (f_dict_op[dropObj->m_vt_index].m_abort_start)
  {
    jam();
    (this->*f_dict_op[dropObj->m_vt_index].m_abort_start)(signal, dropObj);
  }
  else
  {
    jam();
    execute(signal, dropObj->m_callback, 0);
  }
}

void
Dbdict::dropObj_abort_start_done(Signal* signal, 
				 Uint32 callbackData,
				 Uint32 returnCode)
{
  DropObjRecordPtr dropObjPtr;

  jam();
  ndbrequire(returnCode == 0);
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  
  XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
  SchemaFile::TableEntry objEntry = * getTableEntry(xsf, 
						    dropObjPtr.p->m_obj_id);

  Callback callback;
  callback.m_callbackData = dropObjPtr.p->key;
  callback.m_callbackFunction = 
    safe_cast(&Dbdict::dropObj_abort_writeSchemaConf);
  
  if (objEntry.m_tableState == SchemaFile::DROP_TABLE_STARTED)
  {
    jam();
    objEntry.m_tableState = SchemaFile::TABLE_ADD_COMMITTED;
        
    updateSchemaState(signal, dropObjPtr.p->m_obj_id, &objEntry, &callback);
  }
  else
  {
    jam();
    execute(signal, callback, 0);
  }
}

void
Dbdict::dropObj_abort_writeSchemaConf(Signal* signal, 
				      Uint32 callbackData,
				      Uint32 returnCode)
{
  DropObjRecordPtr dropObjPtr;

  ndbrequire(returnCode == 0);
  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  dropObjPtr.p->m_callback.m_callbackFunction = 
    safe_cast(&Dbdict::dropObj_abort_complete_done);
  
  if(f_dict_op[dropObjPtr.p->m_vt_index].m_abort_complete)
  {
    jam();
    (this->*f_dict_op[dropObjPtr.p->m_vt_index].m_abort_complete)
      (signal, dropObjPtr.p);
  }
  else
  {
    jam();
    execute(signal, dropObjPtr.p->m_callback, 0);
  }
}

void
Dbdict::dropObj_abort_complete_done(Signal* signal, 
				    Uint32 callbackData,
				    Uint32 returnCode)
{
  DropObjRecordPtr dropObjPtr;
  DictAbortConf * const conf = (DictAbortConf*)signal->getDataPtr();

  ndbrequire(c_opDropObj.find(dropObjPtr, callbackData));
  jam();
  conf->senderRef = reference();
  conf->senderData = dropObjPtr.p->m_senderData;
  sendSignal(dropObjPtr.p->m_senderRef, GSN_DICT_ABORT_CONF,
	     signal, DictAbortConf::SignalLength, JBB);
  c_opDropObj.release(dropObjPtr);
}

void 
Dbdict::create_fg_prepare_start(Signal* signal, SchemaOperation* op)
{
  /**
   * Put data into table record
   */
  jam();
  SegmentedSectionPtr objInfoPtr;
  getSection(objInfoPtr, ((OpCreateObj*)op)->m_obj_info_ptr_i);
  SimplePropertiesSectionReader it(objInfoPtr, getSectionSegmentPool());

  Ptr<DictObject> obj_ptr; obj_ptr.setNull();
  FilegroupPtr fg_ptr; fg_ptr.setNull();
  
  SimpleProperties::UnpackStatus status;
  DictFilegroupInfo::Filegroup fg; fg.init();
  do {
    status = SimpleProperties::unpack(it, &fg, 
				      DictFilegroupInfo::Mapping, 
				      DictFilegroupInfo::MappingSize, 
				      true, true);
    
    if(status != SimpleProperties::Eof)
    {
      jam();
      op->m_errorCode = CreateTableRef::InvalidFormat;
      break;
    }

    if(fg.FilegroupType == DictTabInfo::Tablespace)
    {
      if(!fg.TS_ExtentSize)
      {
        jam();
	op->m_errorCode = CreateFilegroupRef::InvalidExtentSize;
	break;
      }
    } 
    else if(fg.FilegroupType == DictTabInfo::LogfileGroup)
    {
      /**
       * undo_buffer_size can't be less than 96KB in LGMAN block 
       */
      if(fg.LF_UndoBufferSize < 3 * File_formats::NDB_PAGE_SIZE)
      {
        jam();
	op->m_errorCode = CreateFilegroupRef::InvalidUndoBufferSize;
	break;
      }
    }
    
    Uint32 len = strlen(fg.FilegroupName) + 1;
    Uint32 hash = Rope::hash(fg.FilegroupName, len);
    if(get_object(fg.FilegroupName, len, hash) != 0){
      jam();
      op->m_errorCode = CreateTableRef::TableAlreadyExist;
      break;
    }
    
    if(!c_obj_pool.seize(obj_ptr)){
      jam();
      op->m_errorCode = CreateTableRef::NoMoreTableRecords;
      break;
    }
    new (obj_ptr.p) DictObject;
    
    if(!c_filegroup_pool.seize(fg_ptr)){
      jam();
      op->m_errorCode = CreateTableRef::NoMoreTableRecords;
      break;
    }
  
    new (fg_ptr.p) Filegroup();

    {
      Rope name(c_rope_pool, obj_ptr.p->m_name);
      if(!name.assign(fg.FilegroupName, len, hash)){
        jam();
	op->m_errorCode = CreateTableRef::OutOfStringBuffer;
	break;
      }
    }
    
    fg_ptr.p->key = op->m_obj_id;
    fg_ptr.p->m_obj_ptr_i = obj_ptr.i;
    fg_ptr.p->m_type = fg.FilegroupType;
    fg_ptr.p->m_version = op->m_obj_version;
    fg_ptr.p->m_name = obj_ptr.p->m_name;

    switch(fg.FilegroupType){
    case DictTabInfo::Tablespace:
    {
      //fg.TS_DataGrow = group.m_grow_spec;
      fg_ptr.p->m_tablespace.m_extent_size = fg.TS_ExtentSize;
      fg_ptr.p->m_tablespace.m_default_logfile_group_id = fg.TS_LogfileGroupId;

      Ptr<Filegroup> lg_ptr;
      if (!c_filegroup_hash.find(lg_ptr, fg.TS_LogfileGroupId))
      {
        jam();
	op->m_errorCode = CreateFilegroupRef::NoSuchLogfileGroup;
	goto error;
      }

      if (lg_ptr.p->m_version != fg.TS_LogfileGroupVersion)
      {
        jam();
	op->m_errorCode = CreateFilegroupRef::InvalidFilegroupVersion;
	goto error;
      }
      increase_ref_count(lg_ptr.p->m_obj_ptr_i);
      break;
    }
    case DictTabInfo::LogfileGroup:
    {
      jam();
      fg_ptr.p->m_logfilegroup.m_undo_buffer_size = fg.LF_UndoBufferSize;
      fg_ptr.p->m_logfilegroup.m_files.init();
      //fg.LF_UndoGrow = ;
      break;
    }
    default:
      ndbrequire(false);
    }

    obj_ptr.p->m_id = op->m_obj_id;
    obj_ptr.p->m_type = fg.FilegroupType;
    obj_ptr.p->m_ref_count = 0;
    c_obj_hash.add(obj_ptr);
    c_filegroup_hash.add(fg_ptr);
    
    op->m_obj_ptr_i = fg_ptr.i;
  } while(0);

error:
  if (op->m_errorCode)
  {
    jam();
    if (!fg_ptr.isNull())
    {
      jam();
      c_filegroup_pool.release(fg_ptr);
    }

    if (!obj_ptr.isNull())
    {
      jam();
      c_obj_pool.release(obj_ptr);
    }
  }
  
  execute(signal, op->m_callback, 0);
}

void
Dbdict::create_fg_prepare_complete(Signal* signal, SchemaOperation* op)
{
  /**
   * CONTACT TSMAN LGMAN PGMAN 
   */
  CreateFilegroupImplReq* req = 
    (CreateFilegroupImplReq*)signal->getDataPtrSend();
  jam();
  req->senderData = op->key;
  req->senderRef = reference();
  req->filegroup_id = op->m_obj_id;
  req->filegroup_version = op->m_obj_version;

  FilegroupPtr fg_ptr;
  c_filegroup_pool.getPtr(fg_ptr, op->m_obj_ptr_i);
  
  Uint32 ref= 0;
  Uint32 len= 0;
  switch(op->m_obj_type){
  case DictTabInfo::Tablespace:
  {
    jam();
    ref = TSMAN_REF;
    len = CreateFilegroupImplReq::TablespaceLength;
    req->tablespace.extent_size = fg_ptr.p->m_tablespace.m_extent_size;
    req->tablespace.logfile_group_id = 
      fg_ptr.p->m_tablespace.m_default_logfile_group_id;
    break;
  }
  case DictTabInfo::LogfileGroup:
  {
    jam();
    ref = LGMAN_REF;
    len = CreateFilegroupImplReq::LogfileGroupLength;
    req->logfile_group.buffer_size = 
      fg_ptr.p->m_logfilegroup.m_undo_buffer_size;
    break;
  }
  default:
    ndbrequire(false);
  }
  
  sendSignal(ref, GSN_CREATE_FILEGROUP_REQ, signal, len, JBB);
}

void
Dbdict::execCREATE_FILEGROUP_REF(Signal* signal)
{
  CreateFilegroupImplRef * ref = (CreateFilegroupImplRef*)signal->getDataPtr();
  CreateObjRecordPtr op_ptr;
  jamEntry();
  ndbrequire(c_opCreateObj.find(op_ptr, ref->senderData));
  op_ptr.p->m_errorCode = ref->errorCode;

  execute(signal, op_ptr.p->m_callback, 0);  
}  

void
Dbdict::execCREATE_FILEGROUP_CONF(Signal* signal)
{
  CreateFilegroupImplConf * rep = 
    (CreateFilegroupImplConf*)signal->getDataPtr();
  CreateObjRecordPtr op_ptr;
  jamEntry();
  ndbrequire(c_opCreateObj.find(op_ptr, rep->senderData));
  
  execute(signal, op_ptr.p->m_callback, 0);  
}

void
Dbdict::create_fg_abort_start(Signal* signal, SchemaOperation* op){
  (void) signal->getDataPtrSend();

  if (op->m_obj_ptr_i != RNIL)
  {
    jam();
    send_drop_fg(signal, op, DropFilegroupImplReq::Commit);
    return;
  }
  jam();  
  execute(signal, op->m_callback, 0);  
}

void
Dbdict::create_fg_abort_complete(Signal* signal, SchemaOperation* op)
{
  if (op->m_obj_ptr_i != RNIL)
  {
    jam();
    FilegroupPtr fg_ptr;
    c_filegroup_pool.getPtr(fg_ptr, op->m_obj_ptr_i);
    
    release_object(fg_ptr.p->m_obj_ptr_i);
    c_filegroup_hash.release(fg_ptr);
  }
  jam();  
  execute(signal, op->m_callback, 0);
}

void 
Dbdict::create_file_prepare_start(Signal* signal, SchemaOperation* op)
{
  /**
   * Put data into table record
   */
  SegmentedSectionPtr objInfoPtr;
  getSection(objInfoPtr, ((OpCreateObj*)op)->m_obj_info_ptr_i);
  SimplePropertiesSectionReader it(objInfoPtr, getSectionSegmentPool());
  
  Ptr<DictObject> obj_ptr; obj_ptr.setNull();
  FilePtr filePtr; filePtr.setNull();

  DictFilegroupInfo::File f; f.init();
  SimpleProperties::UnpackStatus status;
  status = SimpleProperties::unpack(it, &f, 
				    DictFilegroupInfo::FileMapping, 
				    DictFilegroupInfo::FileMappingSize, 
				    true, true);
  
  do {
    if(status != SimpleProperties::Eof){
      jam();
      op->m_errorCode = CreateFileRef::InvalidFormat;
      break;
    }

    // Get Filegroup
    FilegroupPtr fg_ptr;
    if(!c_filegroup_hash.find(fg_ptr, f.FilegroupId)){
      jam();
      op->m_errorCode = CreateFileRef::NoSuchFilegroup;
      break;
    }
    
    if(fg_ptr.p->m_version != f.FilegroupVersion){
      jam();
      op->m_errorCode = CreateFileRef::InvalidFilegroupVersion;
      break;
    }

    switch(f.FileType){
    case DictTabInfo::Datafile:
    {
      if(fg_ptr.p->m_type != DictTabInfo::Tablespace)
      {
        jam();
	op->m_errorCode = CreateFileRef::InvalidFileType;
      }
      jam();
      break;
    }
    case DictTabInfo::Undofile:
    {
      if(fg_ptr.p->m_type != DictTabInfo::LogfileGroup)
      {
        jam();
	op->m_errorCode = CreateFileRef::InvalidFileType;
      }
      jam();
      break;
    }
    default:
      jam();
      op->m_errorCode = CreateFileRef::InvalidFileType;
    }
    
    if(op->m_errorCode)
    {
      jam();
      break;
    }

    Uint32 len = strlen(f.FileName) + 1;
    Uint32 hash = Rope::hash(f.FileName, len);
    if(get_object(f.FileName, len, hash) != 0){
      jam();
      op->m_errorCode = CreateFileRef::FilenameAlreadyExists;
      break;
    }
    
    {
      Uint32 dl;
      const ndb_mgm_configuration_iterator * p = 
	m_ctx.m_config.getOwnConfigIterator();
      if(!ndb_mgm_get_int_parameter(p, CFG_DB_DISCLESS, &dl) && dl)
      {
        jam();
	op->m_errorCode = CreateFileRef::NotSupportedWhenDiskless;
	break;
      }
    }
    
    // Loop through all filenames...
    if(!c_obj_pool.seize(obj_ptr)){
      jam();
      op->m_errorCode = CreateTableRef::NoMoreTableRecords;
      break;
    }
    new (obj_ptr.p) DictObject;
    
    if (! c_file_pool.seize(filePtr)){
      jam();
      op->m_errorCode = CreateFileRef::OutOfFileRecords;
      break;
    }

    new (filePtr.p) File();

    {
      Rope name(c_rope_pool, obj_ptr.p->m_name);
      if(!name.assign(f.FileName, len, hash)){
        jam();
	op->m_errorCode = CreateTableRef::OutOfStringBuffer;
	break;
      }
    }

    switch(fg_ptr.p->m_type){
    case DictTabInfo::Tablespace:
    {
      jam();
      increase_ref_count(fg_ptr.p->m_obj_ptr_i);
      break;
    }
    case DictTabInfo::LogfileGroup:
    {
      jam();
      Local_file_list list(c_file_pool, fg_ptr.p->m_logfilegroup.m_files);
      list.add(filePtr);
      break;
    }
    default:
      ndbrequire(false);
    }
    
    /**
     * Init file
     */
    filePtr.p->key = op->m_obj_id;
    filePtr.p->m_file_size = ((Uint64)f.FileSizeHi) << 32 | f.FileSizeLo;
    filePtr.p->m_path = obj_ptr.p->m_name;
    filePtr.p->m_obj_ptr_i = obj_ptr.i;
    filePtr.p->m_filegroup_id = f.FilegroupId;
    filePtr.p->m_type = f.FileType;
    filePtr.p->m_version = op->m_obj_version;
    
    obj_ptr.p->m_id = op->m_obj_id;
    obj_ptr.p->m_type = f.FileType;
    obj_ptr.p->m_ref_count = 0;
    c_obj_hash.add(obj_ptr);
    c_file_hash.add(filePtr);

    op->m_obj_ptr_i = filePtr.i;
  } while(0);

  if (op->m_errorCode)
  {
    jam();
    if (!filePtr.isNull())
    {
      jam();
      c_file_pool.release(filePtr);
    }

    if (!obj_ptr.isNull())
    {
      jam();
      c_obj_pool.release(obj_ptr);
    }
  }
  execute(signal, op->m_callback, 0);
}


void
Dbdict::create_file_prepare_complete(Signal* signal, SchemaOperation* op)
{
  /**
   * CONTACT TSMAN LGMAN PGMAN 
   */
  CreateFileImplReq* req = (CreateFileImplReq*)signal->getDataPtrSend();
  FilePtr f_ptr;
  FilegroupPtr fg_ptr;

  jam();
  c_file_pool.getPtr(f_ptr, op->m_obj_ptr_i);
  ndbrequire(c_filegroup_hash.find(fg_ptr, f_ptr.p->m_filegroup_id));

  req->senderData = op->key;
  req->senderRef = reference();
  switch(((OpCreateObj*)op)->m_restart){
  case 0:
  {
    jam();
    req->requestInfo = CreateFileImplReq::Create;
    break;
  }
  case 1:
  {
    jam();
    req->requestInfo = CreateFileImplReq::Open;
    break;
  }
  case 2:
  {
    jam();
    req->requestInfo = CreateFileImplReq::CreateForce;
    break;
  }
  }
	 
  req->file_id = f_ptr.p->key;
  req->filegroup_id = f_ptr.p->m_filegroup_id;
  req->filegroup_version = fg_ptr.p->m_version;
  req->file_size_hi = f_ptr.p->m_file_size >> 32;
  req->file_size_lo = f_ptr.p->m_file_size & 0xFFFFFFFF;

  Uint32 ref= 0;
  Uint32 len= 0;
  switch(op->m_obj_type){
  case DictTabInfo::Datafile:
  {
    jam();
    ref = TSMAN_REF;
    len = CreateFileImplReq::DatafileLength;
    req->tablespace.extent_size = fg_ptr.p->m_tablespace.m_extent_size;
    break;
  }
  case DictTabInfo::Undofile:
  {
    jam();
    ref = LGMAN_REF;
    len = CreateFileImplReq::UndofileLength;
    break;
  }
  default:
    ndbrequire(false);
  }
  
  char name[MAX_TAB_NAME_SIZE];
  ConstRope tmp(c_rope_pool, f_ptr.p->m_path);
  tmp.copy(name);
  LinearSectionPtr ptr[3];
  ptr[0].p = (Uint32*)&name[0];
  ptr[0].sz = (strlen(name)+1+3)/4;
  sendSignal(ref, GSN_CREATE_FILE_REQ, signal, len, JBB, ptr, 1);
}

void
Dbdict::execCREATE_FILE_REF(Signal* signal)
{
  CreateFileImplRef * ref = (CreateFileImplRef*)signal->getDataPtr();
  CreateObjRecordPtr op_ptr;

  jamEntry();
  ndbrequire(c_opCreateObj.find(op_ptr, ref->senderData));
  op_ptr.p->m_errorCode = ref->errorCode;
  execute(signal, op_ptr.p->m_callback, 0);  
}  

void
Dbdict::execCREATE_FILE_CONF(Signal* signal)
{
  CreateFileImplConf * rep = 
    (CreateFileImplConf*)signal->getDataPtr();
  CreateObjRecordPtr op_ptr;

  jamEntry();
  ndbrequire(c_opCreateObj.find(op_ptr, rep->senderData));
  execute(signal, op_ptr.p->m_callback, 0);  
}

void
Dbdict::create_file_commit_start(Signal* signal, SchemaOperation* op)
{
  /**
   * CONTACT TSMAN LGMAN PGMAN 
   */
  CreateFileImplReq* req = (CreateFileImplReq*)signal->getDataPtrSend();
  FilePtr f_ptr;
  FilegroupPtr fg_ptr;

  jam();
  c_file_pool.getPtr(f_ptr, op->m_obj_ptr_i);
  ndbrequire(c_filegroup_hash.find(fg_ptr, f_ptr.p->m_filegroup_id));

  req->senderData = op->key;
  req->senderRef = reference();
  req->requestInfo = CreateFileImplReq::Commit;
  
  req->file_id = f_ptr.p->key;
  req->filegroup_id = f_ptr.p->m_filegroup_id;
  req->filegroup_version = fg_ptr.p->m_version;

  Uint32 ref= 0;
  switch(op->m_obj_type){
  case DictTabInfo::Datafile:
  {
    jam();
    ref = TSMAN_REF;
    break;
  }
  case DictTabInfo::Undofile:
  {
    jam();
    ref = LGMAN_REF;
    break;
  }
  default:
    ndbrequire(false);
  }
  sendSignal(ref, GSN_CREATE_FILE_REQ, signal, 
	     CreateFileImplReq::CommitLength, JBB);
}

void
Dbdict::create_file_abort_start(Signal* signal, SchemaOperation* op)
{
  CreateFileImplReq* req = (CreateFileImplReq*)signal->getDataPtrSend();

  if (op->m_obj_ptr_i != RNIL)
  {
    FilePtr f_ptr;
    FilegroupPtr fg_ptr;

    jam();
    c_file_pool.getPtr(f_ptr, op->m_obj_ptr_i);
    
    ndbrequire(c_filegroup_hash.find(fg_ptr, f_ptr.p->m_filegroup_id));
    
    req->senderData = op->key;
    req->senderRef = reference();
    req->requestInfo = CreateFileImplReq::Abort;
    
    req->file_id = f_ptr.p->key;
    req->filegroup_id = f_ptr.p->m_filegroup_id;
    req->filegroup_version = fg_ptr.p->m_version;
    
    Uint32 ref= 0;
    switch(op->m_obj_type){
    case DictTabInfo::Datafile:
    {
      jam();
      ref = TSMAN_REF;
      break;
    }
    case DictTabInfo::Undofile:
    {
      jam();
      ref = LGMAN_REF;
      break;
    }
    default:
      ndbrequire(false);
    }
    sendSignal(ref, GSN_CREATE_FILE_REQ, signal, 
	       CreateFileImplReq::AbortLength, JBB);
    return;
  }
  execute(signal, op->m_callback, 0);
}

void
Dbdict::create_file_abort_complete(Signal* signal, SchemaOperation* op)
{
  if (op->m_obj_ptr_i != RNIL)
  {
    FilePtr f_ptr;
    FilegroupPtr fg_ptr;

    jam();
    c_file_pool.getPtr(f_ptr, op->m_obj_ptr_i);
    ndbrequire(c_filegroup_hash.find(fg_ptr, f_ptr.p->m_filegroup_id));
    switch(fg_ptr.p->m_type){
    case DictTabInfo::Tablespace:
    {
      jam();
      decrease_ref_count(fg_ptr.p->m_obj_ptr_i);
      break;
    }
    case DictTabInfo::LogfileGroup:
    {
      jam();
      Local_file_list list(c_file_pool, fg_ptr.p->m_logfilegroup.m_files);
      list.remove(f_ptr);
      break;
    }
    default:
      ndbrequire(false);
    }
    
    release_object(f_ptr.p->m_obj_ptr_i);
    c_file_hash.release(f_ptr);
  }
  execute(signal, op->m_callback, 0);
}

void
Dbdict::drop_file_prepare_start(Signal* signal, SchemaOperation* op)
{
  jam();
  send_drop_file(signal, op, DropFileImplReq::Prepare);
}

void
Dbdict::drop_undofile_prepare_start(Signal* signal, SchemaOperation* op)
{
  jam();
  op->m_errorCode = DropFileRef::DropUndoFileNotSupported;
  execute(signal, op->m_callback, 0);  
}

void
Dbdict::drop_file_commit_start(Signal* signal, SchemaOperation* op)
{
  jam();
  send_drop_file(signal, op, DropFileImplReq::Commit);
}

void
Dbdict::drop_file_commit_complete(Signal* signal, SchemaOperation* op)
{
  FilePtr f_ptr;
  FilegroupPtr fg_ptr;

  jam();
  c_file_pool.getPtr(f_ptr, op->m_obj_ptr_i);
  ndbrequire(c_filegroup_hash.find(fg_ptr, f_ptr.p->m_filegroup_id));
  decrease_ref_count(fg_ptr.p->m_obj_ptr_i);
  release_object(f_ptr.p->m_obj_ptr_i);
  c_file_hash.release(f_ptr);
  execute(signal, op->m_callback, 0);
}

void
Dbdict::drop_undofile_commit_complete(Signal* signal, SchemaOperation* op)
{
  FilePtr f_ptr;
  FilegroupPtr fg_ptr;

  jam();
  c_file_pool.getPtr(f_ptr, op->m_obj_ptr_i);
  ndbrequire(c_filegroup_hash.find(fg_ptr, f_ptr.p->m_filegroup_id));
  Local_file_list list(c_file_pool, fg_ptr.p->m_logfilegroup.m_files);
  list.remove(f_ptr);
  release_object(f_ptr.p->m_obj_ptr_i);
  c_file_hash.release(f_ptr);
  execute(signal, op->m_callback, 0);
}

void
Dbdict::drop_file_abort_start(Signal* signal, SchemaOperation* op)
{
  jam();
  send_drop_file(signal, op, DropFileImplReq::Abort);
}

void
Dbdict::send_drop_file(Signal* signal, SchemaOperation* op,
		       DropFileImplReq::RequestInfo type)
{
  DropFileImplReq* req = (DropFileImplReq*)signal->getDataPtrSend();
  FilePtr f_ptr;
  FilegroupPtr fg_ptr;

  jam();
  c_file_pool.getPtr(f_ptr, op->m_obj_ptr_i);
  ndbrequire(c_filegroup_hash.find(fg_ptr, f_ptr.p->m_filegroup_id));
  
  req->senderData = op->key;
  req->senderRef = reference();
  req->requestInfo = type;
  
  req->file_id = f_ptr.p->key;
  req->filegroup_id = f_ptr.p->m_filegroup_id;
  req->filegroup_version = fg_ptr.p->m_version;
  
  Uint32 ref= 0;
  switch(op->m_obj_type){
  case DictTabInfo::Datafile:
  {
    jam();
    ref = TSMAN_REF;
    break;
  }
  case DictTabInfo::Undofile:
  {
    jam();
    ref = LGMAN_REF;
    break;
  }
  default:
    ndbrequire(false);
  }
  sendSignal(ref, GSN_DROP_FILE_REQ, signal, 
	     DropFileImplReq::SignalLength, JBB);
}

void
Dbdict::execDROP_OBJ_REF(Signal* signal)
{
  DropObjRef * const ref = (DropObjRef*)signal->getDataPtr();
  Ptr<SchemaTransaction> trans_ptr;

  jamEntry();
  ndbrequire(c_Trans.find(trans_ptr, ref->senderData));
  if(ref->errorCode != DropObjRef::NF_FakeErrorREF){
    jam();
    trans_ptr.p->setErrorCode(ref->errorCode);
  }
  Uint32 node = refToNode(ref->senderRef);
  schemaOperation_reply(signal, trans_ptr.p, node);
}

void
Dbdict::execDROP_OBJ_CONF(Signal* signal)
{
  DropObjConf * const conf = (DropObjConf*)signal->getDataPtr();
  Ptr<SchemaTransaction> trans_ptr;

  jamEntry();
  ndbrequire(c_Trans.find(trans_ptr, conf->senderData));
  schemaOperation_reply(signal, trans_ptr.p, refToNode(conf->senderRef));
}

void
Dbdict::execDROP_FILE_REF(Signal* signal)
{
  DropFileImplRef * ref = (DropFileImplRef*)signal->getDataPtr();
  DropObjRecordPtr op_ptr;

  jamEntry();
  ndbrequire(c_opDropObj.find(op_ptr, ref->senderData));
  op_ptr.p->m_errorCode = ref->errorCode;
  execute(signal, op_ptr.p->m_callback, 0);  
}  

void
Dbdict::execDROP_FILE_CONF(Signal* signal)
{
  DropFileImplConf * rep = 
    (DropFileImplConf*)signal->getDataPtr();
  DropObjRecordPtr op_ptr;

  jamEntry();
  ndbrequire(c_opDropObj.find(op_ptr, rep->senderData));
  execute(signal, op_ptr.p->m_callback, 0);  
}

void
Dbdict::execDROP_FILEGROUP_REF(Signal* signal)
{
  DropFilegroupImplRef * ref = (DropFilegroupImplRef*)signal->getDataPtr();
  DropObjRecordPtr op_ptr;

  jamEntry();
  ndbrequire(c_opDropObj.find(op_ptr, ref->senderData));
  op_ptr.p->m_errorCode = ref->errorCode;
  execute(signal, op_ptr.p->m_callback, 0);  
}  

void
Dbdict::execDROP_FILEGROUP_CONF(Signal* signal)
{
  DropFilegroupImplConf * rep = 
    (DropFilegroupImplConf*)signal->getDataPtr();
  DropObjRecordPtr op_ptr;

  jamEntry();
  ndbrequire(c_opDropObj.find(op_ptr, rep->senderData));
  execute(signal, op_ptr.p->m_callback, 0);  
}

void
Dbdict::drop_fg_prepare_start(Signal* signal, SchemaOperation* op)
{
  FilegroupPtr fg_ptr;
  c_filegroup_pool.getPtr(fg_ptr, op->m_obj_ptr_i);
  
  DictObject * obj = c_obj_pool.getPtr(fg_ptr.p->m_obj_ptr_i);
  if (obj->m_ref_count)
  {
    jam();
    op->m_errorCode = DropFilegroupRef::FilegroupInUse;
    execute(signal, op->m_callback, 0);  
  }
  else
  {
    jam();
    send_drop_fg(signal, op, DropFilegroupImplReq::Prepare);
  }
}

void
Dbdict::drop_fg_commit_start(Signal* signal, SchemaOperation* op)
{
  FilegroupPtr fg_ptr;
  c_filegroup_pool.getPtr(fg_ptr, op->m_obj_ptr_i);
  if (op->m_obj_type == DictTabInfo::LogfileGroup)
  {
    jam(); 
    /**
     * Mark all undofiles as dropped
     */
    Ptr<File> filePtr;
    Local_file_list list(c_file_pool, fg_ptr.p->m_logfilegroup.m_files);
    XSchemaFile * xsf = &c_schemaFile[c_schemaRecord.schemaPage != 0];
    for(list.first(filePtr); !filePtr.isNull(); list.next(filePtr))
    {
      jam();
      Uint32 objId = filePtr.p->key;
      SchemaFile::TableEntry * tableEntry = getTableEntry(xsf, objId);
      tableEntry->m_tableState = SchemaFile::DROP_TABLE_COMMITTED;
      computeChecksum(xsf, objId / NDB_SF_PAGE_ENTRIES);
      release_object(filePtr.p->m_obj_ptr_i);
      c_file_hash.remove(filePtr);
    }
    list.release();
  }
  else if(op->m_obj_type == DictTabInfo::Tablespace)
  {
    FilegroupPtr lg_ptr;
    jam();
    ndbrequire(c_filegroup_hash.
	       find(lg_ptr, 
		    fg_ptr.p->m_tablespace.m_default_logfile_group_id));
    
    decrease_ref_count(lg_ptr.p->m_obj_ptr_i);
  }
  jam(); 
  send_drop_fg(signal, op, DropFilegroupImplReq::Commit);
}

void
Dbdict::drop_fg_commit_complete(Signal* signal, SchemaOperation* op)
{
  FilegroupPtr fg_ptr;
  c_filegroup_pool.getPtr(fg_ptr, op->m_obj_ptr_i);

  jam();
  release_object(fg_ptr.p->m_obj_ptr_i);
  c_filegroup_hash.release(fg_ptr);
  execute(signal, op->m_callback, 0);
}

void
Dbdict::drop_fg_abort_start(Signal* signal, SchemaOperation* op)
{
  jam();
  send_drop_fg(signal, op, DropFilegroupImplReq::Abort);
}

void
Dbdict::send_drop_fg(Signal* signal, SchemaOperation* op,
		     DropFilegroupImplReq::RequestInfo type)
{
  DropFilegroupImplReq* req = (DropFilegroupImplReq*)signal->getDataPtrSend();
  
  FilegroupPtr fg_ptr;
  c_filegroup_pool.getPtr(fg_ptr, op->m_obj_ptr_i);
  
  req->senderData = op->key;
  req->senderRef = reference();
  req->requestInfo = type;
  
  req->filegroup_id = fg_ptr.p->key;
  req->filegroup_version = fg_ptr.p->m_version;
  
  Uint32 ref= 0;
  switch(op->m_obj_type){
  case DictTabInfo::Tablespace:
    ref = TSMAN_REF;
    break;
  case DictTabInfo::LogfileGroup:
    ref = LGMAN_REF;
    break;
  default:
    ndbrequire(false);
  }
  
  sendSignal(ref, GSN_DROP_FILEGROUP_REQ, signal, 
	     DropFilegroupImplReq::SignalLength, JBB);
}

/*
  return 1 if all of the below is true
  a) node in single user mode
  b) senderRef is not a db node
  c) senderRef nodeid is not the singleUserApi
*/
int Dbdict::checkSingleUserMode(Uint32 senderRef)
{
  Uint32 nodeId = refToNode(senderRef);
  return
    getNodeState().getSingleUserMode() &&
    (getNodeInfo(nodeId).m_type != NodeInfo::DB) &&
    (nodeId != getNodeState().getSingleUserApi());
}

// MODULE: SchemaTrans

// ErrorInfo

void
Dbdict::setError(ErrorInfo& e,
                 Uint32 code,
                 Uint32 line,
                 Uint32 nodeId,
                 Uint32 status,
                 Uint32 key)
{
  D("setError" << V(code) << V(line) << V(nodeId) << V(e.errorCount));

  // can only store details for first error
  if (e.errorCount == 0) {
    e.errorCode = code;
    e.errorLine = line;
    e.errorNodeId = nodeId ? nodeId : getOwnNodeId();
    e.errorStatus = status;
    e.errorKey = key;
  }
  e.errorCount++;
}

void
Dbdict::setError(ErrorInfo& e, const ErrorInfo& e2)
{
  setError(e, e2.errorCode, e2.errorLine, e2.errorNodeId);
}

void
Dbdict::setError(ErrorInfo& e, const ParseDictTabInfoRecord& e2)
{
  setError(e, e2.errorCode, e2.errorLine, 0, e2.status, e2.errorKey);
}

void
Dbdict::setError(SchemaOpPtr op_ptr, Uint32 code, Uint32 line, Uint32 nodeId)
{
  D("setError" << *op_ptr.p << V(code) << V(line) << V(nodeId));
  setError(op_ptr.p->m_error, code, line, nodeId);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  // wl3600_todo remove the check
  if (!trans_ptr.isNull()) {
    setError(trans_ptr.p->m_error, op_ptr.p->m_error);
  }
}

void
Dbdict::setError(SchemaOpPtr op_ptr, const ErrorInfo& e2)
{
  setError(op_ptr, e2.errorCode, e2.errorLine, e2.errorNodeId);
}

void
Dbdict::setError(SchemaTransPtr trans_ptr, Uint32 code, Uint32 line, Uint32 nodeId)
{
  D("setError" << *trans_ptr.p << V(code) << V(line) << V(nodeId));
  setError(trans_ptr.p->m_error, code, line, nodeId);
}

void
Dbdict::setError(SchemaTransPtr trans_ptr, const ErrorInfo& e2)
{
  setError(trans_ptr, e2.errorCode, e2.errorLine, e2.errorNodeId);
}

void
Dbdict::setError(TxHandlePtr tx_ptr, Uint32 code, Uint32 line, Uint32 nodeId)
{
  D("setError" << *tx_ptr.p << V(code) << V(line) << V(nodeId));
  setError(tx_ptr.p->m_error, code, line, nodeId);
}

void
Dbdict::setError(TxHandlePtr tx_ptr, const ErrorInfo& e2)
{
  setError(tx_ptr, e2.errorCode, e2.errorLine, e2.errorNodeId);
}

bool
Dbdict::hasError(const ErrorInfo& e)
{
  return e.errorCount != 0;
}

void
Dbdict::resetError(ErrorInfo& e)
{
  new (&e) ErrorInfo();
}

void
Dbdict::resetError(SchemaOpPtr op_ptr)
{
  if (hasError(op_ptr.p->m_error)) {
    jam();
    D("resetError" << *op_ptr.p);
    resetError(op_ptr.p->m_error);
  }
}

void
Dbdict::resetError(SchemaTransPtr trans_ptr)
{
  if (hasError(trans_ptr.p->m_error)) {
    jam();
    D("resetError" << *trans_ptr.p);
    resetError(trans_ptr.p->m_error);
  }
}

void
Dbdict::resetError(TxHandlePtr tx_ptr)
{
  if (hasError(tx_ptr.p->m_error)) {
    jam();
    D("resetError" << *tx_ptr.p);
    resetError(tx_ptr.p->m_error);
  }
}

// OpInfo

const Dbdict::OpInfo*
Dbdict::g_opInfoList[] = {
  &Dbdict::CreateTableRec::g_opInfo,
  &Dbdict::DropTableRec::g_opInfo,
  &Dbdict::AlterTableRec::g_opInfo,
  &Dbdict::CreateTriggerRec::g_opInfo,
  &Dbdict::DropTriggerRec::g_opInfo,
  &Dbdict::CreateIndexRec::g_opInfo,
  &Dbdict::DropIndexRec::g_opInfo,
  &Dbdict::AlterIndexRec::g_opInfo,
  &Dbdict::BuildIndexRec::g_opInfo,
  0
};

const Dbdict::OpInfo*
Dbdict::findOpInfo(Uint32 gsn)
{
  Uint32 i = 0;
  while (1) {
    const OpInfo* info = g_opInfoList[i];
    if (info->m_impl_req_gsn == 0)
      break;
    if (info->m_impl_req_gsn == gsn)
      return info;
    i++;
  }
  return 0;
}

// OpRec

// OpSection

bool
Dbdict::copyIn(OpSection& op_sec, const SegmentedSectionPtr& ss_ptr)
{
  const Uint32 size = ZSIZE_OF_PAGES_IN_WORDS;
  Uint32 buf[size];

  if (size < ss_ptr.sz) {
    jam();
    return false;
  }
  ::copy(buf, ss_ptr);
  if (!copyIn(op_sec, buf, ss_ptr.sz)) {
    jam();
    return false;
  }
  return true;
}

bool
Dbdict::copyIn(OpSection& op_sec, const Uint32* src, Uint32 srcSize)
{
  OpSectionBuffer buffer(c_opSectionBufferPool, op_sec.m_head);
  if (!buffer.append(src, srcSize)) {
    jam();
    return false;
  }
  return true;
}

bool
Dbdict::copyOut(const OpSection& op_sec, SegmentedSectionPtr& ss_ptr)
{
  const Uint32 size = ZSIZE_OF_PAGES_IN_WORDS;
  Uint32 buf[size];

  if (!copyOut(op_sec, buf, size)) {
    jam();
    return false;
  }
  Ptr<SectionSegment> ptr;
  if (!::import(ptr, buf, op_sec.getSize())) {
    jam();
    return false;
  }
  ss_ptr.i = ptr.i;
  ss_ptr.p = ptr.p;
  ss_ptr.sz = op_sec.getSize();
  return true;
}

bool
Dbdict::copyOut(const OpSection& op_sec, Uint32* dst, Uint32 dstSize)
{
  if (op_sec.getSize() > dstSize) {
    jam();
    return false;
  }

  // there is no const version of LocalDataBuffer
  OpSectionBufferHead tmp_head = op_sec.m_head;
  OpSectionBuffer buffer(c_opSectionBufferPool, tmp_head);

  OpSectionBufferConstIterator iter;
  Uint32 n = 0;
  for(buffer.first(iter); !iter.isNull(); buffer.next(iter)) {
    jam();
    dst[n] = *iter.data;
    n++;
  }
  ndbrequire(n == op_sec.getSize());
  return true;
}

void
Dbdict::release(OpSection& op_sec)
{
  OpSectionBuffer buffer(c_opSectionBufferPool, op_sec.m_head);
  buffer.release();
}

// SchemaOp

const Dbdict::OpInfo&
Dbdict::getOpInfo(SchemaOpPtr op_ptr)
{
  ndbrequire(!op_ptr.isNull());
  OpRecPtr oprec_ptr = op_ptr.p->m_oprec_ptr;
  ndbrequire(!oprec_ptr.isNull());
  return oprec_ptr.p->m_opInfo;
}

bool
Dbdict::seizeSchemaOp(SchemaOpPtr& op_ptr, Uint32 op_key, const OpInfo& info)
{
  if (ERROR_INSERTED(6111) && (
      info.m_impl_req_gsn == GSN_CREATE_TAB_REQ ||
      info.m_impl_req_gsn == GSN_DROP_TAB_REQ ||
      info.m_impl_req_gsn == GSN_ALTER_TAB_REQ) ||
      ERROR_INSERTED(6112) && (
      info.m_impl_req_gsn == GSN_CREATE_INDX_IMPL_REQ ||
      info.m_impl_req_gsn == GSN_DROP_INDX_IMPL_REQ) ||
      ERROR_INSERTED(6113) && (
      info.m_impl_req_gsn == GSN_ALTER_INDX_IMPL_REQ) ||
      ERROR_INSERTED(6114) && (
      info.m_impl_req_gsn == GSN_CREATE_TRIG_IMPL_REQ ||
      info.m_impl_req_gsn == GSN_DROP_TRIG_IMPL_REQ) ||
      ERROR_INSERTED(6116) && (
      info.m_impl_req_gsn == GSN_BUILD_INDX_IMPL_REQ)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    op_ptr.setNull();
    return false;
  }

  if (!findSchemaOp(op_ptr, op_key)) {
    jam();
    if (c_schemaOpHash.seize(op_ptr)) {
      jam();
      new (op_ptr.p) SchemaOp();
      op_ptr.p->op_key = op_key;
      if ((this->*(info.m_seize))(op_ptr)) {
        jam();
        c_schemaOpHash.add(op_ptr);
        op_ptr.p->m_magic = SchemaOp::DICT_MAGIC;
        const char* opType = info.m_opType;
        D("seizeSchemaOp" << V(op_key) << V(opType));
        return true;
      }
      c_schemaOpHash.release(op_ptr);
    }
  }
  op_ptr.setNull();
  return false;
}

bool
Dbdict::findSchemaOp(SchemaOpPtr& op_ptr, Uint32 op_key)
{
  SchemaOp op_rec(op_key);
  if (c_schemaOpHash.find(op_ptr, op_rec)) {
    jam();
    const OpRecPtr& oprec_ptr = op_ptr.p->m_oprec_ptr;
    ndbrequire(!oprec_ptr.isNull());
    ndbrequire(op_ptr.p->m_magic == SchemaOp::DICT_MAGIC);
    D("findSchemaOp" << V(op_key));
    return true;
  }
  return false;
}

void
Dbdict::releaseSchemaOp(SchemaOpPtr& op_ptr)
{
  Uint32 op_key = op_ptr.p->op_key;
  D("releaseSchemaOp" << V(op_key));

  const OpInfo& info = getOpInfo(op_ptr);
  (this->*(info.m_release))(op_ptr);

  while (op_ptr.p->m_sections != 0) {
    jam();
    releaseOpSection(op_ptr, op_ptr.p->m_sections - 1);
  }

  OpRecPtr oprec_ptr = op_ptr.p->m_oprec_ptr;
  Uint32 obj_ptr_i = oprec_ptr.p->m_obj_ptr_i;
  if (obj_ptr_i != RNIL) {
    jam();
    unlinkDictObject(op_ptr);
  }

  if (!op_ptr.p->m_oplnk_ptr.isNull()) {
    jam();
    releaseSchemaOp(op_ptr.p->m_oplnk_ptr);
  }

  ndbrequire(op_ptr.p->m_magic == SchemaOp::DICT_MAGIC);
  op_ptr.p->m_magic = 0;
  c_schemaOpHash.release(op_ptr);
  op_ptr.setNull();
}

// save signal sections

const Dbdict::OpSection&
Dbdict::getOpSection(SchemaOpPtr op_ptr, Uint32 ss_no)
{
  ndbrequire(ss_no < op_ptr.p->m_sections);
  return op_ptr.p->m_section[ss_no];
}

bool
Dbdict::saveOpSection(SchemaOpPtr op_ptr,
                      SectionHandle& handle, Uint32 ss_no)
{
  SegmentedSectionPtr ss_ptr;
  bool ok = handle.getSection(ss_ptr, ss_no);
  ndbrequire(ok);
  return saveOpSection(op_ptr, ss_ptr, ss_no);
}

bool
Dbdict::saveOpSection(SchemaOpPtr op_ptr,
                      SegmentedSectionPtr ss_ptr, Uint32 ss_no)
{
  ndbrequire(ss_no <= 2 && op_ptr.p->m_sections == ss_no);
  OpSection& op_sec = op_ptr.p->m_section[ss_no];
  op_ptr.p->m_sections++;

  bool ok = copyIn(op_sec, ss_ptr);
  ndbrequire(ok);
  return true;
}

void
Dbdict::releaseOpSection(SchemaOpPtr op_ptr, Uint32 ss_no)
{
  ndbrequire(ss_no + 1 == op_ptr.p->m_sections);
  OpSection& op_sec = op_ptr.p->m_section[ss_no];
  release(op_sec);
  op_ptr.p->m_sections = ss_no;
}

// add schema op to trans during parse phase

void
Dbdict::addSchemaOp(SchemaTransPtr trans_ptr, SchemaOpPtr& op_ptr)
{
  LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
  list.addLast(op_ptr);

  op_ptr.p->m_trans_ptr = trans_ptr;

  // jonas_todo REMOVE side effect
  // add global flags from trans
  const Uint32& src_info = trans_ptr.p->m_requestInfo;
  DictSignal::addRequestFlagsGlobal(op_ptr.p->m_requestInfo, src_info);
}

// update op step after successful execution

// the link SchemaOp -> DictObject -> SchemaTrans

// check if link to dict object exists
bool
Dbdict::hasDictObject(SchemaOpPtr op_ptr)
{
  OpRecPtr oprec_ptr = op_ptr.p->m_oprec_ptr;
  return oprec_ptr.p->m_obj_ptr_i != RNIL;
}

// get dict object for existing link
void
Dbdict::getDictObject(SchemaOpPtr op_ptr, DictObjectPtr& obj_ptr)
{
  OpRecPtr oprec_ptr = op_ptr.p->m_oprec_ptr;
  ndbrequire(oprec_ptr.p->m_obj_ptr_i != RNIL);
  c_obj_pool.getPtr(obj_ptr, oprec_ptr.p->m_obj_ptr_i);
}

// create link from schema op to dict object
void
Dbdict::linkDictObject(SchemaOpPtr op_ptr, DictObjectPtr obj_ptr)
{
  ndbrequire(!obj_ptr.isNull());
  D("linkDictObject" << V(op_ptr.p->op_key) << V(obj_ptr.i));

  OpRecPtr oprec_ptr = op_ptr.p->m_oprec_ptr;
  ndbrequire(oprec_ptr.p->m_obj_ptr_i == RNIL);
  oprec_ptr.p->m_obj_ptr_i = obj_ptr.i;

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  if (!trans_ptr.isNull()) {
    jam();
    ndbrequire(trans_ptr.p->trans_key != 0);
    if (obj_ptr.p->m_op_ref_count == 0) {
      jam();
      obj_ptr.p->m_trans_key = trans_ptr.p->trans_key;
    } else {
      ndbrequire(obj_ptr.p->m_trans_key == trans_ptr.p->trans_key);
    }
  } else {
    jam();
    // restart table uses no trans (yet)
    ndbrequire(memcmp(oprec_ptr.p->m_opType, "CTa", 4) == 0);
    ndbrequire(getNodeState().startLevel != NodeState::SL_STARTED);
  }

  obj_ptr.p->m_op_ref_count += 1;
  D("linkDictObject done" << *obj_ptr.p);
}

// drop link from schema op to dict object
void
Dbdict::unlinkDictObject(SchemaOpPtr op_ptr)
{
  DictObjectPtr obj_ptr;
  OpRecPtr oprec_ptr = op_ptr.p->m_oprec_ptr;
  ndbrequire(oprec_ptr.p->m_obj_ptr_i != RNIL);
  c_obj_pool.getPtr(obj_ptr, oprec_ptr.p->m_obj_ptr_i);

  D("unlinkDictObject" << V(op_ptr.p->op_key) << V(obj_ptr.i));

  oprec_ptr.p->m_obj_ptr_i = RNIL;
  ndbrequire(obj_ptr.p->m_op_ref_count != 0);
  obj_ptr.p->m_op_ref_count -= 1;
  ndbrequire(obj_ptr.p->m_trans_key != 0);
  if (obj_ptr.p->m_op_ref_count == 0) {
    jam();
    obj_ptr.p->m_trans_key = 0;
  }
  D("unlinkDictObject done" << *obj_ptr.p);
}

// add new dict object and link schema op to it
void
Dbdict::seizeDictObject(SchemaOpPtr op_ptr,
                        DictObjectPtr& obj_ptr,
                        const RopeHandle& name)
{
  D("seizeDictObject" << *op_ptr.p);

  bool ok = c_obj_hash.seize(obj_ptr);
  ndbrequire(ok);
  new (obj_ptr.p) DictObject();

  obj_ptr.p->m_name = name;
  c_obj_hash.add(obj_ptr);
  obj_ptr.p->m_ref_count = 0;

  linkDictObject(op_ptr, obj_ptr);
  D("seizeDictObject done" << *obj_ptr.p);
}

// find dict object by name and link schema op to it
bool
Dbdict::findDictObject(SchemaOpPtr op_ptr,
                       DictObjectPtr& obj_ptr,
                       const char* name)
{
  D("findDictObject" << *op_ptr.p << V(name));
  if (get_object(obj_ptr, name)) {
    jam();
    linkDictObject(op_ptr, obj_ptr);
    return true;
  }
  return false;
}

// find dict object by i-value and link schema op to it
bool
Dbdict::findDictObject(SchemaOpPtr op_ptr,
                       DictObjectPtr& obj_ptr,
                       Uint32 obj_ptr_i)
{
  D("findDictObject" << *op_ptr.p << V(obj_ptr.i));
  if (obj_ptr_i != RNIL) {
    jam();
    c_obj_pool.getPtr(obj_ptr, obj_ptr_i);
    linkDictObject(op_ptr, obj_ptr);
    return true;
  }
  return false;
}

// remove link to dict object and release the dict object
void
Dbdict::releaseDictObject(SchemaOpPtr op_ptr)
{
  DictObjectPtr obj_ptr;
  getDictObject(op_ptr, obj_ptr);

  D("releaseDictObject" << *op_ptr.p << V(obj_ptr.i) << *obj_ptr.p);

  unlinkDictObject(op_ptr);

  // check no other object or operation references it
  ndbrequire(obj_ptr.p->m_ref_count == 0);
  if (obj_ptr.p->m_op_ref_count == 0) {
    jam();
    release_object(obj_ptr.i);
  }
}

// find last op on dict object
void
Dbdict::findDictObjectOp(SchemaOpPtr& op_ptr, DictObjectPtr obj_ptr)
{
  D("findDictObjectOp" << *obj_ptr.p);
  op_ptr.setNull();

  Uint32 trans_key = obj_ptr.p->m_trans_key;
  do {
    if (trans_key == 0) {
      jam();
      D("no trans_key");
      break;
    }

    SchemaTransPtr trans_ptr;
    findSchemaTrans(trans_ptr, trans_key);
    if (trans_ptr.isNull()) {
      jam();
      D("trans not found");
      break;
    }
    D("found" << *trans_ptr.p);

    {
      LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
      SchemaOpPtr loop_ptr;
      list.first(loop_ptr);
      while (!loop_ptr.isNull()) {
        jam();
        OpRecPtr oprec_ptr = loop_ptr.p->m_oprec_ptr;
        if (oprec_ptr.p->m_obj_ptr_i == obj_ptr.i) {
          jam();
          op_ptr = loop_ptr;
          D("found candidate" << *op_ptr.p);
        }
        list.next(loop_ptr);
      }
    }
  } while (0);
}

// trans state


// SchemaTrans

bool
Dbdict::seizeSchemaTrans(SchemaTransPtr& trans_ptr, Uint32 trans_key)
{
  if (ERROR_INSERTED(6101)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;
    trans_ptr.setNull();
    return false;
  }
  if (!findSchemaTrans(trans_ptr, trans_key)) {
    jam();
    if (c_schemaTransHash.seize(trans_ptr)) {
      jam();
      new (trans_ptr.p) SchemaTrans();
      trans_ptr.p->trans_key = trans_key;
      c_schemaTransHash.add(trans_ptr);
      c_schemaTransList.addLast(trans_ptr);
      c_schemaTransCount++;
      trans_ptr.p->m_magic = SchemaTrans::DICT_MAGIC;
      D("seizeSchemaTrans" << V(trans_key));
      return true;
    }
  }
  trans_ptr.setNull();
  return false;
}

bool
Dbdict::seizeSchemaTrans(SchemaTransPtr& trans_ptr)
{
  Uint32 trans_key = c_opRecordSequence + 1;
  if (seizeSchemaTrans(trans_ptr, trans_key)) {
    c_opRecordSequence = trans_key;
    return true;
  }
  return false;
}

bool
Dbdict::findSchemaTrans(SchemaTransPtr& trans_ptr, Uint32 trans_key)
{
  SchemaTrans trans_rec(trans_key);
  if (c_schemaTransHash.find(trans_ptr, trans_rec)) {
    jam();
    ndbrequire(trans_ptr.p->m_magic == SchemaTrans::DICT_MAGIC);
    D("findSchemaTrans" << V(trans_key));
    return true;
  }
  trans_ptr.setNull();
  return false;
}

void
Dbdict::releaseSchemaTrans(SchemaTransPtr& trans_ptr)
{
  Uint32 trans_key = trans_ptr.p->trans_key;
  D("releaseSchemaTrans" << V(trans_key));

  LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
  SchemaOpPtr op_ptr;
  while (list.first(op_ptr)) {
    list.remove(op_ptr);
    releaseSchemaOp(op_ptr);
  }
  ndbrequire(trans_ptr.p->m_magic == SchemaTrans::DICT_MAGIC);
  trans_ptr.p->m_magic = 0;
  ndbrequire(c_schemaTransCount != 0);
  c_schemaTransCount--;
  c_schemaTransList.remove(trans_ptr);
  c_schemaTransHash.release(trans_ptr);
  trans_ptr.setNull();

  // only 1 trans at time so it is easy to check for leaks
  ndbrequire(c_schemaOpPool.getNoOfFree() == c_schemaOpPool.getSize());
  ndbrequire(c_opSectionBufferPool.getNoOfFree() == c_opSectionBufferPool.getSize());
#ifdef VM_TRACE
  if (getNodeState().startLevel == NodeState::SL_STARTED)
    check_consistency();
#endif
}

// client requests

void
Dbdict::execSCHEMA_TRANS_BEGIN_REQ(Signal* signal)
{
  jamEntry();
  const SchemaTransBeginReq* req =
    (const SchemaTransBeginReq*)signal->getDataPtr();
  Uint32 clientRef = req->clientRef;
  Uint32 transId = req->transId;
  Uint32 requestInfo = req->requestInfo;

  bool localTrans = (requestInfo & DictSignal::RF_LOCAL_TRANS);

  SchemaTransPtr trans_ptr;
  ErrorInfo error;
  do {
    if (getOwnNodeId() != c_masterNodeId && !localTrans) {
      jam();
      setError(error, SchemaTransBeginRef::NotMaster, __LINE__);
      break;
    }

    if (!seizeSchemaTrans(trans_ptr)) {
      jam();
      // future when more than 1 tx allowed
      setError(error, SchemaTransBeginRef::TooManySchemaTrans, __LINE__);
      break;
    }

    trans_ptr.p->m_isMaster = true;
    trans_ptr.p->m_masterRef = reference();
    trans_ptr.p->m_clientRef = clientRef;
    trans_ptr.p->m_transId = transId;
    trans_ptr.p->m_requestInfo = requestInfo;
    if (!localTrans)
    {
      jam();
      trans_ptr.p->m_nodes = c_aliveNodes;
    }
    else
    {
      jam();
      trans_ptr.p->m_nodes.clear();
      trans_ptr.p->m_nodes.set(getOwnNodeId());
    }
    trans_ptr.p->m_clientState = TransClient::BeginReq;

    // lock
    DictLockReq& lockReq = trans_ptr.p->m_lockReq;
    lockReq.userPtr = trans_ptr.p->trans_key;
    lockReq.userRef = reference();
    lockReq.lockType = DictLockReq::SchemaTransLock;
    int lockError = dict_lock_trylock(&lockReq);
    if (lockError != 0)
    {
      // remove the trans
      releaseSchemaTrans(trans_ptr);
      setError(error, lockError, __LINE__);
      break;
    }

    // begin tx on all participants
    trans_ptr.p->m_state = SchemaTrans::TS_STARTING;

    /**
     * Send RT_START
     */
    {
      SchemaTransImplReq* req = (SchemaTransImplReq*)signal->getDataPtrSend();
      req->senderRef = reference();
      req->transKey = trans_ptr.p->trans_key;
      req->opKey = RNIL;
      req->requestInfo = SchemaTransImplReq::RT_START;
      req->clientRef = trans_ptr.p->m_clientRef;
      req->transId = trans_ptr.p->m_transId;
      req->gsn = GSN_SCHEMA_TRANS_BEGIN_REQ;

      trans_ptr.p->m_ref_nodes.clear();
      NodeReceiverGroup rg(DBDICT, trans_ptr.p->m_nodes);
      {
        SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
        bool ok = sc.init<SchemaTransImplRef>(rg, trans_ptr.p->trans_key);
        ndbrequire(ok);
      }

      sendSignal(rg, GSN_SCHEMA_TRANS_IMPL_REQ, signal,
                 SchemaTransImplReq::SignalLength, JBB);
    }

    if (ERROR_INSERTED(6102)) {
      jam();
      CLEAR_ERROR_INSERT_VALUE;
      signal->theData[0] = refToNode(clientRef);
      sendSignal(QMGR_REF, GSN_API_FAILREQ, signal, 1, JBB);
    }
    return;
  } while(0);

  SchemaTrans tmp_trans;
  trans_ptr.i = RNIL;
  trans_ptr.p = &tmp_trans;
  trans_ptr.p->trans_key = 0;
  trans_ptr.p->m_clientRef = clientRef;
  trans_ptr.p->m_transId = transId;
  trans_ptr.p->m_clientState = TransClient::BeginReq;
  setError(trans_ptr.p->m_error, error);
  sendTransClientReply(signal, trans_ptr);
}

void
Dbdict::trans_start_recv_reply(Signal* signal, SchemaTransPtr trans_ptr)
{
  jam();

  switch(trans_ptr.p->m_state){
  case SchemaTrans::TS_STARTING:
    if (hasError(trans_ptr.p->m_error))
    {
      jam();

      trans_ptr.p->m_state = SchemaTrans::TS_ABORT_START;

      /**
       * Abort before replying to client
       */
      SchemaTransImplReq* req = (SchemaTransImplReq*)signal->getDataPtrSend();
      req->senderRef = reference();
      req->transKey = trans_ptr.p->trans_key;
      req->opKey = RNIL;
      req->requestInfo = SchemaTransImplReq::RT_COMPLETE;
      req->clientRef = trans_ptr.p->m_clientRef;
      req->transId = trans_ptr.p->m_transId;
      req->gsn = GSN_SCHEMA_TRANS_BEGIN_REQ;

      trans_ptr.p->m_nodes.bitAND(c_aliveNodes);
      NdbNodeBitmask nodes = trans_ptr.p->m_nodes;
      nodes.bitANDC(trans_ptr.p->m_ref_nodes);
      NodeReceiverGroup rg(DBDICT, nodes);
      {
        SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
        bool ok = sc.init<SchemaTransImplRef>(rg, trans_ptr.p->trans_key);
        ndbrequire(ok);
      }

      sendSignal(rg, GSN_SCHEMA_TRANS_IMPL_REQ, signal,
                 SchemaTransImplReq::SignalLength, JBB);
      return;
    }
    else
    {
      jam();
      sendTransClientReply(signal, trans_ptr);
      return;
    }
    break;
  case SchemaTrans::TS_ABORT_START:{
    jam();
    sendTransClientReply(signal, trans_ptr);

    const DictLockReq& lockReq = trans_ptr.p->m_lockReq;
    dict_lock_unlock(signal, &lockReq);
    releaseSchemaTrans(trans_ptr);
    return;
  }
  default:
    jamLine(trans_ptr.p->m_state);
    ndbrequire(false);
  }
}

void
Dbdict::execSCHEMA_TRANS_END_REQ(Signal* signal)
{
  jamEntry();

  const SchemaTransEndReq* req =
    (const SchemaTransEndReq*)signal->getDataPtr();
  Uint32 clientRef = req->clientRef;
  Uint32 transId = req->transId;
  Uint32 trans_key = req->transKey;
  Uint32 requestInfo = req->requestInfo;
  Uint32 flags = req->flags;

  SchemaTransPtr trans_ptr;
  ErrorInfo error;
  do {
    findSchemaTrans(trans_ptr, trans_key);
    if (trans_ptr.isNull()) {
      jam();
      ndbassert(false);
      setError(error, SchemaTransEndRef::InvalidTransKey, __LINE__);
      break;
    }

    if (trans_ptr.p->m_transId != transId) {
      jam();
      ndbassert(false);
      setError(error, SchemaTransEndRef::InvalidTransId, __LINE__);
      break;
    }

    bool localTrans = (trans_ptr.p->m_requestInfo & DictSignal::RF_LOCAL_TRANS);

    if (getOwnNodeId() != c_masterNodeId && !localTrans) {
      jam();
      // future when MNF is handled
      ndbassert(false);
      setError(error, SchemaTransEndRef::NotMaster, __LINE__);
      break;
    }

    //XXX Check state

    if (hasError(trans_ptr.p->m_error))
    {
      jam();
      ndbassert(false);
      setError(error, SchemaTransEndRef::InvalidTransState, __LINE__);
      break;
    }

    bool localTrans2 = requestInfo & DictSignal::RF_LOCAL_TRANS;
    if (localTrans != localTrans2)
    {
      jam();
      ndbassert(false);
      setError(error, SchemaTransEndRef::InvalidTransState, __LINE__);
      break;
    }

    trans_ptr.p->m_clientState = TransClient::EndReq;

    const bool doBackground = flags & SchemaTransEndReq::SchemaTransBackground;
    if (doBackground)
    {
      jam();
      // send reply to original client and restore EndReq state
      sendTransClientReply(signal, trans_ptr);
      trans_ptr.p->m_clientState = TransClient::EndReq;

      // take over client role via internal trans
      trans_ptr.p->m_clientFlags |= TransClient::Background;
      takeOverTransClient(signal, trans_ptr);
    }

    if (flags & SchemaTransEndReq::SchemaTransAbort)
    {
      jam();
      trans_abort_prepare_start(signal, trans_ptr);
      return;
    }
    else if ((flags & SchemaTransEndReq::SchemaTransPrepare) == 0)
    {
      jam();
      trans_ptr.p->m_clientFlags |= TransClient::Commit;
    }

    trans_prepare_start(signal, trans_ptr);
    return;
  } while (0);

  SchemaTrans tmp_trans;
  trans_ptr.i = RNIL;
  trans_ptr.p = &tmp_trans;
  trans_ptr.p->trans_key = trans_key;
  trans_ptr.p->m_clientRef = clientRef;
  trans_ptr.p->m_transId = transId;
  trans_ptr.p->m_clientState = TransClient::EndReq;
  setError(trans_ptr.p->m_error, error);
  sendTransClientReply(signal, trans_ptr);
}

// coordinator

void
Dbdict::handleClientReq(Signal* signal, SchemaOpPtr op_ptr,
                        SectionHandle& handle)
{
  D("handleClientReq" << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  if (trans_ptr.p->m_state == SchemaTrans::TS_SUBOP)
  {
    jam();
    SchemaOpPtr baseOp;
    c_schemaOpPool.getPtr(baseOp, trans_ptr.p->m_curr_op_ptr_i);
    op_ptr.p->m_base_op_ptr_i = baseOp.i;
  }

  trans_ptr.p->m_curr_op_ptr_i = op_ptr.i;
  op_ptr.p->m_state = SchemaOp::OS_PARSE_MASTER;

  ErrorInfo error;
  const OpInfo& info = getOpInfo(op_ptr);
  (this->*(info.m_parse))(signal, true, op_ptr, handle, error);

  if (hasError(error))
  {
    jam();
    setError(trans_ptr, error);
    releaseSections(handle);
    trans_rollback_sp_start(signal, trans_ptr);
    return;
  }

  trans_ptr.p->m_state = SchemaTrans::TS_PARSING;
  op_ptr.p->m_state = SchemaOp::OS_PARSING;

  Uint32 gsn = info.m_impl_req_gsn;
  const Uint32* src = op_ptr.p->m_oprec_ptr.p->m_impl_req_data;

  Uint32* data = signal->getDataPtrSend();
  Uint32 skip = SchemaTransImplReq::SignalLength;
  Uint32 extra_length = info.m_impl_req_length;
  ndbrequire(skip + extra_length <= 25);

  Uint32 i;
  for (i = 0; i < extra_length; i++)
    data[skip + i] = src[i];

  Uint32 requestInfo = 0;
  DictSignal::setRequestType(requestInfo, SchemaTransImplReq::RT_PARSE);
  DictSignal::addRequestFlags(requestInfo, op_ptr.p->m_requestInfo);
  SchemaTransImplReq* req = (SchemaTransImplReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->transKey = trans_ptr.p->trans_key;
  req->opKey = op_ptr.p->op_key;
  req->requestInfo = requestInfo;
  req->clientRef = trans_ptr.p->m_clientRef;
  req->transId = trans_ptr.p->m_transId;
  req->gsn = gsn;

  trans_ptr.p->m_nodes.bitAND(c_aliveNodes);
  NdbNodeBitmask nodes = trans_ptr.p->m_nodes;
  NodeReceiverGroup rg(DBDICT, nodes);
  {
    SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
    bool ok = sc.init<SchemaTransImplRef>(rg, trans_ptr.p->trans_key);
    ndbrequire(ok);
  }

  sendFragmentedSignal(rg, GSN_SCHEMA_TRANS_IMPL_REQ, signal,
                       SchemaTransImplReq::SignalLength + extra_length, JBB,
                       &handle);
}

void
Dbdict::trans_parse_recv_reply(Signal* signal, SchemaTransPtr trans_ptr)
{
  SchemaOpPtr op_ptr;
  c_schemaOpPool.getPtr(op_ptr, trans_ptr.p->m_curr_op_ptr_i);

  op_ptr.p->m_state = SchemaOp::OS_PARSED;

  if (hasError(trans_ptr.p->m_error))
  {
    jam();
    trans_rollback_sp_start(signal, trans_ptr);
    return;
  }

  const OpInfo& info = getOpInfo(op_ptr);
  if ((this->*(info.m_subOps))(signal, op_ptr))
  {
    jam();
    // more sub-ops on the way
    trans_ptr.p->m_curr_op_ptr_i = op_ptr.i;
    trans_ptr.p->m_state = SchemaTrans::TS_SUBOP;
    return;
  }

  /**
   * Reply to client
   */
  ErrorInfo error;
  (this->*(info.m_reply))(signal, op_ptr, error);

  trans_ptr.p->m_clientState = TransClient::ParseReply;
  trans_ptr.p->m_state = SchemaTrans::TS_STARTED;
}

void
Dbdict::execSCHEMA_TRANS_IMPL_CONF(Signal* signal)
{
  jamEntry();
  ndbrequire(signal->getNoOfSections() == 0);

  const SchemaTransImplConf* conf =
    (const SchemaTransImplConf*)signal->getDataPtr();

  SchemaTransPtr trans_ptr;
  ndbrequire(findSchemaTrans(trans_ptr, conf->transKey));

  Uint32 senderRef = conf->senderRef;
  Uint32 nodeId = refToNode(senderRef);

  {
    SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
    if (!sc.clearWaitingFor(nodeId)) {
      jam();
      return;
    }
  }

  trans_recv_reply(signal, trans_ptr);
}

void
Dbdict::execSCHEMA_TRANS_IMPL_REF(Signal* signal)
{
  jamEntry();
  ndbrequire(signal->getNoOfSections() == 0);

  const SchemaTransImplRef* ref =
    (const SchemaTransImplRef*)signal->getDataPtr();

  SchemaTransPtr trans_ptr;
  ndbrequire(findSchemaTrans(trans_ptr, ref->transKey));

  Uint32 senderRef = ref->senderRef;
  Uint32 nodeId = refToNode(senderRef);

  if (ref->errorCode == SchemaTransImplRef::NF_FakeErrorREF)
  {
    jam();
    // trans_ptr.p->m_nodes.clear(nodeId);
    // No need to clear, will be cleared when next REQ is set
  }
  else
  {
    jam();
    ErrorInfo error;
    setError(error, ref);
    setError(trans_ptr, error);
    switch(trans_ptr.p->m_state){
    case SchemaTrans::TS_STARTING:
      jam();
      trans_ptr.p->m_ref_nodes.set(nodeId);
      break;
    case SchemaTrans::TS_PARSING:
      jam();
      if (ref->errorCode == SchemaTransImplRef::SeizeFailed)
      {
        jam();
        trans_ptr.p->m_ref_nodes.set(nodeId);
      }
      break;
    default:
      jam();
    }
  }

  {
    SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
    if (!sc.clearWaitingFor(nodeId)) {
      jam();
      return;
    }
  }

  trans_recv_reply(signal, trans_ptr);
}

void
Dbdict::trans_recv_reply(Signal* signal, SchemaTransPtr trans_ptr)
{
  switch(trans_ptr.p->m_state){
  case SchemaTrans::TS_INITIAL:
    ndbrequire(false);
  case SchemaTrans::TS_STARTING:
    jam();
    trans_start_recv_reply(signal, trans_ptr);
    return;
  case SchemaTrans::TS_ABORT_START:
    jam();
    trans_start_recv_reply(signal, trans_ptr);
    return;
  case SchemaTrans::TS_PARSING:
    jam();
    trans_parse_recv_reply(signal, trans_ptr);
    return;
  case SchemaTrans::TS_SUBOP:
    ndbrequire(false);
  case SchemaTrans::TS_ROLLBACK_SP:
    trans_rollback_sp_recv_reply(signal, trans_ptr);
    return;
  case SchemaTrans::TS_PREPARING:
    jam();
    trans_prepare_recv_reply(signal, trans_ptr);
    return;
  case SchemaTrans::TS_ABORTING_PREPARE:
    jam();
    trans_abort_prepare_recv_reply(signal, trans_ptr);
    return;
  case SchemaTrans::TS_ABORTING_PARSE:
    jam();
    trans_abort_parse_recv_reply(signal, trans_ptr);
    return;
  case SchemaTrans::TS_COMMITTING:
    trans_commit_recv_reply(signal, trans_ptr);
    return;
  case SchemaTrans::TS_COMPLETING:
    trans_complete_recv_reply(signal, trans_ptr);
    return;

  case SchemaTrans::TS_STARTED:   // These states are waiting for client
    jam();                        // And should not get a "internal" reply
  case SchemaTrans::TS_COMMITTED:
    ndbrequire(false);
  }
}

#if 0
void
Dbdict::handleTransReply(Signal* signal, SchemaTransPtr trans_ptr)
{
  TransLoc& tLoc = trans_ptr.p->m_transLoc;
  SchemaOpPtr op_ptr;
  getOpPtr(tLoc, op_ptr);

  //const Uint32 trans_key = trans_ptr.p->trans_key;
  //const Uint32 clientRef = trans_ptr.p->m_clientRef;
  //const Uint32 transId = trans_ptr.p->m_transId;

  D("handleTransReply" << tLoc);
  if (!op_ptr.isNull())
    D("have op" << *op_ptr.p);

  if (hasError(trans_ptr.p->m_error)) {
    jam();
    if (tLoc.m_mode == TransMode::Normal) {
      if (tLoc.m_phase == TransPhase::Parse) {
        jam();
        setTransMode(trans_ptr, TransMode::Rollback, true);
      } else {
        jam();
        setTransMode(trans_ptr, TransMode::Abort, true);
      }
    }
  }

  if (tLoc.m_mode == TransMode::Normal) {
    if (tLoc.m_phase == TransPhase::Begin) {
      jam();
      sendTransClientReply(signal, trans_ptr);
    }
    else if (tLoc.m_phase == TransPhase::Parse) {
      jam();
      /*
       * Create any sub-operations via client signals.  This is
       * a recursive process.  When done at current level, sends reply
       * to client.  On inner levels the client is us (dict master).
       */
      createSubOps(signal, op_ptr, true);
    }
    else if (tLoc.m_phase == TransPhase::Prepare) {
      jam();
      runTransMaster(signal, trans_ptr);
    }
    else if (tLoc.m_phase == TransPhase::Commit) {
      jam();
      runTransMaster(signal, trans_ptr);
    }
    else if (tLoc.m_phase == TransPhase::End)
    {
      jam();
      trans_commit_done(signal, trans_ptr);
      return;
    }
    else {
      ndbrequire(false);
    }
  }
  else if (tLoc.m_mode == TransMode::Rollback) {
    if (tLoc.m_phase == TransPhase::Parse) {
      jam();
      /*
       * Rolling back current client op and its sub-ops.
       * We do not follow the signal train of master REQs back.
       * Instead we simply run backwards in abort mode until
       * (and including) first op of depth zero.  All ops involved
       * are removed.  On master this is done here.
       */
      ndbrequire(hasError(trans_ptr.p->m_error));
      ndbrequire(!op_ptr.isNull());
      if (tLoc.m_hold) {
        jam();
        // error seen first time, re-run current op
        runTransMaster(signal, trans_ptr);
      }
      else {
        if (op_ptr.p->m_opDepth != 0) {
          jam();
          runTransMaster(signal, trans_ptr);
        }
        else {
          jam();
          // reached original client op
          const OpInfo& info = getOpInfo(op_ptr);
          (this->*(info.m_reply))(signal, op_ptr, trans_ptr.p->m_error);
          resetError(trans_ptr);

          // restore depth counter
          trans_ptr.p->m_opDepth = 0;
          trans_ptr.p->m_clientState = TransClient::ParseReply;
        }
        iteratorRemoveLastOp(tLoc, op_ptr);
      }
    }
    else {
      ndbrequire(false);
    }
  }
  else if (tLoc.m_mode == TransMode::Abort) {
    if (tLoc.m_phase == TransPhase::Begin) {
      jam();
      sendTransClientReply(signal, trans_ptr);
      // unlock
      const DictLockReq& lockReq = trans_ptr.p->m_lockReq;
      dict_lock_unlock(signal, &lockReq);
      releaseSchemaTrans(trans_ptr);
    }
    else if (tLoc.m_phase == TransPhase::Parse) {
      jam();
      runTransMaster(signal, trans_ptr);
    }
    else if (tLoc.m_phase == TransPhase::Prepare) {
      jam();
      runTransMaster(signal, trans_ptr);
    }
    else {
      ndbrequire(false);
    }
  }
  else {
    ndbrequire(false);
  }
}
#endif

void
Dbdict::createSubOps(Signal* signal, SchemaOpPtr op_ptr, bool first)
{
  D("createSubOps" << *op_ptr.p);

  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  const OpInfo& info = getOpInfo(op_ptr);
  if ((this->*(info.m_subOps))(signal, op_ptr)) {
    jam();
    // more sub-ops on the way
    trans_ptr.p->m_curr_op_ptr_i = op_ptr.i;
    trans_ptr.p->m_state = SchemaTrans::TS_SUBOP;
    return;
  }

  ErrorInfo error;
  (this->*(info.m_reply))(signal, op_ptr, error);

  trans_ptr.p->m_clientState = TransClient::ParseReply;
  trans_ptr.p->m_state = SchemaTrans::TS_STARTED;
}

// a sub-op create failed, roll back and send REF to client
void
Dbdict::abortSubOps(Signal* signal, SchemaOpPtr op_ptr, ErrorInfo error)
{
  D("abortSubOps" << *op_ptr.p << error);
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  setError(trans_ptr, error);
  trans_rollback_sp_start(signal, trans_ptr);
}

void
Dbdict::trans_prepare_start(Signal* signal, SchemaTransPtr trans_ptr)
{
  trans_ptr.p->m_state = SchemaTrans::TS_PREPARING;

  SchemaOpPtr op_ptr;
  {
    LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
    if (list.first(op_ptr))
    {
      jam();
      trans_prepare_next(signal, trans_ptr, op_ptr);
      return;
    }
  }

  trans_prepare_done(signal, trans_ptr);
}

void
Dbdict::trans_prepare_next(Signal* signal,
                           SchemaTransPtr trans_ptr,
                           SchemaOpPtr op_ptr)
{
  ndbrequire(trans_ptr.p->m_state == SchemaTrans::TS_PREPARING);

  trans_ptr.p->m_curr_op_ptr_i = op_ptr.i;
  op_ptr.p->m_state = SchemaOp::OS_PREPARING;

  SchemaTransImplReq* req = (SchemaTransImplReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->transKey = trans_ptr.p->trans_key;
  req->opKey = op_ptr.p->op_key;
  req->requestInfo = SchemaTransImplReq::RT_PREPARE;
  req->clientRef = 0;
  req->transId = trans_ptr.p->m_transId;
  req->gsn = 0;

  trans_ptr.p->m_nodes.bitAND(c_aliveNodes);
  NdbNodeBitmask nodes = trans_ptr.p->m_nodes;
  NodeReceiverGroup rg(DBDICT, nodes);
  {
    SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
    bool ok = sc.init<SchemaTransImplRef>(rg, trans_ptr.p->trans_key);
    ndbrequire(ok);
  }

  sendSignal(rg, GSN_SCHEMA_TRANS_IMPL_REQ, signal,
             SchemaTransImplReq::SignalLength, JBB);
}

void
Dbdict::trans_prepare_recv_reply(Signal* signal, SchemaTransPtr trans_ptr)
{
  jam();

  SchemaOpPtr op_ptr;
  c_schemaOpPool.getPtr(op_ptr, trans_ptr.p->m_curr_op_ptr_i);
  if (hasError(trans_ptr.p->m_error))
  {
    jam();
    trans_ptr.p->m_state = SchemaTrans::TS_ABORTING_PREPARE;
    trans_abort_prepare_next(signal, trans_ptr, op_ptr);
    return;
  }

  op_ptr.p->m_state = SchemaOp::OS_PREPARED;
  {
    LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
    if (list.next(op_ptr))
    {
      jam();
      trans_prepare_next(signal, trans_ptr, op_ptr);
      return;
    }
  }

  trans_prepare_done(signal, trans_ptr);
  return;
}

void
Dbdict::trans_prepare_done(Signal* signal, SchemaTransPtr trans_ptr)
{
  ndbrequire(trans_ptr.p->m_state == SchemaTrans::TS_PREPARING);

  if (trans_ptr.p->m_clientFlags & TransClient::Commit)
  {
    jam();
    trans_commit_start(signal, trans_ptr);
    return;
  }

  // prepare not currently implemted (fully)
  ndbrequire(false);
}

void
Dbdict::trans_abort_parse_start(Signal* signal, SchemaTransPtr trans_ptr)
{
  trans_ptr.p->m_state = SchemaTrans::TS_ABORTING_PARSE;

  SchemaOpPtr op_ptr;
  {
    LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
    if (list.last(op_ptr))
    {
      jam();
      trans_abort_parse_next(signal, trans_ptr, op_ptr);
      return;
    }
  }

  trans_abort_parse_done(signal, trans_ptr);

}

void
Dbdict::trans_abort_parse_recv_reply(Signal* signal, SchemaTransPtr trans_ptr)
{
  SchemaOpPtr op_ptr;
  c_schemaOpPool.getPtr(op_ptr, trans_ptr.p->m_curr_op_ptr_i);

  {
    SchemaOpPtr last_op = op_ptr;
    LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
    bool prev = list.prev(op_ptr);
    list.remove(last_op);         // Release aborted op
    releaseSchemaOp(last_op);

    if (prev)
    {
      jam();
      trans_abort_parse_next(signal, trans_ptr, op_ptr);
      return;
    }
  }

  trans_abort_parse_done(signal, trans_ptr);
}

void
Dbdict::trans_abort_parse_next(Signal* signal,
                               SchemaTransPtr trans_ptr,
                               SchemaOpPtr op_ptr)
{
  ndbrequire(trans_ptr.p->m_state == SchemaTrans::TS_ABORTING_PARSE);

  trans_ptr.p->m_curr_op_ptr_i = op_ptr.i;
  op_ptr.p->m_state = SchemaOp::OS_ABORTING_PARSE;

  SchemaTransImplReq* req = (SchemaTransImplReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->transKey = trans_ptr.p->trans_key;
  req->opKey = op_ptr.p->op_key;
  req->requestInfo = SchemaTransImplReq::RT_ABORT_PARSE;
  req->clientRef = 0;
  req->transId = trans_ptr.p->m_transId;
  req->gsn = 0;

  trans_ptr.p->m_nodes.bitAND(c_aliveNodes);
  NdbNodeBitmask nodes = trans_ptr.p->m_nodes;
  nodes.bitANDC(trans_ptr.p->m_ref_nodes);
  trans_ptr.p->m_ref_nodes.clear();
  NodeReceiverGroup rg(DBDICT, nodes);
  {
    SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
    bool ok = sc.init<SchemaTransImplRef>(rg, trans_ptr.p->trans_key);
    ndbrequire(ok);
  }

  sendSignal(rg, GSN_SCHEMA_TRANS_IMPL_REQ, signal,
             SchemaTransImplReq::SignalLength, JBB);
}

void
Dbdict::trans_abort_parse_done(Signal* signal, SchemaTransPtr trans_ptr)
{
  ndbrequire(trans_ptr.p->m_state == SchemaTrans::TS_ABORTING_PARSE);

  // unlock
  const DictLockReq& lockReq = trans_ptr.p->m_lockReq;
  dict_lock_unlock(signal, &lockReq);

  trans_complete_start(signal, trans_ptr);
}

void
Dbdict::trans_abort_prepare_start(Signal* signal, SchemaTransPtr trans_ptr)
{
  trans_ptr.p->m_state = SchemaTrans::TS_ABORTING_PREPARE;

  bool last = false;
  SchemaOpPtr op_ptr;
  {
    LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
    last = list.last(op_ptr);
  }

  if (last)
  {
    jam();
    trans_abort_prepare_next(signal, trans_ptr, op_ptr);
  }
  else
  {
    jam();
    trans_abort_prepare_done(signal, trans_ptr);
  }
}

void
Dbdict::trans_abort_prepare_recv_reply(Signal* signal, SchemaTransPtr trans_ptr)
{
  // XXX error states

  SchemaOpPtr op_ptr;
  c_schemaOpPool.getPtr(op_ptr, trans_ptr.p->m_curr_op_ptr_i);

  op_ptr.p->m_state = SchemaOp::OS_ABORTED_PREPARE;

  bool prev = false;
  {
    LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
    prev = list.prev(op_ptr);
  }

  if (prev)
  {
    jam();
    trans_abort_prepare_next(signal, trans_ptr, op_ptr);
  }
  else
  {
    jam();
    trans_abort_prepare_done(signal, trans_ptr);
  }
}

void
Dbdict::trans_abort_prepare_next(Signal* signal,
                                 SchemaTransPtr trans_ptr,
                                 SchemaOpPtr op_ptr)
{
  ndbrequire(trans_ptr.p->m_state == SchemaTrans::TS_ABORTING_PREPARE);

  trans_ptr.p->m_curr_op_ptr_i = op_ptr.i;

  switch(op_ptr.p->m_state){
  case SchemaOp::OS_PARSED:
    jam();
    /**
     * Operation has not beed prepared...
     *   continue with next
     */
    trans_abort_prepare_recv_reply(signal, trans_ptr);
    return;
  case SchemaOp::OS_PREPARING:
  case SchemaOp::OS_PREPARED:
    break;
  case SchemaOp::OS_INTIAL:
  case SchemaOp::OS_PARSE_MASTER:
  case SchemaOp::OS_PARSING:
  case SchemaOp::OS_ABORTING_PREPARE:
  case SchemaOp::OS_ABORTED_PREPARE:
  case SchemaOp::OS_ABORTING_PARSE:
    //case SchemaOp::OS_ABORTED_PARSE:
  case SchemaOp::OS_COMMITTING:
  case SchemaOp::OS_COMMITTED:
#ifndef VM_TRACE
  default:
#endif
    jamLine(op_ptr.p->m_state);
    ndbrequire(false);
  }

  op_ptr.p->m_state = SchemaOp::OS_ABORTING_PREPARE;

  SchemaTransImplReq* req = (SchemaTransImplReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->transKey = trans_ptr.p->trans_key;
  req->opKey = op_ptr.p->op_key;
  req->requestInfo = SchemaTransImplReq::RT_ABORT_PREPARE;
  req->clientRef = 0;
  req->transId = trans_ptr.p->m_transId;
  req->gsn = 0;

  trans_ptr.p->m_nodes.bitAND(c_aliveNodes);
  NdbNodeBitmask nodes = trans_ptr.p->m_nodes;
  NodeReceiverGroup rg(DBDICT, nodes);
  {
    SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
    bool ok = sc.init<SchemaTransImplRef>(rg, trans_ptr.p->trans_key);
    ndbrequire(ok);
  }

  sendSignal(rg, GSN_SCHEMA_TRANS_IMPL_REQ, signal,
             SchemaTransImplReq::SignalLength, JBB);
}

void
Dbdict::trans_abort_prepare_done(Signal* signal, SchemaTransPtr trans_ptr)
{
  ndbrequire(trans_ptr.p->m_state == SchemaTrans::TS_ABORTING_PREPARE);

  /**
   * Now run abort parse
   */
  trans_abort_parse_start(signal, trans_ptr);
}

/**
 * FLOW: Rollback SP
 *   abort parse each operation (backwards) until op which is not subop
 */
void
Dbdict::trans_rollback_sp_start(Signal* signal, SchemaTransPtr trans_ptr)
{
  SchemaOpPtr op_ptr;

  {
    LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
    ndbrequire(list.last(op_ptr));
  }

  trans_ptr.p->m_state = SchemaTrans::TS_ROLLBACK_SP;

  if (op_ptr.p->m_state == SchemaOp::OS_PARSE_MASTER)
  {
    jam();
    /**
     * This op is only parsed at master..
     *
     */
    NdbNodeBitmask nodes;
    nodes.set(getOwnNodeId());
    NodeReceiverGroup rg(DBDICT, nodes);
    SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
    bool ok = sc.init<SchemaTransImplRef>(rg, trans_ptr.p->trans_key);
    ndbrequire(ok);

    const OpInfo& info = getOpInfo(op_ptr);
    (this->*(info.m_abortParse))(signal, op_ptr);
    return;
  }

  trans_rollback_sp_next(signal, trans_ptr, op_ptr);
}

void
Dbdict::trans_rollback_sp_recv_reply(Signal* signal, SchemaTransPtr trans_ptr)
{
  // TODO split trans error from op_error...?

  SchemaOpPtr op_ptr;
  c_schemaOpPool.getPtr(op_ptr, trans_ptr.p->m_curr_op_ptr_i);

  if (op_ptr.p->m_base_op_ptr_i == RNIL)
  {
    /**
     * SP
     */
    trans_rollback_sp_done(signal, trans_ptr, op_ptr);
    return;
  }

  {
    LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);

    SchemaOpPtr last_op = op_ptr;
    ndbrequire(list.prev(op_ptr)); // Must have prev, as not SP
    list.remove(last_op);         // Release aborted op
    releaseSchemaOp(last_op);
  }

  trans_rollback_sp_next(signal, trans_ptr, op_ptr);
}

void
Dbdict::trans_rollback_sp_next(Signal* signal,
                               SchemaTransPtr trans_ptr,
                               SchemaOpPtr op_ptr)
{
  trans_ptr.p->m_curr_op_ptr_i = op_ptr.i;

  SchemaTransImplReq* req = (SchemaTransImplReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->transKey = trans_ptr.p->trans_key;
  req->opKey = op_ptr.p->op_key;
  req->requestInfo = SchemaTransImplReq::RT_ABORT_PARSE;
  req->clientRef = trans_ptr.p->m_clientRef;
  req->transId = trans_ptr.p->m_transId;
  req->gsn = GSN_SCHEMA_TRANS_BEGIN_REQ;

  trans_ptr.p->m_nodes.bitAND(c_aliveNodes);
  NdbNodeBitmask nodes = trans_ptr.p->m_nodes;
  nodes.bitANDC(trans_ptr.p->m_ref_nodes);     // Note: uses m_ref_nodes
  trans_ptr.p->m_ref_nodes.clear();
  NodeReceiverGroup rg(DBDICT, nodes);
  {
    SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
    bool ok = sc.init<SchemaTransImplRef>(rg, trans_ptr.p->trans_key);
    ndbrequire(ok);
  }

  sendSignal(rg, GSN_SCHEMA_TRANS_IMPL_REQ, signal,
             SchemaTransImplReq::SignalLength, JBB);

}

void
Dbdict::trans_rollback_sp_done(Signal* signal,
                               SchemaTransPtr trans_ptr,
                               SchemaOpPtr op_ptr)
{

  ErrorInfo error = trans_ptr.p->m_error;
  const OpInfo info = getOpInfo(op_ptr);
  (this->*(info.m_reply))(signal, op_ptr, error);

  LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
  list.remove(op_ptr);
  releaseSchemaOp(op_ptr);

  resetError(trans_ptr);
  trans_ptr.p->m_clientState = TransClient::ParseReply;
  trans_ptr.p->m_state = SchemaTrans::TS_STARTED;
}

void
Dbdict::trans_commit_start(Signal* signal, SchemaTransPtr trans_ptr)
{
  jam();
  ndbout_c("trans_commit");

  trans_ptr.p->m_state = SchemaTrans::TS_COMMITTING;

  Mutex mutex(signal, c_mutexMgr, trans_ptr.p->m_commit_mutex);
  Callback c = { safe_cast(&Dbdict::trans_commit_mutex_locked), trans_ptr.i };

  // Todo should alloc mutex on SCHEMA_BEGIN
  bool ok = mutex.lock(c);
  ndbrequire(ok);
}

void
Dbdict::trans_commit_mutex_locked(Signal* signal,
                                  Uint32 transPtrI,
                                  Uint32 ret)
{
  jamEntry();
  ndbout_c("trans_commit_mutex_locked");

  SchemaTransPtr trans_ptr;
  c_schemaTransPool.getPtr(trans_ptr, transPtrI);

  ndbrequire(trans_ptr.p->m_state == SchemaTrans::TS_COMMITTING);

  bool first = false;
  SchemaOpPtr op_ptr;
  {
    LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
    first = list.first(op_ptr);
  }

  if (first)
  {
    jam();
    trans_commit_next(signal, trans_ptr, op_ptr);
  }
  else
  {
    jam();
    trans_commit_done(signal, trans_ptr);
  }
}

void
Dbdict::trans_commit_next(Signal* signal,
                          SchemaTransPtr trans_ptr,
                          SchemaOpPtr op_ptr)
{
  op_ptr.p->m_state = SchemaOp::OS_COMMITTING;
  trans_ptr.p->m_curr_op_ptr_i = op_ptr.i;

  SchemaTransImplReq* req = (SchemaTransImplReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->transKey = trans_ptr.p->trans_key;
  req->opKey = op_ptr.p->op_key;
  req->requestInfo = SchemaTransImplReq::RT_COMMIT;
  req->clientRef = trans_ptr.p->m_clientRef;
  req->transId = trans_ptr.p->m_transId;
  req->gsn = 0;

  trans_ptr.p->m_nodes.bitAND(c_aliveNodes);
  NdbNodeBitmask nodes = trans_ptr.p->m_nodes;
  NodeReceiverGroup rg(DBDICT, nodes);
  {
    SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
    bool ok = sc.init<SchemaTransImplRef>(rg, trans_ptr.p->trans_key);
    ndbrequire(ok);
  }

  sendSignal(rg, GSN_SCHEMA_TRANS_IMPL_REQ, signal,
             SchemaTransImplReq::SignalLength, JBB);
}

void
Dbdict::trans_commit_recv_reply(Signal* signal, SchemaTransPtr trans_ptr)
{
  if (hasError(trans_ptr.p->m_error))
  {
    jam();
    // kill nodes that failed COMMIT
    ndbrequire(false);
    return;
  }

  SchemaOpPtr op_ptr;
  c_schemaOpPool.getPtr(op_ptr, trans_ptr.p->m_curr_op_ptr_i);
  op_ptr.p->m_state = SchemaOp::OS_COMMITTED;

  bool next = false;
  {
    LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
    next = list.next(op_ptr);
  }

  if (next)
  {
    jam();
    trans_commit_next(signal, trans_ptr, op_ptr);
    return;
  }
  else
  {
    jam();
    trans_commit_done(signal, trans_ptr);
  }
  return;
}

void
Dbdict::trans_commit_done(Signal* signal, SchemaTransPtr trans_ptr)
{
  ndbout_c("trans_commit_done");
  Mutex mutex(signal, c_mutexMgr, trans_ptr.p->m_commit_mutex);
  Callback c = { safe_cast(&Dbdict::trans_commit_mutex_unlocked), trans_ptr.i };
  mutex.unlock(c);
}

void
Dbdict::trans_commit_mutex_unlocked(Signal* signal,
                                    Uint32 transPtrI,
                                    Uint32 ret)
{
  jamEntry();
  ndbout_c("trans_commit_mutex_unlocked");
  SchemaTransPtr trans_ptr;
  c_schemaTransPool.getPtr(trans_ptr, transPtrI);

  trans_ptr.p->m_commit_mutex.release(c_mutexMgr);

  // unlock
  const DictLockReq& lockReq = trans_ptr.p->m_lockReq;
  dict_lock_unlock(signal, &lockReq);

  /**
   * Here we should wait for SCHEMA_TRANS_COMMIT_ACK
   *
   * But for now, we proceed and complete transaction
   */
  trans_complete_start(signal, trans_ptr);
}

void
Dbdict::trans_complete_start(Signal * signal, SchemaTransPtr trans_ptr)
{
  trans_ptr.p->m_state = SchemaTrans::TS_COMPLETING;

  SchemaTransImplReq* req = (SchemaTransImplReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->transKey = trans_ptr.p->trans_key;
  req->opKey = RNIL;
  req->requestInfo = SchemaTransImplReq::RT_COMPLETE;
  req->clientRef = trans_ptr.p->m_clientRef;
  req->transId = trans_ptr.p->m_transId;
  req->gsn = 0;

  trans_ptr.p->m_nodes.bitAND(c_aliveNodes);
  NdbNodeBitmask nodes = trans_ptr.p->m_nodes;
  NodeReceiverGroup rg(DBDICT, nodes);
  {
    SafeCounter sc(c_counterMgr, trans_ptr.p->m_counter);
    bool ok = sc.init<SchemaTransImplRef>(rg, trans_ptr.p->trans_key);
    ndbrequire(ok);
  }

  sendSignal(rg, GSN_SCHEMA_TRANS_IMPL_REQ, signal,
             SchemaTransImplReq::SignalLength, JBB);
}

void
Dbdict::trans_complete_recv_reply(Signal* signal, SchemaTransPtr trans_ptr)
{
  trans_complete_done(signal, trans_ptr);
}

void
Dbdict::trans_complete_done(Signal* signal, SchemaTransPtr trans_ptr)
{
  sendTransClientReply(signal, trans_ptr);

  releaseSchemaTrans(trans_ptr);
}

// participant

void
Dbdict::execSCHEMA_TRANS_IMPL_REQ(Signal* signal)
{
  jamEntry();
  if (!assembleFragments(signal)) {
    jam();
    return;
  }

  SchemaTransImplReq reqCopy =
    *(const SchemaTransImplReq*)signal->getDataPtr();
  const SchemaTransImplReq *req = &reqCopy;

  const Uint32 rt = DictSignal::getRequestType(req->requestInfo);
  if (rt == SchemaTransImplReq::RT_START)
  {
    jam();
    slave_run_start(signal, req);
    return;
  }

  ErrorInfo error;
  SchemaTransPtr trans_ptr;
  const Uint32 trans_key = req->transKey;
  if (!findSchemaTrans(trans_ptr, trans_key))
  {
    jam();
    setError(error, SchemaTransImplRef::InvalidTransKey, __LINE__);
    goto err;
  }

  /**
   * Check *transaction* request
   */
  switch(rt){
  case SchemaTransImplReq::RT_PARSE:
    jam();
    slave_run_parse(signal, trans_ptr, req);
    return;
  case SchemaTransImplReq::RT_COMPLETE:
    jam();
    slave_run_complete(signal, trans_ptr);
    return;
  case SchemaTransImplReq::RT_FLUSH_SCHEMA:
    jam();
    slave_flush_schema(signal, trans_ptr);
    return;
  default:
    break;
  }

  SchemaOpPtr op_ptr;
  if (!findSchemaOp(op_ptr, req->opKey))
  {
    jam();
    // wl3600_todo better error no
    setError(error, SchemaTransImplRef::InvalidTransKey, __LINE__);
    goto err;
  }

  {
    const OpInfo info = getOpInfo(op_ptr);
    switch(rt){
    case SchemaTransImplReq::RT_START:
    case SchemaTransImplReq::RT_PARSE:
    case SchemaTransImplReq::RT_COMPLETE:
    case SchemaTransImplReq::RT_FLUSH_SCHEMA:
      ndbrequire(false); // handled above
    case SchemaTransImplReq::RT_PREPARE:
      jam();
      (this->*(info.m_prepare))(signal, op_ptr);
      return;
    case SchemaTransImplReq::RT_ABORT_PARSE:
      jam();
      ndbrequire(op_ptr.p->nextList == RNIL);
      (this->*(info.m_abortParse))(signal, op_ptr);
      if (!trans_ptr.p->m_isMaster)
      {
        /**
         * Remove op (except at coordinator
         */
        LocalDLFifoList<SchemaOp> list(c_schemaOpPool, trans_ptr.p->m_op_list);
        list.remove(op_ptr);
        releaseSchemaOp(op_ptr);
      }
      return;
    case SchemaTransImplReq::RT_ABORT_PREPARE:
      jam();
      (this->*(info.m_abortPrepare))(signal, op_ptr);
      return;
    case SchemaTransImplReq::RT_COMMIT:
      jam();
      (this->*(info.m_commit))(signal, op_ptr);
      return;
    }
  }

  return;
err:
  ndbrequire(false);
}

void
Dbdict::slave_run_start(Signal *signal, const SchemaTransImplReq* req)
{
  ErrorInfo error;
  SchemaTransPtr trans_ptr;
  const Uint32 trans_key = req->transKey;
  if (req->senderRef != reference())
  {
    jam();
    if (!seizeSchemaTrans(trans_ptr, trans_key))
    {
      jam();
      setError(error, SchemaTransImplRef::TooManySchemaTrans, __LINE__);
      goto err;
    }
    trans_ptr.p->m_clientRef = req->clientRef;
    trans_ptr.p->m_transId = req->transId;
    trans_ptr.p->m_isMaster = false;
    trans_ptr.p->m_masterRef = req->senderRef;
    trans_ptr.p->m_requestInfo = req->requestInfo;
  }
  else
  {
    jam();
    // this branch does nothing but is convenient for signal pong
    ndbrequire(findSchemaTrans(trans_ptr, req->transKey));
  }
  sendTransConf(signal, trans_ptr);
  return;

err:
  SchemaTrans tmp_trans;
  trans_ptr.i = RNIL;
  trans_ptr.p = &tmp_trans;
  trans_ptr.p->trans_key = trans_key;
  trans_ptr.p->m_masterRef = req->senderRef;
  setError(trans_ptr.p->m_error, error);
  sendTransRef(signal, trans_ptr);
}

void
Dbdict::slave_run_parse(Signal *signal,
                        SchemaTransPtr trans_ptr,
                        const SchemaTransImplReq* req)
{
  SchemaOpPtr op_ptr;
  D("slave_run_parse");

  const Uint32 op_key = req->opKey;
  const Uint32 gsn = req->gsn;
  const Uint32 requestInfo = req->requestInfo;
  const OpInfo& info = *findOpInfo(gsn);

  // signal data contains impl_req
  const Uint32* src = signal->getDataPtr() + SchemaTransImplReq::SignalLength;
  const Uint32 len = info.m_impl_req_length;

  SectionHandle handle(this, signal);

  ndbrequire(op_key != RNIL);
  ErrorInfo error;
  if (trans_ptr.p->m_isMaster)
  {
    jam();
    // this branch does nothing but is convenient for signal pong

    //XXX Check if op == last op in trans
    findSchemaOp(op_ptr, op_key);
    ndbrequire(!op_ptr.isNull());

    OpRecPtr oprec_ptr = op_ptr.p->m_oprec_ptr;
    const Uint32* dst = oprec_ptr.p->m_impl_req_data;
    ndbrequire(memcmp(dst, src, len << 2) == 0);
  }
  else
  {
    if (seizeSchemaOp(op_ptr, op_key, info))
    {
      jam();

      DictSignal::addRequestExtra(op_ptr.p->m_requestInfo, requestInfo);
      DictSignal::addRequestFlags(op_ptr.p->m_requestInfo, requestInfo);

      OpRecPtr oprec_ptr = op_ptr.p->m_oprec_ptr;
      Uint32* dst = oprec_ptr.p->m_impl_req_data;
      memcpy(dst, src, len << 2);

      addSchemaOp(trans_ptr, op_ptr);
      (this->*(info.m_parse))(signal, false, op_ptr, handle, error);
    } else {
      jam();
      setError(error, SchemaTransImplRef::TooManySchemaOps, __LINE__);
    }
  }

  // parse must consume but not release signal sections
  releaseSections(handle);

  if (hasError(error))
  {
    jam();
    setError(trans_ptr, error);
    sendTransRef(signal, trans_ptr);
    return;
  }
  sendTransConf(signal, op_ptr);
}

void
Dbdict::slave_flush_schema(Signal *signal,
			   SchemaTransPtr trans_ptr)
{

}

void
Dbdict::slave_run_complete(Signal* signal,
                           SchemaTransPtr trans_ptr)
{
  sendTransConf(signal, trans_ptr);
  if (!trans_ptr.p->m_isMaster)
  {
    jam();
    releaseSchemaTrans(trans_ptr);
  }
}

void
Dbdict::sendTransConf(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;

  sendTransConf(signal, trans_ptr);
}

void
Dbdict::sendTransConf(Signal* signal, SchemaTransPtr trans_ptr)
{
  ndbrequire(!trans_ptr.isNull());
  ndbrequire(signal->getNoOfSections() == 0);

  SchemaTransImplConf* conf =
    (SchemaTransImplConf*)signal->getDataPtrSend();
  conf->senderRef = reference();
  conf->transKey = trans_ptr.p->trans_key;

  const Uint32 masterRef = trans_ptr.p->m_masterRef;

  if (ERROR_INSERTED(6103)) {
    jam();
    CLEAR_ERROR_INSERT_VALUE;

    // delay CONF

    Uint32* data = &signal->theData[0];
    memmove(&data[2], &data[0], SchemaTransImplConf::SignalLength << 2);
    data[0] = 6103;
    data[1] = masterRef;
    sendSignalWithDelay(reference(), GSN_CONTINUEB, signal,
                        5000, 2 + SchemaTransImplConf::SignalLength);
    return;
  }

  sendSignal(masterRef, GSN_SCHEMA_TRANS_IMPL_CONF, signal,
             SchemaTransImplConf::SignalLength, JBB);
}

void
Dbdict::sendTransRef(Signal* signal, SchemaOpPtr op_ptr)
{
  SchemaTransPtr trans_ptr = op_ptr.p->m_trans_ptr;
  // must have error and already propagated to trans
  ndbrequire(hasError(op_ptr.p->m_error));
  ndbrequire(hasError(trans_ptr.p->m_error));
  sendTransRef(signal, trans_ptr);
}

void
Dbdict::sendTransRef(Signal* signal, SchemaTransPtr trans_ptr)
{
  D("sendTransRef");
  ndbrequire(hasError(trans_ptr.p->m_error));

  // trans is not defined on RT_BEGIN REF

  SchemaTransImplRef* ref =
    (SchemaTransImplRef*)signal->getDataPtrSend();
  ref->senderRef = reference();
  ref->transKey = trans_ptr.p->trans_key;
  getError(trans_ptr.p->m_error, ref);

  // erro has been reported, clear it
  resetError(trans_ptr.p->m_error);

  const Uint32 masterRef = trans_ptr.p->m_masterRef;
  sendSignal(masterRef, GSN_SCHEMA_TRANS_IMPL_REF, signal,
             SchemaTransImplRef::SignalLength, JBB);
}

// reply to trans client for begin/end trans

void
Dbdict::sendTransClientReply(Signal* signal, SchemaTransPtr trans_ptr)
{
  const Uint32 clientFlags = trans_ptr.p->m_clientFlags;
  D("sendTransClientReply" << hex << V(clientFlags));
  D("c_rope_pool free: " << c_rope_pool.getNoOfFree());

  Uint32 receiverRef = 0;
  Uint32 transId = 0;
  if (!(clientFlags & TransClient::TakeOver)) {
    receiverRef = trans_ptr.p->m_clientRef;
    transId = trans_ptr.p->m_transId;
  } else {
    jam();
    receiverRef = reference();
    transId = trans_ptr.p->m_takeOverTxKey;
  }

  if (trans_ptr.p->m_clientState == TransClient::BeginReq) {
    if (!hasError(trans_ptr.p->m_error)) {
      jam();
      SchemaTransBeginConf* conf =
        (SchemaTransBeginConf*)signal->getDataPtrSend();
      conf->senderRef = reference();
      conf->transId = transId;
      conf->transKey = trans_ptr.p->trans_key;
      sendSignal(receiverRef, GSN_SCHEMA_TRANS_BEGIN_CONF, signal,
                 SchemaTransBeginConf::SignalLength, JBB);
    } else {
      jam();
      SchemaTransBeginRef* ref =
        (SchemaTransBeginRef*)signal->getDataPtrSend();
      ref->senderRef = reference();
      ref->transId = transId;
      getError(trans_ptr.p->m_error, ref);
      sendSignal(receiverRef, GSN_SCHEMA_TRANS_BEGIN_REF, signal,
                 SchemaTransBeginRef::SignalLength, JBB);
    }
    resetError(trans_ptr);
    trans_ptr.p->m_clientState = TransClient::BeginReply;
    return;
  }

  if (trans_ptr.p->m_clientState == TransClient::EndReq) {
    if (!hasError(trans_ptr.p->m_error)) {
      jam();
      SchemaTransEndConf* conf =
        (SchemaTransEndConf*)signal->getDataPtrSend();
      conf->senderRef = reference();
      conf->transId = transId;
      sendSignal(receiverRef, GSN_SCHEMA_TRANS_END_CONF, signal,
                 SchemaTransEndConf::SignalLength, JBB);
    } else {
      jam();
      SchemaTransEndRef* ref =
        (SchemaTransEndRef*)signal->getDataPtrSend();
      ref->senderRef = reference();
      ref->transId = transId;
      getError(trans_ptr.p->m_error, ref);
      sendSignal(receiverRef, GSN_SCHEMA_TRANS_END_REF, signal,
                 SchemaTransEndRef::SignalLength, JBB);
    }
    resetError(trans_ptr);
    trans_ptr.p->m_clientState = TransClient::EndReply;
    return;
  }

  ndbrequire(false);
}


// DICT as schema trans client

bool
Dbdict::seizeTxHandle(TxHandlePtr& tx_ptr)
{
  Uint32 tx_key = c_opRecordSequence + 1;
  if (c_txHandleHash.seize(tx_ptr)) {
    jam();
    new (tx_ptr.p) TxHandle();
    tx_ptr.p->tx_key = tx_key;
    c_txHandleHash.add(tx_ptr);
    tx_ptr.p->m_magic = TxHandle::DICT_MAGIC;
    D("seizeTxHandle" << V(tx_key));
    c_opRecordSequence = tx_key;
    return true;
  }
  tx_ptr.setNull();
  return false;
}

bool
Dbdict::findTxHandle(TxHandlePtr& tx_ptr, Uint32 tx_key)
{
  TxHandle tx_rec(tx_key);
  if (c_txHandleHash.find(tx_ptr, tx_rec)) {
    jam();
    ndbrequire(tx_ptr.p->m_magic == TxHandle::DICT_MAGIC);
    D("findTxHandle" << V(tx_key));
    return true;
  }
  tx_ptr.setNull();
  return false;
}

void
Dbdict::releaseTxHandle(TxHandlePtr& tx_ptr)
{
  Uint32 tx_key = tx_ptr.p->tx_key;
  D("releaseTxHandle" << V(tx_key));

  ndbrequire(tx_ptr.p->m_magic == TxHandle::DICT_MAGIC);
  tx_ptr.p->m_magic = 0;
  c_txHandleHash.release(tx_ptr);
}

void
Dbdict::beginSchemaTrans(Signal* signal, TxHandlePtr tx_ptr)
{
  D("beginSchemaTrans");
  tx_ptr.p->m_transId = tx_ptr.p->tx_key;

  SchemaTransBeginReq* req =
    (SchemaTransBeginReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::addRequestFlags(requestInfo, tx_ptr.p->m_requestInfo);

  const Uint32 clientFlags = tx_ptr.p->m_clientFlags;
  ndbrequire(!(clientFlags & TransClient::TakeOver));
  req->clientRef = reference();
  req->transId = tx_ptr.p->m_transId;
  req->requestInfo = requestInfo;

  sendSignal(reference(), GSN_SCHEMA_TRANS_BEGIN_REQ, signal,
             SchemaTransBeginReq::SignalLength, JBB);
}

void
Dbdict::endSchemaTrans(Signal* signal, TxHandlePtr tx_ptr, Uint32 flags)
{
  D("endSchemaTrans" << hex << V(flags));

  SchemaTransEndReq* req =
    (SchemaTransEndReq*)signal->getDataPtrSend();

  Uint32 requestInfo = 0;
  DictSignal::addRequestFlags(requestInfo, tx_ptr.p->m_requestInfo);

  const Uint32 clientFlags = tx_ptr.p->m_clientFlags;
  Uint32 transId = 0;
  if (!(clientFlags & TransClient::TakeOver)) {
    transId = tx_ptr.p->m_transId;
  } else {
    jam();
    transId = tx_ptr.p->m_takeOverTransId;
    D("take over mode" << hex << V(transId));
  }

  req->clientRef = reference();
  req->transId = transId;
  req->transKey = tx_ptr.p->m_transKey;
  req->requestInfo = requestInfo;
  req->flags = flags;

  sendSignal(reference(), GSN_SCHEMA_TRANS_END_REQ, signal,
             SchemaTransEndReq::SignalLength, JBB);
}

void
Dbdict::execSCHEMA_TRANS_BEGIN_CONF(Signal* signal)
{
  jamEntry();
  const SchemaTransBeginConf* conf =
    (const SchemaTransBeginConf*)signal->getDataPtr();

  TxHandlePtr tx_ptr;
  findTxHandle(tx_ptr, conf->transId);
  ndbrequire(!tx_ptr.isNull());

  // record trans key
  tx_ptr.p->m_transKey = conf->transKey;

  execute(signal, tx_ptr.p->m_callback, 0);
}

void
Dbdict::execSCHEMA_TRANS_BEGIN_REF(Signal* signal)
{
  jamEntry();
  const SchemaTransBeginRef* ref =
    (const SchemaTransBeginRef*)signal->getDataPtr();

  TxHandlePtr tx_ptr;
  findTxHandle(tx_ptr, ref->transId);
  ndbrequire(!tx_ptr.isNull());

  setError(tx_ptr.p->m_error, ref);
  ndbrequire(ref->errorCode != 0);
  execute(signal, tx_ptr.p->m_callback, ref->errorCode);
}

void
Dbdict::execSCHEMA_TRANS_END_CONF(Signal* signal)
{
  jamEntry();
  const SchemaTransEndConf* conf =
    (const SchemaTransEndConf*)signal->getDataPtr();

  TxHandlePtr tx_ptr;
  findTxHandle(tx_ptr, conf->transId);
  ndbrequire(!tx_ptr.isNull());

  execute(signal, tx_ptr.p->m_callback, 0);
}

void
Dbdict::execSCHEMA_TRANS_END_REF(Signal* signal)
{
  jamEntry();
  const SchemaTransEndRef* ref =
    (const SchemaTransEndRef*)signal->getDataPtr();

  TxHandlePtr tx_ptr;
  findTxHandle(tx_ptr, ref->transId);
  ndbrequire(!tx_ptr.isNull());

  setError(tx_ptr.p->m_error, ref);
  ndbrequire(ref->errorCode != 0);
  execute(signal, tx_ptr.p->m_callback, ref->errorCode);
}

// trans client takeover

/*
 * API node failure.  Each trans is taken over by a new TxHandle,
 * unless already running in background by api request (unlikely).
 * All transes are marked with ApiFail.  API node failure handling
 * is complete when last such trans completes.  This is checked
 * in finishApiFail.
 */
void
Dbdict::handleApiFail(Signal* signal,
                      Uint32 failedApiNode,
                      BlockReference retRef)
{
  D("handleApiFail" << V(failedApiNode));

  Uint32 takeOvers = 0;
  SchemaTransPtr trans_ptr;
  c_schemaTransList.first(trans_ptr);
  while (trans_ptr.i != RNIL) {
    jam();
    D("check" << *trans_ptr.p);
    Uint32 clientRef = trans_ptr.p->m_clientRef;

    if (refToNode(clientRef) == failedApiNode)
    {
      jam();
      D("failed" << hex << V(clientRef));

      ndbrequire(!(trans_ptr.p->m_clientFlags & TransClient::ApiFail));
      trans_ptr.p->m_clientFlags |= TransClient::ApiFail;

      if (trans_ptr.p->m_isMaster)
      {
        jam();
        if (trans_ptr.p->m_clientFlags & TransClient::TakeOver)
        {
          // maybe already running in background
          jam();
          ndbrequire(trans_ptr.p->m_clientFlags & TransClient::Background);
        }
        else
        {
          takeOverTransClient(signal, trans_ptr);
        }

        TxHandlePtr tx_ptr;
        bool ok = findTxHandle(tx_ptr, trans_ptr.p->m_takeOverTxKey);
        ndbrequire(ok);

        tx_ptr.p->m_clientFlags |= TransClient::ApiFail;
        tx_ptr.p->m_apiFailRetRef = retRef;
        takeOvers++;
      }
    }

    c_schemaTransList.next(trans_ptr);
  }

  D("handleApiFail" << V(takeOvers));

  if (takeOvers == 0) {
    jam();
    sendApiFailConf(signal, failedApiNode, retRef);
  }
}

/*
 * Take over client trans, either by background request or at API
 * node failure.  Continue to the callback routine.
 */
void
Dbdict::takeOverTransClient(Signal* signal, SchemaTransPtr trans_ptr)
{
  D("takeOverTransClient" << *trans_ptr.p);

  TxHandlePtr tx_ptr;
  bool ok = seizeTxHandle(tx_ptr);
  ndbrequire(ok);

  ndbrequire(!(trans_ptr.p->m_clientFlags & TransClient::TakeOver));
  trans_ptr.p->m_clientFlags |= TransClient::TakeOver;
  trans_ptr.p->m_takeOverTxKey = tx_ptr.p->tx_key;

  tx_ptr.p->m_transId = tx_ptr.p->tx_key;
  tx_ptr.p->m_transKey = trans_ptr.p->trans_key;
  // start with SchemaTrans point of view
  tx_ptr.p->m_clientState = trans_ptr.p->m_clientState;
  tx_ptr.p->m_clientFlags = trans_ptr.p->m_clientFlags;
  tx_ptr.p->m_takeOverRef = trans_ptr.p->m_clientRef;
  tx_ptr.p->m_takeOverTransId = trans_ptr.p->m_transId;

  runTransClientTakeOver(signal, tx_ptr.p->tx_key, 0);
}

/*
 * Run one step in client takeover.  If not waiting for a signal,
 * send end trans abort.  The only commit case is when takeover
 * takes place in client state EndReq.  At the end, send an info
 * event and check if the trans was from API node failure.
 */
void
Dbdict::runTransClientTakeOver(Signal* signal,
                               Uint32 tx_key,
                               Uint32 ret)
{
  TxHandlePtr tx_ptr;
  bool ok = findTxHandle(tx_ptr, tx_key);
  ndbrequire(ok);
  D("runClientTakeOver" << *tx_ptr.p << V(ret));

  if (ret != 0) {
    jam();
    setError(tx_ptr, ret, __LINE__);
  }

  const TransClient::State oldState = tx_ptr.p->m_clientState;
  const Uint32 clientFlags = tx_ptr.p->m_clientFlags;
  ndbrequire(clientFlags & TransClient::TakeOver);

  TransClient::State newState = oldState;
  bool wait_sig = false;
  bool at_end = false;

  switch (oldState) {
  case TransClient::BeginReq:
    jam();
    newState = TransClient::BeginReply;
    wait_sig = true;
    break;
  case TransClient::BeginReply:
    jam();
    newState = TransClient::EndReply;
    break;
  case TransClient::ParseReq:
    jam();
    newState = TransClient::ParseReply;
    wait_sig = true;
    break;
  case TransClient::ParseReply:
    jam();
    newState = TransClient::EndReply;
    break;
  case TransClient::EndReq:
    jam();
    newState = TransClient::EndReply;
    wait_sig = true;
    break;
  case TransClient::EndReply:
    jam();
    newState = TransClient::StateUndef;
    at_end = true;
    break;
  default:
    ndbrequire(false);
    break;
  }

  D("set" << V(oldState) << " -> " << V(newState));
  tx_ptr.p->m_clientState = newState;

  if (!at_end) {
    jam();
    Callback c = {
      safe_cast(&Dbdict::runTransClientTakeOver),
      tx_ptr.p->tx_key
    };
    tx_ptr.p->m_callback = c;

    if (!wait_sig) {
      jam();
      Uint32 flags = 0;
      flags |= SchemaTransEndReq::SchemaTransAbort;
      endSchemaTrans(signal, tx_ptr, flags);
    }
    return;
  }

  if (!hasError(tx_ptr.p->m_error)) {
    jam();
    infoEvent("DICT: api:0x%8x trans:0x%8x takeover completed",
              tx_ptr.p->m_takeOverRef, tx_ptr.p->m_takeOverTransId);
  } else {
    jam();
    infoEvent("DICT: api:0x%8x trans:0x%8x takeover failed, error:%u line:%u",
              tx_ptr.p->m_takeOverRef, tx_ptr.p->m_takeOverTransId,
              tx_ptr.p->m_error.errorCode, tx_ptr.p->m_error.errorLine);
  }

  // if from API fail, check if finished
  if (clientFlags & TransClient::ApiFail) {
    jam();
    finishApiFail(signal, tx_ptr);
  }
  releaseTxHandle(tx_ptr);
}

void
Dbdict::finishApiFail(Signal* signal, TxHandlePtr tx_ptr)
{
  D("finishApiFail" << *tx_ptr.p);
  const Uint32 failedApiNode = refToNode(tx_ptr.p->m_takeOverRef);

  Uint32 takeOvers = 0;
  SchemaTransPtr trans_ptr;
  c_schemaTransList.first(trans_ptr);
  while (trans_ptr.i != RNIL) {
    jam();
    D("check" << *trans_ptr.p);
    const BlockReference clientRef = trans_ptr.p->m_clientRef;

    if (refToNode(clientRef) == failedApiNode) {
      jam();
      const Uint32 clientFlags = trans_ptr.p->m_clientFlags;
      D("failed" << hex << V(clientRef) << dec << V(clientFlags));
      ndbrequire(clientFlags & TransClient::ApiFail);
      takeOvers++;
    }
    c_schemaTransList.next(trans_ptr);
  }

  D("finishApiFail" << V(takeOvers));

  if (takeOvers == 0) {
    jam();
    Uint32 retRef = tx_ptr.p->m_apiFailRetRef;
    sendApiFailConf(signal, failedApiNode, retRef);
  }
}

void
Dbdict::sendApiFailConf(Signal* signal,
                        Uint32 failedApiNode,
                        BlockReference retRef)
{
  signal->theData[0] = failedApiNode;
  signal->theData[1] = reference();
  sendSignal(retRef, GSN_API_FAILCONF, signal, 2, JBB);
}

// find callback for any key

bool
Dbdict::findCallback(Callback& callback, Uint32 any_key)
{
  SchemaOpPtr op_ptr;
  SchemaTransPtr trans_ptr;
  TxHandlePtr tx_ptr;

  bool ok1 = findSchemaOp(op_ptr, any_key);
  bool ok2 = findSchemaTrans(trans_ptr, any_key);
  bool ok3 = findTxHandle(tx_ptr, any_key);
  ndbrequire(ok1 + ok2 + ok3 <= 1);

  if (ok1) {
    callback = op_ptr.p->m_callback;
    return true;
  }
  if (ok2) {
    callback = trans_ptr.p->m_callback;
    return true;
  }
  if (ok3) {
    callback = tx_ptr.p->m_callback;
    return true;
  }
  callback.m_callbackFunction = 0;
  callback.m_callbackData = 0;
  return false;
}

// MODULE: debug

#ifdef VM_TRACE

// DictObject

NdbOut&
operator<<(NdbOut& out, const Dbdict::DictObject& a)
{
  a.print(out);
  return out;
}

void
Dbdict::DictObject::print(NdbOut& out) const
{
  Dbdict* dict = (Dbdict*)globalData.getBlock(DBDICT);
  out << " (DictObject";
  out << dec << V(m_id);
  out << dec << V(m_type);
  out << " name:" << dict->copyRope<MAX_TAB_NAME_SIZE>(m_name);
  out << dec << V(m_ref_count);
  out << dec << V(m_trans_key);
  out << dec << V(m_op_ref_count);
  out << ")";
}

// ErrorInfo

NdbOut&
operator<<(NdbOut& out, const Dbdict::ErrorInfo& a)
{
  a.print(out);
  return out;
}

void
Dbdict::ErrorInfo::print(NdbOut& out) const
{
  out << " (ErrorInfo";
  out << dec << V(errorCode);
  out << dec << V(errorLine);
  out << dec << V(errorNodeId);
  out << ")";
}

// SchemaOp

NdbOut&
operator<<(NdbOut& out, const Dbdict::SchemaOp& a)
{
  a.print(out);
  return out;
}

void
Dbdict::SchemaOp::print(NdbOut& out) const
{
  //Dbdict* dict = (Dbdict*)globalData.getBlock(DBDICT);
  const Dbdict::OpInfo& info = m_oprec_ptr.p->m_opInfo;
  out << " (SchemaOp";
  out << " " << info.m_opType;
  out << dec << V(op_key);
  if (m_error.errorCode != 0)
    out << m_error;
  if (m_sections != 0)
    out << dec << V(m_sections);
  if (m_base_op_ptr_i == RNIL)
    out << "-> RNIL";
  else
  {
    out << "-> " << m_base_op_ptr_i;
  }

  out << ")";
}



// SchemaTrans

NdbOut&
operator<<(NdbOut& out, const Dbdict::SchemaTrans& a)
{
  a.print(out);
  return out;
}

void
Dbdict::SchemaTrans::print(NdbOut& out) const
{
  out << " (SchemaTrans";
  out << dec << V(m_state);
  out << dec << V(trans_key);
  out << dec << V(m_isMaster);
  out << hex << V(m_clientRef);
  out << hex << V(m_transId);
  out << dec << V(m_clientState);
  out << hex << V(m_clientFlags);
  out << ")";
}

// TxHandle

NdbOut&
operator<<(NdbOut& out, const Dbdict::TxHandle& a)
{
  a.print(out);
  return out;
}

void
Dbdict::TxHandle::print(NdbOut& out) const
{
  out << " (TxHandle";
  out << dec << V(tx_key);
  out << hex << V(m_transId);
  out << dec << V(m_transKey);
  out << dec << V(m_clientState);
  out << hex << V(m_clientFlags);
  out << hex << V(m_takeOverRef);
  out << hex << V(m_takeOverTransId);
  out << ")";
}

// check consistency when no schema trans is active

#undef SZ
#define SZ MAX_TAB_NAME_SIZE

void
Dbdict::check_consistency()
{
  D("check_consistency");

  // schema file entries // mis-named "tables"
  TableRecordPtr tablePtr;
  for (tablePtr.i = 0;
      tablePtr.i < c_tableRecordPool.getSize();
      tablePtr.i++) {
    c_tableRecordPool.getPtr(tablePtr);
    switch (tablePtr.p->tabState) {
    case TableRecord::NOT_DEFINED:
      continue;
    default:
      break;
    }
    check_consistency_entry(tablePtr);
  }

  // triggers // should be in schema file
  TriggerRecordPtr triggerPtr;
  for (triggerPtr.i = 0;
      triggerPtr.i < c_triggerRecordPool.getSize();
      triggerPtr.i++) {
    c_triggerRecordPool.getPtr(triggerPtr);
    switch (triggerPtr.p->triggerState) {
    case TriggerRecord::TS_NOT_DEFINED:
      continue;
    default:
      break;
    }
    check_consistency_trigger(triggerPtr);
  }
}

void
Dbdict::check_consistency_entry(TableRecordPtr tablePtr)
{
  switch (tablePtr.p->tableType) {
  case DictTabInfo::SystemTable:
    jam();
    check_consistency_table(tablePtr);
    break;
  case DictTabInfo::UserTable:
    jam();
    check_consistency_table(tablePtr);
    break;
  case DictTabInfo::UniqueHashIndex:
    jam();
    check_consistency_index(tablePtr);
    break;
  case DictTabInfo::OrderedIndex:
    jam();
    check_consistency_index(tablePtr);
    break;
  case DictTabInfo::Tablespace:
  case DictTabInfo::LogfileGroup:
  case DictTabInfo::Datafile:
  case DictTabInfo::Undofile:
    jam();
    break;
  default:
    ndbrequire(false);
    break;
  }
}

void
Dbdict::check_consistency_table(TableRecordPtr tablePtr)
{
  D("table " << copyRope<SZ>(tablePtr.p->tableName));
  ndbrequire(tablePtr.p->tableId == tablePtr.i);

  switch (tablePtr.p->tabState) {
  case TableRecord::DEFINED:
    jam();
    break;
  case TableRecord::BACKUP_ONGOING: // should not be a "TabState"
    jam();
    break;
  default:
    ndbrequire(false);
    break;
  }
  switch (tablePtr.p->tableType) {
  case DictTabInfo::SystemTable: // should just be "Table"
    jam();
    break;
  case DictTabInfo::UserTable: // should just be "Table"
    jam();
    break;
  default:
    ndbrequire(false);
    break;
  }

  DictObjectPtr obj_ptr;
  obj_ptr.i = tablePtr.p->m_obj_ptr_i;
  ndbrequire(obj_ptr.i != RNIL);
  c_obj_pool.getPtr(obj_ptr);
  check_consistency_object(obj_ptr);

  ndbrequire(obj_ptr.p->m_id == tablePtr.p->tableId);
  ndbrequire(!strcmp(
        copyRope<SZ>(obj_ptr.p->m_name),
        copyRope<SZ>(tablePtr.p->tableName)));
}

void
Dbdict::check_consistency_index(TableRecordPtr indexPtr)
{
  D("index " << copyRope<SZ>(indexPtr.p->tableName));
  ndbrequire(indexPtr.p->tableId == indexPtr.i);

  switch (indexPtr.p->tabState) {
  case TableRecord::DEFINED:
    jam();
    break;
  default:
    ndbrequire(false);
    break;
  }

  switch (indexPtr.p->indexState) { // these states are non-sense
  case TableRecord::IS_ONLINE:
    jam();
    break;
  default:
    ndbrequire(false);
    break;
  }

  TableRecordPtr tablePtr;
  tablePtr.i = indexPtr.p->primaryTableId;
  ndbrequire(tablePtr.i != RNIL);
  c_tableRecordPool.getPtr(tablePtr);
  check_consistency_table(tablePtr);

  bool is_unique_index = false;
  switch (indexPtr.p->tableType) {
  case DictTabInfo::UniqueHashIndex:
    jam();
    is_unique_index = true;
    break;
  case DictTabInfo::OrderedIndex:
    jam();
    break;
  default:
    ndbrequire(false);
    break;
  }

  Ptr<TriggerRecord> triggerPtr;
  triggerPtr.i = indexPtr.p->triggerId;
  ndbrequire(triggerPtr.i != RNIL);
  c_triggerRecordPool.getPtr(triggerPtr);

  ndbrequire(triggerPtr.p->tableId == tablePtr.p->tableId);
  ndbrequire(triggerPtr.p->indexId == indexPtr.p->tableId);
  ndbrequire(triggerPtr.p->triggerId == triggerPtr.i);

  check_consistency_trigger(triggerPtr);

  TriggerInfo ti;
  TriggerInfo::unpackTriggerInfo(triggerPtr.p->triggerInfo, ti);
  ndbrequire(ti.triggerEvent == TriggerEvent::TE_CUSTOM);

  DictObjectPtr obj_ptr;
  obj_ptr.i = triggerPtr.p->m_obj_ptr_i;
  ndbrequire(obj_ptr.i != RNIL);
  c_obj_pool.getPtr(obj_ptr);
  check_consistency_object(obj_ptr);

  ndbrequire(obj_ptr.p->m_id == triggerPtr.p->triggerId);
  ndbrequire(!strcmp(copyRope<SZ>(obj_ptr.p->m_name),
		     copyRope<SZ>(triggerPtr.p->triggerName)));
}

void
Dbdict::check_consistency_trigger(TriggerRecordPtr triggerPtr)
{
  ndbrequire(triggerPtr.p->triggerState == TriggerRecord::TS_ONLINE);
  ndbrequire(triggerPtr.p->triggerId == triggerPtr.i);

  TableRecordPtr tablePtr;
  tablePtr.i = triggerPtr.p->tableId;
  ndbrequire(tablePtr.i != RNIL);
  c_tableRecordPool.getPtr(tablePtr);
  check_consistency_table(tablePtr);

  if (triggerPtr.p->indexId != RNIL)
  {
    jam();
    TableRecordPtr indexPtr;
    indexPtr.i = triggerPtr.p->indexId;
    c_tableRecordPool.getPtr(indexPtr);
    ndbrequire(indexPtr.p->tabState == TableRecord::DEFINED);
    ndbrequire(indexPtr.p->indexState == TableRecord::IS_ONLINE);
    TriggerInfo ti;
    TriggerInfo::unpackTriggerInfo(triggerPtr.p->triggerInfo, ti);
    switch (ti.triggerEvent) {
    case TriggerEvent::TE_CUSTOM:
      ndbrequire(triggerPtr.i == indexPtr.p->triggerId);
      break;
    default:
      ndbrequire(false);
      break;
    }
  } else {
    ndbrequire(false);
  }
}

void
Dbdict::check_consistency_object(DictObjectPtr obj_ptr)
{
  ndbrequire(obj_ptr.p->m_trans_key == 0);
  ndbrequire(obj_ptr.p->m_op_ref_count == 0);
}

#endif
