/*
   Copyright (C) 2009 Sun Microsystems Inc
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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef NdbQueryBuilder_H
#define NdbQueryBuilder_H

#include <stdlib.h>
#include <ndb_types.h>

#include "NdbError.hpp"
#include "NdbDictionary.hpp"

class Ndb;
class NdbRecord;
class NdbQueryDef;
class NdbQueryDefImpl;
class NdbQueryBuilderImpl;
class NdbQueryOperandImpl;
class NdbQueryOperationDefImpl;

/**
 * This is the API interface for building a (composite) query definition,
 * possibly existing of multiple operations linked together (aka 'joined')
 *
 * A query mainly consist of two types of objects:
 *  - NdbQueryOperationDef defines a lookup, or scan on a single table.
 *  - NdbQueryOperand defines a single value which may be used to
 *    define a key, filter or bound on a NdbQueryOperationDef.
 *
 * Construction of these objects are through the NdbQueryBuilder factory.
 * To enforce this restriction, c'tor, d'tor operator
 * for the NdbQuery objects has been declared 'private'.
 * NdbQuery objects should not be copied - Copy constructor and assignment
 * operand has been private declared to enforce this restriction.
 *
 */

/**
 * NdbQueryOperand, a construct for specifying values which are used 
 * to specify lookup keys, bounds or filters in the query tree.
 */
class NdbQueryOperand  // A base class specifying a single value
{
public:
  // Column which this operand relates to
  const NdbDictionary::Column* getColumn() const;
  NdbQueryOperandImpl& getImpl() const;

protected:
  // Enforce object creation through NdbQueryBuilder factory 
  explicit NdbQueryOperand(NdbQueryOperandImpl& impl);
  ~NdbQueryOperand();

private:
  // Copying disallowed:
  NdbQueryOperand(const NdbQueryOperand& other);
  NdbQueryOperand& operator = (const NdbQueryOperand& other);

  NdbQueryOperandImpl& m_impl;
};

// A NdbQueryOperand is either of these:
class NdbConstOperand : public NdbQueryOperand
{
private:
  friend class NdbConstOperandImpl;
  explicit NdbConstOperand(NdbQueryOperandImpl& impl);
  ~NdbConstOperand();
};

class NdbLinkedOperand : public NdbQueryOperand
{
private:
  friend class NdbLinkedOperandImpl;
  explicit NdbLinkedOperand(NdbQueryOperandImpl& impl);
  ~NdbLinkedOperand();
};

class NdbParamOperand  : public NdbQueryOperand {
public:
  const char* getName() const;
  Uint32 getEnum() const;

private:
  friend class NdbParamOperandImpl;
  explicit NdbParamOperand(NdbQueryOperandImpl& impl);
  ~NdbParamOperand();
};



/**
 * NdbQueryOperationDef defines an operation on a single NDB table
 */
class NdbQueryOperationDef // Base class for all operation definitions
{
public:

  Uint32 getNoOfParentOperations() const;
  const NdbQueryOperationDef* getParentOperation(Uint32 i) const;

  Uint32 getNoOfChildOperations() const;
  const NdbQueryOperationDef* getChildOperation(Uint32 i) const;

  /**
   * Get table object for this operation
   */
  const NdbDictionary::Table* getTable() const;
  NdbQueryOperationDefImpl& getImpl() const;

protected:
  // Enforce object creation through NdbQueryBuilder factory 
  explicit NdbQueryOperationDef(NdbQueryOperationDefImpl& impl);
  ~NdbQueryOperationDef();

private:
  // Copying disallowed:
  NdbQueryOperationDef(const NdbQueryOperationDef& other);
  NdbQueryOperationDef& operator = (const NdbQueryOperationDef& other);

  NdbQueryOperationDefImpl& m_impl;
}; // class NdbQueryOperationDef


class NdbQueryLookupOperationDef : public NdbQueryOperationDef
{
public:
  /**
   * Get possible index object for this operation
   */
  const NdbDictionary::Index* getIndex() const;

private:
  // Enforce object creation through NdbQueryBuilder factory
  friend class NdbQueryLookupOperationDefImpl;
  explicit NdbQueryLookupOperationDef(NdbQueryOperationDefImpl& impl);
  ~NdbQueryLookupOperationDef();
}; // class NdbQueryLookupOperationDef

class NdbQueryScanOperationDef : public NdbQueryOperationDef  // Base class for scans
{
protected:
  // Enforce object creation through NdbQueryBuilder factory 
  explicit NdbQueryScanOperationDef(NdbQueryOperationDefImpl& impl);
  ~NdbQueryScanOperationDef();
}; // class NdbQueryScanOperationDef

