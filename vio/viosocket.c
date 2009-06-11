/*
   Copyright (C) 2000 MySQL AB
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

/*
  Note that we can't have assertion on file descriptors;  The reason for
  this is that during mysql shutdown, another thread can close a file
  we are working on.  In this case we should just return read errors from
  the file descriptior.
*/

#include "vio_priv.h"

int vio_errno(Vio *vio __attribute__((unused)))
{
  return my_socket_errno();
}


size_t vio_read(Vio * vio, uchar* buf, size_t size)
{
  size_t r;
  DBUG_ENTER("vio_read");
  DBUG_PRINT("enter", ("sd: " MY_SOCKET_FORMAT "  buf: 0x%lx  size: %u",
                       MY_SOCKET_FORMAT_VALUE(vio->sd), (long) buf,
                       (uint) size));

  /* Ensure nobody uses vio_read_buff and vio_read simultaneously */
  DBUG_ASSERT(vio->read_end == vio->read_pos);

  /*
    Callers of 'vio_read' checks "errno" even if no system function
    failed, to avoid false positives "errno" must be reset
  */
  my_socket_set_errno(0);

  r = my_recv(vio->sd, buf, size, 0);

#ifndef DBUG_OFF
  if (r == (size_t) -1)
  {
    DBUG_PRINT("vio_error", ("Got error %d during read",errno));
  }
#endif /* DBUG_OFF */
  DBUG_PRINT("exit", ("%ld", (long) r));
  DBUG_RETURN(r);
}


/*
  Buffered read: if average read size is small it may
  reduce number of syscalls.
*/

size_t vio_read_buff(Vio *vio, uchar* buf, size_t size)
{
  size_t rc;
#define VIO_UNBUFFERED_READ_MIN_SIZE 2048
  DBUG_ENTER("vio_read_buff");
  DBUG_PRINT("enter", ("sd: " MY_SOCKET_FORMAT "  buf: 0x%lx  size: %u",
                       MY_SOCKET_FORMAT_VALUE(vio->sd), (long) buf,
                       (uint) size));

  if (vio->read_pos < vio->read_end)
  {
    rc= min((size_t) (vio->read_end - vio->read_pos), size);
    memcpy(buf, vio->read_pos, rc);
    vio->read_pos+= rc;
    /*
      Do not try to read from the socket now even if rc < size:
      vio_read can return -1 due to an error or non-blocking mode, and
      the safest way to handle it is to move to a separate branch.
    */
  }
  else if (size < VIO_UNBUFFERED_READ_MIN_SIZE)
  {
    rc= vio_read(vio, (uchar*) vio->read_buffer, VIO_READ_BUFFER_SIZE);
    if (rc != 0 && rc != (size_t) -1)
    {
      if (rc > size)
      {
        vio->read_pos= vio->read_buffer + size;
        vio->read_end= vio->read_buffer + rc;
        rc= size;
      }
      memcpy(buf, vio->read_buffer, rc);
    }
  }
  else
    rc= vio_read(vio, buf, size);
  DBUG_RETURN(rc);
#undef VIO_UNBUFFERED_READ_MIN_SIZE
}


size_t vio_write(Vio * vio, const uchar* buf, size_t size)
{
  size_t r;
  DBUG_ENTER("vio_write");
  DBUG_PRINT("enter", ("sd: " MY_SOCKET_FORMAT "  buf: 0x%lx  size: %u",
                       MY_SOCKET_FORMAT_VALUE(vio->sd), (long)buf, (uint)size));

  r = my_send(vio->sd, buf, size, 0);

#ifndef DBUG_OFF
  if (r == (size_t) -1)
  {
    DBUG_PRINT("vio_error", ("Got error on write: %d",socket_errno));
  }
#endif /* DBUG_OFF */
  DBUG_PRINT("exit", ("%u", (uint) r));
  DBUG_RETURN(r);
}

