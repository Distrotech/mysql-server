/*
   Copyright (C) 2009 Sun Microsystems Inc.
   All rights reserved. Use is subject to license terms.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "mysql_priv.h"
#ifdef WITH_NDBCLUSTER_STORAGE_ENGINE
#include "ha_ndbinfo.h"
#include "../storage/ndb/src/ndbapi/NdbInfo.hpp"


static MYSQL_THDVAR_UINT(
  max_rows,                          /* name */
  PLUGIN_VAR_RQCMDARG,
  "Specify max number of rows to fetch per roundtrip to cluster",
  NULL,                              /* check func. */
  NULL,                              /* update func. */
  10,                                /* default */
  1,                                 /* min */
  256,                               /* max */
  0                                  /* block */
);

static MYSQL_THDVAR_UINT(
  max_bytes,                         /* name */
  PLUGIN_VAR_RQCMDARG,
  "Specify approx. max number of bytes to fetch per roundtrip to cluster",
  NULL,                              /* check func. */
  NULL,                              /* update func. */
  0,                                 /* default */
  0,                                 /* min */
  65535,                             /* max */
  0                                  /* block */
);

static MYSQL_THDVAR_BOOL(
  show_hidden,                       /* name */
  PLUGIN_VAR_RQCMDARG,
  "Control if tables should be visible or not",
  NULL,                              /* check func. */
  NULL,                              /* update func. */
  FALSE                              /* default */
);

static char* ndbinfo_dbname = (char*)"ndbinfo";
static MYSQL_SYSVAR_STR(
  database,                         /* name */
  ndbinfo_dbname,                   /* var */
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Name of the database used by ndbinfo",
  NULL,                             /* check func. */
  NULL,                             /* update func. */
  NULL                              /* default */
);

static char* table_prefix = (char*)"ndb$";
static MYSQL_SYSVAR_STR(
  table_prefix,                     /* name */
  table_prefix,                     /* var */
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Prefix to use for all virtual tables loaded from NDB",
  NULL,                             /* check func. */
  NULL,                             /* update func. */
  NULL                              /* default */
);



static NdbInfo* g_ndbinfo;

extern Ndb_cluster_connection* g_ndb_cluster_connection;

static bool
ndbcluster_is_disabled(void)
{
  /*
    ndbinfo uses the same connection as ndbcluster
    to avoid using up another nodeid, this also means that
    if ndbcluster is not enabled, ndbinfo won't start
  */
  if (g_ndb_cluster_connection)
    return false;
  assert(g_ndbinfo == NULL);
  return true;
}

static handler*
create_handler(handlerton *hton, TABLE_SHARE *table, MEM_ROOT *mem_root)
{
  return new (mem_root) ha_ndbinfo(hton, table);
}

struct ha_ndbinfo_impl
{
  const NdbInfo::Table* m_table;
  NdbInfoScanOperation* m_scan_op;
  Vector<const NdbInfoRecAttr *> m_columns;

  ha_ndbinfo_impl() :
    m_table(NULL),
    m_scan_op(NULL)
  {
  }
};

ha_ndbinfo::ha_ndbinfo(handlerton *hton, TABLE_SHARE *table_arg)
: handler(hton, table_arg), m_impl(*new ha_ndbinfo_impl)
{
}

ha_ndbinfo::~ha_ndbinfo()
{
  delete &m_impl;
}

static int err2mysql(int error)
{
  DBUG_ENTER("err2mysql");
  DBUG_PRINT("enter", ("error: %d", error));
  assert(error != 0);
  switch(error)
  {
  case NdbInfo::ERR_ClusterFailure:
    DBUG_RETURN(HA_ERR_NO_CONNECTION);
    break;
  case NdbInfo::ERR_OutOfMemory:
    DBUG_RETURN(HA_ERR_OUT_OF_MEM);
    break;
  default:
    break;
  }
  push_warning_printf(current_thd, MYSQL_ERROR::WARN_LEVEL_ERROR,
                      ER_GET_ERRNO, ER(ER_GET_ERRNO), error);
  DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
}

int ha_ndbinfo::create(const char *name, TABLE *form,
                       HA_CREATE_INFO *create_info)
{
  DBUG_ENTER("ha_ndbinfo::create");
  DBUG_PRINT("enter", ("name: %s", name));

  DBUG_RETURN(0);
}

bool ha_ndbinfo::is_open(void) const
{
  return m_impl.m_table != NULL;
}

