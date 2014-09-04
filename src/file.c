/**
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
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include "file.h"
#include "utilities.h"

RmFile *rm_file_new(
    const char *path, struct stat *statp, RmLintType type,
    bool is_ppath, unsigned pnum
) {
    RmFile *self = g_slice_new0(RmFile);
    self->path = g_strdup(path);
    self->basename = rm_util_basename(path);

    self->inode = statp->st_ino;
    self->dev = statp->st_dev;
    self->mtime = statp->st_mtim.tv_sec;

    if(type == RM_LINT_TYPE_DUPE_CANDIDATE) {
        self->file_size = statp->st_size;
    }

    self->lint_type = type;
    self->is_prefd = is_ppath;
    self->path_index = pnum;

    self->hardlinks.files = NULL;
    self->hardlinks.has_non_prefd = FALSE;
    self->hardlinks.has_prefd = FALSE;

    return self;
}

void rm_file_destroy(RmFile *file) {
    g_free(file->path);

    if (file->digest) {
        rm_digest_free(file->digest);
        file->digest = NULL;
    }

    if (file->disk_offsets) {
        g_sequence_free(file->disk_offsets);
    }
    if (file->hardlinks.files) {
        g_queue_free_full(file->hardlinks.files, (GDestroyNotify)rm_file_destroy);
    }

    g_slice_free(RmFile, file);
}
