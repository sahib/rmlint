/**
* This file is part of rmlint.
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
*  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
*  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
*
* Hosted on http://github.com/sahib/rmlint
*
*/

#ifndef RM_UTILITIES_H_INCLUDE
#define RM_UTILITIES_H_INCLUDE

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

/* Pat(h)tricia Trie implementation */
#include "pathtricia.h"
#include "logger.h"

/* return values for rm_util_link_type */
typedef enum RmLinkType {
    RM_LINK_REFLINK         = EXIT_SUCCESS,
    RM_LINK_ERROR           = EXIT_FAILURE,
    RM_LINK_NOT_FILE        = 3,
    RM_LINK_WRONG_SIZE      = 4,
    RM_LINK_INLINE_EXTENTS  = 5,
    RM_LINK_SAME_FILE       = 6,
    RM_LINK_PATH_DOUBLE     = 7,
    RM_LINK_HARDLINK        = 8,
    RM_LINK_SYMLINK         = 9,
    RM_LINK_XDEV            = 10,
    RM_LINK_NONE            = 11,
} RmLinkType;

#if HAVE_STAT64 && !RM_IS_APPLE
typedef struct stat64 RmStat;
#else
typedef struct stat RmStat;
#endif

////////////////////////
//  MATHS SHORTCUTS   //
////////////////////////

// Signum function
#define SIGN(X) ((X) > 0 ? 1 : ((X) < 0 ? -1 : 0))

// Returns 1 if X>Y, -1 if X<Y or 0 if X==Y
#define SIGN_DIFF(X, Y) (((X) > (Y)) - ((X) < (Y))) /* handy for comparing uint64's */

// Compare two floats; tolerate +/- tol when testing for equality
// See also:
// https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition
#define FLOAT_SIGN_DIFF(X, Y, tol) ((X) - (Y) > (tol) ? 1 : ((Y) - (X) > (tol) ? -1 : 0))

// Time tolerance (seconds) when comparing two mtimes
#define MTIME_TOL (0.000001)

#define RETURN_IF_NONZERO(X) \
    if((X) != 0) {           \
        return (X);          \
    }

////////////////////////////////////
//       SYSCALL WRAPPERS         //
////////////////////////////////////

WARN_UNUSED_RESULT static inline int rm_sys_stat(const char *path, RmStat *buf) {
#if HAVE_STAT64 && !RM_IS_APPLE
    return stat64(path, buf);
#else
    return stat(path, buf);
#endif
}

WARN_UNUSED_RESULT static inline int rm_sys_lstat(const char *path, RmStat *buf) {
#if HAVE_STAT64 && !RM_IS_APPLE
    return lstat64(path, buf);
#else
    return lstat(path, buf);
#endif
}

static inline int rm_sys_open(const char *path, int mode) {
#if HAVE_STAT64
#ifdef O_LARGEFILE
    mode |= O_LARGEFILE;
#endif
#endif

    return open(path, mode, (S_IRUSR | S_IWUSR));
}

static inline gdouble rm_sys_stat_mtime_float(RmStat *stat) {
#if RM_IS_APPLE
    return (gdouble)stat->st_mtimespec.tv_sec + stat->st_mtimespec.tv_nsec / 1000000000.0;
#else
    return (gdouble)stat->st_mtim.tv_sec + stat->st_mtim.tv_nsec / 1000000000.0;
#endif
}

void rm_sys_close(int fd);

gint64 rm_sys_preadv(int fd, const struct iovec *iov, int iovcnt, RmOff offset);

/////////////////////////////////////
//   UID/GID VALIDITY CHECKING     //
/////////////////////////////////////

typedef struct RmUserList {
    GSequence *users;
    GSequence *groups;
    GMutex lock;
} RmUserList;

/**
 * @brief Create a new list of users.
 */
RmUserList *rm_userlist_new(void);

/**
 * @brief Check if a uid and gid is contained in the list.
 *
 * @param valid_uid (out)
 * @param valid_gid (out)
 *
 * @return true if both are valid.
 */
bool rm_userlist_contains(RmUserList *list, unsigned long uid, unsigned gid,
                          bool *valid_uid, bool *valid_gid);

/**
 * @brief Deallocate the memory allocated by rm_userlist_new()
 */
void rm_userlist_destroy(RmUserList *list);

/**
 * @brief Get the name of the user running rmlint.
 */
char *rm_util_get_username(void);

/**
 * @brief Get the group of the user running rmlint.
 */
char *rm_util_get_groupname(void);

////////////////////////////////////
//       GENERAL UTILITIES         //
////////////////////////////////////

#define RM_LIST_NEXT(node) ((node) ? node->next : NULL)

/**
 * @brief parse a string eg '64k' into a guint64
 */
guint64 rm_util_size_string_to_bytes(const char *size_spec, GError **error);

