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


typedef struct RmFmtHandlerFdupes {
    /* must be first */
    RmFmtHandler parent;

    GQueue *text_lines;
} RmFmtHandlerFdupes;


static void rm_fmt_elem(_U RmSession *session, _U RmFmtHandler *parent, _U FILE *out, RmFile *file) {
    RmFmtHandlerFdupes *self = (RmFmtHandlerFdupes *)parent;
    char *line = NULL;

    switch(file->lint_type) {
    case RM_LINT_TYPE_DUPE_DIR_CANDIDATE:
    case RM_LINT_TYPE_DUPE_CANDIDATE:
        line = g_strdup_printf(
                   "%s%s%s%s\n",
                   (file->is_original) ? "\n" : "",
                   (file->is_original) ? MAYBE_GREEN(session) : "",
                   file->path,
                   (file->is_original) ? MAYBE_RESET(session) : ""
               );
        break;
    default:
        line = g_strdup_printf(
                   "%s%s%s\n", MAYBE_BLUE(session), file->path, MAYBE_RESET(session)
               );
        break;
    }

    if(self->text_lines == NULL) {
        self->text_lines = g_queue_new();
    }

    g_queue_push_tail(self->text_lines, line);
}

static void rm_fmt_prog(
    RmSession *session,
    _U RmFmtHandler *parent,
    _U FILE *out,
    RmFmtProgressState state
) {
    RmFmtHandlerFdupes *self = (RmFmtHandlerFdupes *)parent;

    /* We do not respect `out` here; just use stderr and stdout directly.
     * Reason: fdupes does this, let's imitate weird behaviour!
     */

    extern RmFmtHandler *PROGRESS_HANDLER;
    g_assert(PROGRESS_HANDLER->prog);
    PROGRESS_HANDLER->prog(session, (RmFmtHandler *)PROGRESS_HANDLER, stderr, state);

    if(state == RM_PROGRESS_STATE_SUMMARY && self->text_lines) {
        fprintf(stdout, "\n");
        for(GList *iter = self->text_lines->head; iter; iter = iter->next) {
            char *line = iter->data;
            fprintf(stdout, "%s", line);
        }
        g_queue_free_full(self->text_lines, g_free);
    }

    extern RmFmtHandler *SUMMARY_HANDLER;
    g_assert(SUMMARY_HANDLER->prog);
    SUMMARY_HANDLER->prog(session, (RmFmtHandler *)SUMMARY_HANDLER, stderr, state);
}

static RmFmtHandlerFdupes FDUPES_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(FDUPES_HANDLER_IMPL),
        .name = "fdupes",
        .head = NULL,
        .elem = rm_fmt_elem,
        .prog = rm_fmt_prog,
        .foot = NULL
    },
    .text_lines = NULL
};

RmFmtHandler *FDUPES_HANDLER = (RmFmtHandler *) &FDUPES_HANDLER_IMPL;
