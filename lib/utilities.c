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
 *  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "session.h"

/* Be safe: This header is not essential and might be missing on some systems.
 * We only include it here, because it fixes some recent warning...
 * */
#if HAVE_SYSMACROS_H
# include <sys/sysmacros.h>
#endif


#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <grp.h>
#include <pwd.h>

#include <libgen.h>

/* Not available there,
 * but might be on other non-linux systems
 * */
#if HAVE_GIO_UNIX
#include <gio/gunixmounts.h>
#endif

#if HAVE_FIEMAP
#include <linux/fiemap.h>
#include <linux/fs.h>
#endif

/* Internal headers */
#include "config.h"
#include "file.h"
#include "utilities.h"

/* External libraries */
#include <glib.h>

#if HAVE_LIBELF
#include <gelf.h>
#include <libelf.h>
#endif

#if HAVE_BLKID
#include <blkid/blkid.h>
#endif

#if HAVE_JSON_GLIB
#include <json-glib/json-glib.h>
#endif

#define RM_MOUNTTABLE_IS_USABLE (HAVE_BLKID && HAVE_GIO_UNIX)

////////////////////////////////////
//       GENERAL UTILITES         //
////////////////////////////////////

char *rm_util_strsub(const char *string, const char *subs, const char *with) {
    gchar *result = NULL;
    if(string != NULL && string[0] != '\0') {
        gchar **split = g_strsplit(string, subs, 0);
        if(split != NULL) {
            result = g_strjoinv(with, split);
        }
        g_strfreev(split);
    }
    return result;
}

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

char *rm_util_path_extension(const char *basename) {
    char *point = strrchr(basename, '.');
    if(point) {
        return point + 1;
    } else {
        return NULL;
    }
}

bool rm_util_path_is_hidden(const char *path) {
    if(path == NULL) {
        return false;
    }

    if(*path == '.') {
        return true;
    }

    while(*path++) {
        /* Search for '/.' */
        if(*path == G_DIR_SEPARATOR && *(path + 1) == '.') {
            return true;
        }
    }

    return false;
}

int rm_util_path_depth(const char *path) {
    int depth = 0;

    while(path && *path) {
        /* Skip trailing slashes */
        if(*path == G_DIR_SEPARATOR && path[1] != 0) {
            depth++;
        }
        path = strchr(&path[1], G_DIR_SEPARATOR);
    }

    return depth;
}

GQueue *rm_hash_table_setdefault(GHashTable *table, gpointer key,
                                 RmNewFunc default_func) {
    gpointer value = g_hash_table_lookup(table, key);
    if(value == NULL) {
        value = default_func();
        g_hash_table_insert(table, key, value);
    }

    return value;
}

ino_t rm_util_parent_node(const char *path) {
    char *parent_path = g_path_get_dirname(path);

    RmStat stat_buf;
    if(!rm_sys_stat(parent_path, &stat_buf)) {
        g_free(parent_path);
        return stat_buf.st_ino;
    } else {
        g_free(parent_path);
        return -1;
    }
}

void rm_util_queue_push_tail_queue(GQueue *dest, GQueue *src) {
    g_return_if_fail(dest);
    g_return_if_fail(src);

    if(src->length == 0) {
        return;
    }

    src->head->prev = dest->tail;
    if(dest->tail) {
        dest->tail->next = src->head;
    } else {
        dest->head = src->head;
    }
    dest->tail = src->tail;
    dest->length += src->length;
    src->length = 0;
    src->head = src->tail = NULL;
}

gint rm_util_queue_foreach_remove(GQueue *queue, RmRFunc func, gpointer user_data) {
    gint removed = 0;

    for(GList *iter = queue->head, *next = NULL; iter; iter = next) {
        next = iter->next;
        if(func(iter->data, user_data)) {
            g_queue_delete_link(queue, iter);
            ++removed;
        }
    }
    return removed;
}

gint rm_util_list_foreach_remove(GList **list, RmRFunc func, gpointer user_data) {
    gint removed = 0;

    /* iterate over list */
    for(GList *iter = *list, *next = NULL; iter; iter = next) {
        next = iter->next;
        if(func(iter->data, user_data)) {
            /* delete iter from GList */
            if(iter->prev) {
                (iter->prev)->next = next;
            } else {
                *list = next;
            }
            g_list_free_1(iter);
            ++removed;
        }
    }
    return removed;
}

gint rm_util_slist_foreach_remove(GSList **list, RmRFunc func, gpointer user_data) {
    gint removed = 0;

    /* iterate over list, keeping track of previous and next entries */
    for(GSList *prev = NULL, *iter = *list, *next = NULL; iter; iter = next) {
        next = iter->next;
        if(func(iter->data, user_data)) {
            /* delete iter from GSList */
            g_slist_free1(iter);
            if(prev) {
                prev->next = next;
            } else {
                *list = next;
            }
            ++removed;
        } else {
            prev = iter;
        }
    }
    return removed;
}

gpointer rm_util_slist_pop(GSList **list, GMutex *lock) {
    gpointer result = NULL;
    if(lock) {
        g_mutex_lock(lock);
    }
    if(*list) {
        result = (*list)->data;
        *list = g_slist_delete_link(*list, *list);
    }
    if(lock) {
        g_mutex_unlock(lock);
    }
    return result;
}

/* checks uid and gid; returns 0 if both ok, else RM_LINT_TYPE_ corresponding *
 * to RmFile->filter types
 * */
