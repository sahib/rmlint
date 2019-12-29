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
 *  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
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
 */
typedef struct RmParrotCage {
    RmSession *session;
    GQueue *groups;
    GQueue *parrots;
} RmParrotCage;

/**
 * @brief Open a ParrotCage.
 *
 * A ParrotCage is where the json file will get accumulated.
 */
void rm_parrot_cage_open(RmParrotCage *cage, RmSession *session);

/**
 * @brief Load a single json file to the cage.
 *
 * @return true on (partial) success.
 */
bool rm_parrot_cage_load(RmParrotCage *cage, const char *json_path, bool is_prefd);

/**
 * @brief Close the cage, frees resources, but does not do rm_fmt_flush().
 */
void rm_parrot_cage_close(RmParrotCage *cage);

/**
 * @brief Flush the cage's contents to the output module.
 * */
void rm_parrot_cage_flush(RmParrotCage *cage);

#endif /* end of include guard */
