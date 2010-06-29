/* Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.

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


#include "sql_priv.h"
#include "unireg.h"
#include "sql_parse.h"                          // check_access
#ifdef HAVE_REPLICATION

#include "sql_acl.h"                            // SUPER_ACL
#include "log_event.h"
#include "rpl_filter.h"
#include <my_dir.h>
#include "rpl_handler.h"
#include "rpl_master.h"

int max_binlog_dump_events = 0; // unlimited
my_bool opt_sporadic_binlog_dump_fail = 0;

#ifndef DBUG_OFF
static int binlog_dump_count = 0;
#endif

/*
    fake_rotate_event() builds a fake (=which does not exist physically in any
    binlog) Rotate event, which contains the name of the binlog we are going to
    send to the slave (because the slave may not know it if it just asked for
    MASTER_LOG_FILE='', MASTER_LOG_POS=4).
    < 4.0.14, fake_rotate_event() was called only if the requested pos was 4.
    After this version we always call it, so that a 3.23.58 slave can rely on
    it to detect if the master is 4.0 (and stop) (the _fake_ Rotate event has
    zeros in the good positions which, by chance, make it possible for the 3.23
    slave to detect that this event is unexpected) (this is luck which happens
    because the master and slave disagree on the size of the header of
    Log_event).

    Relying on the event length of the Rotate event instead of these
    well-placed zeros was not possible as Rotate events have a variable-length
    part.
*/

static int fake_rotate_event(NET* net, String* packet, char* log_file_name,
                             ulonglong position, const char** errmsg)
{
  DBUG_ENTER("fake_rotate_event");
  char header[LOG_EVENT_HEADER_LEN], buf[ROTATE_HEADER_LEN+100];
  /*
    'when' (the timestamp) is set to 0 so that slave could distinguish between
    real and fake Rotate events (if necessary)
  */
  memset(header, 0, 4);
  header[EVENT_TYPE_OFFSET] = ROTATE_EVENT;

  char* p = log_file_name+dirname_length(log_file_name);
  uint ident_len = (uint) strlen(p);
  ulong event_len = ident_len + LOG_EVENT_HEADER_LEN + ROTATE_HEADER_LEN;
  int4store(header + SERVER_ID_OFFSET, server_id);
  int4store(header + EVENT_LEN_OFFSET, event_len);
  int2store(header + FLAGS_OFFSET, LOG_EVENT_ARTIFICIAL_F);

  // TODO: check what problems this may cause and fix them
  int4store(header + LOG_POS_OFFSET, 0);

  packet->append(header, sizeof(header));
  int8store(buf+R_POS_OFFSET,position);
  packet->append(buf, ROTATE_HEADER_LEN);
  packet->append(p,ident_len);
  if (my_net_write(net, (uchar*) packet->ptr(), packet->length()))
  {
    *errmsg = "failed on my_net_write()";
    DBUG_RETURN(-1);
  }
  DBUG_RETURN(0);
}

/*
  Reset thread transmit packet buffer for event sending

  This function allocates header bytes for event transmission, and
  should be called before store the event data to the packet buffer.
*/
static int reset_transmit_packet(THD *thd, ushort flags,
                                 ulong *ev_offset, const char **errmsg)
{
  int ret= 0;
  String *packet= &thd->packet;

  /* reserve and set default header */
  packet->length(0);
  packet->set("\0", 1, &my_charset_bin);

  if (RUN_HOOK(binlog_transmit, reserve_header, (thd, flags, packet)))
  {
    *errmsg= "Failed to run hook 'reserve_header'";
    my_errno= ER_UNKNOWN_ERROR;
    ret= 1;
  }
  *ev_offset= packet->length();
  return ret;
}

