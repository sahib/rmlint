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
#include "md-scheduler.h"
#include "preprocess.h"
#include "replay.h"
#include "session.h"
#include "shredder.h"
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

    if(sscanf(buf.release, "%d.%d.*", &session->cfg->kernel_version[0],
              &session->cfg->kernel_version[1]) == EOF) {
        session->cfg->kernel_version[0] = -1;
        session->cfg->kernel_version[1] = -1;
        return;
    }

    rm_log_debug_line("Linux kernel version is %d.%d.",
                      session->cfg->kernel_version[0],
                      session->cfg->kernel_version[1]);
}
#else
void rm_session_read_kernel_version(RmSession *session) {
    (void)session;
}
#endif

bool rm_session_check_kernel_version(RmCfg *cfg, int major, int minor) {
    int found_major = cfg->kernel_version[0];
    int found_minor = cfg->kernel_version[1];

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
    session->cfg->formats = rm_fmt_open(session);
    session->cfg->pattern_cache = g_ptr_array_new_full(0, (GDestroyNotify)g_regex_unref);

    session->cfg->verbosity_count = 2;
    session->cfg->paranoia_count = 0;
    session->cfg->output_cnt[0] = -1;
    session->cfg->output_cnt[1] = -1;

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
    rm_fmt_close(cfg->formats);
    g_ptr_array_free(cfg->pattern_cache, TRUE);

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

static int rm_session_replay_main(RmSession *session) {
    /* User chose to replay some json files. */
    RmParrotCage cage;
    rm_parrot_cage_open(&cage, session);

    bool one_valid_json = false;
    RmCfg *cfg = session->cfg;

    for(GSList *iter = cfg->json_paths; iter; iter = iter->next) {
        RmPath *jsonpath = iter->data;

        if(!rm_parrot_cage_load(&cage, jsonpath->path, jsonpath->is_prefd)) {
            rm_log_warning_line("Loading %s failed.", jsonpath->path);
        } else {
            one_valid_json = true;
        }
    }

    if(!one_valid_json) {
        rm_log_error_line(_("No valid .json files given, aborting."));
        return EXIT_FAILURE;
    }

    rm_parrot_cage_close(&cage);
    rm_fmt_flush(session->cfg->formats);
    rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_PRE_SHUTDOWN);
    rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_SUMMARY);

    return EXIT_SUCCESS;
}

static int rm_session_btrfs_clone_main(RmCfg *cfg) {
    g_assert(cfg->btrfs_source);
    g_assert(cfg->btrfs_dest);

#if HAVE_BTRFS_H
    struct {
        struct btrfs_ioctl_same_args args;
        struct btrfs_ioctl_same_extent_info info;
    } extent_same;
    memset(&extent_same, 0, sizeof(extent_same));

    int source_fd = rm_sys_open(cfg->btrfs_source, O_RDONLY);
    if(source_fd < 0) {
        rm_log_error_line(_("btrfs clone: failed to open source file"));
        return EXIT_FAILURE;
    }

    extent_same.info.fd = rm_sys_open(cfg->btrfs_dest, cfg->btrfs_readonly ? O_RDONLY : O_RDWR);
    if(extent_same.info.fd < 0) {
        rm_log_error_line(_("btrfs clone: error %i: failed to open dest file.%s"),
                          errno,
                          cfg->btrfs_readonly ? "" : _("\n\t(if target is a read-only snapshot "
                                                "then -r option is required)"));
        rm_sys_close(source_fd);
        return EXIT_FAILURE;
    }

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

        /* BTRFS_IOC_FILE_EXTENT_SAME has an internal limit at 16MB */
        extent_same.args.length = MIN(16 * 1024 * 1024, bytes_remaining);
        if(extent_same.args.length == 0) {
            extent_same.args.length = bytes_remaining;
        }

        ret = ioctl(source_fd, BTRFS_IOC_FILE_EXTENT_SAME, &extent_same);
        if(ret == 0 && extent_same.info.status == 0) {
            bytes_deduped += extent_same.info.bytes_deduped;
            bytes_remaining -= extent_same.info.bytes_deduped;
        }
    }

    rm_sys_close(source_fd);
    rm_sys_close(extent_same.info.fd);

    if(ret >= 0) {
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
                          extent_same.info.status, cfg->btrfs_dest);
    } else if(bytes_remaining > 0) {
        rm_log_info_line(_("Files don't match - not cloned"));
    }

