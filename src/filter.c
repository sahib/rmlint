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
** Hosted at the time of writing (Do 30. Sep 18:32:19 CEST 2010):
*  on http://github.com/sahib/rmlint
*
**/



/* Needed for nftw() */
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ftw.h>
#include <signal.h>
#include <regex.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>

#include "rmlint.h"
#include "filter.h"
#include "mode.h"
#include "defs.h"
#include "list.h"


uint32 dircount = 0;
uint32 elems = 0;

short iinterrupt = 0;
short tint = 0;

pthread_attr_t p_attr;


/*
 * Callbock from signal()
 * Counts number of CTRL-Cs and reacts
 */
static void interrupt(int p)
{
        /** This was connected with SIGINT (CTRL-C) in recurse_dir() **/
        switch(tint) {
        case 0:
                warning(RED".. "GRE"aborting... "RED"@ \n"NCO);
                break;
        case 1:
                die(1);
        }
        iinterrupt++;
        tint++;
}

/*
 * grep the string "string" to see if it contains the pattern.
 * Will return 0 if yes, 1 otherwise.
 */
int regfilter(const char* input, const char *pattern)
{
        int status;
        int flags = REG_EXTENDED|REG_NOSUB;


        const char *string = basename(input);

        if(!pattern||!string) {
                return 0;
        } else {
                regex_t re;

                if(!set.casematch)
                        flags |= REG_ICASE;

                if(regcomp(&re, pattern, flags))
                        return 0;

                if( (status = regexec(&re, string, (size_t)0, NULL, 0)) != 0) {
                        if(status != REG_NOMATCH) {
                                char err_buff[100];
                                regerror(status, &re, err_buff, 100);
                                warning("Warning: Invalid regex pattern: '%s'\n", err_buff);
                        }
                }

                regfree(&re);
                return (set.invmatch) ? !(status) : (status);
        }
}

/*
 * Callbock from nftw()
 * If the file given from nftw by "path":
 * - is a directory: recurs into it and catch the files there,
 * 	as long the depth doesnt get bigger than max_depth and contains the pattern  cmp_pattern
 * - a file: Push it back to the list, if it has "cmp_pattern" in it. (if --regex was given)
 * If the user interrupts, nftw() stops, and the program will do it'S magic.
 */
int eval_file(const char *path, const struct stat *ptr, int flag, struct FTW *ftwbuf)
{
        if(set.depth && ftwbuf->level > set.depth) {
                /* Do not recurse in this subdir */
                return FTW_SKIP_SIBLINGS;
        }
        if(iinterrupt) {
                return FTW_STOP;
        }
        if(flag == FTW_SLN) {
                error(RED"Bad symlink: %s\n"NCO,path);
                fprintf(get_logstream(),"rm %s #bad link\n",path);
        }
        if(path) {
                if(flag == FTW_F && ptr->st_rdev == 0) {
                        if(!regfilter(path, set.fpattern)) {
                                dircount++;
                                list_append(path, ptr->st_size,ptr->st_dev,ptr->st_ino, ptr->st_nlink);
                        }
                        return FTW_CONTINUE;
                }
        }
        if(flag == FTW_D) {
                if(regfilter(path,set.dpattern)&& strcmp(path,set.paths[get_cpindex()]) != 0) {
                        return FTW_SKIP_SUBTREE;
                }
        }
        return FTW_CONTINUE;
}

/* This calls basically nftw() and sets some options */
int recurse_dir(const char *path)
{
        /* Set options */
        int flags = FTW_ACTIONRETVAL;
        if(!set.followlinks)
                flags |= FTW_PHYS;

        if(0)
                flags |= FTW_DEPTH;

        if(set.samepart)
                flags |= FTW_MOUNT;

        /* Handle SIGINT */
        signal(SIGINT, interrupt);

        /* Start recurse */
        if( nftw(path, eval_file, _XOPEN_SOURCE, flags) == -1) {
                warning("nftw() failed with: %s\n", strerror(errno));
                return EXIT_FAILURE;
        }

        return dircount;
}


/* If we have more than one path, several iFiles  *
 *  may point to the same (physically same file.  *
 *  This woud result in false positves - Kick'em  */
uint32 rm_double_paths(file_group *fp)
{
        iFile *b = fp->grp_stp;
        uint32 c = 0;

        if(b) {
                while(b->next) {
                        if( (b->node == b->next->node) &&
                            (b->dev  == b->next->dev )  ) {
                                iFile *tmp = b;
                                b = list_remove(b);

                                if(tmp == fp->grp_stp)
                                        fp->grp_stp = b;
                                if(tmp == fp->grp_enp)
                                        fp->grp_enp = b;

                                c++;
                        } else {
                                b=b->next;
                        }
                }
        }
        return c;
}

/* Sort criteria for sorting by dev and inode */
static long cmp_nd(iFile *a, iFile *b)
{
        return ((long)(a->dev*a->node) - (long)(b->dev*b->node));
}

