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
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/uio.h>

#include "checksum.h"

#include "preprocess.h"
#include "utilities.h"
#include "formats.h"

#include "shredder.h"
#include "xattr.h"

/* Enable extra debug messages? */
#define _RM_SHRED_DEBUG 0

/* This is the scheduler of rmlint.
 *
 * Files are compared in progressive "generations" to identify matching
 * clusters:
 * Generation 0: Same size files
 * Generation 1: Same size and same hash of first  ~16kB
 * Generation 2: Same size and same hash of first  ~50MB
 * Generation 3: Same size and same hash of first ~100MB
 * Generation 3: Same size and same hash of first ~150MB
 * ... and so on until the end of the file is reached.
 *
 * The default step size can be configured below.
 *
 * The step size algorithm has some adaptive logic and may shorten
 * or increase the step size if (a) a few extra MB will get to the end
 * of the file, or (b) there is a fragmented file which has a file
 * fragment ending within a few MB of the default read increment.
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
 * 1. Pick a file from the device_queue
 * 2. Hash the next increment
 * 3. Check back with the file's parent to see if there is a child RmShredGroup with
 *    matching hash; if not then create a new one.
 * 4. Add the file into the child RmShredGroup and unlink it from its parent(see note
 *    below on Unlinking from Parent)
 * 5. Check if the child RmShredGroup meets criteria for hashing; if no then loop
 *    back to (1) for another file to hash
 * 6. If file meets criteria and is not finished hash then loop back to 2 and
 *    hash its next increment
 * 7. If file meets criteria and is fully hashed then flag it as ready for post-
 *    processing (possibly via paranoid check).  Note that post-processing can't
 *    start until the RmShredGroup's parent is dead (because new siblings may still
 *    be coming).
 *
 * In the above example, the hashing order will end up being something like:
 * F1.1 F2.1 (F3.1,F3.2), (F4.1,F4.2), (F5.1,F5.2)...
 *                ^            ^            ^
 *  (^ indicates where hashing could continue on to a second increment because there
 * 	   was already a matching file after the first increment)
 *
 * The threading looks somewhat like this for two devices:
 *
 *                          +----------+
 *                          | Finisher |
 *                          |  Thread  |
 *                          |  incl    |
 *                          | paranoid |
 *                          +----------+
 *                                ^
 *                                |
 *                        +--------------+
 *                        | Matched      |
 *                        | fully-hashed |
 *                        | dupe groups  |
 *    Device #1           +--------------+      Device #2
 *                               ^
 * +-------------------+         |           +------------------+
 * | +-------------+   |    +-----------+    | +-------------+  |
 * | | Devlist Mgr |<-------+--Push to--+----->| Devlist Mgr |  |
 * | +-------------+   |    |  device   |    | +-------------+  |
 * | pop from          |    |  queues   |    |        pop from  |
 * |  queue            |    |           |    |         queue    |
 * |     |             |    |ShredGroups|    |            |     |
 * |     |<--Continue  |    | (Matched  |    | Continue-->|     |
 * |     |      ^      |    |  partial  |    |    ^       |     |
 * |     v      |      |    |  hashes)  |    |    |       v     |
 * |   Read     Y      |    |           |    |    Y      Read   |
 * |     |      |      |    |    make   |    |    |       |     |
 * |     |   Partial------N----> new <---------Partial    |     |
 * |     |    Match?   |    |   group   |    | Match?     |     |
 * |     |      ^      |    |     ^     |    |    ^       |     |
 * +-----|------|------+    +-----|-----+    +----|-------|-----+
 *       v      |                 |               |       v
 *    +----------+      +------------------+     +----------+
 *    | Hasher   |      |Initial file list |     | Hasher   |
 *    |(1 thread)|      |                  |     |(1 thread)|
 *    +----------+      +------------------+     +----------+
 *
 * Every subbox left and right are the task that are performed.
 *
 * The Devlist Managers, Hashers and Finisher run as separate threads
 * managed by GThreadPool.
 *
 * The Devlist Managers work sequentially through the queue of hashing
 * jobs, sorted in order of disk offset in order to reduce seek times.
 * On init every device gets it's own thread. This thread spawns it own
 * hasher thread from another GTHreadPool.
 *
 * The Devlist Manager calls the reader function to read one file at a
 * time using readv(). The buffers for it come from a central buffer pool
 * that allocates some and just reuses them over and over. The buffers
 * which contain the read data are pushed to the hasher thread, where
 * the data-block is hashed into file->digest.  The buffer is released
 * back to the pool after use.
 *
 * Once the hasher is done, the file is sent back to the Devlist Manager
 * via a GAsyncQueue.  The Devlist Manager does a quick check to see if
 * it can continue with the same file; if not then the file is released
 * back to the RmShredGroups and a new file taken from the device queue.
 *
 *
 * The RmShredGroups don't have a thread managing them, instead the individual
 * Devlist Managers write to the RmShredGroups under mutex protection.
 *
 *
 * The initial ("foreground") thread waits for the Devlist Managers to
 * finish their sequential walk through the files.  If there are still
 * files to process on the device, the initial thread sends them back to
 * the GThreadPool for another pass through the files (starting from the
 * lowest disk offset again).
 *
 * Note re Unlinking a file from parent (this is the most thread-risky
 * part of the operation so sequencing needs to be clear):
 * * Decrease parent's child_file counter; if that is zero then check if
 * the parent's parent is dead; if yes then kill the parent.
 * * When killing the parent, tell all its children RMGroups that they
 * are now orphans (which may mean it is now time for some of them to
 * die too).
 * Note that we need to be careful here to avoid threadlock, eg:
 *    Child file 1 on device 1 has finished an increment.  It takes a look on its new
 *    RmGroup so that it can add itself.  It then locks its parent RmShredGroup so that
 *    it can do the unlinking.  If it turns out it is time for the parent to die, we
 *    we need to lock each of its children so that we can make them orphans:
 *        Q: What if another one of its other children was also trying to unlink?
 *        A: No problem, can't happen (since parent can't be ready to die if it has
 *           any active children left)
 *        Q: What about the mutex loop from this child which is doing the unlinking?
 *        A: No problem, either unlock the child's mutex before calling parent unlink,
 *           or handle the calling child differently from the other children
*
* Below some performance controls are listed that may impact performance.
* Controls are sorted by subjectve importanceness.
*/

////////////////////////////////////////////
// OPTIMISATION PARAMETERS FOR DECIDING   //
// HOW MANY BYTES TO READ BEFORE STOPPING //
// TO COMPARE PROGRESSIVE HASHES          //
////////////////////////////////////////////

/* How many milliseconds to sleep if we encounter an empty file queue.
 * This prevents a "starving" RmShredDevice from hogging cpu and cluttering up
 * debug messages by continually recycling back to the joiner.
 */
#define SHRED_EMPTYQUEUE_SLEEP_US (50 * 1000) /* 0.05 second */

/* how many pages can we read in (seek_time)/(CHEAP)? (use for initial read) */
#define SHRED_BALANCED_PAGES (4)

/* How large a single page is (typically 4096 bytes but not always)*/
#define SHRED_PAGE_SIZE (sysconf(_SC_PAGESIZE))

#define SHRED_MAX_READ_FACTOR \
    ((256 * 1024 * 1024) / SHRED_BALANCED_PAGES / SHRED_PAGE_SIZE)

/* Wether to use buffered fread() or direct preadv()
 * The latter is preferred, since it's slightly faster on linux.
 * Other platforms may have different results though or not even have preadv.
 * */
