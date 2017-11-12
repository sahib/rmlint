//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the
// public domain. The author hereby disclaims copyright to this source
// code.

// Streaming implementation by Daniel Thomas

#ifndef _MURMURHASH3_H_
#define _MURMURHASH3_H_

#include <stdint.h>

//-----------------------------------------------------------------------------
// opaque structs for intermediate checksum states

typedef struct _MurmurHash3_x86_32_state MurmurHash3_x86_32_state;
typedef struct _MurmurHash3_x86_128_state MurmurHash3_x86_128_state;
typedef struct _MurmurHash3_x64_128_state MurmurHash3_x64_128_state;

//-----------------------------------------------------------------------------
// API

/**
 * return newly initialised, seeded state
 */
MurmurHash3_x86_32_state *MurmurHash3_x86_32_new(uint32_t seed);
MurmurHash3_x86_128_state *MurmurHash3_x86_128_new(uint32_t seed1, uint32_t seed2, uint32_t seed3, uint32_t seed4);
MurmurHash3_x64_128_state *MurmurHash3_x64_128_new(uint64_t seed1, uint64_t seed2);

/**
 * return duplicate copy of a state
 */
MurmurHash3_x86_32_state *MurmurHash3_x86_32_copy(MurmurHash3_x86_32_state *state);
MurmurHash3_x86_128_state *MurmurHash3_x86_128_copy(MurmurHash3_x86_128_state *state);
MurmurHash3_x64_128_state *MurmurHash3_x64_128_copy(MurmurHash3_x64_128_state *state);

/**
 * streaming update of checksum
 */
void MurmurHash3_x86_32_update(MurmurHash3_x86_32_state *const restrict state, const void *restrict key, const uint32_t len);
void MurmurHash3_x86_128_update(MurmurHash3_x86_128_state *const restrict state, const void *restrict key, const uint32_t len);
void MurmurHash3_x64_128_update(MurmurHash3_x64_128_state *const restrict state, const void *restrict key, const uint64_t len);

/**
 * output checksum result; does not modify underlying state
 */
void MurmurHash3_x86_32_steal(const MurmurHash3_x86_32_state  *const restrict state, void *const restrict out);
void MurmurHash3_x86_128_steal(const MurmurHash3_x86_128_state *const restrict state, void *const restrict out);
void MurmurHash3_x64_128_steal(const MurmurHash3_x64_128_state *const restrict state, void *const restrict out);

/**
 * output checksum result; frees state
 */
void MurmurHash3_x86_32_finalise(MurmurHash3_x86_32_state *state, void *out);
void MurmurHash3_x86_128_finalise(MurmurHash3_x86_128_state *state, void *out);
void MurmurHash3_x64_128_finalise(MurmurHash3_x64_128_state *state, void *out);

/**
 * free state
 */
void MurmurHash3_x86_32_free(MurmurHash3_x86_32_state  *state);
void MurmurHash3_x86_128_free(MurmurHash3_x86_128_state *state);
void MurmurHash3_x64_128_free(MurmurHash3_x64_128_state *state);

/**
 * convenience single-buffer hash
 */
void MurmurHash3_x86_32(const void *key, uint32_t len, uint32_t seed, void *out);
void MurmurHash3_x86_128(const void *key, uint32_t len, uint32_t seed, void *out);
void MurmurHash3_x64_128(const void *key, uint64_t len, uint32_t seed, void *out);


//-----------------------------------------------------------------------------

#endif  // _MURMURHASH3_H_
