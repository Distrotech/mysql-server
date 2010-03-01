/* -*- mode: java; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=4:tabstop=4:smarttab:
 *
 *  Copyright (C) 2008 MySQL
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

package com.mysql.cluster.crund;

import com.mysql.clusterj.ClusterJHelper;
import com.mysql.clusterj.Constants;
import com.mysql.clusterj.Query;
import com.mysql.clusterj.Session;
import com.mysql.clusterj.SessionFactory;
import com.mysql.clusterj.query.QueryDomainType;
import com.mysql.clusterj.query.Predicate;
import com.mysql.clusterj.query.QueryBuilder;
import java.util.Collection;
import java.util.Iterator;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * A benchmark implementation against a ClusterJ database.
 */
public class ClusterjLoad extends Driver {

    // ClusterJ connection
    protected String mgmdConnect;
    protected SessionFactory sessionFactory;
    protected Session session;

    protected abstract class ClusterjOp extends Op {
        public ClusterjOp(String name) {
            super(name);
        }

        public void init() {}

        public void close() {}
    };

    @Override
    protected void initProperties() {
        super.initProperties();

        // check required properties
        mgmdConnect
            = props.getProperty(Constants.PROPERTY_CLUSTER_CONNECTSTRING);
        descr = "->ClusterJ->NDB JTie->NDBAPI(" + mgmdConnect + ")";
    }

    @Override
    protected void printProperties() {
        super.printProperties();
        out.println("ndb.mgmdConnect             " + mgmdConnect);
        for (Iterator<Map.Entry<Object,Object>> i
                 = props.entrySet().iterator(); i.hasNext();) {
            Map.Entry<Object,Object> e = i.next();
            final String k = (String)e.getKey();
            if (k.startsWith("com.mysql.clusterj")) {
                final StringBuilder s = new StringBuilder("..");
                s.append(k, 10, k.length());
                while (s.length() < 27) s.append(' ');
                out.println(s + " " + e.getValue());
            }
        }
    }

    @Override
    protected void init() throws Exception {
        super.init();
        // load native library (better diagnostics doing it explicitely)
        out.println();
        //loadSystemLibrary("ndbj");

        // instantiate NDB cluster singleton

        out.println();
        out.print("creating SessionFactory ...");
        out.flush();
        sessionFactory = ClusterJHelper.getSessionFactory(props);
        out.println(" [SessionFactory: 1]");
    }

    @Override
    protected void close() throws Exception {
        out.print("closing SessionFactory ...");
        out.flush();
        if (sessionFactory != null)
            sessionFactory.close();
        sessionFactory = null;
        out.println("  [ok]");
        super.close();
    }

    protected void initConnection() {
        out.print("creating Session ...");
        out.flush();
        session = sessionFactory.getSession();
        out.println("        [Session: 1]");
    }

    protected void closeConnection() {
        out.print("closing Session ...");
        out.flush();
        if (session != null)
            session.close();
        session = null;
        out.println("         [ok]");
    }

    protected int checkFields(IA o) {
        final int cint = o.getCint();
        final long clong = o.getClong();
        verify(clong == cint);
        final float cfloat = o.getCfloat();
        verify(cfloat == cint);
        final double cdouble = o.getCdouble();
        verify(cdouble == cint);
        return cint;
    }

    protected int checkFields(IB0 o) {
        final int cint = o.getCint();
        final long clong = o.getClong();
        verify(clong == cint);
        final float cfloat = o.getCfloat();
        verify(cfloat == cint);
        final double cdouble = o.getCdouble();
        verify(cdouble == cint);
        return cint;
    }

