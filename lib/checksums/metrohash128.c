// metrohash128crc.cpp
//
// The MIT License (MIT)
//
// Copyright (c) 2015 J. Andrew Rogers
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include <glib.h>
#include "metrohash.h"

struct _Metro128_state {
    uint8_t xs[32]; /* unhashed data from last increment */
    uint8_t xs_len;
    uint64_t v[4];
    bool use_sse;
};

struct _Metro256_state {
    Metro128State state1;
    Metro128State state2;
};

static const uint64_t k0_1 = 0xC83A91E1;
static const uint64_t k1_1 = 0x8648DBDB;
static const uint64_t k2_1 = 0x7BDEC03B;
static const uint64_t k3_1 = 0x2F5870A5;

static void metrohash128_1_init(Metro128State *state) {
    state->v[0] = -k0_1 * k3_1;
    state->v[1] = k1_1 * k2_1;
    state->v[2] = k0_1 * k2_1;
    state->v[3] = - k1_1 * k3_1;
}

Metro128State *metrohash128_1_new(bool use_sse) {
    Metro128State *state = g_slice_new0(Metro128State);
    metrohash128_1_init(state);
    state->use_sse = use_sse;
    return state;
}

static const uint64_t k0_2 = 0xEE783E2F;
static const uint64_t k1_2 = 0xAD07C493;
static const uint64_t k2_2 = 0x797A90BB;
static const uint64_t k3_2 = 0x2E4B2E1B;

static void metrohash128_2_init(Metro128State *state) {
    state->v[0] = -k0_2 * k3_2;
    state->v[1] = k1_2 * k2_2;
    state->v[2] = k0_2 * k2_2;
    state->v[3] = -k1_2 * k3_2;
}

Metro128State *metrohash128_2_new(bool use_sse) {
    Metro128State *state = g_slice_new0(Metro128State);
    metrohash128_2_init(state);
    state->use_sse = use_sse;
    return state;
}

void metrohash128_free(Metro128State *state) {
    g_slice_free(Metro128State, state);
}

Metro128State *metrohash128_copy(Metro128State *state) {
    return g_slice_copy(sizeof(Metro128State), state);
}

#define METRO_FILL_XS(xs, xs_len, xs_cap, data, data_len)                         \
    const int bytes =  ((int)data_len + (int)xs_len > (int)xs_cap) ? (int)xs_cap - (int)xs_len : (int)data_len; \
    memcpy(xs + xs_len, data, bytes);                                             \
    xs_len += bytes;                                                              \
    data += bytes;

#if HAVE_MM_CRC32_U64

#include <nmmintrin.h>

void metrohash128crc_1_update(Metro128State *state, const uint8_t *key, size_t len) {
    if(!state->use_sse) {
        metrohash128_1_update(state, key, len);
        return;
    }
    uint8_t *data = (uint8_t *)key;
    const uint8_t *stop = data + len;

    METRO_FILL_XS(state->xs, state->xs_len, 32, data, len);

    /* process blocks of 32 bytes */
    while(state->xs_len == 32 || data + 32 <= stop) {
        uint64_t d1;
        uint64_t d2;
        uint64_t d3;
        uint64_t d4;

        if(state->xs_len == 32) {
            /* process remnant data from previous update */
            d1 = read_u64(&state->xs[0]);
            d2 = read_u64(&state->xs[8]);
            d3 = read_u64(&state->xs[16]);
            d4 = read_u64(&state->xs[24]);
            state->xs_len = 0;
        } else {
            /* process new data */
            d1 = read_u64(data);
            d2 = read_u64(data + 8);
            d3 = read_u64(data + 16);
            d4 = read_u64(data + 24);
            data += 32;
        }

        state->v[0] ^= _mm_crc32_u64(state->v[0], d1);
        state->v[1] ^= _mm_crc32_u64(state->v[1], d2);
        state->v[2] ^= _mm_crc32_u64(state->v[2], d3);
        state->v[3] ^= _mm_crc32_u64(state->v[3], d4);
    }

    if(state->xs_len == 0 && stop > data) {
        // store excess data in state
        state->xs_len = stop - data;
        memcpy(state->xs, data, state->xs_len);
    }
}

void metrohash128crc_2_update(Metro128State *state, const uint8_t *key, size_t len) {
    if(!state->use_sse) {
        metrohash128_2_update(state, key, len);
    } else {
        metrohash128crc_1_update(state, key, len);
    }
}