#define SHRED_USE_BUFFERED_READ (0)

/* Flags for the fadvise() call that tells the kernel
 * what we want to do with the file.
 */
#define SHRED_FADVISE_FLAGS                                      \
    (0 | POSIX_FADV_SEQUENTIAL /* Read from 0 to file-size    */ \
     | POSIX_FADV_WILLNEED     /* Tell the kernel to readhead */ \
     | POSIX_FADV_NOREUSE      /* We will not reuse old data  */ \
     )

////////////////////////
//  MATHS SHORTCUTS   //
////////////////////////

#define DIVIDE_CEIL(n, m) ((n) / (m) + !!((n) % (m)))
#define SIGN_DIFF(X, Y) (((X) > (Y)) - ((X) < (Y))) /* handy for comparing unit64's */

///////////////////////////////////////////////////////////////////////
//    INTERNAL STRUCTURES, WITH THEIR INITIALISERS AND DESTROYERS    //
///////////////////////////////////////////////////////////////////////

/////////// RmBufferPool and RmBuffer ////////////////

typedef struct RmBufferPool {
    /* Place where the buffers are stored */
    GTrashStack *stack;

    /* how many buffers are available? */
    RmOff size;

    /* concurrent accesses may happen */
    GMutex lock;
} RmBufferPool;

/* Represents one block of read data */
typedef struct RmBuffer {
    /* file structure the data belongs to */
    RmFile *file;

    /* len of the read input */
    guint32 len;

    /* flag to indicate that there is no more data for the current hash increment */
    bool finished : 1;

    /* *must* be last member of RmBuffer,
     * gets all the rest of the allocated space
     * */
    guint8 data[];
} RmBuffer;

/////////* The main extra data for the scheduler *///////////

typedef struct RmMainTag {
    RmSession *session;
    RmBufferPool *mem_pool;
    GAsyncQueue *device_return;
    GMutex hash_mem_mtx;
    gint64 hash_mem_alloc; /* how much memory to allocate for paranoid checks */
    gint32
        active_groups; /* how many shred groups active (only used with paranoid a.t.m.) */
    GThreadPool *device_pool;
    GAsyncQueue *hash_pool_pool;
    GThreadPool *result_pool;
    gint32 page_size;
    bool mem_refusing;
} RmMainTag;

/////////// RmShredDevice ////////////////

typedef struct RmShredDevice {
    /* queue of files awaiting (partial) hashing, sorted by disk offset.  Note
     * this can be written to be other threads so requires mutex protection */
    GQueue *file_queue;

    /* Counters, used to determine when there is nothing left to do.  These
     * can get written to by other device threads so require mutex protection */
    gint32 remaining_files;
    gint64 remaining_bytes;

    /* True when actual shreddiner began.
     * This is used to update the correct progressbar state.
     */
    bool after_preprocess : 1;

    /* Lock for all of the above */
    GMutex lock;
    GCond change;

    /* disk type; allows optimisation of parameters for rotational or non- */
    bool is_rotational;

    /* Return queue for files which have finished the current increment */
    GAsyncQueue *hashed_file_return;

    /* disk identification, for debugging info only */
    char *disk_name;
    dev_t disk;

    /* head position information, to optimise selection of next file */
    RmOff current_offset;
    dev_t current_dev;

    /* size of one page, cached, so
     * sysconf() does not need to be called always.
     */
    RmOff page_size;
    RmMainTag *main;
} RmShredDevice;

typedef enum RmShredGroupStatus {
    RM_SHRED_GROUP_DORMANT = 0,
    RM_SHRED_GROUP_START_HASHING,
    RM_SHRED_GROUP_HASHING,
    RM_SHRED_GROUP_FINISHING,
    RM_SHRED_GROUP_FINISHED
} RmShredGroupStatus;

#define NEEDS_PREF(group)                            \
    (group->main->session->cfg->must_match_tagged || \
     group->main->session->cfg->keep_all_untagged)
#define NEEDS_NPREF(group)                             \
    (group->main->session->cfg->must_match_untagged || \
     group->main->session->cfg->keep_all_tagged)
#define NEEDS_NEW(group) (group->main->session->cfg->min_mtime)

#define HAS_CACHE(session) \
    (session->cfg->read_cksum_from_xattr || session->cache_list.length)

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

    /* reference count (reasons for keeping group alive):
     *   1 for the parent
     *   1 for each file that hasn't moved into a child group yet (which it can't do until
     * it has hashed the next increment) */
    gulong ref_count;

    /* number of files */
    gulong num_files;

    /* set if group has 1 or more files from "preferred" paths */
    bool has_pref;

    /* set if group has 1 or more files from "non-preferred" paths */
    bool has_npref;

    /* set if group has 1 or more files newer than cfg->min_mtime */
    bool has_new;

    /* set if group has been greenlighted by paranoid mem manager */
    bool is_active;

    /* incremented for each file in the group that obtained it's checksum from ext.
     * If all files came from there we do not even need to hash the group.
     */
    gulong num_ext_cksums;

    /* true if all files in the group have an external checksum */
    bool has_only_ext_cksums;

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
    RmMainTag *main;
} RmShredGroup;

/////////// RmShredGroup ////////////////

/* allocate and initialise new RmShredGroup
 */
static RmShredGroup *rm_shred_group_new(RmFile *file) {
    RmShredGroup *self = g_slice_new0(RmShredGroup);

    if(file->digest) {
        self->digest = rm_digest_copy(file->digest);
        self->digest_type = file->digest->type;
    } else {
        /* initial groups have no checksum */
        g_assert(!file->shred_group);
    }

    self->parent = file->shred_group;
    self->main = file->device->main;

    if(self->parent) {
        self->ref_count++;
        if(self->parent->offset_factor * 8 <= SHRED_MAX_READ_FACTOR) {
            self->offset_factor = self->parent->offset_factor * 8;
        } else {
            self->offset_factor = SHRED_MAX_READ_FACTOR;
        }
    } else {
        self->offset_factor = 1;
    }

    self->held_files = g_queue_new();
    self->file_size = file->file_size;
    self->hash_offset = file->hash_offset;

    g_assert(file->device->main);
    self->main = file->device->main;

    g_mutex_init(&self->lock);

    return self;
}

///////////////////////////////////////
//    BUFFER POOL IMPLEMENTATION     //
///////////////////////////////////////

static RmOff rm_buffer_pool_size(RmBufferPool *pool) {
    return pool->size;
}

static RmBufferPool *rm_buffer_pool_init(gsize size) {
    RmBufferPool *self = g_slice_new(RmBufferPool);
    self->stack = NULL;
    self->size = size;
    g_mutex_init(&self->lock);
    return self;
}

static void rm_buffer_pool_destroy(RmBufferPool *pool) {
    g_mutex_lock(&pool->lock);
    {
        while(pool->stack != NULL) {
            g_slice_free1(pool->size, g_trash_stack_pop(&pool->stack));
        }
    }
    g_mutex_unlock(&pool->lock);
    g_mutex_clear(&pool->lock);
    g_slice_free(RmBufferPool, pool);
}

static void *rm_buffer_pool_get(RmBufferPool *pool) {
    void *buffer = NULL;
    g_mutex_lock(&pool->lock);
    {
        if(!pool->stack) {
            buffer = g_slice_alloc(pool->size);
        } else {
            buffer = g_trash_stack_pop(&pool->stack);
        }
    }
    g_mutex_unlock(&pool->lock);
    g_assert(buffer);
    return buffer;
}

