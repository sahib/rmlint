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

/* This file is mostly boring interface definitions to conform all of the
 * difference hash types to a single interface.
 *  code except for the paranoid digest
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
#include "checksums/highwayhash.h"
#include "checksums/metrohash.h"
#include "checksums/murmur3.h"
#include "checksums/sha3/sha3.h"
#include "checksums/xxhash/xxhash.h"

#include "utilities.h"

#define _RM_CHECKSUM_DEBUG 0

static int RM_DIGEST_USE_SSE = 0;

//////////////////////////////////
//    BUFFER IMPLEMENTATION     //
//////////////////////////////////

RmSemaphore *rm_semaphore_new(int n) {
    RmSemaphore *self = g_malloc0(sizeof(RmSemaphore));
    g_mutex_init(&self->sem_lock);
    g_cond_init(&self->sem_cond);
    self->n = n;
    return self;
}

void rm_semaphore_destroy(RmSemaphore *sem) {
    g_cond_clear(&sem->sem_cond);
    g_mutex_clear(&sem->sem_lock);
    g_free(sem);
}

void rm_semaphore_acquire(RmSemaphore *sem) {
   g_mutex_lock(&sem->sem_lock);
   while(sem->n == 0) {
        g_cond_wait(&sem->sem_cond, &sem->sem_lock);
   }
   --sem->n;
   g_mutex_unlock(&sem->sem_lock);
}

void rm_semaphore_release(RmSemaphore *sem) {
   g_mutex_lock(&sem->sem_lock);
   ++sem->n;
   g_mutex_unlock(&sem->sem_lock);
   g_cond_signal(&sem->sem_cond);
}

////////////////////

RmBuffer *rm_buffer_new(RmSemaphore *sem, gsize buf_size) {
    /* NOTE: Here is a catch:
     *
     * We should only allocate a buffer if we do not surpass
     * a certain number of buffers in memory. If the filesystem
     * is faster than the CPU is able to hash the input, we might
     * slowly allocate too many buffers, causing memory issues.
     *
     * Therefore we'll block here until another thread releases
     * its buffers using rm_buffer_free. This of course means that
     * the logic regarding buffer freeing must be very tough, since
     * we might risk deadlocks otherwise.
     *
     * This was discovered as part of this issue:
     *
     *  https://github.com/sahib/rmlint/issues/309
     *
     * The semaphore is not used in paranoia mode.
     */
    if(sem != NULL) {
        rm_semaphore_acquire(sem);
    }

    RmBuffer *self = g_slice_new0(RmBuffer);
    self->data = g_slice_alloc(buf_size);
    self->buf_size = buf_size;
    return self;
}

void rm_buffer_free(RmSemaphore *sem, RmBuffer *buf) {
    /*  See the explanation in rm_buffer_new */
    if(sem != NULL) {
        rm_semaphore_release(sem);
    }

    g_slice_free1(buf->buf_size, buf->data);
    g_slice_free(RmBuffer, buf);
}

static gboolean rm_buffer_equal(RmBuffer *a, RmBuffer *b) {
    return (a->len == b->len && memcmp(a->data, b->data, a->len) == 0);
}

///////////////////////////////////////
//  RMDIGEST INTERFACE DEFINITIONS   //
///////////////////////////////////////

/* Each digest type must have an RmDigestInterface defined as follows: */
typedef gpointer (*RmDigestNewFunc)(void);
typedef void (*RmDigestFreeFunc)(gpointer state);
typedef void (*RmDigestUpdateFunc)(gpointer state, const unsigned char *data, size_t size);
typedef gpointer (*RmDigestCopyFunc)(gpointer state);
typedef void (*RmDigestStealFunc)(gpointer state, guint8 *result);
typedef guint (*RmDigestLenFunc)(gpointer state);

typedef struct RmDigestInterface {
    const char *name;           // hash name
    const guint bits;           // length of the output checksum in bits (if const)
    RmDigestLenFunc len;        // return length of the output checksum in bytes
    RmDigestNewFunc new;        // returns new digest->state
    RmDigestFreeFunc free;      // frees state allocated by new()
    RmDigestUpdateFunc update;  // hashes data into state
    RmDigestCopyFunc copy;      // allocates and returns a copy of passed state
    RmDigestStealFunc steal;    // writes checksum (as binary) to *result
} RmDigestInterface;

///////////////////////////
//   xxhash interface    //
///////////////////////////

static XXH64_state_t *rm_digest_xxhash_new(void) {
    XXH64_state_t *state = XXH64_createState();
    XXH64_reset(state, 0);
    return state;
}

static XXH64_state_t *rm_digest_xxhash_copy(XXH64_state_t *state) {
    XXH64_state_t *copy = XXH64_createState();
    memcpy(copy, state, sizeof(XXH64_state_t));
    return copy;
}

