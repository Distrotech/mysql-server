/*****************************************************************************

Copyright (c) 2010,  Oracle and/or its affiliates. All Rights Reserved.

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
@file srv/srv0mon.c
Database monitor counter interfaces

Created 12/9/2009 Jimmy Yang
*******************************************************/

#include "os0file.h"
#include "mach0data.h"
#include "srv0mon.h"
#include "srv0srv.h"
#include "buf0buf.h"
#include "trx0sys.h"
#ifdef UNIV_NONINL
#include "srv0mon.ic"
#endif

/** This array defines basic static information of monitor counters,
including each monitor's name, sub module it belongs to, a short
description and its property/type and corresponding monitor_id. */
static monitor_info_t	innodb_counter_info[] =
{
	/* A dummy item to mark the module start, this is
	to accomodate the default value (0) set for the
	global variables with the control system. */
	{"module_start", "module_start", "module_start",
	MONITOR_MODULE, MONITOR_DEFAULT_START},

	/* ========== Counters for Server Metadata ========== */
	{"module_server", "Server Metadata", "Server Metadata",
	 MONITOR_MODULE, MONITOR_MODULE_METADATA},

	{"server_table_open", "Server Metadata",
	 "Number of table handler opened", 0, MONITOR_TABLE_OPEN},

	{"server_table_close", "Server Metadata",
	 "Number of table handler closed", 0, MONITOR_TABLE_CLOSE},

	/* ========== Counters for Lock Module ========== */
	{"module_lock", "Lock", "Lock Module",
	 MONITOR_MODULE, MONITOR_MODULE_LOCK},

	{"lock_deadlock_count", "Lock", "Number of deadlocks",
	 0, MONITOR_DEADLOCK},

	{"lock_timeout", "Lock", "Number of lock timeout",
	 0, MONITOR_TIMEOUT},

	{"lock_lockrec_wait", "Lock", "Number of times wait for record lock",
	 0, MONITOR_LOCKREC_WAIT},

	{"lock_lockrec_request", "Lock", "Number of record lock requested",
	 0, MONITOR_NUM_RECLOCK_REQ},

	{"lock_lockrec_created", "Lock", "Number of record lock created",
	 0, MONITOR_RECLOCK_CREATED},

	{"lock_lockrec_removed", "Lock", "Number of record lock destroyed",
	 0, MONITOR_RECLOCK_REMOVED},

	{"lock_num_lockrec", "Lock", "Total number of record locks",
	 0, MONITOR_NUM_RECLOCK},

	{"lock_tablelock_created", "Lock", "Number of table locks created",
	 0, MONITOR_TABLELOCK_CREATED},

	{"lock_tablelock_removed", "Lock", "Number of table lock destroyed",
	 0, MONITOR_TABLELOCK_REMOVED},

	{"lock_num_tablelock", "Lock", "Total number of table locks",
	 0, MONITOR_NUM_TABLELOCK},

	{"lock_row_lock_current_wait", "Lock", "Current row lock wait",
	 0, MONITOR_ROW_LOCK_CURRENT_WAIT},

	{"lock_row_lock_wait_time", "Lock", "Time waited for lock acquisition",
	 0, MONITOR_LOCK_WAIT_TIME},

	{"lock_row_lock_wait", "Lock", "Total row lock waits",
	 MONITOR_EXISTING, MONITOR_OVLD_ROW_LOCK_WAIT},

	/* ========== Counters for Buffer Manager and I/O ========== */
	{"module_buffer", "Buffer", "Buufer Manager Module",
	 MONITOR_MODULE, MONITOR_MODULE_BUFFER},

	{"buffer_reads", "Buffer", "Number of reads from disk",
	 MONITOR_EXISTING, MONITOR_OVLD_BUF_POOL_READS},

	{"buffer_read_request", "Buffer", "Number of read requests",
	 MONITOR_EXISTING, MONITOR_OVLD_BUF_POOL_READ_REQUESTS},

	{"buffer_write_request", "Buffer", "Number of write requests",
	 MONITOR_EXISTING, MONITOR_OVLD_BUF_POOL_WRITE_REQUEST},

	{"buffer_page_in_flush", "Buffer", "Number of pages in flush list",
	 0, MONITOR_PAGE_INFLUSH},

	{"buffer_wait_free", "Buffer", "Number of times wait for free buffer",
	 MONITOR_EXISTING, MONITOR_OVLD_BUF_POOL_WAIT_FREE},

	{"buffer_read_ahead", "Buffer", "Number of pages read as read ahead",
	 MONITOR_EXISTING, MONITOR_OVLD_BUF_POOL_READ_AHEAD},

	{"buffer_read_ahead_evict", "Buffer",
	 "Read ahead pages evicted without being accessed",
	 MONITOR_EXISTING, MONITOR_OVLD_BUF_POOL_READ_AHEAD_EVICTED},

	{"buffer_pool_total_page", "Buffer", "Total buffer pool pages",
	 MONITOR_EXISTING | MONITOR_DISPLAY_CURRENT,
	 MONITOR_OVLD_BUF_POOL_PAGE_TOTAL},

	{"buffer_pool_page_misc", "Buffer", "Buffer pages for misc use",
	 MONITOR_EXISTING | MONITOR_DISPLAY_CURRENT,
	 MONITOR_OVLD_BUF_POOL_PAGE_MISC},

	{"buffer_pool_page_data", "Buffer", "Buffer pages contain data",
	 MONITOR_EXISTING | MONITOR_DISPLAY_CURRENT,
	 MONITOR_OVLD_BUF_POOL_PAGES_DATA},

	{"buffer_pool_page_dirty", "Buffer", "Buffer pages currently dirty",
	 MONITOR_EXISTING | MONITOR_DISPLAY_CURRENT,
	 MONITOR_OVLD_BUF_POOL_PAGES_DIRTY},

	{"buffer_pool_page_free", "Buffer", "Buffer pages currently free",
	 MONITOR_EXISTING | MONITOR_DISPLAY_CURRENT,
	 MONITOR_OVLD_BUF_POOL_PAGES_FREE},

	{"buffer_page_created", "Buffer", "Number of pages created",
	 MONITOR_EXISTING, MONITOR_OVLD_PAGE_CREATED},

	{"buffer_page_written", "Buffer", "Number of pages written",
	 MONITOR_EXISTING, MONITOR_OVLD_PAGES_WRITTEN},

	{"buffer_page_read", "Buffer", "Number of pages read",
	 MONITOR_EXISTING, MONITOR_OVLD_PAGES_READ},

	{"buffer_byte_read", "Buffer", "Amount data read in bytes",
	 MONITOR_EXISTING, MONITOR_OVLD_BYTE_READ},

	{"buffer_byte_written", "Buffer", "Amount data written in bytes",
	 MONITOR_EXISTING, MONITOR_OVLD_BYTE_WRITTEN},

	/* ========== Counters for OS level operations ========== */
	{"module_os", "OS", "OS Level Operation",
	 MONITOR_MODULE, MONITOR_MODULE_OS},

	{"os_num_reads", "OS", "Number of reads initiated",
	 MONITOR_EXISTING, MONITOR_OVLD_OS_FILE_READ},

	{"os_num_writes", "OS", "Number of writes inititated",
	 MONITOR_EXISTING, MONITOR_OVLD_OS_FILE_WRITE},

	{"os_num_fsync", "OS", "Number of fsync() call",
	 MONITOR_EXISTING, MONITOR_OVLD_OS_FSYNC},

	{"os_num_pending_reads", "OS", "Number of reads pending",
	 0, MONITOR_OS_PENDING_READS},

	{"os_num_pending_writes", "OS", "Number of writes pending",
	 0, MONITOR_OS_PENDING_WRITES},

	{"os_log_byte_written", "Recovery", "Bytes of log written",
	 MONITOR_EXISTING, MONITOR_OVLD_OS_LOG_WRITTEN},

	{"os_log_fsync", "Recovery", "Number of fsync log writes",
	 MONITOR_EXISTING, MONITOR_OVLD_OS_LOG_FSYNC},

	{"os_log_pending_fsync", "Recovery", "Number of pending fsync write",
	 MONITOR_EXISTING, MONITOR_OVLD_OS_LOG_PENDING_FSYNC},

	{"os_log_pending_write", "Recovery",
	 "Number of pending log file writes",
	 MONITOR_EXISTING, MONITOR_OVLD_OS_LOG_PENDING_WRITES},

	/* ========== Counters for Transaction Module ========== */
	{"module_trx", "Transaction", "Transaction Manager",
	 MONITOR_MODULE, MONITOR_MODULE_TRX},

	{"trx_num_commit", "Transaction", "Number of transactions committed",
	 0, MONITOR_TRX_COMMIT},

	{"trx_num_abort", "Transaction", "Number of transactions aborted",
	 0, MONITOR_TRX_ABORT},

	{"trx_active_trx", "Transaction", "Number of active transactions",
	 0, MONITOR_TRX_ACTIVE},

	{"trx_num_row_purged", "Transaction", "Number of rows purged",
	 0, MONITOR_NUM_ROW_PURGE},

	{"trx_purge_delay", "DML",
	 "microseconds DML to be delayed due to purge lagging",
	 MONITOR_DISPLAY_CURRENT, MONITOR_DML_PURGE_DELAY},

	{"trx_rseg_history_len", "Transaction",
	 "Length of the TRX_RSEG_HISTORY list",
	 MONITOR_EXISTING | MONITOR_DISPLAY_CURRENT, MONITOR_RSEG_HISTORY_LEN},

	/* ========== Counters for Recovery Module ========== */
	{"module_log", "Recovery", "Recovery Module",
	 MONITOR_MODULE, MONITOR_MODULE_RECOVERY},

	{"log_num_checkpoint", "Recovery", "Number of checkpoints",
	 0, MONITOR_NUM_CHECKPOINT},

	{"log_lsn_last_flush", "Recovery", "Last flush's LSN",
	 0, MONITOR_LSN_FLUSHDISK},

	{"log_lsn_last_checkpoint", "Recovery", "LSN at last checkpoint",
	 0, MONITOR_LSN_CHECKPOINT},

	{"log_current_lsn", "Recovery", "Current LSN value",
	 0, MONITOR_LSN_CURRENT},

	{"log_pending_log_write", "Recovery", "Pending Log Writes",
	 MONITOR_DISPLAY_CURRENT, MONITOR_PENDING_LOG_WRITE},

	{"log_pending_checkpoint", "Recovery", "Pending Checkpoints",
	 0, MONITOR_PENDING_CHECKPOINT_WRITE},

	{"log_num_log_io", "Recovery", "Number of log I/Os",
	 0, MONITOR_LOG_IO},

	{"log_waits", "Recovery", "Number of log waits due to small log buffer",
	 MONITOR_EXISTING, MONITOR_OVLD_LOG_WAITS},

	{"log_write_request", "Recovery", "Number of log write requests",
	 MONITOR_EXISTING, MONITOR_OVLD_LOG_WRITE_REQUEST},

	{"log_writes", "Recovery", "Number of log writes",
	 MONITOR_EXISTING, MONITOR_OVLD_LOG_WRITES},

	{"log_flush_io_capacity", "Recovery",
	 "Percent of Server IO capacity during flushing",
	 MONITOR_DISPLAY_CURRENT, MONITOR_FLUSH_IO_CAPACITY},

	{"log_flush_dirty_page_exceed", "Recovery",
	 "Number of flush calls when the max dirty page pct was hit",
	 0, MONITOR_FLUSH_DIRTY_PAGE_EXCEED},

	/* ========== Counters for Page Compression ========== */
	{"module_compress", "Compression", "Page Compression Info",
	 MONITOR_MODULE, MONITOR_MODULE_PAGE},

	{"compress_num_page_compressed", "Compression",
	 "Number of page compressed", 0, MONITOR_PAGE_COMPRESS},

	{"compress_num_page_decompressed", "Compression",
	 "Number of page decompressed", 0, MONITOR_PAGE_DECOMPRESS},

	/* ========== Counters for Index ========== */
	{"module_index", "Index", "Index Manager",
	 MONITOR_MODULE, MONITOR_MODULE_INDEX},

	{"index_num_split", "Index", "Number of index split",
	 0, MONITOR_INDEX_SPLIT},

	{"index_num_merge", "Index", "Number of index merge",
	 0, MONITOR_INDEX_MERGE},

	/* ========== Counters for DML operations ========== */
	{"module_dml", "DML", "Statistics for DMLs",
	 MONITOR_MODULE, MONITOR_MODULE_DMLSTATS},

	{"dml_num_reads", "DML", "Number of rows read",
	 MONITOR_EXISTING, MONITOR_OLVD_ROW_READ},

	{"dml_num_inserts", "DML", "Number of rows inserted",
	 MONITOR_EXISTING, MONITOR_OLVD_ROW_INSERTED},

	{"dml_num_deletes", "DML", "Number of rows deleted",
	 MONITOR_EXISTING, MONITOR_OLVD_ROW_DELETED},

	{"dml_updates", "DML", "Number of rows updated",
	 MONITOR_EXISTING, MONITOR_OLVD_ROW_UPDTATED},

	/* ========== To turn on/off reset all counters ========== */
	{"all", "All Counters", "Turn on/off and reset all counters",
	 MONITOR_MODULE, MONITOR_ALL_COUNTER}
};

