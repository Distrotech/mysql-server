/*
   Copyright (C) 2009-2010 Sun Microsystems Inc.
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

package com.mysql.clusterj.core;

import com.mysql.clusterj.ClusterJDatastoreException;
import com.mysql.clusterj.ClusterJException;
import com.mysql.clusterj.ClusterJFatalInternalException;
import com.mysql.clusterj.ClusterJUserException;
import com.mysql.clusterj.LockMode;
import com.mysql.clusterj.Query;
import com.mysql.clusterj.Transaction;

import com.mysql.clusterj.core.spi.DomainTypeHandler;
import com.mysql.clusterj.core.spi.ValueHandler;

import com.mysql.clusterj.core.query.QueryDomainTypeImpl;
import com.mysql.clusterj.core.query.QueryBuilderImpl;
import com.mysql.clusterj.core.query.QueryImpl;

import com.mysql.clusterj.core.spi.SessionSPI;

import com.mysql.clusterj.core.store.ClusterTransaction;
import com.mysql.clusterj.core.store.Db;
import com.mysql.clusterj.core.store.Dictionary;
import com.mysql.clusterj.core.store.Index;
import com.mysql.clusterj.core.store.IndexOperation;
import com.mysql.clusterj.core.store.IndexScanOperation;
import com.mysql.clusterj.core.store.Operation;
import com.mysql.clusterj.core.store.PartitionKey;
import com.mysql.clusterj.core.store.ResultData;
import com.mysql.clusterj.core.store.ScanOperation;
import com.mysql.clusterj.core.store.Table;

import com.mysql.clusterj.core.util.I18NHelper;
import com.mysql.clusterj.core.util.Logger;
import com.mysql.clusterj.core.util.LoggerFactoryService;

import com.mysql.clusterj.query.QueryBuilder;
import com.mysql.clusterj.query.QueryDefinition;
import com.mysql.clusterj.query.QueryDomainType;

import java.util.ArrayList;
import java.util.BitSet;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

/**
 * This class implements Session, the main user interface to ClusterJ.
 * It also implements SessionSPI, the main component interface.
 */
public class SessionImpl implements SessionSPI, CacheManager, StoreManager {

    /** My message translator */
    static final I18NHelper local = I18NHelper.getInstance(SessionImpl.class);

    /** My logger */
    static final Logger logger = LoggerFactoryService.getFactory().getInstance(SessionImpl.class);

    /** My Factory. */
    protected SessionFactoryImpl factory;

    /** Db: one per session. */
    protected Db db;

    /** Dictionary: one per session. */
    protected Dictionary dictionary;

    /** One transaction at a time. */
    protected TransactionImpl transactionImpl;

    /** The partition key */
    protected PartitionKey partitionKey = null;

    /** Rollback only status */
    protected boolean rollbackOnly = false;

    /** The underlying ClusterTransaction */
    protected ClusterTransaction clusterTransaction;

    /** The transaction id to join */
    protected String joinTransactionId = null;

    /** The properties for this session */
    protected Map properties;

    /** Flags for iterating a scan */
    protected final int RESULT_READY = 0;
    protected final int SCAN_FINISHED = 1;
    protected final int CACHE_EMPTY = 2;

    /** The list of objects changed since the last flush */
    protected List<StateManager> changeList = new ArrayList<StateManager>();

    /** The transaction state of this session. */
    protected TransactionState transactionState;

    /** The exception state of an internal transaction. */
    private ClusterJException transactionException;

    /** Nested auto transaction counter. */
    protected int nestedAutoTransactionCounter = 0;

    /** Number of retries for retriable exceptions */
    // TODO get this from properties
    protected int numberOfRetries = 5;

    /** The lock mode for read operations */
    private LockMode lockmode = LockMode.READ_COMMITTED;

    /** Create a SessionImpl with factory, properties, Db, and dictionary
     */
    SessionImpl(SessionFactoryImpl factory, Map properties, 
            Db db, Dictionary dictionary) {
        this.factory = factory;
        this.db = db;
        this.dictionary = dictionary;
        this.properties = properties;
        transactionImpl = new TransactionImpl(this);
        transactionState = transactionStateNotActive;
    }

