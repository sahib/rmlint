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
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "checksum.h"

#include "checksums/city.h"
#include "checksums/citycrc.h"
#include "checksums/murmur3.h"
#include "checksums/spooky-c.h"

RmDigestType rm_string_to_digest_type(const char *string) {
    if(string == NULL) {
        return RM_DIGEST_UNKNOWN;
    } else if(!strcasecmp(string, "md5")) {
        return RM_DIGEST_MD5;
#if HAVE_SHA512
    } else if(!strcasecmp(string, "sha512")) {
        return RM_DIGEST_SHA512;
#endif
    } else if(!strcasecmp(string, "city512")) {
        return RM_DIGEST_CITY512;
    } else if(!strcasecmp(string, "murmur512")) {
        return RM_DIGEST_MURMUR512;
    } else if(!strcasecmp(string, "sha256")) {
        return RM_DIGEST_SHA256;
    } else if(!strcasecmp(string, "city256")) {
        return RM_DIGEST_CITY256;
    } else if(!strcasecmp(string, "murmur256")) {
        return RM_DIGEST_MURMUR256;
    } else if(!strcasecmp(string, "sha1")) {
        return RM_DIGEST_SHA1;
    } else if(!strcasecmp(string, "spooky32")) {
        return RM_DIGEST_SPOOKY32;
    } else if(!strcasecmp(string, "spooky64")) {
        return RM_DIGEST_SPOOKY64;
    } else if(!strcasecmp(string, "murmur") || !strcasecmp(string, "murmur128")) {
        return RM_DIGEST_MURMUR;
    } else if(!strcasecmp(string, "spooky") || !strcasecmp(string, "spooky128")) {
        return RM_DIGEST_SPOOKY;
    } else if(!strcasecmp(string, "city") || !strcasecmp(string, "city128")) {
        return RM_DIGEST_CITY;
    } else if(!strcasecmp(string, "bastard") || !strcasecmp(string, "bastard256")) {
        return RM_DIGEST_BASTARD;
    } else if(!strcasecmp(string, "ext")) {
        return RM_DIGEST_EXT;
    } else if(!strcasecmp(string, "cumulative")) {
        return RM_DIGEST_CUMULATIVE;
    } else if(!strcasecmp(string, "paranoid")) {
        return RM_DIGEST_PARANOID;
    } else {
        return RM_DIGEST_UNKNOWN;
    }
}

const char *rm_digest_type_to_string(RmDigestType type) {
    static const char *names[] = {
        [RM_DIGEST_UNKNOWN]    = "unknown",
        [RM_DIGEST_MURMUR]     = "murmur",
        [RM_DIGEST_SPOOKY]     = "spooky",
        [RM_DIGEST_SPOOKY32]   = "spooky32",
        [RM_DIGEST_SPOOKY64]   = "spooky64",
        [RM_DIGEST_CITY]       = "city",
        [RM_DIGEST_MD5]        = "md5",
        [RM_DIGEST_SHA1]       = "sha1",
        [RM_DIGEST_SHA256]     = "sha256",
        [RM_DIGEST_SHA512]     = "sha512",
        [RM_DIGEST_MURMUR256]  = "murmur256",
        [RM_DIGEST_CITY256]    = "city256",
        [RM_DIGEST_BASTARD]    = "bastard",
        [RM_DIGEST_MURMUR512]  = "murmur512",
        [RM_DIGEST_CITY512]    = "city512",
        [RM_DIGEST_EXT]        = "ext",
        [RM_DIGEST_CUMULATIVE] = "cumulative",
        [RM_DIGEST_PARANOID]   = "paranoid"
    };

    return names[MIN(type, sizeof(names) / sizeof(names[0]))];
}

RmOff rm_digest_paranoia_bytes(void) {
    return 2 * 1024 * 1024;
    /* this is big enough buffer size to make seek time fairly insignificant relative to sequential read time,
     * eg 16MB read at typical 100 MB/s read rate = 160ms read vs typical seek time 10ms*/
}

#define ADD_SEED(digest, seed) {                                                           \
    if(seed) {                                                                             \
        g_checksum_update(digest->glib_checksum, (const guchar *)&seed, sizeof(RmOff));    \
    }                                                                                      \
}

