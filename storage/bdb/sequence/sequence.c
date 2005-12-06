/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2004-2005
 *	Sleepycat Software.  All rights reserved.
 *
 * $Id: sequence.c,v 12.28 2005/10/24 19:22:00 bostic Exp $
 */

#include "db_config.h"

#ifndef NO_SYSTEM_INCLUDES
#include <sys/types.h>

#ifdef HAVE_RPC
#include <rpc/rpc.h>
#endif

#include <string.h>
#endif

#ifdef HAVE_RPC
#include "db_server.h"
#endif

#include "db_int.h"
#include "dbinc/db_page.h"
#include "dbinc/db_shash.h"
#include "dbinc/db_am.h"
#include "dbinc/lock.h"
#include "dbinc/mp.h"
#include "dbinc/txn.h"
#include "dbinc_auto/sequence_ext.h"

#ifdef HAVE_RPC
#include "dbinc_auto/rpc_client_ext.h"
#endif

#ifdef HAVE_SEQUENCE
#define	SEQ_ILLEGAL_AFTER_OPEN(seq, name)				\
	if (seq->seq_key.data != NULL)					\
		return (__db_mi_open((seq)->seq_dbp->dbenv, name, 1));

#define	SEQ_ILLEGAL_BEFORE_OPEN(seq, name)				\
	if (seq->seq_key.data == NULL)					\
		return (__db_mi_open((seq)->seq_dbp->dbenv, name, 0));

#define	SEQ_IS_OPEN(seq)	((seq)->seq_key.data != NULL)

/*
 * Sequences must be architecture independent but they are stored as user
 * data in databases so the code here must handle the byte ordering.  We
 * store them in little-endian byte ordering.  If we are on a big-endian
 * machine we swap in and out when we read from the database. seq->seq_rp
 * always points to the record in native ordering.
 *
 * Version 1 always stored things in native format so if we detect this we
 * upgrade on the fly and write the record back at open time.
 */
#define	SEQ_SWAP(rp)							\
	do {								\
		M_32_SWAP((rp)->seq_version);				\
		M_32_SWAP((rp)->flags);					\
		M_64_SWAP((rp)->seq_value);				\
		M_64_SWAP((rp)->seq_max);				\
		M_64_SWAP((rp)->seq_min);				\
	} while (0)

#define	SEQ_SWAP_IN(seq) \
	do {								\
		if (__db_isbigendian()) {				\
			memcpy(&seq->seq_record, seq->seq_data.data,	\
			     sizeof(seq->seq_record));			\
			SEQ_SWAP(&seq->seq_record);			\
		}							\
	} while (0)

#define	SEQ_SWAP_OUT(seq) \
	do {								\
		if (__db_isbigendian()) {				\
			memcpy(seq->seq_data.data,			\
			     &seq->seq_record, sizeof(seq->seq_record));\
			SEQ_SWAP((DB_SEQ_RECORD*)seq->seq_data.data);	\
		}							\
	} while (0)

static int __seq_chk_cachesize __P((DB_ENV *, int32_t, db_seq_t, db_seq_t));
static int __seq_close __P((DB_SEQUENCE *, u_int32_t));
static int __seq_get
	       __P((DB_SEQUENCE *, DB_TXN *, int32_t,  db_seq_t *, u_int32_t));
static int __seq_get_cachesize __P((DB_SEQUENCE *, int32_t *));
static int __seq_get_db __P((DB_SEQUENCE *, DB **));
static int __seq_get_flags __P((DB_SEQUENCE *, u_int32_t *));
static int __seq_get_key __P((DB_SEQUENCE *, DBT *));
static int __seq_get_range __P((DB_SEQUENCE *, db_seq_t *, db_seq_t *));
static int __seq_initial_value __P((DB_SEQUENCE *, db_seq_t));
static int __seq_open_pp __P((DB_SEQUENCE *, DB_TXN *, DBT *, u_int32_t));
static int __seq_remove __P((DB_SEQUENCE *, DB_TXN *, u_int32_t));
static int __seq_set_cachesize __P((DB_SEQUENCE *, int32_t));
static int __seq_set_flags __P((DB_SEQUENCE *, u_int32_t));
static int __seq_set_range __P((DB_SEQUENCE *, db_seq_t, db_seq_t));
static int __seq_update __P((DB_SEQUENCE *, DB_TXN *, int32_t, u_int32_t));

