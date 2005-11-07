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

#ifndef SLLIST_HPP
#define SLLIST_HPP

#include "ArrayPool.hpp"
#include <NdbOut.hpp>

/**
 * Template class used for implementing an
 *   list of object retreived from a pool
 */
template <class T, class U = T>
class SLList {
public:
  /**
   * List head
   */
  struct HeadPOD {
    Uint32 firstItem;
    void init() { firstItem = RNIL;}
  };

  struct Head : public HeadPOD {
    Head();
    Head& operator= (const HeadPOD& src) { 
      this->firstItem = src.firstItem;
      return *this;
    }
  };
  
  SLList(ArrayPool<T> & thePool);
  
  /**
   * Allocate an object from pool - update Ptr
   *
   * Return i
   */
  bool seize(Ptr<T> &);

  /**
   * Allocate object <b>i</b> from pool - update Ptr
   *
   * Return i
   */
  bool seizeId(Ptr<T> &, Uint32 i);

  /**
   * Allocate <b>n</b>objects from pool
   *
   * Return i value of first object allocated or RNIL if fails
   */
  bool seizeN(Ptr<T> &, Uint32 n);
  
  /**
   * Return all objects to the pool
   */
  void release();

  /**
   * Remove all object from list but don't return to pool
   */
  void remove();

  /**
   *  Update i & p value according to <b>i</b>
   */
  void getPtr(Ptr<T> &, Uint32 i) const;
  
  /**
   * Update p value for ptr according to i value 
   */
  void getPtr(Ptr<T> &) const ;
  
  /**
   * Get pointer for i value
   */
  T * getPtr(Uint32 i) const ;

  /**
   * Update ptr to first element in list
   *
   * Return i
   */
  bool first(Ptr<T> &) const ;

  /**
   * Get next element
   *
   * NOTE ptr must be both p & i
   */
  bool next(Ptr<T> &) const ;
  
  /**
   * Check if next exists
   *
   * NOTE ptr must be both p & i
   */
  bool hasNext(const Ptr<T> &) const;

  /**
   * Add
   */
  void add(Ptr<T> & p){
    p.p->U::nextList = head.firstItem;
    head.firstItem = p.i;
  }

  Uint32 noOfElements() const {
    Uint32 c = 0;
    Uint32 i = head.firstItem;
    while(i != RNIL){
      c++;
      const T * t = thePool.getPtr(i);
      i = t->U::nextList;
    }
    return c;
  }

  /**
   * Print
   * (Run operator NdbOut<< on every element)
   */
  void print(NdbOut & out) {
    out << "firstItem = " << head.firstItem << endl;
    Uint32 i = head.firstItem;
    while(i != RNIL){
      T * t = thePool.getPtr(i);
      t->print(out); out << " ";
      i = t->next;
    }
  }

  inline bool empty() const { return head.firstItem == RNIL;}

protected:
  Head head;
  ArrayPool<T> & thePool;
};

template <class T, class U = T>
class LocalSLList : public SLList<T,U> {
public:
  LocalSLList(ArrayPool<T> & thePool, typename SLList<T,U>::HeadPOD & _src)
    : SLList<T,U>(thePool), src(_src)
  {
    this->head = src;
  }
  
  ~LocalSLList(){
    src = this->head;
  }
private:
  typename SLList<T,U>::HeadPOD & src;
};

template <class T, class U>
inline
SLList<T,U>::SLList(ArrayPool<T> & _pool):
  thePool(_pool){
}

template <class T, class U>
inline
SLList<T,U>::Head::Head(){
  this->init();
}

template <class T, class U>
inline
bool
SLList<T,U>::seize(Ptr<T> & p){
  thePool.seize(p);
  T * t = p.p;
  Uint32 ff = head.firstItem;
  if(p.i != RNIL){
    t->U::nextList = ff;
    head.firstItem = p.i;
    return true;
  }
  return false;
}

template <class T, class U>
inline
bool
SLList<T,U>::seizeId(Ptr<T> & p, Uint32 ir){
  thePool.seizeId(p, ir);
  T * t = p.p;
  Uint32 ff = head.firstItem;
  if(p.i != RNIL){
    t->U::nextList = ff;
    head.firstItem = p.i;
    return true;
  }
  return false;
}

template <class T, class U>
inline
bool
SLList<T,U>::seizeN(Ptr<T> & p, Uint32 n){
  for(Uint32 i = 0; i < n; i++){
    if(seize(p) == RNIL){
      /**
       * Failure
       */
      for(; i > 0; i--){
	const Uint32 tmp = head.firstItem;
	const T * t = thePool.getPtr(tmp);
	head.firstItem = t->U::nextList;
	thePool.release(tmp);
      }
      return false;
    }
  }    

  /**
   * Success
   */
  p.i = head.firstItem;
  p.p = thePool.getPtr(head.firstItem);
  
  return true;
}


template <class T, class U>
inline
void 
SLList<T,U>::remove(){
  head.firstItem = RNIL;
}  

template <class T, class U>
inline
void 
SLList<T,U>::release(){
  while(head.firstItem != RNIL){
    const T * t = thePool.getPtr(head.firstItem);
    const Uint32 i = head.firstItem;
    head.firstItem = t->U::nextList;
    thePool.release(i);
  }
}  

template <class T, class U>
inline
void 
SLList<T,U>::getPtr(Ptr<T> & p, Uint32 i) const {
  p.i = i;
  p.p = thePool.getPtr(i);
}

template <class T, class U>
inline
void 
SLList<T,U>::getPtr(Ptr<T> & p) const {
  thePool.getPtr(p);
}
  
template <class T, class U>
inline
T * 
SLList<T,U>::getPtr(Uint32 i) const {
  return thePool.getPtr(i);
}

/**
 * Update ptr to first element in list
 *
 * Return i
 */
template <class T, class U>
inline
bool
SLList<T,U>::first(Ptr<T> & p) const {
  Uint32 i = head.firstItem;
  p.i = i;
  if(i != RNIL){
    p.p = thePool.getPtr(i);
    return true;
  }
  p.p = NULL;
  return false;
}

template <class T, class U>
inline
bool
SLList<T,U>::next(Ptr<T> & p) const {
  Uint32 i = p.p->U::nextList;
  p.i = i;
  if(i != RNIL){
    p.p = thePool.getPtr(i);
    return true;
  }
  p.p = NULL;
  return false;
}

template <class T, class U>
inline
bool
SLList<T,U>::hasNext(const Ptr<T> & p) const {
  return p.p->U::nextList != RNIL;
}

#endif