/**
 * @brief Replace {subs} with {with} in {string}
 *
 * @return a newly allocated string, g_free it.
 */
char *rm_util_strsub(const char *string, const char *subs, const char *with);

/**
 * @brief Check if a file has an invalid gid/uid or both.
 *
 * @return the appropriate RmLintType for the file
 */
int rm_util_uid_gid_check(RmStat *statp, RmUserList *userlist);

/**
 * @brief Check if a file is a binary that is not stripped.
 *
 * @path: Path to the file to be checked.
 * @statp: valid stat pointer with st_mode filled (allow-none).
 *
 * @return: if it is a binary with debug symbols.
 */
bool rm_util_is_nonstripped(const char *path, RmStat *statp);

/**
 * @brief Get the basename part of the file. It does not change filename.
 *
 * @return NULL on failure, the pointer after the last / on success.
 */
char *rm_util_basename(const char *filename);

/**
 * @brief Check if the file or any components of it are hidden.
 *
 * @return true if it is.
 */
bool rm_util_path_is_hidden(const char *path);

/**
 * @brief Get the depth of a path
 *
 * @param path
 *
 * @return depth of path or 0.
 */
int rm_util_path_depth(const char *path);

typedef gpointer (*RmNewFunc)(void);

/**
 * @brief A setdefault supplementary function for GHashTable.
 *
 * This is about the same as dict.setdefault in python.
 *
 * @param table the table to use
 * @param key key to lookup
 * @param default_func if the key does not exist in table, return default_func
 * and insert it into table
 *
 * @return value, which may be default_func() if key does not exist.
 */
GQueue *rm_hash_table_setdefault(GHashTable *table, gpointer key, RmNewFunc default_func);

/**
 * @brief Push all elements in `src` at the tail of `dst`
 *
 * @param dest The queue to append to.
 * @param src The queue to append from. Will be empty afterwards.
 */
void rm_util_queue_push_tail_queue(GQueue *dest, GQueue *src);

/**
 * @brief Function prototype for remove-iterating over a GQueue/GList/GSList.
 *
 * @param data current element
 * @param user_data optional user_data
 *
 * @return True if the element should be removed.
 */
typedef gint (*RmRFunc)(gpointer data, gpointer user_data);

/**
 * @brief Iterate over a GQueue and call `func` on each element.
 *
 * If func returns true, the element is removed from the queue.
 *
 * @param queue GQueue to iterate
 * @param func Function that evaluates the removal of the item
 * @param user_data optional user data
 *
 * @return Number of removed items.
 */
gint rm_util_queue_foreach_remove(GQueue *queue, RmRFunc func, gpointer user_data);

/**
 * @brief Iterate over a GList and call `func` on each element.
 *
 * If func returns true, the element is removed from the GList.
 *
 * @param list pointer to GList to iterate
 * @param func Function that evaluates the removal of the item
 * @param user_data optional user data
 *
 * @return Number of removed items.
 */
gint rm_util_list_foreach_remove(GList **list, RmRFunc func, gpointer user_data);

/**
 * @brief Iterate over a GSList and call `func` on each element.
 *
 * If func returns true, the element is removed from the GSList.
 *
 * @param list pointer to GSList to iterate
 * @param func Function that evaluates the removal of the item
 * @param user_data optional user data
 *
 * @return Number of removed items.
 */
gint rm_util_slist_foreach_remove(GSList **list, RmRFunc func, gpointer user_data);

/**
 * @brief Pop the first element from a GSList
 *
 * @return pointer to the data associated with the popped element.
 *
 * Note this function returns null if the list is empty, or if the first item
 * has NULL as its data.
 */
gpointer rm_util_slist_pop(GSList **list, GMutex *lock);

/**
 * @brief Return a pointer to the extension part of the file or NULL if none.
 *
 * @return: a pointer >= basename or NULL.
 */
char *rm_util_path_extension(const char *basename);

/**
 * @brief Get the inode of the directory of the file specified in path.
 */
ino_t rm_util_parent_node(const char *path);

/*
 * @brief Takes num and converts into some human readable string. 1024 -> 1KB
 */
void rm_util_size_to_human_readable(RmOff num, char *in, gsize len);

/////////////////////////////////////
//    MOUNTTABLE IMPLEMENTATION    //
/////////////////////////////////////

typedef struct RmMountTable {
    GHashTable *part_table;
    GHashTable *disk_table;
    GHashTable *nfs_table;
    GHashTable *evilfs_table;
    GHashTable *reflinkfs_table;
} RmMountTable;

/**
 * @brief Allocates a new mounttable.
 * @param force_fiemap Create random fiemap data always. Useful for testing.
 *
 * @return The mounttable. Free with rm_mounts_table_destroy.
 */
RmMountTable *rm_mounts_table_new(bool force_fiemap);

/**
 * @brief Destroy a previously allocated mounttable.
 *
 * @param self the table to destroy.
 */
