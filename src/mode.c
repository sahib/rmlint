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
*  http://github.com/sahib/autovac
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


UINT4 duplicates = 0;
UINT4 lintsize = 0; 


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
	UINT4 b1=0,b2=0; 
	FILE *f1,*f2; 
	
	char c1[4096],c2[4096]; 
	
	f1 = fopen(p1,"rb"); 
	f2 = fopen(p2,"rb"); 
	
	if(p1==NULL||p2==NULL) return 0; 
	
	while((b1 = fread(c1,1,4096,f1))&&(b2 = fread(c2,1,4096,f2)))
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
	warning(BLU"x\t"NCO);
	fclose(f1);
	fclose(f2); 
	return 1; 
}

//ToDo: 
static int cmp_inodes(const char *p1, const char *p2)
{
	/* Compare the inodes, so we can sure
	 * it's not the physically same file 
	 * (what would lead to dataloss    */
	struct stat buf1; 
	struct stat buf2; 
	stat(p1,&buf1);
	stat(p2,&buf2); 
	
	if(buf1.st_ino == buf2.st_ino && 
	   buf1.st_dev == buf2.st_dev  )
	{
	   puts("Same inode");
	   return 0; 
	}
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
		case 1:
		
			/* Just list the files */
			warning(GRE"  ls "NCO": \"%s\" \t"RED"=>"NCO"  \"%s\"\n", path, orig);	
		 
		break; 
		
		case 2: 
		{
			/* Ask the user what to do */
			char sel, block = 0; 					
		
			error(  RED"\n\nk"YEL" - keep file; \n"
					RED"d"YEL" - delete file; \n"
					RED"l"YEL" - replace with link; \n"
					RED"q"YEL" - Quit.\n"
					RED"h"YEL" - Help.\n"
					NCO); 
					
			do
			{
				error(RED"#[%ld] \""YEL"%s"RED"\""GRE
						 " == "RED"\""YEL"%s"RED"\"\n"
					  BLU"Remove %s?\n"BLU"=> "NCO, 
					  duplicates+1,orig, path, path); 
							
							
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
										
								case 'q': return;
								
								case 'h': error( RED"\n\nk"YEL" - keep file; \n"
												 RED"d"YEL" - delete file; \n"
												 RED"l"YEL" - replace with link; \n"
												 RED"q"YEL" - Quit.\n"
												 RED"h"YEL" - Help.\n\n"
												 NCO); 
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

static void size_to_human_readable(UINT4 num, char *in) 
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



void findcrap(void)
{
	iFile *ptr = list_begin();
	iFile *sub = ptr->next; 
	UINT4 finds = 0; 
	char lintbuf[256]; 
	 
	FILE *script_out = NULL; 
	  
	info(RED" => "GRE"Almost done!                                                             \r"NCO);
	fflush(stdout); 
	
	info("\n\n Result:\n"RED" --------\n"NCO);
	warning("\n"); 
	
	/* If we're in set.mode 1 we create a file to save the output */
	if(set.mode) 
	{
		script_out = fopen(SCRIPT_NAME, "w");
		if(script_out)
		{
			char *cwd = getcwd(NULL,0);
			if(fchmod(fileno(script_out), S_IRUSR|S_IWUSR|S_IXUSR) == -1)
				perror("Warning, chmod failed on "SCRIPT_NAME);
				
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
		
	/* Go through the rest of the list and find double checksums */
	while(ptr)
	{
		if(ptr->dupflag)
		{
			ptr = ptr->next;
			continue;
		}
	
		
		while(sub) 
		{
			if(ptr == sub || sub->dupflag)
			{
				sub = sub->next;
				continue; 
			}
			if(!cmp_f(ptr->md5_digest, sub->md5_digest) 		&& /* Same checksum?      */
				ptr->fsize == sub->fsize						&& /* Same size? 	 	  */
				cmp_inodes(ptr->path,sub->path) 				&& /* Not the same inode? */
				(set.paranoid) ? paranoid(ptr->path,sub->path) : 1 /* BbB needed?	      */ 
			  ) 
			  {
				
				lintsize += sub->fsize;
				handle_item(sub->path, ptr->path, script_out); 

				sub->dupflag = true;
				ptr->dupflag = true; 
				finds++; 
			}	
			sub = sub->next;
		}
		
		if(ptr->fpc == 42)
			break;
	
		ptr = ptr->next;
		sub = list_begin(); 
	}
	
	size_to_human_readable(lintsize,lintbuf);
	warning("\nWrote result to "BLU"./"SCRIPT_NAME NCO" -- ");
	warning("In total "RED"%ld"NCO" duplicate%sfound. ("GRE"%s"NCO")\n", finds, (finds) ? " " : "s ",lintbuf);
	
	if(script_out)
		fclose(script_out);
}

