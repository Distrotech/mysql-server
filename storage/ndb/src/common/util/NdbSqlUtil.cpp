/*
   Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.

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

#include <NdbSqlUtil.hpp>
#include <ndb_version.h>

/*
 * Data types.  The entries must be in the numerical order.
 */

const NdbSqlUtil::Type
NdbSqlUtil::m_typeList[] = {
  { // 0
    Type::Undefined,
    NULL,
    NULL,
    NULL
  },
  { // 1
    Type::Tinyint,
    cmpTinyint,
    NULL,
    NULL
  },
  { // 2
    Type::Tinyunsigned,
    cmpTinyunsigned,
    NULL,
    NULL
  },
  { // 3
    Type::Smallint,
    cmpSmallint,
    NULL,
    NULL
  },
  { // 4
    Type::Smallunsigned,
    cmpSmallunsigned,
    NULL,
    NULL
  },
  { // 5
    Type::Mediumint,
    cmpMediumint,
    NULL,
    NULL
  },
  { // 6
    Type::Mediumunsigned,
    cmpMediumunsigned,
    NULL,
    NULL
  },
  { // 7
    Type::Int,
    cmpInt,
    NULL,
    NULL
  },
  { // 8
    Type::Unsigned,
    cmpUnsigned,
    NULL,
    NULL
  },
  { // 9
    Type::Bigint,
    cmpBigint,
    NULL,
    NULL
  },
  { // 10
    Type::Bigunsigned,
    cmpBigunsigned,
    NULL,
    NULL
  },
  { // 11
    Type::Float,
    cmpFloat,
    NULL,
    NULL
  },
  { // 12
    Type::Double,
    cmpDouble,
    NULL,
    NULL
  },
  { // 13
    Type::Olddecimal,
    cmpOlddecimal,
    NULL,
    NULL
  },
  { // 14
    Type::Char,
    cmpChar,
    likeChar,
    NULL
  },
  { // 15
    Type::Varchar,
    cmpVarchar,
    likeVarchar,
    NULL
  },
  { // 16
    Type::Binary,
    cmpBinary,
    likeBinary,
    NULL
  },
  { // 17
    Type::Varbinary,
    cmpVarbinary,
    likeVarbinary,
    NULL
  },
  { // 18
    Type::Datetime,
    cmpDatetime,
    NULL,
    NULL
  },
  { // 19
    Type::Date,
    cmpDate,
    NULL,
    NULL
  },
  { // 20
    Type::Blob,
    NULL,
    NULL,
    NULL
  },
  { // 21
    Type::Text,
    NULL,
    NULL,
    NULL
  },
  { // 22
    Type::Bit,
    cmpBit,
    NULL,
    maskBit
  },
  { // 23
    Type::Longvarchar,
    cmpLongvarchar,
    likeLongvarchar,
    NULL
  },
  { // 24
    Type::Longvarbinary,
    cmpLongvarbinary,
    likeLongvarbinary,
    NULL
  },
  { // 25
    Type::Time,
    cmpTime,
    NULL,
    NULL
  },
  { // 26
    Type::Year,
    cmpYear,
    NULL,
    NULL
  },
  { // 27
    Type::Timestamp,
    cmpTimestamp,
    NULL,
    NULL
  },
  { // 28
    Type::Olddecimalunsigned,
    cmpOlddecimalunsigned,
    NULL,
    NULL
  },
  { // 29
    Type::Decimal,
    cmpDecimal,
    NULL,
    NULL
  },
  { // 30
    Type::Decimalunsigned,
    cmpDecimalunsigned,
    NULL,
    NULL
  }
};

const NdbSqlUtil::Type&
NdbSqlUtil::getType(Uint32 typeId)
{
  if (typeId < sizeof(m_typeList) / sizeof(m_typeList[0]) &&
      m_typeList[typeId].m_typeId != Type::Undefined) {
    return m_typeList[typeId];
  }
  return m_typeList[Type::Undefined];
}

const NdbSqlUtil::Type&
NdbSqlUtil::getTypeBinary(Uint32 typeId)
{
  switch (typeId) {
  case Type::Char:
  case Type::Varchar:
  case Type::Binary:
  case Type::Varbinary:
  case Type::Longvarchar:
  case Type::Longvarbinary:
    typeId = Type::Binary;
    break;
  case Type::Text:
    typeId = Type::Blob;
    break;
  default:
    break;
  }
  return getType(typeId);
}

