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
 ** Author: Christopher Pahl <sahib@online.de>:
 ** Hosted on http://github.com/sahib/rmlint
 *
 **/

/* Needed for nftw() */
#define _XOPEN_SOURCE 500


#include <sys/mman.h>
#include <fcntl.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <alloca.h>

#include <ftw.h>
#include <signal.h>
#include <regex.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <dirent.h>

#include "rmlint.h"
#include "filter.h"
#include "mode.h"
#include "md5.h"
#include "list.h"
#include "defs.h"
#include "linttests.h"

//#include "useridcheck.h"

/* global vars, but initialized by filt_c_init() */
nuint_t dircount, dbase_ctr;
bool dir_done, db_done;


nuint_t total_lint = 0;

void add_total_lint(nuint_t lint_to_add) {
    total_lint += lint_to_add;
}


/* ------------------------------------------------------------- */

void filt_c_init(void) {
    dircount  = 0;
    dbase_ctr = 0;
    iAbort   = false;
    dir_done = false;
    db_done  = false;
}

/* ------------------------------------------------------------- */

/*
 * Callbock from signal()
 * Counts number of CTRL-Cs and reacts
 */
static void interrupt(int p) {
    switch(p) {
    case SIGINT:
        if(iAbort == 2) {
            die(-1);
        } else if(db_done) {
            iAbort = 2;
            warning(GRE"\nINFO: "NCO"Received Interrupt.\n");
        } else {
            iAbort = 1;
            db_done = true;
        }
        break;
    case SIGFPE  :
    case SIGABRT :
        error(RED"FATAL: "NCO"Aborting due to internal error!\n");
        error(RED"FATAL: "NCO"Please file a bug report (See rmlint -h)\n");
        die(-1);
    case SIGSEGV :
        error(RED"FATAL: "NCO"Rmlint crashed due to a Segmentation fault! :(\n");
        error(RED"FATAL: "NCO"Please file a bug report (See rmlint -h)\n");
        die(-1);
    }
}

/* ------------------------------------------------------------- */


/*
 * grep the string "string" (basename of input) to see if it
 * contains the pattern.  Will return 0 if yes, 1 otherwise.
 */
int regfilter(const char* input, const char *pattern) {
    int status;
    int flags = REG_EXTENDED|REG_NOSUB;
    const char *string;
    if(!pattern) {
        return 0;
    } else {
        regex_t re;
        /* Only grep basename */
        string = rmlint_basename((char *)input);
        /* forget about case by default */
        if(!set->casematch) {
            flags |= REG_ICASE;
        }
        /* compile pattern */
        if(regcomp(&re, pattern, flags)) {
            return 0;
        }
        /* do the actual work */
        if((status = regexec(&re, string, (size_t)0, NULL, 0)) != 0) {
            /* catch errors */
            if(status != REG_NOMATCH) {
                char err_buff[100];
                regerror(status, &re, err_buff, 100);
                warning(YEL"WARN: "NCO" Invalid regex pattern: '%s'\n", err_buff);
            }
        }
        /* finalize */
        regfree(&re);
        return (set->invmatch) ? !(status) : (status);
    }
}

/* ------------------------------------------------------------- */

/* Mac OSX hackery */
#ifndef FTW_STOP
/* Non-zero value terminates ftw() */
#define FTW_STOP 1
#endif

#ifndef FTW_CONTINUE
#define FTW_CONTINUE 0
#endif

#ifndef FTW_SKIP_SIBLINGS
#define FTW_SKIP_SIBLINGS 0
#endif

#ifndef FTW_SKIP_SUBTREE
#define FTW_SKIP_SUBTREE 0
#endif

#ifndef FTW_ACTIONRETVAL
#define FTW_ACTIONRETVAL 0
#endif
/*
 * Callbock from nftw()
 * If the file given from nftw by "path":
 * - is a directory: recurs into it and catch the files there,
 *  as long the depth doesnt get bigger than max_depth and contains the pattern  cmp_pattern
 * - a file: Push it back to the list, if it has "cmp_pattern" in it. (if --regex was given)
 * If the user interrupts, nftw() stops, and the program will do it'S magic.
 *
 * Not many comments, because man page of nftw will explain everything you'll need to understand
 *
 * Appendum: rmlint uses the inode to sort the contents before doing any I/O to speed up things.
 *           This is nevertheless limited to Unix filesystems like ext*, reiserfs.. (might work in MacOSX - don't know)
 *           note that for btrfs, the inode numbers probably don't map very well to disk block numbers;
 *           TODO:sort by first physical block number (from FIBMAP ioctl) - should be faster?
 *           TODO:If someone likes to port this to Windows he would to replace the inode number by the MFT entry point, or simply disable it
 *           I personally don't have a win comp and won't port it, as I don't found many users knowing what that black window with white
 *           white letters can do ;-)
 */

