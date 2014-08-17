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

RmFile *rm_file_new(const char *path,
                    guint64 fsize,
                    ino_t node,
                    dev_t dev,
                    time_t mtime,
                    RmLintType type,
                    bool is_ppath,
                    unsigned pnum) {
    RmFile *self = g_slice_new0(RmFile);
    self->path = g_strdup(path);
    self->node = node;
    self->dev = dev;
    self->mtime = mtime;
    self->hash_offset = 0;
    self->seek_offset = 0;
    self->state = RM_FILE_STATE_PROCESS;

    // TODO: Use the actualy type from session -> pass it.
    rm_digest_init(&self->digest, RM_DIGEST_SPOOKY, 0, 0);
    g_mutex_init(&self->file_lock);

    if(type == RM_LINT_TYPE_DUPE_CANDIDATE) {
        // self->disk_offsets = rm_offset_create_table(self->path);
        // self->phys_offset = rm_offset_lookup(self->disk_offsets, 0);
        /* TODO: delay this until we have matched file sizes */
        self->file_size = fsize;
    } else {
        self->file_size = 0;
        self->phys_offset = 0;
    }

    self->lint_type = type;

    self->in_ppath = is_ppath;
    self->path_index = pnum;

    /* initialised with no hardlink*/
    self->hardlinked_original = NULL;

    return self;
}

void rm_file_destroy(RmFile *file) {
    g_free(file->path);
    rm_digest_finalize(&file->digest);
    g_mutex_clear(&file->file_lock);
    if (file->disk_offsets) {
        g_sequence_free(file->disk_offsets);
    }
    g_slice_free(RmFile, file);
}
