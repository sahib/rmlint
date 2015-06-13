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
 *  - Christopher <sahib> Pahl 2010-2015 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2015 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#ifndef RM_REPLAY_H
#define RM_REPLAY_H

#include "session.h"
#include "stdbool.h"

/**
 * @brief Load a json file from disk and output it to the output module.
 *
 * Most relevant options are supported as far as possible.
 * Options that alter the hashing/reading of duplicates will have no 
 * effect since almost no IO will be done except for some lstat/stat.
 *
 * Additionally, --followlinks, --crossdev, --hardlink (as well as their
 * negative pendants) and --write-unfinished will have no effect.
 *
 * Only paths are printed that are given on the commandline.
 *
 * @param session Global session.
 * @param json_path Absolute path to the json file.
 *
 * @return true on (some) success.
 */
bool rm_parrot_load(RmSession *session, const char *json_path);

#endif /* end of include guard */