/* ------------------------------------------------------------- */


/* ------------------------------------------------------------- */

/* If we have more than one path, several lint_ts   *
 *  may point to the same (physically same!) file.  *
 *  This would result in potentially dangerous false *
 * positives where the "duplicate" gets deleted but it*
 * was physically the same as the original so now there*
 * is no original. - Kick'em   */
static nuint_t rm_double_paths(file_group *fp) {
    RmFile *b = (fp) ? fp->grp_stp : NULL;
    nuint_t c = 0;
    /* This compares inode and devID
     * This a little bruteforce, but works just fine :-)
     * Note it *will* also kick hardlinked duplicates out (but since
     * they occupy no space anyway it is not such a big deal).
     * TODO: A workaround to keep hardlinked dupes in the search
     * would have to distinguish between hard links (which are
     * safe to include) and bind mounts pointing to
     * the same file (which are dangerous to include).
     * The coreutil df.c has code for this so may be a
     * good start point */

    RmFileList *list = list_begin();

    if(b) {
        while(b->next) {
            if((b->node == b->next->node) &&
                    (b->dev  == b->next->dev) &&
                    ((set->find_hardlinked_dupes==0) ||
                     (strcmp(rmlint_basename(b->path),rmlint_basename(b->next->path))==0)) ) {
                /* adjust grp */
                RmFile *tmp = b;
                fp->size -= b->fsize;
                fp->len--;
                if(b->next->in_ppath || !b->in_ppath) {
                    /* Remove this one  */
                    b = list_remove(b);
                } else {
                    /* b is in preferred path; remove next one */
                    b = b->next;
                    tmp = b;
                    b = list_remove(b);
                }
                /* Update group info */
                if(tmp == fp->grp_stp) {
                    fp->grp_stp = b;
                }
                if(tmp == fp->grp_enp) {
                    fp->grp_enp = b;
                }
                c++;
            } else {
                b=b->next;
            }
        }
    }
    return c;
}

/* ------------------------------------------------------------- */

/* Sort criteria for sorting by dev and inode */
/* This sorts by device second then basename (important later for
 * find_double_bases) */
static long cmp_nd(RmFile *a, RmFile *b) {
    if (a->node != b->node)
        return ((long)(a->node) - (long)(b->node));
    else if (a->dev != b->dev)
        return ((long)(a->dev) - (long)(b->dev));
    else
        return (long)(strcmp(rmlint_basename(a->path),rmlint_basename(b->path)));
}

/* ------------------------------------------------------------- */

/* Sort criteria for sorting by preferred path (first) then user-input criteria */
static long cmp_orig_criteria(RmFile *a, RmFile *b) {

    if (a->in_ppath != b->in_ppath)
        return a->in_ppath - b->in_ppath;
    else {
        int i=0;
        gsize sort_criteria_len = strlen(set->sort_criteria);
        while (i < sort_criteria_len) {
            long cmp=0;
            switch (set->sort_criteria[i]) {
            case 'm':
                cmp = (long)(a->mtime) - (long)(b->mtime);
                break;
            case 'M':
                cmp = (long)(b->mtime) - (long)(a->mtime);
                break;
            case 'a':
                cmp = strcmp (rmlint_basename(a->path),rmlint_basename (b->path));
                break;
            case 'A':
                cmp = strcmp (rmlint_basename(b->path),rmlint_basename (a->path));
                break;
            case 'p':
                cmp = (long)a->pnum - (long)b->pnum;
                break;
            case 'P':
                cmp = (long)b->pnum - (long)a->pnum;
                break;
            }
            if (cmp !=0) return cmp;
            i++;
        }
    }
    return 0;
}

/* ------------------------------------------------------------- */

/* Compares the "fp" array of the RmFile a and b */
static int cmp_fingerprints(RmFile *a,RmFile *b) {
    int i,j;
    /* compare both fp-arrays */
    for(i=0; i<2; i++) {
        for(j=0; j<MD5_LEN; j++) {
            if(a->fp[i][j] != b->fp[i][j]) {
                return  0;
            }
        }
    }
    /* Also compare the bytes which were read 'on the fly' */
    for(i=0; i<BYTE_MIDDLE_SIZE; i++) {
        if(a->bim[i] != b->bim[i]) {
            return 0;
        }
    }
    /* Let it pass! */
    return 1;
}

/* ------------------------------------------------------------- */

