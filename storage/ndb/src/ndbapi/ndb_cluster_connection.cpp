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

#include <ndb_global.h>
#include <my_pthread.h>
#include <my_sys.h>

#include "ndb_cluster_connection_impl.hpp"
#include <mgmapi_configuration.hpp>
#include <mgmapi_config_parameters.h>
#include "TransporterFacade.hpp"
#include <NdbOut.hpp>
#include <NdbSleep.h>
#include <NdbThread.h>
#include <ndb_limits.h>
#include <ConfigRetriever.hpp>
#include <ndb_version.h>
#include <mgmapi_debug.h>
#include <mgmapi_internal.h>
#include <md5_hash.hpp>
#include "NdbImpl.hpp"
#include "NdbDictionaryImpl.hpp"

#include <NdbMutex.h>
#ifdef VM_TRACE
NdbMutex *ndb_print_state_mutex= NULL;
#endif

#include <EventLogger.hpp>
extern EventLogger *g_eventLogger;

static int g_ndb_connection_count = 0;

/*
 * Ndb_cluster_connection
 */
Ndb_cluster_connection::Ndb_cluster_connection(const char *connect_string)
  : m_impl(* new Ndb_cluster_connection_impl(connect_string, 0, 0))
{
}

Ndb_cluster_connection::Ndb_cluster_connection(const char *connect_string,
                                               int force_api_nodeid)
  : m_impl(* new Ndb_cluster_connection_impl(connect_string, 0,
                                             force_api_nodeid))
{
}

Ndb_cluster_connection::Ndb_cluster_connection(const char *connect_string,
                                               Ndb_cluster_connection *
                                               main_connection)
  : m_impl(* new Ndb_cluster_connection_impl(connect_string,
                                             main_connection, 0))
{
}

Ndb_cluster_connection::Ndb_cluster_connection
(Ndb_cluster_connection_impl& impl) : m_impl(impl)
{
}

Ndb_cluster_connection::~Ndb_cluster_connection()
{
  Ndb_cluster_connection_impl *tmp = &m_impl;
  if (this != tmp)
    delete tmp;
}

int Ndb_cluster_connection::get_connected_port() const
{
  if (m_impl.m_config_retriever)
    return m_impl.m_config_retriever->get_mgmd_port();
  return -1;
}

const char *Ndb_cluster_connection::get_connected_host() const
{
  if (m_impl.m_config_retriever)
    return m_impl.m_config_retriever->get_mgmd_host();
  return 0;
}

const char *Ndb_cluster_connection::get_connectstring(char *buf,
						      int buf_sz) const
{
  if (m_impl.m_config_retriever)
    return m_impl.m_config_retriever->get_connectstring(buf,buf_sz);
  return 0;
}

pthread_handler_t run_ndb_cluster_connection_connect_thread(void *me)
{
  Ndb_cluster_connection_impl* connection= (Ndb_cluster_connection_impl*) me;
  connection->m_run_connect_thread= 1;
  connection->connect_thread();
  return me;
}

int Ndb_cluster_connection::start_connect_thread(int (*connect_callback)(void))
{
  int r;
  DBUG_ENTER("Ndb_cluster_connection::start_connect_thread");
  m_impl.m_connect_callback= connect_callback;
  if ((r = connect(0,0,0)) == 1)
  {
    DBUG_PRINT("info",("starting thread"));
    m_impl.m_connect_thread= 
      NdbThread_Create(run_ndb_cluster_connection_connect_thread,
		       (void**)&m_impl,
                       0, // default stack size
                       "ndb_cluster_connection",
		       NDB_THREAD_PRIO_LOW);
  }
  else if (r < 0)
  {
    DBUG_RETURN(-1);
  }
  else if (m_impl.m_connect_callback)
  { 
    (*m_impl.m_connect_callback)();
  }
  DBUG_RETURN(0);
}

void Ndb_cluster_connection::set_optimized_node_selection(int val)
{
  m_impl.m_optimized_node_selection= val;
}

void
Ndb_cluster_connection_impl::init_get_next_node
(Ndb_cluster_connection_node_iter &iter)
{
  if (iter.scan_state != (Uint8)~0)
    iter.cur_pos= iter.scan_state;
  if (iter.cur_pos >= no_db_nodes())
    iter.cur_pos= 0;
  iter.init_pos= iter.cur_pos;
  iter.scan_state= 0;
  //  fprintf(stderr,"[init %d]",iter.init_pos);
  return;
}

