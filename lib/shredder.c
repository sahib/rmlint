/*
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
 * Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/uio.h>

#include "checksum.h"
#include "hasher.h"

#include "formats.h"
#include "preprocess.h"
#include "utilities.h"

#include "md-scheduler.h"
#include "shredder.h"
#include "xattr.h"

/* Enable extra debug messages? */
#define _RM_SHRED_DEBUG 0

/* This is the engine of rmlint for file duplicate matching.
 *
 * Files are compared in progressive "generations" to identify matching
 * clusters termed "ShredGroup"s:
 * Generation 0: Same size files
 * Generation 1: Same size and same hash of first  ~16kB
 * Generation 2: Same size and same hash of first  ~50MB
 * Generation 3: Same size and same hash of first ~100MB
 * Generation 3: Same size and same hash of first ~150MB
 * ... and so on until the end of the file is reached.
 *
 * The default step size can be configured below.
 *
 *
 * The clusters and generations look something like this:
 *
 *+-------------------------------------------------------------------------+
 *|     Initial list after filtering and preprocessing                      |
 *+-------------------------------------------------------------------------+
 *          | same size                   | same size           | same size
 *   +------------------+           +------------------+    +----------------+
 *   |   ShredGroup 1   |           |   ShredGroup 2   |    |   ShredGroup 3 |
 *   |F1,F2,F3,F4,F5,F6 |           |F7,F8,F9,F10,F11  |    |   F12,F13      |
 *   +------------------+           +------------------+    +----------------+
 *       |            |                 |            |
 *  +------------+ +----------+     +------------+  +---------+  +----+ +----+
 *  | Child 1.1  | |Child 1.2 |     | Child 2.1  |  |Child 2.2|  |3.1 | |3.2 |
 *  | F1,F3,F6   | |F2,F4,F5  |     |F7,F8,F9,F10|  |  F11    |  |F12 | |F13 |
 *  |(hash=hash1 | |(hash=h2) |     |(hash=h3)   |  |(hash=h4)|  |(h5)| |(h6)|
 *  +------------+ +----------+     +------------+  +---------+  +----+ +----+
 *       |            |                |        |              \       \
 *   +----------+ +-----------+  +-----------+ +-----------+    free!   free!
 *   |Child1.1.1| |Child 1.2.1|  |Child 2.2.1| |Child 2.2.2|
 *   |F1,F3,F6  | |F2,F4,F5   |  |F7,F9,F10  | |   F8      |
 *   +----------+ +-----------+  +-----------+ +-----------+
 *               \             \              \             \
 *                rm!           rm!            rm!           free!
 *
 *
 * The basic workflow is:
 * 1. One worker thread is established for each physical device
 * 2. The device thread picks a file from its queue, reads the next increment of that
 *    file, and sends it to a hashing thread.
 * 3. Depending on some logic ("shredder_waiting"), the device thread may wait for the
 *    file increment to finish hashing, or may move straight on to the next file in
 *    the queue.  The "shredder_waiting" logic aims to reduce disk seeks on rotational
 *    devices.
 * 4. The hashed fragment result is "sifted" into a child RmShredGroup of its parent
 *    group, and unlinked it from its parent.
 * 5. (a) If the child RmShredGroup needs hashing (ie >= 2 files and not completely hashed
 *    yet) then the file is pushed back to the device queue for further hashing;
 *    (b) If the file is not completely hashed but is the only file in the group (or
 *    otherwise fails criteria such as --must-match-tagged) then it is retained by the
 *    child RmShredGroup until a suitable sibling arrives, whereupon it is released to
 *    the device queue.
 *    (c) If the file has finished hashing, it is retained by the child RmShredGroup
 *    until its parent and all ancestors have finished processing, whereupon the file
 *    is sent to the "result factory" (if >= 2 files in the group) or discarded.
 *
 * In the above example, the hashing order will depend on the "shredder_waiting" logic.
 *    On a rotational device the hashing order should end up being something like:
 *         F1.1 F2.1 (F3.1,F3.2), (F4.1,F4.2), (F5.1,F5.2,F5.3)...
 *                        ^            ^            ^    ^
 *        (^ indicates where hashing could continue on to a second increment (avoiding a
 *           disk seek) because there was already a matching file after the first
 *           increment)
 *
 *    On a non-rotational device where there is no seek penalty, the hashing order is:
 *         F1.1 F2.1 F3.1 F4.1 F5.1...
 *
 *
 * The threading looks somewhat like this for two devices:
 *
 *                          +----------+
 *                          |  Result  |
 *                          |  Factory |
 *                          |  Pipe    |
 *                          +----------+
 *                                ^
 *                                |
 *                        +--------------+
 *                        | Matched      |
 *                        | fully-hashed |
 *                        | dupe groups  |
 *    Device #1           +--------------+      Device #2
 *                                ^
 * +-------------------+          |          +-------------------+
 * | RmShredDevice     |          |          | RmShredDevice     |
 * | Worker            |          |          | Worker            |
 * | +-------------+   |          |          | +-------------+   |
 * | | File Queue  |<--+----+     |     +----+>| File Queue  |   |
 * | +-------------+   |    |     |     |    | +-------------+   |
 * | pop from          |    |     |     |    |        pop from   |
 * |  queue            |    |     |     |    |         queue     |
 * |     |             |    |     |     |    |            |      |
 * |     |<--Continue  |    |     |     |    | Continue-->|      |
 * |     |     ^       |    |     |     |    |      ^     |      |
 * |     v     |       |    |     |     |    |      |     v      |
 * |   Read    |       |    |     |     |    |      |    Read    |
 * |     |     |       |    |     |     |    |      |     |      |
 * |     |     |       |    |     |     |    |      |     |      |
 * |     |     |       |  Device  |  Device  |      |     |      |
 * |    [1]    |       |   Not    |    Not   |      |    [1]     |
 * +-----|-----+-------+ Waiting  |  Waiting +------|-----|------+
 *       |     |            |     |     |           |     |
 *       |     |            |     |     |           |     |
 *       |  Device  +-------+-----+-----+------+  Device  |
 *       | Waiting  |         Sifting          | Waiting  |
 *       |     |    |  (Identifies which       |    |     |
 *       |     -----+  partially-hashed files  +----+     |
 *       |          |  qualify for further     |          |
 *       |     +--->|  hashing)                |<--+      |
 *       |     |    |                          |   |      |
 *       |     |    +--------------------------+   |      |
 *       |     |         ^            |            |      |
 *       |     |         |            v            |      |
 *       |     |  +----------+   +----------+      |      |
 *       |     |  |Initial   |   | Rejects  |      |      |
 *       |     |  |File List |   |          |      |      |
 *       |     |  +----------+   +----------+      |      |
 *       |     |                                   |      |
 *  +----+-----+-----------------------------------+------+----+
 *  |    v     |        Hashing Pool               |      v    |
 *  |  +----------+                              +----------+  |
 *  |  |Hash Pipe |                              |Hash Pipe |  |
 *  |  +----------+                              +----------+  |
 *  +----------------------------------------------------------+
 *
 *  Note [1] - at this point the read results are sent to the hashpipe
 *             and the Device must decide if it is worth waiting for
 *             the hashing/sifting result; if not then the device thread
 *             will immediately pop the next file from its queue.
 *
 *
 *
 * Every subbox left and right are the task that are performed.
 *
 * The Device Workers, Hash Pipes and Finisher Pipe run as separate threads
 * managed by GThreadPool.  Note that while they are implemented as
 * GThreadPools, the hashers and finisher are limited to 1 thread eash
 * hence the term "pipe" is more appropriate than "pool".  This is
 * particularly important for hashing because hash functions are generally
 * order-dependent, ie hash(ab) != hash(ba); the only way to ensure hashing
 * tasks are complete in correct sequence is to use a single pipe.
 *
 * The Device Workers work sequentially through the queue of hashing
 * jobs; if the device is rotational then the files are sorted in order of
 * disk offset in order to reduce seek times.
 *
 * The Devlist Manager calls the hasher library (see hasher.c) to read one
 * file at a time.  The hasher library takes care of read buffers, hash
 * pipe allocation, etc.  Once the hasher is done, the result is sent back
 * via callback to rm_shred_hash_callback.
 *
 * If "shredder_waiting" has been flagged then the callback sends the file
 * back to the Device Worker thread via a GAsyncQueue, whereupon the Device
 * Manager does a quick check to see if it can continue with the same file;
 * if not then a new file is taken from the device queue.
 *
 * The RmShredGroups don't have a thread managing them, instead the individual
 * Device Workers and/or hash pipe callbacks write to the RmShredGroups
 * under mutex protection.
 *
 *
 * The main ("foreground") thread waits for the Devlist Managers to
 * finish their sequential walk through the files.  If there are still
 * files to process on the device, the initial thread sends them back to
 * the GThreadPool for another pass through the files.
 *
 *
 *
 * Additional notes regarding "paranoid" hashing:
 *
 * The default file matching method uses the BLAKE2B cryptographic hash; there are
 * several other hash functions available as well. The data hashing is somewhat
 * cpu-intensive but this is handled by separate threads (the hash pipes) so
 * generally doesn't bottleneck rmlint (as long as CPU exceeds disk reading
 * speed).  The subsequent hash matching is very fast because we only
 * need to compare 32 bytes (in the case of BLAKE2B) to find matching files.
 *
 * The "paranoid" method uses byte-by-byte comparison.  In the implementation,
 * this is masqueraded as a hash function, but there is no hashing involved.
 * Instead, the whole data increment is kept in memory.  This introduces 2 new
 * challenges:
 *
 * (1) Memory management.  In order to avoid overflowing mem availability, we
 * limit the number of concurrent active RmShredGroups and also limit the size
 * of each file increment.
 *
 * (2) Matching time.  Unlike the conventional hashing strategy (CPU-intensive
 * hashing followed by simple matching), the paranoid method requires
 * almost no CPU during reading/hashing, but requires a large memcmp() at the
 * end to find matching files/groups.
 *
 * That would not be a bottleneck as long as the reader thread still has other
 * files that it can go and read while the hasher/sorter does the memcmp in
 * parallel... but unfortunately the memory management issue means that's not
 * always an option and so reading gets delayed while waiting for the memcmp()
 * to catch up.  Two strategies are used to speed this up:
 *
 * (a) Pre-matching of candidate digests.  During reading/hashing, as each
 * buffer (4096 bytes) is read in, it can be checked against a "twin candidate".
 * We can send twin candidates to the hash pipe at any time via
 * rm_digest_send_match_candidate().  If the correct twin candidate has been
 * sent, then when the increment is finished the matching has already been done,
 * and rm_digest_equal() is almost instantaneous.
 *
 * (b) Shadow hash.  A lightweight hash (Murmor) is calculated and used for
 * hashtable lookup to quickly identify potential matches.  This saves time in
 * the case of RmShredGroups with large number of child groups and where the
 * pre-matching strategy failed.
 * */

