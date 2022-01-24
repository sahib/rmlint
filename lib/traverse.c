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
#include "traverse.h"

#include <errno.h>
#include <string.h>

#include "formats.h"
#include "fts/fts.h"
#include "md-scheduler.h"
#include "preprocess.h"
#include "xattr.h"

//////////////////////
// TRAVERSE SESSION //
//////////////////////

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
static char *rm_traverse_symlink_path(const char *path) {
    char target[PATH_MAX + 1];
    int len = readlink(path, target, PATH_MAX);
    if(len == -1) {
        rm_log_debug_line("failed to follow symbolic link of %s: %s", path,
                          g_strerror(errno));
        return NULL;
    }
    target[len] = '\0';

    // Most links will already be absolute.
    if(g_path_is_absolute(target)) {
        return realpath(target, NULL);
    }

    char *link_dir_path = g_path_get_dirname(path);
    char *full_path = g_build_path(G_DIR_SEPARATOR_S, link_dir_path, target, NULL);
    char *clean_path = realpath(full_path, NULL);

    g_free(link_dir_path);
    g_free(full_path);
    return clean_path;
}

bool rm_traverse_file(RmSession *session, RmStat *statp, const char *path, bool is_prefd,
                      unsigned long path_index, RmLintType file_type, bool is_symlink,
                      bool is_hidden, bool is_on_subvol_fs, short depth,
                      bool tagged_original, const char *ext_cksum) {
    RmCfg *cfg = session->cfg;

    if(rm_fmt_is_a_output(session->formats, path)) {
        /* ignore files which are rmlint outputs */
        return false;
    }

    /* Try to autodetect the type of the lint */
    if(file_type == RM_LINT_TYPE_UNKNOWN) {
        RmLintType gid_check;
        /* see if we can find a lint type */
        if(statp->st_size == 0 && cfg->find_emptyfiles) {
            file_type = RM_LINT_TYPE_EMPTY_FILE;
        } else if(cfg->permissions && access(path, cfg->permissions) == -1) {
            /* bad permissions; ignore file */
            session->ignored_files++;
            return false;
        } else if(cfg->find_badids &&
                  (gid_check = rm_util_uid_gid_check(statp, session->userlist))) {
            file_type = gid_check;
        } else if(cfg->find_nonstripped && rm_util_is_nonstripped(path, statp)) {
            file_type = RM_LINT_TYPE_NONSTRIPPED;
        } else {
            RmOff file_size = statp->st_size;
            if(!cfg->limits_specified ||
               ((cfg->minsize <= file_size) && (file_size <= cfg->maxsize))) {
                if(rm_mounts_is_evil(session->mounts, statp->st_dev) == false) {
                    file_type = RM_LINT_TYPE_DUPE_CANDIDATE;
                } else {
                    /* A file in an evil fs. Ignore. */
                    session->ignored_files++;
                    return FALSE;
                }
            } else {
                return FALSE;
            }
        }
    }

    char *resolved_path = NULL;
    if(is_symlink && cfg->follow_symlinks) {
        resolved_path = rm_traverse_symlink_path(path);
        if(resolved_path == NULL) {
            return FALSE;
        }
        path = resolved_path;
        is_symlink = false;
    }

    RmFile *file =
        rm_file_new(session, path, statp, file_type, is_prefd, path_index, depth, NULL);

    g_free(resolved_path);

    if(file != NULL) {
        file->is_symlink = is_symlink;
        file->is_hidden = is_hidden;
        file->is_on_subvol_fs = is_on_subvol_fs;
        file->link_count = statp->st_nlink;
        file->cached_original = tagged_original;
        file->ext_cksum = ext_cksum;

        rm_file_list_insert_file(file, session);

        if(file->lint_type == RM_LINT_TYPE_DUPE_DIR_CANDIDATE) {
            g_atomic_int_add(&session->traversed_folders, 1);
        }
        else {
            g_atomic_int_add(&session->total_files, 1);
        }
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE);

        if(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE) {
            if(cfg->clear_xattr_fields) {
                rm_xattr_clear_hash(file, session);
            }
            if(cfg->read_cksum_from_xattr) {
                rm_xattr_read_hash(file, session);
            }
        }
        return TRUE;
    }
    return FALSE;
}

