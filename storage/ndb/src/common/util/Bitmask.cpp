/*
   Copyright (C) 2004-2005 MySQL AB
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

#include <Bitmask.hpp>
#include <NdbOut.hpp>

void
BitmaskImpl::getFieldImpl(const Uint32 src[],
			  unsigned shiftL, unsigned len, Uint32 dst[])
{
  /* Copy whole words of src to dst, shifting src left
   * by shiftL.  Undefined bits of the last written dst word
   * should be zeroed.
   */
  assert(shiftL < 32);

  unsigned shiftR = 32 - shiftL;
  unsigned undefined = shiftL ? ~0 : 0;

  /* Merge first word with previously set bits if there's a shift */
  * dst = shiftL ? * dst : 0;

  /* Treat the zero-shift case separately to avoid
   * trampling or reading past the end of src
   */
  if (shiftL == 0)
  {
    while(len >= 32)
    {
      * dst++ = * src++;
      len -=32;
    }

    if (len != 0)
    {
      /* Last word has some bits set */
      Uint32 mask= ((1 << len) -1); // 0000111
      * dst = (* src) & mask;
    }
  }
  else // shiftL !=0, need to build each word from two words shifted
  {
    while(len >= 32)
    {
      * dst++ |= (* src) << shiftL;
      * dst = ((* src++) >> shiftR) & undefined;
      len -= 32;
    }

    /* Have space for shiftR more bits in the current dst word
     * is that enough?
     */
    if(len <= shiftR)
    {
      /* Fit the remaining bits in the current dst word */
      * dst |= ((* src) & ((1 << len) - 1)) << shiftL;
    }
    else
    {
      /* Need to write to two dst words */
      * dst++ |= ((* src) << shiftL);
      * dst = ((* src) >> shiftR) & ((1 << (len - shiftR)) - 1) & undefined;
    }
  }
}

void
BitmaskImpl::setFieldImpl(Uint32 dst[],
			  unsigned shiftL, unsigned len, const Uint32 src[])
{
  /**
   *
   * abcd ef00
   * 00ab cdef
   */
  assert(shiftL < 32);
  unsigned shiftR = 32 - shiftL;
  unsigned undefined = shiftL ? ~0 : 0;  
  while(len >= 32)
  {
    * dst = (* src++) >> shiftL;
    * dst++ |= ((* src) << shiftR) & undefined;
    len -= 32;
  }
  
  /* Copy last bits */
  Uint32 mask = ((1 << len) -1);
  * dst = (* dst & ~mask);
  if(len <= shiftR)
  {
    /* Remaining bits fit in current word */
    * dst |= ((* src++) >> shiftL) & mask;
  }
  else
  {
    /* Remaining bits update 2 words */
    * dst |= ((* src++) >> shiftL);
    * dst |= ((* src) & ((1 << (len - shiftR)) - 1)) << shiftR ;
  }
}

/* Bitmask testcase code moved from here to
 * storage/ndb/test/ndbapi/testBitfield.cpp
 * to get coverage from automated testing
 */

int
BitmaskImpl::parseMask(unsigned size, Uint32 data[], const char * src)
{
  int cnt = 0;
  BaseString tmp(src);
  Vector<BaseString> list;
  tmp.split(list, ",");
  for (unsigned i = 0; i<list.size(); i++)
  {
    list[i].trim();
    if (list[i].empty())
      continue;
    unsigned num = 0;
    char * delim = (char*)strchr(list[i].c_str(), '-');
    unsigned first = 0;
    unsigned last = 0;
    if (delim == 0)
    {
      int res = sscanf(list[i].c_str(), "%u", &first);
      if (res != 1)
      {
        return -1;
      }
      last = first;
    }
    else
    {
      * delim = 0;
      delim++;
      int res0 = sscanf(list[i].c_str(), "%u", &first);
      if (res0 != 1)
      {
        return -1;
      }
      int res1 = sscanf(delim, "%u", &last);
      if (res1 != 1)
      {
        return -1;
      }
      if (first > last)
      {
        unsigned tmp = first;
        first = last;
        last = tmp;
      }
    }
    
    for (unsigned j = first; j<(last+1); j++)
    {
      if (j >= (size << 5))
        return -2;

      cnt++;
      BitmaskImpl::set(size, data, j);
    }
  }
  return cnt;
}

template <unsigned size>
BaseString BitmaskPOD<size>::getText()
{
  char buf[32*size+1];
  return BitmaskImpl::getText(size, rep.data, buf);
}

template <unsigned size>
BaseString BitmaskPOD<size>::getPrettyText()
{
  const char* delimiter = "";
  unsigned i, found = 0, MAX_BITS = 8 * size;
  BaseString to;
  for (i = 0; i < MAX_BITS; i++)
  {
    if (get(i))
    {
      to.appfmt("%s%d", delimiter, i);
      found++;
      if (found < count() - 1)
        delimiter = ", ";
      else
        delimiter = " and ";
    }
  }
  return to;
}

template <unsigned size>
BaseString BitmaskPOD<size>::getPrettyTextShort()
{
  const char* delimiter = "";
  unsigned i, found = 0, MAX_BITS = 8 * size;
  BaseString to;
  for (i = 0; i < MAX_BITS; i++)
  {
    if (get(i))
    {
      to.appfmt("%s%d", delimiter, i);
      delimiter = ",";
    }
  }
  return to;
}

#ifdef TEST_BITMASK
#include <NdbTap.hpp>

typedef Bitmask<8UL> Bitmask8;

TAPTEST(Bitmask)
{
    int i, found;
    Bitmask8 b;
    OK(b.isclear());
    
    int MAX_BITS = 32 * b.Size;
    for (i = 0; i < MAX_BITS; i++)
    {
      if (i > 60)
        continue;
      switch(i)
      {
      case 2:case 3:case 5:case 7:case 11:case 13:case 17:case 19:case 23:
      case 29:case 31:case 37:case 41:case 43:
        break;
      default:;
        b.set(i);
      }
    }
    for (found = i = 0; i < MAX_BITS; i++)
      found+=(int)b.get(i);
    OK(found == b.count());
    OK(found == 47);
    printf("getText: %s\n",b.getText().c_str());
    OK(b.getText() == 
        "0000000000000000000000000000000000000000000000001ffff5df5f75d753");
    printf("getPrettyText: %s\n",b.getPrettyText().c_str());
    OK(b.getPrettyText() == 
        "0, 1, 4, 6, 8, 9, 10, 12, 14, 15, 16, 18, 20, 21, 22, 24, 25, 26, "
        "27, 28, 30, 32, 33, 34, 35, 36, 38, 39, 40, 42, 44, 45, 46, 47, "
        "48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59 and 60")
    printf("getPrettyTextShort: %s\n",b.getPrettyTextShort().c_str());
    OK(b.getPrettyTextShort() ==
        "0,1,4,6,8,9,10,12,14,15,16,18,20,21,22,24,25,26,27,28,30,32,"
        "33,34,35,36,38,39,40,42,44,45,46,47,48,49,50,51,52,53,54,55,"
        "56,57,58,59,60")    
    return 1; // OK
}

#endif

template struct BitmaskPOD<8>;
