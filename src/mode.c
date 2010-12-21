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

/*
  mode.c:
  1) log routines
  2) finding double checksums
  3) implementation of different modes

*/

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <alloca.h>

#include "rmlint.h"
#include "mode.h"
#include "md5.h"


nuint_t duplicates = 0;
nuint_t lintsize = 0;


nuint_t dup_counter = 1;
nuint_t get_dupcounter() { return dup_counter; }
void set_dupcounter(nuint_t new) { dup_counter = new; }

/* Make the stream "public" */
FILE *script_out = NULL;
FILE *log_out    = NULL;

pthread_mutex_t mutex_printage = PTHREAD_MUTEX_INITIALIZER;

FILE *get_logstream(void)
{
    return log_out;
}

FILE *get_scriptstream(void)
{
    return script_out;
}


char * __strsubs(char * string, const char * subs, size_t subs_len, const char * with, size_t with_len, long offset)
{
	char * new, * occ = strstr(string+offset,subs);
	size_t strn_len = 0;

	/* Terminate when no occurences left */
	if(occ == NULL)
	{
		return string;
	}

	/* string has a variable length */
	strn_len = strlen(string);
	new = calloc(strn_len + with_len - subs_len + 1,sizeof(char));
	
	/* Split & copy */
	strncat(new, string, occ-string);
	strncat(new, with, with_len);
	strncat(new, occ+subs_len, strn_len - subs_len - (occ-string));

	/* free previous pointer */
	free(string);
	return __strsubs(new,subs,subs_len,with,with_len,(occ-string)+with_len);
}

/* Return always a newly allocated string - wraps around __strsubs() */
char * strsubs(const char * string, const char * subs, const char * with)
{
	size_t subs_len, with_len;

	/* Handle special cases (where __strsubs would return weird things) */
	if(string == NULL || *string == 0)
	{
		return NULL;
	}

	if(subs == NULL || (subs == NULL && with == NULL) || *subs == 0)
	{
		return strdup(string);
	}

	/* Call strlen() only once */
	subs_len = strlen(subs);
	with_len = (with) ? strlen(with) : 0;

	/* Replace all occurenced recursevely */
	return __strsubs(strdup(string), subs, subs_len, with, with_len,0);
}

/* Simple wrapper ariund unlink() syscall */
static void remfile(const char *path)
{
    if(path)
    {
        if(unlink(path))
        {
            perror(YEL"WARN:"NCO" remove():");
        }
    }
}

