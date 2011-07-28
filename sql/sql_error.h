/* Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef SQL_ERROR_H
#define SQL_ERROR_H

#include "sql_list.h" /* Sql_alloc, MEM_ROOT */
#include "m_string.h" /* LEX_STRING */
#include "sql_string.h"                        /* String */
#include "sql_plist.h" /* I_P_List */
#include "mysql_com.h" /* MYSQL_ERRMSG_SIZE */

class THD;

///////////////////////////////////////////////////////////////////////////

/**
  Representation of a SQL condition.
  A SQL condition can be a completion condition (note, warning),
  or an exception condition (error, not found).
  @note This class is named MYSQL_ERROR instead of SQL_condition for
  historical reasons, to facilitate merging code with previous releases.
*/
class MYSQL_ERROR : public Sql_alloc
{
public:
  /*
    Enumeration value describing the severity of the error.

    Note that these enumeration values must correspond to the indices
    of the sql_print_message_handlers array.
  */
  enum enum_warning_level
  { WARN_LEVEL_NOTE, WARN_LEVEL_WARN, WARN_LEVEL_ERROR, WARN_LEVEL_END};
  /**
    Get the MESSAGE_TEXT of this condition.
    @return the message text.
  */
  const char* get_message_text() const;

  /**
    Get the MESSAGE_OCTET_LENGTH of this condition.
    @return the length in bytes of the message text.
  */
  int get_message_octet_length() const;

  /**
    Get the SQLSTATE of this condition.
    @return the sql state.
  */
  const char* get_sqlstate() const
  { return m_returned_sqlstate; }

  /**
    Get the SQL_ERRNO of this condition.
    @return the sql error number condition item.
  */
  uint get_sql_errno() const
  { return m_sql_errno; }

  /**
    Get the error level of this condition.
    @return the error level condition item.
  */
  MYSQL_ERROR::enum_warning_level get_level() const
  { return m_level; }

private:
  /*
    The interface of MYSQL_ERROR is mostly private, by design,
    so that only the following code:
    - various raise_error() or raise_warning() methods in class THD,
    - the implementation of SIGNAL / RESIGNAL
    - catch / re-throw of SQL conditions in stored procedures (sp_rcontext)
    is allowed to create / modify a SQL condition.
    Enforcing this policy prevents confusion, since the only public
    interface available to the rest of the server implementation
    is the interface offered by the THD methods (THD::raise_error()),
    which should be used.
  */
  friend class THD;
  friend class Warning_info;
  friend class Sql_cmd_common_signal;
  friend class Sql_cmd_signal;
  friend class Sql_cmd_resignal;
  friend class sp_rcontext;

  /**
    Default constructor.
    This constructor is usefull when allocating arrays.
    Note that the init() method should be called to complete the MYSQL_ERROR.
  */
  MYSQL_ERROR();

  /**
    Complete the MYSQL_ERROR initialisation.
    @param mem_root The memory root to use for the condition items
    of this condition
  */
  void init(MEM_ROOT *mem_root);

  /**
    Constructor.
    @param mem_root The memory root to use for the condition items
    of this condition
  */
  MYSQL_ERROR(MEM_ROOT *mem_root);

  /** Destructor. */
  ~MYSQL_ERROR()
  {}

  /**
    Copy optional condition items attributes.
    @param cond the condition to copy.
  */
  void copy_opt_attributes(const MYSQL_ERROR *cond);

  /**
    Set this condition area with a fixed message text.
    @param thd the current thread.
    @param code the error number for this condition.
    @param str the message text for this condition.
    @param level the error level for this condition.
    @param MyFlags additional flags.
  */
  void set(uint sql_errno, const char* sqlstate,
           MYSQL_ERROR::enum_warning_level level,
           const char* msg);

  /**
    Set the condition message test.
    @param str Message text, expressed in the character set derived from
    the server --language option
  */
  void set_builtin_message_text(const char* str);

  /** Set the SQLSTATE of this condition. */
  void set_sqlstate(const char* sqlstate);

  /**
    Clear this SQL condition.
  */
  void clear();

private:
  /** SQL CLASS_ORIGIN condition item. */
  String m_class_origin;

  /** SQL SUBCLASS_ORIGIN condition item. */
  String m_subclass_origin;

  /** SQL CONSTRAINT_CATALOG condition item. */
  String m_constraint_catalog;

  /** SQL CONSTRAINT_SCHEMA condition item. */
  String m_constraint_schema;

  /** SQL CONSTRAINT_NAME condition item. */
  String m_constraint_name;

