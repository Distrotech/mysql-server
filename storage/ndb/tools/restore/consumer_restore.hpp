/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef CONSUMER_RESTORE_HPP
#define CONSUMER_RESTORE_HPP

#include "consumer.hpp"

bool map_nodegroups(Uint16 *ng_array, Uint32 no_parts);

struct restore_callback_t {
  class BackupRestore *restore;
  class TupleS tup;
  class NdbTransaction *connection;
  int    retries;
  int error_code;
  Uint32 fragId;
  Uint32 n_bytes;
  restore_callback_t *next;
};

struct char_n_padding_struct {
Uint32 n_old;
Uint32 n_new;
char new_row[1];
};

struct PromotionRules {
  NDBCOL::Type old_type;
  NDBCOL::Type new_type;
  AttrCheckCompatFunc  attr_check_compatability;
  AttrConvertFunc attr_convert;
};

class BackupRestore : public BackupConsumer 
{
public:
  BackupRestore(NODE_GROUP_MAP *ng_map,
                uint ng_map_len,
                Uint32 parallelism=1)
  {
    m_ndb = 0;
    m_cluster_connection = 0;
    m_nodegroup_map = ng_map;
    m_nodegroup_map_len = ng_map_len;
    m_n_tablespace = 0;
    m_n_logfilegroup = 0;
    m_n_datafile = 0;
    m_n_undofile = 0;
    m_n_tables = 0;
    m_logBytes = m_dataBytes = 0;
    m_logCount = m_dataCount = 0;
    m_restore = false;
    m_restore_meta = false;
    m_no_restore_disk = false;
    m_restore_epoch = false;
    m_parallelism = parallelism;
    m_callback = 0;
    m_free_callback = 0;
    m_temp_error = false;
    m_no_upgrade = false;
    m_promote_attributes = false;
    m_reserve_tail_spaces = false;
    m_transactions = 0;
    m_cache.m_old_table = 0;
  }
  
  virtual ~BackupRestore();
  virtual bool init();
  virtual void release();
  virtual bool object(Uint32 type, const void* ptr);
  virtual bool table(const TableS &);
  virtual bool endOfTables();
  virtual void tuple(const TupleS &, Uint32 fragId);
  virtual void tuple_free();
  virtual void tuple_a(restore_callback_t *cb);
  virtual void cback(int result, restore_callback_t *cb);
  virtual bool errorHandler(restore_callback_t *cb);
  virtual void exitHandler();
  virtual void endOfTuples();
  virtual void logEntry(const LogEntry &);
  virtual void endOfLogEntrys();
  virtual bool finalize_table(const TableS &);
  virtual bool has_temp_error();
  virtual bool createSystable(const TableS & table);
  virtual bool table_equal(const TableS & table);
  virtual bool table_compatible_check(const TableS & tableS);
  virtual bool update_apply_status(const RestoreMetaData &metaData);
  virtual bool report_started(unsigned node_id, unsigned backup_id);
  virtual bool report_meta_data(unsigned node_id, unsigned backup_id);
  virtual bool report_data(unsigned node_id, unsigned backup_id);
  virtual bool report_log(unsigned node_id, unsigned backup_id);
  virtual bool report_completed(unsigned node_id, unsigned backup_id);
  void connectToMysql();
  bool map_in_frm(char *new_data, const char *data,
                  uint data_len, uint *new_data_len);
  bool search_replace(char *search_str, char **new_data,
                      const char **data, const char *end_data,
                      uint *new_data_len);
  bool map_nodegroups(Uint16 *ng_array, Uint32 no_parts);
  Uint32 map_ng(Uint32 ng);
  bool translate_frm(NdbDictionary::Table *table);

