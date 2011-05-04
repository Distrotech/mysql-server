/*
   Copyright (C) 2003-2006, 2008 MySQL AB, 2009 Sun Microsystems, Inc.
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

#define DBTUX_CMP_CPP
#include "Dbtux.hpp"

/*
 * Search key vs node prefix or entry.
 *
 * The comparison starts at given attribute position.  The position is
 * updated by number of equal initial attributes found.  The entry data
 * may be partial in which case CmpUnknown may be returned.
 *
 * The attributes are normalized and have variable size given in words.
 */
int
Dbtux::cmpSearchKey(TuxCtx& ctx,
                    const Frag& frag, unsigned& start, ConstData searchKey, ConstData entryData, unsigned maxlen)
{
  const Index& index = *c_indexPool.getPtr(frag.m_indexId);
  const unsigned numAttrs = index.m_numAttrs;
  const DescHead& descHead = getDescHead(index);
  const KeyType* keyTypes = getKeyTypes(descHead);
  const AttributeHeader* keyAttrs = getKeyAttrs(descHead);
  // skip to right position in search key only
  for (unsigned i = 0; i < start; i++) {
    thrjam(ctx.jamBuffer);
    searchKey += AttributeHeaderSize + ah(searchKey).getDataSize();
  }
  // number of words of entry data left
  unsigned len2 = maxlen;
  int ret = 0;
  while (start < numAttrs) {
    if (len2 <= AttributeHeaderSize) {
      thrjam(ctx.jamBuffer);
      ret = NdbSqlUtil::CmpUnknown;
      break;
    }
    len2 -= AttributeHeaderSize;
    if (! ah(searchKey).isNULL()) {
      if (! ah(entryData).isNULL()) {
        thrjam(ctx.jamBuffer);
        // verify attribute id
        const KeyType& keyType = keyTypes[start];
        Uint32 primaryAttrId = keyAttrs[start].getAttributeId();
        ndbrequire(ah(searchKey).getAttributeId() == primaryAttrId);
        ndbrequire(ah(entryData).getAttributeId() == primaryAttrId);
        // sizes
        const unsigned bytes1 = ah(searchKey).getByteSize();
        const unsigned bytes2 = min(ah(entryData).getByteSize(), len2 << 2);
        const unsigned size2 = min(ah(entryData).getDataSize(), len2);
        len2 -= size2;
        // compare
        NdbSqlUtil::Cmp* const cmp = ctx.c_sqlCmp[start];
        const Uint32* const p1 = &searchKey[AttributeHeaderSize];
        const Uint32* const p2 = &entryData[AttributeHeaderSize];
        const bool full = (maxlen == MaxAttrDataSize);
        ret = (*cmp)(0, p1, bytes1, p2, bytes2, full);
        if (ret != 0) {
          thrjam(ctx.jamBuffer);
          break;
        }
      } else {
        thrjam(ctx.jamBuffer);
        // not NULL > NULL
        ret = +1;
        break;
      }
    } else {
      if (! ah(entryData).isNULL()) {
        thrjam(ctx.jamBuffer);
        // NULL < not NULL
        ret = -1;
        break;
      }
    }
    searchKey += AttributeHeaderSize + ah(searchKey).getDataSize();
    entryData += AttributeHeaderSize + ah(entryData).getDataSize();
    start++;
  }
  return ret;
}

/*
 * Scan bound vs node prefix or entry.
 *
 * Compare lower or upper bound and index entry data.  The entry data
 * may be partial in which case CmpUnknown may be returned.  Otherwise
 * returns -1 if the bound is to the left of the entry and +1 if the
 * bound is to the right of the entry.
 *
 * The routine is similar to cmpSearchKey, but 0 is never returned.
 * Suppose all attributes compare equal.  Recall that all bounds except
 * possibly the last one are non-strict.  Use the given bound direction
 * (0-lower 1-upper) and strictness of last bound to return -1 or +1.
 *
 * Following example illustrates this.  We are at (a=2, b=3).
 *
 * idir bounds                  strict          return
 * 0    a >= 2 and b >= 3       no              -1
 * 0    a >= 2 and b >  3       yes             +1
 * 1    a <= 2 and b <= 3       no              +1
 * 1    a <= 2 and b <  3       yes             -1
 *
 * The attributes are normalized and have variable size given in words.
 */
int
Dbtux::cmpScanBound(const Frag& frag, unsigned idir, ConstData boundInfo, unsigned boundCount, ConstData entryData, unsigned maxlen)
{
  const Index& index = *c_indexPool.getPtr(frag.m_indexId);
  const DescHead& descHead = getDescHead(index);
  const KeyType* keyTypes = getKeyTypes(descHead);
  const AttributeHeader* keyAttrs = getKeyAttrs(descHead);
  // direction 0-lower 1-upper
  ndbrequire(idir <= 1);
  // number of words of data left
  unsigned len2 = maxlen;
  // in case of no bounds, init last type to something non-strict
  unsigned type = 4;
  while (boundCount != 0) {
    if (len2 <= AttributeHeaderSize) {
      jam();
      return NdbSqlUtil::CmpUnknown;
    }
    len2 -= AttributeHeaderSize;
    // get and skip bound type (it is used after the loop)
    type = boundInfo[0];
    boundInfo += 1;
    if (! ah(boundInfo).isNULL()) {
      if (! ah(entryData).isNULL()) {
        jam();
        // verify attribute id
        const Uint32 attrId = ah(boundInfo).getAttributeId();
        ndbrequire(attrId < index.m_numAttrs);
        const KeyType& keyType = keyTypes[attrId];
        Uint32 primaryAttrId = keyAttrs[attrId].getAttributeId();
        ndbrequire(ah(entryData).getAttributeId() == primaryAttrId);
        // sizes
        const unsigned bytes1 = ah(boundInfo).getByteSize();
        const unsigned bytes2 = min(ah(entryData).getByteSize(), len2 << 2);
        const unsigned size2 = min(ah(entryData).getDataSize(), len2);
        len2 -= size2;
        // compare
        NdbSqlUtil::Cmp* const cmp = c_ctx.c_sqlCmp[attrId];
        const Uint32* const p1 = &boundInfo[AttributeHeaderSize];
        const Uint32* const p2 = &entryData[AttributeHeaderSize];
        const bool full = (maxlen == MaxAttrDataSize);
        int ret = (*cmp)(0, p1, bytes1, p2, bytes2, full);
        if (ret != 0) {
          jam();
          return ret;
        }
      } else {
        jam();
        // not NULL > NULL
        return +1;
      }
    } else {
      jam();
      if (! ah(entryData).isNULL()) {
        jam();
        // NULL < not NULL
        return -1;
      }
    }
    boundInfo += AttributeHeaderSize + ah(boundInfo).getDataSize();
    entryData += AttributeHeaderSize + ah(entryData).getDataSize();
    boundCount -= 1;
  }
  // all attributes were equal
  const int strict = (type & 0x1);
  return (idir == 0 ? (strict == 0 ? -1 : +1) : (strict == 0 ? +1 : -1));
}