static void rm_buffer_pool_release(RmBufferPool *pool, void *buf) {
    g_mutex_lock(&pool->lock);
    { g_trash_stack_push(&pool->stack, buf); }
    g_mutex_unlock(&pool->lock);
}

//////////////////////////////////
// OPTIMISATION AND MEMORY      //
// MANAGEMENT ALGORITHMS        //
//////////////////////////////////

/* Compute optimal size for next hash increment
 * call this with group locked
 * */
static gint32 rm_shred_get_read_size(RmFile *file, RmMainTag *tag) {
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
    if (group->hash_offset + target_bytes + balanced_bytes >= group->file_size) {
        group->next_offset = group->file_size;
    } else {
        group->next_offset = group->hash_offset + target_bytes;
    }

    /* for paranoid digests, make sure next read is not > max size of paranoid buffer */
    if(group->digest_type == RM_DIGEST_PARANOID) {
        group->next_offset = MIN(group->next_offset, group->hash_offset + rm_digest_paranoia_bytes() );
    }

    file->status = RM_FILE_STATE_NORMAL;
    result = (group->next_offset - file->seek_offset);

    return result;
}

/* Memory manager (only used for RM_DIGEST_PARANOID at the moment
 * but could also be adapted for other digests if very large
 * filesystems are contemplated)
 */

