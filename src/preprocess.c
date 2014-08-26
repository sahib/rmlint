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

static void dev_queue_free_func(gconstpointer p) {
    g_queue_free_full((GQueue *)p, (GDestroyNotify)rm_file_destroy);
}

typedef struct RmGroup {
    GQueue *queue;
    gulong num_pref;
    gulong num_non_pref;
    bool needs_hashing;
} RmGroup;

static RmGroup *rm_group_new(void) {
    RmGroup *self = g_slice_new0(RmGroup);
    self->queue = g_queue_new();
    g_assert(self->num_pref == 0);
    g_assert(self->num_non_pref == 0);
    g_assert(!self->needs_hashing);
    return self;
}

void rm_group_destroy(RmGroup *group) {
    g_queue_free_full(group->queue, (GDestroyNotify)rm_file_destroy);
    g_slice_free(RmGroup, group);
}


RmFileTables *rm_file_tables_new(RmSession *session) {

    RmFileTables *tables = g_slice_new0(RmFileTables);

    tables->dev_table = g_hash_table_new_full(
                            g_direct_hash, g_direct_equal,
                            NULL, (GDestroyNotify)dev_queue_free_func
                        );

    tables->size_table = g_hash_table_new( NULL, NULL);

    tables->size_groups = g_hash_table_new_full( NULL, NULL, NULL, (GDestroyNotify)rm_group_destroy);

    tables->node_table = g_hash_table_new_full( NULL, NULL, NULL, (GDestroyNotify)g_queue_free );

    tables->orig_table = g_hash_table_new(NULL, NULL);

    RmSettings *settings = session->settings;

    g_assert(settings);
    g_assert(settings->namecluster == 0);

    if (session->settings->namecluster) {
        tables->name_table = g_hash_table_new_full(
                                 g_str_hash, g_str_equal,
                                 g_free, (GDestroyNotify)g_list_free );
    } else {
        g_assert(tables->name_table ==  NULL);
    }

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

        g_assert(tables->dev_table);
        g_hash_table_unref(tables->dev_table);

        g_assert(tables->size_table);
        g_hash_table_unref(tables->size_table);

        g_assert(tables->size_groups);
        g_hash_table_unref(tables->size_groups);

        g_assert(tables->mounts);
        rm_mounts_table_destroy(tables->mounts);

        g_assert(tables->orig_table);
        g_hash_table_unref(tables->orig_table);

        if (tables->name_table) {
            g_hash_table_unref(tables->name_table);
        }

//   not needed because freed by handle_other_lint:
//        for (uint i = 0; i < sizeof(tables->other_lint) / sizeof(tables->other_lint[0]); i++) {
//            if (tables->other_lint[i]) {
//                g_list_free(tables->other_lint[i]); //TODO: free_full??
//            }
//        }
    }
    g_rec_mutex_unlock(&tables->lock);
    g_rec_mutex_clear(&tables->lock);
    g_slice_free(RmFileTables, tables);
}

//static gint rm_file_list_cmp_file(gconstpointer a, gconstpointer b, G_GNUC_UNUSED gpointer data) {
//    const RmFile *fa = a, *fb = b;
//    if (fa->inode != fb->inode)
//        return fa->inode - fb->inode;
//    else if (fa->dev != fb->dev)
//        return fa->dev - fb->dev;
//    else
//        return strcmp(rm_util_basename(fa->path), rm_util_basename(fb->path));
//}

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
                cmp = strcmp (rm_util_basename(a->path), rm_util_basename (b->path));
                break;
            case 'A':
                cmp = strcmp (rm_util_basename(b->path), rm_util_basename (a->path));
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
bool rm_file_list_insert(RmSession *session, RmFile *file) {
    RmFileTables *tables = session->tables;

    GHashTable *node_table = tables->node_table;
    guint64 node_key = (ulong)file->dev << 32 | (ulong)file->inode;

    g_rec_mutex_lock(&tables->lock);

    GQueue *hardlink_group = g_hash_table_lookup(node_table, (gpointer)node_key );
    if (!hardlink_group) {
        hardlink_group = g_queue_new();
        g_hash_table_insert(node_table, (gpointer)node_key, hardlink_group);
    } else {
        /* file(s) with matching dev,inode already in table; compare this file to all of the
           existing ones to check if it's a path double; if yes then swap or discard */
        for (GList *iter = hardlink_group->head; iter; iter = iter->next) {
            RmFile *current = iter->data;
            g_assert(current);
            if (1
                    && (strcmp(rm_util_basename(file->path), rm_util_basename(current->path)) == 0)
                    /* double paths and loops will always have same basename
                     * (cheap call to potentially avoid the next call which requires a stat()) */
                    && (rm_util_parent_node(file->path) == rm_util_parent_node(current->path))
                    /* double paths and loops will always have same dir inode number*/
               ) {
                /* file is path double or filesystem loop - kick one or the other */
                if ( cmp_orig_criteria(file, current, session) > 0) {
                    /* file outranks current */
                    info("Ignoring path double %s, keeping %s\n", current->path, file->path);
                    rm_file_destroy(current);
                    iter->data = file;
                } else {
                    info("Ignoring path double %s, keeping %s\n", file->path, current->path);
                    rm_file_destroy(file);
                }
                g_rec_mutex_unlock(&tables->lock);
                return false;
            }
        }
        /* no path double found; must be hardlink */
    }
    g_queue_insert_sorted (
        hardlink_group,
        file,
        (GCompareDataFunc)cmp_orig_criteria,
        session);
    g_rec_mutex_unlock(&tables->lock);
    return true;
}



