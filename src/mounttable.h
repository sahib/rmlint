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

/**
 * @brief Find out if a device id lies on a non-rotational device.
 *
 * @param device a dev_t, as returned by stat(2) (in st_dev)
 *
 * If you use this function you should call rm_mounts_clear()
 * once you are done.
 *
 * @return True if the device id refers to a non-rotational device.
 */
bool rm_mounts_file_is_on_sdd(dev_t device);

/**
 * @brief Free the ressources that were internally allocated.
 *
 * You shall not use rm_mounts_file_is_on_sdd() afterwards.
 */
void rm_mounts_clear(void);
