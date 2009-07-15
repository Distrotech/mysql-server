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

#ifdef __sgi
/* define on IRIX to get posix compliant vsnprintf */
#define _XOPEN_SOURCE 500
#endif
#include <stdio.h>
#include <basestring_vsnprintf.h>
#include <my_global.h>

int
basestring_snprintf(char *str, size_t size, const char *format, ...)
{
  int ret;
  va_list ap;
  va_start(ap, format);
  ret= basestring_vsnprintf(str, size, format, ap);
  va_end(ap);
  return(ret);
}

int
vsnprintf_doubling(size_t size, const char *format, va_list ap)
{
  char *buf = 0;
  int ret = -1;

  while (ret < 0 || ret >= size)
  {
    buf = realloc(buf, size*=2);
    ret = vsnprintf(buf, size, format, ap);
  }
  free(buf);
  return ret;
}

int
basestring_vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
  int ret;

  if (size == 0)
  {
    char buf[1];
    return basestring_vsnprintf(buf, 1, format, ap);
  }
  ret = IF_WIN(_vsnprintf,vsnprintf)(str, size, format, ap);
  if (ret >= 0 && ret < size)
    return ret;
#ifdef _WIN32
  if (errno == EINVAL)
    return ret;
  // otherwise, more than size chars are needed
  return _vscprintf(format, ap);
#endif
#ifdef SNPRINTF_RETURN_TRUNC
  {
    char buf[512];
    ret = vsnprintf(buf, sizeof(buf), format, ap);
    if (ret >= 0 && ret < size)
      return ret;
    ret = vsnprintf_doubling(sizeof(buf), format, ap);
  }
#endif
  return ret;
}
