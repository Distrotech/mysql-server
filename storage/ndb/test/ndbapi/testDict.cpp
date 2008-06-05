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
#include <HugoTransactions.hpp>
#include <UtilTransactions.hpp>
#include <NdbRestarter.hpp>
#include <Vector.hpp>
#include <signaldata/DumpStateOrd.hpp>
#include <../../include/kernel/ndb_limits.h>
#include <../../include/kernel/trigger_definitions.h>
#include <random.h>
#include <NdbAutoPtr.hpp>
#include <NdbMixRestarter.hpp>
#include <NdbSqlUtil.hpp>
#include <NdbEnv.h>

char f_tablename[256];
 
#define CHECK(b) if (!(b)) { \
  g_err << "ERR: "<< step->getName() \
         << " failed on line " << __LINE__ << endl; \
  result = NDBT_FAILED; \
  break; } 

#define CHECK2(b, c) if (!(b)) { \
  g_err << "ERR: "<< step->getName() \
         << " failed on line " << __LINE__ << ": " << c << endl; \
  result = NDBT_FAILED; \
  goto end; }

int runLoadTable(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);
  int records = ctx->getNumRecords();
  HugoTransactions hugoTrans(*ctx->getTab());
  if (hugoTrans.loadTable(pNdb, records) != 0){
    return NDBT_FAILED;
  }
  return NDBT_OK;
}

int runCreateInvalidTables(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);
  int result = NDBT_OK;

  char failTabName[256];

  for (int i = 0; i < 10; i++){
    BaseString::snprintf(failTabName, 256, "F%d", i);
  
    const NdbDictionary::Table* pFailTab = NDBT_Tables::getTable(failTabName);
    if (pFailTab != NULL){
      ndbout << "|- " << failTabName << endl;

      // Try to create table in db
      if (pFailTab->createTableInDb(pNdb) == 0){
        ndbout << failTabName << " created, this was not expected"<< endl;
        result = NDBT_FAILED;
      }

      // Verify that table is not in db    
      const NdbDictionary::Table* pTab2 = 
	NDBT_Table::discoverTableFromDb(pNdb, failTabName) ;
      if (pTab2 != NULL){
        ndbout << failTabName << " was found in DB, this was not expected"<< endl;
        result = NDBT_FAILED;
	if (pFailTab->equal(*pTab2) == true){
	  ndbout << "It was equal" << endl;
	} else {
	  ndbout << "It was not equal" << endl;
	}
	int records = 1000;
	HugoTransactions hugoTrans(*pTab2);
	if (hugoTrans.loadTable(pNdb, records) != 0){
	  ndbout << "It can NOT be loaded" << endl;
	} else{
	  ndbout << "It can be loaded" << endl;
	  
	  UtilTransactions utilTrans(*pTab2);
	  if (utilTrans.clearTable(pNdb, records, 64) != 0){
	    ndbout << "It can NOT be cleared" << endl;
	  } else{
	    ndbout << "It can be cleared" << endl;
	  }	  
	}
	
	if (pNdb->getDictionary()->dropTable(pTab2->getName()) == -1){
	  ndbout << "It can NOT be dropped" << endl;
	} else {
	  ndbout << "It can be dropped" << endl;
	}
      }
    }
  }
  return result;
}

int runCreateTheTable(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);  
  const NdbDictionary::Table* pTab = ctx->getTab();

  // Try to create table in db
  if (NDBT_Tables::createTable(pNdb, pTab->getName()) != 0){
    return NDBT_FAILED;
  }

  // Verify that table is in db     
  const NdbDictionary::Table* pTab2 = 
    NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
  if (pTab2 == NULL){
    ndbout << pTab->getName() << " was not found in DB"<< endl;
    return NDBT_FAILED;
  }
  ctx->setTab(pTab2);

  BaseString::snprintf(f_tablename, sizeof(f_tablename), pTab->getName());

  return NDBT_OK;
}

int runDropTheTable(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);  
  const NdbDictionary::Table* pTab = ctx->getTab();
  
  // Try to create table in db
  pNdb->getDictionary()->dropTable(f_tablename);
  
  return NDBT_OK;
}

int runCreateTableWhenDbIsFull(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);
  int result = NDBT_OK;
  const char* tabName = "TRANSACTION"; //Use a util table
  
  const NdbDictionary::Table* pTab = NDBT_Tables::getTable(tabName);
  if (pTab != NULL){
    ndbout << "|- " << tabName << endl;
    
    // Verify that table is not in db     
    if (NDBT_Table::discoverTableFromDb(pNdb, tabName) != NULL){
      ndbout << tabName << " was found in DB"<< endl;
      return NDBT_FAILED;
    }

    // Try to create table in db
    if (NDBT_Tables::createTable(pNdb, pTab->getName()) == 0){
      result = NDBT_FAILED;
    }

    // Verify that table is in db     
    if (NDBT_Table::discoverTableFromDb(pNdb, tabName) != NULL){
      ndbout << tabName << " was found in DB"<< endl;
      result = NDBT_FAILED;
    }
  }

  return result;
}

int runDropTableWhenDbIsFull(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);
  int result = NDBT_OK;
  const char* tabName = "TRANSACTION"; //Use a util table
  
  const NdbDictionary::Table* pTab = NDBT_Table::discoverTableFromDb(pNdb, tabName);
  if (pTab != NULL){
    ndbout << "|- TRANSACTION" << endl;
    
    // Try to drop table in db
    if (pNdb->getDictionary()->dropTable(pTab->getName()) == -1){
      result = NDBT_FAILED;
    }

    // Verify that table is not in db     
    if (NDBT_Table::discoverTableFromDb(pNdb, tabName) != NULL){
      ndbout << tabName << " was found in DB"<< endl;
      result = NDBT_FAILED;
    }
  }

  return result;

}


int runCreateAndDrop(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);
  int loops = ctx->getNumLoops();
  int i = 0;
  
  const NdbDictionary::Table* pTab = ctx->getTab();
  ndbout << "|- " << pTab->getName() << endl;
  
  while (i < loops){

    ndbout << i << ": ";    
    // Try to create table in db
    if (NDBT_Tables::createTable(pNdb, pTab->getName()) != 0){
      return NDBT_FAILED;
    }

    // Verify that table is in db     
    const NdbDictionary::Table* pTab2 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab2 == NULL){
      ndbout << pTab->getName() << " was not found in DB"<< endl;
      return NDBT_FAILED;
    }

    if (pNdb->getDictionary()->dropTable(pTab2->getName())){
      ndbout << "Failed to drop "<<pTab2->getName()<<" in db" << endl;
      return NDBT_FAILED;
    }
    
    // Verify that table is not in db     
    const NdbDictionary::Table* pTab3 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab3 != NULL){
      ndbout << pTab3->getName() << " was found in DB"<< endl;
      return NDBT_FAILED;
    }
    i++;
  }

  return NDBT_OK;
}

int runCreateAndDropAtRandom(NDBT_Context* ctx, NDBT_Step* step)
{
  myRandom48Init(NdbTick_CurrentMillisecond());
  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* pDic = pNdb->getDictionary();
  int loops = ctx->getNumLoops();
  int numTables = NDBT_Tables::getNumTables();
  bool* tabList = new bool [ numTables ];
  int tabCount;

  {
    for (int num = 0; num < numTables; num++) {
      (void)pDic->dropTable(NDBT_Tables::getTable(num)->getName());
      tabList[num] = false;
    }
    tabCount = 0;
  }

  NdbRestarter restarter;
  int result = NDBT_OK;
  int bias = 1; // 0-less 1-more
  int i = 0;
  
  while (i < loops) {
    g_info << "loop " << i << " tabs " << tabCount << "/" << numTables << endl;
    int num = myRandom48(numTables);
    const NdbDictionary::Table* pTab = NDBT_Tables::getTable(num);
    char tabName[200];
    strcpy(tabName, pTab->getName());

    if (tabList[num] == false) {
      if (bias == 0 && myRandom48(100) < 80)
        continue;
      g_info << tabName << ": create" << endl;
      if (pDic->createTable(*pTab) != 0) {
        const NdbError err = pDic->getNdbError();
        g_err << tabName << ": create failed: " << err << endl;
        result = NDBT_FAILED;
        break;
      }
      const NdbDictionary::Table* pTab2 = pDic->getTable(tabName);
      if (pTab2 == NULL) {
        const NdbError err = pDic->getNdbError();
        g_err << tabName << ": verify create: " << err << endl;
        result = NDBT_FAILED;
        break;
      }
      tabList[num] = true;
      assert(tabCount < numTables);
      tabCount++;
      if (tabCount == numTables)
        bias = 0;
    }
    else {
      if (bias == 1 && myRandom48(100) < 80)
        continue;
      g_info << tabName << ": drop" << endl;
      if (restarter.insertErrorInAllNodes(4013) != 0) {
        g_err << "error insert failed" << endl;
        result = NDBT_FAILED;
        break;
      }
      if (pDic->dropTable(tabName) != 0) {
        const NdbError err = pDic->getNdbError();
        g_err << tabName << ": drop failed: " << err << endl;
        result = NDBT_FAILED;
        break;
      }
      const NdbDictionary::Table* pTab2 = pDic->getTable(tabName);
      if (pTab2 != NULL) {
        g_err << tabName << ": verify drop: table exists" << endl;
        result = NDBT_FAILED;
        break;
      }
      if (pDic->getNdbError().code != 709 &&
          pDic->getNdbError().code != 723) {
        const NdbError err = pDic->getNdbError();
        g_err << tabName << ": verify drop: " << err << endl;
        result = NDBT_FAILED;
        break;
      }
      tabList[num] = false;
      assert(tabCount > 0);
      tabCount--;
      if (tabCount == 0)
        bias = 1;
    }
    i++;
  }
  
  for (Uint32 i = 0; i<numTables; i++)
    if (tabList[i])
      pDic->dropTable(NDBT_Tables::getTable(i)->getName());
  
  delete [] tabList;
  return result;
}


int runCreateAndDropWithData(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);
  int loops = ctx->getNumLoops();
  int records = ctx->getNumRecords();
  int i = 0;
  
  NdbRestarter restarter;
  int val = DumpStateOrd::DihMinTimeBetweenLCP;
  if(restarter.dumpStateAllNodes(&val, 1) != 0){
    int result;
    do { CHECK(0); } while (0);
    g_err << "Unable to change timebetween LCP" << endl;
    return NDBT_FAILED;
  }
  
  const NdbDictionary::Table* pTab = ctx->getTab();
  ndbout << "|- " << pTab->getName() << endl;
  
  while (i < loops){
    ndbout << i << ": ";
    // Try to create table in db
    
    if (NDBT_Tables::createTable(pNdb, pTab->getName()) != 0){
      return NDBT_FAILED;
    }

    // Verify that table is in db     
    const NdbDictionary::Table* pTab2 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab2 == NULL){
      ndbout << pTab->getName() << " was not found in DB"<< endl;
      return NDBT_FAILED;
    }

    HugoTransactions hugoTrans(*pTab2);
    if (hugoTrans.loadTable(pNdb, records) != 0){
      return NDBT_FAILED;
    }

    int count = 0;
    UtilTransactions utilTrans(*pTab2);
    if (utilTrans.selectCount(pNdb, 64, &count) != 0){
      return NDBT_FAILED;
    }
    if (count != records){
      ndbout << count <<" != "<<records << endl;
      return NDBT_FAILED;
    }

    if (pNdb->getDictionary()->dropTable(pTab2->getName()) != 0){
      ndbout << "Failed to drop "<<pTab2->getName()<<" in db" << endl;
      return NDBT_FAILED;
    }
    
    // Verify that table is not in db     
    const NdbDictionary::Table* pTab3 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab3 != NULL){
      ndbout << pTab3->getName() << " was found in DB"<< endl;
      return NDBT_FAILED;
    }
    

    i++;
  }

  return NDBT_OK;
}

int runFillTable(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);
  HugoTransactions hugoTrans(*ctx->getTab());
  if (hugoTrans.fillTable(pNdb) != 0){
    return NDBT_FAILED;
  }
  return NDBT_OK;
}

int runClearTable(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);
  int records = ctx->getNumRecords();
  
  UtilTransactions utilTrans(*ctx->getTab());
  if (utilTrans.clearTable(pNdb,  records) != 0){
    return NDBT_FAILED;
  }
  return NDBT_OK;
}

int runCreateAndDropDuring(NDBT_Context* ctx, NDBT_Step* step){
  int result = NDBT_OK;
  int loops = ctx->getNumLoops();
  int i = 0;
  
  const NdbDictionary::Table* pTab = ctx->getTab();
  ndbout << "|- " << pTab->getName() << endl;
  
  while (i < loops && result == NDBT_OK){
    ndbout << i << ": " << endl;    
    // Try to create table in db

    Ndb* pNdb = GETNDB(step);
    g_debug << "Creating table" << endl;

    if (NDBT_Tables::createTable(pNdb, pTab->getName()) != 0){
      g_err << "createTableInDb failed" << endl;
      result =  NDBT_FAILED;
      continue;
    }
    
    g_debug << "Verifying creation of table" << endl;

    // Verify that table is in db     
    const NdbDictionary::Table* pTab2 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab2 == NULL){
      g_err << pTab->getName() << " was not found in DB"<< endl;
      result =  NDBT_FAILED;
      continue;
    }
    
    NdbSleep_MilliSleep(3000);

    g_debug << "Dropping table" << endl;

    if (pNdb->getDictionary()->dropTable(pTab2->getName()) != 0){
      g_err << "Failed to drop "<<pTab2->getName()<<" in db" << endl;
      result =  NDBT_FAILED;
      continue;
    }
    
    g_debug << "Verifying dropping of table" << endl;

    // Verify that table is not in db     
    const NdbDictionary::Table* pTab3 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab3 != NULL){
      g_err << pTab3->getName() << " was found in DB"<< endl;
      result =  NDBT_FAILED;
      continue;
    }
    i++;
  }
  ctx->stopTest();
  
  return result;
}


int runUseTableUntilStopped(NDBT_Context* ctx, NDBT_Step* step){
  int records = ctx->getNumRecords();

  const NdbDictionary::Table* pTab = ctx->getTab();

  while (ctx->isTestStopped() == false) {
    // g_info << i++ << ": ";


    // Delete and recreate Ndb object
    // Otherwise you always get Invalid Schema Version
    // It would be a nice feature to remove this two lines
    //step->tearDown();
    //step->setUp();

    Ndb* pNdb = GETNDB(step);

    const NdbDictionary::Table* pTab2 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab2 == NULL)
      continue;

    int res;
    HugoTransactions hugoTrans(*pTab2);
    if ((res = hugoTrans.loadTable(pNdb, records)) != 0){
      NdbError err = pNdb->getNdbError(res);
      if(err.classification == NdbError::SchemaError){
	pNdb->getDictionary()->invalidateTable(pTab->getName());
      }
      continue;
    }
    
    if ((res = hugoTrans.clearTable(pNdb,  records)) != 0){
      NdbError err = pNdb->getNdbError(res);
      if(err.classification == NdbError::SchemaError){
	pNdb->getDictionary()->invalidateTable(pTab->getName());
      }
      continue;
    }
  }
  g_info << endl;
  return NDBT_OK;
}

int runUseTableUntilStopped2(NDBT_Context* ctx, NDBT_Step* step){
  int records = ctx->getNumRecords();

  Ndb* pNdb = GETNDB(step);
  const NdbDictionary::Table* pTab = ctx->getTab();
  const NdbDictionary::Table* pTab2 = 
    NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
  HugoTransactions hugoTrans(*pTab2);

  int i = 0;
  while (ctx->isTestStopped() == false) 
  {
    ndbout_c("loop: %u", i++);


    // Delete and recreate Ndb object
    // Otherwise you always get Invalid Schema Version
    // It would be a nice feature to remove this two lines
    //step->tearDown();
    //step->setUp();


    int res;
    if ((res = hugoTrans.loadTable(pNdb, records)) != 0){
      NdbError err = pNdb->getNdbError(res);
      if(err.classification == NdbError::SchemaError){
	pNdb->getDictionary()->invalidateTable(pTab->getName());
      }
      continue;
    }
    
    if ((res = hugoTrans.scanUpdateRecords(pNdb, records)) != 0)
    {
      NdbError err = pNdb->getNdbError(res);
      if(err.classification == NdbError::SchemaError){
	pNdb->getDictionary()->invalidateTable(pTab->getName());
      }
      continue;
    }

    if ((res = hugoTrans.clearTable(pNdb,  records)) != 0){
      NdbError err = pNdb->getNdbError(res);
      if(err.classification == NdbError::SchemaError){
	pNdb->getDictionary()->invalidateTable(pTab->getName());
      }
      continue;
    }
  }
  g_info << endl;
  return NDBT_OK;
}

int runUseTableUntilStopped3(NDBT_Context* ctx, NDBT_Step* step){
  int records = ctx->getNumRecords();

  Ndb* pNdb = GETNDB(step);
  const NdbDictionary::Table* pTab = ctx->getTab();
  const NdbDictionary::Table* pTab2 =
    NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
  HugoTransactions hugoTrans(*pTab2);

  int i = 0;
  while (ctx->isTestStopped() == false)
  {
    ndbout_c("loop: %u", i++);


    // Delete and recreate Ndb object
    // Otherwise you always get Invalid Schema Version
    // It would be a nice feature to remove this two lines
    //step->tearDown();
    //step->setUp();


    int res;
    if ((res = hugoTrans.scanUpdateRecords(pNdb, records)) != 0)
    {
      NdbError err = pNdb->getNdbError(res);
      if(err.classification == NdbError::SchemaError){
	pNdb->getDictionary()->invalidateTable(pTab->getName());
      }
      continue;
    }
  }
  g_info << endl;
  return NDBT_OK;
}


int
runCreateMaxTables(NDBT_Context* ctx, NDBT_Step* step)
{
  char tabName[256];
  int numTables = ctx->getProperty("tables", 1000);
  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* pDic = pNdb->getDictionary();
  int i = 0;
  for (i = 0; i < numTables; i++) {
    BaseString::snprintf(tabName, 256, "MAXTAB%d", i);
    if (pNdb->waitUntilReady(30) != 0) {
      // Db is not ready, return with failure
      return NDBT_FAILED;
    }
    const NdbDictionary::Table* pTab = ctx->getTab();
    //ndbout << "|- " << tabName << endl;
    // Set new name for T1
    NdbDictionary::Table newTab(* pTab);
    newTab.setName(tabName);
    // Drop any old (or try to)
    (void)pDic->dropTable(newTab.getName());
    // Try to create table in db
    if (newTab.createTableInDb(pNdb) != 0) {
      ndbout << tabName << " could not be created: "
             << pDic->getNdbError() << endl;
      if (pDic->getNdbError().code == 707 ||
          pDic->getNdbError().code == 708 ||
          pDic->getNdbError().code == 826 ||
          pDic->getNdbError().code == 827)
        break;
      return NDBT_FAILED;
    }
    // Verify that table exists in db    
    const NdbDictionary::Table* pTab3 = 
      NDBT_Table::discoverTableFromDb(pNdb, tabName) ;
    if (pTab3 == NULL){
      ndbout << tabName << " was not found in DB: "
             << pDic->getNdbError() << endl;
      return NDBT_FAILED;
    }
    if (! newTab.equal(*pTab3)) {
      ndbout << "It was not equal" << endl; abort();
      return NDBT_FAILED;
    }
    int records = ctx->getNumRecords();
    HugoTransactions hugoTrans(*pTab3);
    if (hugoTrans.loadTable(pNdb, records) != 0) {
      ndbout << "It can NOT be loaded" << endl;
      return NDBT_FAILED;
    }
    UtilTransactions utilTrans(*pTab3);
    if (utilTrans.clearTable(pNdb, records, 64) != 0) {
      ndbout << "It can NOT be cleared" << endl;
      return NDBT_FAILED;
    }
  }
  if (pNdb->waitUntilReady(30) != 0) {
    // Db is not ready, return with failure
    return NDBT_FAILED;
  }
  ctx->setProperty("maxtables", i);
  // HURRAAA!
  return NDBT_OK;
}

int runDropMaxTables(NDBT_Context* ctx, NDBT_Step* step)
{
  char tabName[256];
  int numTables = ctx->getProperty("maxtables", (Uint32)0);
  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* pDic = pNdb->getDictionary();
  for (int i = 0; i < numTables; i++) {
    BaseString::snprintf(tabName, 256, "MAXTAB%d", i);
    if (pNdb->waitUntilReady(30) != 0) {
      // Db is not ready, return with failure
      return NDBT_FAILED;
    }
    // Verify that table exists in db    
    const NdbDictionary::Table* pTab3 = 
      NDBT_Table::discoverTableFromDb(pNdb, tabName) ;
    if (pTab3 == NULL) {
      ndbout << tabName << " was not found in DB: "
             << pDic->getNdbError() << endl;
      return NDBT_FAILED;
    }
    // Try to drop table in db
    if (pDic->dropTable(pTab3->getName()) != 0) {
      ndbout << tabName << " could not be dropped: "
             << pDic->getNdbError() << endl;
      return NDBT_FAILED;
    }
  }
  return NDBT_OK;
}

int runTestFragmentTypes(NDBT_Context* ctx, NDBT_Step* step){
  int records = ctx->getNumRecords();
  int fragTtype = ctx->getProperty("FragmentType");
  Ndb* pNdb = GETNDB(step);
  int result = NDBT_OK;
  NdbRestarter restarter;

  if (pNdb->waitUntilReady(30) != 0){
    // Db is not ready, return with failure
    return NDBT_FAILED;
  }
  
  const NdbDictionary::Table* pTab = ctx->getTab();
  pNdb->getDictionary()->dropTable(pTab->getName());

  NdbDictionary::Table newTab(* pTab);
  // Set fragment type for table    
  newTab.setFragmentType((NdbDictionary::Object::FragmentType)fragTtype);
  
  // Try to create table in db
  if (newTab.createTableInDb(pNdb) != 0){
    ndbout << newTab.getName() << " could not be created"
	   << ", fragmentType = "<<fragTtype <<endl;
    ndbout << pNdb->getDictionary()->getNdbError() << endl;
    return NDBT_FAILED;
  }
  
  // Verify that table exists in db    
  const NdbDictionary::Table* pTab3 = 
    NDBT_Table::discoverTableFromDb(pNdb, pTab->getName()) ;
  if (pTab3 == NULL){
    ndbout << pTab->getName() << " was not found in DB"<< endl;
    return NDBT_FAILED;
    
  }
  
  if (pTab3->getFragmentType() != fragTtype){
    ndbout << pTab->getName() << " fragmentType error "<< endl;
    result = NDBT_FAILED;
    goto drop_the_tab;
  }
/**
   This test does not work since fragmentation is
   decided by the kernel, hence the fragementation
   attribute on the column will differ

  if (newTab.equal(*pTab3) == false){
    ndbout << "It was not equal" << endl;
    result = NDBT_FAILED;
    goto drop_the_tab;
  } 
*/
  do {
    
    HugoTransactions hugoTrans(*pTab3);
    UtilTransactions utilTrans(*pTab3);
    int count;
    CHECK(hugoTrans.loadTable(pNdb, records) == 0);
    CHECK(hugoTrans.pkUpdateRecords(pNdb, records) == 0);
    CHECK(utilTrans.selectCount(pNdb, 64, &count) == 0);
    CHECK(count == records);
    CHECK(hugoTrans.pkDelRecords(pNdb, records/2) == 0);
    CHECK(hugoTrans.scanUpdateRecords(pNdb, records/2) == 0);
    CHECK(utilTrans.selectCount(pNdb, 64, &count) == 0);
    CHECK(count == (records/2));

    // restart all
    ndbout << "Restarting cluster" << endl;
    CHECK(restarter.restartAll() == 0);
    int timeout = 120;
    CHECK(restarter.waitClusterStarted(timeout) == 0);
    CHECK(pNdb->waitUntilReady(timeout) == 0);

    // Verify content
    CHECK(utilTrans.selectCount(pNdb, 64, &count) == 0);
    CHECK(count == (records/2));

    CHECK(utilTrans.clearTable(pNdb, records) == 0);
    CHECK(hugoTrans.loadTable(pNdb, records) == 0);
    CHECK(utilTrans.clearTable(pNdb, records) == 0);
    CHECK(hugoTrans.loadTable(pNdb, records) == 0);
    CHECK(hugoTrans.pkUpdateRecords(pNdb, records) == 0);
    CHECK(utilTrans.clearTable(pNdb, records, 64) == 0);
    
  } while(false);
  
 drop_the_tab:
  
  // Try to drop table in db
  if (pNdb->getDictionary()->dropTable(pTab3->getName()) != 0){
    ndbout << pTab3->getName()  << " could not be dropped"<< endl;
    result =  NDBT_FAILED;
  }
  
  return result;
}


