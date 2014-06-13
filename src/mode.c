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
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdlib.h>

#include "rmlint.h"
#include "mode.h"
#include "md5.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>


nuint_t dup_counter=0;
nuint_t get_dupcounter()
{
    return dup_counter;
}

/* ------------------------------------------------------------- */

void set_dupcounter(nuint_t new)
{
    dup_counter = new;
}

/* ------------------------------------------------------------- */

/* Make the stream "public" */
FILE *script_out;
FILE *log_out;

pthread_mutex_t mutex_printage =  PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------- */

void mode_c_init(void)
{
    script_out = NULL;
    log_out    = NULL;
    dup_counter = 0;
    pthread_mutex_init(&mutex_printage, NULL);
}

/* ------------------------------------------------------------- */

FILE *get_logstream(void)
{
    return log_out;
}
/* ------------------------------------------------------------- */

FILE *get_scriptstream(void)
{
    return script_out;
}

/* ------------------------------------------------------------- */

static char * __strsubs(char * string, const char * subs, size_t subs_len, const char * with, size_t with_len, long offset)
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

/* ------------------------------------------------------------- */

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

/* ------------------------------------------------------------- */

/* Simple wrapper around unlink() syscall */
static void remfile(const char *r_path)
{
    if(r_path)
    {
        if(unlink(r_path) == -1)
        {
            perror(YEL"WARN:"NCO" unlink():");
        }
    }
}

/* ------------------------------------------------------------- */


static void print_askhelp(void)
{
    error(GRE"\nk"NCO" - keep file \n"
          GRE"d"NCO" - delete file \n"
          GRE"i"NCO" - show fileinfo\n"
          GRE"l"NCO" - replace with link \n"
          GRE"q"NCO" - quit all\n"
          GRE"h"NCO" - show help.\n\n"
          NCO);
}

/* ------------------------------------------------------------- */

void log_print(FILE * stream, const char * string)
{
    if(stream && string)
    {
        if(set->verbosity == 4)
        {
            fprintf(stdout,"%s",string);
        }
        fprintf(stream,"%s",string);
    }
}

/* ------------------------------------------------------------- */

static char * make_cmd_ready(bool is_orig, const char * orig, const char * dupl)
{
    char * repl_orig = NULL;
    if(!is_orig)
        repl_orig = strsubs(set->cmd_path,CMD_ORIG,orig);
    else
        repl_orig = strsubs(set->cmd_orig,CMD_ORIG,orig);
    if(repl_orig != NULL && !is_orig)
    {
        char * repl_dups = strsubs(repl_orig,CMD_DUPL,dupl);
        if(repl_dups != NULL)
        {
            free(repl_orig);
            repl_orig = repl_dups;
        }
    }
    return repl_orig;
}

/* ------------------------------------------------------------- */

void script_print(char * string)
{
    if(string != NULL)
    {
        if(set->verbosity == 5)
        {
            fprintf(stdout,"%s",string);
        }
        fprintf(get_scriptstream(),"%s",string);
        free(string);
    }
}

/* ------------------------------------------------------------- */

#define _sd_ strdup_printf

/* ------------------------------------------------------------- */

