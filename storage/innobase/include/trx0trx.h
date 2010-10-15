/*****************************************************************************

Copyright (c) 1996, 2010, Innobase Oy. All Rights Reserved.

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
@file include/trx0trx.h
The transaction

Created 3/26/1996 Heikki Tuuri
*******************************************************/

#ifndef trx0trx_h
#define trx0trx_h

#include "univ.i"
#include "trx0types.h"
#include "dict0types.h"
#ifndef UNIV_HOTBACKUP
#include "lock0types.h"
#include "usr0types.h"
#include "que0types.h"
#include "mem0mem.h"
#include "read0types.h"
#include "trx0xa.h"
#include "ut0vec.h"

/** Dummy session used currently in MySQL interface */
extern sess_t*	trx_dummy_sess;

/** Number of transactions currently allocated for MySQL: protected by
the trx_sys_t::mutex */
extern ulint	trx_n_mysql_transactions;

/********************************************************************//**
Releases the search latch if trx has reserved it. */
UNIV_INTERN
void
trx_search_latch_release_if_reserved(
/*=================================*/
	trx_t*	   trx); /*!< in: transaction */
/******************************************************************//**
Set detailed error message for the transaction. */
UNIV_INTERN
void
trx_set_detailed_error(
/*===================*/
	trx_t*		trx,	/*!< in: transaction struct */
	const char*	msg);	/*!< in: detailed error message */
/*************************************************************//**
Set detailed error message for the transaction from a file. Note that the
file is rewinded before reading from it. */
UNIV_INTERN
void
trx_set_detailed_error_from_file(
/*=============================*/
	trx_t*	trx,	/*!< in: transaction struct */
	FILE*	file);	/*!< in: file to read message from */
/****************************************************************//**
Retrieves the error_info field from a trx.
@return	the error info */
UNIV_INLINE
const dict_index_t*
trx_get_error_info(
/*===============*/
	const trx_t*	trx);	/*!< in: trx object */
/****************************************************************//**
Creates and initializes a transaction object.
@return	own: the transaction */
UNIV_INTERN
trx_t*
trx_create(
/*=======*/
	sess_t*	sess)	/*!< in: session */
	__attribute__((nonnull));
/********************************************************************//**
Creates a transaction object for MySQL.
@return	own: transaction object */
UNIV_INTERN
trx_t*
trx_allocate_for_mysql(void);
/*========================*/
/********************************************************************//**
Creates a transaction object for background operations by the master thread.
@return	own: transaction object */
UNIV_INTERN
trx_t*
trx_allocate_for_background(void);
/*=============================*/
/********************************************************************//**
Frees a transaction object for MySQL. */
UNIV_INTERN
void
trx_free_for_mysql(
/*===============*/
	trx_t*	trx);	/*!< in, own: trx object */
/********************************************************************//**
Frees a transaction object of a background operation of the master thread. */
UNIV_INTERN
void
trx_free_for_background(
/*====================*/
	trx_t*	trx);	/*!< in, own: trx object */
/****************************************************************//**
Creates trx objects for transactions and initializes the trx list of
trx_sys at database start. Rollback segment and undo log lists must
already exist when this function is called, because the lists of
transactions to be rolled back or cleaned up are built based on the
undo log lists. */
UNIV_INTERN
void
trx_lists_init_at_db_start(void);
/*============================*/
/*************************************************************//**
Starts the transaction if it is not yet started. */
UNIV_INTERN
void
trx_start_if_not_started_xa(
/*========================*/
	trx_t*	trx);	/*!< in: transaction */
/*************************************************************//**
Starts the transaction if it is not yet started. */
UNIV_INTERN
void
trx_start_if_not_started(
/*=====================*/
	trx_t*	trx);	/*!< in: transaction */
/****************************************************************//**
Commits a transaction. */
UNIV_INTERN
void
trx_commit(
/*=======*/
	trx_t*	trx);	/*!< in: transaction */
/****************************************************************//**
Cleans up a transaction at database startup. The cleanup is needed if
the transaction already got to the middle of a commit when the database
crashed, and we cannot roll it back. */
UNIV_INTERN
void
trx_cleanup_at_db_startup(
/*======================*/
	trx_t*	trx);	/*!< in: transaction */
