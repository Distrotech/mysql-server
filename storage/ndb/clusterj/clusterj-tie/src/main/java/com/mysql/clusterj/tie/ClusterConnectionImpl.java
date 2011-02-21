/*
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
 */

package com.mysql.clusterj.tie;

import com.mysql.ndbjtie.ndbapi.Ndb;
import com.mysql.ndbjtie.ndbapi.Ndb_cluster_connection;

import com.mysql.clusterj.ClusterJDatastoreException;

import com.mysql.clusterj.core.store.Db;

import com.mysql.clusterj.core.util.I18NHelper;
import com.mysql.clusterj.core.util.Logger;
import com.mysql.clusterj.core.util.LoggerFactoryService;

/**
 *
 */
public class ClusterConnectionImpl
        implements com.mysql.clusterj.core.store.ClusterConnection {

    /** My message translator */
    static final I18NHelper local = I18NHelper.getInstance(ClusterConnectionImpl.class);

    /** My logger */
    static final Logger logger = LoggerFactoryService.getFactory()
            .getInstance(com.mysql.clusterj.core.store.ClusterConnection.class);

    /** Load the ndbjtie system library */
    static {
        loadSystemLibrary("ndbclient");
        // initialize the charset map
        Utility.getCharsetMap();
    }

    /** Ndb_cluster_connection: one per factory. */
    protected Ndb_cluster_connection clusterConnection;

    /** Connect to the MySQL Cluster
     * 
     * @param connectString the connect string
     */
    public ClusterConnectionImpl(String connectString) {
        clusterConnection = Ndb_cluster_connection.create(connectString);
        handleError(clusterConnection, connectString);
    }

    static protected void loadSystemLibrary(String name) {
        String message;
        String path;
        try {
            System.loadLibrary(name);
        } catch (UnsatisfiedLinkError e) {
            path = getLoadLibraryPath();
            message = local.message("ERR_Failed_Loading_Library",
                    name, path, "UnsatisfiedLinkError", e.getLocalizedMessage());
            logger.fatal(message);
            throw e;
        } catch (SecurityException e) {
            path = getLoadLibraryPath();
            message = local.message("ERR_Failed_Loading_Library",
                    name, path, "SecurityException", e.getLocalizedMessage());
            logger.fatal(message);
            throw e;
        }
    }

    /**
     * @return the load library path or the Exception string
     */
    private static String getLoadLibraryPath() {
        String path;
        try {
            path = System.getProperty("java.library.path");
        } catch (Exception ex) {
            path = "<Exception: " + ex.getMessage() + ">";
        }
        return path;
    }


    public void connect(int connectRetries, int connectDelay, boolean verbose) {
        int returnCode = clusterConnection.connect(connectRetries, connectDelay, verbose?1:0);
        handleError(returnCode, clusterConnection);
    }

    public Db createDb(String database, int maxTransactions) {
        Ndb ndb = null;
        // synchronize because create is not guaranteed thread-safe
        synchronized(this) {
            ndb = Ndb.create(clusterConnection, database, "def");
            handleError(ndb, clusterConnection);
        }
        return new DbImpl(ndb, maxTransactions);
    }

    public void waitUntilReady(int connectTimeoutBefore, int connectTimeoutAfter) {
        int returnCode = clusterConnection.wait_until_ready(connectTimeoutBefore, connectTimeoutAfter);
        handleError(returnCode, clusterConnection);
    }

    protected static void handleError(int returnCode, Ndb_cluster_connection clusterConnection) {
        if (returnCode >= 0) {
            return;
        } else {
            throwError(returnCode, clusterConnection);
        }
    }

    protected static void handleError(Object object, Ndb_cluster_connection clusterConnection) {
        if (object != null) {
            return;
        } else {
            throwError(null, clusterConnection);
        }
    }

    protected static void handleError(Ndb_cluster_connection clusterConnection, String connectString) {
        if (clusterConnection == null) {
            throw new ClusterJDatastoreException(
                    local.message("ERR_Cannot_Create_Cluster_Connection", connectString));
        }
    }

    protected static void throwError(Object returnCode, Ndb_cluster_connection clusterConnection) {
        String message = clusterConnection.get_latest_error_msg();
        int errorCode = clusterConnection.get_latest_error();
        String msg = local.message("ERR_NdbError", returnCode, errorCode, message);
        throw new ClusterJDatastoreException(msg);
    }

}
