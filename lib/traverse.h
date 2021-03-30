
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

#ifndef RM_TRAVERSE_H
#define RM_TRAVERSE_H

#include "session.h"

/**
 * @brief Traverse all specified paths.
 */
void rm_traverse_tree(RmSession *session);

bool rm_traverse_file(RmSession *session, RmStat *statp, const char *path,
                             bool is_prefd, unsigned long path_index,
                             RmLintType file_type, bool is_symlink, bool is_hidden,
                             bool is_on_subvol_fs, short depth, bool tagged_original, const char *ext_cksum);

bool rm_traverse_is_emptydir(const char *path, RmCfg *cfg, int current_depth);

#endif
