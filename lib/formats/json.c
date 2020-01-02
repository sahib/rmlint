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

#include "../checksums/murmur3.h"
#include "../formats.h"
#include "../preprocess.h"
#include "../utilities.h"
#include "../treemerge.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

typedef struct RmFmtHandlerJSON {
    /* must be first */
    RmFmtHandler parent;

    /* More human readable output? */
    bool pretty;

    /* set of already existing ids */
    GHashTable *id_set;
} RmFmtHandlerJSON;

//////////////////////////////////////////
//          FILE ID GENERATOR           //
//////////////////////////////////////////

static guint32 rm_fmt_json_generate_id(RmFmtHandlerJSON *self, RmFile *file,
                                       const char *file_path, char *cksum) {
    guint32 hash = 0;
    hash = file->inode ^ file->dev;
    hash ^= file->actual_file_size;

    for(int i = 0; i < 8192; ++i) {
        hash ^= MurmurHash3_x86_32(file_path, strlen(file_path), i);
        if(cksum != NULL) {
            hash ^= MurmurHash3_x86_32(cksum, strlen(cksum), i);
        }

        if(!g_hash_table_contains(self->id_set, GUINT_TO_POINTER(hash))) {
            break;
        }
    }

    g_hash_table_add(self->id_set, GUINT_TO_POINTER(hash));
    return hash;
}

//////////////////////////////////////////
//  POOR MAN'S JSON FORMATTING TOOLBOX  //
//////////////////////////////////////////

static void rm_fmt_json_key(FILE *out, const char *key, const char *value) {
    fprintf(out, "\"%s\": \"%s\"", key, value);
}

static void rm_fmt_json_key_bool(FILE *out, const char *key, bool value) {
    fprintf(out, "\"%s\": %s", key, value ? "true" : "false");
}

static void rm_fmt_json_key_int(FILE *out, const char *key, RmOff value) {
    fprintf(out, "\"%s\": %" LLU "", key, value);
}

static void rm_fmt_json_key_float(FILE *out, const char *key, gdouble value) {
    // Make sure that the floating point number gets printed with a '.',
    // not with a comma as usual in e.g. the german language.
    gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
    fprintf(out, "\"%s\": %s", key, g_ascii_dtostr(buf, sizeof(buf) - 1, value));
}

static bool rm_fmt_json_fix(const char *string, char *fixed, size_t fixed_len) {
    /* More information here:
     *
     * http://stackoverflow.com/questions/4901133/json-and-escaping-characters/4908960#4908960
     */

    int n = strlen(string);
    char *safe_iter = fixed;

    for(int i = 0; i < n && (size_t)(safe_iter - fixed) < fixed_len; ++i) {
        unsigned char *curr = (unsigned char *)&string[i];

        char text[20];
        memset(text, 0, sizeof(text));

        if(*curr == '"' || *curr == '\\') {
            /* Printable, but needs to be escaped */
            text[0] = '\\';
            text[1] = *curr;
        } else if((*curr > 0 && *curr < 0x1f) || *curr == 0x7f) {
            /* Something unprintable */
            switch(*curr) {
            case '\b':
                g_snprintf(text, sizeof(text), "\\b");
                break;
            case '\f':
                g_snprintf(text, sizeof(text), "\\f");
                break;
            case '\n':
                g_snprintf(text, sizeof(text), "\\n");
                break;
            case '\r':
                g_snprintf(text, sizeof(text), "\\r");
                break;
            case '\t':
                g_snprintf(text, sizeof(text), "\\t");
                break;
            default:
                g_snprintf(text, sizeof(text), "\\u00%02x", (guint)*curr);
                break;
            }
        } else {
            /* Take it unmodified */
            text[0] = *curr;
        }

        safe_iter = g_stpcpy(safe_iter, text);
    }

    return (size_t)(safe_iter - fixed) < fixed_len;
}

static void rm_fmt_json_key_unsafe(FILE *out, const char *key, const char *value) {
    char safe_value[PATH_MAX + 4 + 1];
    memset(safe_value, 0, sizeof(safe_value));

    if(rm_fmt_json_fix(value, safe_value, sizeof(safe_value))) {
        fprintf(out, "\"%s\": \"%s\"", key, safe_value);
    } else {
        /* This should never happen but give at least means of debugging */
        fprintf(out, "\"%s\": \"<BROKEN PATH>\"", key);
    }
}

