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

#include "atrt.hpp"
#include <NdbSleep.h>

static bool connect_mysqld(atrt_process* proc);
static bool populate_db(atrt_config&, atrt_process*);
static bool setup_repl(atrt_config&);

static
bool
run_query(atrt_process* proc, const char * query)
{
  MYSQL* mysql = &proc->m_mysql;
  g_logger.debug("'%s@%s' - Running query '%s'",
		 proc->m_cluster->m_name.c_str(),
		 proc->m_host->m_hostname.c_str(),
                 query);

  if (mysql_query(mysql, query))
  {
    g_logger.error("'%s@%s' - Failed to run query '%s' %d:%s",
                   proc->m_cluster->m_name.c_str(),
                   proc->m_host->m_hostname.c_str(),
                   query,
                   mysql_errno(mysql),
                   mysql_error(mysql));
    return false;
  }
  return true;
}

static const char* create_sql[] = {
"create database atrt",

"use atrt",

"create table host ("
"   id int primary key,"
"   name varchar(255),"
"   port int unsigned,"
"   unique(name, port)"
") engine = myisam;",

"create table cluster ("
"   id int primary key,"
"   name varchar(255),"
"   unique(name)"
"   ) engine = myisam;",

"create table process ("
"  id int primary key,"
"  host_id int not null,"
"  cluster_id int not null,"
"  node_id int not null,"
"  type enum ('ndbd', 'ndbapi', 'ndb_mgmd', 'mysqld', 'mysql') not null,"
"  state enum ('starting', 'started', 'stopping', 'stopped') not null"
"  ) engine = myisam;",

"create table options ("
"  id int primary key,"
"  process_id int not null,"
"  name varchar(255) not null,"
"  value varchar(255) not null"
"  ) engine = myisam;",

"create table repl ("
"  id int auto_increment primary key,"
"  master_id int not null,"
"  slave_id int not null"
"  ) engine = myisam;",

"create table command ("
"  id int auto_increment primary key,"
"  state enum ('new', 'running', 'done') not null default 'new',"
"  cmd int not null,"
"  process_id int not null,"
"  process_args varchar(255) default NULL"
"  ) engine = myisam;",

  0};

bool
setup_db(atrt_config& config)
{
  /**
   * Install atrt db
   */
  atrt_process* atrt_client = 0;
  {
    atrt_cluster* cluster = 0;
    for (size_t i = 0; i<config.m_clusters.size(); i++)
    {
      if (strcmp(config.m_clusters[i]->m_name.c_str(), ".atrt") == 0)
      {
	cluster = config.m_clusters[i];

	for (size_t i = 0; i<cluster->m_processes.size(); i++)
	{
	  if (cluster->m_processes[i]->m_type == atrt_process::AP_CLIENT)
	  {
	    atrt_client = cluster->m_processes[i];
	    break;
	  }
	}
	break;
      }
    }
  }    
  
  /**
   * connect to all mysqld's
   */
  for (size_t i = 0; i<config.m_processes.size(); i++)
  {
    atrt_process * proc = config.m_processes[i];
    if (proc->m_type == atrt_process::AP_MYSQLD)
    {
      if (!connect_mysqld(config.m_processes[i]))
	return false;
    }
  }    
  
  if (atrt_client)
  {
    atrt_process* atrt_mysqld = atrt_client->m_mysqld;
    assert(atrt_mysqld);

    // Run the commands to create the db
    for (int i = 0; create_sql[i]; i++)
    {
      const char* query = create_sql[i];
      if (!run_query(atrt_mysqld, query))
        return false;
      g_logger.info("hello");
    }
    
    if (!populate_db(config, atrt_mysqld))
      return false;
  }
  
  /**
   * setup replication
   */
  if (setup_repl(config) != true)
    return false;
  
  return true;
}

static
const char*
find(atrt_process* proc, const char * key)
{
  const char * res = 0;
  if (proc->m_options.m_loaded.get(key, &res))
    return res;
  
  proc->m_options.m_generated.get(key, &res);
  return res;
}

bool
connect_mysqld(atrt_process* proc)
{
  if ( !mysql_init(&proc->m_mysql))
  {
    g_logger.error("Failed to init mysql");
    return false;
  }

  const char * port = find(proc, "--port=");
  const char * socket = find(proc, "--socket=");
  if (port == 0 && socket == 0)
  {
    g_logger.error("Neither socket nor port specified...cant connect to mysql");
    return false;
  }
  
  if (port)
  {
    mysql_protocol_type val = MYSQL_PROTOCOL_TCP;
    mysql_options(&proc->m_mysql, MYSQL_OPT_PROTOCOL, &val);
  }
  for (size_t i = 0; i<20; i++)
  {
    if (mysql_real_connect(&proc->m_mysql,
			   proc->m_host->m_hostname.c_str(),
			   "root", "", "test",
			   port ? atoi(port) : 0,
			   socket,
			   0))
    {
      return true;
    }
    g_logger.info("Retrying connect to %s:%u 3s",
		  proc->m_host->m_hostname.c_str(),atoi(port));
    NdbSleep_SecSleep(3); 
  }
  
  g_logger.error("Failed to connect to mysqld err: >%s< >%s:%u:%s<",
		 mysql_error(&proc->m_mysql),
		 proc->m_host->m_hostname.c_str(), port ? atoi(port) : 0,
		 socket ? socket : "<null>");
  return false;
}

