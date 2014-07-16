#include <glib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <string.h>

#include "config.h"

#if HAVE_LIBMOUNT
    #include <libmount.h>
#endif 

#define PATH_MAX 4095 // todo


static gboolean rm_mounts_check_if_non_rotational(const char *device_path) {
    // TODO: Follow link if lvm device or other pseudo device.
    // TODO: Proper parsing, currently "...[0-9]" is assumed.
    //       dm-0 would fail, sda1 would work.
    //
    //
    char copy[PATH_MAX];
    if(readlink(device_path, copy, PATH_MAX) == -1) {
        strncpy(copy, device_path, PATH_MAX);
    } 

    char *base = strrchr(copy, G_DIR_SEPARATOR);
    unsigned char is_rotational = 1;

    if(base == NULL) {
        base = copy;
    } else {
        base++;
    }

    if(strstr(device_path, "mapper") == NULL) {
        for(int i = 0; i < 3 && *base++; ++i);

        *base = '\0';
        base -= 3;
    }

    char *sys_path = g_strdup_printf("/sys/block/%s/queue/rotational", base);
    FILE *sys_fdes = fopen(sys_path, "r");
    if(sys_fdes != NULL) {
        int bytes_read = fread(&is_rotational, 1, 1, sys_fdes);
        if(bytes_read != 1) {
            printf("Huh? Read %d bytes\n", bytes_read);
        } else {
            is_rotational -= '0';
        }
        fclose(sys_fdes);
    }

    g_free(sys_path);
    return !is_rotational;
}


static GHashTable * rm_mounts_create_table(void) {
    GHashTable *mount_table = g_hash_table_new(g_direct_hash, g_direct_equal);

#if HAVE_LIBMOUNT
    struct libmnt_table *table =  mnt_new_table_from_file("/etc/fstab");
    struct libmnt_fs *fs = NULL;
    struct libmnt_iter * iter = mnt_new_iter(MNT_ITER_FORWARD);

    mnt_table_parse_mtab(table, "/etc/mtab");
    mnt_table_parse_mtab(table, "/proc/mounts");
    mnt_table_parse_mtab(table, "/proc/self/mountinfo");
    mnt_table_parse_mtab(table, "/run/mount/utabs");

    while(mnt_table_next_fs(table, iter, &fs) == 0) {
        const char *dir = mnt_fs_get_target(fs);
        const char *src = mnt_fs_get_source(fs);
        
        if(src && *src == '/') {
            /* check if we can stat it */
            struct stat stat_buf;
            stat(src, &stat_buf);

            if(S_ISBLK(stat_buf.st_mode)) {
                gboolean non_rotational = rm_mounts_check_if_non_rotational(src);
                printf(
                    "%lu: %s (%s) is a %srotational device\n",
                    stat_buf.st_rdev, dir, src, non_rotational ? "non-" : ""
                );

                g_hash_table_insert(
                    mount_table,
                    GINT_TO_POINTER(stat_buf.st_rdev),
                    GINT_TO_POINTER(non_rotational)
                );
            }
        }

    }
    mnt_free_iter(iter);
    mnt_free_table(table);
#endif
    return mount_table;
}

#ifdef _RM_COMPILE_MAIN

int main(void) {
    GHashTable *mount_table = rm_mounts_create_table();
    g_hash_table_unref(mount_table);

    return 0;
}

#endif
