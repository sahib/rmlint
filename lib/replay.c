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
#include "shredder.h"

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
    self->index = 1;

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
        // TODO: translate:
        rm_log_warning_line(
            "... not a valid type: `%s`",
            json_object_get_string_member(object, "type")
        );
        return NULL;
    }

    RmStat lstat_buf, stat_buf;
    RmStat *stat_info = &lstat_buf;

    if(rm_sys_lstat(path, &lstat_buf) == -1) {
        return NULL;
    }

    bool is_symlink = (lstat_buf.st_mode & S_IFLNK);

    if(rm_sys_stat(path, &stat_buf) != -1) {
        stat_info = &stat_buf;
    }

    if(json_object_get_int_member(object, "mtime") < stat_info->st_mtim.tv_sec) {
        rm_log_warning_line("modification time of `%s` changed. Ignoring.", path);
        return NULL;
    }

    RmFile *file = rm_file_new(
        self->session, path, path_len, stat_info, type, 0, 0, 0
    );

    file->is_original = json_object_get_boolean_member(object, "is_original");
    file->is_symlink = is_symlink;
    file->depth = json_object_get_int_member(object, "depth");
    file->digest = rm_digest_new(RM_DIGEST_EXT, 0, 0, 0, 0);

    JsonNode *cksum_node = json_object_get_member(object, "checksum");
    if(cksum_node != NULL) {
        const char *cksum = json_object_get_string_member(object, "checksum");
        if(cksum != NULL) {
            rm_digest_update(file->digest, (unsigned char *)cksum, strlen(cksum));
        }
    }

    JsonNode *hardlink_of = json_object_get_member(object, "hardlink_of");
    if(hardlink_of != NULL) {
        file->hardlinks.is_head = false;
        // TODO: link to last original.
    } else {
        file->hardlinks.is_head = true;
    }

    return file;
}

static bool rm_parrot_check_depth(RmCfg *cfg, RmFile *file) {
    return file->depth == 0 || file->depth <= cfg->depth;
}

static bool rm_parrot_check_size(RmCfg *cfg, RmFile *file) {
    if(cfg->limits_specified == false) {
        return true;
    }

    return
        ((cfg->minsize == (RmOff)-1 || cfg->minsize <= file->file_size) &&
        (cfg->maxsize == (RmOff)-1 || file->file_size <= cfg->maxsize));
}

static bool rm_parrot_check_hidden(RmCfg *cfg, RmFile *file) {
    RM_DEFINE_PATH(file);
    if(!cfg->ignore_hidden) {
        return true;
    }

    return !rm_util_path_is_hidden(file_path);
}

static bool rm_parrot_check_permissions(RmCfg *cfg, RmFile *file) {
    if(!cfg->permissions) {
        return true;
    }

    RM_DEFINE_PATH(file);
    if(access(file_path, cfg->permissions) == -1) {
        rm_log_debug("[permissions]\n");
        return false;
    }

    return true;
}
 
static bool rm_parrot_check_path(RmParrot *polly, RmFile *file) {
    RmCfg *cfg = polly->session->cfg;
    RM_DEFINE_PATH(file);

    size_t highest_match = 0;

    for(int i = 0; cfg->paths[i]; ++i) {
        char *path = cfg->paths[i];
        size_t path_len = strlen(path);

        if(strncmp(file_path, path, path_len) == 0) {
            if(path_len > highest_match) {
                highest_match = path_len;

                file->is_prefd = cfg->is_prefd[i];
                file->path_index = i;

                /* We can't just break here, a more fitting path may come. */
            }
        }
    }

    if(highest_match == 0) {
        rm_log_debug("[wrong prefix]\n");
    }

    return highest_match > 0;
}

static bool rm_parrot_check_types(RmCfg *cfg, RmFile *file) {
    switch(file->lint_type) {
        case RM_LINT_TYPE_DUPE_CANDIDATE:
            return cfg->find_duplicates;
        case RM_LINT_TYPE_DUPE_DIR_CANDIDATE:
            return cfg->merge_directories;
        case RM_LINT_TYPE_BADLINK:
            return cfg->find_badlinks;
        case RM_LINT_TYPE_EMPTY_DIR:
            return cfg->find_emptydirs;
        case RM_LINT_TYPE_EMPTY_FILE:
            return cfg->find_emptyfiles;
        case RM_LINT_TYPE_NONSTRIPPED:
            return cfg->find_nonstripped;
        case RM_LINT_TYPE_BADUID:
        case RM_LINT_TYPE_BADGID:
        case RM_LINT_TYPE_BADUGID:
            return cfg->find_badids;
        case RM_LINT_TYPE_UNFINISHED_CKSUM:
            return cfg->write_unfinished;
        case RM_LINT_TYPE_UNKNOWN:
        default:
            rm_log_debug("[type %d not allowed]\n", file->lint_type);
            return false;
    }
}

