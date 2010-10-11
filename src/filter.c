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

/**  
 * Callbock from signal() 
 * Counts number of CTRL-Cs 
 **/

static void interrupt(int p) 
{
	 /** This was connected with SIGINT (CTRL-C) in recurse_dir() **/ 
	 switch(tint)
	 {
		 case 0: warning(RED".. "GRE"aborting... "RED"@ \n"NCO); break; 
		 case 1: die(1);
	 }
	 iinterrupt++; 
	 tint++;  
}


/** 
 * grep the string "string" to see if it contains the pattern.
 * Will return 0 if yes, 1 otherwise.
 */ 

int regfilter(const char* input, const char *pattern)
{
  int status;
  int flags = REG_EXTENDED|REG_NOSUB; 
  
  
  const char *string = basename(input);
  
  if(!pattern||!string)
  {
	   return 0; 
  }
  else
  {
	  regex_t re;
	  
	  if(!set.casematch) 
		flags |= REG_ICASE; 
	  
	  if(regcomp(&re, pattern, flags)) 
		return 0; 

	  if( (status = regexec(&re, string, (size_t)0, NULL, 0)) != 0)
	  {
		  if(status != REG_NOMATCH)
		  {
			  char err_buff[100];
			  regerror(status, &re, err_buff, 100);
			  warning("Warning: Invalid regex pattern: '%s'\n", err_buff);
		  }
	  }
	  
	  regfree(&re);
	  return (set.invmatch) ? !(status) : (status);
  }
}

/** 
 * Callbock from nftw() 
 * If the file given from nftw by "path": 
 * - is a directory: recurs into it and catch the files there, 
 * 	as long the depth doesnt get bigger than max_depth and contains the pattern  cmp_pattern
 * - a file: Push it back to the list, if it has "cmp_pattern" in it. (if --regex was given) 
 * If the user interrupts, nftw() stops, and the program will build fingerprints.  
 **/ 
 
int eval_file(const char *path, const struct stat *ptr, int flag, struct FTW *ftwbuf)
{
	if(set.depth && ftwbuf->level > set.depth)
	{
		/* Do not recurse in this subdir */
		return FTW_SKIP_SIBLINGS; 
	} 
	if(iinterrupt)
	{
		return FTW_STOP;
	}
	if(flag == FTW_SLN)
	{
		error(RED"Bad symlink: %s\n"NCO,path);
		fprintf(get_logstream(),"rm %s #bad link\n",path); 
	}	
	if(ptr->st_size != 0) 
	{
		if(flag == FTW_F && ptr->st_rdev == 0)
		{
			if(!regfilter(path, set.fpattern))
			{
				dircount++; 
				list_append(path, ptr->st_size,ptr->st_dev,ptr->st_ino, ptr->st_nlink);
			}
			return FTW_CONTINUE; 
		}	
	}
	else
	{
	/*	error(NCO"    Empty file: %s\n",path); */
		fprintf(get_logstream(), "rm %s # empty file\n",path); 
	}
	if(flag == FTW_D)
	{	
		if(regfilter(path,set.dpattern)&& strcmp(path,set.paths[get_cpindex()]) != 0)
		{
			return FTW_SKIP_SUBTREE;
		}
	}
	return FTW_CONTINUE;
}



uint32 rem_double_paths(void)
{
	iFile *b = list_begin();
	iFile *s = NULL;  
	
	uint32 c = 0;
	while(b)
	{	
		s=list_begin(); 
		
		while(s)
		{
			if(s->dev == b->dev && s->node == b->node && b!=s)
			{
				c++; 
				s = list_remove(s); 
			}
			else
			{
				s=s->next; 
			}
		}
		b=b->next; 
	}
	
	return c;
}