///* If we have more than one path, or a fs loop, several RMFILEs may point to the
// * same (physically same!) file.  This would result in potentially dangerous
// * false positives where the "duplicate" that gets deleted is actually the
// * original rm_file_list_remove_double_paths() searches for and removes items in
// * GROUP  which are pointing to the same file.  Depending on settings, also
// * removes hardlinked duplicates sets, keeping just one of each set.
// * Returns number of files removed from GROUP.
// * */
//static guint rm_file_list_remove_double_paths(GQueue *group, RmSession *session) {
//    RmSettings *settings = session->settings;
//    guint removed_cnt = 0;
//    g_assert(group);
//
//    g_queue_sort(group, rm_file_list_cmp_file, NULL);
//
//    GList *iter = group->head;
//    while(iter && iter->next) {
//        RmFile *file = iter->data, *next_file = iter->next->data;
//
//        if (file->inode == next_file->inode && file->dev == next_file->dev) {
//            /* files have same dev and inode:  might be hardlink (safe to delete), or
//             * two paths to the same original (not safe to delete) */
//            if   (0
//                    || (!settings->find_hardlinked_dupes)
//                    /* not looking for hardlinked dupes so kick out all dev/inode collisions*/
//                    ||  (1
//                         && (strcmp(rm_util_basename(file->path), rm_util_basename(next_file->path)) == 0)
//                         /* double paths and loops will always have same basename */
//                         && (rm_util_parent_node(file->path) == rm_util_parent_node(next_file->path))
//                         /* double paths and loops will always have same dir inode number*/
//                        )
//                 ) {
//                /* kick FILE or NEXT_FILE out */
//                if ( cmp_orig_criteria(file, next_file, session) >= 0 ) {
//                    /* FILE does not outrank NEXT_FILE in terms of ppath */
//                    iter = iter->next;
//                    g_queue_delete_link (group, iter->prev); /*TODO: this breaks size_table because we delete the file
//                                                              * without updating the size group */
//                    rm_file_destroy(file);
//
//                } else {
//                    /*iter = iter->next->next;  no, actually we want to leave FILE where it is */
//                    g_queue_delete_link (group, iter->next);
//                    rm_file_destroy(next_file);
//                }
//
//                removed_cnt++;
//            } else {
//                /*hardlinked - store the hardlink to save time later building checksums*/
//                if (file->hardlinked_original)
//                    next_file->hardlinked_original = file->hardlinked_original;
//                else
//                    next_file->hardlinked_original = file;
//                iter = iter->next;
//            }
//        } else {
//            iter = iter->next;
//        }
//    }
//
//    return removed_cnt;
//}


static gboolean rm_move_files_to_queue(gpointer key, RmGroup *group, RmSession *session) {
    g_assert(key); //void
    RmFileTables *tables = session->tables;

    GList *iter = group->queue->head;
    while (iter) {
        RmFile *file = iter->data;
        GList *next = iter->next;
        g_queue_delete_link (group->queue, iter);
        iter = next;
        g_assert(file->lint_type  == RM_LINT_TYPE_DUPE_CANDIDATE);
        g_queue_push_head(tables->file_queue, file);
    }
    return true;
}


