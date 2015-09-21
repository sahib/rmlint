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
#include "checksums/xxhash/xxhash.h"

///////////////////////////////////////
//    BUFFER POOL IMPLEMENTATION     //
///////////////////////////////////////

RmOff rm_buffer_size(RmBufferPool *pool) {
    return pool->buffer_size;
}

RmBuffer *rm_buffer_new(RmBufferPool *pool) {
    RmBuffer *self = g_slice_new0(RmBuffer);
    self->pool = pool;
    self->data = g_slice_alloc(pool->buffer_size);
    return self;
}

static void rm_buffer_free(RmBuffer *buf) {
    g_slice_free1(buf->pool->buffer_size, buf->data);
    g_slice_free(RmBuffer, buf);
}

RmBufferPool *rm_buffer_pool_init(gsize buffer_size, gsize max_mem, gsize max_kept_mem) {
    RmBufferPool *self = g_slice_new(RmBufferPool);
    self->stack = NULL;
    self->buffer_size = buffer_size;
    self->avail_buffers = MAX(max_mem / buffer_size, 1);
    self->min_buffers = self->avail_buffers;
    self->max_buffers = self->avail_buffers;
    self->max_kept_buffers = MAX(max_kept_mem / buffer_size, 1);
    self->kept_buffers = 0;
    rm_log_debug_line("rm_buffer_pool_init: allocated max %lu buffers of %lu bytes each",
                 self->avail_buffers, self->buffer_size);
    g_cond_init(&self->change);
    g_mutex_init(&self->lock);
    return self;
}

void rm_buffer_pool_destroy(RmBufferPool *pool) {
    rm_log_debug_line("had %lu unused read buffers", pool->min_buffers);

    /* Wait for all buffers to come back */
    g_mutex_lock(&pool->lock);
    while(pool->kept_buffers) {
        rm_log_debug_line("Waiting for buf releases.");
        g_cond_wait(&pool->change, &pool->lock);
    }
    g_mutex_unlock(&pool->lock);

    /* Free 'em */
    while(pool->stack != NULL) {
        rm_buffer_free(g_trash_stack_pop(&pool->stack));
    }

    g_mutex_clear(&pool->lock);
    g_cond_clear(&pool->change);
    g_slice_free(RmBufferPool, pool);
}

