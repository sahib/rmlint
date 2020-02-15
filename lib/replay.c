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
#include "preprocess.h"
#include "session.h"
#include "shredder.h"

/* External libraries */
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#if HAVE_JSON_GLIB
#include <json-glib/json-glib.h>

typedef struct RmUnpackedDirectory {
    GQueue *files;
} RmUnpackedDirectory;

static RmUnpackedDirectory *rm_unpacked_directory_new(void) {
    RmUnpackedDirectory *self = g_malloc0(sizeof(RmUnpackedDirectory));
    self->files = g_queue_new();
    return self;
}

static bool rm_unpacked_directory_has_next(RmUnpackedDirectory *self) {
    return g_queue_get_length(self->files) > 0;
}

static RmFile *rm_unpacked_directory_next(RmUnpackedDirectory *self) {
    return g_queue_pop_head(self->files);
}

static void rm_unpacked_directory_add(RmUnpackedDirectory *self, RmFile *file) {
    g_queue_push_tail(self->files, file);
}

static void rm_unpacked_directory_free(RmUnpackedDirectory *self) {
    g_queue_free(self->files);
    g_free(self);
}

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

    /* true if the .json file came after a //
     * on the commandline. */
    bool is_prefd;

    /* If true deliver each RmFile in a duplicate directory. */
    bool unpack_directories;
    RmUnpackedDirectory *unpacker;

    /* If true, cluster up found RmFiles */
    bool pack_directories;

    /* map duplicate directories paths to the group of RmFiles
     * they consist of (read from the "part_of_directory" type
     * (required for the unpacking feature)
     * */
    RmTrie directory_trie;
} RmParrot;

static int rm_parrot_dir_trie_clear_children(_UNUSED RmTrie *self, RmNode *node, _UNUSED int level, _UNUSED void *user_data) {
    GQueue *children = node->data;
    if(children != NULL) {
        g_queue_free(children);
    }
    return 0;
}

static void rm_parrot_close(RmParrot *polly) {
    if(polly->parser) {
        g_object_unref(polly->parser);
    }

    g_hash_table_unref(polly->disk_ids);

    /* Free the GQeues in the trie */
    rm_trie_iter(
        &polly->directory_trie,
        NULL,
        true,
        true,
        rm_parrot_dir_trie_clear_children,
        NULL
    );

    rm_trie_destroy(&polly->directory_trie);

    g_free(polly);
}

static RmParrot *rm_parrot_open(RmSession *session, const char *json_path, bool is_prefd,
                                GError **error) {
    RmParrot *polly = g_malloc0(sizeof(RmParrot));
    polly->session = session;
    polly->parser = json_parser_new();
    polly->disk_ids = g_hash_table_new(NULL, NULL);
    polly->index = 1;
    polly->is_prefd = is_prefd;
    rm_trie_init(&polly->directory_trie);

    for(GSList *iter = session->cfg->paths; iter; iter = iter->next) {
        RmPath *rmpath = iter->data;
        RmStat stat_buf;
        if(rm_sys_stat(rmpath->path, &stat_buf) != -1) {
            g_hash_table_add(polly->disk_ids, GUINT_TO_POINTER(stat_buf.st_dev));
        }
    }

    if(!json_parser_load_from_file(polly->parser, json_path, error)) {
        return NULL;
    }

    JsonNode *root = json_parser_get_root(polly->parser);
    if(JSON_NODE_TYPE(root) != JSON_NODE_ARRAY) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("No valid json cache (no array in /)"));
        return NULL;
    }

    polly->root = json_node_get_array(root);

    JsonObject *object = json_array_get_object_element(polly->root, 0);
    JsonNode *merge_directories_node = json_object_get_member(object, "merge_directories");

    if(merge_directories_node != NULL) {
        bool json_had_merge_dirs = json_node_get_boolean(merge_directories_node);

        if(session->cfg->merge_directories != json_had_merge_dirs) {
            if(json_had_merge_dirs) {
                /* The .json file was created with the -D option
                 * We need to unpack directories while running.
                 * */
                rm_log_info_line("»%s« was created with -D, but you're running without.", json_path)
                rm_log_info_line("rmlint will unpack duplicate directories into individual files.")
                rm_log_info_line("If you do not want this, pass -D to the next run.")
                polly->unpack_directories = true;
            } else {
                rm_log_info_line("»%s« was created without -D, but you're running with.", json_path)
                rm_log_info_line("rmlint will pack duplicate files into directories where applicable.")
                rm_log_info_line("If you do not want this, omit -D from the next run.")
                polly->pack_directories = true;
            }
        }
    }

    return polly;
}

