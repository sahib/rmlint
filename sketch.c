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
#include "src/rmlint.h"

/* This is the scheduler of rmlint.
 *
 * The threading looks somewhat like this for two devices:
 *
 * Device #1                                              Device #2
 *
 *                           +----------+
 *                           | Finisher |
 *                           |  Thread  |
 *                           +----------+
 *                                 ^
 * +---------------------+         |         +---------------------+  
 * | +------+   +------+ |    +---------+    | +------+   +------+ | 
 * | | Read |-->| Hash |----->| Joiner  |<-----| Hash |<--| Read | | 
 * | +------+   +------+ |    |         |    | +------+   +------+ | 
 * |     ^         ^     |    |  Main   |    |    ^          ^     | 
 * |     | n       | 1   |    |---------|    |  1 |        n |     | 
 * |     +-------------+ |    | Thread  |    | +-------------+     | 
 * |     | Devlist Mgr |<-----|         |----->| Devlist Mgr |     | 
 * |     +-------------+ |    |  Init   |    | +-------------+     | 
 * +---------------------+    +---------+    +---------------------+
 *                                 ^
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
 * (shred_devlist_factory). On each iteration the block size is incremented, so
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
 * On comparable groups shred_findmatches() is called, which finds files that
 * can be ignored and files that are finished already. In both cases file->state
 * is modified accordingly. In the latter case the group is processed; i.e.
 * written to log, stdout and script.
 *
 * Below some performance controls are listed that may impact performance.
 * Benchmarks are left to determine reasonable defaults. TODO therefore.  The
 * controls are sorted by subjectve importanceness.
 */

/* How many pages are read initially at max.  This value is important since it
 * decides how much data will be read for small files, so it should not be too
 * large nor too small, since reading small files twice is very slow.
 */
#define SHRED_N_PAGES         (16)

/* After how many files the join-table is cleaned up from old entries.  This
 * settings will not have much performance impact, just keeps memory a bit
 * lower.
 */
#define SHRED_GC_INTERVAL     (100)

/* Maximum number of bytes to read in one pass.
 * Never goes beyond this value.
 */
#define SHRED_MAX_READ_SIZE   (1024 * 1024 * 1024)

/* Flags for the fadvise() call that tells the kernel
 * what we want to do with the file.
 */
#define SHRED_FADVISE_FLAGS   (0                                                         \
                               | POSIX_FADV_SEQUENTIAL /* Read from 0 to file-size    */ \
                               | POSIX_FADV_WILLNEED   /* Tell the kernel to readhead */ \
                               | POSIX_FADV_NOREUSE    /* We will not reuse old data  */ \
                              )                                                          \

/* How many pages to use during paranoid byte-by-byte comparasion?
 * More pages use more memory but result in less syscalls.
 */
#define SHRED_PARANOIA_PAGES  (64)


/* Determines the next amount of bytes_read to read.
 * Currently just doubles the amount.
 * */
static int shred_get_next_read_size(int read_size) {
    /* Protect agains integer overflows */
    if(read_size >= SHRED_MAX_READ_SIZE) {
        return SHRED_MAX_READ_SIZE;
    }  else {
        return read_size * 2;
    }
}

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
    GThreadPool *device_pool;
    GThreadPool *result_pool;
} RmMainTag;

