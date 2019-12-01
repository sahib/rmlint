/**
* This file is part of rmlint.
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
* Authors:
*
*  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
*  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
*
* Hosted on http://github.com/sahib/rmlint
*
**/

#ifndef RM_FILE_H
#define RM_FILE_H

#include <glib.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "cfg.h"
#include "pathtricia.h"
#include "utilities.h"

typedef enum RmFileState {
    /* File still processing
     */
    RM_FILE_STATE_NORMAL,

    /* File can be ignored, has a unique hash, gets read failure
     * or is elsewhise not noteworthy.
     */
    RM_FILE_STATE_IGNORE,

} RmFileState;

/* types of lint */
typedef enum RmLintType {
    RM_LINT_TYPE_UNKNOWN = 0,
    RM_LINT_TYPE_BADLINK,
    RM_LINT_TYPE_EMPTY_DIR,
    RM_LINT_TYPE_EMPTY_FILE,
    RM_LINT_TYPE_NONSTRIPPED,
    RM_LINT_TYPE_BADUID,
    RM_LINT_TYPE_BADGID,
    RM_LINT_TYPE_BADUGID,

    /* note: this needs to be after all non-duplicate lint type item in list */
    RM_LINT_TYPE_DUPE_CANDIDATE,

    /* Directories are no "normal" RmFiles, they are actual
     * different structs that hide themselves as RmFile to
     * be compatible with the output system.
     *
     * Also they only appear at the very end of processing temporarily.
     * So it does not matter if this type is behind RM_LINT_TYPE_DUPE_CANDIDATE.
     */
    RM_LINT_TYPE_DUPE_DIR_CANDIDATE,

    /* Special type for files that got sieved out during shreddering.
     * if cfg->write_unfinished is true, those may be included in the
     * json/xattr/csv output.
     *
     * This is mainly useful for caching.
     */
    RM_LINT_TYPE_UNIQUE_FILE,

    /* Special type for files that are outputted as part of treemerge.
     * They land still in the json output with this type.
     */
    RM_LINT_TYPE_PART_OF_DIRECTORY,
} RmLintType;

struct RmSession;

typedef guint16 RmPatternBitmask;

/* Get the number of usable fields in a RmPatternBitmask */
#define RM_PATTERN_N_MAX (sizeof(RmPatternBitmask) * 8 / 2)

/* Check if a field has been set already in the RmPatternBitmask */
#define RM_PATTERN_IS_CACHED(mask, idx) (!!((*mask) & (1 << (idx + RM_PATTERN_N_MAX))))

/* If the field was set, this will retrieve the previous result */
#define RM_PATTERN_GET_CACHED(mask, idx) (!!((*mask) & (1 << idx)))

/* Set a field and remember that it was set. */
#define RM_PATTERN_SET_CACHED(mask, idx, match) \
    ((*mask) |= ((!!match << idx) | (1 << (idx + RM_PATTERN_N_MAX))))

struct RmDirectory;

/**
 * RmFile structure; used by pretty much all rmlint modules.
 */
typedef struct RmFile {
    /* file path lookup ID (if using swap table)
        * */
    RmOff path_id;

    /* file folder as node of folder n-ary tree
     * */
    RmNode *folder;

    /* File modification date/time
     * */
    gdouble mtime;

    /* Depth of the file, relative to the path it was found in.
     */
    gint16 depth;

    /* Link count (number of hardlinks + 1) of the file as told by stat()
     * This is used for the 'hH'-sortcriteria.
     */
    gint16 link_count;

    /* Hardlinks to this file *outside* of the paths that rmlint traversed.
     * This is used for the 'oO'-sortcriteria.
     * */
    gint16 outer_link_count;

    /* Depth of the path of this file.
     */
    guint8 path_depth;

    /* The inode and device of this file.
     * Used to filter double paths and hardlinks.
     */
    ino_t inode;
    dev_t dev;
    struct _RmMDSDevice *disk;

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
     * than cfg->min_mtime
     */
    bool is_new : 1;

    /* True if this file, or at least one its path's componennts, is a hidden
     * file. This excludes files above the directory rmlint was started on.
     * This is relevant to --partial-hidden.
     */
    bool is_hidden : 1;

    /* If false rm_file_destroy will not destroy the digest. This is useful
     * for sharing the digest of duplicates in a group.
     */
    bool free_digest : 1;

    /* If true, the file will be request to be pre-cached on the next read */
    bool fadvise_requested : 1;

    /* Set to true if rm_shred_process_file() for hash increment */
    bool shredder_waiting : 1;

    /* Set to true if file belongs to a subvolume-capable filesystem eg btrfs */
    bool is_on_subvol_fs : 1;

    /* Set to true when file was created by rm_file_shallow_copy */
    bool is_shallow_copy : 1;

    /* The pre-matched file cluster that this file belongs to (or NULL) */
    GQueue *cluster;

    /* pointer to hardlinks collection (or NULL); one list shared between hardlink twin
     * set */
    GQueue *hardlinks;

    /* The index of the path this file belongs to. */
    RmOff path_index;

    /* Filesize in bytes; this may be less than actual_file_size,
     * since -q / -Q may limit this number.
     */
    RmOff file_size;

    /* Filesize of a file when it was traversed by rmlint.
     */
    RmOff actual_file_size;

    /* How many bytes were already read.
    * (lower or equal file_size)
    */
    RmOff hash_offset;

    /* Flag for when we do intermediate steps within a hash increment because the file is
     * fragmented */
    RmFileState status;

    /* digest of this file updated on every hash iteration.  Use a pointer so we can share
     * with RmShredGroup
     */
    RmDigest *digest;

    /* digest of this file read from file extended attributes (previously written by
     * rmlint)
     */
    char *ext_cksum;

    /* Those are never used at the same time.
     * disk_offset is used during computation,
     * twin_count during output.
     */
    union {
        /* Count of twins of this file.
         * (i.e. length of group of this file)
         */
        gint64 twin_count;

        /* Disk fiemap / physical offset at start of file (tests mapping subsequent
         * file fragements did not deliver any significant additionl benefit) */
        RmOff disk_offset;
    };

    /* What kind of lint this file is.
     */
    RmLintType lint_type;

    /* Link to the RmShredGroup that the file currently belongs to */
    struct RmShredGroup *shred_group;

    /* Required for rm_file_equal and for RM_DEFINE_PATH */
    const struct RmSession *session;

    struct RmSignal *signal;

    /* Caching bitmasks to ensure each file is only matched once
     * for every GRegex combination.
     * See also preprocess.c for more explanation.
     * */
    RmPatternBitmask pattern_bitmask_path;
    RmPatternBitmask pattern_bitmask_basename;

    /* Parent directory.
     * Only filled if type is RM_LINT_TYPE_PART_OF_DIRECTORY.
     */
    struct RmDirectory *parent_dir;
} RmFile;

