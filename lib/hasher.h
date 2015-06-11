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

#ifndef RM_HASHER_H
#define RM_HASHER_H

#include <glib.h>
#include "config.h"

typedef struct _RmHasher RmHasher;

typedef GThreadPool RmHasherTask;

RmHasher *rm_hasher_new(
            RmDigestType digest_type,
            uint num_threads,
            gboolean use_buffered_read,
            gsize buf_size,
            guint64 cache_quota_bytes,
            guint64 target_kept_bytes,
            gpointer user_data);

void rm_hasher_free(RmHasher *hasher);

RmHasherTask *rm_hasher_start_increment(RmHasher *hasher, char *path, RmDigest *digest, guint64 start_offset, guint64 bytes_to_read, gboolean is_symlink);

void rm_hasher_finish_increment(RmHasher *hasher, RmHasherTask *increment, RmDigest *digest, RmDigestCallback callback, gpointer file_user_data);

#endif /* end of include guard */
