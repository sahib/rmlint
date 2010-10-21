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

int pkg_sz; 
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
	if(path && ptr->st_size != 0) 
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



int recurse_dir(const char *path)
{
  /* Set options */
  int flags = FTW_ACTIONRETVAL; 
  if(!set.followlinks) 
	flags |= FTW_PHYS;
	
  if(set.samepart)
	flags |= FTW_MOUNT;

  /* Handle SIGINT */
  signal(SIGINT, interrupt); 

  /* Start recurse */ 
  if( nftw(path, eval_file, _XOPEN_SOURCE, flags) == -1)
  {
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

	if(b)
	{
		while(b->next)
		{	
				if( (b->node == b->next->node) &&
				    (b->dev  == b->next->dev )  ) 
				    {
						iFile *tmp = b;
						b = list_remove(b); 
						
						if(tmp == fp->grp_stp) 
							fp->grp_stp = b; 
						if(tmp == fp->grp_enp) 
							fp->grp_enp = b; 
						
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

/* Sort criteria */
static int cmp_nd(iFile *a, iFile *b) 
{
	if(a->dev == b->dev) return a->node - b->node;  
	else                 return a->dev  - b->dev; 
}

/* Compares the "fp" array of the iFiles a and b */ 
static int cmp_fingerprints(iFile *a,iFile *b) 
{
	int i,j; 
	
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

/* Performs a fingerprint check on the group fp */
static void *group_filter(file_group *fp) 
{
	iFile *p = fp->grp_stp; 
	iFile *i,*j; 
	
	while(p) { 
		md5_fingerprint(p); 
		p=p->next; 
	}
	
	/* Compare each other */
	i = fp->grp_stp; 
	while(i)
	{
		if(i->filter)
		{
			j = i;
			while(j)
			{
				if(i==j)
				{
					if(cmp_fingerprints(i,j))
					{
						i->filter = false;
						j->filter = false;  
					}
				}
				j=j->next; 
			}
		}
		if(i->filter == false)
		{
			i=i->next; 
		}
		else
		{	
			/* Kick that elem */
			iFile *tmp = i;
			i=list_remove(i); 
			
			/* Update start&end */
			if(tmp == fp->grp_stp) 
				fp->grp_stp = i; 
			if(tmp == fp->grp_enp)
				fp->grp_enp = i;
				
		}
	}
	
	return NULL;
}

static void* sheduler_cb(void * group_ptr)
{
	file_group *group = group_ptr; 
	group_filter(group);
	build_checksums(group->grp_stp);
	findmatches(group);
}

static void start_sheduler(file_group *fp, uint32 nlistlen)
{
	uint32 ii, tot_sz = 0;
	pthread_t tt[nlistlen]; 
	  
	for(ii=0; ii < nlistlen; ii++) 
	{
		sheduler_cb(&fp[ii]); 
		/*
		pthread_create(tt[ii],NULL,sheduler_cb,(void*)&fp[ii]); 
		*/
	}
	/*
	for(ii=0; ii > nlistlen; ii++)
		pthread_join(tt[ii],NULL);
	*/
}

iFile* prefilter_(iFile *b)
{
	file_group *fglist = NULL;
	
	uint32 spelen = 0; 
	uint32 ii = 0;
	
	while(b)
	{
		iFile *q = b;
		iFile *prev = NULL; 
		uint32 islesz = 0;
	
		do 
		{
			prev = b; 
			b = b->next; 
			islesz++;
			
		} while(b && q->fsize == b->fsize);
		
		if(islesz == 1)
		{
			/* This is only a single element        */ 
			/* We can remove it without feelind bad */
			q = list_remove(q);
			if(b) b=q; 
		}
		else
		{
			/* Mark this isle as 'sublist' */
			prev->next = NULL; 
			
			fglist = realloc(fglist, (spelen+1) * sizeof(file_group));
			 
			fglist[spelen].grp_stp = q; 
			fglist[spelen].grp_enp = prev; 
			fglist[spelen].grp_sz  = islesz; 
	
			/* Sort by inode (speeds up IO on normal HDs [not SSDs]) */
			list_sort(fglist[spelen].grp_stp,cmp_nd); 	
			
			if(get_cpindex() > 1 || set.followlinks)	
				rm_double_paths(&fglist[spelen]);
			
			spelen++; 
		}
	}
	
	/* Grups are splitted, now give it to the sheduler */ 
	/* The sheduler will do another filterstep, build checkusm 
	 *  and compare 'em. The result is printed afterwards */ 
	start_sheduler(fglist, spelen); 
	
	/* Clue list together again (for convinience) */
	for(ii=0; ii < spelen; ii++)  
	{			 
		if(ii + 1 != spelen && 0)
		{
			fglist[ii].grp_enp->next   = fglist[ii+1].grp_stp;
			fglist[ii+1].grp_stp->last = fglist[ii].grp_enp; 			
		} 
	}
	print(list_begin());
}


static void *cksum_cb(void * vp)
{ 
	iFile *file = (iFile *)vp; 
	int i = 0; 

	for(; i < pkg_sz && file != NULL; i++)
	{
		md5_file(file);
		file=file->next; 
	}
	return NULL;
}

void build_checksums(iFile *begin)
{
    iFile *ptr = begin; 

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
		for(c=0;c < d; c++)
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
