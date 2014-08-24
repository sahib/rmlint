#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/uio.h>

#include "checksum.h"
#include "checksums/city.h"

#include "postprocess.h"
#include "preprocess.h"
#include "utilities.h"

#include "shredder.h"
#include <inttypes.h>

//TODO: for hardlinked originals, only hash one of each set

/* This is the scheduler of rmlint.
 *
 * The threading looks somewhat like this for two devices:
 *
  * Device #1                                              Device #2
 *
 *                           +----------+
 *                           | Finisher |
 *                           |  Thread  |
 *                           |  incl    |
 *                           | paranoid |
 *                           +----------+
 *                                 ^
 *                                 |
 *                         +--------------+
 *                         | Matched      |
 *                         | fully-hashed |
 *                         | dupe groups  |
 *                         +--------------+
 *                                 ^
 * +---------------------+         |         +---------------------+
 * |     +-------------+ |    +---------+    | +-------------+     |
 * |     | Devlist Mgr |<-----| Matched |----->| Devlist Mgr |     |
 * |     +-------------+ |    | partial |    | +-------------+     |
 * |    pop              |    | hashes  |    |              pop    |
 * |  nearest            |    |         |    |            nearest  |
 * |  offset             |    |         |    |            offset   |
 * |     |               |    |---------|    |               |     |
 * |     v               |    |         |    |               v     |
 * | +------+   +------+ |    | Shred   |    | +------+   +------+ |
 * | | Read |-->| Hash |----->| Groups  |<-----| Hash |<--| Read | |
 * | |  (n) |   | (1)  |<-cont| (mutex) |cont->| (1)  |   |(n)   | |
 * | +------+   +------+ |    +---------+      +------+   +------+ |
 * +---------------------+         ^         +---------------------+
 *                                 |
 *                           Initial file list
 *
 *
 * Every subbox left and right are the task that are performed.
 *
 * Every task is backed up by a GThreadPool, this allows regulating the number
 * of threads easily and e.g. use more reading threads for nonrotational
 * devices.
 *
 * On init every device gets it's own thread. This thread spawns reader and
 * hasher threads from two more GTHreadPools. The initial thread works as
 * manager for the spawnend threads. The manager repeats reading the files on
 * its device until no file is flagged with RM_FILE_STATE_PROCESS as state.
 * (rm_shred_devlist_factory). On each iteration the block size is incremented, so
 * the next round reads more data, since it gets increasingly less likely to
 * find differences in files. Additionally on every few iterations the files in
 * the devlist are resorted according to their physical block on the device.
 *
 * The reader thread(s) read one file at a time using readv(). The buffers for
 * it come from a central buffer pool that allocates some and just reuses them
 * over and over. The buffer which contain the read data are pusehd to the
 * hasher thread, where the data-block is hashed into file->digest.  The buffer
 * is released back to the pool after use.
 *
 * Once the hasher is done, the file is send back to the mainthread via a
 * GAsyncQueue. There a table with the hash_offset and the file_size as key and
 * a list of files is updated with it. If one of the list is as long as the full
 * list found during traversing, we know that we compare these files with each
 * other.
 *
 * On comparable groups rm_shred_findmatches() is called, which finds files that
 * can be ignored and files that are finished already. In both cases file->state
 * is modified accordingly. In the latter case the group is processed; i.e.
 * written to log, stdout and script.
 *
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
 *  | F1,F2,F3   | |F4,F5,F6  |     |F7,F8,F9,F10|  |  F11    |  |F12 | |F13 |
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
 * The basic workflow is:
 * 1. Pick a file from the device_queue
 * 2. Hash the next increment
 * 3. Check back with the child's parent to see if there is a child ShredGroup with
 *    matching hash; if not then create a new one.
 * 4. Add the file into the child ShredGroup and unlink it from its parent(see note
 *    below on Unlinking from Parent)
 * 5. Check if the child ShredGroup meets criteria for hashing; if no then loop
 *    back to (1) for another file to hash
 * 6. If file meets criteria and is not finished hash then loop back to 2 and
 *    hash its next increment
 * 7. If file meets criteria and is fully hashed then flag it as ready for post-
 *    processing (possibly via paranoid check).  Note that post-processing can't
 *    start until the ShredGroup's parent is dead (because new siblings may still
 *    be coming).
 *
 * The above is all carried out by xxxx, which is called from the device worker thread(s).
 *
 *
 * Unlinking a file from parent:
 * Decrease child_file counter; if that is zero then check if the parent's parent is
 * dead; if yes then kill the parent.
 * When killing the parent, tell all its children that they are now orphans and check
 * if it is time for them to die too.
 * Note that we need to be careful here to avoid threadlock, eg:
 *    Child file 1 on device 1 has finished an increment.  It takes a lonk on its new
 *    RmGroup so that it can add itself.  It then locks its parent ShredGroup so that
 *    it can do the unlinking.  If it turns out it is time for the parent to die, we
 *    we need to lock each of its children so that we can make them orphans:
 *        Q: What if another one of its other children was also trying to unlink?
 *        A: No problem, can't happen (since parent can't be ready to die if it has
 *           any active children left)
 *        Q: What about the mutex loop from this child which is doing the unlinking?
 *        A: No problem, either unlock the child's mutex before calling parent unlink,
 *           or handle the calling child differently from the other children
 */

/*
*
* Below some performance controls are listed that may impact performance.
* Benchmarks are left to determine reasonable defaults. TODO therefore.  The
* controls are sorted by subjectve importanceness.
*/

/* How much buffers to keep allocated at max. */
#define SHRED_MAX_PAGES       (64)

/* How large a single page is (typically 4096 bytes but not always)*/
#define SHRED_PAGE_SIZE       (sysconf(_SC_PAGESIZE))

/* How many pages are read initially at max.  This value is important since it
 * decides how much data will be read for small files, so it should not be too
 * large nor too small, since reading small files twice is very slow.  Logic for
 * picking 32 kib is based on typical seek time vs read time:
 * (eg http://www.anandtech.com/show/8265/wd-red-pro-review-4-tb-drives-for-nas-systems-benchmarked/4)
 * Typical hard drive random access time is ~15ms,
 * Typical data transfer rate ~100MB/s,
 * so reading 1 Byte will take 15.00001 ms
 * while reading 32 kiB will take 15.3 ms
 * in fact we could go up to 256 MiB (17.6 ms) without much penalty but then we start to get
 * significant risk of file fragmentation costing us (a) an extra seek and (b) messing up the
 * queue of sorted offsets.  So 32kiB is a resonable compromise.
 * TODO: why not CLAMP(x, 4kiB, 256kiB), where x is the smallest first fragment in the ShredGroup...
 */