RmDigest *rm_digest_new(RmDigestType type, RmOff seed1, RmOff seed2, RmOff paranoid_size) {
    RmDigest *digest = g_slice_new0(RmDigest);

    digest->checksum = NULL;
    digest->type = type;
    digest->bytes = 0;

    digest->initial_seed1 = seed1;
    digest->initial_seed2 = seed2;

    switch(type) {
    case RM_DIGEST_SPOOKY32:
        /* cannot go lower than 64, since we read 8 byte in some places.
         * simulate by leaving the part at the end empty
         */
        digest->bytes = 64 / 8;
        break;
    case RM_DIGEST_SPOOKY64:
        digest->bytes = 64 / 8;
        break;
    case RM_DIGEST_MD5:
        digest->glib_checksum = g_checksum_new(G_CHECKSUM_MD5);
        ADD_SEED(digest, seed1);
        digest->bytes = 128 / 8;
        return digest;
#if HAVE_SHA512
    case RM_DIGEST_SHA512:
        digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA512);
        ADD_SEED(digest, seed1);
        digest->bytes = 512 / 8;
        return digest;
#endif
    case RM_DIGEST_SHA256:
        digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA256);
        ADD_SEED(digest, seed1);
        digest->bytes = 256 / 8;
        return digest;
    case RM_DIGEST_SHA1:
        digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA1);
        ADD_SEED(digest, seed1);
        digest->bytes = 160 / 8;
        return digest;
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_CITY512:
        digest->bytes = 512 / 8;
        break;
    case RM_DIGEST_EXT:
        /* gets allocated on rm_digest_update() */
        digest->bytes = paranoid_size;
        break;
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_BASTARD:
        digest->bytes = 256 / 8;
        break;
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
    case RM_DIGEST_CUMULATIVE:
        digest->bytes = 128 / 8;
        break;
    case RM_DIGEST_PARANOID:
        g_assert(paranoid_size <= rm_digest_paranoia_bytes());
        g_assert(paranoid_size > 0);
        digest->bytes = paranoid_size;
        digest->paranoid_offset = 0;
        digest->shadow_hash = rm_digest_new(RM_DIGEST_SPOOKY, seed1, seed2, 0);
        break;
    default:
        g_assert_not_reached();
    }

    /* starting values to let us generate up to 4 different hashes in parallel with
     * different starting seeds:
     * */
    static const RmOff seeds[4] = {
        0x0000000000000000,
        0xf0f0f0f0f0f0f0f0,
        0x3333333333333333,
        0xaaaaaaaaaaaaaaaa
    };

    if(digest->bytes > 0) {
        const int n_seeds = sizeof(seeds) / sizeof(seeds[0]);

        /* checksum type - allocate memory and initialise */
        digest->checksum = g_slice_alloc0(digest->bytes);
        for (gsize block = 0; block < (digest->bytes / 16); block++) {
            digest->checksum[block].first = seeds[block % n_seeds] ^ digest->initial_seed1;
            digest->checksum[block].second = seeds[block % n_seeds] ^ digest->initial_seed2;
        }
    }

    if (digest->type == RM_DIGEST_BASTARD) {
        /* bastard type *always* has *pure* murmur hash for first checksum
         * and seeded city for second checksum */
        digest->checksum[0].first = digest->checksum[0].second = 0;
    }
    return digest;
}

void rm_digest_paranoia_shrink(RmDigest *digest, gsize new_size) {
    g_assert(new_size < digest->bytes);
    g_assert(digest->type == RM_DIGEST_PARANOID);

    RmUint128 *old_checksum = digest->checksum;
    gsize old_bytes = digest->bytes;

    digest->checksum = g_slice_alloc0(new_size);
    digest->bytes = new_size;
    memcpy(digest->checksum, old_checksum, new_size);

    g_slice_free1(old_bytes, old_checksum);
}

void rm_digest_free(RmDigest *digest) {
    switch(digest->type) {
    case RM_DIGEST_MD5:
    case RM_DIGEST_SHA512:
    case RM_DIGEST_SHA256:
    case RM_DIGEST_SHA1:
        g_checksum_free(digest->glib_checksum);
        digest->glib_checksum = NULL;
        break;
    case RM_DIGEST_PARANOID:
        if(digest->shadow_hash) {
            rm_digest_free(digest->shadow_hash);
        }
    case RM_DIGEST_EXT:
    case RM_DIGEST_CUMULATIVE:
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_CITY512:
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_BASTARD:
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_SPOOKY32:
    case RM_DIGEST_SPOOKY64:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
        if(digest->checksum) {
            g_slice_free1(digest->bytes, digest->checksum);
            digest->checksum = NULL;
        }
        break;
    default:
        g_assert_not_reached();
    }
    g_slice_free(RmDigest, digest);
}