static bool rm_traverse_is_hidden(RmCfg *cfg, const char *basename, char *hierarchy,
                                  size_t hierarchy_len) {
    if(cfg->partial_hidden == false) {
        return false;
    } else if(*basename == '.') {
        return true;
    } else {
        // true if any paths above us are hidden
        return !!memchr(hierarchy, 1, hierarchy_len);
    }
}

/* Macro for rm_traverse_directory() for easy file adding */
#define _ADD_FILE(lint_type, is_symlink, stat_buf)                                 \
    rm_traverse_file(                                                              \
        session, (RmStat *)stat_buf, p->fts_path, is_prefd, path_index, lint_type, \
        is_symlink,                                                                \
        rm_traverse_is_hidden(cfg, p->fts_name, is_hidden, p->fts_level + 1),      \
        rmpath->treat_as_single_vol, p->fts_level, FALSE, NULL);

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

G_STATIC_ASSERT(G_SIZEOF_MEMBER(RmStat, st_size) == G_SIZEOF_MEMBER(__fts_stat_t, st_size));

#define ADD_FILE(lint_type, is_symlink) \
    _ADD_FILE(lint_type, is_symlink, (RmStat *)p->fts_statp)

#endif

bool rm_traverse_is_emptydir(const char *path, RmCfg *cfg, int current_depth) {
    /* Initialize ftsp */
    int fts_flags = FTS_PHYSICAL | FTS_COMFOLLOW | FTS_NOCHDIR;
    FTS *ftsp = fts_open((const char *const[2]){path, NULL}, fts_flags, NULL);
    if(ftsp == NULL) {
        rm_log_error_line("fts_open() == NULL");
        return FALSE;
    }

    FTSENT *p, *chp;
    chp = fts_children(ftsp, 0);
    if(chp == NULL) {
        rm_log_warning_line("fts_children() == NULL");
        return FALSE;
    }

    bool is_emptydir = TRUE;
    while(!rm_session_was_aborted() && (p = fts_read(ftsp)) != NULL && is_emptydir) {
        switch(p->fts_info) {
        case FTS_D: /* preorder directory */
            if(cfg->depth != 0 && p->fts_level + current_depth >= cfg->depth) {
                /* continuing into folder would exceed maxdepth*/
                is_emptydir = FALSE;
                rm_log_debug_line("Not descending into %s because max depth reached",
                                  p->fts_path);
            } else if(!(cfg->crossdev) && p->fts_dev != chp->fts_dev) {
                /* continuing into folder would cross file systems*/
                is_emptydir = FALSE;
                rm_log_info(
                    "Not descending into %s because it is a different filesystem\n",
                    p->fts_path);
            }
            break;
        case FTS_DC: /* directory that causes cycles */
            is_emptydir = FALSE;
            rm_log_warning_line(_("filesystem loop detected at %s (skipping)"),
                                p->fts_path);
            break;
        case FTS_DNR: /* unreadable directory */
            is_emptydir = FALSE;
            rm_log_warning_line(_("cannot read directory %s: %s"), p->fts_path,
                                g_strerror(p->fts_errno));
            break;
        case FTS_DOT:  /* dot or dot-dot */
        case FTS_DP:   /* postorder directory */
        case FTS_INIT: /* initialized only */
            break;
        case FTS_ERR: /* error; errno is set */
            is_emptydir = FALSE;
            rm_log_warning_line(_("error %d in fts_read for %s (skipping)"), errno,
                                p->fts_path);
            break;
            break;
        case FTS_SLNONE:  /* symbolic link without target */
        case FTS_W:       /* whiteout object */
        case FTS_NS:      /* rm_sys_stat(2) failed */
        case FTS_SL:      /* symbolic link */
        case FTS_NSOK:    /* no rm_sys_stat(2) requested */
        case FTS_F:       /* regular file */
        case FTS_DEFAULT: /* any file type not explicitly described by one of the
                             above*/
            is_emptydir = FALSE;
            break;
        default:
            /* unknown case; assume current dir not empty but otherwise do nothing */
            is_emptydir = FALSE;
            rm_log_error_line(_("Unknown fts_info flag %d for file %s"), p->fts_info,
                              p->fts_path);
            break;
        }
    }
    fts_close(ftsp);
    return is_emptydir;
}

