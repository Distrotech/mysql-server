/*
   Copyright (C) 2003 MySQL AB
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

#include "API.hpp"
#include <NdbScanFilter.hpp>
#include <Vector.hpp>
#include <NdbOut.hpp>
#include <Interpreter.hpp>
#include <signaldata/AttrInfo.hpp>

#ifdef VM_TRACE
#include <NdbEnv.h>
#define INT_DEBUG(x) \
  { const char* tmp = NdbEnv_GetEnv("INT_DEBUG", (char*)0, 0); \
  if (tmp != 0 && strlen(tmp) != 0) { ndbout << "INT:"; ndbout_c x; } }
#else
#define INT_DEBUG(x)
#endif

class NdbScanFilterImpl {
public:
  NdbScanFilterImpl() {}
  struct State {
    NdbScanFilter::Group m_group;
    Uint32 m_popCount;
    Uint32 m_ownLabel;
    Uint32 m_trueLabel;
    Uint32 m_falseLabel;
  };

  int m_label;
  State m_current;
  Uint32 m_negative;    //used for translating NAND/NOR to AND/OR, equal 0 or 1 
  Vector<State> m_stack;
  Vector<Uint32> m_stack2;    //to store info of m_negative
  NdbInterpretedCode * m_code;
  NdbError m_error;

  /* Members for supporting old Api */
  NdbScanOperation *m_associated_op;

  int cond_col(Interpreter::UnaryCondition, Uint32 attrId);
  
  int cond_col_const(Interpreter::BinaryCondition, Uint32 attrId, 
		     const void * value, Uint32 len);

  /* Method to initialise the members */
  void init (NdbInterpretedCode *code)
  {
    m_current.m_group = (NdbScanFilter::Group)0;
    m_current.m_popCount = 0;
    m_current.m_ownLabel = 0;
    m_current.m_trueLabel = ~0;
    m_current.m_falseLabel = ~0;
    m_label = 0;
    m_negative = 0;
    m_code= code;
    m_associated_op= NULL;
    
    if (code == NULL)
      /* NdbInterpretedCode not supported for operation type */
      m_error.code = 4539;
    else
      m_error.code = 0;
  };

  /* This method propagates an error code from NdbInterpretedCode
   * back to the NdbScanFilter object
   */
  int propagateErrorFromCode()
  {
    NdbError codeError= m_code->getNdbError();

    /* Map interpreted code's 'Too many instructions in 
     * interpreted program' to FilterTooLarge error for 
     * NdbScanFilter
     */
    if (codeError.code == 4518) 
      m_error.code = NdbScanFilter::FilterTooLarge;
    else
      m_error.code = codeError.code;

    return -1;
  };

  /* This method performs any steps required once the
   * filter definition is complete
   */
  int handleFilterDefined()
  {
    /* Finalise the interpreted program */
    if (m_code->finalise() != 0)
      return propagateErrorFromCode();

    /* For old Api support, we set the passed-in operation's
     * interpreted code to be the code generated by the
     * scanfilter
     */
    if (m_associated_op != NULL)
    {
      m_associated_op->setInterpretedCode(m_code);
    }

    return 0;
  }

};

const Uint32 LabelExit = ~0;


NdbScanFilter::NdbScanFilter(NdbInterpretedCode* code) :
  m_impl(* new NdbScanFilterImpl())
{
  DBUG_ENTER("NdbScanFilter::NdbScanFilter(NdbInterpretedCode)");
  m_impl.init(code);
  DBUG_VOID_RETURN;
}