static void rm_digest_xxhash_steal(gpointer state, guint8 *result) {
    *(unsigned long long *)result = XXH64_digest(state);
}

static const RmDigestInterface xxhash_interface = {
    .name = "xxhash",
    .bits = 64,
    .len = NULL,
    .new = (RmDigestNewFunc)rm_digest_xxhash_new,
    .free = (RmDigestFreeFunc)XXH64_freeState,
    .update = (RmDigestUpdateFunc)XXH64_update,
    .copy = (RmDigestCopyFunc)rm_digest_xxhash_copy,
    .steal = rm_digest_xxhash_steal};

///////////////////////////
//        murmur         //
///////////////////////////

static const RmDigestInterface murmur_interface = {
    .name = "murmur",
    .bits = 128,
    .len = NULL,
#if RM_PLATFORM_32
    .new = (RmDigestNewFunc)MurmurHash3_x86_128_new,
    .free = (RmDigestFreeFunc)MurmurHash3_x86_128_free,
    .update = (RmDigestUpdateFunc)MurmurHash3_x86_128_update,
    .copy = (RmDigestCopyFunc)MurmurHash3_x86_128_copy,
    .steal = (RmDigestStealFunc)MurmurHash3_x86_128_steal,
#elif RM_PLATFORM_64
    /* use 64-bit optimised murmur hash interface */
    .new = (RmDigestNewFunc)MurmurHash3_x64_128_new,
    .free = (RmDigestFreeFunc)MurmurHash3_x64_128_free,
    .update = (RmDigestUpdateFunc)MurmurHash3_x64_128_update,
    .copy = (RmDigestCopyFunc)MurmurHash3_x64_128_copy,
    .steal = (RmDigestStealFunc)MurmurHash3_x64_128_steal,
#else
#error "Probably not a good idea to compile rmlint on 16bit."
#endif
};

///////////////////////////
//         metro         //
///////////////////////////

static Metro128State *rm_digest_metro_new(void) {
    return metrohash128_1_new(FALSE);
}

static Metro256State *rm_digest_metro256_new(void) {
    return metrohash256_new(FALSE);
}

static const RmDigestInterface metro_interface = {
    .name = "metro",
    .bits = 128,
    .len = NULL,
    .new = (RmDigestNewFunc)rm_digest_metro_new,
    .free = (RmDigestFreeFunc)metrohash128_free,
    .update = (RmDigestUpdateFunc)metrohash128_1_update,
    .copy = (RmDigestCopyFunc)metrohash128_copy,
    .steal = (RmDigestStealFunc)metrohash128_1_steal};

static const RmDigestInterface metro256_interface = {
    .name = "metro256",
    .bits = 256,
    .len = NULL,
    .new = (RmDigestNewFunc)rm_digest_metro256_new,
    .free = (RmDigestFreeFunc)metrohash256_free,
    .update = (RmDigestUpdateFunc)metrohash256_update,
    .copy = (RmDigestCopyFunc)metrohash256_copy,
    .steal = (RmDigestStealFunc)metrohash256_steal};

#if HAVE_MM_CRC32_U64
/* also define crc-optimised metro variants metrocrc and metrocrc256*/

static Metro128State *rm_digest_metrocrc_new(void) {
    return metrohash128_1_new(g_atomic_int_get(&RM_DIGEST_USE_SSE));
}

static Metro256State *rm_digest_metrocrc256_new(void) {
    return metrohash256_new(g_atomic_int_get(&RM_DIGEST_USE_SSE));
}

static const RmDigestInterface metrocrc_interface = {
    .name = "metrocrc",
    .bits = 128,
    .len = NULL,
    .new = (RmDigestNewFunc)rm_digest_metrocrc_new,
    .free = (RmDigestFreeFunc)metrohash128_free, /* <-same */
    .update = (RmDigestUpdateFunc)metrohash128crc_1_update,
    .copy = (RmDigestCopyFunc)metrohash128_copy, /* <-same */
    .steal = (RmDigestStealFunc)metrohash128crc_1_steal};

static const RmDigestInterface metrocrc256_interface = {
    .name = "metrocrc256",
    .bits = 256,
    .len = NULL,
    .new = (RmDigestNewFunc)rm_digest_metrocrc256_new,
    .free = (RmDigestFreeFunc)metrohash256_free, /* <-same */
    .update = (RmDigestUpdateFunc)metrohash256crc_update,
    .copy = (RmDigestCopyFunc)metrohash256_copy, /* <-same */
    .steal = (RmDigestStealFunc)metrohash256crc_steal};

#endif

///////////////////////////
//      cumulative       //
///////////////////////////

#define RM_DIGEST_CUMULATIVE_MAX_BYTES 64

