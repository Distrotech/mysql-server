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

#ifndef START_INFO_HPP
#define START_INFO_HPP

/**
 * This signal is sent from the master DIH to all DIHs
 * when a node is starting.
 * If the typeStart is initial node restart then the node
 * has started without filesystem.
 * All DIHs must then "forget" that the starting node has 
 * performed LCP's ever.
 *
 * @see StartPermReq
 */

class StartInfoReq {  
  /**
   * Sender/Receiver
   */
  friend class Dbdih;

  Uint32 startingNodeId;
  Uint32 typeStart;
  Uint32 systemFailureNo;

public:
  STATIC_CONST( SignalLength = 3 );
};

class StartInfoConf {
  
  /**
   * Sender/Receiver
   */
  friend class Dbdih;
  
  /**
   * NodeId of sending node
   * which is "done"
   */
  Uint32 sendingNodeId;
  Uint32 startingNodeId;

public:
  STATIC_CONST( SignalLength = 2 );
};

class StartInfoRef {
  
  /**
   * Sender/Receiver
   */
  friend class Dbdih;
  
  /**
   * NodeId of sending node
   * The node was refused to start. This could be
   * because there are still processes handling
   * previous information from the starting node.
   */
  Uint32 sendingNodeId;
  Uint32 startingNodeId;
  Uint32 errorCode;

public:
  STATIC_CONST( SignalLength = 3 );
};

#endif
