/*****************************************************************************

Copyright (c) 2010, 2012, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/**************************************************//**
@file include/row0ftsort.h
Create Full Text Index with (parallel) merge sort

Created 10/13/2010 Jimmy Yang
*******************************************************/

#ifndef row0ftsort_h
#define row0ftsort_h

#include "univ.i"
#include "data0data.h"
#include "dict0types.h"
#include "row0mysql.h"
#include "fts0fts.h"
#include "fts0types.h"
#include "fts0priv.h"
#include "row0merge.h"

/** This structure defineds information the scan thread will fetch
and put to the linked list for parallel tokenization/sort threads
to process */
typedef struct fts_doc_item     fts_doc_item_t;

/** Information about temporary files used in merge sort */
struct fts_doc_item {
	dfield_t*	field;		/*!< field contains document string */
	doc_id_t	doc_id;		/*!< document ID */
	UT_LIST_NODE_T(fts_doc_item_t)	doc_list;
					/*!< list of doc items */
};

/** This defines the list type that scan thread would feed the parallel
tokenization threads and sort threads. */
typedef UT_LIST_BASE_NODE_T(fts_doc_item_t)     fts_doc_list_t;

#define FTS_NUM_AUX_INDEX	6
#define FTS_PLL_MERGE		1

/** Sort information passed to each individual parallel sort thread */
struct fts_psort_t;

/** Common info passed to each parallel sort thread */
struct fts_psort_common_t {
	row_merge_dup_t*	dup;		/*!< descriptor of FTS index */
	dict_table_t*		new_table;	/*!< source table */
	trx_t*			trx;		/*!< transaction */
	fts_psort_t*		all_info;	/*!< all parallel sort info */
	os_event_t		sort_event;	/*!< sort event */
	os_event_t		merge_event;	/*!< merge event */
	ibool			opt_doc_id_size;/*!< whether to use 4 bytes
						instead of 8 bytes integer to
						store Doc ID during sort, if
						Doc ID will not be big enough
						to use 8 bytes value */
};

struct fts_psort_t {
	ulint			psort_id;	/*!< Parallel sort ID */
	row_merge_buf_t*	merge_buf[FTS_NUM_AUX_INDEX];
						/*!< sort buffer */
	merge_file_t*		merge_file[FTS_NUM_AUX_INDEX];
						/*!< sort file */
	row_merge_block_t*	merge_block[FTS_NUM_AUX_INDEX];
						/*!< buffer to write to file */
	row_merge_block_t*	block_alloc[FTS_NUM_AUX_INDEX];
						/*!< buffer to allocated */
	ulint			child_status;	/*!< child thread status */
	ulint			state;		/*!< child thread state */
	fts_doc_list_t		fts_doc_list;	/*!< doc list to process */
	fts_psort_common_t*	psort_common;	/*!< ptr to all psort info */
};

/** Structure stores information from string tokenization operation */
struct fts_tokenize_ctx {
	ulint			processed_len;  /*!< processed string length */
	ulint			init_pos;       /*!< doc start position */
	ulint			buf_used;       /*!< the sort buffer (ID) when
						tokenization stops, which
						could due to sort buffer full */
	ulint			rows_added[FTS_NUM_AUX_INDEX];
						/*!< number of rows added for
						each FTS index partition */
	ib_rbt_t*		cached_stopword;/*!< in: stopword list */
	dfield_t		sort_field[FTS_NUM_FIELDS_SORT];
						/*!< in: sort field */
};

typedef struct fts_tokenize_ctx fts_tokenize_ctx_t;

/** Structure stores information needed for the insertion phase of FTS
parallel sort. */
struct fts_psort_insert {
	trx_t*		trx;		/*!< Transaction used for insertion */
	que_t**		ins_graph;	/*!< insert graph */
	fts_table_t	fts_table;	/*!< auxiliary table */
	CHARSET_INFO*	charset;	/*!< charset info */
	mem_heap_t*	heap;		/*!< heap */
	ibool		opt_doc_id_size;/*!< Whether to use smaller (4 bytes)
					integer for Doc ID */
};

typedef struct fts_psort_insert	fts_psort_insert_t;


/** status bit used for communication between parent and child thread */
#define FTS_PARENT_COMPLETE	1
#define FTS_CHILD_COMPLETE	1
#define FTS_CHILD_EXITING	2

/** Print some debug information */
#define	FTSORT_PRINT

#ifdef	FTSORT_PRINT
#define	DEBUG_FTS_SORT_PRINT(str)		\
	do {					\
		ut_print_timestamp(stderr);	\
		fprintf(stderr, str);		\
	} while (0)
#else
#define DEBUG_FTS_SORT_PRINT(str)
#endif	/* FTSORT_PRINT */

