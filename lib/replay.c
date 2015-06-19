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
#include "replay.h"
#include "session.h"
#include "formats.h"
#include "file.h"
#include "preprocess.h"
#include "shredder.h"

/* External libraries */
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#if HAVE_JSON_GLIB
# include <json-glib/json-glib.h>

/////////////////////////////////////////////////
//  POLLY THE PARROT REPEATS WHAT RMLINT SAID  //
/////////////////////////////////////////////////

typedef struct RmParrot {
    /* Global session */
    RmSession *session;
    
    /* Json parser instance */
    JsonParser *parser;

    /* Root array of the json document */
    JsonArray *root;

    /* Last original file that we encountered */
    RmFile *last_original;

    /* Index inside the document 
     * (0 is header, 1 first element, len(root) is footer)
     * */
    guint index;

    /* Set of diskids in cfg->paths */
    GHashTable *disk_ids;
} RmParrot;

static void rm_parrot_close(RmParrot *polly) {
    if(polly->parser) {
        g_object_unref(polly->parser);
    }

    g_hash_table_unref(polly->disk_ids);
    g_free(polly);
}

static RmParrot *rm_parrot_open(RmSession *session, const char *json_path, GError **error) {
    RmParrot *polly = g_malloc0(sizeof(RmParrot));
    polly->session = session;
    polly->parser = json_parser_new();
    polly->disk_ids = g_hash_table_new(NULL, NULL);
    polly->index = 1;

    for(int idx = 0; session->cfg->paths[idx]; ++idx) {
        RmStat stat_buf;
        const char *path = session->cfg->paths[idx];

        if(rm_sys_stat(path, &stat_buf) != -1) {
            g_hash_table_add(polly->disk_ids, GUINT_TO_POINTER(stat_buf.st_dev));
        }   
    }

    if(!json_parser_load_from_file(polly->parser, json_path, error)) {
        goto failure;
    }

    JsonNode *root = json_parser_get_root(polly->parser);
    if(JSON_NODE_TYPE(root) != JSON_NODE_ARRAY) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("No valid json cache (no array in /)"));
        goto failure;
    }

    polly->root = json_node_get_array(root);
    return polly;

failure:
    rm_parrot_close(polly);
    return NULL;
}

static bool rm_parrot_has_next(RmParrot *polly) {
    return (polly->index < json_array_get_length(polly->root));
}

static RmFile *rm_parrot_try_next(RmParrot *polly) {
    if(!rm_parrot_has_next(polly)) {
        return NULL;
    }

    RmFile *file = NULL;
    const char *path = NULL;
    size_t path_len = 0;

    JsonObject *object = json_array_get_object_element(polly->root, polly->index);

    /* Deliver a higher index the next time, even if it fails */
    polly->index += 1;

    /* Read the path (without generating a warning if it's not there) */
    JsonNode *path_node = json_object_get_member(object, "path");
    if(path_node == NULL) {
        return NULL;
    }

    path = json_node_get_string(path_node);
    path_len = strlen(path);

    if(rm_trie_search_node(&polly->session->cfg->file_trie, path)) {
        /* We have this node already */
        return NULL;
    }

    /* Check for the lint type */
    RmLintType type = rm_file_string_to_lint_type(
        json_object_get_string_member(object, "type")
    );

    if(type == RM_LINT_TYPE_UNKNOWN) {
        rm_log_warning_line(
            _("lint type '%s' not recognised"),
            json_object_get_string_member(object, "type")
        );
        return NULL;
    }

    /* Collect file information (for rm_file_new) */
    RmStat lstat_buf, stat_buf;
    RmStat *stat_info = &lstat_buf;
    if(rm_sys_lstat(path, &lstat_buf) == -1) {
        return NULL;
    }

    /* use stat() after lstat() to find out if it's an symlink.
     * If it's a bad link, this will fail with stat_info still pointing to lstat.
     * */
    if(rm_sys_stat(path, &stat_buf) != -1) {
        stat_info = &stat_buf;
    }

    /* Check if we're late and issue an warning */
    JsonNode *mtime_node = json_object_get_member(object, "mtime");
    if(mtime_node && json_node_get_int(mtime_node) < rm_sys_stat_mtime_seconds(stat_info)) {
        // TODO: translate:
        rm_log_warning_line(
            _("modification time of `%s` changed. Ignoring."), path
        );
        return NULL;
    }

    /* Fill up the RmFile */
    file = rm_file_new(polly->session, path, path_len, stat_info, type, 0, 0, 0);
    file->is_original = json_object_get_boolean_member(object, "is_original");
    file->is_symlink = (lstat_buf.st_mode & S_IFLNK);
    file->digest = rm_digest_new(RM_DIGEST_EXT, 0, 0, 0, 0);
    file->free_digest = true;

    if(file->is_original) {
        polly->last_original = file;
    }

    JsonNode *depth_node = json_object_get_member(object, "depth");
    if(depth_node != NULL) {
        file->depth = json_node_get_int(depth_node);
    }

    /* Fake the checksum using RM_DIGEST_EXT */
    JsonNode *cksum_node = json_object_get_member(object, "checksum");
    if(cksum_node != NULL) {
        const char *cksum = json_object_get_string_member(object, "checksum");
        if(cksum != NULL) {
            rm_digest_update(file->digest, (unsigned char *)cksum, strlen(cksum));
        }
    }

    /* Fix the hardlink relationship */
    JsonNode *hardlink_of = json_object_get_member(object, "hardlink_of");
    if(hardlink_of != NULL) {
        file->hardlinks.is_head = false;
        file->hardlinks.hardlink_head = polly->last_original;
    } else {
        file->hardlinks.is_head = true;
    }

    return file;
}

