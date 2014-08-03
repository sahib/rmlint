#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/uio.h>

#include "src/checksum.h"
#include "src/checksums/city.h"
#include "src/list.h"
#include "src/filemap.h"

/* This is the scheduler of rmlint.
 *
 * The threading looks somewhat like this for two devices:
 *
 * Device #1                  Mainthread                  Device #2
 *                                 ^
 * +---------------------+         |         +---------------------+  
 * | +------+   +------+ |    +---------+    | +------+   +------+ | 
 * | | Read |-->| Hash |----->| Joiner  |<-----| Hash |<--| Read | | 
 * | +------+   +------+ |    | Thread  |    | +------+   +------+ | 
 * |     ^         ^     |    |         |    |    ^          ^     | 
 * |     | n       | n   |    |---------|    |  n |        n |     | 
 * |     +-------------+ |    |         |    | +-------------+     | 
 * |     | Devlist Mgr |<-----|  Init   |----->| Devlist Mgr |     | 
 * |     +-------------+ |    +---------+    | +-------------+     | 
 * +---------------------+         ^         +---------------------+
 *                                 |
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
 * (sched_devlist_factory). On each iteration the block size is incremented, so the
 * next round reads more data, since it gets increasingly less likely to find
 * differences in files. Additionally on every few iterations the files in the
 * devlist are resorted according to their physical block on the device.
 *
 * The reader thread(s) read one file at a time using readv(). The buffers for
 * it come from a central buffer pool that allocates some and just reuses them
 * over and over. The buffer which contain the read data are pusehd to an
 * available hasher thread, where the data-block is hashed into file->digest.
 * The buffer is released back to the pool after use. 
 *
 * Once the hasher is done, the file is send back to the mainthread via a
 * GAsyncQueue. There a table with the hash_offset and the file_size as key and
 * a list of files is updated with it. If one of the list is as long as the full
 * list found during traversing, we know that we compare these files with each
 * other. 
 *
 * On comparable groups sched_findmatches() is called, which finds files
 * that can be ignored and files that are finished already. In both cases
 * file->state is modified accordingly. In the latter case the group is
 * processed; i.e. written to log, stdout and script.
 *
 * Below some performance controls are listed that maby impact performance.
 * Benchmarks are left to determine reasonable defaults. TODO therefore.
 * The controls are sorted by subjectve importanceness.
 */

/* Determines the next amount of bytes_read to read.
 * Currently just doubles the amount.
 * */
static int sched_get_next_read_size(int read_size) {
    /* Protect agains integer overflows */
    if(read_size >= 1024 * 1024 * 1024) {
        return 1024 * 1024 * 1024;
    }  else {
        return read_size * 2;
    }
}

/* How many pages are read initially at max.  This value is important since it
 * decides how much data will be read for small files, so it should not be too
 * large nor too small, since reading small files twice is very slow.
 */
#define SCHED_N_PAGES   (16)

/* After which amount of bytes_read the devlist is resorted to their physical block.
 * The reprobing of the blocks has some cost and makes no sense if we did not
 * jump far. 
 */
#define SCHED_RESORT_INTERVAL (128 * 1024 * 1024)

/* After how many files the join-table is cleaned up from old entries.  This
 * settings will not have much performance impact, just keeps memory a bit
 * lower.
 */ 
#define SCHED_GC_INTERVAL     (100)

///////////////////////////////////////
//    BUFFER POOL IMPLEMENTATION     //
///////////////////////////////////////

typedef struct RmBufferPool {
    /* Place where the buffers are stored */
    GTrashStack *stack;

    /* how many buffers are available? */
    guint64 size;

    /* concurrent accesses may happen */
    GMutex lock;
} RmBufferPool;

static guint64 rm_buffer_pool_size(RmBufferPool * pool) {
    return pool->size; 
}

static RmBufferPool *rm_buffer_pool_init(gsize size) {
    RmBufferPool * self = g_slice_new(RmBufferPool);
    self->stack = NULL;
    self->size = size;
    g_mutex_init(&self->lock);
    return self;
}

static void rm_buffer_pool_destroy(RmBufferPool * pool) {
    g_mutex_lock(&pool->lock); {
        while(pool->stack != NULL) {
            g_slice_free1(pool->size, g_trash_stack_pop(&pool->stack));
        }
    } g_mutex_unlock(&pool->lock); 
    g_mutex_clear(&pool->lock);
    g_slice_free(RmBufferPool, pool);
}