int rm_util_uid_gid_check(RmStat *statp, RmUserList *userlist) {
    bool has_gid = 1, has_uid = 1;
    if(!rm_userlist_contains(userlist, statp->st_uid, statp->st_gid, &has_uid,
                             &has_gid)) {
        if(has_gid == false && has_uid == false) {
            return RM_LINT_TYPE_BADUGID;
        } else if(has_gid == false && has_uid == true) {
            return RM_LINT_TYPE_BADGID;
        } else if(has_gid == true && has_uid == false) {
            return RM_LINT_TYPE_BADUID;
        }
    }

    return RM_LINT_TYPE_UNKNOWN;
}

/* Method to test if a file is non stripped binary. Uses libelf*/
bool rm_util_is_nonstripped(_UNUSED const char *path, _UNUSED RmStat *statp) {
    bool is_ns = false;

#if HAVE_LIBELF
    g_return_val_if_fail(path, false);

    if(statp && (statp->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0) {
        return false;
    }

    /* inspired by "jschmier"'s answer at http://stackoverflow.com/a/5159890 */
    int fd;

    /* ELF handle */
    Elf *elf;

    /* section descriptor pointer */
    Elf_Scn *scn;

    /* section header */
    GElf_Shdr shdr;

    /* Open ELF file to obtain file descriptor */
    if((fd = rm_sys_open(path, O_RDONLY)) == -1) {
        rm_log_warning_line(_("cannot open file '%s' for nonstripped test: "), path);
        rm_log_perror("");
        return 0;
    }

    /* Protect program from using an older library */
    if(elf_version(EV_CURRENT) == EV_NONE) {
        rm_log_error_line(_("ELF Library is out of date!"));
        rm_sys_close(fd);
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
    rm_sys_close(fd);
#endif

    return is_ns;
}

char *rm_util_get_username(void) {
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

void rm_util_size_to_human_readable(RmOff num, char *in, gsize len) {
    if(num < 512) {
        snprintf(in, len, "%" LLU " B", num);
    } else if(num < 512 * 1024) {
        snprintf(in, len, "%.2f KB", num / 1024.0);
    } else if(num < 512 * 1024 * 1024) {
        snprintf(in, len, "%.2f MB", num / (1024.0 * 1024.0));
    } else {
        snprintf(in, len, "%.2f GB", num / (1024.0 * 1024.0 * 1024.0));
    }
}

/////////////////////////////////////
//   UID/GID VALIDITY CHECKING     //
/////////////////////////////////////

static int rm_userlist_cmp_ids(gconstpointer a, gconstpointer b, _UNUSED gpointer ud) {
    return GPOINTER_TO_UINT(a) - GPOINTER_TO_UINT(b);
}

RmUserList *rm_userlist_new(void) {
    struct passwd *node = NULL;
    struct group *grp = NULL;

    RmUserList *self = g_malloc0(sizeof(RmUserList));
    self->users = g_sequence_new(NULL);
    self->groups = g_sequence_new(NULL);

    setpwent();
    while((node = getpwent()) != NULL) {
        g_sequence_insert_sorted(self->users, GUINT_TO_POINTER(node->pw_uid),
                                 rm_userlist_cmp_ids, NULL);
        g_sequence_insert_sorted(self->groups, GUINT_TO_POINTER(node->pw_gid),
                                 rm_userlist_cmp_ids, NULL);
    }
    endpwent();

    /* add all groups, not just those that are user primary gid's */
    while((grp = getgrent()) != NULL) {
        g_sequence_insert_sorted(self->groups, GUINT_TO_POINTER(grp->gr_gid),
                                 rm_userlist_cmp_ids, NULL);
    }

    endgrent();
    g_mutex_init(&self->lock);
    return self;
}

bool rm_userlist_contains(RmUserList *self, unsigned long uid, unsigned gid,
                          bool *valid_uid, bool *valid_gid) {
    g_assert(self);
    bool gid_found = FALSE;
    bool uid_found = FALSE;

    g_mutex_lock(&self->lock);
    {
        gid_found = g_sequence_lookup(self->groups, GUINT_TO_POINTER(gid),
                                      rm_userlist_cmp_ids, NULL);
        uid_found = g_sequence_lookup(self->users, GUINT_TO_POINTER(uid),
                                      rm_userlist_cmp_ids, NULL);
    }
    g_mutex_unlock(&self->lock);

    if(valid_uid != NULL) {
        *valid_uid = uid_found;
    }

    if(valid_gid != NULL) {
        *valid_gid = gid_found;
    }

    return (gid_found && uid_found);
}

void rm_userlist_destroy(RmUserList *self) {
    g_assert(self);

    g_sequence_free(self->users);
    g_sequence_free(self->groups);
    g_mutex_clear(&self->lock);
    g_free(self);
}

/////////////////////////////////////
//    MOUNTTABLE IMPLEMENTATION    //
/////////////////////////////////////

typedef struct RmDiskInfo {
    char *name;
    bool is_rotational;
} RmDiskInfo;

typedef struct RmPartitionInfo {
    char *name;
    char *fsname;
    dev_t disk;
} RmPartitionInfo;

#if RM_MOUNTTABLE_IS_USABLE

RmPartitionInfo *rm_part_info_new(char *name, char *fsname, dev_t disk) {
    RmPartitionInfo *self = g_new0(RmPartitionInfo, 1);
    self->name = g_strdup(name);
    self->fsname = g_strdup(fsname);
    self->disk = disk;
    return self;
}

void rm_part_info_free(RmPartitionInfo *self) {
    g_free(self->name);
    g_free(self->fsname);
    g_free(self);
}

RmDiskInfo *rm_disk_info_new(char *name, char is_rotational) {
    RmDiskInfo *self = g_new0(RmDiskInfo, 1);
    self->name = g_strdup(name);
    self->is_rotational = is_rotational;
    return self;
}

void rm_disk_info_free(RmDiskInfo *self) {
    g_free(self->name);
    g_free(self);
}

static gchar rm_mounts_is_rotational_blockdev(const char *dev) {
    gchar is_rotational = -1;

#if HAVE_SYSBLOCK /* this works only on linux */
    char sys_path[PATH_MAX + 30];
    snprintf(sys_path, sizeof(sys_path) - 1, "/sys/block/%s/queue/rotational", dev);

    FILE *sys_fdes = fopen(sys_path, "r");
    if(sys_fdes == NULL) {
        return -1;
    }

    if(fread(&is_rotational, 1, 1, sys_fdes) == 1) {
        is_rotational -= '0';
    }

    fclose(sys_fdes);
#else
    (void)dev;
#endif

    return is_rotational;
}

static bool rm_mounts_is_ramdisk(const char *fs_type) {
    const char *valid[] = {"tmpfs", "rootfs", "devtmpfs", "cgroup",
                           "proc",  "sys",    "dev",      NULL};

    for(int i = 0; valid[i]; ++i) {
        if(strcmp(valid[i], fs_type) == 0) {
            return true;
        }
    }

    return false;
}

typedef struct RmMountEntry {
    char *fsname; /* name of mounted file system */
    char *dir;    /* file system path prefix     */
    char *type;   /* Type of fs: ufs, nfs, etc   */
} RmMountEntry;

typedef struct RmMountEntries {
    GList *mnt_entries;
    GList *entries;
    GList *current;
} RmMountEntries;

static void rm_mount_list_close(RmMountEntries *self) {
    g_assert(self);

    for(GList *iter = self->entries; iter; iter = iter->next) {
        RmMountEntry *entry = iter->data;
        g_free(entry->fsname);
        g_free(entry->dir);
        g_free(entry->type);
        g_slice_free(RmMountEntry, entry);
    }

    g_list_free_full(self->mnt_entries, (GDestroyNotify)g_unix_mount_free);
    g_list_free(self->entries);
    g_slice_free(RmMountEntries, self);
}

static RmMountEntry *rm_mount_list_next(RmMountEntries *self) {
    g_assert(self);

    if(self->current) {
        self->current = self->current->next;
    } else {
        self->current = self->entries;
    }

    if(self->current) {
        return self->current->data;
    } else {
        return NULL;
    }
}

static bool fs_supports_reflinks(char *fstype, char *mountpoint) {
    if(strcmp(fstype, "btrfs")==0) {
        return true;
    }
    if(strcmp(fstype, "ocfs2")==0) {
        return true;
    }
    if(strcmp(fstype, "xfs")==0) {
        /* xfs *might* support reflinks...*/
        char *cmd = g_strdup_printf("xfs_info '%s' | grep -q 'reflink=1'", mountpoint);
        int res = system(cmd);
        g_free(cmd);
        return(res==0);
    }
    return false;
}

static RmMountEntries *rm_mount_list_open(RmMountTable *table) {
    RmMountEntries *self = g_slice_new(RmMountEntries);

    self->mnt_entries = g_unix_mounts_get(NULL);
    self->entries = NULL;
    self->current = NULL;

    for(GList *iter = self->mnt_entries; iter; iter = iter->next) {
        RmMountEntry *wrap_entry = g_slice_new(RmMountEntry);
        GUnixMountEntry *entry = iter->data;

        wrap_entry->fsname = g_strdup(g_unix_mount_get_device_path(entry));
        wrap_entry->dir = g_strdup(g_unix_mount_get_mount_path(entry));
        wrap_entry->type = g_strdup(g_unix_mount_get_fs_type(entry));

        self->entries = g_list_prepend(self->entries, wrap_entry);
    }

    RmMountEntry *wrap_entry = NULL;
    while((wrap_entry = rm_mount_list_next(self))) {
        /* bindfs mounts mirror directory trees.
        * This cannot be detected properly by rmlint since
        * files in it have the same inode as their unmirrored file, but
        * a different dev_t.
        *
        * Also ignore kernel filesystems.
        *
        * So better go and ignore it.
        */
        static struct RmEvilFs {
            /* fsname as show by `mount` */
            const char *name;

            /* Wether to warn about the exclusion on this */
            bool unusual;
        } evilfs_types[] = {{"bindfs", 1},
                            {"nullfs", 1},
                            /* Ignore the usual linux file system spam */
                            {"proc", 0},
                            {"cgroup", 0},
                            {"configfs", 0},
                            {"sys", 0},
                            {"devtmpfs", 0},
                            {"debugfs", 0},
                            {NULL, 0}};


        const struct RmEvilFs *evilfs_found = NULL;
        for(int i = 0; evilfs_types[i].name && !evilfs_found; ++i) {
            if(strcmp(evilfs_types[i].name, wrap_entry->type) == 0) {
                evilfs_found = &evilfs_types[i];
            }
        }

        if(evilfs_found != NULL) {
            RmStat dir_stat;
            rm_sys_stat(wrap_entry->dir, &dir_stat);
            g_hash_table_insert(table->evilfs_table,
                                GUINT_TO_POINTER(dir_stat.st_dev),
                                GUINT_TO_POINTER(1));

            GLogLevelFlags log_level = G_LOG_LEVEL_DEBUG;

            if(evilfs_found->unusual) {
                log_level = G_LOG_LEVEL_WARNING;
                rm_log_warning_prefix();
            } else {
                rm_log_debug_prefix();
            }

            g_log("rmlint", log_level,
                  _("`%s` mount detected at %s (#%u); Ignoring all files in it.\n"),
                  evilfs_found->name, wrap_entry->dir, (unsigned)dir_stat.st_dev);
        }

        if(fs_supports_reflinks(wrap_entry->type, wrap_entry->dir)) {
            RmStat dir_stat;
            rm_sys_stat(wrap_entry->dir, &dir_stat);
            g_hash_table_insert(table->reflinkfs_table,
                                GUINT_TO_POINTER(dir_stat.st_dev),
                                wrap_entry->type);
            rm_log_debug_line("Filesystem %s: reflink capable", wrap_entry->dir);
        } else {
            rm_log_debug_line("Filesystem %s: not reflink capable", wrap_entry->dir);
        }
    }

    return self;
}

int rm_mounts_devno_to_wholedisk(_UNUSED RmMountEntry *entry, _UNUSED dev_t rdev,
                                 _UNUSED char *disk, _UNUSED size_t disk_size,
                                 _UNUSED dev_t *result) {
    return blkid_devno_to_wholedisk(rdev, disk, disk_size, result);
}

static bool rm_mounts_create_tables(RmMountTable *self, bool force_fiemap) {
    /* partition dev_t to disk dev_t */
    self->part_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                             (GDestroyNotify)rm_part_info_free);

    /* disk dev_t to boolean indication if disk is rotational */
    self->disk_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                             (GDestroyNotify)rm_disk_info_free);

    self->nfs_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* Mapping dev_t => true (used as set) */
    self->evilfs_table = g_hash_table_new(NULL, NULL);
    self->reflinkfs_table = g_hash_table_new(NULL, NULL);

    RmMountEntry *entry = NULL;
    RmMountEntries *mnt_entries = rm_mount_list_open(self);

    if(mnt_entries == NULL) {
        return false;
    }

    while((entry = rm_mount_list_next(mnt_entries))) {
        RmStat stat_buf_folder;
        if(rm_sys_stat(entry->dir, &stat_buf_folder) == -1) {
            continue;
        }

        dev_t whole_disk = 0;
        gchar is_rotational = true;
        char diskname[PATH_MAX];
        memset(diskname, 0, sizeof(diskname));

        RmStat stat_buf_dev;
        if(rm_sys_stat(entry->fsname, &stat_buf_dev) == -1) {
            char *nfs_marker = NULL;
            /* folder rm_sys_stat() is ok but devname rm_sys_stat() is not; this happens
             * for example
             * with tmpfs and with nfs mounts.  Try to handle a few such cases.
             * */
            if(rm_mounts_is_ramdisk(entry->fsname)) {
                strncpy(diskname, entry->fsname, sizeof(diskname)-1);
                is_rotational = false;
                whole_disk = stat_buf_folder.st_dev;
            } else if((nfs_marker = strstr(entry->fsname, ":/")) != NULL) {
                size_t until_slash =
                    MIN((int)sizeof(entry->fsname), nfs_marker - entry->fsname);
                strncpy(diskname, entry->fsname, until_slash);
                is_rotational = true;

                /* Assign different dev ids (with major id 0) to different nfs servers */
                if(!g_hash_table_contains(self->nfs_table, diskname)) {
                    g_hash_table_insert(self->nfs_table, g_strdup(diskname), NULL);
                }
                whole_disk = makedev(0, g_hash_table_size(self->nfs_table));
            } else {
                strncpy(diskname, "unknown", sizeof(diskname));
                is_rotational = true;
                whole_disk = 0;
            }
        } else {
            if(rm_mounts_devno_to_wholedisk(entry, stat_buf_dev.st_rdev, diskname,
                                            sizeof(diskname), &whole_disk) == -1) {
                /* folder and devname rm_sys_stat() are ok but blkid failed; this happens
                 * when?
                 * Treat as a non-rotational device using devname dev as whole_disk key
                 * */
                rm_log_debug_line(RED "devno_to_wholedisk failed for %s" RESET,
                                  entry->fsname);
                whole_disk = stat_buf_dev.st_dev;
                strncpy(diskname, entry->fsname, sizeof(diskname)-1);
                is_rotational = false;
            } else {
                is_rotational = rm_mounts_is_rotational_blockdev(diskname);
            }
        }

        is_rotational |= force_fiemap;

        RmPartitionInfo *existing = g_hash_table_lookup(
            self->part_table, GUINT_TO_POINTER(stat_buf_folder.st_dev));
        if(!existing || (existing->disk == 0 && whole_disk != 0)) {
            if(existing) {
                rm_log_debug_line("Replacing part_table entry %s for path %s with %s",
                                  existing->fsname, entry->dir, entry->fsname);
            }
            g_hash_table_insert(self->part_table,
                                GUINT_TO_POINTER(stat_buf_folder.st_dev),
                                rm_part_info_new(entry->dir, entry->fsname, whole_disk));
        } else {
            rm_log_debug_line("Skipping duplicate mount entry for dir %s dev %02u:%02u",
                              entry->dir, major(stat_buf_folder.st_dev),
                              minor(stat_buf_folder.st_dev));
            continue;
        }

        /* small hack, so also the full disk id can be given to the api below */
        if(!g_hash_table_contains(self->part_table, GINT_TO_POINTER(whole_disk))) {
            g_hash_table_insert(self->part_table,
                                GUINT_TO_POINTER(whole_disk),
                                rm_part_info_new(entry->dir, entry->fsname, whole_disk));
        }

        if(!g_hash_table_contains(self->disk_table, GINT_TO_POINTER(whole_disk))) {
            g_hash_table_insert(self->disk_table,
                                GINT_TO_POINTER(whole_disk),
                                rm_disk_info_new(diskname, is_rotational));
        }

        rm_log_debug_line(
            "%02u:%02u %50s -> %02u:%02u %-12s (underlying disk: %s; rotational: %3s)",
            major(stat_buf_folder.st_dev), minor(stat_buf_folder.st_dev), entry->dir,
            major(whole_disk), minor(whole_disk), entry->fsname, diskname,
            is_rotational ? "yes" : "no");
    }

    rm_mount_list_close(mnt_entries);
    return true;
}

/////////////////////////////////
//         PUBLIC API          //
/////////////////////////////////

RmMountTable *rm_mounts_table_new(bool force_fiemap) {
    RmMountTable *self = g_slice_new(RmMountTable);
    if(rm_mounts_create_tables(self, force_fiemap) == false) {
        g_slice_free(RmMountTable, self);
        return NULL;
    } else {
        return self;
    }
}

void rm_mounts_table_destroy(RmMountTable *self) {
    g_hash_table_unref(self->part_table);
    g_hash_table_unref(self->disk_table);
    g_hash_table_unref(self->nfs_table);
    g_hash_table_unref(self->evilfs_table);
    g_hash_table_unref(self->reflinkfs_table);
    g_slice_free(RmMountTable, self);
}

#else /* probably FreeBSD */

RmMountTable *rm_mounts_table_new(_UNUSED bool force_fiemap) {
    return NULL;
}

void rm_mounts_table_destroy(_UNUSED RmMountTable *self) {
    /* NO-OP */
}

#endif /* RM_MOUNTTABLE_IS_USABLE */

bool rm_mounts_is_nonrotational(RmMountTable *self, dev_t device) {
    if(self == NULL) {
        return true;
    }

    RmPartitionInfo *part =
        g_hash_table_lookup(self->part_table, GINT_TO_POINTER(device));
    if(part) {
        RmDiskInfo *disk =
            g_hash_table_lookup(self->disk_table, GINT_TO_POINTER(part->disk));
        if(disk) {
            return !disk->is_rotational;
        } else {
            rm_log_error_line("Disk not found in rm_mounts_is_nonrotational");
            return true;
        }
    } else {
        rm_log_error_line("Partition not found in rm_mounts_is_nonrotational");
        return true;
    }
}

dev_t rm_mounts_get_disk_id(RmMountTable *self, _UNUSED dev_t dev,
                            _UNUSED const char *path) {
    if(self == NULL) {
        return 0;
    }

#if RM_MOUNTTABLE_IS_USABLE

    RmPartitionInfo *part = g_hash_table_lookup(self->part_table, GINT_TO_POINTER(dev));
    if(part != NULL) {
        return part->disk;
    }

    /* probably a btrfs subvolume which is not a mountpoint;
     * walk up tree until we get to a recognisable partition
     * */
    char *prev = g_strdup(path);
    while(TRUE) {
        char *parent_path = g_path_get_dirname(prev);

        RmStat stat_buf;
        if(!rm_sys_stat(parent_path, &stat_buf)) {
            RmPartitionInfo *parent_part =
                g_hash_table_lookup(self->part_table, GINT_TO_POINTER(stat_buf.st_dev));
            if(parent_part) {
                /* create new partition table entry for dev pointing to parent_part*/
                rm_log_debug_line("Adding partition info for " GREEN "%s" RESET
                                  " - looks like subvolume %s on volume " GREEN
                                  "%s" RESET,
                                  path, prev, parent_part->name);
                part = rm_part_info_new(prev, parent_part->fsname, parent_part->disk);
                g_hash_table_insert(self->part_table, GINT_TO_POINTER(dev), part);
                /* if parent_part is in the reflinkfs_table, add dev as well */
                char *parent_type = g_hash_table_lookup(
                    self->reflinkfs_table, GUINT_TO_POINTER(stat_buf.st_dev));
                if(parent_type) {
                    g_hash_table_insert(self->reflinkfs_table, GUINT_TO_POINTER(dev),
                                        parent_type);
                }
                g_free(prev);
                g_free(parent_path);
                return parent_part->disk;
            }
        }

        if(strcmp(prev, "/") == 0) {
            g_free(prev);
            break;
        }

        g_free(prev);
        prev = parent_path;
    }

    return 0;
#else
    (void)dev;
    (void)path;
    return 0;
#endif
}

dev_t rm_mounts_get_disk_id_by_path(RmMountTable *self, const char *path) {
    if(self == NULL) {
        return 0;
    }

    RmStat stat_buf;
    if(rm_sys_stat(path, &stat_buf) == -1) {
        return 0;
    }

    return rm_mounts_get_disk_id(self, stat_buf.st_dev, path);
}

bool rm_mounts_is_evil(RmMountTable *self, dev_t to_check) {
    if(self == NULL) {
        return false;
    }

    return g_hash_table_contains(self->evilfs_table, GUINT_TO_POINTER(to_check));
}

bool rm_mounts_can_reflink(RmMountTable *self, dev_t source, dev_t dest) {
    g_assert(self);
    if(g_hash_table_contains(self->reflinkfs_table, GUINT_TO_POINTER(source))) {
        if(source == dest) {
            return true;
        } else {
            RmPartitionInfo *source_part =
                g_hash_table_lookup(self->part_table, GINT_TO_POINTER(source));
            RmPartitionInfo *dest_part =
                g_hash_table_lookup(self->part_table, GINT_TO_POINTER(dest));
            g_assert(source_part);
            g_assert(dest_part);
            return (strcmp(source_part->fsname, dest_part->fsname) == 0);
        }
    } else {
        return false;
    }
}

/////////////////////////////////
//    FIEMAP IMPLEMENATION     //
/////////////////////////////////

#if HAVE_FIEMAP

#define _RM_OFFSET_DEBUG 0

/* Return fiemap structure containing n_extents for file descriptor fd.
 * Return NULL if errors encountered.
 * Needs to be freed with g_free if not NULL.
 * */
static struct fiemap *rm_offset_get_fiemap(int fd, const int n_extents,
                                           const uint64_t file_offset) {
#if _RM_OFFSET_DEBUG
    rm_log_debug_line(_("rm_offset_get_fiemap: fd=%d, n_extents=%d, file_offset=%d"),
                      fd, n_extents, file_offset);
#endif
    /* struct fiemap does not allocate any extents by default,
     * so we allocate the nominated number
     * */
    struct fiemap *fm =
        g_malloc0(sizeof(struct fiemap) + n_extents * sizeof(struct fiemap_extent));

    fm->fm_flags = 0;
    fm->fm_extent_count = n_extents;
    fm->fm_length = FIEMAP_MAX_OFFSET;
    fm->fm_start = file_offset;

    if(ioctl(fd, FS_IOC_FIEMAP, (unsigned long)fm) == -1) {
        g_free(fm);
        fm = NULL;
    }
    return fm;
}

/* Return physical (disk) offset of the beginning of the file extent containing the
 * specified logical file_offset.
 * If a pointer to file_offset_next is provided then read fiemap extents until
 * the next non-contiguous extent (fragment) is encountered and writes the corresponding
 * file offset to &file_offset_next.
 * */
RmOff rm_offset_get_from_fd(int fd, RmOff file_offset, RmOff *file_offset_next, bool *is_last) {
    RmOff result = 0;
    bool done = FALSE;
    bool first = TRUE;

    /* used for detecting contiguous extents */
    unsigned long expected = 0;

    fsync(fd);

    while(!done) {
        /* read in next extent */
        struct fiemap *fm = rm_offset_get_fiemap(fd, 1, file_offset);

        if(fm==NULL) {
            /* got no extent data */
#if _RM_OFFSET_DEBUG
            rm_log_info_line(_("rm_offset_get_fiemap: got no fiemap for %d"), fd);
#endif
            break;
        }

        if (fm->fm_mapped_extents == 0) {
#if _RM_OFFSET_DEBUG
            rm_log_info_line(_("rm_offset_get_fiemap: got no extents for %d"), fd);
#endif
            done = TRUE;
        } else {

            /* retrieve data from fiemap */
            struct fiemap_extent fm_ext = fm->fm_extents[0];

            if (first) {
                /* remember disk location of start of data */
                result = fm_ext.fe_physical;
                first=FALSE;
            } else {
                /* check if subsequent extents are contiguous */
                if(fm_ext.fe_physical != expected)  {
                    /* current extent is not contiguous with previous, so we can stop */
                    done = TRUE;
                }
            }

            if (!done && file_offset_next != NULL) {
                /* update logical offset of next fragment */
                *file_offset_next = fm_ext.fe_logical + fm_ext.fe_length;
            }

            if(fm_ext.fe_flags & FIEMAP_EXTENT_LAST) {
                done = TRUE;

                if(is_last != NULL) {
                    *is_last = TRUE;
                }
            }

            if(fm_ext.fe_length <= 0) {
                /* going nowhere; bail out rather than looping indefinitely */
                done = TRUE;
            }

            /* move offsets in preparation for reading next extent */
            file_offset += fm_ext.fe_length;
            expected = fm_ext.fe_physical + fm_ext.fe_length;
        }

        g_free(fm);
    }

    if (file_offset_next != NULL) {
        /* return value of *file_offset_next: */
        *file_offset_next = file_offset;
    }

    return result;
}

RmOff rm_offset_get_from_path(const char *path, RmOff file_offset,
                              RmOff *file_offset_next) {
    int fd = rm_sys_open(path, O_RDONLY);
    if(fd == -1) {
        rm_log_info("Error opening %s in rm_offset_get_from_path\n", path);
        return 0;
    }
    RmOff result = rm_offset_get_from_fd(fd, file_offset, file_offset_next, NULL);
    rm_sys_close(fd);
    return result;
}

#else /* Probably FreeBSD */

RmOff rm_offset_get_from_fd(_UNUSED int fd, _UNUSED RmOff file_offset,
                            _UNUSED RmOff *file_offset_next, _UNUSED bool *is_last) {
    return 0;
}

RmOff rm_offset_get_from_path(_UNUSED const char *path, _UNUSED RmOff file_offset,
                              _UNUSED RmOff *file_offset_next) {
    return 0;
}

#endif

static gboolean rm_util_is_path_double(char *path1, char *path2) {
    char *basename1 = rm_util_basename(path1);
    char *basename2 = rm_util_basename(path2);
    return (strcmp(basename1, basename2) == 0 &&
            rm_util_parent_node(path1) == rm_util_parent_node(path2));
}

/* test if two file paths are on the same device (even if on different
 * mountpoints)
 */
static gboolean rm_util_same_device(const char *path1, const char *path2) {
    const char *best1 = NULL;
    const char *best2 = NULL;
    int len1 = 0;
    int len2 = 0;

    GList *mounts = g_unix_mounts_get(NULL);
    for(GList *iter = mounts; iter; iter = iter->next) {
        GUnixMountEntry *mount = iter->data;
        const char *mountpath = g_unix_mount_get_mount_path(mount);
        int len = strlen(mountpath);
        if(len > len1 && strncmp(mountpath, path1, len) == 0) {
            best1 = g_unix_mount_get_device_path(mount);
            len1 = len;
        }
        if(len > len2 && strncmp(mountpath, path2, len) == 0) {
            best2 = g_unix_mount_get_device_path(mount);
            len2 = len;
        }
    }
    gboolean result = (best1 && best2 && strcmp(best1, best2) == 0);
    g_list_free_full(mounts, (GDestroyNotify)g_unix_mount_free);
    return result;
}

RmLinkType rm_util_link_type(char *path1, char *path2) {
#if _RM_OFFSET_DEBUG
    rm_log_debug_line("Checking link type for %s vs %s", path1, path2);
#endif
    int fd1 = rm_sys_open(path1, O_RDONLY);
    if(fd1 == -1) {
        rm_log_perrorf("rm_util_link_type: Error opening %s", path1);
        return RM_LINK_ERROR;
    }

#define RM_RETURN(value)   \
    {                      \
        rm_sys_close(fd1); \
        return (value);    \
    }

    RmStat stat1;
    int stat_state = rm_sys_lstat(path1, &stat1);
    if(stat_state == -1) {
        rm_log_perrorf("rm_util_link_type: Unable to stat file %s", path1);
        RM_RETURN(RM_LINK_ERROR);
    }

    if(!S_ISREG(stat1.st_mode)) {
        RM_RETURN(RM_LINK_NOT_FILE);
    }

    int fd2 = rm_sys_open(path2, O_RDONLY);
    if(fd2 == -1) {
        rm_log_perrorf("rm_util_link_type: Error opening %s", path2);
        RM_RETURN(RM_LINK_ERROR);
    }

#undef RM_RETURN
#define RM_RETURN(value)   \
    {                      \
        rm_sys_close(fd1); \
        rm_sys_close(fd2); \
        return (value);    \
    }

    RmStat stat2;
    stat_state = rm_sys_lstat(path2, &stat2);
    if(stat_state == -1) {
        rm_log_perrorf("rm_util_link_type: Unable to stat file %s", path2);
        RM_RETURN(RM_LINK_ERROR);
    }

    if(!S_ISREG(stat2.st_mode)) {
        RM_RETURN(RM_LINK_NOT_FILE);
    }

    if(stat1.st_size != stat2.st_size) {
#if _RM_OFFSET_DEBUG
        rm_log_debug_line("rm_util_link_type: Files have different sizes: %" G_GUINT64_FORMAT
                          " <> %" G_GUINT64_FORMAT, stat1.st_size,
                          stat2.st_size);
#endif
        RM_RETURN(RM_LINK_WRONG_SIZE);
    }

    if(stat1.st_dev == stat2.st_dev && stat1.st_ino == stat2.st_ino) {
        /* hardlinks or maybe even same file */
        if(strcmp(path1, path2) == 0) {
            RM_RETURN(RM_LINK_SAME_FILE);
        } else if(rm_util_is_path_double(path1, path2)) {
            RM_RETURN(RM_LINK_PATH_DOUBLE);
        } else {
            RM_RETURN(RM_LINK_HARDLINK);
        }
    }

    if(stat1.st_dev != stat2.st_dev) {
        /* reflinks must be on same filesystem but not necessarily
         * same st_dev (btrfs subvolumes have different st_dev's) */
        if(!rm_util_same_device(path1, path2)) {
            RM_RETURN(RM_LINK_XDEV);
        }
    }

    /* If both are symbolic links we do not follow them */
    if(S_ISLNK(stat1.st_mode) || S_ISLNK(stat2.st_mode)) {
        RM_RETURN(RM_LINK_SYMLINK);
    }

#if HAVE_FIEMAP

    RmOff logical_current = 0;

    bool is_last_1 = false;
    bool is_last_2 = false;
    bool at_least_one_checked = false;

    while(!rm_session_was_aborted()) {
        RmOff logical_next_1 = 0;
        RmOff logical_next_2 = 0;

        RmOff physical_1 = rm_offset_get_from_fd(fd1, logical_current, &logical_next_1, &is_last_1);
        RmOff physical_2 = rm_offset_get_from_fd(fd2, logical_current, &logical_next_2, &is_last_2);

        if(is_last_1 != is_last_2) {
            RM_RETURN(RM_LINK_NONE);
        }

        if(is_last_1 && is_last_2 && at_least_one_checked) {
            RM_RETURN(RM_LINK_REFLINK);
        }

        if(physical_1 != physical_2) {
#if _RM_OFFSET_DEBUG
            rm_log_debug_line("Physical offsets differ at byte %" G_GUINT64_FORMAT
                              ": %"G_GUINT64_FORMAT "<> %" G_GUINT64_FORMAT,
                              logical_current, physical_1, physical_2);
#endif
            RM_RETURN(RM_LINK_NONE);
        }
        if(logical_next_1 != logical_next_2) {
#if _RM_OFFSET_DEBUG
            rm_log_debug_line("File offsets differ after %" G_GUINT64_FORMAT
                              " bytes: %" G_GUINT64_FORMAT "<> %" G_GUINT64_FORMAT,
                              logical_current, logical_next_1, logical_next_2);
#endif
            RM_RETURN(RM_LINK_NONE);
        }

        if(physical_1 == 0) {
#if _RM_OFFSET_DEBUG
            rm_log_debug_line(
                "Can't determine whether files are clones (maybe inline extents?)");
#endif
            RM_RETURN(RM_LINK_MAYBE_REFLINK);
        }

#if _RM_OFFSET_DEBUG
        rm_log_debug_line("Offsets match at fd1=%d, fd2=%d, logical=%" G_GUINT64_FORMAT ", physical=%" G_GUINT64_FORMAT,
                          fd1, fd2, logical_current, physical_1);
#endif
        if(logical_next_1 <= logical_current) {
            /* oops we seem to be getting nowhere (this shouldn't really happen) */
            rm_log_info_line(
                "rm_util_link_type() giving up: file1_offset_next<=file_offset_current for %s vs %s", path1, path2);
            RM_RETURN(RM_LINK_ERROR)
        }

        if(logical_next_1 >= (RmOff)stat1.st_size) {
            /* phew, we got to the end */
#if _RM_OFFSET_DEBUG
            rm_log_debug_line("Files are clones (share same data)")
#endif
            RM_RETURN(RM_LINK_REFLINK)
        }

        logical_current = logical_next_1;
        at_least_one_checked = true;
    }

    RM_RETURN(RM_LINK_ERROR);
#else
    RM_RETURN(RM_LINK_NONE);
#endif

#undef RM_RETURN
}


/////////////////////////////////
//  GTHREADPOOL WRAPPERS       //
/////////////////////////////////

/* wrapper for g_thread_pool_push with error reporting */
bool rm_util_thread_pool_push(GThreadPool *pool, gpointer data) {
    GError *error = NULL;
    g_thread_pool_push(pool, data, &error);
    if(error != NULL) {
        rm_log_error_line("Unable to push thread to pool %p: %s", pool, error->message);
        g_error_free(error);
        return false;
    } else {
        return true;
    }
}

/* wrapper for g_thread_pool_new with error reporting */
GThreadPool *rm_util_thread_pool_new(GFunc func, gpointer data, int threads) {
    GError *error = NULL;
    GThreadPool *pool = g_thread_pool_new(func, data, threads, FALSE, &error);

    if(error != NULL) {
        rm_log_error_line("Unable to create thread pool.");
        g_error_free(error);
    }
    return pool;
}

//////////////////////////////
//    TIMESTAMP HELPERS     //
//////////////////////////////

gdouble rm_iso8601_parse(const char *string) {
#if GLIB_CHECK_VERSION(2,56,0)
    GDateTime *time_result = g_date_time_new_from_iso8601(string, NULL);
    if(time_result == NULL) {
        rm_log_perror("Converting time failed");
        return 0;
    }


    gdouble result = g_date_time_to_unix(time_result);
    result += g_date_time_get_microsecond(time_result) / (gdouble)(G_USEC_PER_SEC);

    g_date_time_unref(time_result);
    return result;
#else
    /* Remove this branch in a few years (written end of 2019) */

    GTimeVal time_result;
    if(!g_time_val_from_iso8601(string, &time_result)) {
        rm_log_perror("Converting time failed");
        return 0;
    }

    return time_result.tv_sec + time_result.tv_usec / (gdouble)(G_USEC_PER_SEC);
#endif
}

bool rm_iso8601_format(time_t stamp, char *buf, gsize buf_size) {
    struct tm now_ctime;
    if(localtime_r(&stamp, &now_ctime) != NULL) {
        return (strftime(buf, buf_size, "%FT%T%z", &now_ctime) != 0);
    }

    return false;
}

#define SECONDS_PER_DAY (24 * 60 * 60)
#define SECONDS_PER_HOUR (60 * 60)
#define SECONDS_PER_MINUTE (60)

char *rm_format_elapsed_time(gfloat elapsed_sec, int sec_precision) {
    GString *buf = g_string_new(NULL);

    if(elapsed_sec > SECONDS_PER_DAY) {
        gint days = elapsed_sec / SECONDS_PER_DAY;
        elapsed_sec -= days * SECONDS_PER_DAY;
        g_string_append_printf(buf, "%2dd ", days);
    }

    if(elapsed_sec > SECONDS_PER_HOUR) {
        gint hours = elapsed_sec / SECONDS_PER_HOUR;
        elapsed_sec -= hours * SECONDS_PER_HOUR;
        g_string_append_printf(buf, "%2dh ", hours);
    }

    if(elapsed_sec > SECONDS_PER_MINUTE) {
        gint minutes = elapsed_sec / SECONDS_PER_MINUTE;
        elapsed_sec -= minutes * SECONDS_PER_MINUTE;
        g_string_append_printf(buf, "%2dm ", minutes);
    }

    g_string_append_printf(buf, "%2.*fs", sec_precision, elapsed_sec);
    return g_string_free(buf, FALSE);
}

void rm_running_mean_init(RmRunningMean *m, int max_values) {
    m->sum = 0;
    m->values = g_malloc0(max_values * sizeof(gdouble));
    m->max_values = max_values;
    m->cursor = 0;
}

void rm_running_mean_add(RmRunningMean *m, gdouble value) {
    int pos = (++m->cursor) % m->max_values;
    m->sum += value;
    m->sum -= m->values[pos];
    m->values[pos] = value;
}

gdouble rm_running_mean_get(RmRunningMean *m) {
    int n = MIN(m->max_values, m->cursor);
    if(n == 0) {
        return 0.0;
    }

    return m->sum / n;
}

void rm_running_mean_unref(RmRunningMean *m) {
    if(m->values) {
        g_free(m->values);
        m->values = NULL;
    }
}
