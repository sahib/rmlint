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


typedef struct RmFmtHandlerFdupes {
    /* must be first */
    RmFmtHandler parent;

    /* Storage for all strings */
    GStringChunk *text_chunks;

    /* Pointers into text_chunks */
    GQueue *text_lines;

    /* Do not print original (fdupes emulation) */
    bool omit_first_line;

    /* Do not print newlines between files */
    bool use_same_line;
} RmFmtHandlerFdupes;

static void rm_fmt_elem(_U RmSession *session, _U RmFmtHandler *parent, _U FILE *out, RmFile *file) {
    RmFmtHandlerFdupes *self = (RmFmtHandlerFdupes *)parent;

    char line[512 + 32];
    memset(line, 0, sizeof(line));

    if(file->lint_type == RM_LINT_TYPE_UNFINISHED_CKSUM) {
        /* we do not want to list unfinished files. */
        return;
    }

    RM_DEFINE_PATH(file);

    switch(file->lint_type) {
    case RM_LINT_TYPE_DUPE_DIR_CANDIDATE:
    case RM_LINT_TYPE_DUPE_CANDIDATE:
        if(self->omit_first_line && file->is_original) {
            strcpy(line, "\n");
        } else {
            g_snprintf(
                line, sizeof(line),
                "%s%s%s%s%c",
                (file->is_original) ? "\n" : "",
                (file->is_original) ? MAYBE_GREEN(out, session) : "",
                file_path,
                (file->is_original) ? MAYBE_RESET(out, session) : "",
                (self->use_same_line) ? ' ' : '\n'
            );
        }
        break;
    default:
        g_snprintf(
            line, sizeof(line),
            "%s%s%s%c",
            MAYBE_BLUE(out, session),
            file_path,
            MAYBE_RESET(out, session),
            (self->use_same_line) ? ' ' : '\n'
        );
        break;
    }

    if(self->text_chunks == NULL) {
        self->text_chunks = g_string_chunk_new(PATH_MAX / 2);
        self->text_lines = g_queue_new();
    }

    /* remember the line (use GStringChunk for effiecient storage) */
    g_queue_push_tail(
        self->text_lines,
        g_string_chunk_insert(self->text_chunks, line)
    );
}

static void rm_fmt_prog(
    RmSession *session,
    _U RmFmtHandler *parent,
    _U FILE *out,
    RmFmtProgressState state
) {
    RmFmtHandlerFdupes *self = (RmFmtHandlerFdupes *)parent;

    if(state == RM_PROGRESS_STATE_INIT) {
        self->omit_first_line = (rm_fmt_get_config_value(session->formats, "fdupes", "omitfirst") != NULL);
        self->use_same_line = (rm_fmt_get_config_value(session->formats, "fdupes", "sameline") != NULL);
    }

    /* We do not respect `out` here; just use stderr and stdout directly.
     * Reason: fdupes does this, let's imitate weird behaviour!
     */
    extern RmFmtHandler *PROGRESS_HANDLER;
    g_assert(PROGRESS_HANDLER->prog);
    PROGRESS_HANDLER->prog(session, (RmFmtHandler *)PROGRESS_HANDLER, stderr, state);

    /* Print all cached lines on shutdown. */
    if(state == RM_PROGRESS_STATE_PRE_SHUTDOWN && self->text_lines) {
        for(GList *iter = self->text_lines->head; iter; iter = iter->next) {
            char *line = iter->data;
            if(line != NULL) {
                fprintf(stdout, "%s", line);
            }
        }
        g_queue_free(self->text_lines);
        g_string_chunk_free(self->text_chunks);
        fprintf(stdout, "\n");
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
        .foot = NULL,
        .valid_keys = {"omitfirst", "sameline", NULL},
    },
    .text_lines = NULL,
    .use_same_line = false,
    .omit_first_line = false

};

RmFmtHandler *FDUPES_HANDLER = (RmFmtHandler *) &FDUPES_HANDLER_IMPL;