static int send_file(THD *thd)
{
  NET* net = &thd->net;
  int fd = -1, error = 1;
  size_t bytes;
  char fname[FN_REFLEN+1];
  const char *errmsg = 0;
  int old_timeout;
  unsigned long packet_len;
  uchar buf[IO_SIZE];				// It's safe to alloc this
  DBUG_ENTER("send_file");

  /*
    The client might be slow loading the data, give him wait_timeout to do
    the job
  */
  old_timeout= net->read_timeout;
  my_net_set_read_timeout(net, thd->variables.net_wait_timeout);

  /*
    We need net_flush here because the client will not know it needs to send
    us the file name until it has processed the load event entry
  */
  if (net_flush(net) || (packet_len = my_net_read(net)) == packet_error)
  {
    errmsg = "while reading file name";
    goto err;
  }

  // terminate with \0 for fn_format
  *((char*)net->read_pos +  packet_len) = 0;
  fn_format(fname, (char*) net->read_pos + 1, "", "", 4);
  // this is needed to make replicate-ignore-db
  if (!strcmp(fname,"/dev/null"))
    goto end;

  if ((fd= mysql_file_open(key_file_send_file,
                           fname, O_RDONLY, MYF(0))) < 0)
  {
    errmsg = "on open of file";
    goto err;
  }

  while ((long) (bytes= mysql_file_read(fd, buf, IO_SIZE, MYF(0))) > 0)
  {
    if (my_net_write(net, buf, bytes))
    {
      errmsg = "while writing data to client";
      goto err;
    }
  }

 end:
  if (my_net_write(net, (uchar*) "", 0) || net_flush(net) ||
      (my_net_read(net) == packet_error))
  {
    errmsg = "while negotiating file transfer close";
    goto err;
  }
  error = 0;

 err:
  my_net_set_read_timeout(net, old_timeout);
  if (fd >= 0)
    mysql_file_close(fd, MYF(0));
  if (errmsg)
  {
    sql_print_error("Failed in send_file() %s", errmsg);
    DBUG_PRINT("error", ("%s", errmsg));
  }
  DBUG_RETURN(error);
}


int test_for_non_eof_log_read_errors(int error, const char **errmsg)
{
  if (error == LOG_READ_EOF)
    return 0;
  my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
  switch (error) {
  case LOG_READ_BOGUS:
    *errmsg = "bogus data in log event";
    break;
  case LOG_READ_TOO_LARGE:
    *errmsg = "log event entry exceeded max_allowed_packet; \
Increase max_allowed_packet on master";
    break;
  case LOG_READ_IO:
    *errmsg = "I/O error reading log event";
    break;
  case LOG_READ_MEM:
    *errmsg = "memory allocation failed reading log event";
    break;
  case LOG_READ_TRUNC:
    *errmsg = "binlog truncated in the middle of event";
    break;
  default:
    *errmsg = "unknown error reading log event on the master";
    break;
  }
  return error;
}


/**
  An auxiliary function for calling in mysql_binlog_send
  to initialize the heartbeat timeout in waiting for a binlogged event.

  @param[in]    thd  THD to access a user variable

  @return        heartbeat period an ulonglong of nanoseconds
                 or zero if heartbeat was not demanded by slave
*/ 
static ulonglong get_heartbeat_period(THD * thd)
{
  my_bool null_value;
  LEX_STRING name=  { C_STRING_WITH_LEN("master_heartbeat_period")};
  user_var_entry *entry= 
    (user_var_entry*) my_hash_search(&thd->user_vars, (uchar*) name.str,
                                  name.length);
  return entry? entry->val_int(&null_value) : 0;
}

/*
  Function prepares and sends repliation heartbeat event.

  @param net                net object of THD
  @param packet             buffer to store the heartbeat instance
  @param event_coordinates  binlog file name and position of the last
                            real event master sent from binlog

  @note 
    Among three essential pieces of heartbeat data Log_event::when
    is computed locally.
    The  error to send is serious and should force terminating
    the dump thread.
*/
static int send_heartbeat_event(NET* net, String* packet,
                                const struct event_coordinates *coord)
{
  DBUG_ENTER("send_heartbeat_event");
  char header[LOG_EVENT_HEADER_LEN];
  /*
    'when' (the timestamp) is set to 0 so that slave could distinguish between
    real and fake Rotate events (if necessary)
  */
  memset(header, 0, 4);  // when

  header[EVENT_TYPE_OFFSET] = HEARTBEAT_LOG_EVENT;

  char* p= coord->file_name + dirname_length(coord->file_name);

  uint ident_len = strlen(p);
  ulong event_len = ident_len + LOG_EVENT_HEADER_LEN;
  int4store(header + SERVER_ID_OFFSET, server_id);
  int4store(header + EVENT_LEN_OFFSET, event_len);
  int2store(header + FLAGS_OFFSET, 0);

  int4store(header + LOG_POS_OFFSET, coord->pos);  // log_pos

  packet->append(header, sizeof(header));
  packet->append(p, ident_len);             // log_file_name

  if (my_net_write(net, (uchar*) packet->ptr(), packet->length()) ||
      net_flush(net))
  {
    DBUG_RETURN(-1);
  }
  DBUG_RETURN(0);
}

/*
  TODO: Clean up loop to only have one call to send_file()
*/

