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

/* Minimum number of pages to read */
#define SHRED_MIN_READ_PAGES (1)

////////////////////////////////////////////
// OPTIMISATION PARAMETERS FOR DECIDING   //
// HOW MANY BYTES TO READ BEFORE STOPPING //
// TO COMPARE PROGRESSIVE HASHES          //
////////////////////////////////////////////

/* How many milliseconds to sleep if we encounter an empty file queue.
 * This prevents a "starving" RmShredDevice from hogging cpu by continually
 * recycling back to the joiner.
 */
#define SHRED_EMPTYQUEUE_SLEEP_US (10 * 1000) /* 10ms */

/* expected typical seek time in milliseconds - used to calculate optimum read*/
#define SHRED_SEEK_MS (10)
/** Note that 15 ms this would be appropriate value for random reads
 * but for short seeks in the 1MB to 1GB range , 5ms is more appropriate; refer:
 * https://www.usenix.org/legacy/event/usenix09/tech/full_papers/vandebogart/vandebogart_html/index.html
 * But it's better to have this value a bit too high rather than a bit too low...
 */

/* expected typical sequential read speed in MB per second */
#define SHRED_READRATE (100)

/* define what we consider cheap as a fraction of total cost, for example a cheap read has
 * a read time less than 1/50th the seek time, or a cheap seek has a seek time less then 1/50th
 * the total read time */
#define SHRED_CHEAP (50)

/* how many pages can we read in (seek_time)? (eg 5ms seeking folled by 5ms reading) */
#define SHRED_BALANCED_READ_BYTES (SHRED_SEEK_MS * SHRED_READRATE * 1024) /* ( * 1024 / 1000 to be exact) */

/* how many bytes do we have to read before seek_time becomes CHEAP relative to read time? */
#define SHRED_CHEAP_SEEK_BYTES (SHRED_BALANCED_READ_BYTES * SHRED_CHEAP)

/* how many pages can we read in (seek_time)/(CHEAP)? (use for initial read) */
#define SHRED_CHEAP_READ_BYTES (SHRED_BALANCED_READ_BYTES / SHRED_CHEAP)

/** for reference, if SEEK_MS=5, READRATE=100 and CHEAP=50 then BALANCED_READ_BYTES=0.5MB,
 * CHEAP_SEEK_BYTES=25MB and CHEAP_READ_BYTES=10kB. */

/* Maximum number of bytes to read in one pass.
 * Never goes beyond SHRED_MAX_READ_SIZE unless the extra bytes to finish the file are "cheap".
 * Never goes beyond SHRED_HARD_MAX_READ_SIZE.
 */
#define SHRED_MAX_READ_SIZE   (512 * 1024 * 1024)
#define SHRED_HARD_MAX_READ_SIZE   (1024 * 1024 * 1024)

/* How many pages to use during paranoid byte-by-byte comparison?
 * More pages use more memory but result in less syscalls.
 */
#define SHRED_PARANOIA_PAGES  (64)

/* How much buffers to keep allocated at max. */
#define SHRED_MAX_PAGES       (64)

/* How large a single page is (typically 4096 bytes but not always)*/
#define SHRED_PAGE_SIZE       (sysconf(_SC_PAGESIZE))

/* How many pages to read in each generation?
 * This value is important since it decides how much data will be read for small files,
 * so it should not be too large nor too small, since reading small files twice is very slow.
 */


/* Flags for the fadvise() call that tells the kernel
 * what we want to do with the file.
 */
#define SHRED_FADVISE_FLAGS   (0                                                         \
                               | POSIX_FADV_SEQUENTIAL /* Read from 0 to file-size    */ \
                               | POSIX_FADV_WILLNEED   /* Tell the kernel to readhead */ \
                               | POSIX_FADV_NOREUSE    /* We will not reuse old data  */ \
                              )                                                          \
 
////////////////////////
//  MATHS SHORTCUTS   //
////////////////////////

#define DIVIDE_CEIL(n, m) ((n) / (m) + !!((n) % (m)))
#define SIGN_DIFF(X, Y) (((X) > (Y)) - ((X) < (Y)))  /* handy for comparing unit64's */

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

    /* is this the last buffer of the current increment? */
    bool is_last;

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
    GMutex group_lock;     /* single lock for all access to any RmShredGroups */
    gint64 hash_mem_alloc; /* how much memory to allocate for paranoid checks */
    gint32 active_files;   /* how many files active (only used with paranoid a.t.m.) */
    GThreadPool *device_pool;
    GThreadPool *result_pool;
    gint32 page_size;
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

    /* Lock for all of the above */
    GMutex lock;

    /* disk type; allows optimisation of parameters for rotational or non- */
    bool is_rotational;

    /* Pool for the hashing workers */
    GThreadPool *hash_pool;

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

#define NEEDS_PREF(group)  (group->main->session->cfg->must_match_tagged   || group->main->session->cfg->keep_all_untagged )
#define NEEDS_NPREF(group) (group->main->session->cfg->must_match_untagged || group->main->session->cfg->keep_all_tagged)
#define NEEDS_NEW(group)   (group->main->session->cfg->min_mtime)

#define HAS_CACHE(session) (session->cfg->read_cksum_from_xattr || session->cache_list.length)

