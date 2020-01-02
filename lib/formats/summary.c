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

typedef struct RmFmtHandlerSummary {
    /* must be first */
    RmFmtHandler parent;
} RmFmtHandlerSummary;

#define ARROW \
    fprintf(out, "%s==>%s ", MAYBE_YELLOW(out, session), MAYBE_RESET(out, session));

static void rm_fmt_prog(RmSession *session,
                        _UNUSED RmFmtHandler *parent,
                        _UNUSED FILE *out,
                        RmFmtProgressState state) {
    if(state != RM_PROGRESS_STATE_SUMMARY) {
        return;
    }

    if(session->total_files <= 1) {
        ARROW fprintf(out, "%s%d%s", MAYBE_RED(out, session), session->total_files,
                      MAYBE_RESET(out, session));
        fprintf(out, _(" file(s) after investigation, nothing to search through.\n"));
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
        ARROW fprintf(out, _("Early shutdown, probably not all lint was found.\n"));
    }

    if(rm_fmt_has_formatter(session->formats, "pretty") &&
       rm_fmt_has_formatter(session->formats, "sh")) {
        ARROW fprintf(out, _("Note: Please use the saved script below for removal, not "
                             "the above output."));
        fprintf(out, "\n");
    }

    char numbers[3][512];
    snprintf(numbers[0], sizeof(numbers[0]), "%s%d%s", MAYBE_RED(out, session),
             session->total_files, MAYBE_RESET(out, session));
    snprintf(numbers[1], sizeof(numbers[1]), "%s%" LLU "%s", MAYBE_RED(out, session),
             session->dup_counter, MAYBE_RESET(out, session));
    snprintf(numbers[2], sizeof(numbers[2]), "%s%" LLU "%s", MAYBE_RED(out, session),
             session->dup_group_counter, MAYBE_RESET(out, session));

    ARROW fprintf(out, _("In total %s files, whereof %s are duplicates in %s groups.\n"),
                  numbers[0], numbers[1], numbers[2]);

    /* log10(2 ** 64) + 2 = 21; */
    char size_string_buf[22] = {0};
    rm_util_size_to_human_readable(session->total_lint_size, size_string_buf,
                                   sizeof(size_string_buf));

    ARROW fprintf(out, _("This equals %s%s%s of duplicates which could be removed.\n"),
                  MAYBE_RED(out, session), size_string_buf, MAYBE_RESET(out, session));

    if(session->other_lint_cnt > 0) {
        ARROW fprintf(out, "%s%" LLU "%s ", MAYBE_RED(out, session),
                      session->other_lint_cnt, MAYBE_RESET(out, session));

        fprintf(out, _("other suspicious item(s) found, which may vary in size.\n"));
    }

    gfloat elapsed = g_timer_elapsed(session->timer_since_proc_start, NULL);
    char *elapsed_time = rm_format_elapsed_time(elapsed, 3);
    ARROW fprintf(out, _("Scanning took in total %s%s%s.\n"),
                  MAYBE_RED(out, session), elapsed_time, MAYBE_RESET(out, session));
    g_free(elapsed_time);

    bool first_print_flag = true;
    GHashTableIter iter;
    char *path = NULL;
    RmFmtHandler *handler = NULL;
    rm_fmt_get_pair_iter(session->formats, &iter);

    while(g_hash_table_iter_next(&iter, (gpointer *)&path, (gpointer *)&handler)) {
        static const char *forbidden[] = {"stdout", "stderr", "stdin"};
        gsize forbidden_len = sizeof(forbidden) / sizeof(forbidden[0]);
        bool forbidden_found = false;

        for(gsize i = 0; i < forbidden_len; i++) {
            if(g_strcmp0(forbidden[i], path) == 0) {
                forbidden_found = true;
                break;
            }
        }

        if(forbidden_found) {
            continue;
        }

        /* Check if the file really exists, so we can print it for sure */
        if(access(path, R_OK) == -1) {
            continue;
        }

        if(first_print_flag) {
            fprintf(out, "\n");
            first_print_flag = false;
        }

        fprintf(out, _("Wrote a %s%s%s file to: %s%s%s\n"), MAYBE_BLUE(out, session),
                handler->name, MAYBE_RESET(out, session), MAYBE_GREEN(out, session), path,
                MAYBE_RESET(out, session));
    }
}

static RmFmtHandlerSummary SUMMARY_HANDLER_IMPL = {
    /* Initialize parent */
    .parent =
        {
            .size = sizeof(SUMMARY_HANDLER_IMPL),
            .name = "summary",
            .head = NULL,
            .elem = NULL,
            .prog = rm_fmt_prog,
            .foot = NULL,
            .valid_keys = {NULL},
        },
};

RmFmtHandler *SUMMARY_HANDLER = (RmFmtHandler *)&SUMMARY_HANDLER_IMPL;
