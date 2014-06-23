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
** Hosted on http://github.com/sahib/rmlint
*
**/


#pragma once
#ifndef DEF_H
#define DEF_H

#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <stdbool.h>
#include <glib.h>


/* Use colored output? Note: there's also a -Bb option */
#define USE_COLOR 1

#if USE_COLOR
#define RED "\x1b[31;01m"
#define YEL "\x1b[33;01m"
#define NCO "\x1b[0m"
#define GRE "\x1b[32;01m"
#define BLU "\x1b[34;01m"
#endif

#if !USE_COLOR
#define RED ""
#define YEL ""
#define NCO ""
#define GRE ""
#define BLU ""
#endif

/* not supposed to be changed */
#define ABS(a)  (((a) < 0) ? -(a) : (a))
#define MD5_LEN 16

/* Which scheduler to take
 * + 1) Always single threaded on each group
 * + 2) Run max. n (where n may be max. set->threads) at the same time.
 * + 3) If a group-size is larger than MD5_MTHREAD_SIZE a new thread is started, otherwise singlethreaded
 * */
#define THREAD_SHEDULER_MTLIMIT 8388608

/* ------------------------------------------------------------- */

/** IO: **/
/* Those values are by no means constants, you can/should adjust them to fit your system */
/* nevertheless: They should fit quite well for an average 2010's desk, so be careful when changing */

#define MD5_MTHREAD_SIZE   2097152   /* If size of grp > chekcksum are built in parallel.   2MB */
#define MD5_IO_BLOCKSIZE   1048576   /* Block size in what IO buffers are read. Default:    1MB */
#define MD5_FP_MAX_RSZ     8192      /* The maximal size read in for fingerprints. Default   8K */
#define MD5_FP_PERCENT     10        /* Percent of a file read in for fingerprint. Default  10% */
#define MD5_SERIAL_IO      1         /* Align threads before doing md5 related IO. Default:   1 */
#define MD5_USE_MMAP       -1        /* Use mmap() instead of fread() EXPERIMENTAL! Use = risk! */
/*
0 = fread only
1 = mmap only
      -1 = autochoice (which is best mostly)
     */

/* Do not use O_DIRECT! read() will do weird things */
/*
From man 2 open:

 "The thing that has always disturbed me about O_DIRECT is that the whole interface is just stupid,
  and was probably designed by a deranged monkey on some serious mind-controlling substances."
  - Linus Torvalds

*/
#ifdef O_CLOEXEC
#define MD5_FILE_FLAGS  (O_RDONLY | O_CLOEXEC)
#else
#define MD5_FILE_FLAGS  (O_RDONLY)
#endif

/* ------------------------------------------------------------- */

#define MMAP_LIMIT      (MD5_MTHREAD_SIZE << 4)

/* ------------------------------------------------------------- */

#define MD5_FPSIZE_FORM(X) sqrt(X / MD5_FP_PERCENT) + 1

/* ------------------------------------------------------------- */

/** nuint_t = normal unsigned integer type :-) **/
typedef uint64_t nuint_t;

/* I can haz bool? */
/*#typedef char bool;
#define false ( 0)
#define true  (!0)*/

/* ------------------------------------------------------------- */

/* Investigate directories by a depth first algorithm instead of (mostly) random access */
/* found no advantage in depth first + it's just a paramter of ntwf() */
/* Just have it here for convienience */
#define USE_DEPTH_FIRST 0

/* Reads a short sequence of bytes in the middle of a file, while doing fingerprints */
/* This almost cost nothing, but helps a lot with lots of similiar datasets */
#define BYTE_MIDDLE_SIZE 16

/* Use double slashes, so we can easily split the line to an array */
#define LOGSEP "//"

/* ------------------------------------------------------------- */

/* -cC */
#define CMD_DUPL "<dupl>"
#define CMD_ORIG "<orig>"

/* ------------------------------------------------------------- */

/* types of lint */
enum {
    TYPE_DUPE_CANDIDATE = 1,
    TYPE_BLNK = 3,
    TYPE_OTMP,
    TYPE_EDIR,
    TYPE_JNK_DIRNAME,
    TYPE_JNK_FILENAME,
    TYPE_NBIN,
    TYPE_BASE,
    TYPE_BADUID,
    TYPE_BADGID,
    TYPE_BADUGID
} LintType;

/* all available settings see rmlint -h */
typedef struct {
    char mode;
    char color;
    char collide;
    char samepart;
    char ignore_hidden;
    char followlinks;
    char casematch;
    char paranoid;
    char invmatch;
    char namecluster;
    char doldtmp;
    char findbadids;
    char searchdup;
    char findemptydirs;
    char nonstripped;
    char verbosity;
    char listemptyfiles;
    char **paths;
    char *is_ppath;           /*NEW - flag for each path; 1 if preferred/orig, 0 otherwise*/
    char *dpattern;
    char *fpattern;
    char *cmd_path;
    char *cmd_orig;
    char *junk_chars;
    char *output;
    char *sort_criteria;       /*NEW - sets criteria for ranking and selecting "original"*/
    int minsize;
    int maxsize;
    char keep_all_originals;   /*NEW - if set, will ONLY delete dupes that are not in ppath */
    char must_match_original;  /*NEW - if set, will ONLY search for dupe sets where at least one file is in ppath*/
    char invert_original;      /*NEW - if set, inverts selection so that paths _not_ prefixed with // are preferred*/
    char find_hardlinked_dupes;/*NEW - if set, will also search for hardlinked duplicates*/
    char skip_confirm;         /*NEW - if set, bypasses user confirmation of input settings*/
    nuint_t threads;
    short depth;
    nuint_t oldtmpdata;
} RmSettings;

typedef enum RmFileType {
    RM_FILE_TYPE_UNKNOWN = 0,
    RM_FILE_TYPE_DUPLICATE = 1
} RmFileType;

typedef struct RmFile {
    unsigned char md5_digest[MD5_LEN];   /* md5sum of the file */
    unsigned char fp[2][MD5_LEN];        /* A short fingerprint of a file - start and back */
    unsigned char bim[BYTE_MIDDLE_SIZE]; /* Place where the infamouse byInThMiddle are stored */

    char *path;                          /* absolute path from working dir */
    bool in_ppath;                       /* set if this file is in one of the preferred (originals) paths */
    unsigned int pnum;                   /* numerical index of user-input paths */
    guint64 fsize;                       /* Size of the file (bytes) */
    time_t mtime;                        /* File modification date/time */
    bool filter;                         /* this is used in calculations  */
    RmFileType dupflag;                  /* Is the file marked as duplicate? */

    /* This is used to find pointers to the physically same file */
    ino_t node;
    dev_t dev;

    GList *list_node;
    GQueue *file_group;
} RmFile;


/* file_group; models a 'sublist' */
typedef struct {
    /* Start and end pointer of a 'group' */
    RmFile *grp_stp, *grp_enp;

    /* elems in this list and total size in bytes */
    nuint_t len, size;

} file_group;


typedef struct {
    unsigned long gid;
    unsigned long uid;
} UserGroupList;


#endif