int runTestTemporaryTables(NDBT_Context* ctx, NDBT_Step* step){
  int result = NDBT_OK;
  int loops = ctx->getNumLoops();
  int records = ctx->getNumRecords();
  Ndb* pNdb = GETNDB(step);
  int i = 0;
  NdbRestarter restarter;
  
  const NdbDictionary::Table* pTab = ctx->getTab();
  ndbout << "|- " << pTab->getName() << endl;
  
  NdbDictionary::Table newTab(* pTab);
  // Set table as temporary
  newTab.setStoredTable(false);
  
  // Try to create table in db
  if (newTab.createTableInDb(pNdb) != 0){
    return NDBT_FAILED;
  }
  
  // Verify that table is in db     
  const NdbDictionary::Table* pTab2 = 
    NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
  if (pTab2 == NULL){
    ndbout << pTab->getName() << " was not found in DB"<< endl;
    return NDBT_FAILED;
  }

  if (pTab2->getStoredTable() != false){
    ndbout << pTab->getName() << " was not temporary in DB"<< endl;
    result = NDBT_FAILED;
    goto drop_the_tab;
  }

  
  while (i < loops && result == NDBT_OK){
    ndbout << i << ": ";

    HugoTransactions hugoTrans(*pTab2);
    CHECK(hugoTrans.loadTable(pNdb, records) == 0);

    int count = 0;
    UtilTransactions utilTrans(*pTab2);
    CHECK(utilTrans.selectCount(pNdb, 64, &count) == 0);
    CHECK(count == records);

    // restart all
    ndbout << "Restarting cluster" << endl;
    CHECK(restarter.restartAll() == 0);
    int timeout = 120;
    CHECK(restarter.waitClusterStarted(timeout) == 0);
    CHECK(pNdb->waitUntilReady(timeout) == 0);

    ndbout << "Verifying records..." << endl;
    CHECK(utilTrans.selectCount(pNdb, 64, &count) == 0);
    CHECK(count == 0);

    i++;
  }

 drop_the_tab:

   
  if (pNdb->getDictionary()->dropTable(pTab2->getName()) != 0){
    ndbout << "Failed to drop "<<pTab2->getName()<<" in db" << endl;
    result = NDBT_FAILED;
  }
  
  // Verify that table is not in db     
  const NdbDictionary::Table* pTab3 = 
    NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
  if (pTab3 != NULL){
    ndbout << pTab3->getName() << " was found in DB"<< endl;
    result = NDBT_FAILED;
  }
    
  return result;
}

int runPkSizes(NDBT_Context* ctx, NDBT_Step* step){
  int result = NDBT_OK;
  char tabName[256];
  int minPkSize = 1;
  ndbout << "minPkSize=" <<minPkSize<<endl;
  int maxPkSize = MAX_KEY_SIZE_IN_WORDS * 4;
  ndbout << "maxPkSize=" <<maxPkSize<<endl;
  Ndb* pNdb = GETNDB(step);
  int numRecords = ctx->getNumRecords();

  for (int i = minPkSize; i < maxPkSize; i++){
    BaseString::snprintf(tabName, 256, "TPK_%d", i);

    int records = numRecords;
    int max = ~0;
    // Limit num records for small PKs
    if (i == 1)
      max = 99;
    if (i == 2)
      max = 999;
    if (i == 3)
      max = 9999;
    if (records > max)
      records = max;
    ndbout << "records =" << records << endl;

    if (pNdb->waitUntilReady(30) != 0){
      // Db is not ready, return with failure
      return NDBT_FAILED;
    }
  
    ndbout << "|- " << tabName << endl;

    if (NDBT_Tables::createTable(pNdb, tabName) != 0){
      ndbout << tabName << " could not be created"<< endl;
      return NDBT_FAILED;
    }
    
    // Verify that table exists in db    
    const NdbDictionary::Table* pTab3 = 
      NDBT_Table::discoverTableFromDb(pNdb, tabName) ;
    if (pTab3 == NULL){
      g_err << tabName << " was not found in DB"<< endl;
      return NDBT_FAILED;
    }

    //    ndbout << *pTab3 << endl;

    if (pTab3->equal(*NDBT_Tables::getTable(tabName)) == false){
      g_err << "It was not equal" << endl;
      return NDBT_FAILED;
    }

    do {
      // Do it all
      HugoTransactions hugoTrans(*pTab3);
      UtilTransactions utilTrans(*pTab3);
      int count;
      CHECK(hugoTrans.loadTable(pNdb, records) == 0);
      CHECK(hugoTrans.pkUpdateRecords(pNdb, records) == 0);
      CHECK(utilTrans.selectCount(pNdb, 64, &count) == 0);
      CHECK(count == records);
      CHECK(hugoTrans.pkDelRecords(pNdb, records/2) == 0);
      CHECK(hugoTrans.scanUpdateRecords(pNdb, records/2) == 0);
      CHECK(utilTrans.selectCount(pNdb, 64, &count) == 0);
      CHECK(count == (records/2));
      CHECK(utilTrans.clearTable(pNdb, records) == 0);
      
#if 0
      // Fill table
      CHECK(hugoTrans.fillTable(pNdb) == 0);        
      CHECK(utilTrans.clearTable2(pNdb, records) == 0);
      CHECK(utilTrans.selectCount(pNdb, 64, &count) == 0);
      CHECK(count == 0);
#endif
    } while(false);

    // Drop table
    if (pNdb->getDictionary()->dropTable(pTab3->getName()) != 0){
      ndbout << "Failed to drop "<<pTab3->getName()<<" in db" << endl;
      return NDBT_FAILED;
    }
  }
  return result;
}

int runStoreFrm(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);  
  const NdbDictionary::Table* pTab = ctx->getTab();
  int result = NDBT_OK;
  int loops = ctx->getNumLoops();

  for (int l = 0; l < loops && result == NDBT_OK ; l++){

    Uint32 dataLen = (Uint32)myRandom48(MAX_FRM_DATA_SIZE);
    // size_t dataLen = 10;
    unsigned char data[MAX_FRM_DATA_SIZE];

    char start = l + 248;
    for(Uint32 i = 0; i < dataLen; i++){
      data[i] = start;
      start++;
    }
#if 0
    ndbout << "dataLen="<<dataLen<<endl;
    for (Uint32 i = 0; i < dataLen; i++){
      unsigned char c = data[i];
      ndbout << hex << c << ", ";
    }
    ndbout << endl;
#endif
        
    NdbDictionary::Table newTab(* pTab);
    void* pData = &data;
    newTab.setFrm(pData, dataLen);
    
    // Try to create table in db
    if (newTab.createTableInDb(pNdb) != 0){
      result = NDBT_FAILED;
      continue;
    }
    
    // Verify that table is in db     
    const NdbDictionary::Table* pTab2 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab2 == NULL){
      g_err << pTab->getName() << " was not found in DB"<< endl;
      result = NDBT_FAILED;
      continue;
    }
    
    const void* pData2 = pTab2->getFrmData();
    Uint32 resultLen = pTab2->getFrmLength();
    if (dataLen != resultLen){
      g_err << "Length of data failure" << endl
	    << " expected = " << dataLen << endl
	    << " got = " << resultLen << endl;
      result = NDBT_FAILED;      
    }
    
    // Verfiy the frm data
    if (memcmp(pData, pData2, resultLen) != 0){
      g_err << "Wrong data recieved" << endl;
      for (size_t i = 0; i < dataLen; i++){
	unsigned char c = ((unsigned char*)pData2)[i];
	g_err << hex << c << ", ";
      }
      g_err << endl;
      result = NDBT_FAILED;
    }
    
    if (pNdb->getDictionary()->dropTable(pTab2->getName()) != 0){
      g_err << "It can NOT be dropped" << endl;
      result = NDBT_FAILED;
    } 
  }
  
  return result;
}

int runStoreFrmError(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);  
  const NdbDictionary::Table* pTab = ctx->getTab();
  int result = NDBT_OK;
  int loops = ctx->getNumLoops();

  for (int l = 0; l < loops && result == NDBT_OK ; l++){

    const Uint32 dataLen = MAX_FRM_DATA_SIZE + 10;
    unsigned char data[dataLen];

    char start = l + 248;
    for(Uint32 i = 0; i < dataLen; i++){
      data[i] = start;
      start++;
    }
#if 0
    ndbout << "dataLen="<<dataLen<<endl;
    for (Uint32 i = 0; i < dataLen; i++){
      unsigned char c = data[i];
      ndbout << hex << c << ", ";
    }
    ndbout << endl;
#endif

    NdbDictionary::Table newTab(* pTab);
        
    void* pData = &data;
    newTab.setFrm(pData, dataLen);
    
    // Try to create table in db
    if (newTab.createTableInDb(pNdb) == 0){
      result = NDBT_FAILED;
      continue;
    }
    
    const NdbDictionary::Table* pTab2 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab2 != NULL){
      g_err << pTab->getName() << " was found in DB"<< endl;
      result = NDBT_FAILED;
      if (pNdb->getDictionary()->dropTable(pTab2->getName()) != 0){
	g_err << "It can NOT be dropped" << endl;
	result = NDBT_FAILED;
      } 
      
      continue;
    } 
    
  }

  return result;
}

int verifyTablesAreEqual(const NdbDictionary::Table* pTab, const NdbDictionary::Table* pTab2){
  // Verify that getPrimaryKey only returned true for primary keys
  for (int i = 0; i < pTab2->getNoOfColumns(); i++){
    const NdbDictionary::Column* col = pTab->getColumn(i);
    const NdbDictionary::Column* col2 = pTab2->getColumn(i);
    if (col->getPrimaryKey() != col2->getPrimaryKey()){
      g_err << "col->getPrimaryKey() != col2->getPrimaryKey()" << endl;
      return NDBT_FAILED;
    }
  }
  
  if (!pTab->equal(*pTab2)){
    g_err << "equal failed" << endl;
    g_info << *(NDBT_Table*)pTab; // gcc-4.1.2
    g_info << *(NDBT_Table*)pTab2;
    return NDBT_FAILED;
  }
  return NDBT_OK;
}

int runGetPrimaryKey(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);
  const NdbDictionary::Table* pTab = ctx->getTab();
  ndbout << "|- " << pTab->getName() << endl;
  g_info << *(NDBT_Table*)pTab;
  // Try to create table in db
  if (pTab->createTableInDb(pNdb) != 0){
    return NDBT_FAILED;
  }

  const NdbDictionary::Table* pTab2 = 
    NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
  if (pTab2 == NULL){
    ndbout << pTab->getName() << " was not found in DB"<< endl;
    return NDBT_FAILED;
  }

  int result = NDBT_OK;
  if (verifyTablesAreEqual(pTab, pTab2) != NDBT_OK)
    result = NDBT_FAILED;
  
  
#if 0
  // Create an index on the table and see what 
  // the function returns now
  char name[200];
  sprintf(name, "%s_X007", pTab->getName());
  NDBT_Index* pInd = new NDBT_Index(name);
  pInd->setTable(pTab->getName());
  pInd->setType(NdbDictionary::Index::UniqueHashIndex);
  //  pInd->setLogging(false);
  for (int i = 0; i < 2; i++){
    const NDBT_Attribute* pAttr = pTab->getAttribute(i);
    pInd->addAttribute(*pAttr);
  }
  g_info << "Create index:" << endl << *pInd;
  if (pInd->createIndexInDb(pNdb, false) != 0){
    result = NDBT_FAILED;
  }  
  delete pInd;

  const NdbDictionary::Table* pTab3 = 
    NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
  if (pTab3 == NULL){
    ndbout << pTab->getName() << " was not found in DB"<< endl;
    return NDBT_FAILED;
  }

  if (verifyTablesAreEqual(pTab, pTab3) != NDBT_OK)
    result = NDBT_FAILED;
  if (verifyTablesAreEqual(pTab2, pTab3) != NDBT_OK)
    result = NDBT_FAILED;
#endif

#if 0  
  if (pTab2->getDictionary()->dropTable(pNdb) != 0){
    ndbout << "Failed to drop "<<pTab2->getName()<<" in db" << endl;
    return NDBT_FAILED;
  }
  
  // Verify that table is not in db     
  const NdbDictionary::Table* pTab4 = 
    NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
  if (pTab4 != NULL){
    ndbout << pTab4->getName() << " was found in DB"<< endl;
    return NDBT_FAILED;
  }
#endif

  return result;
}

struct ErrorCodes { int error_id; bool crash;};
ErrorCodes
NF_codes[] = {
  {6003, true}
  ,{6004, true}
  //,6005, true,
  //{7173, false}
};

int
runNF1(NDBT_Context* ctx, NDBT_Step* step){
  NdbRestarter restarter;
  if(restarter.getNumDbNodes() < 2)
    return NDBT_OK;

  myRandom48Init(NdbTick_CurrentMillisecond());
  
  Ndb* pNdb = GETNDB(step);
  const NdbDictionary::Table* pTab = ctx->getTab();

  NdbDictionary::Dictionary* dict = pNdb->getDictionary();
  dict->dropTable(pTab->getName());

  int result = NDBT_OK;

  const int loops = ctx->getNumLoops();
  for (int l = 0; l < loops && result == NDBT_OK ; l++){
    const int sz = sizeof(NF_codes)/sizeof(NF_codes[0]);
    for(int i = 0; i<sz; i++){
      int rand = myRandom48(restarter.getNumDbNodes());
      int nodeId = restarter.getRandomNotMasterNodeId(rand);
      struct ErrorCodes err_struct = NF_codes[i];
      int error = err_struct.error_id;
      bool crash = err_struct.crash;
      
      g_info << "NF1: node = " << nodeId << " error code = " << error << endl;
      
      int val2[] = { DumpStateOrd::CmvmiSetRestartOnErrorInsert, 3};
      
      CHECK2(restarter.dumpStateOneNode(nodeId, val2, 2) == 0,
	     "failed to set RestartOnErrorInsert");

      CHECK2(restarter.insertErrorInNode(nodeId, error) == 0,
	     "failed to set error insert");
      
      CHECK2(dict->createTable(* pTab) == 0,
	     "failed to create table");
      
      if (crash) {
        CHECK2(restarter.waitNodesNoStart(&nodeId, 1) == 0,
	    "waitNodesNoStart failed");

        if(myRandom48(100) > 50){
  	  CHECK2(restarter.startNodes(&nodeId, 1) == 0,
	       "failed to start node");
          
	  CHECK2(restarter.waitClusterStarted() == 0,
	       "waitClusterStarted failed");

  	  CHECK2(dict->dropTable(pTab->getName()) == 0,
	       "drop table failed");
        } else {
	  CHECK2(dict->dropTable(pTab->getName()) == 0,
	       "drop table failed");
	
	  CHECK2(restarter.startNodes(&nodeId, 1) == 0,
	       "failed to start node");
          
	  CHECK2(restarter.waitClusterStarted() == 0,
	       "waitClusterStarted failed");
        }
      }
    }
  }
 end:  
  dict->dropTable(pTab->getName());
  
  return result;
}
  
#define APIERROR(error) \
  { g_err << "Error in " << __FILE__ << ", line:" << __LINE__ << ", code:" \
              << error.code << ", msg: " << error.message << "." << endl; \
  }

int
runCreateAutoincrementTable(NDBT_Context* ctx, NDBT_Step* step){

  Uint32 startvalues[5] = {256-2, 0, 256*256-2, ~0, 256*256*256-2};

  int ret = NDBT_OK;

  for (int jj = 0; jj < 5 && ret == NDBT_OK; jj++) {
    char tabname[] = "AUTOINCTAB";
    Uint32 startvalue = startvalues[jj];

    NdbDictionary::Table myTable;
    NdbDictionary::Column myColumn;

    Ndb* myNdb = GETNDB(step);
    NdbDictionary::Dictionary* myDict = myNdb->getDictionary();


    if (myDict->getTable(tabname) != NULL) {
      g_err << "NDB already has example table: " << tabname << endl;
      APIERROR(myNdb->getNdbError());
      return NDBT_FAILED;
    }

    myTable.setName(tabname);

    myColumn.setName("ATTR1");
    myColumn.setType(NdbDictionary::Column::Unsigned);
    myColumn.setLength(1);
    myColumn.setPrimaryKey(true);
    myColumn.setNullable(false);
    myColumn.setAutoIncrement(true);
    if (startvalue != ~0) // check that default value starts with 1
      myColumn.setAutoIncrementInitialValue(startvalue);
    myTable.addColumn(myColumn);

    if (myDict->createTable(myTable) == -1) {
      g_err << "Failed to create table " << tabname << endl;
      APIERROR(myNdb->getNdbError());
      return NDBT_FAILED;
    }


    if (startvalue == ~0) // check that default value starts with 1
      startvalue = 1;

    for (int i = 0; i < 16; i++) {

      Uint64 value;
      if (myNdb->getAutoIncrementValue(tabname, value, 1) == -1) {
        g_err << "getAutoIncrementValue failed on " << tabname << endl;
        APIERROR(myNdb->getNdbError());
        return NDBT_FAILED;
      }
      else if (value != (startvalue+i)) {
        g_err << "value = " << value << " expected " << startvalue+i << endl;;
        APIERROR(myNdb->getNdbError());
        //      ret = NDBT_FAILED;
        //      break;
      }
    }

    if (myDict->dropTable(tabname) == -1) {
      g_err << "Failed to drop table " << tabname << endl;
      APIERROR(myNdb->getNdbError());
      ret = NDBT_FAILED;
    }
  }

  return ret;
}

int
runTableRename(NDBT_Context* ctx, NDBT_Step* step){

  int result = NDBT_OK;

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* dict = pNdb->getDictionary();
  int records = ctx->getNumRecords();
  const int loops = ctx->getNumLoops();

  ndbout << "|- " << ctx->getTab()->getName() << endl;  

  for (int l = 0; l < loops && result == NDBT_OK ; l++){
    const NdbDictionary::Table* pTab = ctx->getTab();

    // Try to create table in db
    if (pTab->createTableInDb(pNdb) != 0){
      return NDBT_FAILED;
    }
    
    // Verify that table is in db     
    const NdbDictionary::Table* pTab2 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab2 == NULL){
      ndbout << pTab->getName() << " was not found in DB"<< endl;
      return NDBT_FAILED;
    }
    ctx->setTab(pTab2);

    // Load table
    HugoTransactions hugoTrans(*ctx->getTab());
    if (hugoTrans.loadTable(pNdb, records) != 0){
      return NDBT_FAILED;
    }

    // Rename table
    BaseString pTabName(pTab->getName());
    BaseString pTabNewName(pTabName);
    pTabNewName.append("xx");
    
    const NdbDictionary::Table * oldTable = dict->getTable(pTabName.c_str());
    if (oldTable) {
      NdbDictionary::Table newTable = *oldTable;
      newTable.setName(pTabNewName.c_str());
      CHECK2(dict->alterTable(*oldTable, newTable) == 0,
	     "TableRename failed");
    }
    else {
      result = NDBT_FAILED;
    }
    
    // Verify table contents
    NdbDictionary::Table pNewTab(pTabNewName.c_str());
    
    UtilTransactions utilTrans(pNewTab);
    if (utilTrans.clearTable(pNdb,  records) != 0){
      continue;
    }    

    // Drop table
    dict->dropTable(pNewTab.getName());
  }
 end:

  return result;
}

int
runTableRenameNF(NDBT_Context* ctx, NDBT_Step* step){
  NdbRestarter restarter;
  if(restarter.getNumDbNodes() < 2)
    return NDBT_OK;

  int result = NDBT_OK;

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* dict = pNdb->getDictionary();
  int records = ctx->getNumRecords();
  const int loops = ctx->getNumLoops();

  ndbout << "|- " << ctx->getTab()->getName() << endl;  

  for (int l = 0; l < loops && result == NDBT_OK ; l++){
    const NdbDictionary::Table* pTab = ctx->getTab();

    // Try to create table in db
    if (pTab->createTableInDb(pNdb) != 0){
      return NDBT_FAILED;
    }
    
    // Verify that table is in db     
    const NdbDictionary::Table* pTab2 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab2 == NULL){
      ndbout << pTab->getName() << " was not found in DB"<< endl;
      return NDBT_FAILED;
    }
    ctx->setTab(pTab2);

    // Load table
    HugoTransactions hugoTrans(*ctx->getTab());
    if (hugoTrans.loadTable(pNdb, records) != 0){
      return NDBT_FAILED;
    }

    BaseString pTabName(pTab->getName());
    BaseString pTabNewName(pTabName);
    pTabNewName.append("xx");
    
    const NdbDictionary::Table * oldTable = dict->getTable(pTabName.c_str());
    if (oldTable) {
      NdbDictionary::Table newTable = *oldTable;
      newTable.setName(pTabNewName.c_str());
      CHECK2(dict->alterTable(*oldTable, newTable) == 0,
	     "TableRename failed");
    }
    else {
      result = NDBT_FAILED;
    }
    
    // Restart one node at a time
    
    /**
     * Need to run LCP at high rate otherwise
     * packed replicas become "to many"
     */
    int val = DumpStateOrd::DihMinTimeBetweenLCP;
    if(restarter.dumpStateAllNodes(&val, 1) != 0){
      do { CHECK(0); } while(0);
      g_err << "Failed to set LCP to min value" << endl;
      return NDBT_FAILED;
    }
    
    const int numNodes = restarter.getNumDbNodes();
    for(int i = 0; i<numNodes; i++){
      int nodeId = restarter.getDbNodeId(i);
      int error = NF_codes[i].error_id;

      g_info << "NF1: node = " << nodeId << " error code = " << error << endl;

      CHECK2(restarter.restartOneDbNode(nodeId) == 0,
	     "failed to set restartOneDbNode");

      CHECK2(restarter.waitClusterStarted() == 0,
	     "waitClusterStarted failed");

    }

    // Verify table contents
    NdbDictionary::Table pNewTab(pTabNewName.c_str());
    
    UtilTransactions utilTrans(pNewTab);
    if (utilTrans.clearTable(pNdb,  records) != 0){
      continue;
    }    

    // Drop table
    dict->dropTable(pTabNewName.c_str());
  }
 end:    
  return result;
}