static void rm_preprocess_files(RmFile *file, RmSession *session) {
    RmFileTables *tables = session->tables;

    file->disk = rm_mounts_get_disk_id(tables->mounts, file->dev); //TODO: don't think we need this

    g_hash_table_insert(
        tables->size_table,
        GUINT_TO_POINTER(file->file_size), //TODO: check overflow for >4GB files
        g_hash_table_lookup(
            tables->size_table, GUINT_TO_POINTER(file->file_size)) + 1
    );

    if (tables->name_table) {
        //insert file into hashtable of basename lists
        char *basename = g_strdup(rm_util_basename(file->path));
        g_hash_table_insert(tables->name_table,
                            basename,
                            g_list_prepend(g_hash_table_lookup(
                                               tables->name_table,
                                               basename),
                                           file)
                           );
    }

    GQueue *dev_list = g_hash_table_lookup(tables->dev_table, GUINT_TO_POINTER(file->disk));
    if(dev_list == NULL) {
        dev_list = g_queue_new();
        g_hash_table_insert(tables->dev_table, GUINT_TO_POINTER(file->disk), dev_list);
        rm_error("new device queue for disk %lu\n", file->disk);
    }
    g_queue_push_head(dev_list, file);

    bool nonrotational = rm_mounts_is_nonrotational(tables->mounts, file->disk);
    if(!nonrotational /*TODO: && offset_sort_optimisation */
            && ( file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE )
            && ( file->file_size > 0 ) ) {
        //TODO: if(file->hardlinked_original) we can avoid this:
        //gdouble start = g_timer_elapsed(session->timer, NULL);
        file->disk_offsets = rm_offset_create_table(file->path);
        session->offsets_read++;
        if(file->disk_offsets) {
            session->offset_fragments += g_sequence_get_length((GSequence *)file->disk_offsets);
        } else {
            session->offset_fails++;
        }
        file->phys_offset = rm_offset_lookup(file->disk_offsets, 0);
        //rm_error("fiemap finished read time %f.6\n", g_timer_elapsed(session->timer, NULL)-start);
    }

    info("Added Inode: %d Offset: %lu file: %s\n", (int)file->inode, file->phys_offset, file->path);
}


static bool rm_is_group_non_candidate( gpointer key, RmGroup *group, RmSession *session) {
    g_assert(key && session); //void
    g_assert(group);
    return (!group->needs_hashing);
}


static bool rm_is_group_candidate(RmGroup *group, RmSession *session) {
    g_assert(session);
    return (group->num_non_pref + group->num_non_pref >= 2); //TODO
}

static void rm_group_add_file(RmGroup *group, RmFile *file, RmSession *session) {
    g_assert(session);
    g_queue_push_head(group->queue, file);
    if (file->is_prefd) {
        group->num_pref++;
    } else {
        group->num_non_pref++;
    }
    if (!group->needs_hashing) {
        group->needs_hashing = rm_is_group_candidate(group, session);
    }
}

static void rm_add_file_to_size_groups(RmFile *file, RmSession *session) {
    RmGroup *group = g_hash_table_lookup(
                         session->tables->size_groups,
                         GUINT_TO_POINTER(file->file_size));
    if (!group) {
        group = rm_group_new();
        g_hash_table_insert(
            session->tables->size_groups,
            GUINT_TO_POINTER(file->file_size), //TODO: check overflow for >4GB files
            group);
    }
    rm_group_add_file(group, file, session);
}


static gboolean rm_populate_size_groups(gpointer key, GQueue *hardlink_cluster, RmSession *session) {
    g_assert(key); //key not used
    g_queue_foreach (hardlink_cluster,
                     (GFunc)rm_add_file_to_size_groups,
                     session);
    return true;
}

