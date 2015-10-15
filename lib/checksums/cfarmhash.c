#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define bswap32(x) __builtin_bswap32(x)
#define bswap64(x) __builtin_bswap64(x)

#ifdef FARMHASH_BIG_ENDIAN
#define uint32_in_expected_order(x) bswap32(x)
#define uint64_in_expected_order(x) bswap64(x)
#else
#define uint32_in_expected_order(x) (x)
#define uint64_in_expected_order(x) (x)
#endif

// Some primes between 2^63 and 2^64 for various uses.

static const uint64_t k0 = 0xc3a5c85c97cb3127ULL;
static const uint64_t k1 = 0xb492b66fbe98f273ULL;
static const uint64_t k2 = 0x9ae16a3b2f90404fULL;

static inline uint32_t fetch32(const char *p)
{
  uint32_t result;

  memcpy(&result, p, sizeof(result));

  return uint32_in_expected_order(result);
}

static inline uint64_t fetch64(const char *p)
{
  uint64_t result;
  
  memcpy(&result, p, sizeof(result));

  return uint64_in_expected_order(result);
}

static inline uint64_t shift_mix(uint64_t v)
{
  return v ^ (v >> 47);
}

static inline uint64_t rotate64(uint64_t v, int shift)
{
  return ((v >> shift) | (v << (64 - shift)));
}

static inline uint64_t hash_len_16(uint64_t u, uint64_t v, uint64_t mul)
{
  uint64_t a, b;
  
  a = (u ^ v) * mul;
  a ^= (a >> 47);
  b = (v ^ a) * mul;
  b ^= (b >> 47);
  b *= mul;

  return b;
}

static inline uint64_t hash_len_0_to_16(const char *s, size_t len)
{
  if (len >= 8)
    {
      uint64_t mul = k2 + len * 2;
      uint64_t a = fetch64(s) + k2;
      uint64_t b = fetch64(s + len - 8);
      uint64_t c = rotate64(b, 37) * mul + a;
      uint64_t d = (rotate64(a, 25) + b) * mul;
      return hash_len_16(c, d, mul);
  }
  
  if (len >= 4)
    {
      uint64_t mul = k2 + len * 2;
      uint64_t a = fetch32(s);
      return hash_len_16(len + (a << 3), fetch32(s + len - 4), mul);
    }
  
  if (len > 0)
    {
      uint8_t a = s[0];
      uint8_t b = s[len >> 1];
      uint8_t c = s[len - 1];
      uint32_t y = (uint32_t) a + ((uint32_t) b << 8);
      uint32_t z = len + ((uint32_t) c << 2);
      return shift_mix(y * k2 ^ z * k0) * k2;
    }
  
  return k2;
}

static inline uint64_t hash_len_17_to_32(const char *s, size_t len)
{
  uint64_t mul = k2 + len * 2;
  uint64_t a = fetch64(s) * k1;
  uint64_t b = fetch64(s + 8);
  uint64_t c = fetch64(s + len - 8) * mul;
  uint64_t d = fetch64(s + len - 16) * k2;
  
  return hash_len_16(rotate64(a + b, 43) + rotate64(c, 30) + d,
		     a + rotate64(b + k2, 18) + c, mul);
}

static inline uint64_t hash_len_33_to_64(const char *s, size_t len)
{
  uint64_t mul = k2 + len * 2;
  uint64_t a = fetch64(s) * k2;
  uint64_t b = fetch64(s + 8);
  uint64_t c = fetch64(s + len - 8) * mul;
  uint64_t d = fetch64(s + len - 16) * k2;
  uint64_t y = rotate64(a + b, 43) + rotate64(c, 30) + d;
  uint64_t z = hash_len_16(y, a + rotate64(b + k2, 18) + c, mul);
  uint64_t e = fetch64(s + 16) * mul;
  uint64_t f = fetch64(s + 24);
  uint64_t g = (y + fetch64(s + len - 32)) * mul;
  uint64_t h = (z + fetch64(s + len - 24)) * mul;
  
  return hash_len_16(rotate64(e + f, 43) + rotate64(g, 30) + h,
                   e + rotate64(f + a, 18) + g, mul);
}

#define swap(x, y) do {(x) = (x) ^ (y); (y) = (x) ^ (y); (x) = (x) ^ y;} while(0); 

typedef struct pair64 pair64;

struct pair64
{
  uint64_t first;
  uint64_t second;
};

static inline pair64 weak_hash_len_32_with_seeds2(uint64_t w, uint64_t x, uint64_t y,
						  uint64_t z, uint64_t a, uint64_t b)
{
  uint64_t c;
  pair64 result;
  
  a += w;
  b = rotate64(b + a + z, 21);
  c = a;
  a += x;
  a += y;
  b += rotate64(a, 44);
  result.first = a + z;
  result.second = b + c;
  
  return result;
}

static inline pair64 weak_hash_len_32_with_seeds(const char *s, uint64_t a, uint64_t b)
{
  return weak_hash_len_32_with_seeds2(fetch64(s), fetch64(s + 8), fetch64(s + 16),
				      fetch64(s + 24), a, b);
}

uint64_t cfarmhash(const char *s, size_t len)
{
  uint64_t mul;
  const uint64_t seed = 81;

  if (len <= 16)
    return hash_len_0_to_16(s, len);
  
  if (len <= 32)
    return hash_len_17_to_32(s, len);

  if (len <= 64)  
    return hash_len_33_to_64(s, len);
  
  uint64_t x = seed, y = seed * k1 + 113, z = shift_mix(y * k2 + 113) * k2;
  pair64 v = {0, 0}, w = {0, 0};

  x = x * k2 + fetch64(s);

  const char *end = s + ((len - 1) / 64) * 64;
  const char *last64 = end + ((len - 1) & 63) - 63;

  do
    {
      x = rotate64(x + y + v.first + fetch64(s + 8), 37) * k1;
      y = rotate64(y + v.second + fetch64(s + 48), 42) * k1;
      x ^= w.second;
      y += v.first + fetch64(s + 40);
      z = rotate64(z + w.first, 33) * k1;
      v = weak_hash_len_32_with_seeds(s, v.second * k1, x + w.first);
      w = weak_hash_len_32_with_seeds(s + 32, z + w.second, y + fetch64(s + 16));
      swap(z, x);
      s += 64;
    }
  while (s != end);

  mul = k1 + ((z & 0xff) << 1);
  s = last64;
  w.first += ((len - 1) & 63);
  v.first += w.first;
  w.first += v.first;
  x = rotate64(x + y + v.first + fetch64(s + 8), 37) * mul;
  y = rotate64(y + v.second + fetch64(s + 48), 42) * mul;
  x ^= w.second * 9;
  y += v.first * 9 + fetch64(s + 40);
  z = rotate64(z + w.first, 33) * mul;
  v = weak_hash_len_32_with_seeds(s, v.second * mul, x + w.first);
  w = weak_hash_len_32_with_seeds(s + 32, z + w.second, y + fetch64(s + 16));
  swap(z, x);

  return hash_len_16(hash_len_16(v.first, w.first, mul) + shift_mix(y) * k0 + z,
		     hash_len_16(v.second, w.second, mul) + x,
		     mul);
}
