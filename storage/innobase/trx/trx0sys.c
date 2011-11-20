/*****************************************************************************

Copyright (c) 1996, 2011, Oracle and/or its affiliates. All Rights Reserved.

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
@file trx/trx0sys.c
Transaction system

Created 3/26/1996 Heikki Tuuri
*******************************************************/

#include "trx0sys.h"

#ifdef UNIV_NONINL
#include "trx0sys.ic"
#endif

#ifndef UNIV_HOTBACKUP
#include "fsp0fsp.h"
#include "mtr0log.h"
#include "mtr0log.h"
#include "trx0trx.h"
#include "trx0rseg.h"
#include "trx0undo.h"
#include "srv0srv.h"
#include "srv0start.h"
#include "trx0purge.h"
#include "log0log.h"
#include "log0recv.h"
#include "os0file.h"
#include "read0read.h"

/** The file format tag structure with id and name. */
struct file_format_struct {
	ulint		id;		/*!< id of the file format */
	const char*	name;		/*!< text representation of the
					file format */
	mutex_t		mutex;		/*!< covers changes to the above
					fields */
};

/** The file format tag */
typedef struct file_format_struct	file_format_t;

/** The transaction system */
UNIV_INTERN trx_sys_t*		trx_sys		= NULL;
/** The doublewrite buffer */
UNIV_INTERN trx_doublewrite_t*	trx_doublewrite = NULL;

/** The following is set to TRUE when we are upgrading from pre-4.1
format data files to the multiple tablespaces format data files */
UNIV_INTERN ibool	trx_doublewrite_must_reset_space_ids	= FALSE;
/** Set to TRUE when the doublewrite buffer is being created */
UNIV_INTERN ibool	trx_doublewrite_buf_is_being_created = FALSE;

/** The following is TRUE when we are using the database in the
post-4.1 format, i.e., we have successfully upgraded, or have created
a new database installation */
UNIV_INTERN ibool	trx_sys_multiple_tablespace_format	= FALSE;

/** In a MySQL replication slave, in crash recovery we store the master log
file name and position here. */
/* @{ */
/** Master binlog file name */
UNIV_INTERN char	trx_sys_mysql_master_log_name[TRX_SYS_MYSQL_LOG_NAME_LEN];
/** Master binlog file position.  We have successfully got the updates
up to this position.  -1 means that no crash recovery was needed, or
there was no master log position info inside InnoDB.*/
UNIV_INTERN ib_int64_t	trx_sys_mysql_master_log_pos	= -1;
/* @} */

/** If this MySQL server uses binary logging, after InnoDB has been inited
and if it has done a crash recovery, we store the binlog file name and position
here. */
/* @{ */
/** Binlog file name */
UNIV_INTERN char	trx_sys_mysql_bin_log_name[TRX_SYS_MYSQL_LOG_NAME_LEN];
/** Binlog file position, or -1 if unknown */
UNIV_INTERN ib_int64_t	trx_sys_mysql_bin_log_pos	= -1;
/* @} */
#endif /* !UNIV_HOTBACKUP */

/** List of animal names representing file format. */
static const char*	file_format_name_map[] = {
	"Antelope",
	"Barracuda",
	"Cheetah",
	"Dragon",
	"Elk",
	"Fox",
	"Gazelle",
	"Hornet",
	"Impala",
	"Jaguar",
	"Kangaroo",
	"Leopard",
	"Moose",
	"Nautilus",
	"Ocelot",
	"Porpoise",
	"Quail",
	"Rabbit",
	"Shark",
	"Tiger",
	"Urchin",
	"Viper",
	"Whale",
	"Xenops",
	"Yak",
	"Zebra"
};

/** The number of elements in the file format name array. */
static const ulint	FILE_FORMAT_NAME_N
	= sizeof(file_format_name_map) / sizeof(file_format_name_map[0]);

#ifdef UNIV_PFS_MUTEX
/* Key to register the mutex with performance schema */
UNIV_INTERN mysql_pfs_key_t	trx_doublewrite_mutex_key;
UNIV_INTERN mysql_pfs_key_t	file_format_max_mutex_key;
UNIV_INTERN mysql_pfs_key_t	trx_sys_mutex_key;
#endif /* UNIV_PFS_RWLOCK */

#ifndef UNIV_HOTBACKUP
/** This is used to track the maximum file format id known to InnoDB. It's
updated via SET GLOBAL innodb_file_format_max = 'x' or when we open
or create a table. */
static	file_format_t	file_format_max;

/****************************************************************//**
Determines if a page number is located inside the doublewrite buffer.
@return TRUE if the location is inside the two blocks of the
doublewrite buffer */
UNIV_INTERN
ibool
trx_doublewrite_page_inside(
/*========================*/
	ulint	page_no)	/*!< in: page number */
{
	if (trx_doublewrite == NULL) {

		return(FALSE);
	}

	if (page_no >= trx_doublewrite->block1
	    && page_no < trx_doublewrite->block1
	    + TRX_SYS_DOUBLEWRITE_BLOCK_SIZE) {
		return(TRUE);
	}

	if (page_no >= trx_doublewrite->block2
	    && page_no < trx_doublewrite->block2
	    + TRX_SYS_DOUBLEWRITE_BLOCK_SIZE) {
		return(TRUE);
	}

	return(FALSE);
}

/****************************************************************//**
Creates or initialializes the doublewrite buffer at a database start. */
static
void
trx_doublewrite_init(
/*=================*/
	byte*	doublewrite)	/*!< in: pointer to the doublewrite buf
				header on trx sys page */
{
	ulint	buf_size;

	trx_doublewrite = mem_zalloc(sizeof(trx_doublewrite_t));

	/* There are two blocks of same size in the doublewrite
	buffer. */
	buf_size = 2 * TRX_SYS_DOUBLEWRITE_BLOCK_SIZE;

	/* There must be atleast one buffer for single page writes
	and one buffer for batch writes. */
	ut_a(srv_doublewrite_batch_size > 0
	     && srv_doublewrite_batch_size < buf_size);

	/* Since we now start to use the doublewrite buffer, no need to call
	fsync() after every write to a data file */
#ifdef UNIV_DO_FLUSH
	os_do_not_call_flush_at_each_write = TRUE;
#endif /* UNIV_DO_FLUSH */

	mutex_create(trx_doublewrite_mutex_key,
		     &trx_doublewrite->mutex, SYNC_DOUBLEWRITE);

	trx_doublewrite->first_free = 0;
	trx_doublewrite->n_reserved = 0;

	trx_doublewrite->block1 = mach_read_from_4(
		doublewrite + TRX_SYS_DOUBLEWRITE_BLOCK1);
	trx_doublewrite->block2 = mach_read_from_4(
		doublewrite + TRX_SYS_DOUBLEWRITE_BLOCK2);

	trx_doublewrite->in_use = mem_zalloc(buf_size * sizeof(ibool));

	trx_doublewrite->write_buf_unaligned = ut_malloc(
		(1 + buf_size) * UNIV_PAGE_SIZE);
	trx_doublewrite->write_buf = ut_align(
		trx_doublewrite->write_buf_unaligned, UNIV_PAGE_SIZE);

	trx_doublewrite->buf_block_arr = mem_zalloc(
		buf_size * sizeof(void*));
}