/**********************************************************************//**
Does the transaction commit for MySQL.
@return	DB_SUCCESS or error number */
UNIV_INTERN
ulint
trx_commit_for_mysql(
/*=================*/
	trx_t*	trx);	/*!< in: trx handle */
/**********************************************************************//**
Does the transaction prepare for MySQL. */
UNIV_INTERN
void
trx_prepare_for_mysql(
/*==================*/
	trx_t*	trx);	/*!< in: trx handle */
/**********************************************************************//**
This function is used to find number of prepared transactions and
their transaction objects for a recovery.
@return	number of prepared transactions */
UNIV_INTERN
int
trx_recover_for_mysql(
/*==================*/
	XID*	xid_list,	/*!< in/out: prepared transactions */
	ulint	len);		/*!< in: number of slots in xid_list */
/*******************************************************************//**
This function is used to find one X/Open XA distributed transaction
which is in the prepared state
@return	trx or NULL */
UNIV_INTERN
trx_t *
trx_get_trx_by_xid(
/*===============*/
	XID*	xid);	/*!< in: X/Open XA transaction identification */
/**********************************************************************//**
If required, flushes the log to disk if we called trx_commit_for_mysql()
with trx->flush_log_later == TRUE.
@return	0 or error number */
UNIV_INTERN
ulint
trx_commit_complete_for_mysql(
/*==========================*/
	trx_t*	trx);	/*!< in: trx handle */
/**********************************************************************//**
Marks the latest SQL statement ended. */
UNIV_INTERN
void
trx_mark_sql_stat_end(
/*==================*/
	trx_t*	trx);	/*!< in: trx handle */
/********************************************************************//**
Assigns a read view for a consistent read query. All the consistent reads
within the same transaction will get the same read view, which is created
when this function is first called for a new started transaction.
@return	consistent read view */
UNIV_INTERN
read_view_t*
trx_assign_read_view(
/*=================*/
	trx_t*	trx);	/*!< in: active transaction */
/****************************************************************//**
Prepares a transaction for commit/rollback. */
UNIV_INTERN
void
trx_commit_or_rollback_prepare(
/*===========================*/
	trx_t*		trx);		/*!< in: transaction */
/*********************************************************************//**
Creates a commit command node struct.
@return	own: commit node struct */
UNIV_INTERN
commit_node_t*
commit_node_create(
/*===============*/
	mem_heap_t*	heap);	/*!< in: mem heap where created */
/***********************************************************//**
Performs an execution step for a commit type node in a query graph.
@return	query thread to run next, or NULL */
UNIV_INTERN
que_thr_t*
trx_commit_step(
/*============*/
	que_thr_t*	thr);	/*!< in: query thread */

/**********************************************************************//**
Prints info about a transaction to the given file. The caller must own the
trx_t::mutex. */
UNIV_INTERN
void
trx_print(
/*======*/
	FILE*	f,		/*!< in: output stream */
	trx_t*	trx,		/*!< in: transaction */
	ulint	max_query_len);	/*!< in: max query length to print, or 0 to
				   use the default max length */

/** Type of data dictionary operation */
typedef enum trx_dict_op {
	/** The transaction is not modifying the data dictionary. */
	TRX_DICT_OP_NONE = 0,
	/** The transaction is creating a table or an index, or
	dropping a table.  The table must be dropped in crash
	recovery.  This and TRX_DICT_OP_NONE are the only possible
	operation modes in crash recovery. */
	TRX_DICT_OP_TABLE = 1,
	/** The transaction is creating or dropping an index in an
	existing table.  In crash recovery, the data dictionary
	must be locked, but the table must not be dropped. */
	TRX_DICT_OP_INDEX = 2
} trx_dict_op_t;

/**********************************************************************//**
Determine if a transaction is a dictionary operation.
@return	dictionary operation mode */
UNIV_INLINE
enum trx_dict_op
trx_get_dict_operation(
/*===================*/
	const trx_t*	trx)	/*!< in: transaction */
	__attribute__((pure));
