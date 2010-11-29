
# Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
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
#

#
# Run platform checks and create ndb_config.h
#
INCLUDE(CheckFunctionExists)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckCSourceCompiles)
INCLUDE(CheckCXXSourceRuns)
INCLUDE(ndb_require_variable)

CHECK_FUNCTION_EXISTS(posix_memalign HAVE_POSIX_MEMALIGN)
CHECK_FUNCTION_EXISTS(clock_gettime HAVE_CLOCK_GETTIME)
CHECK_FUNCTION_EXISTS(pthread_condattr_setclock HAVE_PTHREAD_CONDATTR_SETCLOCK)
CHECK_FUNCTION_EXISTS(pthread_self HAVE_PTHREAD_SELF)
CHECK_FUNCTION_EXISTS(sched_get_priority_min HAVE_SCHED_GET_PRIORITY_MIN)
CHECK_FUNCTION_EXISTS(sched_get_priority_max HAVE_SCHED_GET_PRIORITY_MAX)
CHECK_FUNCTION_EXISTS(sched_setaffinity HAVE_SCHED_SETAFFINTIY)
CHECK_FUNCTION_EXISTS(sched_setscheduler HAVE_SCHED_SETSCHEDULER)
CHECK_FUNCTION_EXISTS(processor_bind HAVE_PROCESSOR_BIND)
CHECK_FUNCTION_EXISTS(epoll_create HAVE_EPOLL_CREATE)
CHECK_FUNCTION_EXISTS(memalign HAVE_MEMALIGN)
CHECK_FUNCTION_EXISTS(sysconf HAVE_SYSCONF)
CHECK_FUNCTION_EXISTS(directio HAVE_DIRECTIO)
CHECK_FUNCTION_EXISTS(atomic_swap_32 HAVE_ATOMIC_SWAP32)

CHECK_INCLUDE_FILES(sun_prefetch.h HAVE_SUN_PREFETCH_H)

# Linux scheduling and locking support
CHECK_C_SOURCE_COMPILES("
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <unistd.h>
#include <sched.h>
#include <sys/syscall.h>
int main()
{
  const cpu_set_t *p= (const cpu_set_t*)0;
  struct sched_param loc_sched_param;
  int policy = 0;
  pid_t tid = (unsigned)syscall(SYS_gettid);
  tid = getpid();
  int ret = sched_setaffinity(tid, sizeof(* p), p);
  ret = sched_setscheduler(tid, policy, &loc_sched_param);
}"
HAVE_LINUX_SCHEDULING)

# Solaris affinity support
CHECK_C_SOURCE_COMPILES("
#include <sys/types.h>
#include <sys/lwp.h>
#include <sys/processor.h>
#include <sys/procset.h>
int main()
{
  processorid_t cpu_id = (processorid_t)0;
  id_t tid = _lwp_self();
  int ret = processor_bind(P_LWPID, tid, cpu_id, 0);
}"
HAVE_SOLARIS_AFFINITY)

# Linux futex support
CHECK_C_SOURCE_COMPILES("
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>]
#define FUTEX_WAIT        0
#define FUTEX_WAKE        1
#define FUTEX_FD          2
#define FUTEX_REQUEUE     3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP     5
int main()
{
  int a = 0; int * addr = &a;
  return syscall(SYS_futex, addr, FUTEX_WAKE, 1, 0, 0, 0) == 0 ? 0 : errno;
}"
HAVE_LINUX_FUTEX)

OPTION(WITH_NDBMTD
  "Build the MySQL Cluster multithreadded data node" ON)

IF(WITH_NDBMTD)
  # checking atomic.h needed for spinlock's on sparc and Sun Studio
  CHECK_INCLUDE_FILES(atomic.h HAVE_ATOMIC_H)

  # checking assembler needed for ndbmtd
  CHECK_CXX_SOURCE_RUNS("
  #define HAVE_ATOMIC_H ${HAVE_ATOMIC_H}
  #include \"${CMAKE_SOURCE_DIR}/storage/ndb/src/kernel/vm/mt-asm.h\"
  int main()
  {
    unsigned int a = 0;
    volatile unsigned int *ap = (volatile unsigned int*)&a;
    #ifdef NDB_HAVE_XCNG
      a = xcng(ap, 1);
      cpu_pause();
    #endif
    mb();
    * ap = 2;
    rmb();
    * ap = 1;
    wmb();
    * ap = 0;
    read_barrier_depends();
    return a;
  }"
  NDB_BUILD_NDBMTD)

ENDIF()

SET(WITH_NDB_PORT "" CACHE INTEGER
  "Default port used by MySQL Cluster management server")
IF(WITH_NDB_PORT GREATER 0)
  SET(NDB_PORT ${WITH_NDB_PORT})
  MESSAGE(STATUS "Setting MySQL Cluster management server port to ${NDB_PORT}")
ENDIF()

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/include/ndb_config.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/include/ndb_config.h)

# Define HAVE_NDB_CONFIG_H to make ndb_global.h include the
# generated ndb_config.h
ADD_DEFINITIONS(-DHAVE_NDB_CONFIG_H)

# check zlib
IF(NOT DEFINED WITH_ZLIB)
  # Hardcode use of the bundled zlib if not set by MySQL
  MESSAGE(STATUS "Using bundled zlib (hardcoded)")
  SET(ZLIB_LIBRARY zlib)
  SET(ZLIB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/zlib)
ENDIF()
NDB_REQUIRE_VARIABLE(ZLIB_LIBRARY)
NDB_REQUIRE_VARIABLE(ZLIB_INCLUDE_DIR)

