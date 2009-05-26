/*
   Copyright (C) 2003 MySQL AB
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

#ifndef ALTER_TAB_HPP
#define ALTER_TAB_HPP

#include "SignalData.hpp"
#include "GlobalSignalNumbers.h"

/**
 * AlterTab
 *
 * Implemetation of AlterTable
 */
class AlterTabReq {
  /**
   * Sender(s) / Reciver(s)
   */
  friend class Dbdict;
  friend class Dbdih;
  friend class Dbtc;
  friend class Dblqh;
  friend class Suma;
  friend class Dbtup;

  /**
   * For printing
   */
  friend bool printALTER_TAB_REQ(FILE*, const Uint32*, Uint32, Uint16);
  
public:
  STATIC_CONST( SignalLength = 12 );
  
  enum RequestType {
    AlterTablePrepare = 0, // Prepare alter table
    AlterTableCommit = 1,  // Commit alter table
    AlterTableRevert = 2   // Prepare failed, revert instead
  };
private:
  Uint32 senderRef;
  Uint32 senderData;
  Uint32 clientRef;
  Uint32 clientData;

  Uint32 changeMask;
  Uint32 tableId;
  Uint32 tableVersion;
  Uint32 gci;
  Uint32 requestType;

  /* Only used when sending to TUP. */
  Uint32 noOfNewAttr;
  Uint32 newNoOfCharsets;
  Uint32 newNoOfKeyAttrs;

  SECTION( DICT_TAB_INFO = 0 );
  /*
    When sent to DICT, the first section contains the new table definition.
    When sent to TUP, the first section contains the new attributes.
  */
};

struct AlterTabRef {
  /**
   * Sender(s) / Reciver(s)
   */
  friend class Dbdict;
  friend class Dbdih;
  friend class Dbtc;
  friend class Dblqh;
  friend class Dbtup;
  friend class SafeCounter;
  
  /**
   * For printing
   */
  friend bool printALTER_TAB_REF(FILE *, const Uint32 *, Uint32, Uint16);
  
  STATIC_CONST( SignalLength = 7 );
  STATIC_CONST( GSN = GSN_ALTER_TAB_REF );

  enum ErrorCode {
    NF_FakeErrorREF = 255
  };

  Uint32 senderRef;
  Uint32 senderData;
  Uint32 errorCode;
  Uint32 errorLine; 
  Uint32 errorKey;
  Uint32 errorStatus;
  Uint32 requestType;
};

class AlterTabConf {
  /**
   * Sender(s) / Reciver(s)
   */
  friend class Dbdict;
  friend class Dbdih;
  friend class Dbtc;
  friend class Dblqh;
  friend class Dbtup;
  
  /**
   * For printing
   */
  friend bool printALTER_TAB_CONF(FILE *, const Uint32 *, Uint32, Uint16);
  
public:
  STATIC_CONST( SignalLength = 8 );

private:
  Uint32 senderRef;
  Uint32 senderData;
  Uint32 changeMask;
  Uint32 tableId;
  Uint32 tableVersion;
  Uint32 gci;
  Uint32 requestType;

  /* Only used when sent from TUP. */
  Uint32 clientData;
};

/*
  This union can be used to safely refer to a signal data part
  simultaneously as AlterTab{Req,Ref,Conf} without violating the
  strict aliasing rule.
*/
union AlterTabAll {
  AlterTabReq req;
  AlterTabRef ref;
  AlterTabConf conf;
};

#endif