#if RM_PLATFORM_64

#define RM_DIGEST_CUMULATIVE_T guint64
#define RM_DIGEST_CUMULATIVE_DATA data64
#define RM_DIGEST_CUMULATIVE_ALIGN 8

#else

#define RM_DIGEST_CUMULATIVE_T guint32
#define RM_DIGEST_CUMULATIVE_DATA data32
#define RM_DIGEST_CUMULATIVE_ALIGN 4

#endif

#define RM_DIGEST_CUMULATIVE_INTS (RM_DIGEST_CUMULATIVE_LEN / RM_DIGEST_CUMULATIVE_ALIGN)

typedef struct RmDigestCumulative {
    union {
        guint8 *data;
        RM_DIGEST_CUMULATIVE_T *bigdata;
    };
    RM_DIGEST_CUMULATIVE_T bytes; /* data length */
    RM_DIGEST_CUMULATIVE_T pos; /* byte offset within data */
} RmDigestCumulative;

static guint rm_digest_cumulative_len(RmDigestCumulative *state) {
    return state->bytes;
}

static RmDigestCumulative *rm_digest_cumulative_new(void) {
    return g_slice_new0(RmDigestCumulative);
}

static void rm_digest_cumulative_free(RmDigestCumulative *state) {
    if(state->data) {
        g_slice_free1(state->bytes, state->data);
    }
    g_slice_free(RmDigestCumulative, state);
}

static void rm_digest_cumulative_update(RmDigestCumulative *state,
                                        const unsigned char *data, size_t size) {
    if(!state->data) {
        /* first update sets checksum length */
        state->bytes = RM_DIGEST_CUMULATIVE_ALIGN * CLAMP(size / RM_DIGEST_CUMULATIVE_ALIGN, 1, RM_DIGEST_CUMULATIVE_MAX_BYTES / RM_DIGEST_CUMULATIVE_ALIGN);
        state->data = g_slice_alloc0(state->bytes);
    }
        
    guint8 *ptr = (guint8 *)data;
    guint8 *stop = ptr + size;

    /* align so we can use [32|64]-bit xor */
    while((state->pos % RM_DIGEST_CUMULATIVE_ALIGN != 0) && ptr < stop) {
        state->data[state->pos++] ^= *(ptr++);
        if(state->pos == state->bytes) {
            state->pos = 0;
        }
    }

    RM_DIGEST_CUMULATIVE_T *ptr_big = (RM_DIGEST_CUMULATIVE_T *)ptr;
    RM_DIGEST_CUMULATIVE_T *stop_big =
        (RM_DIGEST_CUMULATIVE_T *)(stop + 1 - RM_DIGEST_CUMULATIVE_ALIGN);

    /* plough through body of data efficiently */
    while(ptr_big < stop_big) {
        state->bigdata[state->pos / RM_DIGEST_CUMULATIVE_ALIGN] ^= *ptr_big++;
        state->pos = state->pos + RM_DIGEST_CUMULATIVE_ALIGN;
        if(state->pos == state->bytes) {
            state->pos = 0;
        }
    }

    /* process remaining date byte-wise */
    ptr = (guint8 *)ptr_big;
    while(ptr < stop) {
        state->data[state->pos++] ^= *(ptr++);
        if(state->pos == state->bytes) {
            state->pos = 0;
        }
    }
}

static RmDigestCumulative *rm_digest_cumulative_copy(RmDigestCumulative *state) {
    RmDigestCumulative *copy = g_slice_copy(sizeof(RmDigestCumulative), state);
    copy->data = g_slice_copy(state->bytes, state->data);
    return copy;
}

static void rm_digest_cumulative_steal(RmDigestCumulative *state, guint8 *result) {
    memcpy(result, state->data, state->bytes);
}

static const RmDigestInterface cumulative_interface = {
    .name = "cumulative",
    .bits = 0,
    .len = (RmDigestLenFunc)rm_digest_cumulative_len,
    .new = (RmDigestNewFunc)rm_digest_cumulative_new,
    .free = (RmDigestFreeFunc)rm_digest_cumulative_free,
    .update = (RmDigestUpdateFunc)rm_digest_cumulative_update,
    .copy = (RmDigestCopyFunc)rm_digest_cumulative_copy,
    .steal = (RmDigestStealFunc)rm_digest_cumulative_steal};

///////////////////////////
//     highway hash      //
///////////////////////////

static HighwayHashCat *rm_digest_highway_new(void) {
    HighwayHashCat *state = g_slice_new(HighwayHashCat);
    static const uint64_t key[4] = {1, 2, 3, 4};
    HighwayHashCatStart(key, state);
    return state;
}

static void rm_digest_highway_free(HighwayHashCat *state) {
    g_slice_free(HighwayHashCat, state);
}