static gboolean rm_handle_hardlinks(gpointer key, GQueue *hardlink_cluster, RmSession *session) {
    g_assert(key); //void
    RmSettings *settings = session->settings;
    GList *iter = hardlink_cluster->head;
    while (iter) {
        RmFile *file = iter->data;
        GList *next = iter->next;
        if (file->lint_type != RM_LINT_TYPE_DUPE_CANDIDATE) {
            /* move the file to tables->other_lint */
            session->tables->other_lint[file->lint_type] = g_list_prepend(session->tables->other_lint[file->lint_type], file);
            g_queue_delete_link(hardlink_cluster, iter);
        } else if (iter != hardlink_cluster->head) {
            if (!settings->find_hardlinked_dupes) {
                rm_file_destroy(file);
                g_queue_delete_link(hardlink_cluster, iter);
            } else {
                file->hardlinked_original = hardlink_cluster->head->data;
            }
        }
        iter = next;
    }
    return g_queue_is_empty(hardlink_cluster);
}

gint rm_sort_inode(RmFile *a, RmFile *b) {
    return (a->inode > b->inode) - (a->inode < b->inode);
}


static void handle_double_base_file(RmSession *session, RmFile *file) {
    file->lint_type = RM_LINT_TYPE_BASE;
    rm_fmt_write(session->formats, file);
}

static int find_double_bases(RmSession *session) {
    // TODO:  Finish re-write of this (got part way then realised I didn't fully understand what we are trying to do */
    bool header_printed = false;
    int num_found = 0;

    GHashTableIter iter;
    gpointer key, value;

    GHashTable *name_table = session->tables->name_table;

    g_hash_table_iter_init(&iter, name_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {

        bool node_handled = false;
        GList *list = value;
        g_assert(list);

        if (list->next) {
            /* list is at least 2 files long */
            while (list) {
                RmFile *file = list->data;
                if(!header_printed) {
                    rm_error("\n%s#"RESET" Double basename(s):\n", GREEN);
                    header_printed = true;
                }

                if(!node_handled) {
                    node_handled = true;
                    handle_double_base_file(session, file);
                    num_found++;
                }

                list = list->next;
            }
        } else {
            /* only one file in list */
            g_hash_table_remove(name_table, &key);
        }
    }

    g_hash_table_destroy(name_table);
    return num_found;
}


static guint64 handle_other_lint(RmSession *session) {
    guint64 num_handled = 0;

    RmFileTables *tables = session->tables;

    for(guint64 type = 0; type < RM_LINT_TYPE_DUPE_CANDIDATE; ++type) {
        /* TODO: RM_LINT_TYPE_EDIR list needs sorting into reverse alphabetical order */
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
guint64 rm_preprocess(RmSession *session) {
    guint64 other_lint = 0;

    RmSettings *settings = session->settings;
    RmFileTables *tables = session->tables;

    /* process hardlink groups, and move other_lint into tables- */
    g_hash_table_foreach_remove(tables->node_table,
                                (GHRFunc)rm_handle_hardlinks,
                                session);
    rm_error("process hardlink groups finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    other_lint += handle_other_lint(session);
    rm_error("Other lint handling finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));



    if(settings->searchdup == 0) {
        /* rmlint was originally supposed to find duplicates only
           So we have to free list that whould have been used for
           dup search before dieing */
        die(session, EXIT_SUCCESS);
    }

    /* move remaining files to size_groups */
    g_hash_table_foreach_remove(tables->node_table,
                                (GHRFunc)rm_populate_size_groups,
                                session);
    rm_error("move remaining files to size_groups finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    /* delete irrelevant size groups */
    g_hash_table_foreach_remove(tables->size_groups,
                                (GHRFunc)rm_is_group_non_candidate,
                                session);
    rm_error("delete irrelevant size groups finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    /* move files from remaining groups into inode-sorted queue for faster fiemap */
    tables->file_queue = g_queue_new();
    g_hash_table_foreach_remove(tables->size_groups,
                                (GHRFunc)rm_move_files_to_queue,
                                session);
    g_queue_sort(tables->file_queue, (GCompareDataFunc)rm_sort_inode, NULL);
    rm_error("move files from remaining groups into inode-sorted queue finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    /* move remaining groups into dev_queues for shredder */
    g_queue_foreach(tables->file_queue, (GFunc)rm_preprocess_files, session );
    g_queue_free(tables->file_queue);
    rm_error("move remaining groups into dev_queues finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    rm_error("fiemap'd %lu files containing %lu fragments (failed another %lu files)\n", session->offsets_read - session->offset_fails, session->offset_fragments, session->offset_fails);

    if(settings->namecluster) {
        other_lint += find_double_bases(session);
        rm_error("\n");
        rm_error("Double basenames finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));
    }

    return other_lint;
}
