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

#include "zgroups.h"
#include "sql_class.h"
#include "binlog.h"


#ifdef HAVE_GTID


Group_cache::Group_cache()
{
  DBUG_ENTER("Group_cache::Group_cache");
  my_init_dynamic_array(&subgroups, sizeof(Cached_subgroup), 8, 8);
  DBUG_VOID_RETURN;
}


Group_cache::~Group_cache()
{
  DBUG_ENTER("Group_cache::~Group_cache");
  delete_dynamic(&subgroups);
  DBUG_VOID_RETURN;
}


void Group_cache::clear()
{
  DBUG_ENTER("Group_cache::clear");
  subgroups.elements= 0;
  DBUG_VOID_RETURN;
}


enum_return_status Group_cache::add_subgroup(const Cached_subgroup *group)
{
  DBUG_ENTER("Group_cache::add_subgroup(Cached_Subgroup *)");

  // if possible, merge the subgroup with previous subgroup in the cache
  int n_subgroups= get_n_subgroups();
  if (n_subgroups > 0)
  {
    Cached_subgroup *prev= get_unsafe_pointer(n_subgroups - 1);
    if ((prev->type == group->type || 
         (prev->type == NORMAL_SUBGROUP && group->type == EMPTY_SUBGROUP) ||
         (prev->type == EMPTY_SUBGROUP && group->type == NORMAL_SUBGROUP)) &&
         prev->sidno == group->sidno &&
         prev->gno == group->gno)
    {
      prev->binlog_length += group->binlog_length;
      prev->group_end= group->group_end;
      if (prev->type == EMPTY_SUBGROUP && group->type == NORMAL_SUBGROUP)
        prev->type= NORMAL_SUBGROUP;
      RETURN_OK;
    }
  }

  // if sub-group could not be merged with previous sub-group, append it
  if (insert_dynamic(&subgroups, group) != 0)
  {
    BINLOG_ERROR(("Out of memory."), (ER_OUT_OF_RESOURCES, MYF(0)));
    RETURN_REPORTED_ERROR;
  }

  // Update the internal status of this Group_cache (see comment above
  // definition of enum_group_cache_type).
  if (group->type == EMPTY_SUBGROUP || group->type == NORMAL_SUBGROUP)
  {
    if (group->group_end)
    {
      /*
        @todo: currently group_is_ended() requires a linear scan
        through the cache. if this becomes a performance problem, we
        can add a GTID_set Group_cache::ended_groups and add ended
        groups to it here. /Sven
      */
    }
  }
  RETURN_OK;
}


enum_return_status
Group_cache::add_logged_subgroup(const THD *thd, my_off_t length)
{
  DBUG_ENTER("Group_cache::add_logged_subgroup(THD *, my_off_t)");
  const Gtid_specification *spec= &thd->variables.gtid_next;
  Gtid_specification::enum_type type= spec->type;
  Cached_subgroup cs=
    {
      type == Gtid_specification::ANONYMOUS ? ANONYMOUS_SUBGROUP :
      NORMAL_SUBGROUP,
      spec->group.sidno,
      spec->group.gno,
      length,
      thd->variables.gtid_end
    };
  if (type == Gtid_specification::AUTOMATIC && spec->group.sidno == 0)
    cs.sidno= mysql_bin_log.server_uuid_sidno;
  PROPAGATE_REPORTED_ERROR(add_subgroup(&cs));
  RETURN_OK;
}


bool Group_cache::contains_group(rpl_sidno sidno, rpl_gno gno) const
{
  int n_subgroups= get_n_subgroups();
  for (int i= 0; i < n_subgroups; i++)
  {
    const Cached_subgroup *cs= get_unsafe_pointer(i);
    if ((cs->type == NORMAL_SUBGROUP || cs->type == EMPTY_SUBGROUP) &&
        cs->gno == gno && cs->sidno == sidno)
      return true;
  }
  return false;
}


bool Group_cache::group_is_ended(rpl_sidno sidno, rpl_gno gno) const
{
  int n_subgroups= get_n_subgroups();
  for (int i= 0; i < n_subgroups; i++)
  {
    const Cached_subgroup *cs= get_unsafe_pointer(i);
    if (cs->gno == gno && cs->sidno == sidno && cs->group_end)
      return true;
  }
  return false;
}


enum_return_status
Group_cache::add_empty_subgroup(rpl_sidno sidno, rpl_gno gno, bool group_end)
{
  DBUG_ENTER("Group_cache::add_empty_subgroup");
  Cached_subgroup cs=
    {
      EMPTY_SUBGROUP, sidno, gno, 0/*binlog_length*/, group_end
    };
  PROPAGATE_REPORTED_ERROR(add_subgroup(&cs));
  RETURN_OK;
}

enum_return_status
Group_cache::add_empty_subgroup_if_missing(const Group_log_state *gls,
                                           rpl_sidno sidno, rpl_gno gno)
{
  DBUG_ENTER("Group_cache::add_empty_subgroup_if_missing(Group_log_state *, rpl_sidno, rpl_gno)");
  if (!gls->is_ended(sidno, gno) && !gls->is_partial(sidno, gno) &&
      !contains_group(sidno, gno))
    PROPAGATE_REPORTED_ERROR(add_empty_subgroup(sidno, gno, false));
  RETURN_OK;
}


