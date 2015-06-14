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
 *  - Christopher <sahib> Pahl 2010-2015 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2015 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include <fcntl.h>

#include "checksum.h"
#include "hasher.h"
#include "utilities.h"

/* Flags for the fadvise() call that tells the kernel
 * what we want to do with the file.
 */
#define HASHER_FADVISE_FLAGS                                     \
    (0 | POSIX_FADV_SEQUENTIAL /* Read from 0 to file-size    */ \
     | POSIX_FADV_WILLNEED     /* Tell the kernel to readhead */ \
     | POSIX_FADV_NOREUSE      /* We will not reuse old data  */ \
     )

#define DIVIDE_CEIL(n, m) ((n) / (m) + !!((n) % (m)))


struct _RmHasher {
    RmDigestType digest_type;
    gboolean use_buffered_read;
    guint64 cache_quota_bytes;
    gpointer user_data;
    RmBufferPool *mem_pool;
    GAsyncQueue *hashpipe_pool;
    gsize buf_size;
};

typedef _RmHasherTask{
    RmHasher *hasher;
    GThreadPool hashpipe;
    RmDigest *digest;
}

/* GThreadPool Worker for hashing */
static void rm_hasher_hash(RmBuffer *buffer, _U RmHasher *hasher) {

    bool keep_buffer = (buffer->len > 0  && buffer->digest->type==RM_DIGEST_PARANOID);

    if(buffer->len > 0) {
        /* Hash buffer->len bytes_read of buffer->data into buffer->file */
        rm_digest_buffered_update(buffer);
    }

    if (buffer->callback) {
        buffer->callback(buffer);
    }

    /* Return this buffer to the pool */
    if (!keep_buffer) {
        rm_buffer_pool_release(buffer);
    }
}


//////////////////////////////////////
//  File Reading Utilities          //
//////////////////////////////////////

static void rm_hasher_request_readahead(int fd, RmOff seek_offset, RmOff bytes_to_read) {
    /* Give the kernel scheduler some hints */
    RmOff readahead = bytes_to_read * 8;
    posix_fadvise(fd, seek_offset, readahead, HASHER_FADVISE_FLAGS);
    //TODO: avoid duplicate calls
}


static gint64 rm_hasher_symlink_read(RmHasher *hasher, RmDigest *digest, char *path) {

    /* Fake an IO operation on the symlink.
     */
    RmBuffer *buf = rm_buffer_pool_get(hasher->mem_pool);
    buf->len=256;
    memset(buf->data, 0, buf->len);

    RmStat stat_buf;
    if(rm_sys_stat(path, &stat_buf) == -1) {
        /* Oops, that did not work out, report as an error */
        rm_log_perror("Cannot stat symbolic link");
        return 0;
    }

    gint data_size = snprintf((char *)buf->data, rm_buffer_size(hasher->mem_pool), "%"LLU":%ld", (long)stat_buf.st_dev,
                              (long)stat_buf.st_ino);

    rm_digest_buffered_update(buf);

    /* In case of paranoia: shrink the used data buffer, so comparasion works
     * as expected. Otherwise a full buffer is used with possibly different
     * content */
    if(digest->type == RM_DIGEST_PARANOID) {
        rm_digest_paranoia_shrink(digest, data_size);
    } else {
        rm_buffer_pool_release(buf);
    }
    return 0;  //TODO
}


/* Reads data from file and sends to hasher threadpool
 * returns number of bytes successfully read */

static gint64 rm_hasher_buffered_read(RmHasher *hasher, GThreadPool *hashpipe, RmDigest *digest, char *path, gsize start_offset, gsize bytes_to_read) {
    FILE *fd = NULL;
    gsize total_bytes_read = 0;

    if((fd = fopen(path, "rb")) == NULL) {
        rm_log_info("fopen(3) failed for %s: %s\n", path, g_strerror(errno));
        goto finish;
    }

    gint32 bytes_read = 0;

    rm_hasher_request_readahead(fileno(fd), start_offset, bytes_to_read);

    if(fseek(fd, start_offset, SEEK_SET) == -1) {
        rm_log_perror("fseek(3) failed");
        goto finish;
    }

    posix_fadvise(fileno(fd), start_offset, bytes_to_read, HASHER_FADVISE_FLAGS);

    RmBuffer *buffer = rm_buffer_pool_get(hasher->mem_pool);

    while((bytes_read = fread(buffer->data, 1, MIN(bytes_to_read, hasher->buf_size), fd)) > 0) {
        bytes_to_read -= bytes_read;

        buffer->len = bytes_read;
        buffer->digest = digest;
        rm_util_thread_pool_push(hashpipe, buffer);

        total_bytes_read += bytes_read;
        buffer = rm_buffer_pool_get(hasher->mem_pool);
    }
    rm_buffer_pool_release(buffer);

    if(ferror(fd) != 0) {
        rm_log_perror("fread(3) failed");
        if ( total_bytes_read == bytes_to_read ) {
            /* signal error to caller */
            total_bytes_read++;
        }
    }

finish:
    if(fd != NULL) {
        fclose(fd);
    }
    return total_bytes_read;

}