/*************************************************************//**
Create a temporary "fts sort index" used to merge sort the
tokenized doc string. The index has three "fields":

1) Tokenized word,
2) Doc ID
3) Word's position in original 'doc'.

@return dict_index_t structure for the fts sort index */
UNIV_INTERN
dict_index_t*
row_merge_create_fts_sort_index(
/*============================*/
	dict_index_t*		index,	/*!< in: Original FTS index
					based on which this sort index
					is created */
	const dict_table_t*	table,	/*!< in: table that FTS index
					is being created on */
	ibool*			opt_doc_id_size);
					/*!< out: whether to use 4 bytes
					instead of 8 bytes integer to
					store Doc ID during sort */

/********************************************************************//**
Initialize FTS parallel sort structures.
@return TRUE if all successful */
UNIV_INTERN
ibool
row_fts_psort_info_init(
/*====================*/
	trx_t*			trx,	/*!< in: transaction */
	row_merge_dup_t*	dup,	/*!< in,own: descriptor of
					FTS index being created */
	const dict_table_t*	new_table,/*!< in: table where indexes are
					created */
	ibool			opt_doc_id_size,
					/*!< in: whether to use 4 bytes
					instead of 8 bytes integer to
					store Doc ID during sort */
	fts_psort_t**		psort,	/*!< out: parallel sort info to be
					instantiated */
	fts_psort_t**		merge)	/*!< out: parallel merge info
					to be instantiated */
	__attribute__((nonnull));
/********************************************************************//**
Clean up and deallocate FTS parallel sort structures, and close
temparary merge sort files */
UNIV_INTERN
void
row_fts_psort_info_destroy(
/*=======================*/
	fts_psort_t*	psort_info,	/*!< parallel sort info */
	fts_psort_t*	merge_info);	/*!< parallel merge info */
/********************************************************************//**
Free up merge buffers when merge sort is done */
UNIV_INTERN
void
row_fts_free_pll_merge_buf(
/*=======================*/
	fts_psort_t*	psort_info);	/*!< in: parallel sort info */

/*********************************************************************//**
Function performs parallel tokenization of the incoming doc strings.
@return OS_THREAD_DUMMY_RETURN */
UNIV_INTERN
os_thread_ret_t
fts_parallel_tokenization(
/*======================*/
	void*		arg);		/*!< in: psort_info for the thread */
/*********************************************************************//**
Start the parallel tokenization and parallel merge sort */
UNIV_INTERN
void
row_fts_start_psort(
/*================*/
	fts_psort_t*	psort_info);	/*!< in: parallel sort info */
/*********************************************************************//**
Function performs the merge and insertion of the sorted records.
@return OS_THREAD_DUMMY_RETURN */
UNIV_INTERN
os_thread_ret_t
fts_parallel_merge(
/*===============*/
	void*		arg);		/*!< in: parallel merge info */
/*********************************************************************//**
Kick off the parallel merge and insert thread */
UNIV_INTERN
void
row_fts_start_parallel_merge(
/*=========================*/
	fts_psort_t*	merge_info);	/*!< in: parallel sort info */
/********************************************************************//**
Read sorted FTS data files and insert data tuples to auxillary tables.
@return DB_SUCCESS or error number */
UNIV_INTERN
void
row_fts_insert_tuple(
/*=================*/
	fts_psort_insert_t*
			ins_ctx,        /*!< in: insert context */
	fts_tokenizer_word_t* word,	/*!< in: last processed
					tokenized word */
	ib_vector_t*	positions,	/*!< in: word position */
	doc_id_t*	in_doc_id,	/*!< in: last item doc id */
	dtuple_t*	dtuple);	/*!< in: entry to insert */
/********************************************************************//**
Propagate a newly added record up one level in the selection tree
@return parent where this value propagated to */
UNIV_INTERN
int
row_merge_fts_sel_propagate(
/*========================*/
	int		propogated,	/*<! in: tree node propagated */
	int*		sel_tree,	/*<! in: selection tree */
	ulint		level,		/*<! in: selection tree level */
	const mrec_t**	 mrec,		/*<! in: sort record */
	ulint**		offsets,	/*<! in: record offsets */
	dict_index_t*	index);		/*<! in: FTS index */
/********************************************************************//**
Read sorted file containing index data tuples and insert these data
tuples to the index
@return DB_SUCCESS or error number */
UNIV_INTERN
dberr_t
row_fts_merge_insert(
/*=================*/
	dict_index_t*	index,		/*!< in: index */
	dict_table_t*	table,		/*!< in: new table */
	fts_psort_t*	psort_info,	/*!< parallel sort info */
	ulint		id)		/* !< in: which auxiliary table's data
					to insert to */
	__attribute__((nonnull));
#endif /* row0ftsort_h */
