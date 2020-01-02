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
** Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "formats.h"
#include "preprocess.h"
#include "session.h"
#include "traverse.h"
#include "xattr.h"

#if HAVE_BTRFS_H
#include <linux/btrfs.h>
#endif

#if HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif

#ifdef FIDEDUPERANGE
#define HAVE_FIDEDUPERANGE 1
#else
#define HAVE_FIDEDUPERANGE 0
#endif

#if HAVE_BTRFS_H || HAVE_FIDEDUPERANGE
#include <sys/ioctl.h>
#endif

#if HAVE_UNAME
#include "sys/utsname.h"
#endif

static gpointer rm_session_read_kernel_version(_UNUSED gpointer arg) {
    static int version[2] = {-1, -1};
#if HAVE_UNAME
    struct utsname buf;
    if(uname(&buf) != -1 &&
       sscanf(buf.release, "%d.%d.*", &version[0], &version[1]) != EOF) {
        rm_log_debug_line("Linux kernel version is %d.%d.", version[0], version[1]);
    } else {
        rm_log_warning_line("Unable to read Linux kernel version");
    }
#else
    rm_log_warning_line(
        "rmlint was not compiled with ability to read Linux kernel version");
#endif
    return version;
}

bool rm_session_check_kernel_version(int need_major, int need_minor) {
    static GOnce once = G_ONCE_INIT;
    g_once(&once, rm_session_read_kernel_version, NULL);
    int *version = once.retval;
    int major = version[0];
    int minor = version[1];

    if(major < 0 && minor < 0) {
        /* Could not read kernel version: Assume failure on our side. */
        return true;
    }

    /* Lower is bad. */
    if(major < need_major || (major == need_major && minor < need_minor)) {
        return false;
    }

    return true;
}

void rm_session_init(RmSession *session, RmCfg *cfg) {
    memset(session, 0, sizeof(RmSession));
    session->timer = g_timer_new();

    session->cfg = cfg;
    session->tables = rm_file_tables_new(session);
    session->formats = rm_fmt_open(session);
    session->pattern_cache = g_ptr_array_new_full(0, (GDestroyNotify)g_regex_unref);

    session->verbosity_count = 2;
    session->paranoia_count = 0;
    session->output_cnt[0] = -1;
    session->output_cnt[1] = -1;

    session->offsets_read = 0;
    session->offset_fragments = 0;
    session->offset_fails = 0;

    session->timer_since_proc_start = g_timer_new();
    g_timer_start(session->timer_since_proc_start);

    /* Assume that files are not equal */
    session->equal_exit_code = EXIT_FAILURE;
}

void rm_session_clear(RmSession *session) {
    RmCfg *cfg = session->cfg;

    rm_cfg_free_paths(cfg);

    g_timer_destroy(session->timer_since_proc_start);
    g_free(cfg->sort_criteria);

    g_timer_destroy(session->timer);
    rm_file_tables_destroy(session->tables);
    rm_fmt_close(session->formats);
    g_ptr_array_free(session->pattern_cache, TRUE);

    if(session->mounts) {
        rm_mounts_table_destroy(session->mounts);
    }

    if(session->dir_merger) {
        rm_tm_destroy(session->dir_merger);
    }

    g_free(cfg->joined_argv);
    g_free(cfg->full_argv0_path);
    g_free(cfg->iwd);

    rm_trie_destroy(&cfg->file_trie);
}

volatile int rm_session_abort_count = 0;

void rm_session_acknowledge_abort(const gint abort_count) {
    g_assert(abort_count);
    static bool message_not_yet_shown = true;
    static GMutex m;

    g_mutex_lock(&m);
        if(message_not_yet_shown) {
            rm_log_warning("\n");
            rm_log_warning_line(_("Received interrupt; stopping..."));
            message_not_yet_shown = false;
        }
        if(abort_count > 1) {
            rm_log_warning("\n");
            rm_log_warning_line(_("Received second interrupt; stopping hard."));
            exit(EXIT_FAILURE);
        }
    g_mutex_unlock(&m);
}

/* FIDEDUPERANGE supercedes the btrfs-only BTRFS_IOC_FILE_EXTENT_SAME as of Linux 4.5 and
 * should work for ocfs2 and xfs as well as btrfs.  We should still support the older
 * btrfs ioctl so that this still works on Linux 4.2 to 4.4.  The two ioctl's are
 * identical apart from field names so we can use #define's to accommodate both. */

/* TODO: test this on system running kernel 4.[2|3|4] ; if the c headers
 * support FIDEDUPERANGE but kernel doesn't, then this will fail at runtime
 * because the BTRFS_IOC_FILE_EXTENT_SAME is decided at compile time...
 */

