/*
   Copyright (c) 2012 Oracle and/or its affiliates. All rights reserved.

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

#ifdef TAP_TEST

#include <assert.h>
#include <stdlib.h>

#include "NdbTap.hpp"
#include "md5_hash.hpp"
#include "random.h"
#include "LHLevel.hpp"

#ifndef UINT32_MAX
#define UINT32_MAX (4294967295U)
#endif

#define BUCKSIZE 3

struct elem
{
  Uint32 val;
  Uint16 head;
  static Uint32 hash(Uint32 val)
  {
    return md5_hash((Uint64*)&val, 1) /* | (1 << 31) */;
  }
};

void expand(LHLevel& lh, elem(*arr)[BUCKSIZE]);
bool shrink(LHLevel& lh, elem(*arr)[BUCKSIZE]);
bool insert_elem(LHLevel& lh, elem(*arr)[BUCKSIZE], Uint32 v);
bool delete_elem(LHLevel& lh, elem(*arr)[BUCKSIZE], Uint32 w);
int count_elem(LHLevel& lh, elem(*arr)[BUCKSIZE]);

Uint64 c_inserts = 0;
Uint64 c_expands = 0;
Uint64 c_shrinks = 0;
Uint64 c_deletes = 0;
Uint64 c_moved = 0;
Uint64 c_rehashed = 0;

int main(int argc, char *argv[])
{
  unsigned int nelem = argc > 1 ? atoi(argv[1]) : 1000000;
  plan(4);
  elem(*arr)[BUCKSIZE] = new elem[nelem][BUCKSIZE];
  bzero(arr, nelem * sizeof(elem[BUCKSIZE]));
  LHLevel lh;
  lh.clear();
  expand(lh, arr);
  Uint32 v = 0;
  myRandom48Init(nelem);
  for (int lap = 1; lap <= 2; lap++)
  {
    // Fill up table, with occasionally shrink
    for (;v < UINT32_MAX;v++)
    {
      if (lh.getSize() * (BUCKSIZE - 1) < c_inserts - c_deletes)
      {
        if (!lh.isFull() && lh.getSize() < nelem)
          expand(lh, arr);
        else
          break; /* Filled up */
      }
      insert_elem(lh, arr, v);
      if (rand() % 100 == 0)
        shrink(lh, arr);
    }

    // First lap, shrink to half
    // Second lap, delete all
    Uint32 lim = lh.getSize();
    lim = lap * lim / 2;
    while (v > 0 && lim > 0)
    {
      if (lh.isEmpty()) break;
      Uint32 w = (Uint32)myRandom48(v + 1);
      delete_elem(lh, arr, w);
      delete_elem(lh, arr, v);
      v--;
      if (lh.getSize() * BUCKSIZE * 3 > c_inserts - c_deletes)
        if (shrink(lh, arr))
          lim--;
    }

    // Check table consistency
    if (lap == 1)
    {
      int n = count_elem(lh, arr);
      ok((n >= 0), "all element hash values match stored hash value and bucket address");
      if (n < 0) n = -n;
      ok((c_inserts == c_deletes + n),
         "scanned element count (%u) matches difference between inserts (%llu) and deletes (%llu)",
         n, c_inserts, c_deletes);
    }
  }
  ok((c_inserts == c_deletes), "inserts (%llu) equals deletes (%llu)", c_inserts, c_deletes);
  ok((c_expands == c_shrinks), "expands (%llu) equals shrinks (%llu)", c_expands, c_shrinks);
  return exit_status();
}

