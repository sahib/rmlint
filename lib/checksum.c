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
 *  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

/* Welcome to hell!
 *
 * This file is 90% boring switch statements with innocent, but insane code
 * squashed between. Modify this file with care and make sure to test all
 * checksums afterwards.
 **/

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "checksum.h"

#include "checksums/blake2/blake2.h"
#include "checksums/cfarmhash.h"
#include "checksums/city.h"
#include "checksums/citycrc.h"
#include "checksums/murmur3.h"
#include "checksums/sha3/sha3.h"
#include "checksums/spooky-c.h"
#include "checksums/xxhash/xxhash.h"

#include "utilities.h"

#define _RM_CHECKSUM_DEBUG 0

///////////////////////////////////////
//    BUFFER POOL IMPLEMENTATION     //
///////////////////////////////////////

RmOff rm_buffer_size(RmBufferPool *pool) {
    return pool->buffer_size;
}

static RmBuffer *rm_buffer_new(RmBufferPool *pool) {
    RmBuffer *self = g_slice_new0(RmBuffer);
    self->pool = pool;
    self->data = g_slice_alloc(pool->buffer_size);
    return self;
}

static void rm_buffer_free(RmBuffer *buf) {
    g_slice_free1(buf->pool->buffer_size, buf->data);
    g_slice_free(RmBuffer, buf);
}

RmBufferPool *rm_buffer_pool_init(gsize buffer_size, gsize max_mem) {
    RmBufferPool *self = g_slice_new0(RmBufferPool);
    self->buffer_size = buffer_size;
    self->avail_buffers = max_mem ? MAX(max_mem / buffer_size, 1) : (gsize)-1;

    g_cond_init(&self->change);
    g_mutex_init(&self->lock);
    return self;
}

void rm_buffer_pool_destroy(RmBufferPool *pool) {
    g_slist_free_full(pool->stack, (GDestroyNotify)rm_buffer_free);

    g_mutex_clear(&pool->lock);
    g_cond_clear(&pool->change);
    g_slice_free(RmBufferPool, pool);
}

RmBuffer *rm_buffer_get(RmBufferPool *pool) {
    RmBuffer *buffer = NULL;
    g_mutex_lock(&pool->lock);
    {
        while(!buffer) {
            buffer = rm_util_slist_pop(&pool->stack, NULL);
            if(!buffer && pool->avail_buffers > 0) {
                buffer = rm_buffer_new(pool);
            }
            if(!buffer) {
                if(!pool->mem_warned) {
                    rm_log_warning_line(
                        "read buffer limit reached - waiting for "
                        "processing to catch up");
                    pool->mem_warned = true;
                }
                g_cond_wait(&pool->change, &pool->lock);
            }
        }
        pool->avail_buffers--;
    }
    g_mutex_unlock(&pool->lock);

    rm_assert_gentle(buffer);
    return buffer;
}

void rm_buffer_release(RmBuffer *buf) {
    RmBufferPool *pool = buf->pool;
    g_mutex_lock(&pool->lock);
    {
        pool->avail_buffers++;
        g_cond_signal(&pool->change);
        pool->stack = g_slist_prepend(pool->stack, buf);
    }
    g_mutex_unlock(&pool->lock);
}

static gboolean rm_buffer_equal(RmBuffer *a, RmBuffer *b) {
    return (a->len == b->len && memcmp(a->data, b->data, a->len) == 0);
}

///////////////////////////////////////
//      RMDIGEST IMPLEMENTATION      //
///////////////////////////////////////