int vio_blocking(Vio * vio __attribute__((unused)), my_bool set_blocking_mode,
		 my_bool *old_mode)
{
  int r=0;
  DBUG_ENTER("vio_blocking");

  *old_mode= test(!(vio->fcntl_mode & O_NONBLOCK));
  DBUG_PRINT("enter", ("set_blocking_mode: %d  old_mode: %d",
		       (int) set_blocking_mode, (int) *old_mode));

#if !defined(__WIN__)
#if !defined(NO_FCNTL_NONBLOCK)
  if (my_socket_valid(vio->sd))
  {
    int old_fcntl=vio->fcntl_mode;
    if (set_blocking_mode)
      vio->fcntl_mode &= ~O_NONBLOCK; /* clear bit */
    else
      vio->fcntl_mode |= O_NONBLOCK; /* set bit */
    if (old_fcntl != vio->fcntl_mode)
    {
      r= my_socket_nonblock(vio->sd, (vio->fcntl_mode & O_NONBLOCK));
      if (r!=0)
      {
        DBUG_PRINT("info", ("fcntl failed, errno %d", r));
        vio->fcntl_mode= old_fcntl;
      }
    }
  }
#else
  r= set_blocking_mode ? 0 : 1;
#endif /* !defined(NO_FCNTL_NONBLOCK) */
#else /* !defined(__WIN__) */
  if (vio->type != VIO_TYPE_NAMEDPIPE && vio->type != VIO_TYPE_SHARED_MEMORY)
  { 
    ulong arg;
    int old_fcntl=vio->fcntl_mode;
    if (set_blocking_mode)
    {
      arg = 0;
      vio->fcntl_mode &= ~O_NONBLOCK; /* clear bit */
    }
    else
    {
      arg = 1;
      vio->fcntl_mode |= O_NONBLOCK; /* set bit */
    }
    if (old_fcntl != vio->fcntl_mode)
      r = ioctlsocket(vio->sd.s,FIONBIO,(void*) &arg);
  }
  else
    r=  test(!(vio->fcntl_mode & O_NONBLOCK)) != set_blocking_mode;
#endif /* !defined(__WIN__) */
  DBUG_PRINT("exit", ("%d", r));
  DBUG_RETURN(r);
}

my_bool
vio_is_blocking(Vio * vio)
{
  my_bool r;
  DBUG_ENTER("vio_is_blocking");
  r = !(vio->fcntl_mode & O_NONBLOCK);
  DBUG_PRINT("exit", ("%d", (int) r));
  DBUG_RETURN(r);
}


int vio_fastsend(Vio * vio __attribute__((unused)))
{
  int r=0;
  DBUG_ENTER("vio_fastsend");

#if defined(IPTOS_THROUGHPUT)
  {
    int tos = IPTOS_THROUGHPUT;
    r= my_setsockopt(vio->sd, IPPROTO_IP, IP_TOS, (void *) &tos, sizeof(tos));
  }
#endif                                    /* IPTOS_THROUGHPUT */
  if (!r)
  {
#ifdef __WIN__
    BOOL nodelay= 1;
#else
    int nodelay = 1;
#endif

    r= my_setsockopt(vio->sd, IPPROTO_TCP, TCP_NODELAY,
                  &nodelay, sizeof(nodelay));

  }
  if (r)
  {
    DBUG_PRINT("warning", ("Couldn't set socket option for fast send"));
    r= -1;
  }
  DBUG_PRINT("exit", ("%d", r));
  DBUG_RETURN(r);
}

int vio_keepalive(Vio* vio, my_bool set_keep_alive)
{
  int r=0;
  uint opt = 0;
  DBUG_ENTER("vio_keepalive");
  DBUG_PRINT("enter", ("sd: " MY_SOCKET_FORMAT "  set_keep_alive: %d",
                       MY_SOCKET_FORMAT_VALUE(vio->sd), (int)set_keep_alive));
  if (vio->type != VIO_TYPE_NAMEDPIPE)
  {
    if (set_keep_alive)
      opt = 1;
    r = my_setsockopt(vio->sd, SOL_SOCKET, SO_KEEPALIVE, (char *) &opt,
		   sizeof(opt));
  }
  DBUG_RETURN(r);
}


my_bool
vio_should_retry(Vio * vio __attribute__((unused)))
{
  int en = socket_errno;
  return (en == SOCKET_EAGAIN || en == SOCKET_EINTR ||
	  en == SOCKET_EWOULDBLOCK);
}


