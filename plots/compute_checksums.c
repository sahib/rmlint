#include "city.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <alloca.h>
#include <glib.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include "spooky-c.h"


bool qhashmurmur3_128(const void *data, size_t nbytes, void *retbuf) {
    if (data == NULL || nbytes == 0)
        return false;

    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;

    const int nblocks = nbytes / 16;
    const uint64_t *blocks = (const uint64_t *) (data);
    const uint8_t *tail = (const uint8_t *) (data + (nblocks * 16));

    uint64_t h1 = 0;
    uint64_t h2 = 0;

    int i;
    uint64_t k1, k2;
    for (i = 0; i < nblocks; i++) {
        k1 = blocks[i * 2 + 0];
        k2 = blocks[i * 2 + 1];

        k1 *= c1;
        k1 = (k1 << 31) | (k1 >> (64 - 31));
        k1 *= c2;
        h1 ^= k1;

        h1 = (h1 << 27) | (h1 >> (64 - 27));
        h1 += h2;
        h1 = h1 * 5 + 0x52dce729;

        k2 *= c2;
        k2 = (k2 << 33) | (k2 >> (64 - 33));
        k2 *= c1;
        h2 ^= k2;

        h2 = (h2 << 31) | (h2 >> (64 - 31));
        h2 += h1;
        h2 = h2 * 5 + 0x38495ab5;
    }

    k1 = k2 = 0;
    switch (nbytes & 15) {
        case 15:
            k2 ^= (uint64_t)(tail[14]) << 48;
        case 14:
            k2 ^= (uint64_t)(tail[13]) << 40;
        case 13:
            k2 ^= (uint64_t)(tail[12]) << 32;
        case 12:
            k2 ^= (uint64_t)(tail[11]) << 24;
        case 11:
            k2 ^= (uint64_t)(tail[10]) << 16;
        case 10:
            k2 ^= (uint64_t)(tail[9]) << 8;
        case 9:
            k2 ^= (uint64_t)(tail[8]) << 0;
            k2 *= c2;
            k2 = (k2 << 33) | (k2 >> (64 - 33));
            k2 *= c1;
            h2 ^= k2;

        case 8:
            k1 ^= (uint64_t)(tail[7]) << 56;
        case 7:
            k1 ^= (uint64_t)(tail[6]) << 48;
        case 6:
            k1 ^= (uint64_t)(tail[5]) << 40;
        case 5:
            k1 ^= (uint64_t)(tail[4]) << 32;
        case 4:
            k1 ^= (uint64_t)(tail[3]) << 24;
        case 3:
            k1 ^= (uint64_t)(tail[2]) << 16;
        case 2:
            k1 ^= (uint64_t)(tail[1]) << 8;
        case 1:
            k1 ^= (uint64_t)(tail[0]) << 0;
            k1 *= c1;
            k1 = (k1 << 31) | (k1 >> (64 - 31));
            k1 *= c2;
            h1 ^= k1;
    };

    //----------
    // finalization

    h1 ^= nbytes;
    h2 ^= nbytes;

    h1 += h2;
    h2 += h1;

    h1 ^= h1 >> 33;
    h1 *= 0xff51afd7ed558ccdULL;
    h1 ^= h1 >> 33;
    h1 *= 0xc4ceb9fe1a85ec53ULL;
    h1 ^= h1 >> 33;

    h2 ^= h2 >> 33;
    h2 *= 0xff51afd7ed558ccdULL;
    h2 ^= h2 >> 33;
    h2 *= 0xc4ceb9fe1a85ec53ULL;
    h2 ^= h2 >> 33;

    h1 += h2;
    h2 += h1;

    ((uint64_t *) retbuf)[0] = h1;
    ((uint64_t *) retbuf)[1] = h2;

    return true;
}


typedef enum RmDigestType {
    RM_DIGEST_UNKNOWN = 0,
    RM_DIGEST_MURMUR,
    RM_DIGEST_SPOOKY,
    RM_DIGEST_CITY,
    RM_DIGEST_MD5,
    RM_DIGEST_SHA1,
    RM_DIGEST_SHA256,
    RM_DIGEST_SHA512
} RmDigestType;


typedef struct RmDigest {
    union {
        GChecksum *glib_checksum;
        struct spooky_state spooky_state;
        uint128 hash;
    };
    RmDigestType type;
} RmDigest;

RmDigestType rm_string_to_digest_type(const char *string) {
    if(!strcasecmp(string, "md5")) {
        return RM_DIGEST_MD5;
    } else 
    if(!strcasecmp(string, "sha1")) {
        return RM_DIGEST_SHA1;
    } else 
    if(!strcasecmp(string, "sha256")) {
        return RM_DIGEST_SHA256;
    } else 
    if(!strcasecmp(string, "sha512")) {
        return RM_DIGEST_SHA512;
    } else 
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
        case RM_DIGEST_SPOOKY:
            spooky_init(&digest->spooky_state, seed, ~seed);
            break;
        case RM_DIGEST_MD5:
            digest->glib_checksum = g_checksum_new(G_CHECKSUM_MD5);
            break;
        case RM_DIGEST_SHA1:
            digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA1);
            break;
        case RM_DIGEST_SHA256:
            digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA256);
            break;
        case RM_DIGEST_SHA512:
            digest->glib_checksum = g_checksum_new(G_CHECKSUM_SHA512);
            break;
        case RM_DIGEST_MURMUR:
        case RM_DIGEST_CITY:
        default:
            digest->hash.first = 0;
            digest->hash.second = 0;
            break;
    }
}