Uint32
Ndb_cluster_connection_impl::get_next_node(Ndb_cluster_connection_node_iter &iter)
{
  Uint32 cur_pos= iter.cur_pos;
  if (cur_pos >= no_db_nodes())
    return 0;

  Ndb_cluster_connection_impl::Node *nodes= m_all_nodes.getBase();
  Ndb_cluster_connection_impl::Node &node=  nodes[cur_pos];

  if (iter.scan_state != (Uint8)~0)
  {
    assert(iter.scan_state < no_db_nodes());
    if (nodes[iter.scan_state].group == node.group)
      iter.scan_state= ~0;
    else
      return nodes[iter.scan_state++].id;
  }

  //  fprintf(stderr,"[%d]",node.id);

  cur_pos++;
  Uint32 init_pos= iter.init_pos;
  if (cur_pos == node.next_group)
  {
    cur_pos= nodes[init_pos].this_group;
  }

  //  fprintf(stderr,"[cur_pos %d]",cur_pos);
  if (cur_pos != init_pos)
    iter.cur_pos= cur_pos;
  else
  {
    iter.cur_pos= node.next_group;
    iter.init_pos= node.next_group;
  }
  return node.id;
}

unsigned
Ndb_cluster_connection::no_db_nodes()
{
  return m_impl.m_all_nodes.size();
}

unsigned
Ndb_cluster_connection::node_id()
{
  return m_impl.m_transporter_facade->ownId();
}


int Ndb_cluster_connection::get_no_ready()
{
  TransporterFacade *tp = m_impl.m_transporter_facade;
  if (tp == 0 || tp->ownId() == 0)
    return -1;

  unsigned int foundAliveNode = 0;
  tp->lock_mutex();
  for(unsigned i= 0; i < no_db_nodes(); i++)
  {
    //************************************************
    // If any node is answering, ndb is answering
    //************************************************
    if (tp->get_node_alive(m_impl.m_all_nodes[i].id) != 0) {
      foundAliveNode++;
    }
  }
  tp->unlock_mutex();

  return foundAliveNode;
}

int
Ndb_cluster_connection::wait_until_ready(int timeout,
					 int timeout_after_first_alive)
{
  DBUG_ENTER("Ndb_cluster_connection::wait_until_ready");
  TransporterFacade *tp = m_impl.m_transporter_facade;
  if (tp == 0)
  {
    DBUG_RETURN(-1);
  }
  if (tp->ownId() == 0)
  {
    DBUG_RETURN(-1);
  }
  int secondsCounter = 0;
  int milliCounter = 0;
  int noChecksSinceFirstAliveFound = 0;
  do {
    unsigned int foundAliveNode = get_no_ready();

    if (foundAliveNode == no_db_nodes())
    {
      DBUG_RETURN(0);
    }
    else if (foundAliveNode > 0)
    {
      noChecksSinceFirstAliveFound++;
      // 100 ms delay -> 10*
      if (noChecksSinceFirstAliveFound > 10*timeout_after_first_alive)
	DBUG_RETURN(1);
    }
    else if (secondsCounter >= timeout)
    { // no alive nodes and timed out
      DBUG_RETURN(-1);
    }
    NdbSleep_MilliSleep(100);
    milliCounter += 100;
    if (milliCounter >= 1000) {
      secondsCounter++;
      milliCounter = 0;
    }//if
  } while (1);
}

unsigned Ndb_cluster_connection::get_connect_count() const
{
  return m_impl.get_connect_count();
}

int Ndb_cluster_connection::get_latest_error() const
{
  return m_impl.m_latest_error;
}

const char *Ndb_cluster_connection::get_latest_error_msg() const
{
  return m_impl.m_latest_error_msg.c_str();
}

/*
 * Ndb_cluster_connection_impl
 */

