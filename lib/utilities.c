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
 *  - Christopher <sahib> Pahl 2010-2015 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2015 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <pwd.h>
#include <grp.h>

#include <libgen.h>

#include "config.h"

/* Not available there,
 * but might be on other non-linux systems
 * */
#if HAVE_GIO_UNIX
#include <gio/gunixmounts.h>
#endif

#if HAVE_FIEMAP
#include <linux/fs.h>
#include <linux/fiemap.h>
#endif

/* Internal headers */
#include "config.h"
#include "utilities.h"
#include "file.h"

/* External libraries */
#include <glib.h>

#if HAVE_LIBELF
#include <libelf.h>
#include <gelf.h>
#endif

#if HAVE_BLKID
#include <blkid.h>
#endif

#if HAVE_SYSCTL
#include <sys/sysctl.h>
#endif

#if HAVE_JSON_GLIB
#include <json-glib/json-glib.h>
#endif

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

    while(path) {
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

gint rm_util_queue_foreach_remove(GQueue *queue, RmQRFunc func, gpointer user_data) {
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

/* checks uid and gid; returns 0 if both ok, else RM_LINT_TYPE_ corresponding *
 * to RmFile->filter types                                            */
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
bool rm_util_is_nonstripped(_U const char *path, _U RmStat *statp) {
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

static int rm_userlist_cmp_ids(gconstpointer a, gconstpointer b, _U gpointer ud) {
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
    rm_assert_gentle(self);
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
    rm_assert_gentle(self);

    g_sequence_free(self->users);
    g_sequence_free(self->groups);
    g_mutex_clear(&self->lock);
    g_free(self);
}

/////////////////////////////////////
//    JSON CACHE IMPLEMENTATION    //
/////////////////////////////////////

#if HAVE_JSON_GLIB

void rm_json_cache_parse_entry(_U JsonArray *array, _U guint index,
                               JsonNode *element_node, RmTrie *file_trie) {
    if(JSON_NODE_TYPE(element_node) != JSON_NODE_OBJECT) {
        return;
    }

    JsonObject *object = json_node_get_object(element_node);
    JsonNode *mtime_node = json_object_get_member(object, "mtime");
    JsonNode *path_node = json_object_get_member(object, "path");
    JsonNode *cksum_node = json_object_get_member(object, "checksum");
    JsonNode *type_node = json_object_get_member(object, "type");

    if(mtime_node && path_node && cksum_node && type_node) {
        RmStat stat_buf;
        const char *path = json_node_get_string(path_node);
        const char *cksum = json_node_get_string(cksum_node);
        const char *type = json_node_get_string(type_node);

        if(g_strcmp0(type, "duplicate_file") && g_strcmp0(type, "unfinished_cksum")) {
            /* some other file that has a checksum for weird reasons.
             * This is here to prevent errors like reporting files with
             * empty checksums as duplicates.
             * */
            return;
        }

        if(rm_sys_stat(path, &stat_buf) == -1) {
            /* file does not appear to exist */
            return;
        }

        if(json_node_get_int(mtime_node) < rm_sys_stat_mtime_seconds(&stat_buf)) {
            /* file is newer than stored checksum */
            return;
        }

        char *cksum_copy = g_strdup(cksum);
        if(!rm_trie_set_value(file_trie, path, cksum_copy)) {
            g_free(cksum_copy);
        }
        rm_log_debug_line("* Adding cache entry %s (%s)", path, cksum);
    }
}

#endif

int rm_json_cache_read(RmTrie *file_trie, const char *json_path) {
#if !HAVE_JSON_GLIB
    (void)file_trie;
    (void)json_path;

    rm_log_info_line(_("caching is not supported due to missing json-glib library."));
    return EXIT_FAILURE;
#else
    rm_assert_gentle(file_trie);
    rm_assert_gentle(json_path);

    int result = EXIT_FAILURE;
    GError *error = NULL;
    size_t keys_in_table = rm_trie_size(file_trie);
    JsonParser *parser = json_parser_new();

    rm_log_info_line(_("Loading json-cache `%s'"), json_path);

    if(!json_parser_load_from_file(parser, json_path, &error)) {
        rm_log_warning_line(_("FAILED: %s\n"), error->message);
        g_error_free(error);
        goto failure;
    }

    JsonNode *root = json_parser_get_root(parser);
    if(JSON_NODE_TYPE(root) != JSON_NODE_ARRAY) {
        rm_log_warning_line(_("No valid json cache (no array in /)"));
        goto failure;
    }

    /* Iterate over all objects in it */
    json_array_foreach_element(json_node_get_array(root),
                               (JsonArrayForeach)rm_json_cache_parse_entry,
                               file_trie);

    /* check if some entries were added */
    result = (keys_in_table >= rm_trie_size(file_trie));

failure:
    if(parser) {
        g_object_unref(parser);
    }
    return result;
#endif
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

#if(HAVE_GIO_UNIX)

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
    char sys_path[PATH_MAX];

    snprintf(sys_path, PATH_MAX, "/sys/block/%s/queue/rotational", dev);

    FILE *sys_fdes = fopen(sys_path, "r");
    if(sys_fdes == NULL) {
        return -1;
    }

    if(fread(&is_rotational, 1, 1, sys_fdes) == 1) {
        is_rotational -= '0';
    }

    fclose(sys_fdes);
#elif HAVE_SYSCTL && !RM_IS_APPLE
    /* try with sysctl() */
    int device_num = 0;
    char cmd[32] = {0}, delete_method[32] = {0}, dev_copy[32] = {0};
    size_t delete_method_len = sizeof(delete_method_len);
    (void)dev;

    memset(cmd, 0, sizeof(cmd));
    memset(delete_method, 0, sizeof(delete_method));
    strncpy(dev_copy, dev, sizeof(dev_copy));

    for(int i = 0; dev_copy[i]; ++i) {
        if(isdigit(dev_copy[i])) {
            if(i > 0) {
                dev_copy[i - 1] = 0;
            }

            device_num = g_ascii_strtoll(&dev_copy[i], NULL, 10);
            break;
        }
    }

    if(snprintf(cmd, sizeof(cmd), "kern.cam.%s.%d.delete_method", dev_copy, device_num) ==
       -1) {
        return -1;
    }

    if(sysctlbyname(cmd, delete_method, &delete_method_len, NULL, 0) != 0) {
        rm_log_perror("sysctlbyname");
    } else {
        if(memcmp("NONE", delete_method, MIN(delete_method_len, 4)) == 0) {
            is_rotational = 1;
        } else {
            is_rotational = 0;
        }
    }

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
    rm_assert_gentle(self);

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
    rm_assert_gentle(self);

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

        /* btrfs and ocfs2 filesystems support reflinks for deduplication */
        static const char *reflinkfs_types[] = {"btrfs", "ocfs2", NULL};

        const struct RmEvilFs *evilfs_found = NULL;
        for(int i = 0; evilfs_types[i].name && !evilfs_found; ++i) {
            if(strcmp(evilfs_types[i].name, wrap_entry->type) == 0) {
                evilfs_found = &evilfs_types[i];
            }
        }

        const char *reflinkfs_found = NULL;
        for(int i = 0; reflinkfs_types[i] && !reflinkfs_found; ++i) {
            if(strcmp(reflinkfs_types[i], wrap_entry->type) == 0) {
                reflinkfs_found = reflinkfs_types[i];
                break;
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

        rm_log_debug_line("Filesystem %s: %s", wrap_entry->dir,
                          (reflinkfs_found) ? "reflink" : "normal");

        if(reflinkfs_found != NULL) {
            RmStat dir_stat;
            rm_sys_stat(wrap_entry->dir, &dir_stat);
            g_hash_table_insert(table->reflinkfs_table,
                                GUINT_TO_POINTER(dir_stat.st_dev),
                                (gpointer)reflinkfs_found);
        }
    }

    return self;
}

#if HAVE_SYSCTL && !RM_IS_APPLE

static GHashTable *DISK_TABLE = NULL;

static void rm_mounts_freebsd_list_disks(void) {
    char disks[1024];
    size_t disks_len = sizeof(disks);
    memset(disks, 0, sizeof(disks));

    DISK_TABLE = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if(sysctlbyname("kern.disks", disks, &disks_len, NULL, 0) == 0) {
        char **disk_vec = g_strsplit(disks, " ", -1);
        for(int i = 0; disk_vec[i]; ++i) {
            char *disk = g_strdup_printf("/dev/%s", disk_vec[i]);
            RmStat dev_stat;

            if(rm_sys_stat(disk, &dev_stat) != -1) {
                g_hash_table_insert(DISK_TABLE, disk, GUINT_TO_POINTER(dev_stat.st_rdev));
            } else {
                rm_log_perror("stat on /dev");
                g_free(disk);
            }
        }

        g_strfreev(disk_vec);
    } else {
        rm_log_perror("sysctlbyname");
    }
}
#endif

int rm_mounts_devno_to_wholedisk(_U RmMountEntry *entry, _U dev_t rdev, _U char *disk,
                                 _U size_t disk_size, _U dev_t *result) {
#if HAVE_BLKID
    return blkid_devno_to_wholedisk(rdev, disk, disk_size, result);
#elif HAVE_SYSCTL && !RM_IS_APPLE
    if(DISK_TABLE == NULL) {
        rm_mounts_freebsd_list_disks();
    }

    GHashTableIter iter;
    g_hash_table_iter_init(&iter, DISK_TABLE);

    gpointer key = NULL;
    gpointer value = NULL;

    while(g_hash_table_iter_next(&iter, &key, &value)) {
        char *str_key = key;
        if(g_str_has_prefix(str_key, entry->fsname)) {
            strncpy(disk, strrchr(str_key, '/'), disk_size);
            *result = (dev_t)GPOINTER_TO_UINT(value);
            return 0;
        }
    }

    return -1;
#else
    return -1;
#endif
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
                strncpy(diskname, entry->fsname, sizeof(diskname));
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
                strncpy(diskname, entry->fsname, sizeof(diskname));
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

#if HAVE_SYSCTL && !RM_IS_APPLE
    if(DISK_TABLE) {
        g_hash_table_unref(DISK_TABLE);
    }
#endif

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

RmMountTable *rm_mounts_table_new(_U bool force_fiemap) {
    return NULL;
}

void rm_mounts_table_destroy(_U RmMountTable *self) {
    /* NO-OP */
}

#endif

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

dev_t rm_mounts_get_disk_id(RmMountTable *self, dev_t partition, const char *path) {
    if(self == NULL) {
        return 0;
    }

    RmPartitionInfo *part =
        g_hash_table_lookup(self->part_table, GINT_TO_POINTER(partition));
    if(part) {
        return part->disk;
    } else {
        /* probably a btrfs subvolume which is not a mountpoint; walk up tree until we get
         * to *
         * a recognisable partition */
        char *prev = g_strdup(path);
        while(TRUE) {
            char *temp = g_strdup(prev);
            char *parent_path = g_strdup(dirname(temp));
            g_free(temp);

            RmStat stat_buf;
            if(!rm_sys_stat(parent_path, &stat_buf)) {
                RmPartitionInfo *parent_part = g_hash_table_lookup(
                    self->part_table, GINT_TO_POINTER(stat_buf.st_dev));
                if(parent_part) {
                    /* create new partition table entry */
                    rm_log_debug_line("Adding partition info for " GREEN "%s" RESET
                                      " - looks like subvolume %s on disk " GREEN
                                      "%s" RESET,
                                      path, prev, parent_part->name);
                    part = rm_part_info_new(prev, parent_part->fsname, parent_part->disk);
                    g_hash_table_insert(self->part_table, GINT_TO_POINTER(partition),
                                        part);
                    if(g_hash_table_contains(self->reflinkfs_table,
                                             GUINT_TO_POINTER(stat_buf.st_dev))) {
                        g_hash_table_insert(self->reflinkfs_table,
                                            GUINT_TO_POINTER(partition),
                                            GUINT_TO_POINTER(1));
                    }
                    g_free(prev);
                    g_free(parent_path);
                    return parent_part->disk;
                }
            }
            g_free(prev);
            prev = parent_path;
            rm_assert_gentle(strcmp(prev, "/") != 0);
            rm_assert_gentle(strcmp(prev, ".") != 0);
        }
    }
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
    rm_assert_gentle(self);
    if(g_hash_table_contains(self->reflinkfs_table, GUINT_TO_POINTER(source))) {
        if(source == dest) {
            return true;
        } else {
            RmPartitionInfo *source_part =
                g_hash_table_lookup(self->part_table, GINT_TO_POINTER(source));
            RmPartitionInfo *dest_part =
                g_hash_table_lookup(self->part_table, GINT_TO_POINTER(dest));
            rm_assert_gentle(source_part);
            rm_assert_gentle(dest_part);
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

/* Return fiemap structure containing n_extents for file descriptor fd.
 * Return NULL if errors encountered.
 * Needs to be freed with g_free if not NULL.
 * */
static struct fiemap *rm_offset_get_fiemap(int fd, const int n_extents,
                                           const int file_offset) {
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
RmOff rm_offset_get_from_fd(int fd, RmOff file_offset, RmOff *file_offset_next) {
    RmOff result = 0;
    bool done = FALSE;

    /* used for detecting contiguous extents */
    unsigned long expected = 0;

    while(!done) {
        /* read in one extent */
        struct fiemap *fm = rm_offset_get_fiemap(fd, 1, file_offset);
        if(!fm) {
            done = TRUE;
        } else {
            if(!file_offset_next) {
                /* no need to find end of fragment so one loop is enough*/
                done = TRUE;
            }
            if(fm->fm_mapped_extents > 0) {
                /* retrieve data from fiemap */
                struct fiemap_extent *fm_ext = fm->fm_extents;
                file_offset += fm_ext[0].fe_length;
                if(result == 0) {
                    /* this is the first extent */
                    result = fm_ext[0].fe_physical;
                    if(result == 0) {
                        /* looks suspicious - let's get out of here */
                        done = TRUE;
                    }
                } else if(fm_ext[0].fe_physical > expected ||
                          fm_ext[0].fe_physical < result) {
                    /* current extent is not contiguous with previous, so we can stop*/
                    done = TRUE;
                    if(file_offset_next) {
                        /* caller wants to know logical offset of next fragment */
                        *file_offset_next = fm_ext[0].fe_logical;
                    }
                }
                if(fm_ext[0].fe_flags & FIEMAP_EXTENT_LAST) {
                    if(!done) {
                        done = TRUE;
                        if(file_offset_next) {
                            /* caller wants to know logical offset of next fragment -
                             * signal
                             * that it is EOF */
                            *file_offset_next =
                                fm_ext[0].fe_logical + fm_ext[0].fe_length;
                        }
                    }
                }
                if(!done) {
                    expected = fm_ext[0].fe_physical + fm_ext[0].fe_length;
                }
            } else {
                /* got no extents from rm_offset_get_fiemap */
                done = true;
                if(file_offset_next) {
                    /* caller wants to know logical offset of next fragment but
                     * we have an error... */
                    *file_offset_next = 0;
                }
            }
            g_free(fm);
        }
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
    RmOff result = rm_offset_get_from_fd(fd, file_offset, file_offset_next);
    rm_sys_close(fd);
    return result;
}

bool rm_offsets_match(char *path1, char *path2) {
    bool result = FALSE;
    int fd1 = rm_sys_open(path1, O_RDONLY);
    if(fd1 == -1) {
        rm_log_info_line("Error opening %s in rm_offsets_match", path1);
        return FALSE;
    }

    int fd2 = rm_sys_open(path2, O_RDONLY);
    if(fd2 == -1) {
        rm_log_info_line("Error opening %s in rm_offsets_match", path2);
        rm_sys_close(fd1);
        return FALSE;
    }

    RmOff file1_offset_next = 0;
    RmOff file2_offset_next = 0;
    RmOff file_offset_current = 0;
    while(!result &&
          (rm_offset_get_from_fd(fd1, file_offset_current, &file1_offset_next) ==
           rm_offset_get_from_fd(fd2, file_offset_current, &file2_offset_next)) &&
          file1_offset_next != 0 && file1_offset_next == file2_offset_next) {
        if(file1_offset_next == file_offset_current) {
            /* phew, we got to the end */
            result = TRUE;
            break;
        }
        file_offset_current = file1_offset_next;
    }

    rm_sys_close(fd2);
    rm_sys_close(fd1);
    return result;
}

#else /* Probably FreeBSD */

RmOff rm_offset_get_from_fd(_U int fd, _U RmOff file_offset, _U RmOff *file_offset_next) {
    return 0;
}

RmOff rm_offset_get_from_path(_U const char *path, _U RmOff file_offset,
                              _U RmOff *file_offset_next) {
    return 0;
}

bool rm_offsets_match(char *path1, char *path2) {
    return (path1 == path2);
}

#endif

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

time_t rm_iso8601_parse(const char *string) {
    GTimeVal time_result;
    if(!g_time_val_from_iso8601(string, &time_result)) {
        rm_log_perror("Converting time failed");
        return 0;
    }

    return time_result.tv_sec;
}

bool rm_iso8601_format(time_t stamp, char *buf, gsize buf_size) {
    struct tm now_ctime;
    if(localtime_r(&stamp, &now_ctime) != NULL) {
        return (strftime(buf, buf_size, "%FT%T%z", &now_ctime) != 0);
    }

    return false;
}
