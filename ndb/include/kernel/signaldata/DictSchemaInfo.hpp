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

#ifndef DICT_SCHEMA_INFO_HPP
#define DICT_SCHEMA_INFO_HPP

#include "SignalData.hpp"

class DictSchemaInfo {
  /**
   * Sender(s) / Reciver(s)
   */
  friend class Dbdict;
  
public:
  static const unsigned HeaderLength = 3;
  static const unsigned DataLength = 22;
  
private:  
  Uint32 senderRef;
  Uint32 offset; 
  Uint32 totalLen; 
  
  /**
   * Length in this = signal->length() - 3
   * Sender block ref = signal->senderBlockRef()
   */
  
  Uint32 schemaInfoData[22];
};

#endif
