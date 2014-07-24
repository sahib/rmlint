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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <glibtop/mountlist.h>

//#include "mounttable.h" (already included in rmlint.h)
#include "config.h"

#if HAVE_BLKID
    #include <blkid.h>
#endif

#include "rmlint.h"

static gchar rm_mounts_is_rotational_blockdev(const char *dev) {
    char sys_path[PATH_MAX];
    gchar is_rotational = -1;

    snprintf(sys_path, PATH_MAX, "/sys/block/%s/queue/rotational", dev);

    FILE *sys_fdes = fopen(sys_path, "r");
    if(sys_fdes == NULL) {
        return -1;
    }

    if(fread(&is_rotational, 1, 1, sys_fdes) == 1) {
        is_rotational -= '0';
    }

    fclose(sys_fdes);
    return is_rotational;
}

static void rm_mounts_create_tables(RmMountTable *self) {
    /* partition dev_t to disk dev_t */
    gchar is_rotational;
    dev_t disk_id=0;
    char diskname[PATH_MAX];
    memset(diskname, 0, sizeof(diskname));

    self->part_table = g_hash_table_new_full(
                           g_direct_hash, g_direct_equal,
                           NULL, NULL
                       );

    /* disk dev_t to boolean indication if disk is rotational */
    self->rotational_table = g_hash_table_new_full(
                                 g_direct_hash, g_direct_equal,
                                 NULL, NULL
                             );

#if HAVE_BLKID
    glibtop_mountlist mount_list;
    glibtop_mountentry *mount_entries = glibtop_get_mountlist(&mount_list, true);


	if (mount_entries == NULL) {
		info("can't get glibtop_get_mountlist\n");
		return;
	}

     for (guint64 index = 0; index < mount_list.number; index++) {


        struct stat stat_buf_folder;
        if(stat(mount_entries[index].mountdir, &stat_buf_folder) == -1) {
            rm_perror(mount_entries[index].mountdir);
            continue;
        }

        struct stat stat_buf_dev;
        if(stat(mount_entries[index].devname, &stat_buf_dev) == -1) {
            /*---can't get device for this mount; assume non-rotational and add *
             * to mount table using mountpoint device number ---*/
            debug("%s:%d: Can't stat %s (%s)\n", __FILE__, __LINE__, mount_entries[index].mountdir, g_strerror(errno));
            /* use debug level here rather than error, info or warn because using "true" for glibtop_get_mountlist *
             * means we get a lot of stuff including NFS mounts whose devname's won't stat()  */
            is_rotational = true;
            disk_id=stat_buf_folder.st_dev;
            strcpy(diskname, mount_entries[index].devname);
            info("no stat: %s; assuming non-rotational\n", mount_entries[index].devname);
        } else {
            /*--- get device for this mount and check if rotational ---*/
            dev_t whole_disk = 0;

            if(blkid_devno_to_wholedisk(stat_buf_dev.st_rdev, diskname, sizeof(diskname), &whole_disk) == -1) {
                /*---can't get device for this mount; assume non-rotational and add *
                 * to mount table using mountpoint device number ---*/
                is_rotational = true;
                disk_id=stat_buf_folder.st_dev;
                strcpy(diskname, mount_entries[index].devname);
                info("no blkid_devno_to_wholedisk: %s; assuming non-rotational\n", mount_entries[index].devname);
            } else {
                /* got proper wholedisk data from blkid */
                disk_id=whole_disk;
                is_rotational = rm_mounts_is_rotational_blockdev(diskname);
                info("    %s is rotational: %s\n", diskname, is_rotational ? "yes" : "no");

                /* small hack, so also the full disk id can be given to the api below */
                g_hash_table_insert(
                    self->part_table,
                    GINT_TO_POINTER(disk_id),
                    GINT_TO_POINTER(disk_id)
                );
            }
        }


        info("%lu %s -> %lu (%02d:%02d) %s\n",
             stat_buf_folder.st_dev,
             mount_entries[index].mountdir,
             disk_id, major(disk_id), minor(disk_id),
             diskname
            );

        g_hash_table_insert(
            self->part_table,
            GINT_TO_POINTER(stat_buf_folder.st_dev),
            GINT_TO_POINTER(disk_id)
        );

        if(is_rotational != -1) {
            g_hash_table_insert(
                self->rotational_table,
                GINT_TO_POINTER(disk_id),
                GINT_TO_POINTER(!is_rotational)
            );

        }

    }
    /*TODO:  do we need to free () mount_entries?*/

#endif
}

/////////////////////////////////
//         PUBLIC API          //
/////////////////////////////////


RmMountTable *rm_mounts_table_new(void) {
    RmMountTable *self = g_slice_new(RmMountTable);
    rm_mounts_create_tables(self);
    return self;
}

void rm_mounts_table_destroy(RmMountTable *self) {
    g_hash_table_unref(self->part_table);
    g_hash_table_unref(self->rotational_table);
    g_slice_free(RmMountTable, self);
}

bool rm_mounts_is_nonrotational(RmMountTable *self, dev_t device) {
    dev_t disk_id = rm_mounts_get_disk_id(self, device);
    return GPOINTER_TO_INT(
               g_hash_table_lookup(
                   self->rotational_table, GINT_TO_POINTER(disk_id)
               )
           );
}

bool rm_mounts_is_nonrotational_by_path(RmMountTable *self, const char *path) {
    struct stat stat_buf;
    if(stat(path, &stat_buf) == -1) {
        return -1;
    }

    return rm_mounts_is_nonrotational(self, stat_buf.st_dev);
}

dev_t rm_mounts_get_disk_id(RmMountTable *self, dev_t partition) {
#if HAVE_BLKID
    return GPOINTER_TO_INT(
               g_hash_table_lookup(
                   self->part_table, GINT_TO_POINTER(partition)
               )
           );
#else
    return partition;
#endif
}

dev_t rm_mounts_get_disk_id_by_path(RmMountTable *self, const char *path) {
    struct stat stat_buf;
    if(stat(path, &stat_buf) == -1) {
        return -1;
    }

    return rm_mounts_get_disk_id(self, stat_buf.st_dev);
}

#ifdef _RM_COMPILE_MAIN

int main(int argc, char **argv) {
    RmMountTable *table = rm_mounts_table_new();
    g_printerr("\n");
    for(int i = 1; i < argc; ++i) {
        g_printerr("%30s is on %4srotational device and on disk %02u:%02u\n",
                   argv[i],
                   rm_mounts_is_nonrotational_by_path(table, argv[i]) ? "non-" : "",
                   major(rm_mounts_get_disk_id_by_path(table, argv[i])),
                   minor(rm_mounts_get_disk_id_by_path(table, argv[i]))
                  );
    }

    rm_mounts_table_destroy(table);
    return EXIT_SUCCESS;
}

#endif