/* Compares the "fp" array of the iFiles a and b */
static int cmp_fingerprints(iFile *a,iFile *b)
{
        int i,j;
        for(i=0; i<2; i++) {
                for(j=0; j<MD5_LEN; j++) {
                        if(a->fp[i][j] != b->fp[i][j]) {
                                return  0;
                        }
                }
        }
        return 1;
}

/* Performs a fingerprint check on the group fp */
static uint32 group_filter(file_group *fp)
{
        iFile *p = fp->grp_stp;
        iFile *i,*j;

        uint32 remove_count = 0;
        uint32 fp_sz;

        if(!fp || fp->grp_stp == NULL)
                return 0;

        /* The size read in to build a fingerprint */
        fp_sz = sqrt(fp->grp_stp->fsize / MD5_FP_PERCENT) + 1;

        /* Clamp it to some maximum (4KB) */
        fp_sz = (fp_sz > MD5_FP_MAX_RSZ) ? MD5_FP_MAX_RSZ : fp_sz;

        while(p) {
                md5_fingerprint(p, fp_sz);
                p=p->next;
        }

        /* Compare each other */
        i = fp->grp_stp;

        while(i) {
                if(i->filter) {
                        j=i->next;
                        while(j) {
                                if(j->filter) {
                                        if(cmp_fingerprints(i,j)) {
                                                /* i 'similiar' to j */
                                                j->filter = false;
                                                i->filter = false;
                                        }
                                }
                                j = j->next;
                        }
                        if(i->filter) {
                                iFile *tmp = i;
                                fp->len--;
                                fp->size -= i->fsize;

                                i = list_remove(i);

                                /* Update start / end */
                                if(tmp == fp->grp_stp)
                                        fp->grp_stp = i;

                                if(tmp == fp->grp_enp)
                                        fp->grp_enp = i;

                                remove_count++;
                                continue;
                        } else {
                                i=i->next;
                                continue;
                        }
                }
                i=i->next;
        }
        return remove_count;
}

/* Callback from build_checksums */
static void *cksum_cb(void * vp)
{
        file_group *gp = vp;
        iFile *file = gp->grp_stp;

        int i = 0;
        for(; i < gp->size && file != NULL; i++) {
                md5_file(file);
                file=file->next;
        }
        return NULL;
}

static void build_checksums(file_group *grp)
{
        if(grp == NULL || grp->grp_stp == NULL)
                return;

        if(set.threads == 1 || grp->size < 2*MD5_MTHREAD_SIZE) {
                /* Just loop through this group and built the checksum */
                iFile *p = grp->grp_stp;
                while(p) {
                        md5_file(p);
                        p=p->next;
                }
        } else {
                /* The Group is a bigger one (over 4MB by default),
                 * so it may be more efficient wo work this on more
                 * than one thread (especially with serial IO */

                iFile *ptr = grp->grp_stp;
                int ii = 0, jj = 0;
                int packsize = grp->size / MD5_MTHREAD_SIZE;

                pthread_t *tt = malloc( (grp->len / packsize + 2) * sizeof(pthread_t));
               
                while(ptr) {
                        if(ii == 0 ||  (ii % packsize) == 0) {
                                file_group group_pass;
                                group_pass.grp_stp = ptr;
                                group_pass.size = packsize;

                                if(pthread_create(&tt[jj], NULL, cksum_cb, (void*)&group_pass))
                                        perror("Goaaaad, Im an idiot.. Pthread:");

                                jj++;
                        }

                        ii++;
                        ptr=ptr->next;
                }

                for(ii = 0; ii < jj; ii++) {
                        if(pthread_join(tt[ii],NULL))
                                perror("Pthread is failing.");
                }
                
                if(tt) free(tt); 
        }
}

/* Callback that actually does the work for ONE group */
static void* sheduler_cb(void *gfp)
{
        file_group *group = gfp;
        if(group == NULL || group->grp_stp == NULL)
                return NULL;

        if(set.fingerprint)
                group_filter(group);

        if(group->len == 1) {
                list_clear(group->grp_stp);
                return NULL;
        }

        build_checksums(group);
        findmatches(group);
        list_clear(group->grp_stp);
        return NULL;
}

/* Joins the threads launched by sheduler */
static void sheduler_jointhreads(pthread_t *tt, uint32 n)
{
        uint32 ii = 0;
        for(ii=0; ii < n; ii++) {
                if(pthread_join(tt[ii],NULL))
                        perror("I even suck at joining threads");
        }
}

/* Distributes the groups on the ressources */
static void start_sheduler(file_group *fp, uint32 nlistlen)
{
        uint32 ii;
        pthread_t *tt = malloc(sizeof(pthread_t)*(nlistlen+1));

        if(set.threads == 1) {
                for(ii = 0; ii < nlistlen; ii++) {
                        sheduler_cb(&fp[ii]);
                }
        } else {
                int nrun = 0;
                for(ii = 0; ii < nlistlen; ii++) {
                        if(pthread_create(&tt[nrun],NULL,sheduler_cb,(void*)&fp[ii]))
                                perror("I suck @ pthread.");

                        if(nrun >= set.threads-1) {
                                sheduler_jointhreads(tt, nrun + 1);
                                nrun = 0;
                                continue;
                        }

                        nrun++;
                }
                sheduler_jointhreads(tt, nrun);
        }
        if(tt) free(tt);
}

