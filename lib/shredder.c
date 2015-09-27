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
 *  - Christopher <sahib> Pahl 2010-2015 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2015 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <sys/uio.h>

#include "checksum.h"
#include "hasher.h"

#include "preprocess.h"
#include "utilities.h"
#include "formats.h"

#include "shredder.h"
#include "xattr.h"
#include "md-scheduler.h"

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
 * 3. Depending on some logic ("worth_waiting"), the device thread may wait for the
 *    file increment to finish hashing, or may move straight on to the next file in
 *    the queue.  The "worth_waiting" logic aims to reduce disk seeks on rotational
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
 * In the above example, the hashing order will depend on the "worth_waiting" logic.
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
 * If "worth_waiting" has been flagged then the callback sends the file
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
 *   The default file matching method uses the SHA1 cryptographic hash; there are
 * several other hash functions available as well.  The data hashing is somewhat
 * cpu-intensive but this is handled by separate threads (the hash pipes) so generally
 * doesn't bottleneck rmlint (as long as CPU exceeds disk reading speed).  The subsequent
 * hash matching is very fast because we only need to compare 20 bytes (in the case of
 * SHA1) to find matching files.
 *   The "paranoid" method uses byte-by-byte comparison.  In the implementation, this is
 * masqueraded as a hash function, but there is no hashing involved.  Instead, the whole
 * data increment is kept in memory.  This introduces 2 new challenges:
 * (1) Memory management.  In order to avoid overflowing mem availability, we limit the
 * number of concurrent active RmShredGroups and also limit the size of each file
 * increment.
 * (2) Matching time.  Unlike the conventional hashing strategy (CPU-intensive hashing
 * followed by simple matching), the paranoid method requires almost no CPU during
 * reading/hashing, but requires a large memcmp() at the end to find matching
 *files/groups.
 * That would not be a bottleneck as long as the reader thread still has other files
 * that it can go and read while the hasher/sorter does the memcmp in parallel... but
 * unfortunately the memory management issue means that's not always an option and so
 * reading gets delayed while waiting for the memcmp() to catch up.
 * Two strategies are used to speed this up:
 * (a) Pre-matching of candidate digests.  During reading/hashing, as each buffer (4096
 * bytes) is read in, it can be checked against a "twin candidate".  We can send twin
 * candidates to the hash pipe at any time via rm_digest_send_match_candidate().  If the
 * correct twin candidate has been sent, then when the increment is finished the
 * matching has already been done, and rm_digest_equal() is almost instantaneous.
 * (b) Shadow hash.  A lightweight hash (Murmor) is calculated and used for hashtable
 * lookup to quickly identify potential matches.  This saves time in the case of
 * RmShredGroups with large number of child groups and where the pre-matching strategy
 * failed.
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

/* Whether to use buffered fread() or direct preadv()
 * The latter is preferred, since it's slightly faster on linux.
 * Other platforms may have different results though or not even have preadv.
 * */
#define SHRED_USE_BUFFERED_READ (0)

/* When paranoid hashing, if a file increments is larger
 * than SHRED_PREMATCH_THRESHOLD, we take a guess at the likely
 * matching file and do a progressive memcmp() on each buffer
 * rather than waiting until the whole increment has been read
 * */
#define SHRED_PREMATCH_THRESHOLD (0)

/* Minimum number of files that should be in an update sent to
 * the statistics counters.
 */
#define SHRED_MIN_FILE_STATS_PACK_SIZE (16)

/* empirical estimate of mem usage per file (excluding read buffers and
 * paranoid digests) */
#define RM_AVERAGE_MEM_PER_FILE (100)

////////////////////////
//  MATHS SHORTCUTS   //
////////////////////////

#define SIGN_DIFF(X, Y) (((X) > (Y)) - ((X) < (Y))) /* handy for comparing unit64's */

///////////////////////////////////////////////////////////////////////
//    INTERNAL STRUCTURES, WITH THEIR INITIALISERS AND DESTROYERS    //
///////////////////////////////////////////////////////////////////////


/////////* The main extra data for the duplicate finder *///////////

typedef struct RmShredTag {
    RmSession *session;
    GAsyncQueue *device_return;
    GMutex hash_mem_mtx;
    gint64 paranoid_mem_alloc; /* how much memory to allocate for paranoid checks */
    gint32 active_groups; /* how many shred groups active (only used with paranoid) */
    RmHasher *hasher;
    GThreadPool *result_pool;
    gint32 page_size;
    bool mem_refusing;

    GMutex lock;

    gint32 remaining_files;
    gint64 remaining_bytes;

    bool after_preprocess : 1;

    /* cached counters to avoid blocking delays in rm_shred_adjust_counters */
    gint cache_file_count;
    gint cache_filtered_count;
    gint64 cache_byte_count;

} RmShredTag;