/*
 * Comparison functions.
 */

int
NdbSqlUtil::cmpTinyint(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Int8)) {
    Int8 v1, v2;
    memcpy(&v1, p1, sizeof(Int8));
    memcpy(&v2, p2, sizeof(Int8));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpTinyunsigned(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Uint8)) {
    Uint8 v1, v2;
    memcpy(&v1, p1, sizeof(Uint8));
    memcpy(&v2, p2, sizeof(Uint8));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpSmallint(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Int16)) {
    Int16 v1, v2;
    memcpy(&v1, p1, sizeof(Int16));
    memcpy(&v2, p2, sizeof(Int16));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpSmallunsigned(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Uint16)) {
    Uint16 v1, v2;
    memcpy(&v1, p1, sizeof(Uint16));
    memcpy(&v2, p2, sizeof(Uint16));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpMediumint(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= 3) {
    Int32 v1, v2;
    v1 = sint3korr((const uchar*)p1);
    v2 = sint3korr((const uchar*)p2);
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpMediumunsigned(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= 3) {
    Uint32 v1, v2;
    v1 = uint3korr((const uchar*)p1);
    v2 = uint3korr((const uchar*)p2);
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpInt(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Int32)) {
    Int32 v1, v2;
    memcpy(&v1, p1, sizeof(Int32));
    memcpy(&v2, p2, sizeof(Int32));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpUnsigned(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Uint32)) {
    Uint32 v1, v2;
    memcpy(&v1, p1, sizeof(Uint32));
    memcpy(&v2, p2, sizeof(Uint32));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpBigint(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Int64)) {
    Int64 v1, v2;
    memcpy(&v1, p1, sizeof(Int64));
    memcpy(&v2, p2, sizeof(Int64));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpBigunsigned(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Uint64)) {
    Uint64 v1, v2;
    memcpy(&v1, p1, sizeof(Uint64));
    memcpy(&v2, p2, sizeof(Uint64));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpFloat(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(float)) {
    float v1, v2;
    memcpy(&v1, p1, sizeof(float));
    memcpy(&v2, p2, sizeof(float));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpDouble(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(double)) {
    double v1, v2;
    memcpy(&v1, p1, sizeof(double));
    memcpy(&v2, p2, sizeof(double));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmp_olddecimal(const uchar* s1, const uchar* s2, unsigned n)
{
  int sgn = +1;
  unsigned i = 0;
  while (i < n) {
    int c1 = s1[i];
    int c2 = s2[i];
    if (c1 == c2) {
      if (c1 == '-')
        sgn = -1;
    } else if (c1 == '-') {
      return -1;
    } else if (c2 == '-') {
      return +1;
    } else if (c1 < c2) {
      return -1 * sgn;
    } else {
      return +1 * sgn;
    }
    i++;
  }
  return 0;
}

int
NdbSqlUtil::cmpOlddecimal(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (full) {
    assert(n1 == n2);
    const uchar* v1 = (const uchar*)p1;
    const uchar* v2 = (const uchar*)p2;
    return cmp_olddecimal(v1, v2, n1);
  }
  return CmpUnknown;
}

int
NdbSqlUtil::cmpOlddecimalunsigned(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (full) {
    assert(n1 == n2);
    const uchar* v1 = (const uchar*)p1;
    const uchar* v2 = (const uchar*)p2;
    return cmp_olddecimal(v1, v2, n1);
  }
  return CmpUnknown;
}

int
NdbSqlUtil::cmpDecimal(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  const uchar* v1 = (const uchar*)p1;
  const uchar* v2 = (const uchar*)p2;
  // compare as binary strings
  unsigned n = (n1 <= n2 ? n1 : n2);
  int k = memcmp(v1, v2, n);
  if (k == 0) {
    k = (full ? n1 : n) - n2;
  }
  return k < 0 ? -1 : k > 0 ? +1 : full ? 0 : CmpUnknown;
}

int
NdbSqlUtil::cmpDecimalunsigned(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  const uchar* v1 = (const uchar*)p1;
  const uchar* v2 = (const uchar*)p2;
  // compare as binary strings
  unsigned n = (n1 <= n2 ? n1 : n2);
  int k = memcmp(v1, v2, n);
  if (k == 0) {
    k = (full ? n1 : n) - n2;
  }
  return k < 0 ? -1 : k > 0 ? +1 : full ? 0 : CmpUnknown;
}

int
NdbSqlUtil::cmpChar(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  // collation does not work on prefix for some charsets
  assert(full);
  const uchar* v1 = (const uchar*)p1;
  const uchar* v2 = (const uchar*)p2;
  // not const in MySQL
  CHARSET_INFO* cs = (CHARSET_INFO*)(info);
  // compare with space padding
  int k = (*cs->coll->strnncollsp)(cs, v1, n1, v2, n2, false);
  return k < 0 ? -1 : k > 0 ? +1 : 0;
}

int
NdbSqlUtil::cmpVarchar(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  const unsigned lb = 1;
  // collation does not work on prefix for some charsets
  assert(full && n1 >= lb && n2 >= lb);
  const uchar* v1 = (const uchar*)p1;
  const uchar* v2 = (const uchar*)p2;
  unsigned m1 = *v1;
  unsigned m2 = *v2;
  if (m1 <= n1 - lb && m2 <= n2 - lb) {
    CHARSET_INFO* cs = (CHARSET_INFO*)(info);
    // compare with space padding
    int k = (*cs->coll->strnncollsp)(cs, v1 + lb, m1, v2 + lb, m2, false);
    return k < 0 ? -1 : k > 0 ? +1 : 0;
  }
  // treat bad data as NULL
  if (m1 > n1 - lb && m2 <= n2 - lb)
    return -1;
  if (m1 <= n1 - lb && m2 > n2 - lb)
    return +1;
  return 0;
}

int
NdbSqlUtil::cmpBinary(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  const uchar* v1 = (const uchar*)p1;
  const uchar* v2 = (const uchar*)p2;
  // compare as binary strings
  unsigned n = (n1 <= n2 ? n1 : n2);
  int k = memcmp(v1, v2, n);
  if (k == 0) {
    k = (full ? n1 : n) - n2;
  }
  return k < 0 ? -1 : k > 0 ? +1 : full ? 0 : CmpUnknown;
}

int
NdbSqlUtil::cmpVarbinary(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  const unsigned lb = 1;
  if (n2 >= lb) {
    assert(n1 >= lb);
    const uchar* v1 = (const uchar*)p1;
    const uchar* v2 = (const uchar*)p2;
    unsigned m1 = *v1;
    unsigned m2 = *v2;
    if (m1 <= n1 - lb && m2 <= n2 - lb) {
      // compare as binary strings
      unsigned m = (m1 <= m2 ? m1 : m2);
      int k = memcmp(v1 + lb, v2 + lb, m);
      if (k == 0) {
        k = (full ? m1 : m) - m2;
      }
      return k < 0 ? -1 : k > 0 ? +1 : full ? 0 : CmpUnknown;
    }
    // treat bad data as NULL
    if (m1 > n1 - lb && m2 <= n2 - lb)
      return -1;
    if (m1 <= n1 - lb && m2 > n2 - lb)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpDatetime(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Int64)) {
    Int64 v1, v2;
    memcpy(&v1, p1, sizeof(Int64));
    memcpy(&v2, p2, sizeof(Int64));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpDate(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
#ifdef ndb_date_is_4_byte_native_int
  if (n2 >= sizeof(Int32)) {
    Int32 v1, v2;
    memcpy(&v1, p1, sizeof(Int32));
    memcpy(&v2, p2, sizeof(Int32));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
#else
#ifdef ndb_date_sol9x86_cc_xO3_madness
  if (n2 >= 3) {
    const uchar* v1 = (const uchar*)p1;
    const uchar* v2 = (const uchar*)p2;
    // from Field_newdate::val_int
    Uint64 j1 = uint3korr(v1);
    Uint64 j2 = uint3korr(v2);
    j1 = (j1 % 32L)+(j1 / 32L % 16L)*100L + (j1/(16L*32L))*10000L;
    j2 = (j2 % 32L)+(j2 / 32L % 16L)*100L + (j2/(16L*32L))*10000L;
    if (j1 < j2)
      return -1;
    if (j1 > j2)
      return +1;
    return 0;
  }
#else
  if (n2 >= 3) {
    const uchar* v1 = (const uchar*)p1;
    const uchar* v2 = (const uchar*)p2;
    uint j1 = uint3korr(v1);
    uint j2 = uint3korr(v2);
    uint d1 = (j1 & 31);
    uint d2 = (j2 & 31);
    j1 = (j1 >> 5);
    j2 = (j2 >> 5);
    uint m1 = (j1 & 15);
    uint m2 = (j2 & 15);
    j1 = (j1 >> 4);
    j2 = (j2 >> 4);
    uint y1 = j1;
    uint y2 = j2;
    if (y1 < y2)
      return -1;
    if (y1 > y2)
      return +1;
    if (m1 < m2)
      return -1;
    if (m1 > m2)
      return +1;
    if (d1 < d2)
      return -1;
    if (d1 > d2)
      return +1;
    return 0;
  }
#endif
#endif
  assert(! full);
  return CmpUnknown;
}

// not supported
int
NdbSqlUtil::cmpBlob(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  assert(false);
  return 0;
}

// not supported
int
NdbSqlUtil::cmpText(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  assert(false);
  return 0;
}

int
NdbSqlUtil::cmpBit(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{ 
  /* Bitfields are stored as 32-bit words
   * This means that a byte-by-byte comparison will not work on all platforms
   * We do a word-wise comparison of the significant bytes.
   * It is assumed that insignificant bits (but not bytes) are zeroed in the
   * passed values.
   */
  const Uint32 bytes= MIN(n1, n2);
  Uint32 words= (bytes + 3) >> 2;

  /* Don't expect either value to be length zero */
  assert(words);

  /* Check ptr alignment */
  if (unlikely(((((UintPtr)p1) & 3) != 0) ||
               ((((UintPtr)p2) & 3) != 0)))
  {
    Uint32 copyP1[ MAX_TUPLE_SIZE_IN_WORDS ];
    Uint32 copyP2[ MAX_TUPLE_SIZE_IN_WORDS ];
    memcpy(copyP1, p1, words << 2);
    memcpy(copyP2, p2, words << 2);

    return cmpBit(info, copyP1, bytes, copyP2, bytes, full);
  }

  const Uint32* wp1= (const Uint32*) p1;
  const Uint32* wp2= (const Uint32*) p2;
  while (--words)
  {
    if (*wp1 < *wp2)
      return -1;
    if (*(wp1++) > *(wp2++))
      return 1;
  }

  /* For the last word, we mask out any insignificant bytes */
  const Uint32 sigBytes= bytes & 3; // 0..3; 0 == all bytes significant
  const Uint32 mask= sigBytes?
    (1 << (sigBytes *8)) -1 :
    ~0;
  const Uint32 lastWord1= *wp1 & mask;
  const Uint32 lastWord2= *wp2 & mask;
  
  if (lastWord1 < lastWord2)
    return -1;
  if (lastWord1 > lastWord2)
    return 1;

  return 0;
}


int
NdbSqlUtil::cmpTime(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= 3) {
    const uchar* v1 = (const uchar*)p1;
    const uchar* v2 = (const uchar*)p2;
    // from Field_time::val_int
    Int32 j1 = sint3korr(v1);
    Int32 j2 = sint3korr(v2);
    if (j1 < j2)
      return -1;
    if (j1 > j2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

// not yet

int
NdbSqlUtil::cmpLongvarchar(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  const unsigned lb = 2;
  // collation does not work on prefix for some charsets
  assert(full && n1 >= lb && n2 >= lb);
  const uchar* v1 = (const uchar*)p1;
  const uchar* v2 = (const uchar*)p2;
  unsigned m1 = uint2korr(v1);
  unsigned m2 = uint2korr(v2);
  if (m1 <= n1 - lb && m2 <= n2 - lb) {
    CHARSET_INFO* cs = (CHARSET_INFO*)(info);
    // compare with space padding
    int k = (*cs->coll->strnncollsp)(cs, v1 + lb, m1, v2 + lb, m2, false);
    return k < 0 ? -1 : k > 0 ? +1 : 0;
  }
  // treat bad data as NULL
  if (m1 > n1 - lb && m2 <= n2 - lb)
    return -1;
  if (m1 <= n1 - lb && m2 > n2 - lb)
    return +1;
  return 0;
}

int
NdbSqlUtil::cmpLongvarbinary(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  const unsigned lb = 2;
  if (n2 >= lb) {
    assert(n1 >= lb);
    const uchar* v1 = (const uchar*)p1;
    const uchar* v2 = (const uchar*)p2;
    unsigned m1 = uint2korr(v1);
    unsigned m2 = uint2korr(v2);
    if (m1 <= n1 - lb && m2 <= n2 - lb) {
      // compare as binary strings
      unsigned m = (m1 <= m2 ? m1 : m2);
      int k = memcmp(v1 + lb, v2 + lb, m);
      if (k == 0) {
        k = (full ? m1 : m) - m2;
      }
      return k < 0 ? -1 : k > 0 ? +1 : full ? 0 : CmpUnknown;
    }
    // treat bad data as NULL
    if (m1 > n1 - lb && m2 <= n2 - lb)
      return -1;
    if (m1 <= n1 - lb && m2 > n2 - lb)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpYear(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Uint8)) {
    Uint8 v1, v2;
    memcpy(&v1, p1, sizeof(Uint8));
    memcpy(&v2, p2, sizeof(Uint8));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

int
NdbSqlUtil::cmpTimestamp(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2, bool full)
{
  if (n2 >= sizeof(Uint32)) {
    Uint32 v1, v2;
    memcpy(&v1, p1, sizeof(Uint32));
    memcpy(&v2, p2, sizeof(Uint32));
    if (v1 < v2)
      return -1;
    if (v1 > v2)
      return +1;
    return 0;
  }
  assert(! full);
  return CmpUnknown;
}

// like

static const int ndb_wild_prefix = '\\';
static const int ndb_wild_one = '_';
static const int ndb_wild_many = '%';

int
NdbSqlUtil::likeChar(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2)
{
  const char* v1 = (const char*)p1;
  const char* v2 = (const char*)p2;
  CHARSET_INFO* cs = (CHARSET_INFO*)(info);
  // strip end spaces to match (incorrect) MySQL behaviour
  n1 = (*cs->cset->lengthsp)(cs, v1, n1);
  int k = (*cs->coll->wildcmp)(cs, v1, v1 + n1, v2, v2 + n2, ndb_wild_prefix, ndb_wild_one, ndb_wild_many);
  return k == 0 ? 0 : +1;
}

int
NdbSqlUtil::likeBinary(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2)
{
  assert(info == 0);
  return likeChar(&my_charset_bin, p1, n1, p2, n2);
}

int
NdbSqlUtil::likeVarchar(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2)
{
  const unsigned lb = 1;
  if (n1 >= lb) {
    const uchar* v1 = (const uchar*)p1;
    const uchar* v2 = (const uchar*)p2;
    unsigned m1 = *v1;
    unsigned m2 = n2;
    if (lb + m1 <= n1) {
      const char* w1 = (const char*)v1 + lb;
      const char* w2 = (const char*)v2;
      CHARSET_INFO* cs = (CHARSET_INFO*)(info);
      int k = (*cs->coll->wildcmp)(cs, w1, w1 + m1, w2, w2 + m2, ndb_wild_prefix, ndb_wild_one, ndb_wild_many);
      return k == 0 ? 0 : +1;
    }
  }
  return -1;
}

int
NdbSqlUtil::likeVarbinary(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2)
{
  assert(info == 0);
  return likeVarchar(&my_charset_bin, p1, n1, p2, n2);
}

int
NdbSqlUtil::likeLongvarchar(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2)
{
  const unsigned lb = 2;
  if (n1 >= lb) {
    const uchar* v1 = (const uchar*)p1;
    const uchar* v2 = (const uchar*)p2;
    unsigned m1 = uint2korr(v1);
    unsigned m2 = n2;
    if (lb + m1 <= n1) {
      const char* w1 = (const char*)v1 + lb;
      const char* w2 = (const char*)v2;
      CHARSET_INFO* cs = (CHARSET_INFO*)(info);
      int k = (*cs->coll->wildcmp)(cs, w1, w1 + m1, w2, w2 + m2, ndb_wild_prefix, ndb_wild_one, ndb_wild_many);
      return k == 0 ? 0 : +1;
    }
  }
  return -1;
}

int
NdbSqlUtil::likeLongvarbinary(const void* info, const void* p1, unsigned n1, const void* p2, unsigned n2)
{
  assert(info == 0);
  return likeLongvarchar(&my_charset_bin, p1, n1, p2, n2);
}


// mask Functions

int
NdbSqlUtil::maskBit(const void* data, unsigned dataLen, const void* mask, unsigned maskLen, bool cmpZero)
{
  /* Bitfields are stored in word oriented form, so we must compare them in that
   * style as well
   * It is assumed that insignificant bits (but not bytes) in the passed values
   * are zeroed
   */
  const Uint32 bytes = MIN(dataLen, maskLen);
  Uint32 words = (bytes + 3) >> 2;

  /* Don't expect either value to be length zero */
  assert(words);

  /* Check ptr alignment */
  if (unlikely(((((UintPtr)data) & 3) != 0) ||
               ((((UintPtr)mask) & 3) != 0)))
  {
    Uint32 copydata[ MAX_TUPLE_SIZE_IN_WORDS ];
    Uint32 copymask[ MAX_TUPLE_SIZE_IN_WORDS ];
    memcpy(copydata, data, words << 2);
    memcpy(copymask, mask, words << 2);

    return maskBit(data, bytes, mask, bytes, cmpZero);
  }

  const Uint32* wdata= (const Uint32*) data;
  const Uint32* wmask= (const Uint32*) mask;

  if (cmpZero)
  {
    while (--words)
    {
      if ((*(wdata++) & *(wmask++)) != 0)
        return 1;
    }
    
    /* For the last word, we mask out any insignificant bytes */
    const Uint32 sigBytes= bytes & 3; // 0..3; 0 == all bytes significant
    const Uint32 comparisonMask= sigBytes?
      (1 << (sigBytes *8)) -1 :
      ~0;
    const Uint32 lastDataWord= *wdata & comparisonMask;
    const Uint32 lastMaskWord= *wmask & comparisonMask;
    
    if ((lastDataWord & lastMaskWord) != 0)
      return 1;

    return 0;
  }
  else
  {
    while (--words)
    {
      if ((*(wdata++) & *wmask) != *wmask)
        return 1;
      
      wmask++;
    }

    /* For the last word, we mask out any insignificant bytes */
    const Uint32 sigBytes= bytes & 3; // 0..3; 0 == all bytes significant
    const Uint32 comparisonMask= sigBytes?
      (1 << (sigBytes *8)) -1 :
      ~0;
    const Uint32 lastDataWord= *wdata & comparisonMask;
    const Uint32 lastMaskWord= *wmask & comparisonMask;
    
    if ((lastDataWord & lastMaskWord) != lastMaskWord)
      return 1;

    return 0;
  }
}


// check charset

uint
NdbSqlUtil::check_column_for_pk(Uint32 typeId, const void* info)
{
  const Type& type = getType(typeId);
  switch (type.m_typeId) {
  case Type::Char:
  case Type::Varchar:
  case Type::Longvarchar:
    {
      const CHARSET_INFO *cs = (const CHARSET_INFO*)info;
      if(cs != 0 &&
         cs->cset != 0 &&
         cs->coll != 0 &&
         cs->coll->strnxfrm != 0 &&
         cs->strxfrm_multiply <= MAX_XFRM_MULTIPLY)
        return 0;
      else
        return 743;
    }
    break;
  case Type::Undefined:
  case Type::Blob:
  case Type::Text:
  case Type::Bit:
    break;
  default:
    return 0;
  }
  return 906;
}

uint
NdbSqlUtil::check_column_for_hash_index(Uint32 typeId, const void* info)
{
  return check_column_for_pk(typeId, info);
}

uint
NdbSqlUtil::check_column_for_ordered_index(Uint32 typeId, const void* info)
{
  const Type& type = getType(typeId);
  if (type.m_cmp == NULL)
    return false;
  switch (type.m_typeId) {
  case Type::Char:
  case Type::Varchar:
  case Type::Longvarchar:
    {
      const CHARSET_INFO *cs = (const CHARSET_INFO*)info;
      if (cs != 0 &&
          cs->cset != 0 &&
          cs->coll != 0 &&
          cs->coll->strnxfrm != 0 &&
          cs->coll->strnncollsp != 0 &&
          cs->strxfrm_multiply <= MAX_XFRM_MULTIPLY)
        return 0;
      else
        return 743;
    }
    break;
  case Type::Undefined:
  case Type::Blob:
  case Type::Text:
  case Type::Bit:       // can be fixed
    break;
  default:
    return 0;
  }
  return 906;
}

// utilities

bool
NdbSqlUtil::get_var_length(Uint32 typeId, const void* p, unsigned attrlen, Uint32& lb, Uint32& len)
{
  const unsigned char* const src = (const unsigned char*)p;
  switch (typeId) {
  case NdbSqlUtil::Type::Varchar:
  case NdbSqlUtil::Type::Varbinary:
    lb = 1;
    if (attrlen >= lb) {
      len = src[0];
      if (attrlen >= lb + len)
        return true;
    }
    break;
  case NdbSqlUtil::Type::Longvarchar:
  case NdbSqlUtil::Type::Longvarbinary:
    lb = 2;
    if (attrlen >= lb) {
      len = src[0] + (src[1] << 8);
      if (attrlen >= lb + len)
        return true;
    }
    break;
  default:
    lb = 0;
    len = attrlen;
    return true;
    break;
  }
  return false;
}


size_t
NdbSqlUtil::ndb_strnxfrm(struct charset_info_st * cs,
                         uchar *dst, size_t dstlen,
                         const uchar *src, size_t srclen)
{
#if NDB_MYSQL_VERSION_D < NDB_MAKE_VERSION(5,6,0)
  return (*cs->coll->strnxfrm)(cs, dst, dstlen, src, srclen);
#else
  /*
    strnxfrm has got two new parameters in 5.6, we are using the
    defaults for those and can thus easily calculate them from
    existing params
  */
  return  (*cs->coll->strnxfrm)(cs, dst, dstlen, dstlen,
                                src, srclen, MY_STRXFRM_PAD_WITH_SPACE);
#endif
}

// workaround

int
NdbSqlUtil::strnxfrm_bug7284(CHARSET_INFO* cs, unsigned char* dst, unsigned dstLen, const unsigned char*src, unsigned srcLen)
{
  unsigned char nsp[20]; // native space char
  unsigned char xsp[20]; // strxfrm-ed space char
#ifdef VM_TRACE
  memset(nsp, 0x1f, sizeof(nsp));
  memset(xsp, 0x1f, sizeof(xsp));
#endif
  // convert from unicode codepoint for space
  int n1 = (*cs->cset->wc_mb)(cs, (my_wc_t)0x20, nsp, nsp + sizeof(nsp));
  if (n1 <= 0)
    return -1;
  // strxfrm to binary
  int n2 = ndb_strnxfrm(cs, xsp, sizeof(xsp), nsp, n1);
  if (n2 <= 0)
    return -1;
  // XXX bug workaround - strnxfrm may not write full string
  memset(dst, 0x0, dstLen);
  // strxfrm argument string - returns no error indication
  int n3 = ndb_strnxfrm(cs, dst, dstLen, src, srcLen);
  // pad with strxfrm-ed space chars
  int n4 = n3;
  while (n4 < (int)dstLen) {
    dst[n4] = xsp[(n4 - n3) % n2];
    n4++;
  }
  // no check for partial last
  return dstLen;
}

#if defined(WORDS_BIGENDIAN) || defined (VM_TRACE)

static
void determineParams(Uint32 typeId,
                     Uint32 typeLog2Size,
                     Uint32 arrayType,
                     Uint32 arraySize,
                     Uint32 dataByteSize,
                     Uint32& convSize,
                     Uint32& convLen)
{
  /* Some types need 'normal behaviour' over-ridden in
   * terms of endian-ness conversions
   */
  convSize = 0;
  convLen = 0;
  switch(typeId)
  {
  case NdbSqlUtil::Type::Datetime:
  {
    // Datetime is stored as 8x8, should be twiddled as 64 bit
    assert(typeLog2Size == 3);
    assert(arraySize == 8);
    assert(dataByteSize == 8);
    convSize = 64;
    convLen = 1;
    break;
  }
  case NdbSqlUtil::Type::Timestamp:
  {
    // Timestamp is stored as 4x8, should be twiddled as 32 bit
    assert(typeLog2Size == 3);
    assert(arraySize == 4);
    assert(dataByteSize == 4);
    convSize = 32;
    convLen = 1;
    break;
  }
  case NdbSqlUtil::Type::Bit:
  {
    // Bit is stored as bits, should be twiddled as 32 bit
    assert(typeLog2Size == 0);
    convSize = 32;
    convLen = (arraySize + 31)/32;
    break;
  }
  case NdbSqlUtil::Type::Blob:
  case NdbSqlUtil::Type::Text:
  {
    if (arrayType == NDB_ARRAYTYPE_FIXED)
    {
      // Length of fixed size blob which is stored in first 64 bit's
      // has to be twiddled, the remaining byte stream left as is
      assert(typeLog2Size == 3);
      assert(arraySize > 8);
      assert(dataByteSize > 8);
      convSize = 64;
      convLen = 1;
      break;
    }
    // Fall through for Blob v2
  }
  default:
    /* Default determined by meta-info */
    convSize = 1 << typeLog2Size;
    convLen = arraySize;
    break;
  }

  const Uint32 unitBytes = (convSize >> 3);
  
  if (dataByteSize < (unitBytes * convLen))
  {
    /* Actual data is shorter than expected, could
     * be VAR type or bad FIXED data (which some other
     * code should detect + handle)
     * We reduce convLen to avoid trampling
     */
    assert((dataByteSize % unitBytes) == 0);
    convLen = dataByteSize / unitBytes;
  }

  assert(convSize);
  assert(convLen);
  assert(dataByteSize >= (unitBytes * convLen));
}

static
void doConvert(Uint32 convSize,
               Uint32 convLen,
               uchar* data)
{
  switch(convSize)
  {
  case 8:
    /* Nothing to swap */
    break;
  case 16:
  {
    Uint16* ptr = (Uint16*)data;
    for (Uint32 i = 0; i < convLen; i++){
      Uint16 val = 
        ((*ptr & 0xFF00) >> 8) |
        ((*ptr & 0x00FF) << 8);
      *ptr = val;
      ptr++;
    }
    break;
  }
  case 32:
  {
    Uint32* ptr = (Uint32*)data;
    for (Uint32 i = 0; i < convLen; i++){
      Uint32 val = 
        ((*ptr & 0xFF000000) >> 24) |
        ((*ptr & 0x00FF0000) >> 8)  |
        ((*ptr & 0x0000FF00) << 8)  |
        ((*ptr & 0x000000FF) << 24);
      *ptr = val;
      ptr++;
    }
    break;
  }
  case 64:
  {
    Uint64* ptr = (Uint64*)data;
    for (Uint32 i = 0; i < convLen; i++){
      Uint64 val = 
        ((*ptr & (Uint64)0xFF00000000000000LL) >> 56) |
        ((*ptr & (Uint64)0x00FF000000000000LL) >> 40) |
        ((*ptr & (Uint64)0x0000FF0000000000LL) >> 24) |
        ((*ptr & (Uint64)0x000000FF00000000LL) >> 8)  |
        ((*ptr & (Uint64)0x00000000FF000000LL) << 8)  |
        ((*ptr & (Uint64)0x0000000000FF0000LL) << 24) |
        ((*ptr & (Uint64)0x000000000000FF00LL) << 40) |
        ((*ptr & (Uint64)0x00000000000000FFLL) << 56);
      *ptr = val;
      ptr++;
    }
    break;
  }
  default:
    abort();
    break;
  }
}
#endif

/**
 * Convert attribute byte order if necessary
 */
void
NdbSqlUtil::convertByteOrder(Uint32 typeId, 
                             Uint32 typeLog2Size, 
                             Uint32 arrayType, 
                             Uint32 arraySize, 
                             uchar* data,
                             Uint32 dataByteSize)
{
#if defined(WORDS_BIGENDIAN) || defined (VM_TRACE)
  Uint32 convSize;
  Uint32 convLen;
  determineParams(typeId,
                  typeLog2Size,
                  arrayType,
                  arraySize,
                  dataByteSize,
                  convSize,
                  convLen);

  size_t mask = (((size_t) convSize) >> 3) -1; // Bottom 0,1,2,3 bits set
  bool aligned = (((size_t) data) & mask) == 0;
  uchar* dataPtr = data;
  const Uint32 bufSize = (MAX_TUPLE_SIZE_IN_WORDS + 1)/2;
  Uint64 alignedBuf[bufSize];
  
  if (!aligned)
  {
    assert(dataByteSize <= 4 * MAX_TUPLE_SIZE_IN_WORDS);
    memcpy(alignedBuf, data, dataByteSize);
    dataPtr = (uchar*) alignedBuf;
  }

  /* Now have convSize and convLen, do the conversion */
  doConvert(convSize,
            convLen,
            dataPtr);

#ifdef VM_TRACE
#ifndef WORDS_BIGENDIAN
  // VM trace on little-endian performs double-convert
  doConvert(convSize,
            convLen,
            dataPtr);
#endif
#endif

  if (!aligned)
  {
    memcpy(data, alignedBuf, dataByteSize);
  }  
#endif
}