/* Flags for the fadvise() call that tells the kernel
 * what we want to do with the file.
 */
#define SHRED_FADVISE_FLAGS   (0                                                         \
                               | POSIX_FADV_SEQUENTIAL /* Read from 0 to file-size    */ \
                               | POSIX_FADV_WILLNEED   /* Tell the kernel to readhead */ \
                               | POSIX_FADV_NOREUSE    /* We will not reuse old data  */ \
                              )                                                          \
 
/* After how many files the join-table is cleaned up from old entries.  This
 * settings will not have much performance impact, just keeps memory a bit
 * lower.
 */
#define SHRED_GC_INTERVAL     (2000)


////////////////////////////////////////////
// Optimisation parameters for deciding   //
// how many bytes to read before stopping //
// to compare progressive hashes          //
////////////////////////////////////////////


/* Maximum number of bytes to read in one pass.
 * Never goes beyond SHRED_MAX_READ_SIZE unless the extra bytes to finish the file are "cheap".
 * Never goes beyond SHRED_HARD_MAX_READ_SIZE.
 */
#define SHRED_MAX_READ_SIZE   (512 * 1024 * 1024)
#define SHRED_HARD_MAX_READ_SIZE   (1024 * 1024 * 1024)

/* Minimum number of pages to read */
#define MIN_READ_PAGES (1)

/* expected typical seek time in milliseconds */
#define SEEK_MS (5)
/** Note that 15 ms this would be appropriate value for random reads
 * but for short seeks in the 1MB to 1GB range , 5ms is more apprpriate; refer:
 * https://www.usenix.org/legacy/event/usenix09/tech/full_papers/vandebogart/vandebogart_html/index.html
 */

/* expected typical sequential read speed in MB per second */
#define READRATE (100)

/* define what we consider cheap as a fraction of total cost, for example a cheap read has
 * a read time less than 1/50th the seek time, or a cheap seek has a seek time less then 1/50th
 * the total read time */
#define CHEAP (50)

/* how many pages can we read in (seek_time)? (eg 5ms seeking folled by 5ms reading) */
#define BALANCED_READ_BYTES (SEEK_MS * READRATE * 1024) // ( * 1024 / 1000 to be exact)

/* how many bytes do we have to read before seek_time becomes CHEAP relative to read time? */
#define CHEAP_SEEK_BYTES (BALANCED_READ_BYTES * CHEAP)

/* how many pages can we read in (seek_time)/(CHEAP)? (use for initial read) */
#define CHEAP_READ_BYTES (BALANCED_READ_BYTES / CHEAP)

/* for reference, if SEEK_MS=5, READRATE=100 and CHEAP=50 then BALANCED_READ_BYTES=0.5MB,
 * CHEAP_SEEK_BYTES=25MB and CHEAP_READ_BYTES=10kB. */


/* How many pages to use during paranoid byte-by-byte comparison?
 * More pages use more memory but result in less syscalls.
 */
#define SHRED_PARANOIA_PAGES  (64)

//////////////////////////////
//    INTERNAL STRUCTURES    //
//////////////////////////////

typedef struct RmBufferPool {
    /* Place where the buffers are stored */
    GTrashStack *stack;

    /* how many buffers are available? */
    guint64 size;

    /* concurrent accesses may happen */
    GMutex lock;
} RmBufferPool;


/* The main extra data for the scheduler */
typedef struct RmMainTag {
    RmSession *session;
    RmBufferPool *mem_pool;
    GAsyncQueue *device_return;
    GMutex file_state_mtx;
    GThreadPool *device_pool;
    GThreadPool *result_pool;
    guint32 page_size;
} RmMainTag;


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

/* Easy way to use arbitary structs as key in a GHashTable.
 * Creates a fitting rm_shred_equal, rm_shred_hash and rm_shred_copy
 * function for every data type that works with sizeof().
 */
#define CREATE_HASH_FUNCTIONS(name, HashType)                                  \
    static gboolean rm_shred_equal_##name(const HashType* a, const HashType *b) { \
        return !memcmp(a, b, sizeof(HashType));                                \
    }                                                                          \
                                                                               \
    static guint rm_shred_hash_##name(const HashType * k) {                       \
        return CityHash64((const char *)k, sizeof(HashType));                  \
    }                                                                          \
                                                                               \
    static HashType * rm_shred_copy_##name(const HashType * self) {               \
        HashType *mem = g_new0(HashType, 1);                                   \
        memcpy(mem, self, sizeof(HashType));                                   \
        return mem;                                                            \
    }                                                                          \
 


typedef struct RmCksumKey {
    guint8 data[_RM_HASH_LEN];
} RmCksumKey;

/* create GHashTable boilerplate implicitly */
CREATE_HASH_FUNCTIONS(cksum_key, RmCksumKey);


enum RM_SHRED_GROUP_STATUS {
    RM_SHRED_GROUP_DORMANT = 0,
    RM_SHRED_GROUP_START_HASHING,
    RM_SHRED_GROUP_HASHING,
    RM_SHRED_GROUP_FINISHING
};

typedef struct ShredGroup {
    GQueue *held_files; /** holding queue for files; they are held here until the group first meets
                         * criteria for further hashing (normally just 2 or more files, but sometimes
                         * related to preferred path counts) */

    GQueue *children;   /** link(s) to next generation of ShredGroups(s) which have this ShredGroup as parent*/

    struct ShredGroup *parent; /** ShredGroup of the same size files but with lower RmFile->hash_offset; gets
                                * set to null when parent dies*/

    gulong remaining;    /** number of child group files that have not completed next level of hashing */
    gboolean has_pref;   /** set if group has 1 or more files from "preferred" paths */
    gboolean has_npref;  /** set if group has 1 or more files from "non-preferred" paths */
    gboolean needs_pref; /** set based on settings->must_match_original */
    gboolean needs_npref;/** set based on settings->keep_all_originals */

    char status;         /** initially RM_SHRED_GROUP_DORMANT; triggered as soon as we have >= 2 files and
						 * meet preferred path and will go to either RM_SHRED_GROUP_HASHING or
						 * RM_SHRED_GROUP_FINISHING.  When switching from dormant to hashing, all held_files
						 * are released and future arrivals go straight to hashing */

    guint64 file_size;   /** file size of files in this group */
    guint64 hash_offset; /** file hash_offset when files arrived in this group */
    guint64 frag_offset; /** starting from hash_offset, when do we next hit file fragmentation in one of the RmFiles? */
    guint64 next_offset; /** file hash_offset for next increment */

    GMutex lock;        /** needed because different device threads read and write to this structure */
    RmCksumKey checksum; /** key which distinguishes this group from its siblings */
} ShredGroup;