/* Sort group array via qsort() */
static int cmp_grplist_bynodes(const void *a,const void *b)
{
        const file_group *ap = a;
        const file_group *bp = b;
        if(ap && bp) {
                if(ap->grp_stp && bp->grp_stp) {
                        return ap->grp_stp->node * ap->grp_stp->dev -
                               bp->grp_stp->node * bp->grp_stp->dev ;
                }

        }
        return -1;
}

void size_to_human_readable(uint32 num, char *in)
{
        if(num < 1024) {
                snprintf(in,256,"%ld B",(unsigned long)num);
        } else if(num >= 1024 && num < 1024*1024) {
                snprintf(in,256,"%.2f KB",(float)(num/1024.0));
        } else if(num >= 1024*1024 && num < 1024*1024*1024) {
                snprintf(in,256,"%.2f MB",(float)(num/1024.0/1024.0));
        } else {
                snprintf(in,256,"%.2f GB",(float)(num/1024.0/1024.0/1024.0));
        }
}



/* Splits into groups, and does some serious presortage */
void prefilter(iFile *b)
{
        file_group *fglist = NULL;

        uint32 spelen = 0;
        uint32 ii = 0;

        /* Logvariables - not relevant to algorithm */
        char lintbuf[128];
        uint32 remaining = 0, original = list_len();
        uint32 lint = 0;
        uint32 rem_counter = 0;
        uint32 path_doubles = 0;
        file_group emptylist;
        emptylist.len = 0;

        while(b) {
                iFile *q = b, *prev = NULL;
                uint32 glen = 0, gsize = 0;

                while(b && q->fsize == b->fsize) {
                        prev = b;
                        gsize += b->fsize;
                        glen++;
                        b = b->next;
                }

                if(glen == 1) {
                        /* This is only a single element        */
                        /* We can remove it without feelind bad */
                        q = list_remove(q);
                        if(b != NULL) b = q;
                        rem_counter++;
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
                                if(get_cpindex() > 1 || set.followlinks)
                                        path_doubles += rm_double_paths(&fglist[spelen]);

                                spelen++;
                        } else {
                                iFile *ptr;
                                q = list_sort(q,cmp_nd);
                                emptylist.grp_stp = q;
                                if(get_cpindex() > 1 || set.followlinks)
                                        rm_double_paths(&emptylist);

                                ptr = emptylist.grp_stp;
                                emptylist.len = 0;
                                while(ptr) {
                                        fprintf(get_logstream(), "rm %s #Empty file\n",ptr->path);
                                        ptr = list_remove(ptr);
                                        emptylist.len++;
                                }
                        }

                }
        }

        info("%ld files less in list (Removed %ld pathdoubles first)\n",rem_counter, path_doubles);
        info("By ignoring %ld empty files, list was split in %ld parts.\n", emptylist.len, spelen);
        info("Now sorting groups based on their location on the drive.. ");

        /* Now make sure groups are sorted by their location on the disk*/
        qsort(fglist, spelen, sizeof(file_group), cmp_grplist_bynodes);

        info(" done \nNow doing fingerprints and full checksums:\n\n");

#if DEBUG_CODE == 2
        for(ii = 0; ii < spelen; ii++) {
                iFile *p = fglist[ii].grp_stp;
                error("Group #%2d: ",ii);
                while(p) {
                        error(" %ld |",p->node);
                        p=p->next;
                }
                putchar('\n');
        }
#endif

        /* Grups are splitted, now give it to the sheduler */
        /* The sheduler will do another filterstep, build checkusm
         *  and compare 'em. The result is printed afterwards */
        start_sheduler(fglist, spelen);

        /* Gather the total size of removeable data */
        for(ii=0; ii < spelen; ii++) {
                lint += fglist[ii].size;
                remaining += fglist[ii].len;
        }

        size_to_human_readable(lint, lintbuf);
        info("\nIn total %ld (of %ld files as input) files are duplicates.\n",remaining, original);
        info("This means: %s [%ld Bytes] seems to be useless lint.\n", lintbuf, lint);
        info("A log has been written to %s.\n", SCRIPT_NAME);

        /* Clue list together again (for convinience) */
        for(ii=0; ii < spelen; ii++) {
                if((ii + 1) != spelen && fglist[ii].grp_enp) {
                        fglist[ii].grp_enp->next    = fglist[ii+1].grp_stp;

                        if(fglist[ii+1].grp_stp)
                                fglist[ii+1].grp_stp->last  = fglist[ii].grp_enp;
                }
        };

        /* End of actual program. Now do some file handling / frees */
        if(fglist) free(fglist);
}