/*
 * db_sequence_create --
 *	DB_SEQUENCE constructor.
 *
 * EXTERN: int db_sequence_create __P((DB_SEQUENCE **, DB *, u_int32_t));
 */
int
db_sequence_create(seqp, dbp, flags)
	DB_SEQUENCE **seqp;
	DB *dbp;
	u_int32_t flags;
{
	DB_ENV *dbenv;
	DB_SEQUENCE *seq;
	int ret;

	dbenv = dbp->dbenv;

	DB_ILLEGAL_BEFORE_OPEN(dbp, "db_sequence_create");
#ifdef HAVE_RPC
	if (RPC_ON(dbenv))
		return (__dbcl_dbenv_illegal(dbenv));
#endif

	/* Check for invalid function flags. */
	switch (flags) {
	case 0:
		break;
	default:
		return (__db_ferr(dbenv, "db_sequence_create", 0));
	}

	/* Allocate the sequence. */
	if ((ret = __os_calloc(dbenv, 1, sizeof(*seq), &seq)) != 0)
		return (ret);

	seq->seq_dbp = dbp;
	seq->close = __seq_close;
	seq->get = __seq_get;
	seq->get_cachesize = __seq_get_cachesize;
	seq->set_cachesize = __seq_set_cachesize;
	seq->get_db = __seq_get_db;
	seq->get_flags = __seq_get_flags;
	seq->get_key = __seq_get_key;
	seq->get_range = __seq_get_range;
	seq->initial_value = __seq_initial_value;
	seq->open = __seq_open_pp;
	seq->remove = __seq_remove;
	seq->set_flags = __seq_set_flags;
	seq->set_range = __seq_set_range;
	seq->stat = __seq_stat;
	seq->stat_print = __seq_stat_print;
	seq->seq_rp = &seq->seq_record;
	*seqp = seq;

	return (0);
}

/*
 * __seq_open --
 *	DB_SEQUENCE->open method.
 *
 */