/****************************************************************//**
Marks the trx sys header when we have successfully upgraded to the >= 4.1.x
multiple tablespace format. */
UNIV_INTERN
void
trx_sys_mark_upgraded_to_multiple_tablespaces(void)
/*===============================================*/
{
	buf_block_t*	block;
	byte*		doublewrite;
	mtr_t		mtr;

	/* We upgraded to 4.1.x and reset the space id fields in the
	doublewrite buffer. Let us mark to the trx_sys header that the upgrade
	has been done. */

	mtr_start(&mtr);

	block = buf_page_get(TRX_SYS_SPACE, 0, TRX_SYS_PAGE_NO,
			     RW_X_LATCH, &mtr);
	buf_block_dbg_add_level(block, SYNC_NO_ORDER_CHECK);

	doublewrite = buf_block_get_frame(block) + TRX_SYS_DOUBLEWRITE;

	mlog_write_ulint(doublewrite + TRX_SYS_DOUBLEWRITE_SPACE_ID_STORED,
			 TRX_SYS_DOUBLEWRITE_SPACE_ID_STORED_N,
			 MLOG_4BYTES, &mtr);
	mtr_commit(&mtr);

	/* Flush the modified pages to disk and make a checkpoint */
	log_make_checkpoint_at(LSN_MAX, TRUE);

	trx_sys_multiple_tablespace_format = TRUE;
}

/****************************************************************//**
Creates the doublewrite buffer to a new InnoDB installation. The header of the
doublewrite buffer is placed on the trx system header page. */
UNIV_INTERN
void
trx_sys_create_doublewrite_buf(void)
/*================================*/
{
	buf_block_t*	block;
	buf_block_t*	block2;
#ifdef UNIV_SYNC_DEBUG
	buf_block_t*	new_block;
#endif /* UNIV_SYNC_DEBUG */
	byte*	doublewrite;
	byte*	fseg_header;
	ulint	page_no;
	ulint	prev_page_no;
	ulint	i;
	mtr_t	mtr;

	if (trx_doublewrite) {
		/* Already inited */

		return;
	}

start_again:
	mtr_start(&mtr);
	trx_doublewrite_buf_is_being_created = TRUE;

	block = buf_page_get(TRX_SYS_SPACE, 0, TRX_SYS_PAGE_NO,
			     RW_X_LATCH, &mtr);
	buf_block_dbg_add_level(block, SYNC_NO_ORDER_CHECK);

	doublewrite = buf_block_get_frame(block) + TRX_SYS_DOUBLEWRITE;

	if (mach_read_from_4(doublewrite + TRX_SYS_DOUBLEWRITE_MAGIC)
	    == TRX_SYS_DOUBLEWRITE_MAGIC_N) {
		/* The doublewrite buffer has already been created:
		just read in some numbers */

		trx_doublewrite_init(doublewrite);

		mtr_commit(&mtr);
		trx_doublewrite_buf_is_being_created = FALSE;
	} else {
		fprintf(stderr,
			"InnoDB: Doublewrite buffer not found:"
			" creating new\n");

		if (buf_pool_get_curr_size()
		    < ((2 * TRX_SYS_DOUBLEWRITE_BLOCK_SIZE
			+ FSP_EXTENT_SIZE / 2 + 100)
		       * UNIV_PAGE_SIZE)) {
			fprintf(stderr,
				"InnoDB: Cannot create doublewrite buffer:"
				" you must\n"
				"InnoDB: increase your buffer pool size.\n"
				"InnoDB: Cannot continue operation.\n");

			exit(1);
		}

		block2 = fseg_create(TRX_SYS_SPACE, TRX_SYS_PAGE_NO,
				     TRX_SYS_DOUBLEWRITE
				     + TRX_SYS_DOUBLEWRITE_FSEG, &mtr);

		/* fseg_create acquires a second latch on the page,
		therefore we must declare it: */

		buf_block_dbg_add_level(block2, SYNC_NO_ORDER_CHECK);

		if (block2 == NULL) {
			fprintf(stderr,
				"InnoDB: Cannot create doublewrite buffer:"
				" you must\n"
				"InnoDB: increase your tablespace size.\n"
				"InnoDB: Cannot continue operation.\n");

			/* We exit without committing the mtr to prevent
			its modifications to the database getting to disk */

			exit(1);
		}

		fseg_header = buf_block_get_frame(block)
			+ TRX_SYS_DOUBLEWRITE + TRX_SYS_DOUBLEWRITE_FSEG;
		prev_page_no = 0;

		for (i = 0; i < 2 * TRX_SYS_DOUBLEWRITE_BLOCK_SIZE
			     + FSP_EXTENT_SIZE / 2; i++) {
			page_no = fseg_alloc_free_page(fseg_header,
						       prev_page_no + 1,
						       FSP_UP, &mtr);
			if (page_no == FIL_NULL) {
				fprintf(stderr,
					"InnoDB: Cannot create doublewrite"
					" buffer: you must\n"
					"InnoDB: increase your"
					" tablespace size.\n"
					"InnoDB: Cannot continue operation.\n"
					);

				exit(1);
			}

			/* We read the allocated pages to the buffer pool;
			when they are written to disk in a flush, the space
			id and page number fields are also written to the
			pages. When we at database startup read pages
			from the doublewrite buffer, we know that if the
			space id and page number in them are the same as
			the page position in the tablespace, then the page
			has not been written to in doublewrite. */

#ifdef UNIV_SYNC_DEBUG
			new_block =
#endif /* UNIV_SYNC_DEBUG */
			buf_page_get(TRX_SYS_SPACE, 0, page_no,
				     RW_X_LATCH, &mtr);
			buf_block_dbg_add_level(new_block,
						SYNC_NO_ORDER_CHECK);

			if (i == FSP_EXTENT_SIZE / 2) {
				ut_a(page_no == FSP_EXTENT_SIZE);
				mlog_write_ulint(doublewrite
						 + TRX_SYS_DOUBLEWRITE_BLOCK1,
						 page_no, MLOG_4BYTES, &mtr);
				mlog_write_ulint(doublewrite
						 + TRX_SYS_DOUBLEWRITE_REPEAT
						 + TRX_SYS_DOUBLEWRITE_BLOCK1,
						 page_no, MLOG_4BYTES, &mtr);
			} else if (i == FSP_EXTENT_SIZE / 2
				   + TRX_SYS_DOUBLEWRITE_BLOCK_SIZE) {
				ut_a(page_no == 2 * FSP_EXTENT_SIZE);
				mlog_write_ulint(doublewrite
						 + TRX_SYS_DOUBLEWRITE_BLOCK2,
						 page_no, MLOG_4BYTES, &mtr);
				mlog_write_ulint(doublewrite
						 + TRX_SYS_DOUBLEWRITE_REPEAT
						 + TRX_SYS_DOUBLEWRITE_BLOCK2,
						 page_no, MLOG_4BYTES, &mtr);
			} else if (i > FSP_EXTENT_SIZE / 2) {
				ut_a(page_no == prev_page_no + 1);
			}

			prev_page_no = page_no;
		}

		mlog_write_ulint(doublewrite + TRX_SYS_DOUBLEWRITE_MAGIC,
				 TRX_SYS_DOUBLEWRITE_MAGIC_N,
				 MLOG_4BYTES, &mtr);
		mlog_write_ulint(doublewrite + TRX_SYS_DOUBLEWRITE_MAGIC
				 + TRX_SYS_DOUBLEWRITE_REPEAT,
				 TRX_SYS_DOUBLEWRITE_MAGIC_N,
				 MLOG_4BYTES, &mtr);

		mlog_write_ulint(doublewrite
				 + TRX_SYS_DOUBLEWRITE_SPACE_ID_STORED,
				 TRX_SYS_DOUBLEWRITE_SPACE_ID_STORED_N,
				 MLOG_4BYTES, &mtr);
		mtr_commit(&mtr);

		/* Flush the modified pages to disk and make a checkpoint */
		log_make_checkpoint_at(LSN_MAX, TRUE);

		/* Remove doublewrite pages from LRU */
		buf_pool_invalidate();

		fprintf(stderr, "InnoDB: Doublewrite buffer created\n");

		trx_sys_multiple_tablespace_format = TRUE;

		goto start_again;
	}
}

