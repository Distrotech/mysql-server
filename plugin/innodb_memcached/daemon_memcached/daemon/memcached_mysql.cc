/***********************************************************************

Copyright (c) 2010, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

***********************************************************************/

#include "memcached_mysql.h"
#include <stdlib.h>
#include <ctype.h>
#include <mysql_version.h>
#include "sql_plugin.h"


struct mysql_memcached_context
{
	pthread_t		memcached_thread;
	memcached_context_t	memcached_conf;
};

/** Configuration info passed to memcached, including
the name of our Memcached InnoDB engine and memcached configure
string to be loaded by memcached. */
static char*	mci_engine_library = NULL;
static char*	mci_eng_lib_path = NULL; 
static char*	mci_memcached_option = NULL;
static unsigned int mci_r_batch_size = 1048576;
static unsigned int mci_w_batch_size = 1;

static MYSQL_SYSVAR_STR(engine_lib_name, mci_engine_library,
			PLUGIN_VAR_READONLY | PLUGIN_VAR_MEMALLOC,
			"memcached engine library name", NULL, NULL,
			"innodb_engine.so");

static MYSQL_SYSVAR_STR(engine_lib_path, mci_eng_lib_path,
			PLUGIN_VAR_READONLY | PLUGIN_VAR_MEMALLOC,
			"memcached engine library path", NULL, NULL, NULL);

static MYSQL_SYSVAR_STR(option, mci_memcached_option,
			PLUGIN_VAR_READONLY | PLUGIN_VAR_MEMALLOC,
			"memcached option string", NULL, NULL, NULL);

static MYSQL_SYSVAR_UINT(r_batch_size, mci_r_batch_size,
			 PLUGIN_VAR_READONLY,
			 "read batch commit size", 0, 0, 1048576,
			 1073741824, 0, 0);

static MYSQL_SYSVAR_UINT(w_batch_size, mci_w_batch_size,
			 PLUGIN_VAR_READONLY,
			 "write batch commit size", 0, 0, 1,
			 1048576, 0, 0);

static struct st_mysql_sys_var *daemon_memcached_sys_var[] = {
	MYSQL_SYSVAR(engine_lib_name),
	MYSQL_SYSVAR(engine_lib_path),
	MYSQL_SYSVAR(option),
	MYSQL_SYSVAR(r_batch_size),
	MYSQL_SYSVAR(w_batch_size),
	0
};

static int daemon_memcached_plugin_deinit(void *p)
{
	struct st_plugin_int* plugin = (struct st_plugin_int *)p;
	struct mysql_memcached_context* con = NULL;

	shutdown_server();

	sleep(2);

	con = (struct mysql_memcached_context*) (plugin->data);

	pthread_cancel(con->memcached_thread);

	if (con->memcached_conf.m_engine_library) {
		my_free(con->memcached_conf.m_engine_library);
	}

	my_free(con);

	return(0);
}

static int daemon_memcached_plugin_init(void *p)
{
	struct mysql_memcached_context*	con;
	pthread_attr_t			attr;
	struct st_plugin_int*		plugin = (struct st_plugin_int *)p;

	con = (mysql_memcached_context*) malloc(sizeof(*con));

	if (mci_engine_library) {
		char* lib_path = (mci_eng_lib_path)
				 ? mci_eng_lib_path : opt_plugin_dir;

		con->memcached_conf.m_engine_library = (char*) malloc(
			strlen(lib_path) + strlen(mci_engine_library) + 1);

		strxmov(con->memcached_conf.m_engine_library, lib_path,
			FN_DIRSEP, mci_engine_library, NullS);
	} else {
		con->memcached_conf.m_engine_library = NULL;
	}

	con->memcached_conf.m_mem_option = mci_memcached_option;
	con->memcached_conf.m_innodb_api_cb = plugin->data;
	con->memcached_conf.m_r_batch_size = mci_r_batch_size;
	con->memcached_conf.m_w_batch_size = mci_w_batch_size;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	/* now create the thread */
	if (pthread_create(&con->memcached_thread, &attr,
			   daemon_memcached_main,
			   (void *)&con->memcached_conf) != 0)
	{
		fprintf(stderr,"Could not create memcached daemon thread!\n");
		exit(0);
	}

	plugin->data= (void *)con;

	return(0);
}

struct st_mysql_daemon daemon_memcached_plugin =
	{MYSQL_DAEMON_INTERFACE_VERSION};

mysql_declare_plugin(daemon_memcached)
{
	MYSQL_DAEMON_PLUGIN,
	&daemon_memcached_plugin,
	"daemon_memcached",
	"Jimmy Yang",
	"Memcached Daemon",
	PLUGIN_LICENSE_GPL,
	daemon_memcached_plugin_init,	/* Plugin Init */
	daemon_memcached_plugin_deinit,	/* Plugin Deinit */
	0x0100 /* 1.0 */,
	NULL,				/* status variables */
	daemon_memcached_sys_var,	/* system variables */
	NULL				/* config options */
}
mysql_declare_plugin_end;
