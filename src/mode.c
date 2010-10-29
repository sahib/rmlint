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

#define READSIZE 8192

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

        while((b1 = fread(c1,1,READSIZE,f1))&&(b2 = fread(c2,1,READSIZE,f2))) {
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


static int cmp_f(unsigned char *a, unsigned char *b)
{
        int i = 0;
        for(; i < MD5_LEN; i++) {
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

static void write_to_log(const char* path, bool orig, FILE *script_out)
{
        if(script_out) {
                char *fpath = canonicalize_file_name(path);

                if(!fpath) {
                        perror("Can't get abs path");
                        fpath = (char*)path;
                }

                if(set.cmd) {
                        size_t len = strlen(path)+strlen(set.cmd)+1;
                        char *cmd_buff = alloca(len);
                        snprintf(cmd_buff,len,set.cmd,fpath);
                        fprintf(script_out, "%s\n", cmd_buff);
                } else {
                        if(orig != true)
                                fprintf(script_out,"rm \"%s\"\n", fpath);
                        else
                                fprintf(script_out,"\n#  \"%s\"\n", fpath);
                }

                if(fpath) free(fpath);
        } else {
                error("Unable to write to log\n");
        }
}


static void handle_item(const char *path, const char *orig, FILE *script_out)
{
        /* What set.mode are we in? */
        switch(set.mode) {

        case 1:
                break;
        case 2: {
                /* Ask the user what to do */
                char sel, block = 0;

                print_askhelp();

                do {
                        error(RED"#[%ld] \""YEL"%s"RED"\""GRE" == "RED"\""YEL"%s"RED"\"\n"BLU"Remove %s?\n"BLU"=> "NCO, duplicates+1,orig, path, path);
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
                                fprintf(stdout,"link \"%s\"\t-> \"%s\"\n", path, orig);
                                block = 0;
                                break;

                        case 'q':
                                die(-42);

                        case 'h':
                                print_askhelp();
                                block = 1;
                                break;

                        default :
                                warning("Invalid input."NCO);
                                block = 1;
                                break;
                        }

                } while(block);

        }
        break;

        case 3: {
                /* Just remove it */
                warning(RED" rm "NCO"\"%s\"\n", path);
                remfile(path);
        }
        break;

        case 4: {
                /* Replace the file with a neat symlink */
                error(GRE"link "NCO"\"%s\""RED" -> "NCO"\"%s\"\n", orig, path);
                if(unlink(path))
                        error("remove failed with %s\n", strerror(errno));

                if(link(orig,path))
                        error("symlink() failed with \"%s\"\n", strerror(errno));
        }
        break;

        case 5: {
                int ret;
                size_t len = strlen(path)+strlen(set.cmd)+strlen(orig)+1;
                char *cmd_buff = alloca(len);

                fprintf(stderr,NCO);
                snprintf(cmd_buff,len,set.cmd,path,orig);
                ret = system(cmd_buff);
                if(ret == -1) {
                        perror("System()");
                }

                if (WIFSIGNALED(ret) &&
                    (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT))
                        return;

        }
        break;

        default:
                error(RED"Invalid set.mode. This is a program error. :("NCO);
        }
}


void init_filehandler(void)
{
        script_out = fopen(SCRIPT_NAME, "w");
        if(script_out) {
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
        } else {
                perror(NULL);
        }
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
                                        if( (!cmp_f(i->md5_digest, j->md5_digest))  &&     /* Same checksum?                                             */
                                            (i->fsize == j->fsize)	&&					   /* Same size? (double check, you never know)             	 */
                                            ((set.paranoid)?paranoid(i->path,j->path):1)   /* If we're bothering with paranoid users - Take the gatling! */
                                          ) {
                                                /* i 'similiar' to j */
                                                j->dupflag = false;
                                                i->dupflag = false;

                                                lintsize += j->fsize;

                                                if(printed_original == false) {
                                                        error("# %s\n",i->path);
                                                        write_to_log(i->path, true, script_out);
                                                        handle_item(i->path,i->path,script_out);
                                                        printed_original = true;
                                                }

                                                if(set.paranoid) {
                                                        /* If byte by byte was succesful print a blue "x" */
                                                        warning(BLU"%-1s "NCO,"X");
                                                } else {
                                                        warning(RED"%-1s "NCO,"*");
                                                }
                                                error("%s\n",j->path);

                                                write_to_log(j->path, false, script_out);
                                                handle_item(j->path,i->path,script_out);
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
