#ifndef RM_CHECKSUM_H
#define RM_CHECKSUM_H

#include <glib.h>
#include "config.h"

typedef enum RmDigestType {
    RM_DIGEST_UNKNOWN = 0,
    RM_DIGEST_MURMUR,
    RM_DIGEST_SPOOKY,
    RM_DIGEST_CITY,
    RM_DIGEST_MD5,
    RM_DIGEST_SHA1,
    RM_DIGEST_SHA256,
    RM_DIGEST_SHA512,
    RM_DIGEST_MURMUR256,
    RM_DIGEST_CITY256,
    RM_DIGEST_MURMUR512,
    RM_DIGEST_CITY512
} RmDigestType;


#include "checksums/city.h"
#include "checksums/citycrc.h"
#include "checksums/murmur3.h"
#include "checksums/spooky-c.h"

typedef struct RmDigest {
    union {
        GChecksum *glib_checksum;
        struct spooky_state spooky_state;
        uint128 hash[_RM_HASH_LEN / 16];
    };
    RmDigestType type;
    guint8 num_128bit_blocks;
} RmDigest;

/**
 * @brief Convert a string like "md5" to a RmDigestType member.
 *
 * @param string one of "md5", "sha1", "sha256", "sha512", "spooky", "murmur"
 *
 * @return RM_DIGEST_UNKNOWN on error, the type otherwise.
 */
RmDigestType rm_string_to_digest_type(const char *string);

/**
 * @brief Init a RmDigest.
 *
 * @param digest A pointer to a RmDigest.
 * @param type Which algorithm to use for hashing.
 * @param seed Initial seed. Pass 0 if not interested.
 */
void rm_digest_init(RmDigest *digest, RmDigestType type, uint64_t seed1, uint64_t seed2);

/**
 * @brief Hash a datablock and add it to the current checksum.
 *
 * @param digest a pointer to a RmDigest
 * @param data a block of data.
 * @param size the size of data
 */
void rm_digest_update(RmDigest *digest, const unsigned char *data, guint64 size);

/**
 * @brief Convert the checksum to a hexstring (like `md5sum`)
 *
 * rm_digest_update is not allowed to be called after finalizing.
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
 * @brief Convert the checksum to a byte blob.
 *
 * @param digest a pointer to a RmDigest
 * @param buffer The buffer to write the blob to.
 * @param buflen how long the buffer is.
 *
 * @return how many bytes were written. (for md5sum: 16)
 */
void rm_digest_finalize(RmDigest *digest);

int rm_digest_steal_buffer(RmDigest *digest, guint8 *buf, gsize buflen);

#endif /* end of include guard */


/* Additional functions not in original md5.c */
//void hash_fingerprint(RmSession *session, RmFile *file, const guint64 readsize);
//void hash_file(RmSession *session, RmFile *file);