/* The "innodb_counter_value" array stores actual counter values */
UNIV_INTERN monitor_value_t	innodb_counter_value[NUM_MONITOR];

/* monitor_set_tbl is used to record and determine whether a monitor
has been turned on/off. */
UNIV_INTERN ulint		monitor_set_tbl[(NUM_MONITOR + NUM_BITS_ULINT
						- 1) / NUM_BITS_ULINT];

/****************************************************************//**
Get a monitor's "monitor_info" by its monitor id (index into the
innodb_counter_info array.
@return	Point to corresponding monitor_info_t, or NULL if no such
monitor */
UNIV_INTERN
monitor_info_t*
srv_mon_get_info(
/*=============*/
	monitor_id_t	monitor_id)	/*!< id indexing into the
					innodb_counter_info array */
{
	ut_a(monitor_id < NUM_MONITOR);

	return((monitor_id < NUM_MONITOR)
			? &innodb_counter_info[monitor_id]
			: NULL);
}

/****************************************************************//**
Get monitor's name by its monitor id (indexing into the
innodb_counter_info array.
@return	corresponding monitor name, or NULL if no such
monitor */
UNIV_INTERN
const char*
srv_mon_get_name(
/*=============*/
	monitor_id_t	monitor_id)	/*!< id index into the
					innodb_counter_info array */
{
	ut_a(monitor_id < NUM_MONITOR);

	return((monitor_id < NUM_MONITOR)
			? innodb_counter_info[monitor_id].monitor_name
			: NULL);
}