/* Reads data from file and sends to hasher threadpool
 * returns number of bytes successfully read */

static gint64 rm_hasher_unbuffered_read(RmHasher *hasher, GThreadPool *hashpipe, RmDigest *digest, char *path, gint64 start_offset, gint64 bytes_to_read) {
    gint32 bytes_read = 0;
    gint64 total_bytes_read = 0;
    guint64 file_offset = start_offset;

    /* how many buffers to read? */
    const gint16 N_BUFFERS = MIN(4, DIVIDE_CEIL(bytes_to_read, hasher->buf_size));
    struct iovec readvec[N_BUFFERS + 1];

    int fd = 0;

    fd = rm_sys_open(path, O_RDONLY);
    if(fd == -1) {
        rm_log_info("open(2) failed for %s: %s\n", path, g_strerror(errno));
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
    rm_hasher_request_readahead(fd, start_offset, bytes_to_read);

    /* Initialize the buffers to begin with.
     * After a buffer is full, a new one is retrieved.
     */
    RmBuffer **buffers;
    buffers = g_slice_alloc(sizeof(*buffers) * N_BUFFERS);

    memset(readvec, 0, sizeof(readvec));
    for(int i = 0; i < N_BUFFERS; ++i) {
        /* buffer is one contignous memory block */
        buffers[i] = rm_buffer_pool_get(hasher->mem_pool);
        readvec[i].iov_base = buffers[i]->data;
        readvec[i].iov_len = hasher->buf_size;
    }

    while(total_bytes_read < bytes_to_read &&
          (bytes_read = rm_sys_preadv(fd, readvec, N_BUFFERS, file_offset)) > 0) {
        bytes_read = MIN(bytes_read, bytes_to_read - total_bytes_read); /* ignore over-reads */
        int blocks = DIVIDE_CEIL(bytes_read, hasher->buf_size);
        g_assert(blocks <= N_BUFFERS);

        total_bytes_read += bytes_read;

        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = buffers[i];
            buffer->len = MIN(hasher->buf_size, bytes_read - i * hasher->buf_size);
            buffer->digest = digest;

            /* Send it to the hasher */
            rm_util_thread_pool_push(hashpipe, buffer);

            /* Allocate a new buffer - hasher will release the old buffer */
            buffers[i] = rm_buffer_pool_get(hasher->mem_pool);
            readvec[i].iov_base = buffers[i]->data;
            readvec[i].iov_len = hasher->buf_size;
        }
    }

    if(bytes_read == -1) {
        rm_log_perror("preadv failed");
        goto finish;
    } else if(total_bytes_read != bytes_to_read) {
        rm_log_error_line(_("Something went wrong reading %s; expected %li bytes, "
                            "got %li; ignoring"),
                          path, bytes_to_read, total_bytes_read);
    }

    /* Release the rest of the buffers */
    for(int i = 0; i < N_BUFFERS; ++i) {
        rm_buffer_pool_release(buffers[i]);
    }
    g_slice_free1(sizeof(*buffers) * N_BUFFERS, buffers);
finish:
    if(fd > 0) {
        rm_sys_close(fd);
    }

    return total_bytes_read;
}


//////////////////////////////////////
//  RmHasher                        //
//////////////////////////////////////


static void rm_hasher_hashpipe_free(GThreadPool *hashpipe) {
    /* free the GThreadPool; wait for any in-progress jobs to finish */
    g_thread_pool_free(hashpipe, FALSE, TRUE);
}

static int rm_hasher_hashpipe_sort(GThreadPool *a, GThreadPool *b) {
    return g_thread_pool_unprocessed(a) - g_thread_pool_unprocessed(b);
}

