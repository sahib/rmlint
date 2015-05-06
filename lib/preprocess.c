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
    RmCfg *cfg = file->session->cfg;
    if(cfg->match_basename || cfg->match_with_extension) {
        RM_DEFINE_BASENAME(file);
        return (guint)(
            file->file_size ^ (cfg->match_basename ? g_str_hash(file_basename) : 0) ^
            (cfg->match_with_extension ? g_str_hash(rm_util_path_extension(file_basename))
                                       : 0));
    } else {
        return (guint)(file->file_size);
    }
}

static bool rm_file_check_basename(const RmFile *file_a, const RmFile *file_b) {
    RM_DEFINE_BASENAME(file_a);
    RM_DEFINE_BASENAME(file_b);

    return g_ascii_strcasecmp(file_a_basename, file_b_basename) == 0;
}

static bool rm_file_check_with_extension(const RmFile *file_a, const RmFile *file_b) {
    RM_DEFINE_BASENAME(file_a);
    RM_DEFINE_BASENAME(file_b);

    char *ext_a = rm_util_path_extension(file_a_basename);
    char *ext_b = rm_util_path_extension(file_b_basename);

    if(ext_a && ext_b && g_ascii_strcasecmp(ext_a, ext_b) == 0) {
        return true;
    } else {
        return false;
    }
}

static bool rm_file_check_without_extension(const RmFile *file_a, const RmFile *file_b) {
    RM_DEFINE_BASENAME(file_a);
    RM_DEFINE_BASENAME(file_b);

    char *ext_a = rm_util_path_extension(file_a_basename);
    char *ext_b = rm_util_path_extension(file_b_basename);

    /* Check length till extension, or full length if none present */
    size_t a_len = (ext_a) ? (ext_a - file_a_basename) : (int)strlen(file_a_basename);
    size_t b_len = (ext_b) ? (ext_b - file_b_basename) : (int)strlen(file_b_basename);

    if(a_len != b_len) {
        return false;
    }

    if(g_ascii_strncasecmp(file_a_basename, file_b_basename, a_len) == 0) {
        return true;
    }

    return false;
}

static gboolean rm_file_equal(const RmFile *file_a, const RmFile *file_b) {
    const RmCfg *cfg = file_a->session->cfg;

    return (1 && (file_a->file_size == file_b->file_size) &&
            (0 || (!cfg->match_basename) || (rm_file_check_basename(file_a, file_b))) &&
            (0 || (!cfg->match_with_extension) ||
             (rm_file_check_with_extension(file_a, file_b))) &&
            (0 || (!cfg->match_without_extension) ||
             (rm_file_check_without_extension(file_a, file_b))));
}

static guint rm_node_hash(const RmFile *file) {
    return file->inode ^ file->dev;
}

static gboolean rm_node_equal(const RmFile *file_a, const RmFile *file_b) {
    return (1 && (file_a->inode == file_b->inode) && (file_a->dev == file_b->dev));
}

/* GHashTable key tuned to recognize duplicate paths.
 * i.e. RmFiles that are not only hardlinks but
 * also point to the real path
 */
typedef struct RmPathDoubleKey {
    /* parent_inode and basename are initialized lazily,
     * since often, they are not needed.
     */
    bool parent_inode_set : 1;
    bool basename_set : 1;

    /* stat(dirname(file->path)).st_ino */
    ino_t parent_inode;
    char *basename;

    /* File the key points to */
    RmFile *file;

} RmPathDoubleKey;

