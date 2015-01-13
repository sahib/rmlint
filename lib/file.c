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

#include <unistd.h>
#include <sys/file.h>

RmFile *rm_file_new(
    RmCfg *cfg, const char *path, RmStat *statp, RmLintType type,
    bool is_ppath, unsigned pnum
) {
    RmOff actual_file_size = statp->st_size;
    RmOff start_seek = 0;

    /* Allow an actual file size of 0 for empty files */
    if(actual_file_size != 0) {
        if(cfg->use_absolute_start_offset) {
            start_seek = cfg->skip_start_offset;
            if(cfg->skip_start_offset >= actual_file_size) {
                return NULL;
            }
        } else {
            start_seek = cfg->skip_start_factor * actual_file_size;
            if((int)(actual_file_size * cfg->skip_end_factor) == 0) {
                return NULL;
            }

            if(start_seek >= actual_file_size) {
                return NULL;
            }
        }
    }

    RmFile *self = g_slice_new0(RmFile);
    self->path = g_strdup(path);
    self->basename = rm_util_basename(self->path);

    self->inode = statp->st_ino;
    self->dev = statp->st_dev;
    self->mtime = statp->st_mtim.tv_sec;

    if(type == RM_LINT_TYPE_DUPE_CANDIDATE) {
        if(cfg->use_absolute_end_offset) {
            self->file_size = CLAMP(actual_file_size, 1, cfg->skip_end_offset);
        } else {
            self->file_size = actual_file_size * cfg->skip_end_factor;
        }
    }

    self->seek_offset = self->hash_offset = start_seek;

    self->lint_type = type;
    self->is_prefd = is_ppath;
    self->is_original = false;
    self->is_symlink = false;
    self->path_index = pnum;
    self->cfg = cfg;

    return self;
}

void rm_file_destroy(RmFile *file) {

    if (file->disk_offsets) {
        g_sequence_free(file->disk_offsets);
    }
    if (file->hardlinks.files) {
        g_queue_free_full(file->hardlinks.files, (GDestroyNotify)rm_file_destroy);
    }

    g_free(file->path);
    g_slice_free(RmFile, file);
}

const char *rm_file_lint_type_to_string(RmLintType type) {
    static const char *TABLE[]            = {
        [RM_LINT_TYPE_UNKNOWN]            = "",
        [RM_LINT_TYPE_EDIR]               = "emptydir",
        [RM_LINT_TYPE_NBIN]               = "nonstripped",
        [RM_LINT_TYPE_BLNK]               = "badlink",
        [RM_LINT_TYPE_BADUID]             = "baduid",
        [RM_LINT_TYPE_BADGID]             = "badgid",
        [RM_LINT_TYPE_BADUGID]            = "badugid",
        [RM_LINT_TYPE_EFILE]              = "emptyfile",
        [RM_LINT_TYPE_DUPE_CANDIDATE]     = "duplicate_file",
        [RM_LINT_TYPE_DUPE_DIR_CANDIDATE] = "duplicate_dir",
        [RM_LINT_TYPE_UNFINISHED_CKSUM]   = "unfinished_cksum"
    };

    return TABLE[MIN(type, sizeof(TABLE) / sizeof(const char *))];
}