static void rm_digest_highway_update(HighwayHashCat *state, const unsigned char *data,
                                     RmOff size) {
    HighwayHashCatAppend((const uint8_t *)data, size, state);
}

static HighwayHashCat *rm_digest_highway_copy(HighwayHashCat *state) {
    return g_slice_copy(sizeof(HighwayHashCat), state);
}

static void rm_digest_highway64_steal(HighwayHashCat *state, guint8 *result) {
    /* HighwayHashCatFinish functions are non-destructive so steal funcs don't
     * need to make a copy */
    *(uint64_t *)result = HighwayHashCatFinish64(state);
}

static const RmDigestInterface highway64_interface = {
    .name = "highway64",
    .bits = 64,
    .len = NULL,
    .new = (RmDigestNewFunc)rm_digest_highway_new,
    .free = (RmDigestFreeFunc)rm_digest_highway_free,
    .update = (RmDigestUpdateFunc)rm_digest_highway_update,
    .copy = (RmDigestCopyFunc)rm_digest_highway_copy,
    .steal = (RmDigestStealFunc)rm_digest_highway64_steal};

static const RmDigestInterface highway128_interface = {
    .name = "highway128",
    .bits = 128,
    .len = NULL,
    .new = (RmDigestNewFunc)rm_digest_highway_new,
    .free = (RmDigestFreeFunc)rm_digest_highway_free,
    .update = (RmDigestUpdateFunc)rm_digest_highway_update,
    .copy = (RmDigestCopyFunc)rm_digest_highway_copy,
    .steal = (RmDigestStealFunc)HighwayHashCatFinish128};

static const RmDigestInterface highway256_interface = {
    .name = "highway256",
    .bits = 256,
    .len = NULL,
    .new = (RmDigestNewFunc)rm_digest_highway_new,
    .free = (RmDigestFreeFunc)rm_digest_highway_free,
    .update = (RmDigestUpdateFunc)rm_digest_highway_update,
    .copy = (RmDigestCopyFunc)rm_digest_highway_copy,
    .steal = (RmDigestStealFunc)HighwayHashCatFinish256};

///////////////////////////
//      glib hashes      //
///////////////////////////

static void rm_digest_glib_steal(GChecksum *state, guint8 *result, gsize *len) {
    GChecksum *copy = g_checksum_copy(state);
    g_checksum_get_digest(copy, result, len);
    g_checksum_free(copy);
}

#define RM_DIGEST_DEFINE_GLIB(NAME, BITS)                \
    static const RmDigestInterface NAME##_interface = {  \
        .name = #NAME,                                   \
        .bits = BITS,                                    \
        .len = NULL,                                     \
        .new = (RmDigestNewFunc)rm_digest_##NAME##_new,  \
        .free = (RmDigestFreeFunc)g_checksum_free,       \
        .update = (RmDigestUpdateFunc)g_checksum_update, \
        .copy = (RmDigestCopyFunc)g_checksum_copy,       \
        .steal = (RmDigestStealFunc)rm_digest_##NAME##_steal};

/* md5 */
static GChecksum *rm_digest_md5_new(void) {
    return g_checksum_new(G_CHECKSUM_MD5);
}

static void rm_digest_md5_steal(GChecksum *state, guint8 *result) {
    gsize len = 16;
    rm_digest_glib_steal(state, result, &len);
}
RM_DIGEST_DEFINE_GLIB(md5, 128);

/* sha1 */
static GChecksum *rm_digest_sha1_new(void) {
    return g_checksum_new(G_CHECKSUM_SHA1);
}

static void rm_digest_sha1_steal(GChecksum *state, guint8 *result) {
    gsize len = 20;
    rm_digest_glib_steal(state, result, &len);
}

RM_DIGEST_DEFINE_GLIB(sha1, 160);

/* sha256 */
static GChecksum *rm_digest_sha256_new(void) {
    return g_checksum_new(G_CHECKSUM_SHA256);
}

static void rm_digest_sha256_steal(GChecksum *state, guint8 *result) {
    gsize len = 32;
    rm_digest_glib_steal(state, result, &len);
}

RM_DIGEST_DEFINE_GLIB(sha256, 256);

#if HAVE_SHA512

/* sha512 */
static GChecksum *rm_digest_sha512_new(void) {
    return g_checksum_new(G_CHECKSUM_SHA512);
}

static void rm_digest_sha512_steal(GChecksum *state, guint8 *result) {
    gsize len = 64;
    rm_digest_glib_steal(state, result, &len);
}
RM_DIGEST_DEFINE_GLIB(sha512, 512);

#endif

///////////////////////////
//      sha3 hashes      //
///////////////////////////

