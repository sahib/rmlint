/**
*  This file is part of autovac.
*
*  autovac is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  autovac is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with autovac.  If not, see <http://www.gnu.org/licenses/>.
*
** Author: Christopher Pahl <sahib@online.de>:
** Hosted at the time of writing (Mo 30. Aug 14:02:22 CEST 2010): 
*  http://github.com/sahib/autovac
*   
**/


#pragma once
#ifndef DEF_H
#define DEF_H 

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

 
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

/**
 * FP_BLSIZE = Size of those blocks in bytes
 **/
#define FP_BLSIZE 512


/** Better dont change this - really ;-) **/
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
typedef unsigned long int  UINT4;


/** The name of the output script **/
#define SCRIPT_NAME "rmlint.sh"

/** Dont use - slower atm. / buggy **/
#define USE_MT_FINGERPRINTS 0

#define DEBUG_CODE 0

#endif