    /** Create a query from a query definition.
     * 
     * @param qd the query definition
     * @return the query
     */
    public <T> Query<T> createQuery(QueryDefinition<T> qd) {
        if (!(qd instanceof QueryDomainTypeImpl)) {
            throw new ClusterJUserException(
                    local.message("ERR_Exception_On_Method", "createQuery"));
        }
        return new QueryImpl<T>(this, (QueryDomainTypeImpl<T>)qd);
    }

    /** Find an instance by its class and primary key.
     * If there is a compound primary key, the key is an Object[] containing
     * all of the primary key fields in order of declaration in annotations.
     * 
     * @param cls the class
     * @param key the primary key
     * @return the instance
     */
    public <T> T find(Class<T> cls, Object key) {
        DomainTypeHandler<T> domainTypeHandler = getDomainTypeHandler(cls);
        T instance = (T) factory.newInstance(cls, dictionary);
        ValueHandler keyHandler = domainTypeHandler.createKeyValueHandler(key);
        ValueHandler instanceHandler = domainTypeHandler.getValueHandler(instance);
        // initialize from the database using the key
        return (T) initializeFromDatabase(
                domainTypeHandler, instance, instanceHandler, keyHandler);
    }

    /** Initialize fields from the database. The keyHandler must
     * contain the primary keys, none of which can be null.
     * The instanceHandler, which may be null, manages the values
     * of the instance. If it is null, both the instanceHandler and the
     * instance are created if the instance exists in the database.
     * The instance, which may be null, is the domain instance that is
     * returned after loading the values from the database.
     *
     * @param domainTypeHandler domain type handler for the class
     * @param keyHandler the primary key handler
     * @param instanceHandler the handler for the instance
     * (may be null if not yet initialized)
     * @param instance the instance (may be null)
     * @return the instance with fields initialized from the database
     */
    public <T> T initializeFromDatabase(DomainTypeHandler<T> domainTypeHandler,
            T instance,
            ValueHandler instanceHandler, ValueHandler keyHandler) {
        startAutoTransaction();
        setPartitionKey(domainTypeHandler, keyHandler);
        try {
            ResultData rs = selectUnique(domainTypeHandler, keyHandler, null);
            if (rs.next()) {
                // we have a result; initialize the instance
                if (instanceHandler == null) {
                    if (logger.isDetailEnabled()) logger.detail("Creating instanceHandler for class " + domainTypeHandler.getName() + " table: " + domainTypeHandler.getTableName() + keyHandler.pkToString(domainTypeHandler));
                    // we need both a new instance and its handler
                    instance = domainTypeHandler.newInstance();
                    instanceHandler = domainTypeHandler.getValueHandler(instance);
                } else if (instance == null) {
                if (logger.isDetailEnabled()) logger.detail("Creating instance for class " + domainTypeHandler.getName() + " table: " + domainTypeHandler.getTableName() + keyHandler.pkToString(domainTypeHandler));
                    // we have a handler but no instance
                    instance = domainTypeHandler.getInstance(instanceHandler);
                }
                // found the instance in the datastore
                // put the results into the instance
                domainTypeHandler.objectSetValues(rs, instanceHandler);
                // set the cache manager to track updates
                domainTypeHandler.objectSetCacheManager(this, instanceHandler);
                // reset modified bits in instance
                domainTypeHandler.objectResetModified(instanceHandler);
            } else {
                if (logger.isDetailEnabled()) logger.detail("No instance found in database for class " + domainTypeHandler.getName() + " table: " + domainTypeHandler.getTableName() + keyHandler.pkToString(domainTypeHandler));
                // no instance found in database
                endAutoTransaction();
                return null;
            }
        } catch (ClusterJException ex) {
            failAutoTransaction();
            throw ex;
        }
        endAutoTransaction();
        return instance;
    }

    /** If a transaction is already enlisted, ignore. Otherwise, set
     * the partition key based on the key handler.
     * @param domainTypeHandler the domain type handler
     * @param keyHandler the value handler that holds the key values
     */
    private void setPartitionKey(DomainTypeHandler<?> domainTypeHandler,
            ValueHandler keyHandler) {
        if (!isEnlisted()) {
            // there is still time to set the partition key
            PartitionKey partitionKey = 
                domainTypeHandler.createPartitionKey(keyHandler);
            clusterTransaction.setPartitionKey(partitionKey);
        }
    }

