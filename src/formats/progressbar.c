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
    char percent;
    RmFmtProgressState last_state;
    bool state_changed;
    guint64 n, N;

    struct winsize terminal;
} RmFmtHandlerProgress;

static void rm_fmt_head(_U RmSession *session, _U RmFmtHandler *parent, FILE *out) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;
    if(ioctl(0, TIOCGWINSZ, &self->terminal) != 0) {
        rm_log_warning(YELLOW"Warning:"RESET" Cannot figure out terminal width.\n");
    }

    fprintf(out, "\e[?25l"); /* Hide the cursor */
}

static int X = 0;

static void rm_fmt_elem(_U RmSession *session, RmFmtHandler *parent, FILE *out, _U RmFile *file) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;

    if(self->state_changed) {
        fprintf(out, "\n");
        self->state_changed = false;
    } else if(X++ % 10 != 0) {
        return;
    }

    const int text_width = 30;
    char text[text_width];
    memset(text, 0, sizeof(text));

    strcpy(text, " ");
    strcpy(text, rm_file_lint_type_to_string(file->lint_type));


    double reached_percent = self->n / (double)MAX(1, self->N);

    /* 30 chars left for text */
    int bar_len = self->terminal.ws_col - text_width;
    int cells = bar_len * reached_percent;

    switch(self->last_state) {
        case RM_PROGRESS_STATE_TRAVERSE:
            fprintf(out, MAYBE_BLUE(session));
            break;
        case RM_PROGRESS_STATE_SHREDDER:
            fprintf(out, MAYBE_GREEN(session));
            break;
        default:
            fprintf(out, MAYBE_YELLOW(session));
            break;
    }

    fprintf(out, "[");
    for(int i = 0; i < bar_len; ++i) {
        if(i < cells) {
            fprintf(out, "=");
        } else if(i == cells) {
            fprintf(out, ">");
        } else {
            fprintf(out, " ");
        }
    } 
    fprintf(out, "] %s%s\r", rm_fmt_progress_to_string(self->last_state), MAYBE_RESET(session));
}

static void rm_fmt_prog(
    _U RmSession *session,
    RmFmtHandler *parent,
    _U FILE *out,
    RmFmtProgressState state,
    guint64 n, guint64 N
) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;
    self->n = n;
    self->N = N;
    self->state_changed = !(state == self->last_state);
    self->last_state = state;
}

static void rm_fmt_foot(_U RmSession *session, _U RmFmtHandler *parent, FILE *out) {
    fprintf(out, "\e[?25h"); /* show the cursor */
    fflush(out);
}

static RmFmtHandlerProgress PROGRESS_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(PROGRESS_HANDLER_IMPL),
        .name = "progressbar",
        .head = rm_fmt_head,
        .elem = rm_fmt_elem,
        .prog = rm_fmt_prog,
        .foot = rm_fmt_foot
    },

    /* Initialize own stuff */
    .percent = 0,
    .n = 0, .N = 0,
    .last_state = RM_PROGRESS_STATE_INIT
};

RmFmtHandler *PROGRESS_HANDLER = (RmFmtHandler *) &PROGRESS_HANDLER_IMPL;