#define FLAG_NOT_EMPTY memset(is_emptydir, 0, p->fts_level + 1);

#define FLAG_NOT_TRAVERSED                    \
    memset(is_emptydir, 0, p->fts_level + 1); \
    memset(is_traversed, 0, p->fts_level + 1);

static void rm_traverse_directory(RmTravBuffer *buffer, RmSession *session) {
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

    /* is_hidden[d] indicates whether path element at depth d in the current
     * search branch is hidden (starts with '.')
     */
    char is_hidden[PATH_MAX / 2 + 1];

    /* is_emptydir[d] indicates whether path element at depth d in the current
     * search branch is empty (contains only empty dirs and zero-size files)
     */
    char is_emptydir[PATH_MAX / 2 + 1];

    /* is_traversed[d] indicates whether path element at depth d in the current
     * search branch has been fully traversed (no files or dirs skipped)
     */
    char is_traversed[PATH_MAX / 2 + 1];

    bool next_is_symlink = false;

    memset(is_emptydir, 0, sizeof(is_emptydir) - 1);
    memset(is_hidden, 0, sizeof(is_hidden) - 1);
    memset(is_traversed, 0, sizeof(is_traversed) - 1);

    /* start main processing */
    while(!rm_session_was_aborted() && (p = fts_read(ftsp)) != NULL) {
        /* check for hidden file or folder */
        if(cfg->ignore_hidden && p->fts_level > 0 && p->fts_name[0] == '.') {
            /* ignoring hidden folders*/

            if(p->fts_info == FTS_D) {
                fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                g_atomic_int_inc(&session->ignored_folders);
            } else {
                g_atomic_int_inc(&session->ignored_files);
            }
            FLAG_NOT_TRAVERSED;
        } else {
            switch(p->fts_info) {
            case FTS_D: /* preorder directory */
                if(cfg->depth != 0 && p->fts_level >= cfg->depth) {
                    /* continuing into folder would exceed maxdepth*/
                    fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                    FLAG_NOT_TRAVERSED;
                    rm_log_debug_line("Not descending into %s because max depth reached",
                                      p->fts_path);
                } else if(!(cfg->crossdev) && p->fts_dev != chp->fts_dev) {
                    /* continuing into folder would cross file systems*/
                    fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                    FLAG_NOT_TRAVERSED;
                    rm_log_info(
                        "Not descending into %s because it is a different filesystem\n",
                        p->fts_path);
                } else {
                    /* recurse dir; assume empty until proven otherwise */
                    is_emptydir[p->fts_level + 1] = 1;
                    is_hidden[p->fts_level + 1] =
                        is_hidden[p->fts_level] | (p->fts_name[0] == '.');

                    is_traversed[p->fts_level + 1] = 1;
                }
                break;
            case FTS_DC: /* directory that causes cycles */
                rm_log_warning_line(_("filesystem loop detected at %s (skipping)"),
                                    p->fts_path);
                FLAG_NOT_TRAVERSED;  // TODO: review whether circular dirs can still be
                                     // empty and/or traversed
                break;
            case FTS_DNR: /* unreadable directory */
                rm_log_warning_line(_("cannot read directory %s: %s"), p->fts_path,
                                    g_strerror(p->fts_errno));
                FLAG_NOT_TRAVERSED;
                break;
            case FTS_DOT: /* dot or dot-dot */
                break;
            case FTS_DP: /* postorder directory */
                if(is_emptydir[p->fts_level + 1] && cfg->find_emptydirs) {
                    ADD_FILE(RM_LINT_TYPE_EMPTY_DIR, false);
                } else if(is_traversed[p->fts_level + 1]) {
                    ADD_FILE(RM_LINT_TYPE_DUPE_DIR_CANDIDATE, false);
                }
                is_hidden[p->fts_level + 1] = 0;
                break;
            case FTS_ERR: /* error; errno is set */
                rm_log_warning_line(_("error %d in fts_read for %s (skipping)"), errno,
                                    p->fts_path);
                FLAG_NOT_TRAVERSED;
                break;
            case FTS_INIT: /* initialized only */
                break;
            case FTS_SLNONE: /* symbolic link without target */
                if(cfg->find_badlinks) {
                    ADD_FILE(RM_LINT_TYPE_BADLINK, false);
                    FLAG_NOT_EMPTY;
                } else {
                    FLAG_NOT_TRAVERSED;
                }
                break;
            case FTS_W: /* whiteout object */
                FLAG_NOT_TRAVERSED;
                break;
            case FTS_NS: { /* rm_sys_stat(2) failed */
                RmStat stat_buf;

                /* See if your stat can do better. */
                if(rm_sys_stat(p->fts_path, &stat_buf) != -1) {
                    /* normal stat failed but 64-bit stat worked
                     * -> must be a big file on 32 bit.
                     */
                    rm_traverse_file(session, &stat_buf, p->fts_path, is_prefd,
                                     path_index, RM_LINT_TYPE_UNKNOWN, false,
                                     rm_traverse_is_hidden(cfg, p->fts_name, is_hidden,
                                                           p->fts_level + 1),
                                     rmpath->treat_as_single_vol, p->fts_level, FALSE,
                                     NULL);
                    rm_log_info_line(_("Added big file %s"), p->fts_path);
                    FLAG_NOT_EMPTY;
                } else {
                    rm_log_warning_line(_("cannot stat file %s (skipping)"), p->fts_path);
                    FLAG_NOT_TRAVERSED;
                }
            } break;
            case FTS_SL: /* symbolic link */
                if(!cfg->follow_symlinks) {
                    FLAG_NOT_EMPTY;
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
                    FLAG_NOT_TRAVERSED;
                    next_is_symlink = true;
                    fts_set(ftsp, p, FTS_FOLLOW); /* do recurse */
                }
                break;
            case FTS_NSOK:    /* no rm_sys_stat(2) requested */
            case FTS_F:       /* regular file */
            case FTS_DEFAULT: /* any file type not explicitly described by one of the
                                 above*/
                FLAG_NOT_EMPTY;
                ADD_FILE(RM_LINT_TYPE_UNKNOWN, next_is_symlink);
                next_is_symlink = false;
                break;
            default:
                /* unknown case; assume current dir not empty but otherwise do nothing */
                FLAG_NOT_TRAVERSED;
                rm_log_error_line(_("Unknown fts_info flag %d for file %s"), p->fts_info,
                                  p->fts_path);
                break;
            }
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

    RmMDS *mds = session->mds;
    rm_mds_configure(mds, (RmMDSFunc)rm_traverse_directory, session, 0,
                     cfg->threads_per_disk, NULL);

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

        if(S_ISDIR(buffer->stat_buf.st_mode)) {
            /* It's a directory, traverse it. */
            buffer->disk = rm_mds_device_get(mds, rmpath->path,
                                             (cfg->fake_pathindex_as_disk)
                                                 ? rmpath->idx + 1
                                                 : buffer->stat_buf.st_dev);
            rm_mds_device_ref(buffer->disk, 1);
            rm_mds_push_task(buffer->disk, buffer->stat_buf.st_dev, 0, rmpath->path,
                             buffer);
        }
        else {
            if(S_ISLNK(buffer->stat_buf.st_mode) && !rmpath->realpath_worked) {
                /* A symlink where we could not get the actual path from
                 * (and it was given directly, e.g. by a find call)
                 */
                rm_traverse_file(session, &buffer->stat_buf, rmpath->path, rmpath->is_prefd,
                                 rmpath->idx, RM_LINT_TYPE_BADLINK, false, is_hidden, FALSE,
                                 0, FALSE, NULL);
            } else if(S_ISREG(buffer->stat_buf.st_mode)) {
                rm_traverse_file(session, &buffer->stat_buf, rmpath->path, rmpath->is_prefd,
                                 rmpath->idx, RM_LINT_TYPE_UNKNOWN, false, is_hidden, FALSE,
                                 0, FALSE, NULL);
            } else {
                /* Probably a block device, fifo or something weird. */
                rm_log_warning_line(_("Unable to process path %s"), rmpath->path);
            }
            rm_trav_buffer_free(buffer);
        }
    }

    rm_mds_start(mds);
    rm_mds_finish(mds);

    rm_log_debug_line("Found %d files, ignored %d hidden files and %d hidden folders",
                      session->total_files,
                      session->ignored_files,
                      session->ignored_folders);

    session->traverse_finished = TRUE;
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE);
}