Ndb_cluster_connection_impl::
Ndb_cluster_connection_impl(const char * connect_string,
                            Ndb_cluster_connection *main_connection,
                            int force_api_nodeid)
  : Ndb_cluster_connection(*this),
    m_main_connection(main_connection),
    m_optimized_node_selection(1),
    m_run_connect_thread(0),
    m_latest_trans_gci(0),
    m_first_ndb_object(0),
    m_latest_error_msg(),
    m_latest_error(0)
{
  DBUG_ENTER("Ndb_cluster_connection");
  DBUG_PRINT("enter",("Ndb_cluster_connection this=0x%lx", (long) this));

  NdbMutex_Lock(g_ndb_connection_mutex);
  if(g_ndb_connection_count++ == 0)
  {
    NdbColumnImpl::create_pseudo_columns();
    g_eventLogger->createConsoleHandler();
    g_eventLogger->setCategory("NdbApi");
    g_eventLogger->enable(Logger::LL_ON, Logger::LL_ERROR);
    /*
      Disable repeated message handling as it interfers
      with mysqld logging, in which case messages come out
      of order.  Same applies for regular ndbapi user.
    */
    g_eventLogger->setRepeatFrequency(0);
  }
  NdbMutex_Unlock(g_ndb_connection_mutex);

  m_event_add_drop_mutex= NdbMutex_Create();
  m_new_delete_ndb_mutex = NdbMutex_Create();

  m_connect_thread= 0;
  m_connect_callback= 0;

#ifdef VM_TRACE
  if (ndb_print_state_mutex == NULL)
    ndb_print_state_mutex= NdbMutex_Create();
#endif
  m_config_retriever=
    new ConfigRetriever(connect_string, force_api_nodeid,
                        NDB_VERSION, NDB_MGM_NODE_TYPE_API);
  if (m_config_retriever->hasError())
  {
    m_latest_error= 1;
    m_latest_error_msg.assfmt
      ("Could not initialize handle to management server: %s",
       m_config_retriever->getErrorString());
    printf("%s\n", get_latest_error_msg());
  }
  if (!m_main_connection)
  {
    m_globalDictCache = new GlobalDictCache;
    m_transporter_facade= new TransporterFacade(m_globalDictCache);
  }
  else
  {
    assert(m_main_connection->m_impl.m_globalDictCache != NULL);
    m_globalDictCache = 0;
    m_transporter_facade=
      new TransporterFacade(m_main_connection->m_impl.m_globalDictCache);

    // The secondary connection can't use same nodeid, clear the nodeid
    // in ConfigRetriver to avoid asking for the same nodeid again
    m_config_retriever->setNodeId(0);

  }

  DBUG_VOID_RETURN;
}

Ndb_cluster_connection_impl::~Ndb_cluster_connection_impl()
{
  DBUG_ENTER("~Ndb_cluster_connection");
  if (m_transporter_facade != 0)
  {
    m_transporter_facade->stop_instance();
  }
  if (m_globalDictCache)
  {
    delete m_globalDictCache;
  }
  if (m_connect_thread)
  {
    void *status;
    m_run_connect_thread= 0;
    NdbThread_WaitFor(m_connect_thread, &status);
    NdbThread_Destroy(&m_connect_thread);
    m_connect_thread= 0;
  }
  if (m_transporter_facade != 0)
  {
    delete m_transporter_facade;
    m_transporter_facade = 0;
  }
  if (m_config_retriever)
  {
    delete m_config_retriever;
    m_config_retriever= NULL;
  }
#ifdef VM_TRACE
  if (ndb_print_state_mutex != NULL)
  {
    NdbMutex_Destroy(ndb_print_state_mutex);
    ndb_print_state_mutex= NULL;
  }
#endif

  NdbMutex_Lock(g_ndb_connection_mutex);
  if(--g_ndb_connection_count == 0)
  {
    NdbColumnImpl::destory_pseudo_columns();
  }
  NdbMutex_Unlock(g_ndb_connection_mutex);

  if (m_event_add_drop_mutex)
    NdbMutex_Destroy(m_event_add_drop_mutex);
  m_event_add_drop_mutex = 0;

  if (m_new_delete_ndb_mutex)
    NdbMutex_Destroy(m_new_delete_ndb_mutex);
  m_new_delete_ndb_mutex = 0;
  
  DBUG_VOID_RETURN;
}

void
Ndb_cluster_connection::lock_ndb_objects()
{
  NdbMutex_Lock(m_impl.m_new_delete_ndb_mutex);
}

