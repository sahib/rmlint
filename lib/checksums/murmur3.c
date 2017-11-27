//-----------------------------------------------------------------------------
// Streaming implementation of MurmurHash3 by Daniel Thomas
// Based on single-buffer implementation by Austin Appleby
// Code is placed in the public domain.
// The authors disclaim copyright to this source code.

// Note - The x86 and x64 versions do _not_ produce the same results, as the
// algorithms are optimized for their respective platforms. You can still
// compile and run any of them on any platform, but your performance with the
// non-native version will be less than optimal.
// Also will give different (but equally strong) results on big- vs
// little-endian platforms

#include "murmur3.h"
#include <glib.h>
#include <string.h>

//-----------------------------------------------------------------------------
// Platform-specific functions and macros

__attribute__((__unused__)) static inline uint32_t rotl32(uint32_t x, int8_t r) {
    return (x << r) | (x >> (32 - r));
}

static inline uint64_t rotl64(uint64_t x, int8_t r) {
    return (x << r) | (x >> (64 - r));
}

#define ROTL32(x, y) rotl32(x, y)
#define ROTL64(x, y) rotl64(x, y)

#define BIG_CONSTANT(x) (x##LLU)

    //-----------------------------------------------------------------------------
    // Block read - if your platform needs to do endian-swapping or can only
    // handle aligned reads, do the conversion here

#define GET_UINT64(p) *((uint64_t *)(p));
#define GET_UINT32(p) *((uint32_t *)(p));

struct _MurmurHash3_x86_32_state {
    uint32_t h1;
    union {
		uint8_t xs[4]; /* unhashed data from last increment */
		uint32_t xs32;
	};
    uint8_t xs_len;
    size_t len;
};

struct _MurmurHash3_x86_128_state {
    uint32_t h1;
    uint32_t h2;
    uint32_t h3;
    uint32_t h4;
    union {
		uint8_t xs[16]; /* unhashed data from last increment */
		uint32_t xs32[4];
	};
    uint8_t xs_len;
    size_t len;
};

struct _MurmurHash3_x64_128_state {
    uint64_t h1;
    uint64_t h2;
    union {
		uint8_t xs[16]; /* unhashed data from last increment */
		uint64_t xs64[2];
	};
    uint8_t xs_len;
    size_t len;
};

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

static inline uint32_t fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    return h;
}

//----------

static inline uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= BIG_CONSTANT(0xff51afd7ed558ccd);
    k ^= k >> 33;
    k *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
    k ^= k >> 33;

    return k;
}

    //-----------------------------------------------------------------------------

#define MURMUR_UPDATE_X86(h, k, rotl, ca, cb) \
	k *= ca;  \
	k = ROTL32(k, rotl);  \
	k *= cb;  \
	h ^= k;

#define MURMUR_MIX_X86(ha, hb, rotl, c) \
	ha = ROTL32(ha, rotl); \
	ha += hb;   \
	ha = ha * 5 + c;


#define MURMUR_UPDATE_X64(h, k, rotl, ca, cb) \
    k = k * ca;                              \
    k = ROTL64(k, rotl);                  \
    k *= cb;                              \
    h ^= k;										\


#define MURMUR_MIX_X64(ha, hb, rotl, c) \
    ha = ROTL64(ha, rotl);          \
    ha += hb;                       \
    ha = ha * 5 + c;  \



#define MURMUR_FILL_XS(xs, xs_len, xs_cap, data, data_len)                        \
    const int bytes =                                                             \
        ((int)data_len + (int)xs_len > (int)xs_cap) ? (int)xs_cap - (int)xs_len : (int)data_len; \
    memcpy(xs + xs_len, data, bytes);                                             \
    xs_len += bytes;                                                              \
    data += bytes;

//-----------------------------------------------------------------------------

MurmurHash3_x86_32_state *MurmurHash3_x86_32_new() {
    return g_slice_new0(MurmurHash3_x86_32_state);
}

MurmurHash3_x86_32_state *MurmurHash3_x86_32_copy(MurmurHash3_x86_32_state *state) {
    return g_slice_copy(sizeof(MurmurHash3_x86_32_state), state);
}

#define MURMUR_UPDATE_H1_X86_32(H1, K1) MURMUR_UPDATE_X86(H1, K1, 15, 0xcc9e2d51, 0x1b873593);

