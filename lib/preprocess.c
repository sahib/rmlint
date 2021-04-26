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

#include "preprocess.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "formats.h"

static gint rm_file_cmp_with_extension(const RmFile *file_a, const RmFile *file_b) {
    char *ext_a = rm_util_path_extension(file_a->node->basename);
    char *ext_b = rm_util_path_extension(file_b->node->basename);

    if(ext_a && ext_b) {
        return g_ascii_strcasecmp(ext_a, ext_b);
    } else {
        /* file with an extension outranks one without */
        return (!!ext_a - !!ext_b);
    }
}

static gint rm_file_cmp_without_extension(const RmFile *file_a, const RmFile *file_b) {
    const char *basename_a = file_a->node->basename;
    const char *basename_b = file_b->node->basename;

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


// returns 0 if hardlink or path double, else sorts
static gint rm_file_cmp_hardlink(const RmFile *a, const RmFile *b) {
    RETURN_IF_NONZERO(SIGN_DIFF(a->actual_file_size, b->actual_file_size));
    RETURN_IF_NONZERO(SIGN_DIFF(rm_file_dev(a), rm_file_dev(b)));
    return SIGN_DIFF(rm_file_inode(a), rm_file_inode(b));
}

static gint rm_file_cmp_hardlink_full(const RmFile *a, const RmFile *b, RmSession *session) {
    RETURN_IF_NONZERO(rm_file_cmp_hardlink(a, b));
    return rm_pp_cmp_orig_criteria(a, b, session);
}

// returns 0 if same file / path double, else sorts
static gint rm_file_cmp_samefile(const RmFile *a, const RmFile *b) {
    RETURN_IF_NONZERO(rm_file_cmp_hardlink(a, b));
    RETURN_IF_NONZERO(SIGN_DIFF(
            rm_file_parent_inode(a), rm_file_parent_inode(b)));
    return g_strcmp0(a->node->basename, b->node->basename);
}

static gint rm_file_cmp_samefile_full(const RmFile *a, const RmFile *b, RmSession *session) {
    RETURN_IF_NONZERO(rm_file_cmp_samefile(a, b));
    return rm_pp_cmp_orig_criteria(a, b, session);
}

RmFileTables *rm_file_tables_new(_UNUSED const RmSession *session) {
    RmFileTables *tables = g_slice_new0(RmFileTables);

    tables->all_files = g_queue_new();

    g_mutex_init(&tables->lock);
    return tables;
}


void rm_file_tables_destroy(RmFileTables *tables) {
    if(tables->all_files) {
        g_queue_free(tables->all_files);
    }

    // walk along tables->size_groups, cleaning up as we go:
    while(tables->size_groups) {
        GSList *list = tables->size_groups->data;
        g_slist_free_full(list, (GDestroyNotify)rm_file_unref);
        tables->size_groups =
                g_slist_delete_link(tables->size_groups, tables->size_groups);
    }

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
    int result_a = 0;
    int result_b = 0;

    if(RM_PATTERN_IS_CACHED(mask_a, idx)) {
        /* Get the previous match result */
        result_a = RM_PATTERN_GET_CACHED(mask_a, idx);
    } else {
        /* Match for the first time */
        result_a = g_regex_match(regex, path_a, 0, NULL);
        RM_PATTERN_SET_CACHED(mask_a, idx, result_a);
    }

    if(RM_PATTERN_IS_CACHED(mask_b, idx)) {
        /* Get the previous match result */
        result_b = RM_PATTERN_GET_CACHED(mask_b, idx);
    } else {
        /* Match for the first time */
        result_b = g_regex_match(regex, path_b, 0, NULL);
        RM_PATTERN_SET_CACHED(mask_b, idx, result_b);
    }

    return SIGN_DIFF(result_b, result_a);
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
        return sign * g_ascii_strcasecmp(a->node->basename, b->node->basename);
    case 'f': {
        RM_DEFINE_DIR_PATH(a);
        RM_DEFINE_DIR_PATH(b);
        return sign * strcmp(a_dir_path, b_dir_path);
    }
    case 'l':
        return sign * SIGN_DIFF(strlen(a->node->basename), strlen(b->node->basename));
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
            (RmPatternBitmask *)&a->pattern_bitmask_basename, a->node->basename,
            (RmPatternBitmask *)&b->pattern_bitmask_basename, b->node->basename);
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


/* if file is not DUPE_CANDIDATE then send it to session->tables->other_lint and
 * return 1; else return 0 */
static gint rm_pp_sift_other_lint(RmFile *file, const RmSession *session) {
    if(file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE) {
        return 0;
    }

    if(file->lint_type == RM_LINT_TYPE_DUPE_DIR_CANDIDATE) {
        session->tables->other_lint[file->lint_type] =
                    g_list_prepend(session->tables->other_lint[file->lint_type], file);
    } else if(session->cfg->filter_mtime && file->mtime < session->cfg->min_mtime) {
        rm_file_unref(file);
    } else if((session->cfg->keep_all_tagged && file->is_prefd) ||
              (session->cfg->keep_all_untagged && !file->is_prefd)) {
        /* "Other" lint protected by --keep-all-{un,}tagged */
        rm_file_unref(file);
    } else {
        session->tables->other_lint[file->lint_type] =
            g_list_prepend(session->tables->other_lint[file->lint_type], file);
    }
    return 1;
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

            file->twin_count = -1;
            rm_fmt_write(file, session->formats);
            rm_file_unref(file);
        }

        g_list_free(list);
    }

    return num_handled;
}