void write_to_log(const lint_t *file, bool orig, const lint_t * p_to_orig)
{
    bool free_fullpath = true;
    if(get_logstream() && get_scriptstream() && set->output)
    {
        int i = 0;
        char *fpath = realpath(file->path, NULL);
        const char * chown_cmd = "chown $(whoami):$(id -gn)";
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
            /* This is so scary. */
            /* See http://stackoverflow.com/questions/1250079/bash-escaping-single-quotes-inside-of-single-quoted-strings for more info on this */
            char *tmp_copy = fpath;
            fpath = strsubs(fpath,"'","'\"'\"'");
            free(tmp_copy);
        }
        if(file->dupflag == TYPE_BLNK)
        {
            script_print(_sd_("rm -f '%s' # bad link pointing nowhere.\n", fpath));
            log_print(get_logstream(),"BLNK");
        }
        else if(file->dupflag == TYPE_BASE)
        {
            log_print(get_logstream(),"BASE");
            script_print(_sd_("echo  '%s' # double basename.\n", fpath));
        }
        else if(file->dupflag == TYPE_OTMP)
        {
            script_print(_sd_("rm -f '%s' # temp buffer being <%ld> sec. older than actual file.\n", fpath, set->oldtmpdata));
            log_print(get_logstream(),"OTMP");
        }
        else if(file->dupflag == TYPE_EDIR)
        {
            script_print(_sd_("rmdir '%s' # empty folder.\n", fpath));
            log_print(get_logstream(),"EDIR");
        }
        else if(file->dupflag == TYPE_JNK_DIRNAME)
        {
            script_print(_sd_("echo  '%s' # dirname containing one char of the string \"%s\"\n", fpath, set->junk_chars));
            log_print(get_logstream(),"JNKD");
        }
        else if(file->dupflag == TYPE_JNK_FILENAME)
        {
            script_print(_sd_("echo  '%s' # filename containing one char of the string \"%s\"\n", fpath, set->junk_chars));
            log_print(get_logstream(),"JNKN");
        }
        else if(file->dupflag == TYPE_NBIN)
        {
            script_print(_sd_("strip --strip-debug '%s' # binary with debugsymbols.\n", fpath));
            log_print(get_logstream(),"NBIN");
        }
        else if(file->dupflag == TYPE_BADUID)
        {
            script_print(_sd_("%s '%s' # bad uid\n",chown_cmd, fpath));
            log_print(get_logstream(),"BUID");
        }
        else if(file->dupflag == TYPE_BADGID)
        {
            script_print(_sd_("%s '%s' # bad gid\n",chown_cmd, fpath));
            log_print(get_logstream(),"BGID");
        }
        else if(file->fsize == 0)
        {
            script_print(_sd_("rm -f '%s' # empty file.\n", fpath));
            log_print(get_logstream(),"ZERO");
        }
        else if(orig==false)
        {
            log_print(get_logstream(),"DUPL");
            if(set->cmd_path)
            {
                char *opath = realpath(p_to_orig->path, NULL);
                if(opath != NULL)
                {
                    /*script_print(_sd_(set->cmd_path,fpath,opath));*/
                    char * tmp_opath = strsubs(opath,"'","'\"'\"'");
                    if(tmp_opath != NULL)
                    {
                        script_print(make_cmd_ready(false,tmp_opath,fpath));
                        script_print(_sd_("\n"));
                        free(tmp_opath);
                    }
                    free(opath);
                }
            }
            else
            {
                script_print(_sd_("rm -f '%s' # duplicate\n",fpath));
            }
        }
        else
        {
            log_print(get_logstream(),"ORIG");
            if(set->cmd_orig)
            {
                /*//script_print(_sd_(set->cmd_orig,fpath));*/
                script_print(make_cmd_ready(true,fpath,NULL));
                script_print(_sd_(" \n"));
            }
            else
            {
                script_print(_sd_("echo  '%s' # original\n",fpath));
            }
        }
        log_print(get_logstream(),LOGSEP);
        for(i = 0; i < 16; i++)
        {
            if(set->verbosity == 4)
            {
                fprintf(stdout,"%02x", file->md5_digest[i]);
            }
            fprintf(get_logstream(),"%02x", file->md5_digest[i]);
        }
#define INT_CAST long unsigned
        if(set->verbosity == 4)
        {
            fprintf(stdout,"%s%s%s%lu%s%lu%s%lu%s\n", LOGSEP,fpath, LOGSEP, (INT_CAST)file->fsize, LOGSEP, (INT_CAST)file->dev, LOGSEP, (INT_CAST)file->node,LOGSEP);
        }
        fprintf(get_logstream(),"%s%s%s%lu%s%lu%s%lu%s\n", LOGSEP,fpath, LOGSEP, (INT_CAST)file->fsize, LOGSEP, (INT_CAST)file->dev, LOGSEP, (INT_CAST)file->node,LOGSEP);
#undef INT_CAST
        if(free_fullpath && fpath && file->dupflag != TYPE_BLNK)
        {
            free(fpath);
        }
    }
}

/* ------------------------------------------------------------- */

#undef _sd_

/* ------------------------------------------------------------- */

