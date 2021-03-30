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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

/* Internal headers */
#include "replay.h"
#include "config.h"
#include "file.h"
#include "formats.h"
#include "logger.h"
#include "preprocess.h"
#include "session.h"
#include "shredder.h"
#include "traverse.h"

/* External libraries */
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#include <json-glib/json-glib.h>


static bool rm_parrot_load_file_from_object(RmSession *session, JsonObject *object, bool json_is_prefd) {
    RmCfg *cfg = session->cfg;

    /* Read the path (without generating a warning if it's not there) */ //TODO: why not?
    JsonNode *path_node = json_object_get_member(object, "path");
    if(path_node == NULL) {
        rm_log_warning_line("path_node is NULL");
        return FALSE;
    }
    const char *path = json_node_get_string(path_node);

    /* Check for the lint type */
    JsonNode *type_node = json_object_get_member(object, "type");
    if(type_node == NULL) {
        rm_log_warning_line(_("lint type of node not recognised"));
        return FALSE;
    }

    bool is_prefd;
    unsigned long path_index;
    bool is_hidden;
    bool is_on_subvol_fs;
    short depth;
    RmLintType file_type = RM_LINT_TYPE_UNKNOWN;
    bool is_symlink = FALSE;
    bool tagged_original = FALSE;
    const char *ext_cksum = NULL;

    RmNode *node = rm_trie_insert(&cfg->file_trie, path, NULL);
    if(!rm_cfg_is_traversed(cfg, node, &is_prefd, &path_index, &is_hidden, &is_on_subvol_fs, &depth)){
        rm_log_info_line("Skipping cached file %s because not on search path", path);
        return FALSE;
    }


    /* Collect file information */
    RmStat lstat_buf, stat_buf;
    RmStat *stat_info = &lstat_buf;
    if(rm_sys_lstat(path, &lstat_buf) == -1) {
        // TODO: warning
        return FALSE;
    }

    is_symlink = S_ISLNK(lstat_buf.st_mode);
    bool is_badlink = is_symlink && rm_sys_stat(path, &stat_buf) == -1;
    if(is_badlink && cfg->find_badlinks) {
        is_symlink = FALSE;
        file_type = RM_LINT_TYPE_BADLINK;
    }
    else {
        if (is_symlink) {
            if(cfg->see_symlinks) {
                /* NOTE: bad links are also counted as duplicates
                 *       when -T df,dd (for example) is used.
                 *       They can serve as input for the treemerge
                 *       algorithm which might fail when missing.
                 */
                file_type = RM_LINT_TYPE_UNKNOWN;
            }
            else {
                // follow the symlink
                stat_info = &stat_buf;
            }
        }

        /* Check if we're late and issue an warning */
        JsonNode *mtime_node = json_object_get_member(object, "mtime");
        if(mtime_node) {
            /* Note: lstat_buf used here since for symlinks we want their mtime */ // TODO: test this with respect to see_symlinks etc
            gdouble stat_mtime = rm_sys_stat_mtime_float(&lstat_buf);
            gdouble json_mtime = json_node_get_double(mtime_node);

            /* Allow them a rather large span to deviate to account for inaccuracies */
            if(fabs(stat_mtime - json_mtime) > 0.05) {
                rm_log_warning_line(_("modification time of `%s` changed. Ignoring."), path);
                return FALSE;
            }
        }

        /* Check for the lint type in json file */
        JsonNode *type_node = json_object_get_member(object, "type");
        if(type_node == NULL) {
            rm_log_warning_line(_("lint type of node not recognised"));
        }
        else {
            const char *type_str = json_node_get_string(type_node);
            RmLintType json_lint_type = rm_file_string_to_lint_type(type_str);
            switch(json_lint_type) {
            case RM_LINT_TYPE_EMPTY_DIR:
                // need to re-scan
                if(!rm_traverse_is_emptydir(path, cfg, depth)) {
                    return FALSE;
                }
                file_type = RM_LINT_TYPE_EMPTY_DIR;
                break;
            case RM_LINT_TYPE_DUPE_DIR_CANDIDATE:
                // ignore
                return FALSE;
            default:
                file_type = RM_LINT_TYPE_UNKNOWN;
            }
        }

        JsonNode *is_original_node = json_object_get_member(object, "is_original");
        tagged_original = is_original_node && json_node_get_boolean(is_original_node);

        /* Fake the checksum using RM_DIGEST_EXT */
        JsonNode *cksum_node = json_object_get_member(object, "checksum");
        if(cksum_node != NULL) {
            ext_cksum = json_node_dup_string(cksum_node);
        }
    }

    is_prefd |= json_is_prefd;

    if(rm_traverse_file(session, stat_info, path, is_prefd, path_index, file_type,
            is_symlink, is_hidden, is_on_subvol_fs, depth, tagged_original, ext_cksum)) {
        return TRUE;
    }
    return FALSE;
}

static gboolean rm_parrot_cage_load(RmSession *session, const char *json_path, bool json_is_prefd) {

    GError *error = NULL;

    rm_log_info_line(_("Loading json-results `%s'"), json_path);
    if(!json_parser_load_from_file(session->json_parser, json_path, &error)) {
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(session->json_parser);
    if(JSON_NODE_TYPE(root) != JSON_NODE_ARRAY) {
        g_set_error(&error, RM_ERROR_QUARK, 0, _("No valid json cache (no array in /)"));
        return FALSE;
    }

    JsonArray *array = json_node_get_array(root);
    //JsonObject *header = json_array_get_object_element(array, 0);

    int file_count = json_array_get_length(array) - 2;  // minus 2 for header and footer

    for(int i=1; i<=file_count; i++) {
        JsonObject *object = json_array_get_object_element(array, i);
        rm_parrot_load_file_from_object(session, object, json_is_prefd);
    }
    return TRUE;
}


int rm_parrot_load(RmSession *session) {

    /* User chose to replay some json files. */
    bool one_valid_json = false;
    RmCfg *cfg = session->cfg;
    session->json_parser = json_parser_new();


    for(GSList *iter = cfg->json_paths; iter; iter = iter->next) {
        RmPath *jsonpath = iter->data;
        if(!rm_parrot_cage_load(session, jsonpath->path, jsonpath->is_prefd)) {
            rm_log_warning_line("Loading %s failed.", jsonpath->path);
        } else {
            one_valid_json = true;
        }
    }

    g_object_unref(session->json_parser);
    session->json_parser = NULL;

    if(!one_valid_json) {
        rm_log_error_line(_("No valid .json files given, aborting."));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