static gpointer rm_init_digest_type_table(GHashTable **code_table) {
    static struct {
        char *name;
        RmDigestType code;
    } code_entries[] = {
        {"md5", RM_DIGEST_MD5},
        {"city512", RM_DIGEST_CITY512},
        {"xxhash", RM_DIGEST_XXHASH},
        {"farmhash", RM_DIGEST_XXHASH},
        {"murmur", RM_DIGEST_MURMUR},
        {"murmur128", RM_DIGEST_MURMUR},
        {"murmur256", RM_DIGEST_MURMUR256},
        {"murmur512", RM_DIGEST_MURMUR512},
        {"sha1", RM_DIGEST_SHA1},
        {"sha256", RM_DIGEST_SHA256},
        {"sha3", RM_DIGEST_SHA3_256},
        {"sha3-256", RM_DIGEST_SHA3_256},
        {"sha3-384", RM_DIGEST_SHA3_384},
        {"sha3-512", RM_DIGEST_SHA3_512},
        {"blake2s", RM_DIGEST_BLAKE2S},
        {"blake2b", RM_DIGEST_BLAKE2B},
        {"blake2sp", RM_DIGEST_BLAKE2SP},
        {"blake2bp", RM_DIGEST_BLAKE2BP},
        {"city256", RM_DIGEST_CITY256},
        {"murmur256", RM_DIGEST_MURMUR256},
        {"spooky32", RM_DIGEST_SPOOKY32},
        {"spooky64", RM_DIGEST_SPOOKY64},
        {"spooky128", RM_DIGEST_SPOOKY},
        {"spooky", RM_DIGEST_SPOOKY},
        {"ext", RM_DIGEST_EXT},
        {"cumulative", RM_DIGEST_CUMULATIVE},
        {"paranoid", RM_DIGEST_PARANOID},
        {"bastard", RM_DIGEST_BASTARD},
        {"bastard256", RM_DIGEST_BASTARD},
        {"city", RM_DIGEST_CITY},
        {"city128", RM_DIGEST_CITY},
        {"city256", RM_DIGEST_CITY256},
        {"city512", RM_DIGEST_CITY512},
#if HAVE_SHA512
        {"sha512", RM_DIGEST_SHA512},
#endif
    };

    *code_table = g_hash_table_new(g_str_hash, g_str_equal);

    const size_t n_codes = sizeof(code_entries) / sizeof(code_entries[0]);
    for(size_t idx = 0; idx < n_codes; idx++) {
        g_hash_table_insert(*code_table,
                            code_entries[idx].name,
                            GUINT_TO_POINTER(code_entries[idx].code));
    }

    return NULL;
}

RmDigestType rm_string_to_digest_type(const char *string) {
    static GHashTable *code_table = NULL;
    static GOnce table_once = G_ONCE_INIT;

    if(string == NULL) {
        return RM_DIGEST_UNKNOWN;
    }

    g_once(&table_once, (GThreadFunc)rm_init_digest_type_table, &code_table);

    gchar *lower_key = g_utf8_strdown(string, -1);
    RmDigestType code = GPOINTER_TO_UINT(g_hash_table_lookup(code_table, lower_key));
    g_free(lower_key);

    return code;
}

const char *rm_digest_type_to_string(RmDigestType type) {
    static const char *names[] = {[RM_DIGEST_UNKNOWN] = "unknown",
                                  [RM_DIGEST_MURMUR] = "murmur",
                                  [RM_DIGEST_SPOOKY] = "spooky",
                                  [RM_DIGEST_SPOOKY32] = "spooky32",
                                  [RM_DIGEST_SPOOKY64] = "spooky64",
                                  [RM_DIGEST_CITY] = "city",
                                  [RM_DIGEST_MD5] = "md5",
                                  [RM_DIGEST_SHA1] = "sha1",
                                  [RM_DIGEST_SHA256] = "sha256",
                                  [RM_DIGEST_SHA512] = "sha512",
                                  [RM_DIGEST_SHA3_256] = "sha3-256",
                                  [RM_DIGEST_SHA3_384] = "sha3-384",
                                  [RM_DIGEST_SHA3_512] = "sha3-512",
                                  [RM_DIGEST_BLAKE2S] = "blake2s",
                                  [RM_DIGEST_BLAKE2B] = "blake2b",
                                  [RM_DIGEST_BLAKE2SP] = "blake2sp",
                                  [RM_DIGEST_BLAKE2BP] = "blake2bp",
                                  [RM_DIGEST_MURMUR256] = "murmur256",
                                  [RM_DIGEST_CITY256] = "city256",
                                  [RM_DIGEST_BASTARD] = "bastard",
                                  [RM_DIGEST_MURMUR512] = "murmur512",
                                  [RM_DIGEST_CITY512] = "city512",
                                  [RM_DIGEST_EXT] = "ext",
                                  [RM_DIGEST_CUMULATIVE] = "cumulative",
                                  [RM_DIGEST_PARANOID] = "paranoid",
                                  [RM_DIGEST_FARMHASH] = "farmhash",
                                  [RM_DIGEST_XXHASH] = "xxhash"};

    return names[MIN(type, sizeof(names) / sizeof(names[0]))];
}

