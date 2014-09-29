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
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include "../formats.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include <sys/ioctl.h>

typedef struct RmFmtHandlerProgress {
    /* must be first */
    RmFmtHandler parent;

    /* user data */
    gdouble percent;

    char text_buf[1024];
    guint32 text_len;
    guint32 update_counter;
    guint32 update_interval;

    RmFmtProgressState last_state;
    struct winsize terminal;
} RmFmtHandlerProgress;

static void rm_fmt_progress_format_text(RmSession *session, RmFmtHandlerProgress *self) {
    switch(self->last_state) {
    case RM_PROGRESS_STATE_TRAVERSE:
        self->percent = 1.0;
        self->text_len = g_snprintf(
                             self->text_buf, sizeof(self->text_buf),
                             "Traversing (%s%"LLU"%s usable files / %s%"LLU"%s + %s%"LLU"%s ignored files / folders)",
                             MAYBE_GREEN(session), session->total_files, MAYBE_RESET(session),
                             MAYBE_RED(session), session->ignored_files, MAYBE_RESET(session),
                             MAYBE_RED(session), session->ignored_folders, MAYBE_RESET(session)
                         );
        break;
    case RM_PROGRESS_STATE_PREPROCESS:
        self->percent = 1.0;
        self->text_len = g_snprintf(
                             self->text_buf, sizeof(self->text_buf),
                             "Preprocessing (reduced files to %s%"LLU"%s / found %s%"LLU"%s other lint)",
                             MAYBE_GREEN(session), session->total_filtered_files, MAYBE_RESET(session),
                             MAYBE_RED(session), session->other_lint_cnt, MAYBE_RESET(session)
                         );
        break;
    case RM_PROGRESS_STATE_SHREDDER:
        self->percent = ((gdouble)session->dup_counter + session->dup_group_counter) / ((gdouble)session->total_filtered_files);
        self->text_len = g_snprintf(
                             self->text_buf, sizeof(self->text_buf),
                             "Matching files (%s%"LLU"%s dupes of %s%"LLU"%s originals; %s%.2f%s GiB to scan in %s%"LLU"%s files)",
                             MAYBE_RED(session), session->dup_counter, MAYBE_RESET(session),
                             MAYBE_YELLOW(session), session->dup_group_counter, MAYBE_RESET(session),
                             MAYBE_GREEN(session), (double)session->shred_bytes_remaining / 1024 / 1024 / 1024, MAYBE_RESET(session),
                             MAYBE_GREEN(session), session->shred_files_remaining, MAYBE_RESET(session)
                         );
        break;
    case RM_PROGRESS_STATE_INIT:
    case RM_PROGRESS_STATE_SUMMARY:
    default:
        self->percent = 0;
        memset(self->text_buf, 0, sizeof(self->text_buf));
        break;
    }

    /* Get rid of colors */
    for(char *iter = &self->text_buf[0]; *iter; iter++) {
        if(*iter == '\x1b') {
            char *jump = strchr(iter, 'm');
            if(jump != NULL) {
                self->text_len -= jump - iter + 1;
                iter = jump;
            }
        }
    }
}

static void rm_fmt_progress_print_text(RmFmtHandlerProgress *self, int width, FILE *out) {
    for(guint32 i = 0; i < width - self->text_len - 1; ++i) {
        fprintf(out, " ");
    }

    fprintf(out, "%s\r", self->text_buf);
}

static void rm_fmt_progress_print_bar(RmFmtHandlerProgress *self, int width, FILE *out) {
    int cells = width * self->percent;

    fprintf(out, "[");
    for(int i = 0; i < width; ++i) {
        if(i < cells) {
            fprintf(out, "#");
        } else if(i == cells) {
            fprintf(out, ">");
        } else {
            fprintf(out, "-");
        }
    }
    fprintf(out, "]");
}

static void rm_fmt_prog(
    RmSession *session,
    RmFmtHandler *parent,
    FILE *out,
    RmFmtProgressState state
) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;

    if(state == RM_PROGRESS_STATE_INIT) {
        /* Do initializiation here */
        if(ioctl(0, TIOCGWINSZ, &self->terminal) != 0) {
            rm_log_warning(YELLOW"Warning:"RESET" Cannot figure out terminal width.\n");
        }

        const char *update_interval_str = rm_fmt_get_config_value(
                                              session->formats, "progressbar", "update_interval"
                                          );

        if(update_interval_str) {
            self->update_interval = g_ascii_strtoull(update_interval_str, NULL, 10);
        }

        if(self->update_interval == 0) {
            self->update_interval = 15;
        }

        fprintf(out, "\e[?25l"); /* Hide the cursor */
        fflush(out);
        return;
    }

    if(state == RM_PROGRESS_STATE_SUMMARY || rm_session_was_aborted(session)) {
        fprintf(out, "\e[?25h"); /* show the cursor */
        fflush(out);
        return;
    }

    if(self->last_state != state && self->last_state != RM_PROGRESS_STATE_INIT) {
        fprintf(out, "\n");
    } else if((self->update_counter++ % self->update_interval) > 0) {
        return;
    }

    self->last_state = state;

    rm_fmt_progress_format_text(session, self);
    rm_fmt_progress_print_bar(self, self->terminal.ws_col * 0.5, out);
    rm_fmt_progress_print_text(self, self->terminal.ws_col * 0.5, out);

    if(state == RM_PROGRESS_STATE_SUMMARY) {
        fprintf(out, "\n");
    }
}

static RmFmtHandlerProgress PROGRESS_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(PROGRESS_HANDLER_IMPL),
        .name = "progressbar",
        .head = NULL,
        .elem = NULL,
        .prog = rm_fmt_prog,
        .foot = NULL
    },

    /* Initialize own stuff */
    .percent = 0.0f,
    .text_len = 0,
    .text_buf = {0},
    .update_counter = 0,
    .last_state = RM_PROGRESS_STATE_INIT
};

RmFmtHandler *PROGRESS_HANDLER = (RmFmtHandler *) &PROGRESS_HANDLER_IMPL;
