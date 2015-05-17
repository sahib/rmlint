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


///////////////////////////////////////
//    BUFFER POOL IMPLEMENTATION     //
///////////////////////////////////////

RmOff rm_buffer_size(RmBufferPool *pool) {
    return pool->buffer_size;
}

RmBufferPool *rm_buffer_pool_init(gsize buffer_size, gsize max_mem) {
    RmBufferPool *self = g_slice_new(RmBufferPool);
    self->buffer_size = buffer_size;
    self->avail_buffers = MAX(max_mem / buffer_size, 1);
    self->min_buffers = self->avail_buffers;
    self->max_buffers = self->avail_buffers;
    rm_log_debug("rm_buffer_pool_init: allocated max %lu buffers of %lu bytes each\n", self->avail_buffers, self->buffer_size);
    g_cond_init(&self->change);
    g_mutex_init(&self->lock);
    return self;
}

void rm_buffer_pool_destroy(RmBufferPool *pool) {
    g_mutex_lock(&pool->lock);
    rm_log_info(BLUE"Info: had %lu unused read buffers\n"RESET, pool->min_buffers);
    g_mutex_unlock(&pool->lock);
    g_mutex_clear(&pool->lock);
    g_cond_clear(&pool->change);
    g_slice_free(RmBufferPool, pool);
}

RmBuffer *rm_buffer_new(RmBufferPool *pool) {
    RmBuffer *self = g_slice_new(RmBuffer);
    self->pool = pool;
    self->data = g_slice_alloc(pool->buffer_size);
    return self;
}

RmBuffer *rm_buffer_pool_get(RmBufferPool *pool) {
    RmBuffer *buffer = NULL;
    g_mutex_lock(&pool->lock);
    {
        while (pool->avail_buffers == 0) {
            if (!pool->mem_warned) {
                rm_log_warning(RED"Warning: read buffer limit reached - waiting for processing to catch up\n"RESET);
                pool->mem_warned = true;
            }
            g_cond_wait(&pool->change, &pool->lock);
        }
        pool->avail_buffers --;
        buffer = rm_buffer_new(pool);

        if (pool->avail_buffers < pool->min_buffers) {
            pool->min_buffers = pool->avail_buffers;
        }
    }
    g_mutex_unlock(&pool->lock);

    g_assert(buffer);
    return buffer;
}

void rm_buffer_pool_release(RmBuffer *buf) {
    g_mutex_lock(&buf->pool->lock);
    {
        buf->pool->avail_buffers ++;
        g_cond_signal(&buf->pool->change);
    }
    g_mutex_unlock(&buf->pool->lock);
    g_slice_free1(buf->pool->buffer_size, buf->data);
    g_slice_free(RmBuffer, buf);
}

/* make another buffer available if one is being kept (in paranoid digest) */
static void rm_buffer_pool_signal_keeping(RmBuffer *buf) {
    g_mutex_lock(&buf->pool->lock);
    {
        buf->pool->avail_buffers ++;
        g_cond_signal(&buf->pool->change);
    }
    g_mutex_unlock(&buf->pool->lock);
}

static void rm_buffer_pool_release_kept(RmBuffer *buf) {
    g_slice_free1(buf->pool->buffer_size, buf->data);
    g_slice_free(RmBuffer, buf);
}

static gboolean rm_buffer_equal(RmBuffer *a, RmBuffer *b){
    return (a->len == b->len && memcmp(a->data, b->data, a->len)==0);
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
        {[RM_DIGEST_UNKNOWN] = "unknown",       [RM_DIGEST_MURMUR] = "murmur",
         [RM_DIGEST_SPOOKY] = "spooky",         [RM_DIGEST_SPOOKY32] = "spooky32",
         [RM_DIGEST_SPOOKY64] = "spooky64",     [RM_DIGEST_CITY] = "city",
         [RM_DIGEST_MD5] = "md5",               [RM_DIGEST_SHA1] = "sha1",
         [RM_DIGEST_SHA256] = "sha256",         [RM_DIGEST_SHA512] = "sha512",
         [RM_DIGEST_MURMUR256] = "murmur256",   [RM_DIGEST_CITY256] = "city256",
         [RM_DIGEST_BASTARD] = "bastard",       [RM_DIGEST_MURMUR512] = "murmur512",
         [RM_DIGEST_CITY512] = "city512",       [RM_DIGEST_EXT] = "ext",
         [RM_DIGEST_CUMULATIVE] = "cumulative", [RM_DIGEST_PARANOID] = "paranoid"};

    return names[MIN(type, sizeof(names) / sizeof(names[0]))];
}