void prefilter(void)
{
	iFile *b = list_begin();
	uint32 c =  0;

	iFile *s,*e; 

	if(b == NULL) die(0);
	
	while(b)
	{					
		if(iinterrupt)
		{
			iinterrupt = 0; 
			return; 
		}
	
		if(b->last && b->next)
		{
			if(b->last->fsize != b->fsize && b->next->fsize != b->fsize)
			{
				c++; 
				b = list_remove(b);
				continue;
			}
		}
		b=b->next; 
	}
	
	e=list_end(); 
	if(e->last)
	{
		if(e->fsize != e->last->fsize) 
		{
			e=list_remove(e);
		}
	}	
	
	s=list_begin(); 
	if(s->next) 
	{
		if(s->fsize != s->next->fsize) 
		{
			s=list_remove(s); 
		}
	}
	
	/* If we have more than 2 dirs given, or links may be followd
	 * we should check if the one is a subset of the another
	 * and remove path-doubles */
	if(get_cpindex() > 1 || set.followlinks) 
	{
		uint32 cb; 
		if( (cb=rem_double_paths()) )
			info(RED" => "NCO"Ignoring %ld pathdoubles.\n",cb);
	}
	if(c != 0)
	{
		info(RED" => "NCO"Prefiltered %ld, %ld still in line..\r",c,list_len());
		fflush(stdout);
	}
}


uint32 filter_template(void(*create)(iFile*),  int(*cmp)(iFile*,iFile*), iFile *start, iFile *stop, const char *filter_name)
{
    iFile *ptr = start; 
    iFile *sub = NULL;
    uint32 con = 0;


    while(ptr&&ptr!=stop)
    {
        iFile *i,*j; 
        int isle_sz = 0; 
    
        /* Save a pointer to the start of the isle */
        sub=ptr;

		if(iinterrupt) 
		{
			iinterrupt = 0; 
			sub->filter = 42;
			return 0; 
		}

		if(con % STATUS_UPDATE_INTERVAL == 0 && filter_name) 
		{
			info("Filtering by %s "BLU"["NCO"%ld"BLU"]"NCO"\r", filter_name ,con); 
			fflush(stdout); 
		}

        /* Find the start of the next isle (== end of current isle) (or NULL) */
        while(ptr && ptr->fsize == sub->fsize) 
        {
        
	    isle_sz++; 
            ptr->filter = true;
            (*create)(ptr);
            ptr=ptr->next; 
        }
                
        i=sub;

        while(i&&i!=ptr)
        {

          if(i->filter == false)
          {
              i=i->next;
              continue;
          }
          
          j=sub;
          while(j&&j!=ptr)
          {
              if(i!=j)
              {
                  if( (*cmp)(i,j) )
                  {
                        j->filter=false;
                        i->filter=false;
                        break; 
                  }
              }
              j=j->next;

          }
          if(i->filter)
          {
            i=list_remove(i);
            con++;
          }
          else
          {
            i=i->next; 
          }
        }
    }
    return con; 
}


/* Compares the "fp" array of the iFiles a and b */ 
static int cmp_fingerprints(iFile *a,iFile *b) 
{
	int i,j; 
	if(!(a&&b))
		return 0;
	
	for(i=0;i<2;i++) 
	{ 
		for(j=0;j<MD5_LEN;j++)  
		{
			if(a->fp[i][j] != b->fp[i][j])
			{
				return  0; 
			}
		}
	}
	return 1; 
}


/* Just exists to pass start/stop to threads */
typedef struct
{
	iFile *start;
	iFile *stop;
 
} pt_capsule; 

/* Called by build_fingerprint on own thread */
static void* pt_fingerprint (void* v)
{
	pt_capsule *t = v;  
	filter_template(md5_fingerprint, cmp_fingerprints, t->start, t->stop, NULL); 
	return NULL; 
}
	
