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
** Author: Christopher Pahl <sahib@online.de>:
** Hosted on http://github.com/sahib/rmlint
*
**/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>


#include "list.h"
#include "linttests.h"

// TODO: Remove this when adding RmSession.

RmFileList * list_begin(void) {
    static RmFileList * list = NULL;
    if(list == NULL) {
        list = rm_file_list_new();
    }

    return list;
}

RmFile * rm_file_new(const char * path, struct stat *buf, RmLintType type, bool is_ppath, unsigned pnum) {
    RmFile *self = g_new0(RmFile, 1);

    self->path = g_strdup(path);
    self->node = buf->st_ino;
    self->dev = buf->st_dev;
    self->mtime = buf->st_mtime;
    if(type == TYPE_DUPE_CANDIDATE) {
        self->fsize = buf->st_size;
    }
    self->dupflag = type;
    self->filter = TRUE;

    // TODO: This sucks.
    self->in_ppath = is_ppath;
    self->pnum = pnum;

    /* Make sure the fp arrays are filled with 0
     This is important if a file has a smaller size
     than the size read in for the fingerprint -
     The backsum might not be calculated then, what might
     cause inaccurate results.
    */
    memset(self->fp[0],0,MD5_LEN);
    memset(self->fp[1],0,MD5_LEN);
    memset(self->bim,0,BYTE_MIDDLE_SIZE);

    /* Clear the md5 digest array too */
    memset(self->md5_digest,0,MD5_LEN);
    return self;
}

void rm_file_destroy(RmFile *file) {
    if(file->path) {
        g_free(file->path);
        file->path = NULL;
    }
    g_free(file);
}

static void rm_file_list_destroy_queue(GQueue *queue) {
    g_queue_free_full(queue, (GDestroyNotify)rm_file_destroy);
}

RmFileList *rm_file_list_new(void) {
    RmFileList * list = g_new0(RmFileList, 1);
    list->size_groups = g_sequence_new((GDestroyNotify)rm_file_list_destroy_queue);
    list->size_table = g_hash_table_new(g_direct_hash, g_direct_equal);
    return list;
}

void rm_file_list_destroy(RmFileList *list) {
    g_sequence_free(list->size_groups);
    g_hash_table_unref(list->size_table);
    g_free(list);
}

GSequenceIter *rm_file_list_get_iter(RmFileList *list) {
    return g_sequence_get_begin_iter(list->size_groups);
}

static gint rm_file_list_cmp_file_size(gconstpointer a, gconstpointer b, gpointer data) {
    const GQueue *qa = a, *qb = b;
    if(qa->head && qb->head) {
        RmFile *fa = qa->head->data, *fb = qb->head->data;
        return fa->fsize - fb->fsize;
    } else {
        return qa->head == qb->head;
    }
}

void rm_file_list_append(RmFileList * list, RmFile * file) {
    GQueue *old_group = g_hash_table_lookup(
                            list->size_table, GINT_TO_POINTER(file->fsize)
                        );

    if(old_group == NULL) {
        /* Insert a new group */

        GQueue *new_group = g_queue_new();
        g_queue_push_head(new_group, file);
        g_sequence_insert_sorted(
            list->size_groups, new_group, rm_file_list_cmp_file_size, NULL
        );

        g_hash_table_insert(
            list->size_table, GINT_TO_POINTER(file->fsize), new_group
        );

        file->file_group = new_group;
        file->list_node = new_group->head;
    } else {
        /* Group already exists */
        g_queue_push_head(old_group, file);
        file->file_group = old_group;
        file->list_node = old_group->head;
    }
}

void rm_file_list_clear(RmFileList *list, GSequenceIter * iter) {
    g_sequence_remove(iter);
}

void rm_file_list_remove(RmFileList *list, RmFile *file) {
    g_queue_delete_link(file->file_group, file->list_node);
    rm_file_destroy(file);
}

static gint rm_file_list_cmp_file(gconstpointer a, gconstpointer b, gpointer data) {
    const RmFile *fa = a, *fb = b;
    if (fa->node != fb->node)
        return fa->node - fb->node;
    else if (fa->dev != fb->dev)
        return fa->dev - fb->dev;
    else
        return strcmp(rmlint_basename(fa->path), rmlint_basename(fb->path));
}

