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
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmdline.h"
#include "formats.h"
#include "preprocess.h"
#include "shredder.h"
#include "utilities.h"

static gint rm_file_cmp_with_extension(const RmFile *file_a, const RmFile *file_b) {
    char *ext_a = rm_util_path_extension(file_a->folder->basename);
    char *ext_b = rm_util_path_extension(file_b->folder->basename);

    if(ext_a && ext_b) {
        return g_ascii_strcasecmp(ext_a, ext_b);
    } else {
        /* file with an extension outranks one without */
        return (!!ext_a - !!ext_b);
    }
}

static gint rm_file_cmp_without_extension(const RmFile *file_a, const RmFile *file_b) {
    const char *basename_a = file_a->folder->basename;
    const char *basename_b = file_b->folder->basename;

    char *ext_a = rm_util_path_extension(basename_a);
    char *ext_b = rm_util_path_extension(basename_b);

    /* Check length till extension, or full length if none present */
    size_t a_len = (ext_a) ? (ext_a - basename_a) : (int)strlen(basename_a);
    size_t b_len = (ext_b) ? (ext_b - basename_b) : (int)strlen(basename_a);

    RETURN_IF_NONZERO(SIGN_DIFF(a_len, b_len));

    return g_ascii_strncasecmp(basename_a, basename_b, a_len);
}

/* test if two files qualify for the same "group"; if not then rank them by
 * size and then other factors depending on settings */
gint rm_file_cmp(const RmFile *file_a, const RmFile *file_b) {
    gint result = SIGN_DIFF(file_a->file_size, file_b->file_size);
    RETURN_IF_NONZERO(result);

    RmCfg *cfg = file_a->session->cfg;

    if(cfg->match_basename) {
        result = rm_file_basenames_cmp(file_a, file_b);
        RETURN_IF_NONZERO(result);
    }

    if(cfg->match_with_extension) {
        result = rm_file_cmp_with_extension(file_a, file_b);
        RETURN_IF_NONZERO(result);
    }

    if(cfg->match_without_extension) {
        result = rm_file_cmp_without_extension(file_a, file_b);
    }

    return result;
}

static gint rm_file_cmp_full(const RmFile *file_a, const RmFile *file_b,
                             const RmSession *session) {
    gint result = rm_file_cmp(file_a, file_b);
    RETURN_IF_NONZERO(result);

    if(session->cfg->mtime_window >= 0) {
        result = FLOAT_SIGN_DIFF(file_a->mtime, file_b->mtime, MTIME_TOL);
        RETURN_IF_NONZERO(result);
    }

    return rm_pp_cmp_orig_criteria(file_a, file_b, session);
}

static gint rm_file_cmp_split(const RmFile *file_a, const RmFile *file_b,
                              const RmSession *session) {
    gint result = rm_file_cmp(file_a, file_b);
    RETURN_IF_NONZERO(result);

    /* If --mtime-window is specified, we need to check if the mtime is inside
     * the window. The file list was sorted by rm_file_cmp_full by taking the
     * diff of mtimes, therefore we have to define the split criteria
     * differently.
     */
    if(session->cfg->mtime_window >= 0) {
        /* this will split group (return +/-1) if mtime difference is larger than
         * mtime window */
        return FLOAT_SIGN_DIFF(file_a->mtime, file_b->mtime, session->cfg->mtime_window);
    }

    return 0;
}

static guint rm_node_hash(const RmFile *file) {
    return file->inode ^ file->dev;
}

static gboolean rm_node_equal(const RmFile *file_a, const RmFile *file_b) {
    return (file_a->inode == file_b->inode) && (file_a->dev == file_b->dev);
}

/* GHashTable key tuned to recognize duplicate paths.
 * i.e. RmFiles that are not only hardlinks but
 * also point to the real path
 */
typedef struct RmPathDoubleKey {
    /* stat(dirname(file->path)).st_ino */
    ino_t parent_inode;

    /* File the key points to */
    RmFile *file;

} RmPathDoubleKey;

