/**
*  This file is part of autovac.
*
*  autovac is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  autovac is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with autovac.  If not, see <http://www.gnu.org/licenses/>.
*
** Author: Christopher Pahl <sahib@online.de>:
** Hosted at the time of writing (Mo 30. Aug 14:02:22 CEST 2010): 
*  http://github.com/sahib/autovac
*   
**/

#pragma once
#ifndef PROGRESS_H
#define PROGRESS_H

#include "md5.h"

int countfiles(const char *path); 
UINT4 filterlist(void);
void pushchanges(void);


#endif
