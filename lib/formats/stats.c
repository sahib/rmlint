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
 *  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
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

    if(rm_counter_get(RM_COUNTER_DUPLICATE_BYTES) == 0 &&
       rm_counter_get(RM_COUNTER_SHRED_BYTES_READ) == 0) {
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

    rm_util_size_to_human_readable(rm_counter_get(RM_COUNTER_ORIGINAL_BYTES), numbers,
                                   sizeof(numbers));
    fprintf(out, _("%s%15s%s bytes of originals\n"), MAYBE_RED(out, session), numbers,
            MAYBE_RESET(out, session));

    rm_util_size_to_human_readable(rm_counter_get(RM_COUNTER_DUPLICATE_BYTES), numbers,
                                   sizeof(numbers));
    fprintf(out, _("%s%15s%s bytes of duplicates\n"), MAYBE_RED(out, session), numbers,
            MAYBE_RESET(out, session));

    rm_util_size_to_human_readable(rm_counter_get(RM_COUNTER_UNIQUE_BYTES), numbers,
                                   sizeof(numbers));
    fprintf(out, _("%s%15s%s bytes of non-duplicates\n"), MAYBE_RED(out, session),
            numbers, MAYBE_RESET(out, session));

    rm_util_size_to_human_readable(rm_counter_get(RM_COUNTER_SHRED_BYTES_READ), numbers,
                                   sizeof(numbers));
    fprintf(out, _("%s%15s%s bytes of files data actually read\n"),
            MAYBE_RED(out, session), numbers, MAYBE_RESET(out, session));

    fprintf(out, _("%s%15" RM_COUNTER_FORMAT "%s Files in total\n"),
            MAYBE_RED(out, session), rm_counter_get(RM_COUNTER_TOTAL_FILES),
            MAYBE_RESET(out, session));
    fprintf(out, _("%s%15" RM_COUNTER_FORMAT "%s Duplicate files\n"),
            MAYBE_RED(out, session), rm_counter_get(RM_COUNTER_DUP_COUNTER),
            MAYBE_RESET(out, session));
    fprintf(out, _("%s%15" RM_COUNTER_FORMAT "%s Groups in total\n"),
            MAYBE_RED(out, session), rm_counter_get(RM_COUNTER_DUP_GROUP_COUNTER),
            MAYBE_RESET(out, session));
    fprintf(out, _("%s%15" RM_COUNTER_FORMAT "%s Other lint items\n"),
            MAYBE_RED(out, session), rm_counter_get(RM_COUNTER_OTHER_LINT_CNT),
            MAYBE_RESET(out, session));

    char *elapsed_time = rm_format_elapsed_time(rm_counter_elapsed_time(), 5);
    fprintf(
            out,
            _("%s%15s%s of time spent scanning\n"),
            MAYBE_RED(out, session), elapsed_time, MAYBE_RESET(out, session)
    );
    g_free(elapsed_time);

    char eff_total[64] = "NaN";
    char eff_dupes[64] = "NaN";
    if(rm_counter_get(RM_COUNTER_SHRED_BYTES_READ) != 0) {
        gfloat efficiency = 100 * (0 + rm_counter_get(RM_COUNTER_DUPLICATE_BYTES) +
                                   rm_counter_get(RM_COUNTER_ORIGINAL_BYTES) +
                                   rm_counter_get(RM_COUNTER_UNIQUE_BYTES)) /
                            rm_counter_get(RM_COUNTER_SHRED_BYTES_READ);

        snprintf(eff_total, sizeof(eff_total), "%.0f%%", efficiency);
        efficiency = 100 * (0 + rm_counter_get(RM_COUNTER_DUPLICATE_BYTES) +
                            rm_counter_get(RM_COUNTER_ORIGINAL_BYTES)) /
                     rm_counter_get(RM_COUNTER_SHRED_BYTES_READ);
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
