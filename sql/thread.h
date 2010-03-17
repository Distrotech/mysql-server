/* Copyright (C) 2009 Sun Microsystems, Inc.

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

#ifndef SQL_THREAD_INCLUDED
#define SQL_THREAD_INCLUDED

#include <my_global.h>
#include <my_pthread.h>

namespace thread {

/*
  An abstract class for creating/running/joining threads.
  Thread::start() will create a new pthread, and execute the run() function.
*/
class Thread
{
public:
  Thread() : m_thread_id(0) {}
  virtual ~Thread();

  // Options for starting the thread.
  struct Options
  {
    Options() : m_detached(false), m_stack_size(PTHREAD_STACK_MIN) {}

    void set_datched(bool val) { m_detached= val; }
    bool detached() const { return m_detached; }

    void   set_stack_size(size_t val) { m_stack_size= val; }
    size_t stack_size() const { return m_stack_size; }
  private:
    bool   m_detached;
    size_t m_stack_size;
  };

  /*
    Will create a new pthread, and invoke run();
    Returns the value from pthread_create().
  */
  int start(const Options &options);

  /*
    You may invoke this to wait for the thread to finish.
    You cannot join() a thread which runs in detached mode.
  */
  void join();

  // The id of the thread (valid only if it is actually running).
  pthread_t thread_id() const { return m_thread_id; }

  /*
    A wrapper for the run() function.
    Users should *not* call this function directly, they should rather
    invoke the start() function.
  */
  static void run_wrapper(Thread*);

protected:
  /*
    Define this function in derived classes.
    Users should *not* call this function directly, they should rather
    invoke the start() function.
  */
  virtual void run() = 0;

private:
  pthread_t m_thread_id;
  Options   m_options;

  Thread(const Thread&);                        /* Not copyable. */
  void operator=(const Thread&);                /* Not assignable. */
};


// A simple wrapper around a mutex:
// Grabs the mutex in the CTOR, releases it in the DTOR.
class Mutex_lock
{
public:
  Mutex_lock(pthread_mutex_t *mutex);
  ~Mutex_lock();
private:
  pthread_mutex_t *m_mutex;

  Mutex_lock(const Mutex_lock&);                /* Not copyable. */
  void operator=(const Mutex_lock&);            /* Not assignable. */
};


// A barrier which can be used for one-time synchronization between threads.
class Notification
{
public:
  Notification();
  ~Notification();

  bool has_been_notified();
  void wait_for_notification();
  void notify();
private:
  bool            m_notified;
  pthread_cond_t  m_cond;
  pthread_mutex_t m_mutex;

  Notification(const Notification&);            /* Not copyable. */
  void operator=(const Notification&);          /* Not assignable. */
};

}  // namespace thread

#endif  // SQL_THREAD_INCLUDED
