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
 * This file is mostly boring code except for the paranoid digest
 * optimisations which are pretty insane.
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
#include "checksums/murmur3.h"
#include "checksums/sha3/sha3.h"
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

typedef void (*RmDigestInitFunc)(RmDigest *digest, RmOff seed1, RmOff seed2, RmOff ext_size, bool use_shadow_hash);
typedef void (*RmDigestFreeFunc)(RmDigest *digest);
typedef void (*RmDigestUpdateFunc)(RmDigest *digest, const unsigned char *data, RmOff size);
typedef void (*RmDigestCopyFunc)(RmDigest *digest, RmDigest *copy);
typedef void (*RmDigestStealFunc)(RmDigest *digest, guint8 *result);

typedef struct RmDigestSpec {
    const char *name;
    const uint bits;          // length of the output checksum in bits
    RmDigestInitFunc init;    // performs initialisation of digest->state
    RmDigestFreeFunc free;
    RmDigestUpdateFunc update;
    RmDigestCopyFunc copy;
    RmDigestStealFunc steal;
} RmDigestSpec;


///////////////////////////
//    common funcs for   //
//    non-cryptographic  //
//    hashes             //
///////////////////////////

#define ALLOC_BYTES(bytes) MAX(8, bytes)

static void rm_digest_generic_init(RmDigest *digest, RmOff seed1, RmOff seed2, _UNUSED RmOff ext_size, _UNUSED bool use_shadow_hash) {
    /* init for hashes which just require allocation of digest->checksum */

    /* Cannot go lower than 8, since we read 8 byte in some places.
     * For some checksums this may mean trailing zeros in the unused bytes */
    digest->state = g_slice_alloc0(ALLOC_BYTES(digest->bytes));

    if(seed1 && seed2) {
        /* copy seeds to checksum */
        size_t seed_bytes = MIN(sizeof(RmOff), digest->bytes / 2);
        memcpy(digest->state, &seed1, seed_bytes);
        memcpy(digest->state + digest->bytes/2, &seed2, seed_bytes);
    } else if(seed1) {
        size_t seed_bytes = MIN(sizeof(RmOff), digest->bytes);
        memcpy(digest->state, &seed1, seed_bytes);
    }
}

static void rm_digest_generic_free(RmDigest *digest) {
    if(digest->state) {
        g_slice_free1(digest->bytes, digest->state);
        digest->state = NULL;
    }
}

static void rm_digest_generic_copy(RmDigest *digest, RmDigest *copy) {
    copy->state = g_slice_copy(ALLOC_BYTES(digest->bytes), digest->state);
}

#define GENERIC_FUNCS(ALGO) rm_digest_generic_init,  rm_digest_generic_free,  rm_digest_##ALGO##_update, rm_digest_generic_copy, NULL

///////////////////////////
//        xxhash         //
///////////////////////////

static void rm_digest_xxhash_init(RmDigest *digest, RmOff seed1, RmOff seed2, _UNUSED RmOff ext_size, _UNUSED bool use_shadow_hash) {
    digest->state = XXH64_createState();
    XXH64_reset(digest->state, seed1 ^ seed2);
}

static void rm_digest_xxhash_free(RmDigest *digest) {
    XXH64_freeState(digest->state);
}

static void rm_digest_xxhash_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    XXH64_update(digest->state, data, size);
}

static void rm_digest_xxhash_copy(RmDigest *digest, RmDigest *copy) {
    copy->state = XXH64_createState();
    memcpy(copy->state, digest->state, sizeof(XXH64_state_t));
}

static void rm_digest_xxhash_steal(RmDigest *digest, guint8 *result) {
    *(unsigned long long*)result = XXH64_digest(digest->state);
}


static const RmDigestSpec xxhash_spec =  { "xxhash", 64, rm_digest_xxhash_init, rm_digest_xxhash_free, rm_digest_xxhash_update, rm_digest_xxhash_copy, rm_digest_xxhash_steal};

///////////////////////////
//      farmhash         //
///////////////////////////

/* TODO: check that this is not broken, i.e. final hash is independent of increment size */

static void rm_digest_farmhash_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    uint128_t *hash = digest->state;
    *hash = farmhash128_with_seed((const char*)data, size, *hash);
}