static sha3_context *rm_digest_sha3_256_new(void) {
    sha3_context *state = g_slice_new(sha3_context);
    sha3_Init256(state);
    return state;
}

static sha3_context *rm_digest_sha3_384_new(void) {
    sha3_context *state = g_slice_new(sha3_context);
    sha3_Init384(state);
    return state;
}

static sha3_context *rm_digest_sha3_512_new(void) {
    sha3_context *state = g_slice_new(sha3_context);
    sha3_Init512(state);
    return state;
}

static void rm_digest_sha3_free(sha3_context *state) {
    g_slice_free(sha3_context, state);
}

static sha3_context *rm_digest_sha3_copy(sha3_context *state) {
    return g_slice_copy(sizeof(sha3_context), state);
}

static void rm_digest_sha3_256_steal(sha3_context *state, guint8 *result) {
    sha3_context *copy = g_slice_copy(sizeof(sha3_context), state);
    memcpy(result, sha3_Finalize(copy), 256 / 8);
    rm_digest_sha3_free(copy);
}

static void rm_digest_sha3_384_steal(sha3_context *state, guint8 *result) {
    sha3_context *copy = g_slice_copy(sizeof(sha3_context), state);
    memcpy(result, sha3_Finalize(copy), 384 / 8);
    rm_digest_sha3_free(copy);
}

static void rm_digest_sha3_512_steal(sha3_context *state, guint8 *result) {
    sha3_context *copy = g_slice_copy(sizeof(sha3_context), state);
    memcpy(result, sha3_Finalize(copy), 512 / 8);
    rm_digest_sha3_free(copy);
}

#define RM_DIGEST_DEFINE_SHA3(BITS)                            \
    static const RmDigestInterface sha3_##BITS##_interface = { \
        .name = ("sha3-" #BITS),                               \
        .bits = BITS, \
        .len = NULL, \
        .new = (RmDigestNewFunc)rm_digest_sha3_##BITS##_new,   \
        .free = (RmDigestFreeFunc)rm_digest_sha3_free,         \
        .update = (RmDigestUpdateFunc)sha3_Update,             \
        .copy = (RmDigestCopyFunc)rm_digest_sha3_copy,         \
        .steal = (RmDigestStealFunc)rm_digest_sha3_##BITS##_steal};

RM_DIGEST_DEFINE_SHA3(256)
RM_DIGEST_DEFINE_SHA3(384)
RM_DIGEST_DEFINE_SHA3(512)

///////////////////////////
//      blake hashes     //
///////////////////////////



#define CREATE_BLAKE_INTERFACE(ALGO, ALGO_BIG)                                  \
                                                                                \
    static ALGO##_state *rm_digest_##ALGO##_new(void) {                         \
        ALGO##_state *state = g_slice_new(ALGO##_state);                        \
        ALGO##_init(state, ALGO_BIG##_OUTBYTES);                                \
        return state;                                                           \
    }                                                                           \
                                                                                \
    static void rm_digest_##ALGO##_free(ALGO##_state *state) {                  \
        g_slice_free(ALGO##_state, state);                                      \
    }                                                                           \
                                                                                \
    static ALGO##_state *rm_digest_##ALGO##_copy(ALGO##_state *state) {         \
        return g_slice_copy(sizeof(ALGO##_state), state);                       \
    }                                                                           \
                                                                                \
    static void rm_digest_##ALGO##_steal(ALGO##_state *state, guint8 *result) { \
        ALGO##_state *copy = rm_digest_##ALGO##_copy(state);                    \
        ALGO##_final(copy, result, ALGO_BIG##_OUTBYTES);                        \
        rm_digest_##ALGO##_free(copy);                                          \
    }                                                                           \
                                                                                \
    static const RmDigestInterface ALGO##_interface = {                         \
        .name = #ALGO,                                                          \
        .bits = 8 * ALGO_BIG##_OUTBYTES,                                        \
        .len = NULL,                                                            \
        .new = (RmDigestNewFunc)rm_digest_##ALGO##_new,                         \
        .free = (RmDigestFreeFunc)rm_digest_##ALGO##_free,                      \
        .update = (RmDigestUpdateFunc)ALGO##_update,                            \
        .copy = (RmDigestCopyFunc)rm_digest_##ALGO##_copy,                      \
        .steal = (RmDigestStealFunc)rm_digest_##ALGO##_steal};

CREATE_BLAKE_INTERFACE(blake2b, BLAKE2B);
CREATE_BLAKE_INTERFACE(blake2bp, BLAKE2B);
CREATE_BLAKE_INTERFACE(blake2s, BLAKE2S);
CREATE_BLAKE_INTERFACE(blake2sp, BLAKE2S);

///////////////////////////
//      ext  hash        //
///////////////////////////

