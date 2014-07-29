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
 *  
 * +---------------------+                  +---------------------+  
 * | +------+   +------+ |    +--------+    | +------+   +------+ | 
 * | | Read |-->| Hash |----->| Joiner |<-----| Hash |<--| Read | | 
 * | +------+   +------+ |    | Thread |    | +------+   +------+ | 
 * |     ^               |    |        |    |                 ^   | 
 * |     |               |    |--------|    |                 |   | 
 * |     +-------------+ |    |        |    |   +-------------+   | 
 * |     | Devlist Mgr |<-----|  Init  |------->| Devlist Mgr |   | 
 * |     +-------------+ |    +--------+    |   +-------------+   | 
 * +---------------------+                  +---------------------+
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
 * (sched_factory). On each iteration the block size is incremented, so the
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
 */

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
    while(pool->stack != NULL) {
        g_slice_free1(pool->size, g_trash_stack_pop(&pool->stack));
    }
    g_mutex_clear(&pool->lock);
    g_slice_free(RmBufferPool, pool);
}

static void *rm_buffer_pool_get(RmBufferPool * pool) {
    if (!pool->stack) {
        return g_slice_alloc(pool->size);
    } else {
        void * buffer = NULL;
        g_mutex_lock(&pool->lock); {
            buffer = g_trash_stack_pop(&pool->stack);
        } g_mutex_unlock(&pool->lock); 
        return buffer;
    }
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
} RmMainTag;

