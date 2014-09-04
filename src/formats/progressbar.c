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

typedef struct RmFmtHandlerProgress {
    /* must be first */
    RmFmtHandler parent;

    /* user data */
    char percent;
    RmFmtProgressState last_state;
    guint64 n, N;
} RmFmtHandlerProgress;

static void rm_fmt_head(_U RmSession *session, _U RmFmtHandler *parent, FILE *out) {
    fprintf(out, " Hi, Im a progressbar!\r");
    fflush(out);
}

static void rm_fmt_elem(_U RmSession *session, RmFmtHandler *parent, FILE *out, _U RmFile *file) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;
    if(self->percent > 100) {
        self->percent = 100;
    }

    fprintf(out, " [");

    for(int i = 0; i < self->percent; ++i) {
        if(i == self->percent - 1) {
            fprintf(out, "->");
        } else {
            fprintf(out, "-");
        }
    }

    int left = 50 - self->percent;
    for(int i = 0; i < left; ++i)  {
        fprintf(out, " ");
    }

    fprintf(out, "] %-30s (%lu/%lu)    \r", rm_fmt_progress_to_string(self->last_state), self->n , self->N);
    fflush(out);

    self->percent++;
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
    self->last_state = state;
}

static void rm_fmt_foot(_U RmSession *session, _U RmFmtHandler *parent, FILE *out) {
    fprintf(out, "End of demonstration.%150s\n", " ");
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
