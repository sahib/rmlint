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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#ifndef RM_SHREDDER_H
#define RM_SHREDDER_H

#include <glib.h>
#include "session.h"

typedef enum RmShredGroupStatus {
    RM_SHRED_GROUP_DORMANT = 0,
    RM_SHRED_GROUP_START_HASHING,
    RM_SHRED_GROUP_HASHING,
    RM_SHRED_GROUP_FINISHING,
    RM_SHRED_GROUP_FINISHED
} RmShredGroupStatus;

/**
 * @brief Find duplicate RmFile and pass them to postprocess; free/destroy all other
 *RmFiles.
 *
 * @param session: rmlint session containing all cfg and pseudo-globals
 */
void rm_shred_run(RmSession *session);

/**
 * @brief Forward a group of files to the outout module.
 *
 * @param session the output module's session.
 * @param group a group of dupes that should be reported.
 *
 * This function will determine the original of the group by using:
 *
 * - the is_original flag of the file,
 * - the is_prefd flag of the file
 * - otherwise sort by criteria
 */
void rm_shred_forward_to_output(RmSession *session, GQueue *group);

/**
 * @brief Find the original file in a group and mark it.
 */
void rm_shred_group_find_original(RmSession *session, GQueue *group,
                                  RmShredGroupStatus status);

/**
 * @brief post-processing sorting of files by criteria (-S and -[kmKM])
 *
 * This is slightly different to rm_shred_cmp_orig_criteria in the case of
 * either -K or -M options
 */
int rm_shred_cmp_orig_criteria(RmFile *a, RmFile *b, RmSession *session);

void rm_shred_output_tm_results(RmFile *result, gpointer data);

#endif
