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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>

#include "hasher.h"
#include "utilities.h"

/* Flags for the fadvise() call that tells the kernel
 * what we want to do with the file.
 */
const int HASHER_FADVISE_FLAGS = 0
#ifdef POSIX_FADV_SEQUENTIAL
                                 | POSIX_FADV_SEQUENTIAL /* Read from 0 to file-size    */
#endif
#ifdef POSIX_FADV_WILLNEED
                                 | POSIX_FADV_WILLNEED /* Tell the kernel to readhead */
#endif
#ifdef POSIX_FADV_NOREUSE
                                 | POSIX_FADV_NOREUSE /* We will not reuse old data  */
#endif
    ;

#define DIVIDE_CEIL(n, m) ((n) / (m) + !!((n) % (m)))

/* how many buffers to read? */
const guint16 N_PREADV_BUFFERS = 4;

struct _RmHasher {
    RmDigestType digest_type;
    gboolean use_buffered_read;
    guint64 cache_quota_bytes;
    gpointer session_user_data;
    RmHasherCallback callback;

    GAsyncQueue *hashpipe_pool;
    gint unalloc_hashpipes;
    GAsyncQueue *return_queue;
    GMutex lock;
    GCond cond;

    gsize buf_size;
    guint active_tasks;

    RmSemaphore *buf_sem;
};

struct _RmHasherTask {
    /* pointer back to hasher main */
    RmHasher *hasher;

    /* single-thread threadpool to send buffers to */
    GThreadPool *hashpipe;

    /* checksum to update with read data */
    RmDigest *digest;

    /* user data associated with this specific task */
    gpointer task_user_data;

    /* if true then hasher->callback will be called by rm_hashpipe_worker() */
    gboolean finalise;
};

static void rm_hasher_task_free(RmHasherTask *self) {
    g_async_queue_push(self->hasher->hashpipe_pool, self->hashpipe);
    g_slice_free(RmHasherTask, self);
}

/* GThreadPool Worker for hashing */
static void rm_hasher_hashpipe_worker(RmBuffer *buffer, RmHasher *hasher) {
    g_assert(buffer);
    if(buffer->len > 0) {
        /* Update digest with buffer->data */
        g_assert(buffer->user_data == NULL);
        rm_digest_buffered_update(hasher->buf_sem, buffer);
    } else if(buffer->user_data) {
        /* finalise via callback */
        RmHasherTask *task = buffer->user_data;
        g_assert(task->digest == buffer->digest);

        g_assert(hasher);
        hasher->callback(hasher, task->digest, hasher->session_user_data,
                         task->task_user_data);
        rm_hasher_task_free(task);
        rm_buffer_free(hasher->buf_sem, buffer);

        g_mutex_lock(&hasher->lock);
        {
            /* decrease active task count and signal same */
            hasher->active_tasks--;
            g_cond_signal(&hasher->cond);
        }
        g_mutex_unlock(&hasher->lock);
    }
}

//////////////////////////////////////
//  File Reading Utilities          //
//////////////////////////////////////

static void rm_hasher_request_readahead(int fd, RmOff seek_offset, RmOff bytes_to_read) {
/* Give the kernel scheduler some hints */
#if HAVE_POSIX_FADVISE && HASHER_FADVISE_FLAGS
    RmOff readahead = bytes_to_read * 8;
    posix_fadvise(fd, seek_offset, readahead, HASHER_FADVISE_FLAGS);
#else
    (void)fd;
    (void)seek_offset;
    (void)bytes_to_read;
#endif
}

static gboolean rm_hasher_symlink_read(RmHasher *hasher, GThreadPool *hashpipe,
                                       RmDigest *digest, char *path,
                                       gsize *bytes_actually_read) {
    /* Read contents of symlink (i.e. path of symlink's target).  */

    RmBuffer *buffer = rm_buffer_new(hasher->buf_sem, hasher->buf_size);
    gint len = readlink(path, (char *)buffer->data, hasher->buf_size);

    if (len < 0) {
        rm_log_perror("Cannot read symbolic link");
        rm_buffer_free(hasher->buf_sem, buffer);
        return FALSE;
    }

    *bytes_actually_read = len;
    buffer->len = len;
    buffer->digest = digest;
    buffer->user_data = NULL;
    rm_util_thread_pool_push(hashpipe, buffer);

    return TRUE;
}

