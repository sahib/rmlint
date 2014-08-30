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
    /* File still processing
     */
    RM_FILE_STATE_NORMAL,

    /* File can be ignored, has a unique hash, gets read failure
     * or is elsewhise not noteworthy.
     */
    RM_FILE_STATE_IGNORE,

    /* File hashed to end of (disk) fragment but not yet to target bytes hashed
     */
    RM_FILE_STATE_FRAGMENT,

    /* File that is already finished
     */
    //RM_FILE_STATE_FINISH
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
    RM_LINT_TYPE_DUPE_CANDIDATE,
    RM_LINT_TYPE_ORIGINAL_TAG
} RmLintType;


typedef struct RmShredGroup RmShredGroup;
typedef struct RmShredDevice RmShredDevice;

/* TODO: Reduce size of RmFile */
typedef struct RmFile {
    /* Absolute path of the file
     * */
    char *path;

    /* File modification date/time
     * */
    time_t mtime;

    /* The inode and device of this file.
     * Used to filter double paths and hardlinks.
     */
    ino_t inode;
    dev_t dev;

    /* True if this file is in one of the preferred paths,
     * i.e. paths prefixed with // on the commandline.
     */
    bool is_prefd;

    /* The index of the path this file belongs to.
     * TODO: just use a pointer?
     * alternatively, 64 bits is overkill unless we expect find / -type f | rmlint _ is expected to give
     * more than 4 billion files (and path_index is probably meaningless in terms of ranking of originals
     *  in this case anyway). So could use say guint8 and truncate paths after the 255th to path_index=255
     */
    guint64 path_index;

    /* Filesize in bytes
     */
    guint64 file_size;

    /* Physical offset from the start of the disk for the next byte to read.
     * This gets updated on a change of seek_offset,
     * so it reflects always the current readposition.
     * TODO: do we need to store this in the RmFile?  We can recalculate when needed from disk_offsets and hash_offset.
     */
    guint64 phys_offset;
    /* How many bytes were already hashed
     * (lower or equal seek_offset)
     */
    guint64 hash_offset;

    /* How many bytes were already read.
     * (lower or equal file_size)
     */
    guint64 seek_offset;

    /* Flag for when we do intermediate steps within a hash increment because the file is fragmented */
    char status;


    /* digest of this file updated on every hash iteration.
     */
    RmDigest digest;

    //~ /* State of the file, initially always RM_FILE_STATE_PROCESS
    //~ */
    //~ RmFileState state;

    /* Table of this file's extents.
     */
    RmOffsetTable disk_offsets;

    /* What kind of lint this file is.
     */
    RmLintType lint_type;

    /* If this file is a hardlink, link to (the highest ranked) hardlinked RmFile.
     * This is used to avoid hashing every file within a hardlinked set */
    struct RmFile *hardlinked_original;

    /* Link to the RmShredGroup that the file currently belongs to */
    RmShredGroup *rm_shred_group;

    /* Link to the RmShredDevice that the file is associated with */
    RmShredDevice *device;

} RmFile;

/**
 * @brief Create a new RmFile handle.
 */
RmFile *rm_file_new(
    const char *path, struct stat *statp, RmLintType type,
    RmDigestType cksum_type, bool is_ppath, unsigned pnum
);

/**
 * @brief Deallocate the memory allocated by rm_file_new
 */
void rm_file_destroy(RmFile *file);

#endif /* end of include guard */