static void *rm_buffer_pool_get(RmBufferPool * pool) {
    void * buffer = NULL;
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

static void rm_buffer_pool_release(RmBufferPool * pool, void * buf) {
    g_mutex_lock(&pool->lock); {
        g_trash_stack_push(&pool->stack, buf);
    } g_mutex_unlock(&pool->lock); 
}

//////////////////////////////
//    INTERAL STRUCTURES    //
//////////////////////////////

/* The main extra data for the scheduler */
typedef struct RmMainTag {
    RmSession *session;
    RmBufferPool *mem_pool;
    GAsyncQueue *join_queue;
    GMutex file_state_mtx;
} RmMainTag;

/* Every devlist manager has additional private */
typedef struct RmDevlistTag {
    /* Main information from above */
    RmMainTag *main;

    /* Pool for the hashing workers */
    GThread *hash_thread;
    GAsyncQueue *hash_queue;

    int read_size;
    int readable_files;
    RmFile *finished_file;

    GCond pool_sync_cond;
    GMutex pool_sync_mtx;
    GMutex hash_mtx;
} RmDevlistTag;

/* Represents one block of read data */
typedef struct RmBuffer {
    /* file structure the data belongs to */
    RmFile *file; 

    /* len of the read input */
    guint64 len;

    /* *must* be last member of RmBuffer,
     * gets all the rest of the allocated space 
     * */
    guint8 data[];
} RmBuffer;

/* Copied file-metadata relevant for match filtering.
 * Data needs to be copied since the ref_file might be
 * modified at any time. Plus: lock times can be kept low.
 */
typedef struct RmFileSnapshot {
    guint8 checksum[_RM_HASH_LEN];
    guint64 hash_offset;
    guint64 file_size;
    RmFile *ref_file;
} RmFileSnapshot;

static RmFileSnapshot * sched_create_snapshot(RmFile * file) {
    RmFileSnapshot * self = g_slice_new0(RmFileSnapshot);
    self->hash_offset = file->hash_offset;
    self->file_size = file->fsize;
    self->ref_file = file;

    rm_digest_steal_buffer(&file->digest, self->checksum, sizeof(self->checksum));
    return self;
}

static void sched_set_file_state(RmMainTag *tag, RmFile *file, RmFileState state) {
    g_mutex_lock(&tag->file_state_mtx); {
        file->state = state;
    }
    g_mutex_unlock(&tag->file_state_mtx);
}

static RmFileState sched_get_file_state(RmMainTag *tag, RmFile *file) {
    RmFileState state = 0; 
    g_mutex_lock(&tag->file_state_mtx); {
        state = file->state;
    }
    g_mutex_unlock(&tag->file_state_mtx);
    return state;
}

/////////////////////////////////
//    ACTUAL IMPLEMENTATION    //
/////////////////////////////////

static void sched_read_factory(RmFile *file, RmDevlistTag *tag) {
    g_assert(tag);
    g_assert(file);

    bool was_readable = TRUE;

    if(sched_get_file_state(tag->main, file) != RM_FILE_STATE_PROCESS) {
        goto failure;
    }

    if(file->seek_offset >= file->fsize) {
        goto failure;
    }

    /* get a handle */
    int fd = open(file->path, O_RDONLY);
    if(fd == -1) {
        perror("open failed");
        was_readable = FALSE;
        goto failure;
    }

    /* The buffer is actually a tad larger, since we need to store information 
     * at the beginngin, so we need to get the real size of the buffer.
     */
    int buf_size = rm_buffer_pool_size(tag->main->mem_pool) - offsetof(RmBuffer, data);
    int bytes_read = 0;
    int read_maximum = -1;
    struct iovec readvec[SCHED_N_PAGES];

    g_mutex_lock(&tag->pool_sync_mtx); {
        read_maximum = MIN(tag->read_size, (int)ABS(file->fsize - file->seek_offset));
        g_printerr("read_max: %d\n", read_maximum);
    } g_mutex_unlock(&tag->pool_sync_mtx);

    /* Initialize the buffers to begin with.
     * After a buffer is full, a new one is retrieved.
     */
    for(int i = 0; i < SCHED_N_PAGES; ++i) {
        /* buffer is one contignous memory block */
        RmBuffer * buffer = rm_buffer_pool_get(tag->main->mem_pool);
        readvec[i].iov_base = buffer->data;
        readvec[i].iov_len = buf_size;
    }

    while(read_maximum > 0 && (bytes_read = preadv(fd, readvec, SCHED_N_PAGES, file->seek_offset)) > 0) {
        int remain = bytes_read % buf_size;
        int blocks = bytes_read / buf_size + !!remain;

        read_maximum -= bytes_read;
        file->seek_offset += bytes_read;

        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
            buffer->file = file;

            if(i + 1 == blocks) {
                buffer->len = (remain > 0) ? remain : buf_size;
            } else {
                buffer->len = buf_size;
            }

            /* Send it to the hasher */
            g_async_queue_push(tag->hash_queue, buffer);

            /* Allocate a new buffer - hasher will release the old buffer */            
            buffer = rm_buffer_pool_get(tag->main->mem_pool);
            readvec[i].iov_base = buffer->data;
            readvec[i].iov_len = buf_size;
        }
    }

    /* Release the rest of the buffers */
    for(int i = 0; i < SCHED_N_PAGES; ++i) {
        RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
        rm_buffer_pool_release(tag->main->mem_pool, buffer);
    }
    
    if(file->seek_offset < file->fsize) {
        file->offset = get_disk_offset_openfile(fd, RM_OFFSET_ABSOLUTE, file->offset);
    } else {
        was_readable = FALSE;
    }

    close(fd);

failure:
    g_mutex_lock(&tag->pool_sync_mtx); {
        if(!was_readable) {
            tag->readable_files--;
            if(file->seek_offset < file->fsize) {
                tag->finished_file = file;
            }
        }
        g_cond_signal(&tag->pool_sync_cond);
    }
    g_mutex_unlock(&tag->pool_sync_mtx);
}