static int
__seq_open_pp(seq, txn, keyp, flags)
	DB_SEQUENCE *seq;
	DB_TXN *txn;
	DBT *keyp;
	u_int32_t flags;
{
	DB *dbp;
	DB_ENV *dbenv;
	DB_SEQ_RECORD *rp;
	DB_THREAD_INFO *ip;
	u_int32_t tflags;
	int handle_check, txn_local, ret, t_ret;
#define	SEQ_OPEN_FLAGS	(DB_CREATE | DB_EXCL | DB_THREAD)

	dbp = seq->seq_dbp;
	dbenv = dbp->dbenv;
	txn_local = 0;

	STRIP_AUTO_COMMIT(flags);
	SEQ_ILLEGAL_AFTER_OPEN(seq, "DB_SEQUENCE->open");

	ENV_ENTER(dbenv, ip);

	/* Check for replication block. */
	handle_check = IS_ENV_REPLICATED(dbenv);
	if (handle_check &&
	    (ret = __db_rep_enter(dbp, 1, 0, txn != NULL)) != 0) {
		handle_check = 0;
		goto err;
	}

	if ((ret = __db_fchk(dbenv,
	    "DB_SEQUENCE->open", flags, SEQ_OPEN_FLAGS)) != 0)
		goto err;

	if (keyp->size == 0) {
		__db_err(dbenv, "Zero length sequence key specified");
		goto err;
	}

	if ((ret = __db_get_flags(dbp, &tflags)) != 0)
		goto err;

	if (FLD_ISSET(tflags, DB_DUP)) {
		__db_err(dbenv,
	"Sequences not supported in databases configured for duplicate data");
		goto err;
	}

	if (LF_ISSET(DB_THREAD)) {
		if (RPC_ON(dbenv)) {
			__db_err(dbenv,
		    "DB_SEQUENCE->open: DB_THREAD not supported with RPC");
			goto err;
		}
		if ((ret = __mutex_alloc(dbenv,
		    MTX_SEQUENCE, DB_MUTEX_THREAD, &seq->mtx_seq)) != 0)
			goto err;
	}

	memset(&seq->seq_data, 0, sizeof(DBT));
	if (__db_isbigendian()) {
		if ((ret = __os_umalloc(dbenv,
		     sizeof(seq->seq_record), &seq->seq_data.data)) != 0)
			goto err;
		seq->seq_data.flags = DB_DBT_REALLOC;
	} else {
		seq->seq_data.data = &seq->seq_record;
		seq->seq_data.flags = DB_DBT_USERMEM;
	}

	seq->seq_data.ulen = seq->seq_data.size = sizeof(seq->seq_record);
	seq->seq_rp = &seq->seq_record;

	memset(&seq->seq_key, 0, sizeof(DBT));
	if ((ret = __os_malloc(dbenv, keyp->size, &seq->seq_key.data)) != 0)
		goto err;
	memcpy(seq->seq_key.data, keyp->data, keyp->size);
	seq->seq_key.size = seq->seq_key.ulen = keyp->size;
	seq->seq_key.flags = DB_DBT_USERMEM;

retry:	if ((ret = __db_get(dbp, txn, &seq->seq_key, &seq->seq_data, 0)) != 0) {
		if (ret == DB_BUFFER_SMALL &&
		    seq->seq_data.size > sizeof(seq->seq_record)) {
			seq->seq_data.flags = DB_DBT_REALLOC;
			seq->seq_data.data = NULL;
			goto retry;
		}
		if ((ret != DB_NOTFOUND && ret != DB_KEYEMPTY) ||
		    !LF_ISSET(DB_CREATE))
			goto err;
		ret = 0;

		rp = &seq->seq_record;
		if (!F_ISSET(rp, DB_SEQ_RANGE_SET)) {
			rp->seq_max = INT64_MAX;
			rp->seq_min = INT64_MIN;
		}
		/* INC is the default. */
		if (!F_ISSET(rp, DB_SEQ_DEC))
			F_SET(rp, DB_SEQ_INC);

		rp->seq_version = DB_SEQUENCE_VERSION;

		if (rp->seq_value > rp->seq_max ||
		    rp->seq_value < rp->seq_min) {
			__db_err(dbenv, "Sequence value out of range");
			ret = EINVAL;
			goto err;
		} else {
			SEQ_SWAP_OUT(seq);
			if ((ret = __db_put(dbp, txn, &seq->seq_key,
			     &seq->seq_data, DB_NOOVERWRITE)) != 0) {
				__db_err(dbenv, "Sequence create failed");
				goto err;
			}
		}
	} else if (LF_ISSET(DB_CREATE) && LF_ISSET(DB_EXCL)) {
		ret = EEXIST;
		goto err;
	} else if (seq->seq_data.size < sizeof(seq->seq_record)) {
		__db_err(dbenv, "Bad sequence record format");
		ret = EINVAL;
		goto err;
	}

	if (!__db_isbigendian())
		seq->seq_rp = seq->seq_data.data;

	/*
	 * The first release was stored in native mode.
	 * Check the version number before swapping.
	 */
	rp = seq->seq_data.data;
	if (rp->seq_version == DB_SEQUENCE_OLDVER) {
oldver:		rp->seq_version = DB_SEQUENCE_VERSION;
		if (__db_isbigendian()) {
			if (IS_DB_AUTO_COMMIT(dbp, txn)) {
				if ((ret =
				    __txn_begin(dbenv, NULL, &txn, 0)) != 0)
					goto err;
				txn_local = 1;
				goto retry;
			}
			memcpy(&seq->seq_record, rp, sizeof(seq->seq_record));
			SEQ_SWAP_OUT(seq);
		}
		if ((ret = __db_put(dbp,
		     txn, &seq->seq_key, &seq->seq_data, 0)) != 0)
			goto err;
	}
	rp = seq->seq_rp;

	SEQ_SWAP_IN(seq);

	if (rp->seq_version != DB_SEQUENCE_VERSION) {
		/*
		 * The database may have moved from one type
		 * of machine to another, check here.
		 * If we moved from little-end to big-end then
		 * the swap above will make the version correct.
		 * If the move was from big to little
		 * then we need to swap to see if this
		 * is an old version.
		 */
		if (rp->seq_version == DB_SEQUENCE_OLDVER)
			goto oldver;
		M_32_SWAP(rp->seq_version);
		if (rp->seq_version == DB_SEQUENCE_OLDVER) {
			SEQ_SWAP(rp);
			goto oldver;
		}
		M_32_SWAP(rp->seq_version);
		__db_err(dbenv,
		     "Unsupported sequence version: %d", rp->seq_version);
		goto err;
	}

	seq->seq_last_value = rp->seq_value;
	if (F_ISSET(rp, DB_SEQ_INC))
		seq->seq_last_value--;
	else
		seq->seq_last_value++;

	/*
	 * It's an error to specify a cache larger than the range of sequences.
	 */
	if (seq->seq_cache_size != 0 && (ret = __seq_chk_cachesize(
	    dbenv, seq->seq_cache_size, rp->seq_max, rp->seq_min)) != 0)
		goto err;

err:	if (txn_local &&
	    (t_ret = __db_txn_auto_resolve(dbenv, txn, 0, ret)) && ret == 0)
		ret = t_ret;
	if (ret != 0) {
		__os_free(dbenv, seq->seq_key.data);
		seq->seq_key.data = NULL;
	}
	/* Release replication block. */
	if (handle_check && (t_ret = __env_db_rep_exit(dbenv)) != 0 && ret == 0)
		ret = t_ret;

	ENV_LEAVE(dbenv, ip);
	return (ret);
}

