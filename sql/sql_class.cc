/* Copyright (C) 2000 MySQL AB & MySQL Finland AB & TCX DataKonsult AB

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


/*****************************************************************************
**
** This file implements classes defined in sql_class.h
** Especially the classes to handle a result from a select
**
*****************************************************************************/

#ifdef __GNUC__
#pragma implementation				// gcc: Class implementation
#endif

#include "mysql_priv.h"
#include "sql_acl.h"
#include <m_ctype.h>
#include <sys/stat.h>
#include <thr_alarm.h>
#ifdef	__WIN__
#include <io.h>
#endif
#include <mysys_err.h>
#include <assert.h>

/*****************************************************************************
** Instansiate templates
*****************************************************************************/

#ifdef __GNUC__
/* Used templates */
template class List<Key>;
template class List_iterator<Key>;
template class List<key_part_spec>;
template class List_iterator<key_part_spec>;
template class List<Alter_drop>;
template class List_iterator<Alter_drop>;
template class List<Alter_column>;
template class List_iterator<Alter_column>;
template class List<Set_option>;
template class List_iterator<Set_option>;
#endif

/****************************************************************************
** User variables
****************************************************************************/

static byte* get_var_key(user_var_entry *entry, uint *length,
			 my_bool not_used __attribute__((unused)))
{
  *length=(uint) entry->name.length;
  return (byte*) entry->name.str;
}

static void free_var(user_var_entry *entry)
{
  char *pos= (char*) entry+ALIGN_SIZE(sizeof(*entry));
  if (entry->value && entry->value != pos)
    my_free(entry->value, MYF(0));
  my_free((char*) entry,MYF(0));
}


/****************************************************************************
** Thread specific functions
****************************************************************************/

THD::THD():user_time(0),fatal_error(0),last_insert_id_used(0),
	   insert_id_used(0),in_lock_tables(0),
	   global_read_lock(0),bootstrap(0)
{
  host=user=priv_user=db=query=ip=0;
  host_or_ip="unknown ip";
  locked=killed=count_cuted_fields=some_tables_deleted=no_errors=password=
    query_start_used=safe_to_cache_query=0;
  db_length=query_length=col_access=0;
  query_error=0;
  next_insert_id=last_insert_id=0;
  open_tables=temporary_tables=handler_tables=0;
  handler_items=0;
  tmp_table=0;
  lock=locked_tables=0;
  used_tables=0;
  cuted_fields=sent_row_count=0L;
  start_time=(time_t) 0;
  current_linfo =  0;
  slave_thread = 0;
  slave_proxy_id = 0;
  file_id = 0;
  cond_count=0;
  convert_set=0;
  mysys_var=0;
#ifndef DBUG_OFF
  dbug_sentry=THD_SENTRY_MAGIC;
#endif  
  net.vio=0;
  ull=0;
  system_thread=cleanup_done=0;
  transaction.changed_tables = 0;
#ifdef	__WIN__
  real_id = 0;
#endif
#ifdef SIGNAL_WITH_VIO_CLOSE
  active_vio = 0;
  pthread_mutex_init(&active_vio_lock, MY_MUTEX_INIT_FAST);
#endif  

  /* Variables with default values */
  proc_info="login";
  where="field list";
  server_id = ::server_id;
  slave_net = 0;
  log_pos = 0;
  server_status= SERVER_STATUS_AUTOCOMMIT;
  update_lock_default= low_priority_updates ? TL_WRITE_LOW_PRIORITY : TL_WRITE;
  options= thd_startup_options;
#ifdef HAVE_QUERY_CACHE
  query_cache_type= (byte) query_cache_startup_type;
#else
  query_cache_type= 0; //Safety
#endif
  sql_mode=(uint) opt_sql_mode;
  inactive_timeout=net_wait_timeout;
  open_options=ha_open_options;
  tx_isolation=session_tx_isolation=default_tx_isolation;
  command=COM_CONNECT;
  set_query_id=1;
  default_select_limit= HA_POS_ERROR;
  max_join_size=  ((::max_join_size != ~ (ulong) 0L) ? ::max_join_size :
		   HA_POS_ERROR);
  db_access=NO_ACCESS;

  /* Initialize sub structures */
  bzero((char*) &mem_root,sizeof(mem_root));
  bzero((char*) &transaction.mem_root,sizeof(transaction.mem_root));
  user_connect=(USER_CONN *)0;
  hash_init(&user_vars, system_charset_info, USER_VARS_HASH_SIZE, 0, 0,
	    (hash_get_key) get_var_key,
	    (void (*)(void*)) free_var,0);
#ifdef USING_TRANSACTIONS
  bzero((char*) &transaction,sizeof(transaction));
  if (opt_using_transactions)
  {
    if (open_cached_file(&transaction.trans_log,
			 mysql_tmpdir, LOG_PREFIX, binlog_cache_size,
			 MYF(MY_WME)))
      killed=1;
    transaction.trans_log.end_of_file= max_binlog_cache_size;
  }
#endif
}