static gpointer sched_hash_factory(RmDevlistTag *tag) {
    g_assert(tag);

    RmBuffer * buffer = NULL;
    while((buffer = g_async_queue_pop(tag->hash_queue)) != (RmBuffer *)tag->hash_queue) {
        g_assert(sched_get_file_state(tag->main, buffer->file) == RM_FILE_STATE_PROCESS);

        g_mutex_lock(&tag->hash_mtx); {
            /* Hash buffer->len bytes_read of buffer->data into buffer->file */
            rm_digest_update(&buffer->file->digest, buffer->data, buffer->len);
            buffer->file->hash_offset += buffer->len;

            /* Report the progress to the joiner */
            g_async_queue_push(tag->main->join_queue, sched_create_snapshot(buffer->file));
        }
        g_mutex_unlock(&tag->hash_mtx);

        /* Return this buffer to the pool */
        rm_buffer_pool_release(tag->main->mem_pool, buffer);
    }

    g_async_queue_push(tag->main->join_queue, tag->main->join_queue);
    return NULL;
}

static int sched_iteration_needed(RmMainTag *tag, GQueue *queue) {
    g_assert(queue);

    int processable = 0;
    for(GList * iter = queue->head; iter && processable < 2; iter = iter->next) {
        RmFile * file = iter->data;
        processable += sched_get_file_state(tag, file) == RM_FILE_STATE_PROCESS;
    }

    return processable >= 2;
}

static bool sched_devlist_add_job(RmDevlistTag *tag, GThreadPool * pool, RmFile * file) {
    if(sched_get_file_state(tag->main, file) != RM_FILE_STATE_PROCESS) {
        return false; 
    }

    g_thread_pool_push(pool, file, NULL);
    return true;
}

// static int sched_compare_file_order(const RmFile * a, const RmFile *b, gpointer user_data) {
//     return a->offset - b->offset;
// }

// static int sched_find_next_offset(const RmFile * a, const RmFile *b, guint64 **goal) {
//     
// }

