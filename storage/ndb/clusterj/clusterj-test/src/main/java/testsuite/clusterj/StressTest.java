/*
   Copyright (c) 2012, Oracle and/or its affiliates. All rights reserved.

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

package testsuite.clusterj;

import org.junit.Ignore;

import com.mysql.clusterj.ClusterJFatalUserException;
import com.mysql.clusterj.ColumnMetadata;
import com.mysql.clusterj.DynamicObject;

import testsuite.clusterj.AbstractClusterJModelTest;
import testsuite.clusterj.model.IdBase;

@Ignore
public class StressTest extends AbstractClusterJModelTest {

    static protected final Runtime rt = Runtime.getRuntime();

    private static final int NUMBER_TO_INSERT = 4000;

    private static final int ITERATIONS = 5;

    private static final String tableName = "stress";

    private ColumnMetadata[] columnMetadatas;

    private Timer timer = new Timer();

    @Override
    java.lang.Class<? extends IdBase> getModelClass() {
        return Stress.class;
    }

    @Override
    public void localSetUp() {
        createSessionFactory();
        session = sessionFactory.getSession();
        tx = session.currentTransaction();
        session.deletePersistentAll(Stress.class);
        columnMetadatas = session.newInstance(Stress.class).columnMetadata();
    }

    public void testInsAattr_indy() {
        long total = 0;
        for (int i = 0; i < ITERATIONS; ++i) {
            // garbage collect what we can before each test
            gc();
            timer.start();
            for (int key = 0; key < NUMBER_TO_INSERT; ++key) {
                Stress instance = createObject(key);
                session.makePersistent(instance);
            }
            // drop the first iteration
            timer.stop();
            if (i > 0) total += timer.time();
            System.out.println("testInsAattr_indy: " + timer.time());
        }
        System.out.println("Average: " + total/(ITERATIONS - 1) + "\n");
    }

    public void testInsAattr_each() {
        long total = 0;
        for (int i = 0; i < ITERATIONS; ++i) {
            // garbage collect what we can before each test
            gc();
            timer.start();
            session.currentTransaction().begin();
            for (int key = 0; key < NUMBER_TO_INSERT; ++key) {
                Stress instance = createObject(key);
                session.makePersistent(instance);
                session.flush();
            }
            session.currentTransaction().commit();
            // drop the first iteration
            timer.stop();
            if (i > 0) total += timer.time();
            System.out.println("testInsAattr_each: " + timer.time());
        }
        System.out.println("Average: " + total/(ITERATIONS - 1) + "\n");
    }

    public void testInsAattr_bulk() {
        long total = 0;
        for (int i = 0; i < ITERATIONS; ++i) {
            // garbage collect what we can before each test
            gc();
            timer.start();
            session.currentTransaction().begin();
            for (int key = 0; key < NUMBER_TO_INSERT; ++key) {
                Stress instance = createObject(key);
                session.makePersistent(instance);
            }
            session.currentTransaction().commit();
            // drop the first iteration
            timer.stop();
            if (i > 0) total += timer.time();
            System.out.println("testInsAattr_bulk: " + timer.time());
        }
        System.out.println("Average: " + total/(ITERATIONS - 1) + "\n");
    }

    protected Stress createObject(int key) {
        Stress instance = session.newInstance(Stress.class, key);
        for (int columnNumber = 0; columnNumber < columnMetadatas.length; ++columnNumber) {
            Object value = null;
            // create value based on java type
            ColumnMetadata columnMetadata = columnMetadatas[columnNumber];
            if (columnMetadata.isPrimaryKey()) {
                // we have already set the value of the primary key in newInstance
                continue;
            }
            Class<?> cls = columnMetadata.javaType();
            if (int.class == cls) {
                value = key;
            } else if (long.class == cls) {
                value = (long)key;
            } else if (float.class == cls) {
                value = (float)key;
            } else if (double.class == cls) {
                value = (double)key;
            } else {
                throw new ClusterJFatalUserException("Unsupported column type " + cls.getName()
                        + " for column " + columnMetadata.name());
            }
            instance.set(columnNumber, value);
        }
        return instance;
    }

    public static class Stress extends DynamicObject implements IdBase {
        public Stress() {}

        public String table() {
            return tableName;
        }

        public int getId() {
            return (Integer) get(0);
        }

        public void setId(int id) {
            set(0, id);
        }
    }

    static private void gc() {
        // empirically determined limit after which no further
        // reduction in memory usage has been observed
        //final int nFullGCs = 5;
        final int nFullGCs = 10;
        for (int i = 0; i < nFullGCs; i++) {
            //out.print("gc: ");
            long oldfree;
            long newfree = rt.freeMemory();
            do {
                oldfree = newfree;
                rt.runFinalization();
                rt.gc();
                newfree = rt.freeMemory();
                //out.print('.');
            } while (newfree > oldfree);
            //out.println();
        }
    }

    private static class Timer {

        private long time;

        public void start() {
            time = System.nanoTime() / 1000000;
        }

        public long stop() {
            time = (System.nanoTime() / 1000000) - time;
            return time;
        }

        public long time() {
            return time;
        }
    }

}