typedef struct RmDigestExt {
    guint8 len;
    guint8 *data;
} RmDigestExt;

static guint rm_digest_ext_len(RmDigestExt *state) {
    return state->len;
}

static RmDigestExt *rm_digest_ext_new(void) {
    return g_slice_new0(RmDigestExt);
}

static void rm_digest_ext_free_data(RmDigestExt *state) {
    if(state->data) {
        g_slice_free1(state->len, state->data);
    }
}

static void rm_digest_ext_free(RmDigestExt *state) {
    rm_digest_ext_free_data(state);
    g_slice_free(RmDigestExt, state);
}

static void rm_digest_ext_update(RmDigestExt *state, const unsigned char *data,
                                 RmOff size) {
/* Data is assumed to be a hex representation of a checksum.
 * Needs to be compressed in pure memory first.
 *
 * Checksum is not updated but rather overwritten.
 * */
#define CHAR_TO_NUM(c) (unsigned char)(g_ascii_isdigit(c) ? c - '0' : (c - 'a') + 10)

    if(state->data) {
        rm_digest_ext_free_data(state);
    }

    state->len = size / 2;
    state->data = g_slice_alloc(state->len);

    for(unsigned i = 0; i < state->len; ++i) {
        state->data[i] = (CHAR_TO_NUM(data[2 * i]) << 4) + CHAR_TO_NUM(data[2 * i + 1]);
    }
}

static RmDigestExt *rm_digest_ext_copy(RmDigestExt *state) {
    RmDigestExt *copy = g_slice_copy(sizeof(RmDigestExt), state);
    copy->data = g_slice_copy(state->len, state->data);
    return copy;
}

static void rm_digest_ext_steal(RmDigestExt *state, guint8 *result) {
    memcpy(result, state->data, state->len);
}

static const RmDigestInterface ext_interface = {
    .name = "ext",
    .bits = 0,
    .len = (RmDigestLenFunc) rm_digest_ext_len,
    .new = (RmDigestNewFunc)rm_digest_ext_new,
    .free = (RmDigestFreeFunc)rm_digest_ext_free,
    .update = (RmDigestUpdateFunc)rm_digest_ext_update,
    .copy = (RmDigestCopyFunc)rm_digest_ext_copy,
    .steal = (RmDigestStealFunc)rm_digest_ext_steal};

///////////////////////////
//     paranoid 'hash'   //
///////////////////////////

static RmParanoid *rm_digest_paranoid_new(void) {
    RmParanoid *paranoid = g_slice_new0(RmParanoid);
    paranoid->incoming_twin_candidates = g_async_queue_new();
    paranoid->shadow_hash = rm_digest_new(RM_DIGEST_XXHASH, 0);
    return paranoid;
}

static void rm_buffer_destroy_notify_func(gpointer data) {
    rm_buffer_free(NULL, data);
}

static void rm_digest_paranoid_release_buffers(RmParanoid *paranoid) {
    g_slist_free_full(paranoid->buffers, (GDestroyNotify)rm_buffer_destroy_notify_func);
    paranoid->buffers = NULL;
}

static RmParanoid *rm_digest_paranoid_copy(RmParanoid *paranoid) {
    RmParanoid *copy = g_slice_new0(RmParanoid);
    if(paranoid->shadow_hash != NULL) {
        copy->shadow_hash = rm_digest_copy(paranoid->shadow_hash);
    }
    return copy;
}

static void rm_digest_paranoid_free(RmParanoid *paranoid) {
    rm_digest_free(paranoid->shadow_hash);
    rm_digest_paranoid_release_buffers(paranoid);
    g_async_queue_unref(paranoid->incoming_twin_candidates);
    g_slist_free(paranoid->rejects);
    g_slice_free(RmParanoid, paranoid);
}

