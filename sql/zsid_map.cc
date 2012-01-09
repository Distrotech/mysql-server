/* Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.

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


#include "zgtids.h"


#ifdef HAVE_GTID


#include "hash.h"
#include "mysqld_error.h"


Sid_map::Sid_map(Checkable_rwlock *_sid_lock)
  : sid_lock(_sid_lock)
{
  DBUG_ENTER("Sid_map::Sid_map");
  my_init_dynamic_array(&_sidno_to_sid, sizeof(Node *), 8, 8);
  my_init_dynamic_array(&_sorted, sizeof(rpl_sidno), 8, 8);
  my_hash_init(&_sid_to_sidno, &my_charset_bin, 20,
               offsetof(Node, sid.bytes), Uuid::BYTE_LENGTH, NULL,
               my_free, 0);
  DBUG_VOID_RETURN;
}


Sid_map::~Sid_map()
{
  DBUG_ENTER("Sid_map::~Sid_map");
  delete_dynamic(&_sidno_to_sid);
  delete_dynamic(&_sorted);
  my_hash_free(&_sid_to_sidno);
  DBUG_VOID_RETURN;
}


enum_return_status Sid_map::clear()
{
  DBUG_ENTER("Sid_map::clear");
  my_hash_free(&_sid_to_sidno);
  my_hash_init(&_sid_to_sidno, &my_charset_bin, 20,
               offsetof(Node, sid.bytes), Uuid::BYTE_LENGTH, NULL,
               my_free, 0);
  reset_dynamic(&_sidno_to_sid);
  reset_dynamic(&_sorted);
  RETURN_OK;
}

rpl_sidno Sid_map::add_sid(const rpl_sid *sid)
{
  DBUG_ENTER("Sid_map::add_sid(const rpl_sid *)");
#ifndef DBUG_OFF
  char buf[Uuid::TEXT_LENGTH + 1];
  sid->to_string(buf);
  DBUG_PRINT("info", ("SID=%s", buf));
#endif
  if (sid_lock)
    sid_lock->assert_some_lock();
  Node *node= (Node *)my_hash_search(&_sid_to_sidno, sid->bytes,
                                     rpl_sid::BYTE_LENGTH);
  if (node != NULL)
  {
    DBUG_PRINT("info", ("existed as sidno=%d", node->sidno));
    DBUG_RETURN(node->sidno);
  }

  bool is_wrlock= false;
  if (sid_lock)
  {
    is_wrlock= sid_lock->is_wrlock();
    if (!is_wrlock)
    {
      sid_lock->unlock();
      sid_lock->wrlock();
    }
  }
  DBUG_PRINT("info", ("is_wrlock=%d sid_lock=%p", is_wrlock, sid_lock));
  rpl_sidno sidno;
  node= (Node *)my_hash_search(&_sid_to_sidno, sid->bytes,
                               rpl_sid::BYTE_LENGTH);
  if (node != NULL)
    sidno= node->sidno;
  else
  {
    sidno= get_max_sidno() + 1;
    if (add_node(sidno, sid) != RETURN_STATUS_OK
#ifdef MYSQL_SERVER
        /*
          If this is the global_sid_map, we take the opportunity to
          resize all arrays in gtid_state while holding the wrlock.
        */
        || (this == &global_sid_map && 
            gtid_state.ensure_sidno() != RETURN_STATUS_OK)
#endif
        )
      sidno= -1;
    /// @todo: remove node on write error /sven
  }

  if (sid_lock)
  {
    if (!is_wrlock)
    {
      sid_lock->unlock();
      sid_lock->rdlock();
    }
  }
  DBUG_RETURN(sidno);
}

enum_return_status Sid_map::add_node(rpl_sidno sidno, const rpl_sid *sid)
{
  DBUG_ENTER("Sid_map::add_node(rpl_sidno, const rpl_sid *)");
  if (sid_lock)
    sid_lock->assert_some_wrlock();
  Node *node= (Node *)malloc(sizeof(Node));
  if (node != NULL)
  {
    node->sidno= sidno;
    node->sid= *sid;
    if (insert_dynamic(&_sidno_to_sid, &node) == 0)
    {
      if (insert_dynamic(&_sorted, &sidno) == 0)
      {
        if (my_hash_insert(&_sid_to_sidno, (uchar *)node) == 0)
        {
          // We have added one element to the end of _sorted.  Now we
          // bubble it down to the sorted position.
          int sorted_i= sidno - 1;
          rpl_sidno *prev_sorted_p= dynamic_element(&_sorted, sorted_i,
                                                    rpl_sidno *);
          sorted_i--;
          while (sorted_i >= 0)
          {
            rpl_sidno *sorted_p= dynamic_element(&_sorted, sorted_i,
                                                 rpl_sidno *);
            const rpl_sid *other_sid= sidno_to_sid(*sorted_p);
            if (memcmp(sid->bytes, other_sid->bytes,
                       rpl_sid::BYTE_LENGTH) >= 0)
              break;
            memcpy(prev_sorted_p, sorted_p, sizeof(rpl_sidno));
            sorted_i--;
            prev_sorted_p= sorted_p;
          }
          memcpy(prev_sorted_p, &sidno, sizeof(rpl_sidno));
          RETURN_OK;
        }
        pop_dynamic(&_sorted);
      }
      pop_dynamic(&_sidno_to_sid);
    }
    free(node);
  }
  BINLOG_ERROR(("Out of memory."), (ER_OUT_OF_RESOURCES, MYF(0)));
  RETURN_REPORTED_ERROR;
}

#endif /* HAVE_GTID */
