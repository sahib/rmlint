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
 *  - Christopher <sahib> Pahl 2010-2015 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2015 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#ifndef RM_PREPROCESS_H
#define RM_PREPROCESS_H

#include "session.h"
#include "file.h"

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
 * @brief Moves a GQueue of files into RmFileTables->all_files.
 * @param files The files to insert; ownership is taken and *files is cleared.
 */

void rm_file_list_insert_queue(GQueue *files, const RmSession *session);

/**
 * @brief Clear potential leftover files when shredder was not used.
 */
void rm_file_tables_clear(const RmSession *session);

/**
 * @brief Compare certain attributes (listed below) of files
 *        in order to find out which file is the original.
 *
 * Returns:
 */
int rm_pp_cmp_orig_criteria_impl(const RmSession *session, time_t mtime_a, time_t mtime_b,
                                 const char *basename_a, const char *basename_b,
                                 const char *path_a, const char *path_b, int path_index_a,
                                 int path_index_b, guint8 path_depth_a,
                                 guint8 path_depth_b);

/**
 * @brief Compare two files in order to find out which file is the
 * higher ranked (ie original).
 *
 * Returns:
 */
int rm_pp_cmp_orig_criteria(const RmFile *a, const RmFile *b, const RmSession *session);

/**
 * @brief: Check if two files are equal in terms of size, and match_* options.
 */
gint rm_file_cmp(const RmFile *file_a, const RmFile *file_b);

/**
 * @brief: Compile all r<PATTERN> constructs in `sortcrit` to a GRegex
 *         and store them into session->pattern_cache.
 */
char *rm_pp_compile_patterns(RmSession *session, const char *sortcrit, GError **error);

#endif