/*
* Below some performance controls are listed that may impact performance.
* Controls are sorted by subjectve importanceness.
*/

////////////////////////////////////////////
// OPTIMISATION PARAMETERS FOR DECIDING   //
// HOW MANY BYTES TO READ BEFORE STOPPING //
// TO COMPARE PROGRESSIVE HASHES          //
////////////////////////////////////////////

/* how many pages can we read in (seek_time)/(CHEAP)? (use for initial read) */
#define SHRED_BALANCED_PAGES (4)

/* How large a single page is (typically 4096 bytes but not always)*/
#define SHRED_PAGE_SIZE (sysconf(_SC_PAGESIZE))

#define SHRED_MAX_READ_FACTOR \
    ((256 * 1024 * 1024) / SHRED_BALANCED_PAGES / SHRED_PAGE_SIZE)

/* Maximum increment size for paranoid digests.  This is smaller than for other
 * digest types due to memory management issues.
 * 16MB should be big enough buffer size to make seek time fairly insignificant
 * relative to sequential read time, eg 16MB read at typical 100 MB/s read
 * rate = 160ms read vs typical seek time 10ms*/
#define SHRED_PARANOID_BYTES (16 * 1024 * 1024)

/* When paranoid hashing, if a file increments is larger
 * than SHRED_PREMATCH_THRESHOLD, we take a guess at the likely
 * matching file and do a progressive memcmp() on each buffer
 * rather than waiting until the whole increment has been read
 * */
#define SHRED_PREMATCH_THRESHOLD (0)

/* empirical estimate of mem usage per file (excluding read buffers and
 * paranoid digests) */
#define SHRED_AVERAGE_MEM_PER_FILE (100)

/* Maximum number of bytes before worth_waiting becomes false */
#define SHRED_TOO_MANY_BYTES_TO_WAIT (64 * 1024 * 1024)

///////////////////////////////////////////////////////////////////////
//    INTERNAL STRUCTURES, WITH THEIR INITIALISERS AND DESTROYERS    //
///////////////////////////////////////////////////////////////////////

/////////* The main extra data for the duplicate finder *///////////

typedef struct RmShredTag {
    RmSession *session;
    GMutex hash_mem_mtx;
    gint64 paranoid_mem_alloc; /* how much memory to allocate for paranoid checks */
    gint32 active_groups; /* how many shred groups active (only used with paranoid) */
    RmHasher *hasher;
    GThreadPool *result_pool;
    /* threadpool for progress counters to avoid blocking delays in
     * rm_shred_adjust_counters */
    gint32 page_size;
    bool mem_refusing;

    GMutex lock;

    gint32 remaining_files;
    gint64 remaining_bytes;

    bool after_preprocess : 1;

} RmShredTag;

#define NEEDS_PREF(group) \
    (group->session->cfg->must_match_tagged || group->session->cfg->keep_all_untagged)
#define NEEDS_NPREF(group) \
    (group->session->cfg->must_match_untagged || group->session->cfg->keep_all_tagged)
#define NEEDS_NEW(group) \
    (group->session->cfg->min_mtime)

/* There does not seem to be an performance advance here,
 * but for paranoid mode it's useful to have a checksum in the json output.
 * */
#define NEEDS_SHADOW_HASH(cfg)                                         \
    (TRUE || cfg->merge_directories || cfg->read_cksum_from_xattr)

typedef struct RmShredGroup {
    /* holding queue for files; they are held here until the group first meets
     * criteria for further hashing (normally just 2 or more files, but sometimes
     * related to preferred path counts)
     * */
    GQueue *held_files;

    /* link(s) to next generation of RmShredGroups(s) which have this RmShredGroup as
     * parent*/
    GHashTable *children;

    /* RmShredGroup of the same size files but with lower RmFile->hash_offset;
     * getsset to null when parent dies
     * */
    struct RmShredGroup *parent;

    /* total number of files that have passed through this group (including
     * bundled hardlinked files and ext_cksum twins) */
    gsize num_files;

    /* number of file clusters */
    gsize n_clusters;

    /* number of distinct inodes */
    gsize n_inodes;

    /* number of pending digests (ignores clustered files)*/
    gsize num_pending;

    /* list of in-progress paranoid digests, used for pre-matching */
    GList *in_progress_digests;

    /* number of files from "preferred" paths */
    gsize n_pref;

    /* number of files from "non-preferred" paths */
    gsize n_npref;

    /* number of files newer than cfg->min_mtime */
    gsize n_new;

    /* set if group has been greenlighted by paranoid mem manager */
    bool is_active : 1;

    /* if whole group has same basename, pointer to first file, else null */
    RmFile *unique_basename;

    /* initially RM_SHRED_GROUP_DORMANT; triggered as soon as we have >= 2 files
     * and meet preferred path and will go to either RM_SHRED_GROUP_HASHING or
     * RM_SHRED_GROUP_FINISHING.  When switching from dormant to hashing, all
     * held_files are released and future arrivals go straight to hashing
     * */
    RmShredGroupStatus status;

    /* file size of files in this group */
    RmOff file_size;

    /* file hash_offset when files arrived in this group */
    RmOff hash_offset;

    /* file hash_offset for next increment */
    RmOff next_offset;

    /* Factor of SHRED_BALANCED_PAGES to read next time */
    gint64 offset_factor;

    /* allocated memory for paranoid hashing */
    RmOff mem_allocation;

    /* checksum structure taken from first file to enter the group.  This allows
     * digests to be released from RmFiles and memory freed up until they
     * are required again for further hashing.*/
    RmDigestType digest_type;
    RmDigest *digest;

    /* lock for access to this RmShredGroup */
    GMutex lock;

    /* Reference to main */
    const RmSession *session;
} RmShredGroup;