int
runTableRenameSR(NDBT_Context* ctx, NDBT_Step* step){
  NdbRestarter restarter;
  if(restarter.getNumDbNodes() < 2)
    return NDBT_OK;

  int result = NDBT_OK;

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* dict = pNdb->getDictionary();
  int records = ctx->getNumRecords();
  const int loops = ctx->getNumLoops();

  ndbout << "|- " << ctx->getTab()->getName() << endl;  

  for (int l = 0; l < loops && result == NDBT_OK ; l++){
    // Rename table
    const NdbDictionary::Table* pTab = ctx->getTab();

    // Try to create table in db
    if (pTab->createTableInDb(pNdb) != 0){
      return NDBT_FAILED;
    }
    
    // Verify that table is in db     
    const NdbDictionary::Table* pTab2 = 
      NDBT_Table::discoverTableFromDb(pNdb, pTab->getName());
    if (pTab2 == NULL){
      ndbout << pTab->getName() << " was not found in DB"<< endl;
      return NDBT_FAILED;
    }
    ctx->setTab(pTab2);

    // Load table
    HugoTransactions hugoTrans(*ctx->getTab());
    if (hugoTrans.loadTable(pNdb, records) != 0){
      return NDBT_FAILED;
    }

    BaseString pTabName(pTab->getName());
    BaseString pTabNewName(pTabName);
    pTabNewName.append("xx");
    
    const NdbDictionary::Table * oldTable = dict->getTable(pTabName.c_str());
    if (oldTable) {
      NdbDictionary::Table newTable = *oldTable;
      newTable.setName(pTabNewName.c_str());
      CHECK2(dict->alterTable(*oldTable, newTable) == 0,
	     "TableRename failed");
    }
    else {
      result = NDBT_FAILED;
    }
    
    // Restart cluster
    
    /**
     * Need to run LCP at high rate otherwise
     * packed replicas become "to many"
     */
    int val = DumpStateOrd::DihMinTimeBetweenLCP;
    if(restarter.dumpStateAllNodes(&val, 1) != 0){
      do { CHECK(0); } while(0);
      g_err << "Failed to set LCP to min value" << endl;
      return NDBT_FAILED;
    }
    
    CHECK2(restarter.restartAll() == 0,
	   "failed to set restartOneDbNode");
    
    CHECK2(restarter.waitClusterStarted() == 0,
	   "waitClusterStarted failed");
    
    // Verify table contents
    NdbDictionary::Table pNewTab(pTabNewName.c_str());
    
    UtilTransactions utilTrans(pNewTab);
    if (utilTrans.clearTable(pNdb,  records) != 0){
      continue;
    }    

    // Drop table
    dict->dropTable(pTabNewName.c_str());
  }
 end:    
  return result;
}

/*
  Run online alter table add attributes.
 */
int
runTableAddAttrs(NDBT_Context* ctx, NDBT_Step* step){

  int result = NDBT_OK;

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* dict = pNdb->getDictionary();
  int records = ctx->getNumRecords();
  const int loops = ctx->getNumLoops();

  ndbout << "|- " << ctx->getTab()->getName() << endl;  

  NdbDictionary::Table myTab= *(ctx->getTab());

  for (int l = 0; l < loops && result == NDBT_OK ; l++){
    // Try to create table in db

    if (NDBT_Tables::createTable(pNdb, myTab.getName()) != 0){
      return NDBT_FAILED;
    }

    // Verify that table is in db     
    const NdbDictionary::Table* pTab2 = 
      NDBT_Table::discoverTableFromDb(pNdb, myTab.getName());
    if (pTab2 == NULL){
      ndbout << myTab.getName() << " was not found in DB"<< endl;
      return NDBT_FAILED;
    }
    ctx->setTab(pTab2);

    /*
      Check that table already has a varpart, otherwise add attr is
      not possible.
    */
    if (pTab2->getForceVarPart() == false)
    {
      const NdbDictionary::Column *col;
      for (Uint32 i= 0; (col= pTab2->getColumn(i)) != 0; i++)
      {
        if (col->getStorageType() == NDB_STORAGETYPE_MEMORY &&
            (col->getDynamic() || col->getArrayType() != NDB_ARRAYTYPE_FIXED))
          break;
      }
      if (col == 0)
      {
        /* Alter table add attribute not applicable, just mark success. */
        dict->dropTable(pTab2->getName());
        break;
      }
    }

    // Load table
    HugoTransactions beforeTrans(*ctx->getTab());
    if (beforeTrans.loadTable(pNdb, records) != 0){
      return NDBT_FAILED;
    }

    // Add attributes to table.
    BaseString pTabName(pTab2->getName());
    
    const NdbDictionary::Table * oldTable = dict->getTable(pTabName.c_str());
    if (oldTable) {
      NdbDictionary::Table newTable= *oldTable;

      NDBT_Attribute newcol1("NEWKOL1", NdbDictionary::Column::Unsigned, 1,
                            false, true, 0,
                            NdbDictionary::Column::StorageTypeMemory, true);
      newTable.addColumn(newcol1);
      NDBT_Attribute newcol2("NEWKOL2", NdbDictionary::Column::Char, 14,
                            false, true, 0,
                            NdbDictionary::Column::StorageTypeMemory, true);
      newTable.addColumn(newcol2);
      NDBT_Attribute newcol3("NEWKOL3", NdbDictionary::Column::Bit, 20,
                            false, true, 0,
                            NdbDictionary::Column::StorageTypeMemory, true);
      newTable.addColumn(newcol3);
      NDBT_Attribute newcol4("NEWKOL4", NdbDictionary::Column::Varbinary, 42,
                            false, true, 0,
                            NdbDictionary::Column::StorageTypeMemory, true);
      newTable.addColumn(newcol4);

      CHECK2(dict->alterTable(*oldTable, newTable) == 0,
	     "TableAddAttrs failed");
      /* Need to purge old version and reload new version after alter table. */
      dict->invalidateTable(pTabName.c_str());
    }
    else {
      result = NDBT_FAILED;
    }

    {
      HugoTransactions afterTrans(* dict->getTable(pTabName.c_str()));

      ndbout << "delete...";
      if (afterTrans.clearTable(pNdb) != 0)
      {
        return NDBT_FAILED;
      }
      ndbout << endl;

      ndbout << "insert...";
      if (afterTrans.loadTable(pNdb, records) != 0){
        return NDBT_FAILED;
      }
      ndbout << endl;

      ndbout << "update...";
      if (afterTrans.scanUpdateRecords(pNdb, records) != 0)
      {
        return NDBT_FAILED;
      }
      ndbout << endl;

      ndbout << "delete...";
      if (afterTrans.clearTable(pNdb) != 0)
      {
        return NDBT_FAILED;
      }
      ndbout << endl;
    }
    
    // Drop table.
    dict->dropTable(pTabName.c_str());
  }
 end:

  return result;
}

/*
  Run online alter table add attributes while running simultaneous
  transactions on it in separate thread.
 */
int
runTableAddAttrsDuring(NDBT_Context* ctx, NDBT_Step* step){

  int result = NDBT_OK;

  int records = ctx->getNumRecords();
  const int loops = ctx->getNumLoops();

  ndbout << "|- " << ctx->getTab()->getName() << endl;  

  NdbDictionary::Table myTab= *(ctx->getTab());

  if (myTab.getForceVarPart() == false)
  {
    const NdbDictionary::Column *col;
    for (Uint32 i= 0; (col= myTab.getColumn(i)) != 0; i++)
    {
      if (col->getStorageType() == NDB_STORAGETYPE_MEMORY &&
          (col->getDynamic() || col->getArrayType() != NDB_ARRAYTYPE_FIXED))
        break;
    }
    if (col == 0)
    {
      ctx->stopTest();
      return NDBT_OK;
    }
  }

  //if 

  for (int l = 0; l < loops && result == NDBT_OK ; l++){
    ndbout << l << ": " << endl;    

    Ndb* pNdb = GETNDB(step);
    NdbDictionary::Dictionary* dict = pNdb->getDictionary();

    /*
      Check that table already has a varpart, otherwise add attr is
      not possible.
    */

    // Add attributes to table.
    ndbout << "Altering table" << endl;
    
    const NdbDictionary::Table * oldTable = dict->getTable(myTab.getName());
    if (oldTable) {
      NdbDictionary::Table newTable= *oldTable;
      
      char name[256];
      BaseString::snprintf(name, sizeof(name), "NEWCOL%d", l);
      NDBT_Attribute newcol1(name, NdbDictionary::Column::Unsigned, 1,
                             false, true, 0,
                             NdbDictionary::Column::StorageTypeMemory, true);
      newTable.addColumn(newcol1);
      //ToDo: check #loops, how many columns l
      
      CHECK2(dict->alterTable(*oldTable, newTable) == 0,
	     "TableAddAttrsDuring failed");
      
      dict->invalidateTable(myTab.getName());
      const NdbDictionary::Table * newTab = dict->getTable(myTab.getName());
      HugoTransactions hugoTrans(* newTab);
      hugoTrans.scanUpdateRecords(pNdb, records);
    }
    else {
      result= NDBT_FAILED;
      break;
    }
  }
 end:

  ctx->stopTest();

  return result;
}

static void
f(const NdbDictionary::Column * col){
  if(col == 0){
    abort();
  }
}

int
runTestDictionaryPerf(NDBT_Context* ctx, NDBT_Step* step){
  Vector<char*> cols;
  Vector<const NdbDictionary::Table*> tabs;
  int i;

  Ndb* pNdb = GETNDB(step);  

  const Uint32 count = NDBT_Tables::getNumTables();
  for (i=0; i < count; i++){
    const NdbDictionary::Table * tab = NDBT_Tables::getTable(i);
    pNdb->getDictionary()->createTable(* tab);
    
    const NdbDictionary::Table * tab2 = pNdb->getDictionary()->getTable(tab->getName());
    
    for(size_t j = 0; j<tab->getNoOfColumns(); j++){
      cols.push_back((char*)tab2);
      cols.push_back(strdup(tab->getColumn(j)->getName()));
    }
  }

  const Uint32 times = 10000000;

  ndbout_c("%d tables and %d columns", 
	   NDBT_Tables::getNumTables(), cols.size()/2);

  char ** tcols = cols.getBase();

  srand(time(0));
  Uint32 size = cols.size() / 2;
  char ** columns = &cols[0];
  Uint64 start = NdbTick_CurrentMillisecond();
  for(i = 0; i<times; i++){
    int j = 2 * (rand() % size);
    const NdbDictionary::Table* tab = (const NdbDictionary::Table*)tcols[j];
    const char * col = tcols[j+1];
    const NdbDictionary::Column* column = tab->getColumn(col);
    f(column);
  }
  Uint64 stop = NdbTick_CurrentMillisecond();
  stop -= start;

  Uint64 per = stop;
  per *= 1000;
  per /= times;
  
  ndbout_c("%d random getColumn(name) in %Ld ms -> %d us/get",
	   times, stop, per);

  return NDBT_OK;
}

int
runCreateLogfileGroup(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);  
  NdbDictionary::LogfileGroup lg;
  lg.setName("DEFAULT-LG");
  lg.setUndoBufferSize(8*1024*1024);
  
  int res;
  res = pNdb->getDictionary()->createLogfileGroup(lg);
  if(res != 0){
    g_err << "Failed to create logfilegroup:"
	  << endl << pNdb->getDictionary()->getNdbError() << endl;
    return NDBT_FAILED;
  }

  NdbDictionary::Undofile uf;
  uf.setPath("undofile01.dat");
  uf.setSize(5*1024*1024);
  uf.setLogfileGroup("DEFAULT-LG");
  
  res = pNdb->getDictionary()->createUndofile(uf);
  if(res != 0){
    g_err << "Failed to create undofile:"
	  << endl << pNdb->getDictionary()->getNdbError() << endl;
    return NDBT_FAILED;
  }

  uf.setPath("undofile02.dat");
  uf.setSize(5*1024*1024);
  uf.setLogfileGroup("DEFAULT-LG");
  
  res = pNdb->getDictionary()->createUndofile(uf);
  if(res != 0){
    g_err << "Failed to create undofile:"
	  << endl << pNdb->getDictionary()->getNdbError() << endl;
    return NDBT_FAILED;
  }
  
  return NDBT_OK;
}

int
runCreateTablespace(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);  
  NdbDictionary::Tablespace lg;
  lg.setName("DEFAULT-TS");
  lg.setExtentSize(1024*1024);
  lg.setDefaultLogfileGroup("DEFAULT-LG");

  int res;
  res = pNdb->getDictionary()->createTablespace(lg);
  if(res != 0){
    g_err << "Failed to create tablespace:"
	  << endl << pNdb->getDictionary()->getNdbError() << endl;
    return NDBT_FAILED;
  }

  NdbDictionary::Datafile uf;
  uf.setPath("datafile01.dat");
  uf.setSize(10*1024*1024);
  uf.setTablespace("DEFAULT-TS");

  res = pNdb->getDictionary()->createDatafile(uf);
  if(res != 0){
    g_err << "Failed to create datafile:"
	  << endl << pNdb->getDictionary()->getNdbError() << endl;
    return NDBT_FAILED;
  }

  return NDBT_OK;
}
int
runCreateDiskTable(NDBT_Context* ctx, NDBT_Step* step){
  Ndb* pNdb = GETNDB(step);  

  NdbDictionary::Table tab = *ctx->getTab();
  tab.setTablespaceName("DEFAULT-TS");
  
  for(Uint32 i = 0; i<tab.getNoOfColumns(); i++)
    if(!tab.getColumn(i)->getPrimaryKey())
      tab.getColumn(i)->setStorageType(NdbDictionary::Column::StorageTypeDisk);
  
  int res;
  res = pNdb->getDictionary()->createTable(tab);
  if(res != 0){
    g_err << "Failed to create table:"
	  << endl << pNdb->getDictionary()->getNdbError() << endl;
    return NDBT_FAILED;
  }
  
  return NDBT_OK;
}

#include <NDBT_Tables.hpp>

int runFailAddFragment(NDBT_Context* ctx, NDBT_Step* step){
  static int acclst[] = { 3001, 6200, 6202 };
  static int tuplst[] = { 4007, 4008, 4009, 4010 };
  static int tuxlst[] = { 12001, 12002, 12003, 12004, 
                          6201, 6203 };
  static unsigned acccnt = sizeof(acclst)/sizeof(acclst[0]);
  static unsigned tupcnt = sizeof(tuplst)/sizeof(tuplst[0]);
  static unsigned tuxcnt = sizeof(tuxlst)/sizeof(tuxlst[0]);

  NdbRestarter restarter;
  int nodeId = restarter.getMasterNodeId();
  Ndb* pNdb = GETNDB(step);  
  NdbDictionary::Dictionary* pDic = pNdb->getDictionary();
  NdbDictionary::Table tab(*ctx->getTab());
  tab.setFragmentType(NdbDictionary::Object::FragAllLarge);

  int errNo = 0;
  char buf[100];
  if (NdbEnv_GetEnv("ERRNO", buf, sizeof(buf)))
  {
    errNo = atoi(buf);
    ndbout_c("Using errno: %u", errNo);
  }
  
  // ordered index on first few columns
  NdbDictionary::Index idx("X");
  idx.setTable(tab.getName());
  idx.setType(NdbDictionary::Index::OrderedIndex);
  idx.setLogging(false);
  for (int cnt = 0, i_hate_broken_compilers = 0;
       cnt < 3 &&
       i_hate_broken_compilers < tab.getNoOfColumns();
       i_hate_broken_compilers++) {
    if (NdbSqlUtil::check_column_for_ordered_index
        (tab.getColumn(i_hate_broken_compilers)->getType(), 0) == 0 &&
        tab.getColumn(i_hate_broken_compilers)->getStorageType() != 
        NdbDictionary::Column::StorageTypeDisk)
    {
      idx.addColumn(*tab.getColumn(i_hate_broken_compilers));
      cnt++;
    }
  }

  for (Uint32 i = 0; i<tab.getNoOfColumns(); i++)
  {
    if (tab.getColumn(i)->getStorageType() == 
        NdbDictionary::Column::StorageTypeDisk)
    {
      NDBT_Tables::create_default_tablespace(pNdb);
      break;
    }
  }

  const int loops = ctx->getNumLoops();
  int result = NDBT_OK;
  (void)pDic->dropTable(tab.getName());

  int dump1 = DumpStateOrd::SchemaResourceSnapshot;
  int dump2 = DumpStateOrd::SchemaResourceCheckLeak;

  for (int l = 0; l < loops; l++) {
    for (unsigned i0 = 0; i0 < acccnt; i0++) {
      unsigned j = (l == 0 ? i0 : myRandom48(acccnt));
      int errval = acclst[j];
      if (errNo != 0 && errNo != errval)
        continue;
      g_info << "insert error node=" << nodeId << " value=" << errval << endl;
      CHECK2(restarter.insertErrorInNode(nodeId, errval) == 0,
             "failed to set error insert");
      CHECK(restarter.dumpStateAllNodes(&dump1, 1) == 0);

      CHECK2(pDic->createTable(tab) != 0,
             "failed to fail after error insert " << errval);
      CHECK(restarter.dumpStateAllNodes(&dump2, 1) == 0);
      CHECK2(pDic->createTable(tab) == 0,
             pDic->getNdbError());
      CHECK2(pDic->dropTable(tab.getName()) == 0,
             pDic->getNdbError());
    }
    for (unsigned i1 = 0; i1 < tupcnt; i1++) {
      unsigned j = (l == 0 ? i1 : myRandom48(tupcnt));
      int errval = tuplst[j];
      if (errNo != 0 && errNo != errval)
        continue;
      g_info << "insert error node=" << nodeId << " value=" << errval << endl;
      CHECK2(restarter.insertErrorInNode(nodeId, errval) == 0,
             "failed to set error insert");
      CHECK(restarter.dumpStateAllNodes(&dump1, 1) == 0);
      CHECK2(pDic->createTable(tab) != 0,
             "failed to fail after error insert " << errval);
      CHECK(restarter.dumpStateAllNodes(&dump2, 1) == 0);
      CHECK2(pDic->createTable(tab) == 0,
             pDic->getNdbError());
      CHECK2(pDic->dropTable(tab.getName()) == 0,
             pDic->getNdbError());
    }
    for (unsigned i2 = 0; i2 < tuxcnt; i2++) {
      unsigned j = (l == 0 ? i2 : myRandom48(tuxcnt));
      int errval = tuxlst[j];
      if (errNo != 0 && errNo != errval)
        continue;
      g_info << "insert error node=" << nodeId << " value=" << errval << endl;
      CHECK2(restarter.insertErrorInNode(nodeId, errval) == 0,
             "failed to set error insert");
      CHECK2(pDic->createTable(tab) == 0,
             pDic->getNdbError());
      CHECK(restarter.dumpStateAllNodes(&dump1, 1) == 0);
      CHECK2(pDic->createIndex(idx) != 0,
             "failed to fail after error insert " << errval);
      CHECK(restarter.dumpStateAllNodes(&dump2, 1) == 0);
      CHECK2(pDic->createIndex(idx) == 0,
             pDic->getNdbError());
      CHECK2(pDic->dropTable(tab.getName()) == 0,
             pDic->getNdbError());
    }
  }
end:
  return result;
}

// NFNR

// Restarter controls dict ops : 1-run 2-pause 3-stop
// synced by polling...

static bool
send_dict_ops_cmd(NDBT_Context* ctx, Uint32 cmd)
{
  ctx->setProperty("DictOps_CMD", cmd);
  while (1) {
    if (ctx->isTestStopped())
      return false;
    if (ctx->getProperty("DictOps_ACK") == cmd)
      break;
    NdbSleep_MilliSleep(100);
  }
  return true;
}

static bool
recv_dict_ops_run(NDBT_Context* ctx)
{
  while (1) {
    if (ctx->isTestStopped())
      return false;
    Uint32 cmd = ctx->getProperty("DictOps_CMD");
    ctx->setProperty("DictOps_ACK", cmd);
    if (cmd == 1)
      break;
    if (cmd == 3)
      return false;
    NdbSleep_MilliSleep(100);
  }
  return true;
}

int
runRestarts(NDBT_Context* ctx, NDBT_Step* step)
{
  static int errlst_master[] = {   // non-crashing
    7175,       // send one fake START_PERMREF
    0 
  };
  static int errlst_node[] = {
    7174,       // crash before sending DICT_LOCK_REQ
    7176,       // pretend master does not support DICT lock
    7121,       // crash at receive START_PERMCONF
    0
  };
  const uint errcnt_master = sizeof(errlst_master)/sizeof(errlst_master[0]);
  const uint errcnt_node = sizeof(errlst_node)/sizeof(errlst_node[0]);

  myRandom48Init(NdbTick_CurrentMillisecond());
  NdbRestarter restarter;
  int result = NDBT_OK;
  const int loops = ctx->getNumLoops();

  for (int l = 0; l < loops && result == NDBT_OK; l++) {
    g_info << "1: === loop " << l << " ===" << endl;

    // assuming 2-way replicated

    int numnodes = restarter.getNumDbNodes();
    CHECK(numnodes >= 1);
    if (numnodes == 1)
      break;

    int masterNodeId = restarter.getMasterNodeId();
    CHECK(masterNodeId != -1);

    // for more complex cases need more restarter support methods

    int nodeIdList[2] = { 0, 0 };
    int nodeIdCnt = 0;

    if (numnodes >= 2) {
      int rand = myRandom48(numnodes);
      int nodeId = restarter.getRandomNotMasterNodeId(rand);
      CHECK(nodeId != -1);
      nodeIdList[nodeIdCnt++] = nodeId;
    }

    if (numnodes >= 4 && myRandom48(2) == 0) {
      int rand = myRandom48(numnodes);
      int nodeId = restarter.getRandomNodeOtherNodeGroup(nodeIdList[0], rand);
      CHECK(nodeId != -1);
      if (nodeId != masterNodeId)
        nodeIdList[nodeIdCnt++] = nodeId;
    }

    g_info << "1: master=" << masterNodeId << " nodes=" << nodeIdList[0] << "," << nodeIdList[1] << endl;

    const uint timeout = 60; //secs for node wait
    const unsigned maxsleep = 2000; //ms

    bool NF_ops = ctx->getProperty("Restart_NF_ops");
    uint NF_type = ctx->getProperty("Restart_NF_type");
    bool NR_ops = ctx->getProperty("Restart_NR_ops");
    bool NR_error = ctx->getProperty("Restart_NR_error");

    g_info << "1: " << (NF_ops ? "run" : "pause") << " dict ops" << endl;
    if (! send_dict_ops_cmd(ctx, NF_ops ? 1 : 2))
      break;
    NdbSleep_MilliSleep(myRandom48(maxsleep));

    {
      for (int i = 0; i < nodeIdCnt; i++) {
        int nodeId = nodeIdList[i];

        bool nostart = true;
        bool abort = NF_type == 0 ? myRandom48(2) : (NF_type == 2);
        bool initial = myRandom48(2);

        char flags[40];
        strcpy(flags, "flags: nostart");
        if (abort)
          strcat(flags, ",abort");
        if (initial)
          strcat(flags, ",initial");

        g_info << "1: restart " << nodeId << " " << flags << endl;
        CHECK(restarter.restartOneDbNode(nodeId, initial, nostart, abort) == 0);
      }
    }

    g_info << "1: wait for nostart" << endl;
    CHECK(restarter.waitNodesNoStart(nodeIdList, nodeIdCnt, timeout) == 0);
    NdbSleep_MilliSleep(myRandom48(maxsleep));

    int err_master = 0;
    int err_node[2] = { 0, 0 };

    if (NR_error) {
      err_master = errlst_master[l % errcnt_master];

      // limitation: cannot have 2 node restarts and crash_insert
      // one node may die for real (NF during startup)

      for (int i = 0; i < nodeIdCnt && nodeIdCnt == 1; i++) {
        err_node[i] = errlst_node[l % errcnt_node];

        // 7176 - no DICT lock protection

        if (err_node[i] == 7176) {
          g_info << "1: no dict ops due to error insert "
                 << err_node[i] << endl;
          NR_ops = false;
        }
      }
    }

    g_info << "1: " << (NR_ops ? "run" : "pause") << " dict ops" << endl;
    if (! send_dict_ops_cmd(ctx, NR_ops ? 1 : 2))
      break;
    NdbSleep_MilliSleep(myRandom48(maxsleep));

    g_info << "1: start nodes" << endl;
    CHECK(restarter.startNodes(nodeIdList, nodeIdCnt) == 0);

    if (NR_error) {
      {
        int err = err_master;
        if (err != 0) {
          g_info << "1: insert master error " << err << endl;
          CHECK(restarter.insertErrorInNode(masterNodeId, err) == 0);
        }
      }

      for (int i = 0; i < nodeIdCnt; i++) {
        int nodeId = nodeIdList[i];

        int err = err_node[i];
        if (err != 0) {
          g_info << "1: insert node " << nodeId << " error " << err << endl;
          CHECK(restarter.insertErrorInNode(nodeId, err) == 0);
        }
      }
    }
    NdbSleep_MilliSleep(myRandom48(maxsleep));

    g_info << "1: wait cluster started" << endl;
    CHECK(restarter.waitClusterStarted(timeout) == 0);
    NdbSleep_MilliSleep(myRandom48(maxsleep));

    g_info << "1: restart done" << endl;
  }

  g_info << "1: stop dict ops" << endl;
  send_dict_ops_cmd(ctx, 3);

  return result;
}

