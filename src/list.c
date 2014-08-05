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
** Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <inttypes.h>

#include "list.h"
#include "linttests.h"
#include "filemap.h"
#include "rmlint.h"

RmFile *rm_file_new(const char *path,
                    guint64 fsize,
                    ino_t node,
                    dev_t dev,
                    time_t mtime,
                    RmLintType type,
                    bool is_ppath,
                    unsigned pnum) {
    RmFile *self = g_slice_new0(RmFile);
    self->path = g_strdup(path);
    self->node = node;
    self->dev = dev;
    self->mtime = mtime;
    self->hash_offset = 0;
    self->seek_offset = 0;
    self->state = RM_FILE_STATE_PROCESS;

    // TODO: Use the actualy type from session -> pass it.
    rm_digest_init(&self->digest, RM_DIGEST_SPOOKY, 0, 0);
    g_mutex_init(&self->file_lock);

    if(type == TYPE_DUPE_CANDIDATE) {
        self->offset = 0;
        /* could do ->offset=get_disk_offset(path, 0) here but this is expensive
         * so better to delay this until we have matched file sizes*/
        self->fsize = fsize;
    } else {
        self->fsize = 0;
        self->offset = 0;
    }

    self->lint_type = type;
    self->filter = TRUE;

    self->in_ppath = is_ppath;
    self->pnum = pnum;

    /* Make sure the fp arrays are filled with 0
     This is important if a file has a smaller size
     than the size read in for the fingerprint -
     The backsum might not be calculated then, what might
     cause inaccurate results.
    */
    memset(self->fp[0], 0, _RM_HASH_LEN);
    memset(self->fp[1], 0, _RM_HASH_LEN);
    memset(self->bim, 0, BYTE_MIDDLE_SIZE);

    /* Clear the md5 digest array too */
    memset(self->checksum, 0, _RM_HASH_LEN);

    /* initialised with no hardlink*/
    self->hardlinked_original = NULL;

    return self;
}

void rm_file_destroy(RmFile *file) {
    g_free(file->path);
    rm_digest_finalize(&file->digest);
    g_mutex_clear(&file->file_lock);
    g_slice_free(RmFile, file);
}

void rm_file_set_checksum(RmFileList *list, RmFile *file, RmDigest *digest) {
    g_rec_mutex_lock(&list->lock);
    {
        rm_digest_steal_buffer(digest, file->checksum, _RM_HASH_LEN);
    }
    g_rec_mutex_unlock(&list->lock);
}

void rm_file_set_fingerprint(RmFileList *list, RmFile *file, guint index, RmDigest *digest) {
    g_rec_mutex_lock(&list->lock);
    {
        rm_digest_steal_buffer(digest, file->fp[index], _RM_HASH_LEN);
    }
    g_rec_mutex_unlock(&list->lock);
}

void rm_file_set_middle_bytes(RmFileList *list, RmFile *file, const char *bytes, gsize len) {
    g_rec_mutex_lock(&list->lock);
    {
        memcpy(file->bim, bytes, len);
    }
    g_rec_mutex_unlock(&list->lock);
}

static void rm_file_list_destroy_queue(GQueue *queue) {
    g_queue_free_full(queue, (GDestroyNotify)rm_file_destroy);
}

RmFileList *rm_file_list_new(RmMountTable *mounts) {
    RmFileList *list = g_slice_new0(RmFileList);
    list->size_groups = g_sequence_new((GDestroyNotify)rm_file_list_destroy_queue);
    list->size_table = g_hash_table_new(g_direct_hash, g_direct_equal);
    list->mounts = mounts;
    g_rec_mutex_init(&list->lock);
    return list;
}

void rm_file_list_destroy(RmFileList *list) {
    g_rec_mutex_lock(&list->lock);
    {
        g_sequence_free(list->size_groups);
        g_hash_table_unref(list->size_table);
        rm_mounts_table_destroy(list->mounts);
    }
    g_rec_mutex_unlock(&list->lock);
    g_rec_mutex_clear(&list->lock);
    g_slice_free(RmFileList, list);
}

GSequenceIter *rm_file_list_get_iter(RmFileList *list) {
    GSequenceIter *first = NULL;
    g_rec_mutex_lock(&list->lock);
    {
        first = g_sequence_get_begin_iter(list->size_groups);
    }
    g_rec_mutex_unlock(&list->lock);
    return first;
}

static gint rm_file_list_cmp_file_size(gconstpointer a, gconstpointer b, G_GNUC_UNUSED gpointer data) {
    const GQueue *qa = a, *qb = b;
    RmFile *fa = qa->head->data, *fb = qb->head->data;
    return fa->fsize - fb->fsize;
}

