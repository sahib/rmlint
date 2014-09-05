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

#include "preprocess.h"
#include "utilities.h"
#include "formats.h"
#include "cmdline.h"
#include "shredder.h"

RmFileTables *rm_file_tables_new(RmSession *session) {
    RmFileTables *tables = g_slice_new0(RmFileTables);

    tables->size_table = g_hash_table_new(NULL, NULL);

    tables->size_groups = g_hash_table_new_full(
        g_int64_hash, g_int64_equal, g_free, NULL
    );

    tables->node_table = g_hash_table_new(NULL, NULL);

    tables->orig_table = g_hash_table_new(NULL, NULL);

    RmSettings *settings = session->settings;

    g_assert(settings);

    /* table->other_lint needs no initialising*/
    tables->mounts = session->mounts;

    g_rec_mutex_init(&tables->lock);
    return tables;
}

void rm_file_tables_destroy(RmFileTables *tables) {
    g_rec_mutex_lock(&tables->lock);
    {
        g_assert(tables->node_table);
        g_hash_table_unref(tables->node_table);

        g_assert(tables->size_table);
        g_hash_table_unref(tables->size_table);

        g_assert(tables->size_groups);
        g_hash_table_unref(tables->size_groups);

        g_assert(tables->mounts);
        rm_mounts_table_destroy(tables->mounts);

        g_assert(tables->orig_table);
        g_hash_table_unref(tables->orig_table);

    }
    g_rec_mutex_unlock(&tables->lock);
    g_rec_mutex_clear(&tables->lock);
    g_slice_free(RmFileTables, tables);
}

/* Sort criteria for sorting by preferred path (first) then user-input criteria */
static long cmp_orig_criteria(RmFile *a, RmFile *b, RmSession *session) {
    RmSettings *sets = session->settings;

    if (a->lint_type != b->lint_type) {
        return a->lint_type - b->lint_type;
    } else if (a->is_prefd != b->is_prefd) {
        return a->is_prefd - b->is_prefd;
    } else {
        int sort_criteria_len = strlen(sets->sort_criteria);
        for (int i = 0; i < sort_criteria_len; i++) {
            long cmp = 0;
            switch (sets->sort_criteria[i]) {
            case 'm':
                cmp = (long)(a->mtime) - (long)(b->mtime);
                break;
            case 'M':
                cmp = (long)(b->mtime) - (long)(a->mtime);
                break;
            case 'a':
                cmp = +strcmp(a->basename, b->basename);
                break;
            case 'A':
                cmp = -strcmp(a->basename, b->basename);
                break;
            case 'p':
                cmp = (long)a->path_index - (long)b->path_index;
                break;
            case 'P':
                cmp = (long)b->path_index - (long)a->path_index;
                break;
            }
            if (cmp) {
                return cmp;
            }
        }
    }
    return 0;
}

void rm_file_tables_remember_original(RmFileTables *table, RmFile *file) {
    g_rec_mutex_lock(&table->lock);
    {
        g_hash_table_insert(table->orig_table, file, NULL);
    }
    g_rec_mutex_unlock(&table->lock);
}

bool rm_file_tables_is_original(RmFileTables *table, RmFile *file) {
    bool result = false;
    g_rec_mutex_lock(&table->lock);
    {
        result = g_hash_table_contains(table->orig_table, file);
    }
    g_rec_mutex_unlock(&table->lock);

    return result;
}


/* initial list build, including kicking out path doubles and grouping of hardlinks */
bool rm_file_tables_insert(RmSession *session, RmFile *file) {
    RmFileTables *tables = session->tables;

    GHashTable *node_table = tables->node_table;
    guint64 node_key = ((guint64)file->dev << 32) | ((guint64)file->inode);

    g_rec_mutex_lock(&tables->lock);

    RmFile *inode_match = g_hash_table_lookup(node_table, (gpointer)node_key );
    if (!inode_match) {
        g_hash_table_insert(node_table, (gpointer)node_key, file);
    } else {
        /* file(s) with matching dev,inode already in table... */

        /* if this is the first time, set up the hardlinks.files queue */
        if (!inode_match->hardlinks.files) {
            inode_match->hardlinks.files = g_queue_new();
            g_queue_push_head(inode_match->hardlinks.files, inode_match);
        }

        /* make sure the highest-ranked hardlink is "boss" */
        if ( cmp_orig_criteria(file, inode_match, session) > 0) {
            /*this file outranks existing existing boss; swap */
            /* NOTE: it's important that rm_file_list_insert selects a RM_LINT_TYPE_DUPE_CANDIDATE
             * as head file, unless all the files are "other lint".  This is achieved via cmp_orig_criteria*/
            file->hardlinks.files = inode_match->hardlinks.files;
            inode_match->hardlinks.files = NULL;
            g_hash_table_insert(node_table, (gpointer)node_key, file);
            inode_match = file;
        }

        /* compare this file to all of the existing ones in the cluster
         * to check if it's a path double; if yes then swap or discard */
        for (GList *iter = inode_match->hardlinks.files->head; iter; iter = iter->next) {
            RmFile *iter_file = iter->data;
            if (1
                    && (strcmp(rm_util_basename(file->path), rm_util_basename(iter_file->path)) == 0)
                    /* double paths and loops will always have same basename
                     * (cheap call to potentially avoid the next call which requires a rm_sys_stat()) */
                    && (rm_util_parent_node(file->path) == rm_util_parent_node(iter_file->path))
                    /* double paths and loops will always have same dir inode number*/
               ) {
                /* file is path double or filesystem loop - kick one or the other */
                if (cmp_orig_criteria(file, iter->data, session) > 0) {
                    /* file outranks iter */
                    rm_log_info("Ignoring path double %s, keeping %s\n", iter_file->path, file->path);
                    iter->data = file;
                    rm_file_destroy(iter_file);
                } else {
                    rm_log_info("Ignoring path double %s, keeping %s\n", file->path, iter_file->path);
                    rm_file_destroy(file);
                }
                g_rec_mutex_unlock(&tables->lock);
                return false;
            }
        }
        /* no path double found; must be hardlink */
        g_queue_insert_sorted (
            inode_match->hardlinks.files,
            file,
            (GCompareDataFunc)cmp_orig_criteria,
            session
        );
    }
    g_rec_mutex_unlock(&tables->lock);
    return true;
}