static RmFile *rm_parrot_next(RmParrot *polly) {
    RmFile *file = NULL;

    /* Skip NULL entries */
    while(rm_parrot_has_next(polly)) {
        if((file = rm_parrot_try_next(polly))) {
            return file;
        }
    }

    return NULL;
}

//////////////////////////////////
//   OPTION FILTERING CHECKS    //
//////////////////////////////////

#define FAIL_MSG(msg) rm_log_debug(RED "[" msg "]\n" RESET)

static bool rm_parrot_check_depth(RmCfg *cfg, RmFile *file) {
    return (file->depth == 0 || file->depth <= cfg->depth);
}

static bool rm_parrot_check_size(RmCfg *cfg, RmFile *file) {
    if(cfg->limits_specified == false) {
        return true;
    }

    return
        ((cfg->minsize == (RmOff)-1 || cfg->minsize <= file->file_size) &&
        (cfg->maxsize == (RmOff)-1 || file->file_size <= cfg->maxsize));
}

static bool rm_parrot_check_hidden(RmCfg *cfg, _U RmFile *file, const char *file_path) {
    if(!cfg->ignore_hidden) {
        return true;
    }

    if(rm_util_path_is_hidden(file_path)) {
        FAIL_MSG("nope: hidden");
        return false;
    }

    return true;
}

static bool rm_parrot_check_permissions(RmCfg *cfg, _U RmFile *file, const char *file_path) {
    if(!cfg->permissions) {
        return true;
    }

    if(g_access(file_path, cfg->permissions) == -1) {
        FAIL_MSG("nope: permissions");
        return false;
    }

    return true;
}
 
static bool rm_parrot_check_crossdev(RmParrot *polly, _U RmFile *file) {
    if(polly->session->cfg->crossdev) {
        return true;
    }

    if(!g_hash_table_contains(polly->disk_ids, GUINT_TO_POINTER(file->dev))) {
        FAIL_MSG("nope: on other device");
        return false;
    }

    return true;
}
 
static bool rm_parrot_check_path(RmParrot *polly, RmFile *file, const char *file_path) {
    RmCfg *cfg = polly->session->cfg;

    size_t highest_match = 0;

    /* Find the highest matching path given on the commandline.
     * If found, the path_index and is_prefd information is taken from it.
     * If not found, the file will be discarded.
     *
     * If this turns out to be an performance problem, we could turn cfg->paths
     * into a RmTrie and use it to find the longest prefix easily.
     */
    for(int i = 0; cfg->paths[i]; ++i) {
        char *path = cfg->paths[i];
        size_t path_len = strlen(path);

        if(strncmp(file_path, path, path_len) == 0) {
            if(path_len > highest_match) {
                highest_match = path_len;

                file->is_prefd = cfg->is_prefd[i];
                file->path_index = i;
            }
        }
    }

    if(highest_match == 0) {
        FAIL_MSG("nope: no prefix");
    }

    return (highest_match > 0);
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
            FAIL_MSG("nope: invalid lint type.");
            return false;
    }
}

/////////////////////////////////////////////
//   GROUPWISE FIXES (SORT, FILTER, ...)   //
/////////////////////////////////////////////

static void rm_parrot_fix_match_opts(RmParrotCage *cage, GQueue *group) {
    RmCfg *cfg = cage->session->cfg;
    if(!(cfg->match_with_extension 
    || cfg->match_without_extension 
    || cfg->match_basename)) {
        return;
    }

    /* That's probably a sucky way to do it, due to n^2,
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
                break;
            }
        }

        /* Remove this file */
        if(delete) {
            GList *tmp = iter;
            iter = iter->next;
            rm_file_destroy(file_a);
            g_queue_delete_link(group, tmp);
        } else {
            iter = iter->next;
        }
    }
}