/* Reads data from file and sends to hasher threadpool;
 * returns true if no errors encountered;
 * increments *bytes_read by the actual bytes read */

static gboolean rm_hasher_buffered_read(RmHasher *hasher, GThreadPool *hashpipe,
                                        RmDigest *digest, char *path, gsize start_offset,
                                        gsize bytes_to_read, gsize *bytes_actually_read) {
    FILE *fd = NULL;
    fd = fopen(path, "rb");
    if(fd == NULL) {
        rm_log_info("fopen(3) failed for %s: %s\n", path, g_strerror(errno));
        return FALSE;
    }

    gboolean read_to_eof = (bytes_to_read == 0);
    rm_hasher_request_readahead(fileno(fd), start_offset,
                                read_to_eof ? G_MAXSIZE : bytes_to_read);

    if(fseek(fd, start_offset, SEEK_SET) == -1) {
        rm_log_perror("fseek(3) failed");
        fclose(fd);
        return FALSE;
    }

    gboolean success = FALSE;
    gsize bytes_remaining = bytes_to_read;

    while(TRUE) {
        RmBuffer *buffer = rm_buffer_new(hasher->buf_sem, hasher->buf_size);
        gsize want_bytes = MIN(bytes_remaining, hasher->buf_size);
        gsize bytes_read = fread(buffer->data, 1, want_bytes, fd);

        if(ferror(fd) != 0) {
            rm_log_perror("fread(3) failed");
            rm_buffer_free(hasher->buf_sem, buffer);
            break;
        }

        bytes_remaining -= bytes_read;
        *bytes_actually_read += bytes_read;

        buffer->len = bytes_read;
        buffer->digest = digest;
        buffer->user_data = NULL;
        rm_util_thread_pool_push(hashpipe, buffer);

        if(read_to_eof && feof(fd)) {
            success = TRUE;
            break;
        } else if(bytes_remaining == 0) {
            success = TRUE;
            break;
        } else if(feof(fd)) {
            rm_log_error_line("Unexpected EOF in rm_hasher_buffered_read");
            break;
        } else if(bytes_read == 0) {
            rm_log_warning_line(_("Something went wrong reading %s; expected %li bytes, "
                                "got %li; ignoring"),
                              path, (long int)bytes_to_read,
                              (long int)*bytes_actually_read);
            break;
        }
    }
    fclose(fd);
    return success;
}

/* Reads data from file and sends to hasher threadpool
 * returns true if no errors encountered;
 * increments *bytes_read by the actual bytes read */