static bool rm_parrot_has_next(RmParrot *polly) {
    if(polly->unpacker != NULL) {
        if(rm_unpacked_directory_has_next(polly->unpacker)) {
            return true;
        }

        rm_unpacked_directory_free(polly->unpacker);
        polly->unpacker = NULL;
    }

    return (polly->index < json_array_get_length(polly->root));
}

static RmFile *rm_parrot_try_next(RmParrot *polly) {
    if(!rm_parrot_has_next(polly)) {
        return NULL;
    }

    RmFile *file = NULL;
    const char *path = NULL;

    JsonObject *object = json_array_get_object_element(polly->root, polly->index);

    /* Deliver a higher index the next time, even if it fails */
    polly->index += 1;

    /* Read the path (without generating a warning if it's not there) */
    JsonNode *path_node = json_object_get_member(object, "path");
    if(path_node == NULL) {
        return NULL;
    }

    path = json_node_get_string(path_node);

    /* Check for the lint type */
    RmLintType type =
        rm_file_string_to_lint_type(json_object_get_string_member(object, "type"));

    if(type == RM_LINT_TYPE_UNKNOWN) {
        rm_log_warning_line(_("lint type '%s' not recognised"),
                            json_object_get_string_member(object, "type"));
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
    if(mtime_node) {
        /* Note: lstat_buf used here since for symlinks we want their mtime */
        gdouble stat_mtime = rm_sys_stat_mtime_float(&lstat_buf);
        gdouble json_mtime = json_node_get_double(mtime_node);

        /* Allow them a rather large span to deviate to account for inaccuracies */
        if(fabs(stat_mtime - json_mtime) > 0.05) {
            rm_log_warning_line(_("modification time of `%s` changed. Ignoring."), path);
            return NULL;
        }
    }

    /* Fill up the RmFile */
    file = rm_file_new(polly->session, path, stat_info, type, 0, 0, 0);
    file->is_original = json_object_get_boolean_member(object, "is_original");
    file->is_symlink = (lstat_buf.st_mode & S_IFLNK);
    file->digest = rm_digest_new(RM_DIGEST_EXT, 0);

    if(type != RM_LINT_TYPE_UNIQUE_FILE) {
        file->free_digest = true;
    } else {
        file->free_digest = false;
    }

    // stat() reports directories as size zero.
    // Fix this by actually using the size field from the json node.
    if (type == RM_LINT_TYPE_DUPE_DIR_CANDIDATE && stat_info->st_mode & S_IFDIR) {
        file->actual_file_size = json_object_get_int_member(object, "size");
    }

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
        rm_file_hardlink_add(polly->last_original, file);
    } else {
        g_assert(!file->hardlinks);
    }

    if(file->lint_type == RM_LINT_TYPE_PART_OF_DIRECTORY) {
        const char *parent_path = json_object_get_string_member(object, "parent_path");
        GQueue *children = rm_trie_search(&polly->directory_trie, parent_path);
        if(children == NULL) {
            children = g_queue_new();
            rm_trie_insert(&polly->directory_trie, parent_path, children);
        }

        g_queue_push_tail(children, file);
    }

    return file;
}

