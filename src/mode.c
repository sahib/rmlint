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
                if(unlink(path)) {
                        warning(YEL"WARN: "NCO"remove(): %s\n", strerror(errno));
                }
        }
}

/** This is only for extremely paranoid people **/
static int paranoid(const char *p1, const char *p2, uint32 size)
{
        uint32 b1=0,b2=0;
        FILE *f1,*f2;

        char *c1 = alloca((MD5_IO_BLOCKSIZE>size) ? size+1 : MD5_IO_BLOCKSIZE),
              *c2 = alloca((MD5_IO_BLOCKSIZE>size) ? size+1 : MD5_IO_BLOCKSIZE);

        f1 = fopen(p1,"rb");
        f2 = fopen(p2,"rb");

        if(p1==NULL||p2==NULL) {
                return 0;
        }

        while(  (b1 = fread(c1,sizeof(char),MD5_IO_BLOCKSIZE,f1))
                && (b2 = fread(c2,sizeof(char),MD5_IO_BLOCKSIZE,f2))
             ) {
                int i = 0;

                if(b1!=b2) {
                        return 0;
                }
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
                        if(file->dupflag != TYPE_BLNK) {
                                error(YEL"WARN: "NCO"Unable to get full path [of %s] ", file->path);
                                perror("(write_to_log():mode.c)");
                        }
                        free_fullpath = false;
                        fpath = (char*)file->path;
                }
                if(set.mode == 5) {

                        if(file->dupflag == TYPE_BLNK) {
                                fprintf(fd, "rm \"%s\" # Bad link pointing nowhere.\n", fpath);
                        } else if(file->dupflag == TYPE_OTMP) {
                                fprintf(fd, "rm \"%s\" # Tempdata that is older than the actual file.\n", fpath);
                        } else if(file->dupflag == TYPE_EDIR) {
                                fprintf(fd, "rmdir \"%s\" # Empty directory\n", fpath);
                        } else if(file->dupflag == TYPE_JNK_DIRNAME) {
                                fprintf(fd, "echo \"%s\" # Direcotryname containing one char of the string \"%s\"\n", fpath, set.junk_chars);
                        } else if(file->dupflag == TYPE_JNK_FILENAME) {
                                fprintf(fd, "ls -ls \"%s\" # Filename containing one char of the string \"%s\"\n", fpath, set.junk_chars);
                        } else if(file->dupflag == TYPE_NBIN) {
                                fprintf(fd, "strip -s \"%s\" # Binary containg debug-symbols\n", fpath);
                        } else if(!orig) {
                                fprintf(fd, set.cmd_orig, fpath);
                                if(set.cmd_orig) {
                                        fprintf(fd, SCRIPT_LINE_SUFFIX);
                                        fprintf(fd, " # Duplicate\n");
                                }
                        } else {
                                fprintf(fd, set.cmd_path, fpath);
                                if(set.cmd_path) {
                                        fprintf(fd, SCRIPT_LINE_SUFFIX);
                                        fprintf(fd, " # Original\n");
                                }
                        }
                } else {
                        int i;
                        if(file->dupflag == TYPE_BLNK) {
                                fprintf(fd,"BLNK \"%s\" %lu 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                        } else if(file->dupflag == TYPE_OTMP) {
                                fprintf(fd,"OTMP \"%s\" %lu 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                        } else if(file->dupflag == TYPE_EDIR) {
                                fprintf(fd,"EDIR \"%s\" %lu 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                        } else if(file->dupflag == TYPE_JNK_DIRNAME) {
                                fprintf(fd,"JNKD \"%s\" %lu 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                        } else if(file->dupflag == TYPE_JNK_FILENAME) {
                                fprintf(fd,"JNKN \"%s\" %lu 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                        } else if(file->dupflag == TYPE_NBIN) {
                                fprintf(fd,"NBIN \"%s\" %lu 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                        } else if(file->fsize == 0) {
                                fprintf(fd,"ZERO \"%s\" %lu 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                        } else if(orig != true) {
                                fprintf(fd,"DUPL \"%s\" %lu 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                        } else {
                                fprintf(fd,"ORIG \"%s\" %lu 0x%x %ld ", fpath, file->fsize, (unsigned short)file->dev, file->node);
                        }

                        for (i = 0; i < 16; i++) {
                                fprintf (fd,"%02x", file->md5_digest[i]);
                        }
                        fputc('\n',fd);
                }

                if(free_fullpath && fpath && file->dupflag != TYPE_BLNK) {
                        free(fpath);
                }

        } else if(set.output) {
                error(RED"ERROR: "NCO"Unable to write to log\n");
        }
}


static bool handle_item(iFile *file_path, iFile *file_orig)
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
                if(path == NULL) {
                        break;
                }

                do {
                        error(YEL":: "NCO"'%s' same as '%s' [h for help]\n"YEL":: "NCO,path,orig);
                        do {
                                if(!scanf("%c",&sel)) {
                                        perror("scanf()");
                                }
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
                                return true;
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
                if(path == NULL) {
                        break;
                }
                warning(RED"rm "NCO"\"%s\"\n", path);
                remfile(path);
        }
        break;

        case 4: {
                /* Replace the file with a neat symlink */
                if(path == NULL) {
                        break;
                }
                error(NCO"ln -s "NCO"\"%s\""NCO"\"%s\"\n", orig, path);
                if(unlink(path)) {
                        warning(YEL"WARN: "NCO"remove(): %s\n", strerror(errno));
                }

                if(link(orig,path)) {
                        error(YEL"WARN: "NCO"symlink(\"%s\") failed.\n", strerror(errno));
                }
        }
        break;

        case 5: {
                /* Exec a command on it */
                int ret = 0;
                if(path) {
                        ret=systemf(set.cmd_path,path);
                } else {
                        ret=systemf(set.cmd_orig,orig);
                }

                if (WIFSIGNALED(ret) &&
                    (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT)) {
                        return true;
                }
        }
        break;

        default:
                error(RED"ERROR: "NCO"Invalid set.mode. This is a program error :(");
                return true;
        }
        return false;
}


void init_filehandler(void)
{
        if(set.output) {
                script_out = fopen(set.output, "w");
                if(script_out) {
                        char *cwd = getcwd(NULL,0);

                        /* Make the file executable */
                        if(fchmod(fileno(script_out), S_IRUSR|S_IWUSR|S_IXUSR) == -1) {
                                perror(YEL"WARN: "NCO"chmod");
                        }

                        /* Write a basic header */
                        fprintf(script_out,
                                "#!/bin/sh\n"
                                "#This file was autowritten by 'rmlint'\n"
                                "# rmlint was executed from: %s\n",cwd);

                        if((!set.cmd_orig && !set.cmd_path) || set.mode != 5) {
                                fprintf(get_logstream(), "#\n# Entries are listed like this: \n");
                                fprintf(get_logstream(), "# dupflag | path | size | devID | inode | md5sum\n");
                                fprintf(get_logstream(), "# -------------------------------------------\n");
                                fprintf(get_logstream(), "# dupflag : What type of lint found:\n");
                                fprintf(get_logstream(), "#           BLNK: Bad link pointing nowhere\n"
                                        "#           OTMP: Old tmp data (e.g: test.txt~)\n"
                                        "#           EDIR: Empty directory\n"
                                        "#           JNKD: Dirname containg one a char of a user defined string\n"
                                        "#           JNKF: Filename containg one a char of a user defined string\n"
                                        "#           ZERO: Empty file\n"
                                        "#           NBIN: Nonstripped binary\n"
                                        "#           ORIG: File that has a duplicate, but supposed to be a original\n"
                                        "#           DUPL: File that is supposed to be a duplicate\n"
                                        "#\n");
                                fprintf(get_logstream(), "# path    : The full path to the found file\n");
                                fprintf(get_logstream(), "# size    : total size in byte as a decimal integer\n");
                                fprintf(get_logstream(), "# devID   : The ID of the device where the find is stored in hexadecimal form\n");
                                fprintf(get_logstream(), "# inode   : The Inode of the file (see man 2 stat)\n");
                                fprintf(get_logstream(), "# md5sum  : The full md5-checksum of the file\n#\n");
                        }
                        if(cwd) {
                                free(cwd);
                        }
                } else {
                        perror(NULL);
                }
        }
}

static int cmp_f(iFile *a, iFile *b)
{
        int i = 0;
        for(; i < MD5_LEN; i++) {
                if(a->md5_digest[i] != b->md5_digest[i]) {
                        return 1;
                }
        }
        for(i = 0; i < MD5_LEN; i++) {
                if(a->fp[0][i] != b->fp[0][i]) {
                        return 1;
                }
        }
        for(i = 0; i < MD5_LEN; i++) {
                if(a->fp[1][i] != b->fp[1][i]) {
                        return 1;
                }
        }

        return 0;
}


bool findmatches(file_group *grp)
{
        iFile *i = grp->grp_stp, *j;
        if(i == NULL) {
                return false;
        }

        warning(NCO);

        while(i) {
                if(i->dupflag) {
                        bool printed_original = false;
                        j=i->next;

                        /* Make sure no group is printed / logged at the same time (== chaos) */
                        pthread_mutex_lock(&mutex_printage);

                        while(j) {
                                if(j->dupflag) {
                                        if( (!cmp_f(i,j))           &&                              /* Same checksum?                                             */
                                            (i->fsize == j->fsize)	&&					            /* Same size? (double check, you never know)             	 */
                                            ((set.paranoid)?paranoid(i->path,j->path,i->fsize):1)   /* If we're bothering with paranoid users - Take the gatling! */
                                          ) {
                                                /* i 'similiar' to j */
                                                j->dupflag = false;
                                                i->dupflag = false;

                                                lintsize += j->fsize;

                                                if(printed_original == false) {
                                                        if(set.mode == 1) {
                                                                error("# %s\n",i->path);
                                                        }

                                                        write_to_log(i, true, script_out);
                                                        handle_item(NULL, i);
                                                        printed_original = true;
                                                }

                                                if(set.mode == 1 || (set.mode == 5 && !set.cmd_orig && !set.cmd_path)) {
                                                        if(set.paranoid) {
                                                                /* If byte by byte was succesful print a blue "x" */
                                                                warning(BLU"%-1s "NCO,"X");
                                                        } else {
                                                                warning(GRE"%-1s "NCO,"*");
                                                        }


                                                        error("%s\n",j->path);
                                                }
                                                write_to_log(j, false, script_out);
                                                if(handle_item(j,i)) {
                                                        return true;
                                                }
                                        }
                                }
                                j = j->next;
                        }

                        /* Get ready for next group */
                        if(printed_original) {
                                error("\n");
                        }
                        pthread_mutex_unlock(&mutex_printage);

                        /* Now remove if i didn't match in list */
                        if(i->dupflag) {
                                iFile *tmp = i;

                                grp->len--;
                                grp->size -= i->fsize;
                                i = list_remove(i);

                                /* Update start / end */
                                if(tmp == grp->grp_stp) {
                                        grp->grp_stp = i;
                                }

                                if(tmp == grp->grp_enp) {
                                        grp->grp_enp = i;
                                }

                                continue;
                        } else {
                                i=i->next;
                                continue;
                        }
                }
                i=i->next;
        }

        return false;
}
