/* Copyright (C) 2003 MySQL AB

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/*
  Please read ha_exmple.cc before reading this file.
  Please keep in mind that the federated storage engine implements all methods
  that are required to be implemented. handler.h has a full list of methods
  that you can implement.
*/

#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

#include <mysql.h>

#define FEDERATED_QUERY_BUFFER_SIZE STRING_BUFFER_USUAL_SIZE * 5
#define FEDERATED_RECORDS_IN_RANGE 2

#define FEDERATED_INFO " SHOW TABLE STATUS LIKE "
#define FEDERATED_SELECT "SELECT "
#define FEDERATED_WHERE " WHERE "
#define FEDERATED_FROM " FROM "
#define FEDERATED_PERCENT "%"
#define FEDERATED_IS " IS "
#define FEDERATED_NULL " NULL "
#define FEDERATED_ISNULL " IS NULL "
#define FEDERATED_LIKE " LIKE "
#define FEDERATED_TRUNCATE "TRUNCATE "
#define FEDERATED_DELETE "DELETE "
#define FEDERATED_INSERT "INSERT INTO "
#define FEDERATED_LIMIT1 " LIMIT 1"
#define FEDERATED_VALUES "VALUES "
#define FEDERATED_UPDATE "UPDATE "
#define FEDERATED_SET "SET "
#define FEDERATED_AND " AND "
#define FEDERATED_CONJUNCTION ") AND ("
#define FEDERATED_OR " OR "
#define FEDERATED_NOT " NOT "
#define FEDERATED_STAR "* "
#define FEDERATED_SPACE " "
#define FEDERATED_SQUOTE "'"
#define FEDERATED_COMMA ", "
#define FEDERATED_DQOUTE '"' 
#define FEDERATED_BTICK  "`" 
#define FEDERATED_OPENPAREN " ("
#define FEDERATED_CLOSEPAREN ") "
#define FEDERATED_NE " != "
#define FEDERATED_GT " > "
#define FEDERATED_LT " < "
#define FEDERATED_LE " <= "
#define FEDERATED_GE " >= "
#define FEDERATED_EQ " = "
#define FEDERATED_1EQ0   " 1=0"

/*
  FEDERATED_SHARE is a structure that will be shared amoung all open handlers
  The example implements the minimum of what you will probably need.
*/
typedef struct st_federated_share {
  /*
    the primary select query to be used in rnd_init
  */
  char *select_query;
  /*
    remote host info, parse_url supplies
  */
  char *scheme;
  char *hostname;
  char *username;
  char *password;
  char *database;
  char *table_name;
  char *table;
  char *socket;
  char *sport;
  ushort port;
  uint table_name_length,use_count;
  pthread_mutex_t mutex;
  THR_LOCK lock;
} FEDERATED_SHARE;