static const RmDigestSpec farmhash_spec =  { "farmhash", 64, GENERIC_FUNCS(farmhash)};

///////////////////////////
//        murmur         //
///////////////////////////


#define CREATE_MURMUR_FUNCS(TYPE)                                               \
static void rm_digest_murmur_##TYPE##_free(RmDigest *digest) {                  \
    MurmurHash3_##TYPE##_free(digest->state);                                   \
}                                                                               \
                                                                                \
static void rm_digest_murmur_##TYPE##_update(RmDigest *digest,                  \
                                    const unsigned char *data,                  \
                                    RmOff size) {                               \
    MurmurHash3_##TYPE##_update(digest->state, data, size);                     \
}                                                                               \
                                                                                \
static void rm_digest_murmur_##TYPE##_copy(RmDigest *digest, RmDigest *copy) {  \
    copy->state = MurmurHash3_##TYPE##_copy(digest->state);                     \
}                                                                               \
                                                                                \
static void rm_digest_murmur_##TYPE##_steal(RmDigest *digest, guint8 *result) { \
    MurmurHash3_##TYPE##_steal(digest->state, result);                          \
}

#define MURMUR_FUNCS(TYPE) rm_digest_murmur_##TYPE##_init, rm_digest_murmur_##TYPE##_free, rm_digest_murmur_##TYPE##_update, rm_digest_murmur_##TYPE##_copy, rm_digest_murmur_##TYPE##_steal


#if RM_PLATFORM_32

CREATE_MURMUR_FUNCS(x86_128)

static void rm_digest_murmur_x86_128_init(RmDigest *digest, RmOff seed1, RmOff seed2, _UNUSED RmOff ext_size, _UNUSED bool use_shadow_hash) {
    digest->state = MurmurHash3_x86_128_new(seed1, seed1>>32, seed2, seed2>>32);
}

static const RmDigestSpec murmur_spec = { "murmur", 128, MURMUR_FUNCS(x86_128)};

#elif RM_PLATFORM_64

CREATE_MURMUR_FUNCS(x64_128)

static void rm_digest_murmur_x64_128_init(RmDigest *digest, RmOff seed1, RmOff seed2, _UNUSED RmOff ext_size, _UNUSED bool use_shadow_hash) {
    digest->state = MurmurHash3_x64_128_new(seed1, seed2);
}

static const RmDigestSpec murmur_spec = { "murmur", 128, MURMUR_FUNCS(x64_128)};

#else
#error "Probably not a good idea to compile rmlint on 16bit."
#endif


///////////////////////////
//      cumulative       //
///////////////////////////

static void rm_digest_cumulative_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    /*  This only XORS the two checksums. */
    guint8 *hash = digest->state;
    for(gsize i = 0; i < size; ++i) {
        hash[i % digest->bytes] ^= ((guint8 *)data)[i];
    }
}

static const RmDigestSpec cumulative_spec =  { "cumulative", 128, GENERIC_FUNCS(cumulative)};


///////////////////////////
//     highway hash      //
///////////////////////////

static void rm_digest_highway_init(RmDigest *digest, RmOff seed1, RmOff seed2, _UNUSED RmOff ext_size, _UNUSED bool use_shadow_hash) {
    uint64_t key[4] = {1, 2, 3, 4};
    if(seed1) {
        key[0] = (uint64_t)seed1;
    }
    if(seed2) {
        key[2] = (uint64_t)seed2;
    }

    digest->state = g_slice_alloc0(sizeof(HighwayHashCat));
    HighwayHashCatStart(key, digest->state);
}

static void rm_digest_highway_free(RmDigest *digest) {
    g_slice_free(HighwayHashCat, digest->state);
}

static void rm_digest_highway_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    HighwayHashCatAppend((const uint8_t*)data, size, digest->state);
}

static void rm_digest_highway_copy(RmDigest *digest, RmDigest *copy) {
    copy->state = g_slice_copy(sizeof(HighwayHashCat), digest->state);
}

/* HighwayHashCatFinish functions are non-destructive */
static void rm_digest_highway256_steal(RmDigest *digest, guint8 *result) {
    HighwayHashCatFinish256(digest->state, (uint64_t*)result);
}

