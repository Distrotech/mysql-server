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

#ifndef HUGO_QUERY_BUILDER_HPP
#define HUGO_QUERY_BUILDER_HPP

#include <NDBT.hpp>
#include <Vector.hpp>
#include <NdbQueryBuilder.hpp>

class HugoQueryBuilder {
public:

  /**
   * Options that affects what kind of query is built
   */
  enum QueryOption
  {
    /**
     * Query should be a lookup
     */
    O_LOOKUP = 0x1,

    /**
     * Query should be a scan
     */
    O_SCAN = 0x2,

    /**
     * Query might use primary key index
     */
    O_PK_INDEX = 0x4,

    /**
     * Query might use unique index
     */
    O_UNIQUE_INDEX = 0x8,

    /**
     * Query might use ordered index
     */
    O_ORDERED_INDEX = 0x10,

    /**
     * Query might table scan
     */
    O_TABLE_SCAN = 0x20,

    /**
     * If not any options set, random query qill be created
     */
    O_RANDOM = 0
  };
  typedef Uint64 OptionMask;

  HugoQueryBuilder(Ndb* ndb, const NdbDictionary::Table**tabptr, OptionMask om){
    init();
    for (; * tabptr != 0; tabptr++)
      addTable(ndb, * tabptr);
    setOptionMask(om);
    fixOptions();
  }
  HugoQueryBuilder(Ndb* ndb, const NdbDictionary::Table* tab, QueryOption o) {
    init();
    addTable(ndb, tab);
    setOption(o);
    fixOptions();
  }
  virtual ~HugoQueryBuilder();

  void setMinJoinLevel(int level) { m_joinLevel[0] = level;}
  int getMinJoinLevel() const { return m_joinLevel[0];}
  void setMaxJoinLevel(int level) { m_joinLevel[1] = level;}
  int getMaxJoinLevel() const { return m_joinLevel[1];}

  void setJoinLevel(int level) { setMinJoinLevel(level);setMaxJoinLevel(level);}
  int getJoinLevel() const;

  void addTable(Ndb*, const NdbDictionary::Table*);
  void removeTable(const NdbDictionary::Table*);

  void setOption(QueryOption o) { m_options |= (OptionMask)o;}
  void clearOption(QueryOption o) const { m_options &= ~(OptionMask)o;}
  bool testOption(QueryOption o) const { return (m_options & o) != 0;}

  OptionMask getOptionMask() const { return m_options;}
  void setOptionMask(OptionMask om) { m_options = om;}

  const NdbQueryDef * createQuery(Ndb*, bool takeOwnership = false);

private:
  struct TableDef
  {
    const NdbDictionary::Table * m_table;
    Vector<const NdbDictionary::Index*> m_unique_indexes;
    Vector<const NdbDictionary::Index*> m_ordered_indexes;
  };

  void init();
  mutable OptionMask m_options;
  int m_joinLevel[2]; // min/max
  Vector<TableDef> m_tables;
  Vector<const NdbQueryDef*> m_queries;

  // get random table
  struct TableDef getTable() const;

  struct OpIdx
  {
    NdbQueryOperationDef::Type m_type;
    const NdbDictionary::Table * m_table;
    const NdbDictionary::Index * m_index;
  };
  OpIdx getOp() const;

  struct Op
  {
    int m_parent;
    int m_idx;
    NdbQueryOperationDef * m_op;
  };

  Vector<Op> m_query; // Query built sofar

  /**
   * Check if all column in cols can be bound to a column in tables in
   *   ops
   */
  static bool checkBindable(Vector<const NdbDictionary::Column*> cols,
                            Vector<Op> ops);

  Vector<Op> getParents(OpIdx); //
  NdbQueryOperand * createLink(NdbQueryBuilder&, const NdbDictionary::Column*,
                               Vector<Op> & parents);
  NdbQueryOperationDef* createOp(NdbQueryBuilder&);

  void fixOptions();
};

#endif