/****************************************************************//**
Turn on/off, reset monitor counters in a module. If module_id
is MONITOR_ALL_COUNTER then turn on all monitor counters.
@return	0 if successful, or the first monitor that cannot be
turned on because it has already been turned on. */
UNIV_INTERN
ulint
srv_mon_set_module_control(
/*=======================*/
	monitor_id_t	module_id,	/*!< in: Module ID as in
					monitor_counter_id. If it is
					set to MONITOR_ALL_COUNTER, this means
					we shall turn on all the counters */
	mon_option_t	set_option)	/*!< in: Turn on/off reset the
					counter */
{
	ulint	ix;

	ut_a(module_id <= NUM_MONITOR);
	ut_a(UT_ARR_SIZE(innodb_counter_info) == NUM_MONITOR);

	/* The module_id must be an ID of MONITOR_MODULE type */
	ut_a(innodb_counter_info[module_id].monitor_type & MONITOR_MODULE);

	/* start with the first monitor in the module. If module_id
	is MONITOR_ALL_COUNTER, this means we need to turn on all
	monitor counters. */
	for (ix = (module_id == MONITOR_ALL_COUNTER) ? 1 : module_id + 1;
	     ix < NUM_MONITOR; ix++) {
		/* Cannot turn on a monitor already been turned on. User
		should be aware some counters are already on before
		turn them on again (which could reset counter value) */
		if (MONITOR_IS_ON(ix) && (set_option == MONITOR_TURN_ON)) {
			return(ix);
		}

		/* if we hit the next module counter, we will
		continue if we want to turn on all monitor counters,
		and break if just turn on the counters in the
		current module. */
		if (innodb_counter_info[ix].monitor_type & MONITOR_MODULE) {
			if (module_id == MONITOR_ALL_COUNTER) {
				continue;
			} else {
				break;
			}
		}

		/* For some existing counters (server status variables),
		we will get its counter value at the start/stop time
		to calculate the actual value during the time. */
		if (innodb_counter_info[ix].monitor_type & MONITOR_EXISTING) {
			srv_mon_process_existing_counter(ix, set_option);
		}

		/* Currently support 4 operations on the monitor counters:
		turn on, turn off, reset and reset all operations. */
		switch (set_option) {
		case MONITOR_TURN_ON:
			MONITOR_ON(ix);
			MONITOR_INIT(ix);
			MONITOR_SET_START(ix);
			break;

		case MONITOR_TURN_OFF:
			MONITOR_OFF(ix);
			MONITOR_SET_OFF(ix);
			break;

		case MONITOR_RESET_VALUE:
			srv_mon_reset(ix);
			break;

		case MONITOR_RESET_ALL_VALUE:
			srv_mon_reset_all(ix);
			break;

		default:
			ut_error;
		}
	}

	return(0);
}

