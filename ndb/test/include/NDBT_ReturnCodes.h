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

#ifndef NDBT_RETURNCODES_H
#define NDBT_RETURNCODES_H

#ifdef	__cplusplus
extern "C" {
#endif

#define NDBT_OK 0
#define NDBT_FAILED 1
#define NDBT_WRONGARGS 2
#define NDBT_TEMPORARY 3
/**
 * NDBT_ProgramExit
 * This function will print the returncode togheter with a prefix on 
 * the screen and then exit the test program. 
 * Call this function when exiting the main function in your test programs
 * Returns the return code
 */
int NDBT_ProgramExit(int rcode);


#ifdef	__cplusplus
}
#endif

#endif