void rm_digest_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    switch(digest->type) {
    case RM_DIGEST_EXT:
        /* Data is assumed to be a hex representation of a cchecksum.
         * Needs to be compressed in pure memory first.
         *
         * Checksum is not updated but rather overwritten.
         * */
#define CHAR_TO_NUM(c) (unsigned char)(g_ascii_isdigit(c) ? c - '0' : (c - 'a') + 10)

        g_assert(data);

        digest->bytes = size / 2;
        digest->checksum = g_slice_alloc0(digest->bytes);

        for(unsigned i = 0; i < digest->bytes; ++i) {
            ((guint8 *)digest->checksum)[i] = (CHAR_TO_NUM(data[2 * i]) << 4) + CHAR_TO_NUM(data[2 * i + 1]);
        }

        break;
    case RM_DIGEST_MD5:
    case RM_DIGEST_SHA512:
    case RM_DIGEST_SHA256:
    case RM_DIGEST_SHA1:
        g_checksum_update(digest->glib_checksum, (const guchar *)data, size);
        break;
    case RM_DIGEST_SPOOKY32:
        digest->checksum[0].first = spooky_hash32(data, size, digest->checksum[0].first);
        break;
    case RM_DIGEST_SPOOKY64:
        digest->checksum[0].first = spooky_hash64(data, size, digest->checksum[0].first);
        break;
    case RM_DIGEST_SPOOKY:
        spooky_hash128(data, size, &digest->checksum[0].first, &digest->checksum[0].second);
        break;
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_MURMUR:
        for (guint8 block = 0; block < ( digest->bytes / 16 ); block++) {
#if RM_PLATFORM_32
            MurmurHash3_x86_128(data, size,
                                (uint32_t)digest->checksum[block].first,
                                &digest->checksum[block]); //&
#elif RM_PLATFORM_64
            MurmurHash3_x64_128(data, size,
                                (uint32_t)digest->checksum[block].first,
                                &digest->checksum[block]);
#else
#error "Probably not a good idea to compile rmlint on 16bit."
#endif
        }
        break;
    case RM_DIGEST_CITY:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_CITY512:
        for (guint8 block = 0; block < (digest->bytes / 16); block++) {
            /* Opt out for the more optimized version.
            * This needs the crc command of sse4.2
            * (available on Intel Nehalem and up; my amd box doesn't have this though)
            */
            uint128 old = {digest->checksum[block].first, digest->checksum[block].second};
#if RM_PLATFORM_64 && HAVE_SSE42
            old = CityHashCrc128WithSeed((const char *)data, size, old);
#else
            old = CityHash128WithSeed((const char *)data, size, old);
#endif
            memcpy(&digest->checksum[block], &old, sizeof(uint128));
        }
        break;
    case RM_DIGEST_BASTARD:
        MurmurHash3_x86_128(data, size, (uint32_t)digest->checksum[0].first, &digest->checksum[0]);

        uint128 old = {digest->checksum[1].first, digest->checksum[1].second};
#if RM_PLATFORM_64 && HAVE_SSE42
        old = CityHashCrc128WithSeed((const char *)data, size, old);
#else
        old = CityHash128WithSeed((const char *) data, size, old);
#endif
        memcpy(&digest->checksum[1], &old, sizeof(uint128));
        break;
    case RM_DIGEST_CUMULATIVE: {
        /* This is basically FNV1a, it is just important that the order of
         * adding data to the hash has no effect on the result, so it can
         * be used as a lookup key:
         *
         * http://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
         * */
        RmOff hash = 0xcbf29ce484222325;
        for(gsize i = 0; i < digest->bytes; ++i) {
            hash ^= ((guint8 *)data)[i % size];
            hash *= 0x100000001b3;
            ((guint8 *)digest->checksum)[i] += hash;
        }
    }
    break;
    case RM_DIGEST_PARANOID:
        g_assert(size + digest->paranoid_offset <= digest->bytes);
        memcpy((char *)digest->checksum + digest->paranoid_offset, data, size);
        digest->paranoid_offset += size;
        rm_digest_update(digest->shadow_hash, data, size);
        break;
    default:
        g_assert_not_reached();
    }
}