RmBuffer *rm_buffer_pool_get(RmBufferPool *pool) {
    RmBuffer *buffer = NULL;
    g_mutex_lock(&pool->lock);
    {
        while(!buffer) {
            if(pool->stack) {
                buffer = g_trash_stack_pop(&pool->stack);
            } else if(pool->avail_buffers > 0) {
                buffer = rm_buffer_new(pool);
            } else {
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

        if(pool->avail_buffers < pool->min_buffers) {
            pool->min_buffers = pool->avail_buffers;
        }
    }
    g_mutex_unlock(&pool->lock);

    g_assert(buffer);
    return buffer;
}

void rm_buffer_pool_release(RmBuffer *buf) {
    RmBufferPool *pool = buf->pool;
    g_mutex_lock(&pool->lock);
    {
        pool->avail_buffers++;
        g_trash_stack_push(&pool->stack, buf);
        g_cond_signal(&pool->change);
    }
    g_mutex_unlock(&pool->lock);
}

/* make another buffer available if one is being kept (in paranoid digest) */
static void rm_buffer_pool_signal_keeping(_U RmBuffer *buf) {
    RmBufferPool *pool = buf->pool;

    g_mutex_lock(&pool->lock);
    {
        pool->avail_buffers++;
        pool->kept_buffers++;
        g_cond_signal(&pool->change);
    }
    g_mutex_unlock(&pool->lock);
}

static void rm_buffer_pool_release_kept(RmBuffer *buf) {
    RmBufferPool *pool = buf->pool;
    g_mutex_lock(&pool->lock);
    {
        if(pool->kept_buffers > pool->max_kept_buffers) {
            rm_buffer_free(buf);
        } else {
            g_trash_stack_push(&pool->stack, buf);
        }
        pool->kept_buffers--;
        g_cond_signal(&pool->change);
    }
    g_mutex_unlock(&pool->lock);
}

static gboolean rm_buffer_equal(RmBuffer *a, RmBuffer *b) {
    return (a->len == b->len && memcmp(a->data, b->data, a->len) == 0);
}

///////////////////////////////////////
//      RMDIGEST IMPLEMENTATION      //
///////////////////////////////////////

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
    } else if(!strcasecmp(string, "xxhash")) {
        return RM_DIGEST_XXHASH;
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
    static const char *names[] =
        {[RM_DIGEST_UNKNOWN] = "unknown", [RM_DIGEST_MURMUR] = "murmur",
         [RM_DIGEST_SPOOKY] = "spooky", [RM_DIGEST_SPOOKY32] = "spooky32",
         [RM_DIGEST_SPOOKY64] = "spooky64", [RM_DIGEST_CITY] = "city",
         [RM_DIGEST_MD5] = "md5", [RM_DIGEST_SHA1] = "sha1",
         [RM_DIGEST_SHA256] = "sha256", [RM_DIGEST_SHA512] = "sha512",
         [RM_DIGEST_MURMUR256] = "murmur256", [RM_DIGEST_CITY256] = "city256",
         [RM_DIGEST_BASTARD] = "bastard", [RM_DIGEST_MURMUR512] = "murmur512",
         [RM_DIGEST_CITY512] = "city512", [RM_DIGEST_EXT] = "ext",
         [RM_DIGEST_CUMULATIVE] = "cumulative", [RM_DIGEST_PARANOID] = "paranoid",
         [RM_DIGEST_XXHASH] = "xxhash"};

    return names[MIN(type, sizeof(names) / sizeof(names[0]))];
}

int rm_digest_type_to_multihash_id(RmDigestType type) {
    static int ids[] = {[RM_DIGEST_UNKNOWN] = -1,    [RM_DIGEST_MURMUR] = 17,
                        [RM_DIGEST_SPOOKY] = 14,     [RM_DIGEST_SPOOKY32] = 16,
                        [RM_DIGEST_SPOOKY64] = 18,   [RM_DIGEST_CITY] = 15,
                        [RM_DIGEST_MD5] = 1,         [RM_DIGEST_SHA1] = 2,
                        [RM_DIGEST_SHA256] = 4,      [RM_DIGEST_SHA512] = 6,
                        [RM_DIGEST_MURMUR256] = 7,   [RM_DIGEST_CITY256] = 8,
                        [RM_DIGEST_BASTARD] = 9,     [RM_DIGEST_MURMUR512] = 10,
                        [RM_DIGEST_CITY512] = 11,    [RM_DIGEST_EXT] = 12,
                        [RM_DIGEST_CUMULATIVE] = 13, [RM_DIGEST_PARANOID] = 14};

    return ids[MIN(type, sizeof(ids) / sizeof(ids[0]))];
}

RmOff rm_digest_paranoia_bytes(void) {
    return 16 * 1024 * 1024;
    /* this is big enough buffer size to make seek time fairly insignificant relative to
     * sequential read time,
     * eg 16MB read at typical 100 MB/s read rate = 160ms read vs typical seek time 10ms*/
}

#define ADD_SEED(digest, seed)                                              \
    {                                                                       \
        if(seed) {                                                          \
            g_checksum_update(digest->glib_checksum, (const guchar *)&seed, \
                              sizeof(RmOff));                               \
        }                                                                   \
    }

RmDigest *rm_digest_new(RmDigestType type, RmOff seed1, RmOff seed2, RmOff paranoid_size,
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
        digest->paranoid = g_slice_new0(RmParanoid);
        digest->paranoid->buffers = g_queue_new();
        digest->paranoid->incoming_twin_candidates = g_async_queue_new();
        g_assert(use_shadow_hash);
        if(use_shadow_hash) {
            digest->paranoid->shadow_hash =
                rm_digest_new(RM_DIGEST_XXHASH, seed1, seed2, 0, false);
        } else {
            digest->paranoid->shadow_hash = NULL;
        }
        break;
    default:
        g_assert_not_reached();
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
    g_assert(new_size < digest->bytes);
    g_assert(digest->type == RM_DIGEST_PARANOID);

    digest->bytes = new_size;
}

void rm_digest_release_buffers(RmDigest *digest) {
    if(digest->paranoid && digest->paranoid->buffers) {
        g_queue_free_full(digest->paranoid->buffers,
                          (GDestroyNotify)rm_buffer_pool_release_kept);
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
        g_slice_free(RmParanoid, digest->paranoid);
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
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_MURMUR:
        for(guint8 block = 0; block < (digest->bytes / 16); block++) {
#if RM_PLATFORM_32
            MurmurHash3_x86_128(data, size, (uint32_t)digest->checksum[block].first,
                                &digest->checksum[block]);  //&
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
#if RM_PLATFORM_64 && HAVE_SSE42
            old = CityHashCrc128WithSeed((const char *)data, size, old);
#else
            old = CityHash128WithSeed((const char *)data, size, old);
#endif
            memcpy(&digest->checksum[block], &old, sizeof(uint128));
        }
        break;
    case RM_DIGEST_BASTARD:
        MurmurHash3_x86_128(data, size, (uint32_t)digest->checksum[0].first,
                            &digest->checksum[0]);

        uint128 old = {digest->checksum[1].first, digest->checksum[1].second};
#if RM_PLATFORM_64 && HAVE_SSE42
        old = CityHashCrc128WithSeed((const char *)data, size, old);
#else
        old = CityHash128WithSeed((const char *)data, size, old);
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
    } break;
    case RM_DIGEST_PARANOID:
    default:
        g_assert_not_reached();
    }
}