int
runDictOps(NDBT_Context* ctx, NDBT_Step* step)
{
  myRandom48Init(NdbTick_CurrentMillisecond());
  int result = NDBT_OK;

  for (int l = 0; result == NDBT_OK; l++) {
    if (! recv_dict_ops_run(ctx))
      break;
    
    g_info << "2: === loop " << l << " ===" << endl;

    Ndb* pNdb = GETNDB(step);
    NdbDictionary::Dictionary* pDic = pNdb->getDictionary();
    const NdbDictionary::Table* pTab = ctx->getTab();
    //const char* tabName = pTab->getName(); //XXX what goes on?
    char tabName[40];
    strcpy(tabName, pTab->getName());

    const unsigned long maxsleep = 100; //ms

    g_info << "2: create table" << endl;
    {
      uint count = 0;
    try_create:
      count++;
      if (pDic->createTable(*pTab) != 0) {
        const NdbError err = pDic->getNdbError();
        if (count == 1)
          g_err << "2: " << tabName << ": create failed: " << err << endl;
        if (err.code != 711) {
          result = NDBT_FAILED;
          break;
        }
        NdbSleep_MilliSleep(myRandom48(maxsleep));
        goto try_create;
      }
    }
    NdbSleep_MilliSleep(myRandom48(maxsleep));

    g_info << "2: verify create" << endl;
    const NdbDictionary::Table* pTab2 = pDic->getTable(tabName);
    if (pTab2 == NULL) {
      const NdbError err = pDic->getNdbError();
      g_err << "2: " << tabName << ": verify create: " << err << endl;
      result = NDBT_FAILED;
      break;
    }
    NdbSleep_MilliSleep(myRandom48(maxsleep));

    // replace by the Retrieved table
    pTab = pTab2;

    // create indexes
    const char** indlist = NDBT_Tables::getIndexes(tabName);
    uint indnum = 0;
    while (indlist != 0 && *indlist != 0) {
      uint count = 0;
    try_create_index:
      count++;
      if (count == 1)
        g_info << "2: create index " << indnum << " " << *indlist << endl;
      NdbDictionary::Index ind;
      char indName[200];
      sprintf(indName, "%s_X%u", tabName, indnum);
      ind.setName(indName);
      ind.setTable(tabName);
      if (strcmp(*indlist, "UNIQUE") == 0) {
        ind.setType(NdbDictionary::Index::UniqueHashIndex);
        ind.setLogging(pTab->getLogging());
      } else if (strcmp(*indlist, "ORDERED") == 0) {
        ind.setType(NdbDictionary::Index::OrderedIndex);
        ind.setLogging(false);
      } else {
        assert(false);
      }
      const char** indtemp = indlist;
      while (*++indtemp != 0) {
        ind.addColumn(*indtemp);
      }
      if (pDic->createIndex(ind) != 0) {
        const NdbError err = pDic->getNdbError();
        if (count == 1)
          g_err << "2: " << indName << ": create failed: " << err << endl;
        if (err.code != 711) {
          result = NDBT_FAILED;
          break;
        }
        NdbSleep_MilliSleep(myRandom48(maxsleep));
        goto try_create_index;
      }
      indlist = ++indtemp;
      indnum++;
    }
    if (result == NDBT_FAILED)
      break;

    uint indcount = indnum;

    int records = myRandom48(ctx->getNumRecords());
    g_info << "2: load " << records << " records" << endl;
    HugoTransactions hugoTrans(*pTab);
    if (hugoTrans.loadTable(pNdb, records) != 0) {
      // XXX get error code from hugo
      g_err << "2: " << tabName << ": load failed" << endl;
      result = NDBT_FAILED;
      break;
    }
    NdbSleep_MilliSleep(myRandom48(maxsleep));

    // drop indexes
    indnum = 0;
    while (indnum < indcount) {
      uint count = 0;
    try_drop_index:
      count++;
      if (count == 1)
        g_info << "2: drop index " << indnum << endl;
      char indName[200];
      sprintf(indName, "%s_X%u", tabName, indnum);
      if (pDic->dropIndex(indName, tabName) != 0) {
        const NdbError err = pDic->getNdbError();
        if (count == 1)
          g_err << "2: " << indName << ": drop failed: " << err << endl;
        if (err.code != 711) {
          result = NDBT_FAILED;
          break;
        }
        NdbSleep_MilliSleep(myRandom48(maxsleep));
        goto try_drop_index;
      }
      indnum++;
    }
    if (result == NDBT_FAILED)
      break;

    g_info << "2: drop" << endl;
    {
      uint count = 0;
    try_drop:
      count++;
      if (pDic->dropTable(tabName) != 0) {
        const NdbError err = pDic->getNdbError();
        if (count == 1)
          g_err << "2: " << tabName << ": drop failed: " << err << endl;
        if (err.code != 711) {
          result = NDBT_FAILED;
          break;
        }
        NdbSleep_MilliSleep(myRandom48(maxsleep));
        goto try_drop;
      }
    }
    NdbSleep_MilliSleep(myRandom48(maxsleep));

    g_info << "2: verify drop" << endl;
    const NdbDictionary::Table* pTab3 = pDic->getTable(tabName);
    if (pTab3 != NULL) {
      g_err << "2: " << tabName << ": verify drop: table exists" << endl;
      result = NDBT_FAILED;
      break;
    }
    if (pDic->getNdbError().code != 709 &&
        pDic->getNdbError().code != 723) {
      const NdbError err = pDic->getNdbError();
      g_err << "2: " << tabName << ": verify drop: " << err << endl;
      result = NDBT_FAILED;
      break;
    }
    NdbSleep_MilliSleep(myRandom48(maxsleep));
  }

  return result;
}

int
runBug21755(NDBT_Context* ctx, NDBT_Step* step)
{
  char buf[256];
  NdbRestarter res;
  NdbDictionary::Table pTab0 = * ctx->getTab();
  NdbDictionary::Table pTab1 = pTab0;

  if (res.getNumDbNodes() < 2)
    return NDBT_OK;

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* pDic = pNdb->getDictionary();
  
  if (pDic->createTable(pTab0))
  {
    ndbout << pDic->getNdbError() << endl;
    return NDBT_FAILED;
  }

  NdbDictionary::Index idx0;
  BaseString::snprintf(buf, sizeof(buf), "%s-idx", pTab0.getName());  
  idx0.setName(buf);
  idx0.setType(NdbDictionary::Index::OrderedIndex);
  idx0.setTable(pTab0.getName());
  idx0.setStoredIndex(false);
  for (Uint32 i = 0; i<pTab0.getNoOfColumns(); i++)
  {
    const NdbDictionary::Column * col = pTab0.getColumn(i);
    if(col->getPrimaryKey()){
      idx0.addIndexColumn(col->getName());
    }
  }
  
  if (pDic->createIndex(idx0))
  {
    ndbout << pDic->getNdbError() << endl;
    return NDBT_FAILED;
  }
  
  BaseString::snprintf(buf, sizeof(buf), "%s-2", pTab1.getName());
  pTab1.setName(buf);

  if (pDic->createTable(pTab1))
  {
    ndbout << pDic->getNdbError() << endl;
    return NDBT_FAILED;
  }

  {
    HugoTransactions t0 (*pDic->getTable(pTab0.getName()));
    t0.loadTable(pNdb, 1000);
  }

  {
    HugoTransactions t1 (*pDic->getTable(pTab1.getName()));
    t1.loadTable(pNdb, 1000);
  }
  
  int node = res.getRandomNotMasterNodeId(rand());
  res.restartOneDbNode(node, false, true, true);
  
  if (pDic->dropTable(pTab1.getName()))
  {
    ndbout << pDic->getNdbError() << endl;
    return NDBT_FAILED;
  }

  BaseString::snprintf(buf, sizeof(buf), "%s-idx2", pTab0.getName());    
  idx0.setName(buf);
  if (pDic->createIndex(idx0))
  {
    ndbout << pDic->getNdbError() << endl;
    return NDBT_FAILED;
  }
  
  res.waitNodesNoStart(&node, 1);
  res.startNodes(&node, 1);
  
  if (res.waitClusterStarted())
  {
    return NDBT_FAILED;
  }
  
  if (pDic->dropTable(pTab0.getName()))
  {
    ndbout << pDic->getNdbError() << endl;
    return NDBT_FAILED;
  }
  
  return NDBT_OK;
}

static
int
create_tablespace(NdbDictionary::Dictionary* pDict, 
                  const char * lgname, 
                  const char * tsname, 
                  const char * dfname)
{
  NdbDictionary::Tablespace ts;
  ts.setName(tsname);
  ts.setExtentSize(1024*1024);
  ts.setDefaultLogfileGroup(lgname);
  
  if(pDict->createTablespace(ts) != 0)
  {
    g_err << "Failed to create tablespace:"
          << endl << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }
  
  NdbDictionary::Datafile df;
  df.setPath(dfname);
  df.setSize(1*1024*1024);
  df.setTablespace(tsname);
  
  if(pDict->createDatafile(df) != 0)
  {
    g_err << "Failed to create datafile:"
          << endl << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }
  return 0;
}

int
runBug24631(NDBT_Context* ctx, NDBT_Step* step)
{
  char tsname[256];
  char dfname[256];
  char lgname[256];
  char ufname[256];
  NdbRestarter res;

  if (res.getNumDbNodes() < 2)
    return NDBT_OK;

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* pDict = pNdb->getDictionary();
  
  NdbDictionary::Dictionary::List list;
  if (pDict->listObjects(list) == -1)
    return NDBT_FAILED;
  
  const char * lgfound = 0;
  
  for (Uint32 i = 0; i<list.count; i++)
  {
    switch(list.elements[i].type){
    case NdbDictionary::Object::LogfileGroup:
      lgfound = list.elements[i].name;
      break;
    default:
      break;
    }
    if (lgfound)
      break;
  }

  if (lgfound == 0)
  {
    BaseString::snprintf(lgname, sizeof(lgname), "LG-%u", rand());
    NdbDictionary::LogfileGroup lg;
    
    lg.setName(lgname);
    lg.setUndoBufferSize(8*1024*1024);
    if(pDict->createLogfileGroup(lg) != 0)
    {
      g_err << "Failed to create logfilegroup:"
	    << endl << pDict->getNdbError() << endl;
      return NDBT_FAILED;
    }

    NdbDictionary::Undofile uf;
    BaseString::snprintf(ufname, sizeof(ufname), "%s-%u", lgname, rand());
    uf.setPath(ufname);
    uf.setSize(2*1024*1024);
    uf.setLogfileGroup(lgname);
    
    if(pDict->createUndofile(uf) != 0)
    {
      g_err << "Failed to create undofile:"
            << endl << pDict->getNdbError() << endl;
      return NDBT_FAILED;
    }
  }
  else
  {
    BaseString::snprintf(lgname, sizeof(lgname), "%s", lgfound);
  }

  BaseString::snprintf(tsname, sizeof(tsname), "TS-%u", rand());
  BaseString::snprintf(dfname, sizeof(dfname), "%s-%u.dat", tsname, rand());

  if (create_tablespace(pDict, lgname, tsname, dfname))
    return NDBT_FAILED;

  
  int node = res.getRandomNotMasterNodeId(rand());
  res.restartOneDbNode(node, false, true, true);
  NdbSleep_SecSleep(3);

  if (pDict->dropDatafile(pDict->getDatafile(0, dfname)) != 0)
  {
    g_err << "Failed to drop datafile: " << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }

  if (pDict->dropTablespace(pDict->getTablespace(tsname)) != 0)
  {
    g_err << "Failed to drop tablespace: " << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }

  if (res.waitNodesNoStart(&node, 1))
    return NDBT_FAILED;
  
  res.startNodes(&node, 1);
  if (res.waitClusterStarted())
    return NDBT_FAILED;
  
  if (create_tablespace(pDict, lgname, tsname, dfname))
    return NDBT_FAILED;

  if (pDict->dropDatafile(pDict->getDatafile(0, dfname)) != 0)
  {
    g_err << "Failed to drop datafile: " << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }

  if (pDict->dropTablespace(pDict->getTablespace(tsname)) != 0)
  {
    g_err << "Failed to drop tablespace: " << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }
  
  if (lgfound == 0)
  {
    if (pDict->dropLogfileGroup(pDict->getLogfileGroup(lgname)) != 0)
      return NDBT_FAILED;
  }
  
  return NDBT_OK;
}

int
runBug29186(NDBT_Context* ctx, NDBT_Step* step)
{
  int lgError = 15000;
  int tsError = 16000;
  int res;
  char lgname[256];
  char ufname[256];
  char tsname[256];
  char dfname[256];

  NdbRestarter restarter;

  if (restarter.getNumDbNodes() < 2){
    ctx->stopTest();
    return NDBT_OK;
  }

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* pDict = pNdb->getDictionary();
  NdbDictionary::Dictionary::List list;

  if (pDict->listObjects(list) == -1)
    return NDBT_FAILED;

  // 1.create logfile group
  const char * lgfound = 0;

  for (Uint32 i = 0; i<list.count; i++)
  {
    switch(list.elements[i].type){
    case NdbDictionary::Object::LogfileGroup:
      lgfound = list.elements[i].name;
      break;
    default:
      break;
    }
    if (lgfound)
      break;
  }

  if (lgfound == 0)
  {
    BaseString::snprintf(lgname, sizeof(lgname), "LG-%u", rand());
    NdbDictionary::LogfileGroup lg;

    lg.setName(lgname);
    lg.setUndoBufferSize(8*1024*1024);
    if(pDict->createLogfileGroup(lg) != 0)
    {
      g_err << "Failed to create logfilegroup:"
            << endl << pDict->getNdbError() << endl;
      return NDBT_FAILED;
    }
  }
  else
  {
    BaseString::snprintf(lgname, sizeof(lgname), "%s", lgfound);
  }

  if(restarter.waitClusterStarted(60)){
    g_err << "waitClusterStarted failed"<< endl;
    return NDBT_FAILED;
  }
 
  if(restarter.insertErrorInAllNodes(lgError) != 0){
    g_err << "failed to set error insert"<< endl;
    return NDBT_FAILED;
  }

  g_info << "error inserted"  << endl;
  g_info << "waiting some before add log file"  << endl;
  g_info << "starting create log file group"  << endl;

  NdbDictionary::Undofile uf;
  BaseString::snprintf(ufname, sizeof(ufname), "%s-%u", lgname, rand());
  uf.setPath(ufname);
  uf.setSize(2*1024*1024);
  uf.setLogfileGroup(lgname);

  if(pDict->createUndofile(uf) == 0)
  {
    g_err << "Create log file group should fail on error_insertion " << lgError << endl;
    return NDBT_FAILED;
  }

  //clear lg error
  if(restarter.insertErrorInAllNodes(15099) != 0){
    g_err << "failed to set error insert"<< endl;
    return NDBT_FAILED;
  }
  NdbSleep_SecSleep(5);

  //lg error has been cleared, so we can add undo file
  if(pDict->createUndofile(uf) != 0)
  {
    g_err << "Failed to create undofile:"
          << endl << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }

  if(restarter.waitClusterStarted(60)){
    g_err << "waitClusterStarted failed"<< endl;
    return NDBT_FAILED;
  }

  if(restarter.insertErrorInAllNodes(tsError) != 0){
    g_err << "failed to set error insert"<< endl;
    return NDBT_FAILED;
  }
  g_info << "error inserted"  << endl;
  g_info << "waiting some before create table space"  << endl;
  g_info << "starting create table space"  << endl;

  //r = runCreateTablespace(ctx, step);
  BaseString::snprintf(tsname,  sizeof(tsname), "TS-%u", rand());
  BaseString::snprintf(dfname, sizeof(dfname), "%s-%u-1.dat", tsname, rand());

  NdbDictionary::Tablespace ts;
  ts.setName(tsname);
  ts.setExtentSize(1024*1024);
  ts.setDefaultLogfileGroup(lgname);

  if(pDict->createTablespace(ts) != 0)
  {
    g_err << "Failed to create tablespace:"
          << endl << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }

  NdbDictionary::Datafile df;
  df.setPath(dfname);
  df.setSize(1*1024*1024);
  df.setTablespace(tsname);

  if(pDict->createDatafile(df) == 0)
  {
    g_err << "Create table space should fail on error_insertion " << tsError << endl;
    return NDBT_FAILED;
  }
  //Clear the inserted error
  if(restarter.insertErrorInAllNodes(16099) != 0){
    g_err << "failed to set error insert"<< endl;
    return NDBT_FAILED;
  }
  NdbSleep_SecSleep(5);

  if (pDict->dropTablespace(pDict->getTablespace(tsname)) != 0)
  {
    g_err << "Failed to drop tablespace: " << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }

  if (lgfound == 0)
  {
    if (pDict->dropLogfileGroup(pDict->getLogfileGroup(lgname)) != 0)
      return NDBT_FAILED;
  }

  return NDBT_OK;
}

struct RandSchemaOp
{
  struct Obj 
  { 
    BaseString m_name;
    Uint32 m_type;
    struct Obj* m_parent;
    Vector<Obj*> m_dependant;
  };

  Vector<Obj*> m_objects;

  int schema_op(Ndb*);
  int validate(Ndb*);
  int cleanup(Ndb*);

  Obj* get_obj(Uint32 mask);
  int create_table(Ndb*);
  int create_index(Ndb*, Obj*);
  int drop_obj(Ndb*, Obj*);

  void remove_obj(Obj*);
};

template class Vector<RandSchemaOp::Obj*>;

int
RandSchemaOp::schema_op(Ndb* ndb)
{
  struct Obj* obj = 0;
  Uint32 type = 0;
loop:
  switch((rand() >> 16) & 3){
  case 0:
    return create_table(ndb);
  case 1:
    if ((obj = get_obj(1 << NdbDictionary::Object::UserTable)) == 0)
      goto loop;
    return create_index(ndb, obj);
  case 2:
    type = (1 << NdbDictionary::Object::UserTable);
    goto drop_object;
  case 3:
    type = 
      (1 << NdbDictionary::Object::UniqueHashIndex) |
      (1 << NdbDictionary::Object::OrderedIndex);    
    goto drop_object;
  default:
    goto loop;
  }

drop_object:
  if ((obj = get_obj(type)) == 0)
    goto loop;
  return drop_obj(ndb, obj);
}

RandSchemaOp::Obj*
RandSchemaOp::get_obj(Uint32 mask)
{
  Vector<Obj*> tmp;
  for (Uint32 i = 0; i<m_objects.size(); i++)
  {
    if ((1 << m_objects[i]->m_type) & mask)
      tmp.push_back(m_objects[i]);
  }

  if (tmp.size())
  {
    return tmp[rand()%tmp.size()];
  }
  return 0;
}

int
RandSchemaOp::create_table(Ndb* ndb)
{
  int numTables = NDBT_Tables::getNumTables();
  int num = myRandom48(numTables);
  NdbDictionary::Table pTab = * NDBT_Tables::getTable(num);
  
  NdbDictionary::Dictionary* pDict = ndb->getDictionary();

  if (pDict->getTable(pTab.getName()))
  {
    char buf[100];
    BaseString::snprintf(buf, sizeof(buf), "%s-%d", 
                         pTab.getName(), rand());
    pTab.setName(buf);
    if (pDict->createTable(pTab))
      return NDBT_FAILED;
  }
  else
  {
    if (NDBT_Tables::createTable(ndb, pTab.getName()))
    {
      return NDBT_FAILED;
    }
  }

  ndbout_c("create table %s",  pTab.getName());
  const NdbDictionary::Table* tab2 = pDict->getTable(pTab.getName());
  HugoTransactions trans(*tab2);
  trans.loadTable(ndb, 1000);

  Obj *obj = new Obj;
  obj->m_name.assign(pTab.getName());
  obj->m_type = NdbDictionary::Object::UserTable;
  obj->m_parent = 0;
  m_objects.push_back(obj);
  
  return NDBT_OK;
}

int
RandSchemaOp::create_index(Ndb* ndb, Obj* tab)
{
  NdbDictionary::Dictionary* pDict = ndb->getDictionary();
  const NdbDictionary::Table * pTab = pDict->getTable(tab->m_name.c_str());

  if (pTab == 0)
  {
    return NDBT_FAILED;
  }

  bool ordered = (rand() >> 16) & 1;
  bool stored = (rand() >> 16) & 1;

  Uint32 type = ordered ? 
    NdbDictionary::Index::OrderedIndex :
    NdbDictionary::Index::UniqueHashIndex;
  
  char buf[255];
  BaseString::snprintf(buf, sizeof(buf), "%s-%s", 
                       pTab->getName(),
                       ordered ? "OI" : "UI");
  
  if (pDict->getIndex(buf, pTab->getName()))
  {
    // Index exists...let it be ok
    return NDBT_OK;
  }
  
  ndbout_c("create index %s", buf);
  NdbDictionary::Index idx0;
  idx0.setName(buf);
  idx0.setType((NdbDictionary::Index::Type)type);
  idx0.setTable(pTab->getName());
  idx0.setStoredIndex(ordered ? false : stored);

  for (Uint32 i = 0; i<pTab->getNoOfColumns(); i++)
  {
    if (pTab->getColumn(i)->getPrimaryKey())
      idx0.addColumn(pTab->getColumn(i)->getName());
  }
  if (pDict->createIndex(idx0))
  {
    ndbout << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }
  Obj *obj = new Obj;
  obj->m_name.assign(buf);
  obj->m_type = type;
  obj->m_parent = tab;
  m_objects.push_back(obj);
  
  tab->m_dependant.push_back(obj);
  return NDBT_OK;
}

int
RandSchemaOp::drop_obj(Ndb* ndb, Obj* obj)
{
  NdbDictionary::Dictionary* pDict = ndb->getDictionary();
  
  if (obj->m_type == NdbDictionary::Object::UserTable)
  {
    ndbout_c("drop table %s", obj->m_name.c_str());
    /**
     * Drop of table automatically drops all indexes
     */
    if (pDict->dropTable(obj->m_name.c_str()))
    {
      return NDBT_FAILED;
    }
    while(obj->m_dependant.size())
    {
      remove_obj(obj->m_dependant[0]);
    }
    remove_obj(obj);
  }
  else if (obj->m_type == NdbDictionary::Object::UniqueHashIndex ||
           obj->m_type == NdbDictionary::Object::OrderedIndex)
  {
    ndbout_c("drop index %s", obj->m_name.c_str());
    if (pDict->dropIndex(obj->m_name.c_str(),
                         obj->m_parent->m_name.c_str()))
    {
      return NDBT_FAILED;
    }
    remove_obj(obj);
  }
  return NDBT_OK;
}

void
RandSchemaOp::remove_obj(Obj* obj)
{
  Uint32 i;
  if (obj->m_parent)
  {
    bool found = false;
    for (i = 0; i<obj->m_parent->m_dependant.size(); i++)
    {
      if (obj->m_parent->m_dependant[i] == obj)
      {
        found = true;
        obj->m_parent->m_dependant.erase(i);
        break;
      }
    }
    assert(found);
  }

  {
    bool found = false;
    for (i = 0; i<m_objects.size(); i++)
    {
      if (m_objects[i] == obj)
      {
        found = true;
        m_objects.erase(i);
        break;
      }
    }
    assert(found);
  }
  delete obj;
}

