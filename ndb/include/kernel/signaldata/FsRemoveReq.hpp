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

#ifndef FS_REMOVE_REQ_H
#define FS_REMOVE_REQ_H

#include "SignalData.hpp"
#include "FsOpenReq.hpp"

/**
 * 
 * SENDER:  
 * RECIVER: Ndbfs
 */
class FsRemoveReq {
  /**
   * Reciver(s)
   */
  friend class Ndbfs;         // Reciver
  friend class AsyncFile;     // Uses FsOpenReq to decode file open flags
  friend class Filename;
  friend class VoidFs;

  /**
   * Sender(s)
   */
  friend class Backup;
  friend class Dbdict;
  friend class Dbacc;
  friend class Dbtup;
  friend class Ndbcntr;       // For initial start...

public:
  /**
   * Length of signal
   */
  STATIC_CONST( SignalLength = 8 );

private:

  /**
   * DATA VARIABLES
   */

  UintR userReference;        // DATA 0
  UintR userPointer;          // DATA 1
  UintR fileNumber[4];        // DATA 2 - 5 // See FsOpen for interpretation

  /**
   * 0 = File -> rm file
   * 1 = Directory -> rm -r path
   */
  UintR directory;

  /**
   * If directory = 1
   *
   * 0 = remove only files/direcories in directory specified in fileNumber
   * 1 = remove directory specified in fileNumber
   */
  UintR ownDirectory;
};

#endif