NdbScanFilter::NdbScanFilter(class NdbOperation * op) :
  m_impl(* new NdbScanFilterImpl())
{
  DBUG_ENTER("NdbScanFilter::NdbScanFilter(NdbOperation)");
  
  NdbInterpretedCode* code= NULL;
  NdbOperation::Type opType= op->getType();

  /* If the operation is not of the correct type then
   * m_impl.init() will set an error on the scan filter
   */
  if (likely((opType == NdbOperation::TableScan) || 
             (opType == NdbOperation::OrderedIndexScan)))
  {    
    /* We ask the NdbScanOperation to allocate an InterpretedCode
     * object for us.  It will look after freeing it when 
     * necessary.  This allows the InterpretedCode object to 
     * survive after the NdbScanFilter has gone out of scope
     */
    code= ((NdbScanOperation *)op)->allocInterpretedCodeOldApi();
  }

  m_impl.init(code);

  m_impl.m_associated_op= (NdbScanOperation*) op;

  DBUG_VOID_RETURN;
}

NdbScanFilter::~NdbScanFilter()
{
  delete &m_impl;
}

int
NdbScanFilter::begin(Group group){
  if (m_impl.m_error.code != 0) return -1;

  if (m_impl.m_stack2.push_back(m_impl.m_negative))
  {
    /* Memory allocation problem */
    m_impl.m_error.code= 4000;
    return -1;
  }
  switch(group){
  case NdbScanFilter::AND:
    INT_DEBUG(("Begin(AND)"));
    if(m_impl.m_negative == 1){
      group = NdbScanFilter::OR;
    }
    break;
  case NdbScanFilter::OR:
    INT_DEBUG(("Begin(OR)"));
    if(m_impl.m_negative == 1){
      group = NdbScanFilter::AND;
    }
    break;
  case NdbScanFilter::NAND:
    INT_DEBUG(("Begin(NAND)"));
    if(m_impl.m_negative == 0){
      group = NdbScanFilter::OR;
      m_impl.m_negative = 1; 
    }else{
      group = NdbScanFilter::AND;
      m_impl.m_negative = 0; 
    }
    break;
  case NdbScanFilter::NOR:
    INT_DEBUG(("Begin(NOR)"));
    if(m_impl.m_negative == 0){
      group = NdbScanFilter::AND;
      m_impl.m_negative = 1; 
    }else{
      group = NdbScanFilter::OR;
      m_impl.m_negative = 0; 
    }
    break;
  }

  if(group == m_impl.m_current.m_group){
    switch(group){
    case NdbScanFilter::AND:
    case NdbScanFilter::OR:
      m_impl.m_current.m_popCount++;
      return 0;
    case NdbScanFilter::NOR:
    case NdbScanFilter::NAND:
      break;
    }
  }

  NdbScanFilterImpl::State tmp = m_impl.m_current;
  if (m_impl.m_stack.push_back(m_impl.m_current))
  {
    /* Memory allocation problem */
    m_impl.m_error.code= 4000;
    return -1;
  }
  m_impl.m_current.m_group = group;
  m_impl.m_current.m_ownLabel = m_impl.m_label++;
  m_impl.m_current.m_popCount = 0;
  
  switch(group){
  case NdbScanFilter::AND:
  case NdbScanFilter::NAND:
    m_impl.m_current.m_falseLabel = m_impl.m_current.m_ownLabel;
    m_impl.m_current.m_trueLabel = tmp.m_trueLabel;
    break;
  case NdbScanFilter::OR:
  case NdbScanFilter::NOR:
    m_impl.m_current.m_falseLabel = tmp.m_falseLabel;
    m_impl.m_current.m_trueLabel = m_impl.m_current.m_ownLabel;
    break;
  default: 
    /* Operator is not defined in NdbScanFilter::Group  */
    m_impl.m_error.code= 4260;
    return -1;
  }
  
  return 0;
}

