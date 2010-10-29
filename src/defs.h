/**
*  This file is part of rmlint.
*
*  rmlint is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  rmlint is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with rmlint.  If not, see <http://www.gnu.org/licenses/>.
*
** Author: Christopher Pahl <sahib@online.de>:
** Hosted at the time of writing (Do 30. Sep 18:32:19 CEST 2010):
*  http://github.com/sahib/rmlint
*
**/

/*
 * This needs some clean up
*/
#pragma once
#ifndef DEF_H
#define DEF_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

/* Use colored output? */
#define USE_COLOR 1

#if USE_COLOR
#define RED "\x1b[31;01m"
#define YEL "\x1b[33;01m"
#define NCO "\x1b[0m"
#define GRE "\x1b[32;01m"
#define BLU "\x1b[34;01m"
#endif

#if !USE_COLOR
#define RED "\x1b[0m"
#define YEL "\x1b[0m"
#define NCO "\x1b[0m"
#define GRE "\x1b[0m"
#define BLU "\x1b[0m"
#endif

#define STATUS_UPDATE_INTERVAL 5
#define ABS(a)	(((a) < 0) ? -(a) : (a))
#define MD5_LEN 16

/** IO: **/
#define MD5_IO_BLOCKSIZE  2097152   /* Block size in what IO buffers are read. Default:    2MB */
#define MD5_FP_MAX_RSZ    8192      /* The maximal size read in for fingerprints. Default   4K */
#define MD5_FP_PERCENT    10 		/* Percent of a file read in for fingerprint. Default  10% */
#define MD5_SERIAL_IO     1			/* Align threads before doing md5 related IO. Default:   1 */
#define MD5_MTHREAD_SIZE  2097152   /* If size of grp > chekcksum are built in parallel.   4MB */

/** typedef a 32 bit type **/
typedef uint_fast32_t  uint32;

/* The name of the output script/log */
#define SCRIPT_NAME "rmlint.sh"

/* This will cause some debug code to be compiled. Wont have impact on proram though. */
#define DEBUG_CODE 0

#endif
