/**
*  This file is part of autovac.
*
*  autovac is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  autovac is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with autovac.  If not, see <http://www.gnu.org/licenses/>.
*
** Author: Christopher Pahl <sahib@online.de>:
** Hosted at the time of writing (Mo 30. Aug 14:02:22 CEST 2010): 
*  on http://github.com/sahib/autovac
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
#include "mt.h"

UINT4 dircount = 0;
UINT4 elems = 0;  
short iinterrupt = 0;
short tint = 0;  
pthread_attr_t p_attr; 

/**  
 * Callbock from signal() 
 * Counts number of CTRL-Cs 
 **/

static void interrupt(int p) 
{
	 /** This was connected with SIGINT (CTRL-C) in countfiles() **/ 
	 switch(tint)
	 {
		 case 0: warning(RED".. "GRE"aborting... "RED"@ \n"NCO); break; 
		 case 1: warning(RED":: "GRE"Press "RED"CTRL-C"GRE" to abort immediately.\n"NCO); break;
		 case 2: warning(RED";; "GRE"Doing a hard abort. "RED":(\n"NCO);  
				 die(1);
		 break;
		 default: error("Yes, Sir. I promise to hurry up - although i wonder a bit how you came to read this.\nDo you get blind right now by reading this \"great\" code perhaps?"NCO); 
	 }
	 iinterrupt++; 
	 tint++;  
}


/** 
 * grep the string "string" to see if it contains the pattern.
 * Will return 0 if yes, 1 otherwise.
 */ 