/* Every devlist manager has additional private */
typedef struct RmDevlistTag {
    /* Main information from above */
    RmMainTag *main;

    /* Pool for the hashing workers */
    GThreadPool *hash_pool;

    /* How many bytes shred_read_factory is supposed to read */
    int read_size;

    /* Count of readable files, drops to 0 when done */
    int readable_files;

    /* Queue from shred_read_factory to shred_devlist_factory:
     * The reader notifes the manager to push a new job 
     * this way. 
     */
    GAsyncQueue * finished_queue;
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

static RmFileSnapshot * shred_create_snapshot(RmFile * file) {
    RmFileSnapshot * self = g_slice_new0(RmFileSnapshot);
    self->hash_offset = file->hash_offset;
    self->file_size = file->fsize;
    self->ref_file = file;

    rm_digest_steal_buffer(&file->digest, self->checksum, sizeof(self->checksum));
    return self;
}

static void shred_set_file_state(RmMainTag *tag, RmFile *file, RmFileState state) {
    g_mutex_lock(&tag->file_state_mtx); {
        file->state = state;
    }
    g_mutex_unlock(&tag->file_state_mtx);
}

static RmFileState shred_get_file_state(RmMainTag *tag, RmFile *file) {
    RmFileState state = 0; 
    g_mutex_lock(&tag->file_state_mtx); {
        state = file->state;
    }
    g_mutex_unlock(&tag->file_state_mtx);
    return state;
}

static bool shred_byte_compare_files(RmMainTag * tag, RmFile * a, RmFile *b) {
    g_assert(a->fsize == b->fsize);

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
        RmBuffer * buffer = rm_buffer_pool_get(tag->mem_pool);
        readvec_a[i].iov_base = buffer->data;
        readvec_a[i].iov_len = buf_size;
        
        buffer = rm_buffer_pool_get(tag->mem_pool);
        readvec_b[i].iov_base = buffer->data;
        readvec_b[i].iov_len = buf_size;
    }

    while(result) {
        int bytes_a = readv(fd_a, readvec_a, SHRED_N_PAGES);
        int bytes_b = readv(fd_b, readvec_b, SHRED_N_PAGES);
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

static void shred_thread_pool_push(GThreadPool *pool, gpointer data) {
    GError *error = NULL;
    g_thread_pool_push(pool, data, &error);
    if(error != NULL) {
        rm_error("Unable to push thread to pool %p: %s\n", pool, error->message);
        g_error_free(error);
    }
}

static GThreadPool * shred_thread_pool_new(GFunc func, gpointer data, int threads) {
    GError *error = NULL;
    GThreadPool *pool = g_thread_pool_new(func, data, threads, FALSE, &error);

    if(error != NULL) {
        rm_error("Unable to create thread pool.\n");
        g_error_free(error);
    }
    return pool;
}

/////////////////////////////////
//    ACTUAL IMPLEMENTATION    //
/////////////////////////////////

static void shred_read_factory(RmFile *file, RmDevlistTag *tag) {
    g_assert(tag);
    g_assert(file);

    int fd = 0;
    int bytes_read = 0;
    int read_maximum = -1;
    int buf_size = rm_buffer_pool_size(tag->main->mem_pool) - offsetof(RmBuffer, data);
    struct iovec readvec[SHRED_N_PAGES];

    if(shred_get_file_state(tag->main, file) != RM_FILE_STATE_PROCESS) {
        goto finish;
    }

    if(file->seek_offset >= file->fsize) {
        goto finish;
    }

    fd = open(file->path, O_RDONLY);
    if(fd == -1) {
        perror("open failed");

        /* act like this file was fully read.  Otherwise it would be counted as
         * unreadable on every try, which would result in BadThings™.
         */
        shred_set_file_state(tag->main, file, RM_FILE_STATE_IGNORE);
        if(file->seek_offset < file->fsize) {
            file->seek_offset = file->fsize;
        }

        goto finish;
    }
 
    // TODO: test if this makes any difference.
    posix_fadvise(fd, file->seek_offset, 0, SHRED_FADVISE_FLAGS);

    g_async_queue_lock(tag->finished_queue); {
        read_maximum = MIN(tag->read_size, (int)(file->fsize - file->seek_offset));
    } 
    g_async_queue_unlock(tag->finished_queue);

    /* Initialize the buffers to begin with.
     * After a buffer is full, a new one is retrieved.
     */
    for(int i = 0; i < SHRED_N_PAGES; ++i) {
        /* buffer is one contignous memory block */
        RmBuffer * buffer = rm_buffer_pool_get(tag->main->mem_pool);
        readvec[i].iov_base = buffer->data;
        readvec[i].iov_len = buf_size;
    }

    while(read_maximum > 0 && (bytes_read = preadv(fd, readvec, SHRED_N_PAGES, file->seek_offset)) > 0) {
        int remain = bytes_read % buf_size;
        int blocks = bytes_read / buf_size + !!remain;

        read_maximum -= bytes_read;
        file->seek_offset += bytes_read;

        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
            buffer->file = file;
            buffer->len = buf_size; 

            if(i + 1 == blocks && remain > 0) {
                buffer->len = remain;
            }

            /* Send it to the hasher */
            shred_thread_pool_push(tag->hash_pool, buffer);

            /* Allocate a new buffer - hasher will release the old buffer */            
            buffer = rm_buffer_pool_get(tag->main->mem_pool);
            readvec[i].iov_base = buffer->data;
            readvec[i].iov_len = buf_size;
        }
    }

    /* Release the rest of the buffers */
    for(int i = 0; i < SHRED_N_PAGES; ++i) {
        RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
        rm_buffer_pool_release(tag->main->mem_pool, buffer);
    }

finish:
    g_async_queue_lock(tag->finished_queue); {
        if(file->seek_offset < file->fsize) {
            if(fd > 0) {
                file->offset = get_disk_offset(file->disk_offsets, file->offset);
            }
        } else if(file->seek_offset == file->fsize) {
            tag->readable_files--;

            /* Remember that we had this file already 
             * by setting the seek offset beyond the 
             * file's size.
             */
            file->seek_offset = file->fsize + 1;
        }

        g_async_queue_push_unlocked(tag->finished_queue, file);
    }
    g_async_queue_unlock(tag->finished_queue);

    if(fd > 0) {
        close(fd);
    }
}

static void shred_hash_factory(RmBuffer * buffer, RmDevlistTag *tag) {
    g_assert(tag);
    g_assert(buffer);

    if(shred_get_file_state(tag->main, buffer->file) != RM_FILE_STATE_PROCESS) {
        return;
    }

    g_mutex_lock(&buffer->file->file_lock); {
        /* Hash buffer->len bytes_read of buffer->data into buffer->file */
        rm_digest_update(&buffer->file->digest, buffer->data, buffer->len);
        buffer->file->hash_offset += buffer->len;

        /* Report the progress to the joiner */
        g_async_queue_push(tag->main->join_queue, shred_create_snapshot(buffer->file));
    }
    g_mutex_unlock(&buffer->file->file_lock);

    /* Return this buffer to the pool */
    rm_buffer_pool_release(tag->main->mem_pool, buffer);
}

static void shred_devlist_add_job(RmDevlistTag *tag, GThreadPool * pool, RmFile * file, GHashTable *processing_table) {
    if(file != NULL) {
        if(shred_get_file_state(tag->main, file) == RM_FILE_STATE_PROCESS) {
            g_hash_table_insert(processing_table, file, NULL);
            shred_thread_pool_push(pool, file);
        }
    }
}

static RmFile * shred_devlist_pop_next(RmDevlistTag *tag, GQueue *work_queue, GList *work_list, GHashTable *processing_table) {
    if(work_list == NULL) {
        return NULL;
    }

    RmFile * file = work_list->data;
    if(g_hash_table_contains(processing_table, file) || shred_get_file_state(tag->main, file) != RM_FILE_STATE_PROCESS) {
        return shred_devlist_pop_next(tag, work_queue, work_list->next, processing_table);
    } else {
        g_queue_delete_link(work_queue, work_list);
        return file;
    }
}

static int shred_compare_file_order(const RmFile * a, const RmFile *b, G_GNUC_UNUSED gpointer user_data) {
    int diff = a->offset - b->offset;
    if(diff == 0) {
        /* Sort after inode as secondary criteria.
         * This is meant for files with an offset of 0 as fallback.
         */
        return a->node - b->node;
    } else {
        return diff;
    }
}

static GQueue * shred_create_work_queue(RmDevlistTag * tag, GQueue *device_queue, bool sort) {
    GQueue *work_queue = g_queue_new();
    for(GList * iter = device_queue->head; iter; iter = iter->next) {
        RmFile * file = iter->data;
        if(shred_get_file_state(tag->main, file) == RM_FILE_STATE_PROCESS) {
            g_queue_push_head(work_queue, file);
        }
    }

    if(sort) {
        g_queue_sort(work_queue, (GCompareDataFunc)shred_compare_file_order, NULL);
    }
    return work_queue;
}

static int shred_get_read_threads(RmMainTag *tag, bool nonrotational, int max_threads) {
    if(!nonrotational) {
        return 1;
    } else {
        int devices_running = g_thread_pool_get_num_threads(tag->device_pool);
        return CLAMP((max_threads - devices_running) / devices_running, 1, 16);
    }
}

static void shred_devlist_factory(GQueue *device_queue, RmMainTag *main) {
    RmDevlistTag tag;

    tag.main = main;
    tag.read_size = sysconf(_SC_PAGESIZE) * SHRED_N_PAGES; 
    tag.readable_files = g_queue_get_length(device_queue);
    tag.finished_queue = g_async_queue_new();

    /* Get the device of the files in this list */
    bool nonrotational = rm_mounts_is_nonrotational(
        main->session->mounts, 
        ((RmFile *)device_queue->head->data)->dev
    );

    int max_threads = shred_get_read_threads(
        main, nonrotational, main->session->settings->threads
    );

    GThreadPool *read_pool = shred_thread_pool_new(
        (GFunc)shred_read_factory, &tag, max_threads
    );

    tag.hash_pool = shred_thread_pool_new(
        (GFunc) shred_hash_factory, &tag, 1
    );

    GQueue *work_queue = shred_create_work_queue(&tag, device_queue, !nonrotational);
    GHashTable *processing_table = g_hash_table_new(NULL, NULL);

    /* Push the initial batch to the pool */
    for(int i = 0; i < max_threads; ++i) {
        shred_devlist_add_job(&tag, read_pool, g_queue_pop_head(work_queue), processing_table);
    }
    
    /* Wait for the completion of the first jobs and push new ones
     * once they report as finished. Choose a file with near offset.
     */
    bool run_manager = true;
    while(run_manager) {
        RmFile * finished = NULL;
        g_async_queue_lock(tag.finished_queue); {
            finished = g_async_queue_pop_unlocked(tag.finished_queue);
            g_hash_table_remove(processing_table, finished);
        
            if(tag.readable_files <= 0) {
                run_manager = false;
            } else if(g_queue_get_length(work_queue) == 0) {
                g_queue_free(work_queue);
                work_queue = shred_create_work_queue(&tag, device_queue, !nonrotational);
                tag.read_size = shred_get_next_read_size(tag.read_size);

                /* Maybe we can take more threads now? */
                GError * error = NULL;
                g_thread_pool_set_max_threads(
                    read_pool,
                    shred_get_read_threads(main, nonrotational, main->session->settings->threads),
                    &error
                );

                if(error != NULL) {
                    rm_error("Unable to set different amount of threads.\n");
                    g_error_free(error);
                }
            }
        }
        g_async_queue_unlock(tag.finished_queue);

        /* Find the next file to process (with nearest offset) and push it */
        shred_devlist_add_job(
            &tag, read_pool,
            shred_devlist_pop_next(&tag, work_queue, work_queue->head, processing_table),
            processing_table
        );
    }

    /* Send the queue itself to make the join thread check if we're
     * finished already
     * */
    g_thread_pool_free(read_pool, FALSE, TRUE);
    g_thread_pool_free(tag.hash_pool, FALSE, TRUE);
    g_async_queue_push(tag.main->join_queue, tag.main->join_queue);

    g_async_queue_unref(tag.finished_queue);
    g_hash_table_unref(processing_table);
    g_queue_free(work_queue);
}

static int shred_check_paranoia(RmMainTag * tag, GQueue *candidates) {
    int failure_count = 0;

    for(GList * iter_a = candidates->head; iter_a; iter_a = iter_a->next) {
        RmFile *a = iter_a->data;
        if(shred_get_file_state(tag, a) != RM_FILE_STATE_PROCESS) {
            continue;
        }

        for(GList * iter_b = iter_a->next; iter_b; iter_b = iter_b->next) {
            RmFile *b = iter_b->data;
            if(shred_get_file_state(tag, b) == RM_FILE_STATE_PROCESS && !shred_byte_compare_files(tag, a, b)) {
                failure_count++;
                shred_set_file_state(tag, b, RM_FILE_STATE_IGNORE);
            }
        }
    }
    return failure_count;
}

static void shred_result_factory(GQueue *results, RmMainTag *tag) {
    RmSettings *settings = tag->session->settings;
    int num_no_orig = 0, num_is_orig = 0;

    for(GList *iter = results->head; iter; iter = iter->next) {
        RmFile *candidate = iter->data;
        num_is_orig += candidate->in_ppath;
        num_no_orig += !candidate->in_ppath;

        g_printerr("--> %s size=%lu cksum=", candidate->path, candidate->fsize);

        guint8 checksum[_RM_HASH_LEN];
        rm_digest_steal_buffer(&candidate->digest, checksum, sizeof(checksum));
        for(int i = 0; i < _RM_HASH_LEN; ++i) {
            g_printerr("%02x", checksum[i]);
        }
        g_printerr("\n");
    }

    if(0
       || (g_queue_get_length(results) == 0) 
       || (settings->keep_all_originals && num_no_orig == 0) 
       || (settings->must_match_original && num_is_orig == 0)) {
        for(GList *iter = results->head; iter; iter = iter->next) {
            shred_set_file_state(tag, iter->data, RM_FILE_STATE_IGNORE);
        }
        g_queue_free(results);
        return;
    }

    if(tag->session->settings->paranoid) {
        int failure_count = shred_check_paranoia(tag, results);
        if(failure_count > 0) {
            warning("Removed %d files during paranoia check.\n", failure_count);
        }
    }

    int dupe_count = 0;
    for(GList *iter = results->head; iter; iter = iter->next) {
        RmFile * file = iter->data;
        if(shred_get_file_state(tag, file) == RM_FILE_STATE_PROCESS) {
            shred_set_file_state(tag, file, RM_FILE_STATE_FINISH);
            dupe_count += 1;
        }
    }

    if(dupe_count > 0) {
        // TODO Call processing of results here.
    } 

    g_queue_free(results);
}

/* Easy way to use arbitary structs as key in a GHastTable.
 * Creates a fitting shred_equal, shred_hash and shred_copy 
 * function for every data type that works with sizeof().
 */
#define CREATE_HASH_FUNCTIONS(name, HashType)                                  \
    static gboolean shred_equal_##name(const HashType* a, const HashType *b) { \
        return !memcmp(a, b, sizeof(HashType));                                \
    }                                                                          \
                                                                               \
    static guint shred_hash_##name(const HashType * k) {                       \
        return CityHash64((const char *)k, sizeof(HashType));                  \
    }                                                                          \
                                                                               \
    static HashType * shred_copy_##name(const HashType * self) {               \
        HashType *mem = g_new0(HashType, 1);                                   \
        memcpy(mem, self, sizeof(HashType));                                   \
        return mem;                                                            \
    }                                                                          \

