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


#pragma once
#ifndef LIST_H
#define LIST_H

#include "defs.h"

/* Mostly used by filter.c  */
bool process_doop_groop(file_group *grp);
void init_filehandler(void);
void write_to_log(const lint_t *file, bool orig, const lint_t * p_to_orig);
nuint_t get_dupcounter();


/* ------------------------------------------------------------- */

FILE *get_logstream(void);
FILE *get_scriptstream(void);

/* Method to substitute $subs in $string with $with */
/* Something should really get a std method */
char * strsubs(const char * string, const char * subs, const char * with);

void mode_c_init(void);

#endif
