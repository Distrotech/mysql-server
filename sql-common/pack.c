/* Copyright (C) 2000-2003 MySQL AB

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

#include <my_global.h>
#include <mysql_com.h>
#include <mysql.h>

/* Get the length of next field. Change parameter to point at fieldstart */
ulong STDCALL net_field_length(uchar **packet)
{
  reg1 uchar *pos= (uchar *)*packet;
  if (*pos < 251)
  {
    (*packet)++;
    return (ulong) *pos;
  }
  if (*pos == 251)
  {
    (*packet)++;
    return NULL_LENGTH;
  }
  if (*pos == 252)
  {
    (*packet)+=3;
    return (ulong) uint2korr(pos+1);
  }
  if (*pos == 253)
  {
    (*packet)+=4;
    return (ulong) uint3korr(pos+1);
  }
  (*packet)+=9;					/* Must be 254 when here */
  return (ulong) uint4korr(pos+1);
}

/* The same as above but returns longlong */
my_ulonglong net_field_length_ll(uchar **packet)
{
  reg1 uchar *pos= *packet;
  if (*pos < 251)
  {
    (*packet)++;
    return (my_ulonglong) *pos;
  }
  if (*pos == 251)
  {
    (*packet)++;
    return (my_ulonglong) NULL_LENGTH;
  }
  if (*pos == 252)
  {
    (*packet)+=3;
    return (my_ulonglong) uint2korr(pos+1);
  }
  if (*pos == 253)
  {
    (*packet)+=4;
    return (my_ulonglong) uint3korr(pos+1);
  }
  (*packet)+=9;					/* Must be 254 when here */
#ifdef NO_CLIENT_LONGLONG
  return (my_ulonglong) uint4korr(pos+1);
#else
  return (my_ulonglong) uint8korr(pos+1);
#endif
}

char *
net_store_length(char *pkg, ulonglong length)
{
  uchar *packet=(uchar*) pkg;
  if (length < LL(251))
  {
    *packet=(uchar) length;
    return (char*) packet+1;
  }
  /* 251 is reserved for NULL */
  if (length < LL(65536))
  {
    *packet++=252;
    int2store(packet,(uint) length);
    return (char*) packet+2;
  }
  if (length < LL(16777216))
  {
    *packet++=253;
    int3store(packet,(ulong) length);
    return (char*) packet+3;
  }
  *packet++=254;
  int8store(packet,length);
  return (char*) packet+8;
}