static void rm_fmt_json_open(RmFmtHandlerJSON *self, FILE *out) {
    fprintf(out, "{%s", self->pretty ? "\n  " : "");
}

static void rm_fmt_json_close(RmFmtHandlerJSON *self, FILE *out) {
    if(self->pretty) {
        fprintf(out, "\n}, ");
    } else {
        fprintf(out, "},\n");
    }
}

static void rm_fmt_json_sep(RmFmtHandlerJSON *self, FILE *out) {
    fprintf(out, ",%s", self->pretty ? "\n  " : "");
}

/////////////////////////
//  ACTUAL CALLBACKS   //
/////////////////////////

static void rm_fmt_head(RmSession *session, _UNUSED RmFmtHandler *parent, FILE *out) {
    fprintf(out, "[\n");

    RmFmtHandlerJSON *self = (RmFmtHandlerJSON *)parent;
    self->id_set = g_hash_table_new(NULL, NULL);

    if(rm_fmt_get_config_value(session->formats, "json", "oneline")) {
        self->pretty = false;
    }

    if(!rm_fmt_get_config_value(session->formats, "json", "no_header")) {
        rm_fmt_json_open(self, out);
        {
            rm_fmt_json_key(out, "description", "rmlint json-dump of lint files");
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key(out, "cwd", session->cfg->iwd);
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key(out, "args", session->cfg->joined_argv);
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key(out, "version", RM_VERSION);
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key(out, "rev", RM_VERSION_GIT_REVISION);
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key_int(out, "progress", 0); /* Header is always first. */
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key(out, "checksum_type",
                            rm_digest_type_to_string(session->cfg->checksum_type));
            if(session->hash_seed) {
                rm_fmt_json_sep(self, out);
                rm_fmt_json_key_int(out, "hash_seed", session->hash_seed);
            }

            rm_fmt_json_sep(self, out);
            rm_fmt_json_key_bool(out, "merge_directories", session->cfg->merge_directories);
        }
        rm_fmt_json_close(self, out);
    }
}

static void rm_fmt_foot(_UNUSED RmSession *session, RmFmtHandler *parent, FILE *out) {
    RmFmtHandlerJSON *self = (RmFmtHandlerJSON *)parent;

    if(rm_fmt_get_config_value(session->formats, "json", "no_footer")) {
        fprintf(out, "{}");
    } else {
        rm_fmt_json_open(self, out);
        {
            rm_fmt_json_key_bool(out, "aborted", rm_session_was_aborted());
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key_int(out, "progress", 100); /* Footer is always last. */
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key_int(out, "total_files", session->total_files);
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key_int(out, "ignored_files", session->ignored_files);
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key_int(out, "ignored_folders", session->ignored_folders);
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key_int(out, "duplicates", session->dup_counter);
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key_int(out, "duplicate_sets", session->dup_group_counter);
            rm_fmt_json_sep(self, out);
            rm_fmt_json_key_int(out, "total_lint_size", session->total_lint_size);
        }
        if(self->pretty) {
            fprintf(out, "\n}");
        } else {
            fprintf(out, "}\n");
        }
    }

    fprintf(out, "]\n");
    g_hash_table_unref(self->id_set);
}

static void rm_fmt_json_cksum(RmFile *file, char *checksum_str, size_t size) {
    memset(checksum_str, '0', size);
    checksum_str[size - 1] = 0;
    rm_digest_hexstring(file->digest, checksum_str);
}

