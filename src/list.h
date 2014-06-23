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
#ifndef SLIST_H
#define SLIST_H

#include "defs.h"



/* Sort method; begins with '*begin' ends with NULL, cmp as sort criteria */
lint_t *list_sort(lint_t *begin, long(*cmp)(lint_t*,lint_t*));

/* Returns start pointer (only used before splitting list in groups) */
lint_t *list_begin(void);

/* Removes the element 'ptr' */
lint_t *list_remove(lint_t *ptr);

/* Clears list from begin till NULL */
void list_clear(lint_t *begin);

/* Appends lint_t with those datafields at end of list */
void list_append(const char *n, nuint_t s, struct timespec t, dev_t dev,
                 ino_t node,  char lint_type, bool is_ppath, unsigned int pnum);

/* Returns len of list */
nuint_t list_len(void);

/* Set vars.. (bad design to be honest)*/
void list_c_init(void);

#endif