static void sched_devlist_factory(GQueue *device_queue, RmMainTag *main) {
    // TODO: no heap needed.
    RmDevlistTag *tag = g_slice_new(RmDevlistTag);
    tag->read_size = sysconf(_SC_PAGESIZE) * SCHED_N_PAGES; 
    tag->hash_queue = g_async_queue_new();
    tag->hash_thread = g_thread_new("hasher", (GThreadFunc)sched_hash_factory, tag);
    tag->readable_files = g_queue_get_length(device_queue);
    tag->finished_file = NULL;
    tag->main = main;

    // GSequence *seq = g_sequence_new(NULL);
    // for(GList * iter = device_queue->head; iter; iter = iter->next) {
    //     g_sequence_append(seq, iter->data);
    // }
    // g_sequence_sort(seq, (GCompareDataFunc)sched_compare_file_order, NULL);

    g_cond_init(&tag->pool_sync_cond);
    g_mutex_init(&tag->pool_sync_mtx);
    g_mutex_init(&tag->hash_mtx);

    /* Get the device of the files in this list */
    bool nonrotational = rm_mounts_is_nonrotational(
        main->session->mounts, 
        ((RmFile *)device_queue->head->data)->dev
    );

    /* In this case the devlist is sorted ascending. 
     * The idea that one can toggle this, so one 
     * seek is saved (jumping to the beginning of the list)
     * */
    bool read_direction_is_forward = TRUE;

    int max_threads = (nonrotational) ? main->session->settings->threads : 1;

    g_printerr("Threads: %d\n", max_threads);

    GThreadPool *read_pool = g_thread_pool_new(
        (GFunc)sched_read_factory, 
        tag,
        max_threads,
        FALSE, NULL
    );

    while(tag->readable_files > 0 && sched_iteration_needed(main, device_queue)) {
        rm_file_list_resort_device_offsets(device_queue, read_direction_is_forward, false);
        read_direction_is_forward = !read_direction_is_forward;

        int jobs_pushed = 0;
        for(GList *iter = device_queue->head; iter; iter = iter->next) {
            jobs_pushed += sched_devlist_add_job(tag, read_pool, iter->data);
        }
    
        if(jobs_pushed) {
            g_mutex_lock(&tag->pool_sync_mtx); {
                int thread_counter = MIN(max_threads, jobs_pushed);
                do {
                    g_printerr("COND WAIT\n");
                    g_cond_wait(&tag->pool_sync_cond, &tag->pool_sync_mtx);
                    g_printerr("WAIT THREADS=%d READABLE=%d %d\n", thread_counter, tag->readable_files, g_thread_pool_get_num_unused_threads());
                    // TODO: possibility: push a new job.
                    sched_devlist_add_job(tag, read_pool, tag->finished_file);
                } while(--thread_counter && tag->readable_files);

                /* Read more next time */
                tag->read_size = sched_get_next_read_size(tag->read_size);
            }
            g_mutex_unlock(&tag->pool_sync_mtx);
        }

        g_printerr("-\n");
    }

    /* Send the queue itself to make the join thread check if we're
     * finished already
     * */
    g_thread_pool_set_max_threads(read_pool, 0, NULL);
    g_thread_pool_free(read_pool, TRUE, TRUE);
    g_cond_clear(&tag->pool_sync_cond);
    g_mutex_clear(&tag->pool_sync_mtx);

    g_async_queue_push(tag->hash_queue, tag->hash_queue);
    g_thread_join(tag->hash_thread);
    g_async_queue_unref(tag->hash_queue);

    g_mutex_clear(&tag->hash_mtx);

    g_slice_free(RmDevlistTag, tag);
}

/* Easy way to use arbitary structs as key in a GHastTable.
 * Creates a fitting sched_equal, sched_hash and sched_copy 
 * function for every data type that works with sizeof().
 */
#define CREATE_HASH_FUNCTIONS(name, HashType)                                  \
    static gboolean sched_equal_##name(const HashType* a, const HashType *b) { \
        return !memcmp(a, b, sizeof(HashType));                                \
    }                                                                          \
                                                                               \
    static guint sched_hash_##name(const HashType * k) {                       \
        return CityHash64((const char *)k, sizeof(HashType));                  \
    }                                                                          \
                                                                               \
    static HashType * sched_copy_##name(const HashType * self) {               \
        HashType *mem = g_new0(HashType, 1);                                   \
        memcpy(mem, self, sizeof(HashType));                                   \
        return mem;                                                            \
    }                                                                          \

typedef struct RmCksumKey {
    guint8 checksum[_RM_HASH_LEN];
} RmCksumKey;

/* create GHastTable boilerplate implicitly */
CREATE_HASH_FUNCTIONS(cksum_key, RmCksumKey);

