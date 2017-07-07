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

    rm_counter_session_init();

    memset(session, 0, sizeof(RmSession));

    session->cfg = cfg;
    rm_cfg_init(cfg);

    /* TODO: move this into rm_cfg_init(): */
    cfg->formats = rm_fmt_open(session);

    session->tables = rm_file_tables_new(session);

    rm_session_read_kernel_version(session);

    /* Assume that files are not equal */
    session->equal_exit_code = EXIT_FAILURE;
}

void rm_session_clear(RmSession *session) {
    rm_cfg_clear(session->cfg);

    rm_file_tables_destroy(session->tables);

    if(session->mounts) {
        rm_mounts_table_destroy(session->mounts);
    }

    if(session->dir_merger) {
        rm_tm_destroy(session->dir_merger);
    }

    rm_counter_session_free();
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

/*
* Debian and Ubuntu based distributions fuck up setuptools
* by expecting packages to be installed to dist-packages and not site-packages
* like expected by setuptools. This breaks a lot of packages with the reasoning
* to reduce conflicts between system and user packages:
*
*    https://stackoverflow.com/questions/9387928/whats-the-difference-between-dist-packages-and-site-packages
*
* We try to work around this by manually installing dist-packages to the
* sys.path by first calling a small bootstrap script.
*/
static const char RM_PY_BOOTSTRAP[] =
    ""
    "# This is a bootstrap script for the rmlint-gui.                              \n"
    "# See the src/rmlint.c in rmlint's source for more info.                      \n"
    "import sys, os, site                                                          \n"
    "                                                                              \n"
    "# Also default to dist-packages on debian(-based):                            \n"
    "sites = site.getsitepackages()                                                \n"
    "sys.path.extend([d.replace('dist-packages', 'site-packages') for d in sites]) \n"
    "sys.path.extend(sites)                                                        \n"
    "                                                                              \n"
    "# Cleanup self:                                                               \n"
    "try:                                                                          \n"
    "    os.remove(sys.argv[0])                                                    \n"
    "except:                                                                       \n"
    "    print('Note: Could not remove bootstrap script at ', sys.argv[0])         \n"
    "                                                                              \n"
    "# Run shredder by importing the main:                                         \n"
    "try:                                                                          \n"
    "    import shredder                                                           \n"
    "    shredder.run_gui()                                                        \n"
    "except ImportError as err:                                                    \n"
    "    print('Failed to load shredder:', err)                                    \n"
    "    print('This might be due to a corrupted install; try reinstalling.')      \n";

int rm_session_gui_main(int argc, const char **argv) {
    const char *commands[] = {"python3", "python", NULL};
    const char **command = &commands[0];

    GError *error = NULL;
    gchar *bootstrap_path = NULL;
    int bootstrap_fd =
        g_file_open_tmp(".shredder-bootstrap.py.XXXXXX", &bootstrap_path, &error);

    if(bootstrap_fd < 0) {
        rm_log_warning("Could not bootstrap gui: Unable to create tempfile: %s",
                       error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    if(write(bootstrap_fd, RM_PY_BOOTSTRAP, sizeof(RM_PY_BOOTSTRAP)) < 0) {
        rm_log_warning_line("Could not bootstrap gui: Unable to write to tempfile: %s",
                            g_strerror(errno));
        return EXIT_FAILURE;
    }

    close(bootstrap_fd);

    while(*command) {
        const char *all_argv[512];
        const char **argp = &all_argv[0];
        memset(all_argv, 0, sizeof(all_argv));

        *argp++ = *command;
        *argp++ = bootstrap_path;

        for(size_t i = 0; i < (size_t)argc && i < sizeof(all_argv) / 2; i++) {
            *argp++ = argv[i];
        }

        if(execvp(*command, (char *const *)all_argv) == -1) {
            rm_log_warning("Executed: %s ", *command);
            for(int j = 0; j < (argp - all_argv); j++) {
                rm_log_warning("%s ", all_argv[j]);
            }
            rm_log_warning("\n");
            rm_log_error_line("%s %d", g_strerror(errno), errno == ENOENT);
        } else {
            /* This is not reached anymore when execve suceeded */
            break;
        }

        /* Try next command... */
        command++;
    }
    return EXIT_SUCCESS;
}

int rm_session_replay_main(RmSession *session) {
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

/**
 * *********** btrfs clone session main ************
 **/
int rm_session_btrfs_clone_main(RmCfg *cfg) {
    if(cfg->path_count != 2) {
        rm_log_error(_("Usage: rmlint --btrfs-clone [-r] [-v|V] source dest\n"));
        return EXIT_FAILURE;
    }

    if(!rm_session_check_kernel_version(cfg, 4, 2)) {
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

/**
 * *********** `rmlint --is-clone` session main ************
 **/
int rm_session_is_clone_main(RmCfg *cfg) {

    if(cfg->path_count != 2) {
        rm_log_error(_("Usage: rmlint --is-clone [-v|V] file1 file2\n"));
        return EXIT_FAILURE;
    }

    g_assert(cfg->paths);
    RmPath *a = cfg->paths->data;
    g_assert(cfg->paths->next);
    RmPath *b = cfg->paths->next->data;
    rm_log_debug_line("Testing if %s is clone of %s", a->path, b->path);

    if(rm_offsets_match(a->path, b->path)) {
        rm_log_debug_line("Offsets match");
        return EXIT_SUCCESS;
    }

    rm_log_debug_line("Offsets don't match");
    return EXIT_FAILURE;
}

int rm_session_main(RmSession *session) {
    int exit_state = EXIT_SUCCESS;

    RmCfg *cfg = session->cfg;
    rm_fmt_set_state(cfg->formats, RM_PROGRESS_STATE_INIT);
    rm_fmt_set_state(cfg->formats, RM_PROGRESS_STATE_TRAVERSE);

    if(cfg->list_mounts) {
        session->mounts = rm_mounts_table_new(cfg->fake_fiemap);
    }

    if(session->mounts == NULL) {
        rm_log_debug_line("No mount table created.");
    }

    session->mds = rm_mds_new(cfg->threads, session->mounts, cfg->fake_pathindex_as_disk);

    rm_traverse_tree(session);

    rm_log_debug_line("List build finished at %.3f with %" RM_COUNTER_FORMAT " files",
                      rm_counter_elapsed_time(),
                      rm_counter_get(RM_COUNTER_TOTAL_FILES));

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

    if(rm_counter_get(RM_COUNTER_TOTAL_FILES) < 2 && session->cfg->run_equal_mode) {
        rm_log_warning_line(
            _("Not enough files for --equal (need at least two to compare)"));
        return EXIT_FAILURE;
    }

    if(rm_counter_get(RM_COUNTER_TOTAL_FILES) >= 1) {
        rm_fmt_set_state(session->cfg->formats, RM_PROGRESS_STATE_PREPROCESS);
        rm_preprocess(session);

        if(cfg->find_duplicates || cfg->merge_directories) {
            rm_shred_run(session);

            rm_log_debug_line("Dupe search finished at time %.3f",
                              rm_counter_elapsed_time());
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

    if(rm_counter_get(RM_COUNTER_SHRED_BYTES_REMAINING) != 0) {
        rm_log_error_line("BUG: Number of remaining bytes is %" RM_COUNTER_FORMAT
                          " (not 0). Please report this.",
                          rm_counter_get(RM_COUNTER_SHRED_BYTES_REMAINING));
        exit_state = EXIT_FAILURE;
    }

    if(rm_counter_get(RM_COUNTER_SHRED_FILES_REMAINING) != 0) {
        rm_log_error_line("BUG: Number of remaining files is %" RM_COUNTER_FORMAT
                          " (not 0). Please report this.",
                          rm_counter_get(RM_COUNTER_SHRED_FILES_REMAINING));
        exit_state = EXIT_FAILURE;
    }

    if(exit_state == EXIT_SUCCESS && cfg->run_equal_mode) {
        return session->equal_exit_code;
    }

    return exit_state;
}