/*
 * __seq_get_cachesize --
 *	Accessor for value passed into DB_SEQUENCE->set_cachesize call.
 *
 */
static int
__seq_get_cachesize(seq, cachesize)
	DB_SEQUENCE *seq;
	int32_t *cachesize;
{
	SEQ_ILLEGAL_BEFORE_OPEN(seq, "DB_SEQUENCE->get_cachesize");

	*cachesize = seq->seq_cache_size;
	return (0);
}

/*
 * __seq_set_cachesize --
 *	DB_SEQUENCE->set_cachesize.
 *
 */
static int
__seq_set_cachesize(seq, cachesize)
	DB_SEQUENCE *seq;
	int32_t cachesize;
{
	DB_ENV *dbenv;
	int ret;

	dbenv = seq->seq_dbp->dbenv;

	if (cachesize < 0) {
		__db_err(dbenv, "Cache size must be >= 0");
		return (EINVAL);
	}

	/*
	 * It's an error to specify a cache larger than the range of sequences.
	 */
	if (SEQ_IS_OPEN(seq) && (ret = __seq_chk_cachesize(dbenv,
	    cachesize, seq->seq_rp->seq_max, seq->seq_rp->seq_min)) != 0)
		return (ret);

	seq->seq_cache_size = cachesize;
	return (0);
}

#define	SEQ_SET_FLAGS	(DB_SEQ_WRAP | DB_SEQ_INC | DB_SEQ_DEC)
/*
 * __seq_get_flags --
 *	Accessor for flags passed into DB_SEQUENCE->open call
 *
 */
static int
__seq_get_flags(seq, flagsp)
	DB_SEQUENCE *seq;
	u_int32_t *flagsp;
{
	SEQ_ILLEGAL_BEFORE_OPEN(seq, "DB_SEQUENCE->get_flags");

	*flagsp = F_ISSET(seq->seq_rp, SEQ_SET_FLAGS);
	return (0);
}

/*
 * __seq_set_flags --
 *	DB_SEQUENCE->set_flags.
 *
 */
static int
__seq_set_flags(seq, flags)
	DB_SEQUENCE *seq;
	u_int32_t flags;
{
	DB_ENV *dbenv;
	DB_SEQ_RECORD *rp;
	int ret;

	dbenv = seq->seq_dbp->dbenv;
	rp = seq->seq_rp;

	SEQ_ILLEGAL_AFTER_OPEN(seq, "DB_SEQUENCE->set_flags");

	if ((ret = __db_fchk(
	    dbenv, "DB_SEQUENCE->set_flags", flags, SEQ_SET_FLAGS)) != 0)
		return (ret);
	if ((ret = __db_fcchk(dbenv,
	     "DB_SEQUENCE->set_flags", flags, DB_SEQ_DEC, DB_SEQ_INC)) != 0)
		return (ret);

	if (LF_ISSET(DB_SEQ_DEC | DB_SEQ_INC))
		F_CLR(rp, DB_SEQ_DEC | DB_SEQ_INC);
	F_SET(rp, flags);

	return (0);
}

/*
 * __seq_initial_value --
 *	DB_SEQUENCE->initial_value.
 *
 */
static int
__seq_initial_value(seq, value)
	DB_SEQUENCE *seq;
	db_seq_t value;
{
	DB_ENV *dbenv;
	DB_SEQ_RECORD *rp;

	dbenv = seq->seq_dbp->dbenv;
	SEQ_ILLEGAL_AFTER_OPEN(seq, "DB_SEQUENCE->initial_value");

	rp = seq->seq_rp;
	if (F_ISSET(rp, DB_SEQ_RANGE_SET) &&
	     (value > rp->seq_max || value < rp->seq_min)) {
		__db_err(dbenv, "Sequence value out of range");
		return (EINVAL);
	}

	rp->seq_value = value;

	return (0);
}