class NdbQueryTableScanOperationDef : public NdbQueryScanOperationDef
{
private:
  // Enforce object creation through NdbQueryBuilder factory 
  friend class NdbQueryTableScanOperationDefImpl;
  explicit NdbQueryTableScanOperationDef(NdbQueryOperationDefImpl& impl);
  ~NdbQueryTableScanOperationDef();
}; // class NdbQueryTableScanOperationDef

/** Ordering of scan results when scanning ordered indexes.*/
enum NdbScanOrdering
{
  /** Undefined (not yet set). */
  NdbScanOrdering_void, 
  /** Results will not be ordered.*/
  NdbScanOrdering_unordered, 
  NdbScanOrdering_ascending,
  NdbScanOrdering_descending
};

class NdbQueryIndexScanOperationDef : public NdbQueryScanOperationDef
{
public:
  /** Define result ordering. Alternatively, ordering may be set when the 
   * query definition has been instantiated, using 
   * NdbQueryOperation::setOrdering(). It is an error to call this method 
   * after NdbQueryBuilder::prepare() has been called for the enclosing query.
   * @param ordering The desired ordering of results.
   * @return 0 if ok, -1 in case of error.
   */
  int setOrdering(NdbScanOrdering ordering);

  /** Get the result ordering for this operation.*/
  NdbScanOrdering getOrdering() const;

private:
  // Enforce object creation through NdbQueryBuilder factory 
  friend class NdbQueryIndexScanOperationDefImpl;
  explicit NdbQueryIndexScanOperationDef(NdbQueryOperationDefImpl& impl);
  ~NdbQueryIndexScanOperationDef();
}; // class NdbQueryIndexScanOperationDef


/**
 * class NdbQueryIndexBound is an argument container for defining
 * a NdbQueryIndexScanOperationDef.
 * The contents of this object is copied into the
 * NdbQueryIndexScanOperationDef and does not have to be 
 * persistent after the NdbQueryBuilder::scanIndex() call
 */
class NdbQueryIndexBound
{
public:
  // C'tor for an equal bound:
  NdbQueryIndexBound(const NdbQueryOperand* const *eqKey)
   : m_low(eqKey), m_lowInclusive(true), m_high(eqKey), m_highInclusive(true)
  {};

  // C'tor for a normal range including low & high limit:
  NdbQueryIndexBound(const NdbQueryOperand* const *low,
                     const NdbQueryOperand* const *high)
   : m_low(low), m_lowInclusive(true), m_high(high), m_highInclusive(true)
  {};

  // Complete C'tor where limits might be exluded:
  NdbQueryIndexBound(const NdbQueryOperand* const *low,  bool lowIncl,
                     const NdbQueryOperand* const *high, bool highIncl)
   : m_low(low), m_lowInclusive(lowIncl), m_high(high), m_highInclusive(highIncl)
  {}

private:
  friend class NdbQueryIndexScanOperationDefImpl;
  const NdbQueryOperand* const *m_low;  // 'Pointer to array of pointers', NULL terminated
  const bool m_lowInclusive;
  const NdbQueryOperand* const *m_high; // 'Pointer to array of pointers', NULL terminated
  const bool m_highInclusive;
};


/**
 *
 * The Query builder constructs a NdbQueryDef which is a collection of
 * (possibly linked) NdbQueryOperationDefs
 * Each NdbQueryOperationDef may use NdbQueryOperands to specify keys and bounds.
 *
 * LIFETIME:
 * - All NdbQueryOperand and NdbQueryOperationDef objects created in the 
 *   context of a NdbQueryBuilder has a lifetime restricted by:
 *    1. The NdbQueryDef created by the ::prepare() methode.
 *    2. The NdbQueryBuilder *if* the builder is destructed before the
 *       query was prepared.

 *   A single NdbQueryOperand or NdbQueryOperationDef object may be 
 *   used/referrer multiple times during the build process whenever
 *   we need a reference to the same value/node during the 
 *   build phase.
 *
 * - The NdbQueryDef produced by the ::prepare() method has a lifetime 
 *   determined by the Ndb object, or until it is explicit released by
 *   NdbQueryDef::release()
 *  
 */
class NdbQueryBuilder 
{
public:
  explicit NdbQueryBuilder(Ndb&);    // Or getQueryBuilder() from Ndb..
 ~NdbQueryBuilder();

  const NdbQueryDef* prepare();    // Complete building a queryTree from 'this' NdbQueryBuilder

