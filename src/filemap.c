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
#include "rmlint.h"

#include <linux/fs.h>
#include <linux/fiemap.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <glib.h>

typedef struct RmOffsetEntry {
    guint64 logical;
    guint64 physical;
} RmOffsetEntry;

/* sort sequence into decreasing order of logical offsets */
static int rm_offset_sort_logical(gconstpointer a, gconstpointer b) {
    const RmOffsetEntry *offset_a = a;
    const RmOffsetEntry *offset_b = b;
    if (offset_b->logical > offset_a->logical) {
        return 1;
    } else if (offset_b->logical == offset_a->logical) {
        return 0;
    } else {
        return -1;
    }
}

/* find first item in sequence with logical offset <= target */
static int rm_offset_find_logical(gconstpointer a, gconstpointer b) {
    const RmOffsetEntry *offset_a = a;
    const RmOffsetEntry *offset_b = b;
    if (offset_b->logical >= offset_a->logical) {
        return 1;
    } else {
        return 0;
    }
}

static void rm_offset_free_func(RmOffsetEntry *entry) {
    g_slice_free(RmOffsetEntry, entry);
}

/////////////////////////////////
//         PUBLIC API          //
/////////////////////////////////

RmOffsetTable rm_offset_create_table(const char *path) {
    int fd = open(path, O_RDONLY);
    if(fd < 0) {
        info("Error opening %s in setup_fiemap_extents\n", path);
        return NULL;
    }

    /* struct fiemap does not allocate any extents by default, 
     * so we choose ourself how many of them we allocate. 
     * */
    const int n_extents = 256;
    struct fiemap *fiemap = g_malloc0(sizeof(struct fiemap) + n_extents * sizeof(struct fiemap_extent));
    struct fiemap_extent *fm_ext = fiemap->fm_extents;

    /* data structure we save our offsets in */
    GSequence *self = g_sequence_new((GFreeFunc)rm_offset_free_func);

    bool last = false;
    while(!last) {
        fiemap->fm_flags = 0;
        fiemap->fm_extent_count = n_extents;
        fiemap->fm_length = FIEMAP_MAX_OFFSET;

        if(ioctl(fd, FS_IOC_FIEMAP, (unsigned long) fiemap) < 0) {
            break;
        }

        /* This might happen on empty files - those have no 
         * extents, but they have a offset on the disk.
         */
        if(fiemap->fm_mapped_extents <= 0) {
            break;
        }

        /* used for detecting contiguous extents, which we ignore */
        unsigned long expected = 0;

        unsigned i;
        for (i = 0; i < fiemap->fm_mapped_extents && !last; i++) {
            if (i == 0 || fm_ext[i].fe_physical != expected) {
                /* not a contiguous extents */
                RmOffsetEntry *offset_entry = g_slice_new(RmOffsetEntry);
                offset_entry->logical = fm_ext[i].fe_logical;
                offset_entry->physical = fm_ext[i].fe_physical;
                g_sequence_append(self, offset_entry);
            }

            expected = fm_ext[i].fe_physical + fm_ext[i].fe_length;
            fiemap->fm_start = fm_ext[i].fe_logical + fm_ext[i].fe_length;
            last = fm_ext[i].fe_flags & FIEMAP_EXTENT_LAST;
        }
    }

    close(fd);
    g_free(fiemap);

    g_sequence_sort(self, (GCompareDataFunc)rm_offset_sort_logical, NULL);
    return self;
}

guint64 rm_offset_lookup(RmOffsetTable offset_list, guint64 file_offset) {
#ifdef __linux__   
    if (offset_list != NULL) {
        RmOffsetEntry token;
        token.physical = 0;
        token.logical = file_offset;

        GSequenceIter * nearest = g_sequence_search(
                             offset_list, &token,
                             (GCompareDataFunc)rm_offset_find_logical, NULL
                         );

        if(!g_sequence_iter_is_end(nearest)) {
            RmOffsetEntry *off = g_sequence_get(nearest);
            return off->physical + file_offset - off->logical ;
        } 
    }
#endif
    /* default to 0 always */
    return 0;
}

#ifdef _RM_COMPILE_MAIN_FIEMAP
    int main(int argc, char const *argv[]) {
        if(argc < 3) {
            return EXIT_FAILURE;
        }

        GSequence *db = rm_offset_create_table(argv[1]);
        guint64 off = rm_offset_lookup(db, g_ascii_strtoll(argv[2], NULL, 10));

        g_printerr("Offset: %lu\n", off);
        g_sequence_free(db);

        return EXIT_SUCCESS;
    }
#endif
