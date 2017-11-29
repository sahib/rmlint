//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the
// public domain. The author hereby disclaims copyright to this source
// code.

// Streaming implementation by Daniel Thomas

#ifndef _MURMURHASH3_H_
#define _MURMURHASH3_H_

#include <stdint.h>
#include <stddef.h>

//-----------------------------------------------------------------------------
// opaque structs for intermediate checksum states

typedef struct _MurmurHash3_x86_32_state MurmurHash3_x86_32_state;
typedef struct _MurmurHash3_x86_128_state MurmurHash3_x86_128_state;
typedef struct _MurmurHash3_x64_128_state MurmurHash3_x64_128_state;

//-----------------------------------------------------------------------------
// API

/**
 * return newly initialised state
 */
MurmurHash3_x86_32_state *MurmurHash3_x86_32_new(void);
MurmurHash3_x86_128_state *MurmurHash3_x86_128_new(void);
MurmurHash3_x64_128_state *MurmurHash3_x64_128_new(void);

/**
 * return duplicate copy of a state
 */
MurmurHash3_x86_32_state *MurmurHash3_x86_32_copy(MurmurHash3_x86_32_state *state);
MurmurHash3_x86_128_state *MurmurHash3_x86_128_copy(MurmurHash3_x86_128_state *state);
MurmurHash3_x64_128_state *MurmurHash3_x64_128_copy(MurmurHash3_x64_128_state *state);

/**
 * streaming update of checksum
 */
void MurmurHash3_x86_32_update(MurmurHash3_x86_32_state *const restrict state,
                               const void *restrict key, const size_t len);
void MurmurHash3_x86_128_update(MurmurHash3_x86_128_state *const restrict state,
                                const void *restrict key, const size_t len);
void MurmurHash3_x64_128_update(MurmurHash3_x64_128_state *const restrict state,
                                const void *restrict key, const size_t len);

/**
 * output checksum result; does not modify underlying state
 */
void MurmurHash3_x86_32_steal(const MurmurHash3_x86_32_state *const restrict state,
                              void *const restrict out);
void MurmurHash3_x86_128_steal(const MurmurHash3_x86_128_state *const restrict state,
                               void *const restrict out);
void MurmurHash3_x64_128_steal(const MurmurHash3_x64_128_state *const restrict state,
                               void *const restrict out);

/**
 * output checksum result; frees state
 */
void MurmurHash3_x86_32_finalise(MurmurHash3_x86_32_state *state, void *out);
void MurmurHash3_x86_128_finalise(MurmurHash3_x86_128_state *state, void *out);
void MurmurHash3_x64_128_finalise(MurmurHash3_x64_128_state *state, void *out);

/**
 * free state
 */
void MurmurHash3_x86_32_free(MurmurHash3_x86_32_state *state);
void MurmurHash3_x86_128_free(MurmurHash3_x86_128_state *state);
void MurmurHash3_x64_128_free(MurmurHash3_x64_128_state *state);

/**
 * convenience single-buffer hash
 */
uint32_t MurmurHash3_x86_32(const void *key, size_t len, uint32_t seed);
void MurmurHash3_x86_128(const void *key, size_t len, uint32_t seed, void *out);
void MurmurHash3_x64_128(const void *key, size_t len, uint32_t seed, void *out);

//-----------------------------------------------------------------------------

#endif  // _MURMURHASH3_H_
