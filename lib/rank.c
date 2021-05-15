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

#include "rank.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>



/* Parse pattern specified in `--rank-by x<pattern>` (rank by regex of basename)
 * or `--rank-by r<pattern>` (rank by regex of full path)
 */
static size_t rm_rank_parse_pattern(const char *pattern, GRegex **regex, GError **error) {
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


/* Compare path_a vs path_b according to regex; uses mask_a and mask_b
 * as a cache for the individual regex results */
static int rm_rank_by_regex(GRegex *regex, int idx, RmPatternBitmask *mask_a,
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
static int rm_rank_criterion(unsigned char criterion, const RmFile *a, const RmFile *b,
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
        int cmp = rm_rank_by_regex(
            g_ptr_array_index(session->pattern_cache, *regex_cursor), *regex_cursor,
            (RmPatternBitmask *)&a->pattern_bitmask_basename, a->node->basename,
            (RmPatternBitmask *)&b->pattern_bitmask_basename, b->node->basename);
        (*regex_cursor)++;
        return sign * cmp;
    }
    case 'r': {
        int cmp = rm_rank_by_regex(
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


///////////////////////////////
//     API Implementatin     //
///////////////////////////////

/*--------------------------------------------------------------------*/
/*  --match-basename, --match-extension and --match-without-extension */

/* Compare func to sort two RmFiles in order of filename extension
 * eg comparing "/path1/foo.c" vs "/path2/bar.c" should return 0 (both .c)
 * Used for --match-extension option.
 */
gint rm_rank_with_extension(const RmFile *file_a, const RmFile *file_b) {
    char *ext_a = rm_util_path_extension(file_a->node->basename);
    char *ext_b = rm_util_path_extension(file_b->node->basename);

    if(ext_a && ext_b) {
        return g_ascii_strcasecmp(ext_a, ext_b);
    } else {
        /* file with an extension outranks one without */
        return (!!ext_a - !!ext_b);
    }
}

/* Compare func to sort two RmFiles in order of filename excluding extension,
 * eg comparing "/path1/foo.c" vs "/path2/foo.h" should return 0 (both foo)
 * Used for --match-without-extension option */
gint rm_rank_without_extension(const RmFile *file_a, const RmFile *file_b) {
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


/* Sort criteria for sorting by preferred path (first) then user-input criteria */
/* Return:
 *      a negative integer file 'a' outranks 'b',
 *      0 if they are equal,
 *      a positive integer if file 'b' outranks 'a'
 */
int rm_rank_orig_criteria(const RmFile *a, const RmFile *b, const RmSession *session) {
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
        int res = rm_rank_criterion(cfg->sort_criteria[i], a, b, a_path, b_path,
                                      &regex_cursor, session);
        RETURN_IF_NONZERO(res);
    }
    return 0;
}

/* Compare func to sort two RmFiles into "group" order.  Sorting a list of
 * files according to this func will guarantee that potential duplicate files
 * (ie same size and conforming to optional --match-basename,
 * --match-extension and --match-without-extension settings) are adjacent to
 * each other.  The sorted list can then be split into groups of potential
 * duplicates by splitting whereever rm_rank_group(a, b) != 0 */
gint rm_rank_group(const RmFile *file_a, const RmFile *file_b) {

    RETURN_IF_NONZERO(SIGN_DIFF(file_a->file_size, file_b->file_size));

    RmCfg *cfg = file_a->session->cfg;

    RETURN_IF_NONZERO(cfg->match_basename && rm_rank_basenames(file_a, file_b));

    RETURN_IF_NONZERO(cfg->match_with_extension && rm_rank_with_extension(file_a, file_b));

    return cfg->match_without_extension && rm_rank_without_extension(file_a, file_b);

}

gint rm_rank_basenames(const RmFile *file_a, const RmFile *file_b) {
    return g_ascii_strcasecmp(file_a->node->basename, file_b->node->basename);
}


/* Compile all regexes inside a sortcriteria string.
 * Every regex (enclosed in <>)  will be removed from the
 * sortcriteria spec, so that the returned string will
 * consist only of single letters.
 */
char *rm_rank_compile_patterns(RmSession *session, const char *sortcrit, GError **error) {
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
        i += rm_rank_parse_pattern(&sortcrit[i + 1], &regex, error);

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