/* Do operations that may take a long time */

void THD::cleanup(void)
{
  DBUG_ENTER("THD::cleanup");
  ha_rollback(this);
  if (locked_tables)
  {
    lock=locked_tables; locked_tables=0;
    close_thread_tables(this);
  }
  if (handler_tables)
  {
    open_tables=handler_tables; handler_tables=0;
    close_thread_tables(this);
  }
  close_temporary_tables(this);
#ifdef USING_TRANSACTIONS
  if (opt_using_transactions)
  {
    close_cached_file(&transaction.trans_log);
    ha_close_connection(this);
  }
#endif
  cleanup_done=1;
  DBUG_VOID_RETURN;
}

THD::~THD()
{
  THD_CHECK_SENTRY(this);
  DBUG_ENTER("~THD()");
  /* Close connection */
  if (net.vio)
  {
    vio_delete(net.vio);
    net_end(&net); 
  }
  if (!cleanup_done)
    cleanup();
  if (global_read_lock)
    unlock_global_read_lock(this);
  if (ull)
  {
    pthread_mutex_lock(&LOCK_user_locks);
    item_user_lock_release(ull);
    pthread_mutex_unlock(&LOCK_user_locks);
  }
  hash_free(&user_vars);

  DBUG_PRINT("info", ("freeing host"));

  if (host != localhost)			// If not pointer to constant
    safeFree(host);
  if (user != delayed_user)
    safeFree(user);
  safeFree(db);
  safeFree(ip);
  free_root(&mem_root,MYF(0));
  free_root(&transaction.mem_root,MYF(0));
  mysys_var=0;					// Safety (shouldn't be needed)
#ifdef SIGNAL_WITH_VIO_CLOSE
  pthread_mutex_destroy(&active_vio_lock);
#endif
#ifndef DBUG_OFF
  dbug_sentry = THD_SENTRY_GONE;
#endif  
  DBUG_VOID_RETURN;
}

void THD::awake(bool prepare_to_die)
{
  THD_CHECK_SENTRY(this);
  if (prepare_to_die)
    killed = 1;
  thr_alarm_kill(real_id);
#ifdef SIGNAL_WITH_VIO_CLOSE
  close_active_vio();
#endif    
  if (mysys_var)
    {
      pthread_mutex_lock(&mysys_var->mutex);
      if (!system_thread)		// Don't abort locks
	mysys_var->abort=1;
      // this broadcast could be up in the air if the victim thread
      // exits the cond in the time between read and broadcast, but that is
      // ok since all we want to do is to make the victim thread get out
      // of waiting on  current_cond
      if (mysys_var->current_cond)
      {
	pthread_mutex_lock(mysys_var->current_mutex);
	pthread_cond_broadcast(mysys_var->current_cond);
	pthread_mutex_unlock(mysys_var->current_mutex);
      }
      pthread_mutex_unlock(&mysys_var->mutex);
    }
}

// remember the location of thread info, the structure needed for
// sql_alloc() and the structure for the net buffer

bool THD::store_globals()
{
  return (my_pthread_setspecific_ptr(THR_THD,  this) ||
	  my_pthread_setspecific_ptr(THR_MALLOC, &mem_root) ||
	  my_pthread_setspecific_ptr(THR_NET,  &net));
}

