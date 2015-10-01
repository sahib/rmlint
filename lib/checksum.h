/*
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

#ifndef RM_CHECKSUM_H
#define RM_CHECKSUM_H

#include <glib.h>
#include <stdbool.h>
#include "config.h"

typedef enum RmDigestType {
    RM_DIGEST_UNKNOWN = 0,
    RM_DIGEST_MURMUR,
    RM_DIGEST_SPOOKY,
    RM_DIGEST_SPOOKY32,
    RM_DIGEST_SPOOKY64,
    RM_DIGEST_CITY,
    RM_DIGEST_MD5,
    RM_DIGEST_SHA1,
    RM_DIGEST_SHA256,
    RM_DIGEST_SHA512,
    RM_DIGEST_MURMUR256,
    RM_DIGEST_CITY256,
    RM_DIGEST_BASTARD,
    RM_DIGEST_MURMUR512,
    RM_DIGEST_CITY512,
    RM_DIGEST_XXHASH,

    /* special kids in town */
    RM_DIGEST_CUMULATIVE, /* hash([a, b]) = hash([b, a]) */
    RM_DIGEST_EXT,        /* external hash functions     */
    RM_DIGEST_PARANOID    /* direct block comparisons    */
} RmDigestType;

typedef struct RmUint128 {
    guint64 first;
    guint64 second;
} RmUint128;

typedef struct RmParanoid {
    /* the buffers containing file data byte-by-byte */
    GSList *buffers;
    /* pointer to the last buffer (for efficient appends) */
    GSList *buffer_tail;

    /* A SPOOKY hash is built for every paranoid digest.
     * So we can make rm_digest_hash() and rm_digest_hexstring() work.
     */
    struct RmDigest *shadow_hash;

    /* Optional: if known, a potentially matching *completed* RMDigest
     * can be provided and will be progressively compared against
     * this RmDigest *during* rm_digest_buffered_update(); this speeds
     * up subsequent calls to rm_digest_equal() significantly.
     */
    struct RmDigest *twin_candidate;
    /* Pointer to current buffer in twin_candidate->paranoid->buffers */
    GSList *twin_candidate_buffer;

    /* Optional: incoming queue for additional twin candidate RmDigest's */
    GAsyncQueue *incoming_twin_candidates;
} RmParanoid;

typedef struct RmDigest {
    /* Different storage structures are used depending on digest type: */
    union {
        GChecksum *glib_checksum;
        RmUint128 *checksum;
        RmParanoid *paranoid;
    };

    /* digest type */
    RmDigestType type;

    /* digest size in bytes */
    gsize bytes;
} RmDigest;

/////////// RmBufferPool and RmBuffer ////////////////

typedef struct RmBufferPool {
    /* Place where recycled buffers are stored */
    GSList *stack;

    /* size of each buffer */
    gsize buffer_size;

    /* how many new buffers can we allocate before hitting mem limit? */
    gsize avail_buffers;
    gsize max_buffers;
    gsize min_buffers;
    gsize kept_buffers;
    gsize max_kept_buffers;
    gboolean mem_warned;

    /* concurrent accesses may happen */
    GMutex lock;
    GCond change;

} RmBufferPool;

/* Represents one block of read data */
typedef struct RmBuffer {
    /* note that first (sizeof(pointer)) bytes of this structure get overwritten when it
     * gets
     * pushed to the RmBufferPool stack, so first couple of elements can't be reused */

    /* checksum the data belongs to */
    struct RmDigest *digest;

    /* len of the data actualy filled */
    guint32 len;

    /* user utility data field */
    gpointer user_data;

    /* the pool the buffer belongs to */
    RmBufferPool *pool;

    /* pointer to the data block */
    unsigned char *data;
} RmBuffer;

/**
 * @brief Convert a string like "md5" to a RmDigestType member.
 *
 * @param string one of "md5", "sha1", "sha256", "sha512", "spooky", "murmur"
 *
 * @return RM_DIGEST_UNKNOWN on error, the type otherwise.
 */
RmDigestType rm_string_to_digest_type(const char *string);

/* Hash type to MultiHash ID.
 * Idea of Multihash to encode hash algorithm and size into
 * the first two bytes of the hash.
 *
 * There is no standard yet, I used this one and randomly assigned
 * the other numbers:
 *
 *   https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-18
 */