static guint rm_path_double_hash(const RmPathDoubleKey *key) {
    /* depend only on the always set components, never change the hash duringthe run
     */
    return rm_node_hash(key->file);
}

static ino_t rm_path_parent_inode(RmFile *file) {
    char parent_path[PATH_MAX];
    rm_trie_build_path(
        (RmTrie *)&file->session->cfg->file_trie,
        file->folder->parent,
        parent_path,
        PATH_MAX
    );

    RmStat stat_buf;
    int retval = rm_sys_stat(parent_path, &stat_buf);
    if(retval == -1) {
        RM_DEFINE_PATH(file);
        rm_log_error_line(
            "Failed to get parent path of %s: stat failed: %s",
            file_path,
            g_strerror(errno)
        );
        return 0;
    }

    return stat_buf.st_ino;
}

static bool rm_path_have_same_parent(RmPathDoubleKey *key_a, RmPathDoubleKey *key_b) {
    RmFile *file_a = key_a->file, *file_b = key_b->file;
    return (file_a->folder->parent == file_b->folder->parent ||
            rm_path_parent_inode(file_a) == rm_path_parent_inode(file_b));
}

static gboolean rm_path_double_equal(RmPathDoubleKey *key_a, RmPathDoubleKey *key_b) {
    if(key_a->file->inode != key_b->file->inode) {
        return FALSE;
    }

    if(key_a->file->dev != key_b->file->dev) {
        return FALSE;
    }

    RmFile *file_a = key_a->file;
    RmFile *file_b = key_b->file;

    if(!rm_path_have_same_parent(key_a, key_b)) {
        return FALSE;
    }

    return g_strcmp0(file_a->folder->basename, file_b->folder->basename) == 0;
}

static RmPathDoubleKey *rm_path_double_new(RmFile *file) {
    RmPathDoubleKey *key = g_malloc0(sizeof(RmPathDoubleKey));
    key->file = file;
    return key;
}

static void rm_path_double_free(RmPathDoubleKey *key) {
    g_free(key);
}

RmFileTables *rm_file_tables_new(_UNUSED const RmSession *session) {
    RmFileTables *tables = g_slice_new0(RmFileTables);

    tables->all_files = g_queue_new();
    tables->node_table =
        g_hash_table_new_full((GHashFunc)rm_node_hash, (GEqualFunc)rm_node_equal, NULL,
                              (GDestroyNotify)g_queue_free);
    tables->unique_paths_table =
        g_hash_table_new_full((GHashFunc)rm_path_double_hash,
                              (GEqualFunc)rm_path_double_equal,
                              (GDestroyNotify)rm_path_double_free,
                              NULL);

    g_mutex_init(&tables->lock);
    return tables;
}

void rm_file_tables_destroy(RmFileTables *tables) {
    g_queue_free(tables->all_files);

    if(tables->size_groups) {
        g_slist_free(tables->size_groups);
        tables->size_groups = NULL;
    }

    if(tables->node_table) {
        g_hash_table_unref(tables->node_table);
    }

    g_hash_table_unref(tables->unique_paths_table);

    g_mutex_clear(&tables->lock);
    g_slice_free(RmFileTables, tables);
}

static size_t rm_pp_parse_pattern(const char *pattern, GRegex **regex, GError **error) {
    if(*pattern != '<') {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Pattern has to start with `<`"));
        return 0;
    }

    /* Balance of start and end markers */
    int balance = 1;
    char *iter = (char *)pattern;
    char *last = iter;

    while((iter = strpbrk(&iter[1], "<>"))) {
        if(iter[-1] == '\\') {
            /* escaped, skip */
            break;
        }

        if(iter && *iter == '<') {
            ++balance;
        } else if(iter) {
            --balance;
            last = iter;
        }

        if(balance == 0) {
            break;
        }
    }

    if(balance != 0) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("`<` or `>` imbalance: %d"), balance);
        return 0;
    }

    size_t src_len = (last - pattern - 1);

    if(src_len == 0) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("empty pattern"));
        return 0;
    }

    GString *part = g_string_new_len(&pattern[1], src_len);

    rm_log_debug_line("Compiled pattern: %s\n", part->str);

    /* Actually compile the pattern: */
    *regex = g_regex_new(part->str, G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, error);

    g_string_free(part, TRUE);

    /* Include <> in the result len */
    return src_len + 2;
}