static void rm_digest_highway128_steal(RmDigest *digest, guint8 *result) {
    HighwayHashCatFinish128(digest->state, (uint64_t*)result);
}

static void rm_digest_highway64_steal(RmDigest *digest, guint8 *result) {
    *(uint64_t*)result = HighwayHashCatFinish64(digest->state);
}

#define HIGHWAY_SPEC(BITS) BITS, rm_digest_highway_init, rm_digest_highway_free, rm_digest_highway_update, rm_digest_highway_copy, rm_digest_highway##BITS##_steal

static const RmDigestSpec highway256_spec =  {"highway256", HIGHWAY_SPEC(256)};
static const RmDigestSpec highway128_spec =  {"highway128", HIGHWAY_SPEC(128)};
static const RmDigestSpec highway64_spec  =  {"highway64", HIGHWAY_SPEC(64)};


///////////////////////////
//      glib hashes      //
///////////////////////////

static const GChecksumType glib_map[] = {
    [RM_DIGEST_MD5]        = G_CHECKSUM_MD5,
    [RM_DIGEST_SHA1]       = G_CHECKSUM_SHA1,
    [RM_DIGEST_SHA256]     = G_CHECKSUM_SHA256,
#if HAVE_SHA512
    [RM_DIGEST_SHA512]     = G_CHECKSUM_SHA512,
#endif
};

static void rm_digest_glib_init(RmDigest *digest, RmOff seed1, RmOff seed2, _UNUSED RmOff ext_size, _UNUSED bool use_shadow_hash) {
    digest->state = g_checksum_new(glib_map[digest->type]);
    if(seed1) {
        g_checksum_update(digest->state, (const guchar *)&seed1, sizeof(seed1));
    }
    if(seed2) {
        g_checksum_update(digest->state, (const guchar *)&seed2, sizeof(seed2));
    }
}

static void rm_digest_glib_free(RmDigest *digest) {
    g_checksum_free(digest->state);
}

static void rm_digest_glib_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    g_checksum_update(digest->state, data, size);
}

static void rm_digest_glib_copy(RmDigest *digest, RmDigest *copy) {
    copy->state = g_checksum_copy(digest->state);
}

static void rm_digest_glib_steal(RmDigest *digest, guint8 *result) {
    GChecksum *copy = g_checksum_copy(digest->state);
    gsize buflen = digest->bytes;
    g_checksum_get_digest(copy, result, &buflen);
    rm_assert_gentle(buflen == digest->bytes);
    g_checksum_free(copy);
}

#define GLIB_FUNCS rm_digest_glib_init, rm_digest_glib_free, rm_digest_glib_update, rm_digest_glib_copy, rm_digest_glib_steal

static const RmDigestSpec md5_spec =  {"md5", 128, GLIB_FUNCS};
static const RmDigestSpec sha1_spec = {"sha1", 160, GLIB_FUNCS};
static const RmDigestSpec sha256_spec = {"sha256", 256, GLIB_FUNCS};
#if HAVE_SHA512
static const RmDigestSpec sha512_spec = {"sha512", 512, GLIB_FUNCS};
#endif

///////////////////////////
//      sha3 hashes      //
///////////////////////////


static void rm_digest_sha3_init(RmDigest *digest, RmOff seed1, RmOff seed2, _UNUSED RmOff ext_size, _UNUSED bool use_shadow_hash) {
    digest->state = g_slice_alloc0(sizeof(sha3_context));
    switch(digest->type) {
        case RM_DIGEST_SHA3_256:
            sha3_Init256(digest->state);
            break;
        case RM_DIGEST_SHA3_384:
            sha3_Init384(digest->state);
            break;
        case RM_DIGEST_SHA3_512:
            sha3_Init512(digest->state);
            break;
        default:
            g_assert_not_reached();
    }
    if(seed1) {
        sha3_Update(digest->state, &seed1, sizeof(seed1));
    }
    if(seed2) {
        sha3_Update(digest->state, &seed2, sizeof(seed2));
    }
}

