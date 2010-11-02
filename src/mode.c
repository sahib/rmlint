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
#include <pthread.h>

#include "rmlint.h"
#include "mode.h"
#include "md5.h"
#include "defs.h"
#include "list.h"


uint32 duplicates = 0;
uint32 lintsize = 0;


/* Make the stream "public" */
FILE *script_out = NULL;
pthread_mutex_t mutex_printage = PTHREAD_MUTEX_INITIALIZER;

FILE *get_logstream(void)
{
        return script_out;
}

static void remfile(const char *path)
{
        if(path) {
                if(unlink(path))
                        warning(YEL"WARN: "NCO"remove(): %s\n", strerror(errno));
        }
}

/** This is only for extremely paranoid people **/
static int paranoid(const char *p1, const char *p2)
{
        uint32 b1=0,b2=0;
        FILE *f1,*f2;

        char c1[MD5_IO_BLOCKSIZE],c2[MD5_IO_BLOCKSIZE];

        f1 = fopen(p1,"rb");
        f2 = fopen(p2,"rb");

        if(p1==NULL||p2==NULL) return 0;

        while(  (b1 = fread(c1,sizeof(char),MD5_IO_BLOCKSIZE,f1))
              && (b2 = fread(c2,sizeof(char),MD5_IO_BLOCKSIZE,f2))
              ) {
                int i = 0;

                if(b1!=b2) return 0;
                for(; i < b1; i++) {
                        if(c1[i] - c2[i]) {
                                fclose(f1);
                                fclose(f2);
                                return 0;
                        }
                }
        }

        fclose(f1);
        fclose(f2);
        return 1;
}

static void print_askhelp(void)
{
        error(  GRE"\nk"NCO" - keep file \n"
                GRE"d"NCO" - delete file \n"
                GRE"i"NCO" - show fileinfo\n"
                GRE"l"NCO" - replace with link \n"
                GRE"q"NCO" - quit all\n"
                GRE"h"NCO" - show help.\n\n"
                NCO );
}

