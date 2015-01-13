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
#include "../preprocess.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#if  HAVE_JSON_GLIB
#  include <json-glib/json-glib.h>
#endif

typedef struct RmFmtHandlerJSON {
    /* must be first */
    RmFmtHandler parent;
} RmFmtHandlerJSON;

//////////////////////////////////////////
//  POOR MAN'S JSON FORMATTING TOOLBOX  //
//////////////////////////////////////////

static void rm_fmt_json_key(FILE *out, const char *key, const char *value) {
    fprintf(out, " \"%s\": \"%s\"", key, value);
}

static void rm_fmt_json_key_bool(FILE *out, const char *key, bool value) {
    fprintf(out, " \"%s\": %s", key, value ? "true" : "false");
}

static void rm_fmt_json_key_int(FILE *out, const char *key, RmOff value) {
    fprintf(out, " \"%s\": %"LLU"", key, value);
}

static int rm_fmt_json_fix(const char *string, char *fixed, size_t fixed_len) {
    if(!g_utf8_validate(string, -1, NULL)) {
        return -1;
    }

    /* More information here:
     *
     * http://stackoverflow.com/questions/4901133/json-and-escaping-characters/4908960#4908960
     */

    int n = g_utf8_strlen(string, -1);
    int max = fixed_len;

    for(int i = 0; i < n && max; ++i) {
        char *off = g_utf8_offset_to_pointer(string, i);
        char *text = NULL;

        switch(g_utf8_get_char(off)) {
        case '\\':
            text = "\\\\";
            break;
        case '\"':
            text = "\\\"";
            break;
        case '\b':
            text = "\\b";
            break;
        case '\f':
            text = "\\f";
            break;
        case '\n':
            text = "\\n";
            break;
        case '\r':
            text = "\\r";
            break;
        case '\t':
            text = "\\t";
            break;
        default:
            g_utf8_strncpy(fixed, off, 1);

            char *new_fixed = g_utf8_find_next_char(fixed, NULL);
            max -= (new_fixed - fixed);
            fixed = new_fixed;
            break;
        }

        while(text && *text) {
            *fixed++ = *text++;
            max--;
        }
    }

    return fixed_len - max;
}

static void rm_fmt_json_key_unsafe(FILE *out, const char *key, const char *value) {
    char safe_value[PATH_MAX + 4 + 1];
    memset(safe_value, 0, sizeof(safe_value));

    if(rm_fmt_json_fix(value, safe_value, sizeof(safe_value)) >= 0) {
        fprintf(out, " \"%s\": \"%s\"", key, safe_value);
    }
}

static void rm_fmt_json_open(FILE *out) {
    fprintf(out, "{\n");
}

static void rm_fmt_json_close(FILE *out) {
    fprintf(out, "\n}, ");
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
            rm_fmt_json_key(out, "cwd", session->cfg->iwd);
            rm_fmt_json_sep(out);
            rm_fmt_json_key(out, "args", session->cfg->joined_argv);
            if(session->hash_seed1 && session->hash_seed2) {
                rm_fmt_json_sep(out);
                rm_fmt_json_key_int(out, "hash_seed1", session->hash_seed1);
                rm_fmt_json_sep(out);
                rm_fmt_json_key_int(out, "hash_seed2", session->hash_seed2);
            }
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

    fprintf(out, "]\n");
}

static void rm_fmt_elem(
    _U RmSession *session,
    _U RmFmtHandler *parent,
    FILE *out, RmFile *file
) {
    char checksum_str[rm_digest_get_bytes(file->digest) * 2 + 1];
    memset(checksum_str, '0', sizeof(checksum_str));
    checksum_str[sizeof(checksum_str) - 1] = 0;
    rm_digest_hexstring(file->digest, checksum_str);

    /* Make it look like a json element */
    rm_fmt_json_open(out);
    {
        rm_fmt_json_key(out, "type", rm_file_lint_type_to_string(file->lint_type));
        rm_fmt_json_sep(out);

        if(file->digest) {
            rm_fmt_json_key(out, "checksum", checksum_str);
            rm_fmt_json_sep(out);
        }

        rm_fmt_json_key_unsafe(out, "path", file->path);
        rm_fmt_json_sep(out);
        if(file->lint_type != RM_LINT_TYPE_UNFINISHED_CKSUM) {
            rm_fmt_json_key_int(out, "size", file->file_size);
            rm_fmt_json_sep(out);
            rm_fmt_json_key_int(out, "inode", file->inode);
            rm_fmt_json_sep(out);
            rm_fmt_json_key_int(out, "disk_id", file->dev);
            rm_fmt_json_sep(out);
            rm_fmt_json_key_bool(out, "is_original", file->is_original);
            rm_fmt_json_sep(out);
        }
        rm_fmt_json_key_int(out, "mtime", file->mtime);
    }
    rm_fmt_json_close(out);
}

static RmFmtHandlerJSON JSON_HANDLER_IMPL = {
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
