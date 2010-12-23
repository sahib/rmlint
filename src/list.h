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

#include <sys/types.h>
#include "defs.h"


/* I can haz bool? */
typedef char bool;
#define false ( 0)
#define true  (!0)

/* The structure used from now on to handle nearly everything */
typedef struct lint_t
{
    unsigned char md5_digest[MD5_LEN];   /* md5sum of the file */
    unsigned char fp[2][MD5_LEN];        /* A short fingerprint of a file - start and back */
    char bim[BYTE_MIDDLE_SIZE];

    char *path;		  	                   /* absolute path from working dir */
    nuint_t fsize; 		                   /* Size of the file (bytes) */
    bool filter; 			               /* Uhm? */
    bool dupflag;			               /* Is the file marked as duplicate? */

    /* This is used to find pointers to the same file */
    ino_t node;
    dev_t dev;

    /* Pointer to next element */
    struct lint_t *next;
    struct lint_t *last;

} lint_t;


/* Sort method; begins with '*begin' ends with NULL, cmp as sort criteria */
lint_t *list_sort(lint_t *begin, long (*cmp)(lint_t*,lint_t*));

/* Returns start pointer (only used before splitting list in groups) */
lint_t *list_begin(void);

/* Removes the element 'ptr' */
lint_t *list_remove(lint_t *ptr);

/* Clears list from begin till NULL */
void list_clear(lint_t *begin);

/* Appends lint_t with those datafields at end of list */
void list_append(const char *n, nuint_t s, dev_t dev, ino_t node,  bool dupflag);

/* Returns len of list */
nuint_t list_len(void);

/* Set vars.. (bad design :/)*/
void list_c_init(void);

#endif
