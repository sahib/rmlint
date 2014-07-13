#include <stdio.h>
#include <string.h>
#include <glib.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#include "checksum.h"
#include "defs.h"

/* Less than 16 byte is not allowed */
G_STATIC_ASSERT(_RM_HASH_LEN >= 16);

RmDigestType rm_string_to_digest_type(const char *string) {
    if(string == NULL) {
        return RM_DIGEST_UNKNOWN;
    } else if(!strcasecmp(string, "md5")) {
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
                    } else if(!strcasecmp(string, "spooky")) {
                        return RM_DIGEST_SPOOKY;
                    } else if(!strcasecmp(string, "city")) {
                        return RM_DIGEST_CITY;
                    } else if(!strcasecmp(string, "city512")) {
                        return RM_DIGEST_CITY512;
                    } else if(!strcasecmp(string, "murmur512")) {
                        return RM_DIGEST_MURMUR512;
                    } else {
                        return RM_DIGEST_UNKNOWN;
                    }
}

void rm_digest_init(RmDigest *digest, RmDigestType type, guint64 seed) {
    digest->type = type;
    digest->num_128bit_blocks = 1;
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
        digest->hash[0].first = 0;
        digest->hash[0].second = 0;
        break;
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_CITY512:
        digest->num_128bit_blocks=4;
        digest->hash[0].first = 0;
        digest->hash[0].second = 0;
        digest->hash[1].first =  (0xf0f0f0f0f0f0f0f0);  /*1111000011110000 etc*/
        digest->hash[1].second = (0xf0f0f0f0f0f0f0f0);
        digest->hash[2].first =  (0x3333333333333333);  /*001100110011 etc*/
        digest->hash[2].second = (0x3333333333333333);
        digest->hash[3].first =  (0xaaaaaaaaaaaaaaaa);
        digest->hash[3].second = (0xaaaaaaaaaaaaaaaa);   10101010 etc */
        break;
#endif
    default:
        g_assert_not_reached();
    }
}

void rm_digest_copy ( RmDigest *dest,  RmDigest *src) {

    dest->type = src->type;
    dest->num_128bit_blocks = src->num_128bit_blocks;

    switch(src->type) {
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
        dest->glib_checksum = g_checksum_copy(src->glib_checksum);
        break;
    case RM_DIGEST_SPOOKY:
        spooky_copy(&dest->spooky_state, &src->spooky_state);
    /* Fallthrough */
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_CITY512:
    case RM_DIGEST_MURMUR512:
#endif // _RM_HASH_LEN
        for (int i=0; i < src->num_128bit_blocks; i++) {
            dest->hash[i].first = src->hash[i].first;
            dest->hash[i].second = src->hash[i].second;
        }
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
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_MURMUR512:
#endif // _RM_HASH_LEN
        for (int i=0; i < digest->num_128bit_blocks; i++) {
            /*TODO:  multithread this if num_128bit_blocks > 1 */
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
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_CITY512:
#endif // _RM_HASH_LEN
        for (int i=0; i < digest->num_128bit_blocks; i++) {
            /* Opt out for the more optimized version.
             * This needs the crc command of sse4.2
             * (available on Intel Nehalem and up; my amd box doesn't have this though)
             */
#ifdef __sse4_2__
            digest->hash = CityHashCrc128WithSeed(data, size, digest->hash);
#else
            digest->hash[i] = CityHash128WithSeed((const char *) data, size, digest->hash[i]);
#endif
            /*TODO:  multithread this if num_128bit_blocks > 1 */
        }
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
        spooky_final(&digest->spooky_state, &digest->hash[0].first, &digest->hash[0].second);
    /* fallthrough */
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_CITY512:
#endif // _RM_HASH_LEN
        for (int j=0; j < digest->num_128bit_blocks; j++) {
            while( (int)i < (16 + j * 32) && i < buflen) {
                buffer[i++] = TO_HEX(digest->hash[j].first % 16);
                digest->hash[j].first /= 16;
            }
            while( (int)i < (32 + j * 32) && i < buflen) {
                buffer[i++] = TO_HEX(digest->hash[j].second % 16);
                digest->hash[j].second /= 16;
            }
        }
        assert ( (int) i == 32 * digest->num_128bit_blocks );
        return i;
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
        spooky_final(&digest->spooky_state, &digest->hash[0].first, &digest->hash[0].second);
    /* fallthrough */
    case RM_DIGEST_MURMUR:
    case RM_DIGEST_CITY:
#if _RM_HASH_LEN >= 64
    case RM_DIGEST_MURMUR512:
    case RM_DIGEST_CITY512:
#endif // _RM_HASH_LEN
        for ( int i=0; i < digest->num_128bit_blocks; i++ ) {
            memcpy(buffer + 0 + 16 * i, &digest->hash[0].first, 8);
            memcpy(buffer + 8 + 16 * i, &digest->hash[0].second, 8);
            /*TODO: any reason why we can't we just memcpy in one go? */
        }
        return 16 * digest->num_128bit_blocks;
    default:
        g_assert_not_reached();
    }
}


/* get digest, with or without finalising */
int rm_digest_read_binary(RmDigest *digest, unsigned char *buffer, gsize buflen, bool finalise) {
    if (0
        || finalise
        || digest->type==RM_DIGEST_MURMUR  /* MURMUR binary finalize doesn't change the RmDigest so don't need rm_digest_copy */
        || digest->type==RM_DIGEST_CITY    /* CITY binary finalize doesn't change the RmDigest so don't need rm_digest_copy */
        ) {
        return rm_digest_finalize_binary(digest, buffer, buflen);
    }
    else {
        /* make a copy and then finalise that */
        RmDigest digestCopy;
        rm_digest_copy (&digestCopy, digest);
        return rm_digest_finalize_binary(&digestCopy, buffer, buflen);
    }
}

void rm_digest_print(char *buffer, double buffer_len) {
    for (int i=0; i < buffer_len; i++)
    {
        printf("%02X", ((unsigned char *) buffer)[i]);
    }
    printf("\n");
}

#ifdef _RM_COMPILE_MAIN

/* Use this to compile:
 * $ gcc src/checksum.c src/checksums/ *.c -Wextra -Wall $(pkg-config --libs --cflags glib-2.0) -std=c11 -msse4a -O4 -D_GNU_SOURCE -D_RM_COMPILE_MAIN
 * $ ./a.out mmap <some_file[s]>
 */

static int rm_hash_file(const char *file, RmDigestType type, double buf_size_kb, char *buffer, size_t buf_len) {
    ssize_t bytes = 0;
    const int N = buf_size_kb * 1024;
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
            /* get interim value (will this affect speed? */
            /* todo: add argv to turn this on and off     */
            int len = rm_digest_read_binary(&digest, buffer, buf_len, false);
            //printf ( "partial digest: " );
            //rm_digest_print(buffer, len);
        }
    } while(bytes > 0);

    fclose(fd);
    return rm_digest_finalize(&digest, buffer, buf_len);
}

static int rm_hash_file_mmap(const char *file, RmDigestType type, G_GNUC_UNUSED double buf_size_kb, char *buffer, size_t buf_len) {
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
        const char *types[] = {"spooky", "md5", "sha1", "sha256", "sha512", "murmur", "murmur512", "city", "city512", NULL};

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
