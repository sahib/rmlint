#include <glib.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <alloca.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "src/checksum.h"
#include "src/list.h"


// TODO: Writeup. What does this do?

typedef struct RmBufferPool {
    GTrashStack *stack;
    gsize size;
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

typedef struct RmMainTag {
    RmSession *session;
    RmBufferPool *mem_pool;
    GAsyncQueue *join_queue;
} RmMainTag;

typedef struct RmSchedTag {
    RmMainTag *main;
    GThreadPool *hash_pool;
    GThreadPool *read_pool;
    gsize read_size;
} RmSchedTag;


typedef struct RmBuffer {
    /* file structure the data belongs to */
    RmFile *file; 

    /* len of the read input */
    gsize len;

    /* *must* be last member of RmBuffer */
    guint8 data[];
} RmBuffer;


static void read_factory(RmFile *file, RmSchedTag *tag) {
    const int N = 8; // TODO: Find good N.
    struct iovec readvec[N];

    gsize buf_size = rm_buffer_pool_size(tag->main->mem_pool) - offsetof(RmBuffer, data);
    gsize read_sum = file->hash_offset;
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

    int bytes = 0;
    int fd = open(file->path, O_RDONLY);

    if(lseek(fd, (guint64)g_atomic_pointer_get(&file->hash_offset), SEEK_SET) == -1) {
        perror("lseek failed");
        return;
    }

    while(read_sum < may_read_max && (bytes = readv(fd, readvec, N)) > 0) {
        int blocks = bytes / buf_size + 1;
        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
            buffer->file = file;
            buffer->len = readvec[i].iov_len;
            g_thread_pool_push(tag->hash_pool, buffer, NULL);

            /* Allocate a new buffer - hasher will release the old buffer */            
            buffer = rm_buffer_pool_get(tag->main->mem_pool);
            readvec[i].iov_base = buffer->data;
            readvec[i].iov_len = buf_size;
        }

        read_sum += bytes;
    }

    /* Release the rest of the buffers */
    for(int i = 0; i < N; ++i) {
        RmBuffer *buffer = readvec[i].iov_base - offsetof(RmBuffer, data);
        rm_buffer_pool_release(tag->main->mem_pool, buffer);
    }
    
    close(fd);
}

static void hash_factory(RmBuffer *buffer, RmSchedTag *tag) {
    /* Hash buffer->len bytes of buffer->data into buffer->file */
    rm_digest_update(&buffer->file->digest, buffer->data, buffer->len);

    /* Increment the offset */
    g_atomic_pointer_set(
        &buffer->file->hash_offset, 
        g_atomic_pointer_get(&buffer->file->hash_offset) + buffer->len
    );

    g_async_queue_push(tag->main->join_queue, buffer->file);

    /* Return this buffer to the pool */
    rm_buffer_pool_release(tag->main->mem_pool, buffer);
}

static int scheduler_n_processable(GQueue *queue) {
    int processable = 0;
    for(GList * iter = queue->head; iter; iter = iter->next) {
        RmFile * file = iter->data;
        processable += g_atomic_int_get(&file->state) == RM_FILE_STATE_PROCESS;
    }

    return processable;
}

static gsize scheduler_get_next_read_size(gsize read_size) {
    return read_size * 2;
}

static void scheduler_factory(GQueue *device_queue, RmMainTag *main) {
    RmSchedTag *tag = g_slice_new(RmSchedTag);
    tag->main = main;
    
    dev_t current_device = ((RmFile *)device_queue->head->data)->dev;

    /* Get the device of the files in this list */
    bool nonrotational = rm_mounts_is_nonrotational(
        main->session->mounts, 
        current_device
    );

    bool read_direction_is_forward = TRUE;

    tag->read_size = sysconf(_SC_PAGESIZE) * 16; /* Read 16 pages initially */

    while(scheduler_n_processable(device_queue)) {
        tag->hash_pool = g_thread_pool_new(
            (GFunc)hash_factory,
            tag,
            main->session->settings->threads,
            FALSE, NULL
        );
        tag->read_pool = g_thread_pool_new(
            (GFunc)read_factory, 
            tag,
            (nonrotational) ? main->session->settings->threads : 1,
            FALSE, NULL
        );

        // TODO: Only resort every few 100 mb?
        rm_file_list_resort_device_offsets(device_queue, read_direction_is_forward);
        read_direction_is_forward = !read_direction_is_forward;

        for(GList *iter = device_queue->head; iter; iter = iter->next) {
            RmFile *to_be_read = iter->data;
            if(to_be_read->state == RM_FILE_STATE_PROCESS) {
                g_thread_pool_push(tag->read_pool, iter->data, NULL);
            }
        }
    
        /* Block until this iteration is over */
        g_thread_pool_free(tag->read_pool, FALSE, TRUE);
        g_thread_pool_free(tag->hash_pool, FALSE, TRUE);

        tag->read_size = scheduler_get_next_read_size(tag->read_size);
    }

    /* Send the queue itself to make the join thread check if we're
     * finished already
     * */
    g_async_queue_push(tag->main->join_queue, tag->main->join_queue);
}

