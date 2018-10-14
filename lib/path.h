/**
* This file is part of rmlint.
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
*  - Christopher Pahl <sahib>      2010-2017 (https://github.com/sahib)
*  - Daniel T.        <SeeSpotRun> 2014-2017 (https://github.com/SeeSpotRun)
*  - Michael Witten   <mfwitten>   2018-2018
*
* Hosted on http://github.com/sahib/rmlint
**/

#ifndef RM_PATH_H
#define RM_PATH_H

#include <stdbool.h>    // bool

typedef struct RmPath {
    char *path;                 // result of `realpath()' of <stdlib.h>
    unsigned int index;         // command line order, followed by stdin order
    bool is_prefd;              // whether path was tagged as preferred path
    bool single_volume;         // treat this directory as one file system
} RmPath;

#endif /* end of include guard */