/* Compile all regexes inside a sortcriteria string.
 * Every regex (enclosed in <>)  will be removed from the
 * sortcriteria spec, so that the returned string will
 * consist only of single letters.
 */
char *rm_pp_compile_patterns(RmSession *session, const char *sortcrit, GError **error) {
    /* Total of encountered patterns */
    int pattern_count = 0;

    /* Copy of the sortcriteria without pattern specs in <> */
    size_t minified_cursor = 0;
    char *minified_sortcrit = g_strdup(sortcrit);

    for(size_t i = 0; sortcrit[i]; i++) {
        /* Copy everything that is not a regex pattern */
        minified_sortcrit[minified_cursor++] = sortcrit[i];
        char curr_crit = tolower((unsigned char)sortcrit[i]);

        /* Check if it's a non-regex sortcriteria */
        if(!(curr_crit == 'r' || curr_crit == 'x')) {
            continue;
        }

        if(sortcrit[i + 1] != '<') {
            g_set_error(error, RM_ERROR_QUARK, 0,
                        _("no pattern given in <> after 'r' or 'x'"));
            break;
        }

        GRegex *regex = NULL;

        /* Jump over the regex pattern part */
        i += rm_pp_parse_pattern(&sortcrit[i + 1], &regex, error);

        if(regex != NULL) {
            if(pattern_count < (int)RM_PATTERN_N_MAX && pattern_count != -1) {
                /* Append to already compiled patterns */
                g_ptr_array_add(session->pattern_cache, regex);
                pattern_count++;
            } else if(pattern_count != -1) {
                g_set_error(error, RM_ERROR_QUARK, 0,
                            _("Cannot add more than %lu regex patterns."),
                            (unsigned long)RM_PATTERN_N_MAX);

                /* Make sure to set the warning only once */
                pattern_count = -1;
                g_regex_unref(regex);
            } else {
                g_regex_unref(regex);
            }
        }
    }

    g_prefix_error(error, _("Error while parsing sortcriteria patterns: "));

    minified_sortcrit[minified_cursor] = 0;
    return minified_sortcrit;
}

static int rm_pp_cmp_by_regex(GRegex *regex, int idx, RmPatternBitmask *mask_a,
                              const char *path_a, RmPatternBitmask *mask_b,
                              const char *path_b) {
    int result = 0;

    if(RM_PATTERN_IS_CACHED(mask_a, idx)) {
        /* Get the previous match result */
        result = RM_PATTERN_GET_CACHED(mask_a, idx);
    } else {
        /* Match for the first time */
        result = g_regex_match(regex, path_a, 0, NULL);
        RM_PATTERN_SET_CACHED(mask_a, idx, result);
    }

    if(result) {
        return -1;
    }

    if(RM_PATTERN_IS_CACHED(mask_b, idx)) {
        /* Get the previous match result */
        result = RM_PATTERN_GET_CACHED(mask_b, idx);
    } else {
        /* Match for the first time */
        result = g_regex_match(regex, path_b, 0, NULL);
        RM_PATTERN_SET_CACHED(mask_b, idx, result);
    }

    if(result) {
        return +1;
    }

    /* Both match or none of the both match */
    return 0;
}

/*
 * Sort two files in accordance with single criterion
 */
