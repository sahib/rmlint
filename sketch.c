#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/uio.h>

#include "src/checksum.h"
#include "src/checksums/city.h"
#include "src/list.h"

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
 * (devlist_factory). On each iteration the block size is incremented, so the
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

/* Determines the next amount of bytes to read.
 * Currently just doubles the amount.
 * */
static gsize sched_get_next_read_size(gsize read_size) {
    return read_size * 2;
}

/* How many pages are read initially at max.  This value is important since it
 * decides how much data will be read for small files, so it should not be too
 * large nor too small, since reading small files twice is very slow.
 */
#define SCHED_INITIAL_PAGES   (16)

/* After which amount of bytes the devlist is resorted to their physical block.
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
    gsize size;

    /* concurrent accesses may happen */
    GMutex lock;
} RmBufferPool;

static gsize rm_buffer_pool_size(RmBufferPool * pool) {
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
    GMutex hash_mtx;
    GMutex hash_offset_mtx;
    GMutex file_state_mtx;
} RmMainTag;

/* Every devlist manager has additional private */
typedef struct RmDevlistTag {
    /* Main information from above */
    RmMainTag *main;

    /* Pool for the hashing workers */
    // GThreadPool *hash_pool;
    GThread *hash_thread;
    GAsyncQueue *hash_queue;
    GThreadPool *read_pool;
    gsize read_size;
} RmDevlistTag;

/* Represents one block of read data */
typedef struct RmBuffer {
    /* file structure the data belongs to */
    RmFile *file; 

    /* len of the read input */
    gsize len;

    /* *must* be last member of RmBuffer,
     * gets all the rest of the allocated space 
     * */
    guint8 data[];
} RmBuffer;

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

static void sched_set_hash_offset(RmMainTag *tag, RmFile *file, guint64 off) {
    g_mutex_lock(&tag->hash_offset_mtx); {
        file->hash_offset = off;
    }
    g_mutex_unlock(&tag->hash_offset_mtx);
}

static guint64 sched_get_hash_offset(RmMainTag *tag, RmFile *file) {
    guint64 off = 0; 
    g_mutex_lock(&tag->hash_offset_mtx); {
        off = file->hash_offset;
    }
    g_mutex_unlock(&tag->hash_offset_mtx);
    return off;
}

/////////////////////////////////
//    ACTUAL IMPLEMENTATION    //
/////////////////////////////////

static void sched_read_factory(RmFile *file, RmDevlistTag *tag) {
    g_assert(tag);
    g_assert(file);

    if(sched_get_file_state(tag->main, file) != RM_FILE_STATE_PROCESS) {
        return;
    }

    const int N = SCHED_INITIAL_PAGES; 
    struct iovec readvec[N];

    gsize read_sum = sched_get_hash_offset(tag->main, file);
    gsize buf_size = rm_buffer_pool_size(tag->main->mem_pool) - offsetof(RmBuffer, data);
    gsize may_read_max = MIN(tag->read_size + read_sum, file->fsize);

    if(sched_get_hash_offset(tag->main, file) >= file->fsize) {
        g_printerr("File hash_offset is larger than file size, doing nothing.\n");
        return;
    }

    for(int i = 0; i < N; ++i) {
        /* buffer is one contignous memory block */
        RmBuffer * buffer = rm_buffer_pool_get(tag->main->mem_pool);
        readvec[i].iov_base = buffer->data;
        readvec[i].iov_len = buf_size;
    }

    /* get a handle */
    int fd = open(file->path, O_RDONLY);
    if(fd == -1) {
        perror("open failed");
        return;
    }

    /* Fast forward to the last position */
    if(lseek(fd, sched_get_hash_offset(tag->main, file), SEEK_SET) == -1) {
        perror("lseek failed");
        return;
    }

    int bytes = 0;
    while(read_sum < may_read_max && (bytes = readv(fd, readvec, N)) > 0) {
        // g_printerr("I may read %u bytes at max. (have %u, got %d)\n", may_read_max, read_sum, bytes);
        int blocks = bytes / buf_size + (bytes % buf_size) ? 1 : 0;
        read_sum += MAX(0, bytes);

        guint64 off = 0;

        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
            buffer->file = file;
            buffer->len = readvec[i].iov_len;
            
            off += buffer->len;
            g_printerr("[%lu %lu (%ld)]\n", read_sum, off, buffer->file->fsize);
            // g_printerr("    Sending pack %u/%u (%lu)\n", read_sum, may_read_max, buffer->len);

            /* Send it to the hasher */
            g_async_queue_push(tag->hash_queue, buffer);
            // g_thread_pool_push(tag->hash_pool, buffer, NULL);

            /* Allocate a new buffer - hasher will release the old buffer */            
            buffer = rm_buffer_pool_get(tag->main->mem_pool);
            readvec[i].iov_base = buffer->data;
            readvec[i].iov_len = buf_size;

            off += buffer->len;
        }
    }

    /* Release the rest of the buffers */
    for(int i = 0; i < N; ++i) {
        RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
        rm_buffer_pool_release(tag->main->mem_pool, buffer);
    }
    
    close(fd);
}