    /** Create an instance of a class to be persisted.
     * 
     * @param cls the class
     * @return a new instance that can be used with makePersistent
     */
    public <T> T newInstance(Class<T> cls) {
        DomainTypeHandler domainTypeHandler = getDomainTypeHandler(cls);
        return factory.newInstance(cls, dictionary);
    }

    /** Make an instance persistent.
     * 
     * @param object the instance
     * @return the instance
     */
    public <T> T makePersistent(T object) {
        if (object == null) {
            return null;
        }
        DomainTypeHandler domainTypeHandler = getDomainTypeHandler(object);
        ValueHandler valueHandler = domainTypeHandler.getValueHandler(object);
        insert(domainTypeHandler, valueHandler);
        return object;
    }

    public void insert(
            DomainTypeHandler<?> domainTypeHandler, ValueHandler valueHandler) {
        startAutoTransaction();
        setPartitionKey(domainTypeHandler, valueHandler);
        Operation op = null;
        Table storeTable = null;
        try {
            storeTable = domainTypeHandler.getStoreTable();
            op = clusterTransaction.getInsertOperation(storeTable);
            // set all values in the operation, keys first
            domainTypeHandler.operationSetKeys(valueHandler, op);
            domainTypeHandler.operationSetValuesExcept(valueHandler, op, "PRIMARY");
            // reset modified bits in instance
            domainTypeHandler.objectResetModified(valueHandler);
        } catch (ClusterJUserException cjuex) {
            failAutoTransaction();
            throw cjuex;
        } catch (ClusterJException cjex) {
            failAutoTransaction();
            logger.error(local.message("ERR_Insert", storeTable.getName()));
            throw new ClusterJException(
                    local.message("ERR_Insert", storeTable.getName()), cjex);
        } catch (RuntimeException rtex) {
            failAutoTransaction();
            logger.error(local.message("ERR_Insert", storeTable.getName()));
            throw new ClusterJException(
                    local.message("ERR_Insert", storeTable.getName()), rtex);
        }
        endAutoTransaction();
    }

    /** Make a number of instances persistent.
     * 
     * @param instances a Collection or array of objects to persist
     * @return a Collection or array with the same order of iteration
     */
    public Iterable makePersistentAll(Iterable instances) {
        startAutoTransaction();
        List<Object> result = new ArrayList<Object>();
        for (Object instance:instances) {
            result.add(makePersistent(instance));
            }
        endAutoTransaction();
        return result;
    }

    /** Remove an instance from the database. Only the key field(s) 
     * are used to identify the instance.
     * 
     * @param object the instance to remove from the database
     */
    public void deletePersistent(Object object) {
        if (object == null) {
            return;
        }
        DomainTypeHandler domainTypeHandler = getDomainTypeHandler(object);
        ValueHandler valueHandler = domainTypeHandler.getValueHandler(object);
        delete(domainTypeHandler, valueHandler);
    }

    public void delete(DomainTypeHandler domainTypeHandler, ValueHandler valueHandler) {
        startAutoTransaction();
        Table storeTable = domainTypeHandler.getStoreTable();
        setPartitionKey(domainTypeHandler, valueHandler);
        Operation op = null;
        try {
            op = clusterTransaction.getDeleteOperation(storeTable);
            domainTypeHandler.operationSetKeys(valueHandler, op);
        } catch (ClusterJException ex) {
            failAutoTransaction();
            throw new ClusterJException(
                    local.message("ERR_Delete", storeTable.getName()), ex);
        }
        endAutoTransaction();
    }

    /** Delete the instances corresponding to the parameters.
     * @param objects the objects to delete
     */
    public void deletePersistentAll(Iterable objects) {
        startAutoTransaction();
        for (Iterator it = objects.iterator(); it.hasNext();) {
            deletePersistent(it.next());
        }
        endAutoTransaction();
    }

    /** Delete all instances of the parameter class.
     * @param cls the class of instances to delete
     */
    public <T> int deletePersistentAll(Class<T> cls) {
        DomainTypeHandler domainTypeHandler = getDomainTypeHandler(cls);
        return deletePersistentAll(domainTypeHandler);
    }