/* routings to adding tables to list of changed in transaction tables */

inline static void list_include(CHANGED_TABLE_LIST** prev,
				CHANGED_TABLE_LIST* curr,
				CHANGED_TABLE_LIST* new_table)
{
  if (new_table)
  {
    *prev = new_table;
    (*prev)->next = curr;
  }
}

/* add table to list of changed in transaction tables */
void THD::add_changed_table(TABLE *table)
{
  DBUG_ENTER("THD::add_changed_table (table)");

  DBUG_ASSERT((options & (OPTION_NOT_AUTO_COMMIT | OPTION_BEGIN)) &&
		table->file->has_transactions());

  CHANGED_TABLE_LIST** prev = &transaction.changed_tables;
  CHANGED_TABLE_LIST* curr = transaction.changed_tables;

  for(; curr; prev = &(curr->next), curr = curr->next)
  {
    int cmp =  (long)curr->key_length - (long)table->key_length;
    if (cmp < 0)
    {
      list_include(prev, curr, changed_table_dup(table));
      DBUG_PRINT("info", 
		 ("key_length %u %u", table->key_length, (*prev)->key_length));
      DBUG_VOID_RETURN;
    }
    else if (cmp == 0)
    {
      cmp = memcmp(curr->key ,table->table_cache_key, curr->key_length);
      if (cmp < 0)
      {
	list_include(prev, curr, changed_table_dup(table));
	DBUG_PRINT("info", 
		   ("key_length %u %u", table->key_length, (*prev)->key_length));
	DBUG_VOID_RETURN;
      }
      else if (cmp == 0)
      {
	DBUG_PRINT("info", ("already in list"));
	DBUG_VOID_RETURN;
      }
    }
  }
  *prev = changed_table_dup(table);
  DBUG_PRINT("info", ("key_length %u %u", table->key_length, (*prev)->key_length));
  DBUG_VOID_RETURN;
}

CHANGED_TABLE_LIST* THD::changed_table_dup(TABLE *table)
{
  CHANGED_TABLE_LIST* new_table = 
    (CHANGED_TABLE_LIST*) trans_alloc(ALIGN_SIZE(sizeof(CHANGED_TABLE_LIST))+
				      table->key_length + 1);
  if (!new_table)
  {
    my_error(EE_OUTOFMEMORY, MYF(ME_BELL),
	     ALIGN_SIZE(sizeof(TABLE_LIST)) + table->key_length + 1);
    killed= 1;
    return 0;
  }

  new_table->key = (char *) (((byte*)new_table)+
			     ALIGN_SIZE(sizeof(CHANGED_TABLE_LIST)));
  new_table->next = 0;
  new_table->key_length = table->key_length;
  uint32 db_len = ((new_table->table_name =
		    ::strmake(new_table->key, table->table_cache_key, 
			      table->key_length) + 1) - new_table->key);
  ::memcpy(new_table->key + db_len, table->table_cache_key + db_len,
	   table->key_length - db_len);
  return new_table;
}


/*****************************************************************************
** Functions to provide a interface to select results
*****************************************************************************/

select_result::select_result()
{
  thd=current_thd;
}

static String
	default_line_term("\n",default_charset_info),
	default_escaped("\\",default_charset_info),
	default_field_term("\t",default_charset_info);

sql_exchange::sql_exchange(char *name,bool flag)
  :file_name(name), opt_enclosed(0), dumpfile(flag), skip_lines(0)
{
  field_term= &default_field_term;
  enclosed=   line_start= &empty_string;
  line_term=  &default_line_term;
  escaped=    &default_escaped;
}

bool select_send::send_fields(List<Item> &list,uint flag)
{
  return ::send_fields(thd,list,flag);
}


/* Send data to client. Returns 0 if ok */

bool select_send::send_data(List<Item> &items)
{
  List_iterator_fast<Item> li(items);
  String *packet= &thd->packet;
  DBUG_ENTER("send_data");

  if (unit->offset_limit_cnt)
  {						// using limit offset,count
    unit->offset_limit_cnt--;
    DBUG_RETURN(0);
  }
  packet->length(0);				// Reset packet
  Item *item;
  while ((item=li++))
  {
    if (item->send(thd, packet))
    {
      packet->free();				// Free used
      my_error(ER_OUT_OF_RESOURCES,MYF(0));
      DBUG_RETURN(1);
    }
  }
  thd->sent_row_count++;
  bool error=my_net_write(&thd->net,(char*) packet->ptr(),packet->length());
  DBUG_RETURN(error);
}