static void rm_digest_sha3_free(RmDigest *digest) {
    g_slice_free(sha3_context, digest->state);
}

static void rm_digest_sha3_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    sha3_Update(digest->state, data, size);
}

static void rm_digest_sha3_copy(RmDigest *digest, RmDigest *copy) {
    copy->state = g_slice_copy(sizeof(sha3_context), digest->state);
}

static void rm_digest_sha3_steal(RmDigest *digest, guint8 *result) {
    sha3_context *copy = g_slice_copy(sizeof(sha3_context), digest->state);
    memcpy(result, sha3_Finalize(copy), digest->bytes);
    g_slice_free(sha3_context, copy);
}

#define SHA3_SPEC(BITS) BITS, rm_digest_sha3_init, rm_digest_sha3_free, rm_digest_sha3_update, rm_digest_sha3_copy, rm_digest_sha3_steal

static const RmDigestSpec sha3_256_spec = { "sha3-256", SHA3_SPEC(256)};
static const RmDigestSpec sha3_384_spec = { "sha3-384", SHA3_SPEC(384)};
static const RmDigestSpec sha3_512_spec = { "sha3-512", SHA3_SPEC(512)};

///////////////////////////
//      blake hashes     //
///////////////////////////

#define CREATE_BLAKE_FUNCS(ALGO, ALGO_BIG)                          \
                                                                    \
static void rm_digest_##ALGO##_init(RmDigest *digest, RmOff seed1,  \
                                    RmOff seed2,                    \
                                    _UNUSED RmOff ext_size,         \
                                    _UNUSED bool use_shadow_hash) { \
    digest->state = g_slice_alloc0(sizeof(ALGO##_state));           \
    ALGO##_init(digest->state, ALGO_BIG##_OUTBYTES);                \
    if(seed1) {                                                     \
        ALGO##_update(digest->state, &seed1, sizeof(RmOff));        \
    }                                                               \
    if(seed2) {                                                     \
        ALGO##_update(digest->state, &seed2, sizeof(RmOff));        \
    }                                                               \
    g_assert(digest->bytes==ALGO_BIG##_OUTBYTES);                   \
}                                                                   \
                                                                    \
static void rm_digest_##ALGO##_free(RmDigest *digest) {             \
    g_slice_free(ALGO##_state, digest->state);                      \
}                                                                   \
                                                                    \
static void rm_digest_##ALGO##_update(RmDigest *digest,             \
                                      const unsigned char *data,    \
                                      RmOff size) {                 \
    ALGO##_update(digest->state, data, size);                       \
}                                                                   \
                                                                    \
static void rm_digest_##ALGO##_copy(RmDigest *digest,               \
                                    RmDigest *copy) {               \
    copy->state = g_slice_copy(sizeof(ALGO##_state),                \
                                      digest->state);               \
}                                                                   \
                                                                    \
static void rm_digest_##ALGO##_steal(RmDigest *digest,              \
                                     guint8 *result) {              \
    ALGO##_state *copy = g_slice_copy(sizeof(ALGO##_state),         \
                                      digest->state);               \
    ALGO##_final(copy, result, digest->bytes);                      \
    g_slice_free(ALGO##_state, copy);                               \
}



CREATE_BLAKE_FUNCS(blake2b, BLAKE2B);
CREATE_BLAKE_FUNCS(blake2bp, BLAKE2B);
CREATE_BLAKE_FUNCS(blake2s, BLAKE2S);
CREATE_BLAKE_FUNCS(blake2sp, BLAKE2S);

#define BLAKE_FUNCS(ALGO) rm_digest_##ALGO##_init, rm_digest_##ALGO##_free, rm_digest_##ALGO##_update, rm_digest_##ALGO##_copy, rm_digest_##ALGO##_steal

static const RmDigestSpec blake2b_spec = {"blake2b", 512, BLAKE_FUNCS(blake2b)};
static const RmDigestSpec blake2bp_spec = {"blake2bp", 512, BLAKE_FUNCS(blake2bp)};
static const RmDigestSpec blake2s_spec = {"blake2s", 256, BLAKE_FUNCS(blake2s)};
static const RmDigestSpec blake2sp_spec = {"blake2sp", 256, BLAKE_FUNCS(blake2sp)};

///////////////////////////
//      ext  hash        //
///////////////////////////


static void rm_digest_ext_init(RmDigest *digest, RmOff seed1, RmOff seed2, RmOff ext_size, bool use_shadow_hash) {
    digest->bytes = ext_size;
    rm_digest_generic_init(digest, seed1, seed2, ext_size, use_shadow_hash);
}

static void rm_digest_ext_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    /* Data is assumed to be a hex representation of a checksum.
     * Needs to be compressed in pure memory first.
     *
     * Checksum is not updated but rather overwritten.
     * */
#define CHAR_TO_NUM(c) (unsigned char)(g_ascii_isdigit(c) ? c - '0' : (c - 'a') + 10)

    digest->bytes = size / 2;
    digest->state = g_slice_alloc0(digest->bytes);

    for(unsigned i = 0; i < digest->bytes; ++i) {
        ((guint8 *)digest->state)[i] =
            (CHAR_TO_NUM(data[2 * i]) << 4) + CHAR_TO_NUM(data[2 * i + 1]);
    }
}

