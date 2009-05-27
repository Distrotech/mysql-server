/*
   Copyright (C) 2005 MySQL AB
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

#pragma once
#include "WindowsService.h"

class IMService: public WindowsService
{
public:
  static int main();

private:
  IMService(void);
  ~IMService(void);

protected:
  void Log(const char *msg);
  void Stop();
  void Run(DWORD argc, LPTSTR *argv);
};