bool select_send::send_eof()
{
  /* Unlock tables before sending packet to gain some speed */
  if (thd->lock)
  {
    mysql_unlock_tables(thd, thd->lock); thd->lock=0;
  }
  ::send_eof(&thd->net);
  return 0;
}


/***************************************************************************
** Export of select to textfile
***************************************************************************/


select_export::~select_export()
{
  if (file >= 0)
  {					// This only happens in case of error
    (void) end_io_cache(&cache);
    (void) my_close(file,MYF(0));
    file= -1;
  }
  thd->sent_row_count=row_count;
}

int
select_export::prepare(List<Item> &list, SELECT_LEX_UNIT *u)
{
  char path[FN_REFLEN];
  uint option=4;
  bool blob_flag=0;
  unit= u;
#ifdef DONT_ALLOW_FULL_LOAD_DATA_PATHS
  option|=1;					// Force use of db directory
#endif
  if ((uint) strlen(exchange->file_name) + NAME_LEN >= FN_REFLEN)
    strmake(path,exchange->file_name,FN_REFLEN-1);
  (void) fn_format(path,exchange->file_name, thd->db ? thd->db : "", "",
		   option);
  if (!access(path,F_OK))
  {
    my_error(ER_FILE_EXISTS_ERROR,MYF(0),exchange->file_name);
    return 1;
  }
  /* Create the file world readable */
  if ((file=my_create(path, 0666, O_WRONLY, MYF(MY_WME))) < 0)
    return 1;
#ifdef HAVE_FCHMOD
  (void) fchmod(file,0666);			// Because of umask()
#else
  (void) chmod(path,0666);
#endif
  if (init_io_cache(&cache,file,0L,WRITE_CACHE,0L,1,MYF(MY_WME)))
  {
    my_close(file,MYF(0));
    file= -1;
    return 1;
  }
  /* Check if there is any blobs in data */
  {
    List_iterator_fast<Item> li(list);
    Item *item;
    while ((item=li++))
    {
      if (item->max_length >= MAX_BLOB_WIDTH)
      {
	blob_flag=1;
	break;
      }
    }
  }
  field_term_length=exchange->field_term->length();
  if (!exchange->line_term->length())
    exchange->line_term=exchange->field_term;	// Use this if it exists
  field_sep_char= (exchange->enclosed->length() ? (*exchange->enclosed)[0] :
		   field_term_length ? (*exchange->field_term)[0] : INT_MAX);
  escape_char=	(exchange->escaped->length() ? (*exchange->escaped)[0] : -1);
  line_sep_char= (exchange->line_term->length() ?
		  (*exchange->line_term)[0] : INT_MAX);
  if (!field_term_length)
    exchange->opt_enclosed=0;
  if (!exchange->enclosed->length())
    exchange->opt_enclosed=1;			// A little quicker loop
  fixed_row_size= (!field_term_length && !exchange->enclosed->length() &&
		   !blob_flag);
  return 0;
}


