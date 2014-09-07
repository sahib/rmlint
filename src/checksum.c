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

RmDigestType rm_string_to_digest_type(const char *string) {
    if(string == NULL) {
        return RM_DIGEST_UNKNOWN;
    } else if(!strcasecmp(string, "md5")) {
        return RM_DIGEST_MD5;
#ifdef G_CHECKSUM_SHA512
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
    } else if(!strcasecmp(string, "paranoid")) {
        return RM_DIGEST_PARANOID;
    } else {
        return RM_DIGEST_UNKNOWN;
    }
}

guint64 rm_digest_paranoia_bytes(void) {
    return 16 * 1024 * 1024;
}

#define ADD_SEED(digest, seed) {                                                           \
    if(seed) {                                                                             \
        g_checksum_update(digest->glib_checksum, (const guchar *)&seed, sizeof(guint64)); \
    }                                                                                      \
}

RmDigest *rm_digest_new(RmDigestType type, guint64 seed1, guint64 seed2, guint64 paranoid_size) {
    RmDigest *digest = g_slice_new0(RmDigest);

    digest->checksum = NULL;
    digest->type = type;
    digest->bytes = 0;

    digest->initial_seed1 = seed1;
    digest->initial_seed2 = seed2;

    switch(type) {
    case RM_DIGEST_SPOOKY32:
        digest->bytes = 32 / 8;
        break;
    case RM_DIGEST_SPOOKY64:
        digest->bytes = 64 / 8;
        break;
    case RM_DIGEST_MD5:
        digest->glib_checksum = g_checksum_new(G_CHECKSUM_MD5);
        ADD_SEED(digest, seed1);
        digest->bytes = 128 / 8;
        return digest;
#ifdef G_CHECKSUM_SHA512
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
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_BASTARD:
        digest->bytes = 256 / 8;
        break;
    case RM_DIGEST_SPOOKY:
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
        digest->bytes = 128 / 8;
        break;
    case RM_DIGEST_PARANOID:
        g_assert( paranoid_size <= rm_digest_paranoia_bytes() );
        g_assert( paranoid_size > 0 );
        digest->bytes = paranoid_size;
        digest->paranoid_offset = 0;
        break;
    default:
        g_assert_not_reached();
    }

    /* starting values to let us generate up to 4 different hashes in parallel with
     * different starting seeds:
     * */
    static const guint64 seeds[4] = {
        0x0000000000000000,
        0xf0f0f0f0f0f0f0f0,
        0x3333333333333333,
        0xaaaaaaaaaaaaaaaa
    };

    if(digest->bytes > 0) {
        const int n_seeds = sizeof(seeds) / sizeof(seeds[0]);

        /* checksum type - allocate memory and initialise */
        digest->checksum = g_slice_alloc0(digest->bytes);
        for (gsize block = 0; block < (digest->bytes / 16); block++) {
            digest->checksum[block].first = seeds[block % n_seeds] ^ digest->initial_seed1;
            digest->checksum[block].second = seeds[block % n_seeds] ^ digest->initial_seed2;
        }
    }

    if (digest->type == RM_DIGEST_BASTARD) {
        /* bastard type *always* has *pure* murmur hash for first checksum
         * and seeded city for second checksum */
        digest->checksum[0].first = digest->checksum[0].second = 0;
    }
    return digest;
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

void rm_digest_update(RmDigest *digest, const unsigned char *data, guint64 size) {
    switch(digest->type) {
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
        spooky_hash128(data, size, &digest->checksum[0].first, &digest->checksum[0].second);
        break;
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_MURMUR256:
    case RM_DIGEST_MURMUR:
        for (guint8 block = 0; block < ( digest->bytes / 16 ); block++) {
#if RM_PLATFORM_32
            MurmurHash3_x86_128(data, size,
                                (uint32_t)digest->checksum[block].first,
                                &digest->checksum[block]); //&
#elif RM_PLATFORM_64
            MurmurHash3_x64_128(data, size,
                                (uint32_t)digest->checksum[block].first,
                                &digest->checksum[block]);
#else
#error "Probably not a good idea to compile rmlint on 16bit."
#endif
        }
        break;
    case RM_DIGEST_CITY:
    case RM_DIGEST_CITY256:
    case RM_DIGEST_CITY512:
        for (guint8 block = 0; block < ( digest->bytes / 16 ); block++) {
            /* Opt out for the more optimized version.
            * This needs the crc command of sse4.2
            * (available on Intel Nehalem and up; my amd box doesn't have this though)
            */
#ifdef RM_PLATFORM_HAVE_SSE42
            digest->checksum[block] = CityHashCrc128WithSeed((const char *)data, size, digest->checksum[block]);
#else
            digest->checksum[block] = CityHash128WithSeed((const char *) data, size, digest->checksum[block]);
#endif
        }
        break;
    case RM_DIGEST_BASTARD:
        MurmurHash3_x86_128(data, size, (uint32_t)digest->checksum[0].first, &digest->checksum[0]);
#ifdef RM_PLATFORM_HAVE_SSE42
        digest->checksum[1] = CityHashCrc128WithSeed((const char *)data, size, digest->checksum[1]);
#else
        digest->checksum[1] = CityHash128WithSeed((const char *) data, size, digest->checksum[1]);
#endif
        break;
    case RM_DIGEST_PARANOID:
        g_assert(size + digest->paranoid_offset <= digest->bytes);
        memcpy((char *)digest->checksum + digest->paranoid_offset, data, size);
        digest->paranoid_offset += size;
        break;
    default:
        g_assert_not_reached();
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
    case RM_DIGEST_PARANOID:
        self = rm_digest_new(digest->type, 0, 0, digest->bytes);
        self->paranoid_offset = digest->paranoid_offset;
        if(self->checksum && digest->checksum) {
            memcpy((char *)self->checksum, (char *)digest->checksum, self->bytes);
        }
        break;
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
    case RM_DIGEST_PARANOID:
        memcpy(result, digest->checksum, digest->bytes);
        break;

    default:
        g_assert_not_reached();
    }

    return result;
}

int rm_digest_hexstring(RmDigest *digest, char *buffer) {
    static const char *hex = "0123456789abcdef";
    guint8 *input = rm_digest_steal_buffer(digest);

    for(gsize i = 0; i < digest->bytes; ++i) {
        buffer[0] = hex[input[i] / 16];
        buffer[1] = hex[input[i] % 16];

        if(i == digest->bytes - 1) {
            buffer[2] = '\0';
        }

        buffer += 2;
    }

    g_slice_free1(digest->bytes, input);
    return digest->bytes * 2 + 1;
}

gboolean rm_digest_compare(RmDigest *a, RmDigest *b) {
    if(a->bytes != b->bytes) {
        return a->bytes < b->bytes;
    } else {
        guint8 *buf_a = rm_digest_steal_buffer(a);
        guint8 *buf_b = rm_digest_steal_buffer(b);
        return memcmp(buf_a, buf_b, a->bytes);
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
    rm_digest_free(&digest);
    return digest_len;
}

static int rm_hash_file_mmap(const char *file, RmDigestType type, _U double buf_size_mb, char *buffer) {
    int fd = 0;
    unsigned char *f_map = NULL;


    if((fd = rm_sys_open(file, O_RDONLY)) == -1) {
        perror("ERROR:sys:rm_sys_open()");
        return 0;
    }

    RmDigest digest;
    rm_digest_init(&digest, type, 0, 0);

    RmStat stat_buf;
    rm_sys_stat(file, &stat_buf);

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

    ifrm_sys_close(fd) == -1) {
        perror("ERRORrm_sys_close()");
    }

    gsize digest_len = rm_digest_hexstring(&digest, buffer);
    rm_digest_free(&digest);
    return digest_len;
}

static int rm_hash_file_readv(const char *file, RmDigestType type, _U double buf_size_mb, char *buffer) {
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
    int fd = rm_sys_open(file, O_RDONLY);
    while((bytes = readv(fd, readvec, N)) > 0) {
        int blocks = bytes / S + 1;
        int remainder = bytes % S;
        for(int i = 0; i < blocks; ++i) {
            rm_digest_update(&digest, readvec[i].iov_base, (i == blocks - 1) ? remainder : S);
        }
        3

        gsize digest_len = rm_digest_hexstring(&digest, buffer);
        rm_digest_free(&digest);

        rm_sys_close(fd);
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