static guint rm_path_double_hash(const RmPathDoubleKey *key) {
    /* depend only on the always set components, never change the hash duringthe run */
    return rm_node_hash(key->file);
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

    if(key_a->parent_inode_set == false) {
        RM_DEFINE_PATH(file_a);

        key_a->parent_inode = rm_util_parent_node(file_a_path);
        key_a->parent_inode_set = TRUE;
    }

    if(key_b->parent_inode_set == false) {
        RM_DEFINE_PATH(file_b);

        key_b->parent_inode = rm_util_parent_node(file_b_path);
        key_b->parent_inode_set = TRUE;
    }

    if(key_a->parent_inode != key_b->parent_inode) {
        return FALSE;
    }

    if(!file_a->session->cfg->use_meta_cache) {
        return g_strcmp0(file_a->basename, file_b->basename) == 0;
    }

    /* If using --with-metadata-cache, save the basename for later use
     * so it doesn't trigger SELECTs very often.  Basenames are
     * generally much shorter than the path, so that should be
     * okay.
     */
    if(key_a->basename == NULL) {
        RM_DEFINE_BASENAME(file_a);
        key_a->basename = g_strdup(file_a_basename);
    }

    if(key_b->basename == NULL) {
        RM_DEFINE_BASENAME(file_b);
        key_b->basename = g_strdup(file_b_basename);
    }

    return g_strcmp0(key_a->basename, key_b->basename) == 0;
}

static RmPathDoubleKey *rm_path_double_new(RmFile *file) {
    RmPathDoubleKey *key = g_malloc0(sizeof(RmPathDoubleKey));
    key->file = file;
    return key;
}

static void rm_path_double_free(RmPathDoubleKey *key) {
    if(key->basename != NULL) {
        g_free(key->basename);
    }
    g_free(key);
}

RmFileTables *rm_file_tables_new(_U RmSession *session) {
    RmFileTables *tables = g_slice_new0(RmFileTables);

    tables->size_groups = g_hash_table_new_full((GHashFunc)rm_file_hash,
                                                (GEqualFunc)rm_file_equal, NULL, NULL);

    tables->node_table = g_hash_table_new_full((GHashFunc)rm_node_hash,
                                               (GEqualFunc)rm_node_equal, NULL, NULL);

    g_rec_mutex_init(&tables->lock);
    return tables;
}

void rm_file_tables_destroy(RmFileTables *tables) {
    g_rec_mutex_clear(&tables->lock);
    g_slice_free(RmFileTables, tables);
}

/*  compare two files. return:
 *    - a negative integer file 'a' outranks 'b',
 *    - 0 if they are equal,
 *    - a positive integer if file 'b' outranks 'a'
 */
