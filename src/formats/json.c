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

typedef struct RmFmtHandlerJSON {
    /* must be first */
    RmFmtHandler parent;
} RmFmtHandlerProgress;

//////////////////////////////////////////
//  POOR MAN'S JSON FORMATTING TOOLBOX  //
//////////////////////////////////////////

static void rm_fmt_json_key(FILE *out, const char *key, const char *value) {
    fprintf(out, "  \"%s\": \"%s\"", key, value);
}

static void rm_fmt_json_key_bool(FILE *out, const char *key, bool value) {
    fprintf(out, "  \"%s\": %s", key, value ? "true" : "false");
}

static void rm_fmt_json_key_int(FILE *out, const char *key, RmOff value) {
    fprintf(out, "  \"%s\": %"LLU"", key, value);
}

static void rm_fmt_json_key_unsafe(FILE *out, const char *key, const char *value) {
    char *escaped_value = rm_util_strsub(value, "\"", "\\\"");
    fprintf(out, "  \"%s\": \"%s\"", key, escaped_value);
    g_free(escaped_value);
}

static void rm_fmt_json_open(FILE *out) {
    fprintf(out, "{\n");
}

static void rm_fmt_json_close(FILE *out) {
    fprintf(out, "\n},\n");
}

static void rm_fmt_json_sep(FILE *out) {
    fprintf(out, ",\n");
}

/////////////////////////
//  ACTUAL CALLBACKS   //
/////////////////////////

static void rm_fmt_head(RmSession *session, _U RmFmtHandler *parent, FILE *out) {
    fprintf(out, "[");

    if(!rm_fmt_get_config_value(session->formats, "json", "no_header")) {
        rm_fmt_json_open(out);
        {
            rm_fmt_json_key(out, "description", "rmlint json-dump of lint files");
            rm_fmt_json_sep(out);
            rm_fmt_json_key(out, "cwd", session->settings->iwd);
            rm_fmt_json_sep(out);
            rm_fmt_json_key(out, "args", session->settings->joined_argv);
        }
        rm_fmt_json_close(out);
    }
}

static void rm_fmt_foot(_U RmSession *session, _U RmFmtHandler *parent, FILE *out) {
    if(rm_fmt_get_config_value(session->formats, "json", "no_footer")) {
        fprintf(out, "{\n}");
    } else {
        rm_fmt_json_open(out);
        {
            rm_fmt_json_key_bool(out, "aborted", rm_session_was_aborted(session));
            rm_fmt_json_sep(out);
            rm_fmt_json_key_int(out, "total_files", session->total_files);
            rm_fmt_json_sep(out);
            rm_fmt_json_key_int(out, "ignored_files", session->ignored_files);
            rm_fmt_json_sep(out);
            rm_fmt_json_key_int(out, "ignored_folders", session->ignored_folders);
            rm_fmt_json_sep(out);
            rm_fmt_json_key_int(out, "duplicates", session->dup_counter);
            rm_fmt_json_sep(out);
            rm_fmt_json_key_int(out, "duplicate_sets", session->dup_group_counter);
            rm_fmt_json_sep(out);
            rm_fmt_json_key_int(out, "total_lint_size", session->total_lint_size);
        }
        fprintf(out, "\n}");
    }

    fprintf(out, "\n]\n");
}
static void rm_fmt_elem(
    _U RmSession *session,
    _U RmFmtHandler *parent,
    FILE *out, RmFile *file
) {
    bool has_checksum = false;
    char checksum_str[rm_digest_get_bytes(file->digest) * 2 + 1];
    memset(checksum_str, '0', sizeof(checksum_str));
    checksum_str[sizeof(checksum_str) - 1] = 0;

    if(file->digest && file->digest->type != RM_DIGEST_PARANOID) {
        has_checksum = true;
        rm_digest_hexstring(file->digest, checksum_str);
    }

    /* Make it look like a json element */
    rm_fmt_json_open(out);
    {
        rm_fmt_json_key(out, "type", rm_file_lint_type_to_string(file->lint_type));
        rm_fmt_json_sep(out);
        if(has_checksum) {
            rm_fmt_json_key(out, "checksum", checksum_str);
            rm_fmt_json_sep(out);
        }
        rm_fmt_json_key_unsafe(out, "path", file->path);
        rm_fmt_json_sep(out);
        rm_fmt_json_key_int(out, "size", file->file_size);
        rm_fmt_json_sep(out);
        rm_fmt_json_key_int(out, "inode", file->inode);
        rm_fmt_json_sep(out);
        rm_fmt_json_key_int(out, "disk_id", file->dev);
        rm_fmt_json_sep(out);
        rm_fmt_json_key(out, "is_prefd", file->is_prefd ? "true" : "false");
        rm_fmt_json_sep(out);
        rm_fmt_json_key_int(out, "mtime", file->mtime);
    }
    rm_fmt_json_close(out);
}

static RmFmtHandlerProgress JSON_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(JSON_HANDLER_IMPL),
        .name = "json",
        .head = rm_fmt_head,
        .elem = rm_fmt_elem,
        .prog = NULL,
        .foot = rm_fmt_foot
    }
};

RmFmtHandler *JSON_HANDLER = (RmFmtHandler *) &JSON_HANDLER_IMPL;
