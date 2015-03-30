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
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "preprocess.h"
#include "utilities.h"
#include "formats.h"
#include "cmdline.h"
#include "shredder.h"

static guint rm_file_hash(RmFile *file) {
    return (guint)(file->file_size);
}

static bool rm_file_check_with_extension(const RmFile *file_a, const RmFile *file_b) {
    char *ext_a = rm_util_path_extension(file_a->basename);
    char *ext_b = rm_util_path_extension(file_b->basename);

    if(ext_a && ext_b && g_ascii_strcasecmp(ext_a, ext_b) == 0) {
        return true;
    } else {
        return false;
    }
}

static bool rm_file_check_without_extension(const RmFile *file_a, const RmFile *file_b) {
    char *ext_a = rm_util_path_extension(file_a->basename);
    char *ext_b = rm_util_path_extension(file_b->basename);

    /* Check length till extension, or full length if none present */
    size_t a_len = (ext_a) ? (ext_a - file_a->basename) : (int)strlen(file_a->basename);
    size_t b_len = (ext_b) ? (ext_b - file_b->basename) : (int)strlen(file_b->basename);

    if(a_len != b_len) {
        return false;
    }

    if(g_ascii_strncasecmp(file_a->basename, file_b->basename, a_len) == 0) {
        return true;
    }

    return false;
}

static gboolean rm_file_equal(const RmFile *file_a, const RmFile *file_b) {
    const RmCfg *cfg = file_a->cfg;

    return (1
            && (file_a->file_size == file_b->file_size)
            && (0
                || (!cfg->match_basename)
                || (g_ascii_strcasecmp(file_a->basename, file_b->basename) == 0)
               )
            && (0
                || (!cfg->match_with_extension)
                || (rm_file_check_with_extension(file_a, file_b))
               )
            && (0
                || (!cfg->match_without_extension)
                || (rm_file_check_without_extension(file_a, file_b))
               )
           );
}

static guint rm_node_hash(const RmFile *file) {
    return file->inode ^ file->dev;
}

static gboolean rm_node_equal(const RmFile *file_a, const RmFile *file_b) {
    return (1
            && (file_a->inode == file_b->inode)
            && (file_a->dev   == file_b->dev)
           );
}

RmFileTables *rm_file_tables_new(RmSession *session) {
    RmFileTables *tables = g_slice_new0(RmFileTables);

    tables->size_groups = g_hash_table_new_full(
                              (GHashFunc)rm_file_hash, (GEqualFunc)rm_file_equal, NULL, NULL
                          );

    tables->node_table = g_hash_table_new_full(
                             (GHashFunc)rm_node_hash, (GEqualFunc)rm_node_equal, NULL, NULL
                         );


    RmCfg *cfg = session->cfg;

    g_assert(cfg);

    g_rec_mutex_init(&tables->lock);
    return tables;
}

void rm_file_tables_destroy(RmFileTables *tables) {
    g_rec_mutex_lock(&tables->lock);
    {
        g_assert(tables->node_table);
        g_hash_table_unref(tables->node_table);

        g_assert(tables->size_groups);
        g_hash_table_unref(tables->size_groups);

    }
    g_rec_mutex_unlock(&tables->lock);
    g_rec_mutex_clear(&tables->lock);
    g_slice_free(RmFileTables, tables);
}

/*  compare two files. return:
 *    - a negative integer file 'a' outranks 'b',
 *    - 0 if they are equal,
 *    - a positive integer if file 'b' outranks 'a'
 */
int rm_pp_cmp_orig_criteria_impl(
    RmSession *session,
    time_t mtime_a, time_t mtime_b,
    const char *basename_a, const char *basename_b,
    int path_index_a, int path_index_b
) {
    RmCfg *sets = session->cfg;

    int sort_criteria_len = strlen(sets->sort_criteria);
    for (int i = 0; i < sort_criteria_len; i++) {
        long cmp = 0;
        switch (tolower(sets->sort_criteria[i])) {
        case 'm':
            cmp = (long)(mtime_a) - (long)(mtime_b);
            break;
        case 'a':
            cmp = g_ascii_strcasecmp(basename_a, basename_b);
            break;
        case 'p':
            cmp = (long)path_index_a - (long)path_index_b;
            break;
        }
        if (cmp) {
            /* reverse order if uppercase option (M|A|P) */
            cmp = cmp * (isupper(sets->sort_criteria[i]) ? -1 : +1);
            return cmp;
        }
    }
    return 0;
}

/* Sort criteria for sorting by preferred path (first) then user-input criteria */
/* Return:
 *      a negative integer file 'a' outranks 'b',
 *      0 if they are equal,
 *      a positive integer if file 'b' outranks 'a'
 */
