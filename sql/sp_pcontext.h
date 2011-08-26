/* -*- C++ -*- */
/* Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#ifndef _SP_PCONTEXT_H_
#define _SP_PCONTEXT_H_

#include "sql_string.h"                         // LEX_STRING
#include "mysql_com.h"                          // enum_field_types
#include "field.h"                              // Create_field
#include "sql_array.h"                          // Dynamic_array


/// This structure represents a stored program variable or a parameter
/// (also referenced as 'SP-variable').

struct sp_variable
{
  enum enum_mode
  {
    MODE_IN,
    MODE_OUT,
    MODE_INOUT
  };

  /// Name of the SP-variable.
  LEX_STRING name;

  /// Field-type of the SP-variable.
  enum enum_field_types type;

  /// Mode of the SP-variable.
  enum_mode mode;

  /// The index to the variable's value in the runtime frame.
  ///
  /// It is calculated during parsing and used when creating sp_instr_set
  /// instructions and Item_splocal items. I.e. values are set/referred by
  /// array indexing in runtime.
  uint offset;

  /// Default value of the SP-variable (if any).
  Item *default_value;

  /// Full type information (field meta-data) of the SP-variable.
  Create_field field_def;
};

///////////////////////////////////////////////////////////////////////////

/// This structure represents an SQL/PSM label. Can refer to the identifier
/// used with the "label_name:" construct which may precede some SQL/PSM
/// statements, or to an implicit implementation-dependent identifier which
/// the parser inserts before a high-level flow control statement such as
/// IF/WHILE/REPEAT/LOOP, when such statement is rewritten into a
/// combination of low-level jump/jump_if instructions and labels.

struct sp_label
{
  enum enum_type
  {
    /// Implicit label generated by parser.
    IMPLICIT,

    /// Label at BEGIN.
    BEGIN,

    /// Label at iteration control
    ITERATION
  };

  /// Name of the label.
  char *name;

  /// Instruction pointer of the label.
  uint ip;

  /// Type of the label.
  enum_type type;

  /// Scope of the label.
  class sp_pcontext *ctx;
};

///////////////////////////////////////////////////////////////////////////

/// This class represents condition-value term in DECLARE CONDITION or
/// DECLARE HANDLER statements. sp_condition_value has little to do with
/// SQL-conditions.
///
/// In some sense, this class is a union -- a set of filled attributes
/// depends on the sp_condition_value::type value.

class sp_condition_value
{
public:
  enum enum_type
  {
    ERROR_CODE,
    SQLSTATE,
    WARNING,
    NOT_FOUND,
    EXCEPTION
  };

  /// Type of the condition value.
  enum_type type;

  /// SQLSTATE of the condition value.
  char sqlstate[SQLSTATE_LENGTH+1];

  /// MySQL error code of the condition value.
  uint mysqlerr;

public:
  /// Check if two instances of sp_condition_value are equal or not.
  ///
  /// @param cv another instance of sp_condition_value to check.
  ///
  /// @return true if the instances are equal, false otherwise.
  bool equals(const sp_condition_value *cv) const;
};

///////////////////////////////////////////////////////////////////////////

/// This structure represents 'DECLARE CONDITION' statement.
/// sp_condition has little to do with SQL-conditions.

struct sp_condition
{
  /// Name of the condition.
  LEX_STRING name;

  /// Value of the condition.
  sp_condition_value *value;
};

///////////////////////////////////////////////////////////////////////////

/// This class represents 'DECLARE HANDLER' statement.

class sp_handler
{
public:
  /// Enumeration of possible handler types.
  /// Note: UNDO handlers are not (and have never been) supported.
  enum enum_type
  {
    EXIT,
    CONTINUE
  };

  /// Handler type.
  enum_type type;

  /// Conditions caught by this handler.
  List<sp_condition_value> condition_values;

public:
  /// The constructor.
  ///
  /// @param _type SQL-handler type.
  sp_handler(enum_type _type)
   :type(_type)
  { }
};

///////////////////////////////////////////////////////////////////////////

/// The class represents parse-time context, which keeps track of declared
/// variables/parameters, conditions, handlers, cursors and labels.
///
/// sp_context objects are organized in a tree according to the following
/// rules:
///   - one sp_pcontext object corresponds for for each BEGIN..END block;
///   - one sp_pcontext object corresponds for each exception handler;
///   - one additional sp_pcontext object is created to contain
///     Stored Program parameters.
///
/// sp_pcontext objects are used both at parse-time and at runtime.
///
/// During the parsing stage sp_pcontext objects are used:
///   - to look up defined names (e.g. declared variables and visible
///     labels);
///   - to check for duplicates;
///   - for error checking;
///   - to calculate offsets to be used at runtime.
///
/// During the runtime phase, a tree of sp_pcontext objects is used:
///   - for error checking (e.g. to check correct number of parameters);
///   - to resolve SQL-handlers.

class sp_pcontext : public Sql_alloc
{
public:
  enum enum_scope
  {
    /// REGULAR_SCOPE designates regular BEGIN ... END blocks.
    REGULAR_SCOPE,

    /// HANDLER_SCOPE designates SQL-handler blocks.
    HANDLER_SCOPE
  };

public:
  sp_pcontext();
  ~sp_pcontext();

  /**
    Create and push a new context in the tree.
    @param scope scope of the new parsing context
    @return the node created
  */
  sp_pcontext *push_context(enum_scope scope);

  /**
    Pop a node from the parsing context tree.
    @return the parent node
  */
  sp_pcontext *pop_context();

  sp_pcontext *parent_context() const
  { return m_parent; }

  /// Calculate and return the number of handlers to pop between the given
  /// context and this one.
  ///
  /// @param ctx       the other parsing context.
  /// @param exclusive specifies if the last scope should be excluded.
  ///
  /// @return the number of handlers to pop between the given context and
  /// this one.  If 'exclusive' is true, don't count the last scope we are
  /// leaving; this is used for LEAVE where we will jump to the hpop
  /// instructions.
  uint diff_handlers(const sp_pcontext *ctx, bool exclusive) const;

  /// Calculate and return the number of cursors to pop between the given
  /// context and this one.
  ///
  /// @param ctx       the other parsing context.
  /// @param exclusive specifies if the last scope should be excluded.
  ///
  /// @return the number of cursors to pop between the given context and
  /// this one.  If 'exclusive' is true, don't count the last scope we are
  /// leaving; this is used for LEAVE where we will jump to the cpop
  /// instructions.
  uint diff_cursors(const sp_pcontext *ctx, bool exclusive) const;

  /////////////////////////////////////////////////////////////////////////
  // SP-variables (parameters and variables).
  /////////////////////////////////////////////////////////////////////////

  /// @return the maximum number of variables used in this and all child
  /// contexts. For the root parsing context, this gives us the number of
  /// slots needed for variables during the runtime phase.
  uint max_var_index() const
  { return m_max_var_index; }

  /// @return the current number of variables used in the parent contexts
  /// (from the root), including this context.
  uint current_var_count() const
  { return m_var_offset + m_vars.elements(); }

  /// @return the number of variables in this context alone.
  uint context_var_count() const
  { return m_vars.elements(); }

  /// @return map index in this parsing context to runtime offset.
  uint var_context2runtime(uint i) const
  { return m_var_offset + i; }

  /// Add SP-variable to the parsing context.
  ///
  /// @param name Name of the SP-variable.
  /// @param type Type of the SP-variable.
  /// @param mode Mode of the SP-variable.
  ///
  /// @return instance of newly added SP-variable.
  sp_variable *push_variable(LEX_STRING *name,
                             enum enum_field_types type,
                             sp_variable::enum_mode mode);

  /// Retrieve full type information about SP-variables in this parsing
  /// context and its children.
  ///
  /// @param field_def_lst[out] Container to store type information.
  void retrieve_field_definitions(List<Create_field> *field_def_lst) const;

  /// Find SP-variable by name.
  ///
  /// The function does a linear search (from newer to older variables,
  /// in case we have shadowed names).
  ///
  /// The function is called only at parsing time.
  ///
  /// @param name               Variable name.
  /// @param current_scope_only A flag if we search only in current scope.
  ///
  /// @return instance of found SP-variable, or NULL if not found.
  sp_variable *find_variable(LEX_STRING *name, bool current_scope_only) const;

  /// Find SP-variable by the offset in the root parsing context.
  ///
  /// The function is used for two things:
  /// - When evaluating parameters at the beginning, and setting out parameters
  ///   at the end, of invocation. (Top frame only, so no recursion then.)
  /// - For printing of sp_instr_set. (Debug mode only.)
  ///
  /// @param offset Variable offset in the root parsing context.
  ///
  /// @return instance of found SP-variable, or NULL if not found.
  sp_variable *find_variable(uint offset) const;

  /// Set the current scope boundary (for default values).
  ///
  /// @param n The number of variables to skip.
  void declare_var_boundary(uint n)
  { m_pboundary= n; }

  /////////////////////////////////////////////////////////////////////////
  // CASE expressions.
  /////////////////////////////////////////////////////////////////////////

  int register_case_expr()
  { return m_num_case_exprs++; }

  int get_num_case_exprs() const
  { return m_num_case_exprs; }

  bool push_case_expr_id(int case_expr_id)
  { return m_case_expr_ids.append(case_expr_id); }

  void pop_case_expr_id()
  { m_case_expr_ids.pop(); }

  int get_current_case_expr_id() const
  { return *m_case_expr_ids.back(); }

  /////////////////////////////////////////////////////////////////////////
  // Labels.
  /////////////////////////////////////////////////////////////////////////

  sp_label *push_label(char *name, uint ip);

  sp_label *find_label(const char *name);

  sp_label *last_label()
  {
    sp_label *label= m_labels.head();

    if (!label && m_parent)
      label= m_parent->last_label();

    return label;
  }

  sp_label *pop_label()
  { return m_labels.pop(); }

  /////////////////////////////////////////////////////////////////////////
  // Conditions.
  /////////////////////////////////////////////////////////////////////////

  bool push_condition(LEX_STRING *name, sp_condition_value *value);

  /// See comment for find_variable() above.
  sp_condition_value *find_condition(const LEX_STRING *name,
                                      bool current_scope_only) const;

  /////////////////////////////////////////////////////////////////////////
  // Handlers.
  /////////////////////////////////////////////////////////////////////////

  void push_handler(sp_handler *handler)
  { m_handlers.append(handler); }

  /// This is an auxilary parsing-time function to check if an SQL-handler
  /// exists in the current parsing context (current scope) for the given
  /// SQL-condition. This function is used to check for duplicates during
  /// the parsing phase.
  ///
  /// This function can not be used during the runtime phase to check
  /// SQL-handler existence because it searches for the SQL-handler in the
  /// current scope only (during runtime, current and parent scopes
  /// should be checked according to the SQL-handler resolution rules).
  ///
  /// @param condition_value the handler condition value
  ///                        (not SQL-condition!).
  ///
  /// @retval true if such SQL-handler exists.
  /// @retval false otherwise.
  bool check_duplicate_handler(const sp_condition_value *cond_value) const;

  /// Find an SQL handler for the given SQL condition according to the
  /// SQL-handler resolution rules. This function is used at runtime.
  ///
  /// @param sqlstate         The SQL condition state
  /// @param sql_errno        The error code
  /// @param level            The SQL condition level
  ///
  /// @return a pointer to the found SQL-handler or NULL.
  sp_handler *find_handler(const char *sqlstate,
                           uint sql_errno,
                           Sql_condition::enum_warning_level level);

  /////////////////////////////////////////////////////////////////////////
  // Cursors.
  /////////////////////////////////////////////////////////////////////////

  bool push_cursor(LEX_STRING *name);

  /// See comment for find_variable() above.
  bool find_cursor(LEX_STRING *name, uint *poff, bool current_scope_only) const;

  /// Find cursor by offset (for debugging only).
  bool find_cursor(uint offset, LEX_STRING *n) const;

  uint max_cursor_index() const
  { return m_max_cursor_index + m_cursors.elements(); }

  uint current_cursor_count() const
  { return m_cursor_offset + m_cursors.elements(); }