/*  TODO: remove? */
int rm_digest_type_to_multihash_id(RmDigestType type) {
    static int ids[] = {[RM_DIGEST_UNKNOWN] = -1,  [RM_DIGEST_MURMUR] = 17,
                        [RM_DIGEST_SPOOKY] = 14,   [RM_DIGEST_SPOOKY32] = 16,
                        [RM_DIGEST_SPOOKY64] = 18, [RM_DIGEST_CITY] = 15,
                        [RM_DIGEST_MD5] = 1,       [RM_DIGEST_SHA1] = 2,
                        [RM_DIGEST_SHA256] = 4,    [RM_DIGEST_SHA512] = 6,
                        [RM_DIGEST_MURMUR256] = 7, [RM_DIGEST_CITY256] = 8,
                        [RM_DIGEST_BASTARD] = 9,   [RM_DIGEST_MURMUR512] = 10,
                        [RM_DIGEST_CITY512] = 11,  [RM_DIGEST_EXT] = 12,
                        [RM_DIGEST_FARMHASH] = 19, [RM_DIGEST_CUMULATIVE] = 13,
                        [RM_DIGEST_PARANOID] = 14};

    return ids[MIN(type, sizeof(ids) / sizeof(ids[0]))];
}

#define ADD_SEED(digest, seed)                                              \
    {                                                                       \
        if(seed) {                                                          \
            g_checksum_update(digest->glib_checksum, (const guchar *)&seed, \
                              sizeof(RmOff));                               \
        }                                                                   \
    }

#define BLAKE_INIT(ALGO, ALGO_BIG)                                      \
    {                                                                   \
        digest->ALGO##_state = g_slice_alloc0(sizeof(ALGO##_state));    \
        ALGO##_init(digest->ALGO##_state, ALGO_BIG##_OUTBYTES);         \
        if(seed1) {                                                     \
            ALGO##_update(digest->ALGO##_state, &seed1, sizeof(RmOff)); \
        }                                                               \
        if(seed2) {                                                     \
            ALGO##_update(digest->ALGO##_state, &seed2, sizeof(RmOff)); \
        }                                                               \
        digest->bytes = ALGO_BIG##_OUTBYTES;                            \
    }

#define SHA3_INIT(SIZE)                                       \
    digest->sha3_ctx = g_slice_alloc0(sizeof(sha3_context));  \
    sha3_Init##SIZE(digest->sha3_ctx);                        \
    if(seed1) {                                               \
        sha3_Update(digest->sha3_ctx, &seed1, sizeof(RmOff)); \
    }                                                         \
    if(seed2) {                                               \
        sha3_Update(digest->sha3_ctx, &seed2, sizeof(RmOff)); \
    }                                                         \
    digest->bytes = (SIZE) / 8;