typedef struct RmShredGroup {
    /* holding queue for files; they are held here until the group first meets
     * criteria for further hashing (normally just 2 or more files, but sometimes
     * related to preferred path counts)
     * */
    GQueue *held_files;

    /* link(s) to next generation of RmShredGroups(s) which have this RmShredGroup as parent*/
    GHashTable *children;

    /* RmShredGroup of the same size files but with lower RmFile->hash_offset;
     * getsset to null when parent dies
     * */
    struct RmShredGroup *parent;

    /* reference count (reasons for keeping group alive):
     *   1 for the parent
     *   1 for each file that hasn't moved into a child group yet (which it can't do until it has hashed the next increment) */
    gulong ref_count;

    /* number of files */
    gulong num_files;

    /* set if group has 1 or more files from "preferred" paths */
    bool has_pref;

    /* set if group has 1 or more files from "non-preferred" paths */
    bool has_npref;

    /* set if group has 1 or more files newer than cfg->min_mtime */
    bool has_new;

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

    /* checksum structure taken from first file to enter the group.  This allows
     * digests to be released from RmFiles and memory freed up until they
     * are required again for further hashing.*/
    RmDigestType digest_type;
    RmDigest *digest;

    /* Reference to main */
    RmMainTag *main;
} RmShredGroup;

/////////// RmShredGroup ////////////////

/* allocate and initialise new RmShredGroup
 */
