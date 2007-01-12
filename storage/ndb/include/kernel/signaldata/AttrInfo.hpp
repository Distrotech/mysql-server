/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef ATTRINFO_HPP
#define ATTRINFO_HPP

#include "SignalData.hpp"

class AttrInfo {
  /**
   * Sender(s)
   */
  friend class DbUtil;
  
  /**
   * Receiver(s)
   */
  friend class Dbtup;

  /**
   * Sender(s) / Receiver(s)
   */
  friend class Dbtc;
  friend class Dblqh;
  friend class NdbScanOperation;
  friend class Restore;
  friend class NdbOperation;

  friend bool printATTRINFO(FILE *, const Uint32 *, Uint32, Uint16);
  
public:
  STATIC_CONST( HeaderLength = 3 );
  STATIC_CONST( DataLength = 22 );
  STATIC_CONST( MaxSignalLength = HeaderLength + DataLength );

private:
  Uint32 connectPtr;
  Uint32 transId[2];
  Uint32 attrData[DataLength];
};

/*
  A train of ATTRINFO signals is used to specify attributes to read or
  attributes and values to insert/update in TCKEYREQ, and to specify
  attributes to read in SCAN_TABREQ.

  The ATTRINFO signal train defines a stream of attribute info words.  (Note
  that for TCKEYREQ, the first five words are stored inside the TCKEYREQ
  signal. For SCAN_TABREQ, all attribute info words are sent in ATTRINFO
  signals).

  For SCAN_TABREQ, the attribute information can have up to five sections. The
  initial five words of the stream defines the length of the sections,
  followed by the words of each section in sequence.

  The sections are:
   1. Attributes to read before starting any interpreted program.
   2. Interpreted program.
   3. Attributes to update after running interpreted program.
   4. Attributes to read after interpreted program.
   5. Subroutine data.

  The formats of sections that specify attributes to read or update is a
  sequence of entries, each (1+N) words:
    1 word specifying the AttributeHeader (attribute id in upper 16 bits, and
           size in bytes of data in lower 16 bits).
    N words of data (N = (data byte length+3)>>2).
  For specifying attributes to read, the data length is always zero.
  For an index range scan of a table using an ordered index, the attribute IDs
  refer to columns in the underlying table, not to columns being indexed, so
  all attributes in the underlying table being indexed are accessible.
*/

#endif
