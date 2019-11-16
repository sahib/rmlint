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

#ifndef RM_TREEMERGE_INCLUDE
#define RM_TREEMERGE_INCLUDE

#include <glib.h>
#include "file.h"

/**
 * Module to cluster RmFiles to directories.
 * I.e. find duplicate directories.
 *
 * All API here is defined on the opaque RmTreeMerger structure.
 * Files are feeded and the finished directories are wrapped
 * as RmFiles and written to the output module.
 */

/* Opaque structure, details do not matter to caller */
struct RmTreeMerger;
typedef struct RmTreeMerger RmTreeMerger;

/* RmTreeMerger is part of RmSession, therefore prototype it here */
struct RmSession;

/**
 * @brief Allocate a new RmTreeMerger structure.
 */
RmTreeMerger *rm_tm_new(struct RmSession *session);

/**
 * @brief Add an RmFile to the pool of (to be) investigated files.
 */
void rm_tm_feed(RmTreeMerger *self, RmFile *file);

/**
 * @brief Find duplicate directories through all feeded RmFiles.
 */
void rm_tm_finish(RmTreeMerger *self);

/**
 * @brief Free all memory allocated previously.
 */
void rm_tm_destroy(RmTreeMerger *self);


struct RmDirectoryUnpacker;
typedef struct RmDirectoryUnpacker RmDirectoryUnpacker;

struct RmSession;
typedef struct RmSession RmSession;

/**
 * @brief Create a new unpack helper
 * */
RmDirectoryUnpacker *rm_dir_unpacker_new(RmSession *session, const char *directory, bool is_prefd);

/**
 * @brief Free a unpack helper
 * */
void rm_dir_unpacker_free(RmDirectoryUnpacker *unpacker);

/**
 * @brief Retrieve the next file from the directory.
 * */
RmFile *rm_dir_unpacker_next(RmDirectoryUnpacker *unpacker);

/**
 * @brief Check if we have more entries.
 */
bool rm_dir_unpacker_has_next(RmDirectoryUnpacker *unpacker);

#endif /* RM_TREEMERGE_INCLUDE*/