static RmShredGroup *rm_shred_group_new(RmFile *file) {
    RmShredGroup *self = g_slice_new0(RmShredGroup);

    if (file->digest) {
        self->digest = rm_digest_copy(file->digest);
        self->digest_type = file->digest->type;
    } else {
        /* initial groups have no checksum */
    }

    self->parent = file->shred_group;

    if(self->parent) {
        self->ref_count++;
    }

    self->held_files = g_queue_new();
    self->file_size = file->file_size;
    self->hash_offset = file->hash_offset;

    g_assert(file->device->main);
    self->main = file->device->main;

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
    g_mutex_lock(&pool->lock); {
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
    g_mutex_lock(&pool->lock); {
        if (!pool->stack) {
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
    g_mutex_lock(&pool->lock); {
        g_trash_stack_push(&pool->stack, buf);
    }
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

    /* calculate next_offset property of the RmShredGroup, if not already done */
    if (group->next_offset == 0) {
        RmOff target_bytes = (group->hash_offset == 0) ? SHRED_CHEAP_READ_BYTES : SHRED_CHEAP_SEEK_BYTES;

        /* round to even number of pages, round up to MIN_READ_PAGES */
        RmOff target_pages = MAX(target_bytes / tag->page_size, SHRED_MIN_READ_PAGES);
        target_bytes = target_pages * tag->page_size;

        /* test if cost-effective to read the whole file */
        if (group->hash_offset + target_bytes + SHRED_BALANCED_READ_BYTES >= group->file_size) {
            target_bytes = group->file_size - group->hash_offset;
        }

        group->next_offset = group->hash_offset + target_bytes;

        /* for paranoid digests, make sure next read is not > max size of paranoid buffer */
        if(group->digest_type == RM_DIGEST_PARANOID) {
            group->next_offset = MIN(group->next_offset, group->hash_offset + rm_digest_paranoia_bytes() );
        }
    }

    /* read to end of current file fragment, or to group->next_offset, whichever comes first */
    RmOff bytes_to_next_fragment = 0;

    /* NOTE: need lock because queue sorting also accesses file->disk_offsets, which is not threadsafe */
    g_assert(file->device);
    g_mutex_lock(&file->device->lock); {
        bytes_to_next_fragment = rm_offset_bytes_to_next_fragment(file->disk_offsets, file->seek_offset);
    }
    g_mutex_unlock(&file->device->lock);

    if(bytes_to_next_fragment != 0 && bytes_to_next_fragment + file->seek_offset < group->next_offset) {
        file->status = RM_FILE_STATE_FRAGMENT;
        result = bytes_to_next_fragment;
    } else {
        file->status = RM_FILE_STATE_NORMAL;
        result = (group->next_offset - file->seek_offset);
    }

    return result;
}

/* Memory manager (only used for RM_DIGEST_PARANOID at the moment
 * but could also be adapted for other digests if very large
 * filesystems are contemplated)
 */

/* take or return mem allocation from main */
static bool rm_shred_mem_take(RmMainTag *main, gint64 mem_amount, guint32 numfiles) {
    bool result = true;
    g_mutex_lock(&main->hash_mem_mtx); {
        if (mem_amount <= 0 || mem_amount <= main->hash_mem_alloc || main->active_files <= 0) {
            main->hash_mem_alloc -= mem_amount;
            main->active_files += numfiles;
            rm_log_debug("%s"RESET, mem_amount > 0 ? GREEN"approved! " : "");
        } else {
            result = false;
            rm_log_debug(RED"refused; ");
        }
        rm_log_debug("mem avail %"LLU", active files %d\n"RESET, main->hash_mem_alloc, main->active_files);
    }
    g_mutex_unlock(&main->hash_mem_mtx);

    return result;
}

/* Governer to limit memory usage by limiting how many RmShredGroups can be
 * active at any one time
 * NOTE: group_lock must be held before calling rm_shred_check_hash_mem_alloc
 */
static bool rm_shred_check_hash_mem_alloc(RmFile *file) {
    RmShredGroup *group = file->shred_group;
    if (0
            || group->hash_offset > 0
            /* don't interrupt family tree once started */
            || group->status >= RM_SHRED_GROUP_HASHING) {
        /* group already committed */
        return true;
    }

    bool result;
    gint64 mem_required = group->num_files * file->file_size;

    rm_log_debug("Asking mem allocation for %s...", file->path);
    result = rm_shred_mem_take(group->main, mem_required, group->num_files);
    if(result) {
        group->status = RM_SHRED_GROUP_HASHING;
    }

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
        if (files < 0) {
            session->total_filtered_files += files;
        }
        session->shred_bytes_remaining += bytes;
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_SHREDDER);
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

/* Hash file. Runs as threadpool in parallel / tandem with rm_shred_read_factory above
 * */
static void rm_shred_hash_factory(RmBuffer *buffer, RmShredDevice *device) {
    g_assert(device);
    g_assert(buffer);

    /* Hash buffer->len bytes_read of buffer->data into buffer->file */
    rm_digest_update(buffer->file->digest, buffer->data, buffer->len);
    buffer->file->hash_offset += buffer->len;

    if (buffer->is_last) {
        /* Report the progress to rm_shred_devlist_factory */
        g_assert(buffer->file->hash_offset == buffer->file->shred_group->next_offset
                 || buffer->file->status == RM_FILE_STATE_FRAGMENT);

        /* remember that checksum */
        rm_shred_write_cksum_to_xattr(device->main->session, buffer->file);

        g_async_queue_push(device->hashed_file_return, buffer->file);
    }

    /* Return this buffer to the pool */
    rm_buffer_pool_release(device->main->mem_pool, buffer);
}

static RmShredDevice *rm_shred_device_new(gboolean is_rotational, char *disk_name, RmMainTag *main) {
    RmShredDevice *self = g_slice_new0(RmShredDevice);
    self->main = main;

    if(!rm_session_was_aborted(main->session)) {
        g_assert (self->remaining_files == 0);
        g_assert (self->remaining_bytes == 0);
    }

    self->is_rotational = is_rotational;
    self->disk_name = g_strdup(disk_name);
    self->file_queue = g_queue_new();
    self->hash_pool = rm_util_thread_pool_new(
                          (GFunc)rm_shred_hash_factory, self, 1
                      );

    self->hashed_file_return = g_async_queue_new();
    self->page_size = SHRED_PAGE_SIZE;

    g_mutex_init(&(self->lock));
    return self;
}

static void rm_shred_device_free(RmShredDevice *self) {
    if(!rm_session_was_aborted(self->main->session)) {
        g_assert(self->remaining_files == 0);
        g_assert(g_queue_is_empty(self->file_queue));
        g_assert(g_async_queue_length(self->hashed_file_return) == 0);
    }

    g_async_queue_unref(self->hashed_file_return);
    g_thread_pool_free(self->hash_pool, false, false);
    g_queue_free(self->file_queue);

    g_free(self->disk_name);
    g_mutex_clear(&(self->lock));

    g_slice_free(RmShredDevice, self);
}

/* Unlink RmFile from device queue
 */
static void rm_shred_discard_file(RmFile *file, bool free_file) {
    RmShredDevice *device = file->device;

    /* update device counters */
    if (device) {
        RmSession *session = device->main->session;
        rm_shred_adjust_counters(device, -1, -(gint64)(file->file_size - file->hash_offset));

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
        if (file->shred_group->digest_type == RM_DIGEST_PARANOID) {
            g_assert(file);

            RmMainTag *tag = file->shred_group->main;
            g_assert(tag);

            rm_log_debug("releasing mem %"LLU" bytes from %s; ", file->file_size - file->hash_offset, file->path);
            rm_shred_mem_take(tag, -(gint64)(file->file_size - file->hash_offset), -1);
        }
    }

    if(free_file) {
        /* toss the file (and any embedded hardlinks)*/
        rm_file_destroy(file);
    }
}

/* GCompareFunc for sorting files into optimum read order
 * */
static int rm_shred_compare_file_order(const RmFile *a, const RmFile *b, _U gpointer user_data) {
    /* compare based on partition (dev), then offset, then inode offset is a
     * RmOff, so do not substract them (will cause over or underflows on
     * regular basis) - use SIGN_DIFF instead
     */
    RmOff phys_offset_a = rm_offset_lookup(a->disk_offsets, a->seek_offset);
    RmOff phys_offset_b = rm_offset_lookup(b->disk_offsets, b->seek_offset);

    return (0
            + 4 * SIGN_DIFF(a->dev, b->dev)
            + 2 * SIGN_DIFF(phys_offset_a, phys_offset_b)
            + 1 * SIGN_DIFF(a->inode, b->inode)
           );
}

/* Populate disk_offsets table for each file, if disk is rotational
 * */
static void rm_shred_file_get_offset_table(RmFile *file, RmSession *session) {
    if (file->device->is_rotational) {
        g_assert(!file->disk_offsets);
        file->disk_offsets = rm_offset_create_table(file->path);

        session->offsets_read++;
        if(file->disk_offsets) {
            session->offset_fragments += g_sequence_get_length((GSequence *)file->disk_offsets);
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
    g_mutex_lock (&device->lock);
    {
        if(sorted) {
            g_queue_insert_sorted (device->file_queue, file, (GCompareDataFunc)rm_shred_compare_file_order, NULL);
        } else {
            g_queue_push_head (device->file_queue, file);
        }
    }
    g_mutex_unlock (&device->lock);
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
static void rm_shred_group_free_full(RmShredGroup *self, bool force_free) {
    g_assert(self->parent == NULL);  /* children should outlive their parents! */

    /* For -D we need to hold back the memory a bit longer */
    bool needs_free = !(self->main->session->cfg->merge_directories) || force_free;

    if (self->held_files) {
        g_queue_foreach(self->held_files, (GFunc)rm_shred_discard_file, GUINT_TO_POINTER(needs_free));
        g_queue_free(self->held_files);
        self->held_files = NULL;
    }

    if (self->digest) {
        if (self->digest->type == RM_DIGEST_PARANOID) {
            rm_shred_mem_take(self->main, -(gint64)self->digest->bytes, 0);
        }

        if(needs_free) {
            rm_digest_free(self->digest);
        }
    }

    if (self->children) {
        g_hash_table_destroy(self->children);
    }

    g_slice_free(RmShredGroup, self);
}

static void rm_shred_group_free(RmShredGroup *self) {
    rm_shred_group_free_full(self, true);
}

/* Checks whether group qualifies as duplicate candidate (ie more than
 * two members and meets has_pref and NEEDS_PREF criteria).
 * Assume group already protected by group_lock.
 * */
static void rm_shred_group_update_status(RmShredGroup *group) {
    if (group->status == RM_SHRED_GROUP_DORMANT) {
        if  (1
                && group->num_files >= 2  /* it takes 2 to tango */
                && (group->has_pref || !NEEDS_PREF(group))
                /* we have at least one file from preferred path, or we don't care */
                && (group->has_npref || !NEEDS_NPREF(group))
                /* we have at least one file from non-pref path, or we don't care */
                && (group->has_new || !NEEDS_NEW(group))
                /* we have at least one file newer than cfg->min_mtime, or we don't care */
            ) {

            if (group->hash_offset < group->file_size && group->has_only_ext_cksums == false) {
                /* group can go active */
                group->status = RM_SHRED_GROUP_START_HASHING;
            } else {
                group->status = RM_SHRED_GROUP_FINISHING;
            }
        }
    }
}

/* prototype for rm_shred_group_make_orphan since it and rm_shred_group_unref reference each other */
static void rm_shred_group_make_orphan(RmShredGroup *self);

static void rm_shred_group_unref(RmShredGroup *self) {
    self->ref_count--;

    switch (self->status) {
    case RM_SHRED_GROUP_DORMANT:
        /* group is not going to receive any more files; do required clean-up */
        rm_shred_group_free(self);
        break;
    case RM_SHRED_GROUP_FINISHING:
        /* groups is finished, and meets criteria for a duplicate group; send it to finisher */
        /* note result_pool thread takes responsibility for cleanup of this group after processing results */
        g_assert(self->children == NULL);
        rm_util_thread_pool_push(self->main->result_pool, self);
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
            rm_shred_group_free(self);
        }
        break;
    case RM_SHRED_GROUP_FINISHED:
    default:
        g_assert_not_reached();
    }
}

static void rm_shred_group_make_orphan(RmShredGroup *self) {
    self->parent = NULL;
    rm_shred_group_unref(self);
}

static gboolean rm_shred_group_push_file(RmShredGroup *shred_group, RmFile *file, gboolean initial) {
    gboolean result = false;
    file->shred_group = shred_group;

    if (file->digest) {
        rm_digest_free(file->digest);
        file->digest = NULL;
    }

    shred_group->has_pref  |=  file->is_prefd | file->hardlinks.has_prefd;
    shred_group->has_npref |= !file->is_prefd | file->hardlinks.has_non_prefd;
    shred_group->has_new   |=  file->is_new_or_has_new;

    shred_group->ref_count++;
    shred_group->num_files++;

    g_assert(file->hash_offset == shred_group->hash_offset);

    rm_shred_group_update_status(shred_group);

    switch (shred_group->status) {
    case RM_SHRED_GROUP_START_HASHING:
        /* clear the queue and push all its rmfiles to the appropriate device queue */
        if(shred_group->held_files) {
            g_queue_free_full(shred_group->held_files,
                              (GDestroyNotify)(initial ?
                                               rm_shred_push_queue :
                                               rm_shred_push_queue_sorted
                                              )
                             );
            shred_group->held_files = NULL; /* won't need shred_group queue any more, since new arrivals will bypass */
        }
    /* FALLTHROUGH */
    case RM_SHRED_GROUP_HASHING:
        if (initial) {
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

    return result;
}

/* After partial hashing of RmFile, add it back into the sieve for further
 * hashing if required.  If try_bounce option is set, then try to return the
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
    RmMainTag *tag = current_group->main;

    g_mutex_lock(&tag->group_lock);
    {
        if (file->status == RM_FILE_STATE_IGNORE) {
            rm_digest_free(file->digest);
            rm_shred_discard_file(file, true);
        } else {
            g_assert(file->digest);

            if (file->digest->type == RM_DIGEST_PARANOID && !file->is_symlink) {
                g_assert(file->digest->bytes == current_group->next_offset - current_group->hash_offset);
            }

            /* check if there is already a descendent of current_group which
             * matches snap... if yes then move this file into it; if not then
             * create a new group */
            if (!current_group->children) {
                /* create child queue */
                current_group->children = g_hash_table_new((GHashFunc)rm_digest_hash, (GEqualFunc)rm_digest_equal);
            }

            child_group = g_hash_table_lookup(
                              current_group->children,
                              file->digest
                          );

            if (!child_group) {
                child_group = rm_shred_group_new(file);
                g_hash_table_insert(current_group->children, child_group->digest, child_group);
                child_group->has_only_ext_cksums = current_group->has_only_ext_cksums;
            } else {
                if (file->digest->type == RM_DIGEST_PARANOID) {
                    rm_shred_mem_take(tag, -(gint64)file->digest->bytes, 0);
                }
            }

            result = rm_shred_group_push_file(child_group, file, false);
        }

        /* current_group now has one less file to process */
        rm_shred_group_unref(current_group);
    }
    g_mutex_unlock(&tag->group_lock);
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
    if (file->hardlinks.files) {
        for (GList *iter = file->hardlinks.files->head; iter; iter = iter->next ) {
            RmFile *link = iter->data;
            file->hardlinks.has_non_prefd |= !link->is_prefd;
            file->hardlinks.has_prefd |= link->is_prefd;
            file->is_new_or_has_new |= (link->mtime >= session->cfg->min_mtime);
        }
    }

    /* create RmShredDevice for this file if one doesn't exist yet */
    dev_t disk = rm_mounts_get_disk_id(session->mounts, file->dev);
    RmShredDevice *device = g_hash_table_lookup(session->tables->dev_table, GUINT_TO_POINTER(disk));

    if(device == NULL) {
        rm_log_debug(GREEN"Creating new RmShredDevice for disk %u\n"RESET, (unsigned)disk);
        device = rm_shred_device_new(
                     !rm_mounts_is_nonrotational(session->mounts, disk),
                     rm_mounts_get_disk_name(session->mounts, disk),
                     main );
        device->disk = disk;
        g_hash_table_insert(session->tables->dev_table, GUINT_TO_POINTER(disk), device);
    }

    file->device = device;

    rm_shred_adjust_counters(device, 1, (gint64)file->file_size);

    RmShredGroup *group = g_hash_table_lookup(
                              session->tables->size_groups,
                              file
                          );

    if (group == NULL) {
        group = rm_shred_group_new(file);
        group->digest_type = session->cfg->checksum_type;
        g_hash_table_insert(
            session->tables->size_groups,
            file,
            group
        );
    }

    rm_shred_group_push_file(group, file, true);

    if(main->session->cfg->read_cksum_from_xattr) {
        char *ext_cksum = rm_xattr_read_hash(main->session, file);
        if(ext_cksum != NULL) {
            g_hash_table_insert(
                session->tables->ext_cksums, g_strdup(file->path), ext_cksum
            );
        }
    }

    if(HAS_CACHE(session) && g_hash_table_lookup(session->tables->ext_cksums, file->path)) {
        group->num_ext_cksums += 1;
        file->has_ext_cksum = 1;
    }
}

static bool rm_shred_group_preprocess(_U gpointer key, RmShredGroup *group) {
    g_assert(group);
    if (group->status == RM_SHRED_GROUP_DORMANT) {
        rm_shred_group_free(group);
        return true;
    } else {
        return false;
    }
}

static void rm_shred_device_preprocess(_U gpointer key, RmShredDevice *device, RmMainTag *main) {
    g_mutex_lock(&device->lock);
    g_queue_foreach(device->file_queue, (GFunc)rm_shred_file_get_offset_table, main->session);
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
        rm_json_cache_read(session->tables->ext_cksums, cache_path);
    }

    rm_log_debug("Moving files into size_groups...");
    g_hash_table_foreach_remove(session->tables->node_table,
                                (GHRFunc)rm_shred_file_preprocess,
                                main
                               );

    GHashTableIter iter;
    gpointer size, p_group;

    if(HAS_CACHE(main->session)) {
        g_hash_table_iter_init(&iter, session->tables->size_groups);
        while(g_hash_table_iter_next(&iter, &size, &p_group)) {
            RmShredGroup *group = p_group;
            if(group->num_files == group->num_ext_cksums) {
                group->has_only_ext_cksums = true;
            }
        }
    }

    rm_log_debug("move remaining files to size_groups finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    rm_log_debug("Discarding unique sizes and read fiemap data for others...");
    removed = g_hash_table_foreach_remove(session->tables->size_groups,
                                          (GHRFunc)rm_shred_group_preprocess,
                                          main);
    rm_log_debug("done at time %.3f; removed %u of %"LLU"\n",
                 g_timer_elapsed(session->timer, NULL), removed, session->total_filtered_files
                );

    rm_log_debug("Looking up fiemap data for files on rotational devices...");
    g_hash_table_foreach(
        session->tables->dev_table, (GHFunc)rm_shred_device_preprocess, main
    );
    rm_log_debug("done at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    rm_log_debug(
        "fiemap'd %"LLU" files containing %"LLU" fragments (failed another %"LLU" files)\n",
        session->offsets_read - session->offset_fails,
        session->offset_fragments, session->offset_fails
    );
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
    if (1
            && (a->is_prefd != b->is_prefd)
            && (cfg->keep_all_untagged || cfg->must_match_untagged)
       ) {
        return(a->is_prefd - b->is_prefd);
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
        if (file->hardlinks.files) {
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
        if (0
                || ((file->is_prefd) && (session->cfg->keep_all_tagged))
                || ((!file->is_prefd) && (session->cfg->keep_all_untagged))
           ) {
            file->is_original = true;
            rm_log_debug("tagging %s as original because %s\n",
                         file->path,
                         ((file->is_prefd) && (session->cfg->keep_all_tagged)) ? "tagged" : "untagged"
                        );
        }
    }

    /* sort the unbundled group */
    g_queue_sort (group, (GCompareDataFunc)rm_shred_cmp_orig_criteria, session);

    RmFile *headfile = group->head->data;
    if (!headfile->is_original) {
        headfile->is_original = true;
        rm_log_debug("tagging %s as original because it is highest ranked\n", headfile->path);
    }
}

void rm_shred_forward_to_output(RmSession *session, GQueue *group) {
    g_assert(group);
    g_assert(group->head);

    RmFile *head = group->head->data;
    rm_log_debug("Forwarding %s's group\n", head->path);

    /* Hand it over to the printing module */
    g_queue_foreach(group, (GFunc)rm_fmt_write, session->formats);
}

static void rm_shred_dupe_totals(RmFile *file, RmSession *session) {
    if (!file->is_original) {
        session->dup_counter++;
        session->total_lint_size += file->file_size;
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
    rm_shred_group_free_full(group, false);
}

/////////////////////////////////
//    ACTUAL IMPLEMENTATION    //
/////////////////////////////////

static void rm_shred_readlink_factory(RmFile *file, RmShredDevice *device) {
    g_assert(file->is_symlink);

    /* Fake an IO operation on the symlink.
     */
    char path_buf[PATH_MAX];
    memset(path_buf, 0, sizeof(path_buf));

    if(readlink(file->path, path_buf, sizeof(path_buf)) == -1) {
        /* Oops, that did not work out, report as an error */
        file->status = RM_FILE_STATE_IGNORE;
        return;
    }

    file->status = RM_FILE_STATE_NORMAL;
    file->seek_offset = file->file_size;
    file->hash_offset = file->file_size;

    g_assert(file->digest);

    gsize data_size = strlen(path_buf) + 1;
    rm_digest_update(file->digest, (unsigned char *)path_buf, data_size);

    /* In case of paranoia: shrink the used data buffer, so comparasion works
     * as expected. Otherwise a full buffer is used with possibly different
     * content */
    if(file->digest->type == RM_DIGEST_PARANOID) {
        rm_digest_paranoia_shrink(file->digest, data_size);
    }

    rm_shred_adjust_counters(device, 0, -(gint64)file->file_size);
}

/* Read from file and send to hasher
 * Note this was initially a separate thread but is currently just called
 * directly from rm_devlist_factory.
 * */
static void rm_shred_read_factory(RmFile *file, RmShredDevice *device) {
    int fd = 0;
    gint32 bytes_read = 0;
    gint32 total_bytes_read = 0;

    RmOff buf_size = rm_buffer_pool_size(device->main->mem_pool);
    buf_size -= offsetof(RmBuffer, data);

    gint32 bytes_to_read = rm_shred_get_read_size(file, device->main);
    gint32 bytes_left_to_read = bytes_to_read;

    g_assert(!file->is_symlink);
    g_assert(bytes_to_read > 0);
    g_assert(buf_size >= (RmOff)SHRED_PAGE_SIZE);
    g_assert(bytes_to_read + file->hash_offset <= file->file_size);
    g_assert(file->seek_offset == file->hash_offset);

    struct iovec readvec[SHRED_MAX_PAGES + 1];

    if(file->seek_offset >= file->file_size) {
        goto finish;
    }

    fd = rm_sys_open(file->path, O_RDONLY);
    if(fd == -1) {
        rm_log_info("open(2) failed for %s: %s", file->path, g_strerror(errno));
        file->status = RM_FILE_STATE_IGNORE;
        g_async_queue_push(device->hashed_file_return, file);
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

    /* how many buffers to read? */
    const gint16 N_BUFFERS = MIN(4, DIVIDE_CEIL(bytes_to_read, buf_size));

    /* Give the kernel scheduler some hints */
    posix_fadvise(fd, file->seek_offset, bytes_to_read, SHRED_FADVISE_FLAGS);

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

    while(bytes_left_to_read > 0 && (bytes_read = rm_sys_preadv(fd, readvec, N_BUFFERS, file->seek_offset)) > 0) {
        bytes_read = MIN(bytes_read, bytes_left_to_read); /* ignore over-reads */
        int blocks = DIVIDE_CEIL(bytes_read,  buf_size);

        bytes_left_to_read -= bytes_read;
        file->seek_offset += bytes_read;
        total_bytes_read += bytes_read;

        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
            buffer->file = file;
            buffer->len = MIN (buf_size, bytes_read - i * buf_size);
            buffer->is_last = (i + 1 >= blocks && bytes_left_to_read <= 0);
            if (bytes_left_to_read < 0) {
                rm_log_error_line(_("Negative bytes_left_to_read for %s"), file->path);
            }

            if (buffer->is_last && total_bytes_read != bytes_to_read) {
                rm_log_error_line(
                    _("Something went wrong reading %s; expected %d bytes, got %d; ignoring"),
                    file->path, bytes_to_read, total_bytes_read
                );
                file->status = RM_FILE_STATE_IGNORE;
                g_async_queue_push(device->hashed_file_return, file);
            } else {
                /* Send it to the hasher */
                rm_util_thread_pool_push(device->hash_pool, buffer);

                /* Allocate a new buffer - hasher will release the old buffer */
                buffer = rm_buffer_pool_get(device->main->mem_pool);
                readvec[i].iov_base = buffer->data;
                readvec[i].iov_len = buf_size;
            }
        }
    }

    if (bytes_read == -1) {
        rm_log_perror("preadv failed");
        file->status = RM_FILE_STATE_IGNORE;
        g_async_queue_push(device->hashed_file_return, file);
        goto finish;
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

    /* Update totals for device and session*/
    rm_shred_adjust_counters(device, 0, -(gint64)total_bytes_read);
}

static bool rm_shred_reassign_checksum(RmMainTag *main, RmFile *file) {
    bool can_process = true;

    if (file->shred_group->digest_type == RM_DIGEST_PARANOID) {
        /* check if memory allocation is ok */
        if (!rm_shred_check_hash_mem_alloc(file)) {
            can_process = false;
        } else {
            /* get the required target offset into file->shred_group->next_offset, so
                * that we can make the paranoid RmDigest the right size*/
            if (file->shred_group->next_offset == 0) {
                (void) rm_shred_get_read_size(file, main);
            }
            g_assert (file->shred_group->hash_offset == file->hash_offset);

            if(file->is_symlink) {
                file->digest = rm_digest_new(
                                   main->session->cfg->checksum_type, 0, 0,
                                   PATH_MAX + 1 /* max size of a symlink file */
                               );
            } else {
                file->digest = rm_digest_new(
                                   main->session->cfg->checksum_type, 0, 0,
                                   file->shred_group->next_offset - file->hash_offset
                               );
            }
        }
    } else if(file->shred_group->digest) {
        /* pick up the digest-so-far from the RmShredGroup */
        file->digest = rm_digest_copy(file->shred_group->digest);
    } else if(file->shred_group->has_only_ext_cksums) {
        /* Cool, we were able to read the checksum from disk */
        file->digest = rm_digest_new(RM_DIGEST_EXT, 0, 0, 0);

        char *hexstring = g_hash_table_lookup(main->session->tables->ext_cksums, file->path);
        if(hexstring != NULL) {
            rm_digest_update(file->digest, (unsigned char *)hexstring, strlen(hexstring));
            rm_log_debug("%s=%s was read from cache.\n", hexstring, file->path);
        } else {
            rm_log_warning_line("Unable to read external checksum from interal cache for %s", file->path);
            file->has_ext_cksum = 0;
            file->shred_group->has_only_ext_cksums = 0;
        }
    } else {
        /* this is first generation of RMGroups, so there is no progressive hash yet */
        file->digest = rm_digest_new(
                           main->session->cfg->checksum_type,
                           main->session->hash_seed1,
                           main->session->hash_seed2,
                           0
                       );
    }

    return can_process;
}

static RmFile *rm_shred_process_file(RmShredDevice *device, RmFile *file) {
    if(file->shred_group->has_only_ext_cksums) {
        rm_shred_adjust_counters(device, 0, -(gint64)file->file_size);
        return file;
    }

    /* hash the next increment of the file */
    if(file->is_symlink) {
        rm_shred_readlink_factory(file, device);
    } else {
        rm_shred_read_factory(file, device);

        /* wait until the increment has finished hashing */
        file = g_async_queue_pop(device->hashed_file_return);
    }

    return file;
}

static void rm_shred_devlist_factory(RmShredDevice *device, RmMainTag *main) {
    GList *iter = NULL;
    gboolean emptyqueue = FALSE;

    g_assert(device);
    g_assert(device->hash_pool); /* created when device created */

    g_mutex_lock(&device->lock); {
        rm_log_debug(BLUE"Started rm_shred_devlist_factory for disk %s (%u:%u) with %"LLU" files in queue\n"RESET,
                     device->disk_name,
                     major(device->disk),
                     minor(device->disk),
                     (RmOff)g_queue_get_length(device->file_queue)
                    );

        if(device->is_rotational) {
            g_queue_sort(device->file_queue, (GCompareDataFunc)rm_shred_compare_file_order, NULL);
        }

        if (g_queue_get_length(device->file_queue) == 0) {
            /* give the other device threads a chance to catch up, which will hopefully
             * release held files from RmShredGroups to give us some work to do */
            emptyqueue = TRUE;
        }
        iter = device->file_queue->head;
    }
    g_mutex_unlock(&device->lock);

    if(emptyqueue) {
        /* brief sleep to stop starving devices from hogging too much cpu time */
        g_usleep(SHRED_EMPTYQUEUE_SLEEP_US);
    }

    /* scheduler for one file at a time, optimised to minimise seeks */
    while(iter && !rm_session_was_aborted(main->session)) {
        RmFile *file = iter->data;
        gboolean can_process = true;

        RmOff start_offset = file->hash_offset;

        /* initialise hash (or recover progressive hash so far) */
        g_assert(file->shred_group);
        g_mutex_lock(&main->group_lock); {
            if (file->digest == NULL) {
                g_assert(file->shred_group);

                can_process = rm_shred_reassign_checksum(main, file);
            }
        }
        g_mutex_unlock(&main->group_lock);

        if (can_process) {
            file = rm_shred_process_file(device, file);

            if (start_offset == file->hash_offset && file->has_ext_cksum == false) {
                rm_log_debug(RED"Offset stuck at %"LLU";"RESET, start_offset);
                file->status = RM_FILE_STATE_IGNORE;
                /* rm_shred_sift will dispose of the file */
            }

            if (file->status == RM_FILE_STATE_FRAGMENT) {
                /* file is not ready for checking yet; push it back into the queue */
                rm_log_debug("Recycling fragment %s\n", file->path);
                rm_shred_push_queue_sorted(file); /* call with device unlocked */
                /* NOTE: this temporarily means there are two copies of file in the queue */
            } else if(rm_shred_sift(file)) {
                /* continue hashing same file, ie no change to iter */
                rm_log_debug("Continuing to next generation %s\n", file->path);
                continue;
            } else {
                /* rm_shred_sift has taken responsibility for the file and either disposed
                 * of it (careful!) or pushed it back into our queue (so there may be two
                 * copies in the queue); unlink and move to next file (below).*/
            }
        }

        /* remove file from queue and move to next*/
        g_mutex_lock(&device->lock); {
            GList *tmp = iter;
            iter = iter->next;
            g_assert(tmp->data == file);
            if (can_process) {
                /* file has been processed */
                g_queue_delete_link(device->file_queue, tmp);
            }
        }
        g_mutex_unlock(&device->lock);
    }

    /* threadpool thread terminates but the device will be recycled via
     * the device_return queue
     */
    rm_log_debug(BLUE"Pushing device back to main joiner %d\n"RESET, (int)device->disk);
    g_async_queue_push(main->device_return, device);
}

static void rm_shred_create_devpool(RmMainTag *tag, GHashTable *dev_table) {
    tag->device_pool = rm_util_thread_pool_new(
                           (GFunc)rm_shred_devlist_factory, tag, tag->session->cfg->threads / 2 + 1
                       );

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, dev_table);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        RmShredDevice *device = value;
        g_queue_sort(device->file_queue, (GCompareDataFunc)rm_shred_compare_file_order, NULL);
        rm_log_debug(GREEN"Pushing device %s to threadpool\n", device->disk_name);
        rm_util_thread_pool_push(tag->device_pool, device);
    }
}

void rm_shred_run(RmSession *session) {
    g_assert(session);
    g_assert(session->tables);
    g_assert(session->mounts);
    g_assert(session->tables->node_table);

    RmMainTag tag;
    tag.active_files = 0;
    tag.hash_mem_alloc = 0;
    tag.session = session;

    if(HAS_CACHE(session)) {
        session->tables->ext_cksums = g_hash_table_new_full(
                                          g_str_hash, g_str_equal, g_free, g_free
                                      );
    } else {
        session->tables->ext_cksums = NULL;
    }

    /* Do not rely on sizeof(RmBuffer), compiler might add padding. */
    tag.mem_pool = rm_buffer_pool_init(offsetof(RmBuffer, data) + SHRED_PAGE_SIZE);
    tag.device_return = g_async_queue_new();
    tag.page_size = SHRED_PAGE_SIZE;

    /* would use g_atomic, but helgrind does not like that */
    g_mutex_init(&tag.hash_mem_mtx);
    g_mutex_init(&tag.group_lock);

    session->tables->dev_table = g_hash_table_new_full(
                                     g_direct_hash, g_direct_equal,
                                     NULL, (GDestroyNotify)rm_shred_device_free
                                 );

    rm_shred_preprocess_input(&tag);
    session->shred_bytes_after_preprocess = session->shred_bytes_remaining;

    g_mutex_lock(&tag.hash_mem_mtx); {
        tag.hash_mem_alloc = session->cfg->paranoid_mem;  /* NOTE: needs to be after preprocess */
        tag.active_files = 0;				 	          /* NOTE: needs to be after preprocess */
    }
    g_mutex_unlock(&tag.hash_mem_mtx);

    /* Remember how many devlists we had - so we know when to stop */
    int devices_left = g_hash_table_size(session->tables->dev_table);
    rm_log_debug(BLUE"Devices = %d\n", devices_left);

    tag.result_pool = rm_util_thread_pool_new(
                          (GFunc)rm_shred_result_factory, &tag, 1
                      );

    /* Create a pool fo the devlists and push each queue */
    rm_shred_create_devpool(&tag, session->tables->dev_table);

    /* This is the joiner part */
    while(devices_left > 0 || g_async_queue_length(tag.device_return) > 0) {
        RmShredDevice *device = g_async_queue_pop(tag.device_return);
        g_mutex_lock(&device->lock);
        g_mutex_lock(&tag.hash_mem_mtx); { /* probably unnecessary because we are only reading */
            rm_log_debug(
                BLUE"Got device %s back with %d in queue and %"LLU" bytes remaining in %d remaining files; active files %d and avail mem %"LLU"\n"RESET,
                device->disk_name,
                g_queue_get_length(device->file_queue),
                device->remaining_bytes,
                device->remaining_files,
                tag.active_files,
                tag.hash_mem_alloc
            );

            if (device->remaining_files > 0) {
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

    /* This should not block, or at least only very short. */
    g_thread_pool_free(tag.device_pool, FALSE, TRUE);
    g_thread_pool_free(tag.result_pool, FALSE, TRUE);

    g_async_queue_unref(tag.device_return);
    rm_buffer_pool_destroy(tag.mem_pool);

    if(session->tables->ext_cksums) {
        g_hash_table_unref(session->tables->ext_cksums);
    }

    g_hash_table_unref(session->tables->dev_table);
    g_mutex_clear(&tag.group_lock);
    g_mutex_clear(&tag.hash_mem_mtx);
}
