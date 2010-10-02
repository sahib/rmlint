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


#pragma once
#ifndef DEF_H
#define DEF_H 

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

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
/** 
 * The status interval will cause
 * the progress bar to update 1 times 
 * in x loopruns. Default: 5 
 **/ 

#define STATUS_UPDATE_INTERVAL 5

/**
 * Calculates the abolsute value
 * This macro was shamelessly taken 
 * from the glib. =)
 **/ 
 
#define ABS(a)	(((a) < 0) ? -(a) : (a))

/* This sets the size read by the fingerprint filter in bytes*
 * Setting this higher would slow down the filter but cause more accurate results 
 * Setting it lower will improve perofrmance, but may lead to more full checksums to calculate 
 * 512b is safe. 
 */
#define FP_BLSIZE 128


/* Len of a md5sum in bytes - this is not supposed t be changed */ 
#define MD5_LEN 16


/** IO: **/
/* Block size in what IO buffers are read (sync) Default:256B */
#define MD5_IO_BLOCKSIZE 2048

/* Block size in what IO buffers are read (async) Default: 8MB */
#define MD5_ASYNCBLOCKIZE 8388608

/** If a file is bigger than this value then the async routine is used. 
 *  It is faster for bigger files in general, while the synced one is better suited
 *  for many small files. Default: 8MB 
 * **/
#define MD5_SYNC_ASYNC_TRESHOLD 8388608
							
/** typedef a 32 bit type **/
typedef unsigned long int  uint32;


/** The name of the output script/log **/
#define SCRIPT_NAME "rmlint.sh"

/** Dont use - slower atm. / buggy **/
#define USE_MT_FINGERPRINTS 0

/* This will cause some debug code to be compiled. Wont have impact on proram though. */
#define DEBUG_CODE 1

#endif