uint32 build_fingerprint(void)
{
    /* Use multithreaded fingerprints if not forbidden */
    if(set.threads != 1)
    {
        iFile *p = list_begin();
        iFile *s=p;

        uint32 fs = p->fsize; 
        uint32 ret=0;
        int pacc = 0;
        int t_id = 0;  

        /* The number of isles in a group */
        int pac_sz = list_len()/set.threads;

        pthread_t *thr = malloc(sizeof(pthread_t) * set.threads);

        /*error("Packsize: %d \n",pac_sz); */
        
        while(p)
        {
            if(p->next == NULL) 
            {
                /* Filter from s to NULL (== end) -
                 * this is only one group => do it without pthreads */
                ret += filter_template(md5_fingerprint, cmp_fingerprints, s,NULL,NULL);
                break; 
            }

            /* New group started? */
            if(fs != p->fsize)
            {
                fs=p->fsize;
                if( (pacc++) > pac_sz)
                {
		    pt_capsule cap; 
		    cap.start = s; 
		    cap.stop  = p; 

                    /* Filter from s to p (where p is not inspected) 
                    ret += filter_template(md5_fingerprint, cmp_fingerprints, s,p,NULL);
			*/ 
                    
                    if(pthread_create(&thr[t_id++],NULL, pt_fingerprint, (void*)&cap)) 
			perror("Pthread"); 
                    
                    
                    /* Next islegroup */
                    pacc=0;
                    s = p; 
                }
            }
 
            p=p->next; 
        }

        for(pacc=0; pacc < set.threads; pacc++)
        {
            
        }
        
        if(thr != NULL)
        {
            free(thr);
            thr = NULL; 
        }
        return ret; 
    }
    /* If only one thread is wanted just filter from start to end */
    else
    {
        return filter_template(md5_fingerprint, cmp_fingerprints, list_begin(),NULL,"Fingerprint"); 
    }
}


/** This function is pointless. **/
char blob(uint32 i)
{
	if(!i) return 'x';
	switch(i%12) 
	{
		case 0: return 'O'; 
		case 1: return '0';
		case 2: return 'o'; 
		case 3: return '*';
		case 4: return '|';
		case 5: return ':';
		case 6: return '.';
		case 7: return ' '; 
		case 8: return '-'; 
		case 9: return '|'; 
		case 10:return '^';
		case 11:return '0'; 
		default: return 'x'; 
	}
}

/* Global, Sorry. */
int pkg_sz; 

static void *cksum_cb(void * vp)
{
	iFile *file = (iFile *)vp; 
	int i = 0; 

	for(; i < pkg_sz && file != NULL; i++)
	{
		MD5_CTX con; 
		md5_file(file->path, &con);
		memcpy(file->md5_digest,con.digest, MD5_LEN);
		file=file->next; 
	}
	return NULL;
}



void build_checksums(void)
{
   iFile *ptr = list_begin(); 

    uint32 c=0,d=0; 
    uint32 max_threads; 
    pthread_t *thr = NULL; 

    pkg_sz = (list_len() / set.threads) > 2 ? (list_len() / set.threads) : 2; 
    max_threads = list_len() / pkg_sz + 1 ;
    

	if(ptr == NULL) 
	{
		error(YEL" => "NCO"No files in the list after filtering..\n");
		error(YEL" => "NCO"This means that no duplicates were found. Exiting!\n"); 
		die(0);
	}

	if(set.threads != 1)
	{
		thr = alloca( max_threads *sizeof(pthread_t));
		memset(thr,0,max_threads); 
	}
	while(ptr)
	{	
		ptr->dupflag = 0;	
		if(set.threads != 1)
		{	
			if(c % pkg_sz == 0 || c == 0)
			{
				if(pthread_create(&thr[d++],NULL,cksum_cb,(void*)ptr))
				{
					perror("Pthread");
				}
			}
			c++; 
		}
		else
		{	
			/* If only 1 thread is specified: 
			 * Run without the overhead of mt.c 
			 * and call the routines directly */
			cksum_cb(ptr); 
		}
	
		/*  Neeeeext! */    
		ptr = ptr->next;
		
		/* The user told us that we have to hate him now. */
		if(iinterrupt)
		{
			 ptr->filter = 42;
			 break;
		}
	}

	if(set.threads != 1)
	{
		/* Make sure threads get joined */
		for(c=0;c < max_threads && c < d; c++)
		{
			if(thr[c])
			{
				if(pthread_join(thr[c],NULL)!=0)
				{
					perror("Pthread"); 
				}
			}
		}
	}
}

int recurse_dir(const char *path)
{
  int flags = FTW_ACTIONRETVAL; 
  if(!set.followlinks) 
	flags |= FTW_PHYS;
	
  if(set.samepart)
	flags |= FTW_MOUNT;

  /* Handle SIGINT */
  signal(SIGINT, interrupt); 

  if( nftw(path, eval_file, _XOPEN_SOURCE, flags) == -1)
  {
    warning("nftw() failed with: %s\n", strerror(errno));
    return EXIT_FAILURE;
  } 

  return dircount; 
}
