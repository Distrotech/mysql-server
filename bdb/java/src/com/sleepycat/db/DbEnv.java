/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1997-2002
 *      Sleepycat Software.  All rights reserved.
 *
 * $Id: DbEnv.java,v 11.58 2002/08/29 14:22:22 margo Exp $
 */

package com.sleepycat.db;

import java.io.OutputStream;
import java.io.FileNotFoundException;
import java.util.Date;
import java.util.Enumeration;
import java.util.Vector;

/**
 *
 * @author Donald D. Anderson
 */
public class DbEnv
{
    // methods
    //

    //
    // After using this constructor, set any parameters via
    // the set_* access methods below, and finally open
    // the environment by calling open().
    //
    public DbEnv(int flags) throws DbException
    {
        constructor_flags_ = flags;
        _init(errstream_, constructor_flags_);
    }

    //
    // This constructor is purposely not public.
    // It is used internally to create a DbEnv wrapper
    // when an underlying environment already exists.
    //
    /*package*/ DbEnv(Db db)
    {
        _init_using_db(errstream_, db);
    }

    //
    // When a Db is created, it is kept in a private list,
    // so that Db's can be notified when the environment
    // is closed.  This allows us to detect and guard
    // against the following situation:
    //    DbEnv env = new DbEnv(0);
    //    Db db = new Db(0);
    //    env.close();
    //    db.close();
    //
    // This *is* a programming error, but not protecting
    // against it will crash the VM.
    //
    /*package*/ void _add_db(Db db)
    {
        dblist_.addElement(db);
    }

    //
    // Remove from the private list of Db's.
    //
    /*package*/ void _remove_db(Db db)
    {
        dblist_.removeElement(db);
    }

    //
    // Iterate all the Db's in the list, and
    // notify them that the environment is closing,
    // so they can clean up.
    //
    /*package*/ void _notify_dbs()
    {
        Enumeration enum = dblist_.elements();
        while (enum.hasMoreElements()) {
            Db db = (Db)enum.nextElement();
            db._notify_dbenv_close();
        }
        dblist_.removeAllElements();
    }

    // close discards any internal memory.
    // After using close, the DbEnv can be reopened.
    //
    public synchronized void close(int flags)
        throws DbException
    {
        _notify_dbs();
        _close(flags);
    }

    // (Internal)
    private native void _close(int flags)
        throws DbException;

    public native void dbremove(DbTxn txn, String name, String subdb,
                                int flags)
        throws DbException;

    public native void dbrename(DbTxn txn, String name, String subdb,
                                String newname, int flags)
        throws DbException;

    public native void err(int errcode, String message);

    public native void errx(String message);

    // overrides Object.finalize
    protected void finalize()
        throws Throwable
    {
        _notify_dbs();
        _finalize(errcall_, errpfx_);
    }

    // (Internal)
    protected native void _finalize(DbErrcall errcall, String errpfx)
        throws Throwable;

    // (Internal)
    // called during constructor
    private native void _init(DbErrcall errcall, int flags) throws DbException;

    // (Internal)
    // called when DbEnv is constructed as part of Db constructor.
    private native void _init_using_db(DbErrcall errcall, Db db);

    /*package*/ native void _notify_db_close();

    public native void open(String db_home, int flags, int mode)
         throws DbException, FileNotFoundException;

    // remove removes any files and discards any internal memory.
    // (i.e. implicitly it does a close, if the environment is open).
    // After using close, the DbEnv can no longer be used;
    // create another one if needed.
    //
    public native synchronized void remove(String db_home, int flags)
         throws DbException, FileNotFoundException;

    ////////////////////////////////////////////////////////////////
    // simple get/set access methods
    //
    // If you are calling set_ methods, you need to
    // use the constructor with one argument along with open().

    public native void set_cachesize(int gbytes, int bytes, int ncaches)
         throws DbException;

    // Encryption
    public native void set_encrypt(String passwd, /*u_int32_t*/ int flags)
        throws DbException;

    // Error message callback.
    public void set_errcall(DbErrcall errcall)
    {
        errcall_ = errcall;
        _set_errcall(errcall);
    }

    public native void _set_errcall(DbErrcall errcall);

    // Error stream.
    public void set_error_stream(OutputStream s)
    {
        DbOutputStreamErrcall errcall = new DbOutputStreamErrcall(s);
        set_errcall(errcall);
    }

    // Error message prefix.
    public void set_errpfx(String errpfx)
    {
        errpfx_ = errpfx;
        _set_errpfx(errpfx);
    }

    private native void _set_errpfx(String errpfx);

    // Feedback
    public void set_feedback(DbEnvFeedback feedback)
         throws DbException
    {
        feedback_ = feedback;
        feedback_changed(feedback);
    }

    // (Internal)
    private native void feedback_changed(DbEnvFeedback feedback)
         throws DbException;

    // Generate debugging messages.
    public native void set_verbose(int which, boolean onoff)
         throws DbException;

    public native void set_data_dir(String data_dir)
         throws DbException;

    // Log buffer size.
    public native void set_lg_bsize(/*u_int32_t*/ int lg_bsize)
         throws DbException;

    // Log directory.
    public native void set_lg_dir(String lg_dir)
         throws DbException;

    // Maximum log file size.
    public native void set_lg_max(/*u_int32_t*/ int lg_max)
         throws DbException;

    // Log region size.
    public native void set_lg_regionmax(/*u_int32_t*/ int lg_regionmax)
         throws DbException;

    // Two dimensional conflict matrix.
    public native void set_lk_conflicts(byte[][] lk_conflicts)
         throws DbException;

    // Deadlock detect on every conflict.
    public native void set_lk_detect(/*u_int32_t*/ int lk_detect)
         throws DbException;

