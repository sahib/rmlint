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
**/

#include "xattr.h"
#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/types.h>

#if HAVE_XATTR
#include <sys/xattr.h>
#endif

#ifndef ENODATA
#define ENODATA ENOMSG
#endif

////////////////////////////
//    UTILITY FUNCTIONS   //
////////////////////////////

#if HAVE_XATTR

/* Compat wrappers for MacOSX and other platforms.
 */

#if RM_IS_APPLE

ssize_t rm_sys_getxattr(const char *path, const char *name, void *value, size_t size, bool follow_link) {
    int flags = 0;
    if(!follow_link) {
        flags |= XATTR_NOFOLLOW;
    }

    return getxattr(path, name, value, size, 0, flags);
}

ssize_t rm_sys_setxattr(
    const char *path, const char *name, const void *value, size_t size, int flags, bool follow_link) {
    if(!follow_link) {
        flags |= XATTR_NOFOLLOW;
    }

    return setxattr(path, name, value, size, 0, flags);
}

int rm_sys_removexattr(const char *path, const char *name, bool follow_link) {
    int flags = 0;
    if(!follow_link) {
        flags |= XATTR_NOFOLLOW;
    }

    return removexattr(path, name, flags);
}

int rm_sys_listxattr(const char *path, char *out, size_t out_size, bool follow_link) {
    int flags = 0;
    if(!follow_link) {
        flags |= XATTR_NOFOLLOW;
    }

    return listxattr(path, out, out_size, flags);
}

#else

ssize_t rm_sys_getxattr(const char *path, const char *name, void *value, size_t size, bool follow_link) {
    if(!follow_link) {
    #if HAVE_LXATTR
        return lgetxattr(path, name, value, size);
    #endif
    }

    return getxattr(path, name, value, size);
}

ssize_t rm_sys_setxattr(
    const char *path, const char *name, const void *value, size_t size, int flags, bool follow_link) {
    if(!follow_link) {
    #if HAVE_LXATTR
        return lsetxattr(path, name, value, size, flags);
    #endif
    }

    return setxattr(path, name, value, size, flags);
}

int rm_sys_removexattr(const char *path, const char *name, bool follow_link) {
    if(!follow_link) {
    #if HAVE_LXATTR
        return lremovexattr(path, name);
    #endif
    }

    return removexattr(path, name);
}

int rm_sys_listxattr(const char *path, char *out, size_t out_size, bool follow_link) {
    if(!follow_link) {
    #if HAVE_LXATTR
        return llistxattr(path, out, out_size);
    #endif
    }

    return listxattr(path, out, out_size);
}

#endif

static int rm_xattr_build_key(RmSession *session,
                              const char *suffix,
                              char *buf,
                              size_t buf_size) {
    g_assert(session);

    /* Be safe, assume caller is not concentrated. */
    g_assert(buf);
    memset(buf, 0, sizeof(buf_size));

    const char *digest_name = rm_digest_type_to_string(session->cfg->checksum_type);
    if(session->cfg->checksum_type == RM_DIGEST_PARANOID) {
        digest_name = rm_digest_type_to_string(RM_DEFAULT_DIGEST);
    }

    g_assert(suffix);
    return snprintf(buf, buf_size, "user.rmlint.%s.%s", digest_name, suffix) < 0;
}

static int rm_xattr_build_cksum(RmFile *file, char *buf, size_t buf_size) {
    g_assert(file);
    g_assert(file->digest);

    g_assert(buf);
    memset(buf, '0', buf_size);
    buf[buf_size - 1] = 0;

    return rm_digest_hexstring(file->digest, buf);
}

static int rm_xattr_is_fail(const char *name, char *path, int rc) {
    if(rc != -1) {
        return 0;
    }

    if(errno != ENOTSUP && errno != ENODATA) {
        rm_log_warning_line("failed to %s for %s: %s", name, path, g_strerror(errno));
        return errno;
    }

    return 0;
}

static int rm_xattr_set(RmFile *file,
                        const char *key,
                        const char *value,
                        size_t value_size,
                        bool follow_link) {
    RM_DEFINE_PATH(file);
    return rm_xattr_is_fail("setxattr", file_path,
                            rm_sys_setxattr(file_path, key, value, value_size, 0, follow_link));
}

static int rm_xattr_get(RmFile *file,
                        const char *key,
                        char *out_value,
                        size_t value_size,
                        bool follow_link) {
    RM_DEFINE_PATH(file);

    return rm_xattr_is_fail("getxattr", file_path,
                            rm_sys_getxattr(file_path, key, out_value, value_size, follow_link));
}