int
NdbScanFilter::end(){
  if (m_impl.m_error.code != 0) return -1;

  if(m_impl.m_stack2.size() == 0){
    /* Invalid set of range scan bounds */
    m_impl.m_error.code= 4259;
    return -1;
  }
  m_impl.m_negative = m_impl.m_stack2.back();
  m_impl.m_stack2.erase(m_impl.m_stack2.size() - 1);

  switch(m_impl.m_current.m_group){
  case NdbScanFilter::AND:
    INT_DEBUG(("End(AND pc=%d)", m_impl.m_current.m_popCount));
    break;
  case NdbScanFilter::OR:
    INT_DEBUG(("End(OR pc=%d)", m_impl.m_current.m_popCount));
    break;
  case NdbScanFilter::NAND:
    INT_DEBUG(("End(NAND pc=%d)", m_impl.m_current.m_popCount));
    break;
  case NdbScanFilter::NOR:
    INT_DEBUG(("End(NOR pc=%d)", m_impl.m_current.m_popCount));
    break;
  }

  if(m_impl.m_current.m_popCount > 0){
    m_impl.m_current.m_popCount--;
    return 0;
  }
  
  NdbScanFilterImpl::State tmp = m_impl.m_current;  
  if(m_impl.m_stack.size() == 0){
    /* Invalid set of range scan bounds */
    m_impl.m_error.code= 4259;
    return -1;
  }
  m_impl.m_current = m_impl.m_stack.back();
  m_impl.m_stack.erase(m_impl.m_stack.size() - 1);
  
  switch(tmp.m_group){
  case NdbScanFilter::AND:
    if(tmp.m_trueLabel == (Uint32)~0){
      if (m_impl.m_code->interpret_exit_ok() == -1)
        return m_impl.propagateErrorFromCode();
    } else {
      if (m_impl.m_code->branch_label(tmp.m_trueLabel) == -1)
        return m_impl.propagateErrorFromCode();
    }
    break;
  case NdbScanFilter::NAND:
    if(tmp.m_trueLabel == (Uint32)~0){
      if (m_impl.m_code->interpret_exit_nok() == -1)
        return m_impl.propagateErrorFromCode();
    } else {
      if (m_impl.m_code->branch_label(tmp.m_falseLabel) == -1)
        return m_impl.propagateErrorFromCode();
    }
    break;
  case NdbScanFilter::OR:
    if(tmp.m_falseLabel == (Uint32)~0){
      if (m_impl.m_code->interpret_exit_nok() == -1)
        return m_impl.propagateErrorFromCode();
    } else {
      if (m_impl.m_code->branch_label(tmp.m_falseLabel) == -1)
        return m_impl.propagateErrorFromCode();
    }
    break;
  case NdbScanFilter::NOR:
    if(tmp.m_falseLabel == (Uint32)~0){
      if (m_impl.m_code->interpret_exit_ok() == -1)
        return m_impl.propagateErrorFromCode();
    } else {
      if (m_impl.m_code->branch_label(tmp.m_trueLabel) == -1)
        return m_impl.propagateErrorFromCode();
    }
    break;
  default:
    /* Operator is not defined in NdbScanFilter::Group */
    m_impl.m_error.code= 4260;
    return -1;
  }

  if (m_impl.m_code->def_label(tmp.m_ownLabel) == -1)
    return m_impl.propagateErrorFromCode();

  if(m_impl.m_stack.size() == 0){
    switch(tmp.m_group){
    case NdbScanFilter::AND:
    case NdbScanFilter::NOR:
      if (m_impl.m_code->interpret_exit_nok() == -1)
        return m_impl.propagateErrorFromCode();
      break;
    case NdbScanFilter::OR:
    case NdbScanFilter::NAND:
      if (m_impl.m_code->interpret_exit_ok() == -1)
        return m_impl.propagateErrorFromCode();
      break;
    default:
      /* Operator is not defined in NdbScanFilter::Group */
      m_impl.m_error.code= 4260;
      return -1;
    }

    /* Handle the completion of the filter definition */
    return m_impl.handleFilterDefined();
  }
  return 0;
}