static int rm_pp_cmp_criterion(unsigned char criterion, const RmFile *a, const RmFile *b,
                               const char *a_path, const char *b_path, int *regex_cursor,
                               const RmSession *session) {
    int sign = (isupper(criterion) ? -1 : 1);
    switch(tolower(criterion)) {
    case 'm':
        return sign * FLOAT_SIGN_DIFF(a->mtime, b->mtime, MTIME_TOL);
    case 'a':
        return sign * g_ascii_strcasecmp(a->folder->basename, b->folder->basename);
    case 'l':
        return sign * SIGN_DIFF(strlen(a->folder->basename), strlen(b->folder->basename));
    case 'd':
        return sign * SIGN_DIFF(a->depth, b->depth);
    case 'h':
        return sign * SIGN_DIFF(a->link_count, b->link_count);
    case 'o':
        return sign * SIGN_DIFF(a->outer_link_count, b->outer_link_count);
    case 'p':
        return sign * SIGN_DIFF(a->path_index, b->path_index);
    case 'x': {
        int cmp = rm_pp_cmp_by_regex(
            g_ptr_array_index(session->pattern_cache, *regex_cursor), *regex_cursor,
            (RmPatternBitmask *)&a->pattern_bitmask_basename, a->folder->basename,
            (RmPatternBitmask *)&b->pattern_bitmask_basename, b->folder->basename);
        (*regex_cursor)++;
        return sign * cmp;
    }
    case 'r': {
        int cmp = rm_pp_cmp_by_regex(
            g_ptr_array_index(session->pattern_cache, *regex_cursor), *regex_cursor,
            (RmPatternBitmask *)&a->pattern_bitmask_path, a_path,
            (RmPatternBitmask *)&b->pattern_bitmask_path, b_path);
        (*regex_cursor)++;
        return sign * cmp;
    }
    default:
        g_assert_not_reached();
        return 0;
    }
}

/* Sort criteria for sorting by preferred path (first) then user-input criteria */
/* Return:
 *      a negative integer file 'a' outranks 'b',
 *      0 if they are equal,
 *      a positive integer if file 'b' outranks 'a'
 */
int rm_pp_cmp_orig_criteria(const RmFile *a, const RmFile *b, const RmSession *session) {
    /* "other" lint outranks duplicates and has lower ENUM */
    RETURN_IF_NONZERO(SIGN_DIFF(a->lint_type, b->lint_type))

    RETURN_IF_NONZERO(SIGN_DIFF(a->is_symlink, b->is_symlink))

    RETURN_IF_NONZERO(SIGN_DIFF(b->is_prefd, a->is_prefd))

    /* Only fill in path if we have a pattern in sort_criteria */
    bool path_needed = (session->pattern_cache->len > 0);
    RM_DEFINE_PATH_IF_NEEDED(a, path_needed);
    RM_DEFINE_PATH_IF_NEEDED(b, path_needed);

    RmCfg *cfg = session->cfg;
    for(int i = 0, regex_cursor = 0; cfg->sort_criteria[i]; i++) {
        int res = rm_pp_cmp_criterion(cfg->sort_criteria[i], a, b, a_path, b_path,
                                      &regex_cursor, session);
        RETURN_IF_NONZERO(res);
    }
    return 0;
}

void rm_file_list_insert_file(RmFile *file, const RmSession *session) {
    g_mutex_lock(&session->tables->lock);
    { g_queue_push_tail(session->tables->all_files, file); }
    g_mutex_unlock(&session->tables->lock);
}

void rm_file_tables_clear(const RmSession *session) {
    GHashTableIter iter;
    gpointer key;

    g_hash_table_iter_init(&iter, session->tables->node_table);
    while(g_hash_table_iter_next(&iter, &key, NULL)) {
        RmFile *file = key;
        if(file) {
            rm_file_destroy(file);
        }
    }
}

/* if file is not DUPE_CANDIDATE then send it to session->tables->other_lint and
 * return 1; else return 0 */