  /** SQL CATALOG_NAME condition item. */
  String m_catalog_name;

  /** SQL SCHEMA_NAME condition item. */
  String m_schema_name;

  /** SQL TABLE_NAME condition item. */
  String m_table_name;

  /** SQL COLUMN_NAME condition item. */
  String m_column_name;

  /** SQL CURSOR_NAME condition item. */
  String m_cursor_name;

  /** Message text, expressed in the character set implied by --language. */
  String m_message_text;

  /** MySQL extension, MYSQL_ERRNO condition item. */
  uint m_sql_errno;

  /**
    SQL RETURNED_SQLSTATE condition item.
    This member is always NUL terminated.
  */
  char m_returned_sqlstate[SQLSTATE_LENGTH+1];

  /** Severity (error, warning, note) of this condition. */
  MYSQL_ERROR::enum_warning_level m_level;


  /** Pointers for participating in the list of conditions. */
  MYSQL_ERROR *next_in_wi;
  MYSQL_ERROR **prev_in_wi;

  /** Memory root to use to hold condition item values. */
  MEM_ROOT *m_mem_root;
};

///////////////////////////////////////////////////////////////////////////

/**
  Information about warnings of the current connection.
*/

class Warning_info
{
  /** The type of the counted and doubly linked list of conditions. */
  typedef I_P_List<MYSQL_ERROR,
                   I_P_List_adapter<MYSQL_ERROR,
                                    &MYSQL_ERROR::next_in_wi,
                                    &MYSQL_ERROR::prev_in_wi>,
                   I_P_List_counter,
                   I_P_List_fast_push_back<MYSQL_ERROR> >
          MYSQL_ERROR_list;

  /** A memory root to allocate warnings and errors */
  MEM_ROOT           m_warn_root;

  /** List of warnings of all severities (levels). */
  MYSQL_ERROR_list   m_warn_list;

  /** A break down of the number of warnings per severity (level). */
  uint	             m_warn_count[(uint) MYSQL_ERROR::WARN_LEVEL_END];

  /**
    The number of warnings of the current statement. Warning_info
    life cycle differs from statement life cycle -- it may span
    multiple statements. In that case we get
    m_current_statement_warn_count 0, whereas m_warn_list is not empty.
  */
  uint	             m_current_statement_warn_count;

  /*
    Row counter, to print in errors and warnings. Not increased in
    create_sort_index(); may differ from examined_row_count.
  */
  ulong              m_current_row_for_warning;

  /** Used to optionally clear warnings only once per statement. */
  ulonglong          m_warn_id;

  /**
    A pointer to an element of m_warn_list. It determines SQL-condition
    instance which corresponds to the error state in Diagnostics_area.
  */
  const MYSQL_ERROR *m_error_condition;

  /** Indicates if push_warning() allows unlimited number of warnings. */
  bool               m_allow_unlimited_warnings;

private:
  Warning_info(const Warning_info &rhs); /* Not implemented */
  Warning_info& operator=(const Warning_info &rhs); /* Not implemented */

public:
  Warning_info(ulonglong warn_id_arg, bool allow_unlimited_warnings);
  ~Warning_info();

  /** Type of the warning list. */
  typedef MYSQL_ERROR_list List;

  /** Iterator used to iterate through the warning list. */
  typedef List::Iterator Iterator;

  /** Const iterator used to iterate through the warning list. */
  typedef List::Const_Iterator Const_iterator;

  /**
    Reset the warning information. Clear all warnings,
    the number of warnings, reset current row counter
    to point to the first row.
  */
  void clear_warning_info(ulonglong warn_id_arg);
  /**
    Only clear warning info if haven't yet done that already
    for the current query. Allows to be issued at any time
    during the query, without risk of clearing some warnings
    that have been generated by the current statement.

    @todo: This is a sign of sloppy coding. Instead we need to
    designate one place in a statement life cycle where we call
    clear_warning_info().
  */
  void opt_clear_warning_info(ulonglong query_id)
  {
    if (query_id != m_warn_id)
      clear_warning_info(query_id);
  }

  /**
    Concatenate the list of warnings.
    It's considered tolerable to lose a warning.
  */
  void append_warning_info(THD *thd, const Warning_info *source);

  /**
    Conditional merge of related warning information areas.
  */
  void merge_with_routine_info(THD *thd, const Warning_info *source);

