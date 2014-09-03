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
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
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

/* Less than 16 byte is not allowed */
G_STATIC_ASSERT(_RM_HASH_LEN >= 16);

RmDigestType rm_string_to_digest_type(const char *string) {
    if(string == NULL) {
        return RM_DIGEST_UNKNOWN;
    } else if(!strcasecmp(string, "md5")) {
        return RM_DIGEST_MD5;
    } else
#if _RM_HASH_LEN >= 64
        if(!strcasecmp(string, "sha512")) {
            return RM_DIGEST_SHA512;
        } else if(!strcasecmp(string, "city512")) {
            return RM_DIGEST_CITY512;
        } else if(!strcasecmp(string, "murmur512")) {
            return RM_DIGEST_MURMUR512;
        } else
#endif
#if _RM_HASH_LEN >= 32
            if(!strcasecmp(string, "sha256")) {
                return RM_DIGEST_SHA256;
            } else if(!strcasecmp(string, "city256")) {
                return RM_DIGEST_CITY256;
            } else if(!strcasecmp(string, "murmur256")) {
                return RM_DIGEST_MURMUR256;
            } else
#endif
#if _RM_HASH_LEN >= 20
                if(!strcasecmp(string, "sha1")) {
                    return RM_DIGEST_SHA1;
                } else
#endif
                    if(!strcasecmp(string, "murmur")) {
                        return RM_DIGEST_MURMUR;
                    } else if(!strcasecmp(string, "spooky")) {
                        return RM_DIGEST_SPOOKY;
                    } else if(!strcasecmp(string, "city")) {
                        return RM_DIGEST_CITY;
                    } else {
                        return RM_DIGEST_UNKNOWN;
                    }
}

#define add_seed(digest, seed) {                                                           \
    if(seed) {                                                                             \
        g_checksum_update(digest->glib_checksum, (const guchar *)&seed, sizeof(uint64_t)); \
    }                                                                                      \
}


void rm_digest_init(RmDigest *digest, RmDigestType type, uint64_t seed1, uint64_t seed2) {
    digest->type = type;
    digest->num_128bit_blocks = 1;

    switch(type) {
    case RM_DIGEST_MD5:
        digest->glib_checksum = g_checksum_new(G_CHECKSUM_MD5);
        add_seed(digest, seed1);
        break;
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_SHA512:
        digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA512);
        add_seed(digest, seed1);
        break;
#endif
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_SHA256:
        digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA256);
        add_seed(digest, seed1);
        break;
#endif
#if _RM_HASH_LEN >= 20
    case RM_DIGEST_SHA1:
        digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA1);
        add_seed(digest, seed1);
        break;
#endif
    case RM_DIGEST_SPOOKY:
        digest->hash[0].first = seed1;
        digest->hash[0].second = seed2;
        break;
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_CITY512:
        digest->num_128bit_blocks += 2;
        digest->hash[3].first  = 0xaaaaaaaaaaaaaaaa ^ seed1;
        digest->hash[3].second = 0xaaaaaaaaaaaaaaaa ^ seed2;
        digest->hash[2].first  = 0x3333333333333333 ^ seed1;
        digest->hash[2].second = 0x3333333333333333 ^ seed2;
        /* Fallthrough */
#endif
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_CITY256:
        digest->num_128bit_blocks += 1;
        digest->hash[1].first  = 0xf0f0f0f0f0f0f0f0 ^ seed1;
        digest->hash[1].second = 0xf0f0f0f0f0f0f0f0 ^ seed2;
        /* Fallthrough */
#endif
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
        digest->hash[0].first  = seed1;
        digest->hash[0].second = seed2;
        break;
    default:
        g_assert_not_reached();
    }
}

void rm_digest_update(RmDigest *digest, const unsigned char *data, guint64 size) {
    switch(digest->type) {
    case RM_DIGEST_MD5:
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_SHA512:
#endif
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_SHA256:
#endif
#if _RM_HASH_LEN >= 20
    case RM_DIGEST_SHA1:
#endif
        g_checksum_update(digest->glib_checksum, (const guchar *)data, size);
        break;
    case RM_DIGEST_SPOOKY:
        spooky_hash128(data, size, &digest->hash[0].first, &digest->hash[0].second);
        break;
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_MURMUR512:
#endif
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_MURMUR256:
#endif
    case RM_DIGEST_MURMUR:
        for (guint8 i = 0; i < digest->num_128bit_blocks; i++) {
#if UINTPTR_MAX == 0xffffffff
            /* 32 bit */
            MurmurHash3_x86_128(data, size, (uint32_t)digest->hash[i].first, &digest->hash[i]);
#elif UINTPTR_MAX == 0xffffffffffffffff
            /* 64 bit */
            MurmurHash3_x64_128(data, size, (uint32_t)digest->hash[i].first, &digest->hash[i]);
#else
            /* 16 bit or unknown */
#error "Probably not a good idea to compile rmlint on 16bit."
#endif
        }
        break;
    case RM_DIGEST_CITY:
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_CITY256:
#endif
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_CITY512:
#endif
        for (guint8 i = 0; i < digest->num_128bit_blocks; i++) {
            /* Opt out for the more optimized version.
            * This needs the crc command of sse4.2
            * (available on Intel Nehalem and up; my amd box doesn't have this though)
            */
#ifdef __sse4_2__
            digest->hash[i] = CityHashCrc128WithSeed((const char *)data, size, digest->hash[i]);
#else
            digest->hash[i] = CityHash128WithSeed((const char *) data, size, digest->hash[i]);
#endif
        }
        break;
    default:
        g_assert_not_reached();
    }
}