static gboolean rm_hasher_unbuffered_read(RmHasher *hasher, GThreadPool *hashpipe,
                                          RmDigest *digest, char *path,
                                          gint64 start_offset, gint64 bytes_to_read,
                                          gsize *bytes_actually_read) {
    gint32 bytes_read = 0;
    guint64 file_offset = start_offset;

    gboolean read_to_eof = (bytes_to_read == 0);

    int fd = rm_sys_open(path, O_RDONLY);
    if(fd == -1) {
        rm_log_info("open(2) failed for %s: %s\n", path, g_strerror(errno));
        return FALSE;
    }

    /* preadv() is beneficial for large files since it can cut the
     * number of syscall heavily.  I suggest N_PREADV_BUFFERS=4 as good
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

    guint16 n_preadv_buffers = N_PREADV_BUFFERS;
    if(bytes_to_read > 0) {
        n_preadv_buffers = MIN(n_preadv_buffers, DIVIDE_CEIL(bytes_to_read, hasher->buf_size));
    }

    /* Allocate buffer vector */
    RmBuffer **buffers;
    buffers = g_slice_alloc(sizeof(*buffers) * n_preadv_buffers);

    struct iovec readvec[n_preadv_buffers + 1];
    memset(readvec, 0, sizeof(readvec));

    gboolean success = FALSE;
    gsize bytes_remaining = bytes_to_read;

    while(TRUE) {
        /* allocate buffers for preadv */
        for(int i = 0; i < n_preadv_buffers; ++i) {
            buffers[i] = rm_buffer_new(hasher->buf_sem, hasher->buf_size);
            readvec[i].iov_base = buffers[i]->data;
            readvec[i].iov_len = hasher->buf_size;
        }

        bytes_read = rm_sys_preadv(fd, readvec, n_preadv_buffers, file_offset);

        if(bytes_read == -1) {
            /* error occurred */
            rm_log_perror("preadv failed");
            /* Release the buffers and give up*/
            for(int i = 0; i < n_preadv_buffers; ++i) {
                rm_buffer_free(hasher->buf_sem, buffers[i]);
            }
            break;
        }

        /* ignore over-reads */
        bytes_read = MIN((gsize)bytes_read, bytes_remaining);

        /* update totals */
        file_offset += bytes_read;
        *bytes_actually_read += bytes_read;
        bytes_remaining -= bytes_read;

        /* send buffers */
        for(int i = 0; i < n_preadv_buffers; ++i) {
            RmBuffer *buffer = buffers[i];

            buffer->len = CLAMP(bytes_read - i * (gint32)hasher->buf_size, 0,
                                (gint32)hasher->buf_size);
            if(buffer->len > 0) {
                /* Send it to the hasher */
                buffer->digest = digest;
                buffer->user_data = NULL;
                rm_util_thread_pool_push(hashpipe, buffer);
            } else {
                rm_buffer_free(hasher->buf_sem,  buffer);
            }
        }

        if(read_to_eof && bytes_read == 0) {
            success = TRUE;
            break;
        } else if(bytes_remaining == 0) {
            success = TRUE;
            break;
        } else if(bytes_read == 0) {
            rm_log_error_line(_("Something went wrong reading %s; expected %li bytes, "
                                "got %li; ignoring"),
                              path, (long int)bytes_to_read,
                              (long int)*bytes_actually_read);
            break;
        }
    }

    g_slice_free1(sizeof(*buffers) * n_preadv_buffers, buffers);
    rm_sys_close(fd);

    return success;
}

//////////////////////////////////////
//  RmHasher                        //
//////////////////////////////////////

static void rm_hasher_hashpipe_free(GThreadPool *hashpipe) {
    /* free the GThreadPool; wait for any in-progress jobs to finish */
    g_thread_pool_free(hashpipe, FALSE, TRUE);
}

/* local joiner if user provides no joiner to rm_hasher_new() */
static RmHasherCallback *rm_hasher_joiner(RmHasher *hasher, RmDigest *digest,
                                          _UNUSED gpointer session_user_data,
                                          _UNUSED gpointer task_user_data) {
    g_async_queue_push(hasher->return_queue, digest);
    return 0;
}

//////////////////////////////////////
//     API
//////////////////////////////////////

RmHasher *rm_hasher_new(RmDigestType digest_type,
                        guint num_threads,
                        gboolean use_buffered_read,
                        gsize buf_size,
                        guint64 cache_quota_bytes,
                        RmHasherCallback joiner,
                        gpointer session_user_data) {
    RmHasher *self = g_slice_new0(RmHasher);
    self->digest_type = digest_type;

    if(digest_type != RM_DIGEST_PARANOID) {
        int max_buffers = num_threads * 64;
        if(!use_buffered_read) {
            /*  preadv() uses N_PREADV_BUFFERS in parallel.
             *  Need at least this many for one operation.
             *  */
            max_buffers *= N_PREADV_BUFFERS;
        }

        self->buf_sem = rm_semaphore_new(max_buffers);
    } else {
        self->buf_sem = NULL;
    }

    self->use_buffered_read = use_buffered_read;
    self->buf_size = buf_size;
    self->cache_quota_bytes = cache_quota_bytes;

    if(joiner) {
        self->callback = joiner;
    } else {
        self->callback = (RmHasherCallback)rm_hasher_joiner;
        self->return_queue = g_async_queue_new();
    }

    self->session_user_data = session_user_data;

    /* initialise mutex & cond */
    g_mutex_init(&self->lock);
    g_cond_init(&self->cond);

    /* Create a pool of hashing thread "pools" - each "pool" can only have
     * one thread because hashing must be done in order */
    self->hashpipe_pool = g_async_queue_new_full((GDestroyNotify)rm_hasher_hashpipe_free);
    g_assert(num_threads > 0);
    self->unalloc_hashpipes = num_threads;
    return self;
}