static int rm_parrot_iter_dir_children(_UNUSED RmTrie *self, RmNode *node, _UNUSED int level, void *user_data) {
    RmUnpackedDirectory *unpacker = user_data;

    char buf[PATH_MAX] = {0};
    rm_trie_build_path_unlocked(node, buf, PATH_MAX);

    GQueue *children = node->data;
    if(children == NULL) {
        return 0;
    }

    for(GList *iter = children->head; iter; iter = iter->next) {
        // Mask that RM_LINT_TYPE_PART_OF_DIRECTORY as normal duplicate.
        RmFile *child_file = rm_file_copy(iter->data);
        child_file->lint_type = RM_LINT_TYPE_DUPE_CANDIDATE;
        rm_unpacked_directory_add(unpacker, child_file);
    }

    // Make sure to clear the queue. This is important in case a more top-level
    // directory contains this directory. Then we would output the same files
    // twice.
    g_queue_clear(children);
    return 0;
}

static RmFile *rm_parrot_next(RmParrot *polly) {
    RmFile *file = NULL;

    /* If we have a directory to unpack, let's do that first */
    if(polly->unpacker != NULL) {
        file = rm_unpacked_directory_next(polly->unpacker);
        if(file != NULL) {
            return file;
        }

        rm_unpacked_directory_free(polly->unpacker);
        polly->unpacker = NULL;
    }

    /* Skip NULL entries */
    while(rm_parrot_has_next(polly)) {
        file = rm_parrot_try_next(polly);
        if(file == NULL) {
            /* try again */
            continue;
        }

        if(polly->unpack_directories &&
           file->lint_type == RM_LINT_TYPE_DUPE_DIR_CANDIDATE) {
            RM_DEFINE_PATH(file);
            rm_log_debug_line("unpacking: %s", file_path);

            /* Accumulate all files in that directory into a group.
             * This group will serve as source for the next iterations.
             */
            polly->unpacker = rm_unpacked_directory_new();
            rm_trie_iter(
                &polly->directory_trie,
                rm_trie_search_node(&polly->directory_trie, file_path),
                true,
                true,
                rm_parrot_iter_dir_children,
                polly->unpacker
            );

            /* we cant get rid of the actual directory now */
            rm_file_destroy(file);

            /* call self, which will now read from the unpacker;
             * current duplicate directory is not returned to caller.
             * */
            return rm_parrot_next(polly);
        }

        return file;
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

    if(file->lint_type != RM_LINT_TYPE_DUPE_CANDIDATE &&
       file->lint_type != RM_LINT_TYPE_DUPE_DIR_CANDIDATE) {
        // Non-lint files always count as good size.
        // See: https://github.com/sahib/rmlint/issues/368
        return true;
    }

    if((cfg->minsize == (RmOff)-1 || cfg->minsize <= file->actual_file_size)
    && (cfg->maxsize == (RmOff)-1 || file->actual_file_size <= cfg->maxsize)) {
        return true;
    }

    FAIL_MSG("nope: bad size");
    return false;
}

static bool rm_parrot_check_hidden(RmCfg *cfg, _UNUSED RmFile *file,
                                   const char *file_path) {
    if(cfg->ignore_hidden == false && cfg->partial_hidden == false) {
        // no need to check.
        return true;
    }

    if(rm_util_path_is_hidden(file_path)) {
        FAIL_MSG("nope: hidden");
        return false;
    }

    return true;
}

static bool rm_parrot_check_permissions(RmCfg *cfg, _UNUSED RmFile *file,
                                        const char *file_path) {
    if(!cfg->permissions) {
        return true;
    }

    if(g_access(file_path, cfg->permissions) == -1) {
        FAIL_MSG("nope: permissions");
        return false;
    }

    return true;
}

static bool rm_parrot_check_crossdev(RmParrot *polly, _UNUSED RmFile *file) {
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

    for(GSList *iter = cfg->paths; iter; iter = iter->next) {
        RmPath *rmpath = iter->data;
        size_t path_len = strlen(rmpath->path);

        if(strncmp(file_path, rmpath->path, path_len) == 0) {
            if(path_len > highest_match) {
                highest_match = path_len;

                file->is_prefd = rmpath->is_prefd || polly->is_prefd;
                file->path_index = rmpath->idx;
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
    case RM_LINT_TYPE_UNIQUE_FILE:
        return cfg->write_unfinished;
    case RM_LINT_TYPE_PART_OF_DIRECTORY:
        return true;
    case RM_LINT_TYPE_UNKNOWN:
    default:
        FAIL_MSG("nope: invalid lint type.");
        return false;
    }
}

/////////////////////////////////////////////
//   GROUPWISE FIXES (SORT, FILTER, ...)   //
/////////////////////////////////////////////

/* RmHRFunc to remove file unless it has a twin in group */
static int rm_parrot_fix_match_opts(RmFile *file, GQueue *group) {
    for(GList *iter = group->head; iter; iter = iter->next) {
        if(file != iter->data && rm_file_cmp(file, iter->data) == 0) {
            return 0;
        }
    }

    return 1;
}

static int rm_parrot_sort_by_path(gconstpointer a, gconstpointer b, _UNUSED gpointer data) {
    const RmFile *file_a = a;
    const RmFile *file_b = b;

    RM_DEFINE_PATH(file_a);
    RM_DEFINE_PATH(file_b);
    return strcmp(file_a_path, file_b_path);
}

static void rm_parrot_fix_duplicate_entries(RmParrotCage *cage, GQueue *group) {
    /* This quirk can happen when we have a duplicate directory that
     * was unpacked. If that dir has other duplicates inside them
     * it can happen that we have a "part_of_directory" type that
     * was promoted to a "dupe_canidate" before.
     *
     * This also serves as safety-net for cases when the json files
     * contain a path several times.
     */

    g_queue_sort(
        group,
        (GCompareDataFunc)rm_parrot_sort_by_path,
        NULL
    );

    char *last_path = NULL;

    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        if(file->lint_type == RM_LINT_TYPE_PART_OF_DIRECTORY) {
            /* Those are not visible in the output, allow them. */
            continue;
        }

        RM_DEFINE_PATH(file);

        if(last_path && g_strcmp0(file_path, last_path) == 0) {
            // remove node.
            GList *old_iter = iter;
            iter = iter->prev;
            g_queue_delete_link(cage->groups, old_iter);
            rm_file_destroy(file);
        }

        g_free(last_path);
        last_path = g_strdup(file_path);
    }

    g_free(last_path);
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

    if((!has_prefd && cfg->must_match_tagged) ||
       (!has_non_prefd && cfg->must_match_untagged)) {
        g_queue_clear(group);
    }
}

static void rm_parrot_update_stats(RmParrotCage *cage, RmFile *file) {
    RmSession *session = cage->session;

    if(file->lint_type == RM_LINT_TYPE_PART_OF_DIRECTORY) {
        return;
    }

    session->total_files += 1;

    if(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE ||
       file->lint_type == RM_LINT_TYPE_DUPE_DIR_CANDIDATE) {
        session->dup_group_counter += file->is_original;
        if(!file->is_original) {
            session->dup_counter += 1;

            if(!RM_FILE_IS_HARDLINK(file)) {
                session->total_lint_size += file->actual_file_size;
            }
        }
    } else {
        session->other_lint_cnt += 1;
    }
}

static void rm_parrot_cage_write_group(RmParrotCage *cage, GQueue *group, bool pack_directories) {
    RmCfg *cfg = cage->session->cfg;

    if(cfg->filter_mtime) {
        gsize older = 0;
        for(GList *iter = group->head; iter; iter = iter->next) {
            RmFile *file = iter->data;
            older += (file->mtime >= cfg->min_mtime);
        }

        if(older == group->length) {
            g_queue_clear(group);
            return;
        }
    }

    if(cfg->match_with_extension
    || cfg->match_without_extension
    || cfg->match_basename
    || cfg->unmatched_basenames) {
        /* This is probably a sucky way to do it, due to n^2,
         * but I doubt that will make a large performance difference.
         */
        rm_util_queue_foreach_remove(
            group,
            (RmRFunc)rm_parrot_fix_match_opts,
            group
        );
    }

    rm_parrot_fix_must_match_tagged(cage, group);
    rm_parrot_fix_duplicate_entries(cage, group);

    g_queue_sort(
        group,
        (GCompareDataFunc)rm_shred_cmp_orig_criteria,
        cage->session
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

        /* Other lint never should bother for is_original,
         * since this definition doesn't make sense there.
         * */
        if(file->lint_type != RM_LINT_TYPE_DUPE_CANDIDATE
        && file->lint_type != RM_LINT_TYPE_DUPE_DIR_CANDIDATE) {
            file->is_original = false;
        }

        rm_parrot_update_stats(cage, file);
        file->twin_count = group->length;

        if(pack_directories && file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE) {
            rm_tm_feed(cage->tree_merger, file);
        } else {
            rm_fmt_write(file, cage->session->formats);
        }
    }
}

/////////////////////////////////////////
//  ENTRY POINT TO TRIGGER THE PARROT  //
/////////////////////////////////////////

static void rm_parrot_cage_push_to_group(RmParrotCage *cage, GQueue **group_ref,
                                      bool is_last) {
    GQueue *group = *group_ref;

    // NOTE: We allow groups with only one file in it.
    // Those can happen when we unpack directories.
    // If there's really just one file in the group
    // it is kicked our later in the process.
    if(group->length > 0) {
        g_queue_push_tail(cage->groups, group);
    } else {
        g_queue_free(group);
    }

    if(!is_last) {
        *group_ref = g_queue_new();
    }
}

bool rm_parrot_cage_load(RmParrotCage *cage, const char *json_path, bool is_prefd) {
    GError *error = NULL;

    rm_log_info_line(_("Loading json-results `%s'"), json_path);
    RmParrot *polly = rm_parrot_open(cage->session, json_path, is_prefd, &error);

    if(polly == NULL || error != NULL) {
        rm_log_warning_line("Error: %s", error->message);
        g_error_free(error);
        return false;
    }

    RmCfg *cfg = cage->session->cfg;
    GQueue *group = g_queue_new();
    GQueue *part_of_directory_entries = g_queue_new();
    RmDigest *last_digest = NULL;

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
               rm_parrot_check_depth(cfg, file)
            && rm_parrot_check_size(cfg, file)
            && rm_parrot_check_hidden(cfg, file, file_path)
            && rm_parrot_check_permissions(cfg, file, file_path)
            && rm_parrot_check_types(cfg, file)
            && rm_parrot_check_crossdev(polly, file)
            && rm_parrot_check_path(polly, file, file_path)
        )) {
            rm_log_debug("[nope]\n");
            if(file->lint_type != RM_LINT_TYPE_PART_OF_DIRECTORY) {
                rm_file_destroy(file);
            }

            continue;
        }

        /* Special case for part of dirs:
         * Accumuluate those in a single group that come in front of the other groups.
         * */
        if(file->lint_type == RM_LINT_TYPE_PART_OF_DIRECTORY) {
            rm_log_debug("[part of directory]\n");
            g_queue_push_tail(part_of_directory_entries, file);
            continue;
        }

        if(last_digest == NULL) {
            last_digest = rm_digest_copy(file->digest);
        }

        rm_log_debug("[okay]\n");

        if(file->digest != NULL && !rm_digest_equal(file->digest, last_digest)) {
            rm_digest_free(last_digest);
            last_digest = rm_digest_copy(file->digest);
            rm_parrot_cage_push_to_group(cage, &group, false);
        }

        g_queue_push_tail(group, file);
    }

    if(last_digest != NULL) {
        rm_digest_free(last_digest);
    }

    rm_parrot_cage_push_to_group(cage, &group, true);
    g_queue_push_tail(cage->parrots, polly);

    if(part_of_directory_entries->length > 1) {
        g_queue_push_head(cage->groups, part_of_directory_entries);
    } else {
        g_queue_free_full(part_of_directory_entries, (GDestroyNotify)rm_file_destroy);
    }

    return true;
}

