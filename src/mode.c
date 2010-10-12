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
*  http://github.com/sahib/rmlint
*   
**/

#define _GNU_SOURCE

#include <stdlib.h> 
#include <stdio.h>
#include <string.h> 
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#include "rmlint.h"
#include "mode.h"
#include "md5.h"
#include "defs.h"
#include "list.h"

#define READSIZE 8192

uint32 duplicates = 0;
uint32 lintsize = 0; 

/* Make the stream "public" */
FILE *script_out = NULL; 


FILE *get_logstream(void)  {  return script_out;  }

static void remfile(const char *path)
{
	if(path) 
	{
		if(unlink(path))
			warning("remove failed with %s\n", strerror(errno)); 
	}
}

/** This is only for extremely paranoid people **/
static int paranoid(const char *p1, const char *p2) 
{
	uint32 b1=0,b2=0; 
	FILE *f1,*f2; 
	
	char c1[READSIZE],c2[READSIZE]; 
	
	f1 = fopen(p1,"rb"); 
	f2 = fopen(p2,"rb"); 
	
	if(p1==NULL||p2==NULL) return 0; 
	
	while((b1 = fread(c1,1,READSIZE,f1))&&(b2 = fread(c2,1,READSIZE,f2)))
	{
		int i = 0; 
	
		if(b1!=b2) return 0; 
		for(; i < b1; i++)
		{
				if(c1[i] != c2[i])
				{
					fclose(f1); 
					fclose(f2); 
					return 0; 
				}
		}
	}

	/* If byte by byte was succesful print a blue "x" */ 
	warning(BLU"x "NCO);
	fclose(f1);
	fclose(f2); 
	return 1; 
}


static int cmp_f(unsigned char *a, unsigned char *b)
{
	int i = 0; 
	
	for(; i < MD5_LEN; i++)
	{
		if(a[i] != b[i])
			return 1; 
	}	 
	return 0; 
}

static void print_askhelp(void)
{
	error(  RED"\n\nk"YEL" - keep file; \n"
			RED"d"YEL" - delete file; \n"
			RED"l"YEL" - replace with link; \n"
			RED"q"YEL" - Quit.\n"
			RED"h"YEL" - Help.\n"
			NCO ); 
}

static void handle_item(const char *path, const char *orig, FILE *script_out) 
{	
	if(script_out) 
	{
		char *fpath = canonicalize_file_name(path);
		char *forig = canonicalize_file_name(orig);
		
		if(!fpath||!forig) perror("Full path");
		
		if(set.cmd) 
		{
			  size_t len = strlen(path)+strlen(set.cmd)+strlen(orig)+1; 
			  char *cmd_buff = alloca(len);
			  snprintf(cmd_buff,len,set.cmd,fpath,forig);
			
			  fprintf(script_out, cmd_buff);
			  fprintf(script_out, "\n");
		}
		else
		{
				fprintf(script_out,"rm \"%s\" # == \"%s\"\n", fpath, forig);
		}
		
		if(fpath) free(fpath);
		if(forig) free(forig);	
	}
	
	/* What set.mode are we in? */
	switch(set.mode)
	{		
		
		case 1: break; 		
		case 2: 
		{
			/* Ask the user what to do */
			char sel, block = 0; 					
		
			print_askhelp();
					
			do
			{
				error(RED"#[%ld] \""YEL"%s"RED"\""GRE" == "RED"\""YEL"%s"RED"\"\n"BLU"Remove %s?\n"BLU"=> "NCO, duplicates+1,orig, path, path); 
				do {scanf("%c",&sel);} while ( getchar() != '\n' );
				
							switch(sel) 
							{
								case 'k':  
										  block = 0;
										  break;
										   
								case 'd':  
										  remfile(path); block = 0;  
										  break;
								
								case 'l':  
										  remfile(path);
										  fprintf(stdout,"link \"%s\"\t-> \"%s\"\n", path, orig);	
										  block = 0;
										  break;
										
								case 'q': die(-42);
								
								case 'h': print_askhelp();
										  block = 1;
										  break;
										
								default : warning("Invalid input."NCO); 
										  block = 1;  
										  break; 
							}
							
			} while(block);
			
		} break; 
		
		case 3: 
		{			
			/* Just remove it */
			warning(RED" rm "NCO"\"%s\"\n", path);
			remfile(path);
		} break; 
			
		case 4: 
		{
			/* Replace the file with a neat symlink */
			error(GRE"link "NCO"\"%s\""RED" -> "NCO"\"%s\"\n", orig, path);
			if(unlink(path))
				error("remove failed with %s\n", strerror(errno)); 
						
			if(link(orig,path))
				error("symlink() failed with \"%s\"\n", strerror(errno)); 	
		} break; 
		
		case 5:
		{
			int ret; 
			size_t len = strlen(path)+strlen(set.cmd)+strlen(orig)+1; 
			char *cmd_buff = alloca(len); 
			
			fprintf(stderr,NCO);
			snprintf(cmd_buff,len,set.cmd,path,orig);
			ret = system(cmd_buff);
			if(ret == -1)
			{
				perror("System()");
			}
			
			if (WIFSIGNALED(ret) &&
              (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT))
                  return;
                  
		} break; 
		
		default: error(RED"Invalid set.mode. This is a program error. :("NCO);
	}								
}

