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
** Hosted at the time of writing (Do 30. Sep 18:32:19 CEST 2010):
*  http://github.com/sahib/rmlint
*
**/

#pragma once
#ifndef PROGRESS_H
#define PROGRESS_H

#include "md5.h"
#include "list.h"

typedef struct {

        /* Start and end pointer of a 'group' */
        iFile *grp_stp, *grp_enp;

        /* elems in this list and total size */
        uint32 len, size;

} file_group;


int  regfilter(const char* input, const char *pattern);
int  recurse_dir(const char *path);
void start_processing(iFile *b);

#endif