void rm_file_list_append(RmFileList *list, RmFile *file) {
    /* Normalize the device id to the whole disk */
    file->dev = rm_mounts_get_disk_id(list->mounts, file->dev);

    g_rec_mutex_lock(&list->lock);
    {
        GSequenceIter *old_iter = g_hash_table_lookup(
                                      list->size_table, GINT_TO_POINTER(file->fsize)
                                  );

        g_assert(file);

        if(old_iter == NULL) {
            /* Insert a new group */
            GQueue *old_group = g_queue_new();

            g_queue_push_head(old_group, file);
            file->list_node = old_group->head;
            file->file_group = g_sequence_insert_sorted(
                                   list->size_groups, old_group, rm_file_list_cmp_file_size, NULL
                               );
            g_hash_table_insert(
                list->size_table, GINT_TO_POINTER(file->fsize), file->file_group
            );
        } else {
            GQueue *old_group = g_sequence_get(old_iter);
            g_queue_push_head(old_group, file);
            file->file_group = old_iter;
            file->list_node = old_group->head;
        }
    }
    debug("Added Inode: %d Offset: %" PRId64 " file: %s\n", (int)file->node, file->offset, file->path);
    g_rec_mutex_unlock(&list->lock);
}

void rm_file_list_clear(RmFileList *list, GSequenceIter *iter) {
    g_rec_mutex_lock(&list->lock);
    {
        g_sequence_remove(iter);
    }
    g_rec_mutex_unlock(&list->lock);
}

void rm_file_list_remove(G_GNUC_UNUSED RmFileList *list, RmFile *file) {
    g_rec_mutex_lock(&list->lock);
    {
        GQueue *group = g_sequence_get(file->file_group);
        guint64 file_size = file->fsize;

        g_queue_delete_link(group, file->list_node);
        rm_file_destroy(file);

        if(g_queue_get_length(group) == 0) {
            g_queue_free(group);
            g_hash_table_remove(list->size_table, GINT_TO_POINTER(file_size));
        }
    }
    g_rec_mutex_unlock(&list->lock);
}

static gint rm_file_list_cmp_file(gconstpointer a, gconstpointer b, G_GNUC_UNUSED gpointer data) {
    const RmFile *fa = a, *fb = b;
    if (fa->node != fb->node)
        return fa->node - fb->node;
    else if (fa->dev != fb->dev)
        return fa->dev - fb->dev;
    else
        return strcmp(rm_basename(fa->path), rm_basename(fb->path));

}

static gint rm_file_list_cmp_file_offset(gconstpointer a, gconstpointer b, G_GNUC_UNUSED gpointer data) {
    const RmFile *fa = a, *fb = b;
    const bool forward = (bool)data;

    /* offset can get very large, so returning the difference
     * can lead to a overflow in gint, therefore these detailed ifs
     */
    if(fa->offset < fb->offset) {
        return -1 ? forward : +1;
    }

    if(fa->offset > fb->offset) {
        return +1 ? forward : -1;
    }

    return 0;
}

/* Sort criteria for sorting by preferred path (first) then user-input criteria */
long cmp_orig_criteria(RmFile *a, RmFile *b, gpointer user_data) {
    RmSession *session = user_data;
    RmSettings *sets = session->settings;

    if (a->in_ppath != b->in_ppath) {
        return a->in_ppath - b->in_ppath;
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
                cmp = strcmp (rm_basename(a->path), rm_basename (b->path));
                break;
            case 'A':
                cmp = strcmp (rm_basename(b->path), rm_basename (a->path));
                break;
            case 'p':
                cmp = (long)a->pnum - (long)b->pnum;
                break;
            case 'P':
                cmp = (long)b->pnum - (long)a->pnum;
                break;
            }
            if (cmp) {
                return cmp;
            }
        }
    }
    return 0;
}

/* If we have more than one path, or a fs loop, several RMFILEs may point to the
 * same (physically same!) file.  This would result in potentially dangerous
 * false positives where the "duplicate" that gets deleted is actually the
 * original rm_file_list_remove_double_paths() searches for and removes items in
 * LIST  which are pointing to the same file.  Depending on settings, also
 * removes hardlinked duplicates sets, keeping just one of each set.  Note: LIST
 * must be sorted by dev/node or node/dev before callind.  Returns number of
 * files removed from FP.
 * */
