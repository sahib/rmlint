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
 *  - Christopher <sahib> Pahl 2010-2015 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2015 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <errno.h>

#include <glib.h>

#include "preprocess.h"
#include "formats.h"
#include "utilities.h"
#include "file.h"
#include "xattr.h"

///////////////////////////////////////////
// BUFFER FOR STARTING TRAVERSAL THREADS //
///////////////////////////////////////////

/* Defines a path variable containing the buffer's path */
#define RM_BUFFER_DEFINE_PATH(session, buff)                                      \
    char *buff##_path = NULL;                                                     \
    char buff##_buf[PATH_MAX];                                                    \
    if(session->cfg->use_meta_cache) {                                            \
        rm_swap_table_lookup(session->meta_cache, session->meta_cache_dir_id,     \
                             GPOINTER_TO_UINT(buff->path), buff##_buf, PATH_MAX); \
        buff##_path = buff##_buf;                                                 \
    } else {                                                                      \
        buff##_path = buff->path;                                                 \
    }

typedef struct RmTravBuffer {
    RmStat stat_buf;  /* rm_sys_stat(2) information about the directory */
    char *path;       /* The path of the directory, as passed on command line. */
    bool is_prefd;    /* Was this file in a preferred path? */
    RmOff path_index; /* Index of path, as passed on the commadline */
} RmTravBuffer;

static RmTravBuffer *rm_trav_buffer_new(RmSession *session, char *path, bool is_prefd,
                                        unsigned long path_index) {
    RmTravBuffer *self = g_new0(RmTravBuffer, 1);
    self->path = path;
    self->is_prefd = is_prefd;
    self->path_index = path_index;

    RM_BUFFER_DEFINE_PATH(session, self);

    int stat_state;
    if(session->cfg->follow_symlinks) {
        stat_state = rm_sys_stat(self_path, &self->stat_buf);
    } else {
        stat_state = rm_sys_lstat(self_path, &self->stat_buf);
    }

    if(stat_state == -1) {
        rm_log_perror("Unable to stat file");
    }
    return self;
}

static void rm_trav_buffer_free(RmTravBuffer *self) {
    g_free(self);
}

//////////////////////
// TRAVERSE SESSION //
//////////////////////

typedef struct RmTravSession {
    RmUserList *userlist;
    RmSession *session;
} RmTravSession;

static RmTravSession *rm_traverse_session_new(RmSession *session) {
    RmTravSession *self = g_new0(RmTravSession, 1);
    self->session = session;
    self->userlist = rm_userlist_new();
    return self;
}

static void rm_traverse_session_free(RmTravSession *trav_session) {
    rm_log_info("Found %d files, ignored %d hidden files and %d hidden folders\n",
                trav_session->session->total_files,
                trav_session->session->ignored_files,
                trav_session->session->ignored_folders);

    rm_userlist_destroy(trav_session->userlist);

    g_free(trav_session);
}

//////////////////////
// ACTUAL WORK HERE //
//////////////////////

static void rm_traverse_file(RmTravSession *trav_session, RmStat *statp,
                             GQueue *file_queue, char *path, size_t path_len,
                             bool is_prefd, unsigned long path_index,
                             RmLintType file_type, bool is_symlink, bool is_hidden) {
    RmSession *session = trav_session->session;
    RmCfg *cfg = session->cfg;

    /* Try to autodetect the type of the lint */
    if(file_type == RM_LINT_TYPE_UNKNOWN) {
        RmLintType gid_check;
        /* see if we can find a lint type */
        if(statp->st_size == 0) {
            if(!cfg->find_emptyfiles) {
                return;
            } else {
                file_type = RM_LINT_TYPE_EMPTY_FILE;
            }
        } else if(cfg->permissions && access(path, cfg->permissions) == -1) {
            /* bad permissions; ignore file */
            trav_session->session->ignored_files++;
            return;
        } else if(cfg->find_badids &&
                  (gid_check = rm_util_uid_gid_check(statp, trav_session->userlist))) {
            file_type = gid_check;
        } else if(cfg->find_nonstripped && rm_util_is_nonstripped(path, statp)) {
            file_type = RM_LINT_TYPE_NONSTRIPPED;
        } else {
            RmOff file_size = statp->st_size;
            if(!cfg->limits_specified ||
               ((cfg->minsize == (RmOff)-1 || cfg->minsize <= file_size) &&
                (cfg->maxsize == (RmOff)-1 || file_size <= cfg->maxsize))) {
                if(rm_mounts_is_evil(trav_session->session->mounts, statp->st_dev) ==
                   false) {
                    file_type = RM_LINT_TYPE_DUPE_CANDIDATE;
                } else {
                    /* A file in a evil fs. Ignore. */
                    trav_session->session->ignored_files++;
                    return;
                }
            } else {
                return;
            }
        }
    }

    RmFile *file =
        rm_file_new(session, path, path_len, statp, file_type, is_prefd, path_index);

    if(file != NULL) {
        file->is_symlink = is_symlink;
        file->is_hidden = is_hidden;

        int added = 0;
        if(file_queue != NULL) {
            g_queue_push_tail(file_queue, file);
            added = 1;
        } else {
            added = rm_file_tables_insert(session, file);
        }

        g_atomic_int_add(&trav_session->session->total_files, added);
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE);

        if(trav_session->session->cfg->clear_xattr_fields &&
           file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE) {
            rm_xattr_clear_hash(session, file);
        }
    }
}

