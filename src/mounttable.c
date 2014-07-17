#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

static GOnce ONCE_PROC_MOUNTS = G_ONCE_INIT;
static GOnce ONCE_RESULT_TABLE = G_ONCE_INIT;

static gchar rm_mount_is_rotational_blockdev(const char *dev) {
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

static GHashTable *rm_mounts_parse_proc_mounts(G_GNUC_UNUSED gpointer unused) {
    FILE *proc_fd = fopen("/proc/partitions", "r");
    GHashTable *part_table = g_hash_table_new_full(
                                 g_direct_hash, g_direct_equal,
                                 g_free, NULL
                             );

    if(proc_fd == NULL) {
        return part_table;
    }

    unsigned major, minor, dummy;
    major = minor = dummy = 0;
    char dev_name[200];
    char line[PATH_MAX];

    while(fgets(line, PATH_MAX, proc_fd)) {
        memset(dev_name, 0, sizeof(dev_name));
        sscanf(
            line, "%d%d%d%200s",
            &major, &minor, &dummy, dev_name
        );

        if(*dev_name) {
            g_hash_table_insert(
                part_table,
                g_strdup(dev_name),
                GINT_TO_POINTER(makedev(major, minor))
            );
        }
    }

    fclose(proc_fd);
    return part_table;
}

static void rm_mount_find_partitions(const char *blockdev_name, GHashTable *mount_table, gboolean non_rotational) {
    GHashTableIter iter;
    gpointer key, value;
    gsize blockdev_name_len = strlen(blockdev_name);

    g_once(&ONCE_PROC_MOUNTS, (GThreadFunc)rm_mounts_parse_proc_mounts, NULL);
    GHashTable *partition_table = ONCE_PROC_MOUNTS.retval;

    g_hash_table_iter_init(&iter, partition_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if(strncmp(blockdev_name, (char *)key, blockdev_name_len) == 0) {
            dev_t part_devid = GPOINTER_TO_INT(value);

            printf(
                "%02u:%02u: %5s is a %srotational device\n",
                major(part_devid), minor(part_devid),
                (char *)key, (non_rotational) ? "non-" : ""
            );
            g_hash_table_insert(
                mount_table,
                GINT_TO_POINTER(part_devid),
                GINT_TO_POINTER(non_rotational)
            );
        }
    }
}

static GHashTable *rm_mounts_create_table(void) {
    GError *error = NULL;
    GDir *dir_handle = g_dir_open("/sys/block", 0, &error);
    GHashTable *mount_table = g_hash_table_new(g_direct_hash, g_direct_equal);

    if (dir_handle == NULL) {
        printf("Cannot read /sys/block/: %s\n", error->message);
        g_error_free(error);
        return mount_table;
    }

    const char *dir_entry = NULL;
    while ((dir_entry = g_dir_read_name(dir_handle)) != 0) {
        gchar non_rotational = !rm_mount_is_rotational_blockdev(dir_entry);

        if (non_rotational == -1) {
            printf("Cannot determine if '%s' is rotational. Assuming not.\n", dir_entry);
        } else {
            rm_mount_find_partitions(dir_entry, mount_table, non_rotational);
        }
    }

    g_dir_close(dir_handle);
    return mount_table;
}

/////////////////////////////////
//         PUBLIC API          //
/////////////////////////////////

gboolean rm_mounts_file_is_on_sdd(dev_t device) {
    g_once(&ONCE_RESULT_TABLE, (GThreadFunc)rm_mounts_create_table, NULL);

    GHashTable *mount_table = ONCE_RESULT_TABLE.retval;
    return GPOINTER_TO_INT(
               g_hash_table_lookup(
                   mount_table, GINT_TO_POINTER(device)
               )
           );
}

void rm_mounts_clear(void) {
    GHashTable *table = NULL;

    table = ONCE_RESULT_TABLE.retval;
    if(table) {
        g_hash_table_unref(table);
    }

    table = ONCE_PROC_MOUNTS.retval;
    if(table) {
        g_hash_table_unref(table);
    }
}

#ifdef _RM_COMPILE_MAIN

int main(int argc, char **argv) {
    for(int i = 1; i < argc; ++i) {
        struct stat stat_buf;
        stat(argv[i], &stat_buf);

        g_printerr("%s is on %srotational device.\n",
                   argv[i],
                   rm_mounts_file_is_on_sdd(stat_buf.st_dev) ? "non-" : ""
                  );
    }

    rm_mounts_clear();
    return 0;
}

#endif