int
RandSchemaOp::validate(Ndb* ndb)
{
  NdbDictionary::Dictionary* pDict = ndb->getDictionary();
  for (Uint32 i = 0; i<m_objects.size(); i++)
  {
    if (m_objects[i]->m_type == NdbDictionary::Object::UserTable)
    {
      const NdbDictionary::Table* tab2 = 
        pDict->getTable(m_objects[i]->m_name.c_str());
      HugoTransactions trans(*tab2);
      trans.scanUpdateRecords(ndb, 1000);
      trans.clearTable(ndb);
      trans.loadTable(ndb, 1000);
    }
  }
  
  return NDBT_OK;
}

/*
      SystemTable = 1,        ///< System table
      UserTable = 2,          ///< User table (may be temporary)
      UniqueHashIndex = 3,    ///< Unique un-ordered hash index
      OrderedIndex = 6,       ///< Non-unique ordered index
      HashIndexTrigger = 7,   ///< Index maintenance, internal
      IndexTrigger = 8,       ///< Index maintenance, internal
      SubscriptionTrigger = 9,///< Backup or replication, internal
      ReadOnlyConstraint = 10,///< Trigger, internal
      Tablespace = 20,        ///< Tablespace
      LogfileGroup = 21,      ///< Logfile group
      Datafile = 22,          ///< Datafile
      Undofile = 23           ///< Undofile
*/

int
RandSchemaOp::cleanup(Ndb* ndb)
{
  Int32 i;
  for (i = m_objects.size() - 1; i >= 0; i--)
  {
    switch(m_objects[i]->m_type){
    case NdbDictionary::Object::UniqueHashIndex:
    case NdbDictionary::Object::OrderedIndex:        
      if (drop_obj(ndb, m_objects[i]))
        return NDBT_FAILED;
      
      break;
    default:
      break;
    }
  }

  for (i = m_objects.size() - 1; i >= 0; i--)
  {
    switch(m_objects[i]->m_type){
    case NdbDictionary::Object::UserTable:
      if (drop_obj(ndb, m_objects[i]))
        return NDBT_FAILED;
      break;
    default:
      break;
    }
  }
  
  assert(m_objects.size() == 0);
  return NDBT_OK;
}

int
runDictRestart(NDBT_Context* ctx, NDBT_Step* step)
{
  Ndb* pNdb = GETNDB(step);
  int loops = ctx->getNumLoops();

  NdbMixRestarter res;
  
  RandSchemaOp dict;
  if (res.getNumDbNodes() < 2)
    return NDBT_OK;

  if (res.init(ctx, step))
    return NDBT_FAILED;
  
  for (Uint32 i = 0; i<loops; i++)
  {
    for (Uint32 j = 0; j<10; j++)
      if (dict.schema_op(pNdb))
        return NDBT_FAILED;
    
    if (res.dostep(ctx, step))
      return NDBT_FAILED;

    if (dict.validate(pNdb))
      return NDBT_FAILED;
  }

  if (res.finish(ctx, step))
    return NDBT_FAILED;

  if (dict.validate(pNdb))
    return NDBT_FAILED;
  
  if (dict.cleanup(pNdb))
    return NDBT_FAILED;
  
  return NDBT_OK;
}

int
runBug29501(NDBT_Context* ctx, NDBT_Step* step) {
  NdbRestarter res;
  NdbDictionary::LogfileGroup lg;
  lg.setName("DEFAULT-LG");
  lg.setUndoBufferSize(8*1024*1024);

  if (res.getNumDbNodes() < 2)
    return NDBT_OK;

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* pDict = pNdb->getDictionary();

  int node = res.getRandomNotMasterNodeId(rand());
  res.restartOneDbNode(node, true, true, false);

  if(pDict->createLogfileGroup(lg) != 0){
    g_err << "Failed to create logfilegroup:"
        << endl << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }

  NdbDictionary::Undofile uf;
  uf.setPath("undofile01.dat");
  uf.setSize(5*1024*1024);
  uf.setLogfileGroup("DEFAULT-LG");

  if(pDict->createUndofile(uf) != 0){
    g_err << "Failed to create undofile:"
        << endl << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }

  res.waitNodesNoStart(&node, 1);
  res.startNodes(&node, 1);

  if (res.waitClusterStarted()){
  	g_err << "Node restart failed"
  	<< endl << pDict->getNdbError() << endl;
      return NDBT_FAILED;
  }

  if (pDict->dropLogfileGroup(pDict->getLogfileGroup(lg.getName())) != 0){
  	g_err << "Drop of LFG Failed"
  	<< endl << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }

  return NDBT_OK;
}

int
runDropDDObjects(NDBT_Context* ctx, NDBT_Step* step){
  //Purpose is to drop all tables, data files, Table spaces and LFG's
  Uint32 i = 0;

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* pDict = pNdb->getDictionary();
  
  NdbDictionary::Dictionary::List list;
  if (pDict->listObjects(list) == -1)
    return NDBT_FAILED;
  
  //Search the list and drop all tables found
  const char * tableFound = 0;
  for (i = 0; i < list.count; i++){
    switch(list.elements[i].type){
      case NdbDictionary::Object::UserTable:
        tableFound = list.elements[i].name;
        if(tableFound != 0){
          if(strcmp(tableFound, "ndb_apply_status") != 0 && 
             strcmp(tableFound, "NDB$BLOB_2_3") != 0 &&
             strcmp(tableFound, "ndb_schema") != 0){
      	    if(pDict->dropTable(tableFound) != 0){
              g_err << "Failed to drop table: " << tableFound << pDict->getNdbError() << endl;
              return NDBT_FAILED;
            }
          }
        }
        tableFound = 0;
        break;
      default:
        break;
    }
  }
 
  //Search the list and drop all data file found
  const char * dfFound = 0;
  for (i = 0; i < list.count; i++){
    switch(list.elements[i].type){
      case NdbDictionary::Object::Datafile:
        dfFound = list.elements[i].name;
        if(dfFound != 0){
      	  if(pDict->dropDatafile(pDict->getDatafile(0, dfFound)) != 0){
            g_err << "Failed to drop datafile: " << pDict->getNdbError() << endl;
            return NDBT_FAILED;
          }
        }
        dfFound = 0;
        break;
      default:
        break;
    }
  }

  //Search the list and drop all Table Spaces Found 
  const char * tsFound  = 0;
  for (i = 0; i <list.count; i++){
    switch(list.elements[i].type){
      case NdbDictionary::Object::Tablespace:
        tsFound = list.elements[i].name;
        if(tsFound != 0){
          if(pDict->dropTablespace(pDict->getTablespace(tsFound)) != 0){
            g_err << "Failed to drop tablespace: " << pDict->getNdbError() << endl;
            return NDBT_FAILED;
          }
        }
        tsFound = 0;
        break;
      default:
        break;
    }
  }

  //Search the list and drop all LFG Found
  //Currently only 1 LGF is supported, but written for future 
  //when more then one is supported. 
  const char * lgFound  = 0;
  for (i = 0; i < list.count; i++){
    switch(list.elements[i].type){
      case NdbDictionary::Object::LogfileGroup:
        lgFound = list.elements[i].name;
        if(lgFound != 0){
          if (pDict->dropLogfileGroup(pDict->getLogfileGroup(lgFound)) != 0){
            g_err << "Failed to drop tablespace: " << pDict->getNdbError() << endl;
            return NDBT_FAILED;
          }
       }   
        lgFound = 0;
        break;
      default:
        break;
    }
  }

  return NDBT_OK;
}

int
runWaitStarted(NDBT_Context* ctx, NDBT_Step* step){

  NdbRestarter restarter;
  restarter.waitClusterStarted(300);

  NdbSleep_SecSleep(3);
  return NDBT_OK;
}

int
testDropDDObjectsSetup(NDBT_Context* ctx, NDBT_Step* step){
  //Purpose is to setup to test DropDDObjects
  char tsname[256];
  char dfname[256];

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* pDict = pNdb->getDictionary();

  NdbDictionary::LogfileGroup lg;
  lg.setName("DEFAULT-LG");
  lg.setUndoBufferSize(8*1024*1024);


  if(pDict->createLogfileGroup(lg) != 0){
    g_err << "Failed to create logfilegroup:"
        << endl << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }

  NdbDictionary::Undofile uf;
  uf.setPath("undofile01.dat");
  uf.setSize(5*1024*1024);
  uf.setLogfileGroup("DEFAULT-LG");

  if(pDict->createUndofile(uf) != 0){
    g_err << "Failed to create undofile:"
        << endl << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }
  
  BaseString::snprintf(tsname, sizeof(tsname), "TS-%u", rand());
  BaseString::snprintf(dfname, sizeof(dfname), "%s-%u.dat", tsname, rand());

  if (create_tablespace(pDict, lg.getName(), tsname, dfname)){
  	g_err << "Failed to create undofile:"
        << endl << pDict->getNdbError() << endl;
    return NDBT_FAILED;
  }
  
  return NDBT_OK;
}

int
DropDDObjectsVerify(NDBT_Context* ctx, NDBT_Step* step){
  //Purpose is to verify test DropDDObjects worked
  Uint32 i = 0;

  Ndb* pNdb = GETNDB(step);
  NdbDictionary::Dictionary* pDict = pNdb->getDictionary();

  NdbDictionary::Dictionary::List list;
  if (pDict->listObjects(list) == -1)
    return NDBT_FAILED;

    bool ddFound  = false;
  for (i = 0; i <list.count; i++){
    switch(list.elements[i].type){
      case NdbDictionary::Object::Tablespace:
        ddFound = true;
        break;
      case NdbDictionary::Object::LogfileGroup:
        ddFound = true;
        break;
      default:
        break;
    }
    if(ddFound == true){
      g_err << "DropDDObjects Failed: DD found:"
        << endl;
      return NDBT_FAILED;
    }
  }
  return NDBT_OK;
}

// begin schema trans

#undef chk1
#undef chk2

static bool st_core_on_err = false;

#define chk1(x) \
  do { \
    if (x) break; \
    g_err << "FAIL " << __LINE__ << " " << #x << endl; \
    if (st_core_on_err) abort(); \
    goto err; \
  } while (0)

#define chk2(x, e) \
  do { \
    if (x) break; \
    g_err << "FAIL " << __LINE__ << " " << #x << ": " << e << endl; \
    if (st_core_on_err) abort(); \
    goto err; \
  } while (0)

static uint
urandom(uint m)
{
  assert(m != 0);
  uint n = (uint)random();
  return n % m;
}

static bool
randomly(uint k, uint m)
{
  uint n = urandom(m);
  return n < k;
}

// structs

class ST_Obj;
template class Vector<ST_Obj*>;
typedef Vector<ST_Obj*> ST_Objlist;

static ST_Objlist st_objlist;
static const ST_Obj* st_find_obj(const char* db, const char* name);

#define ST_MAX_NAME_SIZE  (MAX_TAB_NAME_SIZE + 100)

struct ST_Obj {
  NdbDictionary::Object::Type type;
  char dbname[ST_MAX_NAME_SIZE];
  char name[ST_MAX_NAME_SIZE];
  int id;
  bool create; // true/false = create/drop prepared or committed
  bool commit;
  bool exists() const { // visible to trans
    return !(!create && commit);
  }
  virtual bool is_trigger() const {
    return false;
  }
  virtual bool is_index() const {
    return false;
  }
  virtual bool is_table() const {
    return false;
  }
  virtual const char* realname() const {
    return name;
  }
  ST_Obj(const char* a_dbname, const char* a_name) {
    type = NdbDictionary::Object::TypeUndefined;
    strcpy(dbname, a_dbname);
    strcpy(name, a_name);
    id = -1;
    create = false; // init as dropped
    commit = true;
    assert(st_find_obj(dbname, name) == 0);
    st_objlist.push_back(this);
  }
  virtual ~ST_Obj() {}
};

static NdbOut&
operator<<(NdbOut& out, const ST_Obj& obj)
{
  out << obj.name << "[" << obj.id << "]";
  return out;
}

struct ST_Trg : public ST_Obj {
  struct ST_Ind* ind;
  TriggerEvent::Value event;
  mutable char realname_buf[ST_MAX_NAME_SIZE];
  virtual bool is_trigger() const {
    return true;
  }
  virtual const char* realname() const;
  ST_Trg(const char* a_db, const char* a_name) :
    ST_Obj(a_db, a_name) {
    ind = 0;
  }
  virtual ~ST_Trg() {};
};

template class Vector<ST_Trg*>;
typedef Vector<ST_Trg*> ST_Trglist;

struct ST_Ind : public ST_Obj {
  struct ST_Tab* tab;
  const NdbDictionary::Index* ind;
  const NdbDictionary::Index* ind_r; // retrieved
  BaseString colnames;
  ST_Trglist* trglist;
  int trgcount;
  virtual bool is_index() const {
    return true;
  }
  bool is_unique() const {
    return type == NdbDictionary::Object::UniqueHashIndex;
  }
  const ST_Trg& trg(int k) const {
    return *((*trglist)[k]);
  }
  ST_Trg& trg(int k) {
    return *((*trglist)[k]);
  }
  ST_Ind(const char* a_db, const char* a_name) :
    ST_Obj(a_db, a_name) {
    tab = 0;
    ind = 0;
    ind_r = 0;
    trglist = new ST_Trglist;
    trgcount = 0;
  };
  virtual ~ST_Ind() {
    delete ind;
    delete trglist;
    ind = 0;
    trglist = 0;
  }
};

const char*
ST_Trg::realname() const
{
  if (!exists())
    return name;
  const char* p = name;
  const char* q = strchr(p, '<');
  const char* r = strchr(p, '>');
  assert(q != 0 && r != 0 && q < r);
  assert(ind->id != -1);
  sprintf(realname_buf, "%.*s%d%s", q - p, p, ind->id, r + 1);
  return realname_buf;
}

template class Vector<ST_Ind*>;
typedef Vector<ST_Ind*> ST_Indlist;

struct ST_Tab : public ST_Obj {
  const NdbDictionary::Table* tab;
  const NdbDictionary::Table* tab_r; // retrieved
  ST_Indlist* indlist;
  int indcount;
  int induniquecount;
  int indorderedcount;
  virtual bool is_table() const {
    return true;
  }
  const ST_Ind& ind(int j) const {
    return *((*indlist)[j]);
  }
  ST_Ind& ind(int j) {
    return *((*indlist)[j]);
  }
  ST_Tab(const char* a_db, const char* a_name) :
    ST_Obj(a_db, a_name) {
    tab = 0;
    tab_r = 0;
    indlist = new ST_Indlist;
    indcount = 0;
    induniquecount = 0;
    indorderedcount = 0;
  }
  virtual ~ST_Tab() {
    delete tab;
    delete indlist;
    tab = 0;
    indlist = 0;
  }
};

template class Vector<ST_Tab*>;
typedef Vector<ST_Tab*> ST_Tablist;

struct ST_Restarter : public NdbRestarter {
  int get_status();
  const ndb_mgm_node_state& get_state(int node_id);
  ST_Restarter() {
    int i;
    for (i = 0; i < MAX_NODES; i++)
      state[i].node_type = NDB_MGM_NODE_TYPE_UNKNOWN;
    first_time = true;
  }
protected:
  void set_state(const ndb_mgm_node_state& state);
  ndb_mgm_node_state state[MAX_NODES];
  bool first_time;
};

const ndb_mgm_node_state&
ST_Restarter::get_state(int node_id) {
  assert(node_id > 0 && node_id < MAX_NODES);
  assert(!first_time);
  return state[node_id];
}

void
ST_Restarter::set_state(const ndb_mgm_node_state& new_state)
{
  int node_id = new_state.node_id;
  assert(1 <= node_id && node_id < MAX_NODES);

  assert(new_state.node_type == NDB_MGM_NODE_TYPE_MGM ||
         new_state.node_type == NDB_MGM_NODE_TYPE_NDB ||
         new_state.node_type == NDB_MGM_NODE_TYPE_API);

  ndb_mgm_node_state& old_state = state[node_id];
  if (!first_time)
    assert(old_state.node_type == new_state.node_type);
  old_state = new_state;
}

int
ST_Restarter::get_status()
{
  if (getStatus() == -1)
    return -1;
  int i;
  for (i = 0; i < (int)mgmNodes.size(); i++)
    set_state(mgmNodes[i]);
  for (i = 0; i < (int)ndbNodes.size(); i++)
    set_state(ndbNodes[i]);
  for (i = 0; i < (int)apiNodes.size(); i++)
    set_state(apiNodes[i]);
  first_time = false;
  return 0;
}

struct ST_Con {
  Ndb_cluster_connection* ncc;
  Ndb* ndb;
  NdbDictionary::Dictionary* dic;
  ST_Restarter* restarter;
  int numdbnodes;
  char dbname[ST_MAX_NAME_SIZE];
  ST_Tablist* tablist;
  int tabcount;
  bool tx_on;
  bool tx_commit;
  bool is_xcon;
  ST_Con* xcon;
  int node_id;
  int loop;
  const ST_Tab& tab(int i) const {
    return *((*tablist)[i]);
  }
  ST_Tab& tab(int i) {
    return *((*tablist)[i]);
  }
  ST_Con(Ndb_cluster_connection* a_ncc,
         Ndb* a_ndb,
         ST_Restarter* a_restarter) {
    ncc = a_ncc;
    ndb = a_ndb;
    dic = a_ndb->getDictionary();
    restarter = a_restarter;
    numdbnodes = restarter->getNumDbNodes();
    assert(numdbnodes >= 1);
    sprintf(dbname, "%s", ndb->getDatabaseName());
    tablist = new ST_Tablist;
    tabcount = 0;
    tx_on = false;
    tx_commit = false;
    is_xcon = false;
    xcon = 0;
    node_id = ncc->node_id();
    {
      assert(restarter->get_status() == 0);
      const ndb_mgm_node_state& state = restarter->get_state(node_id);
      assert(state.node_type == NDB_MGM_NODE_TYPE_API);
      assert(state.version != 0); // means "connected"
      g_info << "node_id:" << node_id << endl;
    }
    loop = -1;
  }
  ~ST_Con() {
    if (!is_xcon) {
      delete tablist;
    } else {
      delete ndb;
      delete ncc;
    }
    tablist = 0;
    ndb = 0;
    ncc = 0;
  }
};

// initialization

static int
st_drop_all_tables(ST_Con& c)
{
  g_info << "st_drop_all_tables" << endl;
  NdbDictionary::Dictionary::List list;
  chk2(c.dic->listObjects(list) == 0, c.dic->getNdbError());
  int n;
  for (n = 0; n < (int)list.count; n++) {
    const NdbDictionary::Dictionary::List::Element& element =
      list.elements[n];
    if (element.type == NdbDictionary::Object::UserTable &&
        strcmp(element.database, "TEST_DB") == 0) {
      chk2(c.dic->dropTable(element.name) == 0, c.dic->getNdbError());
    }
  }
  return 0;
err:
  return -1;
}

static void
st_init_objects(ST_Con& c, NDBT_Context* ctx)
{
  int numTables = ctx->getNumTables();
  c.tabcount = 0;
  int i;
  for (i = 0; i < numTables; i++) {
    const NdbDictionary::Table* pTab = 0;
#if ndb_test_ALL_TABLES_is_fixed
    const NdbDictionary::Table** tables = ctx->getTables();
    pTab = tables[i];
#else
    const Vector<BaseString>& tables = ctx->getSuite()->m_tables_in_test;
    pTab = NDBT_Tables::getTable(tables[i].c_str());
#endif
    assert(pTab != 0 && pTab->getName() != 0);

    {
      bool ok = true;
      int n;
      for (n = 0; n < pTab->getNoOfColumns(); n++) {
        const NdbDictionary::Column* pCol = pTab->getColumn(n);
        assert(pCol != 0);
        if (pCol->getStorageType() !=
            NdbDictionary::Column::StorageTypeMemory) {
          g_err << pTab->getName() << ": skip non-mem table for now" << endl;
          ok = false;
          break;
        }
      }
      if (!ok)
        continue;
    }

    c.tablist->push_back(new ST_Tab(c.dbname, pTab->getName()));
    c.tabcount++;
    ST_Tab& tab = *c.tablist->back();
    tab.type = NdbDictionary::Object::UserTable;
    tab.tab = new NdbDictionary::Table(*pTab);

    const char** indspec = NDBT_Tables::getIndexes(tab.name);

    while (indspec != 0 && *indspec != 0) {
      char ind_name[ST_MAX_NAME_SIZE];
      sprintf(ind_name, "%sX%d", tab.name, tab.indcount);
      tab.indlist->push_back(new ST_Ind("sys", ind_name));
      ST_Ind& ind = *tab.indlist->back();
      ind.tab = &tab;

      NdbDictionary::Index* pInd = new NdbDictionary::Index(ind.name);
      pInd->setTable(tab.name);
      pInd->setLogging(false);

      const char* type = *indspec++;
      if (strcmp(type, "UNIQUE") == 0) {
        ind.type = NdbDictionary::Object::UniqueHashIndex;
        pInd->setType((NdbDictionary::Index::Type)ind.type);
        tab.induniquecount++;

        { char trg_name[ST_MAX_NAME_SIZE];
          sprintf(trg_name, "NDB$INDEX_<%s>_UI", ind.name);
          ind.trglist->push_back(new ST_Trg("", trg_name));
          ST_Trg& trg = *ind.trglist->back();
          trg.ind = &ind;
          trg.type = NdbDictionary::Object::HashIndexTrigger;
          trg.event = TriggerEvent::TE_INSERT;
        }
        ind.trgcount = 1;
      }
      else if (strcmp(type, "ORDERED") == 0) {
        ind.type = NdbDictionary::Object::OrderedIndex;
        pInd->setType((NdbDictionary::Index::Type)ind.type);
        tab.indorderedcount++;

        { char trg_name[ST_MAX_NAME_SIZE];
          sprintf(trg_name, "NDB$INDEX_<%s>_CUSTOM", ind.name);
          ind.trglist->push_back(new ST_Trg("", trg_name));
          ST_Trg& trg = *ind.trglist->back();
          trg.ind = &ind;
          trg.type = NdbDictionary::Object::IndexTrigger;
          trg.event = TriggerEvent::TE_CUSTOM;
        }
        ind.trgcount = 1;
      }
      else
        assert(false);

      const char* sep = "";
      const char* colname;
      while ((colname = *indspec++) != 0) {
        const NdbDictionary::Column* col = tab.tab->getColumn(colname);
        assert(col != 0);
        pInd->addColumn(*col);

        ind.colnames.appfmt("%s%s", sep, colname);
        sep = ",";
      }

      ind.ind = pInd;
      tab.indcount++;
    }
  }
}

// node states

static int
st_report_db_nodes(ST_Con& c, NdbOut& out)
{
  chk1(c.restarter->get_status() == 0);
  char r1[100]; // up
  char r2[100]; // down
  char r3[100]; // unknown
  r1[0] =r2[0] = r3[0] = 0;
  int i;
  for (i = 1; i < MAX_NODES; i++) {
    const ndb_mgm_node_state& state = c.restarter->get_state(i);
    if (state.node_type == NDB_MGM_NODE_TYPE_NDB) {
      char* r = 0;
      if (state.node_status == NDB_MGM_NODE_STATUS_STARTED)
        r = r1;
      else if (state.node_status == NDB_MGM_NODE_STATUS_NO_CONTACT)
        r = r2;
      else
        r = r3;
      sprintf(r + strlen(r), "%s%d", r[0] == 0 ? "" : ",", i);
    }
  }
  if (r2[0] != 0 || r3[0] != 0) {
    out << "nodes up:" << r1 << " down:" << r2 << " unknown:" << r3 << endl;
    goto err;
  }
  out << "nodes up:" << r1 << " (all)" << endl;
  return 0;
err:
  return -1;
}

static int
st_check_db_nodes(ST_Con& c, int ignore_node_id = -1)
{
  chk1(c.restarter->get_status() == 0);
  int i;
  for (i = 1; i < MAX_NODES; i++) {
    const ndb_mgm_node_state& state = c.restarter->get_state(i);
    if (state.node_type == NDB_MGM_NODE_TYPE_NDB &&
        i != ignore_node_id) {
      chk2(state.node_status == NDB_MGM_NODE_STATUS_STARTED, " node:" << i);
    }
  }
  return 0;
err:
  return -1;
}