RmDigest *rm_digest_new(RmDigestType type, RmOff seed1, RmOff seed2, RmOff ext_size,
                        bool use_shadow_hash) {
    RmDigest *digest = g_slice_new0(RmDigest);

    digest->checksum = NULL;
    digest->type = type;
    digest->bytes = 0;

    switch(type) {
    case RM_DIGEST_SPOOKY32:
        /* cannot go lower than 64, since we read 8 byte in some places.
         * simulate by leaving the part at the end empty
         */
        digest->bytes = 64 / 8;
        break;
    case RM_DIGEST_XXHASH:
    case RM_DIGEST_FARMHASH:
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
    case RM_DIGEST_SHA3_256:
        SHA3_INIT(256);
        return digest;
    case RM_DIGEST_SHA3_384:
        SHA3_INIT(384);
        return digest;
    case RM_DIGEST_SHA3_512:
        SHA3_INIT(512);
        return digest;
    case RM_DIGEST_BLAKE2S:
        BLAKE_INIT(blake2s, BLAKE2S);
        return digest;
    case RM_DIGEST_BLAKE2B:
        BLAKE_INIT(blake2b, BLAKE2B);
        return digest;
    case RM_DIGEST_BLAKE2SP:
        BLAKE_INIT(blake2sp, BLAKE2S);
        return digest;
    case RM_DIGEST_BLAKE2BP:
        BLAKE_INIT(blake2bp, BLAKE2B);
        return digest;
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_CITY512:
        digest->bytes = 512 / 8;
        break;
    case RM_DIGEST_EXT:
        /* gets allocated on rm_digest_update() */
        digest->bytes = ext_size;
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
        digest->bytes = 0;
        digest->paranoid = g_slice_new0(RmParanoid);
        digest->paranoid->incoming_twin_candidates = g_async_queue_new();
        if(use_shadow_hash) {
            digest->paranoid->shadow_hash =
                rm_digest_new(RM_DIGEST_XXHASH, seed1, seed2, 0, false);
        }
        break;
    default:
        rm_assert_gentle_not_reached();
    }

    /* starting values to let us generate up to 4 different hashes in parallel with
     * different starting seeds:
     * */
    static const RmOff seeds[4] = {0x0000000000000000, 0xf0f0f0f0f0f0f0f0,
                                   0x3333333333333333, 0xaaaaaaaaaaaaaaaa};

    if(digest->bytes > 0 && type != RM_DIGEST_PARANOID) {
        const int n_seeds = sizeof(seeds) / sizeof(seeds[0]);

        /* checksum type - allocate memory and initialise */
        digest->checksum = g_slice_alloc0(digest->bytes);
        for(gsize block = 0; block < (digest->bytes / 16); block++) {
            digest->checksum[block].first = seeds[block % n_seeds] ^ seed1;
            digest->checksum[block].second = seeds[block % n_seeds] ^ seed2;
        }
    }

    if(digest->type == RM_DIGEST_BASTARD) {
        /* bastard type *always* has *pure* murmur hash for first checksum
         * and seeded city for second checksum */
        digest->checksum[0].first = digest->checksum[0].second = 0;
    }
    return digest;
}

void rm_digest_paranoia_shrink(RmDigest *digest, gsize new_size) {
    rm_assert_gentle(digest->type == RM_DIGEST_PARANOID);
    digest->bytes = new_size;
}

void rm_digest_release_buffers(RmDigest *digest) {
    if(digest->paranoid && digest->paranoid->buffers) {
        g_slist_free_full(digest->paranoid->buffers, (GDestroyNotify)rm_buffer_free);
        digest->paranoid->buffers = NULL;
    }
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
        if(digest->paranoid->shadow_hash) {
            rm_digest_free(digest->paranoid->shadow_hash);
        }
        rm_digest_release_buffers(digest);
        if(digest->paranoid->incoming_twin_candidates) {
            g_async_queue_unref(digest->paranoid->incoming_twin_candidates);
        }
        g_slist_free(digest->paranoid->rejects);
        g_slice_free(RmParanoid, digest->paranoid);
        break;
    case RM_DIGEST_SHA3_256:
    case RM_DIGEST_SHA3_384:
    case RM_DIGEST_SHA3_512:
        g_slice_free(sha3_context, digest->sha3_ctx);
        break;
    case RM_DIGEST_BLAKE2S:
        g_slice_free(blake2s_state, digest->blake2s_state);
        break;
    case RM_DIGEST_BLAKE2B:
        g_slice_free(blake2b_state, digest->blake2b_state);
        break;
    case RM_DIGEST_BLAKE2SP:
        g_slice_free(blake2sp_state, digest->blake2sp_state);
        break;
    case RM_DIGEST_BLAKE2BP:
        g_slice_free(blake2bp_state, digest->blake2bp_state);
        break;
    case RM_DIGEST_EXT:
    case RM_DIGEST_CUMULATIVE:
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_XXHASH:
    case RM_DIGEST_CITY512:
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_BASTARD:
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_SPOOKY32:
    case RM_DIGEST_SPOOKY64:
    case RM_DIGEST_FARMHASH:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
        if(digest->checksum) {
            g_slice_free1(digest->bytes, digest->checksum);
            digest->checksum = NULL;
        }
        break;
    default:
        rm_assert_gentle_not_reached();
    }
    g_slice_free(RmDigest, digest);
}

