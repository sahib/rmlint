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
#include "rank.h"


////////////////////////////////////////////////
// Handling of "Other Lint" (not duplicates)  //
////////////////////////////////////////////////


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

/*  for sorting emptydirs into bottom-up order for bottom-up deletion */
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


///////////////////////////////
//     Sort Functions        //
///////////////////////////////

/* Much of the preprocessing is done by sorting.  For example, hardlinks are
 * detected by sorting files by dev/inode, which is guaranteed to bring
 * hardlinks together, and then comparing adjacent members.  A more conventional
 * approach would be to use a hashtable with <dev,inode> as key.  However, for
 * a use-once-and-then-discard application like this we have found that the
 * sort-based method is about 30% quicker and uses less RAM.
 *
 * This section defines the sort functions used for preprocessing.
 *
 */





/*--------------------------------------------------------------------*/
/*  hardlink and samefile detection                                   */


/* Compare func to sort two RmFiles into "hardlink" order.  Sorting a list of
 * files according to this func will guarantee that hardlinked files are
 * adjacent to each other.  Hardlinks can then be found by finding
 * adjacent files where rm_file_cmp_hardlink(a, b) == 0 */
 static gint rm_file_cmp_hardlink(const RmFile *a, const RmFile *b) {
    RETURN_IF_NONZERO(SIGN_DIFF(a->actual_file_size, b->actual_file_size));
    RETURN_IF_NONZERO(SIGN_DIFF(rm_file_dev(a), rm_file_dev(b)));
    return SIGN_DIFF(rm_file_inode(a), rm_file_inode(b));
}

/* Compare func as per rm_file_cmp_hardlink except that, additionally, files
 * within each group of hardlinks are sorted in accordance with --rank-by
 * criteria.  This facilitates hardlink detection and identification of which
 * is the "original" hardlink */
static gint rm_file_cmp_hardlink_full(const RmFile *a, const RmFile *b, RmSession *session) {
    RETURN_IF_NONZERO(rm_file_cmp_hardlink(a, b));
    return rm_rank_orig_criteria(a, b, session);
}

/* Compare func to sort two RmFiles into "samefile" order.  Sorting a list of
 * files according to this func will guarantee that "samefiles" (two pointers
 * to the same file, for example '/path/file'=='/path/file'
 * or '/path/file' == '/bindmount/file') are adjacent to each other.
 * 'Samefile's can then be found by finding adjacent files where
 * rm_file_cmp_samefile(a, b) == 0 */
static gint rm_file_cmp_samefile(const RmFile *a, const RmFile *b) {
    RETURN_IF_NONZERO(rm_file_cmp_hardlink(a, b));
    RETURN_IF_NONZERO(SIGN_DIFF(
            rm_file_parent_inode(a), rm_file_parent_inode(b)));
    return g_strcmp0(a->node->basename, b->node->basename);
}

/* Compare func as per rm_file_cmp_samefile except that, additionally, files
 * within each group of samefile's are sorted in accordance with --rank-by
 * criteria.  This facilitates samefile detection and identification of which
 * samefile to discard */
static gint rm_file_cmp_samefile_full(const RmFile *a, const RmFile *b, RmSession *session) {
    RETURN_IF_NONZERO(rm_file_cmp_samefile(a, b));
    return rm_rank_orig_criteria(a, b, session);
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


///////////////////////////////
//     API Implementatin     //
///////////////////////////////


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

/* threadsafe addition of file to session->tables->all_files */
void rm_file_list_insert_file(RmFile *file, const RmSession *session) {
    g_mutex_lock(&session->tables->lock);
    { g_queue_push_tail(session->tables->all_files, file); }
    g_mutex_unlock(&session->tables->lock);
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

    rm_log_debug_line(
        "rm_preprocessstarting at %.3f;",
        g_timer_elapsed(session->timer, NULL));
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
    g_queue_sort(all_files, (GCompareDataFunc)rm_rank_group, session);

    RmFile *prev = NULL, *curr = NULL;
    while((curr = g_queue_pop_head(all_files))) {
        // check if different size to prev (or needs to split on basename
        // criteria etc)
        if(!prev || rm_rank_group(prev, curr) != 0) {
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
