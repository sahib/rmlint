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
** Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#ifndef DEF_H
#define DEF_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <stdbool.h>
#include <glib.h>

//#include "config.h"
//#include "checksum.h"
//#include "utilities.h"

#define RED "\x1b[31;01m"
#define YEL "\x1b[33;01m"
#define NCO "\x1b[0m"
#define GRE "\x1b[32;01m"
#define BLU "\x1b[34;01m"


// TODO: Remove all that once new scheduler is in.
/* Which scheduler to take
 * + 1) Always single threaded on each group
 * + 2) Run max. n (where n may be max. set->threads) at the same time.
 * + 3) If a group-size is larger than HASH_MTHREAD_SIZE a new thread is started, otherwise singlethreaded
 * */
#define THREAD_SHEDULER_MTLIMIT (1024 * 1024 * 8)

/* ------------------------------------------------------------- */

/** IO: **/
/* Those values are by no means constants, you can/should adjust them to fit your system */
/* nevertheless: They should fit quite well for an average 2010's desk, so be careful when changing */

#define HASH_MTHREAD_SIZE   (1024 * 1024 * 2) /* If size of grp > chekcksum are built in parallel.   2MB */
#define HASH_IO_BLOCKSIZE   (1024 * 1024 * 1) /* Block size in what IO buffers are read. Default:    1MB */
#define HASH_FP_MAX_RSZ     (8 * 1024)        /* The maximal size read in for fingerprints. Default   8K */
#define HASH_FP_PERCENT     10                /* Percent of a file read in for fingerprint. Default  10% */
#define HASH_SERIAL_IO      1                 /* Align threads before doing md5 related IO. Default:   1 */
#define HASH_USE_MMAP       -1                /* Use mmap() instead of fread() EXPERIMENTAL! Use = risk! */
/*
0 = fread only
1 = mmap only
-1 = autochoice (which is best mostly)

Do not use O_DIRECT! read() will do weird things
From man 2 open:

 "The thing that has always disturbed me about O_DIRECT is that the whole interface is just stupid,
  and was probably designed by a deranged monkey on some serious mind-controlling substances."
  -- Linus Torvalds
*/

#define HASH_FILE_FLAGS  (O_RDONLY)

/* ------------------------------------------------------------- */

#define MMAP_LIMIT (HASH_MTHREAD_SIZE << 4)

/* ------------------------------------------------------------- */

#define HASH_FPSIZE_FORM(X) sqrt(X / HASH_FP_PERCENT) + 1

/* Reads a short sequence of bytes in the middle of a file, while doing fingerprints */
/* This almost cost nothing, but helps a lot with lots of similiar datasets */
#define BYTE_MIDDLE_SIZE 16




#endif
