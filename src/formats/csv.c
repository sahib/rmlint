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

typedef struct RmFmtHandlerCSV {
    /* must be first */
    RmFmtHandler parent;
} RmFmtHandlerProgress;

static void rm_fmt_head(G_GNUC_UNUSED RmSession *session, G_GNUC_UNUSED RmFmtHandler *parent, FILE *out) {
}

static void rm_fmt_elem(G_GNUC_UNUSED RmSession *session, RmFmtHandler *parent, FILE *out, G_GNUC_UNUSED RmFile *file) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;
    const char *separator = "//";

    char checksum_str[_RM_HASH_LEN * 2 + 1];
    rm_digest_hexstring(&file->digest, checksum_str);

    fprintf(out, "%s%s%lu%s%s%s\n",
        file->path, separator,
        file->file_size, separator,
        checksum_str, separator
    );
}

static void rm_fmt_prog(
    G_GNUC_UNUSED RmSession *session,
    RmFmtHandler *parent,
    G_GNUC_UNUSED FILE *out,
    RmFmtProgressState state,
    guint64 n, guint64 N
) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;
}

static void rm_fmt_foot(G_GNUC_UNUSED RmSession *session, G_GNUC_UNUSED RmFmtHandler *parent, FILE *out) {
}

static RmFmtHandlerProgress CSV_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .name = "csv",
        .head = rm_fmt_head,
        .elem = rm_fmt_elem,
        .prog = rm_fmt_prog,
        .foot = rm_fmt_foot
    }
};

RmFmtHandler * CSV_HANDLER = (RmFmtHandler *) &CSV_HANDLER_IMPL;