#else
    (void)source;
    (void)dest;
    (void)read_only;
    rm_log_error_line(_("rmlint was not compiled with btrfs support."))
#endif

    return EXIT_FAILURE;
}


int rm_session_main(RmSession *session) {
    int exit_state = EXIT_SUCCESS;
    RmCfg *cfg = session->cfg;

    rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_INIT);

    if(cfg->replay) {
        return rm_session_replay_main(session);
    }

    if(cfg->btrfs_clone) {
        return rm_session_btrfs_clone_main(cfg);
    }

    rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_TRAVERSE);

    if(cfg->list_mounts) {
        session->mounts = rm_mounts_table_new(cfg->fake_fiemap);
    }

    if(session->mounts == NULL) {
        rm_log_debug_line("No mount table created.");
    }

    session->mds = rm_mds_new(cfg->threads, session->mounts, cfg->fake_pathindex_as_disk);

    rm_traverse_tree(session);

    rm_log_debug_line("List build finished at %.3f with %d files",
                      g_timer_elapsed(session->timer, NULL), session->total_files);

    if(cfg->merge_directories) {
        rm_assert_gentle(cfg->cache_file_structs);

        /* Currently we cannot use -D and the cloning on btrfs, since this assumes the
         * same layout
         * on two dupicate directories which is likely not a valid assumption.
         * Emit a warning if the raw -D is used in conjunction with that.
         * */
        const char *handler_key =
            rm_fmt_get_config_value(session->cfg->formats, "sh", "handler");
        const char *clone_key =
            rm_fmt_get_config_value(session->cfg->formats, "sh", "clone");
        if(cfg->honour_dir_layout == false &&
           ((handler_key != NULL && strstr(handler_key, "clone") != NULL) ||
            clone_key != NULL)) {
            rm_log_error_line(_(
                "Using -D together with -c sh:clone is currently not possible. Sorry."));
            rm_log_error_line(
                _("Either do not use -D, or attempt to run again with -Dj."));
            return EXIT_FAILURE;
        }

        session->dir_merger = rm_tm_new(session);
    }

    if(session->total_files < 2 && session->cfg->run_equal_mode) {
        rm_log_warning_line(
            _("Not enough files for --equal (need at least two to compare)"));
        return EXIT_FAILURE;
    }

    if(session->total_files >= 1) {
        rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_PREPROCESS);
        rm_preprocess(session);

        if(cfg->find_duplicates || cfg->merge_directories) {
            rm_shred_run(session);

            rm_log_debug_line("Dupe search finished at time %.3f",
                              g_timer_elapsed(session->timer, NULL));
        } else {
            /* Clear leftovers */
            rm_file_tables_clear(session);
        }
    }

    if(cfg->merge_directories) {
        rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_MERGE);
        rm_tm_finish(session->dir_merger);
    }

    rm_fmt_flush(session->cfg->formats);
    rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_PRE_SHUTDOWN);
    rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_SUMMARY);

    if(session->shred_bytes_remaining != 0) {
        rm_log_error_line("BUG: Number of remaining bytes is %" LLU
                          " (not 0). Please report this.",
                          session->shred_bytes_remaining);
        exit_state = EXIT_FAILURE;
    }

    if(session->shred_files_remaining != 0) {
        rm_log_error_line("BUG: Number of remaining files is %" LLU
                          " (not 0). Please report this.",
                          session->shred_files_remaining);
        exit_state = EXIT_FAILURE;
    }

    if(exit_state == EXIT_SUCCESS && cfg->run_equal_mode) {
        return session->equal_exit_code;
    }

    return exit_state;
}