/*
 * __seq_get_range --
 *	Accessor for range passed into DB_SEQUENCE->set_range call
 *
 */
static int
__seq_get_range(seq, minp, maxp)
	DB_SEQUENCE *seq;
	db_seq_t *minp, *maxp;
{
	SEQ_ILLEGAL_BEFORE_OPEN(seq, "DB_SEQUENCE->get_range");

	*minp = seq->seq_rp->seq_min;
	*maxp = seq->seq_rp->seq_max;
	return (0);
}

/*
 * __seq_set_range --
 *	SEQUENCE->set_range.
 *
 */
static int
__seq_set_range(seq, min, max)
	DB_SEQUENCE *seq;
	db_seq_t min, max;
{
	DB_ENV *dbenv;

	dbenv = seq->seq_dbp->dbenv;
	SEQ_ILLEGAL_AFTER_OPEN(seq, "DB_SEQUENCE->set_range");

	if (min >= max) {
		__db_err(dbenv,
	    "Minimum sequence value must be less than maximum sequence value");
		return (EINVAL);
	}

	seq->seq_rp->seq_min = min;
	seq->seq_rp->seq_max = max;
	F_SET(seq->seq_rp, DB_SEQ_RANGE_SET);

	return (0);
}

static int
__seq_update(seq, txn, delta, flags)
	DB_SEQUENCE *seq;
	DB_TXN *txn;
	int32_t delta;
	u_int32_t flags;
{
	DB *dbp;
	DB_ENV *dbenv;
	DB_SEQ_RECORD *rp;
	int32_t adjust;
	int ret, txn_local;

	dbp = seq->seq_dbp;
	dbenv = dbp->dbenv;

	/*
	 * Create a local transaction as necessary, check for consistent
	 * transaction usage, and, if we have no transaction but do have
	 * locking on, acquire a locker id for the handle lock acquisition.
	 */
	if (IS_DB_AUTO_COMMIT(dbp, txn)) {
		if ((ret = __txn_begin(dbenv, NULL, &txn, 0)) != 0)
			return (ret);
		txn_local = 1;
	} else
		txn_local = 0;

	/* Check for consistent transaction usage. */
	if ((ret = __db_check_txn(dbp, txn, DB_LOCK_INVALIDID, 0)) != 0)
		goto err;

retry:	if ((ret = __db_get(dbp, txn, &seq->seq_key, &seq->seq_data, 0)) != 0) {
		if (ret == DB_BUFFER_SMALL &&
		    seq->seq_data.size > sizeof(seq->seq_record)) {
			seq->seq_data.flags = DB_DBT_REALLOC;
			seq->seq_data.data = NULL;
			goto retry;
		}
		goto err;
	}

	if (!__db_isbigendian())
		seq->seq_rp = seq->seq_data.data;
	SEQ_SWAP_IN(seq);
	rp = seq->seq_rp;

	if (F_ISSET(rp, DB_SEQ_WRAPPED))
		goto overflow;

	if (seq->seq_data.size < sizeof(seq->seq_record)) {
		__db_err(dbenv, "Bad sequence record format");
		ret = EINVAL;
		goto err;
	}

	adjust = delta > seq->seq_cache_size ? delta : seq->seq_cache_size;

	/*
	 * Check whether this operation will cause the sequence to wrap.
	 *
	 * The sequence minimum and maximum values can be INT64_MIN and
	 * INT64_MAX, so we need to do the test carefully to cope with
	 * arithmetic overflow.  The first part of the test below checks
	 * whether we will hit the end of the 64-bit range.  The second part
	 * checks whether we hit the end of the sequence.
	 */
again:	if (F_ISSET(rp, DB_SEQ_INC)) {
		if (rp->seq_value + adjust - 1 < rp->seq_value ||
		     rp->seq_value + adjust - 1 > rp->seq_max) {
			/* Don't wrap just to fill the cache. */
			if (adjust > delta) {
				adjust = delta;
				goto again;
			}
			if (F_ISSET(rp, DB_SEQ_WRAP))
				rp->seq_value = rp->seq_min;
			else {
overflow:			__db_err(dbenv, "Sequence overflow");
				ret = EINVAL;
				goto err;
			}
		}
		/* See if we are at the end of the 64 bit range. */
		if (!F_ISSET(rp, DB_SEQ_WRAP) &&
		    rp->seq_value + adjust < rp->seq_value)
			F_SET(rp, DB_SEQ_WRAPPED);
	} else {
		if ((rp->seq_value - adjust) + 1 > rp->seq_value ||
		   (rp->seq_value - adjust) + 1 < rp->seq_min) {
			/* Don't wrap just to fill the cache. */
			if (adjust > delta) {
				adjust = delta;
				goto again;
			}
			if (F_ISSET(rp, DB_SEQ_WRAP))
				rp->seq_value = rp->seq_max;
			else
				goto overflow;
		}
		/* See if we are at the end of the 64 bit range. */
		if (!F_ISSET(rp, DB_SEQ_WRAP) &&
		    rp->seq_value - adjust > rp->seq_value)
			F_SET(rp, DB_SEQ_WRAPPED);
		adjust = -adjust;
	}

	rp->seq_value += adjust;
	SEQ_SWAP_OUT(seq);
	ret = __db_put(dbp, txn, &seq->seq_key, &seq->seq_data, 0);
	rp->seq_value -= adjust;
	if (ret != 0) {
		__db_err(dbenv, "Sequence update failed");
		goto err;
	}
	seq->seq_last_value = rp->seq_value + adjust;
	if (F_ISSET(rp, DB_SEQ_INC))
		seq->seq_last_value--;
	else
		seq->seq_last_value++;

err:	return (txn_local ? __db_txn_auto_resolve(
	    dbenv, txn, LF_ISSET(DB_TXN_NOSYNC), ret) : ret);
}

