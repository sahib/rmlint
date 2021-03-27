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

#include <assert.h>
#include <gio/gunixoutputstream.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <string.h>

#include "../checksums/murmur3.h"
#include "../formats.h"
#include "../logger.h"
#include "../preprocess.h"
#include "../treemerge.h"
#include "../utilities.h"

typedef struct RmFmtHandlerJSON {
    /* must be first */
    RmFmtHandler parent;

    /* set of already existing ids */
    GHashTable *id_set;

    GOutputStream *stream;
    JsonGenerator *generator;
    JsonNode *root;

    bool need_comma_before_next_elem;
    bool pretty;

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

static void rm_fmt_json_sep(RmFmtHandlerJSON *self) {
    g_output_stream_printf(
        self->stream, NULL, NULL, NULL, ",%s", self->pretty ? " " : "\n");
}

static void rm_fmt_json_open(RmSession *session, RmFmtHandlerJSON *self, FILE *out) {
    self->need_comma_before_next_elem = false;
    self->stream = g_unix_output_stream_new(fileno(out), false);
    self->generator = json_generator_new();
    self->pretty = !rm_fmt_get_config_value(session->formats, "json", "oneline");
    json_generator_set_pretty(self->generator, self->pretty);

    self->root = json_node_alloc();
    json_generator_set_root(self->generator, self->root);

    self->id_set = g_hash_table_new(NULL, NULL);

    // write the start of the json array
    g_output_stream_printf(self->stream, NULL, NULL, NULL, "[\n");
}

static void rm_fmt_json_close(RmFmtHandlerJSON *self) {
    // free up memory
    json_node_unref(self->root);
    g_object_unref(self->generator);

    // write the end of the json array
    g_output_stream_printf(self->stream, NULL, NULL, NULL, "]\n");
    g_object_unref(self->stream);

    g_hash_table_unref(self->id_set);
}

static void rm_fmt_object_write_and_free(JsonObject *obj, RmFmtHandlerJSON *self) {
    if(self->need_comma_before_next_elem) {
        rm_fmt_json_sep(self);
    } else {
        self->need_comma_before_next_elem = true;
    }
    GError *error = NULL;
    json_node_set_object(self->root, obj);
    json_generator_set_root(self->generator, self->root);

    if(!json_generator_to_stream(self->generator, self->stream, false, &error)) {
        rm_log_error_line("Error writing to json stream");
    }
    json_object_unref(obj);
}

/////////////////////////
//  ACTUAL CALLBACKS   //
/////////////////////////

static void rm_fmt_head(RmSession *session, RmFmtHandler *parent, FILE *out) {
    RmFmtHandlerJSON *self = (RmFmtHandlerJSON *)parent;

    rm_fmt_json_open(session, self, out);

    if(!rm_fmt_get_config_value(session->formats, "json", "no_header")) {
        JsonObject *header = json_object_new();
        json_object_set_string_member(header, "description",
                                      "rmlint json-dump of lint files");
        json_object_set_string_member(header, "cwd", session->cfg->iwd);
        json_object_set_string_member(header, "args", session->cfg->joined_argv);
        json_object_set_string_member(header, "version", RM_VERSION);
        json_object_set_string_member(header, "rev", RM_VERSION_GIT_REVISION);
        json_object_set_int_member(header, "progress", 0); /* Header is always first. */
        json_object_set_string_member(
            header, "checksum_type",
            rm_digest_type_to_string(session->cfg->checksum_type));
        if(session->hash_seed) {
            json_object_set_int_member(header, "hash_seed", session->hash_seed);
        }
        json_object_set_boolean_member(header, "merge_directories",
                                       session->cfg->merge_directories);
        rm_fmt_object_write_and_free(header, self);
    }
}

static void rm_fmt_foot(_UNUSED RmSession *session, RmFmtHandler *parent,
                        _UNUSED FILE *out) {
    RmFmtHandlerJSON *self = (RmFmtHandlerJSON *)parent;

    if(!rm_fmt_get_config_value(session->formats, "json", "no_footer")) {
        JsonObject *footer = json_object_new();
        json_object_set_boolean_member(footer, "aborted", rm_session_was_aborted());
        json_object_set_int_member(footer, "progress", 100); /* Footer is always last. */
        json_object_set_int_member(footer, "total_files", session->total_files);
        json_object_set_int_member(footer, "ignored_files", session->ignored_files);
        json_object_set_int_member(footer, "ignored_folders", session->ignored_folders);
        json_object_set_int_member(footer, "duplicates", session->dup_counter);
        json_object_set_int_member(footer, "duplicate_sets", session->dup_group_counter);
        json_object_set_int_member(footer, "total_lint_size", session->total_lint_size);
        rm_fmt_object_write_and_free(footer, self);
    }
    rm_fmt_json_close(self);
}

static char *rm_fmt_json_cksum(RmFile *file) {
    if(file->digest == NULL) {
        return NULL;
    }
    size_t checksum_size = rm_digest_get_bytes(file->digest) * 2 + 1;
    char *checksum_str = g_malloc0(checksum_size);
    rm_digest_hexstring(file->digest, checksum_str);
    checksum_str[checksum_size - 1] = 0;
    return checksum_str;
}

static void rm_fmt_elem(RmSession *session, _UNUSED RmFmtHandler *parent,
                        _UNUSED FILE *out, RmFile *file) {
    if(rm_fmt_get_config_value(session->formats, "json", "no_body")) {
        return;
    }

    if(file->lint_type == RM_LINT_TYPE_UNIQUE_FILE) {
        if(!rm_fmt_get_config_value(session->formats, "json", "unique")) {
            if(!file->digest ||
                !(session->cfg->hash_uniques || session->cfg->hash_unmatched)) {
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
    char *checksum_str = rm_fmt_json_cksum(file);

    RmFmtHandlerJSON *self = (RmFmtHandlerJSON *)parent;

    RM_DEFINE_PATH(file);

    JsonObject *elem = json_object_new();
    json_object_set_int_member(
        elem, "id", rm_fmt_json_generate_id(self, file, file_path, checksum_str));
    json_object_set_string_member(elem, "type",
                                  rm_file_lint_type_to_string(file->lint_type));
    gdouble progress = 0;
    if(session->shred_bytes_after_preprocess) {
        progress = CLAMP(100 - 100 * ((gdouble)session->shred_bytes_remaining /
                                      (gdouble)session->shred_bytes_after_preprocess),
                         0,
                         100);
        json_object_set_int_member(elem, "progress", progress);
    }

    if(file->digest) {
        json_object_set_string_member(elem, "checksum", checksum_str);
    }

    json_object_set_string_member(elem, "path", file_path);
    json_object_set_int_member(elem, "size", file->actual_file_size);
    json_object_set_int_member(elem, "depth", file->depth);
    json_object_set_int_member(elem, "inode", file->inode);
    json_object_set_int_member(elem, "disk_id", file->dev);
    json_object_set_boolean_member(elem, "is_original", file->is_original);

    if(file->lint_type == RM_LINT_TYPE_DUPE_DIR_CANDIDATE) {
        json_object_set_int_member(elem, "n_children", file->n_children);
    }

    if(file->lint_type != RM_LINT_TYPE_UNIQUE_FILE) {
        if(file->twin_count >= 0) {
            json_object_set_int_member(elem, "twins", file->twin_count);
        }

        if(file->lint_type == RM_LINT_TYPE_PART_OF_DIRECTORY && file->parent_dir) {
            json_object_set_string_member(elem, "parent_path",
                                          rm_directory_get_dirname(file->parent_dir));
        }

        if(session->cfg->find_hardlinked_dupes) {
            RmFile *hardlink_head = RM_FILE_HARDLINK_HEAD(file);

            if(hardlink_head && hardlink_head != file && file->digest) {
                char *orig_checksum_str = rm_fmt_json_cksum(hardlink_head);
                RM_DEFINE_PATH(hardlink_head);
                guint32 orig_id = rm_fmt_json_generate_id(
                    self, hardlink_head, hardlink_head_path, orig_checksum_str);
                g_free(orig_checksum_str);
                json_object_set_int_member(elem, "hardlink_of", orig_id);
            }
        }
    }

    json_object_set_double_member(elem, "mtime", file->mtime);

    rm_fmt_object_write_and_free(elem, self);
    g_free(checksum_str);
}

static RmFmtHandlerJSON JSON_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(JSON_HANDLER_IMPL),
        .name = "json",
        .head = rm_fmt_head,
        .elem = rm_fmt_elem,
        .prog = NULL,
        .foot = rm_fmt_foot,
        .valid_keys = {"no_header", "no_footer", "no_body", "oneline", "unique", NULL},
    }};

RmFmtHandler *JSON_HANDLER = (RmFmtHandler *)&JSON_HANDLER_IMPL;