    /** Delete all instances of the parameter domainTypeHandler.
     * @param domainTypeHandler the domainTypeHandler of instances to delete
     */
    public int deletePersistentAll(DomainTypeHandler domainTypeHandler) {
        startAutoTransaction();
        // cannot use early autocommit optimization here
        clusterTransaction.setAutocommit(false);
        Table storeTable = domainTypeHandler.getStoreTable();
        ScanOperation op = null;
        int count = 0;
        int cacheCount = 0;
        try {
            op = clusterTransaction.getSelectScanOperationLockModeExclusiveScanFlagKeyInfo(storeTable);
//                    Operation.LockMode.LM_Exclusive,
//                    ScanOperation.ScanFlag.KEY_INFO, 0,0);
            clusterTransaction.executeNoCommit(true, true);
            boolean done = false;
            boolean fetch = true;
            while (!done ) {
                int result = op.nextResult(fetch);
                switch (result) {
                    case RESULT_READY:
                        op.deleteCurrentTuple();
                        ++count;
                        ++cacheCount;
                        fetch = false;
                        break;
                    case SCAN_FINISHED:
                        done = true;
                        if (cacheCount != 0) {
                            clusterTransaction.executeNoCommit(true, true);
                        }
                        op.close();
                        break;
                    case CACHE_EMPTY:
                        clusterTransaction.executeNoCommit(true, true);
                        cacheCount = 0;
                        fetch = true;
                        break;
                    default: 
                        throw new ClusterJException(
                                local.message("ERR_Next_Result_Illegal", result));
                }
            }
        } catch (ClusterJException ex) {
            failAutoTransaction();
            // TODO add table name to the error message
            throw new ClusterJException(
                    local.message("ERR_Select_Scan"), ex);
        }
        endAutoTransaction();
        return count;
    }

    /** Select a single row from the database. Only the fields requested
     * will be selected. A transaction must be active (either via begin
     * or startAutoTransaction).
     *
     * @param domainTypeHandler the domainTypeHandler to be selected
     * @param keyHandler the key supplier for the select
     * @param fields the fields to select; null to select all fields
     * @return the ResultData from the database
     */
    public ResultData selectUnique(DomainTypeHandler domainTypeHandler,
            ValueHandler keyHandler, BitSet fields) {
        assertActive();
        setPartitionKey(domainTypeHandler, keyHandler);
        Table storeTable = domainTypeHandler.getStoreTable();
        // perform a single select by key operation
        Operation op = clusterTransaction.getSelectOperation(storeTable);
        // set the keys into the operation
        domainTypeHandler.operationSetKeys(keyHandler, op);
        // set the expected columns into the operation
        domainTypeHandler.operationGetValues(op);
        // execute the select and get results
        ResultData rs = op.resultData();
        return rs;
    }

    /** Update an instance in the database. The key field(s)
     * are used to identify the instance; modified fields change the
     * values in the database.
     *
     * @param object the instance to update in the database
     */
    public void updatePersistent(Object object) {
        if (object == null) {
            return;
        }
        DomainTypeHandler domainTypeHandler = getDomainTypeHandler(object);
        if (logger.isDetailEnabled()) logger.detail("UpdatePersistent on object " + object);
        ValueHandler valueHandler = domainTypeHandler.getValueHandler(object);
        update(domainTypeHandler, valueHandler);
    }

    public void update(DomainTypeHandler domainTypeHandler, ValueHandler valueHandler) {
        startAutoTransaction();
        setPartitionKey(domainTypeHandler, valueHandler);
        Table storeTable = null;
        try {
            storeTable = domainTypeHandler.getStoreTable();
            Operation op = null;
            op = clusterTransaction.getUpdateOperation(storeTable);
            domainTypeHandler.operationSetKeys(valueHandler, op);
            domainTypeHandler.operationSetModifiedValuesExcept(valueHandler, op, "PRIMARY");
            if (logger.isDetailEnabled()) logger.detail("Updated object " +
                    valueHandler);
        } catch (ClusterJException ex) {
            failAutoTransaction();
            throw new ClusterJException(
                    local.message("ERR_Update", storeTable.getName()) ,ex);
        }
        endAutoTransaction();
    }

    /** Update the instances corresponding to the parameters.
     * @param objects the objects to update
     */
    public void updatePersistentAll(Iterable objects) {
        startAutoTransaction();
        for (Iterator it = objects.iterator(); it.hasNext();) {
            updatePersistent(it.next());
        }
        endAutoTransaction();
    }