static int
__seq_get(seq, txn, delta, retp, flags)
	DB_SEQUENCE *seq;
	DB_TXN *txn;
	int32_t delta;
	db_seq_t *retp;
	u_int32_t flags;
{
	DB *dbp;
	DB_ENV *dbenv;
	DB_SEQ_RECORD *rp;
	DB_THREAD_INFO *ip;
	int handle_check, ret, t_ret;

	dbp = seq->seq_dbp;
	dbenv = dbp->dbenv;
	rp = seq->seq_rp;
	ret = 0;

	STRIP_AUTO_COMMIT(flags);
	SEQ_ILLEGAL_BEFORE_OPEN(seq, "DB_SEQUENCE->get");

	if (delta <= 0) {
		__db_err(dbenv, "Sequence delta must be greater than 0");
		return (EINVAL);
	}

	if (seq->seq_cache_size != 0 && txn != NULL) {
		__db_err(dbenv,
	    "Sequence with non-zero cache may not specify transaction handle");
		return (EINVAL);
	}

	ENV_ENTER(dbenv, ip);

	/* Check for replication block. */
	handle_check = IS_ENV_REPLICATED(dbenv);
	if (handle_check && (ret = __db_rep_enter(dbp, 1, 0, txn != NULL)) != 0)
		return (ret);

	MUTEX_LOCK(dbenv, seq->mtx_seq);

	if (rp->seq_min + delta > rp->seq_max) {
		__db_err(dbenv, "Sequence overflow");
		ret = EINVAL;
		goto err;
	}

	if (F_ISSET(rp, DB_SEQ_INC)) {
		if (seq->seq_last_value + 1 - rp->seq_value < delta &&
		   (ret = __seq_update(seq, txn, delta, flags)) != 0)
			goto err;

		rp = seq->seq_rp;
		*retp = rp->seq_value;
		rp->seq_value += delta;
	} else {
		if ((rp->seq_value - seq->seq_last_value) + 1 < delta &&
		    (ret = __seq_update(seq, txn, delta, flags)) != 0)
			goto err;

		rp = seq->seq_rp;
		*retp = rp->seq_value;
		rp->seq_value -= delta;
	}

err:	MUTEX_UNLOCK(dbenv, seq->mtx_seq);

	/* Release replication block. */
	if (handle_check && (t_ret = __env_db_rep_exit(dbenv)) != 0 && ret == 0)
		ret = t_ret;

	ENV_LEAVE(dbenv, ip);
	return (ret);
}

/*
 * __seq_get_db --
 *	Accessor for dbp passed into DB_SEQUENCE->open call
 *
 */