bool select_export::send_data(List<Item> &items)
{

  DBUG_ENTER("send_data");
  char buff[MAX_FIELD_WIDTH],null_buff[2],space[MAX_FIELD_WIDTH];
  bool space_inited=0;
  String tmp(buff,sizeof(buff),default_charset_info),*res;
  tmp.length(0);

  if (unit->offset_limit_cnt)
  {						// using limit offset,count
    unit->offset_limit_cnt--;
    DBUG_RETURN(0);
  }
  row_count++;
  Item *item;
  char *buff_ptr=buff;
  uint used_length=0,items_left=items.elements;
  List_iterator_fast<Item> li(items);

  if (my_b_write(&cache,(byte*) exchange->line_start->ptr(),
		 exchange->line_start->length()))
    goto err;
  while ((item=li++))
  {
    Item_result result_type=item->result_type();
    res=item->str_result(&tmp);
    if (res && (!exchange->opt_enclosed || result_type == STRING_RESULT))
    {
      if (my_b_write(&cache,(byte*) exchange->enclosed->ptr(),
		     exchange->enclosed->length()))
	goto err;
    }
    if (!res)
    {						// NULL
      if (!fixed_row_size)
      {
	if (escape_char != -1)			// Use \N syntax
	{
	  null_buff[0]=escape_char;
	  null_buff[1]='N';
	  if (my_b_write(&cache,(byte*) null_buff,2))
	    goto err;
	}
	else if (my_b_write(&cache,(byte*) "NULL",4))
	  goto err;
      }
      else
      {
	used_length=0;				// Fill with space
      }
    }
    else
    {
      if (fixed_row_size)
	used_length=min(res->length(),item->max_length);
      else
	used_length=res->length();
      if (result_type == STRING_RESULT && escape_char != -1)
      {
	char *pos,*start,*end;

	for (start=pos=(char*) res->ptr(),end=pos+used_length ;
	     pos != end ;
	     pos++)
	{
#ifdef USE_MB
	  if (use_mb(default_charset_info))
	  {
	    int l;
	    if ((l=my_ismbchar(default_charset_info, pos, end)))
	    {
	      pos += l-1;
	      continue;
	    }
	  }
#endif
	  if ((int) *pos == escape_char || (int) *pos == field_sep_char ||
	      (int) *pos == line_sep_char || !*pos)
	  {
	    char tmp_buff[2];
	    tmp_buff[0]= escape_char;
	    tmp_buff[1]= *pos ? *pos : '0';
	    if (my_b_write(&cache,(byte*) start,(uint) (pos-start)) ||
		my_b_write(&cache,(byte*) tmp_buff,2))
	      goto err;
	    start=pos+1;
	  }
	}
	if (my_b_write(&cache,(byte*) start,(uint) (pos-start)))
	  goto err;
      }
      else if (my_b_write(&cache,(byte*) res->ptr(),used_length))
	goto err;
    }
    if (fixed_row_size)
    {						// Fill with space
      if (item->max_length > used_length)
      {
	/* QQ:  Fix by adding a my_b_fill() function */
	if (!space_inited)
	{
	  space_inited=1;
	  bfill(space,sizeof(space),' ');
	}
	uint length=item->max_length-used_length;
	for ( ; length > sizeof(space) ; length-=sizeof(space))
	{
	  if (my_b_write(&cache,(byte*) space,sizeof(space)))
	    goto err;
	}
	if (my_b_write(&cache,(byte*) space,length))
	  goto err;
      }
    }
    buff_ptr=buff;				// Place separators here
    if (res && (!exchange->opt_enclosed || result_type == STRING_RESULT))
    {
      memcpy(buff_ptr,exchange->enclosed->ptr(),exchange->enclosed->length());
      buff_ptr+=exchange->enclosed->length();
    }
    if (--items_left)
    {
      memcpy(buff_ptr,exchange->field_term->ptr(),field_term_length);
      buff_ptr+=field_term_length;
    }
    if (my_b_write(&cache,(byte*) buff,(uint) (buff_ptr-buff)))
      goto err;
  }
  if (my_b_write(&cache,(byte*) exchange->line_term->ptr(),
		 exchange->line_term->length()))
    goto err;
  DBUG_RETURN(0);
err:
  DBUG_RETURN(1);
}


void select_export::send_error(uint errcode,const char *err)
{
  ::send_error(&thd->net,errcode,err);
  (void) end_io_cache(&cache);
  (void) my_close(file,MYF(0));
  file= -1;
}


bool select_export::send_eof()
{
  int error=test(end_io_cache(&cache));
  if (my_close(file,MYF(MY_WME)))
    error=1;
  if (error)
    ::send_error(&thd->net);
  else
    ::send_ok(&thd->net,row_count);
  file= -1;
  return error;
}


/***************************************************************************
** Dump  of select to a binary file
***************************************************************************/


