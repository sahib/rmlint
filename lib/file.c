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
*  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
*  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
*
* Hosted on http://github.com/sahib/rmlint
*/

#include "file.h"
#include "session.h"
#include "utilities.h"

#include <string.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

RmFile *rm_file_new(struct RmSession *session, const char *path, RmStat *statp,
                    RmLintType type, bool is_ppath, unsigned path_index, short depth) {
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

    rm_file_set_path(self, (char *)path);

    self->depth = depth;
    self->path_depth = rm_util_path_depth(path);
    self->file_size = 0;
    self->actual_file_size = 0;

    self->inode = statp->st_ino;
    self->dev = statp->st_dev;
    self->mtime = rm_sys_stat_mtime_float(statp);
    self->is_new = (self->mtime >= cfg->min_mtime);

    if(type == RM_LINT_TYPE_DUPE_CANDIDATE) {
        if(cfg->use_absolute_end_offset) {
            self->file_size = CLAMP(actual_file_size, 1, cfg->skip_end_offset);
        } else {
            self->file_size = actual_file_size * cfg->skip_end_factor;
        }

        /* Check if the actual slice the file will be > 0; we don't want empty files in shredder */
        if((self->file_size - start_seek) == 0 && actual_file_size != 0) {
            return NULL;
        }

        self->actual_file_size = actual_file_size;
    }

    self->hash_offset = start_seek;

    self->lint_type = type;
    self->is_prefd = is_ppath;
    self->is_original = false;
    self->is_symlink = false;
    self->path_index = path_index;
    self->outer_link_count = -1;

    return self;
}

void rm_file_set_path(RmFile *file, char *path) {
    file->folder = rm_trie_insert(&file->session->cfg->file_trie, path, NULL);
}

void rm_file_build_path(RmFile *file, char *buf) {
    g_assert(file);

    rm_trie_build_path(&file->session->cfg->file_trie, file->folder, buf, PATH_MAX);
}

void rm_file_destroy(RmFile *file) {
    if(file->hardlinks) {
        g_queue_remove(file->hardlinks, file);
        if(file->hardlinks->length == 0) {
            g_queue_free(file->hardlinks);
        }
    }

    if(file->ext_cksum) {
        g_free(file->ext_cksum);
    }

    if(file->free_digest) {
        rm_digest_free(file->digest);
    }

    g_slice_free(RmFile, file);
}

static const char *LINT_TYPES[] = {[RM_LINT_TYPE_UNKNOWN] = "",
                                   [RM_LINT_TYPE_EMPTY_DIR] = "emptydir",
                                   [RM_LINT_TYPE_NONSTRIPPED] = "nonstripped",
                                   [RM_LINT_TYPE_BADLINK] = "badlink",
                                   [RM_LINT_TYPE_BADUID] = "baduid",
                                   [RM_LINT_TYPE_BADGID] = "badgid",
                                   [RM_LINT_TYPE_BADUGID] = "badugid",
                                   [RM_LINT_TYPE_EMPTY_FILE] = "emptyfile",
                                   [RM_LINT_TYPE_DUPE_CANDIDATE] = "duplicate_file",
                                   [RM_LINT_TYPE_DUPE_DIR_CANDIDATE] = "duplicate_dir",
                                   [RM_LINT_TYPE_PART_OF_DIRECTORY] = "part_of_directory",
                                   [RM_LINT_TYPE_UNIQUE_FILE] = "unique_file"};

const char *rm_file_lint_type_to_string(RmLintType type) {
    return LINT_TYPES[MIN(type, sizeof(LINT_TYPES) / sizeof(const char *))];
}

RmLintType rm_file_string_to_lint_type(const char *type) {
    const int N = sizeof(LINT_TYPES) / sizeof(const char *);
    for(int i = 0; i < N; ++i) {
        if(g_strcmp0(type, LINT_TYPES[i]) == 0) {
            return (RmLintType)i;
        }
    }

    return RM_LINT_TYPE_UNKNOWN;
}

gint rm_file_basenames_cmp(const RmFile *file_a, const RmFile *file_b) {
    return g_ascii_strcasecmp(file_a->folder->basename, file_b->folder->basename);
}

void rm_file_hardlink_add(RmFile *head, RmFile *link) {
    if(!head->hardlinks) {
        head->hardlinks = g_queue_new();
    }
    link->hardlinks = head->hardlinks;
    g_queue_push_tail(head->hardlinks, link);
}

static gint rm_file_foreach_hardlink(RmFile *f, RmRFunc func, gpointer user_data) {
    if(!f->hardlinks) {
        return func(f, user_data);
    }

    gint result = 0;
    for(GList *iter = f->hardlinks->head; iter; iter = iter->next) {
        result += func(iter->data, user_data);
    }
    return result;
}

void rm_file_cluster_add(RmFile *host, RmFile *guest) {
    g_assert(host);
    g_assert(guest);
    g_assert(!guest->cluster || host == guest);
    if(!host->cluster) {
        host->cluster = g_queue_new();
        if(guest != host) {
            rm_file_cluster_add(host, host);
        }
    }
    guest->cluster = host->cluster;
    g_queue_push_tail(host->cluster, guest);
}

void rm_file_cluster_remove(RmFile *file) {
    g_assert(file);
    g_assert(file->cluster);

    g_queue_remove(file->cluster, file);
    if(file->cluster->length == 0) {
        g_queue_free(file->cluster);
    }
    file->cluster = NULL;
}

gint rm_file_foreach(RmFile *f, RmRFunc func, gpointer user_data) {
    if(!f->cluster) {
        return rm_file_foreach_hardlink(f, func, user_data);
    }

    gint result = 0;
    for(GList *iter = f->cluster->head; iter; iter = iter->next) {
        result += rm_file_foreach_hardlink(iter->data, func, user_data);
    }
    return result;
}

enum RmFileCountType {
    RM_FILE_COUNT_FILES,
    RM_FILE_COUNT_PREFD,
    RM_FILE_COUNT_NPREFD,
    RM_FILE_COUNT_NEW,
};

static gint rm_file_count(RmFile *file, gint type) {
    switch(type) {
    case RM_FILE_COUNT_FILES:
        return 1;
    case RM_FILE_COUNT_PREFD:
        return file->is_prefd;
    case RM_FILE_COUNT_NPREFD:
        return !file->is_prefd;
    case RM_FILE_COUNT_NEW:
        return file->is_new;
    default:
        g_assert_not_reached();
        return 0;
    }
}

gint rm_file_n_files(RmFile *file) {
    return rm_file_foreach(file, (RmRFunc)rm_file_count,
                           GINT_TO_POINTER(RM_FILE_COUNT_FILES));
}

gint rm_file_n_new(RmFile *file) {
    return rm_file_foreach(file, (RmRFunc)rm_file_count,
                           GINT_TO_POINTER(RM_FILE_COUNT_NEW));
}

gint rm_file_n_prefd(RmFile *file) {
    return rm_file_foreach(file, (RmRFunc)rm_file_count,
                           GINT_TO_POINTER(RM_FILE_COUNT_PREFD));
}

gint rm_file_n_nprefd(RmFile *file) {
    return rm_file_foreach(file, (RmRFunc)rm_file_count,
                           GINT_TO_POINTER(RM_FILE_COUNT_NPREFD));
}