    /** Save the instance even if it does not exist.
     * @param instance the instance to save
     */
    public <T> T savePersistent(T instance) {
        DomainTypeHandler domainTypeHandler = getDomainTypeHandler(instance);
        if (logger.isDetailEnabled()) logger.detail("UpdatePersistent on object " + instance);
        ValueHandler valueHandler = domainTypeHandler.getValueHandler(instance);
        startAutoTransaction();
        setPartitionKey(domainTypeHandler, valueHandler);
        Table storeTable = null;
        try {
            storeTable = domainTypeHandler.getStoreTable();
            Operation op = null;
            op = clusterTransaction.getWriteOperation(storeTable);
            domainTypeHandler.operationSetKeys(valueHandler, op);
            domainTypeHandler.operationSetModifiedValuesExcept(valueHandler, op, "PRIMARY");
            if (logger.isDetailEnabled()) logger.detail("Wrote object " +
                    valueHandler);
        } catch (ClusterJException ex) {
            failAutoTransaction();
            throw new ClusterJException(
                    local.message("ERR_Write", storeTable.getName()) ,ex);
        }
        endAutoTransaction();
        return instance;
    }

    /** Save the instances even if they do not exist.
     * @param instances
     */
    public Iterable savePersistentAll(Iterable instances) {
        List<Object> result = new ArrayList<Object>();
        startAutoTransaction();
        for (Iterator it = instances.iterator(); it.hasNext();) {
            result.add(savePersistent(it.next()));
        }
        endAutoTransaction();
        return result;
    }

    /** Get the current transaction.
     * 
     * @return the transaction
     */
    public Transaction currentTransaction() {
        return transactionImpl;
    }

    /** Close this session and deallocate all resources.
     * 
     */
    public void close() {
        if (clusterTransaction != null) {
            clusterTransaction.close();
            clusterTransaction = null;
        }
        if (db != null) {
            db.close();
            db = null;
        }
    }

    public boolean isClosed() {
        return db==null;
    }

    /** Assert this session is not yet closed. */
    protected void assertNotClosed() {
        if (isClosed()) {
            throw new ClusterJUserException(
                    local.message("ERR_Session_Closed"));
        }
    }

    /** Begin the current transaction.
     * 
     */
    public void begin() {
        if (logger.isDebugEnabled()) logger.debug("begin transaction.");
        transactionState = transactionState.begin();
        handleTransactionException();
    }

    /** Internally begin the transaction.
     * Called by transactionState.begin().
     */
    protected void internalBegin() {
        try {
            clusterTransaction = db.startTransaction(joinTransactionId);
            // if a transaction has already begun, tell the cluster transaction about the key
            if (partitionKey != null) {
                clusterTransaction.setPartitionKey(partitionKey);
            }
        } catch (ClusterJException ex) {
            throw new ClusterJException(
                    local.message("ERR_Ndb_Start"), ex);
        }
    }

    /** Commit the current transaction.
     * 
     */
    public void commit() {
        if (logger.isDebugEnabled()) logger.debug("commit transaction.");
        transactionState = transactionState.commit();
        handleTransactionException();
    }

    /** Internally commit the transaction.
     * Called by transactionState.commit().
     */
    protected void internalCommit() {
        if (rollbackOnly) {
            try {
                internalRollback();
                throw new ClusterJException(
                        local.message("ERR_Transaction_Rollback_Only"));
            } catch (ClusterJException ex) {
                throw new ClusterJException(
                        local.message("ERR_Transaction_Rollback_Only"), ex);
            }
        }
        try {
            clusterTransaction.executeCommit(true, true);
        } finally {
            // always close the transaction
            clusterTransaction.close();
            clusterTransaction = null;
            partitionKey = null;
        }
    }

    /** Roll back the current transaction.
     *
     */
    public void rollback() {
        if (logger.isDebugEnabled()) logger.debug("roll back transaction.");
        transactionState = transactionState.rollback();
        handleTransactionException();
    }

    /** Internally roll back the transaction.
     * Called by transactionState.rollback() and
     * transactionState.commit() if the transaction is marked for rollback.
     *
     */
    protected void internalRollback() {
        try {
                clusterTransaction.executeRollback();
        } catch (ClusterJException ex) {
            throw new ClusterJException(
                    local.message("ERR_Transaction_Execute", "rollback"), ex);
        } finally {
            if (clusterTransaction != null) {
                clusterTransaction.close();
            }
            clusterTransaction = null;
            partitionKey = null;
        }
    }