static void rm_parrot_fix_match_opts(RmParrot *self, GQueue *group) {
    RmCfg *cfg = self->session->cfg;
    if(!(cfg->match_with_extension 
    || cfg->match_without_extension 
    || cfg->match_basename)) {
        return;
    }

    /* That's probably a sucky way to do it due to n^2,
     * but I doubt that will make a large performance difference.
     */

    GList *iter = group->head;
    while(iter) {
        RmFile *file_a = iter->data;
        bool delete = true;

        for(GList *sub_iter = group->head; sub_iter; sub_iter = sub_iter->next) {
            RmFile *file_b = sub_iter->data;
            if(file_a == file_b) {
                continue;
            }

            if(rm_file_equal(file_a, file_b)) {
                delete = false;
            }
        }

        if(delete) {
            GList *old = iter;
            iter = iter->next;
            g_queue_delete_link(group, old);
        } else {
            iter = iter->next;
        }
    }
}

static void rm_parrot_fix_must_match_tagged(RmParrot *self, GQueue *group) {
    RmCfg *cfg = self->session->cfg;
    if(!(cfg->must_match_tagged || cfg->must_match_untagged)) {
        return;
    }

    bool has_prefd = false;
    bool has_non_prefd = false;

    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;

        has_prefd |= file->is_prefd;
        has_non_prefd |= !file->is_prefd;

        if(has_prefd && has_non_prefd) {
            break;
        }
    }

    if((!has_prefd && cfg->must_match_tagged) 
    || (!has_non_prefd && cfg->must_match_untagged)) {
        // TODO: free.
        g_queue_clear(group);
    }
}

static void rm_parrot_write_group(RmParrot *self, GQueue *group) {
    RmCfg *cfg = self->session->cfg;

    if(cfg->filter_mtime) {
        gsize older = 0;
        for(GList *iter = group->head; iter; iter = iter->next) {
            RmFile *file = iter->data;
            older += (file->mtime >= cfg->min_mtime);
        }

        if(older == group->length) {
            // TODO: free.
            return;
        }
    }

    rm_parrot_fix_match_opts(self, group);
    rm_parrot_fix_must_match_tagged(self, group);

    g_queue_sort(
        group, (GCompareDataFunc)rm_shred_cmp_orig_criteria, self->session
    );

    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;

        if(file == group->head->data 
        || (cfg->keep_all_tagged && file->is_prefd)
        || (cfg->keep_all_untagged && !file->is_prefd)) {
            file->is_original = true;
        } else {
            file->is_original = false;

            
        }

        rm_fmt_write(file, self->session->formats);
    }
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

    GQueue group;
    g_queue_init(&group);

    while(rm_parrot_has_next(polly)) {
        RmFile *file = rm_parrot_next(polly);
        if(file == NULL) {
            /* Something went wrong during parse. */
            continue;
        }

        // TODO: We need path, just pass it over.
        RM_DEFINE_PATH(file);
        rm_log_debug("Checking `%s`: ", file_path);

        /* Check --size, --perms, --hidden */
        if(!(
            rm_parrot_check_depth(cfg, file) &&
            rm_parrot_check_size(cfg, file) &&
            rm_parrot_check_hidden(cfg, file) &&
            rm_parrot_check_permissions(cfg, file) &&
            rm_parrot_check_types(cfg, file) && 
            rm_parrot_check_path(polly, file)
        )) {
            rm_file_destroy(file);
            continue;
        }

        rm_log_debug("[okay]\n");

        // TODO: keep all / must match orig -- check

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

        if(file->is_original) {
            if(group.length > 1) {
                rm_log_debug("--- Writing group ---\n");
                rm_parrot_write_group(polly, &group);
            }
            g_queue_clear(&group);
        }

        g_queue_push_tail(&group, file);
    }

    if(group.length > 1) {
        rm_parrot_write_group(polly, &group);
    }

    rm_parrot_close(polly);
    g_queue_clear(&group);
    return true;
}
