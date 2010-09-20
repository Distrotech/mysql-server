/*****************************************************************************

Copyright (c) 1995, 2010, Innobase Oy. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place, Suite 330, Boston, MA 02111-1307 USA

*****************************************************************************/

/******************************************************************//**
@file include/fut0fut.h
File-based utilities

Created 12/13/1995 Heikki Tuuri
***********************************************************************/


#ifndef fut0fut_h
#define fut0fut_h

#include "univ.i"

#include "fil0fil.h"
#include "mtr0mtr.h"
#include "dict0dict.h"


typedef struct fts_stopword_struct fts_stopword_t;

/* status bits for fts_stopword_t status field. */
#define STOPWORD_NOT_INIT               0x1
#define STOPWORD_OFF                    0x2
#define STOPWORD_FROM_DEFAULT           0x4
#define STOPWORD_USER_TABLE             0x8

extern const char*	fts_default_stopword[];

/********************************************************************//**
Gets a pointer to a file address and latches the page.
@return pointer to a byte in a frame; the file page in the frame is
bufferfixed and latched */
UNIV_INLINE
byte*
fut_get_ptr(
/*========*/
	ulint		space,	/*!< in: space id */
	ulint		zip_size,/*!< in: compressed page size in bytes
				or 0 for uncompressed pages */
	fil_addr_t	addr,	/*!< in: file address */
	ulint		rw_latch, /*!< in: RW_S_LATCH, RW_X_LATCH */
	mtr_t*		mtr);	/*!< in: mtr handle */

/******************************************************************//**
Check whether user supplied stopword table exists and is of
the right format.
@return TRUE if the table qualifies */
UNIV_INTERN
ibool
fts_valid_stopword_table(
/*=====================*/
	const char*	stopword_table_name);	/*!< in: Stopword table
						name */
/****************************************************************//**
This function loads specified stopword into FTS cache
@return TRUE if success */
UNIV_INTERN
ibool
fts_load_stopword(
/*==============*/
	const dict_table_t*
			table,			/*!< in: Table with FTS */
	const char*	global_stopword_table,	/*!< in: Global stopword table
						name */
	const char*	session_stopword_table,	/*!< in: Session stopword table
						name */
	ibool		stopword_is_on,		/*!< in: Whether stopword
						option is turned on/off */
	ibool		reload);		/*!< in: Whether it is during
						reload of FTS table */

/********************************************************************
Get the next token from the given string and store it in *token. If no token
was found, token->len is set to 0. */
UNIV_INTERN
ulint
fts_get_next_token(
/*===============*/
                                                /* out: number of characters
                                                handled in this call */
        byte*           start,                  /* in: start of text */
        byte*           end,                    /* in: one character past end of
                                                text */
        fts_string_t*   token,                  /* out: token's text */
        ulint*          offset);                /* out: offset to token,
                                                measured as characters from
                                                'start' */


#ifndef UNIV_NONINL
#include "fut0fut.ic"
#endif

#endif