private:
  /**
    Constructor for a tree node.
    @param prev the parent parsing context
    @param scope scope of this parsing context
  */
  sp_pcontext(sp_pcontext *prev, enum_scope scope);

  void init(uint var_offset, uint cursor_offset, int num_case_expressions);

  /* Prevent use of these */
  sp_pcontext(const sp_pcontext &);
  void operator=(sp_pcontext &);

private:
  /*
    m_max_var_index -- number of variables (including all types of arguments)
    in this context including all children contexts.

    m_max_var_index >= m_vars.elements().

    m_max_var_index of the root parsing context contains number of all
    variables (including arguments) in all enclosed contexts.
  */
  uint m_max_var_index;

  /// The maximum sub context's framesizes.
  uint m_max_cursor_index;

  /// Parent context.
  sp_pcontext *m_parent;

  /// An index of the first SP-variable in this parsing context. The index
  /// belongs to a runtime table of SP-variables.
  ///
  /// Note:
  ///   - m_var_offset is 0 for root parsing context;
  ///   - m_var_offset is different for all nested parsing contexts.
  uint m_var_offset;

  /// Cursor offset for this context.
  uint m_cursor_offset;

  /// Boundary for finding variables in this context. This is the number of
  /// variables currently "invisible" to default clauses. This is normally 0,
  /// but will be larger during parsing of DECLARE ... DEFAULT, to get the
  /// scope right for DEFAULT values.
  uint m_pboundary;

  int m_num_case_exprs;

  /// SP parameters/variables.
  Dynamic_array<sp_variable *> m_vars;

  /// Stack of CASE expression ids.
  Dynamic_array<int> m_case_expr_ids;

  /// Stack of SQL-conditions.
  Dynamic_array<sp_condition *> m_conditions;

  /// Stack of cursors.
  Dynamic_array<LEX_STRING> m_cursors;

  /// Stack of SQL-handlers.
  Dynamic_array<sp_handler *> m_handlers;

  /// List of labels.
  List<sp_label> m_labels;

  /// Children contexts, used for destruction.
  Dynamic_array<sp_pcontext *> m_children;

  /// Scope of this parsing context.
  enum_scope m_scope;
}; // class sp_pcontext : public Sql_alloc


#endif /* _SP_PCONTEXT_H_ */
