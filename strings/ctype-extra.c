/* Copyright (C) 2000 MySQL AB

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
#include <m_ctype.h>

CHARSET_INFO compiled_charsets[] = {
  {
    0,0,0,		/* end-of-list marker */
    0,			/* state      */
    NullS,		/* cs name    */
    NullS,		/* name       */
    NullS,		/* comment    */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,		/* tab_to_uni   */
    NULL,		/* tab_from_uni */
    "","",
    0,
    NULL,		/* strnncoll    */
    NULL,		/* strnncollsp  */
    NULL,		/* strnxfrm     */
    NULL,		/* like_range   */
    NULL,		/* wildcmp      */
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,		 /* mb_wc      */
    NULL,		 /* wc_mb      */
    
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,		/* hash_sort   */
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
  }
};
