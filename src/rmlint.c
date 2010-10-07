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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <getopt.h>
#include <dirent.h>
#include <fcntl.h>
#include <setjmp.h>

#include "rmlint.h"
#include "mode.h" 
#include "filter.h"
#include "list.h"
#include "defs.h"
#include "mt.h"

/**
 * ToDo: 
 * -man page / README / updated help 
 * - some comments.. clean up..
 * - gettext, to translate msgs   -- on ice, because of gettext being crap
 * - pusblish. 
 * - - list_sort() - quicksort 
 * - allow files as input 
 * - --exclude option and --nohide
 * - better prefilter (does not handle start and end of list) 
 * - mt.c is some overhead - in build_checksum we already know the number of files. 
 * - print hashes()
 * - make docs be docs.  
 **/


/**
 
Result of the testdir run should be (dont run with "-f"!): 
----------------------------------------------------------

= testdir/recursed_a/one
X testdir/recursed_b/one

= testdir/recursed_a/two
X testdir/twice_one
X testdir/two

= testdir/recursed_b/three
X testdir/recursed_b/two_plus_one

= testdir/with spaces a
X testdir/with spaces b


Empty files are not shown - they will be 
handled seperate in future versions. 
 **/

/** 
 * ToDo2 (for removing other sort of "lint") 
 * - Write bad symlinks to script 
 * - Find old tmp data (*~)  
 * - empty dirs 
 * - non stripped binaries 
 * => Should be complete after implementing this. 
 **/ 

bool do_exit = false; 
bool use_cwd = false; 
char *mode_string = NULL; 
int  cpindex = 0; 

/* If we abort abnormally we'd like to set the color back */
static void resetcol(void) { printf(NCO); }
jmp_buf place;

/** Messaging **/ 
void error(const char* format, ...)
{
	if(set.verbosity > 0)
	{
	  va_list args;
	  va_start (args, format);
	  vfprintf (stderr, format, args);
	  va_end (args);
	}
}

void warning(const char* format, ...)
{
	if(set.verbosity > 1)
	{
	  va_list args;
	  va_start (args, format);
	  vfprintf (stderr, format, args);
	  va_end (args);
	}
}

void info(const char* format, ...)
{
	if(set.verbosity > 2)
	{
	  va_list args;
	  va_start (args, format);
	  vfprintf (stdout, format, args);
	  va_end (args);
	}
}

static void print_help(void)
{
	fprintf(stderr, "Syntax: rmlint [TargetDir[s]] [Options]\n");
	fprintf(stderr, "\nGeneral options:\n\n"
					"\t-t --threads <t>\tSet the number of threads to <t> used in full checksum creation.\n"
					"\t-p --paranoid\t\tDo a byte-by-byte comparasion additionally. (Slow!)\n"
					"\t-z --skipfp\t\tSkip building fingerprints (good for many small files)\n"
					"\t-y --skippre\t\tSkip Prefiltering (always bad)\n"
					); 
	fprintf(stderr,	"\t-d --maxdepth <depth>\tOnly recurse up to this depth. (default: inf)\n"
					"\t-f --followlinks\tWether links are followed (None is reported twice)\n"
					"\t-s --samepart\t\tNever cross mountpoints, stay on the same partition\n"
					"\t-m --mode <mode>\tTell rmlint how to deal with the files it finds.\n"
					);
	fprintf(stderr,	"\n\t\t\t\tWhere modes are:\n\n"
					"\t\t\t\tlist  - Only list found files and exit.\n"
					"\t\t\t\tlink  - Replace file with a hard link to original.\n"
					"\t\t\t\task   - Ask for each file what to do\n"
					"\t\t\t\tnoask - Full removal without asking.\n"
					"\t\t\t\tcmd   - Takes the command given by -c and executes it on the file.\n\n"
					"\t-c --command <cmd>\tExecute a shellcommand on found files when used with '-m cmd'\n" 
					"\t\t\t\tExample: rmlint -m cmd -c \"ls -lah %%s %%s\"\n"
					"\t\t\t\tFirst %%s expands to found duplicate, second to original.\n"
					);
	fprintf(stderr,	"Regex options:\n\n"
					"\t-r --fregex <pat>\tChecks filenames against the pattern <pat>\n"
					"\t-R --dregex <pat>\tChecks dirnames against the pattern <pat>\n"
					"\t-i --invmatch\t\tInvert match - Only investigate when npt containing <pat>\n"
					"\t-e --matchcase\t\tMatches case of paths (not by default)\n");
	fprintf(stderr,	"\nMisc options:\n\n"
					"\t-h --help\t\tPrints this text and exits\n"
					"\t-v --verbosity <v>\tSets the verbosity level to <v>\n"
					"\t\t\t\tWhere:\n"
					"\t\t\t\t0 prints nothing\n" 
					"\t\t\t\t1 prints only errors\n" 
					"\t\t\t\t2 prints warning\n" 
					"\t\t\t\t3 prints everything\n"
					"\n"					
					); 
	fprintf(stderr,"Additionally, the options p,f,s,e and i have a uppercase option (P,F,S,E and I) that inverse it's effect\n");	
	fprintf(stderr, "\nVersion 0.43 - Copyright Christopher <Sahib Bommelig> Pahl\n"); 
	fprintf(stderr, "Licensed under the terms of the GPLv3 - See COPYRIGHT for more information\n");				
	fprintf(stderr, "See the manpage or the README for more information.\n");
	exit(0);
}