void metrohash128crc_1_steal(Metro128State *state, uint8_t *out) {
    if(!state->use_sse) {
        metrohash128_1_steal(state, out);
        return;
    }
    uint64_t v[4];
    for(int i = 0; i < 4; i++) {
        v[i] = state->v[i];
    }

    v[2] ^= rotate_right(((v[0] + v[3]) * k0_1) + v[1], 34) * k1_1;
    v[3] ^= rotate_right(((v[1] + v[2]) * k1_1) + v[0], 37) * k0_1;
    v[0] ^= rotate_right(((v[0] + v[2]) * k0_1) + v[3], 34) * k1_1;
    v[1] ^= rotate_right(((v[1] + v[3]) * k1_1) + v[2], 37) * k0_1;

    uint8_t *ptr = state->xs;
    uint8_t *end = ptr + state->xs_len;

    if((end - ptr) >= 16) {
        v[0] += read_u64(ptr) * k2_1;
        ptr += 8;
        v[0] = rotate_right(v[0], 34) * k3_1;
        v[1] += read_u64(ptr) * k2_1;
        ptr += 8;
        v[1] = rotate_right(v[1], 34) * k3_1;
        v[0] ^= rotate_right((v[0] * k2_1) + v[1], 30) * k1_1;
        v[1] ^= rotate_right((v[1] * k3_1) + v[0], 30) * k0_1;
    }

    if((end - ptr) >= 8) {
        v[0] += read_u64(ptr) * k2_1;
        ptr += 8;
        v[0] = rotate_right(v[0], 36) * k3_1;
        v[0] ^= rotate_right((v[0] * k2_1) + v[1], 23) * k1_1;
    }

    if((end - ptr) >= 4) {
        v[1] ^= _mm_crc32_u64(v[0], read_u32(ptr));
        ptr += 4;
        v[1] ^= rotate_right((v[1] * k3_1) + v[0], 19) * k0_1;
    }

    if((end - ptr) >= 2) {
        v[0] ^= _mm_crc32_u64(v[1], read_u16(ptr));
        ptr += 2;
        v[0] ^= rotate_right((v[0] * k2_1) + v[1], 13) * k1_1;
    }

    if((end - ptr) >= 1) {
        v[1] ^= _mm_crc32_u64(v[0], read_u8(ptr));
        v[1] ^= rotate_right((v[1] * k3_1) + v[0], 17) * k0_1;
    }

    v[0] += rotate_right((v[0] * k0_1) + v[1], 11);
    v[1] += rotate_right((v[1] * k1_1) + v[0], 26);
    v[0] += rotate_right((v[0] * k0_1) + v[1], 11);
    v[1] += rotate_right((v[1] * k1_1) + v[0], 26);

    memcpy(out, v, 16);
}

void metrohash128crc_2_steal(Metro128State *state, uint8_t *out) {
    if(!state->use_sse) {
        metrohash128_2_steal(state, out);
        return;
    }

    uint64_t v[4];
    for(int i = 0; i < 4; i++) {
        v[i] = state->v[i];
    }

    v[2] ^= rotate_right(((v[0] + v[3]) * k0_2) + v[1], 12) * k1_2;
    v[3] ^= rotate_right(((v[1] + v[2]) * k1_2) + v[0], 19) * k0_2;
    v[0] ^= rotate_right(((v[0] + v[2]) * k0_2) + v[3], 12) * k1_2;
    v[1] ^= rotate_right(((v[1] + v[3]) * k1_2) + v[2], 19) * k0_2;

    uint8_t *ptr = state->xs;
    uint8_t *end = ptr + state->xs_len;

    if((end - ptr) >= 16) {
        v[0] += read_u64(ptr) * k2_2;
        ptr += 8;
        v[0] = rotate_right(v[0], 41) * k3_2;
        v[1] += read_u64(ptr) * k2_2;
        ptr += 8;
        v[1] = rotate_right(v[1], 41) * k3_2;
        v[0] ^= rotate_right((v[0] * k2_2) + v[1], 10) * k1_2;
        v[1] ^= rotate_right((v[1] * k3_2) + v[0], 10) * k0_2;
    }

    if((end - ptr) >= 8) {
        v[0] += read_u64(ptr) * k2_2;
        ptr += 8;
        v[0] = rotate_right(v[0], 34) * k3_2;
        v[0] ^= rotate_right((v[0] * k2_2) + v[1], 22) * k1_2;
    }

    if((end - ptr) >= 4) {
        v[1] ^= _mm_crc32_u64(v[0], read_u32(ptr));
        ptr += 4;
        v[1] ^= rotate_right((v[1] * k3_2) + v[0], 14) * k0_2;
    }

    if((end - ptr) >= 2) {
        v[0] ^= _mm_crc32_u64(v[1], read_u16(ptr));
        ptr += 2;
        v[0] ^= rotate_right((v[0] * k2_2) + v[1], 15) * k1_2;
    }

    if((end - ptr) >= 1) {
        v[1] ^= _mm_crc32_u64(v[0], read_u8(ptr));
        v[1] ^= rotate_right((v[1] * k3_2) + v[0], 18) * k0_2;
    }

    v[0] += rotate_right((v[0] * k0_2) + v[1], 15);
    v[1] += rotate_right((v[1] * k1_2) + v[0], 27);
    v[0] += rotate_right((v[0] * k0_2) + v[1], 15);
    v[1] += rotate_right((v[1] * k1_2) + v[0], 27);

    memcpy(out, v, 16);
}