int
NdbScanFilter::istrue(){
  if(m_impl.m_error.code != 0) return -1;

  if(m_impl.m_current.m_group < NdbScanFilter::AND || 
     m_impl.m_current.m_group > NdbScanFilter::NOR){
    /* Operator is not defined in NdbScanFilter::Group */
    m_impl.m_error.code= 4260;
    return -1;
  }

  if(m_impl.m_current.m_trueLabel == (Uint32)~0){
    if (m_impl.m_code->interpret_exit_ok() == -1)
      return m_impl.propagateErrorFromCode();
  } else {
    if (m_impl.m_code->branch_label(m_impl.m_current.m_trueLabel) == -1)
      return m_impl.propagateErrorFromCode();
  }

  return 0;
}

int
NdbScanFilter::isfalse(){
  if (m_impl.m_error.code != 0) return -1;
  if(m_impl.m_current.m_group < NdbScanFilter::AND || 
     m_impl.m_current.m_group > NdbScanFilter::NOR){
    /* Operator is not defined in NdbScanFilter::Group */
    m_impl.m_error.code= 4260;
    return -1;
  }
  
  if(m_impl.m_current.m_falseLabel == (Uint32)~0){
    if (m_impl.m_code->interpret_exit_nok() == -1)
      return m_impl.propagateErrorFromCode();
  } else {
    if (m_impl.m_code->branch_label(m_impl.m_current.m_falseLabel) == -1)
      return m_impl.propagateErrorFromCode();
  }

  return 0;
}

#define action(x, y, z)

/* One argument branch definition method signature */
typedef int (NdbInterpretedCode:: * Branch1)(Uint32, Uint32 label);
/* Two argument branch definition method signature */
typedef int (NdbInterpretedCode:: * StrBranch2)(const void*, Uint32, Uint32,  Uint32);

/* Table of unary branch methods for each group type */
struct tab2 {
  Branch1 m_branches[5];
};

/* Table of correct branch method to use for group types
 * and condition type.
 * In general, AND branches to fail (short circuits) if the 
 * condition is not satisfied, and OR branches to success 
 * (short circuits) if it is satisfied.
 * NAND is the same as AND, with the branch condition inverted.
 * NOR is the same as OR, with the branch condition inverted
 */
static const tab2 table2[] = {
  /**
   * IS NULL
   */
  { { 0, 
      &NdbInterpretedCode::branch_col_ne_null,      // AND
      &NdbInterpretedCode::branch_col_eq_null,      // OR
      &NdbInterpretedCode::branch_col_eq_null,      // NAND
      &NdbInterpretedCode::branch_col_ne_null } }   // NOR
  
  /**
   * IS NOT NULL
   */
  ,{ { 0, 
       &NdbInterpretedCode::branch_col_eq_null,     // AND
       &NdbInterpretedCode::branch_col_ne_null,     // OR
       &NdbInterpretedCode::branch_col_ne_null,     // NAND
       &NdbInterpretedCode::branch_col_eq_null } }  // NOR
};

const int tab2_sz = sizeof(table2)/sizeof(table2[0]);

int
NdbScanFilterImpl::cond_col(Interpreter::UnaryCondition op, Uint32 AttrId){
  
  if (m_error.code != 0) return -1;

  if(op < 0 || op >= tab2_sz){
    /* Condition is out of bounds */
    m_error.code= 4262;
    return -1;
  }
  
  if(m_current.m_group < NdbScanFilter::AND || 
     m_current.m_group > NdbScanFilter::NOR){
    /* Operator is not defined in NdbScanFilter::Group */
    m_error.code= 4260;
    return -1;
  }
  
  Branch1 branch = table2[op].m_branches[m_current.m_group];
  if ((m_code->* branch)(AttrId, m_current.m_ownLabel) == -1)
    return propagateErrorFromCode();

  return 0;
}

int
NdbScanFilter::isnull(int AttrId){
  if (m_impl.m_error.code != 0) return -1;

  if(m_impl.m_negative == 1)
    return m_impl.cond_col(Interpreter::IS_NOT_NULL, AttrId);
  else
    return m_impl.cond_col(Interpreter::IS_NULL, AttrId);
}