typedef struct ShredDevice {
    /* queue of files awaiting (partial) hashing, sorted by disk offset.  Note
     * this can be written to be other threads so requires mutex protection */
    GQueue *file_queue;

    /* Counters, used to determine when there is nothing left to do.  These
     * can get written to by other device threads so require mutex protection */
    guint32 remaining_files;
    guint64 remaining_bytes;

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

    /* size of one page, cached, so
     * sysconf() does not need to be called always.
     */
    guint64 page_size;
    RmMainTag *main;
} ShredDevice;

/* Copied file-metadata relevant for match filtering.
 * Data needs to be copied since the ref_file might be
 * modified at any time. Plus: lock times can be kept low.
 */
typedef struct RmFileSnapshot {
    RmCksumKey checksum;
    guint64 hash_offset;
    RmFile *ref_file;
} RmFileSnapshot;

static RmFileSnapshot *rm_shred_create_snapshot(RmFile *file) {
    RmFileSnapshot *self = g_slice_new0(RmFileSnapshot);
    self->hash_offset = file->hash_offset;
    self->ref_file = file;

    rm_digest_steal_buffer(&file->digest, self->checksum.data, sizeof(self->checksum.data));
    return self;
}




/* Determines the next amount of bytes_read to read.
 * Currently just doubles the amount.
 * */
