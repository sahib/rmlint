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
 *  along with rmlint.  If not, see <http:      //www.gnu.org/licenses/>.
 *
 * Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

/* Credits: filemap.c based heavily on short test program by dkrotx-prg
 (http://dkrotx-prg.blogspot.com.au/2012/08/speedup-file-reading-on-linux.html) */


#include "filemap.h"
#include "rmlint.h"  /* for error reporting TODO: do we need this??*/


uint64_t get_disk_offset_openfile (const int fd, RmFileOffsetType offset_type, const int64_t offset ) {
    uint64_t returnval = 0;
    uint64_t save_pos = 0;
    struct fiemap *fm = g_malloc0(sizeof(struct fiemap) + sizeof(struct fiemap_extent));
    if (fm == NULL) {
        error("Out of memory allocating fiemap\n");
        return 0;
    }

    memset (fm, 0, sizeof (*fm));

    switch (offset_type) {
    case RM_OFFSET_RELATIVE:
        fm->fm_start = lseek(fd, 0, SEEK_END) + offset;
        break;
    case RM_OFFSET_ABSOLUTE:
        fm->fm_start = offset;
        break;
    case RM_OFFSET_END:
        save_pos = lseek ( fd, 0, SEEK_CUR);
        fm->fm_start = lseek ( fd, 0, SEEK_END);
        save_pos = lseek ( fd, save_pos, SEEK_SET);
        break;
    case RM_OFFSET_START:
    default:
        fm->fm_start = 0;
        break;
    }

    fm->fm_length = 1;       /* one byte mapping*/
    fm->fm_extent_count = 1; /* buffer for one extent provided */

    if (ioctl(fd, FS_IOC_FIEMAP, fm) != -1 && fm->fm_mapped_extents == 1) {
        returnval = fm->fm_extents[0].fe_physical;
    } else {
        if (errno == EBADR) {
            error("FIEMAP failed with unsupported flags %x for file", fm->fm_flags);
        } else {
            error("FIEMAP failed for file: ");
            rm_perror("FS_IOC_FIEMAP");
        }
        returnval = 0;
    }
    g_free(fm);
    return returnval;
}


uint64_t get_disk_offset(const char *path, uint64_t file_offset) {

#if !defined (__linux__) && 0  /*TODO - fix this*/
    return -1;  /*for now, no file map info for non-linux systems*/
#else
    uint64_t returnval = 0;

    /* ---open the file--- */
#if 1 || defined(HAVE_OPEN64) && !defined(__OSX_AVAILABLE_BUT_DEPRECATED)
    int fd = open64(path, O_RDONLY);
#else
    int fd = open(path, O_RDONLY);
#endif
    if (fd < 0) {
        error("Error opening %s in get_disk_offset\n", path);
        return 0;
    }

    if (file_offset != 0) {
        if ((lseek(fd, SEEK_SET, file_offset) != (__off_t)file_offset)) {
            error ("error in lseek");
        }
    }

    returnval = get_disk_offset_openfile ( fd, RM_OFFSET_START, 0 );
    if (returnval == 0)
        error ("%s\n", path);

    close(fd);

    return returnval;
}

#endif


