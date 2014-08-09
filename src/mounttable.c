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
    self->part_table = g_hash_table_new_full(
                           g_direct_hash, g_direct_equal,
                           NULL, NULL
                       );

    /* disk dev_t to boolean indication if disk is rotational */
    self->rotational_table = g_hash_table_new_full(
                                 g_direct_hash, g_direct_equal,
                                 NULL, NULL
                             );
    self->diskname_table = g_hash_table_new_full(
                               g_direct_hash, g_direct_equal,
                               NULL, g_free
                           );

    self->mounted_paths = NULL;

#if HAVE_BLKID

    // TODO: Remove dependency to libgtop, use getmntent().
    //       Someone might want to implement a BSD port with getmntinfo().

    glibtop_mountlist mount_list;
    glibtop_mountentry *mount_entries = glibtop_get_mountlist(&mount_list, true);

    if (mount_entries == NULL) {
        info("can't get glibtop_get_mountlist, some optimizations are disabled.\n");
        return;
    }

    for(unsigned index = 0; index < mount_list.number; index++) {
        struct stat stat_buf_folder;
        if(stat(mount_entries[index].mountdir, &stat_buf_folder) == -1) {
            continue;
        }

        dev_t whole_disk = 0;
        gchar is_rotational;
        char diskname[PATH_MAX];
        memset(diskname, 0, sizeof(diskname));

        struct stat stat_buf_dev;
        if(stat(mount_entries[index].devname, &stat_buf_dev) == -1) {
            /* folder stat() is ok but devname stat() is not; this happens for example
             * with tmpfs and with nfs mounts.  Try to handle a few such cases*/
            if ( strcmp(mount_entries[index].devname, "tmpfs") == 0 ) {
                strcpy(diskname, mount_entries[index].devname);
                is_rotational = 0;
                whole_disk = stat_buf_folder.st_dev;
            } else if ( strstr(mount_entries[index].devname, ":/") != NULL ) {
                unsigned long i;
                for (i = 0;
                        mount_entries[index].devname[i] != '/' && i < sizeof(diskname);
                        i++ ) {
                    diskname[i] = mount_entries[index].devname[i];
                }
                diskname[i] = 0;
                is_rotational = 1;
                whole_disk = 0;  /* treat all NFS mounts as a single rotational    *
                                  * TODO: make this different for each nfs server? */
            } else {
                strcpy(diskname, "unknown");
                whole_disk = 0;
                is_rotational = 1;
            }

        } else {

            if(blkid_devno_to_wholedisk(stat_buf_dev.st_rdev, diskname, sizeof(diskname), &whole_disk) == -1) {
                /* folder and devname stat() are ok but blkid failed; this happens when ??
                 * Treat as a non-rotational device using devname dev as whole_disk key*/
                rm_error(RED"blkid_devno_to_wholedisk failed for %s\n"NCO, mount_entries[index].devname);
                whole_disk = stat_buf_dev.st_dev;
                is_rotational = 0;
                size_t safe_copy = strlen(mount_entries[index].devname) < sizeof(diskname)
                                   ? strlen(mount_entries[index].devname)
                                   : sizeof(diskname) - 1;
                strncpy(diskname, mount_entries[index].devname, safe_copy );

            } else {
                is_rotational = rm_mounts_is_rotational_blockdev(diskname);
            }
        }
        g_hash_table_insert(
            self->part_table,
            GINT_TO_POINTER(stat_buf_folder.st_dev),
            GINT_TO_POINTER(whole_disk));

        self->mounted_paths = g_list_prepend(self->mounted_paths, g_strdup(mount_entries[index].mountdir));

        /* small hack, so also the full disk id can be given to the api below */
        g_hash_table_insert(
            self->part_table,
            GINT_TO_POINTER(whole_disk),
            GINT_TO_POINTER(whole_disk)
        );


        info("%02u:%02u %50s -> %02u:%02u %-10s (underlying disk: %s; rotational: %s)",
             major(stat_buf_folder.st_dev), minor(stat_buf_folder.st_dev),
             mount_entries[index].mountdir,
             major(whole_disk), minor(whole_disk),
             mount_entries[index].devname,
             diskname, is_rotational ? "yes" : "no"
            );

        if(is_rotational != -1) {
            g_hash_table_insert(
                self->rotational_table,
                GINT_TO_POINTER(whole_disk),
                GINT_TO_POINTER(!is_rotational)
            );
        }
        g_hash_table_insert(
            self->diskname_table,
            GINT_TO_POINTER(whole_disk),
            g_strdup(diskname)
        );
    }

    g_free(mount_entries);
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
    g_hash_table_unref(self->diskname_table);
    g_list_free_full(self->mounted_paths, g_free);
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
        return 0;
    }

    return rm_mounts_get_disk_id(self, stat_buf.st_dev);
}

char *rm_mounts_get_name(RmMountTable *self, dev_t device) {
    return (gpointer)g_hash_table_lookup (self->diskname_table, GINT_TO_POINTER(device));
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
