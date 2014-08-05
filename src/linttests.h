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
 * Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#ifndef LINTTESTS_H_INCLUDED
#define LINTTESTS_H_INCLUDED

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include "useridcheck.h"

int uid_gid_check(struct stat *statp, RmUserGroupList **userlist);
bool is_nonstripped(const char *path);
char *rm_basename(char *filename);
ino_t parent_node(char *apath);

#endif /* LINTTESTS_H_INCLUDED*/