/****************************************************************//**
At a database startup initializes the doublewrite buffer memory structure if
we already have a doublewrite buffer created in the data files. If we are
upgrading to an InnoDB version which supports multiple tablespaces, then this
function performs the necessary update operations. If we are in a crash
recovery, this function uses a possible doublewrite buffer to restore
half-written pages in the data files. */
UNIV_INTERN
void
trx_sys_doublewrite_init_or_restore_pages(
/*======================================*/
	ibool	restore_corrupt_pages)	/*!< in: TRUE=restore pages */
{
	byte*	buf;
	byte*	read_buf;
	byte*	unaligned_read_buf;
	ulint	block1;
	ulint	block2;
	ulint	source_page_no;
	byte*	page;
	byte*	doublewrite;
	ulint	space_id;
	ulint	page_no;
	ulint	i;

	/* We do the file i/o past the buffer pool */

	unaligned_read_buf = ut_malloc(2 * UNIV_PAGE_SIZE);
	read_buf = ut_align(unaligned_read_buf, UNIV_PAGE_SIZE);

	/* Read the trx sys header to check if we are using the doublewrite
	buffer */

	fil_io(OS_FILE_READ, TRUE, TRX_SYS_SPACE, 0, TRX_SYS_PAGE_NO, 0,
	       UNIV_PAGE_SIZE, read_buf, NULL);
	doublewrite = read_buf + TRX_SYS_DOUBLEWRITE;

	if (mach_read_from_4(doublewrite + TRX_SYS_DOUBLEWRITE_MAGIC)
	    == TRX_SYS_DOUBLEWRITE_MAGIC_N) {
		/* The doublewrite buffer has been created */

		trx_doublewrite_init(doublewrite);

		block1 = trx_doublewrite->block1;
		block2 = trx_doublewrite->block2;

		buf = trx_doublewrite->write_buf;
	} else {
		goto leave_func;
	}

	if (mach_read_from_4(doublewrite + TRX_SYS_DOUBLEWRITE_SPACE_ID_STORED)
	    != TRX_SYS_DOUBLEWRITE_SPACE_ID_STORED_N) {

		/* We are upgrading from a version < 4.1.x to a version where
		multiple tablespaces are supported. We must reset the space id
		field in the pages in the doublewrite buffer because starting
		from this version the space id is stored to
		FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID. */

		trx_doublewrite_must_reset_space_ids = TRUE;

		fprintf(stderr,
			"InnoDB: Resetting space id's in the"
			" doublewrite buffer\n");
	} else {
		trx_sys_multiple_tablespace_format = TRUE;
	}

	/* Read the pages from the doublewrite buffer to memory */

	fil_io(OS_FILE_READ, TRUE, TRX_SYS_SPACE, 0, block1, 0,
	       TRX_SYS_DOUBLEWRITE_BLOCK_SIZE * UNIV_PAGE_SIZE,
	       buf, NULL);
	fil_io(OS_FILE_READ, TRUE, TRX_SYS_SPACE, 0, block2, 0,
	       TRX_SYS_DOUBLEWRITE_BLOCK_SIZE * UNIV_PAGE_SIZE,
	       buf + TRX_SYS_DOUBLEWRITE_BLOCK_SIZE * UNIV_PAGE_SIZE,
	       NULL);
	/* Check if any of these pages is half-written in data files, in the
	intended position */

	page = buf;

	for (i = 0; i < TRX_SYS_DOUBLEWRITE_BLOCK_SIZE * 2; i++) {

		page_no = mach_read_from_4(page + FIL_PAGE_OFFSET);

		if (trx_doublewrite_must_reset_space_ids) {

			space_id = 0;
			mach_write_to_4(page
					+ FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID, 0);
			/* We do not need to calculate new checksums for the
			pages because the field .._SPACE_ID does not affect
			them. Write the page back to where we read it from. */

			if (i < TRX_SYS_DOUBLEWRITE_BLOCK_SIZE) {
				source_page_no = block1 + i;
			} else {
				source_page_no = block2
					+ i - TRX_SYS_DOUBLEWRITE_BLOCK_SIZE;
			}

			fil_io(OS_FILE_WRITE, TRUE, 0, 0, source_page_no, 0,
			       UNIV_PAGE_SIZE, page, NULL);
			/* printf("Resetting space id in page %lu\n",
			source_page_no); */
		} else {
			space_id = mach_read_from_4(
				page + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);
		}

		if (!restore_corrupt_pages) {
			/* The database was shut down gracefully: no need to
			restore pages */

		} else if (!fil_tablespace_exists_in_mem(space_id)) {
			/* Maybe we have dropped the single-table tablespace
			and this page once belonged to it: do nothing */

		} else if (!fil_check_adress_in_tablespace(space_id,
							   page_no)) {
			fprintf(stderr,
				"InnoDB: Warning: a page in the"
				" doublewrite buffer is not within space\n"
				"InnoDB: bounds; space id %lu"
				" page number %lu, page %lu in"
				" doublewrite buf.\n",
				(ulong) space_id, (ulong) page_no, (ulong) i);

		} else if (space_id == TRX_SYS_SPACE
			   && ((page_no >= block1
				&& page_no
				< block1 + TRX_SYS_DOUBLEWRITE_BLOCK_SIZE)
			       || (page_no >= block2
				   && page_no
				   < (block2
				      + TRX_SYS_DOUBLEWRITE_BLOCK_SIZE)))) {

			/* It is an unwritten doublewrite buffer page:
			do nothing */
		} else {
			ulint	zip_size = fil_space_get_zip_size(space_id);

			/* Read in the actual page from the file */
			fil_io(OS_FILE_READ, TRUE, space_id, zip_size,
			       page_no, 0,
			       zip_size ? zip_size : UNIV_PAGE_SIZE,
			       read_buf, NULL);

			/* Check if the page is corrupt */

			if (UNIV_UNLIKELY
			    (buf_page_is_corrupted(read_buf, zip_size))) {

				fprintf(stderr,
					"InnoDB: Warning: database page"
					" corruption or a failed\n"
					"InnoDB: file read of"
					" space %lu page %lu.\n"
					"InnoDB: Trying to recover it from"
					" the doublewrite buffer.\n",
					(ulong) space_id, (ulong) page_no);

				if (buf_page_is_corrupted(page, zip_size)) {
					fprintf(stderr,
						"InnoDB: Dump of the page:\n");
					buf_page_print(read_buf, zip_size);
					fprintf(stderr,
						"InnoDB: Dump of"
						" corresponding page"
						" in doublewrite buffer:\n");
					buf_page_print(page, zip_size);

					fprintf(stderr,
						"InnoDB: Also the page in the"
						" doublewrite buffer"
						" is corrupt.\n"
						"InnoDB: Cannot continue"
						" operation.\n"
						"InnoDB: You can try to"
						" recover the database"
						" with the my.cnf\n"
						"InnoDB: option:\n"
						"InnoDB:"
						" innodb_force_recovery=6\n");
					exit(1);
				}

				/* Write the good page from the
				doublewrite buffer to the intended
				position */

				fil_io(OS_FILE_WRITE, TRUE, space_id,
				       zip_size, page_no, 0,
				       zip_size ? zip_size : UNIV_PAGE_SIZE,
				       page, NULL);
				fprintf(stderr,
					"InnoDB: Recovered the page from"
					" the doublewrite buffer.\n");
			}
		}

		page += UNIV_PAGE_SIZE;
	}

	fil_flush_file_spaces(FIL_TABLESPACE);

leave_func:
	ut_free(unaligned_read_buf);
}