typedef struct RmSignal {
    GMutex lock;
    GCond cond;
    gboolean done;
} RmSignal;

static RmSignal *rm_signal_new(void) {
    RmSignal *self = g_slice_new(RmSignal);
    g_mutex_init(&self->lock);
    g_cond_init(&self->cond);
    self->done = FALSE;
    return self;
}

static void rm_signal_wait(RmSignal *signal) {
    g_mutex_lock(&signal->lock);
    {
        while(!signal->done) {
            g_cond_wait(&signal->cond, &signal->lock);
        }
    }
    g_mutex_unlock(&signal->lock);
    g_mutex_clear(&signal->lock);
    g_cond_clear(&signal->cond);
    g_slice_free(RmSignal, signal);
}

static void rm_signal_done(RmSignal *signal) {
    g_mutex_lock(&signal->lock);
    {
        signal->done = TRUE;
        g_cond_signal(&signal->cond);
    }
    g_mutex_unlock(&signal->lock);
}

/////////// RmShredGroup ////////////////

/* allocate and initialise new RmShredGroup; uses file's digest type if available */
static RmShredGroup *rm_shred_group_new(RmFile *file) {
    RmShredGroup *self = g_slice_new0(RmShredGroup);

    if(file->digest) {
        self->digest_type = file->digest->type;
        self->digest = file->digest;
        file->digest = NULL;
    }

    self->parent = file->shred_group;
    self->session = file->session;

    if(self->parent) {
        self->offset_factor = MIN(self->parent->offset_factor * 8, SHRED_MAX_READ_FACTOR);
    } else {
        self->offset_factor = 1;
    }

    self->held_files = g_queue_new();
    self->file_size = file->file_size;
    self->hash_offset = file->hash_offset;

    self->session = file->session;

    g_mutex_init(&self->lock);

    return self;
}

//////////////////////////////////
// OPTIMISATION AND MEMORY      //
// MANAGEMENT ALGORITHMS        //
//////////////////////////////////

/* Compute optimal size for next hash increment call this with group locked */
static gint32 rm_shred_get_read_size(RmFile *file, RmShredTag *tag) {
    RmShredGroup *group = file->shred_group;
    rm_assert_gentle(group);

    gint32 result = 0;

    /* calculate next_offset property of the RmShredGroup */
    RmOff balanced_bytes = tag->page_size * SHRED_BALANCED_PAGES;
    RmOff target_bytes = balanced_bytes * group->offset_factor;
    if(group->next_offset == 2) {
        file->fadvise_requested = 1;
    }

    /* round to even number of pages, round up to MIN_READ_PAGES */
    RmOff target_pages = MAX(target_bytes / tag->page_size, 1);
    target_bytes = target_pages * tag->page_size;

    /* test if cost-effective to read the whole file */
    if(group->hash_offset + target_bytes + (balanced_bytes) >= group->file_size ||
        tag->session->cfg->hash) {
        group->next_offset = group->file_size;
        file->fadvise_requested = 1;
    } else {
        group->next_offset = group->hash_offset + target_bytes;
    }

    /* for paranoid digests, make sure next read is not > max size of paranoid buffer */
    if(group->digest_type == RM_DIGEST_PARANOID) {
        group->next_offset =
            MIN(group->next_offset, group->hash_offset + SHRED_PARANOID_BYTES);
    }

    file->status = RM_FILE_STATE_NORMAL;
    result = (group->next_offset - file->hash_offset);

    return result;
}

/* Memory manager (only used for RM_DIGEST_PARANOID at the moment
 * but could also be adapted for other digests if very large
 * filesystems are contemplated)
 */

static void rm_shred_mem_return(RmShredGroup *group) {
    if(group->is_active) {
        RmShredTag *tag = group->session->shredder;
        g_mutex_lock(&tag->hash_mem_mtx);
        {
            tag->paranoid_mem_alloc += group->mem_allocation;
            tag->active_groups--;
            group->is_active = FALSE;
#if _RM_SHRED_DEBUG
            rm_log_debug_line("Mem avail %" LLI ", active groups %d. " YELLOW
                              "Returned %" LLU " bytes for paranoid hashing.",
                              tag->paranoid_mem_alloc,
                              tag->active_groups,
                              group->mem_allocation);
#endif
            tag->mem_refusing = FALSE;
            if(group->digest) {
                rm_assert_gentle(group->digest->type == RM_DIGEST_PARANOID);
                rm_digest_free(group->digest);
                group->digest = NULL;
            }
        }
        g_mutex_unlock(&tag->hash_mem_mtx);
        group->mem_allocation = 0;
    }
}

/* what is the maximum number of files that a group may end up with (including
 * parent, grandparent etc group files that haven't been hashed yet)?
 */
static gulong rm_shred_group_potential_file_count(RmShredGroup *group) {
    if(group) {
        return group->num_pending + rm_shred_group_potential_file_count(group->parent);
    } else {
        return 0;
    }
}

/* Governer to limit memory usage by limiting how many RmShredGroups can be
 * active at any one time
 * NOTE: group_lock must be held before calling rm_shred_check_paranoid_mem_alloc
 */
static bool rm_shred_check_paranoid_mem_alloc(RmShredGroup *group,
                                              int active_group_threshold) {
    if(group->status >= RM_SHRED_GROUP_HASHING) {
        /* group already committed */
        return true;
    }

    gint64 mem_required =
        (rm_shred_group_potential_file_count(group) / 2 + 1) *
        MIN(group->file_size - group->hash_offset, SHRED_PARANOID_BYTES);

    bool result = FALSE;
    RmShredTag *tag = group->session->shredder;
    g_mutex_lock(&tag->hash_mem_mtx);
    {
        gint64 inherited = group->parent ? group->parent->mem_allocation : 0;

        if(mem_required <= tag->paranoid_mem_alloc + inherited ||
           (tag->active_groups <= active_group_threshold)) {
            /* ok to proceed */
            /* only take what we need from parent */
            inherited = MIN(inherited, mem_required);
            if(inherited > 0) {
                group->parent->mem_allocation -= inherited;
                group->mem_allocation += inherited;
            }

            /* take the rest from bank */
            gint64 borrowed =
                MIN(mem_required - inherited, (gint64)tag->paranoid_mem_alloc);
            tag->paranoid_mem_alloc -= borrowed;
            group->mem_allocation += borrowed;

            if(tag->mem_refusing) {
                rm_log_debug_line("Mem avail %" LLI ", active groups %d. Borrowed %" LLI
                                  ". Inherited: %" LLI " bytes for paranoid hashing",
                                  tag->paranoid_mem_alloc, tag->active_groups, borrowed,
                                  inherited);

                if(mem_required > borrowed + inherited) {
                    rm_log_debug_line("...due to %i active group limit",
                                      active_group_threshold);
                }

                tag->mem_refusing = FALSE;
            }

            tag->active_groups++;
            group->is_active = TRUE;
            group->status = RM_SHRED_GROUP_HASHING;
            result = TRUE;
        } else {
            if(!tag->mem_refusing) {
                rm_log_debug_line(
                    "Mem avail %" LLI ", active groups %d. " RED
                    "Refused request for %" LLU " bytes for paranoid hashing.",
                    tag->paranoid_mem_alloc, tag->active_groups, mem_required);
                tag->mem_refusing = TRUE;
            }
            result = FALSE;
        }
    }
    g_mutex_unlock(&tag->hash_mem_mtx);

    return result;
}

///////////////////////////////////
//       Progress Reporting      //
///////////////////////////////////


