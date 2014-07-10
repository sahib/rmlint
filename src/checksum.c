#include <stdio.h>
#include <string.h>
#include <glib.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "checksum.h"
#include "defs.h"

/* Less than 16 byte is not allowed */
G_STATIC_ASSERT(_RM_HASH_LEN >= 16);

RmDigestType rm_string_to_digest_type(const char *string) {
    if(string == NULL) {
        return RM_DIGEST_UNKNOWN;
    } else 
    if(!strcasecmp(string, "md5")) {
        return RM_DIGEST_MD5;
    } else 
#if _RM_HASH_LEN >= 20
    if(!strcasecmp(string, "sha1")) {
        return RM_DIGEST_SHA1;
    } else 
#endif
#if _RM_HASH_LEN >= 32
    if(!strcasecmp(string, "sha256")) {
        return RM_DIGEST_SHA256;
    } else 
#endif
#if _RM_HASH_LEN >= 64
    if(!strcasecmp(string, "sha512")) {
        return RM_DIGEST_SHA512;
    } else 
#endif
    if(!strcasecmp(string, "murmur")) {
        return RM_DIGEST_MURMUR;
    } else 
    if(!strcasecmp(string, "spooky")) {
        return RM_DIGEST_SPOOKY;
    } else 
    if(!strcasecmp(string, "city")) {
        return RM_DIGEST_CITY;
    } else {
        return RM_DIGEST_UNKNOWN;
    }
}

void rm_digest_init(RmDigest *digest, RmDigestType type, guint64 seed) {
    digest->type = type;
    switch(type) {
        case RM_DIGEST_MD5:
            digest->glib_checksum = g_checksum_new(G_CHECKSUM_MD5);
            break;
#if _RM_HASH_LEN >= 20
        case RM_DIGEST_SHA1:
            digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA1);
            break;
#endif
#if _RM_HASH_LEN >= 32
        case RM_DIGEST_SHA256:
            digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA256);
            break;
#endif 
#if _RM_HASH_LEN >= 64
        case RM_DIGEST_SHA512:
            digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA512);
            break;
#endif
        case RM_DIGEST_SPOOKY:
            spooky_init(&digest->spooky_state, seed, ~seed);
            /* Fallthrough */
        case RM_DIGEST_MURMUR:
        case RM_DIGEST_CITY:
            digest->hash.first = 0;
            digest->hash.second = 0;
            break;
        default:
            g_assert_not_reached();
    }
}

void rm_digest_update(RmDigest *digest, const unsigned char *data, guint64 size) {
    switch(digest->type) {
        case RM_DIGEST_MD5:
#if _RM_HASH_LEN >= 20
        case RM_DIGEST_SHA1:
#endif
#if _RM_HASH_LEN >= 32
        case RM_DIGEST_SHA256:
#endif
#if _RM_HASH_LEN >= 64
        case RM_DIGEST_SHA512:
#endif
            g_checksum_update(digest->glib_checksum, (const guchar *)data, size);
            break;
        case RM_DIGEST_SPOOKY:
            spooky_update(&digest->spooky_state, data, size);
            break;
        case RM_DIGEST_MURMUR:
#if UINTPTR_MAX == 0xffffffff
            /* 32 bit */
            MurmurHash3_x86_128(data, size, (uint32_t)digest->hash.first, &digest->hash);
#elif UINTPTR_MAX == 0xffffffffffffffff
            /* 64 bit */
            MurmurHash3_x64_128(data, size, (uint32_t)digest->hash.first, &digest->hash);
#else 
            /* 16 bit or unknown */
            #error "Probably not a good idea to compile rmlint on 16bit."
#endif 
            break;
        case RM_DIGEST_CITY:
            /* Opt out for the more optimized version.
             * This needs the crc command of sse4.2 
             * (available on Intel Nehalem and up; my amd box doesn't have this though)
             */
#ifdef __sse4_2__
            digest->hash = CityHashCrc128WithSeed(data, size, digest->hash);
#else
            digest->hash = CityHash128WithSeed((const char *) data, size, digest->hash);
#endif
            break;
        default:
            g_assert_not_reached();
    }
}

#define TO_HEX(b) "0123456789abcdef"[b]

int rm_digest_finalize(RmDigest *digest, unsigned char *buffer, gsize buflen) {
    gsize i = 0;
    guint8 digest_buf[64];
    gsize digest_len = sizeof(digest_buf);

    switch(digest->type) {
        case RM_DIGEST_MD5:
#if _RM_HASH_LEN >= 20
        case RM_DIGEST_SHA1:
#endif
#if _RM_HASH_LEN >= 32
        case RM_DIGEST_SHA256:
#endif
#if _RM_HASH_LEN >= 64
        case RM_DIGEST_SHA512:
#endif
            g_checksum_get_digest(digest->glib_checksum, digest_buf, &digest_len);
            for(gsize d = 0; d < digest_len && i < buflen; ++d) {
                buffer[i + 0] = TO_HEX(digest_buf[d] / 16);
                buffer[i + 1] = TO_HEX(digest_buf[d] % 16);
                i += 2;
            }
            g_checksum_free(digest->glib_checksum);
            return i;
        case RM_DIGEST_SPOOKY:
            spooky_final(&digest->spooky_state, &digest->hash.first, &digest->hash.second);
            /* fallthrough */
        case RM_DIGEST_MURMUR:
        case RM_DIGEST_CITY:
            while(i < 16 && i < buflen) {
                 buffer[i++] = TO_HEX(digest->hash.first % 16);
                 digest->hash.first /= 16;
            }
            while(i < 32 && i < buflen) {
                 buffer[i++] = TO_HEX(digest->hash.second % 16);
                 digest->hash.second /= 16;
            }
            return 32;
        default:
            g_assert_not_reached();
    }
}

