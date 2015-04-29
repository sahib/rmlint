/**
* This file is part of rmlint.
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
*/

#include "file.h"
#include "utilities.h"
#include "session.h"
#include "swap-table.h"

#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <string.h>


RmFile *rm_file_new(struct RmSession *session, const char *path, size_t path_len,
                    RmStat *statp, RmLintType type, bool is_ppath, unsigned path_index) {
    RmCfg *cfg = session->cfg;
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
    self->session = session;

    rm_file_set_path(self, (char *)path, path_len, true);

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
    self->path_index = path_index;

    return self;
}

void rm_file_set_path(RmFile *file, char *path, size_t path_len, bool copy) {
    if(file->session->cfg->use_meta_cache == false) {
        file->folder = rm_trie_insert(&file->session->cfg->folder_tree_root, path, NULL);
        file->basename = (copy) ? g_strdup(file->folder->basename) : file->folder->basename;

    } else {
        file->path_id = rm_swap_table_insert(
            file->session->meta_cache, file->session->meta_cache_path_id, (char *)path,
            path_len + 1);
    }
}

void rm_file_lookup_path(const struct RmSession *session, RmFile *file, char *buf) {
    g_assert(file);

    RmOff id = file->path_id;

    memset(buf, 0, PATH_MAX);
    rm_swap_table_lookup(session->meta_cache, session->meta_cache_path_id, id, buf,
                         PATH_MAX);
}

void rm_file_build_path(RmFile *file, char *buf) {
    g_assert(file);

    rm_trie_build_path(file->folder, buf, PATH_MAX);
}

void rm_file_destroy(RmFile *file) {
    if(file->disk_offsets) {
        g_sequence_free(file->disk_offsets);
    }
    if(file->hardlinks.is_head && file->hardlinks.files) {
        g_queue_free_full(file->hardlinks.files, (GDestroyNotify)rm_file_destroy);
    }

    /* Only delete the basename when it really was in memory */
    if(file->session->cfg->use_meta_cache == false) {
        g_free(file->basename);
    }
    g_slice_free(RmFile, file);
}

const char *rm_file_lint_type_to_string(RmLintType type) {
    static const char *TABLE[] = {[RM_LINT_TYPE_UNKNOWN] = "",
                                  [RM_LINT_TYPE_EMPTY_DIR] = "emptydir",
                                  [RM_LINT_TYPE_NONSTRIPPED] = "nonstripped",
                                  [RM_LINT_TYPE_BADLINK] = "badlink",
                                  [RM_LINT_TYPE_BADUID] = "baduid",
                                  [RM_LINT_TYPE_BADGID] = "badgid",
                                  [RM_LINT_TYPE_BADUGID] = "badugid",
                                  [RM_LINT_TYPE_EMPTY_FILE] = "emptyfile",
                                  [RM_LINT_TYPE_DUPE_CANDIDATE] = "duplicate_file",
                                  [RM_LINT_TYPE_DUPE_DIR_CANDIDATE] = "duplicate_dir",
                                  [RM_LINT_TYPE_UNFINISHED_CKSUM] = "unfinished_cksum"};

    return TABLE[MIN(type, sizeof(TABLE) / sizeof(const char *))];
}