void write_to_log(const iFile *file, bool orig, FILE *fd)
{
		bool free_fullpath = true;
        if(fd && set.output) {
                char *fpath = canonicalize_file_name(file->path);

                if(!fpath) {
						if(file->dupflag != 42) {
							error(YEL"WARN: "NCO"Unable to get full path [of %s] ", file->path);
                        	perror("(write_to_log():mode.c)");
						}
						free_fullpath = false;
                        fpath = (char*)file->path;
                }
                if(set.mode == 5) {
						if(!orig) {
								fprintf(fd, set.cmd_orig, file->path);
								if(set.cmd_orig) 
								{ 
									fprintf(fd, SCRIPT_LINE_SUFFIX); 
									if(file->dupflag != 42) { 
										fprintf(fd, " # Duplicate\n");
									} else { 
										fprintf(fd, " # Bad link (pointing nowhere)\n");	
									}
								}
						} else { 
								fprintf(fd, set.cmd_path, file->path);
								if(set.cmd_path) { 
									fprintf(fd, SCRIPT_LINE_SUFFIX); 
									fprintf(fd, " # Original\n");
								}
						}
                } else {
						int i;
						if(file->dupflag == 42) 
								fprintf(fd,"BLNK \"%s\" %u 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
						else if(file->dupflag == 43) 
								fprintf(fd,"OTMP \"%s\" %u 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
						else if(file->dupflag == 44) 
								fprintf(fd,"EDIR \"%s\" %u 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
						else if(file->fsize == 0) 
								fprintf(fd,"ZERO \"%s\" %u 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
						else if(orig != true) 
                                fprintf(fd,"DUPL \"%s\" %u 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                        else
                                fprintf(fd,"ORIG \"%s\" %u 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                
					    for (i = 0; i < 16; i++) {
                                fprintf (fd,"%02x", file->md5_digest[i]);
                        }
                        fputc('\n',fd); 
                }

                if(free_fullpath && fpath && file->dupflag != 42) 
					free(fpath);
			
        } else if(set.output) {
                error(RED"ERROR: "NCO"Unable to write to log\n");
        }
}


static void handle_item(iFile *file_path, iFile *file_orig)
{
		char *path = (file_path) ? file_path->path : NULL; 
		char *orig = (file_orig) ? file_orig->path : NULL; 
	
        /* What set.mode are we in? */
        switch(set.mode) {

        case 1:
                break;
        case 2: {
                /* Ask the user what to do */
                char sel, block = 0;
				if(path == NULL) break;
			
                do { 
						error(YEL":: "NCO"'%s' same as '%s' [h for help]\n"YEL":: "NCO,path,orig);
                        do {
                                if(!scanf("%c",&sel)) perror("scanf()");
                        } while ( getchar() != '\n' );

                        switch(sel) {
                        case 'k':
                                block = 0;
                                break;

                        case 'd':
                                remfile(path);
                                block = 0;
                                break;

                        case 'l':
                                remfile(path);
                                error(YEL"EXEC: "NCO"ln -s "NCO"\"%s\" \"%s\"\n", orig, path);
                                block = 0;
                                break;
						case 'i':
								
								puts("\nOutput of ls -lahi --author:");
								puts("-----------------------------");
										
								MDPrintArr(file_path->md5_digest);
								printf(" on DevID %d -> ",(unsigned short)file_path->dev);
								fflush(stdout);
								systemf("ls -lahi --author --color=auto '%s'",path);
								MDPrintArr(file_path->md5_digest);
								printf(" on DevID %d -> ",(unsigned short)file_orig->dev);
								fflush(stdout);
								systemf("ls -lahi --author --color=auto '%s'",path);

								puts(" "); 
								block = 1;
								break;
                        case 'q':
                                die(0);
								break;
                        case 'h':
                                print_askhelp();
                                block = 1;
                                break;

                        default :
                                block = 1;
                                break;
                        }

                } while(block);

        }
        break;

        case 3: {
                /* Just remove it */
				if(path == NULL) break; 
                warning(RED"rm "NCO"\"%s\"\n", path);
                remfile(path);
        }
        break;

        case 4: {
                /* Replace the file with a neat symlink */
				if(path == NULL) break; 
                error(NCO"ln -s "NCO"\"%s\""NCO"\"%s\"\n", orig, path);
                if(unlink(path))
                        warning(YEL"WARN: "NCO"remove(): %s\n", strerror(errno));

                if(link(orig,path))
                        error(YEL"WARN: "NCO"symlink(\"%s\") failed.\n", strerror(errno));
        }
        break;

        case 5: {
				/* Exec a command on it */
				int ret = 0; 
                if(path) ret=systemf(set.cmd_path,path);
				else     ret=systemf(set.cmd_orig,orig); 
					
                if (WIFSIGNALED(ret) &&
                    (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT))
                        return;
        }
        break;

        default:
                error(RED"ERROR: "NCO"Invalid set.mode. This is a program error :(");
        }
}


void init_filehandler(void)
{
	if(set.output)
	{
			script_out = fopen(set.output, "w");
			if(script_out) {
					char *cwd = getcwd(NULL,0);

					/* Make the file executable */
					if(fchmod(fileno(script_out), S_IRUSR|S_IWUSR|S_IXUSR) == -1)
							perror(YEL"WARN: "NCO"chmod");

					/* Write a basic header */
					fprintf(script_out,
							"#!/bin/sh\n"
							"#This file was autowritten by 'rmlint'\n"
							"# rmlint was executed from: %s\n",cwd);

					if((!set.cmd_orig && !set.cmd_path) || set.mode != 5) {
							fprintf(get_logstream(), "#\n# Entries are listed like this: \n"); 
							fprintf(get_logstream(), "# dupf | path | size | devID | inode | md5sum\n"); 
							fprintf(get_logstream(), "# -------------------------------------------\n"); 
							fprintf(get_logstream(), "# dupf  : If rmlint thinks this the original, it's marked with '0' otherwise '1'\n"); 
							fprintf(get_logstream(), "# path  : The full path to the found file\n"); 
							fprintf(get_logstream(), "# size  : total size in byte as a decimal integer\n"); 
							fprintf(get_logstream(), "# devID : The ID of the device where the find is stored in hexadecimal form\n"); 
							fprintf(get_logstream(), "# inode : The Inode of the file (see man 2 stat)\n"); 
							fprintf(get_logstream(), "# md5sum: The full md5-checksum of the file\n#\n"); 
						}
					if(cwd) free(cwd);
			} else {
					perror(NULL);
			}
		}
}

static int cmp_f(iFile *a, iFile *b)
{
        int i = 0;
        for(; i < MD5_LEN; i++) {
                if(a->md5_digest[i] != b->md5_digest[i])
                        return 1;
        }
        for(i = 0; i < MD5_LEN; i++) { 
				if(a->fp[0][i] != b->fp[0][i])
					return 1; 
		}
		for(i = 0; i < MD5_LEN; i++) { 
				if(a->fp[1][i] != b->fp[1][i])
					return 1; 
		}			
		
#if DEBUG_CODE > 1	
		MDPrintArr(a->md5_digest); putchar('\n');
		MDPrintArr(b->md5_digest); putchar('\n');
		
		MDPrintArr(a->fp[0]); putchar('\n');
		MDPrintArr(b->fp[0]); putchar('\n');
			
		MDPrintArr(a->fp[1]); putchar('\n');
		MDPrintArr(b->fp[1]); putchar('\n');
#endif 
        return 0;
}


uint32 findmatches(file_group *grp)
{
        iFile *i = grp->grp_stp, *j;
        uint32 remove_count = 0;
        if(i == NULL)  return 0;

        warning(NCO);

        while(i) {
                if(i->dupflag) {
                        bool printed_original = false;
                        j=i->next;

                        /* Make sure no group is printed / logged at the same time (== chaos) */
                        pthread_mutex_lock(&mutex_printage);

                        while(j) {
                                if(j->dupflag) {
                                        if( (!cmp_f(i,j))           &&                     /* Same checksum?                                             */
                                            (i->fsize == j->fsize)	&&					   /* Same size? (double check, you never know)             	 */
                                            ((set.paranoid)?paranoid(i->path,j->path):1)   /* If we're bothering with paranoid users - Take the gatling! */
                                          ) {
                                                /* i 'similiar' to j */
                                                j->dupflag = false;
                                                i->dupflag = false;

                                                lintsize += j->fsize;

                                                if(printed_original == false) {
														if(set.mode == 1) 
															error("# %s\n",i->path);
                                                        
                                                        write_to_log(i, true, script_out);
                                                        handle_item(NULL, i); 
                                                        printed_original = true;
                                                }
												
												if(set.mode == 1) {
														if(set.paranoid) {
																/* If byte by byte was succesful print a blue "x" */
																warning(BLU"%-1s "NCO,"X");
														} else {
																warning(RED"%-1s "NCO,"*");
														}
												}
												
												if(set.mode == 1) 
													error("%s\n",j->path);

                                                write_to_log(j, false, script_out);
                                                handle_item(j,i);
                                        }
                                }
                                j = j->next;
                        }

                        /* Get ready for next group */
                        if(printed_original) error("\n");
                        pthread_mutex_unlock(&mutex_printage);

                        /* Now remove if i didn't match in list */
                        if(i->dupflag) {
                                iFile *tmp = i;

                                grp->len--;
                                grp->size -= i->fsize;
                                i = list_remove(i);

                                /* Update start / end */
                                if(tmp == grp->grp_stp)
                                        grp->grp_stp = i;

                                if(tmp == grp->grp_enp)
                                        grp->grp_enp = i;

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
