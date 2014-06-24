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

#ifndef RM_LIST_H
#define RM_LIST_H

#include <sys/types.h>
#include <sys/stat.h>
#include <glib.h>
#include "defs.h"

RmFile * rm_file_new(const char * path, struct stat *buf, RmLintType type, bool is_ppath, unsigned pnum);

/**
 * @brief Free the memory allocated by rm_file_new()
 */
void rm_file_destroy(RmFile *file);

////////////////

typedef struct RmFileList {
    GSequence * size_groups;
    GHashTable * size_table;
} RmFileList;

// TODO: Remove once we have RmSession.
RmFileList * list_begin(void);

typedef int (* RmFileListSortFunc)(RmFile *a, RmFile *b, gpointer);

/**
 * @brief Create a new RmFileList object.
 *
 * @return A newly allocated RmFileList.
 */
RmFileList *rm_file_list_new(void);

/**
 * @brief Free a previous RmFileList
 */
void rm_file_list_destroy(RmFileList *list);

/**
 * @brief Get a subgroup (or isle) of the list.
 *
 * If rm_file_list_group() was not called yet, there is only one group at 0.
 *
 * @param child
 *
 * @return a GQueue, containting all children in the group.
 */
GSequenceIter *rm_file_list_get_iter(RmFileList *list);

/**
 * @brief Append a file to the List.
 *
 * Chooses the appropiate group automatically.
 *
 * @param file The file to append; ownership is taken.
 */
void rm_file_list_append(RmFileList * list, RmFile * file);

/**
 * @brief Clear a sub
 *
 * You can iterate over the subgroups like this:
 *
 * for(int i = 0; i < list->n_children; ++i) {
 *     rm_file_list_clear(list, i);
 * }
 *
 * @param child  The index of the group to remove.
 */
void rm_file_list_clear(GSequenceIter * iter);

void rm_file_list_sort_group(RmFileList *list, GSequenceIter *group, GCompareDataFunc func, gpointer user_data);

/**
 * @brief Remove a single file, possibly adjusting groups.
 *
 * @param file The file to remove, will be freed.
 */
void rm_file_list_remove(RmFileList *list, RmFile *file);

gsize rm_file_list_sort_groups(RmFileList *list, RmSettings * settings);
gsize rm_file_list_len(RmFileList *list);
gulong rm_file_list_byte_size(GQueue *group);

#endif /* RM_LIST_H */