static bool handle_item(lint_t *file_path, lint_t *file_orig)
{
    char *path = (file_path) ? file_path->path : NULL;
    char *orig = (file_orig) ? file_orig->path : NULL;
    /* What set->mode are we in? */
    switch(set->mode)
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
            while(getchar() != '\n');
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
        else
        {
            char * full_orig_path = realpath(orig, NULL);
            if(full_orig_path)
            {
                char * full_dupl_path = realpath(path, NULL);
                if(full_dupl_path)
                {
                    error(NCO"   ln -s "NCO"\"%s\" "NCO"\"%s\"\n", full_orig_path, full_dupl_path);
                    remfile(full_dupl_path);
                    if(symlink(full_orig_path ,full_dupl_path) == -1)
                    {
                        perror(YEL"WARN: "NCO"symlink:");
                    }
                    free(full_dupl_path);
                }
                free(full_orig_path);
            }
        }
    }
    break;
    case 5:
    {
        /* Exec a command on it */
        int ret = 0;
        char * tmp_opath = strsubs(orig,"'","'\"'\"'");
        char * tmp_fpath = strsubs(path,"'","'\"'\"'");
        if(tmp_opath && tmp_fpath)
        {
            const char * cmd = NULL;
            if(path)
            {
                cmd = make_cmd_ready(false,tmp_opath,tmp_fpath);
            }
            else
            {
                cmd = make_cmd_ready(true,tmp_opath,NULL);
            }
            if(cmd != NULL)
            {
                ret = system(cmd);
                if(WIFSIGNALED(ret) && (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT))
                {
                    return true;
                }
                free((char*)cmd);
                cmd = NULL;
            }
            free(tmp_opath);
            free(tmp_fpath);
        }
    }
    break;
    default:
        error(RED"ERROR: "NCO"Invalid set->mode. This is a program error :(");
        return true;
    }
    return false;
}

/* ------------------------------------------------------------- */

void init_filehandler(void)
{
    if(set->output)
    {
        char *sc_name = strdup_printf("%s.sh", set->output);
        char *lg_name = strdup_printf("%s.log",set->output);
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
                    "#rmlint was executed from: %s\n"
                    "\n"
                    "ask() {\n"
                    "cat << EOF\n"
                    "This script will delete certain files rmlint found.\n"
                    "It is highly advisable to view the script (or log) first!\n"
                    "\n"
                    "Execute this script with -d to disable this message\n"
                    "Hit enter to continue; CTRL-C to abort immediately\n"
                    "EOF\n"
                    "read dummy_var\n"
                    "}\n"
                    "\n"
                    "usage()\n"
                    "{\n"
                    "cat << EOF\n"
                    "usage: $0 options\n"
                    "\n"
                    "OPTIONS:\n",cwd);
            fprintf(get_scriptstream(),
                    "-h      Show this message\n"
                    "-d      Do not ask before running\n"
                    "-x      Keep rmlint.sh and rmlint.log\n"
                    "EOF\n"
                    "}\n"
                    "\n"
                    "DO_REMOVE=\n"
                    "DO_ASK=\n"
                    "\n"
                    "while getopts “dhx” OPTION\n"
                    "do\n"
                    "  case $OPTION in\n"
                    "     h)\n"
                    "       usage\n"
                    "       exit 1\n"
                    "       ;;\n"
                    "     d)\n"
                    "       DO_ASK=false\n"
                    "       ;;\n"
                    "     x)\n"
                    "       DO_REMOVE=false\n"
                    "       ;;\n"
                    "  esac\n"
                    "done\n"
                    "\n"
                    "if [ -z $DO_ASK ]\n"
                    "then\n"
                    "  usage\n"
                    "  ask \n"
                    "fi\n");
            fprintf(get_logstream(),"#This file was autowritten by 'rmlint'\n");
            fprintf(get_logstream(),"#rmlint was executed from: %s\n",cwd);
            fprintf(get_logstream(), "#\n# Entries are listed like this: \n");
            fprintf(get_logstream(), "# dupflag | md5sum | path | size | devID | inode\n");
            fprintf(get_logstream(), "# -------------------------------------------\n");
            fprintf(get_logstream(), "# dupflag : What type of lint found:\n");
            fprintf(get_logstream(), "#           BLNK: Bad link pointing nowhere\n"
                    "#           OTMP: Old tmp data (e.g: test.txt~)\n"
                    "#           BASE: Double basename\n"
                    "#           EDIR: Empty directory\n");
            fprintf(get_logstream(),
                    "#           BLNK: Bad link pointing nowhere\n"
                    "#           JNKD: Dirname containg one char of a user defined string\n"
                    "#           JNKF: Filename containg one char of a user defined string\n"
                    "#           ZERO: Empty file\n"
                    "#           NBIN: Nonstripped binary\n"
                    "#           BGID: File with Bad GroupID\n"
                    "#           BUID: File with Bad UserID\n"
                    "#           ORIG: File that has a duplicate, but supposed to be a original\n"
                    "#           DUPL: File that is supposed to be a duplicate\n"
                    "#\n");
            fprintf(get_logstream(), "# md5sum  : The md5-checksum of the file (not equal with output of `md5sum`, because only parts are read!)\n");
            fprintf(get_logstream(), "# path    : The full path to the found file\n");
            fprintf(get_logstream(), "# size    : total size in byte as a decimal integer\n");
            fprintf(get_logstream(), "# devID   : The ID of the device where the file is located\n");
            fprintf(get_logstream(), "# inode   : The Inode of the file (see man 2 stat)\n");
            fprintf(get_logstream(), "# The '//' inbetween each word is the seperator.\n");
            if(cwd)
            {
                free(cwd);
            }
        }
        else
        {
            perror(NULL);
        }
        fflush(script_out);
        fflush(log_out);
        if(sc_name) free(sc_name);
        if(lg_name) free(lg_name);
    }
}

