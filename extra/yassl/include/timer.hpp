/*
   Copyright (C) 2000-2007 MySQL AB
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

/* timer.hpp provides a high res and low res timers
 *
*/


#ifndef yaSSL_TIMER_HPP
#define yaSSL_TIMER_HPP

namespace yaSSL {

typedef double       timer_d;
typedef unsigned int uint;



timer_d timer();
uint    lowResTimer();



} // namespace
#endif // yaSSL_TIMER_HPP
