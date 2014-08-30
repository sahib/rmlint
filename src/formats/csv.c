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
#include "../utilities.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#define CSV_SEP  ","
#define CSV_FORMAT "%s"CSV_SEP"%s"CSV_SEP"%lu"CSV_SEP"%s\n"

static const char *LINT_TYPE_TO_COLUMN[] = {
    [RM_LINT_TYPE_UNKNOWN]      = "",
    [RM_LINT_TYPE_EDIR]         = "emptydir",
    [RM_LINT_TYPE_NBIN]         = "nonstripped",
    [RM_LINT_TYPE_BLNK]         = "badlink",
    [RM_LINT_TYPE_BADUID]       = "baduid",
    [RM_LINT_TYPE_BADGID]       = "badgid",
    [RM_LINT_TYPE_BADUGID]      = "badugid",
    [RM_LINT_TYPE_EFILE]        = "emptyfile",
    [RM_LINT_TYPE_DUPE_CANDIDATE] = "duplicate"
};

typedef struct RmFmtHandlerCSV {
    /* must be first */
    RmFmtHandler parent;
} RmFmtHandlerProgress;

static void rm_fmt_head(G_GNUC_UNUSED RmSession *session, G_GNUC_UNUSED RmFmtHandler *parent, FILE *out) {
    if(rm_fmt_get_config_value(session->formats, "csv", "no_header")) {
        return;
    }

    fprintf(out, "%s%s%s%s%s%s%s\n",
            "type", CSV_SEP, "path", CSV_SEP, "size", CSV_SEP, "checksum"
           );
}

static void rm_fmt_elem(
    G_GNUC_UNUSED RmSession *session,
    G_GNUC_UNUSED RmFmtHandler *parent,
    FILE *out, RmFile *file
) {
    char checksum_str[_RM_HASH_LEN * 2 + 1];
    memset(checksum_str, 0, sizeof(checksum_str));
    rm_digest_hexstring(&file->digest, checksum_str);

    /* Escape any possible separator character in the path */
    char *clean_path = rm_util_strsub(file->path, CSV_SEP, "\\"CSV_SEP);

    fprintf(out, CSV_FORMAT,
            LINT_TYPE_TO_COLUMN[file->lint_type], clean_path, file->file_size, checksum_str
           );

    g_free(clean_path);
}

static RmFmtHandlerProgress CSV_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(CSV_HANDLER_IMPL),
        .name = "csv",
        .head = rm_fmt_head,
        .elem = rm_fmt_elem,
        .prog = NULL,
        .foot = NULL
    }
};

RmFmtHandler *CSV_HANDLER = (RmFmtHandler *) &CSV_HANDLER_IMPL;