static const RmDigestSpec ext_spec = {"ext", 0, rm_digest_ext_init, rm_digest_generic_free, rm_digest_ext_update, rm_digest_generic_copy, NULL};


///////////////////////////
//     paranoid 'hash'   //
///////////////////////////


static void rm_digest_paranoid_init(RmDigest *digest, RmOff seed1, RmOff seed2, _UNUSED RmOff ext_size, bool use_shadow_hash) {
    RmParanoid *paranoid = g_slice_new0(RmParanoid);
    digest->state = paranoid;
    paranoid->incoming_twin_candidates = g_async_queue_new();
    if(use_shadow_hash) {
        paranoid->shadow_hash = rm_digest_new(RM_DIGEST_XXHASH, seed1, seed2, 0, false);
        digest->bytes = paranoid->shadow_hash->bytes;
    }
}

static void rm_digest_paranoid_free(RmDigest *digest) {
    RmParanoid *paranoid = digest->state;
    if(paranoid->shadow_hash) {
        rm_digest_free(paranoid->shadow_hash);
    }
    rm_digest_release_buffers(digest);
    if(paranoid->incoming_twin_candidates) {
        g_async_queue_unref(paranoid->incoming_twin_candidates);
    }
    g_slist_free(paranoid->rejects);
    g_slice_free(RmParanoid, paranoid);
}

static void rm_digest_paranoid_steal(RmDigest *digest, guint8 *result) {
    RmParanoid *paranoid = digest->state;
    if(paranoid->shadow_hash) {
        guint8 *buf = rm_digest_steal(paranoid->shadow_hash);
        memcpy(result, buf, digest->bytes);
    } else {
        /* steal the first few bytes of the first buffer */
        if(paranoid->buffers) {
            RmBuffer *buffer = paranoid->buffers->data;
            memcpy(result, buffer->data, MIN(buffer->len, digest->bytes));
        }
    }
}


/* Note: paranoid update implementation is in rm_digest_buffered_update() below */

static const RmDigestSpec paranoid_spec = { "paranoid", 0, rm_digest_paranoid_init, rm_digest_paranoid_free, NULL, NULL, rm_digest_paranoid_steal};


////////////////////////////////
//      RmDigestSpec map      //
////////////////////////////////