#ifdef UNIV_DEBUG
/****************************************************************//**
Checks whether a trx is in one of rw_trx_list or ro_trx_list.
@return	TRUE if is in */
UNIV_INTERN
ibool
trx_in_trx_list(
/*============*/
	const trx_t*	in_trx)	/*!< in: transaction */
{
	const trx_t*	trx;
	trx_list_t*	trx_list;

	/* Non-locking autocommits should not hold any locks. */
	assert_trx_in_list(in_trx);

	trx_list = in_trx->read_only
		? &trx_sys->ro_trx_list : &trx_sys->rw_trx_list;

	ut_ad(mutex_own(&trx_sys->mutex));

	ut_ad(trx_assert_started(in_trx));

	for (trx = UT_LIST_GET_FIRST(*trx_list);
	     trx != NULL && trx != in_trx;
	     trx = UT_LIST_GET_NEXT(trx_list, trx)) {

		assert_trx_in_list(trx);
		ut_ad(trx->read_only == (trx_list == &trx_sys->ro_trx_list));
	}

	return(trx != NULL);
}
#endif /* UNIV_DEBUG */

/*****************************************************************//**
Writes the value of max_trx_id to the file based trx system header. */
UNIV_INTERN
void
trx_sys_flush_max_trx_id(void)
/*==========================*/
{
	mtr_t		mtr;
	trx_sysf_t*	sys_header;

	ut_ad(mutex_own(&trx_sys->mutex));

	mtr_start(&mtr);

	sys_header = trx_sysf_get(&mtr);

	mlog_write_ull(sys_header + TRX_SYS_TRX_ID_STORE,
		       trx_sys->max_trx_id, &mtr);
	mtr_commit(&mtr);
}

/*****************************************************************//**
Updates the offset information about the end of the MySQL binlog entry
which corresponds to the transaction just being committed. In a MySQL
replication slave updates the latest master binlog position up to which
replication has proceeded. */
UNIV_INTERN
void
trx_sys_update_mysql_binlog_offset(
/*===============================*/
	const char*	file_name,/*!< in: MySQL log file name */
	ib_int64_t	offset,	/*!< in: position in that log file */
	ulint		field,	/*!< in: offset of the MySQL log info field in
				the trx sys header */
	mtr_t*		mtr)	/*!< in: mtr */
{
	trx_sysf_t*	sys_header;

	if (ut_strlen(file_name) >= TRX_SYS_MYSQL_LOG_NAME_LEN) {

		/* We cannot fit the name to the 512 bytes we have reserved */

		return;
	}

	sys_header = trx_sysf_get(mtr);

	if (mach_read_from_4(sys_header + field
			     + TRX_SYS_MYSQL_LOG_MAGIC_N_FLD)
	    != TRX_SYS_MYSQL_LOG_MAGIC_N) {

		mlog_write_ulint(sys_header + field
				 + TRX_SYS_MYSQL_LOG_MAGIC_N_FLD,
				 TRX_SYS_MYSQL_LOG_MAGIC_N,
				 MLOG_4BYTES, mtr);
	}

	if (0 != strcmp((char*) (sys_header + field + TRX_SYS_MYSQL_LOG_NAME),
			file_name)) {

		mlog_write_string(sys_header + field
				  + TRX_SYS_MYSQL_LOG_NAME,
				  (byte*) file_name, 1 + ut_strlen(file_name),
				  mtr);
	}

	if (mach_read_from_4(sys_header + field
			     + TRX_SYS_MYSQL_LOG_OFFSET_HIGH) > 0
	    || (offset >> 32) > 0) {

		mlog_write_ulint(sys_header + field
				 + TRX_SYS_MYSQL_LOG_OFFSET_HIGH,
				 (ulint)(offset >> 32),
				 MLOG_4BYTES, mtr);
	}

	mlog_write_ulint(sys_header + field
			 + TRX_SYS_MYSQL_LOG_OFFSET_LOW,
			 (ulint)(offset & 0xFFFFFFFFUL),
			 MLOG_4BYTES, mtr);
}

/*****************************************************************//**
Stores the MySQL binlog offset info in the trx system header if
the magic number shows it valid, and print the info to stderr */
UNIV_INTERN
void
trx_sys_print_mysql_binlog_offset(void)
/*===================================*/
{
	trx_sysf_t*	sys_header;
	mtr_t		mtr;
	ulint		trx_sys_mysql_bin_log_pos_high;
	ulint		trx_sys_mysql_bin_log_pos_low;

	mtr_start(&mtr);

	sys_header = trx_sysf_get(&mtr);

	if (mach_read_from_4(sys_header + TRX_SYS_MYSQL_LOG_INFO
			     + TRX_SYS_MYSQL_LOG_MAGIC_N_FLD)
	    != TRX_SYS_MYSQL_LOG_MAGIC_N) {

		mtr_commit(&mtr);

		return;
	}

	trx_sys_mysql_bin_log_pos_high = mach_read_from_4(
		sys_header + TRX_SYS_MYSQL_LOG_INFO
		+ TRX_SYS_MYSQL_LOG_OFFSET_HIGH);
	trx_sys_mysql_bin_log_pos_low = mach_read_from_4(
		sys_header + TRX_SYS_MYSQL_LOG_INFO
		+ TRX_SYS_MYSQL_LOG_OFFSET_LOW);

	trx_sys_mysql_bin_log_pos
		= (((ib_int64_t)trx_sys_mysql_bin_log_pos_high) << 32)
		+ (ib_int64_t)trx_sys_mysql_bin_log_pos_low;

	ut_memcpy(trx_sys_mysql_bin_log_name,
		  sys_header + TRX_SYS_MYSQL_LOG_INFO
		  + TRX_SYS_MYSQL_LOG_NAME, TRX_SYS_MYSQL_LOG_NAME_LEN);

	fprintf(stderr,
		"InnoDB: Last MySQL binlog file position %lu %lu,"
		" file name %s\n",
		trx_sys_mysql_bin_log_pos_high, trx_sys_mysql_bin_log_pos_low,
		trx_sys_mysql_bin_log_name);

	mtr_commit(&mtr);
}

/*****************************************************************//**
Prints to stderr the MySQL master log offset info in the trx system header if
the magic number shows it valid. */
UNIV_INTERN
void
trx_sys_print_mysql_master_log_pos(void)
/*====================================*/
{
	trx_sysf_t*	sys_header;
	mtr_t		mtr;

	mtr_start(&mtr);

	sys_header = trx_sysf_get(&mtr);

	if (mach_read_from_4(sys_header + TRX_SYS_MYSQL_MASTER_LOG_INFO
			     + TRX_SYS_MYSQL_LOG_MAGIC_N_FLD)
	    != TRX_SYS_MYSQL_LOG_MAGIC_N) {

		mtr_commit(&mtr);

		return;
	}

	fprintf(stderr,
		"InnoDB: In a MySQL replication slave the last"
		" master binlog file\n"
		"InnoDB: position %lu %lu, file name %s\n",
		(ulong) mach_read_from_4(sys_header
					 + TRX_SYS_MYSQL_MASTER_LOG_INFO
					 + TRX_SYS_MYSQL_LOG_OFFSET_HIGH),
		(ulong) mach_read_from_4(sys_header
					 + TRX_SYS_MYSQL_MASTER_LOG_INFO
					 + TRX_SYS_MYSQL_LOG_OFFSET_LOW),
		sys_header + TRX_SYS_MYSQL_MASTER_LOG_INFO
		+ TRX_SYS_MYSQL_LOG_NAME);
	/* Copy the master log position info to global variables we can
	use in ha_innobase.cc to initialize glob_mi to right values */

	ut_memcpy(trx_sys_mysql_master_log_name,
		  sys_header + TRX_SYS_MYSQL_MASTER_LOG_INFO
		  + TRX_SYS_MYSQL_LOG_NAME,
		  TRX_SYS_MYSQL_LOG_NAME_LEN);

	trx_sys_mysql_master_log_pos
		= (((ib_int64_t) mach_read_from_4(
			    sys_header + TRX_SYS_MYSQL_MASTER_LOG_INFO
			    + TRX_SYS_MYSQL_LOG_OFFSET_HIGH)) << 32)
		+ ((ib_int64_t) mach_read_from_4(
			   sys_header + TRX_SYS_MYSQL_MASTER_LOG_INFO
			   + TRX_SYS_MYSQL_LOG_OFFSET_LOW));
	mtr_commit(&mtr);
}

