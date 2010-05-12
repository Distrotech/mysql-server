/*****************************************************************************

Copyright (c) 1996, 2009, Innobase Oy. All Rights Reserved.

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

/**************************************************//**
@file include/trx0purge.h
Purge old versions

Created 3/26/1996 Heikki Tuuri
*******************************************************/

#ifndef trx0purge_h
#define trx0purge_h

#include "univ.i"
#include "trx0types.h"
#include "mtr0mtr.h"
#include "trx0sys.h"
#include "que0types.h"
#include "page0page.h"
#include "usr0sess.h"
#include "fil0fil.h"

/** The global data structure coordinating a purge */
extern trx_purge_t*	purge_sys;

/** A dummy undo record used as a return value when we have a whole undo log
which needs no purge */
extern trx_undo_rec_t	trx_purge_dummy_rec;

/********************************************************************//**
Calculates the file address of an undo log header when we have the file
address of its history list node.
@return	file address of the log */
UNIV_INLINE
fil_addr_t
trx_purge_get_log_from_hist(
/*========================*/
	fil_addr_t	node_addr);	/*!< in: file address of the history
					list node of the log */
/*****************************************************************//**
Checks if trx_id is >= purge_view: then it is guaranteed that its update
undo log still exists in the system.
@return TRUE if is sure that it is preserved, also if the function
returns FALSE, it is possible that the undo log still exists in the
system */
UNIV_INTERN
ibool
trx_purge_update_undo_must_exist(
/*=============================*/
	trx_id_t	trx_id);/*!< in: transaction id */
/********************************************************************//**
Creates the global purge system control structure and inits the history
mutex. */
UNIV_INTERN
void
trx_purge_sys_create(
/*=================*/
	ulint	n_purge_threads);	/*!< in: number of purge threads */
/********************************************************************//**
Frees the global purge system control structure. */
UNIV_INTERN
void
trx_purge_sys_close(void);
/*======================*/
/************************************************************************
Adds the update undo log as the first log in the history list. Removes the
update undo log segment from the rseg slot if it is too big for reuse. */
UNIV_INTERN
void
trx_purge_add_update_undo_to_history(
/*=================================*/
	trx_t*	trx,		/*!< in: transaction */
	page_t*	undo_page,	/*!< in: update undo log header page,
				x-latched */
	mtr_t*	mtr);		/*!< in: mtr */
/*******************************************************************//**
This function runs a purge batch.
@return	number of undo log pages handled in the batch */
UNIV_INTERN
ulint
trx_purge(
/*======*/
	ulint	n_purge_threads,	/*!< in: number of purge tasks to
					submit to task queue. */
	ulint	limit);			/*!< in: the maximum number of
					records to purge in one batch */
/******************************************************************//**
Prints information of the purge system to stderr. */
UNIV_INTERN
void
trx_purge_sys_print(void);
/*======================*/

typedef struct purge_iter_struct {
	trx_id_t	trx_no;		/*!< Purge has advanced past all
					transactions whose number is less
					than this */
	undo_no_t	undo_no;	/*!< Purge has advanced past all records
					whose undo number is less than this */
} purge_iter_t;

/** The control structure used in the purge operation */
struct trx_purge_struct{
	sess_t*		sess;		/*!< System session running the purge
					query */
	trx_t*		trx;		/*!< System transaction running the
				       	purge query: this trx is not in the
				       	trx list of the trx system and it
				       	never ends */
	que_t*		query;		/*!< The query graph which will do the
					parallelized purge operation */
	rw_lock_t	latch;		/*!< The latch protecting the purge
					view. A purge operation must acquire an
					x-latch here for the instant at which
					it changes the purge view: an undo
					log operation can prevent this by
					obtaining an s-latch here. */
	read_view_t*	view;		/*!< The purge will not remove undo logs
					which are >= this view (purge view) */
	ulint		n_submitted;	/*!< Count of total tasks submitted
				       	to the task queue */
	ulint		n_completed;	/*!< Count of total tasks completed */

	mutex_t		mutex;		/*!< Mutex protecting the fields
					below */
	ulint		n_pages_handled;/*!< Approximate number of undo log
					pages processed in purge */
	ulint		n_pages_handled_start; /*!< The value of n_pages_handled
					when a purge was initiated */
	ulint		handle_limit;	/*!< Target of how many pages to get
					processed in the current purge */
	purge_iter_t	limit;		/* Limit up to which we have read and
					parsed the UNDO log records */
	purge_iter_t	iter;		/* The 'purge pointer' which advances
					during a purge, and which is used in
				       	history list truncation */
	/*-----------------------------*/
	ibool		next_stored;	/*!< TRUE if the info of the next record
					to purge is stored below: if yes, then
					the transaction number and the undo
					number of the record are stored in
					purge_trx_no and purge_undo_no above */
	trx_rseg_t*	rseg;		/*!< Rollback segment for the next undo
					record to purge */
	ulint		page_no;	/*!< Page number for the next undo
					record to purge, page number of the
					log header, if dummy record */
	ulint		offset;		/*!< Page offset for the next undo
					record to purge, 0 if the dummy
					record */
	ulint		hdr_page_no;	/*!< Header page of the undo log where
					the next record to purge belongs */
	ulint		hdr_offset;	/*!< Header byte offset on the page */
	/*-----------------------------*/
	trx_undo_arr_t*	arr;		/*!< Array of transaction numbers and
					undo numbers of the undo records
					currently under processing in purge */
	mem_heap_t*	heap;		/*!< Temporary storage used during a
					purge: can be emptied after purge
					completes */
};

/** Info required to purge a record */
struct trx_purge_rec_struct {
	trx_undo_rec_t*	undo_rec;	/*!< Record to purge */
	roll_ptr_t	roll_ptr;	/*!< File pointr to UNDO record */
};

typedef struct trx_purge_rec_struct trx_purge_rec_t;

/** Test if purge mutex is owned. */
#define purge_mutex_own() mutex_own(&purge_sys->mutex)

/** Acquire the flush list mutex. */
#define purge_mutex_enter() do {		\
	mutex_enter(&purge_sys->mutex);		\
} while (0)

/** Release the purge mutex. */
# define purge_mutex_exit() do {	\
	mutex_exit(&purge_sys->mutex);	\
} while (0)

#ifndef UNIV_NONINL
#include "trx0purge.ic"
#endif

#endif
