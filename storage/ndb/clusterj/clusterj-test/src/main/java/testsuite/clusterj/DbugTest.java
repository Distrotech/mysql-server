/*
 *  Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
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

package testsuite.clusterj;

import com.mysql.clusterj.ClusterJHelper;
import com.mysql.clusterj.Dbug;

/**
 * Tests dbug methods.
 */
public class DbugTest extends AbstractClusterJTest{

    public boolean getDebug() {
        return false;
    }

    public void test() {
        Dbug dbug = ClusterJHelper.newDbug();
        if (dbug == null) {
            // nothing else can be tested
            fail("Failed to get new Dbug");
        }
        if (dbug.get() == null) {
            // ndbclient is compiled without DBUG; just make sure nothing blows up
            dbug.set("nothing");
            dbug.push("nada");
            dbug.pop();
            dbug.print("keyword", "message");
            return;
        }
        String originalState = "t";
        String newState = "d,jointx:o,/tmp/clusterj-test-dbug";
        dbug.set(originalState);
        String actualState = dbug.get();
        errorIfNotEqual("Failed to set original state", originalState, actualState);
        dbug.push(newState);
        actualState = dbug.get();
        errorIfNotEqual("Failed to push new state", newState, actualState);
        dbug.pop();
        actualState = dbug.get();
        errorIfNotEqual("Failed to pop original state", originalState, actualState);

        dbug = ClusterJHelper.newDbug();
        dbug.output("/tmp/clusterj-test-dbug").flush().debug(new String[] {"a", "b", "c", "d", "e", "f"}).push();
        actualState = dbug.get();
        // keywords are stored LIFO
        errorIfNotEqual("Wrong state created", "d,f,e,d,c,b,a:O,/tmp/clusterj-test-dbug", actualState);
        dbug.pop();

        dbug = ClusterJHelper.newDbug();
        dbug.append("/tmp/clusterj-test-dbug").trace().debug("a,b,c,d,e,f").set();
        actualState = dbug.get();
        // keywords are stored LIFO
        errorIfNotEqual("Wrong state created", "d,f,e,d,c,b,a:a,/tmp/clusterj-test-dbug:t", actualState);
        dbug.pop();

        failOnError();
    }

}