static int
st_wait_db_node_up(ST_Con& c, int node_id)
{
  int count = 0;
  int max_count = 30;
  int milli_sleep = 2000;
  while (count++ < max_count) {
    // get status and check that other db nodes have not crashed
    chk1(st_check_db_nodes(c, node_id) == 0);

    const ndb_mgm_node_state& state = c.restarter->get_state(node_id);
    assert(state.node_type == NDB_MGM_NODE_TYPE_NDB);
    if (state.node_status == NDB_MGM_NODE_STATUS_STARTED)
      break;
    g_info << "waiting count:" << count << "/" << max_count << endl;
    NdbSleep_MilliSleep(milli_sleep);
  }
  return 0;
err:
  return -1;
}

// extra connection (separate API node)

static int
st_start_xcon(ST_Con& c)
{
  assert(c.xcon == 0);
  g_info << "start extra connection" << endl;

  do {
    int ret;
    Ndb_cluster_connection* xncc = new Ndb_cluster_connection;
    chk2((ret = xncc->connect(30, 1, 0)) == 0, "ret:" << ret);
    chk2((ret = xncc->wait_until_ready(30, 10)) == 0, "ret:" << ret);
    Ndb* xndb = new Ndb(xncc, c.dbname);
    chk1(xndb->init() == 0);
    chk1(xndb->waitUntilReady(30) == 0);
    // share restarter
    c.xcon = new ST_Con(xncc, xndb, c.restarter);
    // share objects
    c.xcon->tablist = c.tablist;
    c.xcon->tabcount = c.tabcount;
    c.xcon->is_xcon = true;
  } while (0);
  return 0;
err:
  return -1;
}

static int
st_stop_xcon(ST_Con& c)
{
  assert(c.xcon != 0);
  int node_id = c.xcon->node_id;
  g_info << "stop extra connection node_id:" << node_id << endl;

  c.xcon->restarter = 0;
  c.xcon->tablist = 0;
  c.xcon->tabcount = 0;
  delete c.xcon;
  c.xcon = 0;
  int count = 0;
  while (1) {
    chk1(c.restarter->get_status() == 0);
    const ndb_mgm_node_state& state = c.restarter->get_state(node_id);
    assert(state.node_type == NDB_MGM_NODE_TYPE_API);
    if (state.version == 0) // means "disconnected"
      break;
    g_info << "waiting count:" << ++count << endl;
    NdbSleep_MilliSleep(10 * count);
  }
  return 0;
err:
  return -1;
}

// error insert

struct ST_Errins {
  int value;              // error value to insert
  int code;               // ndb error code to expect
  int master;             // insert on master / non-master (-1 = random)
  int node;               // insert on node id
  const ST_Errins* list;  // include another list
  bool ends;              // end list
  ST_Errins() :
    value(0), code(0), master(-1), node(0), list(0), ends(true)
  {}
  ST_Errins(const ST_Errins* l) :
    value(0), code(0), master(-1), node(0), list(l), ends(false)
  {}
  ST_Errins(int v, int c, int m = -1) :
    value(v), code(c), master(m), node(0), list(0), ends(false)
  {}
};

static NdbOut&
operator<<(NdbOut& out, const ST_Errins& errins)
{
  out << "value:" << errins.value;
  out << " code:" << errins.code;
  out << " master:" << errins.master;
  out << " node:" << errins.node;
  return out;
}

static ST_Errins
st_get_errins(ST_Con& c, const ST_Errins* list)
{
  uint size = 0;
  while (!list[size++].ends)
    ;
  assert(size > 1);
  uint n = urandom(size - 1);
  const ST_Errins& errins = list[n];
  if (errins.list == 0) {
    assert(errins.value != 0);
    return errins;
  }
  return st_get_errins(c, errins.list);
}

static int
st_do_errins(ST_Con& c, ST_Errins& errins)
{
  assert(errins.value != 0);
  if (c.numdbnodes < 2)
    errins.master = 1;
  else if (errins.master == -1)
    errins.master = randomly(1, 2);
  if (errins.master) {
    errins.node = c.restarter->getMasterNodeId();
  } else {
    uint rand = urandom(c.numdbnodes);
    errins.node = c.restarter->getRandomNotMasterNodeId(rand);
  }
  g_info << "errins: " << errins << endl;
  chk2(c.restarter->insertErrorInNode(errins.node, errins.value) == 0, errins);
  return 0;
err:
  return -1;
}

// debug aid

static const ST_Obj*
st_find_obj(const char* dbname, const char* name)
{
  const ST_Obj* ret_objp = 0;
  int i;
  for (i = 0; i < (int)st_objlist.size(); i++) {
    const ST_Obj* objp = st_objlist[i];
    if (strcmp(objp->dbname, dbname) == 0 &&
        strcmp(objp->name, name) == 0) {
      assert(ret_objp == 0);
      ret_objp = objp;
    }
  }
  return ret_objp;
}

static void
st_print_obj(const char* dbname, const char* name, int line = 0)
{
  const ST_Obj* objp = st_find_obj(dbname, name);
  g_info << name << ": by name:";
  if (objp != 0)
    g_info << " create:" << objp->create
           << " commit:" << objp->commit
           << " exists:" << objp->exists();
  else
    g_info << " not found";
  if (line != 0)
    g_info << " line:" << line;
  g_info << endl;
}

// set object state

static void
st_set_commit_obj(ST_Con& c, ST_Obj& obj)
{
  bool create_old = obj.create;
  bool commit_old = obj.commit;
  if (!c.tx_commit && !obj.commit)
    obj.create = !obj.create;
  obj.commit = true;
  if (create_old != obj.create || commit_old != obj.commit) {
    g_info << obj.name << ": set commit:"
           << " create:" << create_old << "->" << obj.create
           << " commit:" << commit_old << "->" << obj.commit << endl;
  }
}

static void
st_set_commit_trg(ST_Con& c, ST_Trg& trg)
{
  st_set_commit_obj(c, trg);
}

static void
st_set_commit_ind(ST_Con& c, ST_Ind& ind)
{
  st_set_commit_obj(c, ind);
  int k;
  for (k = 0; k < ind.trgcount; k++) {
    ST_Trg& trg = ind.trg(k);
    st_set_commit_obj(c, trg);
  }
}

static void
st_set_commit_tab(ST_Con& c, ST_Tab& tab)
{
  st_set_commit_obj(c, tab);
  int j;
  for (j = 0; j < tab.indcount; j++) {
    ST_Ind& ind = tab.ind(j);
    st_set_commit_ind(c, ind);
  }
}

static void
st_set_commit_all(ST_Con& c)
{
  int i;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    st_set_commit_tab(c, tab);
  }
}

static void
st_set_create_obj(ST_Con& c, ST_Obj& obj, bool create)
{
  bool create_old = obj.create;
  bool commit_old = obj.commit;
  obj.create = create;
  obj.commit = !c.tx_on;
  if (create_old != obj.create || commit_old != obj.commit) {
    g_info << obj.name << ": set create:"
           << " create:" << create_old << "->" << obj.create
           << " commit:" << commit_old << "->" << obj.commit << endl;
  }
}

static void
st_set_create_trg(ST_Con& c, ST_Trg& trg, bool create)
{
  st_set_create_obj(c, trg, create);
}

static void
st_set_create_ind(ST_Con& c, ST_Ind& ind, bool create)
{
  st_set_create_obj(c, ind, create);
  int k;
  for (k = 0; k < ind.trgcount; k++) {
    ST_Trg& trg = ind.trg(k);
    st_set_create_trg(c, trg, create);
  }
}

static void
st_set_create_tab(ST_Con& c, ST_Tab& tab, bool create)
{
  st_set_create_obj(c, tab, create);
  int j;
  for (j = 0; j < tab.indcount; j++) {
    ST_Ind& ind = tab.ind(j);
    if (create == true)
      assert(!ind.exists());
    else {
      if (ind.exists())
        st_set_create_ind(c, ind, false);
    }
  }
}

// verify against database listing

static bool
st_known_type(const NdbDictionary::Dictionary::List::Element& element)
{
  switch (element.type) {
  case NdbDictionary::Object::UserTable:
    assert(element.database != 0);
    if (strcmp(element.database, "mysql") == 0)
      break;
    if (strncmp(element.name, "NDB$BLOB", 8) == 0)
      break;
    return true;
  case NdbDictionary::Object::UniqueHashIndex:
  case NdbDictionary::Object::OrderedIndex:
  case NdbDictionary::Object::HashIndexTrigger:
  case NdbDictionary::Object::IndexTrigger:
    return true;
  default:
    break;
  }
  return false;
}

static bool
st_match_obj(const ST_Obj& obj,
             const NdbDictionary::Dictionary::List::Element& element)
{
  int veryverbose = 0;
  if (veryverbose) {
    g_info
      << "match:"
      << " " << obj.type << "-" << element.type
      << " " << obj.dbname << "-" << element.database
      << " " << obj.realname() << "-" << element.name << endl;
  }
  return
    obj.type == element.type &&
    strcmp(obj.dbname, element.database) == 0 &&
    strcmp(obj.realname(), element.name) == 0;
}

static int // check state
st_verify_obj(const ST_Obj& obj,
              const NdbDictionary::Dictionary::List::Element& element)
{
  chk2(obj.exists(), obj.name);

  if (obj.commit)
    chk2(element.state == NdbDictionary::Object::StateOnline, obj.name);

  // other states are inconsistent

  else if (obj.create) {
    if (obj.is_table() || obj.is_index())
      chk2(element.state == NdbDictionary::Object::StateBuilding, obj.name);
    if (obj.is_trigger())
      chk2(element.state == NdbDictionary::Object::StateBuilding, obj.name);
  }
  else {
    if (obj.is_trigger())
      chk2(element.state == NdbDictionary::Object::StateOnline, obj.name);
    if (obj.is_table() || obj.is_index())
      chk2(element.state == NdbDictionary::Object::StateDropping, obj.name);
  }
  return 0;
err:
  return -1;
}

static int // find on list
st_verify_obj(const ST_Obj& obj,
              const NdbDictionary::Dictionary::List& list)
{
  int found = 0;
  int n;
  for (n = 0; n < (int)list.count; n++) {
    const NdbDictionary::Dictionary::List::Element& element =
      list.elements[n];
    if (!st_known_type(element))
      continue;
    if (st_match_obj(obj, element)) {
      chk1(st_verify_obj(obj, element) == 0);
      found += 1;
    }
  }
  if (obj.exists())
    chk2(found == 1, obj.name);
  else
    chk2(found == 0, obj.name);
  return 0;
err:
  return -1;
}

static int // possible match
st_verify_obj(const ST_Obj& obj,
             const NdbDictionary::Dictionary::List::Element& element,
             int& found)
{
  if (obj.exists()) {
    if (st_match_obj(obj, element)) {
      chk1(st_verify_obj(obj, element) == 0);
      found += 1;
    }
  }
  else {
    chk2(st_match_obj(obj, element) == false, obj.name);
  }
  return 0;
err:
  return -1;
}

static int
st_verify_list(ST_Con& c)
{
  NdbDictionary::Dictionary::List list;
  chk2(c.dic->listObjects(list) == 0, c.dic->getNdbError());
  int i, j, k, n;
  // us vs list
  for (i = 0; i < c.tabcount; i++) {
    const ST_Tab& tab = c.tab(i);
    chk1(st_verify_obj(tab, list) == 0);
    for (j = 0; j < tab.indcount; j++) {
      const ST_Ind& ind = tab.ind(j);
      chk1(st_verify_obj(ind, list) == 0);
      for (k = 0; k < ind.trgcount; k++) {
        const ST_Trg& trg = ind.trg(k);
        chk1(st_verify_obj(trg, list) == 0);
      }
    }
  }
  // list vs us
  for (n = 0; n < (int)list.count; n++) {
    const NdbDictionary::Dictionary::List::Element& element =
      list.elements[n];
    if (!st_known_type(element))
      continue;
    int found = 0;
    for (i = 0; i < c.tabcount; i++) {
      const ST_Tab& tab = c.tab(i);
      chk1(st_verify_obj(tab, element, found) == 0);
      for (j = 0; j < tab.indcount; j++) {
        const ST_Ind& ind = tab.ind(j);
        chk1(st_verify_obj(ind, element, found) == 0);
        for (k = 0; k < ind.trgcount; k++) {
          const ST_Trg& trg = ind.trg(k);
          chk1(st_verify_obj(trg, element, found) == 0);
        }
      }
    }
    const char* dot = element.database[0] != 0 ? "." : "";
    chk2(found == 1, element.database << dot << element.name);
  }
  return 0;
err:
  return -1;
}

// wait for DICT to finish current trans

static int
st_wait_idle(ST_Con& c)
{
  // todo: use try-lock when available
  g_info << "st_wait_idle" << endl;
  int count = 0;
  int max_count = 60;
  int milli_sleep = 1000;
  while (count++ < max_count) {
    NdbDictionary::Dictionary::List list;
    chk2(c.dic->listObjects(list) == 0, c.dic->getNdbError());
    bool ok = true;
    int n;
    for (n = 0; n < (int)list.count; n++) {
      const NdbDictionary::Dictionary::List::Element& element =
        list.elements[n];
      if (!st_known_type(element))
        continue;
      if (element.state != NdbDictionary::Object::StateOnline) {
        ok = false;
        break;
      }
    }
    if (ok)
      return 0;
    g_info << "waiting count:" << count << "/" << max_count << endl;
    NdbSleep_MilliSleep(milli_sleep);
  }
  g_err << "st_wait_idle: objects did not become Online" << endl;
err:
  return -1;
}

// ndb dict comparisons (non-retrieved vs retrieved)

static int
st_equal_column(const NdbDictionary::Column& c1,
                const NdbDictionary::Column& c2,
                NdbDictionary::Object::Type type)
{
  chk1(strcmp(c1.getName(), c2.getName()) == 0);
  chk1(c1.getNullable() == c2.getNullable());
  if (type == NdbDictionary::Object::UserTable) {
    chk1(c1.getPrimaryKey() == c2.getPrimaryKey());
  }
  if (0) { // should fix
    chk1(c1.getColumnNo() == c2.getColumnNo());
  }
  chk1(c1.getType() == c2.getType());
  if (c1.getType() == NdbDictionary::Column::Decimal ||
      c1.getType() == NdbDictionary::Column::Decimalunsigned) {
    chk1(c1.getPrecision() == c2.getPrecision());
    chk1(c1.getScale() == c2.getScale());
  }
  if (c1.getType() != NdbDictionary::Column::Blob &&
      c1.getType() != NdbDictionary::Column::Text) {
    chk1(c1.getLength() == c2.getLength());
  } else {
    chk1(c1.getInlineSize() == c2.getInlineSize());
    chk1(c1.getPartSize() == c2.getPartSize());
    chk1(c1.getStripeSize() == c2.getStripeSize());
  }
  chk1(c1.getCharset() == c2.getCharset());
  if (type == NdbDictionary::Object::UserTable) {
    chk1(c1.getPartitionKey() == c2.getPartitionKey());
  }
  chk1(c1.getArrayType() == c2.getArrayType());
  chk1(c1.getStorageType() == c2.getStorageType());
  chk1(c1.getDynamic() == c2.getDynamic());
  chk1(c1.getAutoIncrement() == c2.getAutoIncrement());
  return 0;
err:
  return -1;
}

static int
st_equal_table(const NdbDictionary::Table& t1, const NdbDictionary::Table& t2)
{
  chk1(strcmp(t1.getName(), t2.getName()) == 0);
  chk1(t1.getLogging() == t2.getLogging());
  chk1(t1.getFragmentType() == t2.getFragmentType());
  chk1(t1.getKValue() == t2.getKValue());
  chk1(t1.getMinLoadFactor() == t2.getMinLoadFactor());
  chk1(t1.getMaxLoadFactor() == t2.getMaxLoadFactor());
  chk1(t1.getNoOfColumns() == t2.getNoOfColumns());
  /*
   * There is no method to get type of table...
   * On the other hand SystemTable/UserTable should be just Table
   * and "System" should be an independent property.
   */
  NdbDictionary::Object::Type type;
  type = NdbDictionary::Object::UserTable;
  int n;
  for (n = 0; n < t1.getNoOfColumns(); n++) {
    const NdbDictionary::Column* c1 = t1.getColumn(n);
    const NdbDictionary::Column* c2 = t2.getColumn(n);
    assert(c1 != 0 && c2 != 0);
    chk2(st_equal_column(*c1, *c2, type) == 0, "col:" << n);
  }
  chk1(t1.getNoOfPrimaryKeys() == t2.getNoOfPrimaryKeys());
  chk1(t1.getTemporary() == t2.getTemporary());
  chk1(t1.getForceVarPart() == t2.getForceVarPart());
  return 0;
err:
  return -1;
}

static int
st_equal_index(const NdbDictionary::Index& i1, const NdbDictionary::Index& i2)
{
  chk1(strcmp(i1.getName(), i2.getName()) == 0);
  assert(i1.getTable() != 0 && i2.getTable() != 0);
  chk1(strcmp(i1.getTable(), i2.getTable()) == 0);
  chk1(i1.getNoOfColumns() == i2.getNoOfColumns());
  chk1(i1.getType() == i2.getType());
  NdbDictionary::Object::Type type;
  type = (NdbDictionary::Object::Type)i1.getType();
  int n;
  for (n = 0; n < i1.getNoOfColumns(); n++) {
    const NdbDictionary::Column* c1 = i1.getColumn(n);
    const NdbDictionary::Column* c2 = i2.getColumn(n);
    assert(c1 != 0 && c2 != 0);
    chk2(st_equal_column(*c1, *c2, type) == 0, "col:" << n);
  }
  chk1(i1.getLogging() == i2.getLogging());
  chk1(i1.getTemporary() == i2.getTemporary());
  return 0;
err:
  return -1;
}

// verify against database objects (hits all nodes randomly)

static int
st_verify_table(ST_Con& c, ST_Tab& tab)
{
  c.dic->invalidateTable(tab.name);
  const NdbDictionary::Table* pTab = c.dic->getTable(tab.name);
  tab.tab_r = pTab;
  if (tab.exists()) {
    chk2(pTab != 0, c.dic->getNdbError());
    chk1(st_equal_table(*tab.tab, *pTab) == 0);
    tab.id = pTab->getObjectId();
    g_info << tab << ": verified exists tx_on:" << c.tx_on << endl;
  } else {
    chk2(pTab == 0, tab);
    chk2(c.dic->getNdbError().code == 723, c.dic->getNdbError());
    g_info << tab << ": verified not exists tx_on:" << c.tx_on << endl;
    tab.id = -1;
  }
  return 0;
err:
  return -1;
}

static int
st_verify_index(ST_Con& c, ST_Ind& ind)
{
  ST_Tab& tab = *ind.tab;
  c.dic->invalidateIndex(ind.name, tab.name);
  const NdbDictionary::Index* pInd = c.dic->getIndex(ind.name, tab.name);
  ind.ind_r = pInd;
  if (ind.exists()) {
    chk2(pInd != 0, c.dic->getNdbError());
    chk1(st_equal_index(*ind.ind, *pInd) == 0);
    ind.id = pInd->getObjectId();
    g_info << ind << ": verified exists tx_on:" << c.tx_on << endl;
  } else {
    chk2(pInd == 0, ind);
    chk2(c.dic->getNdbError().code == 4243, c.dic->getNdbError());
    g_info << ind << ": verified not exists tx_on:" << c.tx_on << endl;
    ind.id = -1;
  }
  return 0;
err:
  return -1;
}

static int
st_verify_all(ST_Con& c)
{
  chk1(st_verify_list(c) == 0);
  int i, j;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    chk1(st_verify_table(c, tab) == 0);
    for (j = 0; j < tab.indcount; j++) {
      ST_Ind& ind = tab.ind(j);
      chk1(st_verify_index(c, ind) == 0);
    }
  }
  return 0;
err:
  return -1;
}

// subroutines

static const uint
ST_AbortFlag = NdbDictionary::Dictionary::SchemaTransAbort;

static const uint
ST_BackgroundFlag = NdbDictionary::Dictionary::SchemaTransBackground;

struct ST_Retry {
  int max_tries;
  int sleep_ms;
};

static int
st_begin_trans(ST_Con& c, int code = 0)
{
  g_info << "begin trans";
  if (code == 0) {
    g_info << endl;
    chk2(c.dic->beginSchemaTrans() == 0, c.dic->getNdbError());
    chk1(c.dic->hasSchemaTrans() == true);
    c.tx_on = true;
  } else {
    g_info << " - expect error " << code << endl;
    chk1(c.dic->beginSchemaTrans() == -1);
    const NdbError& error = c.dic->getNdbError();
    chk2(error.code == code, error << " wanted: " << code);
  }
  return 0;
err:
  return -1;
}

static int
st_begin_trans(ST_Con& c, ST_Errins errins)
{
  assert(errins.code != 0);
  chk1(st_do_errins(c, errins) == 0);
  chk1(st_begin_trans(c, errins.code) == 0);
  return 0;
err:
  return -1;
}

static int
st_begin_trans(ST_Con& c, ST_Retry retry)
{
  int tries = 0;
  while (++tries <= retry.max_tries) {
    int code = 0;
    if (c.dic->beginSchemaTrans() == -1) {
      code = c.dic->getNdbError().code;
      assert(code != 0);
    }
    chk2(code == 0 || code == 780 || code == 701, c.dic->getNdbError());
    if (code == 0) {
      chk1(c.dic->hasSchemaTrans() == true);
      g_info << "begin trans at try " << tries << endl;
      break;
    }
    NdbSleep_MilliSleep(retry.sleep_ms);
  }
  return 0;
err:
  return -1;
}

static int
st_end_trans(ST_Con& c, uint flags)
{
  g_info << "end trans flags:" << hex << flags << endl;
  chk2(c.dic->endSchemaTrans(flags) == 0, c.dic->getNdbError());
  c.tx_on = false;
  c.tx_commit = !(flags & ST_AbortFlag);
  st_set_commit_all(c);
  return 0;
err:
  return -1;
}

static int
st_load_table(ST_Con& c, ST_Tab& tab, int rows = 1000)
{
  g_info << tab.name << ": load data rows:" << rows << endl;
  assert(tab.tab_r != 0);
  HugoTransactions ht(*tab.tab_r);
  chk1(ht.loadTable(c.ndb, rows) == 0);
  return 0;
err:
  return -1;
}

static int
st_create_table(ST_Con& c, ST_Tab& tab, int code = 0)
{
  g_info << tab.name << ": create table";
  if (code == 0) {
    g_info << endl;
    assert(!tab.exists());
    chk2(c.dic->createTable(*tab.tab) == 0, c.dic->getNdbError());
    g_info << tab.name << ": created" << endl;
    st_set_create_tab(c, tab, true);
  }
  else {
    g_info << " - expect error " << code << endl;
    chk1(c.dic->createTable(*tab.tab) == -1);
    const NdbError& error = c.dic->getNdbError();
    chk2(error.code == code, error << " wanted: " << code);
  }
  chk1(st_verify_table(c, tab) == 0);
  return 0;
err:
  return -1;
}

static int
st_create_table(ST_Con& c, ST_Tab& tab, ST_Errins errins)
{
  assert(errins.code != 0);
  chk1(st_do_errins(c, errins) == 0);
  chk1(st_create_table(c, tab, errins.code) == 0);
  return 0;
err:
  return -1;
}

static int
st_drop_table(ST_Con& c, ST_Tab& tab, int code = 0)
{
  g_info << tab.name << ": drop table";
  if (code == 0) {
    g_info << endl;
    assert(tab.exists());
    c.dic->invalidateTable(tab.name);
    chk2(c.dic->dropTable(tab.name) == 0, c.dic->getNdbError());
    g_info << tab.name << ": dropped" << endl;
    st_set_create_tab(c, tab, false);
  } else {
    g_info << " - expect error " << code << endl;
    c.dic->invalidateTable(tab.name);
    chk1(c.dic->dropTable(tab.name) == -1);
    const NdbError& error = c.dic->getNdbError();
    chk2(error.code == code, error << " wanted: " << code);
  }
  chk1(st_verify_table(c, tab) == 0);
  return 0;
err:
  return -1;
}