/****************************************************************//**
This function consolidates some existing server counters used
by "system status variables". These existing system variables do not have
mechanism to start/stop and reset the counters, so we simulate these
controls by remembering the corresponding counter values when the
corresponding monitors are turned on/off/reset, and do appropriate
mathematics to deduct the actual value. Please also refer to
srv_export_innodb_status() for related global counters used by
the existing status variables.*/
UNIV_INTERN
void
srv_mon_process_existing_counter(
/*=============================*/
	monitor_id_t	monitor_id,	/*!< in: the monitor's ID as in
					monitor_counter_id */
	mon_option_t	set_option)	/*!< in: Turn on/off reset the
					counter */
{
	ulint		value;
	monitor_info_t*	monitor_info;
	ibool		update_min = FALSE;
	buf_pool_stat_t	stat;
	ulint		LRU_len;
	ulint		free_len;
	ulint		flush_list_len;

	monitor_info = srv_mon_get_info(monitor_id);

	ut_a(monitor_info->monitor_type & MONITOR_EXISTING);
	ut_a(monitor_id < NUM_MONITOR);

	/* Get the value from corresponding global variable */
	switch (monitor_id) {
	/* export_vars.innodb_buffer_pool_reads. Num Reads from
	disk (page not in buffer) */
	case MONITOR_OVLD_BUF_POOL_READS:
		value = srv_buf_pool_reads;
		break;

	/* innodb_buffer_pool_read_requests, the number of logical
	read requests */
	case MONITOR_OVLD_BUF_POOL_READ_REQUESTS:
		buf_get_total_stat(&stat);
		value = stat.n_page_gets;
		break;

	/* innodb_buffer_pool_write_requests, the number of
	write request */
	case MONITOR_OVLD_BUF_POOL_WRITE_REQUEST:
		value = srv_buf_pool_write_requests;
		break;

	/* innodb_buffer_pool_wait_free */
	case MONITOR_OVLD_BUF_POOL_WAIT_FREE:
		value = srv_buf_pool_wait_free;
		break;

	/* innodb_buffer_pool_read_ahead */
	case MONITOR_OVLD_BUF_POOL_READ_AHEAD:
		buf_get_total_stat(&stat);
		value = stat.n_ra_pages_read;
		break;

	/* innodb_buffer_pool_read_ahead_evicted */
	case MONITOR_OVLD_BUF_POOL_READ_AHEAD_EVICTED:
		buf_get_total_stat(&stat);
		value = stat.n_ra_pages_evicted;
		break;

	/* innodb_buffer_pool_pages_total */
	case MONITOR_OVLD_BUF_POOL_PAGE_TOTAL:
		value = buf_pool_get_curr_size();
		break;

	/* innodb_buffer_pool_pages_misc */
	case MONITOR_OVLD_BUF_POOL_PAGE_MISC:
		buf_get_total_list_len(&LRU_len, &free_len, &flush_list_len);
		value = buf_pool_get_curr_size() - LRU_len - free_len;
		break;

	/* innodb_buffer_pool_pages_data */
	case MONITOR_OVLD_BUF_POOL_PAGES_DATA:
		buf_get_total_list_len(&LRU_len, &free_len, &flush_list_len);
		value = LRU_len;
		break;

	/* innodb_buffer_pool_pages_dirty */
	case MONITOR_OVLD_BUF_POOL_PAGES_DIRTY:
		buf_get_total_list_len(&LRU_len, &free_len, &flush_list_len);
		value = flush_list_len;
		break;

	/* innodb_buffer_pool_pages_free */
	case MONITOR_OVLD_BUF_POOL_PAGES_FREE:
		buf_get_total_list_len(&LRU_len, &free_len, &flush_list_len);
		value = free_len;
		break;

	/* innodb_pages_created, the number of pages created */
	case MONITOR_OVLD_PAGE_CREATED:
		buf_get_total_stat(&stat);
		value = stat.n_pages_created;
		break;

	/* innodb_pages_written, the number of page written */
	case MONITOR_OVLD_PAGES_WRITTEN:
		buf_get_total_stat(&stat);
		value = stat.n_pages_written;
		break;

	/* innodb_pages_read */
	case MONITOR_OVLD_PAGES_READ:
		buf_get_total_stat(&stat);
		value = stat.n_pages_read;
		break;

	/* innodb_data_writes, the total number of data writes. */
	case MONITOR_OVLD_BYTE_READ:
		value = srv_data_read;
		break;

	/* innodb_data_reads, the total number of data reads */
	case MONITOR_OVLD_BYTE_WRITTEN:
		value = srv_data_written;
		break;

	/* innodb_data_reads, the total number of data reads. */
	case MONITOR_OVLD_OS_FILE_READ:
		value = os_n_file_reads;
		break;

	/* innodb_data_writes, the total number of data writes*/
	case MONITOR_OVLD_OS_FILE_WRITE:
		value = os_n_file_writes;
		break;

	/* innodb_data_fsyncs, number of fsync() operations so far. */
	case MONITOR_OVLD_OS_FSYNC:
		value = os_n_fsyncs;
		break;

	/* innodb_os_log_written */
	case MONITOR_OVLD_OS_LOG_WRITTEN:
		value = srv_os_log_written;
		break;

	/* innodb_os_log_fsyncs */
	case MONITOR_OVLD_OS_LOG_FSYNC:
		value = fil_n_log_flushes;
		break;

	/* innodb_os_log_pending_fsyncs */
	case MONITOR_OVLD_OS_LOG_PENDING_FSYNC:
		value = fil_n_pending_log_flushes;
		update_min = TRUE;
		break;

	/* innodb_os_log_pending_writes */
	case MONITOR_OVLD_OS_LOG_PENDING_WRITES:
		value = srv_os_log_pending_writes;
		update_min = TRUE;
		break;

	/* innodb_log_waits */
	case MONITOR_OVLD_LOG_WAITS:
		value = srv_log_waits;
		break;

	/* innodb_log_write_requests */
	case MONITOR_OVLD_LOG_WRITE_REQUEST:
		value = srv_log_write_requests;
		break;

	/* innodb_log_writes */
	case MONITOR_OVLD_LOG_WRITES:
		value = srv_log_writes;
		break;

	/* innodb_rows_read */
	case MONITOR_OLVD_ROW_READ:
		value = srv_n_rows_read;
		break;

	/* innodb_rows_inserted */
	case MONITOR_OLVD_ROW_INSERTED:
		value = srv_n_rows_inserted;
		break;

	/* innodb_rows_deleted */
	case MONITOR_OLVD_ROW_DELETED:
		value = srv_n_rows_deleted;
		break;

	/* innodb_rows_updated */
	case MONITOR_OLVD_ROW_UPDTATED:
		value = srv_n_rows_updated;
		break;

	/* innodb_row_lock_waits */
	case MONITOR_OVLD_ROW_LOCK_WAIT:
		value = srv_n_lock_wait_count;
		break;

	case MONITOR_RSEG_HISTORY_LEN:
		value = trx_sys->rseg_history_len;
		break;

	default:
		ut_error;
	}

	switch (set_option) {
	case MONITOR_TURN_ON:
		/* Save the initial counter value in mon_start_value
		field */
		MONITOR_SAVE_START(monitor_id, value);
		return;

	case MONITOR_TURN_OFF:
		/* Save the counter value to mon_last_value when we
		turn off the monitor but not yet reset. Note the
		counter has not yet been set to off in the bitmap
		table for normal turn off. We need to check the
		count status (on/off) to avoid reset the value
		for an already off conte */
		if (MONITOR_IS_ON(monitor_id)) {
			srv_mon_process_existing_counter(monitor_id,
							 MONITOR_GET_VALUE);
			MONITOR_SAVE_LAST(monitor_id);
		}
		return;

	case MONITOR_GET_VALUE:
		if (MONITOR_IS_ON(monitor_id)) {

			/* If MONITOR_DISPLAY_CURRENT bit is on, we
			only record the current value, rather than
			incremental value over a period. Most of
`			this type of counters are resource related
			counters such as number of buffer pages etc. */
			if (monitor_info->monitor_type
			    & MONITOR_DISPLAY_CURRENT) {
				MONITOR_SET(monitor_id, value);
			} else {
				/* Most status counters are montonically
				increasing, no need to update their
				minimum values. Only do so
				if "update_min" set to TRUE */
				MONITOR_SET_DIFF(monitor_id, value);

				if (update_min
				    && (MONITOR_VALUE(monitor_id)
					< MONITOR_MIN_VALUE(monitor_id))) {
					MONITOR_MIN_VALUE(monitor_id) =
						MONITOR_VALUE(monitor_id);
				}
			}
		}
		return;

	case MONITOR_RESET_VALUE:
		if (!MONITOR_IS_ON(monitor_id)) {
			MONITOR_LAST_VALUE(monitor_id) = 0;
		}
		return;

	/* Nothing special for reset all operation for these existing
	counters */
	case MONITOR_RESET_ALL_VALUE:
		return;
	}
}