static bool rm_traverse_is_hidden(RmCfg *cfg, const char *basename, char *hierarchy,
                                  size_t hierarchy_len) {
    if(cfg->partial_hidden == false) {
        return false;
    } else if(*basename == '.') {
        return true;
    } else {
        return !!memchr(hierarchy, 1, hierarchy_len);
    }
}

/* Macro for rm_traverse_directory() for easy file adding */
#define _ADD_FILE(lint_type, is_symlink, stat_buf)                                  \
    rm_traverse_file(                                                               \
        trav_session, (RmStat *)stat_buf, &file_queue, p->fts_path, p->fts_pathlen, \
        is_prefd, path_index, lint_type, is_symlink,                                \
        rm_traverse_is_hidden(cfg, p->fts_name, is_hidden, p->fts_level + 1));

#if RM_PLATFORM_32 && HAVE_STAT64

static void rm_traverse_convert_small_stat_buf(struct stat *fts_statp, RmStat *buf) {
    /* Break a leg for supporting large files on 32 bit,
     * and convert the needed fields to the large version.
     *
     * We can't use memcpy here, since the layout might be (fatally) different.
     * Yes, this is stupid. *Sigh*
     * */
    memset(buf, 0, sizeof(RmStat));
    buf->st_dev = fts_statp->st_dev;
    buf->st_ino = fts_statp->st_ino;
    buf->st_mode = fts_statp->st_mode;
    buf->st_nlink = fts_statp->st_nlink;
    buf->st_uid = fts_statp->st_uid;
    buf->st_gid = fts_statp->st_gid;
    buf->st_rdev = fts_statp->st_rdev;
    buf->st_size = fts_statp->st_size;
    buf->st_blksize = fts_statp->st_blksize;
    buf->st_blocks = fts_statp->st_blocks;
    buf->st_atim = fts_statp->st_atim;
    buf->st_mtim = fts_statp->st_mtim;
    buf->st_ctim = fts_statp->st_ctim;
}

#define ADD_FILE(lint_type, is_symlink)                         \
    {                                                           \
        RmStat buf;                                             \
        rm_traverse_convert_small_stat_buf(p->fts_statp, &buf); \
        _ADD_FILE(lint_type, is_symlink, &buf)                  \
    }

#else

#define ADD_FILE(lint_type, is_symlink) \
    _ADD_FILE(lint_type, is_symlink, (RmStat *)p->fts_statp)

#endif