/* Every devlist manager has additional private */
typedef struct RmDevlistTag {
    /* Main information from above */
    RmMainTag *main;

    /* Pool for the hashing workers */
    GThreadPool *hash_pool;
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


/////////////////////////////////
//    ACTUAL IMPLEMENTATION    //
/////////////////////////////////

static void sched_read_factory(RmFile *file, RmDevlistTag *tag) {
    g_assert(tag);
    g_assert(file);
    g_assert(file->state == RM_FILE_STATE_PROCESS);

    const int N = 8; // TODO: Find good N through benchmarks.
    struct iovec readvec[N];

    gsize read_sum = file->hash_offset;
    gsize buf_size = rm_buffer_pool_size(tag->main->mem_pool) - offsetof(RmBuffer, data);
    gsize may_read_max = MIN(tag->read_size + file->hash_offset, file->fsize);

    if((guint64)g_atomic_pointer_get(&file->hash_offset) >= file->fsize) {
        g_printerr("File hash_offset is larger than file size, doing nothing.");
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
    if(lseek(fd, (guint64)g_atomic_pointer_get(&file->hash_offset), SEEK_SET) == -1) {
        perror("lseek failed");
        return;
    }

    int bytes = 0;
    while(read_sum < may_read_max && (bytes = readv(fd, readvec, N)) > 0) {
        int blocks = bytes / buf_size + 1;
        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
            buffer->file = file;
            buffer->len = readvec[i].iov_len;
            
            /* Send it to the haser */
            g_thread_pool_push(tag->hash_pool, buffer, NULL);

            /* Allocate a new buffer - hasher will release the old buffer */            
            buffer = rm_buffer_pool_get(tag->main->mem_pool);
            readvec[i].iov_base = buffer->data;
            readvec[i].iov_len = buf_size;
        }

        read_sum += MAX(0, bytes);
    }

    /* Release the rest of the buffers */
    for(int i = 0; i < N; ++i) {
        RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
        rm_buffer_pool_release(tag->main->mem_pool, buffer);
    }
    
    close(fd);
}

static void sched_hash_factory(RmBuffer *buffer, RmDevlistTag *tag) {
    g_assert(tag);
    g_assert(buffer);
    g_assert(buffer->file);

    /* Hash buffer->len bytes of buffer->data into buffer->file */
    rm_digest_update(&buffer->file->digest, buffer->data, buffer->len);

    /* Increment the offset */
    g_atomic_pointer_set(
        &buffer->file->hash_offset, 
        g_atomic_pointer_get(&buffer->file->hash_offset) + buffer->len
    );

    /* Report the progress to the joiner */
    g_async_queue_push(tag->main->join_queue, buffer->file);

    /* Return this buffer to the pool */
    rm_buffer_pool_release(tag->main->mem_pool, buffer);
}

static int sched_n_processable(GQueue *queue) {
    g_assert(queue);

    int processable = 0;
    for(GList * iter = queue->head; iter; iter = iter->next) {
        RmFile * file = iter->data;
        processable += g_atomic_int_get(&file->state) == RM_FILE_STATE_PROCESS;
    }

    return processable;
}

static gsize sched_get_next_read_size(gsize read_size) {
    return read_size * 2;
}

static void sched_factory(GQueue *device_queue, RmMainTag *main) {
    RmDevlistTag *tag = g_slice_new(RmDevlistTag);
    tag->main = main;
    tag->read_size = sysconf(_SC_PAGESIZE) * 16; /* Read 16 pages initially */
 
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
    while(sched_n_processable(device_queue)) {
        tag->hash_pool = g_thread_pool_new(
            (GFunc)sched_hash_factory,
            tag,
            main->session->settings->threads,
            FALSE, NULL
        );
        tag->read_pool = g_thread_pool_new(
            (GFunc)sched_read_factory, 
            tag,
            (nonrotational) ? main->session->settings->threads : 1,
            FALSE, NULL
        );

        /* Resort every 16 (TODO: play with this value) */
        if(read_sum > 16 * 1024 * 1024) {
            rm_file_list_resort_device_offsets(device_queue, read_direction_is_forward);
            read_direction_is_forward = !read_direction_is_forward;
            read_sum = 0;
        }

        for(GList *iter = device_queue->head; iter; iter = iter->next) {
            RmFile *to_be_read = iter->data;
            if(to_be_read->state == RM_FILE_STATE_PROCESS) {
                g_thread_pool_push(tag->read_pool, iter->data, NULL);
            }
        }
    
        /* Block until this iteration is over */
        g_thread_pool_free(tag->read_pool, FALSE, TRUE);
        g_thread_pool_free(tag->hash_pool, FALSE, TRUE);

        /* Read more next time */
        read_sum += tag->read_size;
        tag->read_size = sched_get_next_read_size(tag->read_size);
    }

    /* Send the queue itself to make the join thread check if we're
     * finished already
     * */
    g_async_queue_push(tag->main->join_queue, tag->main->join_queue);
}

#define CREATE_HASH_FUNCTIONS(name, HashType)                                  \
    static gboolean sched_cmp_##name(const HashType* a, const HashType *b) {   \
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


CREATE_HASH_FUNCTIONS(cksum_key, RmCksumKey);

static void sched_findmatches(GQueue *same_size_list) {
    /* same_size_list is a list of files with the same size,
     * find out which are no duplicates.
     * */
    GHashTable * check_table = g_hash_table_new_full(
        (GHashFunc) sched_hash_cksum_key,
        (GEqualFunc) sched_cmp_cksum_key,
        g_free, (GDestroyNotify)g_queue_free
    );

    RmCksumKey keybuf;

    for(GList *iter = same_size_list->head; iter; iter = iter->next) {
        RmFile *file = iter->data;

        /* Generate the key */
        rm_digest_steal_buffer(&file->digest, keybuf.checksum, sizeof(keybuf.checksum));

        GQueue * queue = g_hash_table_lookup(check_table, &keybuf);
        if(queue == NULL) {
            queue = g_queue_new();
            g_hash_table_insert(check_table, sched_copy_cksum_key(&keybuf), queue);
        }

        g_queue_push_head(queue, file);
    }
    
    for(GList *iter = g_hash_table_get_values(check_table); iter; iter = iter->next) {
        GQueue *dupe_list = iter->data;
        if(dupe_list->length == 1) {
            /* We can ignore this file, it has evolved to a different checksum 
             * Only a flag is set, the file is not freed. This is to prevent 
             * cumbersome threading, where reference counting would need to be
             * used.
             * */
            RmFile * lone_wolf = dupe_list->head->data;
            g_atomic_int_set(&lone_wolf->state, RM_FILE_STATE_IGNORE);
        } else {
            /* For the others we check if they were fully read. 
             * In this case we know that those are duplicates.
             */
            for(GList *iter = dupe_list->head; iter; iter = iter->next) {
                RmFile *possible_dupe = iter->data;
                if((guint64)g_atomic_pointer_get(&possible_dupe->hash_offset) >= possible_dupe->fsize) {
                    g_atomic_int_set(&possible_dupe->state, RM_FILE_STATE_FINISH);
                }
            }

            // TODO: Call processing of dupe_list here.
        }
    }

    g_hash_table_unref(check_table);
}

typedef struct RmSizeKey {
    guint64 size;
    guint64 hash_offset;
} RmSizeKey;

CREATE_HASH_FUNCTIONS(size_key, RmSizeKey);

static void sched_start(RmSession *session, GHashTable *dev_table) {
    // TODO: clean is a bit up.
    RmMainTag tag; 
    tag.session = session;
    tag.mem_pool = rm_buffer_pool_init(sizeof(RmBuffer) + sysconf(_SC_PAGESIZE));
    tag.join_queue = g_async_queue_new();

    GThreadPool * sched_pool = g_thread_pool_new(
        (GFunc)sched_factory, 
        &tag,
        tag.session->settings->threads,
        FALSE, NULL
    );

    for(GList *iter = g_hash_table_get_values(dev_table); iter; iter = iter->next) {
        GQueue *device_queue = iter->data;
        g_thread_pool_push(sched_pool, device_queue, NULL);
    }

    GHashTable *size_table = g_hash_table_new_full(
        (GHashFunc) sched_hash_size_key,
        (GEqualFunc) sched_cmp_size_key,
        g_free, (GDestroyNotify) g_queue_free
    );

    RmSizeKey sizekey;
    RmFile * join_file = NULL;
    int gc_counter = 1;
    int dev_finished_counter = g_hash_table_size(dev_table);

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
        }

        sizekey.size = join_file->fsize;
        sizekey.hash_offset = (guint64)g_atomic_pointer_get(&join_file->hash_offset);

        GQueue *size_list = g_hash_table_lookup(size_table, &sizekey);
        if(size_list == NULL) {
            size_list = g_queue_new();
            g_hash_table_insert(size_table, sched_copy_size_key(&sizekey), size_list);
        }

        g_queue_push_head(size_list, join_file);

        /* Find out if the size group has as many items already as the full
         * group */
        if(g_queue_get_length(size_list) == /* TODO: len(files with this size). */ 42) {
            sched_findmatches(size_list);
        }

        /* Garbage collect the size_table from unused entries in regular intervals.
         * This is to keep the memory footprint low.
         * TODO: Own function.
         * */
        if(gc_counter++ % 100 == 0) {
            for(GList *iter = g_hash_table_get_keys(size_table); iter; iter = iter->next) {
                RmSizeKey *key_struct = iter->data;
                /* Same size, but less hashed? Forget about it */
                if(key_struct->size == sizekey.size && key_struct->hash_offset < sizekey.hash_offset) {
                    g_hash_table_remove(size_table, key_struct);
                }
            }
        }
    }

    g_thread_pool_free(sched_pool, FALSE, TRUE);
    rm_buffer_pool_destroy(tag.mem_pool);
}

/////////////////////
//    TEST MAIN    //
/////////////////////

int main(int argc, char const* argv[]) {
    RmSettings settings;
    settings.threads = 4;

    RmSession session;
    session.settings = &settings;

    sched_start(&session, NULL);

    return 0;
}