void rm_digest_update(RmDigest *digest, const char *data, guint64 size) {
    uint128 new_hash = {0, 0};    

    switch(digest->type) {
        case RM_DIGEST_SPOOKY:
            spooky_update(&digest->spooky_state, data, size);
            break;
        case RM_DIGEST_MD5:
        case RM_DIGEST_SHA1:
        case RM_DIGEST_SHA256:
        case RM_DIGEST_SHA512:
            g_checksum_update(digest->glib_checksum, (const guchar *)data, size);
            break;
        case RM_DIGEST_MURMUR:
            qhashmurmur3_128(data, size, &new_hash);
            digest->hash.first ^= new_hash.first;
            digest->hash.second ^= new_hash.second;
            break;
        case RM_DIGEST_CITY:
            new_hash = CityHash128(data, size);
            digest->hash.first ^= new_hash.first;
            digest->hash.second ^= new_hash.second;
            break;
        default:
            /* No init necessary */
            break;
    }
}

#define TO_HEX(b) "0123456789abcdef"[b]

int rm_digest_finalize(RmDigest *digest, char *buffer, gsize buflen) {
    gsize i = 0;
    guint8 digest_buf[64];
    gsize digest_len = sizeof(digest_buf);

    switch(digest->type) {
        case RM_DIGEST_MD5:
        case RM_DIGEST_SHA1:
        case RM_DIGEST_SHA256:
        case RM_DIGEST_SHA512:
            g_checksum_get_digest(digest->glib_checksum, digest_buf, &digest_len);
            for(gsize d = 0; d < digest_len && i < buflen; ++d) {
                buffer[i + 0] = TO_HEX(digest_buf[d] / 16);
                buffer[i + 1] = TO_HEX(digest_buf[d] % 16);
                i++;
                i++;

            }
            return i;
        case RM_DIGEST_SPOOKY:
            spooky_final(&digest->spooky_state, &digest->hash.first, &digest->hash.second);
            /* fallthrough */
        case RM_DIGEST_MURMUR:
        case RM_DIGEST_CITY:
            while(i < 16 && i < buflen) {
                 buffer[i] = TO_HEX(digest->hash.first % 16);
                 digest->hash.first /= 16;
                 i++;
            }
            while(i < 32 && i < buflen) {
                 buffer[i] = TO_HEX(digest->hash.second % 16);
                 digest->hash.second /= 16;
                 i++;
            }
            return 32;
        default:
            return -1;
    }
}

static int hash_file(const char *file, RmDigestType type, double buf_size_mb, char *buffer, size_t buf_len) {
    ssize_t bytes = 0;
    const int N = buf_size_mb * 1024 * 1024;
    char *data = alloca(N);
    int fd = open(file, O_RDONLY);

    /* Can't open file? */
    if(fd == -1) {
        return 0;
    }

    RmDigest digest;
    rm_digest_init(&digest, type, 0);

    do {
        if((bytes = read(fd, data, N)) == -1) {
            printf("ERROR:read()");
        } else {
            rm_digest_update(&digest, data, bytes);
        }
    } while(bytes > 0);

    return rm_digest_finalize(&digest, buffer, buf_len);
}

static int hash_file_mmap(const char *file, RmDigestType type, G_GNUC_UNUSED double buf_size_mb, char *buffer, size_t buf_len) {
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
        perror("ERROR:md5_file->mmap");
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

        printf("# %d MB\n", 1 << (j - 2));
        for(int i = 0; types[i]; ++i) {
            RmDigestType type = rm_string_to_digest_type(types[i]);
            if(type == RM_DIGEST_UNKNOWN) {
                printf("Unknown type: %s\n", types[i]);
                return EXIT_FAILURE;
            }

            GTimer *timer = g_timer_new();
            int digest_len = 0;
            
            const int N = 5;
            char buffer[128];
            memset(buffer, 0, sizeof(buffer));

            for(int n = 0; n < N; n++) {
                if(!strcasecmp(argv[1], "mmap")) {
                    digest_len = hash_file_mmap(argv[j], type, 1, buffer, sizeof(buffer));
                } else {
                    digest_len = hash_file(argv[j], type, strtod(argv[1], NULL), buffer, sizeof(buffer));
                }
            }
            for(int i = 0; i < digest_len; i++) {
                printf("%c", buffer[i]);
            }
            while(digest_len++ < 128) {
                putchar(' ');
            }
            printf("  %2.3fs %s\n", g_timer_elapsed(timer, NULL) / N, types[i]);
            g_timer_destroy(timer);
        }
    }
    return 0;
}
