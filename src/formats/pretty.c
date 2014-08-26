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
#include "../preprocess.h"

#include <glib.h>
#include <stdio.h>

static const char *RM_LINT_TYPE_TO_DESCRIPTION[] = {
    [RM_LINT_TYPE_UNKNOWN]      = "",
    [RM_LINT_TYPE_BLNK]         = "Bad link(s)",
    [RM_LINT_TYPE_EDIR]         = "Empty dir(s)",
    [RM_LINT_TYPE_NBIN]         = "Non stripped binarie(s)",
    [RM_LINT_TYPE_BADUID]       = "Bad UID(s)",
    [RM_LINT_TYPE_BADGID]       = "Bad GID(s)",
    [RM_LINT_TYPE_BADUGID]      = "Bad UID and GID(s)",
    [RM_LINT_TYPE_EFILE]        = "Empty file(s)",
    [RM_LINT_TYPE_DUPE_CANDIDATE] = "Duplicate(s)"
};

static const char *RM_LINT_TYPE_TO_COMMAND[] = {
    [RM_LINT_TYPE_UNKNOWN]        = "",
    [RM_LINT_TYPE_BLNK]           = RED"rm",
    [RM_LINT_TYPE_EDIR]           = RED"rmdir",
    [RM_LINT_TYPE_NBIN]           = BLU"strip --strip-debug",
    [RM_LINT_TYPE_BADUID]         = BLU"chown %s",
    [RM_LINT_TYPE_BADGID]         = BLU"chgrp %s",
    [RM_LINT_TYPE_BADUGID]        = BLU"chown %s:%s",
    [RM_LINT_TYPE_EFILE]          = RED"rm",
    [RM_LINT_TYPE_DUPE_CANDIDATE] = RED"rm",
    [RM_LINT_TYPE_ORIGINAL_TAG]   = GRE"ls"
};

typedef struct RmFmtHandlerPretty {
    /* must be first */
    RmFmtHandler parent;

    /* user data */
    RmLintType last_lint_type;

    const char *user;
    const char *group;
} RmFmtHandlerProgress;

static void rm_fmt_head(RmSession *session, RmFmtHandler *parent, FILE *out) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;

    self->user = rm_util_get_username();
    self->group = rm_util_get_groupname();
}

static void rm_fmt_elem(G_GNUC_UNUSED RmSession *session, RmFmtHandler *parent, FILE *out, RmFile *file) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;

    if(file->lint_type != self->last_lint_type) {
        fprintf(out, "\n"YEL"#"NCO" %s:\n", RM_LINT_TYPE_TO_DESCRIPTION[file->lint_type]);
        self->last_lint_type = file->lint_type;
    }

    fprintf(out, "    ");

    const char *format = RM_LINT_TYPE_TO_COMMAND[file->lint_type];

    switch(file->lint_type) {
    case RM_LINT_TYPE_BADUID:
        fprintf(out, format, self->user);
        break;
    case RM_LINT_TYPE_BADGID:
        fprintf(out, format, self->group);
        break;
    case RM_LINT_TYPE_BADUGID:
        fprintf(out, format, self->user, self->group);
        break;
    case RM_LINT_TYPE_DUPE_CANDIDATE:
        if(rm_file_tables_is_original(session->tables, file)) {
            fprintf(out, RM_LINT_TYPE_TO_COMMAND[RM_LINT_TYPE_ORIGINAL_TAG]);
        } else {
            fprintf(out, format);
        }
        break;
    default:
        fprintf(out, format);
    }

    fprintf(out, NCO" %s\n", file->path);
}

static RmFmtHandlerProgress PRETTY_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .name = "pretty",
        .head = rm_fmt_head,
        .elem = rm_fmt_elem,
        .prog = NULL,
        .foot = NULL
    },

    /* Initialize own stuff */
    .last_lint_type = RM_LINT_TYPE_UNKNOWN
};

RmFmtHandler *PRETTY_HANDLER = (RmFmtHandler *) &PRETTY_HANDLER_IMPL;