static int regfilter(const char* input, const char *pattern)
{
  int status;
  int flags = REG_EXTENDED|REG_NOSUB; 
  regex_t re;
  
  const char *string = basename(input);
  
  if(!pattern||!string) return 0; 
  
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
	}	
	if(flag == FTW_F)
	{
		if(!regfilter(path, set.fpattern))
		{
			dircount++; 
			list_append(path, ptr->st_size);
		}
		return 0; 
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

/**
 * Takes 2 files and make a fast check if both _seem_ to be equal (does not build full checksums) 
 * If the size of both files is not the same 0 is returned. 
 * If sizes are equal a fingerprint of the first and last 
 **/

static int treshold(iFile *a, iFile *b)
{
	int i=0,j=0;  
	
	if(!(a&&b)) 
		return 0; 
	
	/* Double check size so we still can have a progress bar */
	if(a->fsize !=  b->fsize)
		return 0; 
	
#if USE_MT_FINGERPRINTS == 1 

	if(set.threads != 1)
	{
		void *status;
		pthread_t ts[2];
		/* Start two threads to calc checksum in parallel */ 
					
		if(!a->fpc) {
			pthread_create(&ts[0],&p_attr,fpm,(void*)a); 
			a->fpc++; 
		}
		if(!b->fpc) { 
			pthread_create(&ts[1],&p_attr,fpm,(void*)b);
			b->fpc++; 
		}
		
		pthread_join(ts[0],&status);
		pthread_join(ts[1],&status);
	}
	else
	{
#endif 
		if(!a->fpc) { 
			a->fpc++; 
			a->fpc = md5_fingerprint(a);
		}
		if(!b->fpc) { 
			b->fpc++; 
			b->fpc = md5_fingerprint(b);
		}
#if USE_MT_FINGERPRINTS == 1 
	}
#endif
	
	for(; i < 2; i++) 
		for(j=0; j < MD5_LEN; j++)  
			if(a->fp[i][j] != b->fp[i][j])
					return  0; 
			
    return 1; 	
}


void prefilter(void)
{
	iFile *b = list_begin();
	UINT4 c =  0;
	UINT4 l = list_getlen(); 

	if(b == NULL) die(0);

	while(b)
	{	
		if(b->last && b->next)
		{
			if(b->last->fsize != b->fsize && b->next->fsize != b->fsize)
			{
				info("Filtering by Size... "BLU"["NCO"%ld"BLU"]\r"NCO,c++);
				fflush(stdout);
				b = list_remove(b);
				continue;
			}
		}
		b=b->next; 
	}

	info(RED" => "NCO"Prefiltered %ld of %ld files.\n",c,l);
}


UINT4 filterlist(void)
{
	UINT4 i = 0;
	bool s = false; 
	
	iFile *ptr_i;
	iFile *ptr_j; 
	
	/* Prefilter by size. */
	prefilter();
	
	/* list_begin() may change during prefilter() */
	ptr_i = list_begin();
	ptr_j = ptr_i; 
	
#if USE_MT_FINGERPRINTS == 1  
	pthread_attr_init(&p_attr);
	pthread_attr_setdetachstate(&p_attr, PTHREAD_CREATE_JOINABLE);
#endif 
	
	while(ptr_i) 
	{
		
		if(i%STATUS_UPDATE_INTERVAL==0)
		{
			/* Do some (inaccurate) progress bar */
			float perc = (float)i / ((float)list_getlen()-1) * 100.0f; 
			info("Filtering by fingerprint... "GRE"%2.2f"BLU"%% ["NCO"%ld"BLU"|"NCO"%ld"BLU"]   \r"NCO, perc >= 100.0f ? 100.0f : perc,i,list_getlen());  
			fflush(stdout);
		}
		i++; 
		
		/* Start from.. the start. */
		ptr_j = list_begin();
		
		s = true; 
		while(ptr_j)
		{
			if(ptr_j == ptr_i)
			{
				ptr_j = ptr_j->next;
				continue;
			}
			if(iinterrupt) 
			{
				/* Interrupted by user */
				warning(RED"\nAborting at #%ld.. One second please.\n",i);

				/* Reset the iinterrupt flag, so pushchanges will continue */
				iinterrupt = 0; 
				
				/* Mark this as "end" of the investigated files; 42.. Any questions? :)*/
				ptr_i->fpc = 42;  
				return list_getlen(); 
			}
			if(treshold(ptr_i, ptr_j))
			{
				/* Not passed test - forget ptr_i */
				s = false;
				break;  
			}
			ptr_j = ptr_j->next; 
		}
		if(s)
		{
			ptr_i = list_remove(ptr_i);
			i--;
		}
		else 
		{
			ptr_i = ptr_i->next;
		}
	}
	
#if USE_MT_FINGERPRINTS == 1 
	pthread_attr_destroy(&p_attr);
#endif 

	return list_getlen(); 
}

void pushchanges(void)
{
	UINT4 i = 0;
	UINT4 c = 0; 
	float perc = 0; 
	iFile *ptr = list_begin(); 
		
	if(ptr == NULL) 
	{
		error(YEL" => "NCO"No files in the list after filtering..\n");
		error(YEL" => "NCO"This means that no duplicates were found. Exiting!\n"); 
		die(42);
	}
		
	while(ptr)
	{
		if(c%STATUS_UPDATE_INTERVAL==0)
		{
			/* Make the user happy with some progress */
			perc = (((float)c) / ((float)list_getlen())) * 100.0f; 
			info("Building database.. "GRE"%2.1f"BLU"%% "RED"["NCO"%ld"RED"/"NCO"%ld"RED"]"NCO" - [ %s ]  - ["RED"%ld"NCO" Bytes]      \r"NCO, perc,
							c, list_getlen(), (i%4 == 0) ? RED"0"NCO : (i%4 == 1) ? BLU"O"NCO : (i%4 == 2) ? YEL"o"NCO : GRE"."NCO , ptr->fsize);
			fflush(stdout); 
		}
		
		c++;
		
		if(set.threads != 1)
		{
			/* Fill the threading pool with data
			 * If the pool is full then checksums are build 
			 * on $tt threads in parallel */
			fillpool(ptr,c);
			
		}
		else
		{	
			/* If only 1 thread is specified: 
			 * Run without the overhead of mt.c 
			 * and call the routines directly */
			MD5_CTX con;
			md5_file(ptr->path, &con);
			memcpy(ptr->md5_digest,con.digest, MD5_LEN); 
		}
		
		/* Neeeeext! */    
		ptr = ptr->next;
		
		/* The user told us that we have to hate him now. */
		if(iinterrupt)
		{
			 ptr->fpc = 42;
			 break;
		}
	}
	
	/* Make sure you get 100.0% :-) */
	info("Building database.. %2.1f%% [%ld/%ld] - [ %s ]\r", 100.0f, c, list_getlen(), (i%4 == 0) ? "0" : (i%4 == 1) ? "O" : (i%4 == 2) ? "o" : "." );
	fflush(stdout); 
}

int countfiles(const char *path)
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