/**********************************************************************//**
Flag a transaction a dictionary operation. */
UNIV_INLINE
void
trx_set_dict_operation(
/*===================*/
	trx_t*			trx,	/*!< in/out: transaction */
	enum trx_dict_op	op);	/*!< in: operation, not
					TRX_DICT_OP_NONE */

#ifndef UNIV_HOTBACKUP
/**********************************************************************//**
Determines if the currently running transaction has been interrupted.
@return	TRUE if interrupted */
UNIV_INTERN
ibool
trx_is_interrupted(
/*===============*/
	trx_t*	trx);	/*!< in: transaction */
/**********************************************************************//**
Determines if the currently running transaction is in strict mode.
@return	TRUE if strict */
UNIV_INTERN
ibool
trx_is_strict(
/*==========*/
	trx_t*	trx);	/*!< in: transaction */
#else /* !UNIV_HOTBACKUP */
#define trx_is_interrupted(trx) FALSE
#endif /* !UNIV_HOTBACKUP */

/*******************************************************************//**
Calculates the "weight" of a transaction. The weight of one transaction
is estimated as the number of altered rows + the number of locked rows.
@param t	transaction
@return		transaction weight */
#define TRX_WEIGHT(t)	((t)->undo_no + UT_LIST_GET_LEN((t)->lock.trx_locks))

/*******************************************************************//**
Compares the "weight" (or size) of two transactions. Transactions that
have edited non-transactional tables are considered heavier than ones
that have not.
@return	TRUE if weight(a) >= weight(b) */
UNIV_INTERN
ibool
trx_weight_ge(
/*==========*/
	const trx_t*	a,	/*!< in: the first transaction to be compared */
	const trx_t*	b);	/*!< in: the second transaction to be compared */

/* Maximum length of a string that can be returned by
trx_get_que_state_str(). */
#define TRX_QUE_STATE_STR_MAX_LEN	12 /* "ROLLING BACK" */

/*******************************************************************//**
Retrieves transaction's que state in a human readable string. The string
should not be free()'d or modified.
@return	string in the data segment */
UNIV_INLINE
const char*
trx_get_que_state_str(
/*==================*/
	const trx_t*	trx);	/*!< in: transaction */

typedef struct trx_lock_struct trx_lock_t;

/** Transactions locks and state, these variables are protected by
the lock_sys->mutex and trx_mutex. */

struct trx_lock_struct {
	ulint		n_active_thrs;	/*!< number of active query threads */

	trx_que_t	que_state;	/*!< valid when state
					== TRX_STATE_ACTIVE: TRX_QUE_RUNNING,
					TRX_QUE_LOCK_WAIT, ... */

	lock_t*		wait_lock;	/*!< if trx execution state is
					TRX_QUE_LOCK_WAIT, this points to
					the lock request, otherwise this is
					NULL */
	ulint		deadlock_mark;	/*!< a mark field used in deadlock
					checking algorithm. This is only
					covered by the lock_sys_t::mutex */
	ibool		was_chosen_as_deadlock_victim;
					/*!< when the transaction decides to
				       	wait for a lock, it sets this to FALSE;
					if another transaction chooses this
					transaction as a victim in deadlock
					resolution, it sets this to TRUE */

	time_t		wait_started;	/*!< lock wait started at this time,
					protected only by lock_sys_t::mutex  */

	que_thr_t*	wait_thr;	/*!< query thread beloging to this
					trx that is in QUE_THR_LOCK_WAIT
				       	state */

	mem_heap_t*	lock_heap;	/*!< memory heap for the locks of the
					transaction */

	UT_LIST_BASE_NODE_T(lock_t)
			trx_locks;	/*!< locks reserved by the
				       	transaction */
};

#define TRX_MAGIC_N	91118598

