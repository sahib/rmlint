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
    [RM_LINT_TYPE_UNKNOWN]            = "",
    [RM_LINT_TYPE_BLNK]               = N_("Bad symlink(s)"),
    [RM_LINT_TYPE_EDIR]               = N_("Empty dir(s)"),
    [RM_LINT_TYPE_NBIN]               = N_("Non stripped binarie(s)"),
    [RM_LINT_TYPE_BADUID]             = N_("Bad UID(s)"),
    [RM_LINT_TYPE_BADGID]             = N_("Bad GID(s)"),
    [RM_LINT_TYPE_BADUGID]            = N_("Bad UID and GID(s)"),
    [RM_LINT_TYPE_EFILE]              = N_("Empty file(s)"),
    [RM_LINT_TYPE_DUPE_CANDIDATE]     = N_("Duplicate(s)"),
    [RM_LINT_TYPE_DUPE_DIR_CANDIDATE] = N_("Duplicate Directorie(s)")
};

static const char *RM_LINT_TYPE_TO_COMMAND[] = {
    [RM_LINT_TYPE_UNKNOWN]        = "",
    [RM_LINT_TYPE_BLNK]           = "rm",
    [RM_LINT_TYPE_EDIR]           = "rmdir",
    [RM_LINT_TYPE_NBIN]           = "strip --strip-debug",
    [RM_LINT_TYPE_BADUID]         = "chown %s",
    [RM_LINT_TYPE_BADGID]         = "chgrp %s",
    [RM_LINT_TYPE_BADUGID]        = "chown %s:%s",
    [RM_LINT_TYPE_EFILE]          = "rm",
    [RM_LINT_TYPE_DUPE_CANDIDATE] = "rm",
    [RM_LINT_TYPE_DUPE_DIR_CANDIDATE]  = "rm -rf"
};

static const char *rm_fmt_command_color(RmSession *session, RmFile *file) {
    switch(file->lint_type) {
    case RM_LINT_TYPE_NBIN:
    case RM_LINT_TYPE_BADUID:
    case RM_LINT_TYPE_BADGID:
    case RM_LINT_TYPE_BADUGID:
        return MAYBE_BLUE(session);
    case RM_LINT_TYPE_DUPE_CANDIDATE:
    case RM_LINT_TYPE_DUPE_DIR_CANDIDATE:
        if(file->is_original) {
            return MAYBE_GREEN(session);
        } else {
            return MAYBE_RED(session);
        }
    default:
        return MAYBE_RED(session);
    }
}

typedef struct RmFmtHandlerPretty {
    /* must be first */
    RmFmtHandler parent;

    /* user data */
    RmLintType last_lint_type;

    const char *user;
    const char *group;
    int elems_written;
} RmFmtHandlerProgress;

static void rm_fmt_head(_U RmSession *session, RmFmtHandler *parent, _U FILE *out) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;

    self->user = rm_util_get_username();
    self->group = rm_util_get_groupname();
}

static void rm_fmt_elem(_U RmSession *session, RmFmtHandler *parent, FILE *out, RmFile *file) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;
    self->elems_written++;

    if(file->lint_type != self->last_lint_type) {
        fprintf(
            out, "\n%s#%s %s:\n",
            MAYBE_YELLOW(session),
            MAYBE_RESET(session),
            _(RM_LINT_TYPE_TO_DESCRIPTION[file->lint_type])
        );
        self->last_lint_type = file->lint_type;
    }

    fprintf(out, "    %s", rm_fmt_command_color(session, file));

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
        if(file->is_original) {
            fprintf(out, "ls");
        } else {
            fprintf(out, "%s", format);
        }
        break;
    case RM_LINT_TYPE_DUPE_DIR_CANDIDATE:
        if(file->is_original) {
            fprintf(out, "ls -la");
        } else {
            fprintf(out, "%s", format);
        }
        break;
    default:
        fprintf(out, "%s", format);
    }

    fprintf(out, "%s %s\n", MAYBE_RESET(session), file->path);
}

static void rm_fmt_prog(
    _U RmSession *session,
    RmFmtHandler *parent,
    FILE *out,
    RmFmtProgressState state
) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;

    if(state == RM_PROGRESS_STATE_PRE_SHUTDOWN && self->elems_written) {
        fprintf(out, "\n");
    }
}

static RmFmtHandlerProgress PRETTY_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(PRETTY_HANDLER_IMPL),
        .name = "pretty",
        .head = rm_fmt_head,
        .elem = rm_fmt_elem,
        .prog = rm_fmt_prog,
        .foot = NULL
    },

    /* Initialize own stuff */
    .last_lint_type = RM_LINT_TYPE_UNKNOWN,
    .elems_written = 0
};

RmFmtHandler *PRETTY_HANDLER = (RmFmtHandler *) &PRETTY_HANDLER_IMPL;