typedef struct RmCksumKey {
    guint8 checksum[_RM_HASH_LEN];
} RmCksumKey;

/* create GHastTable boilerplate implicitly */
CREATE_HASH_FUNCTIONS(cksum_key, RmCksumKey);

static void shred_findmatches(RmMainTag *tag, GQueue *same_size_list) {
    /* same_size_list is a list of files with the same size,
     * find out which are no duplicates.
     * */
    GHashTable * check_table = g_hash_table_new_full(
        (GHashFunc) shred_hash_cksum_key,
        (GEqualFunc) shred_equal_cksum_key,
        g_free, (GDestroyNotify)g_queue_free
    );

    for(GList *iter = same_size_list->head; iter; iter = iter->next) {
        RmFileSnapshot * meta = iter->data;
        if(shred_get_file_state(tag, meta->ref_file) != RM_FILE_STATE_PROCESS) {
            continue;
        }

        RmCksumKey keybuf;
        memcpy(keybuf.checksum, meta->checksum, sizeof(keybuf.checksum));

        GQueue * queue = g_hash_table_lookup(check_table, &keybuf);
        if(queue == NULL) {
            queue = g_queue_new();
            g_hash_table_insert(check_table, shred_copy_cksum_key(&keybuf), queue);
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
            shred_set_file_state(tag, lonely->ref_file, RM_FILE_STATE_IGNORE);
        } else {
            /* For the others we check if they were fully read. 
             * In this case we know that those are duplicates.
             *
             * If those files are not fully read nothing happens.
             */
            GQueue *results = g_queue_new();
            for(GList *iter = dupe_list->head; iter; iter = iter->next) {
                RmFileSnapshot *candidate = iter->data;
                if(candidate->hash_offset >= candidate->file_size) {
                    g_queue_push_head(results, candidate->ref_file);
                }
            }

            shred_thread_pool_push(tag->result_pool, results);
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

static void shred_gc_join_table(GHashTable *join_table, RmSizeKey *current) {
    static int gc_counter = 1;

    if(gc_counter++ % SHRED_GC_INTERVAL) {
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

static void shred_create_devpool(RmMainTag *tag, GHashTable *dev_table) {
    tag->device_pool = shred_thread_pool_new(
        (GFunc)shred_devlist_factory, tag, tag->session->settings->threads / 2 + 1
    );
    
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, dev_table);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        GQueue *device_queue = value;
        shred_thread_pool_push(tag->device_pool, device_queue);
    }
}

static void shred_free_snapshots(GQueue *snapshots) {
    for(GList * iter = snapshots->head; iter; iter = iter->next) {
        g_slice_free(RmFileSnapshot, iter->data);
    }
    g_queue_free(snapshots);
}

static void shred_run(RmSession *session, GHashTable *dev_table, GHashTable * size_table) {
    g_assert(session);
    g_assert(dev_table);
    g_assert(size_table);

    RmMainTag tag; 
    tag.session = session;
    tag.mem_pool = rm_buffer_pool_init(sizeof(RmBuffer) + sysconf(_SC_PAGESIZE));
    tag.join_queue = g_async_queue_new();

    /* would use g_atomic, but helgrind does not like that */
    g_mutex_init(&tag.file_state_mtx);

    /* Remember how many devlists we had - so we know when to stop */
    int devices_left = g_hash_table_size(dev_table);

    /* Create a pool fo the devlists and push each queue */
    shred_create_devpool(&tag, dev_table);

    /* For results that need to be check with --paranoid.
     * This would clog up the main thread, which is supposed 
     * to flag bad files as soon as possible.
     */
    tag.result_pool = shred_thread_pool_new(
        (GFunc)shred_result_factory, &tag, 1
    );

    /* Key: hash_offset & size Value: GQueue of fitting files */
    GHashTable *join_table = g_hash_table_new_full(
        (GHashFunc) shred_hash_size_key,
        (GEqualFunc) shred_equal_size_key,
        g_free, (GDestroyNotify) shred_free_snapshots
    );

    /* This is the joiner part */
    RmFileSnapshot * snapshot = NULL;
    while((snapshot = g_async_queue_pop(tag.join_queue))) {
        /* Check if the received pointer is the queue itself.
         * The devlist threads notify us this way when they finish.
         * In this case we check if need to quit already.
         */
        if((GAsyncQueue *)snapshot == tag.join_queue) {
            if(--devices_left) {
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
                g_hash_table_insert(join_table, shred_copy_size_key(&key), size_list);
            } 

            /* Append to the list */
            g_queue_push_head(size_list, snapshot);
            
            /* Find out if the size group has as many items already as the full
             * group - in this case we have a full set and can compare it.
             * */
            guint64 count = GPOINTER_TO_INT(
                g_hash_table_lookup(size_table, GINT_TO_POINTER(snapshot->file_size))
            );

            if(count > 1 && g_queue_get_length(size_list) == count) {
                shred_findmatches(&tag, size_list);
            }

            /* Garbage collect the join_table from unused entries in regular intervals.
             * This is to keep the memory footprint low.
             * */
            shred_gc_join_table(join_table, &key);
        }
    }

    /* This should not block, or at least only very short. */
    g_thread_pool_free(tag.device_pool, FALSE, TRUE);
    g_thread_pool_free(tag.result_pool, FALSE, TRUE);

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
    settings.threads = 32;
    settings.paranoid = true;
    settings.keep_all_originals = true;
    settings.must_match_original = false;
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

    shred_run(&session, dev_table, size_table);

    g_hash_table_unref(size_table);
    g_hash_table_unref(dev_table);
    rm_mounts_table_destroy(session.mounts);
    return 0;
}