void MurmurHash3_x86_32_update(MurmurHash3_x86_32_state *const state,
                               const void *restrict key, const size_t len) {
    state->len += len;
    uint8_t *data = (uint8_t *)key;
    const uint8_t *stop = data + len;

    if(state->xs_len > 0) {
        MURMUR_FILL_XS(state->xs, state->xs_len, 4, data, len);
    }

    /* process blocks of 4 bytes */
    while(state->xs_len == 4 || data + 4 <= stop) {
        uint32_t k1;

        if(state->xs_len == 4) {
            /* process remnant data from previous update */
            k1 = state->xs32;
            state->xs_len = 0;
        } else {
            /* process new data */
            k1 = GET_UINT32(data);
            data += 4;
        }

        MURMUR_UPDATE_H1_X86_32(state->h1, k1);
        MURMUR_MIX_X86(state->h1, 0, 13, 0xe6546b64);
    }

    if(state->xs_len == 0 && stop > data) {
        // store excess data in state
        state->xs_len = stop - data;
        memcpy(state->xs, data, state->xs_len);
    }
}

void MurmurHash3_x86_32_steal(const MurmurHash3_x86_32_state *const restrict state,
                              void *const restrict out) {
    uint32_t k1 = 0;

    /* copy h to make this a non-destructive steal */
    uint32_t h1 = state->h1;

    switch(state->xs_len) {
    case 3:
        k1 ^= state->xs[2] << 16;
    case 2:
        k1 ^= state->xs[1] << 8;
    case 1:
        k1 ^= state->xs[0];

        MURMUR_UPDATE_H1_X86_32(h1, k1);
    };

    //----------
    // finalization

    h1 ^= state->len;

    h1 = fmix32(h1);

    *(uint32_t *)out = h1;
}

void MurmurHash3_x86_32_finalise(MurmurHash3_x86_32_state *state, void *out) {
    MurmurHash3_x86_32_steal(state, out);
    MurmurHash3_x86_32_free(state);
}

void MurmurHash3_x86_32_free(MurmurHash3_x86_32_state *state) {
    g_slice_free(MurmurHash3_x86_32_state, state);
}

uint32_t MurmurHash3_x86_32(const void *key, size_t len, uint32_t seed) {
    uint32_t out;
    MurmurHash3_x86_32_state *state = MurmurHash3_x86_32_new();
    if(seed != 0) {
        MurmurHash3_x86_32_update(state, &seed, sizeof(seed));
    }
    MurmurHash3_x86_32_update(state, key, len);
    MurmurHash3_x86_32_finalise(state, &out);
    return out;
}

//-----------------------------------------------------------------------------

MurmurHash3_x86_128_state *MurmurHash3_x86_128_new(void) {
    return g_slice_new0(MurmurHash3_x86_128_state);
}

MurmurHash3_x86_128_state *MurmurHash3_x86_128_copy(MurmurHash3_x86_128_state *state) {
    return g_slice_copy(sizeof(MurmurHash3_x86_128_state), state);
}


#define MURMUR_UPDATE_H1_X86_128(H, K) MURMUR_UPDATE_X86(H, K, 15, 0x239b961b, 0xab0e9789);
#define MURMUR_UPDATE_H2_X86_128(H, K) MURMUR_UPDATE_X86(H, K, 16, 0xab0e9789, 0x38b34ae5);
#define MURMUR_UPDATE_H3_X86_128(H, K) MURMUR_UPDATE_X86(H, K, 17, 0x38b34ae5, 0xa1e38b93);
#define MURMUR_UPDATE_H4_X86_128(H, K) MURMUR_UPDATE_X86(H, K, 18, 0xa1e38b93, 0x239b961b);

void MurmurHash3_x86_128_update(MurmurHash3_x86_128_state *const state,
                                const void *restrict key, const size_t len) {
    state->len += len;
    uint8_t *data = (uint8_t *)key;
    const uint8_t *stop = data + len;

    if(state->xs_len > 0) {
        MURMUR_FILL_XS(state->xs, state->xs_len, 16, data, len);
    }

    /* process blocks of 16 bytes */
    while(state->xs_len == 16 || data + 16 <= stop) {
        uint32_t k1;
        uint32_t k2;
        uint32_t k3;
        uint32_t k4;

        if(state->xs_len == 16) {
            /* process remnant data from previous update */
            k1 = state->xs32[0];
            k2 = state->xs32[1];
            k3 = state->xs32[2];
            k4 = state->xs32[3];
            state->xs_len = 0;
        } else {
            /* process new data */
            k1 = GET_UINT32(data);
            k2 = GET_UINT32(data + 4);
            k3 = GET_UINT32(data + 8);
            k4 = GET_UINT32(data + 12);
            data += 16;
        }

        MURMUR_UPDATE_H1_X86_128(state->h1, k1);
        MURMUR_MIX_X86(state->h1, state->h2, 19, 0x561ccd1b);

        MURMUR_UPDATE_H2_X86_128(state->h2, k2);
        MURMUR_MIX_X86(state->h2, state->h3, 17, 0x0bcaa747);

        MURMUR_UPDATE_H3_X86_128(state->h3, k3);
        MURMUR_MIX_X86(state->h3, state->h4, 15, 0x96cd1c35);

        MURMUR_UPDATE_H4_X86_128(state->h4, k4);
        MURMUR_MIX_X86(state->h4, state->h1, 13, 0x32ac3b17);
    }

    if(state->xs_len == 0 && stop > data) {
        // store excess data in state
        state->xs_len = stop - data;
        memcpy(state->xs, data, state->xs_len);
    }
}