void rmlint_set_default_settings(rmlint_settings *set)
{
	set->mode  		 =  1; 		/* list only    */
	set->casematch   =  0; 		/* Insensitive  */
	set->fingerprint =  1; 		/* Do fprints   */ 
	set->prefilter   =  1;		/* Do prefilter */ 
	set->invmatch	 =  0;		/* Normal mode  */
	set->paranoid	 =  0; 		/* dont be bush */
	set->depth 		 =  0; 		/* inf depth    */
	set->followlinks =  0; 		/* fol. link    */
	set->threads 	 =  4; 		/* Quad,quad.   */
	set->verbosity 	 =  3; 		/* Everything   */
	set->samepart  	 =  0; 		/* Stay parted  */
	set->paths  	 =  NULL;   /* Startnode    */
	set->dpattern 	 =  NULL;   /* DRegpattern  */
	set->fpattern 	 =  NULL;   /* FRegPattern  */
	set->cmd 		 =  NULL;   /* Cmd,if used  */
}

char rmlint_parse_arguments(int argc, char **argv, rmlint_settings *sets) 
{
	   int c,lp=0;
     
       while (1)
       {
             static struct option long_options[] =
             {
               {"threads",     required_argument, 0, 't'},
               {"dregex",      required_argument, 0, 'R'},
               {"fregex",  	   required_argument, 0, 'r'},
               {"mode",        required_argument, 0, 'm'},
               {"skippre",     no_argument, 	  0, 'y'},
               {"dopre",       no_argument, 	  0, 'Y'},
               {"skipfp",      no_argument, 	  0, 'z'},
               {"buildfp",     no_argument, 	  0, 'Z'},
               {"maxdepth",	   required_argument, 0, 'd'},
               {"command",     required_argument, 0, 'c'},
               {"verbosity",   required_argument, 0, 'v'},
               {"matchcase",   no_argument, 	  0, 'e'},
               {"ignorecase",  no_argument, 	  0, 'E'},
               {"followlinks", no_argument, 	  0, 'f'},
               {"ignorelinks", no_argument, 	  0, 'F'},
               {"invertmatch", no_argument, 	  0, 'i'}, 
               {"normalmatch", no_argument, 	  0, 'I'},    
               {"samepart",    no_argument,		  0, 's'},
               {"allpart",     no_argument,		  0, 'S'},
               {"paranoid",    no_argument,		  0, 'p'},
               {"naive",       no_argument,		  0, 'P'},
               {"help",        no_argument, 	  0, 'h'},
               {0, 0, 0, 0}
             };
             
           /* getopt_long stores the option index here. */
           int option_index = 0;
     
           c = getopt_long (argc, argv, "m:R:r:zZyYhpPfFeEsSiIc:t:d:v:",long_options, &option_index);
     
           /* Detect the end of the options. */
           if (c == -1) break;
     
           switch (c)
           {
			     case '?': return 0; 
				 case 't':
				  
					   sets->threads = atoi(optarg);
					   if(!sets->threads || sets->threads < 0)  sets->threads = 4;
				   
				 break;
				 case 'f': sets->followlinks = 1;      		break;
				 case 'F': sets->followlinks = 0;      		break;
				 case 'h': print_help(); 			   		break;
				 case 'c': sets->cmd = optarg;  			break;
				 case 'v': sets->verbosity = atoi(optarg);  break;
				 case 'i': sets->invmatch = 1;			    break;
				 case 'I': sets->invmatch = 0;			    break;
				 case 's': sets->samepart = 1;				break;
				 case 'S': sets->samepart = 0;				break;
				 case 'e': sets->casematch = 1;				break;
				 case 'E': sets->casematch = 0;				break;
				 case 'd': sets->depth = ABS(atoi(optarg));	break;
				 case 'z': sets->fingerprint = 0;			break;
				 case 'Z': sets->fingerprint = 1;			break;
				 case 'y': sets->prefilter = 0;				break;
				 case 'Y': sets->prefilter = 1;				break;
				 case 'r': sets->fpattern = optarg;			break;
				 case 'R': sets->dpattern = optarg;			break;
				 case 'p': sets->paranoid = 1;				break;
				 case 'P': sets->paranoid = 0;				break;
				 case 'm':
				 
					if(!strcasecmp(optarg, "list"))
						sets->mode = 1; 
					if(!strcasecmp(optarg, "ask"))
						sets->mode = 2; 
					if(!strcasecmp(optarg, "noask"))
						sets->mode = 3; 
					if(!strcasecmp(optarg, "link"))
						sets->mode = 4; 
					if(!strcasecmp(optarg, "cmd"))	
						sets->mode = 5; 
					
					if(!sets->mode)
					{
						error("Invalid value for --mode: \"%s\"\n", argv[argc + 1]); 
						error("Available modes are: ask | list | link |noask | move\n"); 
						return 0; 
					}
					mode_string = optarg; 
				 
				 break;
				 default: return 0;
			 }
         }
     
         while(optind < argc)
         {
			int p = open(argv[optind],O_RDONLY);
			if(p == -1) 
			{
				error(YEL" => "RED"Can't open directory "NCO"\"%s\""RED":"NCO" %s\n"NCO, argv[optind], strerror(errno));
				return 0; 
			}
			else
			{
				close(p);
				sets->paths		  = realloc(sets->paths,sizeof(char*)*(lp+2));
				sets->paths[lp++] = argv[optind];  
				sets->paths[lp  ] = NULL; 
			}		
			optind++; 			
         }
                  
         if(lp == 0) 
         {
			/* Still no path set? */
			sets->paths = malloc(sizeof(char*));
			sets->paths[0] = getcwd(NULL,0);
			if(!sets->paths[0]) 
			{
				error(RED"Cannot get working directory: %s\n"NCO, strerror(errno));
				if(sets->paths) free(sets->paths);
				return 0; 
			}
			use_cwd = true; 
			info(RED" => "NCO"Investigating \"%s\"\n",sets->paths);
	  }  
	  return 1; 
}