static void size_to_human_readable(uint32 num, char *in) 
{	
		if(num < 1024) 
		{ 
			snprintf(in,256,"%ld B",num); 
		}
		else if(num >= 1024 && num < 1024*1024) 
		{  
			snprintf(in,256,"%.2f KB",(float)(num/1024.0)); 
		}
		else if(num >= 1024*1024 && num < 1024*1024*1024) 
		{ 
			snprintf(in,256,"%.2f MB",(float)(num/1024.0/1024.0)); 
		}
		else 
		{ 
			snprintf(in,256,"%.2f GB",(float)(num/1024.0/1024.0/1024.0)); 
		}
}


void init_filehandler(void)
{
		script_out = fopen(SCRIPT_NAME, "w");
		if(script_out)
		{
			char *cwd = getcwd(NULL,0);
			
			/* Make the file executable */
			if(fchmod(fileno(script_out), S_IRUSR|S_IWUSR|S_IXUSR) == -1)
				perror("Warning, chmod failed on "SCRIPT_NAME);
				
			/* Write a basic header */
			fprintf(script_out,
					"#!/bin/sh\n"
					"#This file was autowritten by 'rmlint'\n"
					"#If you removed these files already you can use it as a log\n"
					"# If not you can execute this script. Have a nice day.\n"
					"# rmlint was executed from: %s\n\n",cwd);
					
			if(cwd) free(cwd); 
		}
		else
		{
			perror(NULL);
		}
}



void findmatches_(void) 
{
	iFile *i = list_begin();

	uint32 finds = 0; 
	char lintbuf[256]; 

	warning(NCO);
	
	while(i)
	{
		bool p = true; 
		iFile *j = list_begin();
		 
		while(j)
		{
			if(j != i && j->dupflag != 1)
			{
					if( (!cmp_f(i->md5_digest, j->md5_digest))  &&     /* Same checksum?  */
					    (i->fsize == j->fsize)	&&					   /* Same size? 	  */ 
						((set.paranoid)?paranoid(i->path,j->path):1)   /* If we're bothering with paranoid users - Take the gatling! */ 
					)
					{
						if(set.mode == 1)
						{
							if(p == true) 
							{
								error("= %s\n",i->path); 
								p=false; 
							}
							error("X %s\n",j->path); 
						}
						
						lintsize += j->fsize;
						
						handle_item(j->path, i->path, script_out);
						
						j->dupflag = true;
						i->dupflag = true; 
						finds++; 
					}	
			}
			j = j->next; 

		}
		if(!p) error("\n"); 
		i=i->next; 
	}
	
	/* Make sure only dups are left in the list. */
    i = list_begin(); 
    if(i==NULL) puts("huh?");
    
    
    while(i) 
    {
		if(i->dupflag == false)
		{
			puts("lala");
			i=list_remove(i);
		}
		else
		{
			puts(i->path);
			i=i->next; 
		}
	}

	size_to_human_readable(lintsize,lintbuf);
	warning("\nWrote result to "BLU"./"SCRIPT_NAME NCO" -- ");
	warning("In total "RED"%ld"NCO" duplicate%sfound. ("GRE"%s"NCO")\n", finds, (finds) ? " " : "s ",lintbuf);
	
	if(script_out)
	{
		fclose(script_out);
	}
	
}


void findmatches(void) 
{
 	iFile *ptr = list_begin();
	iFile *sub = NULL; 
	
	uint32 finds = 0; 
	char lintbuf[256]; 
	
	warning(NCO);
	
	while(ptr)
	{
		iFile *i=ptr,*j=ptr;  
		uint32 fs = ptr->fsize;
	 
		sub=ptr;
	 
		if(ptr->filter == 42) 
		{
			break; 
		}
	
		while(ptr && ptr->fsize == fs) 
		{ 
			ptr=ptr->next; 
		}
		
		/* Handle one "group" */			
		while(i && i!=ptr)
		{
			bool p = true; 
			j=sub; 
			
			
			while(j && j!=ptr)
			{
					if(i==j || j->dupflag) 
					{
						j=j->next;
						continue; 
					}
					if( (!cmp_f(i->md5_digest, j->md5_digest))  &&     /* Same checksum?  */
					    (i->fsize == j->fsize)	&&					   /* Same size? 	  */ 
						((set.paranoid)?paranoid(i->path,j->path):1)   /* If we're bothering with paranoid users - Take the gatling! */ 
					)
					{
						if(set.mode == 1)
						{
							if(p == true) 
							{
								error("= %s\n",i->path); 
								p=false; 
							}
							error("X %s\n",j->path); 
						}
						
						lintsize += j->fsize;
						
						handle_item(j->path, i->path, script_out);
						
						j->dupflag = true;
						i->dupflag = true; 
						finds++; 	
					}
					
					j = j->next;
			} 
			i=i->next; 
		
			if(!p)
			{
				error("\n");
			}
		}
	}
	
	size_to_human_readable(lintsize,lintbuf);
	warning("\nWrote result to "BLU"./"SCRIPT_NAME NCO" -- ");
	warning("In total "RED"%ld"NCO" duplicate%sfound. ("GRE"%s"NCO")\n", finds, (finds) ? " " : "s ",lintbuf);
	
	if(script_out)
	{
		fclose(script_out);
	}
}