    /**
     * @deprecated DB 3.2.6, see the online documentation.
     */
    // Maximum number of locks.
    public native void set_lk_max(/*unsigned*/ int lk_max)
         throws DbException;

    // Maximum number of lockers.
    public native void set_lk_max_lockers(/*unsigned*/ int lk_max_lockers)
         throws DbException;

    // Maximum number of locks.
    public native void set_lk_max_locks(/*unsigned*/ int lk_max_locks)
         throws DbException;

    // Maximum number of locked objects.
    public native void set_lk_max_objects(/*unsigned*/ int lk_max_objects)
         throws DbException;

    // Maximum file size for mmap.
    public native void set_mp_mmapsize(/*size_t*/ long mmapsize)
         throws DbException;

    public native void set_flags(int flags, boolean onoff)
         throws DbException;

    public native void set_rep_limit(int gbytes, int bytes) throws DbException;

    public void set_rep_transport(int envid, DbRepTransport transport)
         throws DbException
    {
         rep_transport_ = transport;
         rep_transport_changed(envid, transport);
    }

    // (Internal)
    private native void rep_transport_changed(int envid,
				              DbRepTransport transport)
         throws DbException;

    public native void set_rpc_server(DbClient client, String host,
                                      long cl_timeout, long sv_timeout,
                                      int flags)
         throws DbException;

    public native void set_shm_key(long shm_key)
         throws DbException;

    public native void set_tas_spins(int tas_spins)
         throws DbException;

    public native void set_timeout(/*db_timeout_t*/ long timeout,
                                   /*u_int32_t*/ int flags)
        throws DbException;

    public native void set_tmp_dir(String tmp_dir)
         throws DbException;

    // Feedback
    public void set_app_dispatch(DbAppDispatch app_dispatch)
         throws DbException
    {
        app_dispatch_ = app_dispatch;
        app_dispatch_changed(app_dispatch);
    }

    // (Internal)
    private native void app_dispatch_changed(DbAppDispatch app_dispatch)
         throws DbException;

    // Maximum number of transactions.
    public native void set_tx_max(/*unsigned*/ int tx_max)
         throws DbException;

    // Note: only the seconds (not milliseconds) of the timestamp
    // are used in this API.
    public void set_tx_timestamp(Date timestamp)
         throws DbException
    {
        _set_tx_timestamp(timestamp.getTime()/1000);
    }

    // (Internal)
    private native void _set_tx_timestamp(long seconds)
         throws DbException;

    // Versioning information
    public native static int get_version_major();
    public native static int get_version_minor();
    public native static int get_version_patch();
    public native static String get_version_string();

    // Convert DB error codes to strings
    public native static String strerror(int errcode);

    public native int lock_detect(int flags, int atype)
         throws DbException;

    public native DbLock lock_get(/*u_int32_t*/ int locker,
                                  int flags,
                                  Dbt obj,
                                  /*db_lockmode_t*/ int lock_mode)
         throws DbException;

    public native void lock_put(DbLock lock)
         throws DbException;

    public native /*u_int32_t*/ int lock_id()
         throws DbException;

    public native void lock_id_free(/*u_int32_t*/ int id)
         throws DbException;

    public native DbLockStat lock_stat(/*u_int32_t*/ int flags)
         throws DbException;

    public native void lock_vec(/*u_int32_t*/ int locker,
                                int flags,
                                DbLockRequest[] list,
                                int offset,
                                int count)
        throws DbException;

    public native String[] log_archive(int flags)
         throws DbException;

    public native static int log_compare(DbLsn lsn0, DbLsn lsn1);

    public native DbLogc log_cursor(int flags)
         throws DbException;

    public native String log_file(DbLsn lsn)
         throws DbException;

    public native void log_flush(DbLsn lsn)
         throws DbException;

    public native void log_put(DbLsn lsn, Dbt data, int flags)
         throws DbException;

    public native DbLogStat log_stat(/*u_int32_t*/ int flags)
         throws DbException;

    public native DbMpoolStat memp_stat(/*u_int32_t*/ int flags)
         throws DbException;

    public native DbMpoolFStat[] memp_fstat(/*u_int32_t*/ int flags)
         throws DbException;

    public native int memp_trickle(int pct)
         throws DbException;

    public native int rep_elect(int nsites, int pri, int timeout)
         throws DbException;

    public static class RepProcessMessage {
        public int envid;
    }
    public native int rep_process_message(Dbt control, Dbt rec,
            RepProcessMessage result)
         throws DbException;

    public native void rep_start(Dbt cookie, int flags)
         throws DbException;

    public native DbRepStat rep_stat(/*u_int32_t*/ int flags)
         throws DbException;

    public native DbTxn txn_begin(DbTxn pid, int flags)
         throws DbException;

    public native void txn_checkpoint(int kbyte, int min, int flags)
         throws DbException;

    public native DbPreplist[] txn_recover(int count, int flags)
        throws DbException;

    public native DbTxnStat txn_stat(/*u_int32_t*/ int flags)
         throws DbException;

    ////////////////////////////////////////////////////////////////
    //
    // private data
    //
    private long private_dbobj_ = 0;
    private long private_info_ = 0;
    private int constructor_flags_ = 0;
    private Vector dblist_ = new Vector();    // Db's that are open
    private DbEnvFeedback feedback_ = null;
    private DbRepTransport rep_transport_ = null;
    private DbAppDispatch app_dispatch_ = null;
    private DbOutputStreamErrcall errstream_ =
        new DbOutputStreamErrcall(System.err);
    /*package*/ DbErrcall errcall_ = errstream_;
    /*package*/ String errpfx_;

    static {
        Db.load_db();
    }
}

// end of DbEnv.java