static void check_cmd(const char *cmd)
{
	int i = 0, ps = 0; 
	int len = strlen(cmd);  
	for(; i < len; i++)
	{
		 if(cmd[i] == '%' && i+1 != len)
		 {
			 if(cmd[i+1] == 's')
			 {
				ps++;
				continue;
			 } 
			 if(cmd[i+1] != '%') 
			 {
				 error("--command: Only \"%%s\" is allowed!\n"); 
				 die(-3);
			 }  
		 }
	}
	if(ps > 2) 
	{
		error("--command: Format string may only contain two \"%%s\" at the same time!\n");
		die(-2); 
	}
}

int  get_cpindex(void)
{
	return cpindex; 
}

void die(int status)
{
	/* Free mem */
	freepool(); 
	list_clear();
	
	if(use_cwd) 
	{
		if(set.paths[0])
			free(set.paths[0]);
	}
	if(set.paths)
	{
		free(set.paths);
	}
	
	if(status)
	{
		info("Abnormal exit\n"); 
	}
	
	/* Make sure NCO is printed */
	resetcol(); 
	
	/* Prepare to jump to return */
	do_exit = true;
	longjmp(place,status);
}



int cmp_sz(iFile *a, iFile *b)
{
    return a->fsize - b->fsize;
}

int cmp_nd(iFile *a, iFile *b)
{
	return a->node - b->node; 
}



static void print(void)
{
	iFile *p = list_begin();
	fprintf(stdout,"\n----\n"); 
	while(p)
	{
		MDPrintArr(p->md5_digest); 
		fprintf(stdout," => %s | %ld\n",p->path,p->fsize); 
		p=p->next; 
	}
	fprintf(stdout,"----\n");
}