void rm_parrot_cage_open(RmParrotCage *cage, RmSession *session) {
    cage->session = session;
    cage->groups = g_queue_new();
    cage->parrots = g_queue_new();
    cage->tree_merger = NULL;
}

static void rm_parrot_merge_identical_groups(RmParrotCage *cage) {
    GHashTable *digest_to_group =
        g_hash_table_new((GHashFunc)rm_digest_hash, (GEqualFunc)rm_digest_equal);

    for(GList *iter = cage->groups->head; iter; iter = iter->next) {
        GQueue *group = iter->data;
        RmFile *head_file = group->head->data;

        GQueue *existing_group = g_hash_table_lookup(digest_to_group, head_file->digest);

        if(existing_group != NULL) {
            RmFile *existing_head_file = existing_group->head->data;

            /* Merge only groups with the same type */
            if(existing_head_file->lint_type == head_file->lint_type) {
                /* Merge the two groups and get rid of the old one. */
                rm_util_queue_push_tail_queue(existing_group, group);
                g_queue_free(group);
                GList *old_iter = iter;
                iter = iter->prev;
                g_queue_delete_link(cage->groups, old_iter);
                continue;
            }
        }

        // No such group, but remember it.
        g_hash_table_insert(digest_to_group, head_file->digest, group);
    }

    g_hash_table_unref(digest_to_group);
}

