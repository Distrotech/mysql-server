/*
  Copyright (c) 2010 Sun Microsystems, Inc.
  Use is subject to license terms.

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
/*
 * NdbTransactionConst.java
 */

package com.mysql.ndbjtie.ndbapi;

import java.nio.ByteBuffer;

import com.mysql.jtie.Wrapper;

public interface NdbTransactionConst
{
    NdbErrorConst/*_const NdbError &_*/ getNdbError() /*_const_*/;
    NdbOperationConst/*_const NdbOperation *_*/ getNdbErrorOperation() /*_const_*/;
    NdbOperationConst/*_const NdbOperation *_*/ getNextCompletedOperation(NdbOperationConst/*_const NdbOperation *_*/ op) /*_const_*/;
}
