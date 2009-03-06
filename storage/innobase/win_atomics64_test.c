# Copyright (C) 2009 Sun Microsystems AB
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#include <windows.h>

int main()
{
  volatile long long var64 = 0;
  long long add64 = 1;
  long long old64 = 0;
  long long exch64 = 1;
  long long ret_value;

  ret_value = InterlockedExchangeAdd64(&var64, add64);
  ret_value = InterlockedCompareExchange64(&var64, exch64, old64);
  MemoryBarrier();
  return EXIT_SUCCESS;
}