/* When is it unsafe to read the trx_t::state without a covering trx_t::mutex ?
===============================================================================
1. When a transaction is changing its state to TRX_STATE_COMMITTED_IN_MEMORY
during COMMIT. This use case is only relevant during roll back of incomplete
transactions during crash recovery. The reason is because the recovery code
examines the state to determine whether such transactions need to be freed and
does tht asynchronously to the trx_commit() code.

This state transition is covered by both the lock mutex and trx mutex. It is
coupled with trx_t::is_recovered flag. Both must be changed atomically under
the protection of both the previously mentioned mutexes. Therefore the trx_t::mutex
must be acquired by the recovery code to check that trx_t::state along with the
trx_t::is_recovered flag.

Changing the transaction state when a transaction is not waiting for a lock is
safe without any mutex. */

/** The transaction handle; every session has a trx object which is freed only
when the session is freed; in addition there may be session-less transactions
rolling back after a database recovery */

struct trx_struct{
	ulint		magic_n;

	mutex_t		mutex;		/*!< Mutex  protecting the trx_lock_t
				       	fields and state, see below: */

	trx_state_t	state;		/*!< state of the trx from the point
					of view of concurrency control:
					TRX_STATE_ACTIVE, TRX_STATE_COMMITTED_IN_MEMORY,
					... */
	trx_lock_t	lock;		/*!< Information about the transaction
					locks and state. */

	/* These fields are not protected by any mutex. */
	const char*	op_info;	/*!< English text describing the
					current operation, or an empty
					string */
	ulint		isolation_level;/*!< TRX_ISO_REPEATABLE_READ, ... */
	ulint		check_foreigns;	/*!< normally TRUE, but if the user
					wants to suppress foreign key checks,
					(in table imports, for example) we
					set this FALSE */
	ulint		check_unique_secondary;
					/*!< normally TRUE, but if the user
					wants to speed up inserts by
					suppressing unique key checks
					for secondary indexes when we decide
					if we can use the insert buffer for
					them, we set this FALSE */
	ulint		support_xa;	/*!< normally we do the XA two-phase
					commit steps, but by setting this to
					FALSE, one can save CPU time and about
					150 bytes in the undo log size as then
					we skip XA steps */
	ulint		flush_log_later;/* In 2PC, we hold the
					prepare_commit mutex across
					both phases. In that case, we
					defer flush of the logs to disk
					until after we release the
					mutex. */
	ulint		must_flush_log_later;/*!< this flag is set to TRUE in
					trx_commit() if flush_log_later was
				       	TRUE, and there were modifications by
				       	the transaction; in that case we must
				       	flush the log in
				       	trx_commit_complete_for_mysql() */
	ulint		duplicates;	/*!< TRX_DUP_IGNORE | TRX_DUP_REPLACE */
	ulint		active_trans;	/*!< 1 - if a transaction in MySQL
					is active. 2 - if prepare_commit_mutex
					was taken */
	ulint		has_search_latch;
					/*!< TRUE if this trx has latched the
					search system latch in S-mode */
	ulint		search_latch_timeout;
					/*!< If we notice that someone is
					waiting for our S-lock on the search
					latch to be released, we wait in
					row0sel.c for BTR_SEA_TIMEOUT new
					searches until we try to keep
					the search latch again over
					calls from MySQL; this is intended
					to reduce contention on the search
					latch */
	trx_dict_op_t	dict_operation;	/**< @see enum trx_dict_op */

	/* Fields protected by the srv_conc_mutex. */
	ulint		declared_to_be_inside_innodb;
					/*!< this is TRUE if we have declared
					this transaction in
					srv_conc_enter_innodb to be inside the
					InnoDB engine */
	ulint		n_tickets_to_enter_innodb;
					/*!< this can be > 0 only when
					declared_to_... is TRUE; when we come
					to srv_conc_innodb_enter, if the value
					here is > 0, we decrement this by 1 */
	/* Fields protected by dict_operation_lock. The very latch
	it is used to track. */
	ulint		dict_operation_lock_mode;
					/*!< 0, RW_S_LATCH, or RW_X_LATCH:
					the latch mode trx currently holds
					on dict_operation_lock */

	ulint		is_recovered;	/*!< 0=normal transaction,
					1=recovered, must be rolled back,
					protected by the trx_t::mutex */
	trx_id_t	no;		/*!< transaction serialization number ==
					max trx id when the transaction is
					moved to COMMITTED_IN_MEMORY state,
					protected by trx_sys_t::lock */