static const RmDigestSpec *rm_digest_spec(RmDigestType type) {
    static const RmDigestSpec *digest_specs[] = {
        [RM_DIGEST_UNKNOWN]    = NULL,
        [RM_DIGEST_MURMUR]     = &murmur_spec,
        [RM_DIGEST_MD5]        = &md5_spec,
        [RM_DIGEST_SHA1]       = &sha1_spec,
        [RM_DIGEST_SHA256]     = &sha256_spec,
    #if HAVE_SHA512
        [RM_DIGEST_SHA512]     = &sha512_spec,
    #endif
        [RM_DIGEST_SHA3_256]   = &sha3_256_spec,
        [RM_DIGEST_SHA3_384]   = &sha3_384_spec,
        [RM_DIGEST_SHA3_512]   = &sha3_512_spec,
        [RM_DIGEST_BLAKE2S]    = &blake2s_spec,
        [RM_DIGEST_BLAKE2B]    = &blake2b_spec,
        [RM_DIGEST_BLAKE2SP]   = &blake2sp_spec,
        [RM_DIGEST_BLAKE2BP]   = &blake2bp_spec,
        [RM_DIGEST_EXT]        = &ext_spec,
        [RM_DIGEST_CUMULATIVE] = &cumulative_spec,
        [RM_DIGEST_PARANOID]   = &paranoid_spec,
        [RM_DIGEST_FARMHASH]   = &farmhash_spec,
        [RM_DIGEST_XXHASH]     = &xxhash_spec,
        [RM_DIGEST_HIGHWAY64]  = &highway64_spec,
        [RM_DIGEST_HIGHWAY128] = &highway128_spec,
        [RM_DIGEST_HIGHWAY256] = &highway256_spec,
    };

    if(type < RM_DIGEST_SENTINEL && digest_specs[type]) {
        return digest_specs[type];
    }
    rm_log_error_line("No digest spec for enum %i", type);
    g_assert_not_reached();
}

static void rm_digest_table_insert(GHashTable *code_table, char *name, RmDigestType type) {
    if(g_hash_table_contains(code_table, name)) {
        rm_log_error_line("Duplicate entry for %s in rm_init_digest_type_table()", name);
    }
    g_hash_table_insert(code_table, name, GUINT_TO_POINTER(type));
}

static gpointer rm_init_digest_type_table(GHashTable **code_table) {

    *code_table = g_hash_table_new(g_str_hash, g_str_equal);
	for(RmDigestType type=1; type<RM_DIGEST_SENTINEL; type++) {
        rm_digest_table_insert(*code_table, (char*)rm_digest_spec(type)->name, type);
	}

    /* add some synonyms */
    rm_digest_table_insert(*code_table, "sha3", RM_DIGEST_SHA3_256);
    rm_digest_table_insert(*code_table, "highway", RM_DIGEST_HIGHWAY256);

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
    const RmDigestSpec *spec = rm_digest_spec(type);
    return spec->name;
}

/*  TODO: remove? */
int rm_digest_type_to_multihash_id(RmDigestType type) {
    static int ids[] = {[RM_DIGEST_UNKNOWN] = -1,   [RM_DIGEST_MURMUR] = 17,
                        [RM_DIGEST_MD5] = 1,        [RM_DIGEST_SHA1] = 2,
                        [RM_DIGEST_SHA256] = 4,     [RM_DIGEST_SHA512] = 6,
                        [RM_DIGEST_EXT] = 12,       [RM_DIGEST_FARMHASH] = 19,
                        [RM_DIGEST_CUMULATIVE] = 13,[RM_DIGEST_PARANOID] = 14};

    return ids[MIN(type, sizeof(ids) / sizeof(ids[0]))];
}

RmDigest *rm_digest_new(RmDigestType type, RmOff seed1, RmOff seed2, RmOff ext_size,
                        bool use_shadow_hash) {
    g_assert(type != RM_DIGEST_UNKNOWN);

    const RmDigestSpec *spec = rm_digest_spec(type);
    RmDigest *digest = g_slice_new0(RmDigest);
    digest->type = type;
    digest->bytes = spec->bits / 8;
    spec->init(digest, seed1, seed2, ext_size, use_shadow_hash);

    return digest;
}

void rm_digest_paranoia_shrink(RmDigest *digest, gsize new_size) {
    rm_assert_gentle(digest->type == RM_DIGEST_PARANOID);
    digest->bytes = new_size;
}

void rm_digest_release_buffers(RmDigest *digest) {
    RmParanoid *paranoid = digest->state;
    if(paranoid && paranoid->buffers) {
        g_slist_free_full(paranoid->buffers, (GDestroyNotify)rm_buffer_free);
        paranoid->buffers = NULL;
    }
}

void rm_digest_free(RmDigest *digest) {
    const RmDigestSpec *spec = rm_digest_spec(digest->type);
    spec->free(digest);
    g_slice_free(RmDigest, digest);
}