int rm_pp_cmp_orig_criteria(RmFile *a, RmFile *b, RmSession *session) {
    if (a->lint_type != b->lint_type) {
        /* "other" lint outranks duplicates and has lower ENUM */
        return a->lint_type - b->lint_type;
    } else if(a->is_prefd != b->is_prefd) {
        return (b->is_prefd - a->is_prefd);
    } else {
        return rm_pp_cmp_orig_criteria_impl(
                   session,
                   a->mtime, b->mtime,
                   a->basename, b->basename,
                   a->path_index, b->path_index
               );
    }
}

/* initial list build, including kicking out path doubles and grouping of hardlinks */
bool rm_file_tables_insert(RmSession *session, RmFile *file) {
    RmFileTables *tables = session->tables;

    GHashTable *node_table = tables->node_table;

    bool result = true;

    g_rec_mutex_lock(&tables->lock);
    {
        RmFile *inode_match = g_hash_table_lookup(node_table, file);
        if (!inode_match) {
            g_hash_table_insert(node_table, file, file);
        } else {
            /* file(s) with matching dev, inode(, basename) already in table...
             * fails if the hardlinked file has been written to during traversal; so
             * instead we just print a warning
             * */
            if(inode_match->file_size != file->file_size) {
                rm_log_warning_line(_("Hardlink file size changed during traversal: %s"), file->path);
            }

            /* if this is the first time, set up the hardlinks.files queue */
            if (!inode_match->hardlinks.files) {
                inode_match->hardlinks.files = g_queue_new();

                /*NOTE: during list build, the hardlinks.files queue includes the file
                 * itself, as well as its hardlinks.  This makes operations
                 * in rm_file_tables_insert much simpler but complicates things later on,
                 * so the head file gets removed from the hardlinks.files queue
                 * in rm_pp_handle_hardlinks() during preprocessing */
                g_queue_push_head(inode_match->hardlinks.files, inode_match);
            }

            /* make sure the highest-ranked hardlink is "boss" */
            if (rm_pp_cmp_orig_criteria(file, inode_match, session) < 0) {
                /*this file outranks existing existing boss; swap */
                /* NOTE: it's important that rm_file_list_insert selects a RM_LINT_TYPE_DUPE_CANDIDATE
                 * as head file, unless all the files are "other lint".  This is achieved via rm_pp_cmp_orig_criteria*/
                file->hardlinks.files = inode_match->hardlinks.files;
                inode_match->hardlinks.files = NULL;
                g_hash_table_insert(node_table, file, file); /* replaces key and data*/
                inode_match = file;
            }

            /* compare this file to all of the existing ones in the cluster
             * to check if it's a path double; if yes then swap or discard */
            for (GList *iter = inode_match->hardlinks.files->head; iter; iter = iter->next) {
                RmFile *iter_file = iter->data;
                if (1
                        && (g_strcmp0(file->basename, iter_file->basename) == 0)
                        /* double paths and loops will always have same basename
                         * (cheap call to potentially avoid the next call which requires a rm_sys_stat()) */
                        && (rm_util_parent_node(file->path) == rm_util_parent_node(iter_file->path))
                        /* double paths and loops will always have same dir inode number*/
                   ) {
                    /* file is path double or filesystem loop - kick one or the other */
                    if (rm_pp_cmp_orig_criteria(file, iter->data, session) < 0) {
                        /* file outranks iter */
                        rm_log_debug("Ignoring path double %s, keeping %s\n", iter_file->path, file->path);
                        iter->data = file;
                        g_assert (iter_file != inode_match); /* it would be a bad thing to destroy the hashtable key */
                        rm_file_destroy(iter_file);
                    } else {
                        rm_log_debug("Ignoring path double %s, keeping %s\n", file->path, iter_file->path);
                        rm_file_destroy(file);
                    }
                    result = false;
                    break;
                }
            }

            if(result == true) {
                /* no path double found; must be hardlink */
                g_queue_insert_sorted (
                    inode_match->hardlinks.files,
                    file,
                    (GCompareDataFunc)rm_pp_cmp_orig_criteria,
                    session
                );
            }
        }
    }
    g_rec_mutex_unlock(&tables->lock);
    return result;
}

/* if file is not DUPE_CANDIDATE then send it to session->tables->other_lint
 * and return true; else return false */
static bool rm_pp_handle_other_lint(RmSession *session, RmFile *file) {
    if (file->lint_type != RM_LINT_TYPE_DUPE_CANDIDATE) {
        if(session->cfg->filter_mtime && file->mtime < session->cfg->min_mtime) {
            rm_file_destroy(file);
            return true;
        }

        session->tables->other_lint[file->lint_type] = g_list_prepend(
                    session->tables->other_lint[file->lint_type],
                    file
                );
        return true;
    } else {
        return false;
    }
}