static void rm_shred_mem_return(RmShredGroup *group) {
    if (group->is_active) {
        RmMainTag *tag = group->main;
        g_mutex_lock(&tag->hash_mem_mtx);
        {
            tag->hash_mem_alloc += group->mem_allocation;
            tag->active_groups --;
            group->is_active = FALSE;
            rm_log_debug("Mem avail %li, active groups %d. "
                    YELLOW "Returned %"LLU" bytes for paranoid hashing.\n" RESET,
                    tag->hash_mem_alloc, tag->active_groups, group->mem_allocation);
            tag->mem_refusing=FALSE;
            if (group->digest) {
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
static gulong rm_shred_group_potential_file_count(RmShredGroup *group){
    if(group->parent) {
        return group->ref_count + rm_shred_group_potential_file_count(group->parent) - 1;
    } else {
        return group->ref_count;
    }
}

/* Governer to limit memory usage by limiting how many RmShredGroups can be
 * active at any one time
 * NOTE: group_lock must be held before calling rm_shred_check_hash_mem_alloc
 */
static bool rm_shred_check_hash_mem_alloc(RmShredGroup *group, int active_group_threshold) {

    if(group->status >= RM_SHRED_GROUP_HASHING) {
        /* group already committed */
        return true;
    }

    gint64 mem_required = rm_shred_group_potential_file_count(group)
                        * MIN(group->file_size - group->hash_offset, 2 * rm_digest_paranoia_bytes());

    bool result = FALSE;
    RmMainTag *tag = group->main;
    g_mutex_lock(&tag->hash_mem_mtx);
    {
        gint64 inherited = group->parent ? group->parent->mem_allocation : 0;

        if (0
            || mem_required <= tag->hash_mem_alloc + inherited
            || (tag->active_groups <= active_group_threshold)){
            /* ok to proceed */
            /* only take what we need from parent */
            inherited = MIN(inherited, mem_required);
            if (inherited > 0){
                group->parent->mem_allocation -= inherited;
                group->mem_allocation += inherited;
            }

            /* take the rest from bank */
            gint64 borrowed = MIN (mem_required - inherited, (gint64)tag->hash_mem_alloc);
            tag->hash_mem_alloc -= borrowed;
            group->mem_allocation += borrowed;

            rm_log_debug("Mem avail %li, active groups %d."GREEN" Borrowed %li",
                tag->hash_mem_alloc, tag->active_groups, borrowed);
            if (inherited>0) {
                rm_log_debug("and inherited %li", inherited);
            }
            rm_log_debug(" bytes for paranoid hashing");
            if (mem_required > borrowed + inherited) {
                rm_log_debug(" due to %i active group limit", active_group_threshold);
            }
            rm_log_debug("\n" RESET);

            tag->active_groups ++;
            group->is_active = TRUE;
            tag->mem_refusing=FALSE;
            group->status = RM_SHRED_GROUP_HASHING;
            result = TRUE;
        } else {
            if (!tag->mem_refusing) {
                rm_log_debug("Mem avail %li, active groups %d. "
                    RED "Refused request for %"LLU" bytes for paranoid hashing.\n" RESET,
                    tag->hash_mem_alloc, tag->active_groups, mem_required);
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

static void rm_shred_adjust_counters(RmShredDevice *device, int files, gint64 bytes) {
    g_mutex_lock(&(device->lock));
    {
        device->remaining_files += files;
        device->remaining_bytes += bytes;
    }
    g_mutex_unlock(&(device->lock));

    RmSession *session = device->main->session;
    rm_fmt_lock_state(session->formats);
    {
        session->shred_files_remaining += files;
        if(files < 0) {
            session->total_filtered_files += files;
        }
        session->shred_bytes_remaining += bytes;
        rm_fmt_set_state(session->formats, (device->after_preprocess) ?
                RM_PROGRESS_STATE_SHREDDER : RM_PROGRESS_STATE_PREPROCESS);
    }
    rm_fmt_unlock_state(session->formats);
}

static void rm_shred_write_cksum_to_xattr(RmSession *session, RmFile *file) {
    if(session->cfg->write_cksum_to_xattr) {
        if(file->has_ext_cksum == false) {
            rm_xattr_write_hash(session, file);
        }
    }
}

/* prototype functions (TODO: can we untangle the order to avoid this? */
static gboolean rm_shred_sift(RmFile *file);
static void rm_shred_push_queue_sorted(RmFile *file);

/* Hash file. Runs as threadpool in parallel / tandem with rm_shred_read_factory above
 * */
static void rm_shred_hash_factory(RmBuffer *buffer, RmMainTag *tag) {
    g_assert(tag);
    g_assert(buffer);

    if (!buffer->finished) {
        /* Hash buffer->len bytes_read of buffer->data into buffer->file */
        rm_digest_update(buffer->file->digest, buffer->data, buffer->len);
        buffer->file->hash_offset += buffer->len;
    } else {

        /* Report the progress to rm_shred_devlist_factory */
        g_assert(buffer->file->hash_offset == buffer->file->shred_group->next_offset ||
                 buffer->file->status == RM_FILE_STATE_FRAGMENT ||
                 buffer->file->status == RM_FILE_STATE_IGNORE);

        if (buffer->file->status != RM_FILE_STATE_IGNORE) {
            /* remember that checksum */
            rm_shred_write_cksum_to_xattr(tag->session, buffer->file);
        }

        if (buffer->file->devlist_waiting) {
            /* devlist factory is waiting for result */
            g_async_queue_push(buffer->file->device->hashed_file_return, buffer->file);
        } else {
            /* handle the file ourselves; devlist factory has moved on to the next file */
            if (buffer->file->status==RM_FILE_STATE_FRAGMENT) {
                rm_shred_push_queue_sorted(buffer->file);
            } else {
                rm_shred_sift(buffer->file);
            }
        }
    }

    /* Return this buffer to the pool */
    rm_buffer_pool_release(tag->mem_pool, buffer);
}

static RmShredDevice *rm_shred_device_new(gboolean is_rotational, char *disk_name,
                                          RmMainTag *main) {
    RmShredDevice *self = g_slice_new0(RmShredDevice);
    self->main = main;

    if(!rm_session_was_aborted(main->session)) {
        g_assert(self->remaining_files == 0);
        g_assert(self->remaining_bytes == 0);
    }

    self->is_rotational = is_rotational;
    self->disk_name = g_strdup(disk_name);
    self->file_queue = g_queue_new();

    self->hashed_file_return = g_async_queue_new();
    self->page_size = main->page_size;

    g_mutex_init(&(self->lock));
    g_cond_init(&(self->change));
    return self;
}

static void rm_shred_device_free(RmShredDevice *self) {
    if(!rm_session_was_aborted(self->main->session)) {
        g_assert(self->remaining_files == 0);
        g_assert(g_queue_is_empty(self->file_queue));
        g_assert(g_async_queue_length(self->hashed_file_return) == 0);
    }

    g_async_queue_unref(self->hashed_file_return);
    g_queue_free(self->file_queue);

    g_free(self->disk_name);
    g_cond_clear(&(self->change));
    g_mutex_clear(&(self->lock));

    g_slice_free(RmShredDevice, self);
}

/* Unlink RmFile from device queue
 */
static void rm_shred_discard_file(RmFile *file, bool free_file) {
    RmShredDevice *device = file->device;
    /* update device counters */
    if(device) {
        RmSession *session = device->main->session;
        rm_shred_adjust_counters(device, -1,
                                 -(gint64)(file->file_size - file->hash_offset));

        /* ShredGroup that was going nowhere */
        if(file->shred_group->num_files <= 1 && session->cfg->write_unfinished) {
            RmLintType actual_type = file->lint_type;
            file->lint_type = RM_LINT_TYPE_UNFINISHED_CKSUM;
            file->digest = (file->digest) ? file->digest : file->shred_group->digest;

            if(file->digest) {
                rm_fmt_write(file, session->formats);
                rm_shred_write_cksum_to_xattr(session, file);
                file->digest = NULL;
            }

            file->lint_type = actual_type;
        }

        /* update paranoid memory allocator */
        if(file->shred_group->digest_type == RM_DIGEST_PARANOID) {
            g_assert(file);

            RmMainTag *tag = file->shred_group->main;
            g_assert(tag);
        }
    }

    if(free_file) {
        /* toss the file (and any embedded hardlinks)*/
        rm_file_destroy(file);
    }
}

/* GCompareFunc for sorting files into optimum read order
 * */
static int rm_shred_compare_file_order(const RmFile *a, const RmFile *b,
                                       _U gpointer user_data) {
    /* compare based on partition (dev), then offset, then inode offset is a
     * RmOff, so do not substract them (will cause over or underflows on
     * regular basis) - use SIGN_DIFF instead
     */
    RmOff phys_offset_a = a->current_disk_offset;
    RmOff phys_offset_b = b->current_disk_offset;

    return (4 * SIGN_DIFF(a->dev, b->dev) +
            2 * SIGN_DIFF(phys_offset_a, phys_offset_b) +
            1 * SIGN_DIFF(a->inode, b->inode));
}

/* Populate disk_offsets table for each file, if disk is rotational
 * */
static void rm_shred_file_get_offset_table(RmFile *file, RmSession *session) {
    if(file->device->is_rotational && session->cfg->build_fiemap) {
        g_assert(!file->disk_offsets);

        RM_DEFINE_PATH(file);
        file->disk_offsets = rm_offset_create_table(file_path);
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);

        session->offsets_read++;
        if(file->disk_offsets) {
            session->offset_fragments +=
                g_sequence_get_length((GSequence *)file->disk_offsets);
        } else {
            session->offset_fails++;
        }
    }
}

/* Push file to device queue (sorted and unsorted variants)
 * Initial list build is unsorted to avoid slowing down;
 * List re-inserts during Shredding are sorted so that
 * some seeks can be avoided
 * */
static void rm_shred_push_queue_sorted_impl(RmFile *file, bool sorted) {
    RmShredDevice *device = file->device;
    g_assert(!file->digest || file->status == RM_FILE_STATE_FRAGMENT);
    g_mutex_lock(&device->lock);
    {
        if(sorted) {
            g_queue_insert_sorted(device->file_queue, file,
                                  (GCompareDataFunc)rm_shred_compare_file_order, NULL);
        } else {
            g_queue_push_head(device->file_queue, file);
        }
        g_cond_signal (&device->change);
    }
    g_mutex_unlock(&device->lock);
}

static void rm_shred_push_queue(RmFile *file) {
    rm_shred_push_queue_sorted_impl(file, false);
}

static void rm_shred_push_queue_sorted(RmFile *file) {
    rm_shred_push_queue_sorted_impl(file, true);
}

//////////////////////////////////
//    RMSHREDGROUP UTILITIES    //
//    AND SIFTING ALGORITHM     //
//////////////////////////////////

/* Free RmShredGroup and any dormant files still in its queue
 */
static void rm_shred_group_free(RmShredGroup *self, bool force_free) {
    g_assert(self->parent == NULL); /* children should outlive their parents! */

    /* For -D we need to hold back the memory a bit longer */
    bool needs_free = !(self->main->session->cfg->merge_directories) || force_free;

    if(self->held_files) {
        g_queue_foreach(self->held_files, (GFunc)rm_shred_discard_file,
                        GUINT_TO_POINTER(needs_free));
        g_queue_free(self->held_files);
        self->held_files = NULL;
    }

    rm_shred_mem_return(self);

    if(self->digest && needs_free) {
        rm_digest_free(self->digest);
        self->digest = NULL;
    }

    if(self->children) {
        g_hash_table_unref(self->children);
    }

    g_mutex_clear(&self->lock);

    g_slice_free(RmShredGroup, self);
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


/* prototype for rm_shred_group_make_orphan since it and rm_shred_group_unref reference
 * each other */
static void rm_shred_group_make_orphan(RmShredGroup *self);

/* Decrease reference count for RmShredGroup; dispose of group if reference count is 0.
 * Each group has 1 reference for group->parent and one for each file that has not
 * yet been hashed and moved to a child group.
 * So rm_shred_group_unref can be called in 2 scenarios:
 * 1. One of self's files is hashed and sent to rm_shred_sift, from where it is
 *    moved into a new group.
 * 2. Last file in self->parent group is sent to rm_shred_sift and is moved to a new group
 *    (which may or may not be self).  Then rm_shred_sift calls rm_shred_group_unref(self->parent)
 *    whereupon self->parent->ref_count will reach 0 and so rm_shred_group_unref will
 *    call rm_shred_group_make_orphan(self) which in turn calls rm_shred_group_unref(self).
 * Mutex management:
 * In case (1), multiple threads may be sending files to rm_shred_sift which report to self. So
 * rm_shred_group_unref locks self->lock before doing anything.
 * In case (2), to prevent threadlock, need to unlock self->lock before
 * calling rm_shred_group_unref(self->parent)
 * */
static void rm_shred_group_unref(RmShredGroup *self) {
    bool needs_free = FALSE;
    bool unref_parent = FALSE;
    bool send_results = FALSE;

    g_mutex_lock(&self->lock);
    {
        g_assert(self->ref_count>0);
        self->ref_count--;
        if (self->ref_count == 0) {
            rm_shred_mem_return(self);
        }

        switch(self->status) {
        case RM_SHRED_GROUP_DORMANT:
            /* group is not going to receive any more files; do required clean-up */
            needs_free = TRUE;
            unref_parent = TRUE;
            break;
        case RM_SHRED_GROUP_FINISHING:
            /* groups is finished, and meets criteria for a duplicate group; send it to
             * finisher */
            /* note result_pool thread takes responsibility for cleanup of this group after
             * processing results */
            g_assert(self->children == NULL);
            send_results = TRUE;
            break;
        case RM_SHRED_GROUP_START_HASHING:
        case RM_SHRED_GROUP_HASHING:
            if(self->ref_count == 0) {
                /* group no longer required; tell the children we are about to die */
                if(self->children) {
                    GList *values = g_hash_table_get_values(self->children);
                    g_list_foreach(values, (GFunc)rm_shred_group_make_orphan, NULL);
                    g_list_free(values);
                }
                unref_parent=TRUE;
                needs_free=TRUE;
            }
            break;
        case RM_SHRED_GROUP_FINISHED:
        default:
            g_assert_not_reached();
        }
    }
    g_mutex_unlock(&self->lock);

    if (unref_parent && self->parent) {
        rm_shred_group_unref(self->parent);
    }

    if (send_results) {
        rm_util_thread_pool_push(self->main->result_pool, self);
    }

    if (needs_free) {
#if _RM_SHRED_DEBUG
        rm_log_debug("Free from rm_shred_group_unref\n");
#endif
        rm_shred_group_free(self, false);
    }
}

/* Only called by rm_shred_sift() or by rm_shred_group_unref. Call with group->lock unlocked.
 */
static void rm_shred_group_make_orphan(RmShredGroup *self) {
    /* parent is dead */
    self->parent = NULL;

    /* reduce reference count for self and free self if possible */
    rm_shred_group_unref(self);
}

/* Call with shred_group->lock unlocked.
 * */
static gboolean rm_shred_group_push_file(RmShredGroup *shred_group, RmFile *file,
                                         gboolean initial) {
    gboolean result = false;
    file->shred_group = shred_group;

    if(file->digest) {
        rm_digest_free(file->digest);
        file->digest = NULL;
    }

    g_mutex_lock(&shred_group->lock);
    {

        shred_group->has_pref |= file->is_prefd | file->hardlinks.has_prefd;
        shred_group->has_npref |= !file->is_prefd | file->hardlinks.has_non_prefd;
        shred_group->has_new |= file->is_new_or_has_new;

        shred_group->ref_count++;
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
                g_queue_free_full(shred_group->held_files,
                                  (GDestroyNotify)(initial ? rm_shred_push_queue
                                                           : rm_shred_push_queue_sorted));
                shred_group->held_files = NULL; /* won't need shred_group queue any more,
                                                   since new arrivals will bypass */
            }
            if (shred_group->digest_type == RM_DIGEST_PARANOID) {
                rm_shred_check_hash_mem_alloc(shred_group, 1);
            }
        /* FALLTHROUGH */
        case RM_SHRED_GROUP_HASHING:
            if(initial || !file->devlist_waiting) {
                /* add file to device queue */
                g_assert(file->device);
                rm_shred_push_queue(file);
            } else {
                /* calling routine will handle the file */
                result = true;
            }
            break;
        case RM_SHRED_GROUP_DORMANT:
            g_queue_push_head(shred_group->held_files, file);
            break;
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
static gboolean rm_shred_sift(RmFile *file) {
    gboolean result = FALSE;
    g_assert(file);
    g_assert(file->shred_group);

    RmShredGroup *child_group = NULL;
    RmShredGroup *current_group = file->shred_group;

    if(file->status == RM_FILE_STATE_IGNORE) {
        rm_digest_free(file->digest);
        rm_shred_discard_file(file, true);
    } else {
        g_assert(file->digest);

        if(file->digest->type == RM_DIGEST_PARANOID && !file->is_symlink) {
            g_assert(file->digest->bytes ==
                     current_group->next_offset - current_group->hash_offset);
        }

        /* check if there is already a descendent of current_group which
         * matches snap... if yes then move this file into it; if not then
         * create a new group */
        g_mutex_lock(&current_group->lock);
        {
            if(!current_group->children) {
                /* create child queue */
                current_group->children = g_hash_table_new((GHashFunc)rm_digest_hash,
                                                           (GEqualFunc)rm_digest_equal);
            }
            g_assert(current_group->children != NULL);
            child_group = g_hash_table_lookup(current_group->children, file->digest);

            if(!child_group) {
                child_group = rm_shred_group_new(file);
                g_hash_table_insert(current_group->children, child_group->digest,
                                    child_group);
                child_group->has_only_ext_cksums = current_group->has_only_ext_cksums;
            }

        }
        g_mutex_unlock(&current_group->lock);
        result = rm_shred_group_push_file(child_group, file, FALSE);

    }

    /* current_group now has one less file to process */
    rm_shred_group_unref(current_group);
    return result;
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
static void rm_shred_file_preprocess(_U gpointer key, RmFile *file, RmMainTag *main) {
    /* initial population of RmShredDevice's and first level RmShredGroup's */
    RmSession *session = main->session;

    g_assert(file);
    g_assert(session->tables->dev_table);
    g_assert(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE);
    g_assert(file->file_size > 0);

    file->is_new_or_has_new = (file->mtime >= session->cfg->min_mtime);

    /* if file has hardlinks then set file->hardlinks.has_[non_]prefd*/
    if(file->hardlinks.is_head) {
        for(GList *iter = file->hardlinks.files->head; iter; iter = iter->next) {
            RmFile *link = iter->data;
            file->hardlinks.has_non_prefd |= !link->is_prefd;
            file->hardlinks.has_prefd |= link->is_prefd;
            file->is_new_or_has_new |= (link->mtime >= session->cfg->min_mtime);
        }
    }

    /* create RmShredDevice for this file if one doesn't exist yet */
    RM_DEFINE_PATH(file);
    dev_t disk = rm_mounts_get_disk_id(session->mounts, file->dev, file_path);
    RmShredDevice *device =
        g_hash_table_lookup(session->tables->dev_table, GUINT_TO_POINTER(disk));

    if(device == NULL) {
        rm_log_debug(GREEN "Creating new RmShredDevice for disk %u\n" RESET,
                     (unsigned)disk);
        device = rm_shred_device_new(!rm_mounts_is_nonrotational(session->mounts, disk),
                                     rm_mounts_get_disk_name(session->mounts, disk),
                                     main);
        device->disk = disk;
        g_hash_table_insert(session->tables->dev_table, GUINT_TO_POINTER(disk), device);
    }

    file->device = device;

    rm_shred_adjust_counters(device, 1, (gint64)file->file_size);

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

    if(HAS_CACHE(session) &&
       rm_trie_search(&session->cfg->file_trie, file_path)) {
        group->num_ext_cksums += 1;
        file->has_ext_cksum = 1;
    }
}

static gboolean rm_shred_group_preprocess(_U gpointer key, RmShredGroup *group) {
    g_assert(group);
    if(group->status == RM_SHRED_GROUP_DORMANT) {
        rm_shred_group_free(group, FALSE);
        return true;
    } else {
        return false;
    }
}

static void rm_shred_device_preprocess(_U gpointer key, RmShredDevice *device,
                                       RmMainTag *main) {
    g_mutex_lock(&device->lock);
    g_queue_foreach(device->file_queue, (GFunc)rm_shred_file_get_offset_table,
                    main->session);
    g_mutex_unlock(&device->lock);
}

static void rm_shred_preprocess_input(RmMainTag *main) {
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
    g_hash_table_foreach_remove(
        session->tables->node_table, (GHRFunc)rm_shred_file_preprocess, main);
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
    removed = g_hash_table_foreach_remove(
        session->tables->size_groups, (GHRFunc)rm_shred_group_preprocess, main);
    g_hash_table_unref(session->tables->size_groups);
    session->tables->size_groups = NULL;

    rm_log_debug("done at time %.3f; removed %u of %" LLU "\n",
                 g_timer_elapsed(session->timer, NULL), removed,
                 session->total_filtered_files);

    rm_log_debug("Looking up fiemap data for files on rotational devices...");
    g_hash_table_foreach(session->tables->dev_table, (GHFunc)rm_shred_device_preprocess,
                         main);
    rm_log_debug("done at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    rm_log_debug("fiemap'd %" LLU " files containing %" LLU
                 " fragments (failed another %" LLU " files)\n",
                 session->offsets_read - session->offset_fails, session->offset_fragments,
                 session->offset_fails);
}

/////////////////////////////////
//       POST PROCESSING       //
/////////////////////////////////

/* post-processing sorting of files by criteria (-S and -[kmKM])
 * this is slightly different to rm_shred_cmp_orig_criteria in the case of
 * either -K or -M options
 */
static int rm_shred_cmp_orig_criteria(RmFile *a, RmFile *b, RmSession *session) {
    RmCfg *cfg = session->cfg;
    if(1 && (a->is_prefd != b->is_prefd) &&
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
    g_queue_foreach(group, (GFunc)rm_fmt_write, session->formats);
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

static void rm_shred_result_factory(RmShredGroup *group, RmMainTag *tag) {
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
            file->free_digest = false;
            if(tag->session->cfg->merge_directories) {
                rm_tm_feed(tag->session->dir_merger, file);
            }
        }

        if(tag->session->cfg->merge_directories == false) {
            /* Output them directly */
            rm_shred_forward_to_output(tag->session, group->held_files);
        }
    }
    group->status = RM_SHRED_GROUP_FINISHED;
#if _RM_SHRED_DEBUG
    rm_log_debug("Free from rm_shred_result_factory\n");
#endif
    rm_shred_group_free(group, false);
}

/////////////////////////////////
//    ACTUAL IMPLEMENTATION    //
/////////////////////////////////

static void rm_shred_request_readahead(int fd, RmFile *file, RmOff bytes_to_read) {
    /* Give the kernel scheduler some hints */
    if(file->fadvise_requested) {
        RmOff readahead = MIN(file->file_size - file->seek_offset, bytes_to_read * 8);
        posix_fadvise(fd, file->seek_offset, readahead, SHRED_FADVISE_FLAGS);
        file->fadvise_requested = 1;
    }
}

static void rm_shred_readlink_factory(RmFile *file, RmShredDevice *device) {
    g_assert(file->is_symlink);

    /* Fake an IO operation on the symlink.
     */
    char id_buf[256];
    memset(id_buf, 0, sizeof(id_buf));
    RM_DEFINE_PATH(file);

    RmStat stat_buf;
    if(rm_sys_stat(file_path, &stat_buf) == -1) {
        /* Oops, that did not work out, report as an error */
        rm_log_perror("Cannot stat symbolic link");
        file->status = RM_FILE_STATE_IGNORE;
        return;
    }

    file->status = RM_FILE_STATE_NORMAL;
    file->seek_offset = file->file_size;
    file->hash_offset = file->file_size;

    g_assert(file->digest);

    gint data_size = snprintf(id_buf, sizeof(id_buf), "%ld:%ld", (long)stat_buf.st_dev,
                              (long)stat_buf.st_ino);

    rm_digest_update(file->digest, (unsigned char *)id_buf, data_size);

    /* In case of paranoia: shrink the used data buffer, so comparasion works
     * as expected. Otherwise a full buffer is used with possibly different
     * content */
    if(file->digest->type == RM_DIGEST_PARANOID) {
        rm_digest_paranoia_shrink(file->digest, data_size);
    }

    rm_shred_adjust_counters(device, 0, -(gint64)file->file_size);
}

static void rm_shred_buffered_read_factory(RmFile *file, RmShredDevice *device) {
    FILE *fd = NULL;
    gint32 total_bytes_read = 0;

    gint32 buf_size = rm_buffer_pool_size(device->main->mem_pool);
    buf_size -= offsetof(RmBuffer, data);

    RmBuffer *buffer = rm_buffer_pool_get(device->main->mem_pool);

    GThreadPool *hash_pool = g_async_queue_pop(device->main->hash_pool_pool);

    if(file->seek_offset >= file->file_size) {
        goto finish;
    }

    RM_DEFINE_PATH(file);

    if((fd = fopen(file_path, "rb")) == NULL) {
        file->status = RM_FILE_STATE_IGNORE;
        rm_log_info("fopen(3) failed for %s: %s", file_path, g_strerror(errno));
        goto finish;
    }

    gint32 bytes_to_read = rm_shred_get_read_size(file, device->main);
    gint32 bytes_read = 0;

    rm_shred_request_readahead(fileno(fd), file, bytes_to_read);

    if(fseek(fd, file->seek_offset, SEEK_SET) == -1) {
        file->status = RM_FILE_STATE_IGNORE;
        rm_log_perror("fseek(3) failed");
        goto finish;
    }

    posix_fadvise(fileno(fd), file->seek_offset, bytes_to_read, SHRED_FADVISE_FLAGS);

    while((bytes_read = fread(buffer->data, 1, MIN(bytes_to_read, buf_size), fd)) > 0) {
        file->seek_offset += bytes_read;
        bytes_to_read -= bytes_read;

        buffer->file = file;
        buffer->len = bytes_read;
        buffer->finished = FALSE;

        rm_util_thread_pool_push(hash_pool, buffer);

        total_bytes_read += bytes_read;
        buffer = rm_buffer_pool_get(device->main->mem_pool);
    }

    file->current_disk_offset = rm_offset_lookup(file->disk_offsets, file->seek_offset);

    if(ferror(fd) != 0) {
        file->status = RM_FILE_STATE_IGNORE;
        rm_log_perror("fread(3) failed");
    }

finish:
    if(fd != NULL) {
        fclose(fd);
    }

    /* tell the hasher we have finished */
    buffer->file = file;
    buffer->finished = TRUE;
    rm_util_thread_pool_push(hash_pool, buffer);

    /* recycle our hash_pool */
    g_async_queue_push(device->main->hash_pool_pool, hash_pool);

    /* Update totals for device and session*/
    rm_shred_adjust_counters(device, 0, -(gint64)total_bytes_read);
}

/* Read from file and send to hasher
 * Note this was initially a separate thread but is currently just called
 * directly from rm_devlist_factory.
 * */
static void rm_shred_unbuffered_read_factory(RmFile *file, RmShredDevice *device) {
    gint32 bytes_read = 0;
    gint32 total_bytes_read = 0;

    GThreadPool *hash_pool = g_async_queue_pop(device->main->hash_pool_pool);

    RmOff buf_size = rm_buffer_pool_size(device->main->mem_pool);
    buf_size -= offsetof(RmBuffer, data);

    gint32 bytes_to_read = rm_shred_get_read_size(file, device->main);
    gint32 bytes_left_to_read = bytes_to_read;

    g_assert(!file->is_symlink);
    g_assert(bytes_to_read > 0);
    g_assert(bytes_to_read + file->hash_offset <= file->file_size);
    g_assert(file->seek_offset == file->hash_offset);

    /* how many buffers to read? */
    const gint16 N_BUFFERS = MIN(4, DIVIDE_CEIL(bytes_to_read, buf_size));
    struct iovec readvec[N_BUFFERS + 1];

    int fd = 0;

    if(file->seek_offset >= file->file_size) {
        goto finish;
    }

    RM_DEFINE_PATH(file);

    fd = rm_sys_open(file_path, O_RDONLY);
    if(fd == -1) {
        rm_log_info("open(2) failed for %s: %s", file_path, g_strerror(errno));
        file->status = RM_FILE_STATE_IGNORE;
        goto finish;
    }

    /* preadv() is beneficial for large files since it can cut the
     * number of syscall heavily.  I suggest N_BUFFERS=4 as good
     * compromise between memory and cpu.
     *
     * With 16 buffers: 43% cpu 33,871 total
     * With  8 buffers: 43% cpu 32,098 total
     * With  4 buffers: 42% cpu 32,091 total
     * With  2 buffers: 44% cpu 32,245 total
     * With  1 buffers: 45% cpu 34,491 total
     */

    /* Give the kernel scheduler some hints */
    rm_shred_request_readahead(fd, file, bytes_to_read);

    /* Initialize the buffers to begin with.
     * After a buffer is full, a new one is retrieved.
     */
    memset(readvec, 0, sizeof(readvec));
    for(int i = 0; i < N_BUFFERS; ++i) {
        /* buffer is one contignous memory block */
        RmBuffer *buffer = rm_buffer_pool_get(device->main->mem_pool);
        readvec[i].iov_base = buffer->data;
        readvec[i].iov_len = buf_size;
    }

    while(bytes_left_to_read > 0 &&
          (bytes_read = rm_sys_preadv(fd, readvec, N_BUFFERS, file->seek_offset)) > 0) {
        bytes_read = MIN(bytes_read, bytes_left_to_read); /* ignore over-reads */
        int blocks = DIVIDE_CEIL(bytes_read, buf_size);

        bytes_left_to_read -= bytes_read;
        file->seek_offset += bytes_read;
        total_bytes_read += bytes_read;

        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
            buffer->file = file;
            buffer->len = MIN(buf_size, bytes_read - i * buf_size);
            buffer->finished = FALSE;
            if(bytes_left_to_read < 0) {
                rm_log_error_line(_("Negative bytes_left_to_read for %s"), file_path);
            }

            /* Send it to the hasher */
            rm_util_thread_pool_push(hash_pool, buffer);

            /* Allocate a new buffer - hasher will release the old buffer */
            buffer = rm_buffer_pool_get(device->main->mem_pool);
            readvec[i].iov_base = buffer->data;
            readvec[i].iov_len = buf_size;
        }
    }

    file->current_disk_offset = rm_offset_lookup(file->disk_offsets, file->seek_offset);

    if(bytes_read == -1) {
        rm_log_perror("preadv failed");
        file->status = RM_FILE_STATE_IGNORE;
        goto finish;
    } else if(total_bytes_read != bytes_to_read) {
        rm_log_error_line(_("Something went wrong reading %s; expected %d bytes, "
                            "got %d; ignoring"),
                          file_path, bytes_to_read, total_bytes_read);
        file->status = RM_FILE_STATE_IGNORE;
    }

    /* Release the rest of the buffers */
    for(int i = 0; i < N_BUFFERS; ++i) {
        RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
        rm_buffer_pool_release(device->main->mem_pool, buffer);
    }
finish:
    if(fd > 0) {
        rm_sys_close(fd);
    }

    /* tell the hasher we have finished via a dummy buffer*/
    RmBuffer *buffer = rm_buffer_pool_get(device->main->mem_pool);
    buffer->file = file;
    buffer->finished = TRUE;
    rm_util_thread_pool_push(hash_pool, buffer);

    /* recycle our hash_pool */
    g_async_queue_push(device->main->hash_pool_pool, hash_pool);

    /* Update totals for device and session*/
    rm_shred_adjust_counters(device, 0, -(gint64)total_bytes_read);
}

static bool rm_shred_reassign_checksum(RmMainTag *main, RmFile *file) {
    bool can_process = true;

    if(file->shred_group->digest_type == RM_DIGEST_PARANOID) {
        /* check if memory allocation is ok */
        if(!rm_shred_check_hash_mem_alloc(file->shred_group, 0)) {
            can_process = false;
        } else {
            /* get the required target offset into file->shred_group->next_offset, so
                * that we can make the paranoid RmDigest the right size*/
            if(file->shred_group->next_offset == 0) {
                (void)rm_shred_get_read_size(file, main);
            }
            g_assert(file->shred_group->hash_offset == file->hash_offset);

            if(file->is_symlink) {
                file->digest = rm_digest_new(main->session->cfg->checksum_type, 0, 0,
                                             PATH_MAX + 1 /* max size of a symlink file */
                                             );
            } else {
                file->digest =
                    rm_digest_new(main->session->cfg->checksum_type, 0, 0,
                                  file->shred_group->next_offset - file->hash_offset);
            }
        }
    } else if(file->shred_group->digest) {
        /* pick up the digest-so-far from the RmShredGroup */
        file->digest = rm_digest_copy(file->shred_group->digest);
    } else if(file->shred_group->has_only_ext_cksums) {
        /* Cool, we were able to read the checksum from disk */
        file->digest = rm_digest_new(RM_DIGEST_EXT, 0, 0, 0);

        RM_DEFINE_PATH(file);

        char *hexstring = file->folder->data;

        if(hexstring != NULL) {
            rm_digest_update(file->digest, (unsigned char *)hexstring, strlen(hexstring));
            rm_log_debug("%s=%s was read from cache.\n", hexstring, file_path);
        } else {
            rm_log_warning_line(
                "Unable to read external checksum from interal cache for %s", file_path);
            file->has_ext_cksum = 0;
            file->shred_group->has_only_ext_cksums = 0;
        }
    } else {
        /* this is first generation of RMGroups, so there is no progressive hash yet */
        file->digest = rm_digest_new(main->session->cfg->checksum_type,
                                     main->session->hash_seed1,
                                     main->session->hash_seed2,
                                     0);
    }

    return can_process;
}

#define RM_SHRED_TOO_MANY_BYTES_TO_WAIT ( 64 * 1024 * 1024 )

static RmFile *rm_shred_process_file(RmShredDevice *device, RmFile *file) {
    if(file->shred_group->has_only_ext_cksums) {
        rm_shred_adjust_counters(device, 0, -(gint64)file->file_size);
        return file;
    }

    bool worth_waiting=FALSE;

    g_mutex_lock(&file->shred_group->lock);
    {
        worth_waiting = device->main->session->cfg->shred_always_wait
            || ( device->is_rotational
                && file->seek_offset + rm_shred_get_read_size(file, device->main) != file->file_size
                && rm_shred_get_read_size(file, device->main) < RM_SHRED_TOO_MANY_BYTES_TO_WAIT
                && (file->status == RM_FILE_STATE_NORMAL)
                && !device->main->session->cfg->shred_never_wait
            );
    }
    g_mutex_unlock(&file->shred_group->lock);

    /* hash the next increment of the file */
    file->devlist_waiting= worth_waiting;
    if(file->is_symlink) {
        rm_shred_readlink_factory(file, device);
    } else {
        if(SHRED_USE_BUFFERED_READ) {
            rm_shred_buffered_read_factory(file, device);
        } else {
            rm_shred_unbuffered_read_factory(file, device);
        }

        if (worth_waiting) {
            /* wait until the increment has finished hashing */
            g_assert (file == g_async_queue_pop(device->hashed_file_return));
        } else {
            file = NULL;
        }
    }

    return file;
}

static bool rm_shred_can_process(RmFile *file, RmMainTag *main) {
    /* initialise hash (or recover progressive hash so far) */
    bool result = TRUE;
    g_assert(file->shred_group);
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



static void rm_shred_devlist_factory(RmShredDevice *device, RmMainTag *main) {
    GList *iter = NULL;

    g_assert(device);

    g_mutex_lock(&device->lock);
    {
        rm_log_debug(BLUE
                     "Started rm_shred_devlist_factory for disk %s (%u:%u) with %" LLU
                     " files in queue\n" RESET,
                     device->disk_name,
                     major(device->disk),
                     minor(device->disk),
                     (RmOff)g_queue_get_length(device->file_queue));

        if(device->is_rotational) {
            g_queue_sort(device->file_queue,
                         (GCompareDataFunc)rm_shred_compare_file_order, NULL);
        }

        if(g_queue_get_length(device->file_queue) == 0 && device->remaining_files > 0) {
            /* give the other device threads a chance to catch up, which will hopefully
             * release held files from RmShredGroups to give us some work to do */
            gint64 end_time = g_get_monotonic_time () + SHRED_EMPTYQUEUE_SLEEP_US;
            g_cond_wait_until (&device->change, &device->lock, end_time);
        }
        iter = device->file_queue->head;
    }
    g_mutex_unlock(&device->lock);

    /* scheduler for one file at a time, optimised to minimise seeks */
    while(iter && !rm_session_was_aborted(main->session)) {
        RmFile *file = iter->data;
        gboolean can_process = rm_shred_can_process(file, main);

        /* remove current iter from queue and move to next in preparation for next file*/
        g_mutex_lock(&device->lock);
        {
            GList *tmp = iter;
            iter = iter->next;
            if(can_process) {
                /* file will be processed */
                g_queue_delete_link(device->file_queue, tmp);
            }
        }
        g_mutex_unlock(&device->lock);

        while(can_process) {
            can_process = FALSE;
            RmOff start_offset = file->hash_offset;

            file = rm_shred_process_file(device, file);
            if (file) {

                if(start_offset == file->hash_offset && file->has_ext_cksum == false) {
                    rm_log_debug(RED "Offset stuck at %" LLU "\n" RESET, start_offset);
                    file->status = RM_FILE_STATE_IGNORE;
                    /* rm_shred_sift will dispose of the file */
                }

                if(file->status == RM_FILE_STATE_FRAGMENT) {
                    /* file is not ready for checking yet; push it back into the queue */
#if _RM_SHRED_DEBUG
                    RM_DEFINE_PATH(file);
                    rm_log_debug("Recycling fragment %s\n", file_path);
#endif
                    rm_shred_push_queue_sorted(file); /* call with device unlocked */
                } else if(rm_shred_sift(file)) {
                    /* continue hashing same file, ie no change to iter */
#if _RM_SHRED_DEBUG
                    RM_DEFINE_PATH(file);
                    rm_log_debug("Continuing to next generation %s\n", file_path);
#endif
                    can_process = rm_shred_can_process(file, main);
                    if (!can_process) {
                        /* put file back in queue */
                        rm_shred_push_queue(file);
                    }
                    continue;
                } else {
                    /* rm_shred_sift has taken responsibility for the file and either disposed
                     * of it or pushed it back into our queue */
                }
            }
        }
    }

    /* threadpool thread terminates but the device will be recycled via
     * the device_return queue
     */
    rm_log_debug(BLUE "Pushing device back to main joiner %d\n" RESET, (int)device->disk);
    g_async_queue_push(main->device_return, device);
}

static void rm_shred_create_devpool(RmMainTag *tag, GHashTable *dev_table) {
    tag->device_pool = rm_util_thread_pool_new((GFunc)rm_shred_devlist_factory, tag,
                    MIN(tag->session->cfg->threads / 2 + 1, g_hash_table_size(dev_table)));

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, dev_table);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        RmShredDevice *device = value;
        device->after_preprocess = true;
        g_queue_sort(device->file_queue, (GCompareDataFunc)rm_shred_compare_file_order,
                     NULL);
        rm_log_debug(GREEN "Pushing device %s to threadpool\n", device->disk_name);
        rm_util_thread_pool_push(tag->device_pool, device);
    }
}

static void hash_pool_free(GThreadPool *hash_pool) {
    g_thread_pool_free(hash_pool, FALSE, TRUE);
}


void rm_shred_run(RmSession *session) {
    g_assert(session);
    g_assert(session->tables);
    g_assert(session->mounts);

    RmMainTag tag;
    tag.active_groups = 0;
    tag.hash_mem_alloc = session->cfg->paranoid_mem;
    tag.session = session;

    /* Do not rely on sizeof(RmBuffer), compiler might add padding. */
    tag.mem_pool = rm_buffer_pool_init(offsetof(RmBuffer, data) + SHRED_PAGE_SIZE);
    tag.device_return = g_async_queue_new();
    tag.page_size = SHRED_PAGE_SIZE;

    /* would use g_atomic, but helgrind does not like that */
    g_mutex_init(&tag.hash_mem_mtx);

    session->tables->dev_table = g_hash_table_new_full(
        g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)rm_shred_device_free);

    rm_shred_preprocess_input(&tag);
    session->shred_bytes_after_preprocess = session->shred_bytes_remaining;

    /* Remember how many devlists we had - so we know when to stop */
    int devices_left = g_hash_table_size(session->tables->dev_table);
    rm_log_debug(BLUE "Devices = %d\n", devices_left);

    /* Create a pool for results processing */
    tag.result_pool = rm_util_thread_pool_new((GFunc)rm_shred_result_factory, &tag, 1);

    /* Create a pool of hashing threadpools */
    tag.hash_pool_pool = g_async_queue_new_full((GDestroyNotify)hash_pool_free);
    g_assert(session->cfg->threads > 0);
    for(uint i=0; i < session->cfg->threads / 2 + 1; i++) {
        g_async_queue_push(tag.hash_pool_pool, rm_util_thread_pool_new((GFunc)rm_shred_hash_factory, &tag, 1));
    }

    /* Create a pool fo the devlists and push each queue */
    rm_shred_create_devpool(&tag, session->tables->dev_table);

    /* This is the joiner part */
    while(devices_left > 0 || g_async_queue_length(tag.device_return) > 0) {
        RmShredDevice *device = g_async_queue_pop(tag.device_return);
        g_mutex_lock(&device->lock);
        g_mutex_lock(&tag.hash_mem_mtx);
        {
            /* probably unnecessary because we are only reading */
            rm_log_debug(BLUE "Got device %s back with %d in queue and %" LLU
                              " bytes remaining in %d remaining files; active groups %d "
                              "and avail mem %" LLU "\n" RESET,
                         device->disk_name,
                         g_queue_get_length(device->file_queue),
                         device->remaining_bytes,
                         device->remaining_files,
                         tag.active_groups,
                         tag.hash_mem_alloc);

            if(device->remaining_files > 0) {
                /* recycle the device */
                rm_util_thread_pool_push(tag.device_pool, device);
            } else {
                devices_left--;
            }
        }
        g_mutex_unlock(&tag.hash_mem_mtx);
        g_mutex_unlock(&device->lock);

        if(rm_session_was_aborted(session)) {
            break;
        }
    }

    g_async_queue_unref(tag.hash_pool_pool);

    session->shredder_finished = TRUE;
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_SHREDDER);

    /* This should not block, or at least only very short. */
    g_thread_pool_free(tag.device_pool, FALSE, TRUE);
    g_thread_pool_free(tag.result_pool, FALSE, TRUE);

    g_async_queue_unref(tag.device_return);
    rm_buffer_pool_destroy(tag.mem_pool);

    g_hash_table_unref(session->tables->dev_table);
    g_mutex_clear(&tag.hash_mem_mtx);
}