  /**
    Reset between two COM_ commands. Warnings are preserved
    between commands, but statement_warn_count indicates
    the number of warnings of this particular statement only.
  */
  void reset_for_next_command() { m_current_statement_warn_count= 0; }

  /**
    Remove given SQL-condition from the list.
  */
  bool remove_sql_condition(const MYSQL_ERROR *sql_condition);

  /**
    Used for @@warning_count system variable, which prints
    the number of rows returned by SHOW WARNINGS.
  */
  ulong warn_count() const
  {
    /*
      This may be higher than warn_list.elements() if we have
      had more warnings than thd->variables.max_error_count.
    */
    return (m_warn_count[(uint) MYSQL_ERROR::WARN_LEVEL_NOTE] +
            m_warn_count[(uint) MYSQL_ERROR::WARN_LEVEL_ERROR] +
            m_warn_count[(uint) MYSQL_ERROR::WARN_LEVEL_WARN]);
  }

  /**
    Returns a const iterator pointing to the beginning of the warning list.
  */
  Const_iterator iterator() const { return m_warn_list; }

  /**
    The number of errors, or number of rows returned by SHOW ERRORS,
    also the value of session variable @@error_count.
  */
  ulong error_count() const
  {
    return m_warn_count[(uint) MYSQL_ERROR::WARN_LEVEL_ERROR];
  }

  /** Id of the warning information area. */
  ulonglong warn_id() const { return m_warn_id; }

  /** Do we have any errors and warnings that we can *show*? */
  bool is_empty() const { return m_warn_list.is_empty(); }

  /** Increment the current row counter to point at the next row. */
  void inc_current_row_for_warning() { m_current_row_for_warning++; }
  /** Reset the current row counter. Start counting from the first row. */
  void reset_current_row_for_warning() { m_current_row_for_warning= 1; }
  /** Return the current counter value. */
  ulong current_row_for_warning() const { return m_current_row_for_warning; }

  ulong current_statement_warn_count() const
  { return m_current_statement_warn_count; }

  /** Make sure there is room for the given number of conditions. */
  void reserve_space(THD *thd, uint count);

  /** Add a new condition to the current list. */
  MYSQL_ERROR *push_warning(THD *thd,
                            uint sql_errno, const char* sqlstate,
                            MYSQL_ERROR::enum_warning_level level,
                            const char* msg);

  /** Add a new condition to the current list. */
  MYSQL_ERROR *push_warning(THD *thd, const MYSQL_ERROR *sql_condition);

  /**
    Set the read only status for this statement area.
    This is a privileged operation, reserved for the implementation of
    diagnostics related statements, to enforce that the statement area is
    left untouched during execution.
    The diagnostics statements are:
    - SHOW WARNINGS
    - SHOW ERRORS
    - GET DIAGNOSTICS
    @param read_only the read only property to set
  */
  void set_read_only(bool read_only)
  { m_read_only= read_only; }

  /**
    Read only status.
    @return the read only property
  */
  bool is_read_only() const
  { return m_read_only; }

  const MYSQL_ERROR *get_error_condition() const
  { return m_error_condition; }

  void set_error_condition(const MYSQL_ERROR *error_condition)
  { m_error_condition= error_condition; }

  void clear_error_condition()
  { m_error_condition= NULL; }

private:
  /** Read only status. */
  bool m_read_only;

  friend class Sql_cmd_resignal;
};

extern char *err_conv(char *buff, uint to_length, const char *from,
                      uint from_length, const CHARSET_INFO *from_cs);

class ErrConvString
{
  char err_buffer[MYSQL_ERRMSG_SIZE];
public:

  ErrConvString(String *str)
  {
    (void) err_conv(err_buffer, sizeof(err_buffer), str->ptr(),
                    str->length(), str->charset());
  }

  ErrConvString(const char *str, const CHARSET_INFO* cs)
  {
    (void) err_conv(err_buffer, sizeof(err_buffer),
                    str, strlen(str), cs);
  }

  ErrConvString(const char *str, uint length, const CHARSET_INFO* cs)
  {
    (void) err_conv(err_buffer, sizeof(err_buffer),
                    str, length, cs);
  }

  ~ErrConvString() { };
  char *ptr() { return err_buffer; }
};

///////////////////////////////////////////////////////////////////////////

/**
  Stores status of the currently executed statement.
  Cleared at the beginning of the statement, and then
  can hold either OK, ERROR, or EOF status.
  Can not be assigned twice per statement.
*/

