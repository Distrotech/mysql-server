/* Copyright (C) 2003 MySQL AB

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


#include <sched.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

int
main(int argc, char * const argv[]){
  struct sched_param p;
  p.sched_priority = 1;
  
  int ret = sched_setscheduler(getpid(), SCHED_RR, &p);
  printf("ref = %d\n", ret);

  execv(argv[1], &argv[1]);
  return 0;
}
