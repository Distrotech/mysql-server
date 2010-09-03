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

template struct BitmaskPOD<1>;
template struct BitmaskPOD<2>; // NdbNodeBitmask
template struct BitmaskPOD<8>; // NodeBitmask
template struct BitmaskPOD<16>;

#ifdef TEST_BITMASK
#include <util/NdbTap.hpp>
#include <util/BaseString.hpp>

#include "parse_mask.hpp"

TAPTEST(Bitmask)
{
    unsigned i, found;
    Bitmask<8> b;
    OK(b.isclear());
    
    unsigned MAX_BITS = 32 * b.Size;
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
    printf("getText: %s\n", BaseString::getText(b).c_str());
    OK(BaseString::getText(b) ==
        "0000000000000000000000000000000000000000000000001ffff5df5f75d753");
    printf("getPrettyText: %s\n", BaseString::getPrettyText(b).c_str());
    OK(BaseString::getPrettyText(b) ==
        "0, 1, 4, 6, 8, 9, 10, 12, 14, 15, 16, 18, 20, 21, 22, 24, 25, 26, "
        "27, 28, 30, 32, 33, 34, 35, 36, 38, 39, 40, 42, 44, 45, 46, 47, "
       "48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59 and 60");
    printf("getPrettyTextShort: %s\n",
           BaseString::getPrettyTextShort(b).c_str());
    OK(BaseString::getPrettyTextShort(b) ==
        "0,1,4,6,8,9,10,12,14,15,16,18,20,21,22,24,25,26,27,28,30,32,"
        "33,34,35,36,38,39,40,42,44,45,46,47,48,49,50,51,52,53,54,55,"
       "56,57,58,59,60");

    // bitNOT Tests
    Bitmask<8> c = b;
    OK(c.equal(b));
    c.bitNOT();
    printf("getPrettyTextShort(c 1): %s\n",
           BaseString::getPrettyTextShort(c).c_str());
    OK(!c.equal(b));
    c.bitNOT();
    OK(c.count() == b.count());
    OK(c.equal(b));
    printf("getPrettyTextShort(c 2): %s\n",
           BaseString::getPrettyTextShort(c).c_str());

    Bitmask<1> d;
    d.set(1);
    d.set(3);
    d.set(4);
    printf("getPrettyTextShort(d 1): %s\n",
           BaseString::getPrettyTextShort(d).c_str());
    OK(d.count() == 3);
    d.bitNOT();
    printf("getPrettyTextShort(d 2): %s\n",
           BaseString::getPrettyTextShort(d).c_str());
    OK(d.get(2));
    OK(!d.get(4));
    OK(d.count() != 3);
    d.bitNOT();
    OK(d.count() == 3);
    printf("getPrettyTextShort(d 3): %s\n",
           BaseString::getPrettyTextShort(d).c_str());

    /*
      parse_mask
    */
    Bitmask<8> mask;
    OK(parse_mask("1,2,5-7", mask) == 5);

    // Check all specified bits set
    OK(mask.get(1));
    OK(mask.get(2));
    OK(mask.get(5));
    OK(mask.get(6));
    OK(mask.get(7));

    // Check some random bits not set
    OK(!mask.get(0));
    OK(!mask.get(4));
    OK(!mask.get(3));
    OK(!mask.get(8));
    OK(!mask.get(22));

    // Parse at the limit
    OK(parse_mask("254", mask) == 1);
    OK(parse_mask("255", mask) == 1);

    // Parse invalid spec(s)
    OK(parse_mask("xx", mask) == -1);
    OK(parse_mask("5-", mask) == -1);
    OK(parse_mask("-5", mask) == -1);
    OK(parse_mask("1,-5", mask) == -1);

    // Parse too large spec
    OK(parse_mask("256", mask) == -2);
    OK(parse_mask("1-255,256", mask) == -2);


    return 1; // OK
}


#endif