int rm_digest_finalize_binary(RmDigest *digest, unsigned char *buffer, gsize buflen) {
    switch(digest->type) {
        case RM_DIGEST_MD5:
#if _RM_HASH_LEN >= 20
        case RM_DIGEST_SHA1:
#endif
#if _RM_HASH_LEN >= 32
        case RM_DIGEST_SHA256:
#endif
#if _RM_HASH_LEN >= 64
        case RM_DIGEST_SHA512:
#endif
            g_checksum_get_digest(digest->glib_checksum, buffer, &buflen);
            g_checksum_free(digest->glib_checksum);
            return buflen;
        case RM_DIGEST_SPOOKY:
            spooky_final(&digest->spooky_state, &digest->hash.first, &digest->hash.second);
            /* fallthrough */
        case RM_DIGEST_MURMUR:
        case RM_DIGEST_CITY:
            memcpy(buffer + 0, &digest->hash.first, 8);
            memcpy(buffer + 8, &digest->hash.second, 8);
            return 16;
        default:
            g_assert_not_reached();
    }
}

#ifdef _RM_COMPILE_MAIN

/* Use this to compile:
 * $ gcc src/checksum.c src/checksums/ *.c -Wextra -Wall $(pkg-config --libs --cflags glib-2.0) -std=c11 -msse4a -O4 -D_GNU_SOURCE -D_RM_COMPILE_MAIN
 * $ ./a.out mmap <some_file[s]>
 */

static int rm_hash_file(const char *file, RmDigestType type, double buf_size_mb, char *buffer, size_t buf_len) {
    ssize_t bytes = 0;
    const int N = buf_size_mb * 1024 * 1024;
    char *data = g_alloca(N);
    FILE *fd = fopen(file, "rb");

    /* Can't open file? */
    if(fd == NULL) {
        return 0;
    }

    RmDigest digest;
    rm_digest_init(&digest, type, 0);

    do {
        if((bytes = fread(data, 1, N, fd)) == -1) {
            printf("ERROR:read()");
        } else {
            rm_digest_update(&digest, data, bytes);
        }
    } while(bytes > 0);

    fclose(fd);

    return rm_digest_finalize(&digest, buffer, buf_len);
}

static int rm_hash_file_mmap(const char *file, RmDigestType type, G_GNUC_UNUSED double buf_size_mb, char *buffer, size_t buf_len) {
    int fd = 0;
    char *f_map = NULL;


    if((fd = open(file, O_RDONLY)) == -1) {
        perror("ERROR:sys:open()");
        return 0;
    }

    RmDigest digest;
    rm_digest_init(&digest, type, 0);

    struct stat stat_buf;
    stat(file, &stat_buf);

    f_map = mmap(NULL, stat_buf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if(f_map != MAP_FAILED) {
        if(madvise(f_map, stat_buf.st_size, MADV_WILLNEED) == -1) {
            perror("madvise");
        }

        /* Shut your eyes and go through, leave out start & end of fp */
        rm_digest_update(&digest, f_map, stat_buf.st_size);
        /* Unmap this file */

        munmap(f_map, stat_buf.st_size);

    } else {
        perror("ERROR:hash_file->mmap");
    }

    if(close(fd) == -1) {
        perror("ERROR:close()");
    }

    int digest_len = rm_digest_finalize(&digest, buffer, buf_len);
    return digest_len;
}


int main(int argc, char **argv) {
    if(argc < 3) {
        printf("Specify a type and a file\n");
        return EXIT_FAILURE;
    }

    for(int j = 2; j < argc; j++) {
        const char * types[] = {"city", "spooky", "murmur", "md5", "sha1", "sha256", "sha512", NULL};

        // printf("# %d MB\n", 1 << (j - 2));
        for(int i = 0; types[i]; ++i) {
            RmDigestType type = rm_string_to_digest_type(types[i]);
            if(type == RM_DIGEST_UNKNOWN) {
                printf("Unknown type: %s\n", types[i]);
                return EXIT_FAILURE;
            }

            GTimer *timer = g_timer_new();
            int digest_len = 0;
            
            char buffer[128];
            memset(buffer, 0, sizeof(buffer));

            if(!strcasecmp(argv[1], "mmap")) {
                digest_len = rm_hash_file_mmap(argv[j], type, 1, buffer, sizeof(buffer));
            } else {
                digest_len = rm_hash_file(argv[j], type, strtod(argv[1], NULL), buffer, sizeof(buffer));
            }

            for(int i = 0; i < digest_len; i++) {
                printf("%c", buffer[i]);
            }

            while(digest_len++ < 128) {
                putchar(' ');
            }

            printf("  %2.3fs %s\n", g_timer_elapsed(timer, NULL), types[i]);
            g_timer_destroy(timer);
        }
    }

    return 0;
}

#endif