static void rm_digest_paranoid_buffered_update(RmParanoid *paranoid, RmBuffer *buffer) {
    /* Welcome to hell!
     * This is a somewhat crazy part of the rmlint optimisation strategy.
     * Comparing two "paranoid digests" (basically a large chunk of a file stored in
     * a series of buffers) is fairly simple but it's slow because it has to compare
     * each buffer.
     * The algorithm below tries to get a head-start on the comparison by starting the
     * buffer comparison before the last buffer has been read.
     */

    rm_digest_update(paranoid->shadow_hash, buffer->data, buffer->len);

    if(!paranoid->buffers) {
        /* first buffer */
        paranoid->buffers = g_slist_prepend(NULL, buffer);
        paranoid->buffer_tail = paranoid->buffers;
    } else {
        paranoid->buffer_tail = g_slist_append(paranoid->buffer_tail, buffer)->next;
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

    while(!paranoid->twin_candidate && (paranoid->twin_candidate = g_async_queue_try_pop(
                                            paranoid->incoming_twin_candidates))) {
        /* validate the new candidate by comparing the previous buffers (not
         * including current)*/
        RmParanoid *twin = paranoid->twin_candidate->state;
        paranoid->twin_candidate_buffer = twin->buffers;
        GSList *iter_self = paranoid->buffers;
        gboolean match = TRUE;
        while(match && iter_self) {
            match =
                (rm_buffer_equal(paranoid->twin_candidate_buffer->data, iter_self->data));
            iter_self = iter_self->next;
            paranoid->twin_candidate_buffer = paranoid->twin_candidate_buffer->next;
        }
        if(paranoid->twin_candidate && !match) {
        /* reject the twin candidate, also add to rejects list to speed up
         * rm_digest_equal() */
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
            rm_log_debug_line("Added twin candidate %p for %p", paranoid->twin_candidate,
                              paranoid);
#endif
        }
    }
}

static void rm_digest_paranoid_steal(RmParanoid *paranoid, guint8 *result) {
    RmDigest *shadow_hash = paranoid->shadow_hash;
    rm_digest_xxhash_steal(shadow_hash->state, result);
}

/* Note: paranoid update implementation is in rm_digest_buffered_update() below */

static const RmDigestInterface paranoid_interface = {
    .name = "paranoid",
    .bits = 64, /* must match shadow hash length */
    .len = NULL,
    .new = (RmDigestNewFunc)rm_digest_paranoid_new,
    .free = (RmDigestFreeFunc)rm_digest_paranoid_free,
    .update = NULL,
    .copy = (RmDigestCopyFunc)rm_digest_paranoid_copy,
    .steal = (RmDigestStealFunc)rm_digest_paranoid_steal};

////////////////////////////////
//   RmDigestInterface map    //
////////////////////////////////

static const RmDigestInterface *rm_digest_get_interface(RmDigestType type) {
    static const RmDigestInterface *digest_interfaces[] = {
        [RM_DIGEST_MURMUR] = &murmur_interface,
        [RM_DIGEST_METRO] = &metro_interface,
        [RM_DIGEST_METRO256] = &metro256_interface,
#if HAVE_MM_CRC32_U64
        [RM_DIGEST_METROCRC] = &metrocrc_interface,
        [RM_DIGEST_METROCRC256] = &metrocrc256_interface,
#endif
        [RM_DIGEST_MD5] = &md5_interface,
        [RM_DIGEST_SHA1] = &sha1_interface,
        [RM_DIGEST_SHA256] = &sha256_interface,
#if HAVE_SHA512
        [RM_DIGEST_SHA512] = &sha512_interface,
#endif
        [RM_DIGEST_SHA3_256] = &sha3_256_interface,
        [RM_DIGEST_SHA3_384] = &sha3_384_interface,
        [RM_DIGEST_SHA3_512] = &sha3_512_interface,
        [RM_DIGEST_BLAKE2S] = &blake2s_interface,
        [RM_DIGEST_BLAKE2B] = &blake2b_interface,
        [RM_DIGEST_BLAKE2SP] = &blake2sp_interface,
        [RM_DIGEST_BLAKE2BP] = &blake2bp_interface,
        [RM_DIGEST_EXT] = &ext_interface,
        [RM_DIGEST_CUMULATIVE] = &cumulative_interface,
        [RM_DIGEST_PARANOID] = &paranoid_interface,
        [RM_DIGEST_XXHASH] = &xxhash_interface,
        [RM_DIGEST_HIGHWAY64] = &highway64_interface,
        [RM_DIGEST_HIGHWAY128] = &highway128_interface,
        [RM_DIGEST_HIGHWAY256] = &highway256_interface,
    };

    g_assert(type < RM_DIGEST_SENTINEL);
    g_assert(type != RM_DIGEST_UNKNOWN);
    g_assert(digest_interfaces[type]);

    return digest_interfaces[type];
}

static void rm_digest_table_insert(GHashTable *code_table, char *name,
                                   RmDigestType type) {
    g_assert(!g_hash_table_contains(code_table, name));
    g_hash_table_insert(code_table, name, GUINT_TO_POINTER(type));
}

static gpointer rm_init_digest_type_table(GHashTable **code_table) {
    *code_table = g_hash_table_new(g_str_hash, g_str_equal);
    for(RmDigestType type = 1; type < RM_DIGEST_SENTINEL; type++) {
        rm_digest_table_insert(*code_table, (char *)rm_digest_get_interface(type)->name,
                               type);
    }

    /* add some synonyms */
    rm_digest_table_insert(*code_table, "sha3", RM_DIGEST_SHA3_256);
    rm_digest_table_insert(*code_table, "highway", RM_DIGEST_HIGHWAY256);

    return NULL;
}

///////////////////////////////////////
//           RMDIGEST API            //
///////////////////////////////////////

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
    const RmDigestInterface *interface = rm_digest_get_interface(type);
    return interface->name;
}