void
Ndb_cluster_connection::unlock_ndb_objects()
{
  NdbMutex_Unlock(m_impl.m_new_delete_ndb_mutex);
}

const Ndb*
Ndb_cluster_connection::get_next_ndb_object(const Ndb* p)
{
  if (p == 0)
    return m_impl.m_first_ndb_object;
  
  return p->theImpl->m_next_ndb_object;
}

void
Ndb_cluster_connection_impl::link_ndb_object(Ndb* p)
{
  lock_ndb_objects();
  if (m_first_ndb_object != 0)
  {
    m_first_ndb_object->theImpl->m_prev_ndb_object = p;
  }
  
  p->theImpl->m_next_ndb_object = m_first_ndb_object;
  m_first_ndb_object = p;
  
  unlock_ndb_objects();
}

void
Ndb_cluster_connection_impl::unlink_ndb_object(Ndb* p)
{
  lock_ndb_objects();
  Ndb* prev = p->theImpl->m_prev_ndb_object;
  Ndb* next = p->theImpl->m_next_ndb_object;

  if (prev == 0)
  {
    assert(m_first_ndb_object == p);
    m_first_ndb_object = next;
  }
  else
  {
    prev->theImpl->m_next_ndb_object = next;
  }

  if (next)
  {
    next->theImpl->m_prev_ndb_object = prev;
  }
  
  p->theImpl->m_prev_ndb_object = 0;
  p->theImpl->m_next_ndb_object = 0;

  unlock_ndb_objects();  
}

void
Ndb_cluster_connection_impl::set_name(const char *name)
{
  NdbMgmHandle h= m_config_retriever->get_mgmHandle();
  ndb_mgm_set_name(h, name);
}

int
Ndb_cluster_connection_impl::init_nodes_vector(Uint32 nodeid,
					       const ndb_mgm_configuration 
					       &config)
{
  DBUG_ENTER("Ndb_cluster_connection_impl::init_nodes_vector");
  ndb_mgm_configuration_iterator iter(config, CFG_SECTION_CONNECTION);
  
  for(iter.first(); iter.valid(); iter.next())
  {
    Uint32 nodeid1, nodeid2, remoteNodeId, group= 5;
    const char * remoteHostName= 0, * localHostName= 0;
    if(iter.get(CFG_CONNECTION_NODE_1, &nodeid1)) continue;
    if(iter.get(CFG_CONNECTION_NODE_2, &nodeid2)) continue;

    if(nodeid1 != nodeid && nodeid2 != nodeid) continue;
    remoteNodeId = (nodeid == nodeid1 ? nodeid2 : nodeid1);

    iter.get(CFG_CONNECTION_GROUP, &group);

    {
      const char * host1= 0, * host2= 0;
      iter.get(CFG_CONNECTION_HOSTNAME_1, &host1);
      iter.get(CFG_CONNECTION_HOSTNAME_2, &host2);
      localHostName  = (nodeid == nodeid1 ? host1 : host2);
      remoteHostName = (nodeid == nodeid1 ? host2 : host1);
    }

    Uint32 type = ~0;
    if(iter.get(CFG_TYPE_OF_SECTION, &type)) continue;

    switch(type){
    case CONNECTION_TYPE_SHM:{
      break;
    }
    case CONNECTION_TYPE_SCI:{
      break;
    }
    case CONNECTION_TYPE_TCP:{
      // connecting through localhost
      // check if config_hostname is local
      if (SocketServer::tryBind(0,remoteHostName))
	group--; // upgrade group value
      break;
    }
    }
    if (m_all_nodes.push_back(Node(group,remoteNodeId)))
    {
      DBUG_RETURN(-1);
    }
    DBUG_PRINT("info",("saved %d %d", group,remoteNodeId));
    for (int i= m_all_nodes.size()-2;
	 i >= 0 && m_all_nodes[i].group > m_all_nodes[i+1].group;
	 i--)
    {
      Node tmp= m_all_nodes[i];
      m_all_nodes[i]= m_all_nodes[i+1];
      m_all_nodes[i+1]= tmp;
    }
  }

  int i;
  Uint32 cur_group, i_group= 0;
  cur_group= ~0;
  for (i= (int)m_all_nodes.size()-1; i >= 0; i--)
  {
    if (m_all_nodes[i].group != cur_group)
    {
      cur_group= m_all_nodes[i].group;
      i_group= i+1;
    }
    m_all_nodes[i].next_group= i_group;
  }
  cur_group= ~0;
  for (i= 0; i < (int)m_all_nodes.size(); i++)
  {
    if (m_all_nodes[i].group != cur_group)
    {
      cur_group= m_all_nodes[i].group;
      i_group= i;
    }
    m_all_nodes[i].this_group= i_group;
  }
#if 0
  for (i= 0; i < (int)m_all_nodes.size(); i++)
  {
    fprintf(stderr, "[%d] %d %d %d %d\n",
	   i,
	   m_all_nodes[i].id,
	   m_all_nodes[i].group,
	   m_all_nodes[i].this_group,
	   m_all_nodes[i].next_group);
  }

  do_test();
#endif
  DBUG_RETURN(0);
}