static gboolean rm_pp_handle_other_lint(RmFile *file, const RmSession *session) {
    if(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE) {
        return FALSE;
    }

    if(session->cfg->filter_mtime && file->mtime < session->cfg->min_mtime) {
        rm_file_destroy(file);
    } else if((session->cfg->keep_all_tagged && file->is_prefd) ||
              (session->cfg->keep_all_untagged && !file->is_prefd)) {
        /* "Other" lint protected by --keep-all-{un,}tagged */
        rm_file_destroy(file);
    } else {
        session->tables->other_lint[file->lint_type] =
            g_list_prepend(session->tables->other_lint[file->lint_type], file);
    }
    return TRUE;
}

static gboolean rm_pp_check_path_double(RmFile *file, GHashTable *unique_paths_table) {
    RmPathDoubleKey *key = rm_path_double_new(file);

    /* Lookup if there is a file with the same path */
    RmPathDoubleKey *match_double_key = g_hash_table_lookup(unique_paths_table, key);

    if(match_double_key == NULL) {
        g_hash_table_add(unique_paths_table, key);
        return FALSE;
    }

    g_assert(match_double_key->file != file);

    rm_path_double_free(key);
    rm_file_destroy(file);
    return TRUE;
}

static gint rm_pp_handle_hardlink(RmFile *file, RmFile *head) {
    if(file == head) {
        return FALSE;
    }

    if(head->hardlinks) {
        /* bundle hardlink */
        rm_file_hardlink_add(head, file);
    }

    /* remove file from inode_cluster queue */
    return TRUE;
}

/* Preprocess files, including embedded hardlinks.  Any embedded hardlinks
 * that are "other lint" types are sent to rm_pp_handle_other_lint.  If the
 * file itself is "other lint" types it is likewise sent to rm_pp_handle_other_lint.
 * If there are no files left after this then return TRUE so that the
 * cluster can be deleted from the node_table hash table.
 * NOTE: we rely on rm_file_list_insert to select an RM_LINT_TYPE_DUPE_CANDIDATE as
 * head
 * file (unless ALL the files are "other lint"). */
static gboolean rm_pp_handle_inode_clusters(_UNUSED gpointer key, GQueue *inode_cluster,
                                            RmSession *session) {
    RmCfg *cfg = session->cfg;

    if(inode_cluster->length > 1) {
        /* there is a cluster of inode matches */

        /* remove path doubles.
         * Special case for --equal; consider this:
         * $ rmlint --equal link_to_xyz other_link_to_xyz
         *
         * Two symbolic links to the same file should be seen as equal.
         * Normally rmlint will use realpath() to resolve explicitly given symlinks
         * and remove the paths double later on here. Disable for --equal therefore.
         * */
        if(!session->cfg->run_equal_mode) {
            session->total_filtered_files -=
                rm_util_queue_foreach_remove(inode_cluster, (RmRFunc)rm_pp_check_path_double,
                                             session->tables->unique_paths_table);
        }

        /* clear the hashtable ready for the next cluster */
        g_hash_table_remove_all(session->tables->unique_paths_table);
    }

    /* process and remove other lint */
    session->total_filtered_files -= rm_util_queue_foreach_remove(
        inode_cluster, (RmRFunc)rm_pp_handle_other_lint, (RmSession *)session);

    if(inode_cluster->length > 1) {
        /* bundle or free the non-head files */
        RmFile *headfile = inode_cluster->head->data;
        if(cfg->find_hardlinked_dupes) {
            /* prepare to bundle files under the hardlink head */
            rm_file_hardlink_add(headfile, headfile);
        }

        /* hardlink cluster are counted as filtered files since they are either
         * ignored or treated as automatic duplicates depending on settings (so
         * no effort either way); rm_pp_handle_hardlink will either free or bundle
         * the hardlinks depending on value of headfile->hardlinks.is_head.
         */
        session->total_filtered_files -= rm_util_queue_foreach_remove(
            inode_cluster, (RmRFunc)rm_pp_handle_hardlink, headfile);
    }

    /* update counters */
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);

    g_assert(inode_cluster->length <= 1);
    if(inode_cluster->length == 1) {
        session->tables->size_groups->data = g_slist_prepend(
            session->tables->size_groups->data, inode_cluster->head->data);
    }

    return TRUE;
}