void MurmurHash3_x86_128_steal(const MurmurHash3_x86_128_state *const restrict state,
                               void *const restrict out) {
    uint32_t k1 = 0;
    uint32_t k2 = 0;
    uint32_t k3 = 0;
    uint32_t k4 = 0;

    /* copy h to make this a non-destructive steal */
    uint32_t h1 = state->h1;
    uint32_t h2 = state->h2;
    uint32_t h3 = state->h3;
    uint32_t h4 = state->h4;

    switch(state->len & 15) {
    case 15:
        k4 ^= state->xs[14] << 16;
    case 14:
        k4 ^= state->xs[13] << 8;
    case 13:
        k4 ^= state->xs[12] << 0;

        MURMUR_UPDATE_H4_X86_128(h4, k4);

    case 12:
        k3 ^= state->xs[11] << 24;
    case 11:
        k3 ^= state->xs[10] << 16;
    case 10:
        k3 ^= state->xs[9] << 8;
    case 9:
        k3 ^= state->xs[8] << 0;

        MURMUR_UPDATE_H3_X86_128(h3, k3);

    case 8:
        k2 ^= state->xs[7] << 24;
    case 7:
        k2 ^= state->xs[6] << 16;
    case 6:
        k2 ^= state->xs[5] << 8;
    case 5:
        k2 ^= state->xs[4] << 0;

        MURMUR_UPDATE_H2_X86_128(h2, k2);

    case 4:
        k1 ^= state->xs[3] << 24;
    case 3:
        k1 ^= state->xs[2] << 16;
    case 2:
        k1 ^= state->xs[1] << 8;
    case 1:
        k1 ^= state->xs[0] << 0;

        MURMUR_UPDATE_H1_X86_128(h1, k1);
    };

    //----------
    // finalization

    h1 ^= state->len;
    h2 ^= state->len;
    h3 ^= state->len;
    h4 ^= state->len;

    h1 += h2;
    h1 += h3;
    h1 += h4;
    h2 += h1;
    h3 += h1;
    h4 += h1;

    h1 = fmix32(h1);
    h2 = fmix32(h2);
    h3 = fmix32(h3);
    h4 = fmix32(h4);

    h1 += h2;
    h1 += h3;
    h1 += h4;
    h2 += h1;
    h3 += h1;
    h4 += h1;

    ((uint32_t *)out)[0] = h1;
    ((uint32_t *)out)[1] = h2;
    ((uint32_t *)out)[2] = h3;
    ((uint32_t *)out)[3] = h4;
}

void MurmurHash3_x86_128_finalise(MurmurHash3_x86_128_state *state, void *out) {
    MurmurHash3_x86_128_steal(state, out);
    MurmurHash3_x86_128_free(state);
}

void MurmurHash3_x86_128_free(MurmurHash3_x86_128_state *state) {
    g_slice_free(MurmurHash3_x86_128_state, state);
}

void MurmurHash3_x86_128(const void *key, size_t len, uint32_t seed, void *out) {
    MurmurHash3_x86_128_state *state = MurmurHash3_x86_128_new();
    if(seed != 0) {
        MurmurHash3_x86_128_update(state, &seed, sizeof(seed));
    }
    MurmurHash3_x86_128_update(state, key, len);
    MurmurHash3_x86_128_finalise(state, out);
}

//-----------------------------------------------------------------------------

MurmurHash3_x64_128_state *MurmurHash3_x64_128_new(void) {
    return g_slice_new0(MurmurHash3_x64_128_state);
}

MurmurHash3_x64_128_state *MurmurHash3_x64_128_copy(MurmurHash3_x64_128_state *state) {
    return g_slice_copy(sizeof(MurmurHash3_x64_128_state), state);
}

#define MURMUR_UPDATE_H1_X64_128(H1)                            \
    MURMUR_UPDATE_X64(H1, k1, 31, BIG_CONSTANT(0x87c37b91114253d5), \
                  BIG_CONSTANT(0x4cf5ad432745937f));
#define MURMUR_UPDATE_H2_X64_128(H2)                            \
    MURMUR_UPDATE_X64(H2, k2, 33, BIG_CONSTANT(0x4cf5ad432745937f), \
                  BIG_CONSTANT(0x87c37b91114253d5));

