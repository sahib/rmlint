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

#include <stdio.h>
#include <string.h>
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

nuint_t dircount = 0;
bool iAbort   = false,
     dir_done = false,
     db_done  = false;

/* Counter for additional lint (!= duplicates) (lazy) */
nuint_t addlint = 0;

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
        error(RED"FATAL: "NCO"Aborting due to internal error.\n");
        die(-1);
    case SIGSEGV :
        error(RED"FATAL: "NCO"o hai. I haz segfault. Can I haz backtrace? Kthxbai.\n");
        die(-1);
    }
}

/* Cheap function to check if c is a char in str */
static int junkinstr(const char *str)
{
    int i = 0, j = 0;
    if(set.junk_chars == NULL)
    {
        return 0;
    }

    for(; set.junk_chars[i]; i++)
    {
        for(j=0; str[j]; j++)
        {
            if(str[j] == set.junk_chars[i])
            {
                return true;
            }
        }
    }
    return 0;
}


/* A simply method to test if a file is non stripped binary.


 */

static int check_binary_to_be_stripped(const char *path)
{
    FILE *pipe = NULL;
    int bytes = 0;
    char dummy_buf = 0,
         *cmd = NULL;

    if(path == NULL)
    {
        return 0;
    }

    cmd = strdup_printf("file '%s' | grep 'not stripped'", path);

    pipe = popen(cmd,"r");
    if(pipe == NULL)
    {
        return 0;
    }

    bytes = fread(&dummy_buf, sizeof(char), 1, pipe);
    pclose(pipe);

    if(bytes)
    {
        return 1;
    }
    return 0;
}



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
        string = basename(input);

        if(!set.casematch)
        {
            flags |= REG_ICASE;
        }

        if(regcomp(&re, pattern, flags))
        {
            return 0;
        }

        if( (status = regexec(&re, string, (size_t)0, NULL, 0)) != 0)
        {
            if(status != REG_NOMATCH)
            {
                char err_buff[100];
                regerror(status, &re, err_buff, 100);
                warning(YEL"WARN: "NCO" Invalid regex pattern: '%s'\n", err_buff);
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
 *
 *
 * Appendum: rmlint uses the inode to sort the contents before doing any I/O to speed up things.
 * 			 This is nevertheless limited to Unix filesystems like ext*, reiserfs.. (might work in MacOSX - don't know)
 * 			 If someone likes to port this to Windows he would to replace the inode number by the MFT entry point, or simply disable it
 * 			 I personally don't have a win comp and won't port it, as I don't found many users knowing what that black window with white
 *           lines can do ;-)
 */
static int eval_file(const char *path, const struct stat *ptr, int flag, struct FTW *ftwbuf)
{

    if(set.depth && ftwbuf->level > set.depth)
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
            if(!strcmp(set.paths[get_cpindex()], path))
            {
                dir_done = true;
                return FTW_CONTINUE;
            }
        }
        if(flag == FTW_F)
        {

            if(regfilter(path, set.fpattern))
            {
                return FTW_CONTINUE;
            }
            if(junkinstr(basename(path)))
            {
                list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_JNK_FILENAME);
                addlint += ptr->st_dev;
                return FTW_CONTINUE;
            }
            if(set.nonstripped)
            {
                if(check_binary_to_be_stripped(path))
                {
                    list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_NBIN);
                    addlint += ptr->st_dev;
                }
            }
            if(set.oldtmpdata)
            {
                size_t len = strlen(path);
                if(path[len-1] == '~')
                {
                    char *cpy = strndup(path,len-1);
                    struct stat stat_buf;

                    if(!stat(cpy, &stat_buf))
                    {
                        if(ABS(stat_buf.st_mtime - ptr->st_mtime) > set.oldtmpdata)
                        {
                            list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_OTMP);
                            addlint += ptr->st_dev;
                        }
                        if(cpy)
                        {
                            free(cpy);
                        }
                        return FTW_CONTINUE;
                    }
                    if(cpy)
                    {
                        free(cpy);
                    }
                }
            }
            /* Check this to be a valid file and NOT a blockfile (reading /dev/null does funny things) */
            if(flag == FTW_F && ptr->st_rdev == 0)
            {
                if(!access(path,R_OK))
                {
                    if(set.ignore_hidden)
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

            if(regfilter(path, set.dpattern) && dir_done)
            {
                return FTW_SKIP_SUBTREE;
            }
            if(junkinstr(basename(path)))
            {
                list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_JNK_DIRNAME);
                return FTW_SKIP_SUBTREE;
            }
            if(set.findemptydirs)
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
                    if(dir_counter == 2 &&
                            dir_p == NULL
                      )
                    {
                        list_append(path, 0,ptr->st_dev,ptr->st_ino,TYPE_EDIR);
                        return FTW_SKIP_SUBTREE;
                    }
                }
            }

            if(set.ignore_hidden && path)
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

