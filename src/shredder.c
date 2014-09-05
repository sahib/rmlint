#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/uio.h>

#include "checksum.h"
#include "checksums/city.h"

#include "preprocess.h"
#include "utilities.h"
#include "formats.h"

#include "shredder.h"
#include <inttypes.h>


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
#define SHRED_BALANCED_READ_BYTES (SHRED_SEEK_MS * SHRED_READRATE * 1024) // ( * 1024 / 1000 to be exact)

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
    guint64 size;

    /* concurrent accesses may happen */
    GMutex lock;
} RmBufferPool;


/* Represents one block of read data */
typedef struct RmBuffer {
    /* file structure the data belongs to */
    RmFile *file;

    /* len of the read input */
    guint64 len;

    /* is this the last buffer of the current increment? */
    gboolean is_last;

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
    GMutex file_state_mtx;
    GThreadPool *device_pool;
    GThreadPool *result_pool;
    gint32 page_size;
    guint32 totalfiles;
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
    guint64 current_offset;
    dev_t current_dev;

    /* size of one page, cached, so
     * sysconf() does not need to be called always.
     */
    guint64 page_size;
    RmMainTag *main;
} RmShredDevice;

typedef enum RmShredGroupStatus {
    RM_SHRED_GROUP_DORMANT = 0,
    RM_SHRED_GROUP_START_HASHING,
    RM_SHRED_GROUP_HASHING,
    RM_SHRED_GROUP_FINISHING
} RmShredGroupStatus;

typedef struct RmShredGroup {
    /* holding queue for files; they are held here until the group first meets
     * criteria for further hashing (normally just 2 or more files, but sometimes
     * related to preferred path counts)
     * */
    GQueue *held_files;

    /* link(s) to next generation of RmShredGroups(s) which have this RmShredGroup as parent*/
    GQueue *children;

    /* RmShredGroup of the same size files but with lower RmFile->hash_offset;
     * getsset to null when parent dies
     * */
    struct RmShredGroup *parent;

    /* number of child group files that have not completed next level of hashing */
    gulong remaining;

    /* set if group has 1 or more files from "preferred" paths */
    gboolean has_pref;

    /* set if group has 1 or more files from "non-preferred" paths */
    gboolean has_npref;

    /* set based on settings->must_match_original */
    gboolean needs_pref;

    /* set based on settings->keep_all_originals */
    gboolean needs_npref;

    /* initially RM_SHRED_GROUP_DORMANT; triggered as soon as we have >= 2 files
     * and meet preferred path and will go to either RM_SHRED_GROUP_HASHING or
     * RM_SHRED_GROUP_FINISHING.  When switching from dormant to hashing, all
     * held_files are released and future arrivals go straight to hashing
     * */
    RmShredGroupStatus status;

    /* file size of files in this group */
    guint64 file_size;

    /* file hash_offset when files arrived in this group */
    guint64 hash_offset;

    /* file hash_offset for next increment */
    guint64 next_offset;

    /* needed because different device threads read and write to this structure */
    GMutex lock;

    /* checksum structure taken from first file to enter the group.  This allows
     * digests to be released from RmFiles and memory freed up until they
     * are required again for further hashing.*/
    RmDigest *digest;
    /* checksum result (length is given by digest->bytes) */
    guint8 *checksum;

    /* Reference to main */
    RmMainTag *main;
} RmShredGroup;

/* header to avoid implicit reference warning  */
static void rm_shred_hash_factory(RmBuffer *buffer, RmShredDevice *device);

