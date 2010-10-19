/* -*- mode: java; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=4:tabstop=4:smarttab:
 *
 *  Copyright (C) 2010 MySQL
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

package com.mysql.cluster.benchmark.tws;

// reusing this enum from ClusterJ
import com.mysql.clusterj.LockMode;


public class TwsDriver extends Driver {

    // benchmark settings
    protected boolean renewConnection;
    protected boolean doJdbc;
    protected boolean doClusterj;
    protected boolean doNdbjtie;
    protected boolean doInsert;
    protected boolean doLookup;
    protected boolean doUpdate;
    protected boolean doDelete;
    protected boolean doSingle;
    protected boolean doBulk;
    protected boolean doBatch;
    protected LockMode lockMode;
    protected int nRows;
    protected int nRuns;

    // benchmark resources
    protected TwsLoad jdbcLoad;
    protected TwsLoad clusterjLoad;
    protected TwsLoad ndbjtieLoad;

    static public void main(String[] args) throws Exception {
        parseArguments(args);
        TwsDriver main = new TwsDriver();
        main.run();
    }

    // ----------------------------------------------------------------------
    // benchmark intializers/finalizers
    // ----------------------------------------------------------------------

    protected void init() throws Exception {
        super.init();

        if (doJdbc) {
            assert (jdbcLoad == null);
            jdbcLoad = new JdbcLoad(this);
            jdbcLoad.init();
        }
        if (doClusterj) {
            assert (clusterjLoad == null);
            clusterjLoad = new ClusterjLoad(this);
            clusterjLoad.init();
        }
        if (doNdbjtie) {
            assert (ndbjtieLoad == null);
            ndbjtieLoad = new NdbjtieLoad(this);
            ndbjtieLoad.init();
        }

        initConnections();
    }

    protected void close() throws Exception {
        closeConnections();

        if (doJdbc) {
            assert (jdbcLoad != null);
            jdbcLoad.close();
            jdbcLoad = null;
        }
        if (doClusterj) {
            assert (clusterjLoad != null);
            clusterjLoad.close();
            clusterjLoad = null;
        }
        if (doNdbjtie) {
            assert (ndbjtieLoad != null);
            ndbjtieLoad.close();
            ndbjtieLoad = null;
        }

        super.close();
    }

    protected void initProperties() {
        super.initProperties();

        out.print("setting tws properties ...");

        final StringBuilder msg = new StringBuilder();
        final String eol = System.getProperty("line.separator");

        renewConnection = parseBoolean("renewConnection", false);
        doJdbc = parseBoolean("doJdbc", false);
        doClusterj = parseBoolean("doClusterj", false);
        doNdbjtie = parseBoolean("doNdbjtie", false);
        doInsert = parseBoolean("doInsert", false);
        doLookup = parseBoolean("doLookup", false);
        doUpdate = parseBoolean("doUpdate", false);
        doDelete = parseBoolean("doDelete", false);
        doSingle = parseBoolean("doSingle", false);
        doBulk = parseBoolean("doBulk", false);
        doBatch = parseBoolean("doBatch", false);

        final String lm = props.getProperty("lockMode");
        try {
            lockMode = (lm == null
                        ? LockMode.READ_COMMITTED : LockMode.valueOf(lm));
        } catch (IllegalArgumentException e) {
            msg.append("[ignored] lockMode:             " + lm + eol);
            lockMode = LockMode.READ_COMMITTED;
        }

        nRows = parseInt("nRows", 256);
        if (nRows < 1) {
            msg.append("[ignored] nRows:            '"
                       + props.getProperty("nRows") + "'" + eol);
            nRows = 256;
        }

        nRuns = parseInt("nRuns", 1);
        if (nRuns < 0) {
            msg.append("[ignored] nRuns:                " + nRuns + eol);
            nRuns = 1;
        }

        if (msg.length() == 0) {
            out.println("      [ok]");
        } else {
            out.println();
            out.print(msg.toString());
        }
    }

    protected void printProperties() {
        super.printProperties();
        out.println();
        out.println("tws settings ...");
        out.println("renewConnection:                " + renewConnection);
        out.println("doJdbc:                         " + doJdbc);
        out.println("doClusterj:                     " + doClusterj);
        out.println("doNdbjtie:                      " + doNdbjtie);
        out.println("doInsert:                       " + doInsert);
        out.println("doLookup:                       " + doLookup);
        out.println("doUpdate:                       " + doUpdate);
        out.println("doDelete:                       " + doDelete);
        out.println("doSingle:                       " + doSingle);
        out.println("doBulk:                         " + doBulk);
        out.println("doBatch:                        " + doBatch);
        out.println("lockMode:                       " + lockMode);
        out.println("nRows:                          " + nRows);
        out.println("nRuns:                          " + nRuns);
    }

    // ----------------------------------------------------------------------
    // benchmark operations
    // ----------------------------------------------------------------------

    protected void runTests() throws Exception {
        //initConnection();

        //assert(rStart <= rEnd && rScale > 1);
        //for (int i = rStart; i <= rEnd; i *= rScale)
        runLoads();

        //closeConnection();
    }

    protected void runLoads() throws Exception {
        if (doJdbc)
            runSeries(jdbcLoad);
        if (doClusterj)
            runSeries(clusterjLoad);
        if (doNdbjtie)
            runSeries(ndbjtieLoad);
    }

    protected void runSeries(TwsLoad load) throws Exception {
        if (nRuns == 0)
            return; // nothing to do

        out.println();
        out.println("------------------------------------------------------------");
        out.print("running " + nRuns + " iterations on load: "
                  + load.getDescriptor());

        for (int i = 0; i < nRuns; i++) {
            out.println();
            out.println("------------------------------------------------------------");
            runOperations(load);
        }

        writeLogBuffers(load.getDescriptor());
        clearLogBuffers();
    }
    
    enum XMode { SINGLE, BULK, BATCH }

    protected void runOperations(TwsLoad load) throws Exception {
        //out.println("running operations ..."
        //            + "          [nRows=" + nRows + "]");

        // log buffers
        if (logRealTime) {
            rtimes.append("nRows=" + nRows);
            ta = 0;
        }
        if (logMemUsage) {
            musage.append("nRows=" + nRows);
            ma = 0;
        }

        // pre-run cleanup
        if (renewConnection) {
            load.closeConnection();
            load.initConnection();
        }
        //clearData(); // not used

        load.runOperations();

        out.println();
        out.println("total");
        if (logRealTime) {
            out.println("tx real time                    " + ta
                        + "\tms");
        }
        if (logMemUsage) {
            out.println("net mem usage                   "
                        + (ma >= 0 ? "+" : "") + ma
                        + "\tKiB");
        }

        // log buffers
        if (logHeader) {
            header.append("\ttotal");
            logHeader = false;
        }
        if (logRealTime) {
            rtimes.append("\t" + ta);
            rtimes.append(endl);
        }
        if (logMemUsage) {
            musage.append("\t" + ma);
            musage.append(endl);
        }
    }

    // ----------------------------------------------------------------------
    // datastore operations
    // ----------------------------------------------------------------------

    protected void initConnections() throws Exception {
        if (doJdbc) {
            assert (jdbcLoad != null);
            jdbcLoad.initConnection();
        }
        if (doClusterj) {
            assert (clusterjLoad != null);
            clusterjLoad.initConnection();
        }
        if (doNdbjtie) {
            assert (ndbjtieLoad != null);
            ndbjtieLoad.initConnection();
        }
    }

    protected void closeConnections() throws Exception {
        if (doJdbc) {
            assert (jdbcLoad != null);
            jdbcLoad.closeConnection();
        }
        if (doClusterj) {
            assert (clusterjLoad != null);
            clusterjLoad.closeConnection();
        }
        if (doNdbjtie) {
            assert (ndbjtieLoad != null);
            ndbjtieLoad.closeConnection();
        }
    }

    //abstract protected void clearPersistenceContext() throws Exception;
    //abstract protected void clearData() throws Exception;
}