#if HAVE_FIDEDUPERANGE
# define _DEDUPE_IOCTL_NAME        "FIDEDUPERANGE"
# define _DEDUPE_IOCTL             FIDEDUPERANGE
# define _DEST_FD                  dest_fd
# define _SRC_OFFSET               src_offset
# define _DEST_OFFSET              dest_offset
# define _SRC_LENGTH               src_length
# define _DATA_DIFFERS             FILE_DEDUPE_RANGE_DIFFERS
# define _FILE_DEDUPE_RANGE        file_dedupe_range
# define _FILE_DEDUPE_RANGE_INFO   file_dedupe_range_info
# define _MIN_LINUX_SUBVERSION     5
#else
# define _DEDUPE_IOCTL_NAME        "BTRFS_IOC_FILE_EXTENT_SAME"
# define _DEDUPE_IOCTL             BTRFS_IOC_FILE_EXTENT_SAME
# define _DEST_FD                  fd
# define _SRC_OFFSET               logical_offset
# define _DEST_OFFSET              logical_offset
# define _SRC_LENGTH               length
# define _DATA_DIFFERS             BTRFS_SAME_DATA_DIFFERS
# define _FILE_DEDUPE_RANGE        btrfs_ioctl_same_args
# define _FILE_DEDUPE_RANGE_INFO   btrfs_ioctl_same_extent_info
# define _MIN_LINUX_SUBVERSION     2
#endif

/**
 * *********** dedupe session main ************
 **/
int rm_session_dedupe_main(RmCfg *cfg) {
#if HAVE_FIDEDUPERANGE || HAVE_BTRFS_H
    g_assert(cfg->path_count == g_slist_length(cfg->paths));
    if(cfg->path_count != 2) {
        rm_log_error(_("Usage: rmlint --dedupe [-r] [-v|V] source dest\n"));
        return EXIT_FAILURE;
    }

    g_assert(cfg->paths);
    RmPath *dest = cfg->paths->data;
    g_assert(cfg->paths->next);
    RmPath *source = cfg->paths->next->data;
    rm_log_debug_line("Cloning %s -> %s", source->path, dest->path);

    if(cfg->dedupe_check_xattr) {
        // Check if we actually need to deduplicate.
        // This utility will write a value to the extended attributes
        // of the file so we know that we do not need to do it again
        // next time. This is supposed to avoid disk thrashing.
        // (See also: https://github.com/sahib/rmlint/issues/349)
        if(rm_xattr_is_deduplicated(dest->path, cfg->follow_symlinks)) {
            rm_log_debug_line("Already deduplicated according to xattr!");
            return EXIT_SUCCESS;
        }
    }

    // Also use --is-reflink on both files before doing extra work:
    if(rm_util_link_type(source->path, dest->path) == RM_LINK_REFLINK) {
        rm_log_debug_line("Already an exact reflink!");
        return EXIT_SUCCESS;
    }

    int source_fd = rm_sys_open(source->path, O_RDONLY);
    if(source_fd < 0) {
        rm_log_error_line(_("dedupe: failed to open source file"));
        return EXIT_FAILURE;
    }

    struct stat source_stat;
    fstat(source_fd, &source_stat);
    gint64 bytes_deduped = 0;

    /* a poorly-documented limit for dedupe ioctl's */
    static const gint64 max_dedupe_chunk = 16 * 1024 * 1024;

    /* how fine a resolution to use once difference detected;
     * use btrfs default node size (16k): */
    static const gint64 min_dedupe_chunk = 16 * 1024;

    rm_log_debug_line("Cloning using %s", _DEDUPE_IOCTL_NAME);

    if(!rm_session_check_kernel_version(4, _MIN_LINUX_SUBVERSION)) {
        rm_log_warning_line("This needs at least linux >= 4.%d.", _MIN_LINUX_SUBVERSION);
        return EXIT_FAILURE;
    }

    struct {
        struct _FILE_DEDUPE_RANGE args;
        struct _FILE_DEDUPE_RANGE_INFO info;
    } dedupe;
    memset(&dedupe, 0, sizeof(dedupe));

    dedupe.info._DEST_FD =
        rm_sys_open(dest->path, cfg->dedupe_readonly ? O_RDONLY : O_RDWR);

    if(dedupe.info._DEST_FD < 0) {
        rm_log_error_line(
            _("dedupe: error %i: failed to open dest file.%s"),
            errno,
            cfg->dedupe_readonly ? "" : _("\n\t(if target is a read-only snapshot "
                                          "then -r option is required)"));
        rm_sys_close(source_fd);
        return EXIT_FAILURE;
    }

    /* fsync's needed to flush extent mapping */
    if(fsync(source_fd) != 0) {
        rm_log_warning_line("Error syncing source file %s: %s", source->path,
                            strerror(errno));
    }

    if(fsync(dedupe.info._DEST_FD) != 0) {
        rm_log_warning_line("Error syncing dest file %s: %s", dest->path,
                            strerror(errno));
    }

    int ret = 0;
    gint64 dedupe_chunk = max_dedupe_chunk;
    while(bytes_deduped < source_stat.st_size && !rm_session_was_aborted()) {
        dedupe.args.dest_count = 1;
        /* TODO: multiple destinations at same time? */
        dedupe.args._SRC_OFFSET = bytes_deduped;
        dedupe.info._DEST_OFFSET = bytes_deduped;

        /* try to dedupe the rest of the file */
        dedupe.args._SRC_LENGTH = MIN(dedupe_chunk, source_stat.st_size - bytes_deduped);

        ret = ioctl(source_fd, _DEDUPE_IOCTL, &dedupe);

        if(ret != 0) {
            break;
        } else if(dedupe.info.status == _DATA_DIFFERS) {
            if(dedupe_chunk != min_dedupe_chunk) {
                dedupe_chunk = min_dedupe_chunk;
                rm_log_debug_line("Dropping to %"G_GINT64_FORMAT"-byte chunks "
                                  "after %"G_GINT64_FORMAT" bytes",
                                  dedupe_chunk, bytes_deduped);
                continue;
            } else {
                break;
            }
        } else if(dedupe.info.status != 0) {
            ret = -dedupe.info.status;
            errno = ret;
            break;
        } else if(dedupe.info.bytes_deduped == 0) {
            break;
        }

        bytes_deduped += dedupe.info.bytes_deduped;
    }
    rm_log_debug_line("Bytes deduped: %"G_GINT64_FORMAT, bytes_deduped);

    if (ret!=0) {
        rm_log_perrorf(_("%s returned error: (%d)"), _DEDUPE_IOCTL_NAME, ret);
    } else if(bytes_deduped == 0) {
        rm_log_info_line(_("Files don't match - not deduped"));
    } else if(bytes_deduped < source_stat.st_size) {
        rm_log_info_line(_("Only first %"G_GINT64_FORMAT" bytes deduped "
                           "- files not fully identical"),
                         bytes_deduped);
    }

    rm_sys_close(source_fd);
    rm_sys_close(dedupe.info._DEST_FD);

    if(bytes_deduped == source_stat.st_size) {
        if(cfg->dedupe_check_xattr && !cfg->dedupe_readonly) {
            rm_xattr_mark_deduplicated(dest->path, cfg->follow_symlinks);
        }

        return EXIT_SUCCESS;
    }

#else
    (void)cfg;
    rm_log_error_line(_("rmlint was not compiled with file cloning support."))
#endif

    return EXIT_FAILURE;
}