static void rm_shred_adjust_counters(RmShredTag *tag, int files, gint64 bytes) {
    RmSession *session = tag->session;

    rm_counter_add(RM_COUNTER_SHRED_FILES_REMAINING, files);
    gint64 bytes_remaining =
        rm_counter_add_and_get(RM_COUNTER_SHRED_BYTES_REMAINING, bytes);

    if(files < 0) {
        rm_counter_add(RM_COUNTER_TOTAL_FILTERED_FILES, files);
    }

    rm_fmt_set_state(session->cfg->formats, (tag->after_preprocess)
                                           ? RM_PROGRESS_STATE_SHREDDER
                                           : RM_PROGRESS_STATE_PREPROCESS);

    /* fake interrupt option for debugging/testing: */
    if(tag->after_preprocess && session->cfg->fake_abort &&
       bytes_remaining * 10 < rm_counter_get_unlocked(RM_COUNTER_SHRED_BYTES_TOTAL) * 9) {
        rm_session_abort();
        /* prevent multiple aborts */
        rm_counter_set(RM_COUNTER_SHRED_BYTES_TOTAL, 0);
    }
}

static void rm_shred_write_cksum_to_xattr(const RmSession *session, RmFile *file) {
    if(session->cfg->write_cksum_to_xattr) {
        if(!file->ext_cksum) {
            rm_xattr_write_hash(file, (RmSession *)session);
        }
    }
}

/* Unlink RmFile from Shredder
 */
static void rm_shred_discard_file(RmFile *file, bool free_file) {
    const RmSession *session = file->session;
    RmShredTag *tag = session->shredder;

    /* update device counters (unless this file was a bundled hardlink) */
    if(file->disk) {
        rm_mds_device_ref(file->disk, -1);
        file->disk = NULL;
        rm_shred_adjust_counters(tag, -1, -(gint64)(file->file_size - file->hash_offset));
    }

    if(free_file) {
        /* toss the file (and any embedded hardlinks)*/
        rm_file_destroy(file);
    }
}

/* Push file to scheduler queue.
 * */
static void rm_shred_push_queue(RmFile *file) {
    if(file->hash_offset == 0) {
        /* first-timer; lookup disk offset */
        if(file->session->cfg->build_fiemap &&
           !rm_mounts_is_nonrotational(file->session->mounts, file->dev)) {
            RM_DEFINE_PATH(file);
            file->disk_offset = rm_offset_get_from_path(file_path, 0, NULL);
        } else {
            /* use inode number instead of disk offset */
            file->disk_offset = file->inode;
        }
    }
    rm_mds_push_task(file->disk, file->dev, file->disk_offset, NULL, file);
}

//////////////////////////////////
//    RMSHREDGROUP UTILITIES    //
//    AND SIFTING ALGORITHM     //
//////////////////////////////////

/* Free RmShredGroup and any dormant files still in its queue
 */
static void rm_shred_group_free(RmShredGroup *self, bool force_free) {
    rm_assert_gentle(self->parent == NULL); /* children should outlive their parents! */
    rm_assert_gentle(self->num_pending == 0);

    RmCfg *cfg = self->session->cfg;

    bool needs_free = !(cfg->cache_file_structs) || force_free;

    /* May not free though when unfinished checksums are written.
     * Those are freed by the output module.
     */
    if(cfg->write_unfinished) {
        needs_free = false;
    }

    if(self->held_files) {
        g_queue_foreach(self->held_files, (GFunc)rm_shred_discard_file,
                        GUINT_TO_POINTER(needs_free));
        g_queue_free(self->held_files);
        self->held_files = NULL;
    }

    if(self->digest && needs_free) {
        rm_digest_free(self->digest);
        self->digest = NULL;
    }

    if(self->children) {
        /* note: calls GDestroyNotify function rm_shred_group_make_orphan()
         * for each RmShredGroup member of self->children: */
        g_hash_table_unref(self->children);
    }

    rm_assert_gentle(!self->in_progress_digests);

    g_mutex_clear(&self->lock);

    g_slice_free(RmShredGroup, self);
}

static gboolean rm_shred_group_qualifies(RmShredGroup *group) {
    return group->session->cfg->hash ||
           ((group->num_files >= 2)
            /* it takes 2 to tango */
            &&
            (group->n_pref > 0 || !NEEDS_PREF(group))
            /* we have at least one file from preferred path, or we don't care */
            &&
            (group->n_npref > 0 || !NEEDS_NPREF(group))
            /* we have at least one file from non-pref path, or we don't care */
            &&
            (group->n_new > 0 || !NEEDS_NEW(group))
            /* we have at least one file newer than cfg->min_mtime, or we don't care */
            &&
            (!group->unique_basename || !group->session->cfg->unmatched_basenames)
            /* we have more than one unique basename, or we don't care */
            );
}

/* call unlocked; should be no contention issues since group is finished */
static void rm_shred_group_finalise(RmShredGroup *self) {
    /* return any paranoid mem allocation */
    rm_shred_mem_return(self);

    switch(self->status) {
    case RM_SHRED_GROUP_DORMANT:
        /* Group didn't need hashing, either because it didn't meet criteria,
         * or possible because all files were pre-matched */
        if(rm_shred_group_qualifies(self)) {
            /* upgrade status */
            self->status = RM_SHRED_GROUP_FINISHING;
        }
        rm_util_thread_pool_push(self->session->shredder->result_pool, self);
        break;
    case RM_SHRED_GROUP_START_HASHING:
    case RM_SHRED_GROUP_HASHING:
        /* intermediate increment group no longer required; force free */
        rm_shred_group_free(self, TRUE);
        break;
    case RM_SHRED_GROUP_FINISHING:
        /* free any paranoid buffers held in group->digest (should not be needed for
         * results processing */
        if(self->digest_type == RM_DIGEST_PARANOID) {
            rm_digest_release_buffers(self->digest);
        }
        /* send it to finisher (which takes responsibility for calling
         * rm_shred_group_free())*/
        rm_util_thread_pool_push(self->session->shredder->result_pool, self);
        break;
    case RM_SHRED_GROUP_FINISHED:
    default:
        rm_assert_gentle_not_reached();
    }
}

/* Checks whether group qualifies as duplicate candidate (ie more than
 * two members and meets has_pref and NEEDS_PREF criteria).
 * Assume group already protected by group_lock.
 * */
static void rm_shred_group_update_status(RmShredGroup *group) {
    if(group->status == RM_SHRED_GROUP_DORMANT && rm_shred_group_qualifies(group) &&
       group->hash_offset < group->file_size &&
       (group->n_clusters > 1 ||
        (group->n_inodes == 1 &&
         (group->session->cfg->merge_directories || group->session->cfg->hash)))) {
        /* group can go active */
        group->status = RM_SHRED_GROUP_START_HASHING;
    }
}

/* Only called by rm_shred_group_free (via GDestroyNotify of group->children).
 * Call with group->lock unlocked.
 */
static void rm_shred_group_make_orphan(RmShredGroup *self) {
    gboolean group_finished = FALSE;
    g_mutex_lock(&self->lock);
    {
        self->parent = NULL;
        group_finished = (self->num_pending == 0);
    }
    g_mutex_unlock(&self->lock);

    if(group_finished) {
        rm_shred_group_finalise(self);
    }
}

