/* Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.

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

/**
  @file storage/perfschema/pfs_server.cc
  Private interface for the server (implementation).
*/

#include "my_global.h"
#include "my_sys.h"
#include "mysys_err.h"
#include "pfs_server.h"
#include "pfs.h"
#include "pfs_global.h"
#include "pfs_instr_class.h"
#include "pfs_instr.h"
#include "pfs_events_waits.h"
#include "pfs_events_stages.h"
#include "pfs_events_statements.h"
#include "pfs_timer.h"
#include "pfs_setup_actor.h"
#include "pfs_setup_object.h"
#include "pfs_host.h"
#include "pfs_user.h"
#include "pfs_account.h"
#include "pfs_defaults.h"

PFS_global_param pfs_param;

C_MODE_START
static void destroy_pfs_thread(void *key);
C_MODE_END

static void cleanup_performance_schema(void);

struct PSI_bootstrap*
initialize_performance_schema(const PFS_global_param *param)
{
  pfs_initialized= false;

  if (! param->m_enabled)
  {
    /*
      The performance schema is disabled in the startup command line.
      All the instrumentation is turned off.
    */
    return NULL;
  }

  init_timers();
  PFS_atomic::init();
  init_event_name_sizing(param);

  if (pthread_key_create(&THR_PFS, destroy_pfs_thread))
    return NULL;

  THR_PFS_initialized= true;

  if (init_sync_class(param->m_mutex_class_sizing,
                      param->m_rwlock_class_sizing,
                      param->m_cond_class_sizing) ||
      init_thread_class(param->m_thread_class_sizing) ||
      init_table_share(param->m_table_share_sizing) ||
      init_file_class(param->m_file_class_sizing) ||
      init_stage_class(param->m_stage_class_sizing) ||
      init_statement_class(param->m_statement_class_sizing) ||
      init_socket_class(param->m_socket_class_sizing) ||
      init_instruments(param) ||
      init_events_waits_history_long(
        param->m_events_waits_history_long_sizing) ||
      init_events_stages_history_long(
        param->m_events_stages_history_long_sizing) ||
      init_events_statements_history_long(
        param->m_events_statements_history_long_sizing) ||
      init_file_hash() ||
      init_table_share_hash() ||
      init_setup_actor(param) ||
      init_setup_actor_hash() ||
      init_setup_object(param) ||
      init_setup_object_hash() ||
      init_host(param) ||
      init_host_hash() ||
      init_user(param) ||
      init_user_hash() ||
      init_account(param) ||
      init_account_hash())
  {
    /*
      The performance schema initialization failed.
      Free the memory used, and disable the instrumentation.
    */
    cleanup_performance_schema();
    return NULL;
  }

  pfs_initialized= true;

  /** Default values for SETUP_CONSUMERS */
  flag_events_stages_current=          param->m_consumer_events_stages_current_enabled;
  flag_events_stages_history=          param->m_consumer_events_stages_history_enabled;
  flag_events_stages_history_long=     param->m_consumer_events_stages_history_long_enabled;
  flag_events_statements_current=      param->m_consumer_events_statements_current_enabled;
  flag_events_statements_history=      param->m_consumer_events_statements_history_enabled;
  flag_events_statements_history_long= param->m_consumer_events_statements_history_long_enabled;
  flag_events_waits_current=           param->m_consumer_events_waits_current_enabled;
  flag_events_waits_history=           param->m_consumer_events_waits_history_enabled;
  flag_events_waits_history_long=      param->m_consumer_events_waits_history_long_enabled;
  flag_global_instrumentation=         param->m_consumer_global_instrumentation_enabled;
  flag_thread_instrumentation=         param->m_consumer_thread_instrumentation_enabled;

  install_default_setup(&PFS_bootstrap);
  return &PFS_bootstrap;
}

static void destroy_pfs_thread(void *key)
{
  PFS_thread* pfs= reinterpret_cast<PFS_thread*> (key);
  DBUG_ASSERT(pfs);
  /*
    This automatic cleanup is a last resort and best effort to avoid leaks,
    and may not work on windows due to the implementation of pthread_key_create().
    Please either use:
    - my_thread_end()
    - or PSI_server->delete_current_thread()
    in the instrumented code, to explicitly cleanup the instrumentation.

    Avoid invalid writes when the main() thread completes after shutdown:
    the memory pointed by pfs is already released.
  */
  if (pfs_initialized)
    destroy_thread(pfs);
}

