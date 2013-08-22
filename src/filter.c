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
#define _GNU_SOURCE
#define __USE_GNU

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
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
#include "useridcheck.h"

/* global vars, but initialized by filt_c_init() */
nuint_t dircount, dbase_ctr;
bool dir_done, db_done;
int iAbort;

UserGroupList ** global_ug_list = NULL;

/* ------------------------------------------------------------- */

void delete_userlist(void)
{
    userlist_destroy(global_ug_list);
}

/* ------------------------------------------------------------- */

void filt_c_init(void)
{
    dircount  = 0;
    dbase_ctr = 0;
    iAbort   = false;
    dir_done = false;
    db_done  = false;

    global_ug_list = userlist_new();
    atexit(delete_userlist);
}

/* ------------------------------------------------------------- */

/*
 * Callbock from signal()
 * Counts number of CTRL-Cs and reacts
 */
static void interrupt(int p)
{
    switch(p)
    {
    case SIGINT:
        if(iAbort == 2)
        {
            die(-1);
        }
        else if(db_done)
        {
            iAbort = 2;
            warning(GRE"\nINFO: "NCO"Received Interrupt.\n");
        }
        else
        {
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

/* Cheap function to check if c is a char in str */
static int junkinstr(const char *str)
{
    int i = 0, j = 0;
    if(set->junk_chars == NULL)
    {
        return 0;
    }
    for(; set->junk_chars[i]; i++)
    {
        for(j=0; str[j]; j++)
        {
            if(str[j] == set->junk_chars[i])
            {
                return true;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------- */

/* A simply method to test if a file is non stripped binary. Uses popen() */
/* It's a bit slow, because it has to call fork() internally */
static int check_binary_to_be_stripped(const char *path)
{
    FILE *pipe = NULL;
    int bytes = 0;
    char dummy_buf = 0,
         * cmd = NULL,
           * escaped = NULL;
    if(path == NULL)
    {
        return 0;
    }
    /* Escape ' so the shell does not get confused */
    escaped = strsubs(path,"'","'\"'\"'");
    /* The command we're using */
    cmd = strdup_printf("file '%s' | grep 'not stripped'", escaped);
    if(escaped)
    {
        free(escaped);
    }
    /* Pipe  */
    pipe = popen(cmd,"re");
    if(pipe == NULL)
    {
        return 0;
    }
    /* If one byte can be read, the file is a nonstripped binary */
    bytes = fread(&dummy_buf, sizeof(char), 1, pipe);
    /* close pipe */
    pclose(pipe);
    /* clean up */
    if(cmd)
    {
        free(cmd);
    }
    if(bytes)
    {
        return 1;
    }
    /* not a nsb  */
    return 0;
}

/* ------------------------------------------------------------- */

/*
 * grep the string "string" to see if it contains the pattern.
 * Will return 0 if yes, 1 otherwise.
 */
int regfilter(const char* input, const char *pattern)
{
    int status;
    int flags = REG_EXTENDED|REG_NOSUB;
    const char *string;
    if(!pattern)
    {
        return 0;
    }
    else
    {
        regex_t re;
        /* Only grep basename */
        string = basename(input);
        /* forget about case by default */
        if(!set->casematch)
        {
            flags |= REG_ICASE;
        }
        /* compile pattern */
        if(regcomp(&re, pattern, flags))
        {
            return 0;
        }
        /* do the actual work */
        if((status = regexec(&re, string, (size_t)0, NULL, 0)) != 0)
        {
            /* catch errors */
            if(status != REG_NOMATCH)
            {
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
 *           If someone likes to port this to Windows he would to replace the inode number by the MFT entry point, or simply disable it
 *           I personally don't have a win comp and won't port it, as I don't found many users knowing what that black window with white
 *           white letters can do ;-)
 */
static int eval_file(const char *path, const struct stat *ptr, int flag, struct FTW *ftwbuf)
{
    if(set->depth && ftwbuf->level > set->depth)
    {
        /* Do not recurse in this subdir */
        return FTW_SKIP_SIBLINGS;
    }
    if(iAbort)
    {
        return FTW_STOP;
    }
    if(flag == FTW_SLN)
    {
        list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_BLNK);
        return FTW_CONTINUE;
    }
    if(path)
    {
        if(!dir_done)
        {
            char *orig_path = set->paths[get_cpindex()];
            size_t orig_path_len = strlen(set->paths[get_cpindex()]);
            if(orig_path[orig_path_len - 1] == '/') {
                orig_path_len -= 1;
            }
            if(!strncmp(set->paths[get_cpindex()], path, orig_path_len))
            {
                dir_done = true;
                return FTW_CONTINUE;
            }
        }
        if(flag == FTW_F &&
                (set->minsize <= ptr->st_size || set->minsize < 0) &&
                (set->maxsize >= ptr->st_size || set->maxsize < 0))
        {
            if(regfilter(path, set->fpattern))
            {
                return FTW_CONTINUE;
            }
            if(junkinstr(basename(path)))
            {
                list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_JNK_FILENAME);
                if(set->collide)
                {
                    return FTW_CONTINUE;
                }
            }
            if(set->findbadids)
            {
                bool has_gid, has_uid;
                if(userlist_contains(global_ug_list,ptr->st_uid,ptr->st_gid,&has_uid,&has_gid) == false)
                {
                    if(has_gid == false)
                        list_append(path,0,ptr->st_dev,ptr->st_ino,TYPE_BADGID);
                    if(has_uid == false)
                        list_append(path,0,ptr->st_dev,ptr->st_ino,TYPE_BADUID);

                    if(set->collide)
                    {
                        return FTW_CONTINUE;
                    }
                }
            }
            if(set->nonstripped)
            {
                if(check_binary_to_be_stripped(path))
                {
                    list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_NBIN);
                    if(set->collide)
                    {
                        return FTW_CONTINUE;
                    }
                }
            }
            if(set->doldtmp)
            {
                /* This checks only for *~ and .*.swp */
                size_t len = strlen(path);
                if(path[len-1] == '~' || (len>3&&path[len-1] == 'p'&&path[len-2] == 'w'&&path[len-3] == 's'&&path[len-4] == '.'))
                {
                    char *cpy = NULL;
                    struct stat stat_buf;
                    if(path[len - 1] == '~')
                    {
                        cpy = strndup(path,len-1);
                    }
                    else
                    {
                        char * p = strrchr(path,'/');
                        size_t p_len = p-path;
                        char * front = alloca(p_len+1);
                        memset(front, '\0', p_len+1);
                        strncpy(front, path, p_len);
                        cpy = strdup_printf("%s/%s",front,p+2);
                        cpy[strlen(cpy)-4] = 0;
                    }
                    if(!stat(cpy, &stat_buf))
                    {
                        if(ptr->st_mtime - stat_buf.st_mtime >= set->oldtmpdata)
                        {
                            list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_OTMP);
                        }
                    }
                    if(cpy)
                    {
                        free(cpy);
                    }
                    if(set->collide)
                    {
                        return FTW_CONTINUE;
                    }
                }
            }
            /* Check this to be a valid file and NOT a blockfile (reading /dev/null does funny things) */
            if(flag == FTW_F && ptr->st_rdev == 0 && (set->listemptyfiles || ptr->st_size != 0))
            {
                if(!access(path,R_OK))
                {
                    if(set->ignore_hidden)
                    {
                        char *base = basename(path);
                        if(*base != '.')
                        {
                            dircount++;
                            list_append(path, ptr->st_size,ptr->st_dev,ptr->st_ino,1);
                        }
                    }
                    else
                    {
                        dircount++;
                        list_append(path, ptr->st_size,ptr->st_dev,ptr->st_ino,1);
                    }
                }
                return FTW_CONTINUE;
            }
        }
        if(flag == FTW_D)
        {
            if(regfilter(path, set->dpattern) && dir_done)
            {
                return FTW_SKIP_SUBTREE;
            }
            if(junkinstr(basename(path)))
            {
                list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_JNK_DIRNAME);
                if(set->collide)
                {
                    return FTW_SKIP_SUBTREE;
                }
            }
            if(set->findemptydirs)
            {
                int dir_counter = 0;
                DIR *dir_e = opendir(path);
                struct dirent *dir_p = NULL;
                if(dir_e)
                {
                    while((dir_p=readdir(dir_e)) && dir_counter < 2)
                    {
                        dir_counter++;
                    }
                    closedir(dir_e);
                    if(dir_counter == 2 && dir_p == NULL)
                    {
                        list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_EDIR);
                        return FTW_SKIP_SUBTREE;
                    }
                }
            }
            if(set->ignore_hidden && path)
            {
                char *base = basename(path);
                if(*base == '.' && dir_done)
                {
                    return FTW_SKIP_SUBTREE;
                }
            }
        }
    }
    return FTW_CONTINUE;
}

/* ------------------------------------------------------------- */

/* This calls basically nftw() and sets some options */
int recurse_dir(const char *path)
{
    /* Set options */
    int ret = 0;
    int flags = FTW_ACTIONRETVAL;
    if(!set->followlinks)
    {
        flags |= FTW_PHYS;
    }
    if(USE_DEPTH_FIRST)
    {
        flags |= FTW_DEPTH;
    }
    if(set->samepart)
    {
        flags |= FTW_MOUNT;
    }
    /* Handle SIGs */
    signal(SIGINT,  interrupt);
    signal(SIGSEGV, interrupt);
    signal(SIGFPE,  interrupt);
    signal(SIGABRT, interrupt);
    /* (Re)-Init */
    dir_done = false;
    /* Start recurse */
    if((ret = nftw(path, eval_file, _XOPEN_SOURCE, flags)))
    {
        printf("Nftw: %d\n", ret);
        if(ret != FTW_STOP)
        {
            /* Some error occured */
            perror(YEL"FATAL: "NCO"nftw():");
            return EXIT_FAILURE;
        }
        else
        {
            /* The user pressed SIGINT -> Quit from ntfw() to shutdown in peace. */
            error(YEL"quitting.\n"NCO);
            die(0);
        }
    }
    return dircount;
}

/* ------------------------------------------------------------- */

/* If we have more than one path, several lint_ts   *
 *  may point to the same (physically same!) file.  *
 *  This would result in false positves - Kick'em   */
static nuint_t rm_double_paths(file_group *fp)
{
    lint_t *b = (fp) ? fp->grp_stp : NULL;
    nuint_t c = 0;
    /* This compares inode and devID */
    /* This a little bruteforce, but works just fine :-) */
    if(b)
    {
        while(b->next)
        {
            if((b->node == b->next->node) &&
                    (b->dev  == b->next->dev))
            {
                /* adjust grp */
                lint_t *tmp = b;
                fp->size -= b->fsize;
                fp->len--;
                /* Remove this one  */
                b = list_remove(b);
                /* Update group info */
                if(tmp == fp->grp_stp)
                {
                    fp->grp_stp = b;
                }
                if(tmp == fp->grp_enp)
                {
                    fp->grp_enp = b;
                }
                c++;
            }
            else
            {
                b=b->next;
            }
        }
    }
    return c;
}

/* ------------------------------------------------------------- */

/* Sort criteria for sorting by dev and inode */
/* This does not take care of the device, because Linux can read several disks in parallel */
static long cmp_nd(lint_t *a, lint_t *b)
{
    return ((long)(a->node) - (long)(b->node));
}

/* ------------------------------------------------------------- */

/* Compares the "fp" array of the lint_t a and b */
static int cmp_fingerprints(lint_t *a,lint_t *b)
{
    int i,j;
    /* compare both fp-arrays */
    for(i=0; i<2; i++)
    {
        for(j=0; j<MD5_LEN; j++)
        {
            if(a->fp[i][j] != b->fp[i][j])
            {
                return  0;
            }
        }
    }
    /* Also compare the bytes which were read 'on the fly' */
    for(i=0; i<BYTE_MIDDLE_SIZE; i++)
    {
        if(a->bim[i] != b->bim[i])
        {
            return 0;
        }
    }
    /* Let it pass! */
    return 1;
}

/* ------------------------------------------------------------- */

/* Performs a fingerprint check on the group fp */
static nuint_t group_filter(file_group *fp)
{
    lint_t *p = fp->grp_stp;
    lint_t *i,*j;
    nuint_t remove_count = 0;
    nuint_t fp_sz;
    /* Prevent crashes (should not happen too often) */
    if(!fp || fp->grp_stp == NULL)
    {
        return 0;
    }
    /* The size read in to build a fingerprint */
    fp_sz = MD5_FPSIZE_FORM(fp->grp_stp->fsize);
    /* Clamp it to some maximum (4KB) */
    fp_sz = (fp_sz > MD5_FP_MAX_RSZ) ? MD5_FP_MAX_RSZ : fp_sz;
    /* Calc fingerprints  */
    /* Note: this is already multithreaded, because large groups get their own thread */
    /* No need for additional threading code here */
    while(p)
    {
        /* see md5.c for explanations */
        md5_fingerprint(p, fp_sz);
        p=p->next;
    }
    /* Compare each other */
    i = fp->grp_stp;
    while(i)
    {
        /* Useful debugging stuff: */
        /*
           int z = 0;
           MDPrintArr(i->fp[0]);
           printf("|#|");
           MDPrintArr(i->fp[1]);
           printf("|#|");
           for(; z < BYTE_MIDDLE_SIZE; z++)
           {
           printf("%2x",i->bim[z]);
           }
           fflush(stdout);
           */
        if(i->filter)
        {
            j=i->next;
            while(j)
            {
                if(j->filter)
                {
                    if(cmp_fingerprints(i,j))
                    {
                        /* i 'similiar' to j */
                        j->filter = false;
                        i->filter = false;
                    }
                }
                j = j->next;
            }
            if(i->filter)
            {
                lint_t *tmp = i;
                fp->len--;
                fp->size -= i->fsize;
                i = list_remove(i);
                /* Update start / end */
                if(tmp == fp->grp_stp)
                {
                    fp->grp_stp = i;
                }
                if(tmp == fp->grp_enp)
                {
                    fp->grp_enp = i;
                }
                remove_count++;
                continue;
            }
            else
            {
                i=i->next;
                continue;
            }
        }
        i=i->next;
    }
    return remove_count;
}

/* ------------------------------------------------------------- */

/* Callback from build_checksums */
static void *cksum_cb(void * vp)
{
    file_group *gp = vp;
    lint_t *file = gp->grp_stp;
    /* Iterate over all files in group */
    while(file && file != gp->grp_enp)
    {
        /* See md5.c */
        md5_file(file);
        file=file->next;
    }
    /* free group, for each thread */
    if(gp)
    {
        free(gp);
    }
    /* same as pthread_exit() */
    return NULL;
}

/* ------------------------------------------------------------- */

static void build_checksums(file_group *grp)
{
    if(grp == NULL || grp->grp_stp == NULL)
    {
        if(grp->grp_stp==NULL)
        {
            printf("WARN: Empty group received: (end_pointer: %s) len: %lu bsize: %lu\n",(grp->grp_enp) ? grp->grp_enp->path : "null",
                   (long unsigned)grp->len,
                   (long unsigned)grp->size
                  );
        }
        return;
    }
    if(set->threads == 1 ||  grp->size < (2 * MD5_MTHREAD_SIZE))
    {
        /* Just loop through this group and built the checksum */
        file_group * whole_grp = malloc(sizeof(file_group));
        whole_grp->grp_stp = grp->grp_stp;
        whole_grp->grp_enp = NULL;
        cksum_cb((void*)whole_grp);
    }
    else /* split group in subgroups and start a seperate thread for each */
    {
        nuint_t  sz = 0;
        lint_t * ptr = grp->grp_stp;
        lint_t * lst = grp->grp_stp;
        /* The refereces to all threads */
        pthread_t * thread_queue = malloc((grp->size / MD5_MTHREAD_SIZE + 2) * sizeof(pthread_t));
        int thread_counter = 0, ii = 0;
        while(ptr)
        {
            sz += ptr->fsize;
            if(sz >= MD5_MTHREAD_SIZE || ptr->next == NULL)
            {
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
                if(pthread_create(&thread_queue[thread_counter++], NULL, cksum_cb, sub_grp))
                {
                    perror(RED"ERROR: "NCO"pthread_create in build_checksums()");
                }
            }
            else
            {
                ptr = ptr->next;
            }
        }
        /* Make sure all threads are joined */
        for(ii = 0; ii < thread_counter; ii++)
        {
            if(pthread_join(thread_queue[ii],NULL))
            {
                perror(RED"ERROR: "NCO"pthread_join in build_checksums()");
            }
        }
        /* free */
        if(thread_queue)
        {
            free(thread_queue);
        }
    }
}

/* ------------------------------------------------------------- */

/* Callback from scheduler that actually does the work for ONE group */
static void* scheduler_cb(void *gfp)
{
    /* cast from *void */
    file_group *group = gfp;
    if(group == NULL || group->grp_stp == NULL)
    {
        return NULL;
    }
    /* do the fingerprint filter */
    group_filter(group);
    /* Kick empty groups, or groups with 1 elem */
    if(group->grp_stp == NULL || group->len <= 1)
    {
        list_clear(group->grp_stp);
        group->grp_stp = NULL;
        group->grp_enp = NULL;
        return NULL;
    }
    /* build checksum for the rest */
    build_checksums(group);
    /* Finalize and free sublist */
    if(findmatches(group))
    {
        /* this happens when 'q' was selected in askmode */
        list_clear(group->grp_stp);
        group->grp_stp = NULL;
        group->grp_enp = NULL;
        die(0);
    }
    list_clear(group->grp_stp);
    return NULL;
}

/* ------------------------------------------------------------- */

/* Joins the threads launched by scheduler */
static void scheduler_jointhreads(pthread_t *tt, nuint_t n)
{
    nuint_t ii = 0;
    for(ii=0; ii < n; ii++)
    {
        if(pthread_join(tt[ii],NULL))
        {
            perror(RED"ERROR: "NCO"pthread_join in scheduler()");
        }
    }
}

/* ------------------------------------------------------------- */

/* Distributes the groups on the ressources */
static void start_scheduler(file_group *fp, nuint_t nlistlen)
{
    nuint_t ii;
    pthread_t *tt = malloc(sizeof(pthread_t)*(nlistlen+1));
    if(set->threads == 1)
    {
        for(ii = 0; ii < nlistlen && !iAbort; ii++)
        {
            scheduler_cb(&fp[ii]);
        }
    }
    else /* if size of certain group exceeds limit start an own thread, else run in 'foreground' */
    {
        /* Run max set->threads at the same time. */
        int nrun = 0;
        for(ii = 0; ii < nlistlen && !iAbort; ii++)
        {
            if(fp[ii].size > THREAD_SHEDULER_MTLIMIT) /* Group exceeds limit */
            {
                if(pthread_create(&tt[nrun],NULL,scheduler_cb,(void*)&fp[ii]))
                {
                    perror(RED"ERROR: "NCO"pthread_create in scheduler()");
                }
                if(nrun >= set->threads-1)
                {
                    scheduler_jointhreads(tt, nrun + 1);
                    nrun = 0;
                    continue;
                }
                nrun++;
            }
            else /* run in fg */
            {
                scheduler_cb(&fp[ii]);
            }
        }
        scheduler_jointhreads(tt, nrun);
    }
    if(tt)
    {
        free(tt);
    }
}

/* ------------------------------------------------------------- */

/* Sort group array via qsort() */
static int cmp_grplist_bynodes(const void *a,const void *b)
{
    const file_group *ap = a;
    const file_group *bp = b;
    if(ap && bp)
    {
        if(ap->grp_stp && bp->grp_stp)
        {
            return ap->grp_stp->node - bp->grp_stp->node;
        }
    }
    return -1;
}

/* ------------------------------------------------------------- */

/* Takes num and converts into some human readable string. 1024 -> 1KB */
static void size_to_human_readable(nuint_t num, char *in, int sz)
{
    if(num < 1024 / 2)
    {
        sprintf(in,"%ld B",(unsigned long)num);
    }
    else if(num < 1048576)
    {
        sprintf(in,"%.2f KB",(float)(num/1024.0));
    }
    else if(num < 1073741824 / 2)
    {
        sprintf(in,"%.2f MB",(float)(num/1048576.0));
    }
    else
    {
        sprintf(in,"%.2f GB",(float)(num/1073741824.0));
    }
}

/* ------------------------------------------------------------- */

static void find_double_bases(lint_t *starting)
{
    lint_t *i = starting;
    lint_t *j = NULL;
    bool phead = true;
    while(i)
    {
        if(i->dupflag != TYPE_BASE)
        {
            bool pr = false;
            j=i->next;
            while(j)
            {
                /* compare basenames */
                if(!strcmp(basename(i->path), basename(j->path)) && i->node != j->node && j->dupflag != TYPE_BASE)
                {
                    lint_t *x = j;
                    char *tmp2 = realpath(j->path, NULL);
                    if(phead)
                    {
                        error("\n%s#"NCO" Double basename(s):\n", (set->verbosity > 1) ? GRE : NCO);
                        phead = false;
                    }
                    if(!pr)
                    {
                        char * tmp = realpath(i->path, NULL);
                        i->dupflag = TYPE_BASE;
                        error("   %sls"NCO" %s\n", (set->verbosity!=1) ? GRE : "", tmp,i->fsize);
                        write_to_log(i,false,NULL);
                        pr = true;
                        if(tmp)
                        {
                            free(tmp);
                        }
                        /* At this point files with same inode and device are NOT handled yet.
                           Therefore this foolish, but well working approach is made.
                           (So it works also with more than one dir in the cmd)  */
                        while(x)
                        {
                            if(x->node == j->node)
                            {
                                x->dupflag = TYPE_BASE;
                            }
                            x=x->next;
                        }
                    }
                    j->dupflag = TYPE_BASE;
                    error("   %sls"NCO" %s\n",(set->verbosity!=1) ? GRE : "",tmp2);
                    dbase_ctr++;
                    write_to_log(j,false,NULL);
                    if(tmp2)
                    {
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
static long cmp_sort_dupID(lint_t* a, lint_t* b)
{
    return ((long)a->dupflag-(long)b->dupflag);
}

/* ------------------------------------------------------------- */

/* This the actual main() of rmlint */
void start_processing(lint_t *b)
{
    file_group *fglist = NULL,
                emptylist;
    char lintbuf[128];
    char suspbuf[128];
    nuint_t ii           = 0,
            lint         = 0,
            spelen       = 0,
            rem_counter  = 0,
            suspicious   = 0,
            path_doubles = 0;
    if(set->namecluster)
    {
        find_double_bases(b);
        error("\n");
    }
    emptylist.len  = 1;
    emptylist.size = 0;
    /* Split into groups, based by size */
    while(b)
    {
        lint_t *q = b, *prev = NULL;
        nuint_t glen = 0, gsize = 0;
        while(b && q->fsize == b->fsize)
        {
            prev = b;
            if(b->dupflag == false)
            {
                b->dupflag = true;
            }
            gsize += b->fsize;
            glen++;
            b = b->next;
        }
        if(glen == 1)
        {
            /* This is only a single element        */
            /* We can remove it without feelind bad */
            q = list_remove(q);
            if(b != NULL)
            {
                b = q;
            }
            rem_counter++;
        }
        else
        {
            /* Mark this isle as 'sublist' by setting next/last pointers */
            if(b != NULL)
            {
                prev->next = NULL;
                b->last = NULL;
            }
            if(q->fsize != 0)
            {
                /* Sort by inode (speeds up IO on normal HDs [not SSDs]) */
                q = list_sort(q,cmp_nd);
                /* Add this group to the list array */
                fglist = realloc(fglist, (spelen+1) * sizeof(file_group));
                fglist[spelen].grp_stp = q;
                fglist[spelen].grp_enp = prev;
                fglist[spelen].len     = glen;
                fglist[spelen].size    = gsize;
                /* Remove 'path-doubles' (files pointing to the physically SAME file) - this requires a node sorted list */
                if((set->followlinks && get_cpindex() == 1) || get_cpindex() > 1)
                    path_doubles += rm_double_paths(&fglist[spelen]);
                /* number_of_groups++ */
                spelen++;
            }
            else /* this is some other sort of lint (indicated by a size of 0) */
            {
                lint_t *ptr;
                char flag = 42;
                bool e_file_printed = false;
                const char * chown_cmd = "   chown $(whoami):$(id -gn)";
                q = list_sort(q,cmp_nd);
                emptylist.grp_stp = q;
                if((set->followlinks && get_cpindex() == 1) || get_cpindex() > 1)
                    rm_double_paths(&emptylist);

                /* sort by lint_ID (== dupID) */
                ptr = emptylist.grp_stp;
                ptr = list_sort(ptr,cmp_sort_dupID);
                emptylist.grp_stp = ptr;
                emptylist.len = 0;


                while(ptr)
                {
                    if(flag != ptr->dupflag)
                    {
                        if(set->verbosity > 1)
                        {
                            error(YEL"\n#"NCO);
                        }
                        else
                        {
                            error("\n#");
                        }
                        /* -- */
                        if(ptr->dupflag == TYPE_BLNK)
                        {
                            error(" Bad link(s): \n");
                        }
                        else if(ptr->dupflag == TYPE_OTMP)
                        {
                            error(" Old Tempfile(s): \n");
                        }
                        else if(ptr->dupflag == TYPE_EDIR)
                        {
                            error(" Empty dir(s): \n");
                        }
                        else if(ptr->dupflag == TYPE_JNK_DIRNAME)
                        {
                            error(" Junk dirname(s): \n");
                        }
                        else if(ptr->dupflag == TYPE_JNK_FILENAME)
                        {
                            error(" Junk filename(s): \n");
                        }
                        else if(ptr->dupflag == TYPE_NBIN)
                        {
                            error(" Non stripped binarie(s): \n");
                        }
                        else if(ptr->dupflag == TYPE_BADUID)
                        {
                            error(" Bad UID: \n");
                        }
                        else if(ptr->dupflag == TYPE_BADGID)
                        {
                            error(" Bad GID: \n");
                        }
                        else if(ptr->fsize == 0 && e_file_printed == false)
                        {
                            error(" Empty file(s): \n");
                            e_file_printed = true;
                        }
                        flag = ptr->dupflag;
                    }
                    if(set->verbosity > 1)
                    {
                        error(GRE);
                    }
                    if(ptr->dupflag == TYPE_BLNK)
                    {
                        error("   rm");
                    }
                    else if(ptr->dupflag == TYPE_OTMP)
                    {
                        error("   rm");
                    }
                    else if(ptr->dupflag == TYPE_EDIR)
                    {
                        error("   rmdir");
                    }
                    else if(ptr->dupflag == TYPE_JNK_DIRNAME)
                    {
                        error("   ls");
                    }
                    else if(ptr->dupflag == TYPE_JNK_FILENAME)
                    {
                        error("   ls");
                    }
                    else if(ptr->dupflag == TYPE_NBIN)
                    {
                        error("   strip --strip-debug");
                    }
                    else if(ptr->dupflag == TYPE_BADUID)
                    {
                        error(chown_cmd);
                    }
                    else if(ptr->dupflag == TYPE_BADGID)
                    {
                        error(chown_cmd);
                    }
                    else if(ptr->fsize   == 0)
                    {
                        error("   rm");
                    }
                    if(set->verbosity > 1)
                    {
                        error(NCO);
                    }
                    error(" %s\n",ptr->path);
                    if(set->output)
                    {
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
    if(set->searchdup == 0)
    {
        nuint_t i = 0;
        /* rmlint was originally supposed to find duplicates only
           So we have to free list that whould have been used for
           dup search before dieing */
        for(; i < spelen; i++)
        {
            list_clear(fglist[i].grp_stp);
        }
        if(fglist)
        {
            free(fglist);
        }
        die(0);
    }
    info("\nNow attempting to find duplicates. This may take a while...\n");
    info("Now removing files with unique sizes from list...");
    info(""YEL"%ld item(s) less"NCO" in list.",rem_counter);
    if(path_doubles)
    {
        info(" (removed "YEL"%ld pathzombie(s)"NCO")", path_doubles);
    }
    info(NCO"\nNow removing "GRE"%ld"NCO" empty files / bad links / junk names from list...\n"NCO, emptylist.len);
    info("Now sorting groups based on their location on the drive...");
    /* Now make sure groups are sorted by their location on the disk*/
    qsort(fglist, spelen, sizeof(file_group), cmp_grplist_bynodes);
    info(" done. \nNow doing fingerprints and full checksums.%c\n",set->verbosity > 4 ? '.' : '\n');
    db_done = true;
    error("%s Duplicate(s):",(set->verbosity > 1) ? YEL"#"NCO : "#");
    /* Groups are splitted, now give it to the scheduler
     * The scheduler will do another filterstep, build checkusm
     * and compare 'em. The result is printed afterwards */
    start_scheduler(fglist, spelen);
    if(get_dupcounter() == 0)
    {
        error("\r                    ");
    }
    else
    {
        error("\n");
    }
    /* Gather the total size of removeable data */
    for(ii=0; ii < spelen; ii++)
    {
        if(fglist[ii].grp_stp != NULL)
        {
            lint += fglist[ii].size;
        }
    }
    /* now process the ouptput we gonna print */
    size_to_human_readable(lint, lintbuf, 127 /* bufsize */);
    size_to_human_readable(emptylist.size, suspbuf, 127);
    /* Now announce */
    warning("\n"RED"=> "NCO"In total "RED"%llu"NCO" files, whereof "RED"%llu"NCO" are duplicate(s)",get_totalfiles(), get_dupcounter());
    suspicious = emptylist.len + dbase_ctr;
    if(suspicious > 1)
    {
        warning(RED"\n=> %llu"NCO" other suspicious items found ["GRE"%s"NCO"]",emptylist.len + dbase_ctr,suspbuf);
    }
    warning("\n");
    if(!iAbort)
    {
        warning(RED"=> "NCO"Totally "GRE" %s "NCO" [%llu Bytes] can be removed.\n", lintbuf, lint);
    }
    if((set->mode == 1 || set->mode == 2) && get_dupcounter())
    {
        warning(RED"=> "NCO"Nothing removed yet!\n");
    }
    warning("\n");
    if(set->verbosity == 6)
    {
        info("Now calculation finished.. now writing end of log...\n");
        info(RED"=> "NCO"In total "RED"%llu"NCO" files, whereof "RED"%llu"NCO" are duplicate(s)\n",get_totalfiles(), get_dupcounter());
        if(!iAbort)
        {
            info(RED"=> "NCO"In total "GRE" %s "NCO" ["BLU"%llu"NCO" Bytes] can be removed without dataloss.\n", lintbuf, lint);
        }
    }
    if(get_logstream() == NULL && set->output)
    {
        error(RED"\nERROR: "NCO);
        fflush(stdout);
        perror("Unable to write log");
        putchar('\n');
    }
    else if(set->output)
    {
        warning("A log has been written to "BLU"%s.log"NCO".\n", set->output);
        warning("A ready to use shellscript to "BLU"%s.sh"NCO".\n", set->output);
    }
    /* End of actual program. Now do some finalizing */
    if(fglist)
    {
        free(fglist);
    }
}
