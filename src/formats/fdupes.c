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
} RmFmtHandlerFdupes;


static void rm_fmt_elem(_U RmSession *session, RmFmtHandler *parent, FILE *out, RmFile *file) {
    switch(file->lint_type) {
    case RM_LINT_TYPE_DUPE_DIR_CANDIDATE:
    case RM_LINT_TYPE_DUPE_CANDIDATE:
        if(file->is_original) {
            fprintf(out, "\n");
            fprintf(out, MAYBE_GREEN(session));
        } 

        fprintf(out, "%s\n", file->path);

        if(file->is_original) {
            fprintf(out, MAYBE_RESET(session));
        } 
        break;
    default:
        fprintf(out, "%s%s%s\n", MAYBE_BLUE(session), file->path, MAYBE_RESET(session));
        break;
    }
}

static RmFmtHandlerFdupes FDUPES_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(FDUPES_HANDLER_IMPL),
        .name = "fdupes",
        .head = NULL,
        .elem = rm_fmt_elem,
        .prog = NULL,
        .foot = NULL
    },
};

RmFmtHandler *FDUPES_HANDLER = (RmFmtHandler *) &FDUPES_HANDLER_IMPL;