static void rm_traverse_directory(RmTravBuffer *buffer, RmTravSession *trav_session) {
    RmSession *session = trav_session->session;
    RmCfg *cfg = session->cfg;

    char is_prefd = buffer->is_prefd;
    RmOff path_index = buffer->path_index;

    /* Initialize ftsp */
    int fts_flags = FTS_PHYSICAL | FTS_COMFOLLOW | FTS_NOCHDIR;

    RM_BUFFER_DEFINE_PATH(trav_session->session, buffer);

    FTS *ftsp = fts_open((char * [2]){buffer_path, NULL}, fts_flags, NULL);

    if(ftsp == NULL) {
        rm_log_error_line("fts_open() == NULL");
        return;
    }

    FTSENT *p, *chp;
    chp = fts_children(ftsp, 0);
    if(chp == NULL) {
        rm_log_warning_line("fts_children() == NULL");
        return;
    }

    /* start main processing */
    char is_emptydir[PATH_MAX / 2 + 1];
    char is_hidden[PATH_MAX / 2 + 1];
    bool have_open_emptydirs = false;
    bool clear_emptydir_flags = false;

    memset(is_emptydir, 0, sizeof(is_emptydir) - 1);
    memset(is_hidden, 0, sizeof(is_hidden) - 1);

    /* rm_traverse_file add the finished file (if any) to this queue.  They are
     * added to the preprocessing module in batch so there isn't many jumping
     * between BEGIN; INSERT[...]; COMMIT and SELECT with --with-metadata-cache.
     */
    GQueue file_queue = G_QUEUE_INIT;

    while(!rm_session_was_aborted(trav_session->session) &&
          (p = fts_read(ftsp)) != NULL) {
        /* check for hidden file or folder */
        if(cfg->ignore_hidden && p->fts_level > 0 && p->fts_name[0] == '.') {
            /* ignoring hidden folders*/

            if(p->fts_info == FTS_D) {
                fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                g_atomic_int_inc(&trav_session->session->ignored_folders);
            } else {
                g_atomic_int_inc(&trav_session->session->ignored_files);
            }

            clear_emptydir_flags = true; /* flag current dir as not empty */
            is_emptydir[p->fts_level] = 0;
        } else {
            switch(p->fts_info) {
            case FTS_D: /* preorder directory */
                if(cfg->depth != 0 && p->fts_level >= cfg->depth) {
                    /* continuing into folder would exceed maxdepth*/
                    fts_set(ftsp, p, FTS_SKIP);  /* do not recurse */
                    clear_emptydir_flags = true; /* flag current dir as not empty */
                    rm_log_debug("Not descending into %s because max depth reached\n",
                                 p->fts_path);
                } else if(cfg->crossdev && p->fts_dev != chp->fts_dev) {
                    /* continuing into folder would cross file systems*/
                    fts_set(ftsp, p, FTS_SKIP);  /* do not recurse */
                    clear_emptydir_flags = true; /*flag current dir as not empty*/
                    rm_log_info(
                        "Not descending into %s because it is a different filesystem\n",
                        p->fts_path);
                } else {
                    /* recurse dir; assume empty until proven otherwise */
                    is_emptydir[p->fts_level + 1] = 1;
                    is_hidden[p->fts_level + 1] =
                        is_hidden[p->fts_level] | (p->fts_name[0] == '.');
                    have_open_emptydirs = true;
                }
                break;
            case FTS_DC: /* directory that causes cycles */
                rm_log_warning_line(_("filesystem loop detected at %s (skipping)"),
                                    p->fts_path);
                clear_emptydir_flags = true; /* current dir not empty */
                break;
            case FTS_DNR: /* unreadable directory */
                rm_log_warning_line(_("cannot read directory %s: %s"), p->fts_path,
                                    g_strerror(p->fts_errno));
                clear_emptydir_flags = true; /* current dir not empty */
                break;
            case FTS_DOT: /* dot or dot-dot */
                break;
            case FTS_DP: /* postorder directory */
                if(is_emptydir[p->fts_level + 1] && cfg->find_emptydirs) {
                    ADD_FILE(RM_LINT_TYPE_EMPTY_DIR, false);
                }
                is_hidden[p->fts_level + 1] = 0;
                break;
            case FTS_ERR: /* error; errno is set */
                rm_log_warning_line(_("error %d in fts_read for %s (skipping)"), errno,
                                    p->fts_path);
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_INIT: /* initialized only */
                break;
            case FTS_SLNONE: /* symbolic link without target */
                if(cfg->find_badlinks) {
                    ADD_FILE(RM_LINT_TYPE_BADLINK, false);
                }
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_W:                      /* whiteout object */
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_NS: {                   /* rm_sys_stat(2) failed */
                clear_emptydir_flags = true; /*current dir not empty*/
                RmStat stat_buf;

                /* See if your stat can do better. */
                if(rm_sys_stat(p->fts_path, &stat_buf) != -1) {
                    /* normal stat failed but 64-bit stat worked
                     * -> must be a big file on 32 bit.
                     */
                    rm_traverse_file(trav_session, &stat_buf, &file_queue, p->fts_path,
                                     p->fts_pathlen, is_prefd, path_index,
                                     RM_LINT_TYPE_UNKNOWN, false,
                                     rm_traverse_is_hidden(cfg, p->fts_name, is_hidden,
                                                           p->fts_level + 1));
                    rm_log_warning_line(_("Added big file %s"), p->fts_path);
                } else {
                    rm_log_warning(_("cannot stat file %s (skipping)"), p->fts_path);
                }
            } break;
            case FTS_SL:                     /* symbolic link */
                clear_emptydir_flags = true; /* current dir not empty */
                if(!cfg->follow_symlinks) {
                    if(p->fts_level != 0) {
                        rm_log_debug("Not following symlink %s because of cfg\n",
                                     p->fts_path);
                    }

                    RmStat dummy_buf;
                    if(rm_sys_stat(p->fts_path, &dummy_buf) == -1 && errno == ENOENT) {
                        /* Oops, that's a badlink. */
                        if(cfg->find_badlinks) {
                            ADD_FILE(RM_LINT_TYPE_BADLINK, false);
                        }
                    } else if(cfg->see_symlinks) {
                        ADD_FILE(RM_LINT_TYPE_UNKNOWN, true);
                    }
                } else {
                    rm_log_debug("Following symlink %s\n", p->fts_path);
                    fts_set(ftsp, p, FTS_FOLLOW); /* do not recurse */
                }
                break;
            case FTS_NSOK:    /* no rm_sys_stat(2) requested */
            case FTS_F:       /* regular file */
            case FTS_DEFAULT: /* any file type not explicitly described by one of the
                                 above*/
                clear_emptydir_flags = true; /* current dir not empty*/
                ADD_FILE(RM_LINT_TYPE_UNKNOWN, false);
                break;
            default:
                /* unknown case; assume current dir not empty but otherwise do nothing */
                clear_emptydir_flags = true;
                rm_log_error_line(_("Unknown fts_info flag %d for file %s"), p->fts_info,
                                  p->fts_path);
                break;
            }

            if(clear_emptydir_flags) {
                /* non-empty dir found above; need to clear emptydir flags for all open
                 * levels */
                if(have_open_emptydirs) {
                    memset(is_emptydir, 0, sizeof(is_emptydir) - 1);
                    have_open_emptydirs = false;
                }
                clear_emptydir_flags = false;
            }
            /* current dir may not be empty; by association, all open dirs are non-empty
             */
        }
    }

    if(errno != 0 && !rm_session_was_aborted(session)) {
        rm_log_error_line(_("'%s': fts_read failed on %s"), g_strerror(errno),
                          ftsp->fts_path);
    }

#undef ADD_FILE

    fts_close(ftsp);
    rm_trav_buffer_free(buffer);

    /* Pass the files to the preprocessing machinery. We collect the files first
     * in order to make -with-metadata-cache work: Without, too many
     * insert/selects would crossfire.
     */
    for(GList *iter = file_queue.head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        g_atomic_int_add(&trav_session->session->total_files,
                         -(rm_file_tables_insert(session, file) == 0));
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE);
    }
    g_queue_clear(&file_queue);
}