my_bool
vio_was_interrupted(Vio *vio __attribute__((unused)))
{
  int en= socket_errno;
  return (en == SOCKET_EAGAIN || en == SOCKET_EINTR ||
	  en == SOCKET_EWOULDBLOCK || en == SOCKET_ETIMEDOUT);
}


int vio_close(Vio * vio)
{
  int r=0;
  DBUG_ENTER("vio_close");
#ifdef __WIN__
  if (vio->type == VIO_TYPE_NAMEDPIPE)
  {
#if defined(__NT__) && defined(MYSQL_SERVER)
    CancelIo(vio->hPipe);
    DisconnectNamedPipe(vio->hPipe);
#endif
    r=CloseHandle(vio->hPipe);
  }
  else
#endif /* __WIN__ */
 if (vio->type != VIO_CLOSED)
  {
    DBUG_ASSERT(my_socket_valid(vio->sd));
    if (my_shutdown(vio->sd, SHUT_RDWR))
      r= -1;
    if (my_socket_close(vio->sd))
      r= -1;
  }
  if (r)
  {
    DBUG_PRINT("vio_error", ("close() failed, error: %d",socket_errno));
    /* FIXME: error handling (not critical for MySQL) */
  }
  vio->type= VIO_CLOSED;
  my_socket_invalidate(&(vio->sd));
  DBUG_RETURN(r);
}


const char *vio_description(Vio * vio)
{
  return vio->desc;
}

enum enum_vio_type vio_type(Vio* vio)
{
  return vio->type;
}

my_socket vio_fd(Vio* vio)
{
  return vio->sd;
}


my_bool vio_peer_addr(Vio * vio, char *buf, uint16 *port, size_t buflen)
{
  DBUG_ENTER("vio_peer_addr");
  DBUG_PRINT("enter", ("sd: " MY_SOCKET_FORMAT,
                       MY_SOCKET_FORMAT_VALUE(vio->sd)));
  if (vio->localhost)
  {
    strmov(buf,"127.0.0.1");
    *port= 0;
  }
  else
  {
    int error;
    char port_buf[NI_MAXSERV];
    size_socket addrLen = sizeof(vio->remote);
    if (my_getpeername(vio->sd, (struct sockaddr *) (&vio->remote),
                       &addrLen) != 0)
    {
      DBUG_PRINT("exit", ("getpeername gave error: %d", socket_errno));
      DBUG_RETURN(1);
    }
    vio->addrLen= (int)addrLen;
    
    if ((error= getnameinfo((struct sockaddr *)(&vio->remote), 
                            addrLen,
                            buf, buflen,
                            port_buf, NI_MAXSERV, NI_NUMERICHOST|NI_NUMERICSERV)))
    {
      DBUG_PRINT("exit", ("getnameinfo gave error: %s", 
                          gai_strerror(error)));
      DBUG_RETURN(1);
    }
    
    *port= (uint16)strtol(port_buf, (char **)NULL, 10);

    /*
      A lot of users do not have IPv6 loopback resolving to localhost
      correctly setup. Should this exist? No. If we do not do it though
      we will be getting a lot of support questions from users who
      have bad setups. This code should be removed by say... 2012.
        -Brian
    */
    if (!memcmp(buf, "::ffff:127.0.0.1", sizeof("::ffff:127.0.0.1")))
      strmov(buf, "127.0.0.1");
  }
  DBUG_PRINT("exit", ("addr: %s", buf));
  DBUG_RETURN(0);
}


/* Return 0 if there is data to be read */

my_bool vio_poll_read(Vio *vio,uint timeout)
{
#ifndef HAVE_POLL
  return 0;
#else
  struct pollfd fds;
  int res;
  DBUG_ENTER("vio_poll");
  fds.fd=vio->sd.fd; /* FIXME: Vista has WSAPoll(), can emulate elsewhere */
  fds.events=POLLIN;
  fds.revents=0;
  if ((res=poll(&fds,1,(int) timeout*1000)) <= 0)
  {
    DBUG_RETURN(res < 0 ? 0 : 1);		/* Don't return 1 on errors */
  }
  DBUG_RETURN(fds.revents & POLLIN ? 0 : 1);
#endif
}