/* Have fun reading. ;-) */
int rmlint_main(void)
{
  uint32 total_files = 0;
  uint32 fpfilterd    = 0; 
  uint32 firc		= 0;
  
  int retval = setjmp(place);
  if(do_exit != true)
  {
	  if(set.cmd == NULL && set.mode == 5)
	  {
		error(YEL" => "NCO"\""YEL"-m cmd"NCO"\" needs a command specified by \""YEL"-c <CMD>"NCO"\"!\n");
		error(YEL" => Example:"NCO" rmlint . -m cmd -c \"ls -la %%s --color=auto\"\n"); 
		die(-1); 
	  }
	  
	  if(set.mode == 5)
		check_cmd(set.cmd);
	  
	  /* Give the user a status line - you will see this two calls more often */
	  info(YEL"Setting up db...\r"NCO);  
	  fflush(stdout);
	 
	   init_filehandler(); 
	 
	  /* Count files and do some init  */ 	 
	  while(set.paths[cpindex] != NULL)
	  {
		DIR *p = opendir(set.paths[cpindex]);
		if(p == NULL && errno == ENOTDIR)
		{
			/* The path is a file */
			struct stat buf; 
			if(stat(set.paths[cpindex],&buf) == -1) 
				continue; 
			
			if(!regfilter(set.paths[cpindex],set.fpattern))
			{
				list_append(set.paths[cpindex],(uint32)buf.st_size,buf.st_dev,buf.st_ino,buf.st_nlink);
				total_files++; 
				firc++; 
			}
		}
		else
		{
			/* The path points to a dir - recurse it! */
			info(RED" => "NCO"Investigating \"%s\"\n",set.paths[cpindex]);
			total_files += recurse_dir(set.paths[cpindex]);
			closedir(p);
		}
		
		cpindex++;
	  }
	  
	  
	  if(total_files == 0)
	  {
		  warning(RED" => "NCO"No files to search through"RED" --> "NCO"No duplicates\n"); 
		  die(0);
	  }
	  info(RED" => "NCO"Using %d thread%c.\n", set.threads, (set.threads != 1) ? 's' : ' '); 
	  info(RED" => "NCO"In total %ld usable files.\n", total_files); 
	  info(RED" => "NCO"Using mode: \"%s\".\n", (mode_string) ? mode_string : "list"); 
	 
	  /* Set threads to be less than the number of files */
	  if(set.threads > total_files) 
		set.threads = total_files;
	  
	  
	  /* Till thios point the list is unsorted
	   * The filter alorithms requires the list to be size-sorted, 
	   * so it can easily filter unique sizes, and build "groups"  
	   * */
	  list_sort(cmp_sz);  
	  
	
	  /* Apply the prefilter and outsort inique sizes */
	  if(set.prefilter)
	  {
		  info(RED" => "NCO"Applying Prefilter... \r"); 
		  fflush(stdout);
		 
		  prefilter(); 
	  }

	   
	  if(0) /*TODO*/
	  {
		  uint32 bfilt = byte_filter(); 
		  if(bfilt != 0) 
		  {
			  info(RED" => "NCO"Bytefiltered %ld files.          \n",bfilt); 
		  }
	  }

	  if(set.fingerprint)
	  {
		  /* Go through directories and filter files with a fingerprint */
		  fpfilterd  = build_fingerprint();
		  
		  if(fpfilterd) 
		  {
			  info(RED" => "NCO"Filtered %ld files\n",fpfilterd); 
		  }
	  }

	  /* Apply it once more - There might be new unique sizes now */
	  if(set.prefilter)
	  {
		  prefilter();
	  }


	  /* The rest of the list is sorted by their inodes -
	   *  so the HD doesnt have to jump all day. */
	  list_sort(cmp_nd); 

	  /* Push filtered files to md5-ToDo list */
	  build_checksums();

	  
	  /* Now we're nearly done */
	  info(RED" => "GRE"Almost done!                                                             \r"NCO);
	  fflush(stdout); 
	  
	  info("\n\n Result:\n"RED" --------\n"NCO);
	  warning("\n");

	  list_sort(cmp_sz); 

	  print();

	  
	  /* Finally find double checksums */
	  findmatches();
	  
	  die(0);
  }
  return retval; 
}