void rm_parrot_cage_output_treemerge_results(RmFile *file, gpointer data) {
    RmParrotCage *cage = data;
    g_assert(cage);

    rm_fmt_write(file, cage->session->formats);
}

void rm_parrot_cage_flush(RmParrotCage *cage) {
    rm_parrot_merge_identical_groups(cage);

    bool pack_directories = false;

    /* Check if any of the .json files were created with -D.
     * If so, we need to merge them up again using treemerge.c
     */
    for(GList *iter = cage->parrots->head; iter; iter = iter->next) {
        RmParrot *parrot = iter->data;
        if(parrot->pack_directories) {
            pack_directories = true;
            break;
        }
    }

    if(pack_directories) {
        cage->tree_merger = rm_tm_new(cage->session);
        rm_tm_set_callback(cage->tree_merger, (RmTreeMergeOutputFunc)rm_parrot_cage_output_treemerge_results, cage); 
    }

    for(GList *iter = cage->groups->head; iter; iter = iter->next) {
        GQueue *group = iter->data;
        if(group->length > 1) {
            rm_parrot_cage_write_group(cage, group, pack_directories);
        }
    }

    if(pack_directories && cage->tree_merger) {
        rm_tm_finish(cage->tree_merger);
    }
}

void rm_parrot_cage_close(RmParrotCage *cage) {
    g_queue_free_full(cage->groups, (GDestroyNotify)g_queue_free);
    g_queue_free_full(cage->parrots, (GDestroyNotify)rm_parrot_close);

    if(cage->tree_merger) {
        rm_tm_destroy(cage->tree_merger);
    }
}

#else

bool rm_parrot_cage_load(_UNUSED RmParrotCage *cage, _UNUSED const char *json_path,
                         _UNUSED bool is_prefd) {
    return false;
}

void rm_parrot_cage_open(_UNUSED RmParrotCage *cage, _UNUSED RmSession *session) {
    rm_log_error_line(_("json-glib is needed for using --replay."));
    rm_log_error_line(_("Please recompile `rmlint` with it installed."));
}

void rm_parrot_cage_flush(_UNUSED RmParrotCage *cage) {
}

void rm_parrot_cage_close(_UNUSED RmParrotCage *cage) {
}

#endif