static int rm_pp_cmp_reverse_alphabetical(const RmFile *a, const RmFile *b) {
    RM_DEFINE_PATH(a);
    RM_DEFINE_PATH(b);
    return g_strcmp0(b_path, a_path);
}

static RmOff rm_pp_handler_other_lint(const RmSession *session) {
    RmOff num_handled = 0;
    RmFileTables *tables = session->tables;

    for(RmOff type = 0; type < RM_LINT_TYPE_DUPE_CANDIDATE; ++type) {
        if(type == RM_LINT_TYPE_EMPTY_DIR) {
            tables->other_lint[type] = g_list_sort(
                tables->other_lint[type], (GCompareFunc)rm_pp_cmp_reverse_alphabetical);
        }

        GList *list = tables->other_lint[type];
        for(GList *iter = list; iter; iter = iter->next) {
            RmFile *file = iter->data;

            g_assert(file);
            g_assert(type == file->lint_type);

            num_handled++;

            rm_fmt_write(file, session->formats, -1);
        }

        if(!session->cfg->cache_file_structs) {
            g_list_free_full(list, (GDestroyNotify)rm_file_destroy);
        } else {
            g_list_free(list);
        }
    }

    return num_handled;
}

/* This does preprocessing including handling of "other lint" (non-dupes)
 * After rm_preprocess(), all remaining duplicate candidates are in
 * a jagged GSList of GSLists as follows:
 * session->tables->size_groups->group1->file1a
 *                                     ->file1b
 *                                     ->file1c
 *                             ->group2->file2a
 *                                     ->file2b
 *                                       etc
 */
void rm_preprocess(RmSession *session) {
    RmFileTables *tables = session->tables;
    GQueue *all_files = tables->all_files;

    session->total_filtered_files = session->total_files;

    /* initial sort by size */
    g_queue_sort(all_files, (GCompareDataFunc)rm_file_cmp_full, session);
    rm_log_debug_line("initial size sort finished at time %.3f; sorted %d files",
                      g_timer_elapsed(session->timer, NULL),
                      session->total_files);

    /* split into file size groups; for each size, remove path doubles and bundle
     * hardlinks */
    g_assert(all_files->head);
    RmFile *file = g_queue_pop_head(all_files);
    RmFile *current_size_file = file;
    guint removed = 0;
    GHashTable *node_table = tables->node_table;
    while(file && !rm_session_was_aborted()) {
        /* group files into inode clusters */
        GQueue *inode_cluster =
            rm_hash_table_setdefault(node_table, file, (RmNewFunc)g_queue_new);

        g_queue_push_tail(inode_cluster, file);

        /* get next file and check if it is part of the same group */
        file = g_queue_pop_head(all_files);
        if(!file || rm_file_cmp_split(file, current_size_file, session) != 0) {
            /* process completed group (all same size & other criteria)*/
            /* remove path doubles and handle "other" lint */

            /* add an empty GSlist to our list of lists */
            tables->size_groups = g_slist_prepend(tables->size_groups, NULL);

            removed += g_hash_table_foreach_remove(
                node_table, (GHRFunc)rm_pp_handle_inode_clusters, session);

            /* free up the node table for the next group */
            g_hash_table_steal_all(node_table);
            if(tables->size_groups->data == NULL) {
                /* zero size group after handling other lint; remove it */
                tables->size_groups =
                    g_slist_delete_link(tables->size_groups, tables->size_groups);
            }
        }

        current_size_file = file;
    }

    session->other_lint_cnt += rm_pp_handler_other_lint(session);

    rm_log_debug_line(
        "path doubles removal/hardlink bundling/other lint finished at %.3f; removed "
        "%u "
        "of %d",
        g_timer_elapsed(session->timer, NULL), removed, session->total_files);

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);
}
