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
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#ifndef RM_SHREDDER_H
#define RM_SHREDDER_H

#include <glib.h>
#include "session.h"



//~ /**
//~ * @brief Allocate and initialise new ShredGroup.
//~ *
//~ * @return Pointer to the new group.  Free using shred_group_free.
//~ */
//~ ShredGroup *shred_group_new(void);
//~
//~ /**
//~ * @brief Discards any RmFiles still in held_files (for case when group didn't pass tests to require hashing).
//~ *        Tells all children that their parent is dead.
//~ *        Frees memory allocated to ShredGroup.
//~ *
//~ * @param group: the group to free.
//~ */
//~ void shred_group_free(ShredGroup *self);
//~
//~ /**
//~ * @brief Test where a ShredGroup meets criteria for potential duplicate cluster.
//~ *
//~ * @return true if ShredGroup meets criteria, false otherwise.
//~ */
//~ gboolean shred_group_is_candidate(ShredGroup *self, RmSession *session);

//void rm_add_file_to_size_groups(RmFile *file, RmSession *session);

/**
 * @brief Find duplicate RmFile and pass them to postprocess; free/destroy all other RmFiles.
 *
 * @param session: rmlint session containing all settings and pseudo-globals
 *
 */
void rm_shred_run(RmSession *session);

#endif
// was 35