	time_t		start_time;	/*!< time the trx object was created
					or the state last time became
					TRX_STATE_ACTIVE */
	trx_id_t	id;		/*!< transaction id */
	XID		xid;		/*!< X/Open XA transaction
					identification to identify a
					transaction branch */
	ib_uint64_t	commit_lsn;	/*!< lsn at the time of the commit */
	table_id_t	table_id;	/*!< Table to drop iff dict_operation
					is TRUE, or 0. */
	/*------------------------------*/
	void*		mysql_thd;	/*!< MySQL thread handle corresponding
					to this trx, or NULL */
	const char*	mysql_log_file_name;
					/*!< if MySQL binlog is used, this field
					contains a pointer to the latest file
					name; this is NULL if binlog is not
					used */
	ib_int64_t	mysql_log_offset;/*!< if MySQL binlog is used, this
					 field contains the end offset of the
					 binlog entry */
	os_thread_id_t	mysql_thread_id;/*!< id of the MySQL thread associated
					with this transaction object */
	ulint		mysql_process_no;/*!< since in Linux, 'top' reports
					process id's and not thread id's, we
					store the process number too */
	/*------------------------------*/
	ulint		n_mysql_tables_in_use; /*!< number of Innobase tables
					used in the processing of the current
					SQL statement in MySQL */
	ulint		mysql_n_tables_locked;
					/*!< how many tables the current SQL
					statement uses, except those
					in consistent read */
	/*------------------------------*/
	UT_LIST_NODE_T(trx_t)
			trx_list;	/*!< list of transactions */
	UT_LIST_NODE_T(trx_t)
			mysql_trx_list;	/*!< list of transactions created for
					MySQL */
	/*------------------------------*/
	enum db_err	error_state;	/*!< 0 if no error, otherwise error
					number; NOTE That ONLY the thread
					doing the transaction is allowed to
					set this field: this is NOT protected
					by any mutex */
	const dict_index_t*error_info;	/*!< if the error number indicates a
					duplicate key error, a pointer to
					the problematic index is stored here */
	ulint		error_key_num;	/*!< if the index creation fails to a
					duplicate key error, a mysql key
					number of that index is stored here */
	sess_t*		sess;		/*!< session of the trx, NULL if none */
	que_t*		graph;		/*!< query currently run in the session,
					or NULL if none; NOTE that the query
					belongs to the session, and it can
					survive over a transaction commit, if
					it is a stored procedure with a COMMIT
					WORK statement, for instance */
	que_t*		graph_before_signal_handling;
					/*!< value of graph when signal handling
					for this trx started: this is used to
					return control to the original query
					graph for error processing */
	mem_heap_t*	global_read_view_heap;
					/*!< memory heap for the global read
					view */
	read_view_t*	global_read_view;
					/*!< consistent read view associated
					to a transaction or NULL */
	read_view_t*	read_view;	/*!< consistent read view used in the
					transaction or NULL, this read view
					if defined can be normal read view
					associated to a transaction (i.e.
					same as global_read_view) or read view
					associated to a cursor */
	/*------------------------------*/
	UT_LIST_BASE_NODE_T(trx_named_savept_t)
			trx_savepoints;	/*!< savepoints set with SAVEPOINT ...,
					oldest first */
	/*------------------------------*/
	mutex_t		undo_mutex;	/*!< mutex protecting the fields in this
					section (down to undo_no_arr), EXCEPT
					last_sql_stat_start, which can be
					accessed only when we know that there
					cannot be any activity in the undo
					logs! */
	undo_no_t	undo_no;	/*!< next undo log record number to
					assign; since the undo log is
					private for a transaction, this
					is a simple ascending sequence
					with no gaps; thus it represents
					the number of modified/inserted
					rows in a transaction */
	trx_savept_t	last_sql_stat_start;
					/*!< undo_no when the last sql statement
					was started: in case of an error, trx
					is rolled back down to this undo
					number; see note at undo_mutex! */
	trx_rseg_t*	rseg;		/*!< rollback segment assigned to the
					transaction, or NULL if not assigned
					yet */
	trx_undo_t*	insert_undo;	/*!< pointer to the insert undo log, or
					NULL if no inserts performed yet */
	trx_undo_t*	update_undo;	/*!< pointer to the update undo log, or
					NULL if no update performed yet */
	undo_no_t	roll_limit;	/*!< least undo number to undo during
					a rollback */
	ulint		pages_undone;	/*!< number of undo log pages undone
					since the last undo log truncation */
	trx_undo_arr_t*	undo_no_arr;	/*!< array of undo numbers of undo log
					records which are currently processed
					by a rollback operation */
	/*------------------------------*/
	ulint		n_autoinc_rows;	/*!< no. of AUTO-INC rows required for
					an SQL statement. This is useful for
					multi-row INSERTs */
	ib_vector_t*    autoinc_locks;  /* AUTOINC locks held by this
					transaction. Note that these are
					also in the lock list trx_locks. This
					vector needs to be freed explicitly
					when the trx_t instance is desrtoyed */
	/*------------------------------*/
	char detailed_error[256];	/*!< detailed error message for last
					error, or empty. */
};

