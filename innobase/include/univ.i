/***************************************************************************
Version control for database, common definitions, and include files

(c) 1994 - 2000 Innobase Oy

Created 1/20/1994 Heikki Tuuri
****************************************************************************/

#ifndef univ_i
#define univ_i

#if (defined(_WIN32) || defined(_WIN64)) && !defined(MYSQL_SERVER)
#define __WIN__

#include <windows.h>

/* When compiling for Itanium IA64, undefine the flag below to prevent use
of 32-bit assembler */

#ifndef WIN64
#define UNIV_CAN_USE_X86_ASSEMBLER
#endif

/* If you want to check for errors with compiler level -W4,
comment out the above include of windows.h and let the following defines
be defined:
#define HANDLE void*
#define CRITICAL_SECTION	ulint
*/

#ifdef _NT_
#define __NT__
#endif

#else
/* The Unix version */

/* Most C compilers other than gcc do not know 'extern inline' */ 
#if !defined(__GNUC__) && !defined(__WIN__)
#undef UNIV_MUST_NOT_INLINE
#define UNIV_MUST_NOT_INLINE
#endif

/* Include two header files from MySQL to make the Unix flavor used
in compiling more Posix-compatible. We assume that 'innobase' is a
subdirectory of 'mysql'. */
#include <my_global.h>
#include <my_pthread.h>

/* Include <sys/stat.h> to get S_I... macros defined for os0file.c */
#include <sys/stat.h>

#undef PACKAGE
#undef VERSION

/* Include the header file generated by GNU autoconf */
#include "../ib_config.h"

#ifdef HAVE_SCHED_H
#include <sched.h>
#endif

#ifdef HAVE_PREAD
#define HAVE_PWRITE
#endif

#endif

/*			DEBUG VERSION CONTROL
			===================== */

/* The following flag will make InnoDB to initialize
all memory it allocates to zero. It hides Purify
warnings about reading unallocated memory unless
memory is read outside the allocated blocks. */
/*
#define UNIV_INIT_MEM_TO_ZERO
*/

/* Make a non-inline debug version */

/*
#define UNIV_DEBUG
#define UNIV_MEM_DEBUG
#define UNIV_SYNC_DEBUG

#define UNIV_IBUF_DEBUG
#define UNIV_SEARCH_DEBUG
#define UNIV_SYNC_PERF_STAT
#define UNIV_SEARCH_PERF_STAT
*/
#define UNIV_LIGHT_MEM_DEBUG

#define YYDEBUG			1

/*
#define UNIV_SQL_DEBUG
#define UNIV_LOG_DEBUG
*/
			/* the above option prevents forcing of log to disk
			at a buffer page write: it should be tested with this
			option off; also some ibuf tests are suppressed */
/*
#define UNIV_BASIC_LOG_DEBUG
*/
			/* the above option enables basic recovery debugging:
			new allocated file pages are reset */

#if (!defined(UNIV_DEBUG) && !defined(INSIDE_HA_INNOBASE_CC) && !defined(UNIV_MUST_NOT_INLINE))
/* Definition for inline version */

#ifdef __WIN__
#define UNIV_INLINE  	__inline
#else
/* config.h contains the right def for 'inline' for the current compiler */
#if (__GNUC__ == 2)
#define UNIV_INLINE  extern inline
#else
/* extern inline doesn't work with gcc 3.0.2 */
#define UNIV_INLINE  static inline
#endif
#endif

#else
/* If we want to compile a noninlined version we use the following macro
definitions: */

#define UNIV_NONINL
#define UNIV_INLINE

#endif	/* UNIV_DEBUG */

#ifdef _WIN32
#define UNIV_WORD_SIZE		4
#elif defined(_WIN64)
#define UNIV_WORD_SIZE		8
#else
/* MySQL config.h generated by GNU autoconf will define SIZEOF_LONG in Posix */
#define UNIV_WORD_SIZE		SIZEOF_LONG
#endif

/* The following alignment is used in memory allocations in memory heap
management to ensure correct alignment for doubles etc. */
#define UNIV_MEM_ALIGNMENT      8

/* The following alignment is used in aligning lints etc. */
#define UNIV_WORD_ALIGNMENT	UNIV_WORD_SIZE

/*
			DATABASE VERSION CONTROL
			========================
*/

/* The universal page size of the database */
#define UNIV_PAGE_SIZE          (2 * 8192) /* NOTE! Currently, this has to be a
					power of 2 */
/* The 2-logarithm of UNIV_PAGE_SIZE: */
#define UNIV_PAGE_SIZE_SHIFT	14					

/* Maximum number of parallel threads in a parallelized operation */
#define UNIV_MAX_PARALLELISM	32

/*
			UNIVERSAL TYPE DEFINITIONS
			==========================
*/

/* Note that inside MySQL 'byte' is defined as char on Linux! */
#define byte	unsigned char

/* Another basic type we use is unsigned long integer which is intended to be
equal to the word size of the machine. */

typedef unsigned long int	ulint;

typedef long int		lint;

#ifdef __WIN__
typedef __int64       ib_longlong;
#else
typedef longlong ib_longlong;
#endif

/* The following type should be at least a 64-bit floating point number */
typedef double		utfloat;

/* The 'undefined' value for a ulint */
#define ULINT_UNDEFINED		((ulint)(-1))

/* The undefined 32-bit unsigned integer */
#define	ULINT32_UNDEFINED	0xFFFFFFFF

/* Maximum value for a ulint */
#define ULINT_MAX		((ulint)(-2))

/* This 'ibool' type is used within Innobase. Remember that different included
headers may define 'bool' differently. Do not assume that 'bool' is a ulint! */
#define ibool	ulint

#ifndef TRUE

#define TRUE    1
#define FALSE   0

#endif

/* The following number as the length of a logical field means that the field
has the SQL NULL as its value. */
#define UNIV_SQL_NULL 	ULINT_UNDEFINED

/* Lengths which are not UNIV_SQL_NULL, but bigger than the following
number indicate that a field contains a reference to an externally
stored part of the field in the tablespace. The length field then
contains the sum of the following flag and the locally stored len. */

#define UNIV_EXTERN_STORAGE_FIELD (UNIV_SQL_NULL - UNIV_PAGE_SIZE)

/* The following definition of __FILE__ removes compiler warnings
associated with const char* / char* mismatches with __FILE__ */

#define IB__FILE__	((char*)__FILE__)

#include <stdio.h>
#include "ut0dbg.h"
#include "ut0ut.h"
#include "db0err.h"

#endif
