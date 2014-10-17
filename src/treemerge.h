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

#ifndef RM_TREEMERGE_INCLUDE
#define RM_TREEMERGE_INCLUDE

#include <glib.h>
#include "file.h"

/* Opaque structure, details do not matter to caller */
struct RmTreeMerger;
typedef struct RmTreeMerger RmTreeMerger;

/* RmTreeMerger is part of RmSession, therefore prototype it here */
struct RmSession;

RmTreeMerger * rm_tm_new(struct RmSession *session);
void rm_tm_feed(RmTreeMerger *self, RmFile *file);
void rm_tm_finish(RmTreeMerger *self);
void rm_tm_destroy(RmTreeMerger *self);

#endif /* RM_TREEMERGE_INCLUDE*/
