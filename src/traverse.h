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

#ifndef TRAVERSE_H
#define TRAVERSE_H

#include <fts.h>
#include <assert.h>
#include <stddef.h>
#include <regex.h>
#include <glib.h>
#include <stdbool.h>

//#include "defs.h"
//#include "cmdline.h"
#include "config.h"
#include "checksum.h"
//#include "utilities.h"

//#include "shredder.h"


///////////////////////////////
// RmFile defs and utilities //
///////////////////////////////

/* types of lint */
typedef enum RmLintType {
    RM_LINT_TYPE_UNKNOWN = 0,
    RM_LINT_TYPE_BLNK,
    RM_LINT_TYPE_EDIR,
    RM_LINT_TYPE_EFILE,
    RM_LINT_TYPE_NBIN,
    RM_LINT_TYPE_BASE,
    RM_LINT_TYPE_BADUID,
    RM_LINT_TYPE_BADGID,
    RM_LINT_TYPE_BADUGID,
    /* Border */
    RM_LINT_TYPE_OTHER_LINT,
    /* note: this needs to be last item in list */
    RM_LINT_TYPE_DUPE_CANDIDATE
} RmLintType;


typedef enum RmFileState {
    RM_FILE_STATE_PROCESS,
    RM_FILE_STATE_IGNORE,
    RM_FILE_STATE_FINISH
} RmFileState;

/* typedef RmOffsetTable, in case we need to exchange
 * the data structure at any point.
 */
typedef GSequence *RmOffsetTable;


/* TODO: Reduce size of RmFile */
typedef struct _RmFile {
    unsigned char checksum[_RM_HASH_LEN];// TODO: remove.   /* md5sum of the file */
    //unsigned char fp[2][_RM_HASH_LEN];// TODO: remove.        /* A short fingerprint of a file - start and back */
    //unsigned char bim[BYTE_MIDDLE_SIZE];// TODO: remove. /* Place where the infamouse byInThMiddle are stored */

    char *path;                          /* absolute path from working dir */
    bool in_ppath;                       /* set if this file is in one of the preferred (originals) paths */
    unsigned long pnum;                  /* numerical index of user-input paths */
    guint64 fsize;                       /* Size of the file (bytes) */
    time_t mtime;                        /* File modification date/time */
    bool filter;// TODO: remove.                         /* this is used in calculations  */
    RmLintType lint_type;                /* Is the file marked as duplicate? */

    /* This is used to find pointers to the physically same file */
    ino_t node;
    dev_t dev;
    dev_t disk;
    guint64 offset;                    /*offset in bytes from start of device*/
    guint64 hash_offset;
    guint64 seek_offset;

    RmDigest digest;

    GList *list_node; // TODO: remove.
    GSequenceIter *file_group;// TODO: remove.
    struct _RmFile *hardlinked_original;
    RmFileState state;

    RmOffsetTable disk_offsets;
    GMutex file_lock;
} RmFile;



typedef struct RmSession RmSession;

guint64 rm_search_tree(RmSession *session);

/**
 * @brief Allocate and new RmFile and populate with args
 */

//RmFile *rm_file_new(const char *path,
//                    guint64 fsize,
//                    ino_t node,
//                    dev_t dev,
//                    time_t mtime,
//                    RmLintType type,
//                    bool is_ppath,
//                    unsigned pnum);
//
/**
 * @brief Free the memory allocated by rm_file_new()
 */

void rm_file_destroy(RmFile *file);


#endif
