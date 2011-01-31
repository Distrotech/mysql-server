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

/*
 * hrt_stopwatch.h
 *
 */

#ifndef _utils_hrt_stopwatch
#define _utils_hrt_stopwatch
#ifdef __cplusplus
extern "C" {
#endif

#include "hrt_utils.h"

/*
 * High-Resolution Time Stopwatch Utility
 */

/**
 * A snapshot of the system's real and this process's cpu time count.
 */
typedef struct hrt_stopwatch {
    hrt_tstamp* tstamps;
    unsigned int top;
    unsigned int cap;
} hrt_stopwatch;

/*
 * Functions for time marks.
 */

/**
 * Initializes a stopwatch with a given storage capacity for timemarks.
 */
extern void hrt_sw_init(hrt_stopwatch* sw, int cap);

/**
 * Releases all reources allocated by a stopwatch.
 */
extern void hrt_sw_close(hrt_stopwatch* sw);

/**
 * Returns the index of a stopwatch's last timemark.
 */
extern int hrt_sw_top(const hrt_stopwatch* sw);

/**
 * Returns the number of timemarks a stopwatch can hold.
 */
extern int hrt_sw_capacity(const hrt_stopwatch* sw);

/**
 * Marks the time and stores that mark in a stopwatch returning the index.
 */
extern int hrt_sw_pushmark(hrt_stopwatch* sw);

/**
 * Removes the top timemark from a stopwatch.
 */
extern void hrt_sw_popmark(hrt_stopwatch* sw);

/**
 * Returns the real time amount between two timemarks in microseonds
 * (i.e., y - x).
 */
extern double hrt_sw_rtmicros(const hrt_stopwatch* sw, int y, int x);

/**
 * Returns the cpu time amount between two timemarks in microseonds
 * (i.e., y - x).
 */
extern double hrt_sw_ctmicros(const hrt_stopwatch* sw, int y, int x);

/**
 * Clears all timemarks stored in a stopwatch.
 */
extern void hrt_sw_clear(hrt_stopwatch* sw);

#ifdef __cplusplus
}
#endif
#endif