class Diagnostics_area
{
public:
  enum enum_diagnostics_status
  {
    /** The area is cleared at start of a statement. */
    DA_EMPTY= 0,
    /** Set whenever one calls my_ok(). */
    DA_OK,
    /** Set whenever one calls my_eof(). */
    DA_EOF,
    /** Set whenever one calls my_error() or my_message(). */
    DA_ERROR,
    /** Set in case of a custom response, such as one from COM_STMT_PREPARE. */
    DA_DISABLED
  };
  /** True if status information is sent to the client. */
  bool is_sent;
  /** Set to make set_error_status after set_{ok,eof}_status possible. */
  bool can_overwrite_status;

  void set_ok_status(THD *thd, ulonglong affected_rows_arg,
                     ulonglong last_insert_id_arg,
                     const char *message);
  void set_eof_status(THD *thd);
  void set_error_status(THD *thd, uint sql_errno_arg, const char *message_arg,
                        const char *sqlstate, MYSQL_ERROR *error_condition);

  void disable_status();

  void reset_diagnostics_area();

  bool is_set() const { return m_status != DA_EMPTY; }
  bool is_error() const { return m_status == DA_ERROR; }
  bool is_eof() const { return m_status == DA_EOF; }
  bool is_ok() const { return m_status == DA_OK; }
  bool is_disabled() const { return m_status == DA_DISABLED; }
  enum_diagnostics_status status() const { return m_status; }

  const char *message() const
  { DBUG_ASSERT(m_status == DA_ERROR || m_status == DA_OK); return m_message; }

  uint sql_errno() const
  { DBUG_ASSERT(m_status == DA_ERROR); return m_sql_errno; }

  const char* get_sqlstate() const
  { DBUG_ASSERT(m_status == DA_ERROR); return m_sqlstate; }

  ulonglong affected_rows() const
  { DBUG_ASSERT(m_status == DA_OK); return m_affected_rows; }

  ulonglong last_insert_id() const
  { DBUG_ASSERT(m_status == DA_OK); return m_last_insert_id; }

  uint statement_warn_count() const
  {
    DBUG_ASSERT(m_status == DA_OK || m_status == DA_EOF);
    return m_statement_warn_count;
  }

public:
  Diagnostics_area();
  Diagnostics_area(ulonglong warn_id, bool allow_unlimited_warnings);

public:
  inline Warning_info *get_warning_info()
  { return m_current_wi; }

  inline const Warning_info *get_warning_info() const
  { return m_current_wi; }

  inline void set_warning_info(Warning_info *wi)
  { m_current_wi= wi; }

private:
  /** Message buffer. Can be used by OK or ERROR status. */
  char m_message[MYSQL_ERRMSG_SIZE];
  /**
    SQL error number. One of ER_ codes from share/errmsg.txt.
    Set by set_error_status.
  */
  uint m_sql_errno;

  char m_sqlstate[SQLSTATE_LENGTH+1];

  /**
    The number of rows affected by the last statement. This is
    semantically close to thd->row_count_func, but has a different
    life cycle. thd->row_count_func stores the value returned by
    function ROW_COUNT() and is cleared only by statements that
    update its value, such as INSERT, UPDATE, DELETE and few others.
    This member is cleared at the beginning of the next statement.

    We could possibly merge the two, but life cycle of thd->row_count_func
    can not be changed.
  */
  ulonglong    m_affected_rows;
  /**
    Similarly to the previous member, this is a replacement of
    thd->first_successful_insert_id_in_prev_stmt, which is used
    to implement LAST_INSERT_ID().
  */
  ulonglong   m_last_insert_id;
  /**
    Number of warnings of this last statement. May differ from
    the number of warnings returned by SHOW WARNINGS e.g. in case
    the statement doesn't clear the warnings, and doesn't generate
    them.
  */
  uint	     m_statement_warn_count;
  enum_diagnostics_status m_status;

  Warning_info m_main_wi;
  Warning_info *m_current_wi;
};

///////////////////////////////////////////////////////////////////////////


void push_warning(THD *thd, MYSQL_ERROR::enum_warning_level level,
                  uint code, const char *msg);
void push_warning_printf(THD *thd, MYSQL_ERROR::enum_warning_level level,
                         uint code, const char *format, ...);
bool mysqld_show_warnings(THD *thd, ulong levels_to_show);
uint32 convert_error_message(char *to, uint32 to_length,
                             const CHARSET_INFO *to_cs,
                             const char *from, uint32 from_length,
                             const CHARSET_INFO *from_cs, uint *errors);

extern const LEX_STRING warning_level_names[];

#endif // SQL_ERROR_H