int rm_digest_type_to_multihash_id(RmDigestType type);

/**
 * @brief Convert a RmDigestType to a human readable string.
 *
 * @param type the type to convert.
 *
 * @return a statically allocated string.
 */
const char *rm_digest_type_to_string(RmDigestType type);

/**
 * @brief Allocate and initialise a RmDigest.
 *
 * @param type Which algorithm to use for hashing.
 * @param seed Initial seed. Pass 0 if not interested.
 * @param paranoid_size. Digest size in bytes for "paranoid" (exact copy) digest
 * @param use_shadow_hash.  Keep a shadow hash for lookup purposes.
 */
RmDigest *rm_digest_new(RmDigestType type, RmOff seed1, RmOff seed2, RmOff ext_size, bool use_shadow_hash);

/**
 * @brief Deallocate memory assocated with a RmDigest.
 */
void rm_digest_free(RmDigest *digest);

/**
 * @brief Hash a datablock and add it to the current checksum.
 *
 * @param digest a pointer to a RmDigest
 * @param data a block of data.
 * @param size the size of data
 */
void rm_digest_update(RmDigest *digest, const unsigned char *data, RmOff size);

/**
 * @brief Hash a datablock and add it to the current checksum.
 *
 * @param digest a pointer to a RmDigest
 * @param buffer a RmBuffer of data.
 */
void rm_digest_buffered_update(RmBuffer *buffer);

/**
 * @brief Convert the checksum to a hexstring (like `md5sum`)
 *
 * rm_digest_update is not allowed to be called after finalizing.
 *
 * If the type is RM_DIGEST_PARANOID, this will return the hexstring
 * of the shadow digest built in the background.
 *
 * @param digest a pointer to a RmDigest
 * @param input The input buffer to convert
 * @param buflen Size of the buffer.
 * @param buffer The buffer to write the hexadecimal checksum to.
 *
 * @return how many bytes were written. (for md5sum: 32)
 */
int rm_digest_hexstring(RmDigest *digest, char *buffer);

/**
 * @brief steal digest result into allocated memory slice.
 *
 * @param digest a pointer to a RmDigest
 *
 * Steal the internal buffer of the digest. For RM_DIGEST_PARANOID,
 * this will be the actual file data.
 *
 * @return pointer to result (note: result length will = digest->bytes)
 */
guint8 *rm_digest_steal(RmDigest *digest);

/**
 * @brief Hash a Digest, suitable for GHashTable.
 *
 * For RM_DIGEST_PARANOID this is basically the same
 * as rm_digest_hash(digest->shadow_hash);
 */
guint rm_digest_hash(RmDigest *digest);

/**
 * @brief Compare two RmDigest checksums (without modifying them).
 *
 * @param a a pointer to a RmDigest
 * @param b a pointer to another RmDigest.
 *
 * The checksums are compared byte for byte, even
 * for RM_DIGEST_PARANOID.
 *
 * @return true if digests match
 */
gboolean rm_digest_equal(RmDigest *a, RmDigest *b);

/**
 * @brief Make a copy of a RMDigest.
 *
 * @param digest a pointer to a RmDigest
 *
 * @return copy of digest
 */
RmDigest *rm_digest_copy(RmDigest *digest);

/**
 * For RM_DIGEST_PARANOID this is  the number of bytes in the
 * shadow hash.
 *
 * @return The number of bytes used for storing the checksum.
 */
int rm_digest_get_bytes(RmDigest *self);

/**
 * Shrink the paranoid checksum buffer to new_size.
 *
 * This is mainly useful for using an adjusted buffer for symlinks.
 */
void rm_digest_paranoia_shrink(RmDigest *digest, gsize new_size);

/**
 * Release any kept (paranoid) buffers.
 */
void rm_digest_release_buffers(RmDigest *digest);

RmOff rm_buffer_size(RmBufferPool *pool);
RmBufferPool *rm_buffer_pool_init(gsize buffer_size, gsize max_mem, gsize max_kept_mem);
void rm_buffer_pool_destroy(RmBufferPool *pool);
RmBuffer *rm_buffer_pool_get(RmBufferPool *pool);
void rm_buffer_pool_release(RmBuffer *buf);

void rm_digest_send_match_candidate(RmDigest *target, RmDigest *candidate);

#endif /* end of include guard */