void rm_digest_buffered_update(RmBuffer *buffer) {
    RmDigest *digest = buffer->digest;
    if(digest->type != RM_DIGEST_PARANOID) {
        rm_digest_update(digest, buffer->data, buffer->len);
    } else {
        RmParanoid *paranoid = digest->paranoid;

        g_queue_push_tail(paranoid->buffers, buffer);
        paranoid->buffer_count++;
        rm_buffer_pool_signal_keeping(buffer);

        g_assert(paranoid->shadow_hash);
        if(paranoid->shadow_hash) {
            rm_digest_update(paranoid->shadow_hash, buffer->data, buffer->len);
        }

        if(!paranoid->twin_candidate) {
            /* try to pop a candidate from the incoming queue */
            if(paranoid->incoming_twin_candidates &&
               (paranoid->twin_candidate =
                    g_async_queue_try_pop(paranoid->incoming_twin_candidates))) {
                /* validate the new candidate by comparing the previous buffers (not
                 * including current)*/
                paranoid->twin_candidate_buffer =
                    paranoid->twin_candidate->paranoid->buffers->head;
                GList *iter_self = paranoid->buffers->head;
                gboolean match = TRUE;
                while(match && iter_self) {
                    match = (rm_buffer_equal(paranoid->twin_candidate_buffer->data,
                                             iter_self->data));
                    iter_self = iter_self->next;
                    paranoid->twin_candidate_buffer =
                        paranoid->twin_candidate_buffer->next;
                }
                if(!match) {
                    /* reject the twin candidate (new candidate might be added
                     * on next call to rm_digest_buffered_update)*/
                    paranoid->twin_candidate = NULL;
                    paranoid->twin_candidate_buffer = NULL;
                } else {
                    rm_log_debug_line("Added twincandidate %p", paranoid->twin_candidate);
                }
            }
        /* do a running check that digest remains the same as its candidate twin */
        } else if(rm_buffer_equal(buffer, paranoid->twin_candidate_buffer->data)) {
            /* buffers match; move ptr to next one ready for next buffer */
            paranoid->twin_candidate_buffer = paranoid->twin_candidate_buffer->next;
        } else {
            /* buffers don't match - delete candidate (new candidate might be
             * added on next call to rm_digest_buffered_update) */
            paranoid->twin_candidate = NULL;
            paranoid->twin_candidate_buffer = NULL;
            rm_log_debug_line("Ejected candidate match at buffer #%u", paranoid->buffer_count);
        }
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
        g_assert_not_reached();
    }

    return self;
}

static gboolean rm_digest_needs_steal(RmDigestType digest_type) {
    switch(digest_type) {
    case RM_DIGEST_MD5:
    case RM_DIGEST_SHA512:
    case RM_DIGEST_SHA256:
    case RM_DIGEST_SHA1:
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
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_BASTARD:
    case RM_DIGEST_CUMULATIVE:
    case RM_DIGEST_EXT:
    case RM_DIGEST_PARANOID:
        return FALSE;
    default:
        g_assert_not_reached();
    }
}

guint8 *rm_digest_steal(RmDigest *digest) {
    guint8 *result = g_slice_alloc0(digest->bytes);
    gsize buflen = digest->bytes;

    if(rm_digest_needs_steal(digest->type)) {
        /* reading the digest is destructive, so we need to take a copy */
        RmDigest *copy = rm_digest_copy(digest);
        g_checksum_get_digest(copy->glib_checksum, result, &buflen);
        g_assert(buflen == digest->bytes);
        rm_digest_free(copy);
    } else {
        memcpy(result, digest->checksum, digest->bytes);
    }
    return result;
}

guint rm_digest_hash(RmDigest *digest) {
    guint8 *buf = NULL;
    gsize bytes = 0;

    if(digest->type == RM_DIGEST_PARANOID) {
        if(digest->paranoid->shadow_hash) {
            buf = rm_digest_steal(digest->paranoid->shadow_hash);
            bytes = digest->paranoid->shadow_hash->bytes;
        }
    } else {
        buf = rm_digest_steal(digest);
        bytes = digest->bytes;
    }

    guint hash = 0;
    if(buf != NULL) {
        hash = *(RmOff *)buf;
        g_slice_free1(bytes, buf);
    }
    return hash;
}

gboolean rm_digest_equal(RmDigest *a, RmDigest *b) {
    g_assert(a && b);

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
        if(a->paranoid->twin_candidate == b || b->paranoid->twin_candidate == a) {
            return true;
        }

        GList *a_iter = a->paranoid->buffers->head;
        GList *b_iter = b->paranoid->buffers->head;
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

        gboolean result;

        if(a->bytes != b->bytes) {
            result = false;
        } else {
            result = !memcmp(buf_a, buf_b, MIN(a->bytes, b->bytes));
        }

        g_slice_free1(a->bytes, buf_a);
        g_slice_free1(b->bytes, buf_b);

        return result;
    } else {
        return !memcmp(a->checksum, b->checksum, MIN(a->bytes, b->bytes));
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
    } else if(self->paranoid->shadow_hash) {
        return self->paranoid->shadow_hash->bytes;
    } else {
        return 0;
    }
}

void rm_digest_send_match_candidate(RmDigest *target, RmDigest *candidate) {
    if(!target->paranoid->incoming_twin_candidates) {
        target->paranoid->incoming_twin_candidates = g_async_queue_new();
    }
    g_async_queue_push(target->paranoid->incoming_twin_candidates, candidate);
}