/**
 * *********** `rmlint --is-reflink` session main ************
 **/
int rm_session_is_reflink_main(RmCfg *cfg) {
    /* the linux OS doesn't provide any easy way to check if two files are
     * reflinks / clones (eg:
     * https://unix.stackexchange.com/questions/263309/how-to-verify-a-file-copy-is-reflink-cow
     *
     * `rmlint --is-clone file_a file_b` provides this functionality rmlint.
     * return values:
     * EXIT_SUCCESS if clone confirmed
     * EXIT_FAILURE if definitely not clones
     * Other return values defined in utilities.h 'RmOffsetsMatchCode' enum
     */

    g_assert(cfg->path_count == g_slist_length(cfg->paths));
    if(cfg->path_count != 2) {
        rm_log_error(_("Usage: rmlint --is-reflink [-v|V] file1 file2\n"));
        return EXIT_FAILURE;
    }

    g_assert(cfg->paths);

    RmPath *a = cfg->paths->data;
    g_assert(cfg->paths->next);

    RmPath *b = cfg->paths->next->data;
    rm_log_debug_line("Testing if %s is clone of %s", a->path, b->path);

    int result = rm_util_link_type(a->path, b->path);
    switch(result) {
    case RM_LINK_REFLINK:
        rm_log_debug_line("Offsets match");
        break;
    case RM_LINK_NONE:
        rm_log_debug_line("Offsets differ");
        break;
    case RM_LINK_MAYBE_REFLINK:
        rm_log_debug_line("Can't read file offsets (maybe inline extents?)");
        break;
    default:
        break;
    }

    return result;
}
