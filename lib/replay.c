/**
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
 *  - Christopher <sahib> Pahl 2010-2015 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2015 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

/* Internal headers */
#include "config.h"
#include "session.h"
#include "formats.h"
#include "file.h"

/* External libraries */
#include <string.h>
#include <glib.h>

#if HAVE_JSON_GLIB
#include <json-glib/json-glib.h>
#endif

//////////////////////////////////////
//        JSON REPLAY PARSER        //
//////////////////////////////////////

typedef struct RmParrot {
    RmSession *session;
    JsonParser *parser;
    JsonArray *root;
    guint index;
} RmParrot;

void rm_parrot_close(RmParrot *self) {
    if(self->parser) {
        g_object_unref(self->parser);
    }

    g_free(self);
}

RmParrot *rm_parrot_open(RmSession *session, const char *json_path, GError **error) {
    RmParrot *self = g_malloc0(sizeof(RmParrot));
    self->session = session;
    self->parser = json_parser_new();
    self->index = 0;

    // TODO: translate
    rm_log_info_line(_("Loading json-results `%s'"), json_path);

    if(!json_parser_load_from_file(self->parser, json_path, error)) {
        goto failure;
    }

    JsonNode *root = json_parser_get_root(self->parser);
    if(JSON_NODE_TYPE(root) != JSON_NODE_ARRAY) {
        rm_log_warning_line(_("No valid json cache (no array in /)"));
        goto failure;
    }

    self->root = json_node_get_array(root);
    return self;

failure:
    rm_parrot_close(self);
    return NULL;
}

bool rm_parrot_has_next(RmParrot *self) {
    return self->index < json_array_get_length(self->root);
}

RmFile *rm_parrot_next(RmParrot *self) {
    if(!rm_parrot_has_next(self)) {
        return NULL;
    }

    JsonObject *object = json_array_get_object_element(self->root, self->index);

    self->index += 1;

    JsonNode *path_node = json_object_get_member(object, "path");
    if(path_node == NULL) {
        return NULL;
    }

    const char *path = json_node_get_string(path_node);
    size_t path_len = strlen(path);

    RmLintType type = rm_file_string_to_lint_type(
        json_object_get_string_member(object, "type")
    );

    if(type == RM_LINT_TYPE_UNKNOWN) {
        rm_log_warning_line("... not a type: `%s`", json_object_get_string_member(object, "type"));
        return NULL;
    }

    RmStat stat_buf;
    if(rm_sys_stat(path, &stat_buf) == -1) {
        return NULL;
    }

    if(json_object_get_int_member(object, "mtime") < stat_buf.st_mtim.tv_sec) {
        rm_log_warning_line("modification time of `%s` changed. Ignoring.", path);
        return NULL;
    }

    RmFile *file = rm_file_new(self->session, path, path_len, &stat_buf, type, 0, 0);
    file->is_original = json_object_get_boolean_member(object, "is_original");
    file->digest = rm_digest_new(RM_DIGEST_EXT, 0, 0, 0, 0);

    JsonNode *cksum_node = json_object_get_member(object, "checksum");
    if(cksum_node != NULL) {
        const char *cksum = json_object_get_string_member(object, "checksum");
        if(cksum != NULL) {
            rm_digest_update(file->digest, (unsigned char *)cksum, strlen(cksum));
        }
    }

    return file;
}

bool rm_parrot_load(RmSession *session, const char *json_path) {
    GError *error = NULL;
    RmParrot *polly = rm_parrot_open(session, json_path, &error);
    if(polly == NULL) {
        if(error != NULL) {
            rm_log_warning_line("Error was: %s", error->message);
            g_error_free(error);
        }
        return false; 
    }

    RmCfg *cfg = session->cfg;

    while(rm_parrot_has_next(polly)) {
        RmFile *file = rm_parrot_next(polly);
        if(file == NULL) {
            /* Something went wrong during parse. */
            continue;
        }

        if(cfg->limits_specified &&
            ((cfg->minsize != (RmOff)-1 && cfg->minsize > file->file_size) ||
            (cfg->maxsize != (RmOff)-1 && file->file_size > cfg->maxsize))) {

            rm_file_destroy(file);
            continue;
        }

        // TODO: Checks for perms, types etc.

        session->total_files += 1;

        if(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE) {
            session->dup_group_counter += file->is_original;
            if(!file->is_original) {
                session->dup_counter += 1;
                session->total_lint_size += file->file_size;
            }
        } else {
            session->other_lint_cnt += 1;
        }

        rm_fmt_write(file, session->formats);
    }

    rm_parrot_close(polly);
    return true;
}
