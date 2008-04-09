/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef BUILD_INDX_IMPL_HPP
#define BUILD_INDX_IMPL_HPP

#include "SignalData.hpp"

struct BuildIndxImplReq {
  STATIC_CONST( SignalLength = 10 );
  STATIC_CONST( INDEX_COLUMNS = 0 );
  STATIC_CONST( KEY_COLUMNS = 1 );
  STATIC_CONST( NoOfSections = 2 );

  Uint32 senderRef;
  Uint32 senderData;
  Uint32 requestType;
  Uint32 transId;
  Uint32 buildId;		// Suma subscription id
  Uint32 buildKey;		// Suma subscription key
  Uint32 tableId;
  Uint32 indexId;
  Uint32 indexType;
  Uint32 parallelism;
};

struct BuildIndxImplConf {
  STATIC_CONST( SignalLength = 2 );

  Uint32 senderRef;
  Uint32 senderData;
};

struct BuildIndxImplRef {
  enum ErrorCode {
    NoError = 0,
    Busy = 701,
    NotMaster = 702,
    BadRequestType = 4247,
    InvalidPrimaryTable = 4249,
    InvalidIndexType = 4250,
    IndexNotUnique = 4251,
    AllocationFailure = 4252,
    InternalError = 4346
  };

  STATIC_CONST( SignalLength = 6 );

  Uint32 senderRef;
  Uint32 senderData;
  Uint32 errorCode;
  Uint32 errorLine;
  Uint32 errorNodeId;
  Uint32 masterNodeId;
};

#endif