select_dump::~select_dump()
{
  if (file >= 0)
  {					// This only happens in case of error
    (void) end_io_cache(&cache);
    (void) my_close(file,MYF(0));
    file= -1;
  }
}

int
select_dump::prepare(List<Item> &list __attribute__((unused)),
		     SELECT_LEX_UNIT *u)
{
  uint option=4;
  unit= u;
#ifdef DONT_ALLOW_FULL_LOAD_DATA_PATHS
  option|=1;					// Force use of db directory
#endif
  (void) fn_format(path,exchange->file_name, thd->db ? thd->db : "", "",
		   option);
  if (!access(path,F_OK))
  {
    my_error(ER_FILE_EXISTS_ERROR,MYF(0),exchange->file_name);
    return 1;
  }
  /* Create the file world readable */
  if ((file=my_create(path, 0666, O_WRONLY, MYF(MY_WME))) < 0)
    return 1;
#ifdef HAVE_FCHMOD
  (void) fchmod(file,0666);			// Because of umask()
#else
  (void) chmod(path,0666);
#endif
  if (init_io_cache(&cache,file,0L,WRITE_CACHE,0L,1,MYF(MY_WME)))
  {
    my_close(file,MYF(0));
    my_delete(path,MYF(0));
    file= -1;
    return 1;
  }
  return 0;
}


bool select_dump::send_data(List<Item> &items)
{
  List_iterator_fast<Item> li(items);
  char buff[MAX_FIELD_WIDTH];
  String tmp(buff,sizeof(buff),default_charset_info),*res;
  tmp.length(0);
  Item *item;
  DBUG_ENTER("send_data");

  if (unit->offset_limit_cnt)
  {						// using limit offset,count
    unit->offset_limit_cnt--;
    DBUG_RETURN(0);
  }
  if (row_count++ > 1) 
  {
    my_error(ER_TOO_MANY_ROWS,MYF(0));
    goto err;
  }
  while ((item=li++))
  {
    res=item->str_result(&tmp);
    if (!res)					// If NULL
    {
      if (my_b_write(&cache,(byte*) "",1))
	goto err;
    }
    else if (my_b_write(&cache,(byte*) res->ptr(),res->length()))
    {
      my_error(ER_ERROR_ON_WRITE,MYF(0), path, my_errno);
      goto err;
    }
  }
  DBUG_RETURN(0);
err:
  DBUG_RETURN(1);
}


void select_dump::send_error(uint errcode,const char *err)
{
  ::send_error(&thd->net,errcode,err);
  (void) end_io_cache(&cache);
  (void) my_close(file,MYF(0));
  (void) my_delete(path,MYF(0));		// Delete file on error
  file= -1;
}


bool select_dump::send_eof()
{
  int error=test(end_io_cache(&cache));
  if (my_close(file,MYF(MY_WME)))
    error=1;
  if (error)
    ::send_error(&thd->net);
  else
    ::send_ok(&thd->net,row_count);
  file= -1;
  return error;
}

select_subselect::select_subselect(Item_subselect *item)
{
  this->item=item;
}

bool select_subselect::send_data(List<Item> &items)
{
  DBUG_ENTER("select_subselect::send_data");
  if (item->executed){
    my_printf_error(ER_SUBSELECT_NO_1_ROW, ER(ER_SUBSELECT_NO_1_ROW), MYF(0));
    DBUG_RETURN(1);
  }
  if (unit->offset_limit_cnt)
  {				          // Using limit offset,count
    unit->offset_limit_cnt--;
    DBUG_RETURN(0);
  }
  List_iterator_fast<Item> li(items);
  Item *val_item= li++;                   // Only one (single value subselect)
  /*
    Following val() call have to be first, because function AVG() & STD()
    calculate value on it & determinate "is it NULL?".
  */
  item->real_value= val_item->val();
  if ((item->null_value= val_item->is_null()))
  {
    item->assign_null();
  } else {
    item->max_length= val_item->max_length;
    item->decimals= val_item->decimals;
    item->binary= val_item->binary;
    val_item->val_str(&item->str_value);
    item->int_value= val_item->val_int();
    item->res_type= val_item->result_type();
  }
  item->executed= 1;
  DBUG_RETURN(0);
}
