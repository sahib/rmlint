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

#include <stdlib.h>
#include <string.h>

#include <fts.h>
#include <errno.h>
#include <sys/stat.h>

#include <glib.h>

#include "preprocess.h"
#include "utilities.h"
#include "file.h"

///////////////////////////////////////////
// BUFFER FOR STARTING TRAVERSAL THREADS //
///////////////////////////////////////////

typedef struct RmTravBuffer {
    struct stat stat_buf;  /* stat(2) information about the directory */
    const char *path;      /* The path of the directory, as passed on command line. */
    bool is_prefd;         /* Was this file in a preferred path? */
    guint64 path_index;    /* Index of path, as passed on the commadline */
    bool is_first_path;    /* True if the first path and FTS_NOCHDIR might be disabled */
} RmTravBuffer;

static RmTravBuffer *rm_trav_buffer_new(RmSession *session, char *path, bool is_prefd, unsigned long path_index) {
    RmTravBuffer *self = g_new0(RmTravBuffer, 1);
    self->path = path;
    self->is_prefd = is_prefd;
    self->path_index = path_index;
    self->is_first_path = false;

    int stat_state;
    if(session->settings->followlinks) {
        stat_state = stat(path, &self->stat_buf);
    } else {
        stat_state = lstat(path, &self->stat_buf);
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
    RmUserGroupNode **userlist;
    RmSession *session;
    GMutex lock;
} RmTravSession;

static RmTravSession *rm_traverse_session_new(RmSession *session) {
    RmTravSession *self = g_new0(RmTravSession, 1);
    self->session = session;
    self->userlist = rm_userlist_new();
    g_mutex_init(&self->lock);
    return self;
}

static void rm_traverse_session_free(RmTravSession *trav_session) {
    rm_log_info("Found %" G_GUINT64_FORMAT " files, ignored %" G_GUINT64_FORMAT
                " hidden files and %" G_GUINT64_FORMAT " hidden folders\n",
                trav_session->session->total_files,
                trav_session->session->ignored_files,
                trav_session->session->ignored_folders
               );

    rm_userlist_destroy(trav_session->userlist);
    g_mutex_clear(&trav_session->lock);
    g_free(trav_session);
}

//////////////////////
// ACTUAL WORK HERE //
//////////////////////

static void rm_traverse_file(
    RmTravSession *trav_session, struct stat *statp,
    char *path, bool is_prefd, unsigned long path_index, RmLintType file_type
) {
    RmSession *session = trav_session->session;
    RmSettings *settings = session->settings;

    /* Try to autodetect the type of the lint */
    if (file_type == RM_LINT_TYPE_UNKNOWN) {
        RmLintType gid_check;
        /*see if we can find a lint type*/
        if (settings->findbadids && (gid_check = rm_util_uid_gid_check(statp, trav_session->userlist))) {
            file_type = gid_check;
        } else if(settings->nonstripped && rm_util_is_nonstripped(path)) {
            file_type = RM_LINT_TYPE_NBIN;
        } else if(statp->st_size == 0) {
            if (!settings->listemptyfiles) {
                return;
            } else {
                file_type = RM_LINT_TYPE_EFILE;
            }
        } else {
            guint64 file_size = statp->st_size;
            if(!settings->limits_specified || (settings->minsize <= file_size && file_size <= settings->maxsize)) {
                file_type = RM_LINT_TYPE_DUPE_CANDIDATE;
            }
        }
    }

    RmFile *file = rm_file_new(path, statp, file_type, is_prefd, path_index);
    g_mutex_lock(&trav_session->lock);
    {
        trav_session->session->total_files += rm_file_tables_insert(session, file);
    }
    g_mutex_unlock(&trav_session->lock);
}

static void rm_traverse_directory(RmTravBuffer *buffer, RmTravSession *trav_session) {
    RmSession *session = trav_session->session;
    RmSettings *settings = session->settings;

    char *path = (char *)buffer->path;
    char is_prefd = buffer->is_prefd;
    guint64 path_index = buffer->path_index;

    /* Initialize ftsp */
    int fts_flags =  FTS_PHYSICAL | FTS_COMFOLLOW | (buffer->is_first_path * FTS_NOCHDIR);
    FTS *ftsp = fts_open((char *[2]) {
        path, NULL
    }, fts_flags, NULL);

    if (ftsp == NULL) {
        rm_log_error("fts_open failed");
        return;
    }

    FTSENT *p, *chp;
    chp = fts_children(ftsp, 0);
    if (chp == NULL) {
        rm_log_warning("fts_children: can't initialise");
        return;
    }

    /* start main processing */
    char is_emptydir[PATH_MAX / 2 + 1];
    bool have_open_emptydirs = false;
    bool clear_emptydir_flags = false;
    memset(&is_emptydir[0], 'N', sizeof(is_emptydir) - 1);
    is_emptydir[sizeof(is_emptydir) - 1] = '\0';

    while(!rm_session_was_aborted(trav_session->session) && (p = fts_read(ftsp)) != NULL) {
        /* check for hidden file or folder */
        if (settings->ignore_hidden && p->fts_level > 0 && p->fts_name[0] == '.') {
            /* ignoring hidden folders*/
            g_mutex_lock(&trav_session->lock);
            {
                if (p->fts_info == FTS_D) {
                    fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                    trav_session->session->ignored_folders++;
                } else {
                    trav_session->session->ignored_files++;
                }
            }
            g_mutex_unlock(&trav_session->lock);
            clear_emptydir_flags = true; /* flag current dir as not empty */
        } else {
            switch (p->fts_info) {
            case FTS_D:         /* preorder directory */
                if (settings->depth != 0 && p->fts_level >= settings->depth) {
                    /* continuing into folder would exceed maxdepth*/
                    fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                    clear_emptydir_flags = true; /* flag current dir as not empty */
                    rm_log_debug("Not descending into %s because max depth reached\n", p->fts_path);
                } else if (settings->samepart && p->fts_dev != chp->fts_dev) {
                    /* continuing into folder would cross file systems*/
                    fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                    clear_emptydir_flags = true; /*flag current dir as not empty*/
                    rm_log_info("Not descending into %s because it is a different filesystem\n", p->fts_path);
                } else {
                    /* recurse dir; assume empty until proven otherwise */
                    is_emptydir[p->fts_level + 1] = 'E';
                    have_open_emptydirs = true;
                }
                break;
            case FTS_DC:        /* directory that causes cycles */
                rm_log_warning(RED"Warning: filesystem loop detected at %s (skipping)\n"RESET, p->fts_path);
                clear_emptydir_flags = true; /* current dir not empty */
                break;
            case FTS_DNR:       /* unreadable directory */
                rm_log_warning(YELLOW"Warning: cannot read directory"RESET" %s: %s\n", p->fts_path, g_strerror(p->fts_errno));
                clear_emptydir_flags = true; /* current dir not empty */
                break;
            case FTS_DOT:       /* dot or dot-dot */
                break;
            case FTS_DP:        /* postorder directory */
                if (is_emptydir[p->fts_level + 1] == 'E' && settings->findemptydirs) {
                    rm_traverse_file(trav_session, p->fts_statp, p->fts_path, is_prefd, path_index, RM_LINT_TYPE_EDIR);
                }
                break;
            case FTS_ERR:       /* error; errno is set */
                rm_log_warning(RED"Warning: error %d in fts_read for %s (skipping)\n"RESET, errno, p->fts_path);
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_INIT:      /* initialized only */
                break;
            case FTS_SLNONE:    /* symbolic link without target */
                if (settings->findbadlinks) {
                    rm_traverse_file(trav_session, p->fts_statp, p->fts_path, is_prefd, path_index, RM_LINT_TYPE_BLNK);
                }
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_W:         /* whiteout object */
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_NS:        /* stat(2) failed */
                clear_emptydir_flags = true; /*current dir not empty*/
                rm_log_warning(RED"Warning: cannot stat file %s (skipping)\n"RESET, p->fts_path);
                break;
            case FTS_SL:        /* symbolic link */
                clear_emptydir_flags = true; /* current dir not empty */
                if (!settings->followlinks) {
                    if (p->fts_level != 0) {
                        rm_log_debug("Not following symlink %s because of settings\n", p->fts_path);
                    }
                } else {
                    rm_log_debug("Following symlink %s\n", p->fts_path);
                    fts_set(ftsp, p, FTS_FOLLOW); /* do not recurse */
                }
                break;
            case FTS_NSOK:      /* no stat(2) requested */
            case FTS_F:         /* regular file */
            case FTS_DEFAULT:   /* any file type not explicitly described by one of the above*/
                clear_emptydir_flags = true; /* current dir not empty*/
                rm_traverse_file(trav_session, p->fts_statp, p->fts_path, is_prefd, path_index, RM_LINT_TYPE_UNKNOWN);
                break;
            default:
                /* unknown case; assume current dir not empty but otherwise do nothing */
                clear_emptydir_flags = true;
                rm_log_error(RED"Unknown fts_info flag %d for file %s\n"RESET, p->fts_info, p->fts_path);
                break;
            }

            if(clear_emptydir_flags) {
                /* non-empty dir found above; need to clear emptydir flags for all open levels */
                if (have_open_emptydirs) {
                    memset(&is_emptydir[0], 'N', sizeof(is_emptydir) - 1);
                    have_open_emptydirs = false;
                }
                clear_emptydir_flags = false;
            }
            /* current dir may not be empty; by association, all open dirs are non-empty */
        }
    }

    if (errno != 0) {
        rm_log_error("Error '%s': fts_read failed on %s\n", g_strerror(errno), ftsp->fts_path);
    }

    fts_close(ftsp);
    rm_trav_buffer_free(buffer);
}

static void rm_traverse_directories(GQueue *path_queue, RmTravSession *trav_session) {
    g_queue_foreach(path_queue, (GFunc)rm_traverse_directory, trav_session);
}

////////////////
// PUBLIC API //
////////////////

void rm_traverse_tree(RmSession *session) {
    RmSettings *settings = session->settings;
    RmTravSession *trav_session = rm_traverse_session_new(session);

    GHashTable *paths_per_disk = g_hash_table_new_full(
                                     NULL, NULL, NULL, (GDestroyNotify)g_queue_free
                                 );

    for(guint64 idx = 0; settings->paths[idx] != NULL; ++idx) {
        char *path = settings->paths[idx];
        bool is_prefd = settings->is_prefd[idx];

        RmTravBuffer *buffer = rm_trav_buffer_new(
                                   session, path, is_prefd, idx
                               );

        /* Append normal paths directly */
        if(S_ISREG(buffer->stat_buf.st_mode)) {
            rm_traverse_file(trav_session, &buffer->stat_buf, path, is_prefd, idx, RM_LINT_TYPE_UNKNOWN);
            rm_trav_buffer_free(buffer);
        } else if(S_ISDIR(buffer->stat_buf.st_mode)) {
            dev_t disk = rm_mounts_get_disk_id_by_path(session->mounts, path);
            buffer->is_first_path = (g_hash_table_size(paths_per_disk) == 0);

            GQueue *path_queue = g_hash_table_lookup(paths_per_disk, GUINT_TO_POINTER(disk));
            if(path_queue == NULL) {
                path_queue = g_queue_new();
                g_hash_table_insert(paths_per_disk, GUINT_TO_POINTER(disk), path_queue);
            }

            g_queue_push_tail(path_queue, buffer);
        } else {
            rm_trav_buffer_free(buffer);
        }
    }

    GThreadPool *traverse_pool = rm_util_thread_pool_new(
                                     (GFunc) rm_traverse_directories,
                                     trav_session,
                                     CLAMP(settings->threads, 1, g_hash_table_size(paths_per_disk))
                                 );

    GHashTableIter iter;
    GQueue *path_queue = NULL;

    g_hash_table_iter_init(&iter, paths_per_disk);
    while(g_hash_table_iter_next(&iter, NULL, (gpointer *)&path_queue)) {
        rm_util_thread_pool_push(traverse_pool, path_queue);
    }

    g_thread_pool_free(traverse_pool, false, true);
    g_hash_table_unref(paths_per_disk);
    rm_traverse_session_free(trav_session);
}