    /** Start a transaction if there is not already an active transaction.
     * Throw a ClusterJException if there is any problem.
     */
    public void startAutoTransaction() {
        if (logger.isDebugEnabled()) logger.debug("start AutoTransaction");
        transactionState = transactionState.start();
        handleTransactionException();
    }

    /** End an auto transaction if it was started.
     * Throw a ClusterJException if there is any problem.
     */
    public void endAutoTransaction() {
        if (logger.isDebugEnabled()) logger.debug("end AutoTransaction");
        transactionState = transactionState.end();
        handleTransactionException();
    }

    /** Fail an auto transaction if it was started.
     * Throw a ClusterJException if there is any problem.
     */
    public void failAutoTransaction() {
        if (logger.isDebugEnabled()) logger.debug("fail AutoTransaction");
        transactionState = transactionState.fail();
    }

    protected void handleTransactionException() {
        if (transactionException == null) {
            return;
        } else {
            ClusterJException ex = transactionException;
            transactionException = null;
            throw ex;
        }
    }

    /** Mark the current transaction as rollback only.
     *
     */
    public void setRollbackOnly() {
        rollbackOnly = true;
    }

    /** Is the current transaction marked for rollback only?
     * @return true if the current transaction is marked for rollback only
     */
    public boolean getRollbackOnly() {
        return rollbackOnly;
    }

    /** Manage the state of the transaction associated with this
     * StateManager.
     */
    protected interface TransactionState {
        boolean isActive();

        TransactionState begin();
        TransactionState commit();
        TransactionState rollback();

        TransactionState start();
        TransactionState end();
        TransactionState fail();
    }

    /** This represents the state of Transaction Not Active. */
    protected TransactionState transactionStateNotActive =
            new TransactionState() {

        public boolean isActive() {
            return false;
        }

        public TransactionState begin() {
            try {
                internalBegin();
                return transactionStateActive;
            } catch (ClusterJException ex) {
                transactionException = ex;
                return transactionStateNotActive;
            }
        }

        public TransactionState commit() {
            transactionException = new ClusterJUserException(
                    local.message("ERR_Transaction_Must_Be_Active_For_Method",
                    "commit"));
            return transactionStateNotActive;
        }

        public TransactionState rollback() {
            transactionException = new ClusterJUserException(
                    local.message("ERR_Transaction_Must_Be_Active_For_Method",
                    "rollback"));
            return transactionStateNotActive;
        }

        public TransactionState start() {
            try {
                internalBegin();
                clusterTransaction.setAutocommit(true);
                nestedAutoTransactionCounter = 1;
                return transactionStateAutocommit;
            } catch (ClusterJException ex) {
                transactionException = ex;
                return transactionStateNotActive;
            }
        }

        public TransactionState end() {
            throw new ClusterJFatalInternalException(
                    local.message("ERR_Transaction_Auto_Start", "end"));
        }

        public TransactionState fail() {
            throw new ClusterJFatalInternalException(
                    local.message("ERR_Transaction_Auto_Start", "end"));
        }

    };

    /** This represents the state of Transaction Active. */
    protected TransactionState transactionStateActive =
            new TransactionState() {

        public boolean isActive() {
            return true;
        }

        public TransactionState begin() {
            transactionException = new ClusterJUserException(
                    local.message("ERR_Transaction_Must_Not_Be_Active_For_Method",
                    "begin"));
            return transactionStateActive;
        }

        public TransactionState commit() {
            try {
                // flush unwritten changes
                flush();
                internalCommit();
            } catch (ClusterJException ex) {
                transactionException = ex;
            }
            return transactionStateNotActive;
        }

        public TransactionState rollback() {
            try {
                internalRollback();
                return transactionStateNotActive;
            } catch (ClusterJException ex) {
                transactionException = ex;
                return transactionStateNotActive;
            }
        }

        public TransactionState start() {
            // nothing to do
            return transactionStateActive;
        }

        public TransactionState end() {
            // nothing to do
            return transactionStateActive;
        }

        public TransactionState fail() {
            // nothing to do
            return transactionStateActive;
        }

    };