/* if file is not DUPE_CANDIDATE then send it to session->tables->other_lint
 * and return true; else return false */
static gboolean rm_handle_other_lint(RmFile *file, RmSession *session) {
    if (file->lint_type != RM_LINT_TYPE_DUPE_CANDIDATE) {
        session->tables->other_lint[file->lint_type] = g_list_prepend(
                    session->tables->other_lint[file->lint_type],
                    file);
        return true;
    } else {
        return false;
    }
}

/* preprocess hardlinked files via rm_handle_other_lint, stripping out
 * "other lint".  If there are no files left at the end then destroy the
 * head file and return TRUE so that the cluster can be deleted from the
 * node_table hash table */
static gboolean rm_handle_hardlinks(gpointer key, RmFile *file, RmSession *session) {
    (void)key;
    g_assert(file);
    RmSettings *settings = session->settings;

    if (file->hardlinks.files) {
        /* it has a hardlink cluster - process each file */
        GList *next = NULL;
        for (GList *iter = file->hardlinks.files->head; iter; iter = next) {
            next = iter->next;

            if ( rm_handle_other_lint(iter->data, session) ) {
                g_queue_delete_link(file->hardlinks.files, iter);
            } else if (iter->data != file && !settings->find_hardlinked_dupes) {
                /* settings say disregard hardlinked duplicates */
                rm_file_destroy(iter->data);
                g_queue_delete_link(file->hardlinks.files, iter);
            }
        }

        if ( !g_queue_is_empty(file->hardlinks.files) ) {
            /* remove self from the list to avoid messiness later */
            g_assert(g_queue_remove(file->hardlinks.files, file) );
        }
    }
    /* handle the head file; if it's "other lint" then send it there, else keep it
     * NOTE: it's important that rm_file_list_insert selects a RM_LINT_TYPE_DUPE_CANDIDATE as head
     * file, unless all the files are "other lint"*/
    return rm_handle_other_lint(file, session);
}


int cmp_reverse_alphabetical(char *a, char *b) {
    return -strcmp(a, b);
}

static guint64 handle_other_lint(RmSession *session) {
    guint64 num_handled = 0;

    RmFileTables *tables = session->tables;

    for(guint64 type = 0; type < RM_LINT_TYPE_DUPE_CANDIDATE; ++type) {
        if (type == RM_LINT_TYPE_EDIR) {
            tables->other_lint[type] = g_list_sort(
                                           tables->other_lint[type],
                                           (GCompareFunc)cmp_reverse_alphabetical
                                       );
        }

        GList *list = tables->other_lint[type];
        for(GList *iter = list; iter; iter = iter->next) {
            RmFile *file = iter->data;

            g_assert(file);
            g_assert(type == file->lint_type);

            num_handled++;

            rm_fmt_write(session->formats, file);
        }
        g_list_free_full(list, (GDestroyNotify)rm_file_destroy);
    }

    return num_handled;
}

/* This does preprocessing including handling of "other lint" (non-dupes) */
void rm_preprocess(RmSession *session) {
    RmSettings *settings = session->settings;
    RmFileTables *tables = session->tables;

    /* process hardlink groups, and move other_lint into tables- */
    g_assert(tables->node_table);
    g_hash_table_foreach_remove(
        tables->node_table,
        (GHRFunc)rm_handle_hardlinks,
        session
    );

    rm_log_debug(
        "process hardlink groups finished at time %.3f\n",
        g_timer_elapsed(session->timer, NULL)
    );

    session->other_lint_cnt += handle_other_lint(session);
    rm_log_debug(
        "Other lint handling finished at time %.3f\n",
        g_timer_elapsed(session->timer, NULL)
    );

    if(settings->searchdup == 0) {
        return;
    }

}