/* This calls basically nftw() and sets some options */
int recurse_dir(const char *path)
{
    /* Set options */
    int ret = 0;
    int flags = FTW_ACTIONRETVAL;
    if(!set.followlinks)
    {
        flags |= FTW_PHYS;
    }

    if(USE_DEPTH_FIRST)
    {
        flags |= FTW_DEPTH;
    }

    if(set.samepart)
    {
        flags |= FTW_MOUNT;
    }

    /* Handle SIGINT */
    signal(SIGINT, interrupt);

    /* (Re)-Init */
    dir_done = false;

    /* Start recurse */
    if((ret = nftw(path, eval_file, _XOPEN_SOURCE, flags)))
    {
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


/* If we have more than one path, several lint_ts  *
 *  may point to the same (physically same file.  *
 *  This woud result in false positves - Kick'em  */
static nuint_t rm_double_paths(file_group *fp)
{
    lint_t *b = fp->grp_stp;
    nuint_t c = 0;

    if(b)
    {
        while(b->next)
        {
            if( (b->node == b->next->node) &&
                    (b->dev  == b->next->dev )  )
            {
                lint_t *tmp = b;
                fp->size -= b->fsize;
                fp->len--;

                b = list_remove(b);

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

/* Sort criteria for sorting by dev and inode */
static long cmp_nd(lint_t *a, lint_t *b)
{
    return ((long)(a->node) - (long)(b->node));
}

/* Compares the "fp" array of the lint_t a and b */
static int cmp_fingerprints(lint_t *a,lint_t *b)
{
    int i,j;
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

    for(i=0; i<BYTE_MIDDLE_SIZE; i++)
    {
        if(a->bim[i] != b->bim[i])
        {
            return 0;
        }
    }

    return 1;
}

/* Performs a fingerprint check on the group fp */
static nuint_t group_filter(file_group *fp)
{
    lint_t *p = fp->grp_stp;
    lint_t *i,*j;

    nuint_t remove_count = 0;
    nuint_t fp_sz;

    if(!fp || fp->grp_stp == NULL)
    {
        return 0;
    }

    /* The size read in to build a fingerprint */
    fp_sz = MD5_FPSIZE_FORM(fp->grp_stp->fsize);

    /* Clamp it to some maximum (4KB) */
    fp_sz = (fp_sz > MD5_FP_MAX_RSZ) ? MD5_FP_MAX_RSZ : fp_sz;

    while(p)
    {
        md5_fingerprint(p, fp_sz);
        p=p->next;
    }

    /* Compare each other */
    i = fp->grp_stp;

    while(i)
    {
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

/* Callback from build_checksums */
static void *cksum_cb(void * vp)
{
    file_group *gp = vp;
    lint_t *file = gp->grp_stp;

    while( file && file != gp->grp_enp )
    {
        md5_file(file);
        file=file->next;
    }

    return NULL;
}

static void build_checksums(file_group *grp)
{
    if(grp == NULL || grp->grp_stp == NULL)
    {
        if(grp->grp_stp==NULL)
        {
            printf("WARN: Empty group received: (end_pointer: %s) len: %ld bsize: %ld\n",(grp->grp_enp) ? grp->grp_enp->path : "null",grp->len, grp->size);
        }
        return;
    }

    if(set.threads == 1 ||  grp->size < (2 * MD5_MTHREAD_SIZE))
    {
        /* Just loop through this group and built the checksum */
        file_group whole_grp;
        whole_grp.grp_stp = grp->grp_stp;
        whole_grp.grp_enp = NULL;
        cksum_cb((void*)&whole_grp);
    }
    else
    {
        nuint_t  sz = 0;
        lint_t * ptr = grp->grp_stp;
        lint_t * lst = grp->grp_stp;

        pthread_t * thread_queue = malloc( (grp->size / MD5_MTHREAD_SIZE + 2) * sizeof(pthread_t));
        int thread_counter = 0, ii = 0;

        while( ptr )
        {
            sz += ptr->fsize;
            if(sz >= MD5_MTHREAD_SIZE || ptr->next == NULL)
            {
                file_group * sub_grp = malloc(sizeof(file_group));
                sub_grp->grp_stp = lst;
                sub_grp->grp_enp = ptr->next;
                sub_grp->size = sz;

                /* Update */
                ptr = ptr->next;
                lst = ptr;
                sz  = 0;

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

        for(ii = 0; ii < thread_counter; ii++)
        {
            if(pthread_join(thread_queue[ii],NULL))
            {
                perror(RED"ERROR: "NCO"pthread_join in build_checksums()");
            }
        }

        if(thread_queue)
        {
            free(thread_queue);
        }
    }
}

/* Callback that actually does the work for ONE group */
static void* sheduler_cb(void *gfp)
{
    file_group *group = gfp;
    if(group == NULL || group->grp_stp == NULL)
    {
        return NULL;
    }

    group_filter(group);


    if(group->grp_stp == NULL || group->len <= 1)
    {
        list_clear(group->grp_stp);
        group->grp_stp = NULL;
        group->grp_enp = NULL;
        return NULL;
    }
    
    build_checksums(group);

    if(findmatches(group))
    {
        list_clear(group->grp_stp);
        die(0);
    }

    list_clear(group->grp_stp);
    return NULL;
}

/* Joins the threads launched by sheduler */
static void sheduler_jointhreads(pthread_t *tt, nuint_t n)
{
    nuint_t ii = 0;
    for(ii=0; ii < n; ii++)
    {
        if(pthread_join(tt[ii],NULL))
        {
            perror(RED"ERROR: "NCO"pthread_join in sheduler()");
        }
    }
}

/* Distributes the groups on the ressources */
static void start_sheduler(file_group *fp, nuint_t nlistlen)
{
    nuint_t ii;
    pthread_t *tt = malloc(sizeof(pthread_t)*(nlistlen+1));

    if(set.threads == 1)
    {
        for(ii = 0; ii < nlistlen && !iAbort; ii++)
        {
            sheduler_cb(&fp[ii]);
        }
    }
    else
    {
        /* Run max set.threads at the same time. */
        int nrun = 0;
        for(ii = 0; ii < nlistlen && !iAbort; ii++)
        {

            if(fp[ii].size >  THREAD_SHEDULER_MTLIMIT)
            {
                if(pthread_create(&tt[nrun],NULL,sheduler_cb,(void*)&fp[ii]))
                {
                    perror(RED"ERROR: "NCO"pthread_create in sheduler()");
                }

                if(nrun >= set.threads-1)
                {
                    sheduler_jointhreads(tt, nrun + 1);
                    nrun = 0;
                    continue;
                }

                nrun++;
            }
            else
            {
                sheduler_cb(&fp[ii]);
            }

        }
        sheduler_jointhreads(tt, nrun);
    }
    if(tt)
    {
        free(tt);
    }
}

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

/* Takes num and converts into some human readable string. 1024 -> 1KB */
static void size_to_human_readable(nuint_t num, char *in, int sz)
{
    if(num < 1024)
    {
        snprintf(in,sz,"%ld B",(unsigned long)num);
    }
    else if(num >= 1024 && num < 1048576.0)
    {
        snprintf(in,sz,"%.2f KB",(float)(num/1024.0));
    }
    else if(num >= 1024*1024 && num < 1073741824)
    {
        snprintf(in,sz,"%.2f MB",(float)(num/1048576.0));
    }
    else
    {
        snprintf(in,sz,"%.2f GB",(float)(num/1073741824.0));
    }
}


static void find_double_bases(lint_t *starting)
{
    lint_t *i = starting;
    lint_t *j = NULL;

    bool phead = true;

    while(i)
    {
        if(i->dupflag)
        {
            bool pr = false;
            j=i->next;
            while(j)
            {
                if(!strcmp(basename(i->path), basename(j->path)) &&
                        i->node != j->node && j->dupflag
                  )
                {
                    lint_t *x = j;
                    char *tmp2 = canonicalize_file_name(j->path);

                    if(phead)
                    {
                        error("\n%s#"NCO" Double basenames: \n", (set.verbosity > 1) ? GRE : NCO);
                        phead = false;
                    }

                    if(!pr)
                    {
                        char * tmp = canonicalize_file_name(i->path);
                        i->dupflag = false;
                        printf("\n%s #Size: %lu\n",tmp,i->fsize);
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
                                x->dupflag = false;
                            }

                            x=x->next;
                        }
                    }
                    j->dupflag = false;
                    printf("%s #Size %lu\n",tmp2,j->fsize);
                    if(tmp2)
                    {
                        free(tmp2);
                    }
                }
                j=j->next;
            }
            if(pr)
            {
            }
        }
        i=i->next;
    }
}

static long cmp_sort_dupID(lint_t* a, lint_t* b)
{
    return ((long)a->dupflag-(long)b->dupflag);
}

void start_processing(lint_t *b)
{
    file_group *fglist = NULL,
                emptylist;

    char lintbuf[128];
    nuint_t ii           = 0,
            lint         = 0,
            spelen       = 0,
            remaining    = 0,
            rem_counter  = 0,
            path_doubles = 0;
            
    if(set.namecluster)
    {
        find_double_bases(b);
        error("\n");
    }
    emptylist.len = 1;
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
                if(get_cpindex() > 1 || set.followlinks)
                {
                    path_doubles += rm_double_paths(&fglist[spelen]);
                }

                spelen++;
            }
            else
            {
                lint_t *ptr;
                bool flag = 42;

                q = list_sort(q,cmp_nd);
                emptylist.grp_stp = q;

                if(get_cpindex() > 1 || set.followlinks)
                {
                    rm_double_paths(&emptylist);
                }

                ptr = emptylist.grp_stp;
                ptr = list_sort(ptr,cmp_sort_dupID);
                emptylist.grp_stp = ptr;
                emptylist.len = 0;
                while(ptr)
                {

                    if(flag != ptr->dupflag)
                    {
                        if(set.verbosity > 1)
                        {
                            error(GRE"\n#"NCO);
                        }
                        else
                        {
                            error("\n#");
                        }
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
                        else if(ptr->fsize   == 0)
                        {
                            error(" Empty file(s): \n");
                        }
                        flag = ptr->dupflag;
                    }

                    if(set.verbosity > 1)
                    {
                        error(GRE);
                    }

                    if(ptr->dupflag == TYPE_BLNK)
                    {
                        error("   rm -rf");
                    }
                    else if(ptr->dupflag == TYPE_OTMP)
                    {
                        error("   rm -rf");
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
                        error("   strip -s");
                    }
                    else if(ptr->fsize   == 0)
                    {
                        error("   rm");
                    }

                    if(set.verbosity > 1)
                    {
                        error(NCO);
                    }

                    error(" %s\n",ptr->path);

                    if(set.output)
                    {
                        write_to_log(ptr, false);
                    }

                    emptylist.size += ptr->fsize;
                    ptr = list_remove(ptr);
                    emptylist.len++;
                }
            }
        }
    }

    error("\n");

    if(set.searchdup == 0)
    {
        int i = 0;

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
    info("Now removing files with unique sizes from list.. ");
    info(""YEL"%ld elem%c less"NCO" in list.",rem_counter,(rem_counter > 1) ? 's' : ' ');
    if(path_doubles)
    {
        info(" (removed "YEL"%ld pathzombies"NCO")", path_doubles);
    }
    info("\nBy ignoring "YEL"%ld empty files "NCO"/"YEL" bad links "NCO"/"YEL" junk names"NCO", list was split in %ld parts.\n", emptylist.len, spelen);
    info("Now sorting groups based on their location on the drive... ");

    /* Now make sure groups are sorted by their location on the disk*/
    qsort(fglist, spelen, sizeof(file_group), cmp_grplist_bynodes);

    info(" done. \nNow doing fingerprints and full checksums:\n\n");
    db_done = true;

    error("%s Duplicate(s):\n",(set.verbosity > 1) ? GRE"#"NCO : "#");
    /* Groups are splitted, now give it to the sheduler */
    /* The sheduler will do another filterstep, build checkusm
     *  and compare 'em. The result is printed afterwards */
    start_sheduler(fglist, spelen);

    if(set.mode == 5 && set.output)
    {
        if(get_logstream())
        {
            fprintf(get_logstream(), SCRIPT_LAST);
            fprintf(get_logstream(), "\n# End of script. Have a nice day.");
        }
    }

    /* Gather the total size of removeable data */
    for(ii=0; ii < spelen; ii++)
    {
        if(fglist[ii].grp_stp != NULL)
        {
            lint += fglist[ii].size - ((fglist[ii].len > 0) ? (fglist[ii].size / fglist[ii].len) : 0);
            remaining += (fglist[ii].len > 1) ? fglist[ii].len - 1 : 0;
        }
    }

    size_to_human_readable(lint+emptylist.size, lintbuf, 127);
    warning("\n"RED"=> "NCO"In total "RED"%ld"NCO
            " files (whereof %d are duplicates) found%s\n",
            remaining + emptylist.len, get_dupcounter(),
            (set.mode == 1 || set.mode == 2) ? ". (Nothing removed yet!)" : ".");

    warning(RED"=> "NCO"Totally "GRE" ~%s "NCO" [%ld Bytes] can be removed.\n\n", lintbuf, lint + emptylist.size);

    if(get_logstream() == NULL)
    {
        error(RED"\nERROR: "NCO);
        fflush(stdout);
        perror("Unable to write log");
        putchar('\n');
    }
    else if(set.output)
    {
        warning("A log has been written to "BLU"%s.log"NCO".\n", set.output);
        warning("A ready to use shellscript to "BLU"%s.sh"NCO".\n", set.output);
    }

    /* End of actual program. Now do some file handling / frees */
    if(fglist)
    {
        free(fglist);
    }
}
