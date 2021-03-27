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
#include "logger.h"
#include "preprocess.h"
#include "session.h"
#include "traverse.h"
#include "xattr.h"


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
