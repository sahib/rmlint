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
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <pwd.h>
#include <grp.h>

#include <fts.h>
#include <libgen.h>

#include <mntent.h>

#include <linux/fs.h>
#include <linux/fiemap.h>

/* Internal headers */
#include "config.h"
#include "utilities.h"
#include "cmdline.h"
#include "defs.h"

/* External libraries */
#include <glib.h>
#include <libelf.h>
#include <gelf.h>

#if HAVE_BLKID
    #include <blkid.h>
#endif

////////////////////////////////////
//       GENERAL UTILITES         //
////////////////////////////////////

char *rm_util_basename(const char *filename) {
    char *base = strrchr(filename, G_DIR_SEPARATOR);
    if(base != NULL) {
        /* Return a pointer to the part behind it
         * (which may be the empty string)
         * */
        return base + 1;
    }

    /* It's the full path anyway */
    return (char *)filename;
}

ino_t rm_util_parent_node(const char *path) {
    char *dummy  = g_strdup(path);
    char *parent_path = dirname(dummy);
    g_free(dummy);

    struct stat stat_buf;
    if(!stat(parent_path, &stat_buf)) {
        return stat_buf.st_ino;
    } else {
        return -1;
    }
}

/* checks uid and gid; returns 0 if both ok, else RM_LINT_TYPE_ corresponding *
 * to RmFile->filter types                                            */
int rm_util_uid_gid_check(struct stat *statp, RmUserGroupNode **userlist) {
    bool has_gid, has_uid;
    if (rm_userlist_contains(userlist, statp->st_uid, statp->st_gid, &has_uid, &has_gid)) {
        if(has_gid == false && has_uid == false) {
            return RM_LINT_TYPE_BADUGID;
        } else
        if(has_gid == false && has_uid == true) {
                return RM_LINT_TYPE_BADGID;
        } else
        if(has_gid == true && has_uid == false) {
            return RM_LINT_TYPE_BADUID;
        } 
    }

    return RM_LINT_TYPE_UNKNOWN;
}

/* Method to test if a file is non stripped binary. Uses libelf*/
bool rm_util_is_nonstripped(const char *path) {
    bool is_ns = false;
    g_return_val_if_fail(path, false);

    /* inspired by "jschmier"'s answer at http://stackoverflow.com/a/5159890 */
    int fd;

    /* ELF handle */
    Elf *elf;       

    /* section descriptor pointer */
    Elf_Scn *scn;   

    /* section header */
    GElf_Shdr shdr; 

    /* Open ELF file to obtain file descriptor */
    if((fd = open(path, O_RDONLY)) == -1) {
        warning("Error opening file '%s' for nonstripped test: ", path);
        rm_perror("");
        return 0;
    }

    /* Protect program from using an older library */
    if(elf_version(EV_CURRENT) == EV_NONE) {
        rm_error("ERROR - ELF Library is out of date!\n");
        return false;
    }

    /* Initialize elf pointer for examining contents of file */
    elf = elf_begin(fd, ELF_C_READ, NULL);

    /* Initialize section descriptor pointer so that elf_nextscn()
     * returns a pointer to the section descriptor at index 1. 
     * */
    scn = NULL;

    /* Iterate through ELF sections */
    while((scn = elf_nextscn(elf, scn)) != NULL) {
        /* Retrieve section header */
        gelf_getshdr(scn, &shdr);

        /* If a section header holding a symbol table (.symtab)
         * is found, this ELF file has not been stripped. */
        if(shdr.sh_type == SHT_SYMTAB) {
            is_ns = true;
            break;
        }
    }
    elf_end(elf);
    close(fd);

    return is_ns;
}

char * rm_util_get_username(void) {
    struct passwd *user = getpwuid(geteuid());
    if(user) {
        return user->pw_name;
    } else {
        return NULL;
    }
}

char *rm_util_get_groupname(void) {
    struct passwd *user = getpwuid(geteuid());
    struct group *grp = getgrgid(user->pw_gid);
    if(grp) {
        return grp->gr_name;
    } else {
        return NULL;
    }
}

/////////////////////////////////////
//   UID/GID VALIDITY CHECKING     //
/////////////////////////////////////

