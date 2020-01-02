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

#ifndef RM_HASHER_H
#define RM_HASHER_H

#include <glib.h>
#include "checksum.h"
#include "config.h"

/**
 * @file hasher.h
 * @brief High level API for file hashing
 *
 * Provides a high-level API for rmlint's low-level checksum.h API.
 *
 * File reading is done in the foreground thread (multi-threading of
 * file reading is generally a bad idea because it tends to lead to
 * disk thrash for rotational disks, and gives little or no benefit
 * for non-rotational devices.
 *
 * File checksum calculation is done in one or more background threads,
 * so that file reading can proceed uninterrupted.  This should give
 * faster performance than conventional (single-threaded) checksum
 * calculating utilities
 *
 *
 **/

/**
 * @struct RmHasher
 *
 * RmHasher is an opaque data structure to represent a hashing engine.
 * It should be created and destroyed via rm_hasher_new() and rm_hasher_free().
 * File hashing tasks are carried out via RmHasherTask objects.
 **/
typedef struct _RmHasher RmHasher;

/**
 * @struct RmHasherTask
 * RmHasherTask is an opaque data structure for carrying out file hashing.
 * Individual tasks are created, executed and finalised via rm_hasher_task_new(),
 * rm_hasher_task_hash() and rm_hasher_task_finish().
 **/
typedef struct _RmHasherTask RmHasherTask;

/**
 * @brief RmHasherCallback function prototype for rm_hasher_task_finish()
 *
 * @param digest The finished RmDigest
 * @param session_user_data User data passed to rm_hasher_new()
 * @param file_user_data User data passed to rm_hasher_task_new()
 * @retval (UNUSED)
 **/
typedef int (*RmHasherCallback)(RmHasher *hasher,
                                RmDigest *digest,
                                gpointer session_user_data,
                                gpointer task_user_data);

/**
 * @brief Allocate and initialise a new hashing object
 *
 * @param digest_type The type of digest
 * @param num_threads The maximum number of hashing threads
 * @param use_buffered_read If TRUE, read using preadv()
 * @param buf_size Size of each read buffer size in bytes
 * @param cache_quota_bytes Total bytes to allocate for read buffers
 * @param target_kept_bytes Target number of bytes to be stored in paranoid digest buffers
 * @param joiner The callback function for completed digests, triggered by
 *rm_hasher_task_finish()
 * If joiner is NULL then rm_hasher_task_finish() will wait for (background) hashing to
 *finish
 * and return the completed digest to the caller.
 *
 * @param session_user_data Pointer to user data associated with the session; will be
 *passed to joiner.
 *
 **/
RmHasher *rm_hasher_new(RmDigestType digest_type,
                        uint num_threads,
                        gboolean use_buffered_read,
                        gsize buf_size,
                        guint64 cache_quota_bytes,
                        RmHasherCallback joiner,
                        gpointer session_user_data);

/**
 * @brief Free a hashing object
 *
 * @param wait Wait for pending tasks to finish.
 **/
void rm_hasher_free(RmHasher *hasher, gboolean wait);

/**
 * @brief Allocate and initialise a new hashing task.
 *
 * @param hasher The hashing object
 * @param digest An existing digest to update.  If NULL passed, will allocate a new
 *digest.
 * @param task_user_data Pointer to user data associated with the task; will be passed to
 *session->joiner by rm_hasher_task_finish().
 **/
RmHasherTask *rm_hasher_task_new(RmHasher *hasher,
                                 RmDigest *digest,
                                 gpointer task_user_data);

/**
 * @brief Read data from a file and send it for hashing in separate thread
 *
 * @param task  An existing RmHasherTask
 * @param path  The file path to read from
 * @param start_offset  Where to start reading the file (number of bytes from start)
 * @param bytes_to_read  How many bytes to read (pass 0 to read whole file)
 * @param is_symlink  If path is a symlink, pass TRUE to read the symlink itself rather
 * @param bytes_read_out Out parameter for the number of bytes physically read.
 *than the linked file
 * @retval FALSE if read errors occurred
 **/
gboolean rm_hasher_task_hash(RmHasherTask *task,
                             char *path,
                             guint64 start_offset,
                             size_t bytes_to_read,
                             gboolean is_symlink,
                             gsize *bytes_read_out);

/**
 * @brief Finalise a hashing task
 *
 * @param task  An existing RmHasherTask
 *
 * If a NULL callback passed, will wait for the task to finish and return the finalised
 *RmDigest to caller.
 **/
RmDigest *rm_hasher_task_finish(RmHasherTask *task);

#endif /* end of include guard */