/* Compare criteria of checksums */
static int cmp_f(RmFile *a, RmFile *b) {
    int i, fp_i, x;
    int is_empty[2][3] = { {1,1,1}, {1,1,1} };
    for(i = 0; i < MD5_LEN; i++) {
        if(a->md5_digest[i] != b->md5_digest[i]) {
            return 1;
        }
        if(a->md5_digest[i] != 0) {
            is_empty[0][0] = 0;
        }
        if(b->md5_digest[i] != 0) {
            is_empty[1][0] = 0;
        }
    }
    for(fp_i = 0; fp_i < 2; fp_i++) {
        for(i = 0; i < MD5_LEN; i++) {
            if(a->fp[fp_i][i] != b->fp[fp_i][i]) {
                return 1;
            }
            if(a->fp[fp_i][i] != 0) {
                is_empty[0][fp_i+1] = 0;
            }
            if(b->fp[fp_i][i] != 0) {
                is_empty[1][fp_i+1] = 0;
            }
        }
    }
    /* check for empty checkusm AND fingerprints - refuse and warn */
    for(x=0; x<2; x++) {
        if(is_empty[x][0] && is_empty[x][1] && is_empty[x][2]) {
            warning(YEL"WARN: "NCO"Refusing file with empty checksum and empty fingerprint.\n");
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------- */
static int paranoid(const RmFile *p1, const RmFile *p2) {
    int result = 0,file_a,file_b;
    char * file_map_a, * file_map_b;
    if(!p1 || !p2)
        return 0;
    if(p1->fsize != p2->fsize)
        return 0;
    if((file_a = open(p1->path, MD5_FILE_FLAGS)) == -1) {
        perror(RED"ERROR:"NCO"sys:open()");
        return 0;
    }
    if((file_b = open(p2->path, MD5_FILE_FLAGS)) == -1) {
        perror(RED"ERROR:"NCO"sys:open()");
        return 0;
    }
    if(p1->fsize < MMAP_LIMIT && p1->fsize > MD5_IO_BLOCKSIZE>>1) {
        file_map_a = mmap(NULL, (size_t)p1->fsize, PROT_READ, MAP_PRIVATE, file_a, 0);
        if(file_map_a != MAP_FAILED) {
            if(madvise(file_map_a,p1->fsize, MADV_SEQUENTIAL) == -1)
                perror("madvise");
            file_map_b = mmap(NULL, (size_t)p2->fsize, PROT_READ, MAP_PRIVATE, file_a, 0);
            if(file_map_b != MAP_FAILED) {
                if(madvise(file_map_b,p2->fsize, MADV_SEQUENTIAL) == -1)
                    perror("madvise");
                result = !memcmp(file_map_a, file_map_b, p1->fsize);
                munmap(file_map_b,p1->fsize);
            } else {
                perror("paranoid->mmap");
                result = 0;
            }
            munmap(file_map_a,p1->fsize);
        } else {
            perror("paranoid->mmap");
            result = 0;
        }
    } else { /* use fread() */
        nuint_t blocksize = MD5_IO_BLOCKSIZE/2;
        char * read_buf_a = alloca(blocksize);
        char * read_buf_b = alloca(blocksize);
        int read_a=-1,read_b=-1;
        while(read_a && read_b) {
            if((read_a=read(file_a,read_buf_a,blocksize) == -1)) {
                result = 0;
                break;
            }
            if((read_b=read(file_b,read_buf_b,blocksize) == -1)) {
                result = 0;
                break;
            }
            if(read_a == read_b) {
                if((result = !memcmp(read_buf_a,read_buf_b,read_a)) == 0) {
                    break;
                }
            } else {
                result = 0;
                break;
            }
        }
    }
    if(close(file_a) == -1)
        perror(RED"ERROR:"NCO"close()");
    if(close(file_b) == -1)
        perror(RED"ERROR:"NCO"close()");
    return result;
}

/* ------------------------------------------------------------- */


/* Callback from build_checksums */
static void *cksum_cb(void * vp) {
    file_group *gp = vp;
    RmFile *file = gp->grp_stp;
    /* Iterate over all files in group */
    while(file && file != gp->grp_enp) {
        /* See md5.c */
        md5_file(file);
        file=file->next;
    }
    /* free group, for each thread */
    if(gp) {
        free(gp);
    }
    /* same as pthread_exit() */
    return NULL;
}

/* ------------------------------------------------------------- */

static void build_fingerprints (file_group *grp) {
    RmFile *p = grp->grp_stp;

    nuint_t grp_sz;
    /* Prevent crashes (should not happen too often) */
    if(!grp || grp->grp_stp == NULL) {
        return;
    }
    /* The size read in to build a fingerprint */
    grp_sz = MD5_FPSIZE_FORM(grp->grp_stp->fsize);
    /* Clamp it to some maximum (4KB) */
    grp_sz = (grp_sz > MD5_FP_MAX_RSZ) ? MD5_FP_MAX_RSZ : grp_sz;
    /* Calc fingerprints  */
    /* Note: this is already multithreaded, because large groups get their own thread */
    /* No need for additional threading code here */
    while(p) {
        /* see md5.c for explanations */
        md5_fingerprint(p, grp_sz);
        p=p->next;
    }
}
/* ------------------------------------------------------------- */

static void build_checksums(file_group *grp) {
    if(grp == NULL || grp->grp_stp == NULL) {
        if(grp->grp_stp==NULL) {
            printf("WARN: Empty group received: (end_pointer: %s) len: %lu bsize: %lu\n",(grp->grp_enp) ? grp->grp_enp->path : "null",
                   (long unsigned)grp->len,
                   (long unsigned)grp->size
                  );
        }
        return;
    }
    if(set->threads == 1 ||  grp->size < (2 * MD5_MTHREAD_SIZE)) {
        /* Just loop through this group and built the checksum */
        file_group * whole_grp = malloc(sizeof(file_group));
        whole_grp->grp_stp = grp->grp_stp;
        whole_grp->grp_enp = NULL;
        cksum_cb((void*)whole_grp);
    } else { /* split group in subgroups and start a seperate thread for each */
        nuint_t  sz = 0;
        RmFile * ptr = grp->grp_stp;
        RmFile * lst = grp->grp_stp;
        /* The refereces to all threads */
        pthread_t * thread_queue = malloc((grp->size / MD5_MTHREAD_SIZE + 2) * sizeof(pthread_t));
        int thread_counter = 0, ii = 0;
        while(ptr) {
            sz += ptr->fsize;
            if(sz >= MD5_MTHREAD_SIZE || ptr->next == NULL) {
                /* This part here was bugging me quite a while
                   In previous versions I passed a non-dynamically-allocated file_group to the thread.
                   From what I understood at this time, this shoudl work, becuase C/gcc would take care, or at least warn me.
                   Well, It didn't. Neither I was warned, nor it crashed, nor it worked, because the local variable wasn't
                   valid after the 'if'-statement ended. So the thread was a bit clueless what happened and just continued with
                   building checksums. So about 300% too many checksums where build for nothiing.

                   Short version: Never pass local variables (or stackallocated buffers) in general to threads.
                   */
                file_group * sub_grp = malloc(sizeof(file_group));
                sub_grp->grp_stp = lst;
                sub_grp->grp_enp = ptr->next;
                sub_grp->size = sz;
                /* Update */
                ptr = ptr->next;
                lst = ptr;
                sz  = 0;
                /* Now create the thread */
                if(pthread_create(&thread_queue[thread_counter++], NULL, cksum_cb, sub_grp)) {
                    perror(RED"ERROR: "NCO"pthread_create in build_checksums()");
                }
            } else {
                ptr = ptr->next;
            }
        }
        /* Make sure all threads are joined */
        for(ii = 0; ii < thread_counter; ii++) {
            if(pthread_join(thread_queue[ii],NULL)) {
                perror(RED"ERROR: "NCO"pthread_join in build_checksums()");
            }
        }
        /* free */
        if(thread_queue) {
            free(thread_queue);
        }
    }
}

/* ------------------------------------------------------------- */

bool findmatches(file_group *grp, int testlevel) {
    RmFile *i = grp->grp_stp, *j;
    file_group * island = malloc(sizeof(file_group));
    file_group * mainland = malloc(sizeof(file_group));
    int returnval = 0;  /* not sure what we are using this for */
    /* but for now it will be 1 if any dupes found*/
    mainland->grp_stp=grp->grp_stp;
    mainland->grp_enp=grp->grp_enp;
    mainland->len=grp->len;
    mainland->size=grp->size;



    if(i == NULL) {
        return false;
    }
    switch (testlevel) {
    case 1:
        /*fingerprint compare - calculate fingerprints*/
        build_fingerprints(mainland);
        break;
    case 2:
        /*md5 compare - calculate checksums*/
        build_checksums(mainland);
        break;
    case 3:
        break;
    default:
        break;
    }

    warning(NCO);
    while(i) {
        int num_orig = 0;
        int num_non_orig = 0;

        /*start new island of matched files  */
        /* first remove i from mainland */
        mainland->grp_stp = i->next;
        if (mainland->grp_stp) mainland->grp_stp->last=NULL;
        mainland->len--;
        mainland->size -= i->fsize;

        /*now add i to island as first and only member*/
        island->grp_stp = i;
        island->grp_enp = i;
        island->len     = 1;
        island->size    = i->fsize;
        num_orig += i->in_ppath;
        num_non_orig += !i->in_ppath;
        i->next=NULL;
        i->last=NULL;

        j=mainland->grp_stp;
        while(j) {
            int match = 0;
            switch (testlevel) {
            case 1:
                /*fingerprint compare*/
                match = (cmp_fingerprints(i,j) == 1);
                break;
            case 2:
                /*md5 compare*/
                match = ( cmp_f(i,j) == 0 );
                break;
            case 3:
                /* If we're bothering with paranoid users - Take the gatling! */
                match = ((set->paranoid)?paranoid(i,j):1);
                break;
            default:
                match = 0;
                break;
            }
            if (match) {
                /* move j from grp onto island*/
                /* first get pointer to j before we start messing with j*/
                RmFile *tmp = j;
                /* now remove j from grp */
                if(j->last&&j->next) {
                    j->last->next = j->next;
                    j->next->last = j->last;
                } else if(j->last&&!j->next) {
                    j->last->next=NULL;
                } else if(!j->last&&j->next) {
                    j->next->last=NULL;
                }

                if (mainland->grp_stp == j) {
                    if (j->next) {
                        mainland->grp_stp = j->next;
                    } else {
                        mainland->grp_stp = NULL;
                    }
                }
                if (mainland->grp_enp == j) {
                    /*removing j from end of grp*/
                    if (j->last) mainland->grp_enp = j->last;
                    else mainland->grp_enp = NULL;
                }

                mainland->len --;
                mainland->size -= j->fsize;
                j = j->next;

                /* now add j to island, at the end */
                island->grp_enp->next = tmp;
                tmp->last=island->grp_enp;
                tmp->next=NULL;
                island->grp_enp = tmp;
                island->len ++;
                island->size += tmp->fsize;
                num_orig += tmp->in_ppath;
                num_non_orig += !tmp->in_ppath;
            } else {
                j = j->next;
            }
        }

        /* So we have created an island of everything that matched i. */
        /* Now check if it is singleton or if it fails the other      */
        /* criteria related to setting must_match_original or 		  */
        /* keep_all_originals										  */
        if ( (island->len == 1) ||
                ((set->keep_all_originals==1) && (num_non_orig==0) ) ||
                ((set->must_match_original==1) && (num_orig==0) ) ) {
            /* cast island adrift */
            i = island->grp_stp;
            while (i) {
                i = list_remove(i);
            }
            /*list_clear(island->grp_stp);*/
            island->grp_stp = NULL;
            island->grp_enp = NULL;
        } else {
            if ( (testlevel == 3) ||
                    (!set->paranoid && (testlevel == 2) ) ) {
                /* done testing; process the island */
                island->grp_stp=list_sort(island->grp_stp, cmp_orig_criteria);
                returnval = ( returnval || process_doop_groop(island) );
            } else {
                /* go to next level */
                returnval = ( returnval ||
                              findmatches ( island, testlevel + 1 ) );
            }
        }

        i = mainland->grp_stp;
    }
    return returnval;
}


/* ------------------------------------------------------------- */

/* Callback from scheduler that actually does the work for ONE group */
static void* scheduler_cb(void *gfp) {
    /* cast from *void */
    file_group *group = gfp;
    if(group == NULL || group->grp_stp == NULL) {
        return NULL;
    }
    /* start matching (start at level 1 (fingerprint filter)) then */
    /* recursively escalates to higher levels                      */
    if(findmatches(group, 1)) {
        /* this happens when 'q' was selected in askmode */
        /* note: group should be empty by now so following not needed?*/
        /*list_clear(group->grp_stp);
        group->grp_stp = NULL;
        group->grp_enp = NULL;
        die(0);*/
    }
    /* note: group should be empty by now so following not needed?*/
    /*list_clear(group->grp_stp);*/

    return NULL;
}

/* ------------------------------------------------------------- */

/* Joins the threads launched by scheduler */
static void scheduler_jointhreads(pthread_t *tt, nuint_t n) {
    nuint_t ii = 0;
    for(ii=0; ii < n; ii++) {
        if(pthread_join(tt[ii],NULL)) {
            perror(RED"ERROR: "NCO"pthread_join in scheduler()");
        }
    }
}

/* ------------------------------------------------------------- */

/* Distributes the groups on the ressources */
static void start_scheduler(file_group *fp, nuint_t nlistlen) {
    nuint_t ii;
    pthread_t *tt = malloc(sizeof(pthread_t)*(nlistlen+1));
    if(set->threads == 1) {
        for(ii = 0; ii < nlistlen && !iAbort; ii++) {
            scheduler_cb(&fp[ii]);
        }
    } else { /* if size of certain group exceeds limit start an own thread, else run in 'foreground' */
        /* Run max set->threads at the same time. */
        int nrun = 0;
        for(ii = 0; ii < nlistlen && !iAbort; ii++) {
            if(fp[ii].size > THREAD_SHEDULER_MTLIMIT) { /* Group exceeds limit */
                if(pthread_create(&tt[nrun],NULL,scheduler_cb,(void*)&fp[ii])) {
                    perror(RED"ERROR: "NCO"pthread_create in scheduler()");
                }
                if(nrun >= set->threads-1) {
                    scheduler_jointhreads(tt, nrun + 1);
                    nrun = 0;
                    continue;
                }
                nrun++;
            } else { /* run in fg */
                scheduler_cb(&fp[ii]);
            }
        }
        scheduler_jointhreads(tt, nrun);
    }
    if(tt) {
        free(tt);
    }
}

/* ------------------------------------------------------------- */

/* Sort group array via qsort() */
static int cmp_grplist_bynodes(const void *a,const void *b) {
    const file_group *ap = a;
    const file_group *bp = b;
    if(ap && bp) {
        if(ap->grp_stp && bp->grp_stp) {
            return ap->grp_stp->node - bp->grp_stp->node;
        }
    }
    return -1;
}

/* ------------------------------------------------------------- */

/* Takes num and converts into some human readable string. 1024 -> 1KB */
static void size_to_human_readable(nuint_t num, char *in, int sz) {
    if(num < 1024 / 2) {
        sprintf(in,"%ld B",(unsigned long)num);
    } else if(num < 1048576) {
        sprintf(in,"%.2f KB",(float)(num/1024.0));
    } else if(num < 1073741824 / 2) {
        sprintf(in,"%.2f MB",(float)(num/1048576.0));
    } else {
        sprintf(in,"%.2f GB",(float)(num/1073741824.0));
    }
}

/* ------------------------------------------------------------- */

static void find_double_bases(RmFile *starting) {
    RmFile *i = starting;
    RmFile *j = NULL;
    bool phead = true;
    while(i) {
        if(i->dupflag != TYPE_BASE) {
            bool pr = false;
            j=i->next;
            while(j) {
                /* compare basenames */
                if(!strcmp(rmlint_basename(i->path), rmlint_basename(j->path)) && i->node != j->node && j->dupflag != TYPE_BASE) {
                    RmFile *x = j;
                    char *tmp2 = realpath(j->path, NULL);
                    if(phead) {
                        error("\n%s#"NCO" Double basename(s):\n", (set->verbosity > 1) ? GRE : NCO);
                        phead = false;
                    }
                    if(!pr) {
                        char * tmp = realpath(i->path, NULL);
                        i->dupflag = TYPE_BASE;
                        error("   %sls"NCO" %s\n", (set->verbosity!=1) ? GRE : "", tmp,i->fsize);
                        write_to_log(i,false,NULL);
                        pr = true;
                        if(tmp) {
                            free(tmp);
                        }
                        /* At this point files with same inode and device are NOT handled yet.
                           Therefore this foolish, but well working approach is made.
                           (So it works also with more than one dir in the cmd)  */
                        while(x) {
                            if(x->node == j->node) {
                                x->dupflag = TYPE_BASE;
                            }
                            x=x->next;
                        }
                    }
                    j->dupflag = TYPE_BASE;
                    error("   %sls"NCO" %s\n",(set->verbosity!=1) ? GRE : "",tmp2);
                    dbase_ctr++;
                    write_to_log(j,false,NULL);
                    if(tmp2) {
                        free(tmp2);
                    }
                }
                j=j->next;
            }
        }
        i=i->next;
    }
}

/* ------------------------------------------------------------- */

/* You don't have to understand this really - don't worry. */
/* Only used in conjuction with qsort to make output a lil nicer */
static long cmp_sort_dupID(lint_t* a, lint_t* b) {
    return ((long)a->dupflag-(long)b->dupflag);
}

/* ------------------------------------------------------------- */

/* This the actual main() of rmlint */
void start_processing(RmFile *b) {
    file_group *fglist = NULL,
                emptylist;
    char lintbuf[128];
    char suspbuf[128];
    nuint_t spelen       = 0,
            rem_counter  = 0,
            suspicious   = 0,
            path_doubles = 0;
    if(set->namecluster) {
        find_double_bases(b);
        error("\n");
    }
    emptylist.len  = 1;
    emptylist.size = 0;
    /* Split into groups, based by size */
    while(b) {
        RmFile *q = b, *prev = NULL;
        nuint_t glen = 0, gsize = 0;
        nuint_t num_pref = 0;
        nuint_t num_nonpref = 0;
        while(b && q->fsize == b->fsize) {
            prev = b;
            if(b->dupflag == false) {
                b->dupflag = true;
            }
            if(b->in_ppath) num_pref++;
            else num_nonpref++;

            gsize += b->fsize;
            glen++;
            b = b->next;
        }
        if(glen == 1) {
            /* This is only a single element        */
            /* We can remove it without feeling bad */
            q = list_remove(q);
            if(b != NULL) {
                b = q;
            }
            rem_counter++;
        // TODO: Port this.
        } else if (((set->must_match_original) && (num_pref == 0) ) ||
                   /* isle doesn't contain any ppath original entries, but options
                    * require ppath entry; or...*/
                   ((set->keep_all_originals) && (num_nonpref == 0) ))
            /* isle contains only ppath original entries, but options
             * don't allow deleting any ppath files so no point searching*/
        {
            /* delete this isle */

            while (q != b) {
                q = list_remove(q);
                rem_counter++;
            }

        } else {
            /* Mark this isle as 'sublist' by setting next/last pointers */
            if(b != NULL) {
                prev->next = NULL;
                b->last = NULL;
            }
            if(q->fsize != 0) {
                /* Sort by inode (speeds up IO on normal HDs [not SSDs]) */
                q = list_sort(q,cmp_nd);
                /* Add this group to the list array */
                fglist = realloc(fglist, (spelen+1) * sizeof(file_group));
                fglist[spelen].grp_stp = q;
                fglist[spelen].grp_enp = prev;
                fglist[spelen].len     = glen;
                fglist[spelen].size    = gsize;

                /* Remove 'path-doubles' (files pointing to the physically SAME file) - this requires a node sorted list */
                if((set->followlinks && get_cpindex() == 1) || ( get_cpindex() > 1 ) || 1 )
                    /* actually need this even for a single path because of possible bind mounts */
                    path_doubles += rm_double_paths(&fglist[spelen]);
                /* number_of_groups++ */
                spelen++;
            } else { /* this is some other sort of lint (indicated by a size of 0) */
                RmFile *ptr;
                char flag = 42;
                bool e_file_printed = false;
                const char * chown_cmd = "   chown $(whoami):$(id -gn)";
                q = list_sort(q,cmp_nd);
                emptylist.grp_stp = q;
                if((set->followlinks && get_cpindex() == 1) || get_cpindex() > 1 || 1)
                    rm_double_paths(&emptylist);

                /* sort by lint_ID (== dupID) */
                ptr = emptylist.grp_stp;
                ptr = list_sort(ptr,cmp_sort_dupID);
                emptylist.grp_stp = ptr;
                emptylist.len = 0;


                while(ptr) {
                    if(flag != ptr->dupflag) {
                        if(set->verbosity > 1) {
                            error(YEL"\n#"NCO);
                        } else {
                            error("\n#");
                        }
                        /* -- */
                        if(ptr->dupflag == TYPE_BLNK) {
                            error(" Bad link(s): \n");
                        } else if(ptr->dupflag == TYPE_OTMP) {
                            error(" Old Tempfile(s): \n");
                        } else if(ptr->dupflag == TYPE_EDIR) {
                            error(" Empty dir(s): \n");
                        } else if(ptr->dupflag == TYPE_JNK_DIRNAME) {
                            error(" Junk dirname(s): \n");
                        } else if(ptr->dupflag == TYPE_JNK_FILENAME) {
                            error(" Junk filename(s): \n");
                        } else if(ptr->dupflag == TYPE_NBIN) {
                            error(" Non stripped binarie(s): \n");
                        } else if(ptr->dupflag == TYPE_BADUID) {
                            error(" Bad UID: \n");
                        } else if(ptr->dupflag == TYPE_BADGID) {
                            error(" Bad GID: \n");
                        } else if(ptr->fsize == 0 && e_file_printed == false) {
                            error(" Empty file(s): \n");
                            e_file_printed = true;
                        }
                        flag = ptr->dupflag;
                    }
                    if(set->verbosity > 1) {
                        error(GRE);
                    }
                    if(ptr->dupflag == TYPE_BLNK) {
                        error("   rm");
                    } else if(ptr->dupflag == TYPE_OTMP) {
                        error("   rm");
                    } else if(ptr->dupflag == TYPE_EDIR) {
                        error("   rmdir");
                    } else if(ptr->dupflag == TYPE_JNK_DIRNAME) {
                        error("   ls");
                    } else if(ptr->dupflag == TYPE_JNK_FILENAME) {
                        error("   ls");
                    } else if(ptr->dupflag == TYPE_NBIN) {
                        error("   strip --strip-debug");
                    } else if(ptr->dupflag == TYPE_BADUID) {
                        error(chown_cmd);
                    } else if(ptr->dupflag == TYPE_BADGID) {
                        error(chown_cmd);
                    } else if(ptr->fsize   == 0) {
                        error("   rm");
                    }
                    if(set->verbosity > 1) {
                        error(NCO);
                    }
                    error(" %s\n",ptr->path);
                    if(set->output) {
                        write_to_log(ptr, false,NULL);
                    }
                    emptylist.size += ptr->fsize;
                    ptr = list_remove(ptr);
                    emptylist.len++;
                }
            }
        }
    }
    error("\n");
    if(set->searchdup == 0) {
        nuint_t i = 0;
        /* rmlint was originally supposed to find duplicates only
           So we have to free list that whould have been used for
           dup search before dieing */
        for(; i < spelen; i++) {
            list_clear(fglist[i].grp_stp);
        }
        if(fglist) {
            free(fglist);
        }
        die(0);
    }
    info("\nNow attempting to find duplicates. This may take a while...\n");
    info("Now removing files with unique sizes from list...");  /*actually this was done already above while building the list*/
    info(""YEL"%ld item(s) less"NCO" in list.",rem_counter);
    if(path_doubles) {
        info(" (removed "YEL"%ld pathzombie(s)"NCO")", path_doubles);
    }
    info(NCO"\nNow removing "GRE"%ld"NCO" empty files / bad links / junk names from list...\n"NCO, emptylist.len);
    /*actually this was done already above while building the list*/
    info("Now sorting groups based on their location on the drive...");
    /* Now make sure groups are sorted by their location on the disk - TODO? can remove this because was already sorted above?*/
    qsort(fglist, spelen, sizeof(file_group), cmp_grplist_bynodes);
    info(" done. \nNow doing fingerprints and full checksums.%c\n",set->verbosity > 4 ? '.' : '\n');
    db_done = true;
    error("%s Duplicate(s):",(set->verbosity > 1) ? YEL"#"NCO : "#");
    /* Groups are splitted, now give it to the scheduler
     * The scheduler will do another filterstep, build checkusm
     * and compare 'em. The result is printed afterwards */
    start_scheduler(fglist, spelen);
    if(get_dupcounter() == 0) {
        error("\r                    ");
    } else {
        error("\n");
    }
    /* now process the ouptput we gonna print */
    size_to_human_readable(total_lint, lintbuf, 127 /* bufsize */);
    size_to_human_readable(emptylist.size, suspbuf, 127);
    /* Now announce */
    warning("\n"RED"=> "NCO"In total "RED"%llu"NCO" files, whereof "RED"%llu"NCO" are duplicate(s)",get_totalfiles(), get_dupcounter());
    suspicious = emptylist.len + dbase_ctr;
    if(suspicious > 1) {
        warning(RED"\n=> %llu"NCO" other suspicious items found ["GRE"%s"NCO"]",emptylist.len + dbase_ctr,suspbuf);
    }
    warning("\n");
    if(!iAbort) {
        warning(RED"=> "NCO"Totally "GRE" %s "NCO" [%llu Bytes] can be removed.\n", lintbuf, total_lint);
    }
    if((set->mode == 1 || set->mode == 2) && get_dupcounter()) {
        warning(RED"=> "NCO"Nothing removed yet!\n");
    }
    warning("\n");
    if(set->verbosity == 6) {
        info("Now calculation finished.. now writing end of log...\n");
        info(RED"=> "NCO"In total "RED"%llu"NCO" files, whereof "RED"%llu"NCO" are duplicate(s)\n",get_totalfiles(), get_dupcounter());
        if(!iAbort) {
            info(RED"=> "NCO"In total "GRE" %s "NCO" ["BLU"%llu"NCO" Bytes] can be removed without dataloss.\n", lintbuf, total_lint);
        }
    }
    if(get_logstream() == NULL && set->output) {
        error(RED"\nERROR: "NCO);
        fflush(stdout);
        perror("Unable to write log - target file:");
        perror(set->output);
        putchar('\n');
    } else if(set->output) {
        warning("A log has been written to "BLU"%s.log"NCO".\n", set->output);
        warning("A ready to use shellscript to "BLU"%s.sh"NCO".\n", set->output);
    }
    /* End of actual program. Now do some finalizing */
    if(fglist) {
        free(fglist);
    }
}
