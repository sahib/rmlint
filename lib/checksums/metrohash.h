// metrohash.h
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

#ifndef METROHASH_METROHASH_H
#define METROHASH_METROHASH_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "../config.h"

typedef struct _Metro64_state Metro64State;
typedef struct _Metro128_state Metro128State;
typedef struct _Metro256_state Metro256State;

// MetroHash 64-bit hash functions
void metrohash64_1(const uint8_t *key, size_t len, uint32_t seed, uint8_t *out);
void metrohash64_2(const uint8_t *key, size_t len, uint32_t seed, uint8_t *out);

// MetroHash 128-bit hash functions
Metro128State *metrohash128_1_new(bool use_sse);
Metro128State *metrohash128_2_new(bool use_sse);
Metro256State *metrohash256_new(bool use_sse);

Metro128State *metrohash128_copy(Metro128State *state);
Metro256State *metrohash256_copy(Metro256State *state);

void metrohash128_free(Metro128State *state);
void metrohash256_free(Metro256State *state);

void metrohash128_1(const uint8_t *key, size_t len, uint32_t seed, uint8_t *out);
void metrohash128_2(const uint8_t *key, size_t len, uint32_t seed, uint8_t *out);

void metrohash128_1_update(Metro128State *state, const uint8_t *key, size_t len);
void metrohash128_1_steal(Metro128State *state, uint8_t *out);

void metrohash128_2_update(Metro128State *state, const uint8_t *key, size_t len);
void metrohash128_2_steal(Metro128State *state, uint8_t *out);

void metrohash256_update(Metro256State *state, const uint8_t *key, size_t len);
void metrohash256_steal(Metro256State *state, uint8_t *out);

#if HAVE_MM_CRC32_U64
// MetroHash 128-bit hash functions using CRC instruction
void metrohash128crc_1(const uint8_t *key, size_t len, uint32_t seed, uint8_t *out);
void metrohash128crc_2(const uint8_t *key, size_t len, uint32_t seed, uint8_t *out);

void metrohash128crc_1_update(Metro128State *state, const uint8_t *key, size_t len);
void metrohash128crc_2_update(Metro128State *state, const uint8_t *key, size_t len);

void metrohash128crc_1_steal(Metro128State *state, uint8_t *out);
void metrohash128crc_2_steal(Metro128State *state, uint8_t *out);

void metrohash256crc_update(Metro256State *state, const uint8_t *key, size_t len);
void metrohash256crc_steal(Metro256State *state, uint8_t *out);
#endif

/* rotate right idiom recognized by compiler*/
inline static uint64_t rotate_right(uint64_t v, unsigned k) {
    return (v >> k) | (v << (64 - k));
}

// unaligned reads, fast and safe on Nehalem and later microarchitectures
inline static uint64_t read_u64(const void *const ptr) {
    return *(uint64_t *)ptr;
}

inline static uint64_t read_u32(const void *const ptr) {
    return *(uint32_t *)ptr;
}

inline static uint64_t read_u16(const void *const ptr) {
    return *(uint16_t *)ptr;
}

inline static uint64_t read_u8(const void *const ptr) {
    return *(uint8_t *)ptr;
}

#endif  // #ifndef METROHASH_METROHASH_H