/* Call with shred_group->lock unlocked. */
static RmFile *rm_shred_group_push_file(RmShredGroup *shred_group, RmFile *file,
                                        gboolean initial) {
    RmFile *result = NULL;
    RmCfg *cfg = shred_group->session->cfg;

    file->shred_group = shred_group;

    if(file->digest) {
        rm_digest_free(file->digest);
        file->digest = NULL;
    }

    g_mutex_lock(&shred_group->lock);
    {
        if(cfg->unmatched_basenames) {
            /* do some fancy footwork for cfg->unmatched_basenames criterion */
            if(shred_group->num_files == 0) {
                shred_group->unique_basename = file;
            } else if(shred_group->unique_basename &&
                      rm_file_basenames_cmp(file, shred_group->unique_basename) != 0) {
                shred_group->unique_basename = NULL;
            }
            if(file->cluster) {
                for(GList *iter = file->cluster->head; iter; iter = iter->next) {
                    if(rm_file_basenames_cmp(iter->data, shred_group->unique_basename) !=
                       0) {
                        shred_group->unique_basename = NULL;
                        break;
                    }
                }
            }
        }

        /* update group counters */
        shred_group->num_files += rm_file_n_files(file);
        shred_group->n_pref += rm_file_n_prefd(file);
        shred_group->n_npref += rm_file_n_nprefd(file);
        shred_group->n_new += rm_file_n_new(file);
        shred_group->n_clusters++;
        shred_group->n_inodes += RM_FILE_INODE_COUNT(file);

        rm_assert_gentle(file->hash_offset == shred_group->hash_offset);

        rm_shred_group_update_status(shred_group);
        switch(shred_group->status) {
        case RM_SHRED_GROUP_START_HASHING:
            /* clear the queue and push all its rmfiles to the md-scheduler */
            if(shred_group->held_files) {
                shred_group->num_pending += g_queue_get_length(shred_group->held_files);
                g_queue_free_full(shred_group->held_files,
                                  (GDestroyNotify)rm_shred_push_queue);
                shred_group->held_files = NULL; /* won't need shred_group queue any more,
                                                   since new arrivals will bypass */
            }
            if(shred_group->digest_type == RM_DIGEST_PARANOID && !initial) {
                rm_shred_check_paranoid_mem_alloc(shred_group, 1);
            }
        /* FALLTHROUGH */
        case RM_SHRED_GROUP_HASHING:
            shred_group->num_pending++;
            if(!file->shredder_waiting) {
                /* add file to device queue */
                rm_shred_push_queue(file);
            } else {
                /* calling routine will handle the file */
                result = file;
            }
            break;
        case RM_SHRED_GROUP_DORMANT:
        case RM_SHRED_GROUP_FINISHING:
            /* add file to held_files */
            g_queue_push_head(shred_group->held_files, file);
            break;
        case RM_SHRED_GROUP_FINISHED:
        default:
            rm_assert_gentle_not_reached();
        }
    }
    g_mutex_unlock(&shred_group->lock);

    return result;
}

/* After partial hashing of RmFile, add it back into the sieve for further
 * hashing if required.  If waiting option is set, then try to return the
 * RmFile to the calling routine so it can continue with the next hashing
 * increment (this bypasses the normal device queue and so avoids an unnecessary
 * file seek operation ) returns true if the file can be immediately be hashed
 * some more.
 * */
static RmFile *rm_shred_sift(RmFile *file) {
    RmFile *result = NULL;
    gboolean current_group_finished = FALSE;

    rm_assert_gentle(file);
    RmShredGroup *current_group = file->shred_group;
    rm_assert_gentle(current_group);

    g_mutex_lock(&current_group->lock);
    {
        current_group->num_pending--;
        if(current_group->in_progress_digests) {
            /* remove this file from current_group's pending digests list */
            current_group->in_progress_digests =
                g_list_remove(current_group->in_progress_digests, file->digest);
        }

        if(file->status == RM_FILE_STATE_IGNORE) {
            /* reading/hashing failed somewhere */
            if(file->digest) {
                rm_digest_free(file->digest);
            }
            rm_shred_discard_file(file, true);

        } else {
            rm_assert_gentle(file->digest);

            /* check is child group hashtable has been created yet */
            if(current_group->children == NULL) {
                current_group->children =
                    g_hash_table_new_full((GHashFunc)rm_digest_hash,
                                          (GEqualFunc)rm_digest_equal,
                                          NULL,
                                          (GDestroyNotify)rm_shred_group_make_orphan);
            }

            /* check if there is already a descendent of current_group which
             * matches snap... if yes then move this file into it; if not then
             * create a new group ... */
            RmShredGroup *child_group =
                g_hash_table_lookup(current_group->children, file->digest);
            if(!child_group) {
                child_group = rm_shred_group_new(file);
                g_hash_table_insert(current_group->children, child_group->digest,
                                    child_group);

                /* signal any pending (paranoid) digests that there is a new match
                 * candidate digest */
                g_list_foreach(current_group->in_progress_digests,
                               (GFunc)rm_digest_send_match_candidate,
                               child_group->digest);
            }
            result = rm_shred_group_push_file(child_group, file, FALSE);
        }

        /* is current shred group needed any longer? */
        current_group_finished =
            !current_group->parent && current_group->num_pending == 0;
    }
    g_mutex_unlock(&current_group->lock);

    if(current_group_finished) {
        rm_shred_group_finalise(current_group);
    }

    return result;
}

/* Hasher callback when file increment hashing is completed.
 * */
static void rm_shred_hash_callback(_UNUSED RmHasher *hasher, RmDigest *digest,
                                   RmShredTag *tag, RmFile *file) {
    if(!file->digest) {
        file->digest = digest;
    }
    rm_assert_gentle(file->digest == digest);
    rm_assert_gentle(file->hash_offset == file->shred_group->next_offset);

    if(file->status != RM_FILE_STATE_IGNORE) {
        /* remember that checksum */
        rm_shred_write_cksum_to_xattr(tag->session, file);
    }

    if(file->shredder_waiting) {
        /* MDS scheduler is waiting for result */
        rm_signal_done(file->signal);
    } else {
        /* handle the file ourselves; MDS scheduler has moved on to the next file */
        rm_shred_sift(file);
    }
}

////////////////////////////////////
//  SHRED-SPECIFIC PREPROCESSING  //
////////////////////////////////////

/* Basically this unloads files from the initial list build (which has
 * hardlinks already grouped).
 * Outline:
 * 1. Use g_hash_table_foreach_remove to send RmFiles from node_table
 *    to size_groups via rm_shred_file_preprocess.
 * 2. Use g_hash_table_foreach_remove to delete all singleton and other
 *    non-qualifying groups from size_groups via rm_shred_group_preprocess.
 * 3. Use g_hash_table_foreach to do the FIEMAP lookup for all remaining
 *    files via rm_shred_device_preprocess.
 * */

/* Called for each file; find appropriate RmShredGroup (ie files with same size) and
 * push the file to it.
 * */
static void rm_shred_file_preprocess(RmFile *file, RmShredGroup **group) {
    /* initial population of RmShredDevice's and first level RmShredGroup's */
    RmSession *session = (RmSession *)file->session;
    RmShredTag *shredder = session->shredder;
    RmCfg *cfg = session->cfg;

    rm_assert_gentle(file);
    rm_assert_gentle(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE);

    /* Create an empty checksum for empty files */
    if(file->file_size == 0) {
        file->digest = rm_digest_new(cfg->checksum_type, 0, 0, 0, NEEDS_SHADOW_HASH(cfg));
    }

    if(!(*group)) {
        /* create RmShredGroup using first file in size group as template*/
        *group = rm_shred_group_new(file);
        (*group)->digest_type = cfg->checksum_type;
    }

    RM_DEFINE_PATH(file);

    /* add reference for this file to the MDS scheduler, and get pointer to its device */
    file->disk = rm_mds_device_get(
        session->mds, file_path,
        (cfg->fake_pathindex_as_disk) ? file->path_index + 1 : file->dev);
    rm_mds_device_ref(file->disk, 1);

    rm_shred_adjust_counters(shredder, 1, (gint64)file->file_size - file->hash_offset);

    rm_shred_group_push_file(*group, file, true);
}

/* if file and prev are external checksum twins then cluster file into prev */
static gint rm_shred_cluster_ext(RmFile *file, RmFile *prev) {
    if(prev && file->ext_cksum && prev->ext_cksum &&
       strcmp(file->ext_cksum, prev->ext_cksum) == 0) {
        /* ext_cksum match: cluster it */
#if _RM_SHRED_DEBUG
        RM_DEFINE_PATH(file);
        RM_DEFINE_PATH(prev);
        rm_log_debug_line("cluster %s <-- %s", prev_path, file_path);
#endif
        rm_file_cluster_add(prev, file);
        return TRUE;
    }

    if(file->hardlinks) {
        /* create cluster based on files own hardlinks (does counting) */
#if _RM_SHRED_DEBUG
        RM_DEFINE_PATH(file);
        rm_log_debug_line("self cluster %s", file_path);
#endif
        rm_file_cluster_add(file, file);
    }

    return FALSE;
}