enum_return_status
Group_cache::add_empty_subgroups_if_missing(const Group_log_state *gls,
                                            const GTID_set *gtid_set)
{
  DBUG_ENTER("Group_cache::add_empty_subgroups_if_missing(Group_log_state *, GTID_set *)");
  /*
    @todo: This algorithm is
    O(n_groups_in_cache*n_groups_in_gtid_set) because contains_group
    is O(n_groups_in_cache).  We can optimize this to
    O(n_groups_in_cache+n_groups_in_gtid_set), as follows: create a
    HASH and copy all groups from the cache to the HASH.  Then use the
    HASH to detect if the group is in the cache or not.  This has some
    overhead so should only be used if
    n_groups_in_cache*n_groups_in_gtid_set is significantly bigger
    than n_groups_in_cache+n_groups_in_gtid_set elements. /Sven
  */
  GTID_set::GTID_iterator git(gtid_set);
  Group g= git.get();
  while (g.sidno) {
    PROPAGATE_REPORTED_ERROR(add_empty_subgroup_if_missing(gls,
                                                           g.sidno, g.gno));
    git.next();
    g= git.get();
  }
  RETURN_OK;
}


enum_return_status
Group_cache::update_group_log_state(const THD *thd, Group_log_state *gls) const
{
  DBUG_ENTER("Group_cache::update_group_log_state");

  const GTID_set *lock_set= thd->variables.gtid_next_list.get_gtid_set();
  int n_subgroups= get_n_subgroups();
  rpl_sidno lock_sidno= 0;

  if (lock_set != NULL)
    gls->lock_sidnos(lock_set);
  else
  {
    DBUG_ASSERT(n_subgroups <= 1);
    lock_sidno= n_subgroups > 0 ? get_unsafe_pointer(0)->sidno : 0;
    if (lock_sidno)
      gls->lock_sidno(lock_sidno);
  }

  enum_return_status ret= RETURN_STATUS_OK;
  bool updated= false;

  for (int i= 0; i < n_subgroups; i++)
  {
    Cached_subgroup *cs= get_unsafe_pointer(i);
    if (cs->type == NORMAL_SUBGROUP || cs->type == EMPTY_SUBGROUP)
    {
      DBUG_ASSERT(lock_set != NULL ? lock_set->contains_sidno(cs->sidno) :
                  (lock_sidno > 0 && cs->sidno == lock_sidno));
      if (cs->group_end)
      {
        updated= true;
        ret= gls->end_group(cs->sidno, cs->gno);
        if (ret != RETURN_STATUS_OK)
          break;
      }
      else if (!gls->mark_partial(cs->sidno, cs->gno))
        updated= true;
    }
  }

  if (lock_set != NULL)
  {
    if (updated)
      gls->broadcast_sidnos(lock_set);
    gls->unlock_sidnos(lock_set);
  }
  else if (lock_sidno != 0)
  {
    if (updated)
      gls->broadcast_sidno(lock_sidno);
    gls->unlock_sidno(lock_sidno);
  }
  RETURN_STATUS(ret);
}


enum_return_status Group_cache::generate_automatic_gno(const THD *thd,
                                                       Group_log_state *gls)
{
  DBUG_ENTER("Group_cache::generate_automatic_gno");
  if (thd->variables.gtid_next.type != Gtid_specification::AUTOMATIC)
    RETURN_OK;
  DBUG_ASSERT(thd->variables.gtid_next_list.get_gtid_set() == NULL);
  int n_subgroups= get_n_subgroups();
  rpl_gno automatic_gno= 0;
  rpl_sidno sidno= 0;
  Cached_subgroup *last_automatic_subgroup= NULL;
  for (int i= 0; i < n_subgroups; i++)
  {
    Cached_subgroup *cs= get_unsafe_pointer(i);
    if (cs->type == NORMAL_SUBGROUP && cs->gno <= 0)
    {
      if (automatic_gno == 0)
      {
        sidno= cs->sidno;
        gls->lock_sidno(sidno);
        automatic_gno= gls->get_automatic_gno(sidno);
        if (automatic_gno == -1)
          RETURN_REPORTED_ERROR;
        gls->acquire_ownership(sidno, automatic_gno, thd);
        gls->unlock_sidno(sidno);
      }
      cs->gno= automatic_gno;
      cs->sidno= sidno;
      last_automatic_subgroup= cs;
    }
  }
  if (last_automatic_subgroup != NULL)
    last_automatic_subgroup->group_end= true;
  RETURN_OK;
}


enum_return_status Group_cache::get_ended_groups(GTID_set *gs) const
{
  DBUG_ENTER("Group_cache::get_groups");
  int n_subgroups= get_n_subgroups();
  PROPAGATE_REPORTED_ERROR(gs->ensure_sidno(gs->get_sid_map()->get_max_sidno()));
  for (int i= 0; i < n_subgroups; i++)
  {
    Cached_subgroup *cs= get_unsafe_pointer(i);
    if (cs->group_end)
      PROPAGATE_REPORTED_ERROR(gs->_add(cs->sidno, cs->gno));
  }
  RETURN_OK;
}


enum_return_status Group_cache::get_partial_groups(GTID_set *gs) const
{
  DBUG_ENTER("Group_cache::get_groups");
  Sid_map *sid_map= gs->get_sid_map();
  PROPAGATE_REPORTED_ERROR(gs->ensure_sidno(sid_map->get_max_sidno()));
  GTID_set ended_groups(sid_map);
  PROPAGATE_REPORTED_ERROR(get_ended_groups(&ended_groups));
  int n_subgroups= get_n_subgroups();
  for (int i= 0; i < n_subgroups; i++)
  {
    Cached_subgroup *cs= get_unsafe_pointer(i);
    if (!ended_groups.contains_group(cs->sidno, cs->gno))
      PROPAGATE_REPORTED_ERROR(gs->_add(cs->sidno, cs->gno));
  }
  RETURN_OK;
}


#endif
