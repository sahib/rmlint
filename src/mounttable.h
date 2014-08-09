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
*  Authors:
*
*  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
*  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
*
*
* Hosted on http://github.com/sahib/rmlint
*
* This file was partly authored by qitta (https://github.com/qitta) - Thanks!
**/

#include <stdbool.h>
#include <sys/types.h>
#include <glib.h>

#ifndef RM_MOUNT_TABLE_H
#define RM_MOUNT_TABLE_H

typedef struct RmMountTable {
    GHashTable *part_table;
    GHashTable *rotational_table;
    GHashTable *diskname_table;
    GList *mounted_paths;
} RmMountTable;

/**
 * @brief Allocates a new mounttable.
 *
 * @return The mounttable. Free with rm_mounts_table_destroy.
 */
RmMountTable *rm_mounts_table_new(void);

/**
 * @brief Destroy a previously allocated mounttable.
 *
 * @param self the table to destroy.
 */
void rm_mounts_table_destroy(RmMountTable *self);

/**
 * @brief Check if the device is or is part of a nonrotational device.
 *
 * This operation has constant time.
 *
 * @param self the table to lookup from.
 * @param device the dev_t of a file, e.g. looked up from stat(2)
 *
 * @return true if it is non a nonrational device.
 */
bool rm_mounts_is_nonrotational(RmMountTable *self, dev_t device);

/**
 * @brief Return name of device/disk.
 *
 * This operation has constant time.
 *
 * @param self the table to lookup from.
 * @param device the dev_t of a disk
 *
 * @return pointer to disk name.
 */
char *rm_mounts_get_name(RmMountTable *self, dev_t device);

/**
 * @brief Same as above, but calls stat(2) on path for you.
 */
bool rm_mounts_is_nonrotational_by_path(RmMountTable *self, const char *path);

/**
 * @brief Get the disk behind the partition.
 *
 * @param self the table to lookup from.
 * @param partition the dev_t of a partition (sda1 -> 8:1), e.g. looked up from stat(2)
 *
 * @return the dev_t of the whole disk. (sda 8:0)
 */
dev_t rm_mounts_get_disk_id(RmMountTable *self, dev_t partition);

/**
 * @brief Same as above, but calls stat(2) on path for you.
 */
dev_t rm_mounts_get_disk_id_by_path(RmMountTable *self, const char *path);

#endif /* end of include guard */