/* sorting function to sort by external checksums */
static gint rm_shred_cmp_ext_cksum(RmFile *a, RmFile *b) {
    if(!a->ext_cksum && !b->ext_cksum) {
        return 0;
    }

    RETURN_IF_NONZERO(!a->ext_cksum - !b->ext_cksum);

    return strcmp(a->ext_cksum, b->ext_cksum);
}

static void rm_shred_process_group(GSList *files, _UNUSED RmShredTag *main) {
    rm_assert_gentle(files);
    rm_assert_gentle(files->data);

    /* cluster hardlinks and ext_cksum matches;
     * Initially I over-complicated this until I realised that hardlinks
     * share common extended attributes.  So there is no need to
     * contemplate the case of xattr checksums that are "hidden" somewhere
     * in the hardlink cluster.  Instead we only need to check the head
     * hardlink.
     */

    /* sort list so that external checksums are grouped; with large sets
     * this is faster than a triangular search for twins */
    files = g_slist_sort(files, (GCompareFunc)rm_shred_cmp_ext_cksum);

    /* cluster ext_cksum twins */
    gboolean all_have_ext_cksums = TRUE;
    for(GSList *prev = NULL, *iter = files, *next = NULL; iter; iter = next) {
        next = iter->next;
        RmFile *file = iter->data;
        all_have_ext_cksums &= !!file->ext_cksum;
        RmFile *prev_file = prev ? prev->data : NULL;
        if(rm_shred_cluster_ext(file, prev_file)) {
            /* delete iter from GSList */
            g_slist_free1(iter);
            if(prev) {
                prev->next = next;
            } else {
                files = next;
            }
        } else {
            prev = iter;
        }
    }

    /* push files to shred group */
    RmShredGroup *group = NULL;
    RmFile *file = NULL;
    while((file = rm_util_slist_pop(&files, NULL))) {
        rm_shred_file_preprocess(file, &group);
        if(all_have_ext_cksums) {
            /* only one cluster per RmShredGroup */
            rm_shred_group_finalise(group);
            group = NULL;
        }
    }

    /* remove group if it failed to launch (eg if only 1 file) */
    if(group && group->status == RM_SHRED_GROUP_DORMANT) {
        rm_shred_group_finalise(group);
    }
}

static void rm_shred_preprocess_input(RmShredTag *main) {
    RmSession *session = main->session;
    guint removed = 0; /* TODO: fix this, does not count removed files */

    /* move files from node tables into initial RmShredGroups */
    rm_log_debug_line("preparing size groups for shredding (dupe finding)...");
    RmFileTables *tables = session->tables;
    g_slist_foreach(tables->size_groups, (GFunc)rm_shred_process_group, main);
    g_slist_free(tables->size_groups);
    tables->size_groups = NULL;
    rm_log_debug_line("...done at time %.3f; removed %u of %" RM_COUNTER_FORMAT,
                      rm_counter_elapsed_time(), removed,
                      rm_counter_get(RM_COUNTER_TOTAL_FILTERED_FILES));
}

/////////////////////////////////
//       POST PROCESSING       //
/////////////////////////////////

/* post-processing sorting of files by criteria (-S and -[kmKM]) this is
 * slightly different to rm_shred_cmp_orig_criteria in the case of either -K or
 * -M options
 */
int rm_shred_cmp_orig_criteria(RmFile *a, RmFile *b, RmSession *session) {
    RmCfg *cfg = session->cfg;

    /* Make sure to *never* make a symlink to be the original */
    if(a->is_symlink != b->is_symlink) {
        return a->is_symlink - b->is_symlink;
    } else if((a->is_prefd != b->is_prefd) &&
              (cfg->keep_all_untagged || cfg->must_match_untagged)) {
        return (a->is_prefd - b->is_prefd);
    } else {
        int comparasion = rm_pp_cmp_orig_criteria(a, b, session);
        if(comparasion == 0) {
            return b->is_original - a->is_original;
        }

        return comparasion;
    }
}

/* iterate over group to find highest ranked; return it and tag it as original    */
/* also in special cases (eg keep_all_tagged) there may be more than one original,
 * in which case tag them as well
 */
void rm_shred_group_find_original(RmSession *session, GQueue *files,
                                  RmShredGroupStatus status) {
    /* iterate over group, identifying "tagged" originals */
    for(GList *iter = files->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        file->is_original = false;

        if(status == RM_SHRED_GROUP_FINISHING) {
            /* identify "tagged" originals: */
            if(((file->is_prefd) && (session->cfg->keep_all_tagged)) ||
               ((!file->is_prefd) && (session->cfg->keep_all_untagged))) {
                file->is_original = true;

#if _RM_SHRED_DEBUG
                RM_DEFINE_PATH(file);
                rm_log_debug_line("tagging %s as original because %s",
                                  file_path,
                                  ((file->is_prefd) && (session->cfg->keep_all_tagged))
                                      ? "tagged"
                                      : "untagged");
#endif
            }
        } else {
            file->lint_type = RM_LINT_TYPE_UNIQUE_FILE;
            rm_counter_add(RM_COUNTER_UNIQUE_BYTES, file->actual_file_size);
        }
    }

    /* sort the group (order probably changed since initial preprocessing sort) */
    g_queue_sort(files, (GCompareDataFunc)rm_shred_cmp_orig_criteria, session);

    RmFile *headfile = files->head->data;
    if(!headfile->is_original && status == RM_SHRED_GROUP_FINISHING) {
        headfile->is_original = true;

#if _RM_SHRED_DEBUG
        RM_DEFINE_PATH(headfile);
        rm_log_debug_line("tagging %s as original because it is highest ranked",
                          headfile_path);
#endif
    }
}

void rm_shred_forward_to_output(RmSession *session, GQueue *group) {
    rm_assert_gentle(group);
    rm_assert_gentle(group->head);

#if _RM_SHRED_DEBUG
    RmFile *head = group->head->data;
    RM_DEFINE_PATH(head);
    rm_log_debug_line("Forwarding %s's group", head_path);
#endif

    /* Hand it over to the printing module */
    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        rm_fmt_write(file, session->cfg->formats, group->length);
    }
}

/* only called by rm_shred_result_factory() which is single-thread threadpool
 * so no lock required for session->dup_counter or session->total_lint_size */
static void rm_shred_dupe_totals(RmFile *file) {
    if(!file->is_original) {
        rm_counter_add(RM_COUNTER_DUP_COUNTER, 1);
        rm_counter_add(RM_COUNTER_DUPLICATE_BYTES, file->actual_file_size);

        /* Only check file size if it's not a hardlink.  Since deleting
         * hardlinks does not free any space they should not be counted unless
         * all of them would be removed.
         */
        if(!RM_FILE_IS_HARDLINK(file) && file->outer_link_count == 0) {
            rm_counter_add(RM_COUNTER_TOTAL_LINT_SIZE, file->actual_file_size);
        }
    } else {
        rm_counter_add(RM_COUNTER_ORIGINAL_BYTES, file->actual_file_size);
    }
}


static int rm_shred_sort_by_mtime(const RmFile *file_a, const RmFile *file_b,
                                  RmShredTag *tag) {
    if(tag->session->cfg->mtime_window >= 0) {
        return FLOAT_SIGN_DIFF(file_a->mtime, file_b->mtime, MTIME_TOL);
    }

    return 0;
}

static RmShredGroup *rm_shred_create_rejects(RmShredGroup *group, RmFile *file) {
    if(group->digest) {
        file->digest = rm_digest_copy(group->digest);
    }
    RmShredGroup *rejects = rm_shred_group_new(file);
    rejects->status = group->status;
    rejects->parent = group->parent;
    return rejects;
}

static void rm_shred_group_transfer(RmFile *file, RmShredGroup *source,
                                    RmShredGroup *dest) {
    rm_shred_group_push_file(dest, file, FALSE);
    rm_assert_gentle(g_queue_remove(source->held_files, file));
    source->num_files--;
    source->n_pref -= file->is_prefd;
    source->n_npref -= !file->is_prefd;
    source->n_new -= !file->is_new;
}