int ha_ndbinfo::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_ndbinfo::open");
  DBUG_PRINT("enter", ("name: %s, mode: %d", name, mode));

  assert(is_closed());

  if (mode == O_RDWR)
  {
    if (table->db_stat & HA_TRY_READ_ONLY)
    {
      DBUG_PRINT("info", ("Telling server to use readonly mode"));
      DBUG_RETURN(EROFS); // Read only fs
    }
    // Find any commands that does not allow open readonly
    DBUG_ASSERT(false);
  }

  if (ndbcluster_is_disabled())
  {
    // Allow table to be opened with ndbcluster disabled
    DBUG_RETURN(0);
  }

  /* Increase "ref_length" to allow a whole row to be stored in "ref" */
  ref_length = 0;
  for (uint i = 0; i < table->s->fields; i++)
    ref_length += table->field[i]->pack_length();
  DBUG_PRINT("info", ("ref_length: %u", ref_length));

  int err = g_ndbinfo->openTable(name, &m_impl.m_table);
  if (err)
  {
    if (err == NdbInfo::ERR_NoSuchTable)
      DBUG_RETURN(HA_ERR_NO_SUCH_TABLE);
    DBUG_RETURN(err2mysql(err));
  }

  DBUG_RETURN(0);
}

int ha_ndbinfo::close(void)
{
  DBUG_ENTER("ha_ndbinfo::close");

  if (ndbcluster_is_disabled())
    DBUG_RETURN(0);

  assert(is_open());
  if (m_impl.m_table)
  {
    g_ndbinfo->closeTable(m_impl.m_table);
    m_impl.m_table = NULL;
  }
  DBUG_RETURN(0);
}

int ha_ndbinfo::rnd_init(bool scan)
{
  DBUG_ENTER("ha_ndbinfo::rnd_init");
  DBUG_PRINT("info", ("scan: %d", scan));

  if (ndbcluster_is_disabled())
  {
    push_warning(current_thd, MYSQL_ERROR::WARN_LEVEL_NOTE, 1,
                 "'NDBINFO' has been started "
                 "in limited mode since the 'NDBCLUSTER' "
                 "engine is disabled - no rows can be returned");
    DBUG_RETURN(0);
  }

  assert(is_open());
  assert(m_impl.m_scan_op == NULL); // No scan already ongoing

  if (!scan)
  {
    // Just an init to read using 'rnd_pos'
    DBUG_PRINT("info", ("not scan"));
    DBUG_RETURN(0);
  }

  THD* thd = current_thd;
  int err;
  NdbInfoScanOperation* scan_op = NULL;
  if ((err = g_ndbinfo->createScanOperation(m_impl.m_table,
                                            &scan_op,
                                            THDVAR(thd, max_rows),
                                            THDVAR(thd, max_bytes))) != 0)
    DBUG_RETURN(err2mysql(err));

  if ((err = scan_op->readTuples()) != 0)
    DBUG_RETURN(err2mysql(err));

  /* Read all columns specified in read_set */
  for (uint i = 0; i < table->s->fields; i++)
  {
    Field *field = table->field[i];
    if (bitmap_is_set(table->read_set, i))
      m_impl.m_columns.push_back(scan_op->getValue(field->field_name));
    else
      m_impl.m_columns.push_back(NULL);
  }

  if ((err = scan_op->execute()) != 0)
    DBUG_RETURN(err2mysql(err));

  m_impl.m_scan_op = scan_op;
  DBUG_RETURN(0);
}

int ha_ndbinfo::rnd_end()
{
  DBUG_ENTER("ha_ndbinfo::rnd_end");

  if (ndbcluster_is_disabled())
    DBUG_RETURN(0);

  assert(is_open());

  if (m_impl.m_scan_op)
  {
    g_ndbinfo->releaseScanOperation(m_impl.m_scan_op);
    m_impl.m_scan_op = NULL;
  }
  m_impl.m_columns.clear();

  DBUG_RETURN(0);
}

int ha_ndbinfo::rnd_next(uchar *buf)
{
  int err;
  DBUG_ENTER("ha_ndbinfo::rnd_next");

  if (ndbcluster_is_disabled())
    DBUG_RETURN(HA_ERR_END_OF_FILE);

  assert(is_open());
  assert(m_impl.m_scan_op);

  if ((err = m_impl.m_scan_op->nextResult()) == 0)
    DBUG_RETURN(HA_ERR_END_OF_FILE);

  if (err != 1)
    DBUG_RETURN(err2mysql(err));

  unpack_record(buf);

  DBUG_RETURN(0);
}

int ha_ndbinfo::rnd_pos(uchar *buf, uchar *pos)
{
  DBUG_ENTER("ha_ndbinfo::rnd_pos");
  assert(is_open());
  assert(m_impl.m_scan_op == NULL); // No scan started

  /* Copy the saved row into "buf" and set all fields to not null */
  memcpy(buf, pos, ref_length);
  for (uint i = 0; i < table->s->fields; i++)
    table->field[i]->set_notnull();

  DBUG_RETURN(0);
}

void ha_ndbinfo::position(const uchar *record)
{
  DBUG_ENTER("ha_ndbinfo::position");
  assert(is_open());
  assert(m_impl.m_scan_op);

  /* Save away the whole row in "ref" */
  memcpy(ref, record, ref_length);

  DBUG_VOID_RETURN;
}

