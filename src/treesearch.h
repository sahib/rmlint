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
#ifndef TREESEARCH_H
#define TREESEARCH_H

#include <fts.h>
#include <assert.h>
#include <stddef.h>
#include <regex.h>
//#include <errno.h>
#include "list.h"
#include "rmlint.h"

int rmlint_search_tree(rmlint_settings *settings);
void search_init(void);

#endif
