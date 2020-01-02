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
*  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
*  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
*
* Hosted on http://github.com/sahib/rmlint
**/

#ifndef RM_XATTR_H
#define RM_XATTR_H

#include "file.h"
#include "session.h"

/**
 * @brief Write hash and timestamp to the xattr of a file.
 *
 * The checksum is read from the file->digest structure.
 *
 * @param session Session to validate cfg against.
 * @param file file to get data and write to.
 *
 * @return 0 on sucess, some errno on failure.
 */
int rm_xattr_write_hash(RmFile *file, RmSession *session);

/**
 * @brief Read hash as hexstring from xattrs into file->ext_cksum.
 *
 * If the mtime of the file does not match, the checksum is discarded.
 *
 * @param session Session to validate cfg against.
 * @param file file to read the path from and to check the mtime.
 *
 * @return true if checksum read successfully.
 */
gboolean rm_xattr_read_hash(RmFile *file, RmSession *session);

/**
 * @brief Clear all data that may have been writen to file.
 *
 * @param session Session to validate cfg against.
 * @param file to read the path to clear from.
 *
 * @return 0 on success, some errno on failure.
 */
int rm_xattr_clear_hash(RmFile *file, RmSession *session);

/**
 * @brief Check if `path` was already deduplicated.
 *
 * @param path Path to check.
 * @param follow_symlinks Wether to check if it is a symbolic link;
 * @param res Where to store the result.
 *
 * @return true if deduplicated
 */
bool rm_xattr_is_deduplicated(const char *path, bool follow_symlinks);

/**
 * @brief Mark the file as deduplicated.
 *
 * @param res The result from rm_xattr_is_deduplicated;
 *
 * @return 0 on success, some errno on failure.
 */
int rm_xattr_mark_deduplicated(const char *path, bool follow_symlinks);

#endif