void rm_digest_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    switch(digest->type) {
    case RM_DIGEST_EXT:
/* Data is assumed to be a hex representation of a checksum.
 * Needs to be compressed in pure memory first.
 *
 * Checksum is not updated but rather overwritten.
 * */
#define CHAR_TO_NUM(c) (unsigned char)(g_ascii_isdigit(c) ? c - '0' : (c - 'a') + 10)

        rm_assert_gentle(data);

        digest->bytes = size / 2;
        digest->checksum = g_slice_alloc0(digest->bytes);

        for(unsigned i = 0; i < digest->bytes; ++i) {
            ((guint8 *)digest->checksum)[i] =
                (CHAR_TO_NUM(data[2 * i]) << 4) + CHAR_TO_NUM(data[2 * i + 1]);
        }

        break;
    case RM_DIGEST_MD5:
    case RM_DIGEST_SHA512:
    case RM_DIGEST_SHA256:
    case RM_DIGEST_SHA1:
        g_checksum_update(digest->glib_checksum, (const guchar *)data, size);
        break;
    case RM_DIGEST_SHA3_256:
    case RM_DIGEST_SHA3_384:
    case RM_DIGEST_SHA3_512:
        sha3_Update(digest->sha3_ctx, data, size);
        break;
    case RM_DIGEST_BLAKE2S:
        blake2s_update(digest->blake2s_state, data, size);
        break;
    case RM_DIGEST_BLAKE2B:
        blake2b_update(digest->blake2b_state, data, size);
        break;
    case RM_DIGEST_BLAKE2SP:
        blake2sp_update(digest->blake2sp_state, data, size);
        break;
    case RM_DIGEST_BLAKE2BP:
        blake2bp_update(digest->blake2bp_state, data, size);
        break;
    case RM_DIGEST_SPOOKY32:
        digest->checksum[0].first = spooky_hash32(data, size, digest->checksum[0].first);
        break;
    case RM_DIGEST_SPOOKY64:
        digest->checksum[0].first = spooky_hash64(data, size, digest->checksum[0].first);
        break;
    case RM_DIGEST_SPOOKY:
        spooky_hash128(data, size, (uint64_t *)&digest->checksum[0].first,
                       (uint64_t *)&digest->checksum[0].second);
        break;
    case RM_DIGEST_XXHASH:
        digest->checksum[0].first = XXH64(data, size, digest->checksum[0].first);
        break;
    case RM_DIGEST_FARMHASH:
        digest->checksum[0].first = cfarmhash((const char *)data, size);
        break;
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_MURMUR:
        for(guint8 block = 0; block < (digest->bytes / 16); block++) {
#if RM_PLATFORM_32
            MurmurHash3_x86_128(data, size, (uint32_t)digest->checksum[block].first,
                                &digest->checksum[block]);
#elif RM_PLATFORM_64
            MurmurHash3_x64_128(data, size, (uint32_t)digest->checksum[block].first,
                                &digest->checksum[block]);
#else
#error "Probably not a good idea to compile rmlint on 16bit."
#endif
        }
        break;
    case RM_DIGEST_CITY:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_CITY512:
        for(guint8 block = 0; block < (digest->bytes / 16); block++) {
            /* Opt out for the more optimized version.
            * This needs the crc command of sse4.2
            * (available on Intel Nehalem and up; my amd box doesn't have this though)
            */
            uint128 old = {digest->checksum[block].first, digest->checksum[block].second};
            old = CityHash128WithSeed((const char *)data, size, old);
            memcpy(&digest->checksum[block], &old, sizeof(uint128));
        }
        break;
    case RM_DIGEST_BASTARD:
        MurmurHash3_x86_128(data, size, (uint32_t)digest->checksum[0].first,
                            &digest->checksum[0]);

        uint128 old = {digest->checksum[1].first, digest->checksum[1].second};
        old = CityHash128WithSeed((const char *)data, size, old);
        memcpy(&digest->checksum[1], &old, sizeof(uint128));
        break;
    case RM_DIGEST_CUMULATIVE: {
        /*  This only XORS the two checksums. */
        for(gsize i = 0; i < digest->bytes; ++i) {
            ((guint8 *)digest->checksum)[i] ^= ((guint8 *)data)[i % size];
        }
    } break;
    case RM_DIGEST_PARANOID:
    default:
        rm_assert_gentle_not_reached();
    }
}