static bool rm_pp_handle_own_files(RmSession *session, RmFile *file) {
    return rm_fmt_is_a_output(session->formats, file->path);
}

/* Preprocess files, including embedded hardlinks.  Any embedded hardlinks
 * that are "other lint" types are sent to rm_pp_handle_other_lint.  If the
 * file itself is "other lint" types it is likewise sent to rm_pp_handle_other_lint.
 * If there are no files left after this then return TRUE so that the
 * cluster can be deleted from the node_table hash table.
 * NOTE: we rely on rm_file_list_insert to select a RM_LINT_TYPE_DUPE_CANDIDATE as head
 * file (unless ALL the files are "other lint"). */
static gboolean rm_pp_handle_hardlinks(_U gpointer key, RmFile *file, RmSession *session) {
    g_assert(file);
    RmCfg *cfg = session->cfg;

    if(file->hardlinks.files) {
        /* it has a hardlink cluster - process each file (except self) */
        /* remove self */
        {
            bool was_removed = g_queue_remove(file->hardlinks.files, file);

            /* Do not call that directly in g_assert, might be disabled at
             * compile time. */
            g_assert(was_removed);
        }

        for (GList *iter = file->hardlinks.files->head, *next = NULL; iter; iter = next) {
            /* Remember next element early */
            next = iter->next;

            /* call self to handle each embedded hardlink */
            RmFile *embedded = iter->data;
            g_assert(!embedded->hardlinks.files);

            if (rm_pp_handle_hardlinks(NULL, embedded, session)) {
                g_queue_delete_link(file->hardlinks.files, iter);
            } else if (!cfg->find_hardlinked_dupes) {
                rm_file_destroy(embedded);
                g_queue_delete_link(file->hardlinks.files, iter);
            }
        }

        if (g_queue_is_empty(file->hardlinks.files)) {
            g_queue_free(file->hardlinks.files);
            file->hardlinks.files = NULL;
        }
    }

    /* handle the head file; if it's "other lint" then process it via rm_pp_handle_other_lint
     * and return TRUE, else keep it
     */
    bool remove = rm_pp_handle_own_files(session, file);

    if(remove == false && rm_pp_handle_other_lint(session, file)) {
        remove = true;
    } else if(remove == true) {
        /*
        * Also check if the file is a output of rmlint itself. Which we definitely
        * not want to handle. Creating a script that deletes itself is fun but useless.
        * */
        rm_file_destroy(file);
    }

    session->total_filtered_files -= remove;
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);

    return remove;
}

static int rm_pp_cmp_reverse_alphabetical(const RmFile *a, const RmFile *b) {
    return g_strcmp0(b->path, a->path);
}

static RmOff rm_pp_handler_other_lint(RmSession *session) {
    RmOff num_handled = 0;
    RmFileTables *tables = session->tables;

    for(RmOff type = 0; type < RM_LINT_TYPE_DUPE_CANDIDATE; ++type) {
        if (type == RM_LINT_TYPE_EMPTY_DIR) {
            tables->other_lint[type] = g_list_sort(
                                           tables->other_lint[type],
                                           (GCompareFunc)rm_pp_cmp_reverse_alphabetical
                                       );
        }

        GList *list = tables->other_lint[type];
        for(GList *iter = list; iter; iter = iter->next) {
            RmFile *file = iter->data;

            g_assert(file);
            g_assert(type == file->lint_type);

            num_handled++;
            rm_fmt_write(file, session->formats);
        }
        g_list_free_full(list, (GDestroyNotify)rm_file_destroy);
    }

    return num_handled;
}

/* This does preprocessing including handling of "other lint" (non-dupes) */
void rm_preprocess(RmSession *session) {
    RmFileTables *tables = session->tables;
    g_assert(tables->node_table);

    session->total_filtered_files = session->total_files;

    /* process hardlink groups, and move other_lint into tables- */
    guint removed = g_hash_table_foreach_remove(
                        tables->node_table,
                        (GHRFunc)rm_pp_handle_hardlinks,
                        session
                    );

    rm_log_debug(
        "process hardlink groups finished at time %.3f; removed %u of %d\n",
        g_timer_elapsed(session->timer, NULL), removed, session->total_files
    );

    session->other_lint_cnt += rm_pp_handler_other_lint(session);
    rm_log_debug(
        "Other lint handling finished at time %.3f\n",
        g_timer_elapsed(session->timer, NULL)
    );

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);
}