int
Ndb_cluster_connection_impl::configure(Uint32 nodeId,
                                       const ndb_mgm_configuration &config)
{
  DBUG_ENTER("Ndb_cluster_connection_impl::configure");
  {
    ndb_mgm_configuration_iterator iter(config, CFG_SECTION_NODE);
    if(iter.find(CFG_NODE_ID, nodeId))
      DBUG_RETURN(-1);

    // Configure scan settings
    Uint32 scan_batch_size= 0;
    if (!iter.get(CFG_MAX_SCAN_BATCH_SIZE, &scan_batch_size)) {
      m_config.m_scan_batch_size= scan_batch_size;
    }
    Uint32 batch_byte_size= 0;
    if (!iter.get(CFG_BATCH_BYTE_SIZE, &batch_byte_size)) {
      m_config.m_batch_byte_size= batch_byte_size;
    }
    Uint32 batch_size= 0;
    if (!iter.get(CFG_BATCH_SIZE, &batch_size)) {
      m_config.m_batch_size= batch_size;
    }

    // Configure timeouts
    Uint32 timeout = 120000;
    for (iter.first(); iter.valid(); iter.next())
    {
      Uint32 tmp1 = 0, tmp2 = 0;
      iter.get(CFG_DB_TRANSACTION_CHECK_INTERVAL, &tmp1);
      iter.get(CFG_DB_TRANSACTION_DEADLOCK_TIMEOUT, &tmp2);
      tmp1 += tmp2;
      if (tmp1 > timeout)
        timeout = tmp1;
    }
    m_config.m_waitfor_timeout = timeout;
  }
  DBUG_RETURN(init_nodes_vector(nodeId, config));
}

void
Ndb_cluster_connection_impl::do_test()
{
  Ndb_cluster_connection_node_iter iter;
  int n= no_db_nodes()+5;
  Uint32 *nodes= new Uint32[n+1];

  for (int g= 0; g < n; g++)
  {
    for (int h= 0; h < n; h++)
    {
      Uint32 id;
      Ndb_cluster_connection_node_iter iter2;
      {
	for (int j= 0; j < g; j++)
	{
	  nodes[j]= get_next_node(iter2);
	}
      }

      for (int i= 0; i < n; i++)
      {
	init_get_next_node(iter);
	fprintf(stderr, "%d dead:(", g);
	id= 0;
	while (id == 0)
	{
	  if ((id= get_next_node(iter)) == 0)
	    break;
	  for (int j= 0; j < g; j++)
	  {
	    if (nodes[j] == id)
	    {
	      fprintf(stderr, " %d", id);
	      id= 0;
	      break;
	    }
	  }
	}
	fprintf(stderr, ")");
	if (id == 0)
	{
	  break;
	}
	fprintf(stderr, " %d\n", id);
      }
      fprintf(stderr, "\n");
    }
  }
  delete [] nodes;
}

void Ndb_cluster_connection::set_name(const char *name)
{
  m_impl.set_name(name);
}