void rm_mounts_table_destroy(RmMountTable *self);

/**
 * @brief Check if the device is or is part of a nonrotational device.
 *
 * This operation has constant time.
 *
 * @param self the table to lookup from.
 * @param device the dev_t of a file, e.g. looked up from rm_sys_stat(2)
 *
 * @return true if it is non a nonrational device.
 */
bool rm_mounts_is_nonrotational(RmMountTable *self, dev_t device);

/**
 * @brief Get the disk behind the partition.
 *
 * @param self the table to lookup from.
 * @param partition the dev_t of a partition (sda1 -> 8:1), e.g. looked up from
 *rm_sys_stat(2)
 *
 * @return the dev_t of the whole disk. (sda 8:0)
 */
dev_t rm_mounts_get_disk_id(RmMountTable *self, dev_t dev, const char *path);

/**
 * @brief Same as above, but calls rm_sys_stat(2) on path for you.
 */
dev_t rm_mounts_get_disk_id_by_path(RmMountTable *self, const char *path);

/**
 * @brief Indicates true if dev_t points to a filesystem that might confuse
 * rmlint.
 */
bool rm_mounts_is_evil(RmMountTable *self, dev_t to_check);

/**
 * @brief Indicates true if source and dest are on same partition, and the
 * partition supports reflink copies (cp --reflink).
 */
bool rm_mounts_can_reflink(RmMountTable *self, dev_t source, dev_t dest);

/////////////////////////////////
//    FIEMAP IMPLEMENTATION     //
/////////////////////////////////

/**
 * @brief Lookup the physical offset of a file fd at any given offset.
 *
 * @return the physical offset starting from the disk.
 */
RmOff rm_offset_get_from_fd(int fd, RmOff file_offset, RmOff *file_offset_next,
                            bool *is_last, bool *is_inline);

/**
 * @brief Lookup the physical offset of a file path at any given offset.
 *
 * @return the physical offset starting from the disk.
 */
RmOff rm_offset_get_from_path(const char *path, RmOff file_offset,
                              RmOff *file_offset_next);

/**
 * @brief Test if two files have identical fiemaps.
 * @retval see RmLinkType enum definition.
 */
RmLinkType rm_util_link_type(const char *path1, const char *path2, bool use_fiemap);

/**
 * @brief Map RmLinkType to description.
 * @retval Array of descriptions.
 */
const char **rm_link_type_to_desc(void);

//////////////////////////////
//    TIMESTAMP HELPERS     //
//////////////////////////////

/**
 * @brief Parse an ISO8601 timestamp to a unix timestamp.
 */
gdouble rm_iso8601_parse(const char *string);

/**
 * @brief convert a unix timestamp as iso8601 timestamp string.
 *
 * @param stamp unix timestamp
 * @param buf result buffer to hold the string.
 * @param buf_size sizeof buf.
 *
 * @return true if conversion succeeded.
 */
bool rm_iso8601_format(time_t stamp, char *buf, gsize buf_size);

///////////////////////////////
//    THREADPOOL HELPERS     //
///////////////////////////////

/**
 * @brief Create a new GThreadPool with default cfg.
 *
 * @param func func to execute
 * @param data user_data to pass
 * @param threads how many threads at max to use.
 *
 * @return newly allocated GThreadPool
 */
GThreadPool *rm_util_thread_pool_new(GFunc func, gpointer data, int threads);

/**
 * @brief Push a new job to a threadpool.
 *
 * @return true on success.
 */
bool rm_util_thread_pool_push(GThreadPool *pool, gpointer data);

/**
 * @brief Format some elapsed seconds into a human readable timestamp.
 *
 * @return The formatted string, free with g_free.
 */
char *rm_format_elapsed_time(gfloat elapsed_sec, int sec_precision);

typedef struct {
    gdouble sum;
    gdouble *values;

    int max_values;
    int cursor;
} RmRunningMean;

/**
 * @brief Initialize a running mean window.
 *
 * The window has a fixed length. rm_running_mean_get() can be used
 * to efficiently calculate the mean of this window. When new values
 * are added, the oldest values will be removed.
 */
void rm_running_mean_init(RmRunningMean *m, int max_values);

/**
 * @brief Add a new value to the mean window.
 */
void rm_running_mean_add(RmRunningMean *m, gdouble value);

/**
 * @brief Get the current mean.
 *
 * @return The current mean (0.0 if no values available)
 */
gdouble rm_running_mean_get(RmRunningMean *m);

/**
 * @brief Release internal mem used to store values.
 */
void rm_running_mean_unref(RmRunningMean *m);

/**
 * @brief See GLib docs for g_canonicalize_filename().
 */
gchar *rm_canonicalize_filename(const gchar *filename, const gchar *relative_to);

#endif /* RM_UTILITIES_H_INCLUDE*/
