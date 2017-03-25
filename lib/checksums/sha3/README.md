# A single-file C implementation of SHA-3 with Init/Update/Finalize API

The purpose of this project is:

* provide an API that hashes bytes, not bits
* provide a simple __single-file__ reference implementation of a SHA-3 message digest algorithm, as defined in the [FIPS 202][fips202_standard] standard;
* implement the hashing API that employs the __IUF__ paradigm (or `Init`, `Update`, `Finalize` style).
* answer the design questions, such as:
  * what does the state for IUF look like?
  * how small can the state be (224 bytes on a 64-bit system for a unified SHA-3 algorithm)
  * what is the incremental cost of adding e.g. SHA3-384 to a SHA3-256 implementation?

The implementation is written in C and uses `uint64_t` types to manage the state. The code will compile and run on 64-bit and 32-bit architectures (`gcc` and `gcc -m32` on `x86_64` were tested).

[fips202_standard]: http://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.202.pdf "FIPS 202 standard"

## License, prior work

This work is in public domain. 

I would appreciate any attribution to this work if you used the code or ideas. I thank you for this in advance.

This is a clean-room implementation of IUF API for SHA3. The `keccakf()` is based on the code from [keccak.noekeon.org](http://keccak.noekeon.org/).

1600-bit message hashing test vectors are [NIST test vectors](http://csrc.nist.gov/groups/ST/toolkit/examples.html).

## Overview of the API

Let's hash 'abc' with SHA3-256 using two methods: single buffer (but using IUF paradigm), and using the IUF API. 

    sha3_context c;
    uint8_t *hash;

Single-buffer hashing:

    sha3_Init256(&c);
    sha3_Update(&c, "abc", 3);
    hash = sha3_Finalize(&c);
    // 'hash' points to a buffer inside 'c'
    // with the value of SHA3-256

Alternatively, IUF hashing:

    sha3_Init256(&c);
    sha3_Update(&c, "a", 1);
    sha3_Update(&c, "bc", 2);
    hash = sha3_Finalize(&c);

    // no free for 'c' is needed

The `hash` points to the same `256/8=32` bytes in both cases.

## Self-tests

    $ gcc -Wall sha3.c -o _ && ./_
    SHA3-256, SHA3-384, SHA3-512 tests passed OK

or 

    $ gcc -m32 Wall sha3.c -o _ && ./_
    SHA3-256, SHA3-384, SHA3-512 tests passed OK

## API

* the same `sha3_context` object maintains the state for SHA3-256, SHA3-384, or SHA3-512 algorithm;
* the hash algorithm used is determined by how the context was initialized with `sha3_InitX`, e.g. `sha3_Init256`, `sha3_Init384`, or `sha3_Init512` call;
* `sha3_Update` and `sha3_Finalize` are the same for regardless the type of the algorithm (`X`);
* the buffer returned by `sha3_Finalize` will have `X` bits of hash;
* `sha3_InitX` works also as Reset or Free (zeroization) of the hash context.

See [`sha3.c`](sha3.c) for details.

## Notes

SHA3-224 is not supported, but can easily be added.

The code was written to work with the Microsoft Visual Studio compiler (under `_MSC_VER`), but this build target was not tested.

This project was created to support [SHA3 in OpenPGP](https://tools.ietf.org/html/draft-jivsov-openpgp-sha3) work, but it applies to other protocols and formats, e.g. TLS.

