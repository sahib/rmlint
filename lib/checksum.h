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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#ifndef RM_CHECKSUM_H
#define RM_CHECKSUM_H

#include <glib.h>
#include <stdbool.h>
#include "config.h"

#include "checksums/blake2/blake2.h"
#include "checksums/sha3/sha3.h"
#include "checksums/highwayhash.h"

typedef enum RmDigestType {
    RM_DIGEST_UNKNOWN = 0,
    RM_DIGEST_MURMUR,
    RM_DIGEST_METRO,
    RM_DIGEST_METRO256,
#if HAVE_MM_CRC32_U64
    RM_DIGEST_METROCRC,
    RM_DIGEST_METROCRC256,
#endif
    RM_DIGEST_MD5,
    RM_DIGEST_SHA1,
    RM_DIGEST_SHA256,
#if HAVE_SHA512
    RM_DIGEST_SHA512,
#endif
    RM_DIGEST_SHA3_256,
    RM_DIGEST_SHA3_384,
    RM_DIGEST_SHA3_512,
    RM_DIGEST_BLAKE2S,
    RM_DIGEST_BLAKE2B,
    RM_DIGEST_BLAKE2SP /*  Parallel version of BLAKE2P */,
    RM_DIGEST_BLAKE2BP /*  Parallel version of BLAKE2S */,
    RM_DIGEST_XXHASH,
    RM_DIGEST_HIGHWAY64,
    RM_DIGEST_HIGHWAY128,
    RM_DIGEST_HIGHWAY256,
    /* special kids in town */
    RM_DIGEST_CUMULATIVE, /* hash([a, b]) = hash([b, a]) */
    RM_DIGEST_EXT,        /* read hash as string         */
    RM_DIGEST_PARANOID,   /* direct block comparisons    */
    /* sentinel */
    RM_DIGEST_SENTINEL,
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

    /* A hash is built for every paranoid digest.
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
    GSList *rejects;

    /* Optional: incoming queue for additional twin candidate RmDigest's */
    GAsyncQueue *incoming_twin_candidates;
} RmParanoid;

typedef struct RmDigest {
    /* Different storage structures are used depending on digest type: */
    gpointer state;

    /* digest type */
    RmDigestType type;

    /* digest output size in bytes */
    gsize bytes;

} RmDigest;

typedef struct RmSemaphore {
    volatile int n;
    GMutex sem_lock;
    GCond sem_cond;
} RmSemaphore;

/**
 * @brief Allocate a new RmSemaphore.
 *
 * @param n: The number of acquire operations before the semaphore blocks.
 */
RmSemaphore *rm_semaphore_new(int n);

/**
 * @brief Allocate a new RmSemaphore.
 *
 * @param n: The number of acquire operations before the semaphore blocks.
 */
void rm_semaphore_destroy(RmSemaphore *sem);

/**
 * @brief Acquire a share of the semaphore.
 *
 * This call will block if already too may acquires happened.
 */
void rm_semaphore_acquire(RmSemaphore *sem);

/**
 * @brief Release one share of the semaphore.
 */
void rm_semaphore_release(RmSemaphore *sem);

/////////// RmBuffer ////////////////

/* Represents one block of read data */
typedef struct RmBuffer {
    /* note that first (sizeof(pointer)) bytes of this structure get overwritten
     * when it gets pushed to the RmBufferPool stack, so first couple of
     * elements can't be reused */

    /* checksum the data belongs to */
    struct RmDigest *digest;

    /* len of data */
    guint32 buf_size;

    /* len of the data actually filled */
    guint32 len;

    /* user utility data field */
    gpointer user_data;

    /* pointer to the data block */
    unsigned char *data;
} RmBuffer;

RmBuffer *rm_buffer_new(RmSemaphore *sem, gsize buf_size);

void rm_buffer_free(RmSemaphore *sem, RmBuffer *buf);

/**
 * @brief Convert a string like "md5" to a RmDigestType member.
 *
 * @param string A valid digest type.
 *
 * @return RM_DIGEST_UNKNOWN on error, the type otherwise.
 */
RmDigestType rm_string_to_digest_type(const char *string);

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
 */
RmDigest *rm_digest_new(RmDigestType type, RmOff seed);

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
void rm_digest_buffered_update(RmSemaphore *sem, RmBuffer *buffer);

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
 * @brief Hash `length` bytes of `data` with the algorithm `algo`.
 *
 * @return a newly allocated checksum with the length of *out_len.
 *           Free with g_slice_free1
 **/
guint8 *rm_digest_sum(RmDigestType algo, const guint8 *data, gsize len, gsize *out_len);

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
 * Release any kept (paranoid) buffers.
 */
void rm_digest_release_buffers(RmDigest *digest);

/**
 * @brief Send a new (pending) paranoid digest match `candidate` for `target`.
 */
void rm_digest_send_match_candidate(RmDigest *target, RmDigest *candidate);

/**
 * @brief Enable or disable SSE optimisations.
 * @note will also check __builtin_cpu_supports("sse4.2") before enabling
 */
void rm_digest_enable_sse(gboolean use_sse);

#endif /* end of include guard */