static gpointer sched_hash_factory(RmDevlistTag *tag) {
    g_assert(tag);

    RmBuffer * buffer = NULL;
    while((buffer = g_async_queue_pop(tag->hash_queue)) != (RmBuffer *)tag->hash_queue) {
        if(sched_get_file_state(tag->main, buffer->file) != RM_FILE_STATE_PROCESS) {
            return NULL;
        }

        g_mutex_lock(&tag->main->hash_mtx); {
            /* Hash buffer->len bytes of buffer->data into buffer->file */
            rm_digest_update(&buffer->file->digest, buffer->data, buffer->len);

            guint64 s = sched_get_hash_offset(tag->main, buffer->file) + buffer->len;
            sched_set_hash_offset(
                tag->main, buffer->file, s
            );

            g_printerr("#%lu (%ld)#\n", sched_get_hash_offset(tag->main, buffer->file), buffer->file->fsize);

            /* Report the progress to the joiner */
            g_async_queue_push(tag->main->join_queue, buffer->file);
        }
        g_mutex_unlock(&tag->main->hash_mtx);

        /* Return this buffer to the pool */
        // TODO can this be removed? 
        memset(buffer->data, 0, buffer->len);
        rm_buffer_pool_release(tag->main->mem_pool, buffer);
    }

    return NULL;
}

static int sched_n_processable(RmMainTag *tag, GQueue *queue) {
    g_assert(queue);

    int processable = 0;
    for(GList * iter = queue->head; iter; iter = iter->next) {
        RmFile * file = iter->data;
        processable += sched_get_file_state(tag, file) == RM_FILE_STATE_PROCESS;
    }

    // g_printerr("Still to process: %d\n", processable);
    return processable;
}