/****************************************************************//**
Looks for a free slot for a rollback segment in the trx system file copy.
@return	slot index or ULINT_UNDEFINED if not found */
UNIV_INTERN
ulint
trx_sysf_rseg_find_free(
/*====================*/
	mtr_t*	mtr)	/*!< in: mtr */
{
	ulint		i;
	trx_sysf_t*	sys_header;

	sys_header = trx_sysf_get(mtr);

	for (i = 0; i < TRX_SYS_N_RSEGS; i++) {
		ulint	page_no;

		page_no = trx_sysf_rseg_get_page_no(sys_header, i, mtr);

		if (page_no == FIL_NULL) {

			return(i);
		}
	}

	return(ULINT_UNDEFINED);
}

/*****************************************************************//**
Creates the file page for the transaction system. This function is called only
at the database creation, before trx_sys_init. */
static
void
trx_sysf_create(
/*============*/
	mtr_t*	mtr)	/*!< in: mtr */
{
	trx_sysf_t*	sys_header;
	ulint		slot_no;
	buf_block_t*	block;
	page_t*		page;
	ulint		page_no;
	byte*		ptr;
	ulint		len;

	ut_ad(mtr);

	/* Note that below we first reserve the file space x-latch, and
	then enter the kernel: we must do it in this order to conform
	to the latching order rules. */

	mtr_x_lock(fil_space_get_latch(TRX_SYS_SPACE, NULL), mtr);

	/* Create the trx sys file block in a new allocated file segment */
	block = fseg_create(TRX_SYS_SPACE, 0, TRX_SYS + TRX_SYS_FSEG_HEADER,
			    mtr);
	buf_block_dbg_add_level(block, SYNC_TRX_SYS_HEADER);

	ut_a(buf_block_get_page_no(block) == TRX_SYS_PAGE_NO);

	page = buf_block_get_frame(block);

	mlog_write_ulint(page + FIL_PAGE_TYPE, FIL_PAGE_TYPE_TRX_SYS,
			 MLOG_2BYTES, mtr);

	/* Reset the doublewrite buffer magic number to zero so that we
	know that the doublewrite buffer has not yet been created (this
	suppresses a Valgrind warning) */

	mlog_write_ulint(page + TRX_SYS_DOUBLEWRITE
			 + TRX_SYS_DOUBLEWRITE_MAGIC, 0, MLOG_4BYTES, mtr);

	sys_header = trx_sysf_get(mtr);

	/* Start counting transaction ids from number 1 up */
	mach_write_to_8(sys_header + TRX_SYS_TRX_ID_STORE, 1);

	/* Reset the rollback segment slots.  Old versions of InnoDB
	define TRX_SYS_N_RSEGS as 256 (TRX_SYS_OLD_N_RSEGS) and expect
	that the whole array is initialized. */
	ptr = TRX_SYS_RSEGS + sys_header;
	len = ut_max(TRX_SYS_OLD_N_RSEGS, TRX_SYS_N_RSEGS)
		* TRX_SYS_RSEG_SLOT_SIZE;
	memset(ptr, 0xff, len);
	ptr += len;
	ut_a(ptr <= page + (UNIV_PAGE_SIZE - FIL_PAGE_DATA_END));

	/* Initialize all of the page.  This part used to be uninitialized. */
	memset(ptr, 0, UNIV_PAGE_SIZE - FIL_PAGE_DATA_END + page - ptr);

	mlog_log_string(sys_header, UNIV_PAGE_SIZE - FIL_PAGE_DATA_END
			+ page - sys_header, mtr);

	/* Create the first rollback segment in the SYSTEM tablespace */
	slot_no = trx_sysf_rseg_find_free(mtr);
	page_no = trx_rseg_header_create(TRX_SYS_SPACE, 0, ULINT_MAX, slot_no,
					 mtr);

	ut_a(slot_no == TRX_SYS_SYSTEM_RSEG_ID);
	ut_a(page_no == FSP_FIRST_RSEG_PAGE_NO);
}

/*****************************************************************//**
Compare two trx_rseg_t instances on last_trx_no. */
static
int
trx_rseg_compare_last_trx_no(
/*=========================*/
	const void*	p1,		/*!< in: elem to compare */
	const void*	p2)		/*!< in: elem to compare */
{
	ib_int64_t	cmp;

	const rseg_queue_t*	rseg_q1 = (const rseg_queue_t*) p1;
	const rseg_queue_t*	rseg_q2 = (const rseg_queue_t*) p2;

	cmp = rseg_q1->trx_no - rseg_q2->trx_no;

	if (cmp < 0) {
		return(-1);
	} else if (cmp > 0) {
		return(1);
	}

	return(0);
}

/*****************************************************************//**
Creates and initializes the central memory structures for the transaction
system. This is called when the database is started.
@return min binary heap of rsegs to purge */
UNIV_INTERN
ib_bh_t*
trx_sys_init_at_db_start(void)
/*==========================*/
{
	mtr_t		mtr;
	ib_bh_t*	ib_bh;
	trx_sysf_t*	sys_header;
	ib_uint64_t	rows_to_undo	= 0;
	const char*	unit		= "";

	/* We create the min binary heap here and pass ownership to
	purge when we init the purge sub-system. Purge is responsible
	for freeing the binary heap. */

	ib_bh = ib_bh_create(
		trx_rseg_compare_last_trx_no,
		sizeof(rseg_queue_t), TRX_SYS_N_RSEGS);

	mtr_start(&mtr);

	sys_header = trx_sysf_get(&mtr);

	if (srv_force_recovery < SRV_FORCE_NO_UNDO_LOG_SCAN) {
		trx_rseg_array_init(sys_header, ib_bh, &mtr);
	}

	/* VERY important: after the database is started, max_trx_id value is
	divisible by TRX_SYS_TRX_ID_WRITE_MARGIN, and the 'if' in
	trx_sys_get_new_trx_id will evaluate to TRUE when the function
	is first time called, and the value for trx id will be written
	to the disk-based header! Thus trx id values will not overlap when
	the database is repeatedly started! */

	trx_sys->max_trx_id = 2 * TRX_SYS_TRX_ID_WRITE_MARGIN
		+ ut_uint64_align_up(mach_read_from_8(sys_header
						   + TRX_SYS_TRX_ID_STORE),
				     TRX_SYS_TRX_ID_WRITE_MARGIN);

	UT_LIST_INIT(trx_sys->mysql_trx_list);

	trx_dummy_sess = sess_open();

	trx_lists_init_at_db_start();

	/* This S lock is not strictly required, it is here only to satisfy
	the debug code (assertions). We are still running in single threaded
	bootstrap mode. */

	mutex_enter(&trx_sys->mutex);

	ut_a(UT_LIST_GET_LEN(trx_sys->ro_trx_list) == 0);

	if (UT_LIST_GET_LEN(trx_sys->rw_trx_list) > 0) {
		const trx_t*	trx;

		for (trx = UT_LIST_GET_FIRST(trx_sys->rw_trx_list);
		     trx != NULL;
		     trx = UT_LIST_GET_NEXT(trx_list, trx)) {

			ut_ad(trx->is_recovered);
			assert_trx_in_rw_list(trx);

			if (trx_state_eq(trx, TRX_STATE_ACTIVE)) {
				rows_to_undo += trx->undo_no;
			}
		}

		if (rows_to_undo > 1000000000) {
			unit = "M";
			rows_to_undo = rows_to_undo / 1000000;
		}

		fprintf(stderr,
			"InnoDB: %lu transaction(s) which must be"
			" rolled back or cleaned up\n"
			"InnoDB: in total %lu%s row operations to undo\n",
			(ulong) UT_LIST_GET_LEN(trx_sys->rw_trx_list),
			(ulong) rows_to_undo, unit);

		fprintf(stderr, "InnoDB: Trx id counter is " TRX_ID_FMT "\n",
			(ullint) trx_sys->max_trx_id);
	}

	mutex_exit(&trx_sys->mutex);

	UT_LIST_INIT(trx_sys->view_list);

	mtr_commit(&mtr);

	return(ib_bh);
}