static int
__seq_get_db(seq, dbpp)
	DB_SEQUENCE *seq;
	DB **dbpp;
{
	SEQ_ILLEGAL_BEFORE_OPEN(seq, "DB_SEQUENCE->get_db");

	*dbpp = seq->seq_dbp;
	return (0);
}

/*
 * __seq_get_key --
 *	Accessor for key passed into DB_SEQUENCE->open call
 *
 */
static int
__seq_get_key(seq, key)
	DB_SEQUENCE *seq;
	DBT *key;
{
	SEQ_ILLEGAL_BEFORE_OPEN(seq, "DB_SEQUENCE->get_key");

	key->data = seq->seq_key.data;
	key->size = key->ulen = seq->seq_key.size;
	key->flags = seq->seq_key.flags;
	return (0);
}

/*
 * __seq_close --
 *	Close a sequence
 *
 */
static int
__seq_close(seq, flags)
	DB_SEQUENCE *seq;
	u_int32_t flags;
{
	DB_ENV *dbenv;
	int ret, t_ret;

	ret = 0;
	dbenv = seq->seq_dbp->dbenv;

	if (flags != 0)
		ret = __db_ferr(dbenv, "DB_SEQUENCE->close", 0);

	if ((t_ret = __mutex_free(dbenv, &seq->mtx_seq)) != 0 && ret == 0)
		ret = t_ret;

	if (seq->seq_key.data != NULL)
		__os_free(dbenv, seq->seq_key.data);
	if (seq->seq_data.data != NULL &&
	    seq->seq_data.data != &seq->seq_record)
		__os_ufree(dbenv, seq->seq_data.data);
	seq->seq_key.data = NULL;

	memset(seq, CLEAR_BYTE, sizeof(*seq));
	__os_free(dbenv, seq);

	return (ret);
}

/*
 * __seq_remove --
 *	Remove a sequence from the database.
 */
static int
__seq_remove(seq, txn, flags)
	DB_SEQUENCE *seq;
	DB_TXN *txn;
	u_int32_t flags;
{
	DB *dbp;
	DB_ENV *dbenv;
	DB_THREAD_INFO *ip;
	int handle_check, ret, t_ret;

	dbp = seq->seq_dbp;
	dbenv = dbp->dbenv;

	SEQ_ILLEGAL_BEFORE_OPEN(seq, "DB_SEQUENCE->remove");
	ENV_ENTER(dbenv, ip);

	/* Check for replication block. */
	handle_check = IS_ENV_REPLICATED(dbenv);
	if (handle_check &&
	    (ret = __db_rep_enter(dbp, 1, 0, txn != NULL)) != 0) {
		handle_check = 0;
		goto err;
	}
	if (flags != 0)
		ret = __db_ferr(dbenv, "DB_SEQUENCE->remove", 0);

	ret = __db_del(dbp, txn, &seq->seq_key, 0);

	if ((t_ret = __seq_close(seq, 0)) != 0 && ret == 0)
		ret = t_ret;

	/* Release replication block. */
	if (handle_check && (t_ret = __env_db_rep_exit(dbenv)) != 0 && ret == 0)
		ret = t_ret;
err:	ENV_LEAVE(dbenv, ip);
	return (ret);
}

/*
 * __seq_chk_cachesize --
 *	Validate the cache size vs. the range.
 */
static int
__seq_chk_cachesize(dbenv, cachesize, max, min)
	DB_ENV *dbenv;
	int32_t cachesize;
	db_seq_t max, min;
{
	/*
	 * It's an error to specify caches larger than the sequence range.
	 *
	 * The min and max of the range can be either positive or negative,
	 * the difference will fit in an unsigned variable of the same type.
	 * Assume a 2's complement machine, and simply subtract.
	 */
	if ((u_int32_t)cachesize > (u_int64_t)max - (u_int64_t)min) {
		__db_err(dbenv,
    "Number of items to be cached is larger than the sequence range");
		return (EINVAL);
	}
	return (0);
}

#else /* !HAVE_SEQUENCE */

int
db_sequence_create(seqp, dbp, flags)
	DB_SEQUENCE **seqp;
	DB *dbp;
	u_int32_t flags;
{
	COMPQUIET(seqp, NULL);
	COMPQUIET(flags, 0);
	__db_err(dbp->dbenv,
	    "library build did not include support for sequences");
	return (DB_OPNOTSUP);
}
#endif /* HAVE_SEQUENCE */