    protected TransactionState transactionStateAutocommit =
            new TransactionState() {

        public boolean isActive() {
            return true;
        }

        public TransactionState begin() {
            throw new ClusterJFatalInternalException(
                    local.message("ERR_Transaction_Auto_End", "begin"));
        }

        public TransactionState commit() {
            throw new ClusterJFatalInternalException(
                    local.message("ERR_Transaction_Auto_End", "commit"));
        }

        public TransactionState rollback() {
            throw new ClusterJFatalInternalException(
                    local.message("ERR_Transaction_Auto_End", "rollback"));
        }

        public TransactionState start() {
            // nested start; increment counter
            nestedAutoTransactionCounter++;
            return transactionStateAutocommit;
        }

        public TransactionState end() {
            if (--nestedAutoTransactionCounter > 0) {
                return transactionStateAutocommit;
            } else if (nestedAutoTransactionCounter == 0) {
                try {
                    internalCommit();
                } catch (ClusterJException ex) {
                    transactionException = ex;
                }
                return transactionStateNotActive;
            } else {
            throw new ClusterJFatalInternalException(
                    local.message("ERR_Transaction_Auto_Start", "end"));
            }
        }

        public TransactionState fail() {
            try {
                nestedAutoTransactionCounter = 0;
                internalRollback();
                return transactionStateNotActive;
            } catch (ClusterJException ex) {
                // ignore failures caused by internal rollback
                return transactionStateNotActive;
            }
        }

    };

    /** Get the domain type handler for an instance.
     * 
     * @param object the instance for which to get the domain type handler
     * @return the domain type handler
     */
    protected synchronized DomainTypeHandler getDomainTypeHandler(Object object) {
        DomainTypeHandler domainTypeHandler =
                factory.getDomainTypeHandler(object, dictionary);
        return domainTypeHandler;
    }

    /** Get the domain type handler for a class.
     * 
     * @param cls the class
     * @return the domain type handler
     */
    public synchronized <T> DomainTypeHandler<T> getDomainTypeHandler(Class<T> cls) {
        DomainTypeHandler<T> domainTypeHandler =
                factory.getDomainTypeHandler(cls, dictionary);
        return domainTypeHandler;
    }

    public Dictionary getDictionary() {
        return dictionary;
    }

    /** Is there an active transaction.
     * 
     * @return true if there is an active transaction
     */
    boolean isActive() {
        return transactionState.isActive();
    }

    /** Is the transaction enlisted. A transaction is enlisted if and only if
     * an operation has been defined that requires an ndb transaction to be
     * started.
     * @return true if the transaction is enlisted
     */
    public boolean isEnlisted() {
        return clusterTransaction==null?false:clusterTransaction.isEnlisted();
    }

    /** Assert that there is an active transaction (the user has called begin
     * or an autotransaction has begun).
     * Throw a user exception if not.
     */
    private void assertActive() {
        if (!transactionState.isActive()) {
            throw new ClusterJUserException(
                    local.message("ERR_Transaction_Must_Be_Active"));
        }
    }

    /** Assert that there is not an active transaction.
     * Throw a user exception if there is an active transaction.
     * @param methodName the name of the method
     */
    private void assertNotActive(String methodName) {
        if (transactionState.isActive()) {
            throw new ClusterJUserException(
                    local.message("ERR_Transaction_Must_Not_Be_Active_For_Method",
                    methodName));
        }
    }

    /** Create a query from a class.
     * 
     * @param cls the class
     * @return the query
     */
    public Query createQuery(Class cls) {
        throw new UnsupportedOperationException(
                local.message("ERR_NotImplemented"));
    }

    /** Get a query builder.
     * 
     * @return the query builder
     */
    public QueryBuilder getQueryBuilder() {
        return new QueryBuilderImpl(this);
    }

    /** Create an index scan operation for an index and table.
     * 
     * @param storeIndex the index
     * @param storeTable the table
     * @return the index scan operation
     */
    public IndexScanOperation getIndexScanOperation(Index storeIndex, Table storeTable) {
        // TODO make this possible outside a transaction
        assertActive();
        try {
            IndexScanOperation result = clusterTransaction.getSelectIndexScanOperation(storeIndex, storeTable);
            return result;
        } catch (ClusterJException ex) {
            throw new ClusterJException(
                    local.message("ERR_Index_Scan", storeTable.getName(), storeIndex.getName()), ex);
        }
    }

    /** Create a table scan operation for a table.
     *
     * @param storeTable the table
     * @return the table scan operation
     */
    public ScanOperation getTableScanOperation(Table storeTable) {
        assertActive();
        try {
            ScanOperation result = clusterTransaction.getSelectScanOperation(storeTable);
            return result;
        } catch (ClusterJException ex) {
            throw new ClusterJException(
                    local.message("ERR_Table_Scan", storeTable.getName()), ex);
        }
    }