void rm_digest_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    const RmDigestSpec *spec = rm_digest_spec(digest->type);
    spec->update(digest, data, size);
}

void rm_digest_buffered_update(RmBuffer *buffer) {
    rm_assert_gentle(buffer);
    RmDigest *digest = buffer->digest;
    if(digest->type != RM_DIGEST_PARANOID) {
        rm_digest_update(digest, buffer->data, buffer->len);
        rm_buffer_release(buffer);
    } else {
        RmParanoid *paranoid = digest->state;
        /* paranoid update... */
        if(!paranoid->buffers) {
            /* first buffer */
            paranoid->buffers = g_slist_prepend(NULL, buffer);
            paranoid->buffer_tail = paranoid->buffers;
        } else {
            paranoid->buffer_tail = g_slist_append(paranoid->buffer_tail, buffer)->next;
        }

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
            RmParanoid *twin = paranoid->twin_candidate->state;
            paranoid->twin_candidate_buffer = twin->buffers;
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

RmDigest *rm_digest_copy(RmDigest *digest) {
    rm_assert_gentle(digest);

    RmDigest *copy = g_slice_copy(sizeof(RmDigest), digest);

    const RmDigestSpec *spec = rm_digest_spec(digest->type);
    spec->copy(digest, copy);

    return copy;
}

guint8 *rm_digest_steal(RmDigest *digest) {

    const RmDigestSpec *spec = rm_digest_spec(digest->type);
    if(!spec->steal) {
        return g_slice_copy(digest->bytes, digest->state);
    }

    guint8 *result = g_slice_alloc0(digest->bytes);
    spec->steal(digest, result);
    return result;
}

guint rm_digest_hash(RmDigest *digest) {
    guint8 *buf = NULL;
    gsize bytes = 0;
    guint hash = 0;

    buf = rm_digest_steal(digest);
    bytes = digest->bytes;

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

    const RmDigestSpec *spec = rm_digest_spec(a->type);

    if(a->type == RM_DIGEST_PARANOID) {
        RmParanoid *pa = a->state;
        RmParanoid *pb = b->state;
        if(!pa->buffers) {
            /* buffers have been freed so we need to rely on shadow hash */
            return rm_digest_equal(pa->shadow_hash, pb->shadow_hash);
        }
        /* check if pre-matched twins */
        if(pa->twin_candidate == b || pb->twin_candidate == a) {
            return true;
        }
        /* check if already rejected */
        if(g_slist_find(pa->rejects, b) ||
           g_slist_find(pb->rejects, a)) {
            return false;
        }
        /* all the "easy" ways failed... do manual check of all buffers */
        GSList *a_iter = pa->buffers;
        GSList *b_iter = pb->buffers;
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

        return (!a_iter && !b_iter);
    } else if(spec->steal) {
        guint8 *buf_a = rm_digest_steal(a);
        guint8 *buf_b = rm_digest_steal(b);
        gboolean result = !memcmp(buf_a, buf_b, a->bytes);

        g_slice_free1(a->bytes, buf_a);
        g_slice_free1(b->bytes, buf_b);

        return result;
    } else {
        return !memcmp(a->state, b->state, a->bytes);
    }
}

int rm_digest_hexstring(RmDigest *digest, char *buffer) {
    static const char *hex = "0123456789abcdef";
    if(digest == NULL) {
        return 0;
    }

    guint8 *input = rm_digest_steal(digest);
    gsize bytes = digest->bytes;
    gsize out = 0;

    for(gsize i = 0; i < bytes; ++i) {
        buffer[out++] = hex[input[i] / 16];
        buffer[out++] = hex[input[i] % 16];
    }
    buffer[out++] = '\0';

    g_slice_free1(bytes, input);
    return out;
}

int rm_digest_get_bytes(RmDigest *self) {
    if(self == NULL) {
        return 0;
    }

    return self->bytes;
}

void rm_digest_send_match_candidate(RmDigest *target, RmDigest *candidate) {
    RmParanoid *paranoid = target->state;
    
    if(!paranoid->incoming_twin_candidates) {
        paranoid->incoming_twin_candidates = g_async_queue_new();
    }
    g_async_queue_push(paranoid->incoming_twin_candidates, candidate);
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