void metrohash128crc_1(const uint8_t *key, size_t len, uint32_t seed, uint8_t *out) {
    Metro128State *state = metrohash128_1_new(TRUE);
    metrohash128crc_1_update(state, (const uint8_t*)&seed, sizeof(seed));
    metrohash128crc_1_update(state, key, len);
    metrohash128crc_1_steal(state, out);
    metrohash128_free(state);
}

void metrohash128crc_2(const uint8_t *key, size_t len, uint32_t seed, uint8_t *out) {
    Metro128State *state = metrohash128_2_new(TRUE);
    metrohash128crc_2_update(state, (const uint8_t*)&seed, sizeof(seed));
    metrohash128crc_2_update(state, key, len);
    metrohash128crc_2_steal(state, out);
    metrohash128_free(state);
}

void metrohash256crc_update(Metro256State *state, const uint8_t *key, size_t len) {
    metrohash128crc_1_update(&state->state1, key, len);
    metrohash128crc_2_update(&state->state2, key, len);
}

void metrohash256crc_steal(Metro256State *state, uint8_t *out) {
    metrohash128crc_1_steal(&state->state1, out);
    metrohash128crc_2_steal(&state->state2, out + 16);
}

#endif

void metrohash128_1_update(Metro128State *state, const uint8_t *key, size_t len) {

    uint8_t *data = (uint8_t *)key;
    const uint8_t *stop = data + len;

    METRO_FILL_XS(state->xs, state->xs_len, 32, data, len);

    /* process blocks of 32 bytes */
    while(state->xs_len == 32 || data + 32 <= stop) {
        uint64_t d1;
        uint64_t d2;
        uint64_t d3;
        uint64_t d4;

        if(state->xs_len == 32) {
            /* process remnant data from previous update */
            d1 = read_u64(&state->xs[0]);
            d2 = read_u64(&state->xs[8]);
            d3 = read_u64(&state->xs[16]);
            d4 = read_u64(&state->xs[24]);
            state->xs_len = 0;
        } else {
            /* process new data */
            d1 = read_u64(data);
            d2 = read_u64(data + 8);
            d3 = read_u64(data + 16);
            d4 = read_u64(data + 24);
            data += 32;
        }

        state->v[0] += d1 * k0_1;
        state->v[0] = rotate_right(state->v[0], 29) + state->v[2];

        state->v[1] += d2 * k1_1;
        state->v[1] = rotate_right(state->v[1], 29) + state->v[3];

        state->v[2] += d3 * k2_1;
        state->v[2] = rotate_right(state->v[2], 29) + state->v[0];

        state->v[3] += d4 * k3_1;
        state->v[3] = rotate_right(state->v[3], 29) + state->v[1];
    }

    if(state->xs_len == 0 && stop > data) {
        // store excess data in state
        state->xs_len = stop - data;
        memcpy(state->xs, data, state->xs_len);
    }
}
void metrohash128_2_update(Metro128State *state, const uint8_t *key, size_t len) {
    uint8_t *data = (uint8_t *)key;
    const uint8_t *stop = data + len;

    METRO_FILL_XS(state->xs, state->xs_len, 32, data, len);

    /* process blocks of 32 bytes */
    while(state->xs_len == 32 || data + 32 <= stop) {
        uint64_t d1;
        uint64_t d2;
        uint64_t d3;
        uint64_t d4;

        if(state->xs_len == 32) {
            /* process remnant data from previous update */
            d1 = read_u64(&state->xs[0]);
            d2 = read_u64(&state->xs[8]);
            d3 = read_u64(&state->xs[16]);
            d4 = read_u64(&state->xs[24]);
            state->xs_len = 0;
        } else {
            /* process new data */
            d1 = read_u64(data);
            d2 = read_u64(data + 8);
            d3 = read_u64(data + 16);
            d4 = read_u64(data + 24);
            data += 32;
        }

        state->v[0] += d1 * k0_2;
        state->v[0] = rotate_right(state->v[0], 29) + state->v[2];

        state->v[1] += d2 * k1_2;
        state->v[1] = rotate_right(state->v[1], 29) + state->v[3];

        state->v[2] += d3 * k2_2;
        state->v[2] = rotate_right(state->v[2], 29) + state->v[0];

        state->v[3] += d4 * k3_2;
        state->v[3] = rotate_right(state->v[3], 29) + state->v[1];
    }

    if(state->xs_len == 0 && stop > data) {
        // store excess data in state
        state->xs_len = stop - data;
        memcpy(state->xs, data, state->xs_len);
    }
}

