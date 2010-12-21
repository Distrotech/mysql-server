/*
   Copyright 2010, Oracle and/or its affiliates. All rights reserved.

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

package com.mysql.clusterj;

public interface ColumnMetadata {

    /** Return the name of the column.
     * @return the name of the column
     */
    String name();

    /** Return the type of the column.
     * @return the type of the column
     */
    Type type();

    /** Return the java type of the column.
     * @return the java type of the column
     */
    Class<?> javaType();

    /** Return the maximum number of bytes that can be stored in the column
     * after translating the characters using the character set.
     * @return the maximum number of bytes that can be stored in the column
     */
    int maximumLength();

    /** Return the column number. This number is used as the first parameter in
     * the get and set methods of DynamicColumn.
     * @return the column number.
     */
    int number();

    /** Return whether this column is a primary key column.
     * @return true if this column is a primary key column
     */
    boolean isPrimaryKey();

    /** Return whether this column is a partition key column.
     * @return true if this column is a partition key column
     */
    boolean isPartitionKey();

    /** Return the precision of the column.
     * @return the precision of the column
     */
    int precision();

    /** Return the scale of the column.
     * @return the scale of the column
     */
    int scale();

    /** Return whether this column is nullable.
     * @return whether this column is nullable
     */
    boolean nullable();

    /** Return the charset name.
     * @return the charset name
     */
    String charsetName();

    public enum Type {

        Bigint,          ///< 64 bit. 8 byte signed integer, can be used in array
        Bigunsigned,     ///< 64 Bit. 8 byte signed integer, can be used in array
        Binary,          ///< Len
        Bit,             ///< Bit, length specifies no of bits
        Blob,            ///< Binary large object (see NdbBlob)
        Char,            ///< Len. A fixed array of 1-byte chars
        Date,            ///< Precision down to 1 day(sizeof(Date) == 4 bytes )
        Datetime,        ///< Precision down to 1 sec (sizeof(Datetime) == 8 bytes )
        Double,          ///< 64-bit float. 8 byte float, can be used in array
        Decimal,         ///< MySQL >= 5.0 signed decimal,  Precision, Scale
        Decimalunsigned,
        Float,           ///< 32-bit float. 4 bytes float, can be used in array
        Int,             ///< 32 bit. 4 byte signed integer, can be used in array
        Longvarchar,     ///< Length bytes: 2, little-endian
        Longvarbinary,   ///< Length bytes: 2, little-endian
        Mediumint,       ///< 24 bit. 3 byte signed integer, can be used in array
        Mediumunsigned,  ///< 24 bit. 3 byte unsigned integer, can be used in array
        Olddecimal,
        Olddecimalunsigned,
        Smallint,        ///< 16 bit. 2 byte signed integer, can be used in array
        Smallunsigned,   ///< 16 bit. 2 byte unsigned integer, can be used in array
        Text,            ///< Text blob
        Time,            ///< Time without date
        Timestamp,       ///< Unix time
        Tinyint,         ///< 8 bit. 1 byte signed integer, can be used in array
        Tinyunsigned,    ///< 8 bit. 1 byte unsigned integer, can be used in array
        Undefined,
        Unsigned,        ///< 32 bit. 4 byte unsigned integer, can be used in array
        Varbinary,       ///< Length bytes: 1, Max: 255
        Varchar,         ///< Length bytes: 1, Max: 255
        Year             ///< Year 1901-2155 (1 byte)
    }

}