void mysql_binlog_send(THD* thd, char* log_ident, my_off_t pos,
		       ushort flags)
{
  LOG_INFO linfo;
  char *log_file_name = linfo.log_file_name;
  char search_file_name[FN_REFLEN], *name;

  ulong ev_offset;

  IO_CACHE log;
  File file = -1;
  String* packet = &thd->packet;
  int error;
  const char *errmsg = "Unknown error";
  NET* net = &thd->net;
  mysql_mutex_t *log_lock;
  bool binlog_can_be_corrupted= FALSE;
#ifndef DBUG_OFF
  int left_events = max_binlog_dump_events;
#endif
  DBUG_ENTER("mysql_binlog_send");
  DBUG_PRINT("enter",("log_ident: '%s'  pos: %ld", log_ident, (long) pos));

  bzero((char*) &log,sizeof(log));
  /* 
     heartbeat_period from @master_heartbeat_period user variable
  */
  ulonglong heartbeat_period= get_heartbeat_period(thd);
  struct timespec heartbeat_buf;
  struct event_coordinates coord_buf;
  struct timespec *heartbeat_ts= NULL;
  struct event_coordinates *coord= NULL;
  if (heartbeat_period != LL(0))
  {
    heartbeat_ts= &heartbeat_buf;
    set_timespec_nsec(*heartbeat_ts, 0);
    coord= &coord_buf;
    coord->file_name= log_file_name; // initialization basing on what slave remembers
    coord->pos= pos;
  }
  sql_print_information("Start binlog_dump to slave_server(%d), pos(%s, %lu)",
                        thd->server_id, log_ident, (ulong)pos);
  if (RUN_HOOK(binlog_transmit, transmit_start, (thd, flags, log_ident, pos)))
  {
    errmsg= "Failed to run hook 'transmit_start'";
    my_errno= ER_UNKNOWN_ERROR;
    goto err;
  }

#ifndef DBUG_OFF
  if (opt_sporadic_binlog_dump_fail && (binlog_dump_count++ % 2))
  {
    errmsg = "Master failed COM_BINLOG_DUMP to test if slave can recover";
    my_errno= ER_UNKNOWN_ERROR;
    goto err;
  }
#endif

  if (!mysql_bin_log.is_open())
  {
    errmsg = "Binary log is not open";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }
  if (!server_id_supplied)
  {
    errmsg = "Misconfigured master - server id was not set";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  name=search_file_name;
  if (log_ident[0])
    mysql_bin_log.make_log_name(search_file_name, log_ident);
  else
    name=0;					// Find first log

  linfo.index_file_offset = 0;

  if (mysql_bin_log.find_log_pos(&linfo, name, 1))
  {
    errmsg = "Could not find first log file name in binary log index file";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  mysql_mutex_lock(&LOCK_thread_count);
  thd->current_linfo = &linfo;
  mysql_mutex_unlock(&LOCK_thread_count);

  if ((file=open_binlog(&log, log_file_name, &errmsg)) < 0)
  {
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }
  if (pos < BIN_LOG_HEADER_SIZE || pos > my_b_filelength(&log))
  {
    errmsg= "Client requested master to start replication from \
impossible position";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  /* reset transmit packet for the fake rotate event below */
  if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
    goto err;

  /*
    Tell the client about the log name with a fake Rotate event;
    this is needed even if we also send a Format_description_log_event
    just after, because that event does not contain the binlog's name.
    Note that as this Rotate event is sent before
    Format_description_log_event, the slave cannot have any info to
    understand this event's format, so the header len of
    Rotate_log_event is FROZEN (so in 5.0 it will have a header shorter
    than other events except FORMAT_DESCRIPTION_EVENT).
    Before 4.0.14 we called fake_rotate_event below only if (pos ==
    BIN_LOG_HEADER_SIZE), because if this is false then the slave
    already knows the binlog's name.
    Since, we always call fake_rotate_event; if the slave already knew
    the log's name (ex: CHANGE MASTER TO MASTER_LOG_FILE=...) this is
    useless but does not harm much. It is nice for 3.23 (>=.58) slaves
    which test Rotate events to see if the master is 4.0 (then they
    choose to stop because they can't replicate 4.0); by always calling
    fake_rotate_event we are sure that 3.23.58 and newer will detect the
    problem as soon as replication starts (BUG#198).
    Always calling fake_rotate_event makes sending of normal
    (=from-binlog) Rotate events a priori unneeded, but it is not so
    simple: the 2 Rotate events are not equivalent, the normal one is
    before the Stop event, the fake one is after. If we don't send the
    normal one, then the Stop event will be interpreted (by existing 4.0
    slaves) as "the master stopped", which is wrong. So for safety,
    given that we want minimum modification of 4.0, we send the normal
    and fake Rotates.
  */
  if (fake_rotate_event(net, packet, log_file_name, pos, &errmsg))
  {
    /*
       This error code is not perfect, as fake_rotate_event() does not
       read anything from the binlog; if it fails it's because of an
       error in my_net_write(), fortunately it will say so in errmsg.
    */
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  /*
    Adding MAX_LOG_EVENT_HEADER_LEN, since a binlog event can become
    this larger than the corresponding packet (query) sent 
    from client to master.
  */
  thd->variables.max_allowed_packet+= MAX_LOG_EVENT_HEADER;

  /*
    We can set log_lock now, it does not move (it's a member of
    mysql_bin_log, and it's already inited, and it will be destroyed
    only at shutdown).
  */
  log_lock = mysql_bin_log.get_log_lock();
  if (pos > BIN_LOG_HEADER_SIZE)
  {
    /* reset transmit packet for the event read from binary log
       file */
    if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
      goto err;

     /*
       Try to find a Format_description_log_event at the beginning of
       the binlog
     */
     if (!(error = Log_event::read_log_event(&log, packet, log_lock)))
     {
       /*
         The packet has offsets equal to the normal offsets in a
         binlog event + ev_offset (the first ev_offset characters are
         the header (default \0)).
       */
       DBUG_PRINT("info",
                  ("Looked for a Format_description_log_event, found event type %d",
                   (*packet)[EVENT_TYPE_OFFSET+ev_offset]));
       if ((*packet)[EVENT_TYPE_OFFSET+ev_offset] == FORMAT_DESCRIPTION_EVENT)
       {
         binlog_can_be_corrupted= test((*packet)[FLAGS_OFFSET+ev_offset] &
                                       LOG_EVENT_BINLOG_IN_USE_F);
         (*packet)[FLAGS_OFFSET+ev_offset] &= ~LOG_EVENT_BINLOG_IN_USE_F;
         /*
           mark that this event with "log_pos=0", so the slave
           should not increment master's binlog position
           (rli->group_master_log_pos)
         */
         int4store((char*) packet->ptr()+LOG_POS_OFFSET+ev_offset, 0);
         /*
           if reconnect master sends FD event with `created' as 0
           to avoid destroying temp tables.
          */
         int4store((char*) packet->ptr()+LOG_EVENT_MINIMAL_HEADER_LEN+
                   ST_CREATED_OFFSET+ev_offset, (ulong) 0);
         /* send it */
         if (my_net_write(net, (uchar*) packet->ptr(), packet->length()))
         {
           errmsg = "Failed on my_net_write()";
           my_errno= ER_UNKNOWN_ERROR;
           goto err;
         }

         /*
           No need to save this event. We are only doing simple reads
           (no real parsing of the events) so we don't need it. And so
           we don't need the artificial Format_description_log_event of
           3.23&4.x.
         */
       }
     }
     else
     {
       if (test_for_non_eof_log_read_errors(error, &errmsg))
         goto err;
       /*
         It's EOF, nothing to do, go on reading next events, the
         Format_description_log_event will be found naturally if it is written.
       */
     }
  } /* end of if (pos > BIN_LOG_HEADER_SIZE); */
  else
  {
    /* The Format_description_log_event event will be found naturally. */
  }

  /* seek to the requested position, to start the requested dump */
  my_b_seek(&log, pos);			// Seek will done on next read

  while (!net->error && net->vio != 0 && !thd->killed)
  {
    Log_event_type event_type= UNKNOWN_EVENT;

    /* reset the transmit packet for the event read from binary log
       file */
    if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
      goto err;
    while (!(error = Log_event::read_log_event(&log, packet, log_lock)))
    {
#ifndef DBUG_OFF
      if (max_binlog_dump_events && !left_events--)
      {
	net_flush(net);
	errmsg = "Debugging binlog dump abort";
	my_errno= ER_UNKNOWN_ERROR;
	goto err;
      }
#endif
      /*
        log's filename does not change while it's active
      */
      if (coord)
        coord->pos= uint4korr(packet->ptr() + ev_offset + LOG_POS_OFFSET);

      event_type= (Log_event_type)((*packet)[LOG_EVENT_OFFSET+ev_offset]);
      if (event_type == FORMAT_DESCRIPTION_EVENT)
      {
        binlog_can_be_corrupted= test((*packet)[FLAGS_OFFSET+ev_offset] &
                                      LOG_EVENT_BINLOG_IN_USE_F);
        (*packet)[FLAGS_OFFSET+ev_offset] &= ~LOG_EVENT_BINLOG_IN_USE_F;
      }
      else if (event_type == STOP_EVENT)
        binlog_can_be_corrupted= FALSE;

      pos = my_b_tell(&log);
      if (RUN_HOOK(binlog_transmit, before_send_event,
                   (thd, flags, packet, log_file_name, pos)))
      {
        my_errno= ER_UNKNOWN_ERROR;
        errmsg= "run 'before_send_event' hook failed";
        goto err;
      }

      if (my_net_write(net, (uchar*) packet->ptr(), packet->length()))
      {
	errmsg = "Failed on my_net_write()";
	my_errno= ER_UNKNOWN_ERROR;
	goto err;
      }

      DBUG_PRINT("info", ("log event code %d", event_type));
      if (event_type == LOAD_EVENT)
      {
	if (send_file(thd))
	{
	  errmsg = "failed in send_file()";
	  my_errno= ER_UNKNOWN_ERROR;
	  goto err;
	}
      }

      if (RUN_HOOK(binlog_transmit, after_send_event, (thd, flags, packet)))
      {
        errmsg= "Failed to run hook 'after_send_event'";
        my_errno= ER_UNKNOWN_ERROR;
        goto err;
      }

      /* reset transmit packet for next loop */
      if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
        goto err;
    }

    /*
      here we were reading binlog that was not closed properly (as a result
      of a crash ?). treat any corruption as EOF
    */
    if (binlog_can_be_corrupted && error != LOG_READ_MEM)
      error=LOG_READ_EOF;
    /*
      TODO: now that we are logging the offset, check to make sure
      the recorded offset and the actual match.
      Guilhem 2003-06: this is not true if this master is a slave
      <4.0.15 running with --log-slave-updates, because then log_pos may
      be the offset in the-master-of-this-master's binlog.
    */
    if (test_for_non_eof_log_read_errors(error, &errmsg))
      goto err;

    if (!(flags & BINLOG_DUMP_NON_BLOCK) &&
        mysql_bin_log.is_active(log_file_name))
    {
      /*
	Block until there is more data in the log
      */
      if (net_flush(net))
      {
	errmsg = "failed on net_flush()";
	my_errno= ER_UNKNOWN_ERROR;
	goto err;
      }

      /*
	We may have missed the update broadcast from the log
	that has just happened, let's try to catch it if it did.
	If we did not miss anything, we just wait for other threads
	to signal us.
      */
      {
	log.error=0;
	bool read_packet = 0;

#ifndef DBUG_OFF
	if (max_binlog_dump_events && !left_events--)
	{
	  errmsg = "Debugging binlog dump abort";
	  my_errno= ER_UNKNOWN_ERROR;
	  goto err;
	}
#endif

        /* reset the transmit packet for the event read from binary log
           file */
        if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
          goto err;
        
	/*
	  No one will update the log while we are reading
	  now, but we'll be quick and just read one record

	  TODO:
          Add an counter that is incremented for each time we update the
          binary log.  We can avoid the following read if the counter
          has not been updated since last read.
	*/

        mysql_mutex_lock(log_lock);
        switch (error= Log_event::read_log_event(&log, packet, (mysql_mutex_t*) 0)) {
	case 0:
	  /* we read successfully, so we'll need to send it to the slave */
          mysql_mutex_unlock(log_lock);
	  read_packet = 1;
          if (coord)
            coord->pos= uint4korr(packet->ptr() + ev_offset + LOG_POS_OFFSET);
          event_type= (Log_event_type)((*packet)[LOG_EVENT_OFFSET+ev_offset]);
	  break;

	case LOG_READ_EOF:
        {
          int ret;
          ulong signal_cnt;
	  DBUG_PRINT("wait",("waiting for data in binary log"));
	  if (thd->server_id==0) // for mysqlbinlog (mysqlbinlog.server_id==0)
	  {
            mysql_mutex_unlock(log_lock);
	    goto end;
	  }

#ifndef DBUG_OFF
          ulong hb_info_counter= 0;
#endif
          signal_cnt= mysql_bin_log.signal_cnt;
          do 
          {
            if (coord)
            {
              DBUG_ASSERT(heartbeat_ts && heartbeat_period != 0);
              set_timespec_nsec(*heartbeat_ts, heartbeat_period);
            }
            ret= mysql_bin_log.wait_for_update_bin_log(thd, heartbeat_ts);
            DBUG_ASSERT(ret == 0 || (heartbeat_period != 0 && coord != NULL));
            if (ret == ETIMEDOUT || ret == ETIME)
            {
#ifndef DBUG_OFF
              if (hb_info_counter < 3)
              {
                sql_print_information("master sends heartbeat message");
                hb_info_counter++;
                if (hb_info_counter == 3)
                  sql_print_information("the rest of heartbeat info skipped ...");
              }
#endif
              /* reset transmit packet for the heartbeat event */
              if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
                goto err;
              if (send_heartbeat_event(net, packet, coord))
              {
                errmsg = "Failed on my_net_write()";
                my_errno= ER_UNKNOWN_ERROR;
                mysql_mutex_unlock(log_lock);
                goto err;
              }
            }
            else
            {
              DBUG_PRINT("wait",("binary log received update or a broadcast signal caught"));
            }
          } while (signal_cnt == mysql_bin_log.signal_cnt && !thd->killed);
          mysql_mutex_unlock(log_lock);
        }
        break;
            
        default:
          mysql_mutex_unlock(log_lock);
          test_for_non_eof_log_read_errors(error, &errmsg);
          goto err;
	}

	if (read_packet)
        {
          thd_proc_info(thd, "Sending binlog event to slave");
          pos = my_b_tell(&log);
          if (RUN_HOOK(binlog_transmit, before_send_event,
                       (thd, flags, packet, log_file_name, pos)))
          {
            my_errno= ER_UNKNOWN_ERROR;
            errmsg= "run 'before_send_event' hook failed";
            goto err;
          }
	  
	  if (my_net_write(net, (uchar*) packet->ptr(), packet->length()) )
	  {
	    errmsg = "Failed on my_net_write()";
	    my_errno= ER_UNKNOWN_ERROR;
	    goto err;
	  }

	  if (event_type == LOAD_EVENT)
	  {
	    if (send_file(thd))
	    {
	      errmsg = "failed in send_file()";
	      my_errno= ER_UNKNOWN_ERROR;
	      goto err;
	    }
	  }

          if (RUN_HOOK(binlog_transmit, after_send_event, (thd, flags, packet)))
          {
            my_errno= ER_UNKNOWN_ERROR;
            errmsg= "Failed to run hook 'after_send_event'";
            goto err;
          }
	}

	log.error=0;
      }
    }
    else
    {
      bool loop_breaker = 0;
      /* need this to break out of the for loop from switch */

      thd_proc_info(thd, "Finished reading one binlog; switching to next binlog");
      switch (mysql_bin_log.find_next_log(&linfo, 1)) {
      case 0:
	break;
      case LOG_INFO_EOF:
        if (mysql_bin_log.is_active(log_file_name))
        {
          loop_breaker = (flags & BINLOG_DUMP_NON_BLOCK);
          break;
        }
      default:
	errmsg = "could not find next log";
	my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
	goto err;
      }

      if (loop_breaker)
        break;

      end_io_cache(&log);
      mysql_file_close(file, MYF(MY_WME));

      /* reset transmit packet for the possible fake rotate event */
      if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
        goto err;
      
      /*
        Call fake_rotate_event() in case the previous log (the one which
        we have just finished reading) did not contain a Rotate event
        (for example (I don't know any other example) the previous log
        was the last one before the master was shutdown & restarted).
        This way we tell the slave about the new log's name and
        position.  If the binlog is 5.0, the next event we are going to
        read and send is Format_description_log_event.
      */
      if ((file=open_binlog(&log, log_file_name, &errmsg)) < 0 ||
	  fake_rotate_event(net, packet, log_file_name, BIN_LOG_HEADER_SIZE,
                            &errmsg))
      {
	my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
	goto err;
      }

      if (coord)
        coord->file_name= log_file_name; // reset to the next
    }
  }

end:
  end_io_cache(&log);
  mysql_file_close(file, MYF(MY_WME));

  RUN_HOOK(binlog_transmit, transmit_stop, (thd, flags));
  my_eof(thd);
  thd_proc_info(thd, "Waiting to finalize termination");
  mysql_mutex_lock(&LOCK_thread_count);
  thd->current_linfo = 0;
  mysql_mutex_unlock(&LOCK_thread_count);
  DBUG_VOID_RETURN;

err:
  thd_proc_info(thd, "Waiting to finalize termination");
  end_io_cache(&log);
  RUN_HOOK(binlog_transmit, transmit_stop, (thd, flags));
  /*
    Exclude  iteration through thread list
    this is needed for purge_logs() - it will iterate through
    thread list and update thd->current_linfo->index_file_offset
    this mutex will make sure that it never tried to update our linfo
    after we return from this stack frame
  */
  mysql_mutex_lock(&LOCK_thread_count);
  thd->current_linfo = 0;
  mysql_mutex_unlock(&LOCK_thread_count);
  if (file >= 0)
    mysql_file_close(file, MYF(MY_WME));

  my_message(my_errno, errmsg, MYF(0));
  DBUG_VOID_RETURN;
}


/**
  An auxiliary function extracts slave UUID.

  @param[in]    thd  THD to access a user variable
  @param[out]   value String to return UUID value.

  @return       if success value is returned else NULL is returned.
*/
String *get_slave_uuid(THD *thd, String *value)
{
  uchar name[]= "slave_uuid";

  if (value == NULL)
    return NULL;
  user_var_entry *entry=
    (user_var_entry*) my_hash_search(&thd->user_vars, name, sizeof(name)-1);
  if (entry && entry->length > 0)
  {
    value->copy(entry->value, entry->length, NULL);
    return value;
  }
  else
    return NULL;
}

/*

  Kill all Binlog_dump threads which previously talked to the same slave
  ("same" means with the same server id). Indeed, if the slave stops, if the
  Binlog_dump thread is waiting (mysql_cond_wait) for binlog update, then it
  will keep existing until a query is written to the binlog. If the master is
  idle, then this could last long, and if the slave reconnects, we could have 2
  Binlog_dump threads in SHOW PROCESSLIST, until a query is written to the
  binlog. To avoid this, when the slave reconnects and sends COM_BINLOG_DUMP,
  the master kills any existing thread with the slave's server id (if this id is
  not zero; it will be true for real slaves, but false for mysqlbinlog when it
  sends COM_BINLOG_DUMP to get a remote binlog dump).

  SYNOPSIS
    kill_zombie_dump_threads()
    slave_uuid      the slave's UUID

*/


void kill_zombie_dump_threads(String *slave_uuid)
{
  if (slave_uuid->length() == 0)
    return;
  DBUG_ASSERT(slave_uuid->length() == UUID_LENGTH);

  mysql_mutex_lock(&LOCK_thread_count);
  I_List_iterator<THD> it(threads);
  THD *tmp;

  while ((tmp=it++))
  {
    if (tmp != current_thd && tmp->get_command() == COM_BINLOG_DUMP)
    {
      String tmp_uuid;
      if (get_slave_uuid(tmp, &tmp_uuid) != NULL &&
          !strncmp(slave_uuid->c_ptr(), tmp_uuid.c_ptr(), UUID_LENGTH))
      {
        mysql_mutex_lock(&tmp->LOCK_thd_data);	// Lock from delete
        break;
      }
    }
  }
  mysql_mutex_unlock(&LOCK_thread_count);
  if (tmp)
  {
    /*
      Here we do not call kill_one_thread() as
      it will be slow because it will iterate through the list
      again. We just to do kill the thread ourselves.
    */
    tmp->awake(THD::KILL_QUERY);
    mysql_mutex_unlock(&tmp->LOCK_thd_data);
  }
}


/**
  Execute a RESET MASTER statement.

  @param thd Pointer to THD object of the client thread executing the
  statement.

  @retval 0 success
  @retval 1 error
*/
int reset_master(THD* thd)
{
  if (!mysql_bin_log.is_open())
  {
    my_message(ER_FLUSH_MASTER_BINLOG_CLOSED,
               ER(ER_FLUSH_MASTER_BINLOG_CLOSED), MYF(ME_BELL+ME_WAITTANG));
    return 1;
  }

  if (mysql_bin_log.reset_logs(thd))
    return 1;
  RUN_HOOK(binlog_transmit, after_reset_master, (thd, 0 /* flags */));
  return 0;
}

int cmp_master_pos(const char* log_file_name1, ulonglong log_pos1,
		   const char* log_file_name2, ulonglong log_pos2)
{
  int res;
  size_t log_file_name1_len=  strlen(log_file_name1);
  size_t log_file_name2_len=  strlen(log_file_name2);

  //  We assume that both log names match up to '.'
  if (log_file_name1_len == log_file_name2_len)
  {
    if ((res= strcmp(log_file_name1, log_file_name2)))
      return res;
    return (log_pos1 < log_pos2) ? -1 : (log_pos1 == log_pos2) ? 0 : 1;
  }
  return ((log_file_name1_len < log_file_name2_len) ? -1 : 1);
}


/**
  Execute a SHOW MASTER STATUS statement.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @retval FALSE success
  @retval TRUE failure
*/
bool show_binlog_info(THD* thd)
{
  Protocol *protocol= thd->protocol;
  DBUG_ENTER("show_binlog_info");
  List<Item> field_list;
  field_list.push_back(new Item_empty_string("File", FN_REFLEN));
  field_list.push_back(new Item_return_int("Position",20,
					   MYSQL_TYPE_LONGLONG));
  field_list.push_back(new Item_empty_string("Binlog_Do_DB",255));
  field_list.push_back(new Item_empty_string("Binlog_Ignore_DB",255));

  if (protocol->send_result_set_metadata(&field_list,
                            Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    DBUG_RETURN(TRUE);
  protocol->prepare_for_resend();

  if (mysql_bin_log.is_open())
  {
    LOG_INFO li;
    mysql_bin_log.get_current_log(&li);
    int dir_len = dirname_length(li.log_file_name);
    protocol->store(li.log_file_name + dir_len, &my_charset_bin);
    protocol->store((ulonglong) li.pos);
    protocol->store(binlog_filter->get_do_db());
    protocol->store(binlog_filter->get_ignore_db());
    if (protocol->write())
      DBUG_RETURN(TRUE);
  }
  my_eof(thd);
  DBUG_RETURN(FALSE);
}


/**
  Execute a SHOW BINARY LOGS statement.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @retval FALSE success
  @retval TRUE failure
*/
bool show_binlogs(THD* thd)
{
  IO_CACHE *index_file;
  LOG_INFO cur;
  File file;
  char fname[FN_REFLEN];
  List<Item> field_list;
  uint length;
  int cur_dir_len;
  Protocol *protocol= thd->protocol;
  DBUG_ENTER("show_binlogs");

  if (!mysql_bin_log.is_open())
  {
    my_message(ER_NO_BINARY_LOGGING, ER(ER_NO_BINARY_LOGGING), MYF(0));
    DBUG_RETURN(TRUE);
  }

  field_list.push_back(new Item_empty_string("Log_name", 255));
  field_list.push_back(new Item_return_int("File_size", 20,
                                           MYSQL_TYPE_LONGLONG));
  if (protocol->send_result_set_metadata(&field_list,
                            Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    DBUG_RETURN(TRUE);
  
  mysql_mutex_lock(mysql_bin_log.get_log_lock());
  mysql_bin_log.lock_index();
  index_file=mysql_bin_log.get_index_file();
  
  mysql_bin_log.raw_get_current_log(&cur); // dont take mutex
  mysql_mutex_unlock(mysql_bin_log.get_log_lock()); // lockdep, OK
  
  cur_dir_len= dirname_length(cur.log_file_name);

  reinit_io_cache(index_file, READ_CACHE, (my_off_t) 0, 0, 0);

  /* The file ends with EOF or empty line */
  while ((length=my_b_gets(index_file, fname, sizeof(fname))) > 1)
  {
    int dir_len;
    ulonglong file_length= 0;                   // Length if open fails
    fname[--length] = '\0';                     // remove the newline

    protocol->prepare_for_resend();
    dir_len= dirname_length(fname);
    length-= dir_len;
    protocol->store(fname + dir_len, length, &my_charset_bin);

    if (!(strncmp(fname+dir_len, cur.log_file_name+cur_dir_len, length)))
      file_length= cur.pos;  /* The active log, use the active position */
    else
    {
      /* this is an old log, open it and find the size */
      if ((file= mysql_file_open(key_file_binlog,
                                 fname, O_RDONLY | O_SHARE | O_BINARY,
                                 MYF(0))) >= 0)
      {
        file_length= (ulonglong) mysql_file_seek(file, 0L, MY_SEEK_END, MYF(0));
        mysql_file_close(file, MYF(0));
      }
    }
    protocol->store(file_length);
    if (protocol->write())
      goto err;
  }
  mysql_bin_log.unlock_index();
  my_eof(thd);
  DBUG_RETURN(FALSE);

err:
  mysql_bin_log.unlock_index();
  DBUG_RETURN(TRUE);
}

#endif /* HAVE_REPLICATION */