/* Transaction isolation levels (trx->isolation_level) */
#define TRX_ISO_READ_UNCOMMITTED	0	/* dirty read: non-locking
						SELECTs are performed so that
						we do not look at a possible
						earlier version of a record;
						thus they are not 'consistent'
						reads under this isolation
						level; otherwise like level
						2 */

#define TRX_ISO_READ_COMMITTED		1	/* somewhat Oracle-like
						isolation, except that in
						range UPDATE and DELETE we
						must block phantom rows
						with next-key locks;
						SELECT ... FOR UPDATE and ...
						LOCK IN SHARE MODE only lock
						the index records, NOT the
						gaps before them, and thus
						allow free inserting;
						each consistent read reads its
						own snapshot */

#define TRX_ISO_REPEATABLE_READ		2	/* this is the default;
						all consistent reads in the
						same trx read the same
						snapshot;
						full next-key locking used
						in locking reads to block
						insertions into gaps */

#define TRX_ISO_SERIALIZABLE		3	/* all plain SELECTs are
						converted to LOCK IN SHARE
						MODE reads */

/* Treatment of duplicate values (trx->duplicates; for example, in inserts).
Multiple flags can be combined with bitwise OR. */
#define TRX_DUP_IGNORE	1	/* duplicate rows are to be updated */
#define TRX_DUP_REPLACE	2	/* duplicate rows are to be replaced */


/* Types of a trx signal */
#define TRX_SIG_NO_SIGNAL		0
#define TRX_SIG_TOTAL_ROLLBACK		1
#define TRX_SIG_ROLLBACK_TO_SAVEPT	2
#define TRX_SIG_COMMIT			3
#define TRX_SIG_BREAK_EXECUTION		5

/* Sender types of a signal */
#define TRX_SIG_SELF		0	/* sent by the session itself, or
					by an error occurring within this
					session */
#define TRX_SIG_OTHER_SESS	1	/* sent by another session (which
					must hold rights to this) */

/** Commit node states */
enum commit_node_state {
	COMMIT_NODE_SEND = 1,	/*!< about to send a commit signal to
				the transaction */
	COMMIT_NODE_WAIT	/*!< commit signal sent to the transaction,
				waiting for completion */
};

/** Commit command node in a query graph */
struct commit_node_struct{
	que_common_t	common;	/*!< node type: QUE_NODE_COMMIT */
	enum commit_node_state
			state;	/*!< node execution state */
};


/** Test if trx->mutex is owned. */
#define trx_mutex_own(t) mutex_own(&t->mutex)

/** Acquire the trx->mutex. */
#define trx_mutex_enter(t) do {			\
	mutex_enter(&t->mutex);			\
} while (0)

/** Release the trx->mutex. */
#define trx_mutex_exit(t) do {			\
	mutex_exit(&t->mutex);			\
} while (0)

#ifndef UNIV_NONINL
#include "trx0trx.ic"
#endif
#endif /* !UNIV_HOTBACKUP */

#endif