void metrohash128_1_steal(Metro128State *state, uint8_t *out) {
    uint64_t v[4];
    for(int i = 0; i < 4; i++) {
        v[i] = state->v[i];
    }

    v[2] ^= rotate_right(((v[0] + v[3]) * k0_1) + v[1], 26) * k1_1;
    v[3] ^= rotate_right(((v[1] + v[2]) * k1_1) + v[0], 26) * k0_1;
    v[0] ^= rotate_right(((v[0] + v[2]) * k0_1) + v[3], 26) * k1_1;
    v[1] ^= rotate_right(((v[1] + v[3]) * k1_1) + v[2], 30) * k0_1;

    uint8_t *ptr = state->xs;
    uint8_t *end = ptr + state->xs_len;

    if((end - ptr) >= 16) {
        v[0] += read_u64(ptr) * k2_1;
        ptr += 8;
        v[0] = rotate_right(v[0], 33) * k3_1;
        v[1] += read_u64(ptr) * k2_1;
        ptr += 8;
        v[1] = rotate_right(v[1], 33) * k3_1;
        v[0] ^= rotate_right((v[0] * k2_1) + v[1], 17) * k1_1;
        v[1] ^= rotate_right((v[1] * k3_1) + v[0], 17) * k0_1;
    }

    if((end - ptr) >= 8) {
        v[0] += read_u64(ptr) * k2_1;
        ptr += 8;
        v[0] = rotate_right(v[0], 33) * k3_1;
        v[0] ^= rotate_right((v[0] * k2_1) + v[1], 20) * k1_1;
    }

    if((end - ptr) >= 4) {
        v[1] += read_u32(ptr) * k2_1;
        ptr += 4;
        v[1] = rotate_right(v[1], 33) * k3_1;
        v[1] ^= rotate_right((v[1] * k3_1) + v[0], 18) * k0_1;
    }

    if((end - ptr) >= 2) {
        v[0] += read_u16(ptr) * k2_1;
        ptr += 2;
        v[0] = rotate_right(v[0], 33) * k3_1;
        v[0] ^= rotate_right((v[0] * k2_1) + v[1], 24) * k1_1;
    }

    if((end - ptr) >= 1) {
        v[1] += read_u8(ptr) * k2_1;
        v[1] = rotate_right(v[1], 33) * k3_1;
        v[1] ^= rotate_right((v[1] * k3_1) + v[0], 24) * k0_1;
    }

    v[0] += rotate_right((v[0] * k0_1) + v[1], 13);
    v[1] += rotate_right((v[1] * k1_1) + v[0], 37);
    v[0] += rotate_right((v[0] * k2_1) + v[1], 13);
    v[1] += rotate_right((v[1] * k3_1) + v[0], 37);

    memcpy(out, v, 16);
}