static RmShredGroup *rm_shred_mtime_rejects(RmShredGroup *group, RmShredTag *tag) {
    RmShredGroup *rejects = NULL;
    gdouble mtime_window = tag->session->cfg->mtime_window;

    if(mtime_window >= 0) {
        g_queue_sort(group->held_files, (GCompareDataFunc)rm_shred_sort_by_mtime, tag);

        for(GList *iter = group->held_files->head, *next = NULL; iter; iter = next) {
            next = iter->next;
            RmFile *curr = iter->data, *next_file = next ? next->data : NULL;
            if(rejects) {
                /* move remaining files into a new group */
                rm_shred_group_transfer(curr, group, rejects);
            } else if(next_file &&
                      next_file->mtime - curr->mtime > mtime_window + MTIME_TOL) {
                /* create new group for rejects */
                rejects = rm_shred_create_rejects(group, next_file);
            }
        }
    }
    return rejects;
}

static RmShredGroup *rm_shred_basename_rejects(RmShredGroup *group, RmShredTag *tag) {
    RmShredGroup *rejects = NULL;
    if(tag->session->cfg->unmatched_basenames &&
       group->status == RM_SHRED_GROUP_FINISHING) {
        /* remove files which match headfile's basename */
        RmFile *headfile = group->held_files->head->data;
        for(GList *iter = group->held_files->head->next, *next = NULL; iter;
            iter = next) {
            next = iter->next;
            RmFile *curr = iter->data;
            if(rm_file_basenames_cmp(curr, headfile) == 0) {
                if(!rejects) {
                    rejects = rm_shred_create_rejects(group, curr);
                }
                rm_shred_group_transfer(curr, group, rejects);
            }
        }
    }
    return rejects;
}


/* post-process a group:
 * decide which file(s) are originals
 * maybe split out mtime rejects (--mtime-window option)
 * maybe split out basename twins (--unmatched-basename option)
 */
static void rm_shred_group_postprocess(RmShredGroup *group, RmShredTag *tag) {
    if(!group) {
        return;
    }
    RmCfg *cfg = tag->session->cfg;

    rm_assert_gentle(group->held_files);

    /* Features like --mtime-window require post processing, i.e. the shred group
     * needs to be split up further by criteria like "mtime difference too high".
     * This is done here.
     * */
    rm_shred_group_find_original(tag->session, group->held_files, group->status);
    rm_shred_group_postprocess(rm_shred_basename_rejects(group, tag), tag);
    rm_shred_group_postprocess(rm_shred_mtime_rejects(group, tag), tag);

    /* re-check whether what is left of the group still meets all criteria */
    group->status = (rm_shred_group_qualifies(group)) ? RM_SHRED_GROUP_FINISHING
                                                      : RM_SHRED_GROUP_DORMANT;

    /* find the original(s) (note this also sorts the group from highest
     * ranked to lowest ranked
     */
    rm_shred_group_find_original(tag->session, group->held_files, group->status);

    /* Update statistics */
    if(group->status == RM_SHRED_GROUP_FINISHING) {
        rm_fmt_lock_state(tag->session->cfg->formats);
        {
            rm_counter_add(RM_COUNTER_DUP_GROUP_COUNTER, 1);
            g_queue_foreach(group->held_files, (GFunc)rm_shred_dupe_totals, NULL);
        }
        rm_fmt_unlock_state(tag->session->cfg->formats);
    }

    gboolean treemerge =
        cfg->merge_directories && group->status == RM_SHRED_GROUP_FINISHING;
    for(GList *iter = group->held_files->head; iter; iter = iter->next) {
        /* link file to its (shared) digest */
        RmFile *file = iter->data;
        file->digest = group->digest;

        /* Cache the files for merging them into directories */
        if(treemerge) {
            rm_tm_feed(tag->session->dir_merger, file);
        }
    }

    if(!treemerge) {
        /* Output them directly, do not merge them first. */
        rm_shred_forward_to_output(tag->session, group->held_files);
    }

    if(group->status == RM_SHRED_GROUP_FINISHING) {
        group->status = RM_SHRED_GROUP_FINISHED;
    }
#if _RM_SHRED_DEBUG
    rm_log_debug_line("Free from rm_shred_group_postprocess");
#endif

    /* Do not force free files here, output module might need do that itself. */
    rm_shred_group_free(group, false);
}

static void rm_shred_result_factory(RmShredGroup *group, RmShredTag *tag) {
    RmCfg *cfg = tag->session->cfg;

    /* maybe create group's digest from external checksums */
    RmFile *headfile = group->held_files->head->data;
    char *cksum = headfile->ext_cksum;
    if(cksum && !group->digest) {
        group->digest = rm_digest_new(RM_DIGEST_EXT, 0, 0, 0, NEEDS_SHADOW_HASH(cfg));
        rm_digest_update(group->digest, (unsigned char *)cksum, strlen(cksum));
    }

    /* Unbundle the hardlinks and clusters of each file to a flattened list of files */
    for(GList *iter = group->held_files->head; iter; iter = iter->next) {
        RmFile *file = iter->data;

        while(file->cluster) {
            /* unbundle ext_cksum twins */
            RmFile *last = file->cluster->tail->data;
            rm_file_cluster_remove(last);
            if(last != file) {
                g_queue_push_tail(group->held_files, last);
            }
        }

        if(RM_FILE_HAS_HARDLINKS(file)) {
            /* if group member has a hardlink cluster attached to it then
             * unbundle the cluster and append it to the queue
             */
            file->outer_link_count = file->link_count - file->hardlinks->length;

            for(GList *link = file->hardlinks->head; link; link = link->next) {
                RmFile *bundled_file = link->data;
                if(bundled_file != file) {
                    bundled_file->outer_link_count = file->outer_link_count;
                    g_queue_push_tail(group->held_files, bundled_file);
                }
            }

        } else if(file->outer_link_count < 0) {
            file->outer_link_count = file->link_count - 1;
        }
    }

    rm_shred_group_postprocess(group, tag);
}

/////////////////////////////////
//    ACTUAL IMPLEMENTATION    //
/////////////////////////////////

static bool rm_shred_reassign_checksum(RmShredTag *main, RmFile *file) {
    RmCfg *cfg = main->session->cfg;
    RmShredGroup *group = file->shred_group;

    if(group->digest_type == RM_DIGEST_PARANOID) {
        /* check if memory allocation is ok */
        if(!rm_shred_check_paranoid_mem_alloc(group, 0)) {
            return false;
        }

        /* get the required target offset into group->next_offset, so that
         * we can make the paranoid RmDigest the right size*/
        g_mutex_lock(&group->lock);
        {
            if(group->next_offset == 0) {
                (void)rm_shred_get_read_size(file, main);
            }
            rm_assert_gentle(group->hash_offset == file->hash_offset);
        }
        g_mutex_unlock(&group->lock);

        file->digest = rm_digest_new(RM_DIGEST_PARANOID, 0, 0, 0, NEEDS_SHADOW_HASH(cfg));

        if((file->is_symlink == false || cfg->see_symlinks == false) &&
           (group->next_offset > file->hash_offset + SHRED_PREMATCH_THRESHOLD)) {
            /* send candidate twin(s) */
            g_mutex_lock(&group->lock);
            {
                if(group->children) {
                    GList *children = g_hash_table_get_values(group->children);
                    while(children) {
                        RmShredGroup *child = children->data;
                        rm_digest_send_match_candidate(file->digest, child->digest);
                        children = g_list_delete_link(children, children);
                    }
                }
                /* store a reference so the shred group knows where to send any future
                 * twin candidate digests */
                group->in_progress_digests =
                    g_list_prepend(group->in_progress_digests, file->digest);
            }
            g_mutex_unlock(&group->lock);
        }
    } else if(group->digest) {
        /* pick up the digest-so-far from the RmShredGroup */
        file->digest = rm_digest_copy(group->digest);
    } else {
        /* this is first generation of RMGroups, so there is no progressive hash yet */
        file->digest = rm_digest_new(cfg->checksum_type,
                                     cfg->hash_seed1,
                                     cfg->hash_seed2,
                                     0,
                                     NEEDS_SHADOW_HASH(cfg));
    }
    return true;
}