RmDigest *rm_digest_new(RmDigestType type, RmOff seed) {
    const RmDigestInterface *interface = rm_digest_get_interface(type);

    RmDigest *digest = g_slice_new0(RmDigest);
    digest->type = type;
    digest->bytes = interface->bits / 8;
    digest->state = interface->new();
    if(seed) {
        interface->update(digest->state, (const unsigned char *)&seed, sizeof(seed));
    }

    return digest;
}

void rm_digest_release_buffers(RmDigest *digest) {
    g_assert(digest->type == RM_DIGEST_PARANOID);
    rm_digest_paranoid_release_buffers(digest->state);
}

void rm_digest_free(RmDigest *digest) {
    const RmDigestInterface *interface = rm_digest_get_interface(digest->type);
    interface->free(digest->state);
    g_slice_free(RmDigest, digest);
}

void rm_digest_update(RmDigest *digest, const unsigned char *data, RmOff size) {
    const RmDigestInterface *interface = rm_digest_get_interface(digest->type);
    interface->update(digest->state, data, size);
    if(digest->bytes == 0) {
        digest->bytes = interface->len(digest->state);
    }
}

void rm_digest_buffered_update(RmSemaphore *sem, RmBuffer *buffer) {
    g_assert(buffer);
    RmDigest *digest = buffer->digest;
    if(digest->type != RM_DIGEST_PARANOID) {
        rm_digest_update(digest, buffer->data, buffer->len);
        rm_buffer_free(sem, buffer);
    } else {
        RmParanoid *paranoid = digest->state;
        rm_digest_paranoid_buffered_update(paranoid, buffer);
    }
}

RmDigest *rm_digest_copy(RmDigest *digest) {
    g_assert(digest);

    RmDigest *copy = g_slice_copy(sizeof(RmDigest), digest);

    const RmDigestInterface *interface = rm_digest_get_interface(digest->type);
    if(interface->copy == NULL) {
        return NULL;
    }

    copy->state = interface->copy(digest->state);
    return copy;
}

guint8 *rm_digest_steal(RmDigest *digest) {
    const RmDigestInterface *interface = rm_digest_get_interface(digest->type);
    guint8 *result = g_slice_alloc0(digest->bytes);
    interface->steal(digest->state, result);
    
    return result;
}

guint rm_digest_hash(RmDigest *digest) {
    guint8 *buf = NULL;
    gsize bytes = 0;
    guint hash = 0;

    buf = rm_digest_steal(digest);
    bytes = digest->bytes;

    if(buf != NULL) {
        g_assert(bytes >= sizeof(guint));
        hash = *(guint *)buf;
        g_slice_free1(bytes, buf);
    }
    return hash;
}

gboolean rm_digest_equal(RmDigest *a, RmDigest *b) {
    g_assert(a);
    g_assert(b);

    if(a->type != b->type) {
        return false;
    }

    if(a->bytes != b->bytes) {
        return false;
    }

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
        if(g_slist_find(pa->rejects, b) || g_slist_find(pb->rejects, a)) {
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
    } else {
        guint8 *buf_a = rm_digest_steal(a);
        guint8 *buf_b = rm_digest_steal(b);
        gboolean result = !memcmp(buf_a, buf_b, a->bytes);

        g_slice_free1(a->bytes, buf_a);
        g_slice_free1(b->bytes, buf_b);

        return result;
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
    g_async_queue_push(paranoid->incoming_twin_candidates, candidate);
}

guint8 *rm_digest_sum(RmDigestType algo, const guint8 *data, gsize len, gsize *out_len) {
    RmDigest *digest = rm_digest_new(algo, 0);
    rm_digest_update(digest, data, len);

    guint8 *buf = rm_digest_steal(digest);
    if(out_len != NULL) {
        *out_len = digest->bytes;
    }

    rm_digest_free(digest);
    return buf;
}

void rm_digest_enable_sse(gboolean use_sse) {
#if HAVE_MM_CRC32_U64 && HAVE_BUILTIN_CPU_SUPPORTS
    if (use_sse && __builtin_cpu_supports("sse4.2")) {
        g_atomic_int_set(&RM_DIGEST_USE_SSE, TRUE);
    } else {
        g_atomic_int_set(&RM_DIGEST_USE_SSE, FALSE);
        if (use_sse) {
            rm_log_warning_line("Can't enable sse4.2");
        }
    }
#else
    if (use_sse) {
        rm_log_warning_line("Can't enable sse4.2");
        g_atomic_int_set(&RM_DIGEST_USE_SSE, FALSE);
    }
#endif    
}