RmDigest *rm_digest_copy(RmDigest *digest) {
    RmDigest *self = g_slice_new0(RmDigest);
    self->type = digest->type;

    switch(digest->type) {
    case RM_DIGEST_MD5:
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_SHA512:
#endif
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_SHA256:
#endif
#if _RM_HASH_LEN >= 20
    case RM_DIGEST_SHA1:
#endif
        self->glib_checksum = g_checksum_copy(digest->glib_checksum);
        break;
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_CITY256:
    case RM_DIGEST_MURMUR256:
#endif
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_CITY512:
    case RM_DIGEST_MURMUR512:
#endif
        for (int i = 0; i < digest->num_128bit_blocks; i++) {
            self->hash[i].first = digest->hash[i].first;
            self->hash[i].second = digest->hash[i].second;
        }
        break;
    default:
        g_assert_not_reached();
    }
    return self;
}

int rm_digest_steal_buffer(RmDigest *digest, guint8 *buf, gsize buflen) {
    RmDigest *copy = rm_digest_copy(digest);
    gsize bytes_written = 0;

    switch(copy->type) {
    case RM_DIGEST_MD5:
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_SHA512:
#endif
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_SHA256:
#endif
#if _RM_HASH_LEN >= 20
    case RM_DIGEST_SHA1:
#endif
        g_checksum_get_digest(copy->glib_checksum, buf, &buflen);
        bytes_written = buflen;
        break;
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_CITY256:
    case RM_DIGEST_MURMUR256:
#endif
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_CITY512:
    case RM_DIGEST_MURMUR512:
#endif
        memcpy(buf, copy->hash, sizeof(uint128) * digest->num_128bit_blocks);
        bytes_written = 16 * digest->num_128bit_blocks;
        break;
    default:
        g_assert_not_reached();
    }

    rm_digest_finalize(copy);
    g_slice_free(RmDigest, copy);
    return bytes_written;
}

int rm_digest_hexstring(RmDigest *digest, char *buffer) {
    static const char *hex = "0123456789abcdef";

    guint8 input[_RM_HASH_LEN];
    memset(input, 0, sizeof(input));
    gsize digest_len = rm_digest_steal_buffer(digest, input, sizeof(input));

    for(gsize i = 0; i < digest_len; ++i) {
        buffer[0] = hex[input[i] / 16];
        buffer[1] = hex[input[i] % 16];

        if(i == digest_len - 1) {
            buffer[2] = '\0';
        }

        buffer += 2;
    }

    return digest_len * 2 + 1;
}

int rm_digest_compare(RmDigest *a, RmDigest *b) {
    guint8 buf_a[_RM_HASH_LEN];
    guint8 buf_b[_RM_HASH_LEN];

    memset(buf_a, 0, sizeof(buf_a));
    memset(buf_b, 0, sizeof(buf_b));

    gsize len_a = rm_digest_steal_buffer(a, buf_a, sizeof(buf_a));
    gsize len_b = rm_digest_steal_buffer(b, buf_b, sizeof(buf_b));

    if(len_a != len_b) {
        return len_a < len_b;
    }

    return memcmp(buf_a, buf_b, MIN(len_a, len_b));
}

void rm_digest_finalize(RmDigest *digest) {
    switch(digest->type) {
    case RM_DIGEST_MD5:
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_SHA512:
#endif
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_SHA256:
#endif
#if _RM_HASH_LEN >= 20
    case RM_DIGEST_SHA1:
#endif
        g_checksum_free(digest->glib_checksum);
        break;
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
#if _RM_HASH_LEN >= 32
    case RM_DIGEST_CITY256:
    case RM_DIGEST_MURMUR256:
#endif
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_CITY512:
    case RM_DIGEST_MURMUR512:
#endif
        break;
    default:
        g_assert_not_reached();
    }
}

#ifdef _RM_COMPILE_MAIN_CKSUM

#include <alloca.h>
#include <sys/types.h>
#include <sys/uio.h>