static int rm_xattr_del(RmFile *file, const char *key, bool follow_link) {
    RM_DEFINE_PATH(file);
    return rm_xattr_is_fail(
            "removexattr", file_path,
            rm_sys_removexattr(file_path, key, follow_link));
}

#endif

////////////////////////////
//  ACTUAL API FUNCTIONS  //
////////////////////////////

int rm_xattr_write_hash(RmFile *file, RmSession *session) {
    g_assert(file);
    g_assert(file->digest);
    g_assert(session);

#if HAVE_XATTR
    if(file->ext_cksum || session->cfg->write_cksum_to_xattr == false) {
        return EINVAL;
    }

    char cksum_key[64], mtime_key[64],
        cksum_hex_str[rm_digest_get_bytes(file->digest) * 2 + 1], timestamp[64] = {0};

    bool follow = session->cfg->follow_symlinks;
    g_ascii_dtostr(timestamp, sizeof(timestamp), file->mtime);

    if(rm_xattr_build_key(session, "cksum", cksum_key, sizeof(cksum_key)) ||
       rm_xattr_build_key(session, "mtime", mtime_key, sizeof(mtime_key)) ||
       rm_xattr_build_cksum(file, cksum_hex_str, sizeof(cksum_hex_str)) <= 0 ||
       rm_xattr_set(file, cksum_key, cksum_hex_str, sizeof(cksum_hex_str), follow) ||
       rm_xattr_set(file, mtime_key, timestamp, strlen(timestamp), follow)) {
        return errno;
    }
#endif
    return 0;
}

gboolean rm_xattr_read_hash(RmFile *file, RmSession *session) {
    g_assert(file);
    g_assert(session);

#if HAVE_XATTR
    if(session->cfg->read_cksum_from_xattr == false) {
        return FALSE;
    }

    char cksum_key[64] = {0},
         mtime_key[64] = {0},
         mtime_buf[64] = {0},
         cksum_hex_str[512] = {0};

    memset(cksum_hex_str, 0, sizeof(cksum_hex_str));
    cksum_hex_str[sizeof(cksum_hex_str) - 1] = 0;

    bool follow = session->cfg->follow_symlinks;
    if(rm_xattr_build_key(session, "cksum", cksum_key, sizeof(cksum_key)) ||
       rm_xattr_get(file, cksum_key, cksum_hex_str, sizeof(cksum_hex_str) - 1, follow) ||
       rm_xattr_build_key(session, "mtime", mtime_key, sizeof(mtime_key)) ||
       rm_xattr_get(file, mtime_key, mtime_buf, sizeof(mtime_buf) - 1, follow)) {
        return FALSE;
    }

    if(cksum_hex_str[0] == 0 || mtime_buf[0] == 0) {
        return FALSE;
    }

    gdouble xattr_mtime = g_strtod(mtime_buf, NULL);
    if(FLOAT_SIGN_DIFF(xattr_mtime, file->mtime, MTIME_TOL) != 0) {
        /* Data is too old and not useful, autoclean it */
        RM_DEFINE_PATH(file);
        rm_log_debug_line(
            "mtime differs too much for %s, %f (xattr) != %f (actual) (diff: %f)\n",
            file_path,
            xattr_mtime,
            file->mtime,
            file->mtime-xattr_mtime
        );
        rm_xattr_clear_hash(file, session);
        return FALSE;
    }

    file->ext_cksum = g_strdup(cksum_hex_str);
    return TRUE;
#else
    return FALSE;
#endif
}

int rm_xattr_clear_hash(RmFile *file, RmSession *session) {
    g_assert(file);
    g_assert(session);

#if HAVE_XATTR
    int error = 0;
    const char *keys[] = {"cksum", "mtime", NULL};

    for(int i = 0; keys[i]; ++i) {
        char key[64] = {0};

        if(rm_xattr_build_key(session, keys[i], key, sizeof(key))) {
            error = EINVAL;
            continue;
        }

        if(rm_xattr_del(file, key, session->cfg->follow_symlinks)) {
            error = errno;
        }
    }

    return error;
#else
    return EXIT_FAILURE;
#endif
}

#if HAVE_XATTR