/* call with device unlocked */
static bool rm_shred_can_process(RmFile *file, RmShredTag *main) {
    if(file->digest) {
        return TRUE;
    } else {
        return rm_shred_reassign_checksum(main, file);
    }
}

/* Callback for RmMDS
 * Return value of 1 tells md-scheduler that we have processed the file and either
 * disposed of it or pushed it back to the scheduler queue.
 * Return value of 0 tells md-scheduler we can't process the file right now, and
 * have pushed it back to the queue.
 * */
static gint rm_shred_process_file(RmFile *file, RmSession *session) {
    RmShredTag *tag = session->shredder;

    if(rm_session_was_aborted()) {
        file->status = RM_FILE_STATE_IGNORE;
        rm_shred_sift(file);
        return 1;
    }

    gint result = 0;
    RM_DEFINE_PATH(file);

    while(file && rm_shred_can_process(file, tag)) {
        result = 1;
        /* hash the next increment of the file */
        RmCfg *cfg = session->cfg;
        RmOff bytes_to_read = rm_shred_get_read_size(file, tag);

        gboolean shredder_waiting =
            (file->shred_group->next_offset != file->file_size) &&
            (cfg->shred_always_wait ||
             (!cfg->shred_never_wait && rm_mds_device_is_rotational(file->disk) &&
              bytes_to_read < SHRED_TOO_MANY_BYTES_TO_WAIT));

        RmOff bytes_read = 0;
        RmHasherTask *task = rm_hasher_task_new(tag->hasher, file->digest, file);
        if(!rm_hasher_task_hash(task, file_path, file->hash_offset, bytes_to_read,
                                file->is_symlink, &bytes_read)) {
            /* rm_hasher_start_increment failed somewhere */
            file->status = RM_FILE_STATE_IGNORE;
            shredder_waiting = FALSE;
        }

        rm_counter_add(RM_COUNTER_SHRED_BYTES_READ, bytes_read);

        /* Update totals for file, device and session*/
        file->hash_offset += bytes_to_read;
        if(file->is_symlink) {
            rm_shred_adjust_counters(tag, 0, -(gint64)file->file_size);
        } else {
            rm_shred_adjust_counters(tag, 0, -(gint64)bytes_to_read);
        }

        if(shredder_waiting) {
            /* some final checks if it's still worth waiting for the hash result */
            shredder_waiting =
                shredder_waiting &&
                /* no point waiting if we have no siblings */
                file->shred_group->children &&
                /* no point waiting if paranoid digest with no twin candidates */
                (file->digest->type != RM_DIGEST_PARANOID ||
                 file->digest->paranoid->twin_candidate);
        }
        file->signal = shredder_waiting ? rm_signal_new() : NULL;
        file->shredder_waiting = shredder_waiting;

        /* tell the hasher we have finished */
        rm_hasher_task_finish(task);

        if(shredder_waiting) {
            /* wait until the increment has finished hashing; assert that we get the
             * expected file back */
            rm_signal_wait(file->signal);
            file->signal = NULL;
            /* sift file; if returned then continue processing it */
            file = rm_shred_sift(file);
        } else {
            /* rm_shred_hash_callback will take care of the file */
            file = NULL;
        }
    }
    if(file) {
        /* file was not handled by rm_shred_sift so we need to add it back to the queue */
        rm_mds_push_task(file->disk, file->dev, file->disk_offset, NULL, file);
    }
    return result;
}

void rm_shred_run(RmSession *session) {
    rm_assert_gentle(session);
    rm_assert_gentle(session->tables);

    RmCfg *cfg = session->cfg;

    RmShredTag tag;
    tag.active_groups = 0;
    tag.session = session;
    tag.mem_refusing = false;
    session->shredder = &tag;

    tag.page_size = SHRED_PAGE_SIZE;

    tag.after_preprocess = FALSE;

    /* would use g_atomic, but helgrind does not like that */
    g_mutex_init(&tag.hash_mem_mtx);

    g_mutex_init(&tag.lock);

    rm_mds_configure(session->mds,
                     (RmMDSFunc)rm_shred_process_file,
                     session,
                     session->cfg->sweep_count,
                     session->cfg->threads_per_disk,
                     (RmMDSSortFunc)rm_mds_elevator_cmp);

    /* Create a pool for results processing */
    tag.result_pool = rm_util_thread_pool_new((GFunc)rm_shred_result_factory, &tag, 1);

    rm_shred_preprocess_input(&tag);
    rm_log_debug_line("Done shred preprocessing");

    rm_log_debug_line("Byte and file counters up to date");

    tag.after_preprocess = TRUE;
    rm_counter_set(RM_COUNTER_SHRED_BYTES_AFTER_PREPROCESS,
                   rm_counter_get(RM_COUNTER_SHRED_BYTES_REMAINING));

    /* estimate mem used for RmFiles and allocate any leftovers to read buffer and/or
     * paranoid mem */
    RmOff mem_used =
        SHRED_AVERAGE_MEM_PER_FILE * rm_counter_get(RM_COUNTER_SHRED_FILES_REMAINING);
    RmOff read_buffer_mem = MAX(1024 * 1024, (gint64)cfg->total_mem - (gint64)mem_used);

    if(cfg->checksum_type == RM_DIGEST_PARANOID) {
        /* allocate any spare mem for paranoid hashing */
        tag.paranoid_mem_alloc = (gint64)cfg->total_mem - (gint64)mem_used;
        tag.paranoid_mem_alloc = MAX(0, tag.paranoid_mem_alloc);
        rm_log_debug_line("Paranoid Mem: %" LLU, tag.paranoid_mem_alloc);
        /* paranoid memory manager takes care of memory load; */
        read_buffer_mem = 0;
    }
    rm_log_debug_line("Read buffer Mem: %" LLU, read_buffer_mem);

    /* Initialise hasher */
    /* Optimum buffer size based on /usr without dropping caches:
     * SHRED_PAGE_SIZE * 1 => 5.29 seconds
     * SHRED_PAGE_SIZE * 2 => 5.11 seconds
     * SHRED_PAGE_SIZE * 4 => 5.04 seconds
     * SHRED_PAGE_SIZE * 8 => 5.08 seconds
     * With dropped caches:
     * SHRED_PAGE_SIZE * 1 => 45.2 seconds
     * SHRED_PAGE_SIZE * 4 => 45.0 seconds
     * Optimum buffer size using a rotational disk and paranoid hash:
     * SHRED_PAGE_SIZE * 1 => 16.5 seconds
     * SHRED_PAGE_SIZE * 2 => 16.5 seconds
     * SHRED_PAGE_SIZE * 4 => 15.9 seconds
     * SHRED_PAGE_SIZE * 8 => 15.8 seconds */

    tag.hasher = rm_hasher_new(cfg->checksum_type,
                               cfg->threads,
                               cfg->use_buffered_read,
                               SHRED_PAGE_SIZE * 4,
                               read_buffer_mem,
                               (RmHasherCallback)rm_shred_hash_callback,
                               &tag);

    rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_SHREDDER);

    rm_counter_set(RM_COUNTER_SHRED_BYTES_TOTAL,
                   rm_counter_get(RM_COUNTER_SHRED_BYTES_REMAINING));
    rm_mds_start(session->mds);

    /* should complete shred session and then free: */
    rm_mds_free(session->mds, FALSE);
    rm_hasher_free(tag.hasher, TRUE);

    session->shredder_finished = TRUE;
    rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_SHREDDER);

    /* This should not block, or at least only very short. */
    g_thread_pool_free(tag.result_pool, FALSE, TRUE);

    g_mutex_clear(&tag.hash_mem_mtx);
    rm_log_debug_line("Remaining %" LLU " bytes in %" LLU " files",
                      rm_counter_get(RM_COUNTER_SHRED_BYTES_REMAINING),
                      rm_counter_get(RM_COUNTER_SHRED_FILES_REMAINING));
}
