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

#ifndef MODE_H
#define MODE_H

#include "defs.h"

/* Mostly used by filter.c  */
bool process_doop_groop(RmSession *session, GQueue *group);
void init_filehandler(RmSession * session);
void write_to_log(RmSession * session, const RmFile *file, bool orig, const RmFile * p_to_orig);

/* Method to substitute $subs in $string with $with */
/* Something should really get a std method */
char * strsubs(const char * string, const char * subs, const char * with);

#endif