static guint64 rm_shred_get_read_size(RmFile *file, RmMainTag *tag) {
    ShredGroup *group = file->shred_group;
    g_mutex_lock(&group->lock);
    {
        if (group->next_offset == 0) {

            guint64 target_bytes = (group->hash_offset == 0) ? CHEAP_READ_BYTES : CHEAP_SEEK_BYTES;

            /* round to even number of pages, round up to MIN_READ_PAGES */
            guint64 target_pages = MAX( target_bytes / tag->page_size, MIN_READ_PAGES );
            target_bytes = target_pages * tag->page_size;

            /* If there is a file fragmentation within BALANCED_READ_BYTES of the target then swap for that */
            if (group->frag_offset) {
                if (1
                        && group->frag_offset - (group->hash_offset + target_bytes) < BALANCED_READ_BYTES
                        && (group->hash_offset + target_bytes) - group->frag_offset < BALANCED_READ_BYTES
                   ) {
                    target_bytes = group->frag_offset - group->hash_offset;
                }
            }

            /* test if cost-effective to read the whole file */
            if (group->hash_offset + target_bytes >= group->file_size - BALANCED_READ_BYTES) {
                target_bytes = group->file_size - group->hash_offset;
            }

            group->next_offset = group->hash_offset + target_bytes;
        }
    }
    g_mutex_unlock(&group->lock);

    /* read to end of current file fragment, or to group->next_offset, whichever comes first */
    guint64 bytes_to_next_fragment = rm_offset_bytes_to_next_fragment(file->disk_offsets, file->seek_offset);
    if (bytes_to_next_fragment
            && bytes_to_next_fragment < group->next_offset - file->seek_offset) {
        file->fragment_hash = true;
        return(bytes_to_next_fragment);
    } else {
        file->fragment_hash = false;
        return group->next_offset - file->seek_offset;
    }
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

//~ static void rm_shred_set_file_state(RmMainTag *tag, RmFile *file, RmFileState state) {
//~ g_mutex_lock(&tag->file_state_mtx);
//~ {
//~ file->state = state;
//~ }
//~ g_mutex_unlock(&tag->file_state_mtx);
//~ }

//~ static RmFileState rm_shred_get_file_state(RmMainTag *tag, RmFile *file) {
//~ RmFileState state = 0;
//~ g_mutex_lock(&tag->file_state_mtx);
//~ {
//~ state = file->state;
//~ }
//~ g_mutex_unlock(&tag->file_state_mtx);
//~ return state;
//~ }

/* Paranoid bitwise comparison of two rmfiles */
/* TODO: pairwise comparison requires 2(n-1) reads eg 6 reads for a cluster of 4 files; consider
 * instead comparing larger groups in parallel - this would require more mem (or smaller reads)
 * but saves (n-2) times reading in a whole file */
static bool rm_shred_byte_compare_files(RmMainTag *tag, RmFile *a, RmFile *b) {
    g_assert(a->file_size == b->file_size);

    int fd_a = open(a->path, O_RDONLY);
    if(fd_a == -1) {
        rm_perror("Unable to open file_a for paranoia");
        return false;
    } else {
        posix_fadvise(fd_a, 0, 0, SHRED_FADVISE_FLAGS);
    }

    int fd_b = open(b->path, O_RDONLY);
    if(fd_b == -1) {
        rm_perror("Unable to open file_b for paranoia");
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

    close(fd_a);
    close(fd_b);

    return result;
}

/////////////////////////////////
//  GThreadPool Wrappers       //
/////////////////////////////////

/* wrapper for g_thread_pool_push with error reporting */
static bool rm_shred_thread_pool_push(GThreadPool *pool, gpointer data) {
    GError *error = NULL;
    g_thread_pool_push(pool, data, &error);
    if(error != NULL) {
        rm_error("Unable to push thread to pool %p: %s\n", pool, error->message);
        g_error_free(error);
        return false;
    } else {
        return true;
    }
}

/* wrapper for g_thread_pool_new with error reporting */
static GThreadPool *rm_shred_thread_pool_new(GFunc func, gpointer data, int threads) {
    GError *error = NULL;
    GThreadPool *pool = g_thread_pool_new(func, data, threads, FALSE, &error);

    if(error != NULL) {
        rm_error("Unable to create thread pool.\n");
        g_error_free(error);
    }
    return pool;
}


#define DIVIDE_ROUND_UP(n,m) ( (n) / (m) + !!((n) % (m)) )

/////////////////////////////////
//    ACTUAL IMPLEMENTATION    //
/////////////////////////////////

static void rm_shred_read_factory(RmFile *file, ShredDevice *device) {
    g_assert(device);
    g_assert(file);

    int fd = 0;
    gint32 bytes_read = 0;
    guint32 buf_size = rm_buffer_pool_size(device->main->mem_pool) - offsetof(RmBuffer, data);
    guint32 bytes_to_read = rm_shred_get_read_size(file, device->main);

    struct iovec readvec[SHRED_MAX_PAGES + 1];

    if(file->seek_offset >= file->file_size) {
        goto finish;
    }

    fd = open(file->path, O_RDONLY);
    if(fd == -1) {
        perror("open failed");
        // TODO: discard file rm_shred_set_file_state(device->main, file, RM_FILE_STATE_IGNORE);
        goto finish;
    }

    /* how many buffers to read half of bytes_to_read? */
    guint16 N_BUFFERS = MIN(SHRED_MAX_PAGES, DIVIDE_ROUND_UP(bytes_to_read, device->main->page_size) );

    /* Give the kernel scheduler some hints */
    posix_fadvise(fd, file->seek_offset, bytes_to_read, SHRED_FADVISE_FLAGS);

    /* Initialize the buffers to begin with.
     * After a buffer is full, a new one is retrieved.
     */
    for(int i = 0; i < N_BUFFERS; ++i) {
        /* buffer is one contignous memory block */
        RmBuffer *buffer = rm_buffer_pool_get(device->main->mem_pool);
        readvec[i].iov_base = buffer->data;
        readvec[i].iov_len = buf_size;
    }

    guint16 buffers_to_read = N_BUFFERS;

    while(bytes_to_read > 0 && (bytes_read = preadv(fd, readvec, buffers_to_read, file->seek_offset)) > 0) {
        int blocks = DIVIDE_ROUND_UP(bytes_read,  buf_size);

        bytes_to_read -= bytes_read;
        file->seek_offset += bytes_read;

        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
            buffer->file = file;
            buffer->len = MIN (buf_size, bytes_read - i * buf_size);
            buffer->is_last = (i + 1 == blocks && bytes_to_read == 0);

            /* Send it to the hasher */
            rm_shred_thread_pool_push(device->hash_pool, buffer);

            /* Allocate a new buffer - hasher will release the old buffer */
            buffer = rm_buffer_pool_get(device->main->mem_pool);
            readvec[i].iov_base = buffer->data;
            readvec[i].iov_len = buf_size;
        }

        /* how many buffers to read half of remaining bytes_to_read? */
        buffers_to_read = MIN(buffers_to_read, DIVIDE_ROUND_UP(bytes_to_read, device->main->page_size) );
        /**
         * NOTE: The above "read half" strategy is aimed at having maximum N_BUFFERS initially (to minimise
         * the number of preadv calls) while also minimising the lag between when rm_read_factory finishes
         * reading bytes_to_read and when rm_shred_hash_factory finishes hashing. */
    }

    /* Release the rest of the buffers */
    for(int i = 0; i < N_BUFFERS; ++i) {
        RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
        rm_buffer_pool_release(device->main->mem_pool, buffer);
    }

    file->phys_offset = rm_offset_lookup(file->disk_offsets, file->phys_offset);

finish:
    g_async_queue_lock(device->hashed_file_return);
    {
        //TODO: do we really need this two-step or can we just g_async_queue_push?
        g_async_queue_push_unlocked(device->hashed_file_return, file);
    }
    g_async_queue_unlock(device->hashed_file_return);

    if(fd > 0) {
        close(fd);
    }
}

static void rm_shred_hash_factory(RmBuffer *buffer, ShredDevice *device) {
    g_assert(device);
    g_assert(buffer);

    g_mutex_lock(&buffer->file->file_lock);  //TODO: don't need this with the new scheduler?
    {
        /* Hash buffer->len bytes_read of buffer->data into buffer->file */
        rm_digest_update(&buffer->file->digest, buffer->data, buffer->len);
        buffer->file->hash_offset += buffer->len;

        if (buffer->is_last) {
            /* Report the progress to rm_shred_devlist_factory */
            g_async_queue_push(device->hashed_file_return, buffer->file);
        }
    }
    g_mutex_unlock(&buffer->file->file_lock);

    /* Return this buffer to the pool */
    rm_buffer_pool_release(device->main->mem_pool, buffer);
}

#define SGN(X, Y)  ( ((X) > (Y)) - ((X) < (Y)) )


static int rm_shred_compare_file_order(const RmFile *a, const RmFile *b, G_GNUC_UNUSED gpointer user_data) {
    /* offset is a guint64, so do not substract them.
     * (will cause over or underflows on regular base)
     */
    /* compare based on partition (dev), then offset, then inode */
    return (4 * SGN(a->dev, b->dev)
            + 2 * SGN(a->phys_offset, b->phys_offset)
            + 1 * SGN(a->inode, b->inode) );
}

static int rm_shred_get_read_threads(RmMainTag *tag, bool nonrotational, int max_threads) {
    if(!nonrotational) {
        return 1;
    } else {
        int devices_running = g_thread_pool_get_num_threads(tag->device_pool);
        return CLAMP((max_threads - devices_running) / devices_running, 1, 16);
    }
}

static void rm_shred_file_queue_push(ShredDevice *device, RmFile *file) {
    g_mutex_lock(&device->lock);
    g_queue_insert_sorted(device->file_queue,
                          file,
                          (GCompareDataFunc)rm_shred_compare_file_order,
                          NULL);
    g_mutex_unlock(&device->lock);
}

static GList *rm_shred_file_queue_get_next_remove(ShredDevice *device, GList *current) {

    GList *result;
    g_mutex_lock(&device->lock);
    {
        if (current) {
            /* temporarily manipulate the current file to reflect the disk head position at
             * the end of the last read */
            RmFile *current_file = current->data;
            guint64 save_offset = current_file->phys_offset;
            current_file->phys_offset = rm_offset_lookup(current_file->disk_offsets,
                                        current_file->seek_offset - 1);

            /* iterate over the list to find the nearest file after disk head position */
            result = current->next;
            int direction = rm_shred_compare_file_order(result->data, current_file, NULL);
            if (direction == -1) {
                result = current->prev;
            }
            while (1
                    && result
                    && rm_shred_compare_file_order(result->data, current_file, NULL) == direction
                  ) {
                result = (direction >= 0) ? result->next : result->prev;
            }

            /* restore current_file->phys_offset to true value*/
            current_file->phys_offset = save_offset;
            /* remove current from the queue then advance to next*/
            g_queue_delete_link(device->file_queue, current);

            /* move forwards one file if we were searching backwards */
            if (direction == -1) {
                if (result) {
                    result = result->next;
                } else {
                    result = device->file_queue->head;
                }
            }

        } else {
            result = device->file_queue->head;
        }
    }
    g_mutex_unlock(&device->lock);
    return result;
}


void shred_discard_file(RmFile *file) {
    ShredDevice *device = file->device;
    g_mutex_lock(&(device->lock));
    device->remaining_files--;
    device->remaining_bytes -= (file->file_size - file->hash_offset);
    g_mutex_unlock(&(device->lock));
    rm_file_destroy(file);
    //TODO: SHRED_FADVISE no longer required
}

ShredGroup *shred_group_new(RmFile *file) {

    ShredGroup *self = g_slice_new0(ShredGroup);

    //TODO: memcpy(&self->checksum, checksum, sizeof(self->checksum));
    rm_digest_steal_buffer(&file->digest, self->checksum.data, sizeof(self->checksum));

    self->parent = file->shred_group;
    if(self->parent) {
        self->needs_npref = self->parent->needs_npref;
        self->needs_pref = self->parent->needs_pref;
    }

    self->held_files = g_queue_new();
    self->children   = g_queue_new();

    self->file_size = file->file_size;
    self->hash_offset = file->hash_offset;

    g_assert(self->remaining == 0);  //TODO: remove asserts
    g_assert(!self->has_pref);
    g_assert(!self->has_npref);
    g_assert(!self->status == RM_SHRED_GROUP_DORMANT);

    g_mutex_init(&(self->lock));
    return self;
}



/* header for shred_group_make_orphan since it and shred_group_free reference each other  */
void shred_group_make_orphan(ShredGroup *self);

void shred_group_free(ShredGroup *self) {
    g_assert(self->parent == NULL);  // children should outlive their parents!

    /** discard RmFiles which failed file duplicate criteria */
    g_queue_free_full(self->held_files, (GDestroyNotify)shred_discard_file);

    /** give our children the bad news */
    g_queue_free_full(self->children, (GDestroyNotify)shred_group_make_orphan);

    /** clean up */
    g_mutex_clear(&self->lock);
    //g_free(self->checksum);
    g_slice_free(ShredGroup, self);
}



static int rm_shred_check_paranoia(RmMainTag *tag, GQueue *candidates) {
    int failure_count = 0;

    for(GList *iter_a = candidates->head; iter_a; iter_a = iter_a->next) {
        RmFile *a = iter_a->data;
        //~ if(rm_shred_get_file_state(tag, a) != RM_FILE_STATE_PROCESS) {
        //~ continue;
        //~ }

        for(GList *iter_b = iter_a->next; iter_b; iter_b = iter_b->next) {
            RmFile *b = iter_b->data;
            if(!rm_shred_byte_compare_files(tag, a, b)) {
                failure_count++;
                //TODO: discard file rm_shred_set_file_state(tag, b, RM_FILE_STATE_IGNORE);
            }
        }
    }
    return failure_count;
}

static void rm_shred_result_factory(ShredGroup *group, RmMainTag *tag) {

    if(tag->session->settings->paranoid) {
        int failure_count = rm_shred_check_paranoia(tag, group->held_files);
        if(failure_count > 0) {
            warning("Removed %d files during paranoia check.\n", failure_count);
        }
    }

    if(g_queue_get_length(group->held_files) > 0) {
        process_island(tag->session, group->held_files);
    }
    shred_group_free(group);
}



//~ static void rm_shred_findmatches(RmMainTag *tag, GQueue *same_size_list) {
//~ /* same_size_list is a list of files with the same size,
//~ * find out which are no duplicates.
//~ * */
//~ GHashTable *check_table = g_hash_table_new_full(
//~ (GHashFunc) rm_shred_hash_cksum_key,
//~ (GEqualFunc) rm_shred_equal_cksum_key,
//~ g_free, (GDestroyNotify)g_queue_free
//~ );
//~
//~ for(GList *iter = same_size_list->head; iter; iter = iter->next) {
//~ RmFileSnapshot *meta = iter->data;
//~ RmCksumKey keybuf;
//~ memcpy(&keybuf.checksum, meta->checksum, sizeof(keybuf.checksum));
//~
//~ GQueue *queue = g_hash_table_lookup(check_table, &keybuf);
//~ if(queue == NULL) {
//~ queue = g_queue_new();
//~ g_hash_table_insert(check_table, rm_shred_copy_cksum_key(&keybuf), queue);
//~ }
//~
//~ g_queue_push_head(queue, meta);
//~ }
//~
//~ GList *check_table_values = g_hash_table_get_values(check_table);
//~ for(GList *iter = check_table_values; iter; iter = iter->next) {
//~ GQueue *dupe_list = iter->data;
//~ if(dupe_list->length == 1) {
//~ /* We can ignore this file, it has evolved to a different checksum
//~ * Only a flag is set, the file is not freed. This is to prevent
//~ * cumbersome threading, where reference counting would need to be
//~ * used.
//~ * */
//~ RmFileSnapshot *lonely = dupe_list->head->data;
//~ rm_shred_set_file_state(tag, lonely->ref_file, RM_FILE_STATE_IGNORE);
//~ } else {
//~ /* For the others we check if they were fully read.
//~ * In this case we know that those are duplicates.
//~ *
//~ * If those files are not fully read nothing happens.
//~ */
//~ GQueue *results = g_queue_new();
//~ for(GList *iter = dupe_list->head; iter; iter = iter->next) {
//~ RmFileSnapshot *candidate = iter->data;
//~ if(candidate->hash_offset >= candidate->ref_file->file_size) {
//~ g_queue_push_head(results, candidate->ref_file);
//~ }
//~ }
//~
//~ rm_shred_thread_pool_push(tag->result_pool, results);
//~ }
//~ }
//~
//~ g_list_free(check_table_values);
//~ g_hash_table_unref(check_table);
//~ }

//~ typedef struct RmSizeKey {
//~ guint64 size;
//~ guint64 hash_offset;
//~ } RmSizeKey;
//~
//~ CREATE_HASH_FUNCTIONS(size_key, RmSizeKey);

//~ static void rm_shred_gc_join_table(GHashTable *join_table, RmSizeKey *current) {
//~ static int gc_counter = 1;
//~
//~ if(gc_counter++ % SHRED_GC_INTERVAL) {
//~ return;
//~ }
//~
//~ /* do not call remove() while live-iterating, just get a list of keys
//~ * insteead that don't get altered in between.
//~ */
//~ GList *join_table_keys = g_hash_table_get_keys(join_table);
//~ for(GList *iter = join_table_keys; iter; iter = iter->next) {
//~ RmSizeKey *old_key = iter->data;
//~ /* Same size, but less hashed? Forget about it */
//~ if(old_key->size == current->size && old_key->hash_offset < current->hash_offset) {
//~ g_hash_table_remove(join_table, old_key);
//~ }
//~ }
//~ g_list_free(join_table_keys);
//~ }

static void rm_shred_free_snapshots(GQueue *snapshots) {
    for(GList *iter = snapshots->head; iter; iter = iter->next) {
        g_slice_free(RmFileSnapshot, iter->data);
    }
    g_queue_free(snapshots);
}

////////////////////////////////////
//  SHRED-SPECIFIC PREPROCESSING  //
////////////////////////////////////


static void rm_preprocess_files(RmFile *file, RmSession *session) {
    RmFileTables *tables = session->tables;

    g_assert(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE);
    g_assert(file->file_size > 0 );

    dev_t disk = rm_mounts_get_disk_id(tables->mounts, file->dev);

    GQueue *dev_list = g_hash_table_lookup(tables->dev_table, GUINT_TO_POINTER(disk));
    if(dev_list == NULL) {
        dev_list = g_queue_new();
        g_hash_table_insert(tables->dev_table, GUINT_TO_POINTER(disk), dev_list);
        rm_error("new device queue for disk %lu\n", disk);
    }
    g_queue_push_head(dev_list, file);

    bool nonrotational = rm_mounts_is_nonrotational(tables->mounts, disk);
    if(!nonrotational
            && !file->hardlinked_original
            /*TODO: && settings->offset_sort_optimisation? */ ) {
        g_assert(!file->disk_offsets);
        file->disk_offsets = rm_offset_create_table(file->path);

        session->offsets_read++;
        if(file->disk_offsets) {
            session->offset_fragments += g_sequence_get_length((GSequence *)file->disk_offsets);
        } else {
            session->offset_fails++;
        }
        file->phys_offset = rm_offset_lookup(file->disk_offsets, 0); /* TODO: workaround this so we
            can drop phys_offset from RmFile structure?? */
    }

    info("Added Inode: %d Offset: %" PRId64 " file: %s\n", (int)file->inode, file->phys_offset, file->path);
}


static gboolean rm_preprocess_groups(gpointer key, ShredGroup *group, RmSession *session) {
    g_assert(key); //Void

    /* push files into appropriate dev_list queue*/
    g_queue_foreach(group->held_files, (GFunc)rm_preprocess_files, session);

    /* disown the files */
    g_queue_clear(group->held_files);

    /* tell caller it can delete the ShredGroup
       (but note it will not destroy the ShredGroup because */
    return true;
}



static char shred_group_get_status_locked(ShredGroup *group) {
    if (!group->status) {
        if (1
                && group->remaining >= 2  // it take 2 to tango
                && (group->has_pref || !group->needs_pref)
                //we have at least one file from preferred path, or we don't care
                && (group->has_npref || !group->needs_npref)
                //we have at least one file from non-pref path, or we don't care
           ) {
            /* group can go active */
            if (group->hash_offset < group->file_size) {
                group->status = RM_SHRED_GROUP_HASHING;
            } else {
                group->status = RM_SHRED_GROUP_FINISHING;
            }
        }
    }
    return group->status;
}
static char shred_group_get_status_unlocked(ShredGroup *group) {
    g_mutex_lock(&group->lock);
    char status = shred_group_get_status_locked(group);
    g_mutex_lock(&group->lock);
    return status;
}


void shred_group_make_orphan(ShredGroup *self) {
    g_mutex_lock(&(self->lock));
    self->parent = NULL;
    if (self->status == RM_SHRED_GROUP_DORMANT) {
        /* group doesn't need hashing, and not expecting any more (since
           parent is dead), so this group is now also dead */
        g_mutex_unlock(&(self->lock));
        shred_group_free(self); /*NOTE: there is no potential race here
						because parent can only die once - TODO: double check */
    } else {
        g_mutex_unlock(&(self->lock));
    }
}



void shred_group_unref(ShredGroup *group) {
    //assert (g_mutex_trylock(group->lock) == FALSE); // should already be locked by calling routine
    g_assert(group);
    g_mutex_lock(&(group->lock));
    if ( (--group->remaining) == 0
            && group->parent == NULL ) {
        /* no reason for living any more */
        g_mutex_unlock(&(group->lock));
        shred_group_free(group);
    } else {
        g_mutex_unlock(&(group->lock));
    }
}


static bool rm_group_can_discard( gpointer key, ShredGroup *group, RmSession *session) {
    (void)(key);
    (void)session;
    g_assert(group);
    if (g_mutex_trylock(&group->lock)) {
        rm_error ("should call shred_group_get_status_unlocked in rm_group_can_discard");
        char status = shred_group_get_status_locked(group);
        g_mutex_unlock(&group->lock);
        return (status == RM_SHRED_GROUP_DORMANT);
    } else {
        rm_error ("should call shred_group_get_status_locked in rm_group_can_discard");
        return (shred_group_get_status_locked(group) == RM_SHRED_GROUP_DORMANT);
    }
}

static void rm_group_add_file(ShredGroup *group, RmFile *file, RmSession *session) {
    g_assert(session);
    g_queue_push_head(group->held_files, file);
    group->has_pref |= file->is_prefd;
    group->has_npref |= !file->is_prefd;
}

static void rm_add_file_to_size_groups(RmFile *file, RmSession *session) {
    ShredGroup *group = g_hash_table_lookup(
                            session->tables->size_groups,
                            GUINT_TO_POINTER(file->file_size));
    if (!group) {
        group = shred_group_new(file);
        g_hash_table_insert(
            session->tables->size_groups,
            GUINT_TO_POINTER(file->file_size), //TODO: check overflow for >4GB files
            group);
    }
    rm_group_add_file(group, file, session);
}

static gboolean rm_populate_size_groups(gpointer key, GQueue *hardlink_cluster, RmSession *session) {
    g_assert(key); //key not used
    g_queue_foreach (hardlink_cluster,
                     (GFunc)rm_add_file_to_size_groups,
                     session);
    return true;
}


static void rm_shred_preprocess_input(RmSession *session) {
    /* move remaining files to size_groups */
    g_hash_table_foreach_remove(session->tables->node_table,
                                (GHRFunc)rm_populate_size_groups,
                                session);
    rm_error("move remaining files to size_groups finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    /* delete irrelevant size groups */
    g_hash_table_foreach_remove(session->tables->size_groups, //TODO: move size_groups to RmMainTag
                                (GHRFunc)rm_group_can_discard,
                                session);
    rm_error("delete irrelevant size groups finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));


    /* move files from remaining groups into dev queues */
    g_hash_table_foreach_remove(session->tables->size_groups,
                                (GHRFunc)rm_preprocess_groups,
                                session );

    rm_error("move remaining groups into dev_queues finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    rm_error("fiemap'd %lu files containing %lu fragments (failed another %lu files)\n", session->offsets_read - session->offset_fails, session->offset_fragments, session->offset_fails);
    //TODO: is this another kind of lint (heavily fragmented files)?

}

//static void rm_shred_preprocess_input(GHashTable *dev_table, GHashTable *size_table) {
//    // TODO: this is slightly ugly and leaks a bit of memory.
//    GList *values = g_hash_table_get_values(dev_table);
//    for(GList *value_link = values; value_link; value_link = value_link->next) {
//        GQueue *value = value_link->data;
//        GQueue to_delete = G_QUEUE_INIT;
//
//        for(GList *file_link = value->head; file_link; file_link = file_link->next) {
//            RmFile *file = file_link->data;
//            guint64 count = GPOINTER_TO_UINT(g_hash_table_lookup(size_table, GUINT_TO_POINTER(file->file_size)));
//
//            if(count == 1) {
//                g_queue_push_head(&to_delete, file_link);
//            }
//        }
//
//        for(GList *del_link = to_delete.head; del_link; del_link = del_link->next) {
//            GList *actual_link = del_link->data;
//            RmFile *file = actual_link->data;
//            g_queue_delete_link(value, actual_link);
//
//            g_hash_table_insert(
//                size_table,
//                GUINT_TO_POINTER(file->file_size),
//                g_hash_table_lookup(
//                    size_table, GUINT_TO_POINTER(file->file_size)) - 1
//            );
//            rm_file_destroy(file);
//        }
//        g_queue_clear(&to_delete);
//    }
//
//    g_list_free(values);
//}

ShredDevice *shred_device_new(gboolean is_rotational, char *disk_name) {

    ShredDevice *self = g_slice_new0(ShredDevice);

    g_assert (self->remaining_files == 0);
    g_assert (self->remaining_bytes == 0);

    self->is_rotational = is_rotational;
    self->disk_name = g_strdup(disk_name);

    self->file_queue = g_queue_new();

    self->hash_pool = rm_shred_thread_pool_new(
                          (GFunc) rm_shred_hash_factory, self, 1
                      );
    self->hashed_file_return = g_async_queue_new();

    self->page_size = SHRED_PAGE_SIZE;

    g_mutex_init(&(self->lock));
    return self;
}


void shred_device_free(ShredDevice *self) {
    g_assert(self->remaining_files == 0);
    g_assert(self->remaining_bytes == 0);
    g_assert(g_queue_is_empty(self->file_queue));
    g_assert(g_async_queue_length(self->hashed_file_return) == 0);
    g_assert(g_thread_pool_unprocessed(self->hash_pool) == 0);

    g_async_queue_unref(self->hashed_file_return);
    g_thread_pool_free(self->hash_pool, false, false);
    g_queue_free(self->file_queue);

    g_free(self->disk_name);
    g_mutex_clear(&(self->lock));

    g_slice_free(ShredDevice, self);
}


//~
//~
//~ void shred_device_queue_insert(RmFile *file) {
//~ ShredDevice *device = file->device;
//~ g_assert(device);
//~ GTree *queue = device->file_queue;
//~
//~ g_mutex_lock(&device->lock);
//~
//~ g_tree_insert(queue,
//~ g_memdup(&file->offset, sizeof(file->offset)),
//~ g_list_prepend(g_tree_lookup(queue, (gpointer)file->offset), file)
//~ );
//~
//~ g_mutex_unlock(&device->lock);
//~ }

gint shred_device_queue_insert(RmFile *file) {
    ShredDevice *device = file->device;
    g_mutex_lock (&device->lock);
    {
        g_queue_insert_sorted (device->file_queue, file, (GCompareDataFunc)rm_shred_compare_file_order, NULL);
    }
    g_mutex_unlock (&device->lock);
}

gboolean shred_add_file_to_group(ShredGroup *shred_group, RmFile *file, bool try_bounce) {
    gboolean result = false;

    ShredGroup *old_shred_group = NULL;

    g_mutex_lock(&(shred_group->lock));
    {
        old_shred_group = file->shred_group;
        file->shred_group = shred_group;

        shred_group->has_pref |= file->is_prefd;
        shred_group->has_npref |= !file->is_prefd;

        shred_group->remaining++;
        g_assert(file->hash_offset == shred_group->hash_offset);

        switch (shred_group_get_status_locked(shred_group)) {
        case RM_SHRED_GROUP_START_HASHING:
            /* clear the queue and push all its rmfiles to the appropriate device queue */
            g_assert(shred_group->held_files);
            g_queue_free_full(shred_group->held_files,
                              (GDestroyNotify)shred_device_queue_insert);
            shred_group->held_files = NULL; /* won't need shred_group queue any more, since new arrivals will bypass */
            shred_group->status = RM_SHRED_GROUP_HASHING;
        /** FALLTHROUGH */
        case RM_SHRED_GROUP_HASHING:
            if (try_bounce) {
                /* calling routine will handle the file */
                result = true;
            } else {
                /* add file to device queue */
                g_assert(file->device);
                shred_device_queue_insert(file);
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
    if(old_shred_group) {
        shred_group_unref(old_shred_group);
    }
    return result;
}

/* compares checksums of a ShredGroup and a RmFileSnapshot */
gboolean shred_snap_matches_group(ShredGroup *group, RmFileSnapshot *snap) {
    return rm_shred_equal_cksum_key(&group->checksum, &snap->checksum);
}


/* After partial hashing of RmFile, add it back into the sieve for further hashing
   if required.  If try_bounce option is set, then try to return the RmFile to the calling
   routine so it can continue with the next hashing increment (this bypasses the normal device
   queue and so avoids an unnecessary file seek operation )
   returns true if the file can be immediately be hashed some more*/

gboolean rm_shred_sift(RmFile *file, bool try_bounce) {
    RmFileSnapshot *snap = rm_shred_create_snapshot(file);
    ShredGroup *child_group = NULL;
    ShredGroup *parent_group = file->shred_group;

    g_assert(parent_group);

    g_mutex_lock(&(parent_group->lock));
    {
        child_group = g_queue_find_custom (parent_group->children,
                                           snap,
                                           (GCompareFunc)shred_snap_matches_group)->data;
        if (!child_group) {
            child_group = shred_group_new(file);
            g_queue_push_tail(parent_group->children, child_group);
        }
    }
    g_mutex_unlock(&(parent_group->lock));

    return shred_add_file_to_group(child_group, file, try_bounce);
}

static void rm_shred_devlist_factory(ShredDevice *device, RmMainTag *main) {

    rm_error(BLU"Started rm_shred_devlist_factory for disk %s (%u:%u)\n"NCO,
             device->disk_name,
             major(device->disk),
             minor(device->disk) );

    int max_threads = rm_shred_get_read_threads(
                          main,
                          !device->is_rotational,
                          main->session->settings->threads
                      );

    g_assert(max_threads == 1);  /*TODO */

    //g_assert(device->read_pool); // created when device created
    /*read_pool = rm_shred_thread_pool_new(
                                 (GFunc)rm_shred_read_factory, &tag, max_threads
                             ); */

    g_assert(device->hash_pool); // created when device created
    /*= rm_shred_thread_pool_new(
                        (GFunc) rm_shred_hash_factory, &tag, 1
                    ); */

    if(device->is_rotational) {
        g_queue_sort(device->file_queue, (GCompareDataFunc)rm_shred_compare_file_order, NULL);
    }

    if (max_threads == 1) {
        /* scheduler for one file at a time, optimised to minimise seeks */
        GList *iter = rm_shred_file_queue_get_next_remove(device, NULL);
        while (iter) {
            RmFile *file = iter->data;

            /* hash the next increment of the file */
            rm_shred_read_factory(file, device);

            /* wait until the increment has finished hashing */
            g_assert(file == g_async_queue_pop(device->hashed_file_return));

            if (file->fragment_hash) {
                /* file is not ready for checking yet; push it back into the queue */
                rm_shred_file_queue_push(device, file);
            } else if (rm_shred_sift(file, true)) {
                /* continue hashing same file, ie no change to iter */
                continue;
            } else {
                /* rm_shred_sift has taken responsibility for the file */
            }
            iter = rm_shred_file_queue_get_next_remove(device, iter);
        }

    } else {
        rm_error("Multiple threads per disk not implemented yet");
    }

    /* threadpool thread terminates but the device will be recycled via
     * the device_return queue
     */
    g_async_queue_push(main->device_return, device);
}

static void rm_shred_create_devpool(RmMainTag *tag, GHashTable *dev_table) {
    tag->device_pool = rm_shred_thread_pool_new(
                           (GFunc)rm_shred_devlist_factory, tag, tag->session->settings->threads / 2 + 1
                       );

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, dev_table);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        GQueue *device_queue = value;
        rm_shred_thread_pool_push(tag->device_pool, device_queue);
    }
}



void rm_shred_run(RmSession *session) {
    g_assert(session);

    GHashTable *dev_table = session->tables->dev_table;

    RmMainTag tag;
    tag.session = session;
    tag.mem_pool = rm_buffer_pool_init(sizeof(RmBuffer) + SHRED_PAGE_SIZE);
    tag.device_return = g_async_queue_new();
    tag.page_size = SHRED_PAGE_SIZE;

    /* would use g_atomic, but helgrind does not like that */
    g_mutex_init(&tag.file_state_mtx);

    rm_shred_preprocess_input(session);

    /* Remember how many devlists we had - so we know when to stop */
    int devices_left = g_hash_table_size(dev_table);

    /* Create a pool fo the devlists and push each queue */
    rm_shred_create_devpool(&tag, dev_table);

    /* For results that need to be check with --paranoid.
     * This would clog up the main thread, which is supposed
     * to flag bad files as soon as possible.
     */
    tag.result_pool = rm_shred_thread_pool_new(
                          (GFunc)rm_shred_result_factory, &tag, 1
                      );

    //~ /* Key: hash_offset & size Value: GQueue of fitting files */
    //~ GHashTable *join_table = g_hash_table_new_full(
    //~ (GHashFunc) rm_shred_hash_size_key,
    //~ (GEqualFunc) rm_shred_equal_size_key,
    //~ g_free, (GDestroyNotify) rm_shred_free_snapshots
    //~ );

    /* This is the joiner part */
    while(devices_left > 0 || g_async_queue_length(tag.device_return) > 0) {
        ShredDevice *device = g_async_queue_pop(tag.device_return);

        /* Check if device has finished.
         */
        if(device->remaining_files == 0) {
            --devices_left;
            continue;  //TODO: free, or use as paranoid device?
        } else {
            /* recycle the device */
            rm_shred_thread_pool_push(tag.device_pool , device);

        }
    }

    /* This should not block, or at least only very short. */
    g_thread_pool_free(tag.device_pool, FALSE, TRUE);
    g_thread_pool_free(tag.result_pool, FALSE, TRUE);

    g_async_queue_unref(tag.device_return);
    //g_hash_table_unref(join_table);
    rm_buffer_pool_destroy(tag.mem_pool);

    g_mutex_clear(&tag.file_state_mtx);
}


/////////////////////
//    TEST MAIN    //
/////////////////////

#ifdef _RM_COMPILE_MAIN_SHRED

static void main_free_func(gconstpointer p) {
    g_queue_free_full((GQueue *)p, (GDestroyNotify)rm_file_destroy);
}

int main(int argc, const char **argv) {
    RmSettings settings;
    settings.threads = 32;
    settings.paranoid = false;
    settings.keep_all_originals = false;
    settings.must_match_original = false;
    settings.checksum_type = RM_DIGEST_SPOOKY;

    bool offset_sort_optimisation = true;
    if(argc > 1 && g_ascii_strcasecmp(argv[1], "--no-offsets") == 0)  {
        offset_sort_optimisation = false;
    }

    RmSession session;
    session.settings = &settings;
    session.mounts = rm_mounts_table_new();

    GHashTable *dev_table = g_hash_table_new_full(
                                g_direct_hash, g_direct_equal,
                                NULL, (GDestroyNotify)main_free_func
                            );

    GHashTable *size_table = g_hash_table_new( NULL, NULL);

    char path[PATH_MAX];
    while(fgets(path, sizeof(path),  stdin)) {
        path[strlen(path) - 1] = '\0';
        struct stat stat_buf;
        stat(path, &stat_buf);
        if(stat_buf.st_size == 0) {
            continue;
        }

        dev_t whole_disk = rm_mounts_get_disk_id(session.mounts, stat_buf.st_dev);
        stat_buf.st_dev = whole_disk;

        RmFile *file =  rm_file_new(
                            path, &stat_buf, RM_LINT_TYPE_DUPE_CANDIDATE, RM_DIGEST_SPOOKY, 0, 0
                        );

        bool nonrotational = rm_mounts_is_nonrotational(session.mounts, whole_disk);
        if(!nonrotational && offset_sort_optimisation) {
            file->disk_offsets = rm_offset_create_table(path);
            file->phys_offset = rm_offset_lookup(file->disk_offsets, 0);
        }

        g_hash_table_insert(
            size_table,
            GUINT_TO_POINTER(stat_buf.st_size),
            g_hash_table_lookup(
                size_table, GUINT_TO_POINTER(stat_buf.st_size)) + 1
        );

        GQueue *dev_list = g_hash_table_lookup(dev_table, GUINT_TO_POINTER(whole_disk));
        if(dev_list == NULL) {
            dev_list = g_queue_new();
            g_hash_table_insert(dev_table, GUINT_TO_POINTER(whole_disk), dev_list);
        }

        g_queue_push_head(dev_list, file);
    }

    rm_shred_run(&session, dev_table, size_table);

    g_hash_table_unref(size_table);
    g_hash_table_unref(dev_table);
    rm_mounts_table_destroy(session.mounts);
    return 0;
}

#endif
// was line 1054