  // NdbQueryOperand builders:
  // ::constValue constructors variants, considder to added/removed variants
  // If the attribute is of a fixed size datatype, its value must include all bytes.
  // A fixed-Char value must be native-blank padded
  // Partly based on value types currently supported through NdbOperation::equal()
  NdbConstOperand* constValue(Int32  value); 
  NdbConstOperand* constValue(Uint32 value); 
  NdbConstOperand* constValue(Int64  value); 
  NdbConstOperand* constValue(Uint64 value); 
  NdbConstOperand* constValue(double value); 
  NdbConstOperand* constValue(const char* value);  // Null terminated char/varchar C-type string

  // Raw data with specified length, with src type as specified by 'record.column[attrId]'.
  // Provide very basic type check to match destination column it is
  // used against.
  NdbConstOperand* constValue(const void* rowptr,
                              const NdbRecord* record, Uint32 attrId);

  // ::paramValue()
  NdbParamOperand* paramValue(const char* name = 0);  // Parameterized

  NdbLinkedOperand* linkedValue(const NdbQueryOperationDef*, const char* attr); // Linked value


  // NdbQueryOperationDef builders:
  //
  // Common argument 'ident' may be used to identify each NdbQueryOperationDef with a name.
  // This may later be used to find the corresponding NdbQueryOperation instance when
  // the NdbQueryDef is executed. 
  // Each NdbQueryOperationDef will also be assigned an numeric ident (starting from 0)
  // as an alternative way of locating the NdbQueryOperation.
  
  NdbQueryLookupOperationDef* readTuple(
                                const NdbDictionary::Table*,          // Primary key lookup
				const NdbQueryOperand* const keys[],  // Terminated by NULL element 
                                const char* ident = 0);

  NdbQueryLookupOperationDef* readTuple(
                                const NdbDictionary::Index*,          // Unique key lookup w/ index
			        const NdbDictionary::Table*,
				const NdbQueryOperand* const keys[],  // Terminated by NULL element 
                                const char* ident = 0);

  NdbQueryTableScanOperationDef* scanTable(
                                const NdbDictionary::Table*,
                                const char* ident = 0);

  NdbQueryIndexScanOperationDef* scanIndex(
                                const NdbDictionary::Index*, 
	                        const NdbDictionary::Table*,
                                const NdbQueryIndexBound* bound = 0,
                                const char* ident = 0);


  /** 
   * @name Error Handling
   * @{
   */

  /**
   * Get error object with information about the latest error.
   *
   * @return An error object with information about the latest error.
   */
  const NdbError& getNdbError() const;

  NdbQueryBuilderImpl& getImpl() const;

private:
  NdbQueryBuilderImpl* const m_pimpl;

}; // class NdbQueryBuilder

/**
 * NdbQueryDef represents a ::prepare()'d object from NdbQueryBuilder.
 *
 * The NdbQueryDef is reusable in the sense that it may be executed multiple
 * times. Its lifetime is defined by the Ndb object which it was created with,
 * or it may be explicitely released() when no longer required.
 *
 * The NdbQueryDef *must* be keept alive until the last thread
 * which executing a query based on this NdbQueryDef has completed execution 
 * *and* result handling. Used from multiple threads this implies either:
 *
 *  - Keep the NdbQueryDef until all threads terminates.
 *  - Implement reference counting on the NdbQueryDef.
 *  - Use the supplied copy constructor to give each thread its own copy
 *    of the NdbQueryDef.
 *
 * A NdbQueryDef is scheduled for execution by appending it to an open 
 * transaction - optionally together with a set of parameters specifying 
 * the actuall values required by ::execute() (ie. Lookup an bind keys).
 *
 */
class NdbQueryDef
{
  friend class NdbQueryDefImpl;

public:

  // Copy construction of the NdbQueryDef IS defined.
  // May be convenient to take a copy when the same query is used from
  // multiple threads.
  NdbQueryDef(const NdbQueryDef& other);
  NdbQueryDef& operator = (const NdbQueryDef& other);

  Uint32 getNoOfOperations() const;

  // Get a specific NdbQueryOperationDef by ident specified
  // when the NdbQueryOperationDef was created.
  const NdbQueryOperationDef* getQueryOperation(const char* ident) const;
  const NdbQueryOperationDef* getQueryOperation(Uint32 index) const;

  // A scan query may return multiple rows, and may be ::close'ed when
  // the client has completed access to it.
  bool isScanQuery() const;

  // Remove this NdbQueryDef including operation and operands it contains
  void release() const;

  NdbQueryDefImpl& getImpl() const;

private:
  NdbQueryDefImpl& m_impl;

  explicit NdbQueryDef(NdbQueryDefImpl& impl);
  ~NdbQueryDef();
};


#endif