void
BINDI(MYSQL_BIND& bind, int * i)
{
  bind.buffer_type= MYSQL_TYPE_LONG;
  bind.buffer= (char*)i;
  bind.is_unsigned= 0;
  bind.is_null= 0;
}

void
BINDS(MYSQL_BIND& bind, const char * s, unsigned long * len)
{
  bind.buffer_type= MYSQL_TYPE_STRING;
  bind.buffer= (char*)s;
  bind.buffer_length= * len = strlen(s);
  bind.length= len;
  bind.is_null= 0;
}

template <typename T>
int
find(T* obj, Vector<T*>& arr)
{
  for (size_t i = 0; i<arr.size(); i++)
    if (arr[i] == obj)
      return (int)i;
  abort();
  return -1;
}

static 
bool 
populate_options(MYSQL* mysql, MYSQL_STMT* stmt, int* option_id,
		 int process_id, Properties* p)
{
  int kk = *option_id;
  Properties::Iterator it(p);
  const char * name = it.first();
  for (; name; name = it.next())
  {
    int optid = kk;
    int proc_id = process_id;
    unsigned long l0, l1;
    const char * value;
    p->get(name, &value);
    MYSQL_BIND bind2[4];	
    bzero(bind2, sizeof(bind2));
    BINDI(bind2[0], &optid);
    BINDI(bind2[1], &proc_id);
    BINDS(bind2[2], name, &l0);
    BINDS(bind2[3], value, &l1);
    
    if (mysql_stmt_bind_param(stmt, bind2))
    {
      g_logger.error("Failed to bind: %s", mysql_error(mysql));
      return false;
    }
    
    if (mysql_stmt_execute(stmt))
    {
      g_logger.error("0 Failed to execute: %s", mysql_error(mysql));
      return false;
    }
    kk++;
  }
  *option_id = kk;
  return true;
}