static int
st_drop_table(ST_Con& c, ST_Tab& tab, ST_Errins errins)
{
  assert(errins.code != 0);
  chk1(st_do_errins(c, errins) == 0);
  chk1(st_drop_table(c, tab, errins.code) == 0);
  return 0;
err:
  return -1;
}

static int
st_create_index(ST_Con& c, ST_Ind& ind, int code = 0)
{
  ST_Tab& tab = *ind.tab;
  g_info << ind.name << ": create index on "
         << tab.name << "(" << ind.colnames.c_str() << ")";
  if (code == 0) {
    g_info << endl;
    assert(!ind.exists());
    chk2(c.dic->createIndex(*ind.ind, *tab.tab_r) == 0, c.dic->getNdbError());
    st_set_create_ind(c, ind, true);
    g_info << ind.name << ": created" << endl;
  } else {
    g_info << " - expect error " << code << endl;
    chk1(c.dic->createIndex(*ind.ind, *tab.tab_r) == -1);
    const NdbError& error = c.dic->getNdbError();
    chk2(error.code == code, error << " wanted: " << code);
  }
  chk1(st_verify_index(c, ind) == 0);
  return 0;
err:
  return -1;
}

static int
st_create_index(ST_Con& c, ST_Ind& ind, ST_Errins errins)
{
  assert(errins.code != 0);
  chk1(st_do_errins(c, errins) == 0);
  chk1(st_create_index(c, ind, errins.code) == 0);
  return 0;
err:
  return -1;
}

static int
st_drop_index(ST_Con& c, ST_Ind& ind, int code = 0)
{
  ST_Tab& tab = *ind.tab;
  g_info << ind.name << ": drop index";
  if (code == 0) {
    g_info << endl;
    assert(ind.exists());
    c.dic->invalidateIndex(ind.name, tab.name);
    chk2(c.dic->dropIndex(ind.name, tab.name) == 0, c.dic->getNdbError());
    g_info << ind.name << ": dropped" << endl;
    st_set_create_ind(c, ind, false);
  } else {
    g_info << " expect error " << code << endl;
    c.dic->invalidateIndex(ind.name, tab.name);
    chk1(c.dic->dropIndex(ind.name, tab.name) == -1);
    const NdbError& error = c.dic->getNdbError();
    chk2(error.code == code, error << " wanted: " << code);
  }
  chk1(st_verify_index(c, ind) == 0);
  return 0;
err:
  return -1;
}

static int
st_drop_index(ST_Con& c, ST_Ind& ind, ST_Errins errins)
{
  assert(errins.code != 0);
  chk1(st_do_errins(c, errins) == 0);
  chk1(st_drop_index(c, ind, errins.code) == 0);
  return 0;
err:
  return -1;
}

static int
st_create_table_index(ST_Con& c, ST_Tab& tab)
{
  chk1(st_create_table(c, tab) == 0);
  int j;
  for (j = 0; j < tab.indcount; j++) {
    ST_Ind& ind = tab.ind(j);
    chk1(st_create_index(c, ind) == 0);
  }
  return 0;
err:
  return -1;
}

// drop all

static int
st_drop_test_tables(ST_Con& c)
{
  g_info << "st_drop_test_tables" << endl;
  int i;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (tab.exists())
      chk1(st_drop_table(c, tab) == 0);
  }
  return 0;
err:
  return -1;
}

// error insert values

static const ST_Errins
st_errins_trans[] = {
  ST_Errins(6101, 780),
  ST_Errins()
};

static const ST_Errins
st_errins_table[] = {
  ST_Errins(6111, 783),
  ST_Errins(6121, 9121),
  //ST_Errins(6131, 9131),
  ST_Errins()
};

static ST_Errins
st_errins_index[] = {
  ST_Errins(st_errins_table),
  ST_Errins(6112, 783),
  ST_Errins(6113, 783),
  ST_Errins(6114, 783),
  ST_Errins(6122, 9122),
  ST_Errins(6123, 9123),
  ST_Errins(6124, 9124),
  //ST_Errins(6132, 9131),
  //ST_Errins(6133, 9131),
  //ST_Errins(6134, 9131),
  //ST_Errins(6135, 9131),
  ST_Errins()
};

static ST_Errins
st_errins_index_create[] = {
  ST_Errins(st_errins_index),
  ST_Errins(6116, 783),
  ST_Errins(6126, 9126),
  //ST_Errins(6136, 9136),
  ST_Errins()
};

static ST_Errins
st_errins_index_drop[] = {
  ST_Errins(st_errins_index),
  ST_Errins()
};

// specific test cases