static void rm_fmt_elem(RmSession *session, _UNUSED RmFmtHandler *parent, FILE *out, RmFile *file) {
    if(rm_fmt_get_config_value(session->formats, "json", "no_body")) {
        return;
    }

    if(file->lint_type == RM_LINT_TYPE_UNIQUE_FILE) {
        if(!rm_fmt_get_config_value(session->formats, "json", "unique")) {
            if(!file->digest || !session->cfg->write_unfinished) {
                return;
            }
        }

        if(session->cfg->keep_all_tagged && !file->is_prefd) {
            /* don't list 'untagged' files as unique */
            file->is_original = false;
        } else if(session->cfg->keep_all_untagged && file->is_prefd) {
            /* don't list 'tagged' files as unique */
            file->is_original = false;
        } else {
            file->is_original = true;
        }
    }

    char *checksum_str = NULL;
    size_t checksum_size = 0;

    if(file->digest != NULL) {
        checksum_size = rm_digest_get_bytes(file->digest) * 2 + 1;
        checksum_str = g_slice_alloc0(checksum_size);
        rm_fmt_json_cksum(file, checksum_str, checksum_size);
        checksum_str[checksum_size - 1] = 0;
    }

    RmFmtHandlerJSON *self = (RmFmtHandlerJSON *)parent;

    /* Make it look like a json element */
    rm_fmt_json_open(self, out);
    {
        RM_DEFINE_PATH(file);

        rm_fmt_json_key_int(out, "id",
                            rm_fmt_json_generate_id(self, file, file_path, checksum_str));
        rm_fmt_json_sep(self, out);
        rm_fmt_json_key(out, "type", rm_file_lint_type_to_string(file->lint_type));
        rm_fmt_json_sep(self, out);

        gdouble progress = 0;
        if(session->shred_bytes_after_preprocess) {
            progress = CLAMP(
                100 - 100 * (
                    (gdouble)session->shred_bytes_remaining /
                    (gdouble)session->shred_bytes_after_preprocess
                ),
                0,
                100
            );
        }
        rm_fmt_json_key_int(out, "progress", progress);
        rm_fmt_json_sep(self, out);

        if(file->digest) {
            rm_fmt_json_key(out, "checksum", checksum_str);
            rm_fmt_json_sep(self, out);
        }

        rm_fmt_json_key_unsafe(out, "path", file_path);
        rm_fmt_json_sep(self, out);
        rm_fmt_json_key_int(out, "size", file->actual_file_size);
        rm_fmt_json_sep(self, out);
        rm_fmt_json_key_int(out, "depth", file->depth);
        rm_fmt_json_sep(self, out);
        rm_fmt_json_key_int(out, "inode", file->inode);
        rm_fmt_json_sep(self, out);
        rm_fmt_json_key_int(out, "disk_id", file->dev);
        rm_fmt_json_sep(self, out);
        rm_fmt_json_key_bool(out, "is_original", file->is_original);
        rm_fmt_json_sep(self, out);

        if(file->lint_type != RM_LINT_TYPE_UNIQUE_FILE) {
            if(file->twin_count >= 0) {
                rm_fmt_json_key_int(out, "twins", file->twin_count);
                rm_fmt_json_sep(self, out);
            }


			if(file->lint_type == RM_LINT_TYPE_PART_OF_DIRECTORY && file->parent_dir) {
				rm_fmt_json_key(out, "parent_path", rm_directory_get_dirname(file->parent_dir));
				rm_fmt_json_sep(self, out);

			}

            if(session->cfg->find_hardlinked_dupes) {
                RmFile *hardlink_head = RM_FILE_HARDLINK_HEAD(file);

                if(hardlink_head && hardlink_head != file && file->digest) {
                    char orig_checksum_str[rm_digest_get_bytes(file->digest) * 2 + 1];
                    rm_fmt_json_cksum(hardlink_head, orig_checksum_str,
                                      sizeof(orig_checksum_str));

                    RM_DEFINE_PATH(hardlink_head);

                    guint32 orig_id = rm_fmt_json_generate_id(
                        self, hardlink_head, hardlink_head_path, orig_checksum_str);

                    rm_fmt_json_key_int(out, "hardlink_of", orig_id);
                    rm_fmt_json_sep(self, out);
                }
            }
        }

        rm_fmt_json_key_float(out, "mtime", file->mtime);
    }
    rm_fmt_json_close(self, out);

    if(checksum_str != NULL) {
        g_slice_free1(checksum_size, checksum_str);
    }
}

static RmFmtHandlerJSON JSON_HANDLER_IMPL = {
    /* Initialize parent */
    .parent =
        {
            .size = sizeof(JSON_HANDLER_IMPL),
            .name = "json",
            .head = rm_fmt_head,
            .elem = rm_fmt_elem,
            .prog = NULL,
            .foot = rm_fmt_foot,
            .valid_keys = {"no_header", "no_footer", "no_body", "oneline", "unique", NULL},
        },
    .pretty = true};

RmFmtHandler *JSON_HANDLER = (RmFmtHandler *)&JSON_HANDLER_IMPL;