void metrohash128_2_steal(Metro128State *state, uint8_t *out) {
    uint64_t v[4];
    for(int i = 0; i < 4; i++) {
        v[i] = state->v[i];
    }

    v[2] ^= rotate_right(((v[0] + v[3]) * k0_2) + v[1], 33) * k1_2;
    v[3] ^= rotate_right(((v[1] + v[2]) * k1_2) + v[0], 33) * k0_2;
    v[0] ^= rotate_right(((v[0] + v[2]) * k0_2) + v[3], 33) * k1_2;
    v[1] ^= rotate_right(((v[1] + v[3]) * k1_2) + v[2], 33) * k0_2;

    uint8_t *ptr = state->xs;
    uint8_t *end = ptr + state->xs_len;

    if((end - ptr) >= 16) {
        v[0] += read_u64(ptr) * k2_2;
        ptr += 8;
        v[0] = rotate_right(v[0], 29) * k3_2;
        v[1] += read_u64(ptr) * k2_2;
        ptr += 8;
        v[1] = rotate_right(v[1], 29) * k3_2;
        v[0] ^= rotate_right((v[0] * k2_2) + v[1], 29) * k1_2;
        v[1] ^= rotate_right((v[1] * k3_2) + v[0], 29) * k0_2;
    }

    if((end - ptr) >= 8) {
        v[0] += read_u64(ptr) * k2_2;
        ptr += 8;
        v[0] = rotate_right(v[0], 29) * k3_2;
        v[0] ^= rotate_right((v[0] * k2_2) + v[1], 29) * k1_2;
    }

    if((end - ptr) >= 4) {
        v[1] += read_u32(ptr) * k2_2;
        ptr += 4;
        v[1] = rotate_right(v[1], 29) * k3_2;
        v[1] ^= rotate_right((v[1] * k3_2) + v[0], 25) * k0_2;
    }

    if((end - ptr) >= 2) {
        v[0] += read_u16(ptr) * k2_2;
        ptr += 2;
        v[0] = rotate_right(v[0], 29) * k3_2;
        v[0] ^= rotate_right((v[0] * k2_2) + v[1], 30) * k1_2;
    }

    if((end - ptr) >= 1) {
        v[1] += read_u8(ptr) * k2_2;
        v[1] = rotate_right(v[1], 29) * k3_2;
        v[1] ^= rotate_right((v[1] * k3_2) + v[0], 18) * k0_2;
    }

    v[0] += rotate_right((v[0] * k0_2) + v[1], 33);
    v[1] += rotate_right((v[1] * k1_2) + v[0], 33);
    v[0] += rotate_right((v[0] * k2_2) + v[1], 33);
    v[1] += rotate_right((v[1] * k3_2) + v[0], 33);

    memcpy(out, v, 16);
}

void metrohash128_1(const uint8_t *key, size_t len, uint32_t seed, uint8_t *out) {
    Metro128State *state = metrohash128_1_new(FALSE);
    metrohash128_1_update(state, (const uint8_t*)&seed, sizeof(seed));
    metrohash128_1_update(state, key, len);
    metrohash128_1_steal(state, out);
    metrohash128_free(state);
}

void metrohash128_2(const uint8_t *key, size_t len, uint32_t seed, uint8_t *out) {
    Metro128State *state = metrohash128_2_new(FALSE);
    metrohash128_2_update(state, (const uint8_t*)&seed, sizeof(seed));
    metrohash128_2_update(state, key, len);
    metrohash128_2_steal(state, out);
    metrohash128_free(state);
}

Metro256State *metrohash256_new(bool use_sse) {
    Metro256State *state = g_slice_new0(Metro256State);
    metrohash128_1_init(&state->state1);
    state->state1.use_sse = use_sse;
    metrohash128_2_init(&state->state2);
    state->state2.use_sse = use_sse;
    return state;
}

void metrohash256_free(Metro256State *state) {
    g_slice_free(Metro256State, state);
}

Metro256State *metrohash256_copy(Metro256State *state) {
    return g_slice_copy(sizeof(Metro256State), state);
}

void metrohash256_update(Metro256State *state, const uint8_t *key, size_t len) {
    metrohash128_1_update(&state->state1, key, len);
    metrohash128_2_update(&state->state2, key, len);
}

void metrohash256_steal(Metro256State *state, uint8_t *out) {
    metrohash128_1_steal(&state->state1, out);
    metrohash128_2_steal(&state->state2, out + 16);
}