int rm_pp_cmp_orig_criteria_impl(RmSession *session, time_t mtime_a, time_t mtime_b,
                                 const char *basename_a, const char *basename_b,
                                 int path_index_a, int path_index_b) {
    RmCfg *sets = session->cfg;

    int sort_criteria_len = strlen(sets->sort_criteria);
    for(int i = 0; i < sort_criteria_len; i++) {
        long cmp = 0;
        switch(tolower(sets->sort_criteria[i])) {
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
        if(cmp) {
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
    if(a->lint_type != b->lint_type) {
        /* "other" lint outranks duplicates and has lower ENUM */
        return a->lint_type - b->lint_type;
    } else if(a->is_prefd != b->is_prefd) {
        return (b->is_prefd - a->is_prefd);
    } else {
        RM_DEFINE_BASENAME(a);
        RM_DEFINE_BASENAME(b);
        return rm_pp_cmp_orig_criteria_impl(session, a->mtime, b->mtime, a_basename,
                                            b_basename, a->path_index, b->path_index);
    }
}

/* initial list build, including kicking out path doubles and grouping of hardlinks */
bool rm_file_tables_insert(RmSession *session, RmFile *file) {
    RmFileTables *tables = session->tables;
    GHashTable *node_table = tables->node_table;
    bool is_hardlink = true;

    if(rm_session_was_aborted(session)) {
        return false;
    }

    g_rec_mutex_lock(&tables->lock);
    {
        RmFile *inode_match = g_hash_table_lookup(node_table, file);
        if(inode_match == NULL) {
            g_hash_table_insert(node_table, file, file);
        } else {
            /* file(s) with matching dev, inode(, basename) already in table...
             * fails if the hardlinked file has been written to during traversal; so
             * instead we just print a warning
             * */
            if(inode_match->file_size != file->file_size) {
                RM_DEFINE_PATH(file);
                rm_log_warning_line(_("Hardlink file size changed during traversal: %s"),
                                    file_path);
            }

            /* if this is the first time, set up the hardlinks.files queue */
            if(!inode_match->hardlinks.files) {
                inode_match->hardlinks.files = g_queue_new();

                /* NOTE: during list build, the hardlinks.files queue includes the file
                 * itself, as well as its hardlinks.  This makes operations
                 * in rm_file_tables_insert much simpler but complicates things later on,
                 * so the head file gets removed from the hardlinks.files queue
                 * in rm_pp_handle_hardlinks() during preprocessing */
                g_queue_push_head(inode_match->hardlinks.files, inode_match);
            }

            /* make sure the highest-ranked hardlink is "boss" */
            if(rm_pp_cmp_orig_criteria(file, inode_match, session) < 0) {
                /* this file outranks existing existing boss; swap.
                 * NOTE: it's important that rm_file_list_insert selects a
                 * RM_LINT_TYPE_DUPE_CANDIDATE as head file, unless all the
                 * files are "other lint".  This is achieved via
                 * rm_pp_cmp_orig_criteria */
                file->hardlinks.files = inode_match->hardlinks.files;
                inode_match->hardlinks.files = NULL;
                g_hash_table_add(node_table, file); /* replaces key and data*/
                g_queue_push_head(file->hardlinks.files, file);
            } else {
                /* Find the right place to insert sorted */
                GList *iter = inode_match->hardlinks.files->head;
                while(iter && rm_pp_cmp_orig_criteria(iter->data, file, session) <= 0) {
                    /* iter outranks file - keep moving down the queue */
                    iter = iter->next;
                }

                /* Store the iter to this file, so we can swap it if needed */
                if(iter) {
                    /* file outranks iter (or is equal), so should be inserted before iter
                     */
                    g_queue_insert_before(inode_match->hardlinks.files, iter, file);
                } else {
                    g_queue_push_tail(inode_match->hardlinks.files, file);
                }
            }
        }
    }

    g_rec_mutex_unlock(&tables->lock);
    return is_hardlink;
}

/* if file is not DUPE_CANDIDATE then send it to session->tables->other_lint
 * and return true; else return false */
static bool rm_pp_handle_other_lint(RmSession *session, RmFile *file) {
    if(file->lint_type != RM_LINT_TYPE_DUPE_CANDIDATE) {
        if(session->cfg->filter_mtime && file->mtime < session->cfg->min_mtime) {
            rm_file_destroy(file);
            return true;
        }

        session->tables->other_lint[file->lint_type] =
            g_list_prepend(session->tables->other_lint[file->lint_type], file);
        return true;
    } else {
        return false;
    }
}

static bool rm_pp_handle_own_files(RmSession *session, RmFile *file) {
    RM_DEFINE_PATH(file);
    return rm_fmt_is_a_output(session->formats, file_path);
}

/* Preprocess files, including embedded hardlinks.  Any embedded hardlinks
 * that are "other lint" types are sent to rm_pp_handle_other_lint.  If the
 * file itself is "other lint" types it is likewise sent to rm_pp_handle_other_lint.
 * If there are no files left after this then return TRUE so that the
 * cluster can be deleted from the node_table hash table.
 * NOTE: we rely on rm_file_list_insert to select a RM_LINT_TYPE_DUPE_CANDIDATE as head
 * file (unless ALL the files are "other lint"). */
static gboolean rm_pp_handle_inode_clusters(_U gpointer key, RmFile *file,
                                            RmSession *session) {
    g_assert(file);
    RmCfg *cfg = session->cfg;

    if(file->hardlinks.files && file->hardlinks.files->head) {
        /* there is a cluster of inode matches - unpack them and check for path doubles */

        GHashTable *unique_paths_table =
            g_hash_table_new_full((GHashFunc)rm_path_double_hash,
                                  (GEqualFunc)rm_path_double_equal,
                                  (GDestroyNotify)rm_path_double_free,
                                  NULL);

        GList *next = NULL;

        for(GList *iter = file->hardlinks.files->head; iter; iter = next) {
            next = iter->next;
            RmFile *iter_file = iter->data;

            RmPathDoubleKey *key = rm_path_double_new(iter_file);

            /* Lookup if there is a file with the same path */
            RmPathDoubleKey *match_double_key =
                g_hash_table_lookup(unique_paths_table, key);

            if(match_double_key == NULL) {
                g_hash_table_add(unique_paths_table, key);
            } else {
                g_assert(match_double_key->file != iter_file);
                RmFile *match_double = match_double_key->file;

                g_assert(rm_pp_cmp_orig_criteria(iter_file, match_double, session) >= 0);

                rm_log_debug("Ignoring path double %p, keeping %p\n", iter_file,
                             match_double);

                g_queue_delete_link(file->hardlinks.files, iter);
                rm_file_destroy(iter_file);
            }
        }

        g_hash_table_unref(unique_paths_table);

        /* remove self from hardlink queue */
        g_queue_remove(file->hardlinks.files, file);

        for(GList *iter = file->hardlinks.files->head, *next = NULL; iter; iter = next) {
            /* Remember next element early */
            next = iter->next;

            /* call self to handle each embedded hardlink */
            RmFile *embedded = iter->data;
            g_assert(embedded != file);
            if(embedded->hardlinks.files != NULL) {
                rm_log_error("Warning: embedded file %p has hardlinks", embedded);
                GQueue *hardlinks = embedded->hardlinks.files;
                g_assert(hardlinks->length < 2);
                if(hardlinks->head) {
                    g_assert(hardlinks->head->data == embedded);
                }

                g_queue_free(hardlinks);
                embedded->hardlinks.files = NULL;
            }

            if(rm_pp_handle_inode_clusters(NULL, embedded, session)) {
                g_queue_delete_link(file->hardlinks.files, iter);
            } else if(!cfg->find_hardlinked_dupes) {
                rm_file_destroy(embedded);
                g_queue_delete_link(file->hardlinks.files, iter);
            } else {
                embedded->hardlinks.hardlink_head = file;
                g_assert(!embedded->hardlinks.is_head);
            }
        }

        if(g_queue_is_empty(file->hardlinks.files)) {
            g_queue_free(file->hardlinks.files);
            file->hardlinks.files = NULL;
        } else {
            file->hardlinks.is_head = TRUE;
        }
    }


    if(file->hardlinks.is_head) {
        /* Hardlinks are processed on the fly by shredder later,
         * so we do not really need to process them.
         */
        session->total_filtered_files -= file->hardlinks.files->length;
    }

    /*
    * Check if the file is a output of rmlint itself. Which we definitely
    * not want to handle. Creating a script that deletes itself is fun but useless.
    * */
    bool remove = rm_pp_handle_own_files(session, file);

    /* handle the head file; if it's "other lint" then process it via
     * rm_pp_handle_other_lint
     * and return TRUE, else keep it
     */
    if(remove == false && rm_pp_handle_other_lint(session, file)) {
        remove = true;
    } else if(remove == true) {
        rm_file_destroy(file);
    }

    session->total_filtered_files -= remove;
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);

    return remove;
}

static int rm_pp_cmp_reverse_alphabetical(const RmFile *a, const RmFile *b) {
    RM_DEFINE_PATH(a);
    RM_DEFINE_PATH(b);
    return g_strcmp0(b_path, a_path);
}

static RmOff rm_pp_handler_other_lint(RmSession *session) {
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
        tables->node_table, (GHRFunc)rm_pp_handle_inode_clusters, session);

    rm_log_debug("process hardlink groups finished at time %.3f; removed %u of %d\n",
                 g_timer_elapsed(session->timer, NULL), removed, session->total_files);

    session->other_lint_cnt += rm_pp_handler_other_lint(session);
    rm_log_debug("Other lint handling finished at time %.3f\n",
                 g_timer_elapsed(session->timer, NULL));

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);
}
