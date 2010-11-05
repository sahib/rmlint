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

#define ABS(a)	(((a) < 0) ? -(a) : (a))
#define MD5_LEN 16

/* Whic sheduler to take 
 * + 1) Always single threaded on each group 
 * + 2) Run max. n (where n may be max. set.threads) at the same time. 
 * + 3) If a group-size is larger than MD5_MTHREAD_SIZE a new thread is started, otherwise singlethreaded 
 * */
#define THREAD_SHEDULER 1
#define THREAD_SHEDULER_MTLIMIT 8388608

/** IO: **/
#define MD5_MTHREAD_SIZE   2097152   /* If size of grp > chekcksum are built in parallel.   2MB */
#define MD5_IO_BLOCKSIZE   1048576   /* Block size in what IO buffers are read. Default:    1MB */
#define MD5_FP_MAX_RSZ     8192      /* The maximal size read in for fingerprints. Default   4K */
#define MD5_FP_PERCENT     10 		 /* Percent of a file read in for fingerprint. Default  10% */
#define MD5_SERIAL_IO      1		 /* Align threads before doing md5 related IO. Default:   1 */

#define MD5_FPSIZE_FORM(X) sqrt(X / MD5_FP_PERCENT) + 1;

/** typedef a 32 bit type **/
typedef unsigned int uint32;

/* What to append at the end of a command, Default " ;\n" so script continues also on error*/
#define SCRIPT_LINE_SUFFIX  " ;"

/* Last line in script */
#define SCRIPT_LAST "echo Done"

/* Investigate directories by a depth first algorithm instead of (mostly) random access */
#define USE_DEPTH_FIRST 0

/* Reads a short sequence of bytes in the middle of a file  */
#define BYTE_IN_THE_MIDDLE 1
#define BYTE_MIDDLE_SIZE 8

/* This will cause some debug code to be compiled. Wont have impact on proram though. */
#define DEBUG_CODE 0


/* types of lint */
#define TYPE_BLNK 42
#define TYPE_OTMP 43
#define TYPE_EDIR 44
#define TYPE_JNK_DIRNAME  45
#define TYPE_JNK_FILENAME 46
#endif
