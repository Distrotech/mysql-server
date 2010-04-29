/*
   Copyright (C) 2009 Sun Microsystems Inc.
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

package com.mysql.clusterj.core.metadata;

import com.mysql.clusterj.core.spi.DomainFieldHandler;
import com.mysql.clusterj.core.spi.ValueHandler;
import com.mysql.clusterj.ClusterJDatastoreException;
import com.mysql.clusterj.ClusterJUserException;

import com.mysql.clusterj.annotation.Column;
import com.mysql.clusterj.annotation.Lob;
import com.mysql.clusterj.annotation.NotPersistent;
import com.mysql.clusterj.annotation.NullValue;
import com.mysql.clusterj.annotation.Persistent;

import com.mysql.clusterj.core.store.Operation;
import com.mysql.clusterj.core.store.Table;

import java.lang.annotation.Annotation;

import java.lang.reflect.Method;

import java.math.BigDecimal;
import java.math.BigInteger;

/** An instance of this class handles a field (property)
 * of a persistence-capable class (interface).
 * Currently only properties (paired get and set methods) of interfaces
 * are supported.
 * Instances of the class bind at construction time to implementations of
 * type-specific handlers for Ndb operations.
 * 
 */
public class DomainFieldHandlerImpl extends AbstractDomainFieldHandlerImpl implements DomainFieldHandler {

    /** The NullValue setting of the column from the Persistent annotation. */
    NullValue nullValue = NullValue.NONE;

    /** The get method. */
    Method getMethod;

    /** The set method. */
    protected Method setMethod;

    /** The null value handler. */
    protected NullObjectOperationHandler nullValueDelegate;

    /** The Index annotation on the get method. */
    protected com.mysql.clusterj.annotation.Index indexAnnotation = null;

    /** The Persistent annotation on the get method. */
    protected Persistent persistentAnnotation = null;

    /** The Column annotation on the get method. */
    protected Column columnAnnotation = null;

    /** Lob annotation is not null if annotated with @Lob. */
    protected Lob lobAnnotation;

    /** The NotPersistent annotation indicates that this field is not
     * persistent, but can be used as a property that holds data not
     * stored in the datastore.
     */
    protected NotPersistent notPersistentAnnotation;

    public int compareTo(Object other) {
        return compareTo((DomainFieldHandlerImpl)other);
    }

