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
#define SHRED_FADVISE_FLAGS                                     \
    (0 | POSIX_FADV_SEQUENTIAL /* Read from 0 to file-size    */ \
     | POSIX_FADV_WILLNEED     /* Tell the kernel to readhead */ \
     | POSIX_FADV_NOREUSE      /* We will not reuse old data  */ \
     )

#define DIVIDE_CEIL(n, m) ((n) / (m) + !!((n) % (m)))

/* Hash file. Runs as threadpool in parallel / tandem with rm_shred_read_factory above
 * */
static void rm_shred_hash_factory(RmBuffer *buffer, RmShredTag *tag) {
    g_assert(tag);
    g_assert(buffer);
    RmFile *file = buffer->file;
    bool keep_buffer = (buffer->finished==FALSE  && file->digest->type==RM_DIGEST_PARANOID);

    if(!buffer->finished) {
        /* Hash buffer->len bytes_read of buffer->data into buffer->file */
        rm_digest_buffered_update(buffer->file->digest, buffer);
        buffer->file->hash_offset += buffer->len;
    } else {
        /* Report the progress to rm_shred_devlist_factory */
        g_assert(buffer->file->hash_offset == buffer->file->shred_group->next_offset ||
                 buffer->file->status == RM_FILE_STATE_FRAGMENT ||
                 buffer->file->status == RM_FILE_STATE_IGNORE);

        if(buffer->file->status != RM_FILE_STATE_IGNORE) {
            /* remember that checksum */
            rm_shred_write_cksum_to_xattr(tag->session, buffer->file);
        }

        if(buffer->file->devlist_waiting) {
            /* devlist factory is waiting for result */
            g_async_queue_push(buffer->file->device->hashed_file_return, buffer->file);
        } else {
            /* handle the file ourselves; devlist factory has moved on to the next file */
            if(buffer->file->status == RM_FILE_STATE_FRAGMENT) {
                rm_shred_push_queue_sorted(buffer->file);
            } else {
                rm_shred_sift(buffer->file);
            }
        }
    }

    /* Return this buffer to the pool */
    if (!keep_buffer) {
        rm_buffer_pool_release(buffer);
        /*TODO: probably easier to do this in checksum.c */
    }
}


static void rm_shred_request_readahead(int fd, RmFile *file, RmOff bytes_to_read) {
    /* Give the kernel scheduler some hints */
    if(file->fadvise_requested) {
        RmOff readahead = MIN(file->file_size - file->seek_offset, bytes_to_read * 8);
        posix_fadvise(fd, file->seek_offset, readahead, SHRED_FADVISE_FLAGS);
        file->fadvise_requested = 1;
    }
}

void rm_shred_readlink_factory(RmFile *file, RmShredDevice *device) {
    g_assert(file->is_symlink);

    /* Fake an IO operation on the symlink.
     */
    RmBuffer *buf = rm_buffer_pool_get(device->main->mem_pool);
    buf->len=256;
    memset(buf->data, 0, buf->len);
    RM_DEFINE_PATH(file);

    RmStat stat_buf;
    if(rm_sys_stat(file_path, &stat_buf) == -1) {
        /* Oops, that did not work out, report as an error */
        rm_log_perror("Cannot stat symbolic link");
        file->status = RM_FILE_STATE_IGNORE;
        return;
    }

    file->status = RM_FILE_STATE_NORMAL;
    file->seek_offset = file->file_size;
    file->hash_offset = file->file_size;

    g_assert(file->digest);

    gint data_size = snprintf((char *)buf->data, rm_buffer_size(buf->pool), "%"LLU":%ld", (long)stat_buf.st_dev,
                              (long)stat_buf.st_ino);

    rm_digest_buffered_update(file->digest, buf);

    /* In case of paranoia: shrink the used data buffer, so comparasion works
     * as expected. Otherwise a full buffer is used with possibly different
     * content */
    if(file->digest->type == RM_DIGEST_PARANOID) {
        rm_digest_paranoia_shrink(file->digest, data_size);
    } else {
        rm_buffer_pool_release(buf);
    }

    rm_shred_adjust_counters(device, 0, -(gint64)file->file_size);
}

void rm_shred_buffered_read_factory(RmFile *file, RmShredDevice *device, GThreadPool *hash_pool) {
    FILE *fd = NULL;
    gint32 total_bytes_read = 0;

    gint32 buf_size = rm_buffer_size(device->main->mem_pool);
    //buf_size -= offsetof(RmBuffer, data);

    RmBuffer *buffer = rm_buffer_pool_get(device->main->mem_pool);

    if(file->seek_offset >= file->file_size) {
        goto finish;
    }

    RM_DEFINE_PATH(file);

    if((fd = fopen(file_path, "rb")) == NULL) {
        file->status = RM_FILE_STATE_IGNORE;
        rm_log_info("fopen(3) failed for %s: %s\n", file_path, g_strerror(errno));
        goto finish;
    }

    gint32 bytes_to_read = rm_shred_get_read_size(file, device->main);
    gint32 bytes_read = 0;

    rm_shred_request_readahead(fileno(fd), file, bytes_to_read);

    if(fseek(fd, file->seek_offset, SEEK_SET) == -1) {
        file->status = RM_FILE_STATE_IGNORE;
        rm_log_perror("fseek(3) failed");
        goto finish;
    }

    posix_fadvise(fileno(fd), file->seek_offset, bytes_to_read, SHRED_FADVISE_FLAGS);

    while((bytes_read = fread(buffer->data, 1, MIN(bytes_to_read, buf_size), fd)) > 0) {
        file->seek_offset += bytes_read;
        bytes_to_read -= bytes_read;

        buffer->file = file;
        buffer->len = bytes_read;
        buffer->finished = FALSE;

        rm_util_thread_pool_push(hash_pool, buffer);

        total_bytes_read += bytes_read;
        buffer = rm_buffer_pool_get(device->main->mem_pool);
    }

    if (file->current_fragment_physical_offset > 0
        && file->seek_offset < file->file_size
        && file->seek_offset >= file->next_fragment_logical_offset) {
        /* TODO: test if second test actually improves speed
         * (if not then RmFile can probably live without RmOff next_fragment_logical_offset) */
        bool jumped = (file->next_fragment_logical_offset != 0);
        file->current_fragment_physical_offset = rm_offset_get_from_fd(fileno(fd), file->seek_offset, &file->next_fragment_logical_offset);
        if (jumped && file->current_fragment_physical_offset != 0) {
            device->new_seek_position = file->current_fragment_physical_offset; /* no lock required */
        }
    }


    if(ferror(fd) != 0) {
        file->status = RM_FILE_STATE_IGNORE;
        rm_log_perror("fread(3) failed");
    }

finish:
    if(fd != NULL) {
        fclose(fd);
    }

    /* Update totals for device and session*/
    rm_shred_adjust_counters(device, 0, -(gint64)total_bytes_read);
}

