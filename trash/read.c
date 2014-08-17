/**
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

#include "read.h"
#include "cmdline.h"
#include "list.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <math.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "checksum.h"

/* Mutexes to (pseudo-)"serialize" IO (avoid unnecessary jumping) */
static pthread_mutex_t mutex_fp_IO = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_ck_IO = PTHREAD_MUTEX_INITIALIZER;

/* Some sort of 32/64 bit comparasion */
bool IS_32BIT = sizeof(int *) > 4;

static void hash_file_mmap(RmSession *session, RmFile *file) {
    int inFile = 0;
    unsigned char *f_map = NULL;
    /* Don't read the $already_read amount of bytes read by hash_fingerprint */
    guint64 already_read = HASH_FPSIZE_FORM(file->fsize);
    already_read = already_read ?  already_read - 1 : 0;
    /* This is some rather seldom case, but skip checksum building here */
    if(file->fsize <= (already_read * 2))
        return;
    if((inFile = open(file->path, HASH_FILE_FLAGS)) == -1) {
        rm_perror(RED"ERROR:"NCO"sys:open()");
        return;
    }

    RmDigest digest;
    rm_digest_init(&digest, session->settings->checksum_type, 0, 0);

    f_map = mmap(NULL, (size_t)file->fsize, PROT_READ, MAP_PRIVATE, inFile, 0);
    if(f_map != MAP_FAILED) {
        guint64 f_offset = already_read;
        if(madvise(f_map, file->fsize - already_read, MADV_WILLNEED) == -1) {
            rm_perror("madvise");
        }
        /* Shut your eyes and go through, leave out start & end of fp */
        rm_digest_update(&digest, f_map + f_offset, file->fsize - already_read);

        /* Unmap this file */
        munmap(f_map, file->fsize);

        rm_file_set_checksum(session->list, file, &digest);
        rm_digest_finalize(&digest);
    } else {
        rm_perror(RED"ERROR:"NCO"hash_file->mmap");
    }
    if(close(inFile) == -1)
        rm_perror(RED"ERROR:"NCO"close()");
}

/* ------------------------------------------------------------- */

/* used to calc the complete checksum of file & save it in File */
static void hash_file_fread(RmSession *session, RmFile *file) {
    /* Number of bytes read in */
    ssize_t bytes = 0;
    size_t offset = 0;
    /* Input stream */
    int inFile = 0;
    /* tmp buffer */
    unsigned char *data = NULL;
    /* Don't read the $already_read amount of bytes read by hash_fingerprint */
    guint64 already_read = 0;
    guint64 actual_size  = 0;
    already_read = HASH_FPSIZE_FORM(file->fsize);
    already_read = already_read ?  already_read - 1 : 0;

    RmDigest digest;
    rm_digest_init(&digest, session->settings->checksum_type, 0, 0);

    /* This is some rather seldom case, but skip checksum building here */
    if(file->fsize <= (already_read * 2)) {
        return;
    }
    actual_size  = (HASH_IO_BLOCKSIZE > file->fsize) ? (file->fsize + 1) : HASH_IO_BLOCKSIZE;
    /* Allocate buffer on the thread's stack */
    data = alloca(actual_size + 1);
    inFile = open(file->path, HASH_FILE_FLAGS);
    /* Can't open file? */
    if(inFile == -1) {
        return;
    }

    lseek(inFile, already_read, SEEK_SET);
    do {
        /* If (pseudo-)serialized IO is requested lock a mutex, so other threads have to wait here
         * This is to prevent that several threads call fread() at the same time, what would case the
         * HD to jump a lot around without doing anything intelligent..
         * */
#if (HASH_SERIAL_IO == 1)
        pthread_mutex_lock(&mutex_ck_IO);
#endif
        /* The call to fread */
        if((bytes = read(inFile, data, actual_size)) == -1) {
            rm_perror(RED"ERROR:"NCO"read()");
        }
        /* Unlock */
#if (HASH_SERIAL_IO == 1)
        pthread_mutex_unlock(&mutex_ck_IO);
#endif
        if(bytes != -1) {
            offset += bytes;
            /* Update the checksum with the current contents of &data */
            rm_digest_update(&digest, data, bytes);
        }
    } while(bytes != -1 && bytes && (offset < (file->fsize - already_read)));

    rm_file_set_checksum(session->list, file, &digest);
    rm_digest_finalize(&digest);

    if(close(inFile) == -1) {
        rm_perror(RED"ERROR:"NCO"close()");
    }
}

/* ------------------------------------------------------------- */

/* Reads <readsize> bytes from each start and end + 8 bytes in the middle
   start and end gets converted into a 128bit md5sum and are written in <file>
   the 8 byte are stored in raw form
*/

