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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>

#include "file.h"
#include "formats.h"
#include "md-scheduler.h"
#include "preprocess.h"
#include "utilities.h"
#include "xattr.h"

#include "fts/fts.h"

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
    rm_log_debug_line("Found %d files, ignored %d hidden files and %d hidden folders",
                      trav_session->session->total_files,
                      trav_session->session->ignored_files,
                      trav_session->session->ignored_folders);

    rm_userlist_destroy(trav_session->userlist);

    g_free(trav_session);
}

///////////////////////////////////////////
// BUFFER FOR STARTING TRAVERSAL THREADS //
///////////////////////////////////////////

typedef struct RmTravBuffer {
    RmStat stat_buf;   /* rm_sys_stat(2) information about the directory */
    RmPath *rmpath;    /* Path and info passed via command line. */
    RmMDSDevice *disk; /* md-scheduler device the buffer was pushed to */
} RmTravBuffer;

static RmTravBuffer *rm_trav_buffer_new(RmSession *session, RmPath *rmpath) {
    RmTravBuffer *self = g_new0(RmTravBuffer, 1);
    self->rmpath = rmpath;

    int stat_state;
    if(session->cfg->follow_symlinks) {
        stat_state = rm_sys_stat(rmpath->path, &self->stat_buf);
    } else {
        stat_state = rm_sys_lstat(rmpath->path, &self->stat_buf);
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
// ACTUAL WORK HERE //
//////////////////////

// Symbolic links may contain relative paths, that are relative to the symbolic
// link location.  When using readlink(), those relative paths are returned but
// may not make sense depending from where rmlint was run. This function takes
// the original link_path and the (potentially relatiev) path it is pointing to
// and constructs an absolute path out of it.
//
// See also: https://github.com/sahib/rmlint/issues/333
static char *rm_traverse_rel_realpath(char *link_path, char *pointing_to) {
    if(pointing_to == NULL) {
        return NULL;
    }

    // Most links will already be absolute.
    if(g_path_is_absolute(pointing_to))  {
        return realpath(pointing_to, NULL);
    }

    char *link_dir_path = g_path_get_dirname(link_path);
    char *full_path = g_build_path(G_DIR_SEPARATOR_S, link_dir_path, pointing_to, NULL);
    char *clean_path = realpath(full_path, NULL);

    g_free(link_dir_path);
    g_free(full_path);
    return clean_path;
}

static void rm_traverse_file(RmTravSession *trav_session, RmStat *statp, char *path,
                             bool is_prefd, unsigned long path_index,
                             RmLintType file_type, bool is_symlink, bool is_hidden,
                             bool is_on_subvol_fs, short depth) {
    RmSession *session = trav_session->session;
    RmCfg *cfg = session->cfg;

    if(rm_fmt_is_a_output(session->formats, path)) {
        /* ignore files which are rmlint outputs */
        return;
    }

    /* Try to autodetect the type of the lint */
    if(file_type == RM_LINT_TYPE_UNKNOWN) {
        RmLintType gid_check;
        /* see if we can find a lint type */
        if(statp->st_size == 0 && cfg->find_emptyfiles) {
            file_type = RM_LINT_TYPE_EMPTY_FILE;
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
               ((cfg->minsize <= file_size) && (file_size <= cfg->maxsize))) {
                if(rm_mounts_is_evil(trav_session->session->mounts, statp->st_dev) ==
                   false) {
                    file_type = RM_LINT_TYPE_DUPE_CANDIDATE;
                } else {
                    /* A file in an evil fs. Ignore. */
                    trav_session->session->ignored_files++;
                    return;
                }
            } else {
                return;
            }
        }
    }

    bool path_needs_free = false;
    if(is_symlink && cfg->follow_symlinks) {
        char *new_path_buf = g_malloc0(PATH_MAX + 1);
        if(readlink(path, new_path_buf, PATH_MAX) == -1) {
            rm_log_debug_line("failed to follow symbolic link of %s: %s", path, g_strerror(errno));
            g_free(new_path_buf);
            return;
        }

        char *resolved_path = rm_traverse_rel_realpath(path, new_path_buf);
        g_free(new_path_buf);

        path = resolved_path;
        is_symlink = false;
        path_needs_free = true;
    }

    RmFile *file =
        rm_file_new(session, path, statp, file_type, is_prefd, path_index, depth);

    if(path_needs_free) {
        g_free(path);
    }

    if(file != NULL) {
        file->is_symlink = is_symlink;
        file->is_hidden = is_hidden;
        file->is_on_subvol_fs = is_on_subvol_fs;
        file->link_count = statp->st_nlink;

        rm_file_list_insert_file(file, session);

        g_atomic_int_add(&trav_session->session->total_files, 1);
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE);

        if(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE) {
            if(cfg->clear_xattr_fields) {
                rm_xattr_clear_hash(file, session);
            }
            if(cfg->read_cksum_from_xattr) {
                rm_xattr_read_hash(file, session);
            }
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
#define _ADD_FILE(lint_type, is_symlink, stat_buf)                                      \
    rm_traverse_file(                                                                   \
        trav_session, (RmStat *)stat_buf, p->fts_path, is_prefd, path_index, lint_type, \
        is_symlink,                                                                     \
        rm_traverse_is_hidden(cfg, p->fts_name, is_hidden, p->fts_level + 1),           \
        rmpath->treat_as_single_vol, p->fts_level);

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
    RmPath *rmpath = buffer->rmpath;

    char is_prefd = rmpath->is_prefd;
    RmOff path_index = rmpath->idx;

    /* Initialize ftsp */
    int fts_flags = FTS_PHYSICAL | FTS_COMFOLLOW | FTS_NOCHDIR;

    if(rmpath->treat_as_single_vol) {
        rm_log_debug_line("Treating files under %s as a single volume", rmpath->path);
    }

    FTS *ftsp = fts_open((const char *const[2]){rmpath->path, NULL}, fts_flags, NULL);

    if(ftsp == NULL) {
        rm_log_error_line("fts_open() == NULL");
        goto done;
    }

    FTSENT *p, *chp;
    chp = fts_children(ftsp, 0);
    if(chp == NULL) {
        rm_log_warning_line("fts_children() == NULL");
        goto done;
    }

    /* start main processing */
    char is_emptydir[PATH_MAX / 2 + 1];
    char is_hidden[PATH_MAX / 2 + 1];
    bool have_open_emptydirs = false;
    bool clear_emptydir_flags = false;
    bool next_is_symlink = false;

    memset(is_emptydir, 0, sizeof(is_emptydir) - 1);
    memset(is_hidden, 0, sizeof(is_hidden) - 1);

    while(!rm_session_was_aborted() && (p = fts_read(ftsp)) != NULL) {
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
                    rm_log_debug_line("Not descending into %s because max depth reached",
                                      p->fts_path);
                } else if(!(cfg->crossdev) && p->fts_dev != chp->fts_dev) {
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
                    rm_traverse_file(trav_session, &stat_buf, p->fts_path, is_prefd,
                                     path_index, RM_LINT_TYPE_UNKNOWN, false,
                                     rm_traverse_is_hidden(cfg, p->fts_name, is_hidden,
                                                           p->fts_level + 1),
                                     rmpath->treat_as_single_vol, p->fts_level);
                    rm_log_warning_line(_("Added big file %s"), p->fts_path);
                } else {
                    rm_log_warning_line(_("cannot stat file %s (skipping)"), p->fts_path);
                }
            } break;
            case FTS_SL:                     /* symbolic link */
                clear_emptydir_flags = true; /* current dir not empty */
                if(!cfg->follow_symlinks) {
                    bool is_badlink = false;
                    if(access(p->fts_path, R_OK) == -1 && errno == ENOENT) {
                        is_badlink = true;
                    }

                    if(is_badlink && cfg->find_badlinks) {
                        ADD_FILE(RM_LINT_TYPE_BADLINK, false);
                    } else if(cfg->see_symlinks) {
                        /* NOTE: bad links are also counted as duplicates
                         *       when -T df,dd (for example) is used.
                         *       They can serve as input for the treemerge
                         *       algorithm which might fail when missing.
                         */
                        ADD_FILE(RM_LINT_TYPE_UNKNOWN, true);
                    }
                } else {
                    next_is_symlink = true;
                    fts_set(ftsp, p, FTS_FOLLOW); /* do recurse */
                }
                break;
            case FTS_NSOK:    /* no rm_sys_stat(2) requested */
            case FTS_F:       /* regular file */
            case FTS_DEFAULT: /* any file type not explicitly described by one of the
                                 above*/
                clear_emptydir_flags = true; /* current dir not empty*/
                ADD_FILE(RM_LINT_TYPE_UNKNOWN, next_is_symlink);
                next_is_symlink = false;
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

    if(errno != 0 && !rm_session_was_aborted()) {
        rm_log_error_line(_("'%s': fts_read failed on %s"), g_strerror(errno),
                          ftsp->fts_path);
    }

#undef ADD_FILE

    fts_close(ftsp);

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE);

done:
    rm_mds_device_ref(buffer->disk, -1);
    rm_trav_buffer_free(buffer);
}

