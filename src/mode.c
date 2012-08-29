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

static int paranoid(const lint_t *p1, const lint_t *p2)
{
    int result = 0,file_a,file_b;
    char * file_map_a, * file_map_b;
    if(!p1 || !p2)
        return 0;
    if(p1->fsize != p2->fsize)
        return 0;
    if((file_a = open(p1->path, MD5_FILE_FLAGS)) == -1)
    {
        perror(RED"ERROR:"NCO"sys:open()");
        return 0;
    }
    if((file_b = open(p2->path, MD5_FILE_FLAGS)) == -1)
    {
        perror(RED"ERROR:"NCO"sys:open()");
        return 0;
    }
    if(p1->fsize < MMAP_LIMIT && p1->fsize > MD5_IO_BLOCKSIZE>>1)
    {
        file_map_a = mmap(NULL, (size_t)p1->fsize, PROT_READ, MAP_PRIVATE, file_a, 0);
        if(file_map_a != MAP_FAILED)
        {
            if(madvise(file_map_a,p1->fsize, MADV_SEQUENTIAL) == -1)
                perror("madvise");
            file_map_b = mmap(NULL, (size_t)p2->fsize, PROT_READ, MAP_PRIVATE, file_a, 0);
            if(file_map_b != MAP_FAILED)
            {
                if(madvise(file_map_b,p2->fsize, MADV_SEQUENTIAL) == -1)
                    perror("madvise");
                result = !memcmp(file_map_a, file_map_b, p1->fsize);
                munmap(file_map_b,p1->fsize);
            }
            else
            {
                perror("paranoid->mmap");
                result = 0;
            }
            munmap(file_map_a,p1->fsize);
        }
        else
        {
            perror("paranoid->mmap");
            result = 0;
        }
    }
    else /* use fread() */
    {
        nuint_t blocksize = MD5_IO_BLOCKSIZE/2;
        char * read_buf_a = alloca(blocksize);
        char * read_buf_b = alloca(blocksize);
        int read_a=-1,read_b=-1;
        while(read_a && read_b)
        {
            if((read_a=read(file_a,read_buf_a,blocksize) == -1))
            {
                result = 0;
                break;
            }
            if((read_b=read(file_b,read_buf_b,blocksize) == -1))
            {
                result = 0;
                break;
            }
            if(read_a == read_b)
            {
                if((result = !memcmp(read_buf_a,read_buf_b,read_a)) == 0)
                {
                    break;
                }
            }
            else
            {
                result = 0;
                break;
            }
        }
    }
    if(close(file_a) == -1)
        perror(RED"ERROR:"NCO"close()");
    if(close(file_b) == -1)
        perror(RED"ERROR:"NCO"close()");
    return result;
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
        char *fpath = canonicalize_file_name(file->path);
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
            script_print(_sd_("strip -s '%s' # binary with debugsymbols.\n", fpath));
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
                char *opath = canonicalize_file_name(p_to_orig->path);
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
            char * full_orig_path = canonicalize_file_name(orig);
            if(full_orig_path)
            {
                char * full_dupl_path = canonicalize_file_name(path);
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

/* Compare criteria of checksums */
static int cmp_f(lint_t *a, lint_t *b)
{
    int i, fp_i, x;
    int is_empty[2][3] = { {1,1,1}, {1,1,1} };
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
            warning(YEL"WARN: "NCO"Refusing file with empty checksum and empty fingerprint.\n");
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------- */

#define NOT_DUP_FLAGGED(ptr) !(ptr->dupflag <= false)
bool print_newline = true;

bool findmatches(file_group *grp)
{
    lint_t *i = grp->grp_stp, *j;
    int class_ctr = -1;
    if(i == NULL)
    {
        return false;
    }
    warning(NCO);
    while(i)
    {
        if(NOT_DUP_FLAGGED(i))
        {
            j=i->next;
            while(j)
            {
                if(NOT_DUP_FLAGGED(j))
                {
                    if((!cmp_f(i,j))              &&     /* Same checksum?                            */
                            (i->fsize == j->fsize)     &&     /* Same size? (double check, you never know) */
                            ((set->paranoid)?paranoid(i,j):1) /* If we're bothering with paranoid users - Take the gatling! */
                      )
                    {
                        /* i twin of j - question of origin is decided later */
                        i->dupflag = class_ctr;
                        i->filter  = false;
                        j->dupflag = class_ctr;
                        j->filter = true;
                    }
                }
                j = j->next;
            }
            class_ctr--;
            /* Now remove if $i didn't match in list */
            if(NOT_DUP_FLAGGED(i))
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
    if(grp->len == 0)
    {
        return false;
    }
    /* If a specific directory is specified to have the 'originals':  */
    /* Find the (first) element being in this directory and swap it with the first */
    /* --> First one is the original otherwise */
    if(set->preferID != -1)
    {
        char * pPath  = set->paths[set->preferID];
        size_t length = strlen(pPath);
        i = grp->grp_stp;
        while(i)
        {
            if(!strncmp(pPath,i->path,length) && i->filter)
            {
                j = grp->grp_stp;
                while(j)
                {
                    if(j->dupflag == i->dupflag && i != j)
                    {
                        /* Swap as long as possible */
                        i->filter = false;
                        j->filter = true;
                    }
                    j = j->next;
                }
            }
            i = i->next;
        }
    }
    /* Make sure no group is printed / logged at the same time (== chaos) */
    pthread_mutex_lock(&mutex_printage);
    i = grp->grp_stp;
    /* Now do the actual printout.. */
    while(i)
    {
        /* not a duplicate */
        if(!i->filter)
        {
            lint_t * from = grp->grp_stp;
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
            /* print all duplicates belonging to this orig. */
            while(from)
            {
                if(from->dupflag == i->dupflag && from != i)
                {
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
                            error("%s\n",from->path);
                        }
                        else
                        {
                            error("   rm %s\n",from->path);
                        }
                    }
                    write_to_log(from, false, i);
                    set_dupcounter(get_dupcounter()+1);
                    if(handle_item(from,i))
                    {
                        pthread_mutex_unlock(&mutex_printage);
                        return true;
                    }
                }
                from = from->next;
            }
        }
        i = i->next;
    }
    pthread_mutex_unlock(&mutex_printage);
    return false;
}

/* ------------------------------------------------------------- */