/* Read from file and send to hasher
 * Note this was initially a separate thread but is currently just called
 * directly from rm_devlist_factory.
 * */
void rm_shred_unbuffered_read_factory(RmFile *file, RmShredDevice *device, GThreadPool *hash_pool) {
    gint32 bytes_read = 0;
    gint32 total_bytes_read = 0;

    RmOff buf_size = rm_buffer_size(device->main->mem_pool);

    gint32 bytes_to_read = rm_shred_get_read_size(file, device->main);
    gint32 bytes_left_to_read = bytes_to_read;

    // TODO g_assert(!file->is_symlink);
    g_assert(bytes_to_read > 0);
    g_assert(bytes_to_read + file->hash_offset <= file->file_size);
    g_assert(file->seek_offset == file->hash_offset);

    /* how many buffers to read? */
    const gint16 N_BUFFERS = MIN(4, DIVIDE_CEIL(bytes_to_read, buf_size));
    struct iovec readvec[N_BUFFERS + 1];

    int fd = 0;

    if(file->seek_offset >= file->file_size) {
        goto finish;
    }

    RM_DEFINE_PATH(file);

    fd = rm_sys_open(file_path, O_RDONLY);
    if(fd == -1) {
        rm_log_info("open(2) failed for %s: %s\n", file_path, g_strerror(errno));
        file->status = RM_FILE_STATE_IGNORE;
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
    rm_shred_request_readahead(fd, file, bytes_to_read);

    /* Initialize the buffers to begin with.
     * After a buffer is full, a new one is retrieved.
     */
    RmBuffer **buffers;
    buffers = g_slice_alloc(sizeof(*buffers) * N_BUFFERS);

    memset(readvec, 0, sizeof(readvec));
    for(int i = 0; i < N_BUFFERS; ++i) {
        /* buffer is one contignous memory block */
        buffers[i] = rm_buffer_pool_get(device->main->mem_pool);
        readvec[i].iov_base = buffers[i]->data;
        readvec[i].iov_len = buf_size;
    }

    while(bytes_left_to_read > 0 &&
          (bytes_read = rm_sys_preadv(fd, readvec, N_BUFFERS, file->seek_offset)) > 0) {
        bytes_read = MIN(bytes_read, bytes_left_to_read); /* ignore over-reads */
        int blocks = DIVIDE_CEIL(bytes_read, buf_size);

        bytes_left_to_read -= bytes_read;
        file->seek_offset += bytes_read;
        total_bytes_read += bytes_read;

        for(int i = 0; i < blocks; ++i) {
            /* Get the RmBuffer from the datapointer */
            RmBuffer *buffer = buffers[i];
            buffer->file = file;
            buffer->len = MIN(buf_size, bytes_read - i * buf_size);
            buffer->finished = FALSE;
            if(bytes_left_to_read < 0) {
                rm_log_error_line(_("Negative bytes_left_to_read for %s"), file_path);
            }

            /* Send it to the hasher */
            rm_util_thread_pool_push(hash_pool, buffer);

            /* Allocate a new buffer - hasher will release the old buffer */
            buffers[i] = rm_buffer_pool_get(device->main->mem_pool);
            readvec[i].iov_base = buffers[i]->data;
            readvec[i].iov_len = buf_size;
        }
    }

    if (file->current_fragment_physical_offset > 0
        && file->seek_offset < file->file_size
        && file->seek_offset >= file->next_fragment_logical_offset) {
        /* TODO: test if second test actually improves speed
         * (if not then RmFile can probably live without RmOff next_fragment_logical_offset) */
        bool jumped = (file->next_fragment_logical_offset != 0);
        file->current_fragment_physical_offset = rm_offset_get_from_fd(fd, file->seek_offset, &file->next_fragment_logical_offset);
        if (jumped && file->current_fragment_physical_offset != 0) {
            device->new_seek_position = file->current_fragment_physical_offset; /* no lock required */
        }
    }

    if(bytes_read == -1) {
        rm_log_perror("preadv failed");
        file->status = RM_FILE_STATE_IGNORE;
        goto finish;
    } else if(total_bytes_read != bytes_to_read) {
        rm_log_error_line(_("Something went wrong reading %s; expected %d bytes, "
                            "got %d; ignoring"),
                          file_path, bytes_to_read, total_bytes_read);
        file->status = RM_FILE_STATE_IGNORE;
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

    /* Update totals for device and session*/
    rm_shred_adjust_counters(device, 0, -(gint64)total_bytes_read);
}
