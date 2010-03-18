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

// Must include gtest first, since MySQL source has macros for min() etc ....
#include <gtest/gtest.h>

#include "thread_utils.h"

#include "mdl.h"

pthread_key(MEM_ROOT**,THR_MALLOC);
pthread_key(THD*, THR_THD);

extern "C" void sql_alloc_error_handler(void)
{
  ADD_FAILURE();
}

using thread::Mutex_lock;
using thread::Notification;
using thread::Thread;

namespace {

const int counter_start_value= 42;

class Notification_thread : public Thread
{
public:
  Notification_thread(Notification *start_notification,
                      Notification *end_notfication,
                      int *counter)
    : m_start_notification(start_notification),
      m_end_notification(end_notfication),
      m_counter(counter)
  {
  }

  virtual void run()
  {
    // Verify counter, increment it, notify the main thread.
    EXPECT_EQ(counter_start_value, *m_counter);
    (*m_counter)+= 1;
    m_start_notification->notify();

    // Wait for notification from other thread.
    m_end_notification->wait_for_notification();
    EXPECT_EQ(counter_start_value, *m_counter);

    // Set counter again before returning from thread.
    (*m_counter)+= 1;
  }

private:
  Notification *m_start_notification;
  Notification *m_end_notification;
  int          *m_counter;

  Notification_thread(const Notification_thread&); // Not copyable.
  void operator=(const Notification_thread&);      // Not assignable.
};


/*
  A basic, single-threaded test of Notification.
 */
TEST(Notification, notify)
{
  Notification notification;
  EXPECT_FALSE(notification.has_been_notified());
  notification.notify();
  EXPECT_TRUE(notification.has_been_notified());
}

/*
  Starts a thread, and verifies that the notification/synchronization
  mechanism works.
 */
TEST(Notification_thread, start_and_wait)
{
  Notification start_notification;
  Notification end_notfication;
  int counter= counter_start_value;
  Notification_thread
    notification_thread(&start_notification, &end_notfication, &counter);
  notification_thread.start();

  // Wait for the other thread to increment counter, and notify us.
  start_notification.wait_for_notification();
  EXPECT_EQ(counter_start_value + 1, counter);
  EXPECT_TRUE(start_notification.has_been_notified());

  // Reset counter, and notify other thread.
  counter= counter_start_value;
  end_notfication.notify();
  notification_thread.join();

  // We should see the final results of the thread we have joined.
  EXPECT_EQ(counter_start_value + 1, counter);
}

}  // namespace