/*****************************************************************//**
Creates the trx_sys instance and initializes ib_bh and mutex. */
UNIV_INTERN
void
trx_sys_create(void)
/*================*/
{
	ut_ad(trx_sys == NULL);

	trx_sys = mem_zalloc(sizeof(*trx_sys));

	mutex_create(trx_sys_mutex_key, &trx_sys->mutex, SYNC_TRX_SYS);
}

/*****************************************************************//**
Creates and initializes the transaction system at the database creation. */
UNIV_INTERN
void
trx_sys_create_sys_pages(void)
/*==========================*/
{
	mtr_t	mtr;

	mtr_start(&mtr);

	trx_sysf_create(&mtr);

	mtr_commit(&mtr);
}

/*****************************************************************//**
Update the file format tag.
@return	always TRUE */
static
ibool
trx_sys_file_format_max_write(
/*==========================*/
	ulint		format_id,	/*!< in: file format id */
	const char**	name)		/*!< out: max file format name, can
					be NULL */
{
	mtr_t		mtr;
	byte*		ptr;
	buf_block_t*	block;
	ib_uint64_t	tag_value;

	mtr_start(&mtr);

	block = buf_page_get(
		TRX_SYS_SPACE, 0, TRX_SYS_PAGE_NO, RW_X_LATCH, &mtr);

	file_format_max.id = format_id;
	file_format_max.name = trx_sys_file_format_id_to_name(format_id);

	ptr = buf_block_get_frame(block) + TRX_SYS_FILE_FORMAT_TAG;
	tag_value = format_id + TRX_SYS_FILE_FORMAT_TAG_MAGIC_N;

	if (name) {
		*name = file_format_max.name;
	}

	mlog_write_ull(ptr, tag_value, &mtr);

	mtr_commit(&mtr);

	return(TRUE);
}

/*****************************************************************//**
Read the file format tag.
@return	the file format or ULINT_UNDEFINED if not set. */
static
ulint
trx_sys_file_format_max_read(void)
/*==============================*/
{
	mtr_t			mtr;
	const byte*		ptr;
	const buf_block_t*	block;
	ib_id_t			file_format_id;

	/* Since this is called during the startup phase it's safe to
	read the value without a covering mutex. */
	mtr_start(&mtr);

	block = buf_page_get(
		TRX_SYS_SPACE, 0, TRX_SYS_PAGE_NO, RW_X_LATCH, &mtr);

	ptr = buf_block_get_frame(block) + TRX_SYS_FILE_FORMAT_TAG;
	file_format_id = mach_read_from_8(ptr);

	mtr_commit(&mtr);

	file_format_id -= TRX_SYS_FILE_FORMAT_TAG_MAGIC_N;

	if (file_format_id >= FILE_FORMAT_NAME_N) {

		/* Either it has never been tagged, or garbage in it. */
		return(ULINT_UNDEFINED);
	}

	return((ulint) file_format_id);
}

/*****************************************************************//**
Get the name representation of the file format from its id.
@return	pointer to the name */
UNIV_INTERN
const char*
trx_sys_file_format_id_to_name(
/*===========================*/
	const ulint	id)	/*!< in: id of the file format */
{
	ut_a(id < FILE_FORMAT_NAME_N);

	return(file_format_name_map[id]);
}

/*****************************************************************//**
Check for the max file format tag stored on disk. Note: If max_format_id
is == UNIV_FORMAT_MAX + 1 then we only print a warning.
@return	DB_SUCCESS or error code */
UNIV_INTERN
ulint
trx_sys_file_format_max_check(
/*==========================*/
	ulint	max_format_id)	/*!< in: max format id to check */
{
	ulint	format_id;

	/* Check the file format in the tablespace. Do not try to
	recover if the file format is not supported by the engine
	unless forced by the user. */
	format_id = trx_sys_file_format_max_read();
	if (format_id == ULINT_UNDEFINED) {
		/* Format ID was not set. Set it to minimum possible
		value. */
		format_id = UNIV_FORMAT_MIN;
	}

	ut_print_timestamp(stderr);
	fprintf(stderr,
		" InnoDB: highest supported file format is %s.\n",
		trx_sys_file_format_id_to_name(UNIV_FORMAT_MAX));

	if (format_id > UNIV_FORMAT_MAX) {

		ut_a(format_id < FILE_FORMAT_NAME_N);

		ut_print_timestamp(stderr);
		fprintf(stderr,
			" InnoDB: %s: the system tablespace is in a file "
			"format that this version doesn't support - %s\n",
			((max_format_id <= UNIV_FORMAT_MAX)
				? "Error" : "Warning"),
			trx_sys_file_format_id_to_name(format_id));

		if (max_format_id <= UNIV_FORMAT_MAX) {
			return(DB_ERROR);
		}
	}

	format_id = (format_id > max_format_id) ? format_id : max_format_id;

	/* We don't need a mutex here, as this function should only
	be called once at start up. */
	file_format_max.id = format_id;
	file_format_max.name = trx_sys_file_format_id_to_name(format_id);

	return(DB_SUCCESS);
}

/*****************************************************************//**
Set the file format id unconditionally except if it's already the
same value.
@return	TRUE if value updated */
UNIV_INTERN
ibool
trx_sys_file_format_max_set(
/*========================*/
	ulint		format_id,	/*!< in: file format id */
	const char**	name)		/*!< out: max file format name or
					NULL if not needed. */
{
	ibool		ret = FALSE;

	ut_a(format_id <= UNIV_FORMAT_MAX);

	mutex_enter(&file_format_max.mutex);

	/* Only update if not already same value. */
	if (format_id != file_format_max.id) {

		ret = trx_sys_file_format_max_write(format_id, name);
	}

	mutex_exit(&file_format_max.mutex);

	return(ret);
}

/********************************************************************//**
Tags the system table space with minimum format id if it has not been
tagged yet.
WARNING: This function is only called during the startup and AFTER the
redo log application during recovery has finished. */
UNIV_INTERN
void
trx_sys_file_format_tag_init(void)
/*==============================*/
{
	ulint	format_id;

	format_id = trx_sys_file_format_max_read();

	/* If format_id is not set then set it to the minimum. */
	if (format_id == ULINT_UNDEFINED) {
		trx_sys_file_format_max_set(UNIV_FORMAT_MIN, NULL);
	}
}

