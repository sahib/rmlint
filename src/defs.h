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
#include <pthread.h>

#include "config.h"
#include "checksum.h"
#include "mounttable.h"

#define RED "\x1b[31;01m"
#define YEL "\x1b[33;01m"
#define NCO "\x1b[0m"
#define GRE "\x1b[32;01m"
#define BLU "\x1b[34;01m"

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

/* Use double slashes, so we can easily split the line to an array */
#define LOGSEP "//"

/* ------------------------------------------------------------- */

/* -cC */
#define CMD_DUPL "<dupl>"
#define CMD_ORIG "<orig>"

/* ------------------------------------------------------------- */

/* types of lint */
typedef enum RmLintType {
    TYPE_UNKNOWN = 0,
    TYPE_BLNK,
    TYPE_EDIR,
    TYPE_EFILE,
    TYPE_NBIN,
    TYPE_BASE,
    TYPE_BADUID,
    TYPE_BADGID,
    TYPE_BADUGID,
    /* Border */
    TYPE_OTHER_LINT,
    TYPE_DUPE_CANDIDATE /* note: this needs to be last item in list so*
						 * that "other" lint gets handled first       */
} RmLintType;

typedef enum RmHandleMode {
    RM_MODE_LIST = 1,
    RM_MODE_NOASK = 3,
    RM_MODE_LINK = 4,
    RM_MODE_CMD = 5
} RmHandleMode;

/* all available settings see rmlint -h */
typedef struct RmSettings {
    RmHandleMode mode;
    char color;
    char collide;
    char samepart;
    char ignore_hidden;
    char followlinks;
    char paranoid;
    char namecluster;
    char findbadids;
    char searchdup;
    char findemptydirs;
    char nonstripped;
    char verbosity;
    char listemptyfiles;
    char **paths;
    char *is_ppath;              /* NEW - flag for each path; 1 if preferred/orig, 0 otherwise*/
    int  num_paths;              /* NEW - counter to make life easier when multi-threading the paths */
    char *cmd_path;
    char *cmd_orig;
    char *output_script;
    char *output_log;
    char *sort_criteria;         /* NEW - sets criteria for ranking and selecting "original"*/
    char limits_specified;
    guint64 minsize;
    guint64 maxsize;
    char keep_all_originals;     /* NEW - if set, will ONLY delete dupes that are not in ppath */
    char must_match_original;    /* NEW - if set, will ONLY search for dupe sets where at least one file is in ppath*/
    char invert_original;        /* NEW - if set, inverts selection so that paths _not_ prefixed with // are preferred*/
    char find_hardlinked_dupes;  /* NEW - if set, will also search for hardlinked duplicates*/
    char skip_confirm;           /* NEW - if set, bypasses user confirmation of input settings*/
    char confirm_settings;       /* NEW - if set, pauses for user confirmation of input settings*/
    guint64 threads;
    short depth;
    RmDigestType checksum_type;  /* NEW - determines the checksum algorithm used */
    char *iwd;                   /* cwd when rmlint called */
} RmSettings;

typedef enum RmFileState {
    RM_FILE_STATE_PROCESS,
    RM_FILE_STATE_IGNORE,
    RM_FILE_STATE_FINISH
} RmFileState;

typedef struct _RmFile {
    unsigned char checksum[_RM_HASH_LEN];// TODO: remove.   /* md5sum of the file */
    unsigned char fp[2][_RM_HASH_LEN];// TODO: remove.        /* A short fingerprint of a file - start and back */
    unsigned char bim[BYTE_MIDDLE_SIZE];// TODO: remove. /* Place where the infamouse byInThMiddle are stored */

    char *path;                          /* absolute path from working dir */
    bool in_ppath;                       /* set if this file is in one of the preferred (originals) paths */
    unsigned pnum;                   /* numerical index of user-input paths */
    //TODO: is this long enough for case where we pipe output of find to rmlint - ??
    guint64 fsize;                       /* Size of the file (bytes) */
    time_t mtime;                        /* File modification date/time */
    bool filter;// TODO: remove.                         /* this is used in calculations  */
    RmLintType lint_type;                /* Is the file marked as duplicate? */

    /* This is used to find pointers to the physically same file */
    ino_t node;
    dev_t dev;
    guint64 offset;                    /*offset in bytes from start of device*/
    guint64 hash_offset;
    guint64 seek_offset;

    RmDigest digest;

    GList *list_node; // TODO: remove.
    GSequenceIter *file_group;// TODO: remove.
    struct _RmFile *hardlinked_original;
    RmFileState state;

    GSequence *disk_offsets;
    GMutex file_lock;
} RmFile;

typedef struct RmFileList {
    RmMountTable *mounts;
    GSequence *size_groups;
    GHashTable *size_table;
    GRecMutex lock;
} RmFileList;

typedef struct RmUserGroupList {
    gulong gid, uid;
} RmUserGroupList;

typedef struct RmSession {
    RmFileList *list;
    RmSettings *settings;
    RmMountTable *mounts;

    guint64 total_files;
    guint64 total_lint_size;
    guint64 dup_counter;

    FILE *script_out;
    FILE *log_out;

    gint activethreads;
    pthread_mutex_t threadlock;

    volatile bool aborted;
} RmSession;

#endif