    public DomainFieldHandlerImpl(DomainTypeHandlerImpl<?> domainTypeHandler, Table table,
            int fieldNumber, String name, Class<?> type,
            Method getMethod, Method setMethod) {
        if (logger.isDebugEnabled()) logger.debug("new DomainFieldHandlerImpl: fieldNumber: " + fieldNumber + "; name:" + name + "; getMethod: " + getMethod + "; setMethod: " + setMethod);
        this.domainTypeHandler = domainTypeHandler;
        this.fieldNumber = fieldNumber;
        this.name = name;
        this.type = type;
        this.setMethod = setMethod;
        this.getMethod = getMethod;
        
        Annotation[] annotations = setMethod.getAnnotations();
        if (annotations != null && annotations.length != 0) {
            for (Annotation a: annotations) {
                error(local.message("ERR_Annotate_Set_Method",
                        name, a.annotationType().getName()));
            }
        }
        notPersistentAnnotation = getMethod.getAnnotation(NotPersistent.class);
        if (isPersistent()) {
            // process column annotation first and check the class annotation
            // for primary key
            // Initialize default column name; may be overridden with annotation
            this.columnName = name.toLowerCase();
            this.columnNames = new String[]{name};
            columnAnnotation = getMethod.getAnnotation(Column.class);
            if (columnAnnotation != null) {
                if (columnAnnotation.name() != null) {
                    columnName = columnAnnotation.name();
                    this.columnNames = new String[]{columnName};
                }
                if (logger.isDebugEnabled())
                    logger.debug("Column name annotation for " + name + " is "
                            + columnName);
                columnAllowsNull = columnAnnotation.allowsNull();
                if (logger.isDebugEnabled())
                    logger.debug("Column allowsNull annotation for " + name
                            + " is " + columnAllowsNull);
                columnDefaultValue = columnAnnotation.defaultValue();
                // if user has not specified column defaultValue, set it to null,
                // which makes it easier for later processing
                if (columnDefaultValue.equals("")) {
                    columnDefaultValue = null;
                }
                if (logger.isDebugEnabled())
                    logger.debug("Column defaultValue annotation for " + name
                            + " is " + columnDefaultValue);
            }
            storeColumn = table.getColumn(columnName);
            if (storeColumn == null) {
                throw new ClusterJUserException(local.message("ERR_No_Column",
                        name, table.getName(), columnName));
            }
            storeColumnType = storeColumn.getType();
            charsetName = storeColumn.getCharsetName();
            if (logger.isDebugEnabled())
                logger.debug("Column type for " + name + " is "
                        + storeColumnType.toString() + "; charset name is "
                        + charsetName);
            primaryKey = storeColumn.isPrimaryKey();
            domainTypeHandler.registerPrimaryKeyColumn(this, columnName);
            lobAnnotation = getMethod.getAnnotation(Lob.class);
        }
        if (primaryKey) {
            if (type.equals(int.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerKeyInt;
            } else if (type.equals(long.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerKeyLong;
            } else if (type.equals(String.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerKeyString;
            } else {
                error(local.message("ERR_Primary_Field_Type", name));
            }
        } else if (lobAnnotation != null) {
            // large object support for byte[]
            if (type.equals(byte[].class)) {
                objectOperationHandlerDelegate = objectOperationHandlerBytesLob;
            } else if (type.equals(String.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerStringLob;
            } else {
                objectOperationHandlerDelegate = objectOperationHandlerUnsupportedType;
                error(
                    local.message("ERR_Unsupported_Field_Type", type.getName(), name));
            }
        } else if (!isPersistent()) {
            // NotPersistent field
            if (type.equals(byte.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerNotPersistentByte;
            } else if (type.equals(double.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerNotPersistentDouble;
            } else if (type.equals(float.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerNotPersistentFloat;
            } else if (type.equals(int.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerNotPersistentInt;
            } else if (type.equals(long.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerNotPersistentLong;
            } else if (type.equals(short.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerNotPersistentShort;
            } else {
                objectOperationHandlerDelegate = objectOperationHandlerNotPersistentObject;
            }
        } else {
            // not a pk field; use xxxValue to set values
            if (type.equals(byte[].class)) {
                objectOperationHandlerDelegate = objectOperationHandlerBytes;
            } else if (type.equals(java.util.Date.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerJavaUtilDate;
            } else if (type.equals(BigDecimal.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerDecimal;
            } else if (type.equals(BigInteger.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerBigInteger;
            } else if (type.equals(double.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerDouble;
            } else if (type.equals(float.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerFloat;
            } else if (type.equals(int.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerInt;
            } else if (type.equals(Integer.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerObjectInteger;
            } else if (type.equals(Long.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerObjectLong;
            } else if (type.equals(Short.class)) {
                if (com.mysql.clusterj.core.store.Column.Type.Year.equals(storeColumnType)) {
                    objectOperationHandlerDelegate = objectOperationHandlerObjectShortYear;
                } else {
                    objectOperationHandlerDelegate = objectOperationHandlerObjectShort;
                }
            } else if (type.equals(Float.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerObjectFloat;
            } else if (type.equals(Double.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerObjectDouble;
            } else if (type.equals(long.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerLong;
            } else if (type.equals(short.class)) {
                if (com.mysql.clusterj.core.store.Column.Type.Year.equals(storeColumnType)) {
                    objectOperationHandlerDelegate = objectOperationHandlerShortYear;
                } else {
                    objectOperationHandlerDelegate = objectOperationHandlerShort;
                }
            } else if (type.equals(String.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerString;
            } else if (type.equals(Byte.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerObjectByte;
            } else if (type.equals(byte.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerByte;
            } else if (type.equals(boolean.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerBoolean;
            } else if (type.equals(Boolean.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerObjectBoolean;
            } else if (type.equals(java.sql.Date.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerJavaSqlDate;
            } else if (type.equals(java.sql.Time.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerJavaSqlTime;
            } else if (type.equals(java.sql.Timestamp.class)) {
                objectOperationHandlerDelegate = objectOperationHandlerJavaSqlTimestamp;
            } else {
                objectOperationHandlerDelegate = objectOperationHandlerUnsupportedType;
                error(
                    local.message("ERR_Unsupported_Field_Type", type.getName()));
            }
        }
        // Handle indexes. One index can be annotated on this field.
        // Other indexes including the column mapped to this field
        // are annotated on the class.
        indexAnnotation = getMethod.getAnnotation(
                com.mysql.clusterj.annotation.Index.class);
        String indexName = null;
        if (indexAnnotation != null) {
            indexName = indexAnnotation.name();
            if (indexAnnotation.columns().length != 0) {
                throw new ClusterJUserException(
                        local.message("ERR_Index_Annotation_Columns", domainTypeHandler.getName(), name));
            }
        }
        indices = domainTypeHandler.registerIndices(this, columnName);
        indexNames = domainTypeHandler.getIndexNames(indices);
        logger.debug("Index names for " + name + " are " + indexNames);
        logger.debug("Indices for " + name + " are " + printIndices());

        persistentAnnotation = getMethod.getAnnotation(Persistent.class);
        if (persistentAnnotation != null) {
            nullValue = persistentAnnotation.nullValue();
            logger.debug("Persistent nullValue annotation for " + name + " is " + nullValue);
        }
        // convert the string default value to type-specific value
        defaultValue = objectOperationHandlerDelegate.getDefaultValueFor(this, columnDefaultValue);
        logger.debug("Default null value for " + name + " is " + defaultValue);

        // set up the null value handler based on the annotation
        switch (nullValue) {
            case DEFAULT:
                // value is null and user has specified a default value
                nullValueDelegate = nullValueDEFAULT;
                break;
            case EXCEPTION:
                // value is null and user wants a ClusterJ exception
                nullValueDelegate = nullValueEXCEPTION;
                break;
            case NONE:
                // value is null and no special handling
            nullValueDelegate = nullValueNONE;
                break;
        }
        reportErrors();
    }

    public boolean isPersistent() {
        return notPersistentAnnotation == null;
    }

    @Override
    public void operationSetValue(ValueHandler handler, Operation op) {
        // handle NullValue here
        boolean isNull = handler.isNull(fieldNumber);
        if (logger.isDetailEnabled()) logger.detail("Column: " + columnName + " field: " + name + "isNull: " + isNull + " type: " + type + " delegate " + objectOperationHandlerDelegate.handler());
        try {
            if (isNull) {
                // value is null; let delegate see what to do
                if (nullValueDelegate.operationSetValue(this, op)) {
                    return;
                }
            }
            objectOperationHandlerDelegate.operationSetValue(this, handler, op);
        } catch (ClusterJDatastoreException ex) {
            throw new ClusterJDatastoreException(local.message("ERR_Value_Delegate", name, columnName, objectOperationHandlerDelegate.handler(), "setValue"), ex);
        }
    }

    protected interface NullObjectOperationHandler {
    /** Handle null values on operationSetValue. This method is called if the
     * value to be set in the handler is null. The execution depends on
     * the null value handling defined for the field.
     *
     * @param fmd the FieldHandler for the field
     * @param op the NDB Operation
     * @return true if the operationSetValue has been handled
     * @throws com.mysql.cluster.ndbj.NdbApiException
     */
        boolean operationSetValue(DomainFieldHandlerImpl fmd, Operation op);
    }

    static NullObjectOperationHandler nullValueDEFAULT = new NullObjectOperationHandler() {
        public boolean operationSetValue(DomainFieldHandlerImpl fmd, Operation op) {
            // set the default value and then return
            fmd.operationSetValue(fmd, fmd.defaultValue, op);
            return true;
        };
    };

    static NullObjectOperationHandler nullValueEXCEPTION = new NullObjectOperationHandler() {
        public boolean operationSetValue(DomainFieldHandlerImpl fmd, Operation op) {
            // always throw an exception
            throw new ClusterJUserException(
                    local.message("ERR_Null_Value_Exception",
                    fmd.domainTypeHandler.getName(), fmd.name));
        };
    };

    static NullObjectOperationHandler nullValueNONE = new NullObjectOperationHandler() {
        public boolean operationSetValue(DomainFieldHandlerImpl fmd, Operation op) {
            // don't do anything here but do the standard processing
            return false;
        };
    };

}