int
NdbScanFilter::isnotnull(int AttrId){
  if (m_impl.m_error.code != 0) return -1;

  if(m_impl.m_negative == 1)
    return m_impl.cond_col(Interpreter::IS_NULL, AttrId);
  else
    return m_impl.cond_col(Interpreter::IS_NOT_NULL, AttrId);
}

/* NdbInterpretedCode two-arg branch method to use for
 * given logical group type
 */
struct tab3 {
  StrBranch2 m_branches[5];
};

/* Table of branch methds to use for each combination of
 * logical group type (AND, OR, NAND, NOR) and comparison
 * type.
 * Generally, AND short circuits by branching to the failure
 * label when the condition fails, and OR short circuits by
 * branching to the success label when the condition passes.
 * NAND and NOR invert these by inverting the 'sense' of the
 * branch
 */
static const tab3 table3[] = {
  /**
   * EQ (AND, OR, NAND, NOR)
   */
  { { 0, 
      &NdbInterpretedCode::branch_col_ne, 
      &NdbInterpretedCode::branch_col_eq, 
      &NdbInterpretedCode::branch_col_ne,  
      &NdbInterpretedCode::branch_col_eq } }
  
  /**
   * NEQ
   */
  ,{ { 0, 
       &NdbInterpretedCode::branch_col_eq, 
       &NdbInterpretedCode::branch_col_ne, 
       &NdbInterpretedCode::branch_col_eq, 
       &NdbInterpretedCode::branch_col_ne } }
  
  /**
   * LT
   */
  ,{ { 0, 
       &NdbInterpretedCode::branch_col_le, 
       &NdbInterpretedCode::branch_col_gt, 
       &NdbInterpretedCode::branch_col_le,
       &NdbInterpretedCode::branch_col_gt } }
  
  /**
   * LE
   */
  ,{ { 0, 
       &NdbInterpretedCode::branch_col_lt, 
       &NdbInterpretedCode::branch_col_ge, 
       &NdbInterpretedCode::branch_col_lt, 
       &NdbInterpretedCode::branch_col_ge } }
  
  /**
   * GT
   */
  ,{ { 0, 
       &NdbInterpretedCode::branch_col_ge, 
       &NdbInterpretedCode::branch_col_lt, 
       &NdbInterpretedCode::branch_col_ge, 
       &NdbInterpretedCode::branch_col_lt } }

  /**
   * GE
   */
  ,{ { 0, 
       &NdbInterpretedCode::branch_col_gt, 
       &NdbInterpretedCode::branch_col_le, 
       &NdbInterpretedCode::branch_col_gt, 
       &NdbInterpretedCode::branch_col_le } }

  /**
   * LIKE
   */
  ,{ { 0, 
       &NdbInterpretedCode::branch_col_notlike, 
       &NdbInterpretedCode::branch_col_like, 
       &NdbInterpretedCode::branch_col_notlike, 
       &NdbInterpretedCode::branch_col_like } }

  /**
   * NOT LIKE
   */
  ,{ { 0, 
       &NdbInterpretedCode::branch_col_like, 
       &NdbInterpretedCode::branch_col_notlike, 
       &NdbInterpretedCode::branch_col_like, 
       &NdbInterpretedCode::branch_col_notlike } }
  
  /**
   * AND EQ MASK
   */
  ,{ { 0,
       &NdbInterpretedCode::branch_col_and_mask_ne_mask,
       &NdbInterpretedCode::branch_col_and_mask_eq_mask,
       &NdbInterpretedCode::branch_col_and_mask_ne_mask,
       &NdbInterpretedCode::branch_col_and_mask_eq_mask } }
  
  /**
   * AND NE MASK
   */
  ,{ { 0,
       &NdbInterpretedCode::branch_col_and_mask_eq_mask,
       &NdbInterpretedCode::branch_col_and_mask_ne_mask,
       &NdbInterpretedCode::branch_col_and_mask_eq_mask,
       &NdbInterpretedCode::branch_col_and_mask_ne_mask } } 

  /**
   * AND EQ ZERO
   */
  ,{ { 0,
       &NdbInterpretedCode::branch_col_and_mask_ne_zero,
       &NdbInterpretedCode::branch_col_and_mask_eq_zero,
       &NdbInterpretedCode::branch_col_and_mask_ne_zero,
       &NdbInterpretedCode::branch_col_and_mask_eq_zero } }
  
  /**
   * AND NE ZERO
   */
  ,{ { 0,
       &NdbInterpretedCode::branch_col_and_mask_eq_zero,
       &NdbInterpretedCode::branch_col_and_mask_ne_zero,
       &NdbInterpretedCode::branch_col_and_mask_eq_zero,
       &NdbInterpretedCode::branch_col_and_mask_ne_zero } } 
};