    protected void initOperations() {
        out.print("initializing operations ...");
        out.flush();

        ops.add(
            new ClusterjOp("insA") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countA; i++) {
                        final IA o = session.newInstance(IA.class);
                        assert o != null;
                        o.setId(i);
                        session.persist(o);
                    }
                }
            });

        ops.add(
            new ClusterjOp("insB0") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countB; i++) {
                        final IB0 o = session.newInstance(IB0.class);
                        assert o != null;
                        o.setId(i);
                        o.setCvarbinary_def(null);
                        session.persist(o);
                    }
                }
            });

        ops.add(
            new ClusterjOp("setAByPK") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countA; i++) {
                        // XXX blind update not working
                        final IA o = session.find(IA.class, i);
                        //final IA o = session.newInstance(IA.class);
                        assert o != null;
                        o.setId((int)i);
                        o.setCint((int)i);
                        o.setClong((long)i);
                        o.setCfloat((float)i);
                        o.setCdouble((double)i);
                        session.updatePersistent(o);
                    }
                }
            });

        ops.add(
            new ClusterjOp("setB0ByPK") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countB; i++) {
                        // XXX blind update not working
                        final IB0 o = session.find(IB0.class, i);
                        //final IB0 o = session.newInstance(IB0.class);
                        assert o != null;
                        o.setId((int)i);
                        o.setCint((int)i);
                        o.setClong((long)i);
                        o.setCfloat((float)i);
                        o.setCdouble((double)i);
                        session.updatePersistent(o);
                    }
                }
            });

        ops.add(
            new ClusterjOp("getAByPK") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countA; i++) {
                        final IA o = session.find(IA.class, i);
                        assert o != null;
                        final int id = o.getId();
                        verify(id == i);
                        final int j = checkFields(o);
                        verify(j == id);
                    }
                }
            });

        ops.add(
            new ClusterjOp("getB0ByPK") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countB; i++) {
                        final IB0 o = session.find(IB0.class, i);
                        assert o != null;
                        final int id = o.getId();
                        verify(id == i);
                        final int j = checkFields(o);
                        verify(j == id);
                    }
                }
            });

        for (int i = 0, l = 1; l <= maxVarbinaryBytes; l *= 10, i++) {
            final byte[] b = bytes[i];
            assert l == b.length;

            ops.add(
                new ClusterjOp("setVarbinary" + l) {
                    public void run(int countA, int countB) {
                        for (int i = 0; i < countB; i++) {
                            // XXX blind update not working
                            final IB0 o = session.find(IB0.class, i);
                            //final IB0 o = session.newInstance(IB0.class);
                            assert o != null;
                            o.setCvarbinary_def(b);
                            session.updatePersistent(o);
                        }
                    }
                });

            ops.add(
                new ClusterjOp("getVarbinary" + l) {
                    public void run(int countA, int countB) {
                        for (int i = 0; i < countB; i++) {
                            final IB0 o = session.find(IB0.class, i);
                            assert o != null;
                            verify(Arrays.equals(b, o.getCvarbinary_def()));
                        }
                    }
                });

            ops.add(
                new ClusterjOp("clearVarbinary" + l) {
                    public void run(int countA, int countB) {
                        for (int i = 0; i < countB; i++) {
                            // XXX blind update not working
                            final IB0 o = session.find(IB0.class, i);
                            //final IB0 o = session.newInstance(IB0.class);
                            assert o != null;
                            o.setCvarbinary_def(null);
                            session.updatePersistent(o);
                        }
                    }
                });
        }        
        
        for (int i = 0, l = 1; l <= maxVarcharChars; l *= 10, i++) {
            final String s = strings[i];
            assert l == s.length();

            ops.add(
                new ClusterjOp("setVarchar" + l) {
                    public void run(int countA, int countB) {
                        for (int i = 0; i < countB; i++) {
                            // XXX blind update not working
                            final IB0 o = session.find(IB0.class, i);
                            //final IB0 o = session.newInstance(IB0.class);
                            assert o != null;
                            o.setCvarchar_def(s);
                            session.updatePersistent(o);
                        }
                    }
                });

            ops.add(
                new ClusterjOp("getVarchar" + l) {
                    public void run(int countA, int countB) {
                        for (int i = 0; i < countB; i++) {
                            final IB0 o = session.find(IB0.class, i);
                            assert o != null;
                            verify(s.equals(o.getCvarchar_def()));
                        }
                    }
                });

            ops.add(
                new ClusterjOp("clearVarchar" + l) {
                    public void run(int countA, int countB) {
                        for (int i = 0; i < countB; i++) {
                            // XXX blind update not working
                            final IB0 o = session.find(IB0.class, i);
                            //final IB0 o = session.newInstance(IB0.class);
                            assert o != null;
                            o.setCvarchar_def(null);
                            session.updatePersistent(o);
                        }
                    }
                });
        }
        
        ops.add(
            new ClusterjOp("setB0->A") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countB; i++) {
                        // XXX blind update not working
                        final IB0 b0 = session.find(IB0.class, i);
                        //final IB0 b0 = session.newInstance(IB0.class);
                        assert b0 != null;
                        final int aId = i % countA;
                        b0.setAid(aId);
                        session.updatePersistent(b0);
                    }
                }
            });

        ops.add(
            new ClusterjOp("navB0->A") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countB; i++) {
                        final IB0 b0 = session.find(IB0.class, i);
                        assert b0 != null;
                        int aid = b0.getAid();
                        final IA a = session.find(IA.class, aid);
                        assert a != null;
                        final int id = a.getId();
                        verify(id == i % countA);
                        final int j = checkFields(a);
                        verify(j == id);
                    }
                }
            });

        ops.add(
            new ClusterjOp("navA->B0") {
                protected Query<IB0> query;

                public void init() {
                    super.init();
                }

                public void close() {
                    // XXX there's no close() on query, dobj, builder?
                    super.close();
                }

                public void run(int countA, int countB) {
                    // QueryBuilder is the sessionFactory for queries
                    final QueryBuilder builder
                        = session.getQueryBuilder();
                    // QueryDomainType is the main interface
                    final QueryDomainType<IB0> dobj
                        = builder.createQueryDefinition(IB0.class);
                    // filter by aid
                    dobj.where(dobj.get("aid").equal(dobj.param("aid")));
                    query = session.createQuery(dobj);
                    for (int i = 0; i < countA; i++) {
                        // find B0s by query
                        query.setParameter("aid", i);
                        // fetch results
                        Collection<IB0> b0s = query.getResultList();
                        assert b0s != null;

                        // check query results
                        verify(b0s.size() > 0);
                        for (IB0 b0 : b0s) {
                            assert b0 != null;
                            final int id = b0.getId();
                            verify(id % countA == i);
                            final int j = checkFields(b0);
                            verify(j == id);
                        }
                    }
                }
            });

        ops.add(
            new ClusterjOp("nullB0->A") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countB; i++) {
                        // XXX blind update not working
                        final IB0 b0 = session.find(IB0.class, i);
                        //final IB0 b0 = session.newInstance(IB0.class);
                        assert b0 != null;
                        b0.setAid(0);
                    }
                }
            });

        ops.add(
            new ClusterjOp("delB0ByPK") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countB; i++) {
                        // XXX can do a blind delete ?
                        final IB0 o = session.find(IB0.class, i);
                        assert o != null;
                        session.remove(o);
                    }
                }
            });

        ops.add(
            new ClusterjOp("delAByPK") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countA; i++) {
                        // XXX can do a blind delete ?
                        final IA o = session.find(IA.class, i);
                        assert o != null;
                        session.remove(o);
                    }
                }
            });

        ops.add(
            new ClusterjOp("insA_attr") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countA; i++) {
                        final IA o = session.newInstance(IA.class);
                        assert o != null;
                        o.setId(i);
                        o.setCint((int)-i);
                        o.setClong((long)-i);
                        o.setCfloat((float)-i);
                        o.setCdouble((double)-i);
                        session.persist(o);
                    }
                }
            });

        ops.add(
            new ClusterjOp("insB0_attr") {
                public void run(int countA, int countB) {
                    for (int i = 0; i < countB; i++) {
                        final IB0 o = session.newInstance(IB0.class);
                        assert o != null;
                        o.setId(i);
                        o.setCint((int)-i);
                        o.setClong((long)-i);
                        o.setCfloat((float)-i);
                        o.setCdouble((double)-i);
                        o.setCvarbinary_def(null);
                        session.persist(o);
                    }
                }
            });

        ops.add(
            new ClusterjOp("delAllB0") {
                public void run(int countA, int countB) {
                    int del = session.deletePersistentAll(IB0.class);
                    assert del == countB;
                }
            });

        ops.add(
            new ClusterjOp("delAllA") {
                public void run(int countA, int countB) {
                    int del = session.deletePersistentAll(IA.class);
                    assert del == countA;
                }
            });

        // prepare queries
        for (Iterator<Driver.Op> i = ops.iterator(); i.hasNext();) {
            ((ClusterjOp)i.next()).init();
        }

        out.println(" [ClusterjOp: " + ops.size() + "]");
    }

    protected void closeOperations() {
        out.print("closing operations ...");
        out.flush();

        // close all queries
        for (Iterator<Driver.Op> i = ops.iterator(); i.hasNext();) {
            ((ClusterjOp)i.next()).close();
        }
        ops.clear();

        out.println("      [ok]");
    }

    protected void beginTransaction() {
        session.currentTransaction().begin();
    }

    protected void commitTransaction() {
        session.currentTransaction().commit();
    }

    protected void rollbackTransaction() {
        session.currentTransaction().rollback();
    }

    protected void clearPersistenceContext() {
        // nothing to do as long as we're not caching beyond Tx scope
    }

    protected void clearData() {
        out.print("deleting all objects ...");
        out.flush();

        session.currentTransaction().begin();
        int delB0 = session.deletePersistentAll(IB0.class);
        out.print("    [B0: " + delB0);
        out.flush();
        int delA = session.deletePersistentAll(IA.class);
        out.print(", A: " + delA);
        out.flush();
        session.currentTransaction().commit();

        out.println("]");
    }

    // ----------------------------------------------------------------------

    @SuppressWarnings("unchecked")
    static public void main(String[] args) {
        System.out.println("ClusterjLoad.main()");
        parseArguments(args);
        new ClusterjLoad().run();
        System.out.println();
        System.out.println("ClusterjLoad.main(): done.");
    }
}
