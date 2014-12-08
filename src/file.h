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

#include "settings.h"
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

    /* note: this needs to be last item in list */
    RM_LINT_TYPE_DUPE_CANDIDATE,

    /* Directories are no "normal" RmFiles, they are actual
     * different structs that hide themselves as RmFile to
     * be compatible with the output system.
     *
     * Also they only appear at the very end of processing temporarily.
     * So it does not matter if this type is behind RM_LINT_TYPE_DUPE_CANDIDATE.
     */
    RM_LINT_TYPE_DUPE_DIR_CANDIDATE
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

    /* True if the file is a symlink 
     * shredder needs to know this, since the metadata might be about the
     * symlink file itself, while open() returns the pointed file.
     * Chaos would break out in this case.
     */ 
    bool is_symlink : 1;

    /* True if this file is in one of the preferred paths,
     * i.e. paths prefixed with // on the commandline.
     * In the case of hardlink clusters, the head of the cluster
     * contains information about the preferred path status of the other
     * files in the cluster
     */
    bool is_prefd : 1;

    /* In the late processing, one file of a group may be set as original file.
     * With this flag we indicate this.
     */
    bool is_original : 1;

    /* True if this file, or at least one of its embedded hardlinks, are newer
     * than settings->min_mtime
     */
    bool is_new_or_has_new : 1;

    /* If false rm_file_destroy will not destroy the digest. This is useful
     * for sharing the digest of duplicates in a group.
     */
    bool free_digest : 1;

    /* If this file is the head of a hardlink cluster, the following structure
     * contains the other hardlinked RmFile's.  This is used to avoid
     * hashing every file within a hardlink set */
    struct {
        bool has_prefd : 1;
        bool has_non_prefd : 1;
        GQueue *files;
    } hardlinks;

    /* The index of the path this file belongs to. */
    RmOff path_index;

    /* Filesize in bytes
     */
    RmOff file_size;

    /* How many bytes were already hashed
     * (lower or equal seek_offset)
     */
    RmOff hash_offset;

    /* How many bytes were already read.
     * (lower or equal file_size)
     */
    RmOff seek_offset;

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

    /* Link to the RmShredGroup that the file currently belongs to */
    struct RmShredGroup *shred_group;

    /* Link to the RmShredDevice that the file is associated with */
    struct RmShredDevice *device;

    /* Required for rm_file_equal for building initial match_table */
    struct RmSettings *settings;
} RmFile;

/**
 * @brief Create a new RmFile handle.
 */
RmFile *rm_file_new(
    RmSettings *settings, const char *path, RmStat *statp, RmLintType type,
    bool is_ppath, unsigned pnum
);

/**
 * @brief Deallocate the memory allocated by rm_file_new
 */
void rm_file_destroy(RmFile *file);

/**
 * @brief Convert RmLintType to a human readable short string.
 */
const char *rm_file_lint_type_to_string(RmLintType type);

#endif /* end of include guard */
