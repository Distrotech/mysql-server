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

#include <ndb_global.h>
#include <ndb_opts.h>
#include <util/BaseString.hpp>
#include "../src/kernel/vm/NdbinfoTables.cpp"

static char* opt_ndbinfo_db = (char*)"ndbinfo";
static char* opt_table_prefix = (char*)"ndb$";

static struct my_option
my_long_options[] =
{
  { "database", 'd',
    "Name of the database used by ndbinfo",
    (uchar**) &opt_ndbinfo_db, (uchar**) &opt_ndbinfo_db, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { "database", 'd',
    "Prefix to use for all virtual tables loaded from NDB",
    (uchar**) &opt_table_prefix, (uchar**) &opt_table_prefix, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};


struct view {
  const char* name;
  const char* sql;
} views[] =
{
  { "pools",
    "SELECT node_id, b.block_name, block_instance, pool_name, "
    "used, total, high, entry_size, cp1.param_name AS param_name1, "
    "cp2.param_name AS param_name2, cp3.param_name AS param_name3, "
    "cp4.param_name AS param_name4 "
    "FROM <NDBINFO_DB>.<TABLE_PREFIX>pools p "
    "JOIN <NDBINFO_DB>.blocks b ON p.block_number = b.block_number "
    "LEFT JOIN <NDBINFO_DB>.config_params cp1 ON p.config_param1 = cp1.param_number "
    "LEFT JOIN <NDBINFO_DB>.config_params cp2 ON p.config_param2 = cp2.param_number "
    "LEFT JOIN <NDBINFO_DB>.config_params cp3 ON p.config_param3 = cp3.param_number "
    "LEFT JOIN <NDBINFO_DB>.config_params cp4 ON p.config_param4 = cp4.param_number"
  },
  { "transporters",
    "SELECT node_id, remote_node_id, "
    " CASE connection_status"
    "  WHEN 0 THEN \"CONNECTED\""
    "  WHEN 1 THEN \"CONNECTING\""
    "  WHEN 2 THEN \"DISCONNECTED\""
    "  WHEN 3 THEN \"DISCONNECTING\""
    "  ELSE NULL "
    " END AS status "
    "FROM <NDBINFO_DB>.<TABLE_PREFIX>transporters"
  },
  { "logspaces",
    "SELECT node_id, "
    " CASE log_type"
    "  WHEN 0 THEN \"REDO\""
    "  WHEN 1 THEN \"DD-UNDO\""
    "  ELSE NULL "
    " END AS log_type, "
    "log_id, log_part, total, used "
    "FROM <NDBINFO_DB>.<TABLE_PREFIX>logspaces"
  },
  { "logbuffers",
    "SELECT node_id, "
    " CASE log_type"
    "  WHEN 0 THEN \"REDO\""
    "  WHEN 1 THEN \"DD-UNDO\""
    "  ELSE \"<unknown>\" "
    " END AS log_type, "
    "log_id, log_part, total, used "
    "FROM <NDBINFO_DB>.<TABLE_PREFIX>logbuffers"
  },
  { "resources",
    "SELECT node_id, "
    " CASE resource_id"
    "  WHEN 0 THEN \"RESERVED\""
    "  WHEN 1 THEN \"DISK_OPERATIONS\""
    "  WHEN 2 THEN \"DISK_RECORDS\""
    "  WHEN 3 THEN \"DATA_MEMORY\""
    "  WHEN 4 THEN \"JOBBUFFER\""
    "  WHEN 5 THEN \"FILE_BUFFERS\""
    "  WHEN 6 THEN \"TRANSPORTER_BUFFERS\""
    "  ELSE \"<unknown>\" "
    " END AS resource_name, "
    "reserved, used, max "
    "FROM <NDBINFO_DB>.<TABLE_PREFIX>resources"
   },
   { "counters",
    "SELECT node_id, b.block_name, block_instance, "
    "counter_id, "
    "CASE counter_id"
    "  WHEN 1 THEN \"ATTRINFO\""
    "  WHEN 2 THEN \"TRANSACTIONS\""
    "  WHEN 3 THEN \"COMMITS\""
    "  WHEN 4 THEN \"READS\""
    "  WHEN 5 THEN \"SIMPLE_READS\""
    "  WHEN 6 THEN \"WRITES\""
    "  WHEN 7 THEN \"ABORTS\""
    "  WHEN 8 THEN \"TABLE_SCANS\""
    "  WHEN 9 THEN \"RANGE_SCANS\""
    "  WHEN 10 THEN \"OPERATIONS\""
    "  ELSE \"<unknown>\" "
    " END AS counter_name, "
    "val "
    "FROM <NDBINFO_DB>.<TABLE_PREFIX>counters c, "
    "<NDBINFO_DB>.blocks b "
    "WHERE c.block_number = b.block_number"
   },
   { "nodes",
    "SELECT node_id, "
    "uptime, "
    "CASE status"
    "  WHEN 0 THEN \"NOTHING\""
    "  WHEN 1 THEN \"CMVMI\""
    "  WHEN 2 THEN \"STARTING\""
    "  WHEN 3 THEN \"STARTED\""
    "  WHEN 4 THEN \"SINGLEUSER\""
    "  WHEN 5 THEN \"STOPPING_1\""
    "  WHEN 6 THEN \"STOPPING_2\""
    "  WHEN 7 THEN \"STOPPING_3\""
    "  WHEN 8 THEN \"STOPPING_4\""
    "  ELSE \"<unknown>\" "
    " END AS status, "
    "start_phase "
    "FROM <NDBINFO_DB>.<TABLE_PREFIX>nodes"
   },
  { "memoryusage",
    "SELECT node_id, \"DATA_MEMORY\", "
    "used, max "
    "FROM <NDBINFO_DB>.<TABLE_PREFIX>resources "
    "WHERE resource_id = 3 "
    "UNION "
    "SELECT node_id, \"INDEX_MEMORY\", "
    "used, total "
    "FROM <NDBINFO_DB>.<TABLE_PREFIX>pools "
    "WHERE block_number = 248 AND pool_name = \"Index memory\" "
  }
};

size_t num_views = sizeof(views)/sizeof(views[0]);


#include "../src/mgmsrv/ConfigInfo.cpp"
static ConfigInfo g_info;
static void fill_config_params(void)
{
  const char* separator = "";
  const ConfigInfo::ParamInfo* pinfo= NULL;
  ConfigInfo::ParamInfoIter param_iter(g_info,
                                       CFG_SECTION_NODE,
                                       NODE_TYPE_DB);
  while((pinfo= param_iter.next())) {
    if (pinfo->_paramId == 0 || // KEY_INTERNAL
        pinfo->_status != ConfigInfo::CI_USED)
      continue;
    printf("%s(%u, \"%s\")", separator, pinfo->_paramId, pinfo->_fname); 
    separator = ", ";
  }
}


#include "../src/common/debugger/BlockNames.cpp"
static void fill_blocks(void)
{
  const char* separator = "";
  for (BlockNumber i = 0; i < NO_OF_BLOCK_NAMES; i++)
  {
    const BlockName& bn = BlockNames[i];
    printf("%s(%u, \"%s\")", separator, bn.number, bn.name);
    separator = ", ";
  }
}

struct lookup {
  const char* name;
  const char* columns;
  void (*fill)(void);
} lookups[] =
{
  { "blocks",
    "block_number INT UNSIGNED, "
    "block_name VARCHAR(512)",
    &fill_blocks
   },
  { "config_params",
    "param_number INT UNSIGNED, "
    "param_name VARCHAR(512)",
    &fill_config_params
   }
};

size_t num_lookups = sizeof(lookups)/sizeof(lookups[0]);


struct replace {
  const char* tag;
  const char* string;
} replaces[] =
{
  {"<TABLE_PREFIX>", opt_table_prefix},
  {"<NDBINFO_DB>", opt_ndbinfo_db},
};

size_t num_replaces = sizeof(replaces)/sizeof(replaces[0]);


BaseString replace_tags(const char* str)
{
  BaseString result(str);
  for (size_t i = 0; i < num_replaces; i++)
  {
    Vector<BaseString> parts;
    const char* p = result.c_str();
    const char* tag = replaces[i].tag;

    /* Split on <tag> */
    const char* first;
    while((first = strstr(p, tag)))
    {
      BaseString part;
      part.assign(p, first - p);
      parts.push_back(part);
      p = first + strlen(tag);
    }
    parts.push_back(p);

    /* Put back together */
    BaseString res;
    const char* separator = "";
    for (unsigned j = 0; j < parts.size(); j++)
    {
      res.appfmt("%s%s", separator, parts[j].c_str());
      separator = replaces[i].string;
    }

    /* Save result from this loop */
    result = res;
  }
  return result;
}


int main(int argc, char** argv){

  if ((handle_options(&argc, &argv, my_long_options, NULL)))
    return 2;

  printf("#\n");
  printf("# SQL commands for creating the tables in MySQL Server which \n");
  printf("# are used by the NDBINFO storage engine to access system \n");
  printf("# information and statistics from MySQL Cluster\n");
  printf("#\n");

  printf("CREATE DATABASE IF NOT EXISTS `%s`;\n\n", opt_ndbinfo_db);

  printf("# Only create tables if NDBINFO is enabled\n");
  printf("SELECT @have_ndbinfo:= COUNT(*) FROM "
                  "information_schema.engines WHERE engine='NDBINFO' "
                  "AND support IN ('YES', 'DEFAULT');\n\n");

  printf("# drop any old views in %s\n", opt_ndbinfo_db);
  for (size_t i = 0; i < num_views; i++)
  {
    printf("DROP VIEW IF EXISTS %s.%s;\n",
            opt_ndbinfo_db, views[i].name);
  }
  printf("\n");

  printf("# drop any old lookup tables in %s\n", opt_ndbinfo_db);
  for (size_t i = 0; i < num_lookups; i++)
  {
    printf("DROP TABLE IF EXISTS %s.%s;\n",
            opt_ndbinfo_db, lookups[i].name);
  }
  printf("\n");

  for (int i = 0; i < Ndbinfo::getNumTables(); i++)
  {
    const Ndbinfo::Table& table = Ndbinfo::getTable(i);

    printf("# %s.%s%s\n",
            opt_ndbinfo_db, opt_table_prefix, table.m.name);

    /* Drop the table if it exists */
    printf("SET @str=IF(@have_ndbinfo,"
                    "'DROP TABLE IF EXISTS `%s`.`%s%s`',"
                    "'SET @dummy = 0');\n",
            opt_ndbinfo_db, opt_table_prefix, table.m.name);
    printf("PREPARE stmt FROM @str;\n");
    printf("EXECUTE stmt;\n");
    printf("DROP PREPARE stmt;\n\n");

    /* Create the table */
    BaseString sql;
    sql.assfmt("CREATE TABLE `%s`.`%s%s` (",
               opt_ndbinfo_db, opt_table_prefix, table.m.name);

    const char* separator = "";
    for(int j = 0; j < table.m.ncols ; j++)
    {
      const Ndbinfo::Column& col = table.col[j];

      sql.appfmt("%s", separator);
      separator = ",";

      sql.appfmt("`%s` ", col.name);

      switch(col.coltype)
      {
      case Ndbinfo::Number:
        sql.appfmt("INT UNSIGNED");
        break;
      case Ndbinfo:: Number64:
        sql.appfmt("BIGINT UNSIGNED");
        break;
      case Ndbinfo::String:
        sql.appfmt("VARCHAR(512)");
        break;
      default:
        fprintf(stderr, "unknown coltype: %d\n", col.coltype);
        abort();
        break;
      }

      if (col.comment[0] != '\0')
        sql.appfmt(" COMMENT \"%s\"", col.comment);

    }

    sql.appfmt(") COMMENT=\"%s\" ENGINE=NDBINFO;", table.m.comment);

    printf("SET @str=IF(@have_ndbinfo,'%s','SET @dummy = 0');\n", sql.c_str());
    printf("PREPARE stmt FROM @str;\n");
    printf("EXECUTE stmt;\n");
    printf("DROP PREPARE stmt;\n\n");

  }


  for (size_t i = 0; i < num_lookups; i++)
  {
    lookup l = lookups[i];
    printf("# %s.%s\n", opt_ndbinfo_db, l.name);

    /* Create lookup table */
    printf("CREATE TABLE `%s`.`%s` (%s);\n",
           opt_ndbinfo_db, l.name, l.columns);

    /* Insert data */
    printf("INSERT INTO `%s`.`%s` VALUES ",
           opt_ndbinfo_db, l.name);
    l.fill();
    printf(";\n");

  }
  printf("\n");

  printf("#\n");
  printf("# %s views\n", opt_ndbinfo_db);
  printf("#\n\n");

  for (size_t i = 0; i < num_views; i++)
  {
    view v = views[i];

    printf("# %s.%s\n", opt_ndbinfo_db, v.name);

    BaseString view_sql = replace_tags(v.sql);

    /* Create or replace the view */
    BaseString sql;
    sql.assfmt("CREATE OR REPLACE DEFINER=`root@localhost` "
               "SQL SECURITY INVOKER VIEW `%s`.`%s` AS %s",
               opt_ndbinfo_db, v.name, view_sql.c_str());

    printf("SET @str=IF(@have_ndbinfo,'%s','SET @dummy = 0');\n",
           sql.c_str());
    printf("PREPARE stmt FROM @str;\n");
    printf("EXECUTE stmt;\n");
    printf("DROP PREPARE stmt;\n\n");
  }

  return 0;
}

