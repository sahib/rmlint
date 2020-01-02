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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include "../formats.h"
#include "../utilities.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#define CSV_SEP ","
#define CSV_QUOTE "\""
#define CSV_FORMAT \
    "%s" CSV_SEP "" CSV_QUOTE "%s" CSV_QUOTE "" CSV_SEP "%" LLU "" CSV_SEP "%s\n"

typedef struct RmFmtHandlerCSV {
    /* must be first */
    RmFmtHandler parent;
} RmFmtHandlerProgress;

static void rm_fmt_head(_UNUSED RmSession *session, _UNUSED RmFmtHandler *parent,
                        FILE *out) {
    if(rm_fmt_get_config_value(session->formats, "csv", "no_header")) {
        return;
    }

    fprintf(
        out,
        "%s%s%s%s%s%s%s\n",
        "type", CSV_SEP,
        "path", CSV_SEP,
        "size", CSV_SEP,
        "checksum"
    );
}

static void rm_fmt_elem(_UNUSED RmSession *session, _UNUSED RmFmtHandler *parent,
                        FILE *out, RmFile *file) {
    if(file->lint_type == RM_LINT_TYPE_UNIQUE_FILE) {
        if(!rm_fmt_get_config_value(session->formats, "csv", "unique")) {
            if(!file->digest || !session->cfg->write_unfinished) {
                return;
            }
        }
    }

    if(file->lint_type == RM_LINT_TYPE_PART_OF_DIRECTORY) {
        return;
    }

    char *checksum_str = NULL;
    size_t checksum_size = 0;

    if(file->digest != NULL) {
        checksum_size = rm_digest_get_bytes(file->digest) * 2 + 1;
        checksum_str = g_slice_alloc0(checksum_size);
        checksum_str[checksum_size - 1] = 0;
        rm_digest_hexstring(file->digest, checksum_str);
    } else {
        checksum_str = g_slice_alloc0(1);
        *checksum_str = 0;
    }

    /* Escape quotes in the path (refer http://tools.ietf.org/html/rfc4180, item 6)*/
    RM_DEFINE_PATH(file);
    char *clean_path = rm_util_strsub(file_path, CSV_QUOTE, CSV_QUOTE "" CSV_QUOTE);

    fprintf(
        out,
        CSV_FORMAT,
        rm_file_lint_type_to_string(file->lint_type),
        clean_path,
        file->actual_file_size,
        checksum_str
    );

    g_free(clean_path);

    if(checksum_str != NULL) {
        g_slice_free1(checksum_size, checksum_str);
    }
}

static RmFmtHandlerProgress CSV_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(CSV_HANDLER_IMPL),
        .name = "csv",
        .head = rm_fmt_head,
        .elem = rm_fmt_elem,
        .prog = NULL,
        .foot = NULL,
        .valid_keys = {"no_header", "unique", NULL},
    }};

RmFmtHandler *CSV_HANDLER = (RmFmtHandler *)&CSV_HANDLER_IMPL;
