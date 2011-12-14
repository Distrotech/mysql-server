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

package com.mysql.clusterj.tie;

import java.nio.ByteBuffer;

import com.mysql.clusterj.Dbug;

import com.mysql.clusterj.core.util.I18NHelper;
import com.mysql.clusterj.core.util.Logger;
import com.mysql.clusterj.core.util.LoggerFactoryService;

import com.mysql.ndbjtie.mysql.Utils;

/**
 * This class encapsulates Utils dbug methods to manage dbug settings. 
 */
public class DbugImpl implements Dbug {

    /** My message translator */
    static final I18NHelper local = I18NHelper
            .getInstance(DbugImpl.class);

    /** My logger */
    static final Logger logger = LoggerFactoryService.getFactory()
            .getInstance(DbugImpl.class);

    private static final int DBUG_SIZE = 256;

    boolean propertyTrace = false;
    String fileName = "";
    Character fileStrategy = 'o';
    String debugList;
    
    public String get() {
        ByteBuffer buffer = ByteBuffer.allocateDirect(DBUG_SIZE);
        String result = Utils.dbugExplain(buffer, DBUG_SIZE);
        return result;
    }

    public void pop() {
        Utils.dbugPop();
    }

    public void push(String state) {
        Utils.dbugPush(state);
    }

    public void set(String state) {
        Utils.dbugSet(state);
    }

    public void set() {
        set(toState());
    }

    public void push() {
        push(toState());
    }

    public Dbug trace(boolean trace) {
        this.propertyTrace = trace;
        return this;
    }

    public Dbug trace() {
        return trace(true);
    }

    public Dbug output(String fileName) {
        this.fileName = fileName;
        this.fileStrategy = 'o';
        return this;
    }

    public Dbug append(String fileName) {
        this.fileName = fileName;
        this.fileStrategy = 'a';
        return this;
    }

    public Dbug flush() {
        this.fileStrategy = Character.toUpperCase(this.fileStrategy);
        return this;
    }

    public Dbug debug(String debugList) {
        this.debugList = debugList;
        return this;
    }

    public Dbug debug(String[] debugList) {
        StringBuilder builder = new StringBuilder();
        String sep = "";
        for (String debug: debugList) {
            builder.append(sep);
            builder.append(debug);
            sep = ",";
        }
        this.debugList = builder.toString();
        return this;
    }

    private String toState() {
        String separator = "";
        StringBuilder builder = new StringBuilder();
        if (propertyTrace) {
            builder.append("t");
            separator = ":";
        }
        if (fileName != null) {
            builder.append(separator);
            builder.append(fileStrategy);
            builder.append(',');
            builder.append(fileName);
            separator = ":";
        }
        if (debugList != null) {
            builder.append(separator);
            builder.append("d,");
            builder.append(debugList);
            separator = ":";
        }
        return builder.toString();
    }

}