RmDigest *rm_digest_copy(RmDigest *digest) {
    g_assert(digest);

    RmDigest *self = NULL;

    switch(digest->type) {
    case RM_DIGEST_MD5:
    case RM_DIGEST_SHA512:
    case RM_DIGEST_SHA256:
    case RM_DIGEST_SHA1:
        self = g_slice_new0(RmDigest);
        self->bytes = digest->bytes;
        self->type = digest->type;
        self->glib_checksum = g_checksum_copy(digest->glib_checksum);
        break;
    case RM_DIGEST_PARANOID:
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_SPOOKY32:
    case RM_DIGEST_SPOOKY64:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_CITY512:
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_BASTARD:
    case RM_DIGEST_CUMULATIVE:
    case RM_DIGEST_EXT:
        self = rm_digest_new(
                   digest->type, digest->initial_seed1, digest->initial_seed2, digest->bytes
               );

        if(self->type == RM_DIGEST_PARANOID) {
            self->paranoid_offset = digest->paranoid_offset;
            rm_digest_free(self->shadow_hash);
            self->shadow_hash = rm_digest_copy(digest->shadow_hash);
        }

        if(self->checksum && digest->checksum) {
            memcpy((char *)self->checksum, (char *)digest->checksum, self->bytes);
        }

        break;
    default:
        g_assert_not_reached();
    }

    return self;
}

guint8 *rm_digest_steal_buffer(RmDigest *digest) {
    guint8 *result = g_slice_alloc0(digest->bytes);
    RmDigest *copy = NULL;
    gsize buflen = digest->bytes;

    switch(digest->type) {
    case RM_DIGEST_MD5:
    case RM_DIGEST_SHA512:
    case RM_DIGEST_SHA256:
    case RM_DIGEST_SHA1:
        /* for all of the above, reading the digest is destructive, so we
         * need to take a copy */
        copy = rm_digest_copy(digest);
        g_checksum_get_digest(copy->glib_checksum, result, &buflen);
        g_assert(buflen == digest->bytes);
        rm_digest_free(copy);
        break;
    case RM_DIGEST_SPOOKY32:
    case RM_DIGEST_SPOOKY64:
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_CITY512:
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_BASTARD:
    case RM_DIGEST_CUMULATIVE:
    case RM_DIGEST_PARANOID:
    case RM_DIGEST_EXT:
        memcpy(result, digest->checksum, digest->bytes);
        break;
    default:
        g_assert_not_reached();
    }

    return result;
}

guint rm_digest_hash(RmDigest *digest) {
    guint8 *buf = NULL;
    gsize bytes = 0;

    if(digest->type == RM_DIGEST_PARANOID) {
        buf = rm_digest_steal_buffer(digest->shadow_hash);
        bytes = digest->shadow_hash->bytes;
    } else {
        buf = rm_digest_steal_buffer(digest);
        bytes = digest->bytes;
    }

    guint hash = *(RmOff *)buf;
    g_slice_free1(bytes, buf);
    return hash;
}

gboolean rm_digest_equal(RmDigest *a, RmDigest *b) {
    guint8 *buf_a = rm_digest_steal_buffer(a);
    guint8 *buf_b = rm_digest_steal_buffer(b);

    gboolean result = !memcmp(buf_a, buf_b, MIN(a->bytes, b->bytes));

    g_slice_free1(a->bytes, buf_a);
    g_slice_free1(b->bytes, buf_b);

    return result;
}

int rm_digest_hexstring(RmDigest *digest, char *buffer) {
    static const char *hex = "0123456789abcdef";
    guint8 *input = NULL;
    gsize bytes = 0;
    if(digest == NULL) {
        return 0;
    }

    if(digest->type == RM_DIGEST_PARANOID) {
        input = rm_digest_steal_buffer(digest->shadow_hash);
        bytes = digest->shadow_hash->bytes;
    } else {
        input = rm_digest_steal_buffer(digest);
        bytes = digest->bytes;
    }

    for(gsize i = 0; i < bytes; ++i) {
        buffer[0] = hex[input[i] / 16];
        buffer[1] = hex[input[i] % 16];

        if(i == bytes - 1) {
            buffer[2] = '\0';
        }

        buffer += 2;
    }

    g_slice_free1(bytes, input);
    return bytes * 2 + 1;
}

int rm_digest_get_bytes(RmDigest *self) {
    if(self == NULL) {
        return 0;
    }

    if(self->type != RM_DIGEST_PARANOID) {
        return self->bytes;
    } else {
        return self->shadow_hash->bytes;
    }
}