const int tab3_sz = sizeof(table3)/sizeof(table3[0]);

int
NdbScanFilterImpl::cond_col_const(Interpreter::BinaryCondition op, 
				  Uint32 AttrId, 
				  const void * value, Uint32 len){
  if (m_error.code != 0) return -1;

  if(op < 0 || op >= tab3_sz){
    /* Condition is out of bounds */
    m_error.code= 4262;
    return -1;
  }
  
  if(m_current.m_group < NdbScanFilter::AND || 
     m_current.m_group > NdbScanFilter::NOR){
    /* Operator is not defined in NdbScanFilter::Group */
    m_error.code= 4260;
    return -1;
  }

  StrBranch2 branch;
  if(m_negative == 1){  //change NdbOperation to its negative
    if(m_current.m_group == NdbScanFilter::AND)
      branch = table3[op].m_branches[NdbScanFilter::OR];
    else if(m_current.m_group == NdbScanFilter::OR)
      branch = table3[op].m_branches[NdbScanFilter::AND];
    else
    {
      /**
       * This is not possible, as NAND/NOR is converted to negative OR/AND in
       * begin().
       * But silence the compiler warning about uninitialised variable `branch`
       */
      assert(FALSE);
      m_error.code= 4260;
      return -1;
    }
  }else{
    branch = table3[op].m_branches[(Uint32)(m_current.m_group)];
  }
  
  const NdbDictionary::Table * table = m_code->getTable();

  if (table == NULL)
  {
    /* NdbInterpretedCode instruction requires that table is set */
    m_error.code=4538;
    return -1;
  }

  const NdbDictionary::Column * col = 
    table->getColumn(AttrId);
  
  if(col == 0){
    /* Column is NULL */
    m_error.code= 4261;
    return -1;
  }
  
  if ((m_code->* branch)(value, len, AttrId, m_current.m_ownLabel) == -1)
    return propagateErrorFromCode();

  return 0;
}

int
NdbScanFilter::cmp(BinaryCondition cond, int ColId, 
		   const void *val, Uint32 len)
{
  switch(cond){
  case COND_LE:
    return m_impl.cond_col_const(Interpreter::LE, ColId, val, len);
  case COND_LT:
    return m_impl.cond_col_const(Interpreter::LT, ColId, val, len);
  case COND_GE:
    return m_impl.cond_col_const(Interpreter::GE, ColId, val, len);
  case COND_GT:
    return m_impl.cond_col_const(Interpreter::GT, ColId, val, len);
  case COND_EQ:
    return m_impl.cond_col_const(Interpreter::EQ, ColId, val, len);
  case COND_NE:
    return m_impl.cond_col_const(Interpreter::NE, ColId, val, len);
  case COND_LIKE:
    return m_impl.cond_col_const(Interpreter::LIKE, ColId, val, len);
  case COND_NOT_LIKE:
    return m_impl.cond_col_const(Interpreter::NOT_LIKE, ColId, val, len);
  case COND_AND_EQ_MASK:
    return m_impl.cond_col_const(Interpreter::AND_EQ_MASK, ColId, val, len);
  case COND_AND_NE_MASK:
    return m_impl.cond_col_const(Interpreter::AND_NE_MASK, ColId, val, len);
  case COND_AND_EQ_ZERO:
    return m_impl.cond_col_const(Interpreter::AND_EQ_ZERO, ColId, val, len);
  case COND_AND_NE_ZERO:
    return m_impl.cond_col_const(Interpreter::AND_NE_ZERO, ColId, val, len);
  }
  return -1;
}