  static bool check_compat_common(const NDBCOL &old_col, 
                                  const NDBCOL &new_col); 
  static bool check_compat_alwaystrue(const NDBCOL &old_col,
                                      const NDBCOL &new_col);
  static void* convert_int8_int16(const void *old_data,    void *parameter);
  static void* convert_int8_int24(const void *old_data,    void *parameter);
  static void* convert_int8_int32(const void *old_data,    void *parameter);
  static void* convert_int8_int64(const void *old_data,    void *parameter);
  static void* convert_int16_int24(const void *old_data,   void *parameter);
  static void* convert_int16_int32(const void *old_data,   void *parameter);
  static void* convert_int16_int64(const void *old_data,   void *parameter);
  static void* convert_int24_int32(const void *old_data,   void *parameter);
  static void* convert_int24_int64(const void *old_data,   void *parameter);
  static void* convert_int32_int64(const void *old_data,   void *parameter);
  static void* convert_uint8_uint16(const void *old_data,  void *parameter);
  static void* convert_uint8_uint24(const void *old_data,  void *parameter);
  static void* convert_uint8_uint32(const void *old_data,  void *parameter);
  static void* convert_uint8_uint64(const void *old_data,  void *parameter);
  static void* convert_uint16_uint24(const void *old_data, void *parameter);
  static void* convert_uint16_uint32(const void *old_data, void *parameter);
  static void* convert_uint16_uint64(const void *old_data, void *parameter);
  static void* convert_uint24_uint32(const void *old_data, void *parameter);
  static void* convert_uint24_uint64(const void *old_data, void *parameter);
  static void* convert_uint32_uint64(const void *old_data, void *parameter);

  static void* convert_char_char(const void *old_data,            void *parameter);
  static void* convert_binary_binary(const void *old_data,        void *parameter);
  static void* convert_char_varchar(const void *old_data,         void *parameter);
  static void* convert_char_longvarchar(const void *old_data,     void *parameter);
  static void* convert_binary_varbinary(const void *old_data,     void *parameter);
  static void* convert_binary_longvarbinary(const void *old_data, void *parameter);
  static void* convert_bit_bit(const void *old_data,              void *parameter);
  static void* convert_var_var(const void *old_data,              void *parameter);
  static void* convert_var_longvar(const void *old_data,          void *parameter);
  static void* convert_longvar_longvar(const void *old_data,      void *parameter);


  AttrCheckCompatFunc 
    get_attr_check_compatability(const NDBCOL::Type &old_type,
                                 const NDBCOL::Type &new_type);
  AttrConvertFunc
  get_convert_func(const NDBCOL::Type &old_type,
                   const NDBCOL::Type &new_type);

  Ndb * m_ndb;
  Ndb_cluster_connection * m_cluster_connection;
  bool m_restore;
  bool m_restore_meta;
  bool m_no_restore_disk;
  bool m_restore_epoch;
  bool m_no_upgrade; // for upgrade ArrayType from 5.0 backup file.
  bool m_promote_attributes; 
  static bool m_reserve_tail_spaces;

  Uint32 m_n_tablespace;
  Uint32 m_n_logfilegroup;
  Uint32 m_n_datafile;
  Uint32 m_n_undofile;
  Uint32 m_n_tables;

  Uint64 m_logBytes;
  Uint64 m_dataBytes;

  Uint32 m_logCount;
  Uint32 m_dataCount;

  Uint32 m_parallelism;
  volatile Uint32 m_transactions;

  restore_callback_t *m_callback;
  restore_callback_t *m_free_callback;
  bool m_temp_error;

  /**
   * m_new_table_ids[X] = Y;
   *   X - old table id
   *   Y != 0  - new table
   */
  Vector<const NdbDictionary::Table*> m_new_tables;
  struct {
    const NdbDictionary::Table* m_old_table;
    const NdbDictionary::Table* m_new_table;
  } m_cache;
  const NdbDictionary::Table* get_table(const NdbDictionary::Table* );

  Vector<const NdbDictionary::Table*> m_indexes;
  Vector<NdbDictionary::Tablespace*> m_tablespaces;    // Index by id
  Vector<NdbDictionary::LogfileGroup*> m_logfilegroups;// Index by id

  static const PromotionRules m_allowed_promotion_attrs[];
};

#endif