static void sched_findmatches(RmMainTag *tag, GQueue *same_size_list) {
    /* same_size_list is a list of files with the same size,
     * find out which are no duplicates.
     * */
    GHashTable * check_table = g_hash_table_new_full(
        (GHashFunc) sched_hash_cksum_key,
        (GEqualFunc) sched_equal_cksum_key,
        g_free, (GDestroyNotify)g_queue_free
    );

    for(GList *iter = same_size_list->head; iter; iter = iter->next) {
        RmFileSnapshot * meta = iter->data;
        if(sched_get_file_state(tag, meta->ref_file) != RM_FILE_STATE_PROCESS) {
            continue;
        }

        RmCksumKey keybuf;
        memcpy(keybuf.checksum, meta->checksum, sizeof(keybuf.checksum));

        GQueue * queue = g_hash_table_lookup(check_table, &keybuf);
        if(queue == NULL) {
            queue = g_queue_new();
            g_hash_table_insert(check_table, sched_copy_cksum_key(&keybuf), queue);
        }

        g_queue_push_head(queue, meta);
    }
    
    GList * check_table_values = g_hash_table_get_values(check_table);
    for(GList *iter = check_table_values; iter; iter = iter->next) {
        GQueue *dupe_list = iter->data;
        if(dupe_list->length == 1) {
            /* We can ignore this file, it has evolved to a different checksum 
             * Only a flag is set, the file is not freed. This is to prevent 
             * cumbersome threading, where reference counting would need to be
             * used.
             * */
            RmFileSnapshot * lonely = dupe_list->head->data;
            sched_set_file_state(tag, lonely->ref_file, RM_FILE_STATE_IGNORE);
            g_printerr("Ignoring: %p %s %lu\n", lonely->ref_file, lonely->ref_file->path, lonely->hash_offset);
        } else {
            /* For the others we check if they were fully read. 
             * In this case we know that those are duplicates.
             */
            for(GList *iter = dupe_list->head; iter; iter = iter->next) {
                RmFileSnapshot *candidate = iter->data;
                if(candidate->hash_offset >= candidate->file_size) {
                    sched_set_file_state(tag, candidate->ref_file, RM_FILE_STATE_FINISH);
                    g_printerr(
                        "--> %s hashed=%lu size=%lu cksum=",
                        candidate->ref_file->path, candidate->hash_offset, candidate->file_size
                    );

                    for(int i = 0; i < _RM_HASH_LEN; ++i) {
                        g_printerr("%02x", candidate->checksum[i]);
                    }
                    g_printerr("\n");
                }
            }

            // TODO: Call processing of dupe_list here.
        }
    }

    g_list_free(check_table_values);
    g_hash_table_unref(check_table);
}

typedef struct RmSizeKey {
    guint64 size;
    guint64 hash_offset;
} RmSizeKey;

CREATE_HASH_FUNCTIONS(size_key, RmSizeKey);

static void sched_gc_join_table(GHashTable *join_table, RmSizeKey *current) {
    static int gc_counter = 1;

    if(gc_counter++ % SCHED_GC_INTERVAL) {
        return;
    }

    /* do not call remove() while live-iterating, just get a list of keys
     * insteead that don't get altered in between.
     */
    GList * join_table_keys = g_hash_table_get_keys(join_table);
    for(GList *iter = join_table_keys; iter; iter = iter->next) {
        RmSizeKey *old_key = iter->data;
        /* Same size, but less hashed? Forget about it */
        if(old_key->size == current->size && old_key->hash_offset < current->hash_offset) {
            g_hash_table_remove(join_table, old_key);
        }
    }
    g_list_free(join_table_keys);
}

static GThreadPool * sched_create_devpool(RmMainTag *tag, GHashTable *dev_table) {
    GThreadPool * devmgr_pool = g_thread_pool_new(
        (GFunc)sched_devlist_factory, 
        tag,
        tag->session->settings->threads,
        FALSE, NULL
    );
    
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, dev_table);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        GQueue *device_queue = value;
        g_thread_pool_push(devmgr_pool, device_queue, NULL);
    }

    return devmgr_pool;
}

static void sched_free_snapshots(GQueue *snapshots) {
    for(GList * iter = snapshots->head; iter; iter = iter->next) {
        g_slice_free(RmFileSnapshot, iter->data);
    }
    g_queue_free(snapshots);
}