RmOff rm_digest_paranoia_bytes(void) {
    return 64 * 1024 * 1024;
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

RmDigest *rm_digest_new(RmDigestType type, RmOff seed1, RmOff seed2,
                        RmOff paranoid_size, bool use_shadow_hash) {
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
        digest->buffers = g_queue_new();
        digest->incoming_twin_candidates=g_async_queue_new();
        if(use_shadow_hash) {
            digest->shadow_hash = rm_digest_new(RM_DIGEST_SPOOKY, seed1, seed2, 0, false);
        } else {
            digest->shadow_hash = NULL;
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
            digest->checksum[block].first =
                seeds[block % n_seeds] ^ digest->initial_seed1;
            digest->checksum[block].second =
                seeds[block % n_seeds] ^ digest->initial_seed2;
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
    /* TODO: @chris, not sure I understand this and how to make it work again*/

    /*g_assert(new_size < digest->bytes);
    g_assert(digest->type == RM_DIGEST_PARANOID);

    RmUint128 *old_checksum = digest->checksum;
    gsize old_bytes = digest->bytes;

    digest->checksum = g_slice_alloc0(new_size);
    digest->bytes = new_size;
    memcpy(digest->checksum, old_checksum, new_size);

    g_slice_free1(old_bytes, old_checksum);*/
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
        if (digest->buffers) {
            g_queue_free_full(digest->buffers, (GDestroyNotify)rm_buffer_pool_release_kept);
        }
        if (digest->incoming_twin_candidates) {
            g_async_queue_unref(digest->incoming_twin_candidates);
        }
        g_list_free(digest->twin_candidates);
        g_list_free(digest->twin_candidate_buffers);
        break;
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
        spooky_hash128(data, size, &digest->checksum[0].first,
                       &digest->checksum[0].second);
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

void rm_digest_buffered_update(RmDigest *digest, RmBuffer *buffer) {
    if (digest->type != RM_DIGEST_PARANOID) {
        rm_digest_update(digest, buffer->data, buffer->len);
    } else {
        g_assert(buffer->len <= buffer->pool->buffer_size);
        g_assert(buffer->len + digest->paranoid_offset <= digest->bytes);
        g_queue_push_tail(digest->buffers, buffer);
        digest->buffer_count++;
        digest->paranoid_offset += buffer->len;
        rm_buffer_pool_signal_keeping(buffer);
        if(digest->shadow_hash) {
            rm_digest_update(digest->shadow_hash, buffer->data, buffer->len);
        }
        if(digest->twin_candidates) {
            /* do a running check that digest remains the same as its candidate twin */
            /* first check for incoming twin candidates */
            RmDigest *new_twin_candidate = NULL;
            /* every 16 buffers (64 kb), check for new match candidates */
            while ( !digest->twin_candidates &&
                    (new_twin_candidate = g_async_queue_try_pop(digest->incoming_twin_candidates))) {
                /* validate the new candidate by comparing the previous buffers (not including current)*/
                GList *iter_new = new_twin_candidate->buffers->head;
                GList *iter_self = digest->buffers->head;
                gboolean match=TRUE;
                while (match && iter_self && iter_self != digest->buffers->tail) {
                    match = (rm_buffer_equal(iter_new->data, iter_self->data));
                    iter_self = iter_self->next;
                    iter_new = iter_new->next;
                }
                if (match) {
                    /* accept the twin candidate */
                    rm_log_debug(BLUE"Adding twin candidate %p\n"RESET, new_twin_candidate);
                    g_assert(iter_self == digest->buffers->tail);
                    digest->twin_candidates = g_list_prepend(digest->twin_candidates, new_twin_candidate);
                    /* also store a pointer to the buffer corresponding to our current buffer */
                    digest->twin_candidate_buffers = g_list_prepend(digest->twin_candidate_buffers, iter_new);
                }
            }

            /* iterate through all twin candidates to see if they still match */
            GList *twin_candidate_iter = digest->twin_candidates;
            GList *twin_candidate_buffer_iter = digest->twin_candidate_buffers;
            while (twin_candidate_iter) {
                /* check next increment of each candidate twin */
                GList *twin_candidate_buffer_ptr = twin_candidate_buffer_iter->data;
                GList *temp1 = twin_candidate_iter;
                GList *temp2 = twin_candidate_buffer_iter;
                twin_candidate_iter = twin_candidate_iter->next;
                twin_candidate_buffer_iter = twin_candidate_buffer_iter->next;
                if (rm_buffer_equal(buffer, twin_candidate_buffer_ptr->data)) {
                    /* buffers match; move ptr to next one ready for next buffer */
                    temp2->data = twin_candidate_buffer_ptr->next;
                } else {
                    /* buffers don't match - delete candidate */
                    digest->twin_candidates = g_list_delete_link(digest->twin_candidates, temp1);
                    digest->twin_candidate_buffers = g_list_delete_link(digest->twin_candidate_buffers, temp2);
                    rm_log_debug(RED"Ejected candidate match at buffer #%lu\n"RESET, digest->buffer_count);
                }
            }
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
    case RM_DIGEST_BASTARD:
    case RM_DIGEST_CUMULATIVE:
    case RM_DIGEST_EXT:
        self = rm_digest_new(digest->type, digest->initial_seed1, digest->initial_seed2,
                             digest->bytes, digest->shadow_hash != NULL);

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
    case RM_DIGEST_EXT:
        memcpy(result, digest->checksum, digest->bytes);
        break;
    case RM_DIGEST_PARANOID:
    default:
        g_assert_not_reached();
    }

    return result;
}

guint rm_digest_hash(RmDigest *digest) {
    guint8 *buf = NULL;
    gsize bytes = 0;

    if(digest->type == RM_DIGEST_PARANOID) {
        if(digest->shadow_hash) {
            buf = rm_digest_steal_buffer(digest->shadow_hash);
            bytes = digest->shadow_hash->bytes;
        }
    } else {
        buf = rm_digest_steal_buffer(digest);
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
    if (a->type == RM_DIGEST_PARANOID) {
        if (a->bytes != b->bytes) {
            rm_log_error(RED"Error: byte counts don't match in rm_digest_equal\n"RESET);
            return false;
        } else if ((a->twin_candidates && a->twin_candidates->data == b)
                || (b->twin_candidates && b->twin_candidates->data == a)) {
            if (a->bytes > 4 * 4096) {
                rm_log_debug(GREEN "+" RESET);
            }
            //rm_log_debug(GREEN "Found match of size %"LLU" via twin_candidate strategy\n" RESET, a->bytes);
            return true;
        }

        GList *a_iter = a->buffers->head;
        GList *b_iter = b->buffers->head;
        guint bytes=0;
        while (a_iter && b_iter) {
            if (!rm_buffer_equal(a_iter->data, b_iter->data)) {
                rm_log_error(RED"Paranoid digest compare found mismatch - must be hash collision in shadow hash"RESET);
                return false;
            }
            bytes += ((RmBuffer*)a_iter->data)->len;
            a_iter = a_iter->next;
            b_iter = b_iter->next;
        }
        if (!a_iter && !b_iter && bytes==a->bytes) {
            if (a->bytes > 4 * 4096) {
                rm_log_debug(RED "+" RESET);
            }
            return true;
        } else {
            return false;
        }
    } else {
        guint8 *buf_a = rm_digest_steal_buffer(a);
        guint8 *buf_b = rm_digest_steal_buffer(b);

        gboolean result = !memcmp(buf_a, buf_b, MIN(a->bytes, b->bytes));

        g_slice_free1(a->bytes, buf_a);
        g_slice_free1(b->bytes, buf_b);

        return result;
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
        if(digest->shadow_hash) {
            input = rm_digest_steal_buffer(digest->shadow_hash);
            bytes = digest->shadow_hash->bytes;
        }
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
    } else if(self->shadow_hash) {
        return self->shadow_hash->bytes;
    } else {
        return 0;
    }
}

void rm_digest_send_match_candidate(RmDigest *target, RmDigest *candidate) {
    g_async_queue_push(target->incoming_twin_candidates, candidate);
}