void rm_digest_buffered_update(RmBuffer *buffer) {
    RmDigest *digest = buffer->digest;
    if(digest->type != RM_DIGEST_PARANOID) {
        rm_digest_update(digest, buffer->data, buffer->len);
        rm_buffer_release(buffer);
    } else {
        RmParanoid *paranoid = digest->paranoid;

        /* efficiently append buffer to buffers GSList */
        if(!paranoid->buffers) {
            /* first buffer */
            paranoid->buffers = g_slist_prepend(NULL, buffer);
            paranoid->buffer_tail = paranoid->buffers;
        } else {
            paranoid->buffer_tail = g_slist_append(paranoid->buffer_tail, buffer)->next;
        }

        digest->bytes += buffer->len;

        if(paranoid->shadow_hash) {
            rm_digest_update(paranoid->shadow_hash, buffer->data, buffer->len);
        }

        if(paranoid->twin_candidate) {
            /* do a running check that digest remains the same as its candidate twin */
            if(rm_buffer_equal(buffer, paranoid->twin_candidate_buffer->data)) {
                /* buffers match; move ptr to next one ready for next buffer */
                paranoid->twin_candidate_buffer = paranoid->twin_candidate_buffer->next;
            } else {
                /* buffers don't match - delete candidate (new candidate might be added on
                 * next
                 * call to rm_digest_buffered_update) */
                paranoid->twin_candidate = NULL;
                paranoid->twin_candidate_buffer = NULL;
#if _RM_CHECKSUM_DEBUG
                rm_log_debug_line("Ejected candidate match at buffer #%u",
                                  g_slist_length(paranoid->buffers));
#endif
            }
        }

        while(!paranoid->twin_candidate && paranoid->incoming_twin_candidates &&
              (paranoid->twin_candidate =
                   g_async_queue_try_pop(paranoid->incoming_twin_candidates))) {
            /* validate the new candidate by comparing the previous buffers (not
             * including current)*/
            paranoid->twin_candidate_buffer = paranoid->twin_candidate->paranoid->buffers;
            GSList *iter_self = paranoid->buffers;
            gboolean match = TRUE;
            while(match && iter_self) {
                match = (rm_buffer_equal(paranoid->twin_candidate_buffer->data,
                                         iter_self->data));
                iter_self = iter_self->next;
                paranoid->twin_candidate_buffer = paranoid->twin_candidate_buffer->next;
            }
            if(paranoid->twin_candidate && !match) {
/* reject the twin candidate, also add to rejects list to speed up rm_digest_equal() */
#if _RM_CHECKSUM_DEBUG
                rm_log_debug_line("Rejected twin candidate %p for %p",
                                  paranoid->twin_candidate, paranoid);
#endif
                if(!paranoid->shadow_hash) {
                    /* we use the rejects file to speed up rm_digest_equal */
                    paranoid->rejects =
                        g_slist_prepend(paranoid->rejects, paranoid->twin_candidate);
                }
                paranoid->twin_candidate = NULL;
                paranoid->twin_candidate_buffer = NULL;
            } else {
#if _RM_CHECKSUM_DEBUG
                rm_log_debug_line("Added twin candidate %p for %p",
                                  paranoid->twin_candidate, paranoid);
#endif
            }
        }
    }
}