static guint rm_file_list_remove_double_paths(RmFileList *list, GQueue *group, RmSession *session) {
    RmSettings *settings = session->settings;
    guint removed_cnt = 0;

    GList *iter = group->head;
    while(iter && iter->next) {
        RmFile *file = iter->data, *next_file = iter->next->data;

        if (file->node == next_file->node && file->dev == next_file->dev) {
            /* files have same dev and inode:  might be hardlink (safe to delete), or
             * two paths to the same original (not safe to delete) */
            if   (0
                    || (!settings->find_hardlinked_dupes)
                    /* not looking for hardlinked dupes so kick out all dev/inode collisions*/
                    ||  (1
                         && (strcmp(rm_basename(file->path), rm_basename(next_file->path)) == 0)
                         /* double paths and loops will always have same basename */
                         && (parent_node(file->path) == parent_node(next_file->path))
                         /* double paths and loops will always have same dir inode number*/
                        )
                 ) {
                /* kick FILE or NEXT_FILE out */
                if ( cmp_orig_criteria(file, next_file, session) >= 0 ) {
                    /* FILE does not outrank NEXT_FILE in terms of ppath */
                    iter = iter->next;
                    rm_file_list_remove(list, file);
                } else {
                    /*iter = iter->next->next;  no, actually we want to leave FILE where it is */
                    rm_file_list_remove(list, next_file);
                }

                removed_cnt++;
            } else {
                /*hardlinked - store the hardlink to save time later building checksums*/
                if (file->hardlinked_original)
                    next_file->hardlinked_original = file->hardlinked_original;
                else
                    next_file->hardlinked_original = file;
                iter = iter->next;
            }
        } else {
            iter = iter->next;
        }
    }

    return removed_cnt;
}

static void rm_file_list_count_pref_paths(GQueue *group, int *num_pref, int *num_nonpref) {
    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        if(file->in_ppath) {
            *num_pref = *num_pref + 1;
        } else {
            *num_nonpref = *num_nonpref + 1;
        }
    }
}

RmFile *rm_file_list_iter_all(RmFileList *list, RmFile *previous) {
    RmFile *result = NULL;
    g_rec_mutex_lock(&list->lock);
    {
        if(previous == NULL) {
            if(rm_file_list_get_iter(list)) {
                GQueue *group = g_sequence_get(rm_file_list_get_iter(list));
                result = group->head->data;
            } else {
                result = NULL;
            }
        } else if(previous->list_node && previous->list_node->next) {
            return previous->list_node->next->data;
        } else {
            GSequenceIter *next_pos = previous->file_group;
            next_pos = g_sequence_iter_next(next_pos);

            /* Advance one group */
            if(g_sequence_iter_is_end(next_pos)) {
                /* That is the only occassion where NULL is returned
                 * with a non-empty list */
                result = NULL;
            } else {
                GQueue *group = g_sequence_get(next_pos);
                result = group->head->data;
            }
        }
    }
    g_rec_mutex_unlock(&list->lock);

    return result;
}

gsize rm_file_list_sort_groups(RmFileList *list, RmSession *session) {
    RmSettings *settings = session->settings;
    gsize removed_cnt = 0;

    g_rec_mutex_lock(&list->lock);
    {
        g_sequence_sort(list->size_groups, rm_file_list_cmp_file_size, NULL);

        GSequenceIter *iter = rm_file_list_get_iter(list);
        while(!g_sequence_iter_is_end(iter)) {
            GQueue *queue = g_sequence_get(iter);
            int num_pref = 0, num_nonpref = 0;
            if(queue->length >= 2) {
                rm_file_list_count_pref_paths(queue, &num_pref, &num_nonpref);
                g_queue_sort(queue, rm_file_list_cmp_file, NULL);
                removed_cnt += rm_file_list_remove_double_paths(
                                   list, queue, session
                               );
            }

            /* Not important for duplicate finding, remove the isle */
            if (
                (queue->length < 2) ||
                ((settings->must_match_original) && (num_pref == 0)) ||
                ((settings->keep_all_originals) && (num_nonpref == 0) )
            ) {
                GSequenceIter *old_iter = iter;
                iter = g_sequence_iter_next(iter);
                g_sequence_remove(old_iter);
            } else {
                for(GList *iter = queue->head; iter; iter = iter->next) {
                    RmFile *file = iter->data;
                    int fd = open(file->path, O_RDONLY);
                    readahead(fd, 0, file->fsize);
                    close(fd);
                }
                iter = g_sequence_iter_next(iter);
            }
        }
    }
    g_rec_mutex_unlock(&list->lock);

    return removed_cnt;
}

gsize rm_file_list_len(RmFileList *list) {
    gsize len = 0;

    g_rec_mutex_lock(&list->lock);
    {
        len = g_sequence_get_length(list->size_groups);
    }
    g_rec_mutex_unlock(&list->lock);

    return len;
}