int ha_ndbinfo::info(uint flag)
{
  DBUG_ENTER("ha_ndbinfo::info");
  DBUG_PRINT("enter", ("flag: %d", flag));
  DBUG_RETURN(0);
}

void
ha_ndbinfo::unpack_record(uchar *dst_row)
{
  DBUG_ENTER("ha_ndbinfo::unpack_record");
  my_ptrdiff_t dst_offset = dst_row - table->record[0];

  for (uint i = 0; i < table->s->fields; i++)
  {
    Field *field = table->field[i];
    const NdbInfoRecAttr* record = m_impl.m_columns[i];
    if (m_impl.m_columns[i])
    {
      field->set_notnull();
      field->move_field_offset(dst_offset);
      switch (field->type()) {

      case (MYSQL_TYPE_VARCHAR):
      {
        DBUG_PRINT("info", ("str: %s", record->c_str()));
        Field_varstring* vfield = (Field_varstring *) field;
        /* Field_bit in DBUG requires the bit set in write_set for store(). */
        my_bitmap_map *old_map =
          dbug_tmp_use_all_columns(table, table->write_set);
        (void)vfield->store(record->c_str(),
                            min(record->length(), field->field_length)-1,
                            field->charset());
        dbug_tmp_restore_column_map(table->write_set, old_map);
        break;
      }

      case (MYSQL_TYPE_LONG):
      {
        memcpy(field->ptr, record->ptr(), sizeof(Uint32));
        break;
      }

      case (MYSQL_TYPE_LONGLONG):
      {
        memcpy(field->ptr, record->ptr(), sizeof(Uint64));
        break;
      }

      default:
        sql_print_error("Found unexpected field type %u", field->type());
        break;
      }

      field->move_field_offset(-dst_offset);
    }
    else
    {
      field->set_null();
    }
  }
  DBUG_VOID_RETURN;
}


static int
ndbinfo_find_files(handlerton *hton, THD *thd,
                   const char *db, const char *path,
                   const char *wild, bool dir, List<LEX_STRING> *files)
{
  DBUG_ENTER("ndbinfo_find_files");
  DBUG_PRINT("enter", ("db: '%s', dir: %d", db, dir));

  const bool show_hidden = THDVAR(thd, show_hidden);

  if(show_hidden)
    DBUG_RETURN(0); // Don't filter out anything

  if (dir)
    DBUG_RETURN(0); // Don't care about filtering databases

  DBUG_ASSERT(db);
  if (strcmp(db, ndbinfo_dbname))
    DBUG_RETURN(0); // Only hide files in "our" db

  /* Hide all files that start with "our" prefix */
  LEX_STRING *file_name;
  List_iterator<LEX_STRING> it(*files);
  while ((file_name=it++))
  {
    if (is_prefix(file_name->str, table_prefix))
    {
      DBUG_PRINT("info", ("Hiding '%s'", file_name->str));
      it.remove();
    }
  }

  DBUG_RETURN(0);
}


handlerton* ndbinfo_hton;

int ndbinfo_init(void *plugin)
{
  DBUG_ENTER("ndbinfo_init");

  handlerton *hton = (handlerton *) plugin;
  hton->create = create_handler;
  hton->flags = HTON_TEMPORARY_NOT_SUPPORTED;
  hton->find_files = ndbinfo_find_files;

  ndbinfo_hton = hton;

  if (ndbcluster_is_disabled())
  {
    // Starting in limited mode since ndbcluster is disabled
     DBUG_RETURN(0);
  }

  char prefix[FN_REFLEN];
  build_table_filename(prefix, sizeof(prefix) - 1,
                       ndbinfo_dbname, table_prefix, "", 0);
  DBUG_PRINT("info", ("prefix: '%s'", prefix));
  assert(g_ndb_cluster_connection);
  g_ndbinfo = new NdbInfo(g_ndb_cluster_connection, prefix,
                          ndbinfo_dbname, table_prefix);
  if (!g_ndbinfo)
  {
    sql_print_error("Failed to create NdbInfo");
    DBUG_RETURN(1);
  }

  if (!g_ndbinfo->init())
  {
    sql_print_error("Failed to init NdbInfo");

    delete g_ndbinfo;
    g_ndbinfo = NULL;

    DBUG_RETURN(1);
  }

  DBUG_RETURN(0);
}

int ndbinfo_deinit(void *plugin)
{
  DBUG_ENTER("ndbinfo_deinit");

  if (g_ndbinfo)
  {
    delete g_ndbinfo;
    g_ndbinfo = NULL;
  }

  DBUG_RETURN(0);
}

struct st_mysql_sys_var* ndbinfo_system_variables[]= {
  MYSQL_SYSVAR(max_rows),
  MYSQL_SYSVAR(max_bytes),
  MYSQL_SYSVAR(show_hidden),
  MYSQL_SYSVAR(database),
  MYSQL_SYSVAR(table_prefix),

  NULL
};

template class Vector<const NdbInfoRecAttr*>;

#endif
