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

#include "mysys_priv.h"
#include <sys/stat.h>
#include <m_string.h>
#if defined(HAVE_UTIME_H)
#include <utime.h>
#elif defined(HAVE_SYS_UTIME_H)
#include <sys/utime.h>
#elif !defined(HPUX10)
struct utimbuf {
  time_t actime;
  time_t modtime;
};
#endif

/*
  Append a file to another

  NOTES
    Don't set MY_FNABP or MY_NABP bits on when calling this function
*/

int my_append(const char *from, const char *to, myf MyFlags)
{
  size_t Count;
  File from_file,to_file;
  uchar buff[IO_SIZE];
  DBUG_ENTER("my_append");
  DBUG_PRINT("my",("from %s to %s MyFlags %d", from, to, MyFlags));

  from_file= to_file= -1;

  if ((from_file=my_open(from,O_RDONLY,MyFlags)) >= 0)
  {
    if ((to_file=my_open(to,O_APPEND | O_WRONLY,MyFlags)) >= 0)
    {
      while ((Count=my_read(from_file,buff,IO_SIZE,MyFlags)) != 0)
	if (Count == (uint) -1 ||
	    my_write(to_file,buff,Count,MYF(MyFlags | MY_NABP)))
	  goto err;
      if (my_close(from_file,MyFlags) | my_close(to_file,MyFlags))
	DBUG_RETURN(-1);				/* Error on close */
      DBUG_RETURN(0);
    }
  }
err:
  if (from_file >= 0) VOID(my_close(from_file,MyFlags));
  if (to_file >= 0)   VOID(my_close(to_file,MyFlags));
  DBUG_RETURN(-1);
}
