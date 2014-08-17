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

#ifndef RM_PREPROCESS_H
#define RM_PREPROCESS_H

#include "session.h"
#include "traverse.h"
#include "file.h"

/**
 * @brief Do some pre-processing (eg remove path doubles) and process "other lint".
 *
 * TODO: better name.
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