/** This is only for extremely paranoid people **/
static int paranoid(const char *p1, const char *p2, nuint_t size)
{
    nuint_t b1=0,b2=0;
    FILE *f1,*f2;

    char *c1 = alloca((MD5_IO_BLOCKSIZE>size) ? size+1 : MD5_IO_BLOCKSIZE);
    char *c2 = alloca((MD5_IO_BLOCKSIZE>size) ? size+1 : MD5_IO_BLOCKSIZE);

    f1 = fopen(p1,"rb");
    f2 = fopen(p2,"rb");

    if(p1==NULL||p2==NULL)
    {
        return 0;
    }

    while( (b1 = fread(c1,sizeof(char),MD5_IO_BLOCKSIZE,f1))
            && (b2 = fread(c2,sizeof(char),MD5_IO_BLOCKSIZE,f2))
         )
    {
        int i = 0;

        if(b1!=b2)
        {
            return 0;
        }
        for(; i < b1; i++)
        {
            if(c1[i] - c2[i])
            {
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

void write_to_log(const lint_t *file, bool orig)
{
    bool free_fullpath = true;
    if(get_logstream() && get_scriptstream() && set.output)
    {
        int i = 0;
        char *fpath = canonicalize_file_name(file->path);
        
        if(!fpath)
        {
            if(file->dupflag != TYPE_BLNK)
            {
                error(YEL"WARN: "NCO"Unable to get full path [of %s] ", file->path);
                perror("(write_to_log():mode.c)");
            }
            free_fullpath = false;
            fpath = (char*)file->path;
        }
        else
        {
            char *tmp_copy = fpath; 
            fpath = strsubs(fpath,"'","'\"'\"'");
            free(tmp_copy);
        }

        if(file->dupflag == TYPE_BLNK)
        {
            fprintf(get_scriptstream(), "rm '%s' # Bad link pointing nowhere.\n", fpath);
            fprintf(get_logstream(),"BLNK '%s' %lu %ld %ld ", fpath, file->fsize, file->dev, file->node);
        }
        else if(file->dupflag == TYPE_OTMP)
        {
            fprintf(get_scriptstream(), "rm '%s' # Tempdata that is older than the actual file.\n", fpath);
            fprintf(get_logstream(),"OTMP '%s' %lu %ld %ld ", fpath, file->fsize, file->dev, file->node);
        }
        else if(file->dupflag == TYPE_EDIR)
        {
            fprintf(get_scriptstream(), "rmdir '%s' # Empty directory\n", fpath);
            fprintf(get_logstream(),"EDIR '%s' %lu %ld %ld ", fpath, file->fsize, file->dev, file->node);
        }
        else if(file->dupflag == TYPE_JNK_DIRNAME)
        {
            fprintf(get_scriptstream(), "echo '%s' # Direcotryname containing one char of the string \"%s\"\n", fpath, set.junk_chars);
            fprintf(get_logstream(),"JNKD '%s' %lu %ld %ld ", fpath, file->fsize, file->dev, file->node);
        }
        else if(file->dupflag == TYPE_JNK_FILENAME)
        {
            fprintf(get_scriptstream(), "ls -ls '%s' # Filename containing one char of the string \"%s\"\n", fpath, set.junk_chars);
            fprintf(get_logstream(),"JNKN '%s' %lu %ld %ld ", fpath, file->fsize, file->dev, file->node);
        }
        else if(file->dupflag == TYPE_NBIN)
        {
            fprintf(get_scriptstream(), "strip -s '%s' # Binary containg debug-symbols\n", fpath);
            fprintf(get_logstream(),"NBIN '%s' %lu %ld %ld ", fpath, file->fsize, file->dev, file->node);
        }
        else if(file->fsize == 0)
        {
            fprintf(get_scriptstream(), "rm -rf '%s' # Empty file\n", fpath);
            fprintf(get_logstream(),"ZERO '%s' %lu %ld %ld ", fpath, file->fsize, file->dev, file->node);
        }
        else if(orig==false)
        {

            fprintf(get_logstream(),"DUPL '%s' %lu %ld %ld ", fpath, file->fsize, file->dev, file->node);
            if(set.cmd_path)
            {
                fprintf(get_scriptstream(),set.cmd_path,fpath);
                fprintf(get_scriptstream()," &&\n");
            }
            else
            {
                fprintf(get_scriptstream(),"rm -rf '%s' # Duplicate\n",fpath);
            }
        }
        else
        {

            fprintf(get_logstream(),"ORIG '%s' %lu %ld %ld ", fpath, file->fsize, file->dev, file->node);
            if(set.cmd_orig)
            {
                fprintf(get_scriptstream(),set.cmd_orig,fpath);
                fprintf(get_scriptstream()," \n");
            }
            else
            {
                fprintf(get_scriptstream(),"ls -lisa '%s' # Original\n",fpath);
            }
        }
        for (i = 0; i < 16; i++)
        {
            fprintf (get_logstream(),"%02x", file->md5_digest[i]);
        }
        fputc('\n',get_logstream());


        if(free_fullpath && fpath && file->dupflag != TYPE_BLNK)
        {
            free(fpath);
        }
    }
}


static bool handle_item(lint_t *file_path, lint_t *file_orig)
{
    char *path = (file_path) ? file_path->path : NULL;
    char *orig = (file_orig) ? file_orig->path : NULL;

    /* What set.mode are we in? */
    switch(set.mode)
    {

    case 1:
        break;
    case 2:
    {
        /* Ask the user what to do */
        char sel, block = 0;
        if(path == NULL)
        {
            break;
        }

        do
        {
            error(YEL":: "NCO"'%s' same as '%s' [h for help]\n"YEL":: "NCO,path,orig);
            do
            {
                if(!scanf("%c",&sel))
                {
                    perror("scanf()");
                }
            }
            while ( getchar() != '\n' );

            switch(sel)
            {
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

        }
        while(block);

    }
    break;

    case 3:
    {
        /* Just remove it */
        if(path == NULL)
        {
            break;
        }
        warning(RED"   rm -rf "NCO"\"%s\"\n", path);
        remfile(path);
    }
    break;

    case 4:
    {
        /* Replace the file with a neat symlink */
        if(path == NULL)
        {
            break;
        }
        error(NCO"   ln -s "NCO"\"%s\" "NCO"\"%s\"\n", orig, path);
        remfile(path);

        if(link(orig,path))
        {
            perror(YEL"WARN: "NCO"symlink(\"%s\"):");
        }
    }
    break;

    case 5:
    {
        /* Exec a command on it */
        int ret = 0;

        if(path)
        {
            ret=systemf(set.cmd_path,path);
        }
        else
        {
            ret=systemf(set.cmd_orig,orig);
        }

        if (WIFSIGNALED(ret) &&
                (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT))
        {
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
    if(set.output)
    {
        char *sc_name = strdup_printf("%s.sh", set.output);
        char *lg_name = strdup_printf("%s.log",set.output);
        script_out = fopen(sc_name, "w");
        log_out    = fopen(lg_name, "w");

        if(script_out && log_out)
        {
            char *cwd = getcwd(NULL,0);

            /* Make the file executable */
            if(fchmod(fileno(script_out), S_IRUSR|S_IWUSR|S_IXUSR) == -1)
            {
                perror(YEL"WARN: "NCO"chmod");
            }

            /* Write a basic header */
            fprintf(get_scriptstream(),
                    "#!/bin/sh\n"
                    "#This file was autowritten by 'rmlint'\n"
                    "# rmlint was executed from: %s\n",cwd);

            if((!set.cmd_orig && !set.cmd_path) || set.mode != 5 || 1)
            {
                fprintf(get_logstream(),"#This file was autowritten by 'rmlint'\n");
                fprintf(get_logstream(),"#rmlint was executed from: %s\n",cwd);
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
                fprintf(get_logstream(), "# devID   : The ID of the device where the file is located\n");
                fprintf(get_logstream(), "# inode   : The Inode of the file (see man 2 stat)\n");
                fprintf(get_logstream(), "# md5sum  : The full md5-checksum of the file\n#\n");
            }
            if(cwd)
            {
                free(cwd);
            }
        }
        else
        {
            perror(NULL);
        }

        /* Now close and reopen the stream.
           Why that? Because you get rmlint.sh
           rmlint.log shown as lint (empty files)
           otherwise. What isn't quite what we're
           looking for. If theres a neater solution
           I would be pleased to hear it!
        */
        if(script_out) fclose(script_out);
        if(log_out) fclose(log_out);
        script_out = fopen(sc_name, "a");
        log_out    = fopen(lg_name, "a");
        if(sc_name) free(sc_name);
        if(lg_name) free(lg_name);
    }
}


/* Compare criteria of checksums */
static int cmp_f(lint_t *a, lint_t *b)
{
    int i, fp_i, x;
    int is_empty[2][3];
    memset(is_empty[0],1,3);
    memset(is_empty[1],1,3);
    
    for(i = 0; i < MD5_LEN; i++)
    {
        if(a->md5_digest[i] != b->md5_digest[i])
        {
            return 1;
        }
        if(a->md5_digest[i] != 0)
        {
            is_empty[0][0] = 0;
        }
        if(b->md5_digest[i] != 0)
        {
            is_empty[1][0] = 0;
        }
    }

    for(fp_i = 0; fp_i < 2; fp_i++)
    {
        for(i = 0; i < MD5_LEN; i++)
        {
            if(a->fp[fp_i][i] != b->fp[fp_i][i])
            {
                return 1;
            }

            if(a->fp[fp_i][i] != 0)
            {
                is_empty[0][fp_i+1] = 0;
            }
            if(b->fp[fp_i][i] != 0)
            {
                is_empty[1][fp_i+1] = 0;
            }
        }
    }

    /* check for empty checkusm AND fingerprints - refuse and warn */
    for(x=0; x<2; x++)
    {
        if(is_empty[x][0] && is_empty[x][1] && is_empty[x][2])
        {
            warning(YEL"WARN: "NCO"Refusing file with empty checksum and empty fingerprint - This may be a bug!\n");
            return 1;
        }
        
    }

    set_dupcounter(get_dupcounter()+1);
    return 0;
}


bool findmatches(file_group *grp)
{
    lint_t *i = grp->grp_stp, *j;
    if(i == NULL)
    {
        return false;
    }

    warning(NCO);

    while(i)
    {
        if(i->dupflag)
        {
            bool printed_original = false;
            j=i->next;

            /* Make sure no group is printed / logged at the same time (== chaos) */
            pthread_mutex_lock(&mutex_printage);

            while(j)
            {
                if(j->dupflag)
                {
                    if( (!cmp_f(i,j))           &&                              /* Same checksum?                                             */
                            (i->fsize == j->fsize)	&&					            /* Same size? (double check, you never know)             	 */
                            ((set.paranoid)?paranoid(i->path,j->path,i->fsize):1)   /* If we're bothering with paranoid users - Take the gatling! */
                      )
                    {
                        /* i 'similiar' to j */
                        j->dupflag = false;
                        i->dupflag = false;

                        lintsize += j->fsize;

                        if(printed_original == false)
                        {
                            if((set.mode == 1 || (set.mode == 5 && set.cmd_orig == NULL && set.cmd_path == NULL)) && set.verbosity > 1)
                            {
                                error("   #  %s\n",i->path);
                            }

                            write_to_log(i, true);
                            handle_item(NULL, i);
                            printed_original = true;
                        }

                        if(set.mode == 1 || (set.mode == 5 && !set.cmd_orig && !set.cmd_path))
                        {
                            if(set.paranoid)
                            {
                                /* If byte by byte was succesful print a blue "x" */
                                warning(BLU"   %-1s "NCO,"rm");
                            }
                            else
                            {
                                warning(GRE"   %-1s "NCO,"rm");
                            }

                            if(set.verbosity > 1)
                            {
                                error("%s\n",j->path);
                            }
                            else
                            {
                                error("   rm %s\n",j->path);
                            }

                        }
                        write_to_log(j, false);
                        if(handle_item(j,i))
                        {
                            return true;
                        }
                    }
                }
                j = j->next;
            }

            /* Get ready for next group */
            if(printed_original && set.verbosity > 1)
            {
                error("\n");
            }
            pthread_mutex_unlock(&mutex_printage);

            /* Now remove if i didn't match in list */
            if(i->dupflag)
            {
                lint_t *tmp = i;

                grp->len--;
                grp->size -= i->fsize;
                i = list_remove(i);

                /* Update start / end */
                if(tmp == grp->grp_stp)
                {
                    grp->grp_stp = i;
                }

                if(tmp == grp->grp_enp)
                {
                    grp->grp_enp = i;
                }

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

    return false;
}