RmShredDevice *rm_shred_device_new(gboolean is_rotational, char *disk_name, RmMainTag *main) {
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

void rm_shred_device_free(RmShredDevice *self) {
    if(!rm_session_was_aborted(self->main->session)) {
        g_assert(self->remaining_files == 0);
        g_assert(self->remaining_bytes == 0);
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

/////////// RmShredGroup ////////////////

/* prototype for rm_shred_group_make_orphan since it and rm_shred_group_free reference each other */
void rm_shred_group_make_orphan(RmShredGroup *self);

/* allocate and initialise new RmShredGroup
 */
RmShredGroup *rm_shred_group_new(RmFile *file, guint8 *key) {
    RmShredGroup *self = g_slice_new0(RmShredGroup);

    if (file->digest) {
        self->digest = rm_digest_copy(file->digest);
        self->checksum = key;
    } else {
        /* initial groups have no checksum */
    }

    self->parent = file->shred_group;

    if(self->parent) {
        self->needs_npref = self->parent->needs_npref;
        self->needs_pref = self->parent->needs_pref;
    }

    self->held_files = g_queue_new();
    self->children   = g_queue_new();

    self->file_size = file->file_size;
    self->hash_offset = file->hash_offset;

    g_assert(file->device->main);
    self->main = file->device->main;

    g_mutex_init(&(self->lock));
    return self;
}

/* Unlink RmFile from device queue
 */
void rm_shred_discard_file(RmFile *file) {
    RmShredDevice *device = file->device;
    g_mutex_lock(&(device->lock));
    {
        device->remaining_files--;
        device->remaining_bytes -= (file->file_size - file->hash_offset);
    }
    g_mutex_unlock(&(device->lock));
    rm_file_destroy(file);
}

/* Free RmShredGroup and any dormant files still in its queue
 */
void rm_shred_group_free(RmShredGroup *self) {
    g_assert(self->parent == NULL);  /* children should outlive their parents! */

    /** discard RmFiles which failed file duplicate criteria */
    if (self->held_files) {
        switch(self->status) {
        case RM_SHRED_GROUP_DORMANT:
            g_queue_free_full(self->held_files, (GDestroyNotify)rm_shred_discard_file);
            break;
        case RM_SHRED_GROUP_FINISHING:
            g_assert_not_reached();
        case RM_SHRED_GROUP_HASHING:
            g_assert_not_reached();
        case RM_SHRED_GROUP_START_HASHING:
            g_assert_not_reached();
        default:
            g_assert_not_reached();
        }
    }

    g_assert(self->children);
    /** give our children the bad news */
    g_queue_foreach(self->children, (GFunc)rm_shred_group_make_orphan, NULL);
    g_queue_free(self->children);

    if (self->digest) {
        g_slice_free1(self->digest->bytes, self->checksum);
        rm_digest_free(self->digest);
    }

    /** clean up */
    g_mutex_clear(&self->lock);
    g_slice_free(RmShredGroup, self);
}

///////////////////////////////////////
//    BUFFER POOL IMPLEMENTATION     //
///////////////////////////////////////

static guint64 rm_buffer_pool_size(RmBufferPool *pool) {
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
    g_mutex_lock(&pool->lock);
    {
        g_trash_stack_push(&pool->stack, buf);
    }
    g_mutex_unlock(&pool->lock);
}

///////////////////////////////////
//    RmShredDevice UTILITIES    //
///////////////////////////////////

/* GCompareFunc for sorting files into optimum read order
 * */
static int rm_shred_compare_file_order(const RmFile *a, const RmFile *b, _U gpointer user_data) {
    /* compare based on partition (dev), then offset, then inode offset is a
     * guint64, so do not substract them (will cause over or underflows on
     * regular basis) - use SIGN_DIFF instead
     */
    guint64 phys_offset_a = rm_offset_lookup(a->disk_offsets, a->seek_offset);
    guint64 phys_offset_b = rm_offset_lookup(b->disk_offsets, b->seek_offset);

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
static void rm_shred_push_queue(RmFile *file) {
    RmShredDevice *device = file->device;

    g_mutex_lock (&device->lock);
    {
        g_queue_push_head (device->file_queue, file);
    }
    g_mutex_unlock (&device->lock);
}
static void rm_shred_push_queue_sorted(RmFile *file) {
    RmShredDevice *device = file->device;

    g_mutex_lock (&device->lock);
    {
        g_queue_insert_sorted (device->file_queue, file, (GCompareDataFunc)rm_shred_compare_file_order, NULL);
    }
    g_mutex_unlock (&device->lock);
}

//////////////////////////////////
//    RmShredGroup UTILITIES    //
//////////////////////////////////

/* compares checksum with that of a RmShredGroup with a  */
gint rm_cksum_matches_group(RmShredGroup *group, guint8 *checksum) {
    g_assert(group);
    g_assert(checksum);

    return memcmp(group->checksum, checksum, group->digest->bytes);
}

/* Checks whether group qualifies as duplicate candidate (ie more than
 * two members and meets has_pref and needs_pref criteria).
 * Assume group already protected by group->lock.
 * */
static char rm_shred_group_get_status_locked(RmShredGroup *group) {
    if (!group->status) {
        if (1
                && group->remaining >= 2  /* it takes 2 to tango */
                && (group->has_pref || !group->needs_pref)
                /* we have at least one file from preferred path, or we don't care */
                && (group->has_npref || !group->needs_npref)
                /* we have at least one file from non-pref path, or we don't care */
           ) {
            /* group can go active */
            if (group->hash_offset < group->file_size) {
                group->status = RM_SHRED_GROUP_START_HASHING;
            } else {
                group->status = RM_SHRED_GROUP_FINISHING;
            }
        }
    }
    return group->status;
}

void rm_shred_group_make_orphan(RmShredGroup *self) {
    RmShredGroupStatus status;
    g_mutex_lock(&(self->lock));
    {
        status = self->status;
        self->parent = NULL;
    }
    g_mutex_unlock(&(self->lock));

    switch (status) {
    /* decide fate of files, triggered by death of parent.
     * NOTE: If files are still hashing, then fate will be decided later via
     * rm_shred_group_unref
     * */
    case RM_SHRED_GROUP_DORMANT:
        /* group doesn't need hashing, and not expecting any more (since
         * parent is dead), so this group is now also dead
         * NOTE: there is no potential race here
         * because parent can only die once
         * */
        rm_shred_group_free(self);

        break;
    case RM_SHRED_GROUP_FINISHING:
        /* groups is finished, and meets criteria for a duplicate group; send it to finisher */
        rm_util_thread_pool_push(self->main->result_pool, self);
        break;
    default:
        break;
    }
}

void rm_shred_group_unref(RmShredGroup *group) {
    g_assert(group);

    g_mutex_lock(&(group->lock));
    if ((--group->remaining) == 0 && group->parent == NULL) {
        /* no reason for living any more */
        g_mutex_unlock(&(group->lock));
        rm_shred_group_free(group);
    } else {
        g_mutex_unlock(&(group->lock));
    }
}

static gboolean rm_shred_group_push_file(RmShredGroup *shred_group, RmFile *file, gboolean initial) {
    gboolean result = false;

    g_mutex_lock(&(shred_group->lock));
    {
        file->shred_group = shred_group;

        if (file->digest) {
            rm_digest_free(file->digest);
            file->digest = NULL;
        }

        shred_group->has_pref |= file->is_prefd | file->hardlinks.has_prefd;
        shred_group->has_npref |= !file->is_prefd | file->hardlinks.has_non_prefd;
        shred_group->remaining++;

        g_assert(file->hash_offset == shred_group->hash_offset);

        switch (rm_shred_group_get_status_locked(shred_group)) {
        case RM_SHRED_GROUP_START_HASHING:
            /* clear the queue and push all its rmfiles to the appropriate device queue */
            g_assert(shred_group->held_files);
            if(initial) {
                g_queue_free_full(shred_group->held_files,
                                  (GDestroyNotify)rm_shred_push_queue);
            } else {
                g_queue_free_full(shred_group->held_files,
                                  (GDestroyNotify)rm_shred_push_queue_sorted);
            }
            shred_group->held_files = NULL; /* won't need shred_group queue any more, since new arrivals will bypass */
            shred_group->status = RM_SHRED_GROUP_HASHING;
        /* FALLTHROUGH */
        case RM_SHRED_GROUP_HASHING:
            if (initial) {
                /* add file to device queue */
                g_assert(file->device);
                if(initial) {
                    rm_shred_push_queue(file);
                } else {
                    rm_shred_push_queue_sorted(file);
                }
            } else {
                /* calling routine will handle the file */
                result = true;
            }
            break;
        case RM_SHRED_GROUP_DORMANT:
        case RM_SHRED_GROUP_FINISHING:
            /* add file to held_files */
            g_queue_push_head(shred_group->held_files, file);
        }
    }
    g_mutex_unlock(&(shred_group->lock));

    /* decrease parent's child count*/
    if(!initial && shred_group->parent) {
        rm_shred_group_unref(shred_group->parent);
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
    g_assert(file);
    g_assert(file->shred_group);

    guint8 *key = rm_digest_steal_buffer(file->digest);

    RmShredGroup *child_group = NULL;
    RmShredGroup *current_group = file->shred_group;

    g_mutex_lock(&(current_group->lock));
    {
        /* check if there is already a descendent of current_group which
         * matches snap... if yes then move this file into it; if not then
         * create a new group */
        GList *child = g_queue_find_custom(
                           current_group->children,
                           key,
                           (GCompareFunc)rm_cksum_matches_group
                       );

        if (!child) {
            child_group = rm_shred_group_new(file, key);
            g_queue_push_tail(current_group->children, child_group);
        } else {
            child_group = child->data;
            g_slice_free1(file->digest->bytes, key);
        }
    }
    g_mutex_unlock(&(current_group->lock));
    return rm_shred_group_push_file(child_group, file, false);
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

/* Insert RmFiles into size_groups
 * */
static void rm_shred_file_preprocess(_U gpointer key, RmFile *file, RmMainTag *main) {
    /* initial population of RmShredDevice's and first level RmShredGroup's */
    RmSession *session = main->session;

    g_assert(file);
    g_assert(session->tables->dev_table);
    g_assert(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE);
    g_assert(file->file_size > 0);

    /* if file has hardlinks then set file->hardlinks.has_[non_]prefd*/
    if (file->hardlinks.files) {
        for (GList *iter = file->hardlinks.files->head; iter; iter = iter->next ) {
            RmFile *link = iter->data;
            file->hardlinks.has_non_prefd |= !link->is_prefd;
            file->hardlinks.has_prefd |= link->is_prefd;
        }
    }

    /* create RmShredDevice for this file if one doesn't exist yet */
    main->totalfiles++;
    dev_t disk = rm_mounts_get_disk_id(session->tables->mounts, file->dev);

    RmShredDevice *device = g_hash_table_lookup(session->tables->dev_table, GUINT_TO_POINTER(disk));
    if(device == NULL) {

        rm_log_debug(GREEN"Creating new RmShredDevice for disk %"LLU"\n"RESET, disk);
        device = rm_shred_device_new(
                     !rm_mounts_is_nonrotational(session->tables->mounts, disk),
                     rm_mounts_get_disk_name(session->tables->mounts, disk),
                     main );
        device->disk = disk;
        g_hash_table_insert(session->tables->dev_table, GUINT_TO_POINTER(disk), device);
    }

    file->device = device;
    device->remaining_files++;
    device->remaining_bytes += file->file_size;

    RmShredGroup *group = g_hash_table_lookup(
                              session->tables->size_groups,
                              &file->file_size
                          );

    if (group == NULL) {
        /* we cannot store a 8byte integer in 4 byte pointer
         * on 32 bit (we can on 64 though), so allocate mem.
         */
        guint64 *file_size_cpy = g_malloc0(sizeof(guint64));
        *file_size_cpy = file->file_size;

        group = rm_shred_group_new(file, NULL);
        g_hash_table_insert(
            session->tables->size_groups,
            file_size_cpy,  
            group
        );
    }

    rm_shred_group_push_file(group, file, true);
}

static gboolean rm_shred_group_preprocess(_U gpointer key, RmShredGroup *group) {
    g_assert(group);
    if (group->status == RM_SHRED_GROUP_DORMANT) {
        rm_shred_group_free(group);
        return true;
    } else {
        return false;
    }
}

static void rm_shred_device_preprocess(_U gpointer key, RmShredDevice *device, RmMainTag *main) {
    g_queue_foreach(device->file_queue, (GFunc)rm_shred_file_get_offset_table, main->session);
}

static void rm_shred_preprocess_input(RmMainTag *main) {
    RmSession *session = main->session;

    /* move remaining files to RmShredGroups */
    g_assert(session->tables->node_table);

    rm_log_debug("Moving files into size_groups...");
    g_hash_table_foreach_remove(session->tables->node_table,
                                (GHRFunc)rm_shred_file_preprocess,
                                main);
    rm_log_debug("move remaining files to size_groups finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    rm_log_debug("Discarding unique sizes and read fiemap data for others...");
    g_hash_table_foreach_remove(session->tables->size_groups,
                                (GHRFunc)rm_shred_group_preprocess,
                                main);
    rm_log_debug("done at time %.3f\n", g_timer_elapsed(session->timer, NULL));

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
//    ACTUAL IMPLEMENTATION    //
/////////////////////////////////

/* Paranoid bitwise comparison of two rmfiles */
/* TODO: pairwise comparison requires 2(n-1) reads eg 6 reads for a cluster of 4 files; consider
 * instead comparing larger groups in parallel - this would require more mem (or smaller reads)
 * but saves (n-2) times reading in a whole file */
static bool rm_shred_byte_compare_files(RmMainTag *tag, RmFile *a, RmFile *b) {
    g_assert(a->file_size == b->file_size);

    int fd_a = rm_sys_open(a->path, O_RDONLY);
    if(fd_a == -1) {
        rm_log_perror("Unable to open file_a for paranoia");
        return false;
    } else {
        posix_fadvise(fd_a, 0, 0, SHRED_FADVISE_FLAGS);
    }

    int fd_b = rm_sys_open(b->path, O_RDONLY);
    if(fd_b == -1) {
        rm_log_perror("Unable to open file_b for paranoia");
        return false;
    } else {
        posix_fadvise(fd_b, 0, 0, SHRED_FADVISE_FLAGS);
    }

    bool result = true;
    int buf_size = rm_buffer_pool_size(tag->mem_pool) - offsetof(RmBuffer, data);

    struct iovec readvec_a[SHRED_PARANOIA_PAGES];
    struct iovec readvec_b[SHRED_PARANOIA_PAGES];

    for(int i = 0; i < SHRED_PARANOIA_PAGES; ++i) {
        RmBuffer *buffer = rm_buffer_pool_get(tag->mem_pool);
        readvec_a[i].iov_base = buffer->data;
        readvec_a[i].iov_len = buf_size;

        buffer = rm_buffer_pool_get(tag->mem_pool);
        readvec_b[i].iov_base = buffer->data;
        readvec_b[i].iov_len = buf_size;
    }

    while(result) {
        int bytes_a = readv(fd_a, readvec_a, SHRED_PARANOIA_PAGES);
        int bytes_b = readv(fd_b, readvec_b, SHRED_PARANOIA_PAGES);
        if(bytes_a <= 0 || bytes_b <= 0) {
            break;
        }

        g_assert(bytes_a == bytes_b);

        int remain = bytes_a % buf_size;
        int blocks = bytes_a / buf_size + !!remain;

        for(int i = 0; i < blocks && result; ++i) {
            RmBuffer *buf_a = readvec_a[i].iov_base - offsetof(RmBuffer, data);
            RmBuffer *buf_b = readvec_b[i].iov_base - offsetof(RmBuffer, data);
            int size = buf_size;

            if(i + 1 == blocks && remain > 0) {
                size = remain;
            }

            result = !memcmp(buf_a->data, buf_b->data, size);
        }
    }

    for(int i = 0; i < SHRED_PARANOIA_PAGES; ++i) {
        RmBuffer *buf_a = readvec_a[i].iov_base - offsetof(RmBuffer, data);
        RmBuffer *buf_b = readvec_b[i].iov_base - offsetof(RmBuffer, data);
        rm_buffer_pool_release(tag->mem_pool, buf_a);
        rm_buffer_pool_release(tag->mem_pool, buf_b);
    }

   rm_sys_close(fd_a);
   rm_sys_close(fd_b);

    return result;
}

static gint32 rm_shred_get_read_size(RmFile *file, RmMainTag *tag) {
    RmShredGroup *group = file->shred_group;
    g_assert(group);

    guint32 result = 0;

    g_mutex_lock(&group->lock);
    {
        /* calculate next_offset property of the RmShredGroup, if not already done */
        if (group->next_offset == 0) {
            guint64 target_bytes = (group->hash_offset == 0) ? SHRED_CHEAP_READ_BYTES : SHRED_CHEAP_SEEK_BYTES;

            /* round to even number of pages, round up to MIN_READ_PAGES */
            guint64 target_pages = MAX(target_bytes / tag->page_size, SHRED_MIN_READ_PAGES);
            target_bytes = target_pages * tag->page_size;

            /* test if cost-effective to read the whole file */
            if (group->hash_offset + target_bytes + SHRED_BALANCED_READ_BYTES >= group->file_size) {
                target_bytes = group->file_size - group->hash_offset;
            }

            group->next_offset = group->hash_offset + target_bytes;
        }
    }
    g_mutex_unlock(&group->lock);

    /* read to end of current file fragment, or to group->next_offset, whichever comes first */
    guint64 bytes_to_next_fragment = rm_offset_bytes_to_next_fragment(file->disk_offsets, file->seek_offset);

    if (bytes_to_next_fragment != 0 && bytes_to_next_fragment + file->seek_offset < group->next_offset) {
        file->status = RM_FILE_STATE_FRAGMENT;
        result = (bytes_to_next_fragment);
    } else {
        file->status = RM_FILE_STATE_NORMAL;
        result = (group->next_offset - file->seek_offset);
    }

    if(file->digest->type == RM_DIGEST_PARANOID) {
        result = MIN(result, rm_digest_paranoia_bytes());
    }

    return result;
}

/* Read from file and send to hasher
 * Note this was initially a separate thread but is currently just called
 * directly from rm_devlist_factory.
 * */
static void rm_shred_read_factory(RmFile *file, RmShredDevice *device) {
    int fd = 0;
    gint64 bytes_read = 0;
    guint64 total_bytes_read = 0;

    guint64 buf_size = rm_buffer_pool_size(device->main->mem_pool);
    buf_size -= offsetof(RmBuffer, data);

    gint64 bytes_to_read = rm_shred_get_read_size(file, device->main);

    g_assert(bytes_to_read > 0);
    g_assert(bytes_to_read + file->hash_offset <= file->file_size);
    g_assert(file->seek_offset == file->hash_offset);

    struct iovec readvec[SHRED_MAX_PAGES + 1];

    if(file->seek_offset >= file->file_size) {
        goto finish;
    }

    fd = rm_sys_open(file->path, O_RDONLY);
    if(fd == -1) {
        rm_log_perror("open failed");
        file->status = RM_FILE_STATE_IGNORE;
        g_async_queue_push(device->hashed_file_return, file);
        goto finish;
    }

    /* preadv() is benifitial for large files since it can cut the
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
    const gint16 N_BUFFERS = 4;

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

    while(bytes_to_read > 0 && (bytes_read = rm_sys_preadv(fd, readvec, N_BUFFERS, file->seek_offset)) > 0) {
        int blocks = DIVIDE_CEIL(bytes_read,  buf_size);

        bytes_to_read -= bytes_read;
        file->seek_offset += bytes_read;
        total_bytes_read += bytes_read;

        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
            buffer->file = file;
            buffer->len = MIN (buf_size, bytes_read - i * buf_size);
            buffer->is_last = (i + 1 >= blocks && bytes_to_read <= 0); //TODO: why does bytes_to_read sometimes go negative? 

            if (buffer->is_last) {
                //TODO: add check for expect byte count; if wrong then set state to ignore.
            }

            /* Send it to the hasher */
            rm_util_thread_pool_push(device->hash_pool, buffer);

            /* Allocate a new buffer - hasher will release the old buffer */
            buffer = rm_buffer_pool_get(device->main->mem_pool);
            readvec[i].iov_base = buffer->data;
            readvec[i].iov_len = buf_size;
        }
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

    /* Update totals for device */
    g_mutex_lock(&(file->device->lock));
    {
        file->device->remaining_bytes -= total_bytes_read;
    }
    g_mutex_unlock(&(device->lock));

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
        g_async_queue_push(device->hashed_file_return, buffer->file);
    }

    /* Return this buffer to the pool */
    rm_buffer_pool_release(device->main->mem_pool, buffer);
}

static int rm_shred_check_paranoia(RmMainTag *tag, GQueue *candidates) {
    int failure_count = 0;

    for(GList *iter_a = candidates->head; iter_a; iter_a = iter_a->next) {
        RmFile *a = iter_a->data;

        for(GList *iter_b = iter_a->next; iter_b; iter_b = iter_b->next) {
            RmFile *b = iter_b->data;
            if(!rm_shred_byte_compare_files(tag, a, b)) {
                failure_count++;
                //TODO: discard file
                //rm_shred_set_file_state(tag, b, RM_FILE_STATE_IGNORE);
            }
        }
    }

    return failure_count;
}

static RmFile *rm_group_find_original(RmSession *session, GQueue *group/*, gboolean recurse*/) {
    RmFile *result = NULL;
    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        if (/*recurse && */file->hardlinks.files) {
            RmFile *hardlink_original = rm_group_find_original(session, file->hardlinks.files/*, false*/);
            if (!result) {
                result = hardlink_original;
            }
        } else if (
            ((file->is_prefd) && (session->settings->keep_all_originals)) ||
            ((file->is_prefd) && (!result))
        ) {
            rm_file_tables_remember_original(session->tables, file);
            if(!result) {
                result = file;
            }
        }
    }
    return result;
}

static void rm_group_fmt_write(RmSession *session, RmShredGroup *shred_group, GQueue *group, RmFile *original_file /*, gboolean recurse*/) {
    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        if (/*recurse &&*/ file->hardlinks.files) {
            rm_group_fmt_write(session, shred_group, file->hardlinks.files, original_file/*, false*/);
        } else {
            if(iter->data != original_file) {
                RmFile *lint = iter->data;
                session->dup_counter += 1;
                session->total_lint_size += lint->file_size;

                /* Fake file->digest for a moment */
                lint->digest = shred_group->digest;
                rm_fmt_write(session->formats, lint);
                lint->digest = NULL;
            }
        }
    }
}

void rm_shred_forward_to_output(RmSession *session, RmShredGroup *shred_group, GQueue *group) {
    session->dup_group_counter++;

    RmFile *original_file = rm_group_find_original(session, group/*, true*/);

    if(!original_file) {
        /* tag first file as the original */
        original_file = group->head->data;
        rm_file_tables_remember_original(session->tables, original_file);
    }

    /* Hand it over to the printing module */
    original_file->digest = shred_group->digest;
    rm_fmt_write(session->formats, original_file);
    original_file->digest = NULL;

    rm_group_fmt_write(session, shred_group, group, original_file/*, true*/);
}

static void rm_shred_result_factory(RmShredGroup *group, RmMainTag *tag) {
    if(tag->session->settings->paranoid) {
        int failure_count = rm_shred_check_paranoia(tag, group->held_files);
        if(failure_count > 0) {
            rm_log_warning("Removed %d files during paranoia check.\n", failure_count);
        }
    }

    if(g_queue_get_length(group->held_files) > 0) {
        rm_shred_forward_to_output(tag->session, group, group->held_files);
    }

    group->status = RM_SHRED_GROUP_DORMANT;
    rm_shred_group_free(group);
}

static void rm_shred_devlist_factory(RmShredDevice *device, RmMainTag *main) {
    g_assert(device);
    g_assert(device->hash_pool); /* created when device created */

    rm_log_debug(BLUE"Started rm_shred_devlist_factory for disk %s (%u:%u) with %"LLU" files in queue\n"RESET,
                 device->disk_name,
                 major(device->disk),
                 minor(device->disk),
                 (guint64)g_queue_get_length(device->file_queue)
                );

    if(device->is_rotational) {
        g_queue_sort(device->file_queue, (GCompareDataFunc)rm_shred_compare_file_order, NULL);
    }

    /* scheduler for one file at a time, optimised to minimise seeks */
    GList *iter = device->file_queue->head;
    while(iter && !rm_session_was_aborted(main->session)) {
        RmFile *file = iter->data;
        guint64 start_offset = file->hash_offset;

        /* initialise hash (or recovery progressive hash so far) */
        if (!file->digest) {
            g_assert(file->shred_group);
            if (!file->shred_group->digest) {
                /* this is first generation of RMGroups, so there is no progressive hash yet */
                g_assert(file->hash_offset == 0);
                file->digest = rm_digest_new(main->session->settings->checksum_type, 0, 0); //TODO: seeds?
            } else {
                file->digest = rm_digest_copy(file->shred_group->digest);
                if (file->digest->type == RM_DIGEST_PARANOID) {
                    /* paranoid digest is non-cumulative, so needs to be reset to start each generation */
                    file->digest->paranoid_offset = 0;
                }

            }
        }

        /* hash the next increment of the file */
        rm_shred_read_factory(file, device);

        /* wait until the increment has finished hashing */
        RmFile *popped = g_async_queue_pop(device->hashed_file_return);
        if (file->status == RM_FILE_STATE_IGNORE) {
            rm_shred_group_unref(file->shred_group);
            rm_shred_discard_file(file);
        } else {
            file = popped;

            if (file->status == RM_FILE_STATE_FRAGMENT) {
                /* file is not ready for checking yet; push it back into the queue */
                rm_shred_push_queue_sorted(file);
            } else if(rm_shred_sift(file)) {
                /* continue hashing same file, ie no change to iter */
                if (start_offset == file->hash_offset) {
                    rm_log_error(RED"Offset stuck at %"LLU"\n", start_offset);
                }
                continue;
            } else {
                /* rm_shred_sift has taken responsibility for the file */
            }
        }

        GList *next = iter->next;
        g_queue_delete_link(device->file_queue, iter);
        iter = next;
    }

    /* threadpool thread terminates but the device will be recycled via
     * the device_return queue
     */
    rm_log_debug(BLUE"Pushing back device %d\n"RESET, (int)device->disk);
    g_async_queue_push(main->device_return, device);
}

static void rm_shred_create_devpool(RmMainTag *tag, GHashTable *dev_table) {
    tag->device_pool = rm_util_thread_pool_new(
                           (GFunc)rm_shred_devlist_factory, tag, tag->session->settings->threads / 2 + 1
                       );

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, dev_table);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        RmShredDevice *device = value;
        rm_log_debug(GREEN"Pushing device %s to threadpool\n", device->disk_name);
        rm_util_thread_pool_push(tag->device_pool, device);
    }
}

void rm_shred_run(RmSession *session) {
    g_assert(session);
    g_assert(session->tables);
    g_assert(session->tables->node_table);

    RmMainTag tag;
    tag.session = session;
    tag.mem_pool = rm_buffer_pool_init(sizeof(RmBuffer) + SHRED_PAGE_SIZE);
    tag.device_return = g_async_queue_new();
    tag.page_size = SHRED_PAGE_SIZE;
    tag.totalfiles = 0;

    /* would use g_atomic, but helgrind does not like that */
    g_mutex_init(&tag.file_state_mtx);

    session->tables->dev_table = g_hash_table_new_full(
                                     g_direct_hash, g_direct_equal,
                                     NULL, (GDestroyNotify)rm_shred_device_free
                                 );

    rm_shred_preprocess_input(&tag);

    /* Remember how many devlists we had - so we know when to stop */
    int devices_left = g_hash_table_size(session->tables->dev_table);
    rm_log_debug(BLUE"Devices = %d\n", devices_left);

    /* For results that need to be check with --paranoid.
     * This would clog up the main thread, which is supposed
     * to flag bad files as soon as possible.
     */
    tag.result_pool = rm_util_thread_pool_new(
                          (GFunc)rm_shred_result_factory, &tag, 1
                      );

    /* Create a pool fo the devlists and push each queue */
    rm_shred_create_devpool(&tag, session->tables->dev_table);

    /* This is the joiner part */
    while(devices_left > 0 || g_async_queue_length(tag.device_return) > 0) {
        RmShredDevice *device = g_async_queue_pop(tag.device_return);
        g_mutex_lock(&device->lock);
        {
            rm_log_debug(BLUE"Got device %s back with %d in queue and %llu bytes remaining in %d remaining files\n"RESET,
                         device->disk_name,
                         g_queue_get_length(device->file_queue),
                         (unsigned long long)device->remaining_bytes,
                         device->remaining_files
                        );

            if (device->remaining_files > 0) {
                /* recycle the device */
                rm_util_thread_pool_push(tag.device_pool , device);
            } else {
                devices_left--;
            }
        }
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

    g_hash_table_unref(session->tables->dev_table);
    g_mutex_clear(&tag.file_state_mtx);
}