/* ------------------------------------------------------------- */


#define NOT_DUP_FLAGGED(ptr) !(ptr->dupflag <= false)
bool print_newline = true;


bool process_doop_groop(file_group *grp)
{   /* This does the final processing on a dupe group. All required   */
	/* comparisons have been done (including paranoid if required) so */
	/* now it's just a matter of deciding which originals to keep and */
	/* which files to delete.                                         */
	    
    /* If a specific directory is specified to have the 'originals':  */
    /* Find the (first) element being in this directory and swap it with the first */
    /* --> First one is the original otherwise */
    
    
	lint_t *i = grp->grp_stp;
	bool tagged_original = false;
	lint_t *original;
	
	while(i)
	{
		if ( ( (i->in_ppath) && (set->keep_all_originals) ) ||
			 /* tag all originals*/
		     ( (i->in_ppath) && (!tagged_original) ) )
			/*tag first original only*/
		{
			i->filter = false;
			if (!tagged_original)
			{
				tagged_original = true;
				original = i;
			}
		}
        else
        {
			/*tag as duplicate*/
			i->filter = true;
		}
        i = i->next;
    }
    if (!tagged_original)
    {
		/* tag first file as the original*/
		grp->grp_stp->filter = false;
		original = grp->grp_stp;
	}
	
    /* Make sure no group is printed / logged at the same time (== chaos) */
    pthread_mutex_lock(&mutex_printage);
    
    /* Now do the actual printout.. */
    i = grp->grp_stp;
    while(i)
    {
        if(!i->filter)
        {   /* original(s) of a duplicate set*/
            if((set->mode == 1 || set->mode == 4 || (set->mode == 5 && set->cmd_orig == NULL && set->cmd_path == NULL)) && set->verbosity > 1)
            {
                if(print_newline)
                {
                    warning("\n");
                    print_newline = false;
                }
                if(set->mode != 4)
                {
                    error(GRE"   ls "NCO"%s\n",i->path);
                }
            }
            write_to_log(i, true, NULL);
            handle_item(NULL, i);
            /* Subtract size of the original , so we can gather it later */
            grp->size -= i->fsize;
        }
        i = i->next;
    }
    i = grp->grp_stp;
    while(i)
    {
        if(i->filter)
        {   /* duplicates(s) of a duplicate set*/
			if(set->mode == 1 || (set->mode == 5 && !set->cmd_orig && !set->cmd_path))
			{
				if(set->paranoid)
				{
					/* If byte by byte was succesful print a blue "x" */
					warning(BLU"   %-1s "NCO,"rm");
				}
				else
				{
					warning(YEL"   %-1s "NCO,"rm");
				}
				if(set->verbosity > 1)
				{
					error("%s\n",i->path);
				}
				else
				{
					error("   rm %s\n",i->path);
				}
			}
			write_to_log(i, false, original);
			set_dupcounter(get_dupcounter()+1);
			add_total_lint(i->fsize);
			if(handle_item(i,original))
			{
				pthread_mutex_unlock(&mutex_printage);
				return true;
			}
		}
		i = i->next;
	}
	pthread_mutex_unlock(&mutex_printage);
    return false;
}

/* ------------------------------------------------------------- */