static void scheduler_findmatches(GQueue *same_size_list) {
    /* same_size_list is a list of files with the same size,
     * find out which are no duplicates.
     * */
    GHashTable * check_table = g_hash_table_new_full(
        g_str_hash, g_str_equal,
        g_free, (GDestroyNotify)g_queue_free
    );

    /* struct for easier handling - think of a string with handles. */
    struct {
        guint8 checksum[_RM_HASH_LEN];
        char nulbyte;
    } keybuf;

    /* Make that memory block act like a string */
    keybuf.nulbyte = '\0';

    for(GList *iter = same_size_list->head; iter; iter = iter->next) {
        RmFile *file = iter->data;

        /* Generate the key */
        rm_digest_steal_buffer(&file->digest, keybuf.checksum, sizeof(keybuf.checksum));

        GQueue * queue = g_hash_table_lookup(check_table, &keybuf);
        if(queue == NULL) {
            queue = g_queue_new();
            g_hash_table_insert(check_table, g_strdup((char *)&keybuf), queue);
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
                if(possible_dupe->hash_offset >= possible_dupe->fsize) {
                    g_atomic_int_set(&possible_dupe->state, RM_FILE_STATE_FINISH);
                }
            }

            // TODO: Call processing of dupe_list here.
        }
    }

    g_hash_table_unref(check_table);
}

static void scheduler_start(RmSession *session, GHashTable *dev_table) {
    RmMainTag tag; 
    tag.session = session;
    tag.mem_pool = rm_buffer_pool_init(sizeof(RmBuffer) + sysconf(_SC_PAGESIZE));
    tag.join_queue = g_async_queue_new();

    GThreadPool * sched_pool = g_thread_pool_new(
        (GFunc)scheduler_factory, 
        &tag,
        tag.session->settings->threads,
        FALSE, NULL
    );

    for(GList *iter = g_hash_table_get_values(dev_table); iter; iter = iter->next) {
        GQueue *device_queue = iter->data;
        g_thread_pool_push(sched_pool, device_queue, NULL);
    }

    GHashTable *size_table = g_hash_table_new_full(
        g_str_hash, g_str_equal,
        g_free, (GDestroyNotify) g_queue_free
    );

    struct sizekey_struct {
        guint64 size;
        guint64 hash_offset;
        char nulbyte;
    } sizekey;

    sizekey.nulbyte = '\0';

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
        sizekey.hash_offset = join_file->hash_offset;

        GQueue *size_list = g_hash_table_lookup(size_table, &sizekey);
        if(size_list == NULL) {
            size_list = g_queue_new();
            g_hash_table_insert(size_table, g_strdup((char *)&sizekey), size_list);
        }

        g_queue_push_head(size_list, join_file);

        /* Find out if the size group has as many items already as the full
         * group */
        if(g_queue_get_length(size_list) == /* TODO */ 42) {
            scheduler_findmatches(size_list);
        }

        /* Garbage collect the size_table from unused entries in regular intervals.
         * This is to keep the memory footprint low.
         * */
        if(gc_counter++ % 100 == 0) {
            struct sizekey_struct * key_struct = NULL;
            for(GList *iter = g_hash_table_get_keys(size_table); iter; iter = iter->next) {
                key_struct = iter->data;
                /* Same size, but less hashed? Forget about it */
                if(key_struct->size == join_file->fsize && key_struct->hash_offset < join_file->hash_offset) {
                    g_hash_table_remove(size_table, key_struct);
                }
            }
        }
    }

    g_thread_pool_free(sched_pool, FALSE, TRUE);

    rm_buffer_pool_destroy(tag.mem_pool);
}

int main(int argc, char const* argv[]) {
    RmSettings settings;
    settings.threads = 4;

    RmSession session;
    session.settings = &settings;

    scheduler_start(&session, NULL);

    return 0;
}
