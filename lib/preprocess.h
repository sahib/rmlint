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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#ifndef RM_PREPROCESS_H
#define RM_PREPROCESS_H

#include "session.h"

/**
 * @brief Do some pre-processing (eg remove path doubles) and process "other lint".
 *
 * Returns: number of other lint items found.
 */
void rm_preprocess(RmSession *session);

/**
 * @brief Create a new RmFileTable object.
 *
 * @return A newly allocated RmFileTable.
 */
RmFileTables *rm_file_tables_new(const RmSession *session);

/**
* @brief Free a previous RmFileTable
*/
void rm_file_tables_destroy(RmFileTables *list);

/**
 * @brief Appends a file in RmFileTables->all_files.
 * @param file The file to insert; ownership is taken.
 */

void rm_file_list_insert_file(RmFile *file, const RmSession *session);

/**
 * @brief Clear potential leftover files when shredder was not used.
 */
void rm_file_tables_clear(const RmSession *session);


#endif /* RM_PREPROCESS_H */
