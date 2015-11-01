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
 *  - Christopher <sahib> Pahl 2010-2015 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2015 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include "../formats.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

typedef struct RmFmtHandlerUniques {
    /* must be first */
    RmFmtHandler parent;

    /* Storage for all strings */
    GStringChunk *text_chunks;

    /* Pointers into text_chunks */
    GQueue *text_lines;

} RmFmtHandlerUniques;

static void rm_fmt_elem(_U RmSession *session, _U RmFmtHandler *parent, _U FILE *out,
                        RmFile *file) {
    RmFmtHandlerUniques *self = (RmFmtHandlerUniques *)parent;

    char line[512 + 32];
    memset(line, 0, sizeof(line));

    if(file->lint_type != RM_LINT_TYPE_UNIQUE_FILE) {
        /* we only not want to list unfinished files. */
        return;
    }

    RM_DEFINE_PATH(file);

    g_snprintf(line, sizeof(line), "%s\n", file_path);

    if(self->text_chunks == NULL) {
        self->text_chunks = g_string_chunk_new(PATH_MAX / 2);
        self->text_lines = g_queue_new();
    }

    /* remember the line (use GStringChunk for effiecient storage) */
    g_queue_push_tail(self->text_lines, g_string_chunk_insert(self->text_chunks, line));
}

static void rm_fmt_prog(RmSession *session,
                        _U RmFmtHandler *parent,
                        FILE *out,
                        RmFmtProgressState state) {
    RmFmtHandlerUniques *self = (RmFmtHandlerUniques *)parent;

    extern RmFmtHandler *PROGRESS_HANDLER;
    rm_assert_gentle(PROGRESS_HANDLER->prog);
    PROGRESS_HANDLER->prog(session, (RmFmtHandler *)PROGRESS_HANDLER, stderr, state);

    /* Print all cached lines on shutdown. */
    if(state == RM_PROGRESS_STATE_PRE_SHUTDOWN && self->text_lines) {
        fprintf(out, "%sUNIQUE FILES:%s\n", MAYBE_GREEN(out, session), MAYBE_RESET(out, session));
        //fprintf(out, "UNIQUE FILES:\n");
        for(GList *iter = self->text_lines->head; iter; iter = iter->next) {
            char *line = iter->data;
            if(line != NULL) {
                fprintf(out, "%s", line);
            }
        }
        g_queue_free(self->text_lines);
        g_string_chunk_free(self->text_chunks);
        fprintf(out, "\n");
    }
}

static RmFmtHandlerUniques UNIQUES_HANDLER_IMPL = {
    /* Initialize parent */
    .parent =
        {
            .size = sizeof(UNIQUES_HANDLER_IMPL),
            .name = "uniques",
            .head = NULL,
            .elem = rm_fmt_elem,
            .prog = rm_fmt_prog,
            .foot = NULL,
            .valid_keys = {NULL},
        },
    .text_lines = NULL,

};

RmFmtHandler *UNIQUES_HANDLER = (RmFmtHandler *)&UNIQUES_HANDLER_IMPL;