GHashTable *rm_xattr_list(const char *path, bool follow_symlinks) {
    const size_t buf_size = 4096;
    const size_t val_size = 1024;
    const char prefix[13] = "user.rmlint.";

    char buf[buf_size];
    memset(buf, 0, buf_size);

    int rc = rm_sys_listxattr(path, buf, buf_size-1, follow_symlinks);
    if(rc < 0) {
        rm_xattr_is_fail("listxattr", (char *)path, rc);
        return NULL;
    }

    GHashTable *map = g_hash_table_new_full(
            g_str_hash, g_str_equal,
            g_free, g_free
    );

    bool failed = false;
    char *curr = buf;
    while(true) {
        size_t n = buf_size - (curr - buf);
        if(n <= 0) {
            break;
        }

        char *next = memchr(curr, 0, n);
        if(next == NULL) {
            break;
        }

        size_t key_len = next-curr;
        if(key_len <= 0) {
            break;
        }

        if(strncmp(curr, prefix, MIN(key_len, sizeof(prefix) -1)) != 0) {
            // Skip this key and save some memory. Not one of ours.
            curr = next + 1;
            continue;
        }

        char *key = g_strndup(curr, key_len);
        char *val = g_malloc0(val_size);

        rc = rm_sys_getxattr(path, key, val, val_size, follow_symlinks);
        if(rc < 0) {
            rm_xattr_is_fail("getxattr", (char *)path, rc);
            g_free(key);
            g_free(val);
            failed = true;
            break;
        }

        g_hash_table_insert(map, key, val);
        curr = next + 1;
    }

    if(failed) {
        g_hash_table_destroy(map);
        map = NULL;
    }

    return map;
}

static void rm_xattr_change_subkey(char *key, char *sub_key) {
    size_t key_len = strlen(key);
    size_t sub_key_len = strlen(sub_key);

    // This method is only thought to be used for our xattr keys.
    // They happen to have all 5 byte long sub keys by chance.
    // So take the chance to save some allocations.
    if(sub_key_len != 5) {
        return;
    }

    strcpy(&key[key_len - sub_key_len], sub_key);
}

bool rm_xattr_is_deduplicated(const char *path, bool follow_symlinks) {
    g_assert(path);

    RmStat stat_buf;
    if(rm_sys_stat(path, &stat_buf) < 0) {
        rm_log_warning_line("failed to check dedupe state of %s: %s", path, g_strerror(errno));
        return EXIT_FAILURE;
    }

    bool result = false;
    char *key = NULL, *value = NULL;
    GHashTable *map = rm_xattr_list(path, follow_symlinks);
    GHashTableIter iter;

    g_hash_table_iter_init(&iter, map);
    while(g_hash_table_iter_next(&iter, (gpointer)&key, (gpointer)&value)) {
        if(!g_str_has_suffix(key, ".mtime")) {
            continue;
        }

        gdouble mtime = g_ascii_strtod(value, NULL);
        if(FLOAT_SIGN_DIFF(mtime, stat_buf.st_mtime, MTIME_TOL) != 0) {
            continue;
        }

        rm_xattr_change_subkey(key, "cksum");
        char *cksum = g_hash_table_lookup(map, key);
        if(cksum == NULL) {
            continue;
        }

        rm_xattr_change_subkey(key, "dedup");
        char *dedup = g_hash_table_lookup(map, key);
        if(dedup == NULL) {
            continue;
        }

        if(g_strcmp0(cksum, dedup) != 0) {
            continue;
        }

        result = true;
        break;
    }

    g_hash_table_destroy(map);
    return result;
}

int rm_xattr_mark_deduplicated(const char *path, bool follow_symlinks) {
    g_assert(path);

    RmStat stat_buf;
    if(rm_sys_stat(path, &stat_buf) < 0) {
        rm_log_warning_line("failed to mark dedupe state of %s: %s", path, g_strerror(errno));
        return EXIT_FAILURE;
    }

    int result = EXIT_FAILURE;
    char *key = NULL, *value = NULL;
    GHashTable *map = rm_xattr_list(path, follow_symlinks);
    GHashTableIter iter;

    g_hash_table_iter_init(&iter, map);
    while(g_hash_table_iter_next(&iter, (gpointer)&key, (gpointer)&value)) {
        if(!g_str_has_suffix(key, ".mtime")) {
            continue;
        }

        gdouble mtime = g_ascii_strtod(value, NULL);
        if(FLOAT_SIGN_DIFF(mtime, stat_buf.st_mtime, MTIME_TOL) != 0) {
            continue;
        }

        rm_xattr_change_subkey(key, "cksum");
        char *cksum = g_hash_table_lookup(map, key);
        if(cksum == NULL) {
            continue;
        }

        rm_xattr_change_subkey(key, "dedup");
        result = rm_sys_setxattr(path, key, cksum, strlen(cksum), 0, follow_symlinks);
    }

    g_hash_table_destroy(map);
    return result;
}

#endif