static void cleanup_performance_schema(void)
{
  cleanup_instruments();
  cleanup_sync_class();
  cleanup_thread_class();
  cleanup_table_share();
  cleanup_file_class();
  cleanup_stage_class();
  cleanup_statement_class();
  cleanup_socket_class();
  cleanup_events_waits_history_long();
  cleanup_events_stages_history_long();
  cleanup_events_statements_history_long();
  cleanup_table_share_hash();
  cleanup_file_hash();
  cleanup_setup_actor();
  cleanup_setup_actor_hash();
  cleanup_setup_object();
  cleanup_setup_object_hash();
  cleanup_host();
  cleanup_host_hash();
  cleanup_user();
  cleanup_user_hash();
  cleanup_account();
  cleanup_account_hash();
  PFS_atomic::cleanup();
}

void shutdown_performance_schema(void)
{
  pfs_initialized= false;
  cleanup_performance_schema();
  /*
    Be careful to not delete un-initialized keys,
    this would affect key 0, which is THR_KEY_mysys,
  */
  if (THR_PFS_initialized)
  {
    my_pthread_setspecific_ptr(THR_PFS, NULL);
    pthread_key_delete(THR_PFS);
    THR_PFS_initialized= false;
  }
}

/**
  Initialize the dynamic array used to hold PFS_INSTRUMENT configuration
  options.
*/
void init_pfs_instrument_array()
{
  my_init_dynamic_array(&pfs_instr_init_array, sizeof(PFS_instr_init*), 10, 10);
}

/**
  Process one PFS_INSTRUMENT configuration string. Isolate the instrument name,
  evaluate the option value, and store them in a dynamic array.
*/
bool add_pfs_instr_to_array(const char* name, const char* value)
{
  int name_length= strlen(name);
  int value_length= strlen(value);

  /* Allocate structure plus string buffers plus null terminators */
  PFS_instr_init* e = (PFS_instr_init*)my_malloc(sizeof(PFS_instr_init)
                       + name_length + 1 + value_length + 1, MYF(MY_WME));
  if (!e) return 1;
  
  /* Copy the name string */
  e->m_name= (char*)e + sizeof(PFS_instr_init);
  memcpy(e->m_name, name, name_length);
  e->m_name_length= name_length;
  e->m_name[name_length]= '\0';
  
  /* Copy the option string */
  e->m_value= e->m_name + name_length + 1;
  memcpy(e->m_value, value, value_length);
  e->m_value_length= value_length;
  e->m_value[value_length]= '\0';

  /* Unrecognized values default to 'disabled'. */
  e->m_enabled= false;
  e->m_timed= false;
  bool valid= true;

  /* Set flags accordingly */
  if (!my_strcasecmp(&my_charset_latin1, e->m_value, "counted"))
  {
    e->m_enabled= true;
    e->m_timed= false;
  }
  else
  if (!my_strcasecmp(&my_charset_latin1, e->m_value, "true") ||
      !my_strcasecmp(&my_charset_latin1, e->m_value, "on") ||
      !my_strcasecmp(&my_charset_latin1, e->m_value, "1") ||
      !my_strcasecmp(&my_charset_latin1, e->m_value, "yes") ||
      !my_strcasecmp(&my_charset_latin1, e->m_value, "enabled"))
  {
    e->m_enabled= true;
    e->m_timed= true;
  }
  else
  if (!my_strcasecmp(&my_charset_latin1, e->m_value, "false") ||
      !my_strcasecmp(&my_charset_latin1, e->m_value, "off") ||
      !my_strcasecmp(&my_charset_latin1, e->m_value, "0") ||
      !my_strcasecmp(&my_charset_latin1, e->m_value, "no") ||
      !my_strcasecmp(&my_charset_latin1, e->m_value, "disabled"))
  {
    e->m_enabled= false;
    e->m_timed= false;
  }
  else
  {
    valid= false; /* ignore invalid option values */
  }

  if (valid && insert_dynamic(&pfs_instr_init_array, &e))
  {
    my_free(e);
    return 1;
  }
  return 0;
}