static void sched_start(RmSession *session, GHashTable *dev_table, GHashTable * size_table) {
    g_assert(session);
    g_assert(dev_table);
    g_assert(size_table);

    RmMainTag tag; 
    tag.session = session;
    tag.mem_pool = rm_buffer_pool_init(sizeof(RmBuffer) + sysconf(_SC_PAGESIZE));
    tag.join_queue = g_async_queue_new();

    /* would use g_atomic, but helgrind does not like that */
    g_mutex_init(&tag.file_state_mtx);

    /* Create a pool fo the devlists and push each queue */
    GThreadPool *devmgr_pool = sched_create_devpool(&tag, dev_table);

    /* Key: hash_offset & size Value: GQueue of fitting files */
    GHashTable *join_table = g_hash_table_new_full(
        (GHashFunc) sched_hash_size_key,
        (GEqualFunc) sched_equal_size_key,
        g_free, (GDestroyNotify) sched_free_snapshots
    );

    /* Remember how many devlists we had - so we know when to stop */
    int dev_finished_counter = g_hash_table_size(dev_table);

    /* This is the joiner part */
    RmFileSnapshot * snapshot = NULL;
    while((snapshot = g_async_queue_pop(tag.join_queue))) {
        /* Check if the received pointer is the queue itself.
         * The devlist threads notify us this way when they finish.
         * In this case we check if need to quit already.
         */
        if((GAsyncQueue *)snapshot == tag.join_queue) {
            if(--dev_finished_counter) {
                continue;
            } else {
                break;
            }
        } else {
            /* It is a regular RmFileSnapshot with updates. */
            RmSizeKey key;
            key.size = snapshot->file_size;
            key.hash_offset = snapshot->hash_offset;

            /* See if we already have had this combination, if not create new entry */
            GQueue *size_list = g_hash_table_lookup(join_table, &key);
            if(size_list == NULL) {
                size_list = g_queue_new();
                g_hash_table_insert(join_table, sched_copy_size_key(&key), size_list);
            } 

            /* Append to the list */
            g_queue_push_head(size_list, snapshot);
            
            /* Find out if the size group has as many items already as the full
             * group - in this case we have a full set and can compare it.
             * */
            guint64 count = GPOINTER_TO_INT(g_hash_table_lookup(size_table, GINT_TO_POINTER(snapshot->file_size)));
            if(count > 1 && g_queue_get_length(size_list) == count) {
                sched_findmatches(&tag, size_list);
            }

            /* Garbage collect the join_table from unused entries in regular intervals.
             * This is to keep the memory footprint low.
             * */
            sched_gc_join_table(join_table, &key);
        }
    }

    /* This should not block, or at least only very short. */
    g_thread_pool_free(devmgr_pool, FALSE, TRUE);

    g_async_queue_unref(tag.join_queue);
    g_hash_table_unref(join_table);
    rm_buffer_pool_destroy(tag.mem_pool);

    g_mutex_clear(&tag.file_state_mtx);
}

/////////////////////
//    TEST MAIN    //
/////////////////////

static void main_free_func(gconstpointer p) {
    g_queue_free_full((GQueue *)p, (GDestroyNotify)rm_file_destroy);
}

int main(int argc, char const* argv[]) {
    RmSettings settings;
    settings.threads = 4;
    settings.checksum_type = RM_DIGEST_SPOOKY;

    RmSession session;
    session.settings = &settings;
    session.mounts = rm_mounts_table_new();

    GHashTable *dev_table = g_hash_table_new_full(
            g_direct_hash, g_direct_equal,
            NULL, (GDestroyNotify)main_free_func
    );
    GHashTable *size_table = g_hash_table_new(
            g_direct_hash, g_direct_equal
    );

    for(int i = 1; i < argc; ++i) {
        struct stat stat_buf;
        stat(argv[i], &stat_buf);

        dev_t whole_disk = rm_mounts_get_disk_id(session.mounts, stat_buf.st_dev);
        RmFile * file =  rm_file_new(
            argv[i], stat_buf.st_size, stat_buf.st_ino, whole_disk,
            0, TYPE_DUPE_CANDIDATE, 0, 0
        );

        g_hash_table_insert(
            size_table,
            GINT_TO_POINTER(stat_buf.st_size),
            g_hash_table_lookup(
                size_table, GINT_TO_POINTER(stat_buf.st_size)) + 1
        );

        GQueue *dev_list = g_hash_table_lookup(dev_table, GINT_TO_POINTER(whole_disk));
        if(dev_list == NULL) {
            dev_list = g_queue_new();
            g_hash_table_insert(dev_table, GINT_TO_POINTER(whole_disk), dev_list);
        }

        g_queue_push_head(dev_list, file);
    }

    sched_start(&session, dev_table, size_table);

    g_hash_table_unref(size_table);
    g_hash_table_unref(dev_table);
    rm_mounts_table_destroy(session.mounts);
    return 0;
}