static guint rm_file_list_remove_double_paths(RmFileList *list, GQueue *group, bool find_hardlinked_dupes) {
    guint removed_cnt = 0;

    GList * iter = group->head;
    while(iter && iter->next) {
        RmFile * file = iter->data, *next_file = iter->next->data;
        
        if(file->node != next_file->node || file->dev != next_file->dev) {
            iter = iter->next;
            continue;
        }

        if(find_hardlinked_dupes && strcmp(
                rmlint_basename(file->path), rmlint_basename(next_file->path)
            ) != 0) {
            iter = iter->next;
            continue;
        }

        if(next_file->in_ppath || !file->in_ppath) {
            iter = iter->next;
            rm_file_list_remove(list, file);
        } else {
            iter = iter->next->next;
            rm_file_list_remove(list, next_file);
        }

        removed_cnt++;
    }

    return removed_cnt;
}


static void rm_file_list_count_pref_paths(GQueue *group, int *num_pref, int *num_nonpref) {
    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile * file = iter->data;
        if(file->in_ppath) {
            *num_pref = *num_pref + 1;
        } else {
            *num_nonpref = *num_nonpref + 1;
        }
    }
}

gsize rm_file_list_sort_groups(RmFileList *list, RmSettings * settings) {
    gsize removed_cnt = 0;
    GSequenceIter * iter = rm_file_list_get_iter(list);
    while(!g_sequence_iter_is_end(iter)) {
        GQueue *queue = g_sequence_get(iter);

        int num_pref = 0, num_nonpref = 0;
        rm_file_list_count_pref_paths(queue, &num_pref, &num_nonpref);

        /* Not important for duplicate finding, remove the isle */
        if (
            (queue->length < 2) || 
            ((settings->must_match_original) && (num_pref == 0)) ||
            ((settings->keep_all_originals) && (num_nonpref == 0) )
        ) {
            GSequenceIter * old_iter = iter;
            iter = g_sequence_iter_next(iter);
            g_sequence_remove(old_iter);
        } else {
            g_queue_sort(queue, rm_file_list_cmp_file, NULL);
            removed_cnt += rm_file_list_remove_double_paths(
                list, queue, settings->find_hardlinked_dupes
            );
            iter = g_sequence_iter_next(iter);
        }
    }

    return removed_cnt;
}

gsize rm_file_list_len(RmFileList *list) {
    return g_sequence_get_length(list->size_groups);
}

gulong rm_file_list_byte_size(GQueue *group) {
    if(group && group->head) {
        return ((RmFile *)group->head)->fsize * group->length;
    }
    return 0;
}

#if 0 /* Testcase */

static void rm_file_list_print_cb(gpointer data, gpointer user_data) {
    GQueue *queue = data;
    for(GList *iter = queue->head; iter; iter = iter->next) {
        RmFile * file = iter->data;
        g_printerr("%d:%ld:%ld:%s\n", file->fsize, file->dev, file->node, file->path);
    }
    g_printerr("----\n");
}

static void rm_file_list_print(RmFileList *list) {
    g_printerr("### PRINT ###\n");
    g_sequence_foreach(list->size_groups, rm_file_list_print_cb, NULL);
}

int main(int argc, const char **argv) {
    RmFileList * list = rm_file_list_new();

    for(int i = 1; i < argc; ++i) {
        struct stat buf;
        stat(argv[i], &buf);

        RmFile *file = rm_file_new(argv[i], &buf, RM_FILE_TYPE_DUPLICATE, 1, 1);
        rm_file_list_append(list, file);
    }
    rm_file_list_print(list);
    rm_file_list_sort_groups(list, FALSE);
    rm_file_list_print(list);

    GSequenceIter * iter = rm_file_list_get_iter(list);
    while(!g_sequence_iter_is_end(iter)) {
        /* Remove the last element of the group - for no particular reason */
        GQueue *group = g_sequence_get(iter);
        rm_file_list_remove(list, group->tail->data);
        iter = g_sequence_iter_next(iter);
    }
    rm_file_list_print(list);

    /* Clear the first group */
    rm_file_list_clear(list, rm_file_list_get_iter(list));

    rm_file_list_print(list);

    rm_file_list_destroy(list);
    return 0;
}

#endif