    /** Create an index operation for an index and table.
     *
     * @param storeIndex the index
     * @param storeTable the table
     * @return the index operation
     */
    public IndexOperation getIndexOperation(Index storeIndex, Table storeTable) {
        assertActive();
        try {
            IndexOperation result = clusterTransaction.getSelectUniqueOperation(storeIndex, storeTable);
            return result;
        } catch (ClusterJException ex) {
            throw new ClusterJException(
                    local.message("ERR_Unique_Scan", storeTable.getName(), storeIndex.getName()), ex);
        }
    }

    /** Create a select operation for a table.
     *
     * @param storeTable the table
     * @return the operation
     */
    public Operation getSelectOperation(Table storeTable) {
        assertActive();
        try {
            Operation result = clusterTransaction.getSelectOperation(storeTable);
            return result;
        } catch (ClusterJException ex) {
            throw new ClusterJException(
                    local.message("ERR_Select_Scan", storeTable), ex);
        }
    }

    public void flush() {
        if (logger.isDetailEnabled()) logger.detail("flush changes with changeList: " + changeList);
        if (changeList.isEmpty()) {
            return;
        }
        for (StateManager sm: changeList) {
            sm.flush(this);
        }
        // now flush changes to the back end
        clusterTransaction.executeNoCommit();
        changeList.clear();
    }

    public List getChangeList() {
        return Collections.unmodifiableList(changeList);
    }

    public void persist(Object instance) {
        makePersistent(instance);
    }

    public void remove(Object instance) {
        deletePersistent(instance);
    }

    public void markModified(StateManager instance) {
        changeList.add(instance);
    }

    public void setPartitionKey(Class<?> domainClass, Object key) {
        DomainTypeHandler<?> domainTypeHandler = getDomainTypeHandler(domainClass);
        String tableName = domainTypeHandler.getTableName();
        // if transaction is enlisted, throw a user exception
        if (isEnlisted()) {
            throw new ClusterJUserException(
                    local.message("ERR_Set_Partition_Key_After_Enlistment", tableName));
        }
        // if a partition key has already been set, throw a user exception
        if (this.partitionKey != null) {
            throw new ClusterJUserException(
                    local.message("ERR_Set_Partition_Key_Twice", tableName));
        }
        ValueHandler handler = domainTypeHandler.createKeyValueHandler(key);
        this.partitionKey= domainTypeHandler.createPartitionKey(handler);
        // if a transaction has already begun, tell the cluster transaction about the key
        if (clusterTransaction != null) {
            clusterTransaction.setPartitionKey(partitionKey);
        }
    }

    /** Mark the field in the instance as modified so it is flushed.
     *
     * @param instance the persistent instance
     * @param fieldName the field to mark as modified
     */
    public void markModified(Object instance, String fieldName) {
        DomainTypeHandler domainTypeHandler = getDomainTypeHandler(instance);
        ValueHandler handler = domainTypeHandler.getValueHandler(instance);
        domainTypeHandler.objectMarkModified(handler, fieldName);
    }

    public void executeNoCommit() {
        clusterTransaction.executeNoCommit();
    }

    public <T> QueryDomainType<T> createQueryDomainType(DomainTypeHandler<T> domainTypeHandler) {
        QueryBuilderImpl builder = (QueryBuilderImpl)getQueryBuilder();
        return builder.createQueryDefinition(domainTypeHandler);
    }

    /** Return the coordinatedTransactionId of the current transaction.
     * The transaction might not have been enlisted.
     * @return the coordinatedTransactionId
     */
    public String getCoordinatedTransactionId() {
        return clusterTransaction.getCoordinatedTransactionId();
    }

    /** Set the coordinatedTransactionId for the next transaction. This
     * will take effect as soon as the transaction is enlisted.
     * @param coordinatedTransactionId the coordinatedTransactionId
     */
    public void setCoordinatedTransactionId(String coordinatedTransactionId) {
        clusterTransaction.setCoordinatedTransactionId(coordinatedTransactionId);
    }

    public void setLockMode(LockMode lockmode) {
        this.lockmode = lockmode;
        if (clusterTransaction != null) {
            clusterTransaction.setLockMode(lockmode);
        }
    }

}