void vio_timeout(Vio *vio, uint which, uint timeout)
{
#if defined(SO_SNDTIMEO) && defined(SO_RCVTIMEO)
  int r;
  DBUG_ENTER("vio_timeout");

  {
#ifdef __WIN__
  /* Windows expects time in milliseconds as int */
  int wait_timeout= (int) timeout * 1000;
#else
  /* POSIX specifies time as struct timeval. */
  struct timeval wait_timeout;
  wait_timeout.tv_sec= timeout;
  wait_timeout.tv_usec= 0;
#endif

  r= my_setsockopt(vio->sd, SOL_SOCKET, which ? SO_SNDTIMEO : SO_RCVTIMEO,
                   &wait_timeout, sizeof(wait_timeout));

  }

#ifndef DBUG_OFF
  if (r != 0)
    DBUG_PRINT("error", ("setsockopt failed: %d, errno: %d", r, socket_errno));
#endif

  DBUG_VOID_RETURN;
#else
/*
  Platforms not suporting setting of socket timeout should either use
  thr_alarm or just run without read/write timeout(s)
*/
#endif
}


#ifdef __WIN__
size_t vio_read_pipe(Vio * vio, uchar* buf, size_t size)
{
  DWORD length;
  DBUG_ENTER("vio_read_pipe");
  DBUG_PRINT("enter", ("sd: %d  buf: 0x%lx  size: %u", vio->sd, (long) buf,
                       (uint) size));

  if (!ReadFile(vio->hPipe, buf, size, &length, NULL))
    DBUG_RETURN(-1);

  DBUG_PRINT("exit", ("%d", length));
  DBUG_RETURN((size_t) length);
}


size_t vio_write_pipe(Vio * vio, const uchar* buf, size_t size)
{
  DWORD length;
  DBUG_ENTER("vio_write_pipe");
  DBUG_PRINT("enter", ("sd: %d  buf: 0x%lx  size: %u", vio->sd, (long) buf,
                       (uint) size));

  if (!WriteFile(vio->hPipe, (char*) buf, size, &length, NULL))
    DBUG_RETURN(-1);

  DBUG_PRINT("exit", ("%d", length));
  DBUG_RETURN((size_t) length);
}

int vio_close_pipe(Vio * vio)
{
  int r;
  DBUG_ENTER("vio_close_pipe");
#if defined(__NT__) && defined(MYSQL_SERVER)
  CancelIo(vio->hPipe);
  DisconnectNamedPipe(vio->hPipe);
#endif
  r=CloseHandle(vio->hPipe);
  if (r)
  {
    DBUG_PRINT("vio_error", ("close() failed, error: %d",GetLastError()));
    /* FIXME: error handling (not critical for MySQL) */
  }
  vio->type= VIO_CLOSED;
  my_socket_invalidate(&(vio->sd));
  DBUG_RETURN(r);
}


void vio_ignore_timeout(Vio *vio __attribute__((unused)),
			uint which __attribute__((unused)),
			uint timeout __attribute__((unused)))
{
}


#ifdef HAVE_SMEM

size_t vio_read_shared_memory(Vio * vio, uchar* buf, size_t size)
{
  size_t length;
  size_t remain_local;
  char *current_postion;
  DBUG_ENTER("vio_read_shared_memory");
  DBUG_PRINT("enter", ("sd: %d  buf: 0x%lx  size: %d", vio->sd, (long) buf,
                       size));

  remain_local = size;
  current_postion=buf;
  do
  {
    if (vio->shared_memory_remain == 0)
    {
      HANDLE events[2];
      events[0]= vio->event_server_wrote;
      events[1]= vio->event_conn_closed;
      /*
        WaitForMultipleObjects can return next values:
         WAIT_OBJECT_0+0 - event from vio->event_server_wrote
         WAIT_OBJECT_0+1 - event from vio->event_conn_closed. We can't read
		           anything
         WAIT_ABANDONED_0 and WAIT_TIMEOUT - fail.  We can't read anything
      */
      if (WaitForMultipleObjects(2, (HANDLE*)&events,FALSE,
                                 vio->net->read_timeout*1000) != WAIT_OBJECT_0)
      {
        DBUG_RETURN(-1);
      };

      vio->shared_memory_pos = vio->handle_map;
      vio->shared_memory_remain = uint4korr((ulong*)vio->shared_memory_pos);
      vio->shared_memory_pos+=4;
    }

    length = size;

    if (vio->shared_memory_remain < length)
       length = vio->shared_memory_remain;
    if (length > remain_local)
       length = remain_local;

    memcpy(current_postion,vio->shared_memory_pos,length);

    vio->shared_memory_remain-=length;
    vio->shared_memory_pos+=length;
    current_postion+=length;
    remain_local-=length;

    if (!vio->shared_memory_remain)
    {
      if (!SetEvent(vio->event_client_read))
        DBUG_RETURN(-1);
    }
  } while (remain_local);
  length = size;

  DBUG_PRINT("exit", ("%lu", (ulong) length));
  DBUG_RETURN(length);
}