static void devlist_factory(GQueue *device_queue, RmMainTag *main) {
    RmDevlistTag *tag = g_slice_new(RmDevlistTag);
    tag->read_size = sysconf(_SC_PAGESIZE) * SCHED_INITIAL_PAGES; 
    tag->hash_queue = g_async_queue_new();
    tag->hash_thread = g_thread_new("hasher", (GThreadFunc)sched_hash_factory, tag);
    tag->main = main;
 
    /* Get the device of the devlist by asking the first file */
    dev_t current_device = ((RmFile *)device_queue->head->data)->dev;

    /* Get the device of the files in this list */
    bool nonrotational = rm_mounts_is_nonrotational(
        main->session->mounts, 
        current_device
    );

    /* In this case the devlist is sorted ascending. 
     * The idea that one can toggle this, so one 
     * seek is saved (jumping to the beginning of the list)
     * */
    bool read_direction_is_forward = TRUE;

    /* Sum of the read bytes (not of all files) */
    guint64 read_sum = 0;

    /* Iterate until no processable files are available.
     * This is of course hard to get right. 
     */
    while(sched_n_processable(main, device_queue)) {
        // tag->hash_pool = g_thread_pool_new(
        //     (GFunc)sched_hash_factory,
        //     tag,
        //     1, // main->session->settings->threads,
        //     FALSE, NULL
        // );

        
        tag->read_pool = g_thread_pool_new(
            (GFunc)sched_read_factory, 
            tag,
            (nonrotational) ? main->session->settings->threads : 1,
            FALSE, NULL
        );

        bool force_reprobe = false;
        if(read_sum > SCHED_RESORT_INTERVAL) {
            force_reprobe = true;
            read_sum = 0;
        }

        rm_file_list_resort_device_offsets(
            device_queue, read_direction_is_forward, force_reprobe && 0
        );
        read_direction_is_forward = !read_direction_is_forward;

        for(GList *iter = device_queue->head; iter; iter = iter->next) {
            RmFile *to_be_read = iter->data;
            if(sched_get_file_state(main, to_be_read) == RM_FILE_STATE_PROCESS) {
                g_thread_pool_push(tag->read_pool, iter->data, NULL);
            }
        }
    
        /* Block until this iteration is over */
        g_thread_pool_free(tag->read_pool, FALSE, TRUE);
        // g_thread_pool_free(tag->hash_pool, FALSE, TRUE);

        /* Read more next time */
        read_sum += tag->read_size;
        tag->read_size = sched_get_next_read_size(tag->read_size);
    }

    /* Send the queue itself to make the join thread check if we're
     * finished already
     * */
    g_async_queue_push(tag->hash_queue, tag->hash_queue);
    g_thread_join(tag->hash_thread);
    g_async_queue_push(tag->main->join_queue, tag->main->join_queue);
    g_async_queue_unref(tag->hash_queue);
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

    g_printerr("-\n");
    for(GList *iter = same_size_list->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        if(sched_get_file_state(tag, file) != RM_FILE_STATE_PROCESS) {
            return;
        }
        g_printerr("Check %s %lu %p\n", file->path, sched_get_hash_offset(tag, file), file);

        RmCksumKey keybuf;

            rm_digest_steal_buffer(&file->digest, keybuf.checksum, sizeof(keybuf.checksum));
            for(int i = 0; i < _RM_HASH_LEN; ++i) {
                g_printerr("%02x", keybuf.checksum[i]);
            }
            g_printerr("\n");

        GQueue * queue = g_hash_table_lookup(check_table, &keybuf);
        if(queue == NULL) {
            queue = g_queue_new();
            g_hash_table_insert(check_table, sched_copy_cksum_key(&keybuf), queue);
        }

        g_queue_push_head(queue, file);
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
            RmFile * lone_wolf = dupe_list->head->data;
            // sched_set_file_state(tag, lone_wolf, RM_FILE_STATE_IGNORE);
            g_printerr("Ignoring: %s\n", lone_wolf->path);
        } else {
            /* For the others we check if they were fully read. 
             * In this case we know that those are duplicates.
             */
            for(GList *iter = dupe_list->head; iter; iter = iter->next) {
                RmFile *possible_dupe = iter->data;
                g_printerr("Finished: %lu %lu\n", sched_get_hash_offset(tag, possible_dupe), possible_dupe->fsize);
                if(sched_get_hash_offset(tag, possible_dupe) >= possible_dupe->fsize) {
                    sched_set_file_state(tag, possible_dupe, RM_FILE_STATE_FINISH);
                    g_printerr("%s\n", possible_dupe->path);
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
        (GFunc)devlist_factory, 
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

static void sched_start(RmSession *session, GHashTable *dev_table, GHashTable * size_table) {
    g_assert(session);
    g_assert(dev_table);
    g_assert(size_table);

    RmMainTag tag; 
    tag.session = session;
    tag.mem_pool = rm_buffer_pool_init(sizeof(RmBuffer) + sysconf(_SC_PAGESIZE));
    tag.join_queue = g_async_queue_new();

    g_mutex_init(&tag.hash_mtx);
    g_mutex_init(&tag.hash_offset_mtx);
    g_mutex_init(&tag.file_state_mtx);

    /* Create a pool fo the devlists and push each queue */
    GThreadPool *devmgr_pool = sched_create_devpool(&tag, dev_table);

    /* Key: hash_offset & size Value: GQueue of fitting files */
    GHashTable *join_table = g_hash_table_new_full(
        (GHashFunc) sched_hash_size_key,
        (GEqualFunc) sched_equal_size_key,
        g_free, (GDestroyNotify) g_queue_free
    );

    /* Remember how many devlists we had - so we know when to stop */
    int dev_finished_counter = g_hash_table_size(dev_table);

    /* This is the joiner part */
    RmFile * join_file = NULL;
    while((join_file = g_async_queue_pop(tag.join_queue))) {
        /* Check if the received pointer is the queue itself.
         * The devlist threads notify us this way when they finish.
         * In this case we check if need to quit already.
         */
        if((GAsyncQueue *)join_file == tag.join_queue) {
            if(--dev_finished_counter) {
                continue;
            } else {
                break;
            }
        } else {
            g_mutex_lock(&tag.hash_mtx);
            /* It is a regular RmFile. */
            RmSizeKey key;
            key.size = join_file->fsize;
            key.hash_offset = sched_get_hash_offset(&tag, join_file);

            /* See if we already have had this combination, if not create new entry */
            GQueue *size_list = g_hash_table_lookup(join_table, &key);
            if(size_list == NULL) {
                size_list = g_queue_new();
                g_hash_table_insert(join_table, sched_copy_size_key(&key), size_list);
            }

            /* Append to the list */
            g_queue_push_head(size_list, join_file);

            /* Find out if the size group has as many items already as the full
             * group - in this case we have a full set and can compare it.
             * */
            guint64 count = GPOINTER_TO_INT(g_hash_table_lookup(size_table, GINT_TO_POINTER(join_file->fsize)));
            if(count > 1 && g_queue_get_length(size_list) == count) {
                sched_findmatches(&tag, size_list);
            }

            /* Garbage collect the join_table from unused entries in regular intervals.
             * This is to keep the memory footprint low.
             * */
            // sched_gc_join_table(join_table, &key);
            //}
            g_mutex_unlock(&tag.hash_mtx);
        }
    }

    g_printerr("Joining...\n");

    /* This should not block, or at least only very short. */
    g_thread_pool_free(devmgr_pool, FALSE, TRUE);
    g_printerr("Exiting...\n");

    g_async_queue_unref(tag.join_queue);
    g_hash_table_unref(join_table);
    rm_buffer_pool_destroy(tag.mem_pool);

    g_mutex_clear(&tag.hash_mtx);
    g_mutex_clear(&tag.hash_offset_mtx);
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
