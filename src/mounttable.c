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
#include <mntent.h>

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

static bool rm_mounts_is_ramdisk(const char *fs_type) {
    const char *valid[] = {
        "tmpfs", "rootfs", "devtmpfs",
        "cgroup", "proc", "sys", "dev",
        NULL
    };

    for(int i = 0; valid[i]; ++i) {
        if(strcmp(valid[i], fs_type) == 0) {
            return true;
        }
    }

    return false;
}

static void rm_mounts_create_tables(RmMountTable *self) {
    /* partition dev_t to disk dev_t */
    self->part_table = g_hash_table_new(NULL, NULL);

    /* disk dev_t to boolean indication if disk is rotational */
    self->rotational_table = g_hash_table_new(NULL, NULL);

    /* Mapping from disk dev_t to diskname */
    self->diskname_table = g_hash_table_new_full(NULL, NULL, NULL, g_free);

    self->mounted_paths = NULL;

    /* 0:0 is reserved for the completely unknown */
    int nfs_counter = 1;
    FILE *mnt_file = setmntent("/etc/mtab", "r");

    struct mntent *entry = NULL;
    while((entry = getmntent(mnt_file))) {
        struct stat stat_buf_folder;
        if(stat(entry->mnt_dir, &stat_buf_folder) == -1) {
            continue;
        }

        dev_t whole_disk = 0;
        gchar is_rotational = true;
        char diskname[PATH_MAX];
        memset(diskname, 0, sizeof(diskname));

        struct stat stat_buf_dev;
        if(stat(entry->mnt_fsname, &stat_buf_dev) == -1) {
            char *nfs_marker = NULL;

            /* folder stat() is ok but devname stat() is not; this happens for example
             * with tmpfs and with nfs mounts.  Try to handle a few such cases.
             * */
            if(rm_mounts_is_ramdisk(entry->mnt_fsname)) {
                strncpy(diskname, entry->mnt_fsname, sizeof(diskname));
                is_rotational = false;
                whole_disk = stat_buf_folder.st_dev;
            } else if((nfs_marker = strstr(entry->mnt_fsname, ":/")) != NULL) {
                size_t until_slash = MIN((int)sizeof(diskname), entry->mnt_fsname - nfs_marker);
                strncpy(diskname, entry->mnt_fsname, until_slash);
                is_rotational = true;

                /* Assign different dev ids (with major id 0) to different nfs servers */
                whole_disk = makedev(0, nfs_counter);
                if(g_hash_table_contains(self->diskname_table, GINT_TO_POINTER(whole_disk))) {
                    nfs_counter += 1;
                    whole_disk = makedev(0, nfs_counter);  
                }
            } else {
                strncpy(diskname, "unknown", sizeof(diskname));
                is_rotational = true;
                whole_disk = 0;
            }
        } else {
#if HAVE_BLKID
            if(blkid_devno_to_wholedisk(stat_buf_dev.st_rdev, diskname, sizeof(diskname), &whole_disk) == -1) {
                /* folder and devname stat() are ok but blkid failed; this happens when?
                 * Treat as a non-rotational device using devname dev as whole_disk key
                 * */
                rm_error(RED"blkid_devno_to_wholedisk failed for %s\n"NCO, entry->mnt_fsname);
                whole_disk = stat_buf_dev.st_dev;
                strncpy(diskname, entry->mnt_fsname, sizeof(diskname));
                is_rotational = false;
            } else {
                is_rotational = rm_mounts_is_rotational_blockdev(diskname);
            }
#else
            is_rotational = true;
            whole_disk = stat_buf_dev.st_dev;
            strncpy(diskname, "blkid_missing", sizeof(diskname));
#endif
        }

        g_hash_table_insert(
            self->part_table,
            GINT_TO_POINTER(stat_buf_folder.st_dev),
            GINT_TO_POINTER(whole_disk));

        self->mounted_paths = g_list_prepend(self->mounted_paths, g_strdup(entry->mnt_dir));

        /* small hack, so also the full disk id can be given to the api below */
        g_hash_table_insert(
            self->part_table,
            GINT_TO_POINTER(whole_disk),
            GINT_TO_POINTER(whole_disk)
        );

        info("%02u:%02u %50s -> %02u:%02u %-12s (underlying disk: %15s; rotational: %3s)",
             major(stat_buf_folder.st_dev), minor(stat_buf_folder.st_dev),
             entry->mnt_dir,
             major(whole_disk), minor(whole_disk),
             entry->mnt_fsname, diskname, is_rotational ? "yes" : "no"
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

    endmntent(mnt_file);
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
    return GPOINTER_TO_INT(
               g_hash_table_lookup(
                   self->part_table, GINT_TO_POINTER(partition)
               )
           );
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

#ifdef _RM_COMPILE_MAIN_MOUNTS

int main(int argc, char **argv) {
    RmMountTable *table = rm_mounts_table_new();
    g_printerr("\n");
    for(int i = 1; i < argc; ++i) {
        dev_t dev = rm_mounts_get_disk_id_by_path(table, argv[i]);
        g_printerr(
            "%30s is on %4srotational device \"%s\" and on disk %02u:%02u\n",
            argv[i],
            rm_mounts_is_nonrotational_by_path(table, argv[i]) ? "non-" : "",
            rm_mounts_get_name(table, dev),
            major(dev), minor(dev)
        );
    }

    rm_mounts_table_destroy(table);
    return EXIT_SUCCESS;
}

#endif