static int
st_test_create(ST_Con& c, int arg = -1)
{
  int do_abort = (arg == 1);
  int i;
  chk1(st_begin_trans(c) == 0);
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    chk1(st_create_table_index(c, tab) == 0);
  }
  chk1(st_verify_list(c) == 0);
  if (!do_abort)
    chk1(st_end_trans(c, 0) == 0);
  else
    chk1(st_end_trans(c, ST_AbortFlag) == 0);
  chk1(st_verify_list(c) == 0);
  if (!do_abort)
    chk1(st_drop_test_tables(c) == 0);
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_drop(ST_Con& c, int arg = -1)
{
  int do_abort = (arg == 1);
  int i;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    chk1(st_create_table_index(c, tab) == 0);
  }
  chk1(st_begin_trans(c) == 0);
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    chk1(st_drop_table(c, tab) == 0);
  }
  chk1(st_verify_list(c) == 0);
  if (!do_abort)
    chk1(st_end_trans(c, 0) == 0);
  else
    chk1(st_end_trans(c, ST_AbortFlag) == 0);
  chk1(st_verify_list(c) == 0);
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_rollback_create_table(ST_Con& c, int arg = -1)
{
  int i;
  chk1(st_begin_trans(c) == 0);
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (i % 2 == 0) {
      ST_Errins errins(6111, 783, 0); // fail CTa seize op
      chk1(st_create_table(c, tab, errins) == 0);
    } else {
      chk1(st_create_table(c, tab) == 0);
    }
  }
  chk1(st_end_trans(c, 0) == 0);
  chk1(st_verify_list(c) == 0);
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (i % 2 == 0)
      assert(!tab.exists());
    else {
      assert(tab.exists());
      chk1(st_drop_table(c, tab) == 0);
    }
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_rollback_drop_table(ST_Con& c, int arg = -1)
{
  int i;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    chk1(st_create_table(c, tab) == 0);
  }
  chk1(st_begin_trans(c) == 0);
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (i % 2 == 0) {
      ST_Errins errins(6111, 783, 0); // fail DTa seize op
      chk1(st_drop_table(c, tab, errins) == 0);
    } else {
      chk1(st_drop_table(c, tab) == 0);
    }
  }
  chk1(st_end_trans(c, 0) == 0);
  chk1(st_verify_list(c) == 0);
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (i % 2 == 0) {
      assert(tab.exists());
      chk1(st_drop_table(c, tab) == 0);
    } else {
      assert(!tab.exists());
    }
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_rollback_create_index(ST_Con& c, int arg = -1)
{
  int i, j;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (tab.indcount < 1)
      continue;
    chk1(st_create_table(c, tab) == 0);
    chk1(st_begin_trans(c) == 0);
    for (j = 0; j < tab.indcount; j++) {
      ST_Ind& ind = tab.ind(j);
      if (j % 2 == 0) {
        ST_Errins errins(6116, 783, 0); // fail BIn seize op
        chk1(st_create_index(c, ind, errins) == 0);
      } else {
        chk1(st_create_index(c, ind) == 0);
      }
    }
    chk1(st_end_trans(c, 0) == 0);
    chk1(st_verify_list(c) == 0);
    for (j = 0; j < tab.indcount; j++) {
      ST_Ind& ind = tab.ind(j);
      if (j % 2 == 0)
        assert(!ind.exists());
      else {
        assert(ind.exists());
        chk1(st_drop_index(c, ind) == 0);
      }
    }
    chk1(st_drop_table(c, tab) == 0);
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_rollback_drop_index(ST_Con& c, int arg = -1)
{
  int i, j;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (tab.indcount < 1)
      continue;
    chk1(st_create_table_index(c, tab) == 0);
  }
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (tab.indcount < 1)
      continue;
    chk1(st_begin_trans(c) == 0);
    for (j = 0; j < tab.indcount; j++) {
      ST_Ind& ind = tab.ind(j);
      if (j % 2 == 0) {
        ST_Errins errins(6114, 783, 0); // fail ATr seize op
        chk1(st_drop_index(c, ind, errins) == 0);
      } else {
        chk1(st_drop_index(c, ind) == 0);
      }
    }
    chk1(st_end_trans(c, 0) == 0);
    chk1(st_verify_list(c) == 0);
    for (j = 0; j < tab.indcount; j++) {
      ST_Ind& ind = tab.ind(j);
      if (j % 2 == 0) {
        assert(ind.exists());
        chk1(st_drop_index(c, ind) == 0);
      } else {
        assert(!ind.exists());
      }
    }
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_dup_create_table(ST_Con& c, int arg = -1)
{
  int do_trans;
  int do_abort;
  int i;
  for (do_trans = 0; do_trans <= 1; do_trans++) {
    for (do_abort = 0; do_abort <= do_trans; do_abort++) {
      g_info << "trans:" << do_trans
             << " abort:" << do_abort << endl;
      for (i = 0; i < c.tabcount; i++) {
        ST_Tab& tab = c.tab(i);
        if (do_trans)
          chk1(st_begin_trans(c) == 0);
        chk1(st_create_table(c, tab) == 0);
        chk1(st_create_table(c, tab, 721) == 0);
        if (do_trans) {
          if (!do_abort)
            chk1(st_end_trans(c, 0) == 0);
          else
            chk1(st_end_trans(c, ST_AbortFlag) == 0);
        }
        chk1(st_verify_list(c) == 0);
        if (tab.exists()) {
          chk1(st_drop_table(c, tab) == 0);
        }
      }
    }
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_dup_drop_table(ST_Con& c, int arg = -1)
{
  int do_trans;
  int do_abort;
  int i;
  for (do_trans = 0; do_trans <= 1; do_trans++) {
    for (do_abort = 0; do_abort <= do_trans; do_abort++) {
      g_info << "trans:" << do_trans
             << " abort:" << do_abort << endl;
      for (i = 0; i < c.tabcount; i++) {
        ST_Tab& tab = c.tab(i);
        chk1(st_create_table(c, tab) == 0);
        if (do_trans)
          chk1(st_begin_trans(c) == 0);
        chk1(st_drop_table(c, tab) == 0);
        if (!do_trans)
          chk1(st_drop_table(c, tab, 723) == 0);
        else
          chk1(st_drop_table(c, tab, 785) == 0);
        if (do_trans) {
          if (!do_abort)
            chk1(st_end_trans(c, 0) == 0);
          else
            chk1(st_end_trans(c, ST_AbortFlag) == 0);
        }
        chk1(st_verify_list(c) == 0);
        if (tab.exists()) {
          chk1(st_drop_table(c, tab) == 0);
        }
      }
    }
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_dup_create_index(ST_Con& c, int arg = -1)
{
  int do_trans;
  int do_abort;
  int i, j;
  for (do_trans = 0; do_trans <= 1; do_trans++) {
    for (do_abort = 0; do_abort <= do_trans; do_abort++) {
      g_info << "trans:" << do_trans
             << " abort:" << do_abort << endl;
      for (i = 0; i < c.tabcount; i++) {
        ST_Tab& tab = c.tab(i);
        if (tab.indcount < 1)
          continue;
        chk1(st_create_table(c, tab) == 0);
        for (j = 0; j < tab.indcount; j++) {
          ST_Ind& ind = tab.ind(j);
          if (do_trans)
            chk1(st_begin_trans(c) == 0);
          chk1(st_create_index(c, ind) == 0);
          chk1(st_create_index(c, ind, 721) == 0);
          if (do_trans) {
            if (!do_abort)
              chk1(st_end_trans(c, 0) == 0);
            else
              chk1(st_end_trans(c, ST_AbortFlag) == 0);
          }
          chk1(st_verify_list(c) == 0);
        }
        chk1(st_drop_table(c, tab) == 0);
      }
    }
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_dup_drop_index(ST_Con& c, int arg = -1)
{
  int do_trans;
  int do_abort;
  int i, j;
  for (do_trans = 0; do_trans <= 1; do_trans++) {
    for (do_abort = 0; do_abort <= do_trans; do_abort++) {
      g_info << "trans:" << do_trans
             << " abort:" << do_abort << endl;
      for (i = 0; i < c.tabcount; i++) {
        ST_Tab& tab = c.tab(i);
        if (tab.indcount < 1)
          continue;
        chk1(st_create_table(c, tab) == 0);
        for (j = 0; j < tab.indcount; j++) {
          ST_Ind& ind = tab.ind(j);
          chk1(st_create_index(c, ind) == 0);
          if (do_trans)
            chk1(st_begin_trans(c) == 0);
          chk1(st_drop_index(c, ind) == 0);
          if (!do_trans)
            chk1(st_drop_index(c, ind, 4243) == 0);
          else
            chk1(st_drop_index(c, ind, 785) == 0);
          if (do_trans) {
            if (!do_abort)
              chk1(st_end_trans(c, 0) == 0);
            else
              chk1(st_end_trans(c, ST_AbortFlag) == 0);
          }
          chk1(st_verify_list(c) == 0);
        }
        chk1(st_drop_table(c, tab) == 0);
      }
    }
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_build_index(ST_Con& c, int arg = -1)
{
  int i, j;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (tab.indcount < 1)
      continue;
    chk1(st_create_table(c, tab) == 0);
    chk1(st_load_table(c, tab) == 0);
    for (j = 0; j < tab.indcount; j++) {
      ST_Ind& ind = tab.ind(j);
      chk1(st_create_index(c, ind) == 0);
      chk1(st_verify_list(c) == 0);
    }
    chk1(st_drop_table(c, tab) == 0);
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static ST_Errins
st_test_local_create_list[] = {
  ST_Errins(8033, 291, 1),    // TC trigger
  ST_Errins(8033, 291, 0),
  ST_Errins(4003, 4237, 1),   // TUP trigger
  ST_Errins(4003, 4237, 0),
  ST_Errins(8034, 292, 1),    // TC index
  ST_Errins(8034, 292, 0)
};

static int
st_test_local_create(ST_Con& c, int arg = -1)
{
  const int n = arg;
  ST_Errins *list = st_test_local_create_list;
  const int listlen = 
    sizeof(st_test_local_create_list)/sizeof(st_test_local_create_list[0]);
  assert(0 <= n && n < listlen);
  const bool only_unique = (n == 0 || n == 1 || n == 4 || n == 5);
  int i, j;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    bool tabdone = false;
    for (j = 0; j < tab.indcount; j++) {
      ST_Ind& ind = tab.ind(j);
      if (only_unique && !ind.is_unique())
        continue;
      if (!tabdone) {
        chk1(st_create_table(c, tab) == 0);
        chk1(st_load_table(c, tab) == 0);
        tabdone = true;
      }
      ST_Errins errins = list[n];
      chk1(st_create_index(c, ind, errins) == 0);
      chk1(st_verify_list(c) == 0);
    }
    if (tabdone)
      chk1(st_drop_table(c, tab) == 0);
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

// random test cases

static const uint ST_AllowAbort = 1;
static const uint ST_AllowErrins = 2;

static int
st_test_trans(ST_Con& c, int arg = -1)
{
  if ((arg & ST_AllowErrins) && randomly(2, 3)) {
    ST_Errins errins = st_get_errins(c, st_errins_trans);
    chk1(st_begin_trans(c, errins) == 0);
  } else {
    chk1(st_begin_trans(c) == 0);
    if (randomly(1, 5)) {
      g_info << "try duplicate begin trans" << endl;
      chk1(st_begin_trans(c, 4410) == 0);
      chk1(c.dic->hasSchemaTrans() == true);
    }
    if ((arg & ST_AllowAbort) && randomly(1, 3)) {
      chk1(st_end_trans(c, ST_AbortFlag) == 0);
    } else {
      chk1(st_end_trans(c, 0) == 0);
    }
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_create_table(ST_Con& c, int arg = -1)
{
  bool trans = randomly(3, 4);
  bool simpletrans = !trans && randomly(1, 2);
  g_info << "trans:" << trans << " simpletrans:" << simpletrans << endl;
  if (trans) {
    chk1(st_begin_trans(c) == 0);
  }
  int i;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (tab.exists()) {
      g_info << tab.name << ": skip existing" << endl;
      continue;
    }
    g_info << tab.name << ": to create" << endl;
    if (simpletrans) {
      chk1(st_begin_trans(c) == 0);
    }
    if ((arg & ST_AllowErrins) && randomly(1, 3)) {
      ST_Errins errins = st_get_errins(c, st_errins_table);
      chk1(st_create_table(c, tab, errins) == 0);
      if (simpletrans) {
        if (randomly(1, 2))
          chk1(st_end_trans(c, 0) == 0);
        else
          chk1(st_end_trans(c, ST_AbortFlag) == 0);
      }
    } else {
      chk1(st_create_table(c, tab) == 0);
      if (simpletrans) {
        uint flags = 0;
        if ((arg & ST_AllowAbort) && randomly(4, 5))
          flags |= ST_AbortFlag;
        chk1(st_end_trans(c, flags) == 0);
      }
    }
    if (tab.exists() && randomly(1, 3)) {
      g_info << tab.name << ": try duplicate create" << endl;
      chk1(st_create_table(c, tab, 721) == 0);
    }
  }
  if (trans) {
    uint flags = 0;
    if ((arg & ST_AllowAbort) && randomly(4, 5))
      flags |= ST_AbortFlag;
    chk1(st_end_trans(c, flags) == 0);
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_drop_table(ST_Con& c, int arg = -1)
{
  bool trans = randomly(3, 4);
  bool simpletrans = !trans && randomly(1, 2);
  g_info << "trans:" << trans << " simpletrans:" << simpletrans << endl;
  if (trans) {
    chk1(st_begin_trans(c) == 0);
  }
  int i;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (!tab.exists()) {
      g_info << tab.name << ": skip not existing" << endl;
      continue;
    }
    g_info << tab.name << ": to drop" << endl;
    if (simpletrans) {
      chk1(st_begin_trans(c) == 0);
    }
    if ((arg & ST_AllowErrins) && randomly(1, 3)) {
      ST_Errins errins = st_get_errins(c, st_errins_table);
      chk1(st_drop_table(c, tab, errins) == 0);
      if (simpletrans) {
        if (randomly(1, 2))
          chk1(st_end_trans(c, 0) == 0);
        else
          chk1(st_end_trans(c, ST_AbortFlag) == 0);
      }
    } else {
      chk1(st_drop_table(c, tab) == 0);
      if (simpletrans) {
        uint flags = 0;
        if ((arg & ST_AllowAbort) && randomly(4, 5))
          flags |= ST_AbortFlag;
        chk1(st_end_trans(c, flags) == 0);
      }
    }
    if (!tab.exists() && randomly(1, 3)) {
      g_info << tab.name << ": try duplicate drop" << endl;
      chk1(st_drop_table(c, tab, 723) == 0);
    }
  }
  if (trans) {
    uint flags = 0;
    if ((arg & ST_AllowAbort) && randomly(4, 5))
      flags |= ST_AbortFlag;
    chk1(st_end_trans(c, flags) == 0);
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_table(ST_Con& c, int arg = -1)
{
  chk1(st_test_create_table(c) == NDBT_OK);
  chk1(st_test_drop_table(c) == NDBT_OK);
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_create_index(ST_Con& c, int arg = -1)
{
  bool trans = randomly(3, 4);
  bool simpletrans = !trans && randomly(1, 2);
  g_info << "trans:" << trans << " simpletrans:" << simpletrans << endl;
  if (trans) {
    chk1(st_begin_trans(c) == 0);
  }
  int i, j;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (tab.indcount == 0)
      continue;
    if (!tab.exists()) {
      g_info << tab.name << ": to create" << endl;
      chk1(st_create_table(c, tab) == 0);
    }
    for (j = 0; j < tab.indcount; j++) {
      ST_Ind& ind = tab.ind(j);
      if (ind.exists()) {
        g_info << ind.name << ": skip existing" << endl;
        continue;
      }
      g_info << ind.name << ": to create" << endl;
      if (simpletrans) {
        chk1(st_begin_trans(c) == 0);
      }
      if ((arg & ST_AllowErrins) && randomly(1, 3)) {
        const ST_Errins* list = st_errins_index_create;
        ST_Errins errins = st_get_errins(c, list);
        chk1(st_create_index(c, ind, errins) == 0);
        if (simpletrans) {
          if (randomly(1, 2))
            chk1(st_end_trans(c, 0) == 0);
          else
            chk1(st_end_trans(c, ST_AbortFlag) == 0);
        }
      } else {
        chk1(st_create_index(c, ind) == 0);
        if (simpletrans) {
          uint flags = 0;
          if ((arg & ST_AllowAbort) && randomly(4, 5))
            flags |= ST_AbortFlag;
          chk1(st_end_trans(c, flags) == 0);
        }
      }
      if (ind.exists() && randomly(1, 3)) {
        g_info << ind.name << ": try duplicate create" << endl;
        chk1(st_create_index(c, ind, 721) == 0);
      }
    }
  }
  if (trans) {
    uint flags = 0;
    if ((arg & ST_AllowAbort) && randomly(4, 5))
      flags |= ST_AbortFlag;
    chk1(st_end_trans(c, flags) == 0);
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_drop_index(ST_Con& c, int arg = -1)
{
  bool trans = randomly(3, 4);
  bool simpletrans = !trans && randomly(1, 2);
  g_info << "trans:" << trans << " simpletrans:" << simpletrans << endl;
  if (trans) {
    chk1(st_begin_trans(c) == 0);
  }
  int i, j;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (tab.indcount == 0)
      continue;
    if (!tab.exists()) {
      g_info << tab.name << ": skip not existing" << endl;
      continue;
    }
    for (j = 0; j < tab.indcount; j++) {
      ST_Ind& ind = tab.ind(j);
      if (!ind.exists()) {
        g_info << ind.name << ": skip not existing" << endl;
        continue;
      }
      g_info << ind.name << ": to drop" << endl;
      if (simpletrans) {
        chk1(st_begin_trans(c) == 0);
      }
      if ((arg & ST_AllowErrins) && randomly(1, 3)) {
        const ST_Errins* list = st_errins_index_drop;
        ST_Errins errins = st_get_errins(c, list);
        chk1(st_drop_index(c, ind, errins) == 0);
        if (simpletrans) {
          if (randomly(1, 2))
            chk1(st_end_trans(c, 0) == 0);
          else
            chk1(st_end_trans(c, ST_AbortFlag) == 0);
        }
      } else {
        chk1(st_drop_index(c, ind) == 0);
        if (simpletrans) {
          uint flags = 0;
          if ((arg & ST_AllowAbort) && randomly(4, 5))
            flags |= ST_AbortFlag;
          chk1(st_end_trans(c, flags) == 0);
        }
      }
      if (!ind.exists() && randomly(1, 3)) {
        g_info << ind.name << ": try duplicate drop" << endl;
        chk1(st_drop_index(c, ind, 4243) == 0);
      }
    }
  }
  if (trans) {
    uint flags = 0;
    if ((arg & ST_AllowAbort) && randomly(4, 5))
      flags |= ST_AbortFlag;
    chk1(st_end_trans(c, flags) == 0);
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_index(ST_Con& c, int arg = -1)
{
  chk1(st_test_create_index(c) == NDBT_OK);
  chk1(st_test_drop_index(c) == NDBT_OK);
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

// node failure and system restart

static int
st_test_anf_parse(ST_Con& c, int arg = -1)
{
  int i;
  chk1(st_start_xcon(c) == 0);
  {
    ST_Con& xc = *c.xcon;
    chk1(st_begin_trans(xc) == 0);
    for (i = 0; i < c.tabcount; i++) {
      ST_Tab& tab = c.tab(i);
      chk1(st_create_table_index(xc, tab) == 0);
    }
    // DICT aborts the trans
    xc.tx_on = false;
    xc.tx_commit = false;
    st_set_commit_all(xc);
    chk1(st_stop_xcon(c) == 0);
    chk1(st_wait_idle(c) == 0);
    chk1(st_verify_list(c) == 0);
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_anf_background(ST_Con& c, int arg = -1)
{
  int i;
  chk1(st_start_xcon(c) == 0);
  {
    ST_Con& xc = *c.xcon;
    chk1(st_begin_trans(xc) == 0);
    for (i = 0; i < c.tabcount; i++) {
      ST_Tab& tab = c.tab(i);
      chk1(st_create_table(xc, tab) == 0);
    }
    // DICT takes over and completes the trans
    st_end_trans(xc, ST_BackgroundFlag);
    chk1(st_stop_xcon(c) == 0);
    chk1(st_wait_idle(c) == 0);
    chk1(st_verify_list(c) == 0);
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_anf_fail_begin(ST_Con& c, int arg = -1)
{
  chk1(st_start_xcon(c) == 0);
  {
    ST_Con& xc = *c.xcon;

    ST_Errins errins1(6102, -1, 1); // master kills us at begin
    ST_Errins errins2(6103, -1, 0); // slave delays conf
    chk1(st_do_errins(xc, errins1) == 0);
    chk1(st_do_errins(xc, errins2) == 0);

    chk1(st_begin_trans(xc, 4009) == 0);

    // DICT aborts the trans
    xc.tx_on = false;
    xc.tx_commit = false;
    st_set_commit_all(xc);
    chk1(st_stop_xcon(c) == 0);

    // xc may get 4009 before takeover is ready (5000 ms delay)
    ST_Retry retry = { 100, 100 }; // 100 * 100ms = 10000ms
    chk1(st_begin_trans(c, retry) == 0);
    chk1(st_wait_idle(c) == 0);
    chk1(st_verify_list(c) == 0);
  }
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_snf_parse(ST_Con& c, int arg = -1)
{
  bool do_abort = (arg == 1);
  chk1(st_begin_trans(c) == 0);
  int node_id;
  node_id = -1;
  int i;
  int midcount;
  midcount = c.tabcount / 2;

  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    if (i == midcount) {
      assert(c.numdbnodes > 1);
      uint rand = urandom(c.numdbnodes);
      node_id = c.restarter->getRandomNotMasterNodeId(rand);
      g_info << "restart node " << node_id << " (async)" << endl;
      int flags = 0;
      chk1(c.restarter->restartOneDbNode2(node_id, flags) == 0);
      chk1(c.restarter->waitNodesNoStart(&node_id, 1) == 0);
      chk1(c.restarter->startNodes(&node_id, 1) == 0);
    }
    chk1(st_create_table_index(c, tab) == 0);
  }
  if (!do_abort)
    chk1(st_end_trans(c, 0) == 0);
  else
    chk1(st_end_trans(c, ST_AbortFlag) == 0);

  g_info << "wait for node " << node_id << " to come up" << endl;
  chk1(c.restarter->waitClusterStarted() == 0);
  g_info << "verify all" << endl;
  chk1(st_verify_all(c) == 0);
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_mnf_parse(ST_Con& c, int arg = -1)
{
  g_info << "not yet" << endl;
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int
st_test_sr_parse(ST_Con& c, int arg = -1)
{
  bool do_abort = (arg == 1);
  chk1(st_begin_trans(c) == 0);
  int i;
  for (i = 0; i < c.tabcount; i++) {
    ST_Tab& tab = c.tab(i);
    chk1(st_create_table_index(c, tab) == 0);
  }
  if (!do_abort)
    chk1(st_end_trans(c, 0) == 0);
  else
    chk1(st_end_trans(c, ST_AbortFlag) == 0);

  g_info << "restart all" << endl;
  int flags;
  flags = NdbRestarter::NRRF_NOSTART;
  chk1(c.restarter->restartAll2(flags) == 0);
  g_info << "wait for cluster started" << endl;
  chk1(c.restarter->waitClusterNoStart() == 0);
  chk1(c.restarter->startAll() == 0);
  chk1(c.restarter->waitClusterStarted() == 0);
  g_info << "verify all" << endl;
  chk1(st_verify_all(c) == 0);
  return NDBT_OK;
err:
  return NDBT_FAILED;
}

// run test cases

struct ST_Test {
  const char* key;
  int mindbnodes;
  int arg;
  int (*func)(ST_Con& c, int arg);
  const char* name;
  const char* desc;
};

static NdbOut&
operator<<(NdbOut& out, const ST_Test& test)
{
  out << "CASE " << test.key;
  out << " " << test.name;
  if (test.arg != -1)
    out << "+" << test.arg;
  out << " - " << test.desc;
  return out;
}

static const ST_Test
st_test_list[] = {
#define func(f) f, #f
  // specific ops
  { "a1", 1, 0,
     func(st_test_create),
     "create all within trans, commit" },
  { "a2", 1, 1,
     func(st_test_create),
     "create all within trans, abort" },
  { "a3", 1, 0,
     func(st_test_drop),
     "drop all within trans, commit" },
  { "a4", 1, 1,
     func(st_test_drop),
     "drop all within trans, abort" },
  { "b1", 1, -1,
    func(st_test_rollback_create_table),
    "partial rollback of create table ops" },
  { "b2", 1, -1,
    func(st_test_rollback_drop_table),
    "partial rollback of drop table ops" },
  { "b3", 1, -1,
    func(st_test_rollback_create_index),
    "partial rollback of create index ops" },
  { "b4", 1, -1,
    func(st_test_rollback_drop_index),
    "partial rollback of drop index ops" },
  { "c1", 1, -1,
    func(st_test_dup_create_table),
    "try to create same table twice" },
  { "c2", 1, -1,
    func(st_test_dup_drop_table),
    "try to drop same table twice" },
  { "c3", 1, -1,
    func(st_test_dup_create_index),
    "try to create same index twice" },
  { "c4", 1, -1,
    func(st_test_dup_drop_index),
    "try to drop same index twice" },
  { "d1", 1, -1,
    func(st_test_build_index),
    "build index on non-empty table" },
  { "e1", 1, 0,
    func(st_test_local_create),
    "fail trigger create in TC, master errins 8033" },
  { "e2", 2, 1,
    func(st_test_local_create),
    "fail trigger create in TC, slave errins 8033" },
  { "e3", 1, 2,
    func(st_test_local_create),
    "fail trigger create in TUP, master errins 4003" },
  { "e4", 2, 3,
    func(st_test_local_create),
    "fail trigger create in TUP, slave errins 4003" },
  { "e5", 1, 4,
    func(st_test_local_create),
    "fail index create in TC, master errins 8034" },
  { "e6", 2, 5,
    func(st_test_local_create),
    "fail index create in TC, slave errins 8034" },
  // random ops
  { "o1", 1, 0,
    func(st_test_trans),
    "start and stop schema trans" },
  { "o2", 1, ST_AllowAbort,
    func(st_test_trans),
    "start and stop schema trans, allow abort" },
  { "o3", 1, ST_AllowAbort | ST_AllowErrins,
    func(st_test_trans),
    "start and stop schema trans, allow abort errins" },
  //
  { "p1", 1, 0,
    func(st_test_create_table),
    "create tables at random" },
  { "p2", 1, ST_AllowAbort,
    func(st_test_create_table),
    "create tables at random, allow abort" },
  { "p3", 1, ST_AllowAbort | ST_AllowErrins,
    func(st_test_create_table),
    "create tables at random, allow abort errins" },
  //
  { "p4", 1, 0,
    func(st_test_table),
    "create and drop tables at random" },
  { "p5", 1, ST_AllowAbort,
    func(st_test_table),
    "create and drop tables at random, allow abort" },
  { "p6", 1, ST_AllowAbort | ST_AllowErrins,
    func(st_test_table),
    "create and drop tables at random, allow abort errins" },
  //
  { "q1", 1, 0,
    func(st_test_create_index),
    "create indexes at random" },
  { "q2", 1, ST_AllowAbort,
    func(st_test_create_index),
    "create indexes at random, allow abort" },
  { "q3", 1, ST_AllowAbort | ST_AllowErrins,
    func(st_test_create_index),
    "create indexes at random, allow abort errins" },
  //
  { "q4", 1, 0,
    func(st_test_index),
    "create and drop indexes at random" },
  { "q5", 1, ST_AllowAbort,
    func(st_test_index),
    "create and drop indexes at random, allow abort" },
  { "q6", 1, ST_AllowAbort | ST_AllowErrins,
    func(st_test_index),
    "create and drop indexes at random, allow abort errins" },
  // node failure and system restart
  { "u1", 1, -1,
    func(st_test_anf_parse),
    "api node fail in parse phase" },
  { "u2", 1, -1,
    func(st_test_anf_background),
    "api node fail after background trans" },
  { "u3", 2, -1,
    func(st_test_anf_fail_begin),
    "api node fail in middle of kernel begin trans" },
  //
  { "v1", 2, 0,
    func(st_test_snf_parse),
    "slave node fail in parse phase, commit" },
  { "v2", 2, 1,
    func(st_test_snf_parse),
    "slave node fail in parse phase, abort" },
#ifdef ndb_notyet
  { "w1", 2, 0,
    func(st_test_mnf_parse),
    "master node fail in parse phase, commit" },
  { "w2", 2, 1,
    func(st_test_mnf_parse),
    "master node fail in parse phase, abort" },
#endif
  { "x1", 1, 0,
    func(st_test_sr_parse),
    "system restart in parse phase, commit" },
  { "x2", 1, 1,
    func(st_test_sr_parse),
    "system restart in parse phase, abort" }
#undef func
};

static const int
st_test_count = sizeof(st_test_list)/sizeof(st_test_list[0]);

static const char* st_test_case = 0;
static const char* st_test_skip = 0;

static bool
st_test_match(const ST_Test& test)
{
  const char* p = 0;
  if (st_test_case == 0)
    goto skip;
  if (strstr(st_test_case, test.key) != 0)
    goto skip;
  p = strchr(st_test_case, test.key[0]);
  if (p != 0 && (p[1] < '0' || p[1] > '9'))
    goto skip;
  return false;
skip:
  if (st_test_skip == 0)
    return true;
  if (strstr(st_test_skip, test.key) != 0)
    return false;
  p = strchr(st_test_skip, test.key[0]);
  if (p != 0 && (p[1] < '0' || p[1] > '9'))
    return false;
  return true;
}

static int
st_test(ST_Con& c, const ST_Test& test)
{
  chk1(st_end_trans(c, ST_AbortFlag) == 0);
  chk1(st_drop_test_tables(c) == 0);
  chk1(st_check_db_nodes(c) == 0);

  g_err << test << endl;
  if (c.numdbnodes < test.mindbnodes) {
    g_err << "skip, too few db nodes" << endl;
    return NDBT_OK;
  }

  chk1((*test.func)(c, test.arg) == NDBT_OK);
  chk1(st_check_db_nodes(c) == 0);
  chk1(st_verify_list(c) == 0);

  return NDBT_OK;
err:
  return NDBT_FAILED;
}

static int st_random_seed = -1;

int
runSchemaTrans(NDBT_Context* ctx, NDBT_Step* step)
{
  { const char* env = NdbEnv_GetEnv("NDB_TEST_DBUG", 0, 0);
    if (env != 0 && env[0] != 0) // e.g. d:t:L:F:o,ndb_test.log
      DBUG_PUSH(env);
  }
  { const char* env = NdbEnv_GetEnv("NDB_TEST_CORE", 0, 0);
    if (env != 0 && env[0] != 0 && env[0] != '0' && env[0] != 'N')
      st_core_on_err = true;
  }
  { const char* env = NdbEnv_GetEnv("NDB_TEST_CASE", 0, 0);
    st_test_case = env;
  }
  { const char* env = NdbEnv_GetEnv("NDB_TEST_SKIP", 0, 0);
    st_test_skip = env;
  }
  { const char* env = NdbEnv_GetEnv("NDB_TEST_SEED", 0, 0);
    if (env != 0)
      st_random_seed = atoi(env);
  }

  if (st_test_case != 0 && strcmp(st_test_case, "?") == 0) {
    int i;
    ndbout << "case func+arg desc" << endl;
    for (i = 0; i < st_test_count; i++) {
      const ST_Test& test = st_test_list[i];
      ndbout << test << endl;
    }
    return NDBT_WRONGARGS;
  }

  if (st_random_seed == -1)
    st_random_seed = (short)getpid();
  if (st_random_seed != 0) {
    g_err << "random seed: " << st_random_seed << endl;
    srandom(st_random_seed);
  } else {
    g_err << "random seed: loop number" << endl;
  }

  Ndb_cluster_connection* ncc = &ctx->m_cluster_connection;
  Ndb* ndb = GETNDB(step);
  ST_Restarter* restarter = new ST_Restarter;
  ST_Con c(ncc, ndb, restarter);

  chk1(st_drop_all_tables(c) == 0);
  st_init_objects(c, ctx);

  int numloops;
  numloops = ctx->getNumLoops();

  for (c.loop = 0; numloops == 0 || c.loop < numloops; c.loop++) {
    g_err << "LOOP " << c.loop << endl;
    if (st_random_seed == 0)
      srandom(c.loop);
    int i;
    for (i = 0; i < st_test_count; i++) {
      const ST_Test& test = st_test_list[i];
      if (st_test_match(test)) {
        chk1(st_test(c, test) == NDBT_OK);
      }
    }
  }

  st_report_db_nodes(c, g_err);
  return NDBT_OK;
err:
  st_report_db_nodes(c, g_err);
  return NDBT_FAILED;
}

// end schema trans

int 
runFailCreateHashmap(NDBT_Context* ctx, NDBT_Step* step)
{
  static int lst[] = { 6204, 6205, 6206, 6207, 6208, 6209, 6210, 6211, 0 };
  
  NdbRestarter restarter;
  int nodeId = restarter.getMasterNodeId();
  Ndb* pNdb = GETNDB(step);  
  NdbDictionary::Dictionary* pDic = pNdb->getDictionary();

  int errNo = 0;
  char buf[100];
  if (NdbEnv_GetEnv("ERRNO", buf, sizeof(buf)))
  {
    errNo = atoi(buf);
    ndbout_c("Using errno: %u", errNo);
  }
  
  const int loops = ctx->getNumLoops();
  int result = NDBT_OK;

  int dump1 = DumpStateOrd::SchemaResourceSnapshot;
  int dump2 = DumpStateOrd::SchemaResourceCheckLeak;

  NdbDictionary::HashMap hm;
  pDic->initDefaultHashMap(hm, 1);

loop:
  if (pDic->getHashMap(hm, hm.getName()) != -1)
  {
    pDic->initDefaultHashMap(hm, rand() % 64);
    goto loop;
  }

  for (int l = 0; l < loops; l++) 
  {
    for (unsigned i0 = 0; lst[i0]; i0++) 
    {
      unsigned j = (l == 0 ? i0 : myRandom48(i0 + l));
      int errval = lst[j];
      if (errNo != 0 && errNo != errval)
        continue;
      g_info << "insert error node=" << nodeId << " value=" << errval << endl;
      CHECK2(restarter.insertErrorInNode(nodeId, errval) == 0,
             "failed to set error insert");
      CHECK(restarter.dumpStateAllNodes(&dump1, 1) == 0);
      
      int res = pDic->createHashMap(hm);
      CHECK2(res != 0, "create hashmap failed to fail");

      NdbDictionary::HashMap check;
      CHECK2(res != 0, "create hashmap existed");
      
      CHECK(restarter.dumpStateAllNodes(&dump2, 1) == 0);
    }
  }
end:
  return result;
}
// end FAIL create hashmap
 
NDBT_TESTSUITE(testDict);
TESTCASE("testDropDDObjects",
         "* 1. start cluster\n"
         "* 2. Create LFG\n"
         "* 3. create TS\n"
         "* 4. run DropDDObjects\n"
         "* 5. Verify DropDDObjectsRestart worked\n"){
INITIALIZER(runWaitStarted);
INITIALIZER(runDropDDObjects);
INITIALIZER(testDropDDObjectsSetup);
STEP(runDropDDObjects);
FINALIZER(DropDDObjectsVerify);
}

TESTCASE("Bug29501",
         "* 1. start cluster\n"
         "* 2. Restart 1 node -abort -nostart\n"
         "* 3. create LFG\n"
         "* 4. Restart data node\n"
         "* 5. Restart 1 node -nostart\n"
         "* 6. Drop LFG\n"){
INITIALIZER(runWaitStarted);
INITIALIZER(runDropDDObjects);
STEP(runBug29501);
FINALIZER(runDropDDObjects);
}
TESTCASE("CreateAndDrop", 
	 "Try to create and drop the table loop number of times\n"){
  INITIALIZER(runCreateAndDrop);
}
TESTCASE("CreateAndDropAtRandom",
	 "Try to create and drop table at random loop number of times\n"
         "Uses all available tables\n"
         "Uses error insert 4013 to make TUP verify table descriptor"){
  INITIALIZER(runCreateAndDropAtRandom);
}
TESTCASE("CreateAndDropWithData", 
	 "Try to create and drop the table when it's filled with data\n"
	 "do this loop number of times\n"){
  INITIALIZER(runCreateAndDropWithData);
}
TESTCASE("CreateAndDropDuring", 
	 "Try to create and drop the table when other thread is using it\n"
	 "do this loop number of times\n"){
  STEP(runCreateAndDropDuring);
  STEP(runUseTableUntilStopped);
}
TESTCASE("CreateInvalidTables", 
	 "Try to create the invalid tables we have defined\n"){ 
  INITIALIZER(runCreateInvalidTables);
}
TESTCASE("CreateTableWhenDbIsFull", 
	 "Try to create a new table when db already is full\n"){ 
  INITIALIZER(runCreateTheTable);
  INITIALIZER(runFillTable);
  INITIALIZER(runCreateTableWhenDbIsFull);
  INITIALIZER(runDropTableWhenDbIsFull);
  FINALIZER(runDropTheTable);
}
TESTCASE("FragmentTypeSingle", 
	 "Create the table with fragment type Single\n"){
  TC_PROPERTY("FragmentType", NdbDictionary::Table::FragSingle);
  INITIALIZER(runTestFragmentTypes);
}
TESTCASE("FragmentTypeAllSmall", 
	 "Create the table with fragment type AllSmall\n"){ 
  TC_PROPERTY("FragmentType", NdbDictionary::Table::FragAllSmall);
  INITIALIZER(runTestFragmentTypes);
}
TESTCASE("FragmentTypeAllMedium", 
	 "Create the table with fragment type AllMedium\n"){ 
  TC_PROPERTY("FragmentType", NdbDictionary::Table::FragAllMedium);
  INITIALIZER(runTestFragmentTypes);
}
TESTCASE("FragmentTypeAllLarge", 
	 "Create the table with fragment type AllLarge\n"){ 
  TC_PROPERTY("FragmentType", NdbDictionary::Table::FragAllLarge);
  INITIALIZER(runTestFragmentTypes);
}
TESTCASE("TemporaryTables", 
	 "Create the table as temporary and make sure it doesn't\n"
	 "contain any data when system is restarted\n"){ 
  INITIALIZER(runTestTemporaryTables);
}
TESTCASE("CreateMaxTables", 
	 "Create tables until db says that it can't create any more\n"){
  TC_PROPERTY("tables", 1000);
  INITIALIZER(runCreateMaxTables);
  INITIALIZER(runDropMaxTables);
}
TESTCASE("PkSizes", 
	 "Create tables with all different primary key sizes.\n"\
	 "Test all data operations insert, update, delete etc.\n"\
	 "Drop table."){
  INITIALIZER(runPkSizes);
}
TESTCASE("StoreFrm", 
	 "Test that a frm file can be properly stored as part of the\n"
	 "data in Dict."){
  INITIALIZER(runStoreFrm);
}
TESTCASE("GetPrimaryKey", 
	 "Test the function NdbDictionary::Column::getPrimaryKey\n"
	 "It should return true only if the column is part of \n"
	 "the primary key in the table"){
  INITIALIZER(runGetPrimaryKey);
}
TESTCASE("StoreFrmError", 
	 "Test that a frm file with too long length can't be stored."){
  INITIALIZER(runStoreFrmError);
}
TESTCASE("NF1", 
	 "Test that create table can handle NF (not master)"){
  INITIALIZER(runNF1);
}
TESTCASE("TableRename",
	 "Test basic table rename"){
  INITIALIZER(runTableRename);
}
TESTCASE("TableRenameNF",
	 "Test that table rename can handle node failure"){
  INITIALIZER(runTableRenameNF);
}
TESTCASE("TableRenameSR",
	 "Test that table rename can handle system restart"){
  INITIALIZER(runTableRenameSR);
}
TESTCASE("DictionaryPerf",
	 ""){
  INITIALIZER(runTestDictionaryPerf);
}
TESTCASE("CreateLogfileGroup", ""){
  INITIALIZER(runCreateLogfileGroup);
}
TESTCASE("CreateTablespace", ""){
  INITIALIZER(runCreateTablespace);
}
TESTCASE("CreateDiskTable", ""){
  INITIALIZER(runCreateDiskTable);
}
TESTCASE("FailAddFragment",
         "Fail add fragment or attribute in ACC or TUP or TUX\n"){
  INITIALIZER(runFailAddFragment);
}
TESTCASE("Restart_NF1",
         "DICT ops during node graceful shutdown (not master)"){
  TC_PROPERTY("Restart_NF_ops", 1);
  TC_PROPERTY("Restart_NF_type", 1);
  STEP(runRestarts);
  STEP(runDictOps);
}
TESTCASE("Restart_NF2",
         "DICT ops during node shutdown abort (not master)"){
  TC_PROPERTY("Restart_NF_ops", 1);
  TC_PROPERTY("Restart_NF_type", 2);
  STEP(runRestarts);
  STEP(runDictOps);
}
TESTCASE("Restart_NR1",
         "DICT ops during node startup (not master)"){
  TC_PROPERTY("Restart_NR_ops", 1);
  STEP(runRestarts);
  STEP(runDictOps);
}
TESTCASE("Restart_NR2",
         "DICT ops during node startup with crash inserts (not master)"){
  TC_PROPERTY("Restart_NR_ops", 1);
  TC_PROPERTY("Restart_NR_error", 1);
  STEP(runRestarts);
  STEP(runDictOps);
}
TESTCASE("TableAddAttrs",
	 "Add attributes to an existing table using alterTable()"){
  INITIALIZER(runTableAddAttrs);
}
TESTCASE("TableAddAttrsDuring",
	 "Try to add attributes to the table when other thread is using it\n"
	 "do this loop number of times\n"){
  INITIALIZER(runCreateTheTable);
  STEP(runTableAddAttrsDuring);
  STEP(runUseTableUntilStopped2);
  STEP(runUseTableUntilStopped3);
  FINALIZER(runDropTheTable);
}
TESTCASE("Bug21755",
         ""){
  INITIALIZER(runBug21755);
}
TESTCASE("DictRestart",
         ""){
  INITIALIZER(runDictRestart);
}
TESTCASE("Bug24631",
         ""){
  INITIALIZER(runBug24631);
}
TESTCASE("SchemaTrans",
         "Schema transactions"){
  ALL_TABLES();
  STEP(runSchemaTrans);
}
TESTCASE("Bug29186",
         ""){
  INITIALIZER(runBug29186);
}
TESTCASE("FailCreateHashmap",
         "Fail create hashmap")
{
  INITIALIZER(runFailCreateHashmap);
}
NDBT_TESTSUITE_END(testDict);

int main(int argc, const char** argv){
  ndb_init();
  // Tables should not be auto created
  testDict.setCreateTable(false);
  myRandom48Init(NdbTick_CurrentMillisecond());
  return testDict.execute(argc, argv);
}