/* Use this to compile:
* $ gcc src/checksum.c src/checksums/ *.c -Wextra -Wall $(pkg-config --libs --cflags glib-2.0) -std=c11 -msse4a -O4 -D_GNU_SOURCE -D_RM_COMPILE_MAIN
* $ ./a.out mmap <some_file[s]>
*/

static int rm_hash_file(const char *file, RmDigestType type, double buf_size_mb, char *buffer) {
    ssize_t bytes = 0;
    const int N = buf_size_mb * 1024 * 1024;
    unsigned char *data = g_alloca(N);
    FILE *fd = fopen(file, "rb");

    /* Can't open file? */
    if(fd == NULL) {
        return 0;
    }

    RmDigest digest;
    rm_digest_init(&digest, type, 0, 0);

    do {
        if((bytes = fread(data, 1, N, fd)) == -1) {
            printf("ERROR:read()");
        } else if(bytes > 0) {
            rm_digest_update(&digest, data, bytes);

            gsize digest_len = rm_digest_hexstring(&digest, buffer);
        }
    } while(bytes > 0);

    fclose(fd);

    gsize digest_len = rm_digest_hexstring(&digest, buffer);
    rm_digest_finalize(&digest);
    return digest_len;
}

static int rm_hash_file_mmap(const char *file, RmDigestType type, G_GNUC_UNUSED double buf_size_mb, char *buffer) {
    int fd = 0;
    unsigned char *f_map = NULL;


    if((fd = open(file, O_RDONLY)) == -1) {
        perror("ERROR:sys:open()");
        return 0;
    }

    RmDigest digest;
    rm_digest_init(&digest, type, 0, 0);

    struct stat stat_buf;
    stat(file, &stat_buf);

    f_map = mmap(NULL, stat_buf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if(f_map != MAP_FAILED) {
        if(madvise(f_map, stat_buf.st_size, MADV_WILLNEED) == -1) {
            perror("madvise");
        }

        rm_digest_update(&digest, f_map, stat_buf.st_size);
        munmap(f_map, stat_buf.st_size);

    } else {
        perror("ERROR:hash_file->mmap");
    }

    if(close(fd) == -1) {
        perror("ERROR:close()");
    }

    gsize digest_len = rm_digest_hexstring(&digest, buffer);
    rm_digest_finalize(&digest);
    return digest_len;
}

static int rm_hash_file_readv(const char *file, RmDigestType type, G_GNUC_UNUSED double buf_size_mb, char *buffer) {
    const int N = 4;
    const int S = 4096 * 4;
    struct iovec readvec[N];
    for(int i = 0; i < N; ++i) {
        readvec[i].iov_base = alloca(S);
        readvec[i].iov_len = S;
    }

    RmDigest digest;
    rm_digest_init(&digest, type, 0, 0);

    int bytes = 0;
    int fd = open(file, O_RDONLY);
    while((bytes = readv(fd, readvec, N)) > 0) {
        int blocks = bytes / S + 1;
        int remainder = bytes % S;
        for(int i = 0; i < blocks; ++i) {
            rm_digest_update(&digest, readvec[i].iov_base, (i == blocks - 1) ? remainder : S);
        }
    }

    gsize digest_len = rm_digest_hexstring(&digest, buffer);
    rm_digest_finalize(&digest);

    close(fd);
    return digest_len;
}


int main(int argc, char **argv) {
    if(argc < 3) {
        printf("Specify a type and a file\n");
        return EXIT_FAILURE;
    }

    for(int j = 2; j < argc; j++) {
        const char *types[] = {
            "city", "spooky", "murmur", "murmur256", "city256", "murmur512",
            "city512", "md5", "sha1", "sha256", "sha512",
            NULL
        };

        // printf("# %d MB\n", 1 << (j - 2));
        for(int i = 0; types[i]; ++i) {
            RmDigestType type = rm_string_to_digest_type(types[i]);
            if(type == RM_DIGEST_UNKNOWN) {
                printf("Unknown type: %s\n", types[i]);
                return EXIT_FAILURE;
            }

            GTimer *timer = g_timer_new();
            int digest_len = 0;

            char buffer[_RM_HASH_LEN * 2];
            memset(buffer, 0, sizeof(buffer));

            if(!strcasecmp(argv[1], "mmap")) {
                digest_len = rm_hash_file_mmap(argv[j], type, 1, buffer);
            } else if(!strcasecmp(argv[1], "readv")) {
                digest_len = rm_hash_file_readv(argv[j], type, 1, buffer);
            } else {
                digest_len = rm_hash_file(argv[j], type, strtod(argv[1], NULL), buffer);
            }

            for(int i = 0; i < digest_len; i++) {
                printf("%c", buffer[i]);
            }

            while(digest_len++ < 128) {
                putchar(' ');
            }

            printf(" %2.3fs %s\n", g_timer_elapsed(timer, NULL), types[i]);
            g_timer_destroy(timer);
        }
    }

    return 0;
}

#endif