////////////////
// PUBLIC API //
////////////////

void rm_traverse_tree(RmSession *session) {
    RmCfg *cfg = session->cfg;
    RmTravSession *trav_session = rm_traverse_session_new(session);

    RmMDS *mds = session->mds;
    rm_mds_configure(mds,
                     (RmMDSFunc)rm_traverse_directory,
                     trav_session,
                     0,
                     cfg->threads_per_disk,
                     NULL);

    /* iterate through paths */
    for(GSList *iter = cfg->paths; iter && !rm_session_was_aborted(); iter = iter->next) {
        RmPath *rmpath = iter->data;
        RmTravBuffer *buffer = rm_trav_buffer_new(session, rmpath);

        /* Append normal paths directly */
        bool is_hidden = false;

        /* The is_hidden information is only needed for --partial-hidden */
        if(cfg->partial_hidden) {
            is_hidden = rm_util_path_is_hidden(rmpath->path);
        }

        if(S_ISLNK(buffer->stat_buf.st_mode) && !rmpath->realpath_worked) {
            /* A symlink where we could not get the actual path from
             * (and it was given directly, e.g. by a find call)
             */
            rm_traverse_file(trav_session, &buffer->stat_buf, rmpath->path,
                             rmpath->is_prefd, rmpath->idx, RM_LINT_TYPE_BADLINK, false,
                             is_hidden, FALSE, 0);
        } else if(S_ISREG(buffer->stat_buf.st_mode)) {
            rm_traverse_file(trav_session, &buffer->stat_buf, rmpath->path,
                             rmpath->is_prefd, rmpath->idx, RM_LINT_TYPE_UNKNOWN, false,
                             is_hidden, FALSE, 0);

            rm_trav_buffer_free(buffer);
        } else if(S_ISDIR(buffer->stat_buf.st_mode)) {
            /* It's a directory, traverse it. */
            buffer->disk =
                rm_mds_device_get(mds, rmpath->path, (cfg->fake_pathindex_as_disk)
                                                         ? rmpath->idx + 1
                                                         : buffer->stat_buf.st_dev);
            rm_mds_device_ref(buffer->disk, 1);
            rm_mds_push_task(buffer->disk, buffer->stat_buf.st_dev, 0, rmpath->path,
                             buffer);

        } else {
            /* Probably a block device, fifo or something weird. */
            rm_trav_buffer_free(buffer);
        }
    }

    rm_mds_start(mds);
    rm_mds_finish(mds);

    rm_traverse_session_free(trav_session);

    session->traverse_finished = TRUE;
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE);
}