/*************************************************************//**
Reset a monitor, create a new base line with the current monitor
value. This baseline is recorded by MONITOR_VALUE_RESET(monitor) */
UNIV_INTERN
void
srv_mon_reset(
/*==========*/
	monitor_id_t	monitor)	/*!< in: monitor id */
{
	ibool	monitor_was_on;

	monitor_was_on = MONITOR_IS_ON(monitor);

	if (monitor_was_on) {
		/* Temporarily turn off the counter for the resetting
		operation */
		MONITOR_OFF(monitor);
	}

	/* Before resetting the current monitor value, first
	calculate and set the max/min value since monitor
	start */
	srv_mon_calc_max_since_start(monitor);
	srv_mon_calc_min_since_start(monitor);

	/* Monitors with MONITOR_DISPLAY_CURRENT bit
	are not incremental, no need to remember
	the reset value. */
	if (innodb_counter_info[monitor].monitor_type
	    & MONITOR_DISPLAY_CURRENT) {
		MONITOR_VALUE_RESET(monitor) = 0;
	} else {
		/* Remember the new baseline */
		MONITOR_VALUE_RESET(monitor) = MONITOR_VALUE_RESET(monitor)
					       + MONITOR_VALUE(monitor);
	}

	/* Reset the counter value */
	MONITOR_VALUE(monitor) = 0;
	MONITOR_MAX_VALUE(monitor) = MAX_RESERVED;
	MONITOR_MIN_VALUE(monitor) = MIN_RESERVED;

	MONITOR_FIELD((monitor), mon_reset_time) = time(NULL);

	if (monitor_was_on) {
		MONITOR_ON(monitor);
	}
}
