/*
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

#include "../formats.h"

#include <glib.h>
#include <search.h>
#include <stdio.h>
#include <string.h>

#include <sys/ioctl.h>

#define ARROW \
    fprintf(out, "%s==>%s ", MAYBE_YELLOW(out, session), MAYBE_RESET(out, session));

typedef struct RmFmtHandlerStats {
    /* must be first */
    RmFmtHandler parent;
} RmFmtHandlerStats;

static void rm_fmt_prog(RmSession *session,
                        _UNUSED RmFmtHandler *parent,
                        _UNUSED FILE *out,
                        RmFmtProgressState state) {
    if(state != RM_PROGRESS_STATE_SUMMARY) {
        return;
    }

    if(session->duplicate_bytes == 0 && session->shred_bytes_read == 0) {
        fprintf(out, _("No shred stats.\n"));
        return;
    }

    if(rm_session_was_aborted()) {
        /* Clear the whole terminal line.
         * Progressbar might leave some junk.
         */
        struct winsize terminal;
        ioctl(fileno(out), TIOCGWINSZ, &terminal);
        for(int i = 0; i < terminal.ws_col; ++i) {
            fprintf(out, " ");
        }

        fprintf(out, "\n");
    }

    char numbers[64];

    ARROW fprintf(out, _("%sDuplicate finding stats (includes hardlinks):%s\n\n"),
                  MAYBE_BLUE(out, session), MAYBE_RESET(out, session));

    rm_util_size_to_human_readable(session->original_bytes, numbers, sizeof(numbers));
    fprintf(out, _("%s%15s%s bytes of originals\n"), MAYBE_RED(out, session), numbers,
            MAYBE_RESET(out, session));

    rm_util_size_to_human_readable(session->duplicate_bytes, numbers, sizeof(numbers));
    fprintf(out, _("%s%15s%s bytes of duplicates\n"), MAYBE_RED(out, session), numbers,
            MAYBE_RESET(out, session));

    rm_util_size_to_human_readable(session->unique_bytes, numbers, sizeof(numbers));
    fprintf(out, _("%s%15s%s bytes of non-duplicates\n"), MAYBE_RED(out, session),
            numbers, MAYBE_RESET(out, session));

    rm_util_size_to_human_readable(session->shred_bytes_read, numbers, sizeof(numbers));
    fprintf(out, _("%s%15s%s bytes of files data actually read\n"),
            MAYBE_RED(out, session), numbers, MAYBE_RESET(out, session));

    fprintf(out, _("%s%15d%s Files in total\n"), MAYBE_RED(out, session),
            session->total_files, MAYBE_RESET(out, session));
    fprintf(out, _("%s%15ld%s Duplicate files\n"), MAYBE_RED(out, session),
            (long)session->dup_counter, MAYBE_RESET(out, session));
    fprintf(out, _("%s%15ld%s Groups in total\n"), MAYBE_RED(out, session),
            (long)session->dup_group_counter, MAYBE_RESET(out, session));
    fprintf(out, _("%s%15ld%s Other lint items\n"), MAYBE_RED(out, session),
            (long)session->other_lint_cnt, MAYBE_RESET(out, session));

    gfloat elapsed = g_timer_elapsed(session->timer_since_proc_start, NULL);

    char *elapsed_time = rm_format_elapsed_time(elapsed, 5);
    fprintf(
            out,
            _("%s%15s%s of time spent scanning\n"),
            MAYBE_RED(out, session), elapsed_time, MAYBE_RESET(out, session)
    );
    g_free(elapsed_time);

    char eff_total[64] = "NaN";
    char eff_dupes[64] = "NaN";
    if(session->shred_bytes_read != 0) {
        gfloat efficiency = 100 * (
                session->duplicate_bytes +
                session->original_bytes +
                session->unique_bytes
            ) /	session->shred_bytes_read;


        snprintf(eff_total, sizeof(eff_total), "%.0f%%", efficiency);
        efficiency = 100 * (0 + session->duplicate_bytes + session->original_bytes) /
                     session->shred_bytes_read;
        snprintf(eff_dupes, sizeof(eff_dupes), "%.1f%%", efficiency);
    }

    fprintf(out, _("%s%15s%s Algorithm efficiency on total files basis\n"),
            MAYBE_RED(out, session), eff_total, MAYBE_RESET(out, session));
    fprintf(out, _("%s%15s%s Algorithm efficiency on duplicate file basis\n"),
            MAYBE_RED(out, session), eff_dupes, MAYBE_RESET(out, session));
}

static RmFmtHandlerStats STATS_HANDLER_IMPL = {
    /* Initialize parent */
    .parent =
        {
            .size = sizeof(STATS_HANDLER_IMPL),
            .name = "stats",
            .head = NULL,
            .elem = NULL,
            .prog = rm_fmt_prog,
            .foot = NULL,
            .valid_keys = {NULL},
        },
};

RmFmtHandler *STATS_HANDLER = (RmFmtHandler *)&STATS_HANDLER_IMPL;
