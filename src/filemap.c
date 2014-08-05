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


/* sort sequence into decreasing order of logical offsets */
int sort_logical (gconstpointer a, gconstpointer b){
    struct OffsetEntry *offset_a = (gpointer) a;
    struct OffsetEntry *offset_b = (gpointer) b;
    if (offset_b->logical > offset_a->logical) {
        return 1;
    } else if (offset_b->logical == offset_a->logical) {
        return 0;
    } else return -1;
}

/* find first item in sequence with logical offset <= target */
int find_logical (gconstpointer a, gconstpointer b){
    struct OffsetEntry *offset_a = (gpointer) a;
    struct OffsetEntry *offset_b = (gpointer) b;
    if (offset_b->logical >= offset_a->logical) {
        return 1;
    } else {
        return 0;
    }
}

/*void printoffset(gpointer data){
    OffsetEntry *offset = (gpointer)data;
    info("%llu:%llu\n", offset->logical, offset->physical);
} */

GSequence *get_fiemap_extents(char *path) {

    /* ---open the file--- */
#if defined(HAVE_OPEN64) && !defined(__OSX_AVAILABLE_BUT_DEPRECATED)
    int fd = open64(file->path, O_RDONLY);
#else
    int fd = open(path, O_RDONLY);
#endif
    if (fd < 0) {
        info("Error opening %s in setup_fiemap_extents\n", path);
        return NULL;
    }

    GSequence *self = g_sequence_new(g_free);

    char buf[16384];
    struct fiemap *fiemap = (struct fiemap *)buf;
    struct fiemap_extent *fm_ext = &fiemap->fm_extents[0];

    int count = (sizeof(buf) - sizeof(*fiemap)) /
            sizeof(struct fiemap_extent); /* max number of extents we can get in one read */
    unsigned long long expected = 0; /* used for detecting contiguous extents, which we ignore */
    unsigned int i;
    int last = 0; /*flag for when we reach last extent of file */

    memset(fiemap, 0, sizeof(struct fiemap));
    do {
        fiemap->fm_length = FIEMAP_MAX_OFFSET;
        fiemap->fm_flags = 0;
        fiemap->fm_extent_count = count;  /* only ask for as many extents as we can fit in our buffer */
        if ( ioctl(fd, FS_IOC_FIEMAP, (unsigned long) fiemap) < 0 ){
            info("FIEMAP failed in setup_fiemap_extents for file %s\n",
                       path);
        } else {
            for (i = 0; i < fiemap->fm_mapped_extents; i++) {
                if (i == 0 || fm_ext[i].fe_physical != expected) {
                    /*not contiguous extents*/
                    //info("File offset %llu Disk offset %llu\n",
                    //     fm_ext[i].fe_logical,
                    //     fm_ext[i].fe_physical);
                    OffsetEntry *offset_entry = g_new0(OffsetEntry, 1);
                    offset_entry->logical=fm_ext[i].fe_logical;
                    offset_entry->physical=fm_ext[i].fe_physical;
                    g_sequence_append(self, offset_entry);
                }
                expected = fm_ext[i].fe_physical + fm_ext[i].fe_length;
                if (fm_ext[i].fe_flags & FIEMAP_EXTENT_LAST) {
                    last = 1;
                }
            }

            if (last != 1) {
                /* set start for next ioctl read */
                fiemap->fm_start = (fm_ext[i - 1].fe_logical +
                    fm_ext[i - 1].fe_length);
            }
        }
    } while (last == 0);

    close(fd);
    g_sequence_sort(self, (GCompareDataFunc)sort_logical, NULL);

    /*g_sequence_foreach(self, (GFunc)printoffset, NULL);*/
    return self;
}

uint64_t get_disk_offset(GSequence *offset_list, uint64_t file_offset) {

#if !defined (__linux__) && 0  /*TODO - fix this*/
    return 0;  /*for now, no file map info for non-linux systems*/
#else

    if (offset_list == NULL) {
        return 0;
    } else {
        struct OffsetEntry dummy;
        dummy.logical=file_offset;
        dummy.physical=0;
        GSequenceIter *nearest = g_sequence_search (offset_list,
                                                    &dummy,
                                                    (GCompareDataFunc)find_logical,
                                                    NULL
                                                    );
        if (nearest) {
            OffsetEntry *off = g_sequence_get(nearest);
            //info("Nearest to %llu is %llu, %llu\n", file_offset, off->logical, off->physical);
            return off->physical + file_offset - off->logical ;
        } else {
            return 0;
        }
    }
}





#endif