RmUserGroupNode **rm_userlist_new(void) {
    struct passwd *node = NULL;
    struct group *grp = NULL;

    GArray *array = g_array_new(TRUE, TRUE, sizeof(RmUserGroupNode *));

    setpwent();
    while((node = getpwent()) != NULL) {
        RmUserGroupNode * item = g_malloc0(sizeof(RmUserGroupNode));
        item->gid = node->pw_gid;
        item->uid = node->pw_uid;
        g_array_append_val(array, item);
    }

    /* add all groups, not just those that are user primary gid's */
    while((grp = getgrent()) != NULL) {
        RmUserGroupNode * item = g_malloc0(sizeof(RmUserGroupNode));
        item->gid = grp->gr_gid;
        item->uid = 0;
        g_array_append_val(array, item);
    }

    endpwent();
    endgrent();
    return (RmUserGroupNode **)g_array_free(array, false);
}

bool rm_userlist_contains(RmUserGroupNode **list, unsigned long uid, unsigned gid, bool *valid_uid, bool *valid_gid) {
    g_assert(list);

    bool rc = false;
    bool gid_found = false;
    bool uid_found = false;

    for(int i = 0; list[i] && rc == false; ++i) {
        if(list[i]->uid == uid) {
            uid_found = true;
        }
        if(list[i]->gid == gid) {
            gid_found = true;
        }

        rc = (gid_found && uid_found);
    }

    if(valid_uid != NULL) {
        *valid_uid = uid_found;
    }

    if(valid_gid != NULL) {
        *valid_gid = gid_found;
    }
    return rc;
}

void rm_userlist_destroy(RmUserGroupNode **list) {
    for(int i = 0; list[i]; ++i) {
        g_free(list[i]);
    }
    g_free(list);
}

/////////////////////////////////////
//    MOUNTTABLE IMPLEMENTATION    //
/////////////////////////////////////

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
                if(g_hash_table_contains(self->diskname_table, GUINT_TO_POINTER(whole_disk))) {
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
            GUINT_TO_POINTER(stat_buf_folder.st_dev),
            GUINT_TO_POINTER(whole_disk));

        self->mounted_paths = g_list_prepend(self->mounted_paths, g_strdup(entry->mnt_dir));

        /* small hack, so also the full disk id can be given to the api below */
        g_hash_table_insert(
            self->part_table,
            GUINT_TO_POINTER(whole_disk),
            GUINT_TO_POINTER(whole_disk)
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
                GUINT_TO_POINTER(whole_disk),
                GUINT_TO_POINTER(!is_rotational)
            );
        }
        g_hash_table_insert(
            self->diskname_table,
            GUINT_TO_POINTER(whole_disk),
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
    return GPOINTER_TO_UINT(
               g_hash_table_lookup(
                   self->rotational_table, GUINT_TO_POINTER(disk_id)
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
    return GPOINTER_TO_UINT(
               g_hash_table_lookup(
                   self->part_table, GUINT_TO_POINTER(partition)
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

/////////////////////////////////
//    FIEMAP IMPLEMENATION     //
/////////////////////////////////

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

RmOffsetTable rm_offset_create_table(const char *path) {
    int fd = open(path, O_RDONLY);
    if(fd == -1) {
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

        if(ioctl(fd, FS_IOC_FIEMAP, (unsigned long) fiemap) == -1) {
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

        /* Remember all non contiguous extents */
        for(unsigned i = 0; i < fiemap->fm_mapped_extents && !last; i++) {
            if (i == 0 || fm_ext[i].fe_physical != expected) {
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

/////////////////////////////////
//     IFDEFD TEST MAINS       //
/////////////////////////////////

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
#ifdef _RM_COMPILE_MAIN_USERLIST

#define yes(v) (v) ? "True" : "False"

int main(int argc, char *argv[]) {
    struct stat stat_buf;
    bool has_gid, has_uid;
    RmUserGroupNode **list = rm_userlist_new();
    if(argc < 2) {
        puts("Usage: prog <path>");
        return EXIT_FAILURE;
    }
    if(stat(argv[1], &stat_buf) != 0) {
        return EXIT_FAILURE;
    }
    printf("File has UID %lu and GID %lu\n",
           (unsigned long)stat_buf.st_uid,
           (unsigned long)stat_buf.st_gid
    );
    rm_userlist_contains(list, stat_buf.st_uid, stat_buf.st_gid, &has_uid, &has_gid);
    printf("=> Valid UID = %s\n", yes(has_uid));
    printf("=> Valid GID = %s\n", yes(has_gid));
    rm_userlist_destroy(list);
    return EXIT_SUCCESS;
}
#endif