/********************************************************************//**
Update the file format tag in the system tablespace only if the given
format id is greater than the known max id.
@return	TRUE if format_id was bigger than the known max id */
UNIV_INTERN
ibool
trx_sys_file_format_max_upgrade(
/*============================*/
	const char**	name,		/*!< out: max file format name */
	ulint		format_id)	/*!< in: file format identifier */
{
	ibool		ret = FALSE;

	ut_a(name);
	ut_a(file_format_max.name != NULL);
	ut_a(format_id <= UNIV_FORMAT_MAX);

	mutex_enter(&file_format_max.mutex);

	if (format_id > file_format_max.id) {

		ret = trx_sys_file_format_max_write(format_id, name);
	}

	mutex_exit(&file_format_max.mutex);

	return(ret);
}

/*****************************************************************//**
Get the name representation of the file format from its id.
@return	pointer to the max format name */
UNIV_INTERN
const char*
trx_sys_file_format_max_get(void)
/*=============================*/
{
	return(file_format_max.name);
}

/*****************************************************************//**
Initializes the tablespace tag system. */
UNIV_INTERN
void
trx_sys_file_format_init(void)
/*==========================*/
{
	mutex_create(file_format_max_mutex_key,
		     &file_format_max.mutex, SYNC_FILE_FORMAT_TAG);

	/* We don't need a mutex here, as this function should only
	be called once at start up. */
	file_format_max.id = UNIV_FORMAT_MIN;

	file_format_max.name = trx_sys_file_format_id_to_name(
		file_format_max.id);
}

/*****************************************************************//**
Closes the tablespace tag system. */
UNIV_INTERN
void
trx_sys_file_format_close(void)
/*===========================*/
{
	/* Does nothing at the moment */
}

/*********************************************************************
Creates the rollback segments.
@return number of rollback segments that are active. */
UNIV_INTERN
ulint
trx_sys_create_rsegs(
/*=================*/
	ulint	n_spaces,	/*!< number of tablespaces for UNDO logs */
	ulint	n_rsegs)	/*!< number of rollback segments to create */
{
	mtr_t	mtr;
	ulint	n_used;

	ut_a(n_spaces < TRX_SYS_N_RSEGS);
	ut_a(n_rsegs <= TRX_SYS_N_RSEGS);

	if (srv_force_recovery >= SRV_FORCE_NO_UNDO_LOG_SCAN) {
		return(ULINT_UNDEFINED);
	}

	/* This is executed in single-threaded mode therefore it is not
	necessary to use the same mtr in trx_rseg_create(). n_used cannot
	change while the function is executing. */

	mtr_start(&mtr);
	n_used = trx_sysf_rseg_find_free(&mtr);
	mtr_commit(&mtr);

	if (n_used == ULINT_UNDEFINED) {
		n_used = TRX_SYS_N_RSEGS;
	}

	/* Do not create additional rollback segments if innodb_force_recovery
	has been set and the database was not shutdown cleanly. */

	if (!srv_force_recovery && !recv_needed_recovery && n_used < n_rsegs) {
		ulint	i;
		ulint	new_rsegs = n_rsegs - n_used;

		for (i = 0; i < new_rsegs; ++i) {
			ulint	space;

			/* Tablespace 0 is the system tablespace. All UNDO
			log tablespaces start from 1. */

			if (n_spaces > 0) {
				space = (i % n_spaces) + 1;
			} else {
				space = 0; /* System tablespace */
			}

			if (trx_rseg_create(space) != NULL) {
				++n_used;
			} else {
				break;
			}
		}
	}

	ut_print_timestamp(stderr);
	fprintf(stderr, " InnoDB: %lu rollback segment(s) are active.\n",
		n_used);

	return(n_used);
}

#else /* !UNIV_HOTBACKUP */
/*****************************************************************//**
Prints to stderr the MySQL binlog info in the system header if the
magic number shows it valid. */
UNIV_INTERN
void
trx_sys_print_mysql_binlog_offset_from_page(
/*========================================*/
	const byte*	page)	/*!< in: buffer containing the trx
				system header page, i.e., page number
				TRX_SYS_PAGE_NO in the tablespace */
{
	const trx_sysf_t*	sys_header;

	sys_header = page + TRX_SYS;

	if (mach_read_from_4(sys_header + TRX_SYS_MYSQL_LOG_INFO
			     + TRX_SYS_MYSQL_LOG_MAGIC_N_FLD)
	    == TRX_SYS_MYSQL_LOG_MAGIC_N) {

		fprintf(stderr,
			"ibbackup: Last MySQL binlog file position %lu %lu,"
			" file name %s\n",
			(ulong) mach_read_from_4(
				sys_header + TRX_SYS_MYSQL_LOG_INFO
				+ TRX_SYS_MYSQL_LOG_OFFSET_HIGH),
			(ulong) mach_read_from_4(
				sys_header + TRX_SYS_MYSQL_LOG_INFO
				+ TRX_SYS_MYSQL_LOG_OFFSET_LOW),
			sys_header + TRX_SYS_MYSQL_LOG_INFO
			+ TRX_SYS_MYSQL_LOG_NAME);
	}
}

/*****************************************************************//**
Reads the file format id from the first system table space file.
Even if the call succeeds and returns TRUE, the returned format id
may be ULINT_UNDEFINED signalling that the format id was not present
in the data file.
@return TRUE if call succeeds */
UNIV_INTERN
ibool
trx_sys_read_file_format_id(
/*========================*/
	const char *pathname,  /*!< in: pathname of the first system
				        table space file */
	ulint *format_id)      /*!< out: file format of the system table
				         space */
{
	os_file_t	file;
	ibool		success;
	byte		buf[UNIV_PAGE_SIZE * 2];
	page_t*		page = ut_align(buf, UNIV_PAGE_SIZE);
	const byte*	ptr;
	ib_id_t		file_format_id;

	*format_id = ULINT_UNDEFINED;

	file = os_file_create_simple_no_error_handling(
		innodb_file_data_key,
		pathname,
		OS_FILE_OPEN,
		OS_FILE_READ_ONLY,
		&success
	);
	if (!success) {
		/* The following call prints an error message */
		os_file_get_last_error(TRUE);

		ut_print_timestamp(stderr);

		fprintf(stderr,
			"  ibbackup: Error: trying to read system tablespace "
			"file format,\n"
			"  ibbackup: but could not open the tablespace "
			"file %s!\n", pathname);
		return(FALSE);
	}

	/* Read the page on which file format is stored */

	success = os_file_read_no_error_handling(
		file, page, TRX_SYS_PAGE_NO * UNIV_PAGE_SIZE, UNIV_PAGE_SIZE);

	if (!success) {
		/* The following call prints an error message */
		os_file_get_last_error(TRUE);

		ut_print_timestamp(stderr);

		fprintf(stderr,
			"  ibbackup: Error: trying to read system tablespace "
			"file format,\n"
			"  ibbackup: but failed to read the tablespace "
			"file %s!\n", pathname);

		os_file_close(file);
		return(FALSE);
	}
	os_file_close(file);

	/* get the file format from the page */
	ptr = page + TRX_SYS_FILE_FORMAT_TAG;
	file_format_id = mach_read_from_8(ptr);
	file_format_id -= TRX_SYS_FILE_FORMAT_TAG_MAGIC_N;

	if (file_format_id >= FILE_FORMAT_NAME_N) {

		/* Either it has never been tagged, or garbage in it. */
		return(TRUE);
	}

	*format_id = (ulint) file_format_id;

	return(TRUE);
}