void rm_hasher_free(RmHasher *hasher, gboolean wait) {
    /* Note that hasher may be multi-threaded, both at the reader level and at
     * the hashpipe level.  To ensure graceful exit, the hasher is reference counted
     * via hasher->active_tasks.
     */
    if(wait) {
        g_mutex_lock(&hasher->lock);
        {
            while(hasher->active_tasks > 0) {
                g_cond_wait(&hasher->cond, &hasher->lock);
            }
        }
        g_mutex_unlock(&hasher->lock);
    }

    g_async_queue_unref(hasher->hashpipe_pool);

    g_cond_clear(&hasher->cond);
    g_mutex_clear(&hasher->lock);

    if(hasher->buf_sem) {
        rm_semaphore_destroy(hasher->buf_sem);
    }

    g_slice_free(RmHasher, hasher);
}

RmHasherTask *rm_hasher_task_new(RmHasher *hasher, RmDigest *digest,
                                 gpointer task_user_data) {
    g_mutex_lock(&hasher->lock);
    { hasher->active_tasks++; }
    g_mutex_unlock(&hasher->lock);

    RmHasherTask *self = g_slice_new0(RmHasherTask);
    self->hasher = hasher;
    if(digest) {
        self->digest = digest;
    } else {
        self->digest = rm_digest_new(hasher->digest_type, 0);
    }

    /* get a recycled hashpipe if available */
    self->hashpipe = g_async_queue_try_pop(hasher->hashpipe_pool);
    if(!self->hashpipe) {
        if(g_atomic_int_get(&hasher->unalloc_hashpipes) > 0) {
            /* create a new hashpipe */
            g_atomic_int_dec_and_test(&hasher->unalloc_hashpipes);
            self->hashpipe =
                rm_util_thread_pool_new((GFunc)rm_hasher_hashpipe_worker, hasher, 1);

        } else {
            /* already at thread limit - wait for a hashpipe to come available */
            self->hashpipe = g_async_queue_pop(hasher->hashpipe_pool);
        }
    }
    g_assert(self->hashpipe);

    self->task_user_data = task_user_data;
    return self;
}

gboolean rm_hasher_task_hash(RmHasherTask *task, char *path, guint64 start_offset,
                             gsize bytes_to_read, gboolean is_symlink,
                             gsize *bytes_read_out) {
    gsize bytes_read = 0;
    gboolean success = false;

    if(is_symlink) {
        success = rm_hasher_symlink_read(task->hasher, task->hashpipe, task->digest,
                                         path, &bytes_read);
    } else if(task->hasher->use_buffered_read) {
        success = rm_hasher_buffered_read(task->hasher, task->hashpipe, task->digest,
                                          path, start_offset, bytes_to_read, &bytes_read);
    } else {
        success =
            rm_hasher_unbuffered_read(task->hasher, task->hashpipe, task->digest, path,
                                      start_offset, bytes_to_read, &bytes_read);
    }

    if(bytes_read_out != NULL) {
        *bytes_read_out = bytes_read;

    }

    return success;
}

RmDigest *rm_hasher_task_finish(RmHasherTask *task) {
    /* get a dummy buffer to use to signal the hasher thread that this increment is
     * finished */
    RmHasher *hasher = task->hasher;
    RmBuffer *finisher = rm_buffer_new(hasher->buf_sem, hasher->buf_size);
    finisher->digest = task->digest;
    finisher->len = 0;
    finisher->user_data = task;
    rm_util_thread_pool_push(task->hashpipe, finisher);

    if(hasher->return_queue) {
        return g_async_queue_pop(hasher->return_queue);
    } else {
        return NULL;
    }
}
