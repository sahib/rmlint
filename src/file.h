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

#ifndef RM_FILE_H
#define RM_FILE_H

#include <sys/stat.h>
#include <stdbool.h>
#include <glib.h>

#include "checksum.h"
#include "utilities.h"

typedef enum RmFileState {
    RM_FILE_STATE_PROCESS,
    RM_FILE_STATE_IGNORE,
    RM_FILE_STATE_FINISH
} RmFileState;

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

/* TODO: Reduce size of RmFile */
typedef struct RmFile {
    char *path;                          /* absolute path from working dir */
    bool in_ppath;                       /* set if this file is in one of the preferred (originals) paths */
    time_t mtime;                        /* File modification date/time */

    ino_t node;
    dev_t dev;
    dev_t disk;

    guint64 path_index;
    guint64 file_size;

    guint64 phys_offset;
    guint64 hash_offset;
    guint64 seek_offset;

    RmDigest digest;
    RmFileState state;
    RmOffsetTable disk_offsets;
    RmLintType lint_type;

    struct RmFile *hardlinked_original;

    GMutex file_lock;
} RmFile;

RmFile *rm_file_new(const char *path,
                    guint64 fsize,
                    ino_t node,
                    dev_t dev,
                    time_t mtime,
                    RmLintType type,
                    bool is_ppath,
                    unsigned pnum);

void rm_file_destroy(RmFile *file);

#endif /* end of include guard */
