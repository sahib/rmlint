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

#ifndef FILTER_H
#define FILTER_H

#include "traverse.h"

typedef struct RmMountTable RmMountTable;

typedef struct RmFileTable {
    RmMountTable *mounts;
    GHashTable *dev_table;
    GHashTable *size_table;
    GHashTable *name_table;
    GList *other_lint[RM_LINT_TYPE_DUPE_CANDIDATE]; // one list for each lint type other than dupe candidates
    GRecMutex lock;
} RmFileTable;


/**
 * @brief Do some pre-processing (eg remove path doubles) and process "other lint".
 *
 */
void do_pre_processing(RmSession *session);

/**
 * @brief Create a new RmFileTable object.
 *
 * @return A newly allocated RmFileTable.
 */
RmFileTable *rm_file_table_new(RmSession *session);


/**
* @brief Free a previous RmFileTable
*/
void rm_file_table_destroy(RmFileTable *list);


/**
 * @brief Insert a file in appropriate part of RmFileTable.
 *
 * Chooses the appropiate group automatically.
 *
 * @param file The file to append; ownership is taken.
 */
void rm_file_table_insert(RmFileTable *list, RmFile *file);


#endif