static void
update(const NdbError & _err){
  NdbError & error = (NdbError &) _err;
  ndberror_struct ndberror = (ndberror_struct)error;
  ndberror_update(&ndberror);
  error = NdbError(ndberror);
}

const NdbError &
NdbScanFilter::getNdbError() const
{
  update(m_impl.m_error);
  return m_impl.m_error;
}

const NdbInterpretedCode*
NdbScanFilter::getInterpretedCode() const
{
  /* Return nothing if this is an old-style
   * ScanFilter as the InterpretedCode is 
   * entirely encapsulated
   */
  if (m_impl.m_associated_op != NULL)
    return NULL;

  return m_impl.m_code;
}

NdbOperation*
NdbScanFilter::getNdbOperation() const
{
  /* Return associated NdbOperation (or NULL
   * if we don't have one)
   */
  return m_impl.m_associated_op;
}

#if 0
int
main(void){
  if(0)
  {
    ndbout << "a > 7 AND b < 9 AND c = 4" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.lt(1, 9);
    f.eq(2, 4);
    f.end();
    ndbout << endl;
  }

  if(0)
  {
    ndbout << "a > 7 OR b < 9 OR c = 4" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::OR);
    f.gt(0, 7);
    f.lt(1, 9);
    f.eq(2, 4);
    f.end();
    ndbout << endl;
  }

  if(0)
  {
    ndbout << "a > 7 AND (b < 9 OR c = 4)" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.begin(NdbScanFilter::OR);
    f.lt(1, 9);
    f.eq(2, 4);
    f.end();
    f.end();
    ndbout << endl;
  }

  if(0)
  {
    ndbout << "a > 7 AND (b < 9 AND c = 4)" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.begin(NdbScanFilter::AND);
    f.lt(1, 9);
    f.eq(2, 4);
    f.end();
    f.end();
    ndbout << endl;
  }

  if(0)
  {
    ndbout << "(a > 7 AND b < 9) AND c = 4" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.lt(1, 9);
    f.end();
    f.eq(2, 4);
    f.end();
    ndbout << endl;
  }

  if(1)
  {
    ndbout << "(a > 7 OR b < 9) AND (c = 4 OR c = 5)" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.begin(NdbScanFilter::OR);
    f.gt(0, 7);
    f.lt(1, 9);
    f.end();
    f.begin(NdbScanFilter::OR);    
    f.eq(2, 4);
    f.eq(2, 5);
    f.end();
    f.end();
    ndbout << endl;
  }

  if(1)
  {
    ndbout << "(a > 7 AND b < 9) OR (c = 4 AND c = 5)" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::OR);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.lt(1, 9);
    f.end();
    f.begin(NdbScanFilter::AND);    
    f.eq(2, 4);
    f.eq(2, 5);
    f.end();
    f.end();
    ndbout << endl;
  }

  if(1)
  {
    ndbout << 
      "((a > 7 AND b < 9) OR (c = 4 AND d = 5)) AND " 
      "((e > 6 AND f < 8) OR (g = 2 AND h = 3)) "  << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.begin(NdbScanFilter::OR);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.lt(1, 9);
    f.end();
    f.begin(NdbScanFilter::AND);    
    f.eq(2, 4);
    f.eq(3, 5);
    f.end();
    f.end();

    f.begin(NdbScanFilter::OR);
    f.begin(NdbScanFilter::AND);
    f.gt(4, 6);
    f.lt(5, 8);
    f.end();
    f.begin(NdbScanFilter::AND);    
    f.eq(6, 2);
    f.eq(7, 3);
    f.end();
    f.end();
    f.end();
  }
  
  return 0;
}
#endif

template class Vector<NdbScanFilterImpl::State>;