typedef enum RmShredGroupStatus {
    RM_SHRED_GROUP_DORMANT = 0,
    RM_SHRED_GROUP_START_HASHING,
    RM_SHRED_GROUP_HASHING,
    RM_SHRED_GROUP_FINISHING,
    RM_SHRED_GROUP_FINISHED
} RmShredGroupStatus;

#define NEEDS_PREF(group)                            \
    (group->session->cfg->must_match_tagged || \
     group->session->cfg->keep_all_untagged)
#define NEEDS_NPREF(group)                             \
    (group->session->cfg->must_match_untagged || \
     group->session->cfg->keep_all_tagged)
#define NEEDS_NEW(group) (group->session->cfg->min_mtime)

#define HAS_CACHE(session) \
    (session->cfg->read_cksum_from_xattr || session->cache_list.length)

#define NEEDS_SHADOW_HASH(cfg) \
    (TRUE || cfg->merge_directories || cfg->read_cksum_from_xattr)
/* @sahib - performance is faster with shadow hash, probably due to hash
 * collisions in large RmShredGroups */

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

    /* total number of files that have passed through this group*/
    gulong num_files;

    /* number of pending digests */
    gulong num_pending;

    /* list of in-progress paranoid digests, used for pre-matching */
    GList *in_progress_digests;

    /* set if group has 1 or more files from "preferred" paths */
    bool has_pref : 1;

    /* set if group has 1 or more files from "non-preferred" paths */
    bool has_npref : 1;

    /* set if group has 1 or more files newer than cfg->min_mtime */
    bool has_new : 1;

    /* set if group has been greenlighted by paranoid mem manager */
    bool is_active : 1;

    /* true if all files in the group have an external checksum */
    bool has_only_ext_cksums : 1;

    /* incremented for each file in the group that obtained it's checksum from ext.
     * If all files came from there we do not even need to hash the group.
     */
    gulong num_ext_cksums;


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
    unsigned offset_factor;

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
        while (!signal->done) {
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

/* allocate and initialise new RmShredGroup */
static RmShredGroup *rm_shred_group_new(RmFile *file) {
    RmShredGroup *self = g_slice_new0(RmShredGroup);

    if(file->digest) {
        self->digest_type = file->digest->type;
        self->digest = file->digest;
        file->digest = NULL;
    } else {
        /* initial groups have no checksum */
        g_assert(!file->shred_group);
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

/* Compute optimal size for next hash increment
 * call this with group locked
 * */
static gint32 rm_shred_get_read_size(RmFile *file, RmShredTag *tag) {
    RmShredGroup *group = file->shred_group;
    g_assert(group);

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
    if(group->hash_offset + target_bytes + (balanced_bytes) >= group->file_size) {
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
            rm_log_debug("Mem avail %li, active groups %d. " YELLOW "Returned %" LLU
                         " bytes for paranoid hashing.\n" RESET,
                         tag->paranoid_mem_alloc, tag->active_groups, group->mem_allocation);
            tag->mem_refusing = FALSE;
            if(group->digest) {
                g_assert(group->digest->type == RM_DIGEST_PARANOID);
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

        if(0 || mem_required <= tag->paranoid_mem_alloc + inherited ||
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

            if (tag->mem_refusing) {
                rm_log_debug("Mem avail %li, active groups %d." GREEN " Borrowed %li",
                             tag->paranoid_mem_alloc, tag->active_groups, borrowed);
                if(inherited > 0) {
                    rm_log_debug("and inherited %li", inherited);
                }
                rm_log_debug(" bytes for paranoid hashing");
                if(mem_required > borrowed + inherited) {
                    rm_log_debug(" due to %i active group limit", active_group_threshold);
                }
                rm_log_debug("\n" RESET);
                tag->mem_refusing = FALSE;
            }

            tag->active_groups++;
            group->is_active = TRUE;
            group->status = RM_SHRED_GROUP_HASHING;
            result = TRUE;
        } else {
            if(!tag->mem_refusing) {
                rm_log_debug("Mem avail %li, active groups %d. " RED
                             "Refused request for %" LLU
                             " bytes for paranoid hashing.\n" RESET,
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
//    RmShredDevice UTILITIES    //
///////////////////////////////////

static void rm_shred_adjust_counters(RmShredTag *tag, int files, gint64 bytes) {
    g_mutex_lock(&tag->lock);
    {
        RmSession *session = tag->session;
        tag->cache_byte_count += bytes;
        tag->cache_file_count +=files;
        if (files<0) {
            tag->cache_filtered_count +=files;
        }

        if (abs(tag->cache_file_count) >= SHRED_MIN_FILE_STATS_PACK_SIZE) {
            rm_fmt_lock_state(session->formats);
            {
#if RM_SHRED_DEBUG
                gint64 bytes_remaining = session->shred_bytes_remaining + tag->cache_byte_count;
                gint64 files_remaining = session->shred_files_remaining + tag->cache_file_count;
                g_assert(check_bytes>=0);
                g_assert(check_files>=0);
#endif
                session->shred_files_remaining += tag->cache_file_count;
                session->total_filtered_files += tag->cache_filtered_count;
                session->shred_bytes_remaining += tag->cache_byte_count;

                rm_fmt_set_state(session->formats, (tag->after_preprocess)
                                                       ? RM_PROGRESS_STATE_SHREDDER
                                                       : RM_PROGRESS_STATE_PREPROCESS);
                tag->cache_file_count = 0;
                tag->cache_filtered_count = 0;
                tag->cache_byte_count = 0;
            }
            rm_fmt_unlock_state(session->formats);
        }
    }
    g_mutex_unlock(&tag->lock);
}

static void rm_shred_write_cksum_to_xattr(const RmSession *session, RmFile *file) {
    if(session->cfg->write_cksum_to_xattr) {
        if(file->has_ext_cksum == false) {
            rm_xattr_write_hash((RmSession*)session, file);
        }
    }
}

/* Unlink RmFile from device queue
 */
static void rm_shred_discard_file(RmFile *file, bool free_file) {
    const RmSession *session = file->session;
    RmShredTag *tag = session->shredder;
    /* update device counters (unless this file was a bundled hardlink) */
    if(!file->hardlinks.hardlink_head) {
        rm_mds_ref_dev(session->mds, file->dev, -1);
        rm_shred_adjust_counters(tag, -1,
                                 -(gint64)(file->file_size - file->hash_offset));

        /* ShredGroup that was going nowhere */
        g_assert(session->cfg->write_unfinished || TRUE);
        if(file->shred_group && file->shred_group->num_files <= 1 && session->cfg->write_unfinished) {
            file->lint_type = RM_LINT_TYPE_UNFINISHED_CKSUM;
            file->digest = (file->digest) ? file->digest : file->shred_group->digest;

            if(file->digest) {
                rm_fmt_write(file, session->formats, -1);
                rm_shred_write_cksum_to_xattr(session, file);
                file->digest = NULL;
            }
        }

        /* update paranoid memory allocator */
        //TODO???
    }

    if(free_file) {
        /* toss the file (and any embedded hardlinks)*/
        rm_file_destroy(file);
    }
}

/* Push file to scheduler queue.
 * */
static void rm_shred_push_queue(RmFile *file) {
    const RmSession *session = file->session;
    if (file->hash_offset==0) {
        /* first-timer; lookup disk offset */
        if (file->session->cfg->build_fiemap &&
                !rm_mounts_is_nonrotational(file->session->mounts, file->dev)) {
            RM_DEFINE_PATH(file);
            file->disk_offset = rm_offset_get_from_path(file_path, 0, NULL);
        } else {
            /* use inode number instead of disk offset */
            file->disk_offset = file->inode;
        }
    }
    rm_mds_push_task_by_dev(session->mds,
            file->session->cfg->fake_pathindex_as_disk ? file->path_index : file->dev,
            file->disk_offset, NULL, file);
}

//////////////////////////////////
//    RMSHREDGROUP UTILITIES    //
//    AND SIFTING ALGORITHM     //
//////////////////////////////////

/* Free RmShredGroup and any dormant files still in its queue
 */
static void rm_shred_group_free(RmShredGroup *self, bool force_free) {
    g_assert(self->parent == NULL); /* children should outlive their parents! */

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
        g_hash_table_unref(self->children);
    }

    g_assert(!self->in_progress_digests);

    g_mutex_clear(&self->lock);

    g_slice_free(RmShredGroup, self);
}

/* call unlocked; should be no contention issues since group is finished */
static void rm_shred_group_finalise(RmShredGroup *self) {
    /* return any paranoid mem allocation */
    rm_shred_mem_return(self);

    switch(self->status) {
    case RM_SHRED_GROUP_DORMANT:
        /* Dead-ended files; don't force free since we may want to write the partial
         * checksums */
        rm_shred_group_free(self, FALSE);
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
        g_assert_not_reached();
    }
}

/* Checks whether group qualifies as duplicate candidate (ie more than
 * two members and meets has_pref and NEEDS_PREF criteria).
 * Assume group already protected by group_lock.
 * */
static void rm_shred_group_update_status(RmShredGroup *group) {
    if(group->status == RM_SHRED_GROUP_DORMANT) {
        if(1 && group->num_files >= 2 /* it takes 2 to tango */
           &&
           (group->has_pref || !NEEDS_PREF(group))
           /* we have at least one file from preferred path, or we don't care */
           &&
           (group->has_npref || !NEEDS_NPREF(group))
           /* we have at least one file from non-pref path, or we don't care */
           &&
           (group->has_new || !NEEDS_NEW(group))
           /* we have at least one file newer than cfg->min_mtime, or we don't care */
           ) {
            if(group->hash_offset < group->file_size &&
               group->has_only_ext_cksums == false) {
                /* group can go active */
                group->status = RM_SHRED_GROUP_START_HASHING;
            } else {
                group->status = RM_SHRED_GROUP_FINISHING;
            }
        }
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

/* Call with shred_group->lock unlocked.
 * */
static RmFile *rm_shred_group_push_file(RmShredGroup *shred_group, RmFile *file,
                                         gboolean initial) {
    RmFile *result = NULL;
    file->shred_group = shred_group;

    if(file->digest) {
        rm_digest_free(file->digest);
        file->digest = NULL;
    }

    g_mutex_lock(&shred_group->lock);
    {
        shred_group->has_pref |= file->is_prefd | file->hardlinks.has_prefd;
        shred_group->has_npref |= (!file->is_prefd) | file->hardlinks.has_non_prefd;
        shred_group->has_new |= file->is_new_or_has_new;

        shred_group->num_files++;
        if(file->hardlinks.is_head) {
            g_assert(file->hardlinks.files);
            shred_group->num_files += file->hardlinks.files->length;
        }

        g_assert(file->hash_offset == shred_group->hash_offset);

        rm_shred_group_update_status(shred_group);
        switch(shred_group->status) {
        case RM_SHRED_GROUP_START_HASHING:
            /* clear the queue and push all its rmfiles to the appropriate device queue */
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
            if(initial || !file->devlist_waiting) {
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
            g_assert_not_reached();
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

    g_assert(file);
    RmShredGroup *current_group = file->shred_group;
    g_assert(current_group);

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
            rm_digest_free(file->digest);
            rm_shred_discard_file(file, true);

        } else {
            g_assert(file->digest);

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
                child_group->has_only_ext_cksums = current_group->has_only_ext_cksums;

                /* signal any pending (paranoid) digests that there is a new match
                 * candidate digest */
                g_list_foreach(current_group->in_progress_digests,
                               (GFunc)rm_digest_send_match_candidate,
                               child_group->digest);
            }
            result =
                rm_shred_group_push_file(child_group, file, FALSE);  // TODO: ok locked?
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

/* Hasher callback file. Runs as threadpool in parallel / tandem with
 * rm_shred_read_factory above
 * */
static void rm_shred_hash_callback(_U RmHasher *hasher, RmDigest *digest, RmShredTag *tag,
                                   RmFile *file) {
    /* Report the progress to rm_shred_devlist_factory */
    g_assert(file->digest == digest);

    if(file->hash_offset == file->shred_group->next_offset ||
       file->status == RM_FILE_STATE_IGNORE) {
        if(file->status != RM_FILE_STATE_IGNORE) {
            /* remember that checksum */
            rm_shred_write_cksum_to_xattr(tag->session, file);
        }

        if(file->signal) {
            /* MDS scheduler is waiting for result */
            rm_signal_done(file->signal);
        } else {
            /* handle the file ourselves; MDS scheduler has moved on to the next file */
            rm_shred_sift(file);
        }
    } else {
        RM_DEFINE_PATH(file);
        rm_log_error("Unexpected hash offset for %s, got %" LLU ", expected %" LLU "\n",
                     file_path, file->hash_offset, file->shred_group->next_offset);
        g_assert_not_reached();
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
 * 	  files via rm_shred_device_preprocess.
 * */
static void rm_shred_file_preprocess(_U gpointer key, RmFile *file, RmShredTag *main) {
    /* initial population of RmShredDevice's and first level RmShredGroup's */
    RmSession *session = main->session;

    g_assert(file);
    g_assert(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE);
    g_assert(file->file_size > 0);

    file->is_new_or_has_new = (file->mtime >= session->cfg->min_mtime);

    /* if file has hardlinks then set file->hardlinks.has_[non_]prefd*/
    if(file->hardlinks.is_head) {
        for(GList *iter = file->hardlinks.files->head; iter; iter = iter->next) {
            RmFile *link = iter->data;
            file->hardlinks.has_non_prefd |= !(link->is_prefd);
            file->hardlinks.has_prefd |= link->is_prefd;
            file->is_new_or_has_new |= (link->mtime >= session->cfg->min_mtime);
        }
    }

    /* add reference for this file to the MDS scheduler */
    rm_mds_ref_dev(session->mds, file->dev, 1);
    rm_shred_adjust_counters(main, 1, (gint64)file->file_size - file->hash_offset);

    RmShredGroup *group = g_hash_table_lookup(session->tables->size_groups, file);

    if(group == NULL) {
        group = rm_shred_group_new(file);
        group->digest_type = session->cfg->checksum_type;
        g_hash_table_insert(session->tables->size_groups, file, group);
    }

    rm_shred_group_push_file(group, file, true);

    if(main->session->cfg->read_cksum_from_xattr) {
        char *ext_cksum = rm_xattr_read_hash(main->session, file);
        if(ext_cksum != NULL) {
            file->folder->data = ext_cksum;
        }
    }

    if(HAS_CACHE(session)) {
        RM_DEFINE_PATH(file);
        if (rm_trie_search(&session->cfg->file_trie, file_path)) {
            group->num_ext_cksums += 1;
            file->has_ext_cksum = 1;
        }
    }
}

static gboolean rm_shred_group_preprocess(_U gpointer key, RmShredGroup *group, _U RmShredTag *tag) {
    g_assert(group);
    if(group->status == RM_SHRED_GROUP_DORMANT) {
        rm_shred_group_free(group, true);
        return true;
    } else {
        return false;
    }
}

static void rm_shred_preprocess_input(RmShredTag *main) {
    RmSession *session = main->session;
    guint removed = 0;

    /* move remaining files to RmShredGroups */
    g_assert(session->tables->node_table);

    /* Read any cache files */
    for(GList *iter = main->session->cache_list.head; iter; iter = iter->next) {
        char *cache_path = iter->data;
        rm_json_cache_read(&session->cfg->file_trie, cache_path);
    }

    rm_log_debug("Moving files into size groups...\n");

    /* move files from node tables into initial RmShredGroups */
    g_hash_table_foreach_remove(session->tables->node_table,
                                (GHRFunc)rm_shred_file_preprocess, main);
    g_hash_table_unref(session->tables->node_table);
    session->tables->node_table = NULL;

    GHashTableIter iter;
    gpointer size, p_group;

    if(HAS_CACHE(main->session)) {
        g_assert(session->tables->size_groups);
        g_hash_table_iter_init(&iter, session->tables->size_groups);
        while(g_hash_table_iter_next(&iter, &size, &p_group)) {
            RmShredGroup *group = p_group;
            if(group->num_files == group->num_ext_cksums) {
                group->has_only_ext_cksums = true;
            }
        }
    }

    rm_log_debug("move remaining files to size_groups finished at time %.3f\n",
                 g_timer_elapsed(session->timer, NULL));

    rm_log_debug("Discarding unique sizes and read fiemap data for others...");
    g_assert(session->tables->size_groups);
    removed = g_hash_table_foreach_remove(session->tables->size_groups,
                                          (GHRFunc)rm_shred_group_preprocess, main);
    g_hash_table_unref(session->tables->size_groups);
    session->tables->size_groups = NULL;

    rm_log_debug("done at time %.3f; removed %u of %" LLU "\n",
                 g_timer_elapsed(session->timer, NULL), removed,
                 session->total_filtered_files);

}

/////////////////////////////////
//       POST PROCESSING       //
/////////////////////////////////

/* post-processing sorting of files by criteria (-S and -[kmKM])
 * this is slightly different to rm_shred_cmp_orig_criteria in the case of
 * either -K or -M options
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
void rm_shred_group_find_original(RmSession *session, GQueue *group) {
    /* iterate over group, unbundling hardlinks and identifying "tagged" originals */
    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        file->is_original = false;

        if(file->hardlinks.is_head && file->hardlinks.files) {
            /* if group member has a hardlink cluster attached to it then
             * unbundle the cluster and append it to the queue
             */
            GQueue *hardlinks = file->hardlinks.files;
            for(GList *link = hardlinks->head; link; link = link->next) {
                g_queue_push_tail(group, link->data);
            }
            g_queue_free(hardlinks);
            file->hardlinks.files = NULL;
        }
        /* identify "tagged" originals: */
        if(((file->is_prefd) && (session->cfg->keep_all_tagged)) ||
           ((!file->is_prefd) && (session->cfg->keep_all_untagged))) {
            file->is_original = true;

#if _RM_SHRED_DEBUG
            RM_DEFINE_PATH(file);
            rm_log_debug("tagging %s as original because %s\n",
                         file_path,
                         ((file->is_prefd) && (session->cfg->keep_all_tagged))
                             ? "tagged"
                             : "untagged");
#endif
        }
    }

    /* sort the unbundled group */
    g_queue_sort(group, (GCompareDataFunc)rm_shred_cmp_orig_criteria, session);

    RmFile *headfile = group->head->data;
    if(!headfile->is_original) {
        headfile->is_original = true;
#if _RM_SHRED_DEBUG
        RM_DEFINE_PATH(headfile);
        rm_log_debug("tagging %s as original because it is highest ranked\n",
                     headfile_path);
#endif
    }
}

void rm_shred_forward_to_output(RmSession *session, GQueue *group) {
    g_assert(group);
    g_assert(group->head);

#if _RM_SHRED_DEBUG
    RmFile *head = group->head->data;
    RM_DEFINE_PATH(head);
    rm_log_debug("Forwarding %s's group\n", head_path);
#endif

    /* Hand it over to the printing module */
    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        rm_fmt_write(file, session->formats, group->length);
    }
}

static void rm_shred_dupe_totals(RmFile *file, RmSession *session) {
    if(!file->is_original) {
        session->dup_counter++;

        /* Only check file size if it's not a hardlink.
         * Since deleting hardlinks does not free any space
         * they should not be counted unless all of them would
         * be removed.
         */
        if(file->hardlinks.is_head || file->hardlinks.hardlink_head == NULL) {
            session->total_lint_size += file->file_size;
        }
    }
}

static void rm_shred_result_factory(RmShredGroup *group, RmShredTag *tag) {
    RmCfg *cfg = tag->session->cfg;

    if(g_queue_get_length(group->held_files) > 0) {
        /* find the original(s)
         * (note this also unbundles hardlinks and sorts the group from
         *  highest ranked to lowest ranked
         */
        rm_shred_group_find_original(tag->session, group->held_files);

        /* Update statistics */
        rm_fmt_lock_state(tag->session->formats);
        {
            tag->session->dup_group_counter++;
            g_queue_foreach(group->held_files, (GFunc)rm_shred_dupe_totals, tag->session);
        }
        rm_fmt_unlock_state(tag->session->formats);

        /* Cache the files for merging them into directories */
        for(GList *iter = group->held_files->head; iter; iter = iter->next) {
            RmFile *file = iter->data;
            file->digest = group->digest;

            if(cfg->merge_directories) {
                rm_tm_feed(tag->session->dir_merger, file);
            }
        }

        if(cfg->merge_directories == false) {
            /* Output them directly, do not merge them first. */
            rm_shred_forward_to_output(tag->session, group->held_files);
        }
    }

    group->status = RM_SHRED_GROUP_FINISHED;
#if _RM_SHRED_DEBUG
    rm_log_debug("Free from rm_shred_result_factory\n");
#endif

    /* Do not force free files here, output module might need do that itself. */
    rm_shred_group_free(group, false);
}

/////////////////////////////////
//    ACTUAL IMPLEMENTATION    //
/////////////////////////////////

static bool rm_shred_reassign_checksum(RmShredTag *main, RmFile *file) {
    bool can_process = true;
    RmCfg *cfg = main->session->cfg;
    RmShredGroup *group = file->shred_group;

    if(group->has_only_ext_cksums) {
        /* Cool, we were able to read the checksum from disk */
        file->digest = rm_digest_new(RM_DIGEST_EXT, 0, 0, 0, NEEDS_SHADOW_HASH(cfg));

        RM_DEFINE_PATH(file);

        char *hexstring = file->folder->data;

        if(hexstring != NULL) {
            rm_digest_update(file->digest, (unsigned char *)hexstring, strlen(hexstring));
            rm_log_debug("%s=%s was read from cache.\n", hexstring, file_path);
        } else {
            rm_log_warning_line(
                "Unable to read external checksum from interal cache for %s", file_path);
            file->has_ext_cksum = 0;
            group->has_only_ext_cksums = 0;
        }
    } else if(group->digest_type == RM_DIGEST_PARANOID) {
        /* check if memory allocation is ok */
        if(!rm_shred_check_paranoid_mem_alloc(group, 0)) {
            can_process = false;
        } else {
            /* get the required target offset into group->next_offset, so
                * that we can make the paranoid RmDigest the right size*/
            if(group->next_offset == 0) {
                (void)rm_shred_get_read_size(file, main);
            }
            g_assert(group->hash_offset == file->hash_offset);

            if(file->is_symlink && cfg->see_symlinks) {
                file->digest =
                    rm_digest_new(RM_DIGEST_PARANOID, 0, 0,
                                  PATH_MAX + 1 /* max size of a symlink file */,
                                  NEEDS_SHADOW_HASH(cfg));
            } else {
                file->digest = rm_digest_new(RM_DIGEST_PARANOID, 0, 0,
                                             group->next_offset - file->hash_offset,
                                             NEEDS_SHADOW_HASH(cfg));
                if(group->next_offset > file->hash_offset + SHRED_PREMATCH_THRESHOLD) {
                    /* send candidate twin(s) */
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
            }
        }
    } else if(group->digest) {
        /* pick up the digest-so-far from the RmShredGroup */
        file->digest = rm_digest_copy(group->digest);
    } else {
        /* this is first generation of RMGroups, so there is no progressive hash yet */
        file->digest = rm_digest_new(cfg->checksum_type,
                                     main->session->hash_seed1,
                                     main->session->hash_seed2,
                                     0,
                                     NEEDS_SHADOW_HASH(cfg));
    }

    return can_process;
}

#define RM_SHRED_TOO_MANY_BYTES_TO_WAIT (64 * 1024 * 1024)

/* call with device unlocked */
static bool rm_shred_can_process(RmFile *file, RmShredTag *main) {
    /* initialise hash (or recover progressive hash so far) */
    if (!file->shred_group) {
        return FALSE;
    }

    bool result = TRUE;
    g_mutex_lock(&file->shred_group->lock);
    {
        if(file->digest == NULL) {
            g_assert(file->shred_group);
            result = rm_shred_reassign_checksum(main, file);
        }
    }
    g_mutex_unlock(&file->shred_group->lock);
    return result;
}

/**
 * @brief  Callback for RmMDS
 **/

static gint rm_shred_process_file(RmFile *file, RmSession *session) {
    RmShredTag *tag = session->shredder;
    if (!rm_shred_can_process(file, tag)) {
        return 0;
    }

    if(file->shred_group->has_only_ext_cksums) {
        rm_shred_adjust_counters(tag, 0, -(gint64)file->file_size);
        rm_shred_sift(file);
        return 1;
    }

    RM_DEFINE_PATH(file);

    while (file && rm_shred_can_process(file, tag)) {

        /* hash the next increment of the file */
        bool worth_waiting = FALSE;
        RmCfg *cfg = session->cfg;
        RmOff bytes_to_read = rm_shred_get_read_size(file, tag);

        g_mutex_lock(&file->shred_group->lock);
        {
            worth_waiting = (file->shred_group->next_offset != file->file_size)
                            &&
                            (cfg->shred_always_wait || (
                                    /* TODO: device->is_rotational && */
                                    rm_shred_get_read_size(file, tag) < RM_SHRED_TOO_MANY_BYTES_TO_WAIT &&
                                    (file->status == RM_FILE_STATE_NORMAL) &&
                                    !cfg->shred_never_wait
                                )
                            );
        }
        g_mutex_unlock(&file->shred_group->lock);

        RmHasherTask *task = rm_hasher_task_new(tag->hasher, file->digest, file);
        if (!rm_hasher_task_hash(task, file_path, file->hash_offset, bytes_to_read, file->is_symlink)) {
            /* rm_hasher_start_increment failed somewhere */
            file->status = RM_FILE_STATE_IGNORE;
            worth_waiting = FALSE;
        }

        /* Update totals for file, device and session*/
        file->hash_offset += bytes_to_read;
        if (file->is_symlink) {
            rm_shred_adjust_counters(tag, 0, -(gint64)file->file_size);
        } else {
            rm_shred_adjust_counters(tag, 0, -(gint64)bytes_to_read);
        }

        if (worth_waiting) {
            /* some final checks if it's still worth waiting for the hash result */
            g_mutex_lock(&file->shred_group->lock);
            {
                worth_waiting = worth_waiting && (file->shred_group->children);
                if (file->digest->type == RM_DIGEST_PARANOID) {
                    worth_waiting = worth_waiting && file->digest->paranoid->twin_candidate;
                }
            }
            g_mutex_unlock(&file->shred_group->lock);
        }

        file->signal = worth_waiting ? rm_signal_new() : NULL;

        /* tell the hasher we have finished */
        rm_hasher_task_finish(task);

        if(worth_waiting) {
            /* wait until the increment has finished hashing; assert that we get the expected file back */
            rm_signal_wait(file->signal);
            file->signal = NULL;
            /* sift file; if returned then continue processing it */
            file = rm_shred_sift(file);
        } else {
            file = NULL;
        }
    }
    return 1;
}

void rm_shred_run(RmSession *session) {
    g_assert(session);
    g_assert(session->tables);
    g_assert(session->mounts);

    RmShredTag tag;
    tag.active_groups = 0;
    tag.session = session;
    session->shredder = &tag;

    tag.device_return = g_async_queue_new();
    tag.page_size = SHRED_PAGE_SIZE;

    tag.cache_file_count = 0;
    tag.cache_byte_count = 0;
    tag.cache_filtered_count = 0;
    tag.after_preprocess = FALSE;

    /* would use g_atomic, but helgrind does not like that */
    g_mutex_init(&tag.hash_mem_mtx);

    g_mutex_init(&tag.lock);

    session->mds = rm_mds_new(
            g_hash_table_size(session->mounts->disk_table),
            session->mounts);
    rm_mds_configure(
            session->mds,
            (RmMDSFunc)rm_shred_process_file,
            session,
            session->cfg->sweep_count,
            (RmMDSSortFunc)rm_mds_elevator_cmp);

    rm_shred_preprocess_input(&tag);
    rm_log_debug("Done shred preprocessing\n");
    tag.after_preprocess = TRUE;
    session->shred_bytes_after_preprocess = session->shred_bytes_remaining;

    /* estimate mem used for RmFiles and allocate any leftovers to read buffer and/or
     * paranoid mem */
    RmOff mem_used = RM_AVERAGE_MEM_PER_FILE * session->shred_files_remaining;

    if(session->cfg->checksum_type == RM_DIGEST_PARANOID) {
        /* allocate any spare mem for paranoid hashing */
        tag.paranoid_mem_alloc = MAX((gint64)session->cfg->paranoid_mem,
                                     (gint64)session->cfg->total_mem - (gint64)mem_used -
                                         (gint64)session->cfg->read_buffer_mem);
        rm_log_info(BLUE "Paranoid Mem: %" LLU "\n", tag.paranoid_mem_alloc);
    } else {
        session->cfg->read_buffer_mem =
            MAX((gint64)session->cfg->read_buffer_mem,
                (gint64)session->cfg->total_mem - (gint64)mem_used);
        tag.paranoid_mem_alloc = 0;
    }
    rm_log_info(BLUE "Read buffer Mem: %" LLU "\n", session->cfg->read_buffer_mem);

    /* Initialise hasher */
    /* Optimum buffer size based on /usr without dropping caches:
     * SHRED_PAGE_SIZE * 1 => 5.29 seconds
     * SHRED_PAGE_SIZE * 2 => 5.11 seconds
     * SHRED_PAGE_SIZE * 4 => 5.04 seconds
     * SHRED_PAGE_SIZE * 8 => 5.08 seconds
     * With dropped caches:
     * SHRED_PAGE_SIZE * 1 => 45.2 seconds
     * SHRED_PAGE_SIZE * 4 => 45.0 seconds*/
    tag.hasher = rm_hasher_new(session->cfg->checksum_type,
                               session->cfg->threads,
                               session->cfg->use_buffered_read,
                               SHRED_PAGE_SIZE * 4,
                               session->cfg->read_buffer_mem,
                               session->cfg->paranoid_mem,
                               (RmHasherCallback)rm_shred_hash_callback,
                               &tag);

    /* Create a pool for results processing */
    tag.result_pool = rm_util_thread_pool_new((GFunc)rm_shred_result_factory, &tag, 1);

    rm_mds_start(session->mds);

    /* should complete shred session and then free: */
    rm_mds_free(session->mds, FALSE);
    rm_hasher_free(tag.hasher, TRUE);

    session->shredder_finished = TRUE;
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_SHREDDER);

    /* This should not block, or at least only very short. */
    g_thread_pool_free(tag.result_pool, FALSE, TRUE);

    g_async_queue_unref(tag.device_return);

    g_mutex_clear(&tag.hash_mem_mtx);
    rm_log_error("Remaining %lu bytes in %lu files, cached %i\n", session->shred_bytes_remaining, session->shred_files_remaining, tag.cache_filtered_count);
}