void MurmurHash3_x64_128_update(MurmurHash3_x64_128_state *const restrict state,
                                const void *restrict key, const size_t len) {
    state->len += len;
    uint8_t *data = (uint8_t *)key;
    const uint8_t *stop = data + len;

    if(state->xs_len > 0) {
        MURMUR_FILL_XS(state->xs, state->xs_len, 16, data, len);
    }

    /* process blocks of 16 bytes */
    while(state->xs_len == 16 || data + 16 <= stop) {
        uint64_t k1;
        uint64_t k2;

        if(state->xs_len == 16) {
            /* process remnant data from previous update */
            k1 = state->xs64[0];
            k2 = state->xs64[1];
            state->xs_len = 0;
        } else {
            /* process new data */
            k1 = GET_UINT64(data);
            k2 = GET_UINT64(data + 8);
            data += 16;
        }

        MURMUR_UPDATE_H1_X64_128(state->h1);
        MURMUR_MIX_X64(state->h1, state->h2, 27, 0x52dce729);

        MURMUR_UPDATE_H2_X64_128(state->h2);
        MURMUR_MIX_X64(state->h2, state->h1, 31, 0x38495ab5);
    }

    if(state->xs_len == 0 && stop > data) {
        // store excess data in state
        state->xs_len = stop - data;
        memcpy(state->xs, data, state->xs_len);
    }
}

void MurmurHash3_x64_128_steal(const MurmurHash3_x64_128_state *const restrict state,
                               void *const restrict out) {
    uint64_t k1 = 0;
    uint64_t k2 = 0;

    /* copy h to make this a non-destructive steal */
    uint64_t h1 = state->h1;
    uint64_t h2 = state->h2;

    switch(state->xs_len) {
    case 15:
        k2 ^= (uint64_t)(state->xs[14]) << 48;
    case 14:
        k2 ^= (uint64_t)(state->xs[13]) << 40;
    case 13:
        k2 ^= (uint64_t)(state->xs[12]) << 32;
    case 12:
        k2 ^= (uint64_t)(state->xs[11]) << 24;
    case 11:
        k2 ^= (uint64_t)(state->xs[10]) << 16;
    case 10:
        k2 ^= (uint64_t)(state->xs[9]) << 8;
    case 9:
        k2 ^= (uint64_t)(state->xs[8]) << 0;

        MURMUR_UPDATE_H2_X64_128(h2);

    case 8:
        k1 ^= (uint64_t)(state->xs[7]) << 56;
    case 7:
        k1 ^= (uint64_t)(state->xs[6]) << 48;
    case 6:
        k1 ^= (uint64_t)(state->xs[5]) << 40;
    case 5:
        k1 ^= (uint64_t)(state->xs[4]) << 32;
    case 4:
        k1 ^= (uint64_t)(state->xs[3]) << 24;
    case 3:
        k1 ^= (uint64_t)(state->xs[2]) << 16;
    case 2:
        k1 ^= (uint64_t)(state->xs[1]) << 8;
    case 1:
        k1 ^= (uint64_t)(state->xs[0]) << 0;

        MURMUR_UPDATE_H1_X64_128(h1);
    };

    //----------
    // finalization

    h1 ^= state->len;
    h2 ^= state->len;

    h1 += h2;
    h2 += h1;

    h1 = fmix64(h1);
    h2 = fmix64(h2);

    h1 += h2;
    h2 += h1;

    ((uint64_t *)out)[0] = h1;
    ((uint64_t *)out)[1] = h2;
}

void MurmurHash3_x64_128_free(MurmurHash3_x64_128_state *state) {
    g_slice_free(MurmurHash3_x64_128_state, state);
}

void MurmurHash3_x64_128(const void *key, const size_t len, const uint32_t seed,
                         void *out) {
    MurmurHash3_x64_128_state *state = MurmurHash3_x64_128_new();
    if(seed != 0) {
        MurmurHash3_x64_128_update(state, &seed, sizeof(seed));
    }
    MurmurHash3_x64_128_update(state, key, len);
    MurmurHash3_x64_128_finalise(state, out);
}

void MurmurHash3_x64_128_finalise(MurmurHash3_x64_128_state *state, void *out) {
    MurmurHash3_x64_128_steal(state, out);
    MurmurHash3_x64_128_free(state);
}

int MurmurHash3_x64_128_equal(MurmurHash3_x64_128_state *a,
                              MurmurHash3_x64_128_state *b) {
    if(a->h1 != b->h1 || a->h2 != b->h2 || a->xs_len != b->xs_len || a->len != b->len) {
        return 0;
    }
    return (a->xs_len == 0 || !memcmp(a->xs, b->xs, a->xs_len));
}

//-----------------------------------------------------------------------------