static void rm_traverse_directories(GQueue *path_queue, RmTravSession *trav_session) {
    g_queue_foreach(path_queue, (GFunc)rm_traverse_directory, trav_session);
}

////////////////
// PUBLIC API //
////////////////

void rm_traverse_tree(RmSession *session) {
    RmCfg *cfg = session->cfg;
    RmTravSession *trav_session = rm_traverse_session_new(session);

    GHashTable *paths_per_disk =
        g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)g_queue_free);

    for(RmOff idx = 0; cfg->paths[idx] != NULL; ++idx) {
        char *path = cfg->paths[idx];
        bool is_prefd = cfg->is_prefd[idx];

        RmTravBuffer *buffer = rm_trav_buffer_new(session, path, is_prefd, idx);

        RM_BUFFER_DEFINE_PATH(session, buffer);

        if(S_ISREG(buffer->stat_buf.st_mode)) {
            /* Append normal paths directly */
            bool is_hidden = false;

            /* The is_hidden information is only needed for --partial-hidden */
            if(cfg->partial_hidden) {
                is_hidden = rm_util_path_is_hidden(buffer_path);
            }

            rm_traverse_file(trav_session, &buffer->stat_buf, NULL, buffer_path,
                             strlen(buffer_path), is_prefd, idx, RM_LINT_TYPE_UNKNOWN,
                             false, is_hidden);

            rm_trav_buffer_free(buffer);
        } else if(S_ISDIR(buffer->stat_buf.st_mode)) {
            /* It's a directory, traverse it. */
            dev_t disk = (!cfg->fake_pathindex_as_disk ? rm_mounts_get_disk_id_by_path(
                                                             session->mounts, buffer_path)
                                                       : (dev_t)idx);

            GQueue *path_queue = rm_hash_table_setdefault(
                paths_per_disk, GUINT_TO_POINTER(disk), (RmNewFunc)g_queue_new);
            g_queue_push_tail(path_queue, buffer);
        } else {
            /* Probably a block device, fifo or something weird. */
            rm_trav_buffer_free(buffer);
        }
    }

    GThreadPool *traverse_pool = rm_util_thread_pool_new(
        (GFunc)rm_traverse_directories, trav_session, session->cfg->threads);

    GHashTableIter iter;
    GQueue *path_queue = NULL;

    g_hash_table_iter_init(&iter, paths_per_disk);
    while(g_hash_table_iter_next(&iter, NULL, (gpointer *)&path_queue)) {
        rm_util_thread_pool_push(traverse_pool, path_queue);
    }

    g_thread_pool_free(traverse_pool, false, true);
    g_hash_table_unref(paths_per_disk);
    rm_traverse_session_free(trav_session);

    session->traverse_finished = TRUE;
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE);
}