/*
  Class definition for the storage engine
*/
class ha_federated: public handler
{
  THR_LOCK_DATA lock;      /* MySQL lock */
  FEDERATED_SHARE *share;    /* Shared lock info */
  MYSQL *mysql; /* MySQL connection */
  MYSQL_RES *stored_result;
  bool scan_flag;
  uint ref_length;
  uint fetch_num; // stores the fetch num
  MYSQL_ROW_OFFSET current_position;  // Current position used by ::position()

private:
  /*
      return 0 on success
      return errorcode otherwise
  */
  uint convert_row_to_internal_format(byte *buf, MYSQL_ROW row);
  bool create_where_from_key(String *to, KEY *key_info, 
                             const key_range *start_key,
                             const key_range *end_key,
                             bool records_in_range);

public:
  ha_federated(TABLE *table): handler(table),
    mysql(0), stored_result(0), scan_flag(0),
    ref_length(sizeof(MYSQL_ROW_OFFSET)), current_position(0)
  {
  }
  ~ha_federated()
  {
  }
  /* The name that will be used for display purposes */
  const char *table_type() const { return "FEDERATED"; }
  /*
    The name of the index type that will be used for display
    don't implement this method unless you really have indexes
   */
  // perhaps get index type
  const char *index_type(uint inx) { return "REMOTE"; }
  const char **bas_ext() const;
  /*
    This is a list of flags that says what the storage engine
    implements. The current table flags are documented in
    handler.h
  */
  ulong table_flags() const
  {
    /* fix server to be able to get remote server table flags */
    return (HA_NOT_EXACT_COUNT |
            HA_PRIMARY_KEY_IN_READ_INDEX | HA_FILE_BASED | HA_REC_NOT_IN_SEQ |
            HA_AUTO_PART_KEY | HA_CAN_INDEX_BLOBS| HA_NO_PREFIX_CHAR_KEYS);
  }
  /*
    This is a bitmap of flags that says how the storage engine
    implements indexes. The current index flags are documented in
    handler.h. If you do not implement indexes, just return zero
    here.

    part is the key part to check. First key part is 0
    If all_parts it's set, MySQL want to know the flags for the combined
    index up to and including 'part'.
  */
    /* fix server to be able to get remote server index flags */
  ulong index_flags(uint inx, uint part, bool all_parts) const
  {
    return (HA_READ_NEXT | HA_READ_RANGE | HA_READ_AFTER_KEY);
  }
  uint max_supported_record_length() const { return HA_MAX_REC_LENGTH; }
  uint max_supported_keys()          const { return MAX_KEY; }
  uint max_supported_key_parts()     const { return MAX_REF_PARTS; }
  uint max_supported_key_length()    const { return MAX_KEY_LENGTH; }
  /*
    Called in test_quick_select to determine if indexes should be used.
    Normally, we need to know number of blocks . For federated we need to
    know number of blocks on remote side, and number of packets and blocks
    on the network side (?)
    Talk to Kostja about this - how to get the
    number of rows * ...
    disk scan time on other side (block size, size of the row) + network time ...
    The reason for "records * 1000" is that such a large number forces 
    this to use indexes "
  */
  double scan_time()
  {
    DBUG_PRINT("info",
               ("records %d", records));
    return (double)(records*1000); 
  }
  /*
    The next method will never be called if you do not implement indexes.
  */
  double read_time(uint index, uint ranges, ha_rows rows) 
  {
    /*
      Per Brian, this number is bugus, but this method must be implemented,
      and at a later date, he intends to document this issue for handler code
    */
    return (double) rows /  20.0+1;
  }

  const key_map *keys_to_use_for_scanning() { return &key_map_full; }
  /*
    Everything below are methods that we implment in ha_federated.cc.

    Most of these methods are not obligatory, skip them and
    MySQL will treat them as not implemented
  */
  int open(const char *name, int mode, uint test_if_locked);    // required
  int close(void);                                              // required

  int write_row(byte *buf);
  int update_row(const byte *old_data, byte *new_data);
  int delete_row(const byte *buf);
  int index_init(uint keynr);
  int index_read(byte *buf, const byte *key,
                 uint key_len, enum ha_rkey_function find_flag);
  int index_read_idx(byte *buf, uint idx, const byte *key,
                     uint key_len, enum ha_rkey_function find_flag);
  int index_next(byte *buf);
  int index_end();
  int read_range_first(const key_range *start_key,
                               const key_range *end_key,
                               bool eq_range, bool sorted);
  int read_range_next();
  /*
    unlike index_init(), rnd_init() can be called two times
    without rnd_end() in between (it only makes sense if scan=1).
    then the second call should prepare for the new table scan
    (e.g if rnd_init allocates the cursor, second call should
    position it to the start of the table, no need to deallocate
    and allocate it again
  */
  int rnd_init(bool scan);                                      //required
  int rnd_end();
  int rnd_next(byte *buf);                                      //required
  int rnd_pos(byte *buf, byte *pos);                            //required
  void position(const byte *record);                            //required
  void info(uint);                                              //required

  int delete_all_rows(void);
  int create(const char *name, TABLE *form,
             HA_CREATE_INFO *create_info);                      //required
  ha_rows records_in_range(uint inx, key_range *start_key,
                                   key_range *end_key);

  THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
                             enum thr_lock_type lock_type);     //required
};

bool federated_db_init(void);
bool federated_db_end(void);