int Ndb_cluster_connection_impl::connect(int no_retries,
                                         int retry_delay_in_seconds,
                                         int verbose)
{
  DBUG_ENTER("Ndb_cluster_connection::connect");
  do {
    if (m_config_retriever == 0)
    {
      if (!m_latest_error)
      {
        m_latest_error = 1;
        m_latest_error_msg.assign("Ndb_cluster_connection init "
                                  "error: m_config_retriever==0");
      }
      DBUG_PRINT("exit", ("no m_config_retriever, ret: -1"));
      DBUG_RETURN(-1);
    }
    if (m_config_retriever->do_connect(no_retries,
                                       retry_delay_in_seconds,
                                       verbose))
    {
      char buf[1024];
      m_latest_error = 1;
      m_latest_error_msg.assfmt("Connect using '%s' timed out",
                                get_connectstring(buf, sizeof(buf)));
      DBUG_PRINT("exit", ("mgmt server not up yet, ret: 1"));
      DBUG_RETURN(1); // mgmt server not up yet
    }

    Uint32 nodeId = m_config_retriever->allocNodeId(4/*retries*/,
                                                    3/*delay*/);
    if(nodeId == 0)
      break;
    ndb_mgm_configuration * props = m_config_retriever->getConfig(nodeId);
    if(props == 0)
      break;

    if (configure(nodeId, *props))
    {
      ndb_mgm_destroy_configuration(props);
      DBUG_PRINT("exit", ("malloc failure, ret: -1"));
      DBUG_RETURN(-1);
    }

    if (m_transporter_facade->start_instance(nodeId, props) < 0)
    {
      ndb_mgm_destroy_configuration(props);
      DBUG_RETURN(-1);
    }

    ndb_mgm_destroy_configuration(props);
    m_transporter_facade->connected();
    m_latest_error = 0;
    m_latest_error_msg.assign("");
    DBUG_PRINT("exit", ("connect ok, ret: 0"));
    DBUG_RETURN(0);
  } while(0);

  const char* erString = m_config_retriever->getErrorString();
  if (erString == 0) {
    erString = "No error specified!";
  }
  m_latest_error = 1;
  m_latest_error_msg.assfmt("Configuration error: %s", erString);
  ndbout << get_latest_error_msg() << endl;
  DBUG_PRINT("exit", ("connect failed, '%s' ret: -1", erString));
  DBUG_RETURN(-1);
}


int
Ndb_cluster_connection::connect(int no_retries,
                                int retry_delay_in_seconds,
                                int verbose)
{
  return m_impl.connect(no_retries, retry_delay_in_seconds, verbose);
}


void Ndb_cluster_connection_impl::connect_thread()
{
  DBUG_ENTER("Ndb_cluster_connection_impl::connect_thread");
  int r;
  do {
    NdbSleep_SecSleep(1);
    if ((r = connect(0,0,0)) == 0)
      break;
    if (r == -1) {
      printf("Ndb_cluster_connection::connect_thread error\n");
      DBUG_ASSERT(false);
      m_run_connect_thread= 0;
    } else {
      // Wait before making a new connect attempt
      NdbSleep_SecSleep(1);
    }
  } while (m_run_connect_thread);
  if (m_connect_callback)
    (*m_connect_callback)();
  DBUG_VOID_RETURN;
}

Uint64 *
Ndb_cluster_connection::get_latest_trans_gci()
{
 return m_impl.get_latest_trans_gci(); 
} 

void
Ndb_cluster_connection::init_get_next_node(Ndb_cluster_connection_node_iter &iter)
{
  m_impl.init_get_next_node(iter);
}

Uint32
Ndb_cluster_connection::get_next_node(Ndb_cluster_connection_node_iter &iter)
{
  return m_impl.get_next_node(iter);
}

unsigned
Ndb_cluster_connection::get_active_ndb_objects() const
{
  return m_impl.m_transporter_facade->get_active_ndb_objects();
}

int Ndb_cluster_connection::set_timeout(int timeout_ms)
{
  return ndb_mgm_set_timeout(m_impl.m_config_retriever->get_mgmHandle(),
                             timeout_ms);
}

int
Ndb_cluster_connection::get_auto_reconnect() const
{
  return m_impl.m_transporter_facade->get_auto_reconnect();
}

void
Ndb_cluster_connection::set_auto_reconnect(int value)
{
  m_impl.m_transporter_facade->set_auto_reconnect(value);
}

template class Vector<Ndb_cluster_connection_impl::Node>;

