#include <glib.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

#include <alloca.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "src/checksum.h"
#include "src/list.h"


// TODO: Writeup. What does this do?

typedef struct RmBufferPool {
    GTrashStack *stack;
    GMutex lock;
    gsize size;
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
    GAsyncQueue *join_queue;
    RmBufferPool *mem_pool;
    gsize read_size;
    GHashTable * zigzag_table;
} RmMainTag;

typedef struct RmSchedTag {
    RmMainTag *main;
    GQueue *device_list;
    GThreadPool *hash_pool;
    GThreadPool *read_pool;
    int iteration_counter;
} RmSchedTag;


typedef struct RmBuffer {
    RmFile *file; 

    gsize len;
    /* must be last member of RmBuffer */
    guint8 data[];
} RmBuffer;


static void read_factory(RmFile *file, RmSchedTag *tag) {
    const int N = 4; // TODO: Find good N.
    struct iovec readvec[N];
    gsize buf_size = rm_buffer_pool_size(tag->main->mem_pool) - offsetof(RmBuffer, data);
    gsize read_sum = 0, may_read_max = MIN(tag->main->read_size + file->hash_offset, file->fsize);

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

    /* Return this buffer to the pool */
    rm_buffer_pool_release(tag->main->mem_pool, buffer);
}

static void scheduler_factory(GQueue *device_queue, RmMainTag *main) {
    RmSchedTag *tag = g_slice_new(RmSchedTag);
    tag->main = main;
    
    /* Get the device of the files in this list */
    bool nonrotational = rm_mounts_is_nonrotational(
        main->session->mounts, 
        ((RmFile *)device_queue->head->data)->dev
    );

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

    /* TODO: Sort devlist by current offset and blocknumber,
     * alternating between reverse and forward. (alternate the true/false)
     * Also only resort every few 100MB
     */
    rm_file_list_resort_device_offsets(device_queue, true);

    for(GList *iter = device_queue->head; iter; iter = iter->next) {
        g_thread_pool_push(tag->read_pool, iter->data, NULL);
    }
    
    /* Block until this iteration is over */
    g_thread_pool_free(tag->read_pool, FALSE, TRUE);
    g_thread_pool_free(tag->hash_pool, FALSE, TRUE);
}

static gsize scheduler_get_next_read_size(gsize read_size) {
    return read_size * 2;
}

static gboolean scheduler_iterate(RmMainTag *tag, GHashTable *dev_table) {
    GThreadPool * sched_pool = g_thread_pool_new(
        (GFunc)scheduler_factory, 
        &tag,
        tag->session->settings->threads,
        FALSE, NULL
    );

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, dev_table);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        g_thread_pool_push(sched_pool, value, NULL);
    }

    /* Read more next time */
    tag->read_size = scheduler_get_next_read_size(tag->read_size);

    g_thread_pool_free(sched_pool, FALSE, TRUE);

    // TODO: Do find matches stuff.
    //
    return FALSE;
}

static void scheduler_start(RmSession *session) {
    RmMainTag tag; 
    tag.session = session;
    tag.read_size = sysconf(_SC_PAGESIZE) * 16; /* Read 16 pages initially */
    tag.mem_pool = rm_buffer_pool_init(sizeof(RmBuffer) + sysconf(_SC_PAGESIZE));
    tag.join_queue = g_async_queue_new();

    GHashTable *files = NULL; /* TODO */

    while(scheduler_iterate(&tag, files));
}

int main(int argc, char const* argv[]) {
    RmSettings settings;
    settings.threads = 4;

    RmSession session;
    session.settings = &settings;

    scheduler_start(&session);
    return 0;
}