static
bool
populate_db(atrt_config& config, atrt_process* mysqld)
{
  {
    const char * sql = "INSERT INTO host (id, name, port) values (?, ?, ?)";
    MYSQL_STMT * stmt = mysql_stmt_init(&mysqld->m_mysql);
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)))
    {
      g_logger.error("Failed to prepare: %s", mysql_error(&mysqld->m_mysql));
      return false;
    }

    for (size_t i = 0; i<config.m_hosts.size(); i++)
    {
      unsigned long l0;
      MYSQL_BIND bind[3];
      bzero(bind, sizeof(bind));
      int id = i;
      int port = config.m_hosts[i]->m_cpcd->getPort();
      BINDI(bind[0], &id);
      BINDS(bind[1], config.m_hosts[i]->m_hostname.c_str(), &l0);
      BINDI(bind[2], &port);
      if (mysql_stmt_bind_param(stmt, bind))
      {
	g_logger.error("Failed to bind: %s", mysql_error(&mysqld->m_mysql));
	return false;
      }
      
      if (mysql_stmt_execute(stmt))
      {
	g_logger.error("1 Failed to execute: %s", mysql_error(&mysqld->m_mysql));
	return false;
      }
    }
    mysql_stmt_close(stmt);
  }

  {
    const char * sql = "INSERT INTO cluster (id, name) values (?, ?)";
    MYSQL_STMT * stmt = mysql_stmt_init(&mysqld->m_mysql);
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)))
    {
      g_logger.error("Failed to prepare: %s", mysql_error(&mysqld->m_mysql));
      return false;
    }

    for (size_t i = 0; i<config.m_clusters.size(); i++)
    {
      unsigned long l0;
      MYSQL_BIND bind[2];
      bzero(bind, sizeof(bind));
      int id = i;
      BINDI(bind[0], &id);
      BINDS(bind[1], config.m_clusters[i]->m_name.c_str(), &l0);

      if (mysql_stmt_bind_param(stmt, bind))
      {
	g_logger.error("Failed to bind: %s", mysql_error(&mysqld->m_mysql));
	return false;
      }
      
      if (mysql_stmt_execute(stmt))
      {
	g_logger.error("2 Failed to execute: %s", mysql_error(&mysqld->m_mysql));
	return false;
      }
    }
    mysql_stmt_close(stmt);
  }

  {
    const char * sql = 
      "INSERT INTO process (id, host_id, cluster_id, type, state, node_id) values (?,?,?,?,?,?)";

    const char * sqlopt =
      "INSERT INTO options (id, process_id, name, value) values (?,?,?,?)";

    MYSQL_STMT * stmt = mysql_stmt_init(&mysqld->m_mysql);
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)))
    {
      g_logger.error("Failed to prepare: %s", mysql_error(&mysqld->m_mysql));
      return false;
    }

    MYSQL_STMT * stmtopt = mysql_stmt_init(&mysqld->m_mysql);
    if (mysql_stmt_prepare(stmtopt, sqlopt, strlen(sqlopt)))
    {
      g_logger.error("Failed to prepare: %s", mysql_error(&mysqld->m_mysql));
      return false;
    }

    int option_id = 0;
    for (size_t i = 0; i<config.m_processes.size(); i++)
    {
      unsigned long l0, l1;
      MYSQL_BIND bind[6];
      bzero(bind, sizeof(bind));
      int id = i;
      atrt_process* proc = config.m_processes[i];
      int host_id = find(proc->m_host, config.m_hosts);
      int cluster_id = find(proc->m_cluster, config.m_clusters);
      int node_id= proc->m_nodeid;

      const char * type = 0;
      const char * state = "started";
      switch(proc->m_type){
      case atrt_process::AP_NDBD:     type = "ndbd"; break;
      case atrt_process::AP_NDB_API:  type = "ndbapi"; state = "stopped";break;
      case atrt_process::AP_NDB_MGMD: type = "ndb_mgmd"; break;
      case atrt_process::AP_MYSQLD:   type = "mysqld"; break;
      case atrt_process::AP_CLIENT:   type = "mysql"; state = "stopped";break;
      default:
	abort();
      }

      BINDI(bind[0], &id);
      BINDI(bind[1], &host_id);
      BINDI(bind[2], &cluster_id);
      BINDS(bind[3], type, &l0);
      BINDS(bind[4], state, &l1);
      BINDI(bind[5], &node_id);

      if (mysql_stmt_bind_param(stmt, bind))
      {
	g_logger.error("Failed to bind: %s", mysql_error(&mysqld->m_mysql));
	return false;
      }
      
      if (mysql_stmt_execute(stmt))
      {
	g_logger.error("3 Failed to execute: %s", mysql_error(&mysqld->m_mysql));
	return false;
      }

      if (populate_options(&mysqld->m_mysql, stmtopt, &option_id, id,
			   &proc->m_options.m_loaded) == false)
	return false;
      
      if (populate_options(&mysqld->m_mysql, stmtopt, &option_id, id,
			   &proc->m_cluster->m_options.m_loaded) == false)
	return false;
      
    }
    mysql_stmt_close(stmt);
    mysql_stmt_close(stmtopt);
  }
  
  return true;
}

static
bool
setup_repl(atrt_process* dst, atrt_process* src)
{
  if (!run_query(src, "STOP SLAVE"))
  {
    g_logger.error("Failed to stop slave: %s",
		   mysql_error(&src->m_mysql));
    return false;
  }

  if (!run_query(src, "RESET SLAVE"))
  {
    g_logger.error("Failed to reset slave: %s",
		   mysql_error(&src->m_mysql));
    return false;
  }
  
  BaseString tmp;
  tmp.assfmt("CHANGE MASTER TO   "
	     " MASTER_HOST='%s', "
	     " MASTER_PORT=%u    ",
	     dst->m_host->m_hostname.c_str(),
	     atoi(find(dst, "--port=")));
  
  if (!run_query(src, tmp.c_str()))
  {
    g_logger.error("Failed to setup repl from %s to %s: %s",
		   src->m_host->m_hostname.c_str(),
		   dst->m_host->m_hostname.c_str(),
		   mysql_error(&src->m_mysql));
    return false;
  }

  if (!run_query(src, "START SLAVE"))
  {
    g_logger.error("Failed to start slave: %s",
		   mysql_error(&src->m_mysql));
    return false;
  }
  
  g_logger.info("Replication from %s(%s) to %s(%s) setup",
		src->m_host->m_hostname.c_str(),
		src->m_cluster->m_name.c_str(),
		dst->m_host->m_hostname.c_str(),
		dst->m_cluster->m_name.c_str());

  return true;
}

bool
setup_repl(atrt_config& config)
{
  for (size_t i = 0; i<config.m_processes.size(); i++)
  {
    atrt_process * dst = config.m_processes[i];
    if (dst->m_rep_src)
    {
      if (setup_repl(dst->m_rep_src, dst) != true)
	return false;
    }
  }
  return true;
}

template int find(atrt_host* obj, Vector<atrt_host*>& arr);
template int find(atrt_cluster* obj, Vector<atrt_cluster*>& arr);