static void rm_parrot_fix_must_match_tagged(RmParrotCage *cage, GQueue *group) {
    RmCfg *cfg = cage->session->cfg;
    if(!(cfg->must_match_tagged || cfg->must_match_untagged)) {
        return;
    }

    bool has_prefd = false, has_non_prefd = false;

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
        g_queue_foreach(group, (GFunc)rm_file_destroy, NULL);
        g_queue_clear(group);
    }
}

static void rm_parrot_update_stats(RmParrotCage *cage, RmFile *file) {
    RmSession *session = cage->session;

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
}

static void rm_parrot_write_group(RmParrotCage *cage, GQueue *group) {
    RmCfg *cfg = cage->session->cfg;

    if(cfg->filter_mtime) {
        gsize older = 0;
        for(GList *iter = group->head; iter; iter = iter->next) {
            RmFile *file = iter->data;
            older += (file->mtime >= cfg->min_mtime);
        }

        if(older == group->length) {
            g_queue_foreach(group, (GFunc)rm_file_destroy, NULL);
            g_queue_clear(group);
            return;
        }
    }

    rm_parrot_fix_match_opts(cage, group);
    rm_parrot_fix_must_match_tagged(cage, group);

    g_queue_sort(
        group, (GCompareDataFunc)rm_shred_cmp_orig_criteria, cage->session
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

        rm_parrot_update_stats(cage, file);
        rm_fmt_write(file, cage->session->formats);
    }
}

/////////////////////////////////////////
//  ENTRY POINT TO TRIGGER THE PARROT  //
/////////////////////////////////////////

static void rm_parrot_cage_push_group(RmParrotCage *cage, GQueue **group_ref, bool is_last) {
    GQueue *group = *group_ref;
    if(group->length > 1) {
        g_queue_push_tail(cage->groups, group);
    } else {
        g_queue_free_full(group, (GDestroyNotify)rm_file_destroy);
    }
        
    if(!is_last) {
        *group_ref = g_queue_new();
    }
}

bool rm_parrot_cage_load(RmParrotCage *cage, const char *json_path) {
    GError *error = NULL;

    rm_log_info_line(_("Loading json-results `%s'"), json_path);
    RmParrot *polly = rm_parrot_open(cage->session, json_path, &error);

    if(polly == NULL || error != NULL) {
        rm_log_warning_line("Error: %s", error->message);
        g_error_free(error);
        return false; 
    }

    RmCfg *cfg = cage->session->cfg;
    GQueue *group = g_queue_new();

    /* group of files; first group is "other lint" */
    while(rm_parrot_has_next(polly)) {
        RmFile *file = rm_parrot_next(polly);
        if(file == NULL) {
            continue;
        }

        RM_DEFINE_PATH(file);
        rm_log_debug("Checking `%s`: ", file_path);

        /* Check --size, --perms, --hidden */
        if(!(
            rm_parrot_check_depth(cfg, file) &&
            rm_parrot_check_size(cfg, file) &&
            rm_parrot_check_hidden(cfg, file, file_path) &&
            rm_parrot_check_permissions(cfg, file, file_path) &&
            rm_parrot_check_types(cfg, file) && 
            rm_parrot_check_crossdev(polly, file) && 
            rm_parrot_check_path(polly, file, file_path)
        )) {
            rm_file_destroy(file);
            continue;
        }

        rm_log_debug("[okay]\n");

        g_queue_push_tail(group, file);

        if(file->is_original) {
            rm_parrot_cage_push_group(cage, &group, false);
        }
    }

    rm_parrot_cage_push_group(cage, &group, true);
    rm_parrot_close(polly);
    return true;
}

void rm_parrot_cage_open(RmParrotCage *cage, RmSession *session) {
    cage->session = session;
    cage->groups = g_queue_new();
}

void rm_parrot_cage_close(RmParrotCage *cage) {
    for(GList *iter = cage->groups->head; iter; iter = iter->next) {
        GQueue *group = iter->data;
        if(group->length > 1) {
            rm_parrot_write_group(cage, group);
        } else {
            g_queue_free_full(group, (GDestroyNotify)rm_file_destroy);
        }
    }

    g_queue_free_full(cage->groups, (GDestroyNotify)g_queue_free);
}

#else

bool rm_parrot_cage_load(_U RmParrotCage *cage, _U const char *json_path) {
    return false;
}

void rm_parrot_cage_open(_U RmParrotCage *cage, _U RmSession *session) {
    rm_log_error_line(_("json-glib is needed for using --replay."));
    rm_log_error_line(_("Please recompile `rmlint` with it installed."));
}

void rm_parrot_cage_close(_U RmParrotCage *cage) {
}

#endif