bool delete_elem(LHLevel& lh, elem(*arr)[BUCKSIZE], Uint32 w)
{
  Uint32 hash(elem::hash(w));
  Uint32 addr = lh.getBucketNumber(hash);
  int i;
  bool found = false;
  for (i = 0; i < BUCKSIZE && (arr[addr][i].head != 0); i++)
  {
    if (arr[addr][i].val == w)
    {
      found = true;
      break;
    }
  }
  if (found)
  {
    assert(arr[addr][i].head > 0);
    int j;
    c_deletes += arr[addr][i].head;
    for (j = i + 1; j < BUCKSIZE; j++, i++)
      arr[addr][i] = arr[addr][j];
    bzero(&arr[addr][i], sizeof(arr[addr][i]));
    return true;
  }
  else if (i < BUCKSIZE)
  {
    assert(arr[addr][i].head == 0);
  }
  return false;
}

bool shrink(LHLevel& lh, elem(*arr)[BUCKSIZE])
{
  assert(!lh.isEmpty());
  Uint32 from;
  Uint32 to;
  if (!lh.getMergeBuckets(from, to))
  {
    int c = 0;
    for (int i = 0; i < BUCKSIZE && arr[from][i].head != 0; i++)
      c++;
    // Only shrink if the only bucket is empty
    if (c==0)
    {
      c_shrinks++;
      lh.shrink();
      return true;
    }
    return false;
  }
  assert(to < from);
  int i, j;
  int c = 0;
  for (i = 0; i < BUCKSIZE && arr[to][i].head != 0; i++)
    c++;
  for (j = 0; j < BUCKSIZE && arr[from][j].head != 0; j++)
    c++;
  // Only shrink if both buckets element can fit in one bucket
  if (c <= BUCKSIZE)
  {
    for (j = 0; j < BUCKSIZE && arr[from][j].head != 0; j++)
    {
      assert(i<BUCKSIZE);
      arr[to][i] = arr[from][j];
      bzero(&arr[from][j], sizeof(arr[from][j]));
      i++;
    }
    c_shrinks++;
    lh.shrink();
    return true;
  }
  return false;
}

void expand(LHLevel& lh, elem(*arr)[BUCKSIZE])
{
  assert(!lh.isFull());
  Uint32 from;
  Uint32 to;
  if (!lh.getSplitBucket(from, to))
  {
    // empty hash table, trivially expands to one bucket
    c_expands++;
    lh.expand();
    return;
  }
  int i, j, k;
  for (i = j = k = 0; i < BUCKSIZE && (arr[from][i].head != 0);
       i++)
  {
    Uint32 hash = Uint32(elem::hash(arr[from][i].val));
    if (lh.shouldMoveBeforeExpand(hash))
    {
      c_moved++;
      arr[to][j] = arr[from][i];
      j++;
    }
    else
    {
      if (k < i)
        arr[from][k] = arr[from][i];
      k++;
    }
  }
  for (; j < BUCKSIZE; j++)
    bzero(&arr[to][j], sizeof(arr[to][j]));
  for (; k < BUCKSIZE; k++)
    bzero(&arr[from][k], sizeof(arr[from][k]));
  lh.expand();
  c_expands++;
}

bool insert_elem(LHLevel& lh, elem(*arr)[BUCKSIZE], Uint32 v)
{
  Uint32 hash = Uint32(elem::hash(v));
  Uint32 addr = lh.getBucketNumber(hash);
  int i;
  bool found = false;
  for (i = 0; i < BUCKSIZE && (arr[addr][i].head != 0); i++)
  {
    if (arr[addr][i].val == v)
    {
      found = true;
      break;
    }
  }
  if (found)
  {
    arr[addr][i].head++;
    c_inserts++;
  }
  else if (i < BUCKSIZE)
  {
    arr[addr][i].head = 1;
    arr[addr][i].val = v;
    c_inserts++;
  }
  else
  {
    return false;
  }
  return true;
}

int count_elem(LHLevel& lh, elem(*arr)[BUCKSIZE])
{
  int elements = 0;
  int failures = 0;
  if (!lh.isEmpty()) for (Uint32 addr = 0; addr <= lh.getTop(); addr++)
    {
      for (int i = 0; i < BUCKSIZE && arr[addr][i].head != 0; i++)
      {
        elements++;
      }
    }
  return failures > 0 ? -elements : elements;
}

#endif