gulong rm_file_list_byte_size(RmFileList *list, GQueue *group) {
    gulong size = 0;
    g_rec_mutex_lock(&list->lock);
    {
        if(group && group->head && group->head->data) {
            RmFile *file = group->head->data;
            size = file->fsize * group->length;
        } else {
            size = 0;
        }
    }
    g_rec_mutex_unlock(&list->lock);
    return size;
}

void rm_file_list_sort_group(RmFileList *list, GSequenceIter *group, GCompareDataFunc func, gpointer user_data) {
    g_rec_mutex_lock(&list->lock);
    {
        GQueue *queue = g_sequence_get(group);
        g_queue_sort(queue, func, user_data);
    }
    g_rec_mutex_unlock(&list->lock);
}

void rm_file_list_resort_device_offsets(GQueue *dev_list, bool forward, bool force_update) {
    if(force_update) {
        for(GList *iter = dev_list->head; iter; iter = iter->next) {
            RmFile *file = iter->data;
            file->offset = get_disk_offset(file->disk_offsets, file->hash_offset);
        }
    }

    g_queue_sort(dev_list, rm_file_list_cmp_file_offset, GINT_TO_POINTER(forward));
}

GHashTable *rm_file_list_create_devlist_table(RmFileList *list) {
    RmFile *file_iter = NULL;
    GHashTable *dev_list_table = g_hash_table_new_full(
                                     g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_queue_free
                                 );

    g_rec_mutex_lock(&list->lock);
    {
        while((file_iter = rm_file_list_iter_all(list, file_iter))) {
            gpointer dev_key = GINT_TO_POINTER(file_iter->dev);
            GQueue *dev_list = g_hash_table_lookup(dev_list_table, dev_key);
            if(dev_list == NULL) {
                dev_list = g_queue_new();
                g_hash_table_insert(dev_list_table, dev_key, dev_list);
            }
            g_queue_push_head(dev_list, file_iter);
        }

        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init(&iter, dev_list_table);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            rm_file_list_resort_device_offsets((GQueue *)value, true, true);
        }
    }
    g_rec_mutex_unlock(&list->lock);
    return dev_list_table;
}

static void rm_file_list_print_cb(gpointer data, gpointer G_GNUC_UNUSED user_data) {
    GQueue *queue = data;
    for(GList *iter = queue->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        g_printerr("  %lu:%lu:%ld:%ld:%s\n", file->offset, file->fsize, file->dev, file->node, file->path);
    }
    g_printerr("----\n");
}

void rm_file_list_print(RmFileList *list) {
    g_rec_mutex_lock(&list->lock);
    {
        g_printerr("### PRINT ###\n");
        g_sequence_foreach(list->size_groups, rm_file_list_print_cb, NULL);
    }
    g_rec_mutex_unlock(&list->lock);
}

#ifdef _RM_COMPILE_MAIN_LIST /* Testcase */

int main(int argc, const char **argv) {
    RmFileList *list = rm_file_list_new();

    for(int i = 1; i < argc; ++i) {
        struct stat buf;
        stat(argv[i], &buf);

        RmFile *file = rm_file_new(argv[i], buf.st_size, buf.st_ino, buf.st_dev, 0, TYPE_DUPE_CANDIDATE, 1, 0);
        rm_file_list_append(list, file);
    }

    GHashTable *table = rm_file_list_create_devlist_table(list);

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        g_printerr("On device: %d:\n", GPOINTER_TO_INT(key));
        rm_file_list_print_cb(value, NULL);
    }

    g_hash_table_unref(table);

    // g_printerr("### INITIAL\n");
    // rm_file_list_print(list);
    // RmSettings settings;
    // settings.must_match_original = FALSE;
    // settings.keep_all_originals = FALSE;
    // settings.find_hardlinked_dupes = TRUE;
    // g_printerr("### SORT AND CLEAN\n");
    // rm_file_list_sort_groups(list, &session);
    // rm_file_list_print(list);

    // g_printerr("### REMOVE LAST ELEMENT OF EACH\n");
    // GSequenceIter *iter = rm_file_list_get_iter(list);
    // while(!g_sequence_iter_is_end(iter)) {
    //     /* Remove the last element of the group - for no particular reason */
    //     GQueue *group = g_sequence_get(iter);
    //     rm_file_list_remove(list, group->tail->data);
    //     iter = g_sequence_iter_next(iter);
    // }

    // rm_file_list_print(list);

    // g_printerr("### CLEAR LAST\n");

    // /* Clear the first group */
    // rm_file_list_clear(list, rm_file_list_get_iter(list));

    // rm_file_list_print(list);

    rm_file_list_destroy(list);
    return 0;
}

#endif