/* remove path doubles / samefiles */
static gint rm_pp_remove_path_doubles(RmSession *session, GQueue *files) {
    guint removed = 0;
    /* sort which will move path doubles together and then rank them
     * in order of -S criteria
     */
    g_queue_sort(files, (GCompareDataFunc)rm_file_cmp_samefile_full, session);

    rm_log_debug_line("initial size sort finished at time %.3f; sorted %d files",
                      g_timer_elapsed(session->timer, NULL),
                      session->total_files);

    for(GList *i = files->head; i && i->next; ) {
        RmFile *this = i->data;
        RmFile *next = i->next->data;
        if(rm_file_cmp_samefile(this, next) == 0) {
            rm_file_unref(next);
            g_queue_delete_link(files, i->next);
            removed++;
        }
        else {
            i = i->next;
        }
    }
    return removed;
}

/* bundle or remove hardlinks */
static gint rm_pp_bundle_hardlinks(RmSession *session, GQueue *files) {
    guint removed = 0;
    /* sort so that files are in size order, and hardlinks are adjacent and
     * in -S criteria order, */
    g_queue_sort(files, (GCompareDataFunc)rm_file_cmp_hardlink_full, session);

    for(GList *i = files->head; i && i->next; ) {
        RmFile *this = i->data;
        RmFile *next = i->next->data;
        if(rm_file_cmp_hardlink(this, next) == 0) {
            // it's a hardlink of prev
            if(session->cfg->find_hardlinked_dupes) {
                /* bundle next file under previous->hardlinks */
                rm_file_hardlink_add(this, next);
            }
            else {
                rm_file_unref(next);
            }
            g_queue_delete_link(files, i->next);
            removed++;
        }
        else {
            i = i->next;
        }
    }
    return removed;
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

    /* remove path doubles and samefiles */
    guint removed = rm_pp_remove_path_doubles(session, all_files);

    /* remove non-dupe lint types from all_files and move them to
     * appropriate list in session->tables->other_lint */
    removed += rm_util_queue_foreach_remove(
            all_files, (RmRFunc)rm_pp_sift_other_lint, session);

    /* bundle (or free) hardlinks */
    removed += rm_pp_bundle_hardlinks(session, all_files);

    /* sort into size groups, also sorting according to --match-basename etc */
    g_queue_sort(all_files, (GCompareDataFunc)rm_file_cmp, session);

    RmFile *prev = NULL, *curr = NULL;
    while((curr = g_queue_pop_head(all_files))) {
        // check if different size to prev (or needs to split on basename
        // criteria etc)
        if(!prev || rm_file_cmp(prev, curr) != 0) {
            // create a new (empty) size group
            tables->size_groups = g_slist_prepend(tables->size_groups, NULL);
        }
        // prepend file to current size group
        tables->size_groups->data = g_slist_prepend(tables->size_groups->data, curr);
        prev = curr;
    }

    session->total_filtered_files-= removed;
    session->other_lint_cnt += rm_pp_handler_other_lint(session);

    rm_log_debug_line(
        "path doubles removal/hardlink bundling/other lint finished at %.3f; removed "
        "%u "
        "of %d",
        g_timer_elapsed(session->timer, NULL), removed, session->total_files);

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);
}
