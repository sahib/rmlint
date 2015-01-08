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
** Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#include "config.h"
#include "xattr.h"

#include <sys/types.h>
#include <string.h>
#include <errno.h>

#if HAVE_XATTR
#  include <sys/xattr.h>
#endif

////////////////////////////
//    UTILITY FUNCTIONS   //
////////////////////////////

static int rm_xattr_build_key(RmSession *session, const char *suffix, char *buf, size_t buf_size) {
    g_assert(session);

    /* Be safe, assume caller is not concentrated. */
    memset(buf, 0, sizeof(buf_size));

    const char *digest_name = rm_digest_type_to_string(session->settings->checksum_type);
    if(session->settings->checksum_type == RM_DIGEST_PARANOID) {
        digest_name = rm_digest_type_to_string(RMLINT_DEFAULT_DIGEST);
    }

    return snprintf(buf, buf_size, "user.rmlint.%s.%s", digest_name, suffix) < 0;
}

static int rm_xattr_build_cksum(RmFile *file, char *buf, size_t buf_size) {
    g_assert(file);
    g_assert(file->digest);

    memset(buf, '0', buf_size);
    buf[buf_size - 1] = 0;

    if(file->digest->type == RM_DIGEST_PARANOID) {
        g_assert(file->digest->shadow_hash);
        return rm_digest_hexstring(file->digest->shadow_hash, buf);
    } else {
        return rm_digest_hexstring(file->digest, buf);
    }
}

static int rm_xattr_is_fail(const char *name, int rc) {
    if(rc != -1) {
        return 0;
    }

    if(errno != ENOTSUP && errno != ENODATA) {
        rm_log_perror(name);
        return errno;
    }

    return 0;
}

static int rm_xattr_set(RmFile *file, const char *key, const char *value, size_t value_size) {
#if HAVE_XATTR
    return rm_xattr_is_fail("setxattr", setxattr(file->path, key, value, value_size, 0));
#else 
    return 0;
#endif
}

static int rm_xattr_get(RmFile *file, const char *key, char *out_value, size_t value_size) {
#if HAVE_XATTR
    return rm_xattr_is_fail("getxattr", getxattr(file->path, key, out_value, value_size));
#else 
    return 0;
#endif
}

static int rm_xattr_del(RmFile *file, const char *key) {
#if HAVE_XATTR
    return rm_xattr_is_fail("rmovexattr", removexattr(file->path, key));
#else 
    return 0;
#endif
}

////////////////////////////
//  ACTUAL API FUNCTIONS  //
////////////////////////////

int rm_xattr_write_hash(RmSession *session, RmFile *file) {
    g_assert(file);
    g_assert(file->digest);
    g_assert(session);

#if HAVE_XATTR
    if(file->has_ext_cksum || session->settings->write_cksum_to_xattr == false) {
        return EINVAL;
    }

    char cksum_key[64],
         mtime_key[64],
         cksum_hex_str[rm_digest_get_bytes(file->digest) * 2 + 1],
         timestamp[64] = {0};

    int timestamp_bytes = 0;
    double actual_time_sec = difftime(file->mtime, 0);

    if(0
        || rm_xattr_build_key(session, "cksum", cksum_key, sizeof(cksum_key))
        || rm_xattr_build_key(session, "mtime", mtime_key, sizeof(mtime_key))
        || rm_xattr_build_cksum(file, cksum_hex_str, sizeof(cksum_hex_str)) <= 0
        || rm_xattr_set(file, cksum_key, cksum_hex_str, sizeof(cksum_hex_str))
        || (timestamp_bytes = snprintf(timestamp, sizeof(timestamp), "%lld", (long long)actual_time_sec)) == -1
        || rm_xattr_set(file, mtime_key, timestamp, timestamp_bytes)
    ) {
        return errno;
    }
#endif
    return 0;
}

char *rm_xattr_read_hash(RmSession *session, RmFile *file) {
    g_assert(file);
    g_assert(session);

#if HAVE_XATTR
    if(session->settings->read_cksum_from_xattr == false) {
        return NULL;
    }

    char cksum_key[64] = {0},
         mtime_key[64] = {0},
         mtime_buf[64] = {0},
         cksum_hex_str[512] = {0};

    memset(cksum_hex_str, '0', sizeof(cksum_hex_str));
    cksum_hex_str[sizeof(cksum_hex_str) - 1] = 0;

    if(0
        || rm_xattr_build_key(session, "cksum", cksum_key, sizeof(cksum_key))
        || rm_xattr_get(file, cksum_key, cksum_hex_str, sizeof(cksum_hex_str) - 1)
        || rm_xattr_build_key(session, "mtime", mtime_key, sizeof(mtime_key))
        || rm_xattr_get(file, mtime_key, mtime_buf, sizeof(mtime_buf) - 1)) {
        return NULL;
    }

    if(g_ascii_strtoll(mtime_buf, NULL, 10) < file->mtime) {
        /* Data is too old and not useful, autoclean it */
        rm_xattr_clear_hash(session, file);
        return NULL;
    }

    /* remember, this file is special. A unicorn amongst files. */
    file->has_ext_cksum = true;

    return g_strdup(cksum_hex_str);
#else
    return NULL;
#endif
}

int rm_xattr_clear_hash(RmSession *session, RmFile *file) {
    g_assert(file);
    g_assert(session);

    int error = 0;
    const char *keys[] = {"cksum", "mtime", NULL};

    for(int i = 0; keys[i]; ++i) {
        char key[64] = {0};

        if(rm_xattr_build_key(session, keys[i], key, sizeof(key))) {
            error = EINVAL;
            continue;
        }

        if(rm_xattr_del(file, key)) {
            error = errno;
        }
    }

    return error;
}
