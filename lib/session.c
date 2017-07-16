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
 *  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
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

#if HAVE_BTRFS_H
#include <linux/btrfs.h>
#include <sys/ioctl.h>
#endif

#if HAVE_UNAME
#include "sys/utsname.h"

void rm_session_read_kernel_version(RmSession *session) {
    struct utsname buf;
    if(uname(&buf) == -1) {
        return;
    }

    if(sscanf(buf.release, "%d.%d.*", &session->kernel_version[0],
              &session->kernel_version[1]) == EOF) {
        session->kernel_version[0] = -1;
        session->kernel_version[1] = -1;
        return;
    }

    rm_log_debug_line("Linux kernel version is %d.%d.",
                      session->kernel_version[0],
                      session->kernel_version[1]);
}
#else
void rm_session_read_kernel_version(RmSession *session) {
    (void)session;
}
#endif

bool rm_session_check_kernel_version(RmSession *session, int major, int minor) {
    int found_major = session->kernel_version[0];
    int found_minor = session->kernel_version[1];

    /* Could not read kernel version: Assume failure on our side. */
    if(found_major <= 0 && found_minor <= 0) {
        return true;
    }

    /* Lower is bad. */
    if(found_major < major || found_minor < minor) {
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

    rm_session_read_kernel_version(session);

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

volatile int SESSION_ABORTED;

void rm_session_abort(void) {
    g_atomic_int_add(&SESSION_ABORTED, 1);
}

static gpointer rm_session_print_first_abort_warn(_UNUSED gpointer data) {
    rm_log_warning("\r");
    rm_log_warning_line(_("Received Interrupt, stopping..."));
    return NULL;
}

bool rm_session_was_aborted() {
    gint rc = g_atomic_int_get(&SESSION_ABORTED);

    static GOnce print_once = G_ONCE_INIT;

    switch(rc) {
    case 1:
        g_once(&print_once, rm_session_print_first_abort_warn, NULL);
        break;
    case 2:
        rm_log_warning_line(_("Received second Interrupt, stopping hard."));
        exit(EXIT_FAILURE);
        break;
    }

    return rc;
}
/**
 * *********** btrfs clone session main ************
 **/
int rm_session_btrfs_clone_main(RmSession *session) {
    RmCfg *cfg = session->cfg;
    if(cfg->path_count != 2) {
        rm_log_error(_("Usage: rmlint --btrfs-clone [-r] [-v|V] source dest\n"));
        return EXIT_FAILURE;
    }

    if(!rm_session_check_kernel_version(session, 4, 2)) {
        rm_log_warning_line("This needs at least linux >= 4.2.");
        return EXIT_FAILURE;
    }

    /* TODO: if kernel version >= 4.5 then use IOCTL-FIDEDUPERANGE
     * http://man7.org/linux/man-pages/man2/ioctl_fideduperange.2.html
     */
#if HAVE_BTRFS_H

    g_assert(cfg->paths);
    RmPath *dest = cfg->paths->data;
    g_assert(cfg->paths->next);
    RmPath *source = cfg->paths->next->data;
    rm_log_debug_line("Cloning %s -> %s", source->path, dest->path);

    struct {
        struct btrfs_ioctl_same_args args;
        struct btrfs_ioctl_same_extent_info info;
    } extent_same;
    memset(&extent_same, 0, sizeof(extent_same));

    int source_fd = rm_sys_open(source->path, O_RDONLY);
    if(source_fd < 0) {
        rm_log_error_line(_("btrfs clone: failed to open source file"));
        return EXIT_FAILURE;
    }

    extent_same.info.fd =
        rm_sys_open(dest->path, cfg->btrfs_readonly ? O_RDONLY : O_RDWR);
    if(extent_same.info.fd < 0) {
        rm_log_error_line(
            _("btrfs clone: error %i: failed to open dest file.%s"),
            errno,
            cfg->btrfs_readonly ? "" : _("\n\t(if target is a read-only snapshot "
                                         "then -r option is required)"));
        rm_sys_close(source_fd);
        return EXIT_FAILURE;
    }

    /* fsync's needed to flush extent mapping */
    fsync(source_fd);
    fsync(extent_same.info.fd);

    struct stat source_stat;
    fstat(source_fd, &source_stat);

    guint64 bytes_deduped = 0;
    gint64 bytes_remaining = source_stat.st_size;
    int ret = 0;
    while(bytes_deduped < (guint64)source_stat.st_size && ret == 0 &&
          extent_same.info.status == 0 && bytes_remaining) {
        extent_same.args.dest_count = 1;
        extent_same.args.logical_offset = bytes_deduped;
        extent_same.info.logical_offset = bytes_deduped;

        /* try to dedupe the rest of the file */
        extent_same.args.length = bytes_remaining;

        ret = ioctl(source_fd, BTRFS_IOC_FILE_EXTENT_SAME, &extent_same);
        bytes_deduped += extent_same.info.bytes_deduped;
        bytes_remaining -= extent_same.info.bytes_deduped;
        rm_log_debug_line("deduped %lu bytes...", bytes_deduped);
    }

    rm_sys_close(source_fd);
    rm_sys_close(extent_same.info.fd);

    if(ret >= 0 && bytes_remaining == 0) {
        return EXIT_SUCCESS;
    }

    if(ret < 0) {
        ret = errno;
        rm_log_error_line(_("BTRFS_IOC_FILE_EXTENT_SAME returned error: (%d) %s"), ret,
                          strerror(ret));
    } else if(extent_same.info.status == -22 && cfg->btrfs_readonly && getuid()) {
        rm_log_error_line(_("Need to run as root user to clone to a read-only snapshot"));
    } else if(extent_same.info.status < 0) {
        rm_log_error_line(_("BTRFS_IOC_FILE_EXTENT_SAME returned status %d for file %s"),
                          extent_same.info.status, dest->path);
    } else if(bytes_deduped == 0) {
        rm_log_info_line(_("Files don't match - not cloned"));
    } else if(bytes_remaining > 0) {
        rm_log_info_line(_("Only first %lu bytes cloned - files not fully identical"),
                         bytes_deduped);
    }

#else
    (void)cfg;
    rm_log_error_line(_("rmlint was not compiled with btrfs support."))
#endif

    return EXIT_FAILURE;
}

