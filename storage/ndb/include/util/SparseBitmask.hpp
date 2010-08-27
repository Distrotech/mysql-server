/*
   Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.

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

#ifndef NDB_SPARSE_BITMASK_H
#define NDB_SPARSE_BITMASK_H

#include <ndb_global.h>
#include <util/Vector.hpp>

class SparseBitmask {
  unsigned m_max_size;
  Vector<unsigned> m_vec;

public:
  STATIC_CONST( NotFound = (unsigned)-1 );

  SparseBitmask(unsigned max_size = NotFound - 1) : m_max_size(max_size) {}

  unsigned max_size() const { return m_max_size; }

  /* Set bit n */
  void set(unsigned n) {
    assert(n <= m_max_size);

    unsigned i = m_vec.size();
    while (i > 0)
    {
      const unsigned j = m_vec[i-1];
      if (n == j)
        return; // Bit n already set

      if (j < n)
        break;
      i--;
    }

    m_vec.push(n, i);
  }

  /* Get bit n */
  bool get(unsigned n) const {
    assert(n <= m_max_size);

    for (unsigned i = 0; i < m_vec.size(); i++)
    {
      const unsigned j = m_vec[i];
      if (j < n)
        continue;

      return (j == n);
    }
    return false;
  }

  /* Clear bit n */
  int clear(unsigned n) {
    assert(n <= m_max_size);
    for (unsigned i = 0; i < m_vec.size(); i++)
    {
      const unsigned j = m_vec[i];
      if (j != n)
        continue;

      m_vec.erase(i);
      return 1;
    }
    return 0;
  }

  /* Clear all bits */
  void clear(void) {
    m_vec.clear();
  }

  /* Find first bit >= n */
  unsigned find(unsigned n) const {
    for (unsigned i = 0; i < m_vec.size(); i++)
    {
      const unsigned j = m_vec[i];
      if (j >= n)
        return j;
    }
    return NotFound;
  }

  /* Number of bits set */
  unsigned count() const { return m_vec.size(); }

  bool isclear() const { return count() == 0; }

  void print(void) const {
    for (unsigned i = 0; i < m_vec.size(); i++)
    {
      const unsigned j = m_vec[i];
      printf("[%u]: %u\n", i, j);
    }
  }

  /**
   * Parse string with numbers format
   *   1,2,3-5
   * @return -1 if unparsable chars found,
   *         -2 str has number > bitmask size
   *            else returns number of bits set
   */
  int parseMask(const char* src);

};

#endif