/* Defines a path variable containing the file's path */
#define RM_DEFINE_PATH_IF_NEEDED(file, needed)           \
    char file##_path[PATH_MAX];                          \
    if(needed) {                                         \
        rm_file_build_path((RmFile *)file, file##_path); \
    }

/* Fill path always */
#define RM_DEFINE_PATH(file) RM_DEFINE_PATH_IF_NEEDED(file, true)

#define RM_FILE_HARDLINK_HEAD(file) \
    (file->hardlinks ? (RmFile *)file->hardlinks->head->data : NULL)

#define RM_FILE_IS_CLUSTERED(file) (file->cluster && file != file->cluster->head->data)

/* for lint counting, we only count the first hardlink: */
#define RM_FILE_IS_HARDLINK(file) (file->hardlinks && file != RM_FILE_HARDLINK_HEAD(file))

#define RM_FILE_HARDLINK_COUNT(file) ((file->hardlinks) ? file->hardlinks->length - 1 : 0)

#define RM_FILE_HAS_HARDLINKS(file) \
    (file->hardlinks && file == RM_FILE_HARDLINK_HEAD(file))

#define RM_FILE_CLUSTER_HEAD(file) \
    ((file->cluster) ? (RmFile *)file->cluster->head->data : NULL)
#define RM_FILE_INODE_COUNT(file) ((file->cluster) ? file->cluster->length : 1)

#define RM_FILE_HAS_PREFD(file) (!!rm_file_n_prefd(file))
#define RM_FILE_HAS_NPREFD(file) (!!rm_file_n_nprefd(file))

/**
 * @brief Create a new RmFile handle.
 */
RmFile *rm_file_new(struct RmSession *session, const char *path, RmStat *statp,
                    RmLintType type, bool is_ppath, unsigned pnum, short depth);

/**
 * @brief Deallocate the memory allocated by rm_file_new.
 * @note does not deallocate file->digest since this is handled by shredder.c
 */
void rm_file_destroy(RmFile *file);

/**
 * @brief add link to head's hardlinks (create hardlinks queue if necessary)
 */
void rm_file_hardlink_add(RmFile *head, RmFile *link);

/**
 * @brief add guest to host's cluster (create cluster if necessary)
 */
void rm_file_cluster_add(RmFile *host, RmFile *guest);

void rm_file_cluster_remove(RmFile *file);

/**
 * @brief call func(file) for each clustered file in f including f
 * retval: sum of returned values from func()
 */
gint rm_file_foreach(RmFile *f, RmRFunc func, gpointer user_data);

/**
 * @brief count number of files of various type including clustered files
 */
gint rm_file_n_files(RmFile *file);
gint rm_file_n_new(RmFile *file);
gint rm_file_n_prefd(RmFile *file);
gint rm_file_n_nprefd(RmFile *file);

/**
 * @brief Convert RmLintType to a human readable short string.
 */
const char *rm_file_lint_type_to_string(RmLintType type);

/**
 * @brief Convert a string to a RmLintType
 *
 * @param type a string description.
 *
 * @return a valid lint type or RM_LINT_TYPE_UNKNOWN
 */
RmLintType rm_file_string_to_lint_type(const char *type);

/**
 * @brief Set a path to the file. Normally, you should never do this since the
 * path is immutable.
 */
void rm_file_set_path(RmFile *file, char *path);

/**
 * @brief Internal helper function for RM_DEFINE_PATH using folder tree and basename.
 */
void rm_file_build_path(RmFile *file, char *buf);

/**
 * @brief Make a shallow copy of a file.
 *
 * Free with rm_file_destroy, but beware that modifying e.g. the digest will
 * have a effect on the original file.
 */
RmFile *rm_file_shallow_copy(RmFile *file);

/**
 * @brief Compare basenames of two files
 * @retval true if basenames match.
 */
gint rm_file_basenames_cmp(const RmFile *file_a, const RmFile *file_b);

#endif /* end of include guard */