size_t vio_write_shared_memory(Vio * vio, const uchar* buf, size_t size)
{
  size_t length, remain, sz;
  HANDLE pos;
  const uchar *current_postion;
  DBUG_ENTER("vio_write_shared_memory");
  DBUG_PRINT("enter", ("sd: %d  buf: 0x%lx  size: %d", vio->sd, (long) buf,
                       size));

  remain = size;
  current_postion = buf;
  while (remain != 0)
  {
    if (WaitForSingleObject(vio->event_server_read,
                            vio->net->write_timeout*1000) !=
        WAIT_OBJECT_0)
    {
      DBUG_RETURN((size_t) -1);
    }

    sz= (remain > shared_memory_buffer_length ? shared_memory_buffer_length :
         remain);

    int4store(vio->handle_map,sz);
    pos = vio->handle_map + 4;
    memcpy(pos,current_postion,sz);
    remain-=sz;
    current_postion+=sz;
    if (!SetEvent(vio->event_client_wrote))
      DBUG_RETURN((size_t) -1);
  }
  length = size;

  DBUG_PRINT("exit", ("%lu", (ulong) length));
  DBUG_RETURN(length);
}


/**
 Close shared memory and DBUG_PRINT any errors that happen on closing.
 @return Zero if all closing functions succeed, and nonzero otherwise.
*/
int vio_close_shared_memory(Vio * vio)
{
  int error_count= 0;
  DBUG_ENTER("vio_close_shared_memory");
  if (vio->type != VIO_CLOSED)
  {
    /*
      Set event_conn_closed for notification of both client and server that
      connection is closed
    */
    SetEvent(vio->event_conn_closed);
    /*
      Close all handlers. UnmapViewOfFile and CloseHandle return non-zero
      result if they are success.
    */
    if (UnmapViewOfFile(vio->handle_map) == 0) 
    {
      error_count++;
      DBUG_PRINT("vio_error", ("UnmapViewOfFile() failed"));
    }
    if (CloseHandle(vio->event_server_wrote) == 0)
    {
      error_count++;
      DBUG_PRINT("vio_error", ("CloseHandle(vio->esw) failed"));
    }
    if (CloseHandle(vio->event_server_read) == 0)
    {
      error_count++;
      DBUG_PRINT("vio_error", ("CloseHandle(vio->esr) failed"));
    }
    if (CloseHandle(vio->event_client_wrote) == 0)
    {
      error_count++;
      DBUG_PRINT("vio_error", ("CloseHandle(vio->ecw) failed"));
    }
    if (CloseHandle(vio->event_client_read) == 0)
    {
      error_count++;
      DBUG_PRINT("vio_error", ("CloseHandle(vio->ecr) failed"));
    }
    if (CloseHandle(vio->handle_file_map) == 0)
    {
      error_count++;
      DBUG_PRINT("vio_error", ("CloseHandle(vio->hfm) failed"));
    }
    if (CloseHandle(vio->event_conn_closed) == 0)
    {
      error_count++;
      DBUG_PRINT("vio_error", ("CloseHandle(vio->ecc) failed"));
    }
  }
  vio->type= VIO_CLOSED;
  my_socket_invalidate(&(vio->sd));
  DBUG_RETURN(error_count);
}
#endif /* HAVE_SMEM */
#endif /* __WIN__ */