#define BLAKE_COPY(ALGO)                                                        \
    {                                                                           \
        self = g_slice_new0(RmDigest);                                          \
        self->bytes = digest->bytes;                                            \
        self->type = digest->type;                                              \
        self->ALGO##_state = g_slice_alloc0(sizeof(ALGO##_state));              \
        memcpy(self->ALGO##_state, digest->ALGO##_state, sizeof(ALGO##_state)); \
    }

RmDigest *rm_digest_copy(RmDigest *digest) {
    rm_assert_gentle(digest);

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
    case RM_DIGEST_SHA3_256:
    case RM_DIGEST_SHA3_384:
    case RM_DIGEST_SHA3_512:
        self = g_slice_new0(RmDigest);
        self->bytes = digest->bytes;
        self->type = digest->type;
        self->sha3_ctx = g_slice_alloc0(sizeof(sha3_context));
        memcpy(self->sha3_ctx, digest->sha3_ctx, sizeof(sha3_context));
        break;
    case RM_DIGEST_BLAKE2S:
        BLAKE_COPY(blake2s);
        break;
    case RM_DIGEST_BLAKE2B:
        BLAKE_COPY(blake2b);
        break;
    case RM_DIGEST_BLAKE2SP:
        BLAKE_COPY(blake2sp);
        break;
    case RM_DIGEST_BLAKE2BP:
        BLAKE_COPY(blake2bp);
        break;
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_SPOOKY32:
    case RM_DIGEST_SPOOKY64:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_CITY512:
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_XXHASH:
    case RM_DIGEST_FARMHASH:
    case RM_DIGEST_BASTARD:
    case RM_DIGEST_CUMULATIVE:
    case RM_DIGEST_EXT:
        self = rm_digest_new(digest->type, 0, 0, digest->bytes, FALSE);

        if(self->checksum && digest->checksum) {
            memcpy((char *)self->checksum, (char *)digest->checksum, self->bytes);
        }

        break;
    case RM_DIGEST_PARANOID:
    default:
        rm_assert_gentle_not_reached();
    }

    return self;
}

static gboolean rm_digest_needs_steal(RmDigestType digest_type) {
    switch(digest_type) {
    case RM_DIGEST_MD5:
    case RM_DIGEST_SHA512:
    case RM_DIGEST_SHA256:
    case RM_DIGEST_SHA1:
    case RM_DIGEST_SHA3_256:
    case RM_DIGEST_SHA3_384:
    case RM_DIGEST_SHA3_512:
    case RM_DIGEST_BLAKE2S:
    case RM_DIGEST_BLAKE2B:
    case RM_DIGEST_BLAKE2SP:
    case RM_DIGEST_BLAKE2BP:
        /* for all of the above, reading the digest is destructive, so we
         * need to take a copy */
        return TRUE;
    case RM_DIGEST_SPOOKY32:
    case RM_DIGEST_SPOOKY64:
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_CITY512:
    case RM_DIGEST_XXHASH:
    case RM_DIGEST_FARMHASH:
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_BASTARD:
    case RM_DIGEST_CUMULATIVE:
    case RM_DIGEST_EXT:
    case RM_DIGEST_PARANOID:
        return FALSE;
    default:
        rm_assert_gentle_not_reached();
        return FALSE;
    }
}

#define BLAKE_STEAL(ALGO)                                 \
    {                                                     \
        RmDigest *copy = rm_digest_copy(digest);          \
        ALGO##_final(copy->ALGO##_state, result, buflen); \
        rm_assert_gentle(buflen == digest->bytes);        \
        rm_digest_free(copy);                             \
    }

guint8 *rm_digest_steal(RmDigest *digest) {
    guint8 *result = g_slice_alloc0(digest->bytes);
    gsize buflen = digest->bytes;

    if(rm_digest_needs_steal(digest->type)) {
        /* reading the digest is destructive, so we need to take a copy */
        switch(digest->type) {
        case RM_DIGEST_SHA3_256:
        case RM_DIGEST_SHA3_384:
        case RM_DIGEST_SHA3_512: {
            RmDigest *copy = rm_digest_copy(digest);
            memcpy(result, sha3_Finalize(copy->sha3_ctx), digest->bytes);
            rm_assert_gentle(buflen == digest->bytes);
            rm_digest_free(copy);
            break;
        }
        case RM_DIGEST_BLAKE2S:
            BLAKE_STEAL(blake2s);
            break;
        case RM_DIGEST_BLAKE2B:
            BLAKE_STEAL(blake2b);
            break;
        case RM_DIGEST_BLAKE2SP:
            BLAKE_STEAL(blake2sp);
            break;
        case RM_DIGEST_BLAKE2BP:
            BLAKE_STEAL(blake2bp);
            break;
        default: {
            RmDigest *copy = rm_digest_copy(digest);
            g_checksum_get_digest(copy->glib_checksum, result, &buflen);
            rm_assert_gentle(buflen == digest->bytes);
            rm_digest_free(copy);
            break;
        }
        }
    } else {
        /*  Stateless checksum, just copy it. */
        memcpy(result, digest->checksum, digest->bytes);
    }
    return result;
}

