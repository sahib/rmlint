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

#ifndef RM_RANK_H
#define RM_RANK_H

#include "session.h"

/**
 * @brief Compare basenames of two files
 * @retval 0 if basenames match.
 */
gint rm_rank_basenames(const RmFile *file_a, const RmFile *file_b);

gint rm_rank_with_extension(const RmFile *file_a, const RmFile *file_b);

gint rm_rank_without_extension(const RmFile *file_a, const RmFile *file_b);

/**
 * @brief Compare two files in order to find out which file is the
 * higher ranked (ie original).
 *
 * Returns: -1 if a > b, 0 if a == b and +1 else.
 */
int rm_rank_orig_criteria(const RmFile *a, const RmFile *b, const RmSession *session);

/**
 * @brief: Check if two files are equal in terms of size, and match_* options.
 */
gint rm_rank_group(const RmFile *file_a, const RmFile *file_b);

/**
 * @brief: Compile all r<PATTERN> constructs in `sortcrit` to a GRegex
 *         and store them into session->pattern_cache.
 */
char *rm_rank_compile_patterns(RmSession *session, const char *sortcrit, GError **error);





#endif