/* finds and pops the least busy hashpipe in hashpipe_pool
 * */
static GThreadPool *rm_hasher_hashpipe_get(GAsyncQueue *hashpipe_pool) {
    GThreadPool *hashpipe = NULL;
    g_async_queue_lock(hashpipe_pool);
    {
        g_async_queue_sort_unlocked (hashpipe_pool, (GCompareDataFunc)rm_hasher_hashpipe_sort, NULL);

        /* for optimisation purposes only: print info message if we are ever blocked waiting for a hashpipe */
        hashpipe = g_async_queue_try_pop_unlocked(hashpipe_pool);
        if (!hashpipe) {
            rm_log_info(YELLOW"Blocked waiting for hashpipe..."RESET);
            hashpipe=g_async_queue_pop_unlocked(hashpipe_pool);
            rm_log_info(GREEN"got\n"RESET);
        }
        if (g_thread_pool_unprocessed(hashpipe)>0) {
            rm_log_debug(RED"Got hash pool with %d unprocessed\n"RESET, g_thread_pool_unprocessed(hashpipe));
        }
    }
    g_async_queue_unlock(hashpipe_pool);
    return hashpipe;
}


//////////////////////////////////////
//     API
//////////////////////////////////////

RmHasher *rm_hasher_new(
            RmDigestType digest_type,
            uint num_threads,
            gboolean use_buffered_read,
            gsize buf_size,
            guint64 cache_quota_bytes,
            guint64 target_kept_bytes,
            gpointer user_data) {
    RmHasher *self = g_slice_new(RmHasher);
    self->digest_type = digest_type;

    self->use_buffered_read = use_buffered_read;
    self->buf_size = buf_size;
    self->cache_quota_bytes = cache_quota_bytes;
    self->user_data = user_data;

    /* Create buffer mem pool */
    self->mem_pool = rm_buffer_pool_init(buf_size, cache_quota_bytes, target_kept_bytes);

    /* Create a pool of hashing thread "pools" - each "pool" can only have
     * one thread because hashing must be done in order */
    self->hashpipe_pool = g_async_queue_new_full((GDestroyNotify)rm_hasher_hashpipe_free);
    g_assert(num_threads > 0);
    for(uint i = 0; i < num_threads; i++) {
        g_async_queue_push(
            self->hashpipe_pool,
            rm_util_thread_pool_new((GFunc)rm_hasher_hash, self, 1));
    }
    return self;
}

void rm_hasher_free(RmHasher *hasher) {
    g_async_queue_unref(hasher->hashpipe_pool);
    rm_buffer_pool_destroy(hasher->mem_pool);
    g_slice_free(RmHasher, hasher);
}

RmHasherTask *rm_hasher_hash(RmHasher *hasher, char *path, RmDigest *digest, guint64 start_offset, guint64 bytes_to_read, gboolean is_symlink) {
    GThreadPool *hash_pool = rm_hasher_pool_get(hasher->hash_pool_pool);
    
    If (!digest) {
        //TODO create new digest
    }

    guint64 bytes_read = 0;
    if (is_symlink) {
        bytes_read = rm_hasher_symlink_read(hasher, digest, path);
    } else if (hasher->use_buffered_read) {
        bytes_read = rm_hasher_buffered_read(hasher, hashpipe, digest, path, start_offset, bytes_to_read);
    } else {
        bytes_read = rm_hasher_unbuffered_read(hasher, hashpipe, digest, path, start_offset, bytes_to_read);
    }

    if (bytes_read == bytes_to_read) {
        return hashpipe;
    } else {
        /* read failed, so close the hashpipe and return it to the hashpipe_pool */
        rm_hasher_finish_task(hasher, hashpipe, digest, NULL, NULL);
        return NULL;
    }
}

RmDigest *rm_hasher_finish_hash(RmHasherTask *task, RmDigestCallback callback, gpointer user_data) {
    /* get a dummy buffer to use to signal the hasher thread that this increment is finished */
    RmBuffer *finisher = rm_buffer_pool_get(task->hasher->mem_pool);
    finisher->digest = task->digest;
    finisher->len = 0;
    finisher->callback = callback;
    finisher->user_data = user_data;
    rm_util_thread_pool_push(task->hash_pipe, finisher);

    /* return hash_pool to hash_pool_pool */
    g_async_queue_push(task->hasher->hash_pool_pool, task->);
    return task->digest;
}