/*****************************************************************//**
Reads the file format id from the given per-table data file.
@return TRUE if call succeeds */
UNIV_INTERN
ibool
trx_sys_read_pertable_file_format_id(
/*=================================*/
	const char *pathname,  /*!< in: pathname of a per-table
				        datafile */
	ulint *format_id)      /*!< out: file format of the per-table
				         data file */
{
	os_file_t	file;
	ibool		success;
	byte		buf[UNIV_PAGE_SIZE * 2];
	page_t*		page = ut_align(buf, UNIV_PAGE_SIZE);
	const byte*	ptr;
	ib_uint32_t	flags;

	*format_id = ULINT_UNDEFINED;

	file = os_file_create_simple_no_error_handling(
		innodb_file_data_key,
		pathname,
		OS_FILE_OPEN,
		OS_FILE_READ_ONLY,
		&success
	);
	if (!success) {
		/* The following call prints an error message */
		os_file_get_last_error(TRUE);

		ut_print_timestamp(stderr);

		fprintf(stderr,
			"  ibbackup: Error: trying to read per-table "
			"tablespace format,\n"
			"  ibbackup: but could not open the tablespace "
			"file %s!\n", pathname);

		return(FALSE);
	}

	/* Read the first page of the per-table datafile */

	success = os_file_read_no_error_handling(file, page, 0, UNIV_PAGE_SIZE);

	if (!success) {
		/* The following call prints an error message */
		os_file_get_last_error(TRUE);

		ut_print_timestamp(stderr);

		fprintf(stderr,
			"  ibbackup: Error: trying to per-table data file "
			"format,\n"
			"  ibbackup: but failed to read the tablespace "
			"file %s!\n", pathname);

		os_file_close(file);
		return(FALSE);
	}
	os_file_close(file);

	/* get the file format from the page */
	ptr = page + 54;
	flags = mach_read_from_4(ptr);
	if (flags == 0) {
		/* file format is Antelope */
		*format_id = 0;
		return (TRUE);
	} else if (flags & 1) {
		/* tablespace flags are ok */
		*format_id = (flags / 32) % 128;
		return (TRUE);
	} else {
		/* bad tablespace flags */
		return(FALSE);
	}
}


/*****************************************************************//**
Get the name representation of the file format from its id.
@return	pointer to the name */
UNIV_INTERN
const char*
trx_sys_file_format_id_to_name(
/*===========================*/
	const ulint	id)	/*!< in: id of the file format */
{
	if (!(id < FILE_FORMAT_NAME_N)) {
		/* unknown id */
		return ("Unknown");
	}

	return(file_format_name_map[id]);
}

#endif /* !UNIV_HOTBACKUP */

#ifndef UNIV_HOTBACKUP
/*********************************************************************
Shutdown/Close the transaction system. */
UNIV_INTERN
void
trx_sys_close(void)
/*===============*/
{
	ulint		i;
	trx_t*		trx;
	read_view_t*	view;

	ut_ad(trx_sys != NULL);
	ut_ad(srv_shutdown_state == SRV_SHUTDOWN_EXIT_THREADS);

	/* Check that all read views are closed except read view owned
	by a purge. */

	mutex_enter(&trx_sys->mutex);

	if (UT_LIST_GET_LEN(trx_sys->view_list) > 1) {
		fprintf(stderr,
			"InnoDB: Error: all read views were not closed"
			" before shutdown:\n"
			"InnoDB: %lu read views open \n",
			UT_LIST_GET_LEN(trx_sys->view_list) - 1);
	}

	mutex_exit(&trx_sys->mutex);

	sess_close(trx_dummy_sess);
	trx_dummy_sess = NULL;

	trx_purge_sys_close();

	/* Free the double write data structures. */
	ut_a(trx_doublewrite != NULL);
	ut_ad(trx_doublewrite->n_reserved == 0);

	ut_free(trx_doublewrite->write_buf_unaligned);
	trx_doublewrite->write_buf_unaligned = NULL;

	mem_free(trx_doublewrite->buf_block_arr);
	trx_doublewrite->buf_block_arr = NULL;

	mem_free(trx_doublewrite->in_use);
	trx_doublewrite->in_use = NULL;

	mutex_free(&trx_doublewrite->mutex);
	mem_free(trx_doublewrite);
	trx_doublewrite = NULL;

	mutex_enter(&trx_sys->mutex);

	ut_a(UT_LIST_GET_LEN(trx_sys->ro_trx_list) == 0);

	/* Only prepared transactions may be left in the system. Free them. */
	ut_a(UT_LIST_GET_LEN(trx_sys->rw_trx_list) == trx_sys->n_prepared_trx);

	while ((trx = UT_LIST_GET_FIRST(trx_sys->rw_trx_list)) != NULL) {
		trx_free_prepared(trx);
	}

	/* There can't be any active transactions. */
	for (i = 0; i < TRX_SYS_N_RSEGS; ++i) {
		trx_rseg_t*	rseg;

		rseg = trx_sys->rseg_array[i];

		if (rseg != NULL) {
			trx_rseg_mem_free(rseg);
		} else {
			break;
		}
	}

	view = UT_LIST_GET_FIRST(trx_sys->view_list);

	while (view != NULL) {
		read_view_t*	prev_view = view;

		view = UT_LIST_GET_NEXT(view_list, prev_view);

		/* Views are allocated from the trx_sys->global_read_view_heap.
		So, we simply remove the element here. */
		UT_LIST_REMOVE(view_list, trx_sys->view_list, prev_view);
	}

	ut_a(UT_LIST_GET_LEN(trx_sys->view_list) == 0);
	ut_a(UT_LIST_GET_LEN(trx_sys->ro_trx_list) == 0);
	ut_a(UT_LIST_GET_LEN(trx_sys->rw_trx_list) == 0);
	ut_a(UT_LIST_GET_LEN(trx_sys->mysql_trx_list) == 0);

	mutex_exit(&trx_sys->mutex);

	mutex_free(&trx_sys->mutex);

	mem_free(trx_sys);

	trx_sys = NULL;
}

/*********************************************************************
Check if there are any active (non-prepared) transactions.
@return total number of active transactions or 0 if none */
UNIV_INTERN
ulint
trx_sys_any_active_transactions(void)
/*=================================*/
{
	ulint	total_trx = 0;

	mutex_enter(&trx_sys->mutex);

	total_trx = UT_LIST_GET_LEN(trx_sys->rw_trx_list)
		+ trx_sys->n_mysql_trx;

	ut_a(total_trx >= trx_sys->n_prepared_trx);
	total_trx -= trx_sys->n_prepared_trx;

	mutex_exit(&trx_sys->mutex);

	return(total_trx);
}

#ifdef UNIV_DEBUG
/*************************************************************//**
Validate the trx_list_t.
@return TRUE if valid. */
static
ibool
trx_sys_validate_trx_list_low(
/*===========================*/
	trx_list_t*	trx_list)	/*!< in: &trx_sys->ro_trx_list
					or &trx_sys->rw_trx_list */
{
	const trx_t*	trx;
	const trx_t*	prev_trx = NULL;

	ut_ad(mutex_own(&trx_sys->mutex));

	ut_ad(trx_list == &trx_sys->ro_trx_list
	      || trx_list == &trx_sys->rw_trx_list);

	for (trx = UT_LIST_GET_FIRST(*trx_list);
	     trx != NULL;
	     prev_trx = trx, trx = UT_LIST_GET_NEXT(trx_list, prev_trx)) {

		assert_trx_in_list(trx);

		ut_a(prev_trx == NULL || prev_trx->id > trx->id);
	}

	return(TRUE);
}

/*************************************************************//**
Validate the trx_sys_t::ro_trx_list and trx_sys_t::rw_trx_list.
@return TRUE if lists are valid. */
UNIV_INTERN
ibool
trx_sys_validate_trx_list(void)
/*===========================*/
{
	ut_ad(mutex_own(&trx_sys->mutex));

	ut_a(trx_sys_validate_trx_list_low(&trx_sys->ro_trx_list));
	ut_a(trx_sys_validate_trx_list_low(&trx_sys->rw_trx_list));

	return(TRUE);
}
#endif /* UNIV_DEBUG */
#endif /* !UNIV_HOTBACKUP */
