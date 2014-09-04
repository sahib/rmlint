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
} RmFileState;

/* types of lint */
typedef enum RmLintType {
    RM_LINT_TYPE_UNKNOWN = 0,
    RM_LINT_TYPE_BLNK,
    RM_LINT_TYPE_EDIR,
    RM_LINT_TYPE_EFILE,
    RM_LINT_TYPE_NBIN,
    RM_LINT_TYPE_BADUID,
    RM_LINT_TYPE_BADGID,
    RM_LINT_TYPE_BADUGID,

    /* Border */
    RM_LINT_TYPE_OTHER_LINT,

    /* note: this needs to be last item in list */
    RM_LINT_TYPE_DUPE_CANDIDATE,
    RM_LINT_TYPE_ORIGINAL_TAG
} RmLintType;


/**
 * RmFile structure; used by pretty much all rmlint modules.
 */

/* Sub-structures defined and used in shredder.c:*/
struct RmShredGroup;
struct RmShredDevice;

typedef struct RmFile {
    /* Absolute path of the file
     * */
    char *path;

    /* Pointer to last part of the path
     * */
    char *basename;

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
     * In the case of hardlink clusters, the head of the cluster
     * contains information about the preferred path status of the other
     * files in the cluster
     */
    bool is_prefd;

    /* The index of the path this file belongs to. */
    guint64 path_index;

    /* Filesize in bytes
     */
    guint64 file_size;

    /* How many bytes were already hashed
     * (lower or equal seek_offset)
     */
    guint64 hash_offset;

    /* How many bytes were already read.
     * (lower or equal file_size)
     */
    guint64 seek_offset;

    /* unlock (with flock(2)) the file on destroy? */
    bool unlock_file;

    /* Flag for when we do intermediate steps within a hash increment because the file is fragmented */
    RmFileState status;

    /* digest of this file updated on every hash iteration.  Use a pointer so we can share with RmShredGroup
     */
    RmDigest *digest;

    /* Table of this file's extents.
     */
    RmOffsetTable disk_offsets;

    /* What kind of lint this file is.
     */
    RmLintType lint_type;

    /* If this file is the head of a hardlink cluster, the following structure
     * contains the other hardlinked RmFile's.  This is used to avoid
     * hashing every file within a hardlink set */
    struct {
        GQueue *files;
        bool has_prefd; // use bool, gboolean is actually a gint
        bool has_non_prefd;
    } hardlinks;

    /* Link to the RmShredGroup that the file currently belongs to */
    struct RmShredGroup *shred_group;

    /* Link to the RmShredDevice that the file is associated with */
    struct RmShredDevice *device;
} RmFile;

/**
 * @brief Create a new RmFile handle.
 */
RmFile *rm_file_new(
    bool lock_file, const char *path, RmStat *statp, RmLintType type,
    bool is_ppath, unsigned pnum
);

/**
 * @brief Deallocate the memory allocated by rm_file_new
 */
void rm_file_destroy(RmFile *file);

#endif /* end of include guard */