static void hash_fingerprint_mmap(RmSession *session, RmFile *file, const guint64 readsize) {
    int pF = open(file->path, HASH_FILE_FLAGS);
    unsigned char *f_map = NULL;

    /* empty? */
    if(pF == -1) {
        warning(YEL"\nWARN: "NCO"Cannot open %s for mmap fingerprint", file->path);
        return;
    }
    f_map = mmap(0, file->fsize, PROT_READ, MAP_PRIVATE, pF, 0);
    if(f_map == MAP_FAILED) {
        rm_perror(RED"ERROR:"NCO"mmap()");
        close(pF);
        return;
    }

    RmDigest digest;
    rm_digest_init(&digest, session->settings->checksum_type, 0, 0);
    rm_digest_update(&digest, f_map, readsize);
    rm_file_set_fingerprint(session->list, file, 0, &digest);

    if(readsize * 2 <= file->fsize) {
        /* Jump to middle of file and read a couple of bytes there s*/
        lseek(pF, file->fsize / 2 , SEEK_SET);
        rm_file_set_middle_bytes(session->list, file, (const char *)f_map, BYTE_MIDDLE_SIZE);

        if(readsize * 2 + BYTE_MIDDLE_SIZE <= file->fsize) {
            /* Jump to end and read final block */
            lseek(pF, -readsize, SEEK_END);

            /* Compute checksum of this last block */
            rm_digest_init(&digest, RM_DIGEST_CITY, 0, 0);
            rm_digest_update(&digest, f_map, readsize);
            rm_file_set_fingerprint(session->list, file, 1, &digest);
            rm_digest_finalize(&digest);
        }
    }
    close(pF);
    munmap(f_map, file->fsize);
}

/* ------------------------------ */

static void hash_fingerprint_fread(RmSession *session, RmFile *file, const guint64 readsize) {
    int bytes = 0;
    bool unlock = true;
    FILE *pF = fopen(file->path, "re");
    unsigned char *data = g_alloca(readsize);

    /* empty? */
    if(!pF) {
        warning(YEL"\nWARN: "NCO"Cannot open %s for fingerprint fread", file->path);
        return;
    }
#if (HASH_SERIAL_IO == 1)
    pthread_mutex_lock(&mutex_fp_IO);
#endif
    /* Read the first block */
    bytes = fread(data, sizeof(char), readsize, pF);
#if (HASH_SERIAL_IO == 1)
    pthread_mutex_unlock(&mutex_fp_IO);
#endif

    /* Compute md5sum of this block */
    RmDigest digest;
    if(bytes) {
        rm_digest_init(&digest, session->settings->checksum_type, 0, 0);
        rm_digest_update(&digest, data, bytes);
        rm_file_set_fingerprint(session->list, file, 0, &digest);
        rm_digest_finalize(&digest);
    }
#if (HASH_SERIAL_IO == 1)
    pthread_mutex_lock(&mutex_fp_IO);
#endif
    if(readsize <= file->fsize) {
        /* Jump to middle of file and read a couple of bytes there s*/
        fseek(pF, file->fsize / 2 , SEEK_SET);
        bytes = fread(file->bim, sizeof(unsigned char), BYTE_MIDDLE_SIZE, pF);
        if(readsize + BYTE_MIDDLE_SIZE <= file->fsize) {
            /* Jump to end and read final block */
            fseek(pF, -readsize, SEEK_END);
            bytes = fread(data, sizeof(char), readsize, pF);
#if (HASH_SERIAL_IO == 1)
            pthread_mutex_unlock(&mutex_fp_IO);
            unlock = false;
#endif
            /* Compute checksum of this last block */
            if(bytes) {
                rm_digest_init(&digest, RM_DIGEST_CITY, 0, 0);
                rm_digest_update(&digest, data, bytes);
                rm_file_set_fingerprint(session->list, file, 1, &digest);
                rm_digest_finalize(&digest);
            }
        }
    }
#if (HASH_SERIAL_IO == 1)
    if(unlock) {
        pthread_mutex_unlock(&mutex_fp_IO);
    }
#endif
    fclose(pF);
}

/* ------------------------------ */

void hash_fingerprint(RmSession *session, RmFile *file, const guint64 readsize) {
#if HASH_USE_MMAP == -1
    if((file->fsize > MMAP_LIMIT) || file->fsize < HASH_IO_BLOCKSIZE >> 1) {
        hash_fingerprint_fread(session, file, readsize);
    } else {
        hash_fingerprint_mmap(session, file, readsize);
    }
#elif HASH_USE_MMAP == 1
    hash_fingerprint_mmap(session, file, readsize);
#else
    hash_fingerprint_fread(session, file, readsize);
#endif
}

/* ------------------------------ */

void hash_file(RmSession *session, RmFile *file) {
#if HASH_USE_MMAP == -1
    if((!IS_32BIT && file->fsize > MMAP_LIMIT) || file->fsize < HASH_IO_BLOCKSIZE >> 1 || file->fsize > MMAP_LIMIT << 4) {
        hash_file_fread(session, file);
#ifdef PRINT_CHOICE
        printf("f->%ld\n", file->fsize);
#endif
    } else {
        hash_file_mmap(session, file);
#ifdef PRINT_CHOICE
        printf("m->%ld\n", file->fsize);
#endif
    }
#elif HASH_USE_MMAP == 1
    hash_file_mmap(session, file);
#else
    hash_file_fread(session, file);
#endif
}
