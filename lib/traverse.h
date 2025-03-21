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

#if RM_IS_APPLE
#include <AvailabilityMacros.h>
#if (MAC_OS_X_VERSION_MAX_ALLOWED < 1070)
#define st_atim st_atimespec
#define st_ctim st_ctimespec
#define st_mtim st_mtimespec
#endif
#endif /* RM_IS_APPLE */

#endif /* RM_TRAVERSE_H */