guint rm_digest_hash(RmDigest *digest) {
    guint8 *buf = NULL;
    gsize bytes = 0;
    guint hash = 0;

    if(digest->type == RM_DIGEST_PARANOID) {
        if(digest->paranoid->shadow_hash) {
            buf = rm_digest_steal(digest->paranoid->shadow_hash);
            bytes = digest->paranoid->shadow_hash->bytes;
        } else {
            /* steal the first few bytes of the first buffer */
            if(digest->paranoid->buffers) {
                RmBuffer *buffer = digest->paranoid->buffers->data;
                if(buffer->len >= sizeof(guint)) {
                    hash = *(guint *)buffer->data;
                    return hash;
                }
            }
        }
    } else {
        buf = rm_digest_steal(digest);
        bytes = digest->bytes;
    }

    if(buf != NULL) {
        rm_assert_gentle(bytes >= sizeof(guint));
        hash = *(guint *)buf;
        g_slice_free1(bytes, buf);
    }
    return hash;
}

gboolean rm_digest_equal(RmDigest *a, RmDigest *b) {
    rm_assert_gentle(a && b);

    if(a->type != b->type) {
        return false;
    }

    if(a->bytes != b->bytes) {
        return false;
    }

    if(a->type == RM_DIGEST_PARANOID) {
        if(!a->paranoid->buffers) {
            /* buffers have been freed so we need to rely on shadow hash */
            return rm_digest_equal(a->paranoid->shadow_hash, b->paranoid->shadow_hash);
        }
        /* check if pre-matched twins */
        if(a->paranoid->twin_candidate == b || b->paranoid->twin_candidate == a) {
            return true;
        }
        /* check if already rejected */
        if(g_slist_find(a->paranoid->rejects, b) ||
           g_slist_find(b->paranoid->rejects, a)) {
            return false;
        }
        /* all the "easy" ways failed... do manual check of all buffers */
        GSList *a_iter = a->paranoid->buffers;
        GSList *b_iter = b->paranoid->buffers;
        guint bytes = 0;
        while(a_iter && b_iter) {
            if(!rm_buffer_equal(a_iter->data, b_iter->data)) {
                rm_log_error_line(
                    "Paranoid digest compare found mismatch - must be hash collision in "
                    "shadow hash");
                return false;
            }
            bytes += ((RmBuffer *)a_iter->data)->len;
            a_iter = a_iter->next;
            b_iter = b_iter->next;
        }

        return (!a_iter && !b_iter && bytes == a->bytes);
    } else if(rm_digest_needs_steal(a->type)) {
        guint8 *buf_a = rm_digest_steal(a);
        guint8 *buf_b = rm_digest_steal(b);
        gboolean result = !memcmp(buf_a, buf_b, a->bytes);

        g_slice_free1(a->bytes, buf_a);
        g_slice_free1(b->bytes, buf_b);

        return result;
    } else {
        return !memcmp(a->checksum, b->checksum, a->bytes);
    }
}

int rm_digest_hexstring(RmDigest *digest, char *buffer) {
    static const char *hex = "0123456789abcdef";
    guint8 *input = NULL;
    gsize bytes = 0;
    if(digest == NULL) {
        return 0;
    }

    if(digest->type == RM_DIGEST_PARANOID) {
        if(digest->paranoid->shadow_hash) {
            input = rm_digest_steal(digest->paranoid->shadow_hash);
            bytes = digest->paranoid->shadow_hash->bytes;
        }
    } else {
        input = rm_digest_steal(digest);
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
    }

    if(self->paranoid->shadow_hash) {
        return self->paranoid->shadow_hash->bytes;
    }

    return 0;
}

void rm_digest_send_match_candidate(RmDigest *target, RmDigest *candidate) {
    if(!target->paranoid->incoming_twin_candidates) {
        target->paranoid->incoming_twin_candidates = g_async_queue_new();
    }
    g_async_queue_push(target->paranoid->incoming_twin_candidates, candidate);
}

guint8 *rm_digest_sum(RmDigestType algo, const guint8 *data, gsize len, gsize *out_len) {
    RmDigest *digest = rm_digest_new(algo, 0, 0, 0, false);
    rm_digest_update(digest, data, len);

    guint8 *buf = rm_digest_steal(digest);
    if(out_len != NULL) {
        *out_len = digest->bytes;
    }

    rm_digest_free(digest);
    return buf;
}
