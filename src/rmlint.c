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
   rmlint.c:
   1) Methods to parse arguments and set vars accordingly
   */
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/stat.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <dirent.h>
#include <fcntl.h>
#include <setjmp.h>

#include "rmlint.h"
#include "mode.h"
#include "md5.h"

/* If more that one path given increment */
int  cpindex = 0;

/* Control flags */
bool do_exit = false,
     use_cwd = false,
     jmp_set = false,
     ex_stat = false,
     abort_n = true;

nuint_t total_files = 0;

/* Default commands */
const char *script_name = "rmlint";

/* If die() is called rmlint will jump back to the end of main
 * rmlint does NOT call exit() or abort() on it's own - so you
 * may use it's methods also in your own programs - see main()
 * It still has various problems with calling rmlint_main() twice... :-( //ToDo
 * */
jmp_buf place;

/* ------------------------------------------------------------- */

void rmlint_init(void)
{
    do_exit = false;
    use_cwd = false;
    jmp_set = false;
    ex_stat = false;
    abort_n = true;
    total_files = 0;
}

/* ------------------------------------------------------------- */

nuint_t get_totalfiles(void)
{
    return total_files;
}

/* ------------------------------------------------------------- */

/* Don't forget to free retvalue */
char *strdup_printf(const char *format, ...)
{
    va_list arg;
    char *tmp;
    va_start(arg, format);
    if(vasprintf(&tmp, format, arg) == -1)
    {
        return NULL;
    }
    va_end(arg);
    return tmp;
}

/* ------------------------------------------------------------- */

/* Make chained calls possible */
char * rm_col(char * string, const char * COL)
{
    char * new = strsubs(string,COL,NULL);
    if(string)
    {
        free(string);
        string = NULL;
    }
    return new;
}

/* ------------------------------------------------------------- */

void msg_macro_print(FILE * stream, const char * string)
{
    if(stream && string)
    {
        char * tmp = strdup(string);
        if(!set->color)
        {
            tmp = rm_col(rm_col(rm_col(rm_col(rm_col(tmp,RED),YEL),GRE),BLU),NCO);
        }
        if(stream != NULL && tmp != NULL)
        {
            fprintf(stream,"%s",tmp);
            fflush(stream);
            free(tmp);
            tmp = NULL;
        }
    }
}

/* ------------------------------------------------------------- */

/** Messaging **/
void error(const char* format, ...)
{
    if(set->verbosity > 0 && set->verbosity < 4)
    {
        char * tmp;
        va_list args;
        va_start(args, format);
        if(vasprintf(&tmp, format, args) == -1)
        {
            return;
        }
        /* print, respect -B */
        msg_macro_print(stdout,tmp);
        va_end(args);
        if(tmp)
        {
            free(tmp);
        }
    }
}

/* ------------------------------------------------------------- */

void warning(const char* format, ...)
{
    if(set->verbosity > 1 && set->verbosity < 4)
    {
        char * tmp;
        va_list args;
        va_start(args, format);
        if(vasprintf(&tmp, format, args) == -1)
        {
            return;
        }
        msg_macro_print(stderr,tmp);
        va_end(args);
        if(tmp)
        {
            free(tmp);
        }
    }
}

/* ------------------------------------------------------------- */

void info(const char* format, ...)
{
    if((set->verbosity > 2 && set->verbosity < 4) || set->verbosity == 6)
    {
        char * tmp;
        va_list args;
        va_start(args, format);
        if(vasprintf(&tmp, format, args) == -1)
        {
            return;
        }
        msg_macro_print(stdout,tmp);
        va_end(args);
        if(tmp)
        {
            free(tmp);
        }
    }
}

/* ------------------------------------------------------------- */

/* The nightmare of every secure program :))
 * this is used twice; 1x with a variable format..
 * Please gimme a note if I forgot to check sth. there. ;)
 */
int systemf(const char* format, ...)
{
    va_list arg;
    char *cmd;
    int ret = 0;
    va_start(arg, format);
    if(vasprintf(&cmd, format, arg) == -1)
        return -1;
    va_end(arg);
    /* Now execute a shell command */
    if((ret = system(cmd)) == -1)
    {
        perror("systemf(const char* format, ...)");
    }
    if(cmd) free(cmd);
    return ret;
}

/* ------------------------------------------------------------- */

/* Version string */
static void print_version(bool exit)
{
    fprintf(stderr, "Version 1.0.6b compiled: [%s]-[%s]\n",__DATE__,__TIME__);
    fprintf(stderr, "Author Christopher Pahl; Report bugs to <sahib@online.de>\n");
    fprintf(stderr, "or use the Issuetracker at https://github.com/sahib/rmlint/issues\n");
    if(exit)
    {
        die(0);
    }
}

/* ------------------------------------------------------------- */


/* Help text */
static void print_help(void)
{
    fprintf(stderr, "Syntax: rmlint [TargetDir[s]] [File[s]] [Options]\n");
    fprintf(stderr, "\nGeneral options:\n\n"
            "\t-t --threads <t>\tSet the number of threads to <t> (Default: 4; May have only minor effect)\n"
            "\t-p --paranoid\t\tDo a byte-by-byte comparasion additionally for duplicates. (Slow!) (Default: No.)\n"
            "\t-j --junk <junkchars>\tSearch for files having one letter of <junkchars> in their name. (Useful for finding names like 'Q@^3!'')\n"
           );
    fprintf(stderr,"\t-z --limit\tMinimum and maximum size of files in Bytes; example: \"20000;-1\" (Default: \"-1;-1\")");
    fprintf(stderr, "\t-a --nonstripped\tSearch for nonstripped binaries (Binaries with debugsymbols) (Slow) (Default: No.)\n"
            "\t-n --namecluster\tSearch for files with the same name (do nothing but printing them) (Default: No.)\n"
            "\t-k --emptyfiles\t\tSearch for empty files (Default: Yes, use -K to disable)\n"
            "\t-y --emptydirs\t\tSearch for empty dirs (Default: Yes, use -Y to disable)\n"
            "\t-x --oldtmp <sec>\tSearch for files with a '~'/'.swp' suffix being min. <sec> seconds older than the corresponding file without the '~'/'.swp'; (Default: 60)\n");
    fprintf(stderr,"\t\t\t\tNegative values are possible, what will find data younger than <sec>\n"
            "\t-u --dups\t\tSearch for duplicates (Default: Yes.)\n"
            "\t-l --badids\t\tSearch for files with bad IDs and GIDs (Default: Yes.)\n"
           );
    fprintf(stderr, "\t-d --maxdepth <depth>\tOnly recurse up to this depth. (default: inf)\n"
            "\t-f --followlinks\tWether links are followed (None is reported twice, set to false if hardlinks are counted as duplicates) (Default: no)\n"
            "\t-s --samepart\t\tNever cross mountpoints, stay on the same partition. (Default: No.)\n"
            "\t-G --hidden\t\tAlso search through hidden files / directories (Default: No.)\n"
            "\t-m --mode <mode>\tTell rmlint how to deal with the duplicates it finds (only on duplicates!).:\n"
           );
    fprintf(stderr, "\n\t\t\t\tWhere modes are:\n\n"
            "\t\t\t\tlist  - Only list found files and exit.\n"
            "\t\t\t\tlink  - Replace file with a symlink to original.\n"
            "\t\t\t\task   - Ask for each file what to do.\n"
            "\t\t\t\tnoask - Full removal without asking.\n"
            "\t\t\t\tcmd   - Takes the command given by -c/-C and executes it on the duplicate/original.\n"
            "\t\t\t\tDefault: list\n\n"
            "\t-c --cmd_dup  <cmd>\tExecute a shellcommand on found duplicates when used with '-m cmd'\n");
    fprintf(stderr,"\t-C --cmd_orig <cmd>\tExecute a shellcommand on original files when used with '-m cmd'\n\n"
            "\t\t\t\tExample: rmlint testdir -m cmd -C \"ls '<orig>'\" -c \"ls -lasi '<dupl>' #== '<orig>'\" -v5\n"
            "\t\t\t\tThis would print all found files (both duplicates and originals via the 'ls' utility\n");
    fprintf(stderr,"\t\t\t\tThe <dupl> expands to the found duplicate, <orig> to the original.\n\n"
            "\t\t\t\tNote: If '-m cmd' is not given, rmlint's deault commands are replaced with the ones from -cC\n"
            "\t\t\t\t      This is especially useful with -v5, so you can pipe your commands to sh in realtime.\n"
           );
    fprintf(stderr, "Regex options:\n\n"
            "\t-r --fregex <pat>\tChecks filenames against the pattern <pat>\n"
            "\t-R --dregex <pat>\tChecks dirnames against the pattern <pat>\n"
            "\t-i --invmatch\t\tInvert match - Only investigate when not containing <pat> (Default: No.)\n"
            "\t-e --matchcase\t\tMatches case of paths (Default: No.)\n");
    fprintf(stderr, "\nMisc options:\n\n"
            "\t-h --help\t\tPrints this text and exits\n"
            "\t-o --output [<o>]\tOutputs logfile to <o>. The <o> argument is optional, specify none to write no log.\n"
            "\t\t\t\tExamples:\n\n\t\t\t\t-o => No Logfile\n\t\t\t\t-o\"la la.txt\" => Logfile to \"la la.txt\"\n\n\t\t\t\tNote the missing whitespace. (Default \"rmlint\")\n\n");
    fprintf(stderr,"\t-v --verbosity <v>\tSets the verbosity level to <v>\n"
            "\t\t\t\tWhere:\n"
            "\t\t\t\t0 prints nothing\n"
            "\t\t\t\t1 prints only errors and results\n"
            "\t\t\t\t2 + prints warning\n"
            "\t\t\t\t3 + info and statistics\n"
            "\t\t\t\t4 + dumps log to stdout (still writes to HD)\n"
            "\t\t\t\t5 + dumps script to stdout (still writes to HD)\n"
            "\t\t\t\t6 + rdfind-like informative output\n\n"
            "\t\t\t\tDefault is 2.\n"
            "\t\t\t\tUse 6 to get an idea what's happening internally, 1 to get raw output without colors, 4 for liveparsing purpose.\n"
           );
    fprintf(stderr,"\t-B --no-color\t\tDon't use colored output.\n\n");
    fprintf(stderr,"Additionally, the options b,p,f,s,e,g,o,i,c,n,a,y,x,u have a uppercase option (B,O,G,P,F,S,E,I,C,N,A,Y,X,U) that inverse it's effect.\n"
            "The corresponding long options have a \"no-\" prefix. E.g.: --no-emptydirs\n\n"
           );
    print_version(false);
    fprintf(stderr, "\nLicensed under the terms of the GPLv3 - See COPYRIGHT for more information\n");
    fprintf(stderr, "See the manpage, README or <http://sahib.github.com/rmlint/> for more information.\n");
    die(0);
}

/* ------------------------------------------------------------- */

/* Options not specified by commandline get a default option - this called before rmlint_parse_arguments */
void rmlint_set_default_settings(rmlint_settings *pset)
{
    set = pset;
    pset->mode      =  1;       /* list only    */
    pset->casematch     =  0;       /* Insensitive  */
    pset->invmatch  =  0;       /* Normal mode  */
    pset->paranoid      =  0;       /* dont be bush */
    pset->depth         =  0;       /* inf depth    */
    pset->followlinks   =  0;       /* fol. link    */
    pset->threads       = 16;           /* Quad*quad.   */
    pset->verbosity     =  2;       /* Most relev.  */
    pset->samepart      =  0;       /* Stay parted  */
    pset->paths         =  NULL;        /* Startnode    */
    pset->dpattern      =  NULL;        /* DRegpattern  */
    pset->fpattern  =  NULL;        /* FRegPattern  */
    pset->cmd_path      =  NULL;        /* Cmd,if used  */
    pset->cmd_orig      =  NULL;        /* Origcmd, -"- */
    pset->junk_chars    =  NULL;        /* You have to set this   */
    pset->oldtmpdata    = 60;           /* Remove 1min old buffers */
    pset->doldtmp       = true;         /* Remove 1min old buffers */
    pset->preferID      = -1;
    pset->ignore_hidden = 1;
    pset->findemptydirs = 1;
    pset->namecluster   = 0;
    pset->nonstripped   = 0;
    pset->searchdup     = 1;
    pset->color         = 1;
    pset->findbadids    = 1;
    pset->output        = (char*)script_name;
    pset->minsize       = -1;
    pset->maxsize       = -1;
    pset->listemptyfiles = 1;
    /* There is no cmdline option for this one    *
     * It controls wether 'other lint' is also    *
     * investigated to be replicas of other files */
    pset->collide = 0;
}

/* ------------------------------------------------------------- */

void parse_limit_sizes(char * limit_string)
{
    char * ptr = limit_string;
    if(ptr != NULL)
    {
        char * semicol = strchr(ptr,';');
        if(semicol != NULL)
        {
            semicol[0] = '\0';
            semicol++;
            set->maxsize = strtol(semicol,NULL,10);
        }
        set->minsize = strtol(ptr,NULL,10);
    }
}


/* ------------------------------------------------------------- */

/* Check if this is the 'preferred' dir */
int check_if_preferred(const char * dir)
{
    if(dir != NULL)
    {
        size_t length = strlen(dir);
        if(length >= 2 && dir[0] == '/' && dir[1] == '/')
            return 1;
    }
    return 0;
}

/* Parse the commandline and set arguments in 'settings' (glob. var accordingly) */
char rmlint_parse_arguments(int argc, char **argv, rmlint_settings *sets)
{
    int c,lp=0;
    rmlint_init();
    while(1)
    {
        static struct option long_options[] =
        {
            {"threads",        required_argument, 0, 't'},
            {"dregex",         required_argument, 0, 'R'},
            {"fregex",         required_argument, 0, 'r'},
            {"mode",           required_argument, 0, 'm'},
            {"maxdepth",       required_argument, 0, 'd'},
            {"cmd_dup",        required_argument, 0, 'c'},
            {"cmd_orig",       required_argument, 0, 'C'},
            {"junk",           required_argument, 0, 'j'},
            {"verbosity",      required_argument, 0, 'v'},
            {"oldtmp",         required_argument, 0, 'x'},
            {"limit",          required_argument, 0, 'z'},
            {"output",         optional_argument, 0, 'o'},
            {"emptyfiles",     no_argument,       0, 'k'},
            {"no-emptyfiles",  no_argument,       0, 'k'},
            {"emptydirs",      no_argument,       0, 'y'},
            {"color",          no_argument,       0, 'b'},
            {"no-color",       no_argument,       0, 'B'},
            {"no-emptydirs",   no_argument,       0, 'Y'},
            {"namecluster",    no_argument,       0, 'n'},
            {"no-namecluster", no_argument,       0, 'N'},
            {"nonstripped",    no_argument,       0, 'a'},
            {"no-nonstripped", no_argument,       0, 'A'},
            {"no-oldtmp",      no_argument,       0, 'X'},
            {"no-hidden",      no_argument,       0, 'g'},
            {"hidden",         no_argument,       0, 'G'},
            {"badids",         no_argument,       0, 'l'},
            {"no-badids",      no_argument,       0, 'L'},
            {"dups",           no_argument,       0, 'u'},
            {"no-dups",        no_argument,       0, 'U'},
            {"matchcase",      no_argument,       0, 'e'},
            {"ignorecase",     no_argument,       0, 'E'},
            {"followlinks",    no_argument,       0, 'f'},
            {"ignorelinks",    no_argument,       0, 'F'},
            {"invertmatch",    no_argument,       0, 'i'},
            {"normalmatch",    no_argument,       0, 'I'},
            {"samepart",       no_argument,       0, 's'},
            {"allpart",        no_argument,       0, 'S'},
            {"paranoid",       no_argument,       0, 'p'},
            {"naive",          no_argument,       0, 'P'},
            {"help",           no_argument,       0, 'h'},
            {"version",        no_argument,       0, 'V'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;
        c = getopt_long(argc, argv, "m:R:r:o::j:kKbBuUVyhYnNaAx:XgGpPlLfFeEsSiIc:C:t:d:v:z:",long_options, &option_index);
        /* Detect the end of the options. */
        if(c == -1)
        {
            break;
        }
        switch(c)
        {
        case '?':
            return 0;
        case 't':
            sets->threads = atoi(optarg);
            if(!sets->threads || sets->threads < 0)
            {
                sets->threads = 8;
            }
            break;
        case 'k':
            sets->listemptyfiles = 1;
            break;
        case 'K':
            sets->listemptyfiles = 0;
            break;
        case 'f':
            sets->followlinks = 1;
            break;
        case 'F':
            sets->followlinks = 0;
            break;
        case 'u':
            sets->searchdup = 1;
            break;
        case 'U':
            sets->searchdup = 0;
            break;
        case 'n':
            sets->namecluster = 1;
            break;
        case 'N':
            sets->namecluster = 0;
            break;
        case 'b':
            sets->color = 1;
            break;
        case 'B':
            sets->color = 0;
            break;
        case 'V':
            print_version(true);
            break;
        case 'h':
            print_help();
            break;
        case 'j':
            sets->junk_chars = optarg;
            break;
        case 'y':
            sets->findemptydirs = 1;
            break;
        case 'Y':
            sets->findemptydirs = 0;
            break;
        case 'a':
            sets->nonstripped = 1;
            break;
        case 'A':
            sets->nonstripped = 0;
            break;
        case 'x':
            sets->oldtmpdata = atoi(optarg);
            break;
        case 'X':
            sets->doldtmp = false;
            break;
        case 'o':
            sets->output = optarg;
            break;
        case 'c':
            sets->cmd_path = optarg;
            break;
        case 'C':
            sets->cmd_orig = optarg;
            break;
        case 'g':
            sets->ignore_hidden = 1;
            break;
        case 'G':
            sets->ignore_hidden = 0;
            break;
        case 'v':
            sets->verbosity = atoi(optarg);
            break;
        case 'i':
            sets->invmatch = 1;
            break;
        case 'I':
            sets->invmatch = 0;
            break;
        case 's':
            sets->samepart = 1;
            break;
        case 'S':
            sets->samepart = 0;
            break;
        case 'e':
            sets->casematch = 1;
            break;
        case 'E':
            sets->casematch = 0;
            break;
        case 'd':
            sets->depth = ABS(atoi(optarg));
            break;
        case 'r':
            sets->fpattern = optarg;
            break;
        case 'R':
            sets->dpattern = optarg;
            break;
        case 'p':
            sets->paranoid = 1;
            break;
        case 'z':
            parse_limit_sizes(optarg);
            break;
        case 'l':
            sets->findbadids = true;
            break;
        case 'L':
            sets->findbadids = false;
            break;
        case 'P':
            sets->paranoid = 0;
            break;
        case 'm':
            sets->mode = 0;
            if(!strcasecmp(optarg, "list"))
            {
                sets->mode = 1;
            }
            if(!strcasecmp(optarg, "ask"))
            {
                sets->mode = 2;
            }
            if(!strcasecmp(optarg, "noask"))
            {
                sets->mode = 3;
            }
            if(!strcasecmp(optarg, "link"))
            {
                sets->mode = 4;
            }
            if(!strcasecmp(optarg, "cmd"))
            {
                sets->mode = 5;
            }
            if(!sets->mode)
            {
                error(YEL"FATAL: "NCO"Invalid value for --mode [-m]\n");
                error("       Available modes are: ask | list | link | noask | cmd\n");
                die(0);
                return 0;
            }
            break;
        default:
            return 0;
        }
    }
    /* Check the directory to be valid */
    while(optind < argc)
    {
        char * theDir = argv[optind];
        int p,isPref = check_if_preferred(theDir);
        if(isPref)
        {
            theDir += 2;
        }
        p = open(theDir,O_RDONLY);
        if(p == -1)
        {
            error(YEL"FATAL: "NCO"Can't open directory \"%s\": %s\n", theDir, strerror(errno));
            return 0;
        }
        else
        {
            close(p);
            sets->paths   = realloc(sets->paths,sizeof(char*)*(lp+2));
            if(isPref) sets->preferID = lp;
            sets->paths[lp++] = theDir;
            sets->paths[lp  ] = NULL;
        }
        optind++;
    }
    if(lp == 0)
    {
        /* Still no path set? - use `pwd` */
        sets->paths = malloc(sizeof(char*)*2);
        sets->paths[0] = getcwd(NULL,0);
        sets->paths[1] = NULL;
        if(!sets->paths[0])
        {
            error(YEL"FATAL: "NCO"Cannot get working directory: "YEL"%s\n"NCO, strerror(errno));
            error("       Are you maybe in a dir that just had been removed?\n");
            if(sets->paths)
            {
                free(sets->paths);
            }
            return 0;
        }
        use_cwd = true;
    }
    return 1;
}

/* ------------------------------------------------------------- */

/* User  may specify in -cC a command that get's excuted on every hit - check for being a safe one */
static void check_cmd(const char *cmd)
{
    int i = 0, ps = 0;
    int len = strlen(cmd);
    for(; i < len; i++)
    {
        if(cmd[i] == '%' && i+1 != len)
        {
            if(cmd[i+1] != '%')
            {
                ps++;
                continue;
            }
        }
    }
    if(ps > 0)
    {
        puts(YEL"FATAL: "NCO"--command [-cC]: printfstyle markups (e.g. %s) are not allowed!");
        puts(YEL"       "NCO"                 Escape '%' with '%%' to get around.");
        die(0);
    }
}

/* ------------------------------------------------------------- */

/* This is only used to pass the current dir to eval_file */
int  get_cpindex(void)
{
    return cpindex;
}

/* ------------------------------------------------------------- */

/* exit and return to calling method */
void die(int status)
{
    /* Free mem */
    if(use_cwd)
    {
        if(set->paths[0])
        {
            free(set->paths[0]);
        }
    }
    if(set->paths)
    {
        free(set->paths);
    }
    if(status)
    {
        info("Abnormal exit\n");
    }
    /* Close logfile */
    if(get_logstream())
    {
        fclose(get_logstream());
    }
    /* Close scriptfile */
    if(get_scriptstream())
    {
        fprintf(get_scriptstream(),
                "                      \n"
                "if [[ -z $DO_REMOVE ]]\n"
                "then                  \n"
                "  rm -f rmlint.log    \n"
                "  rm -f rmlint.sh     \n"
                "fi                    \n"
               );
        fclose(get_scriptstream());
    }
    /* Prepare to jump to return */
    do_exit = true;
    ex_stat = status;
    if(jmp_set)
    {
        longjmp(place,status);
    }
    if(abort_n)
    {
        exit(status);
    }
}

/* ------------------------------------------------------------- */

/* Sort criteria for sizesort */
static long cmp_sz(lint_t *a, lint_t *b)
{
    return a->fsize - b->fsize;
}

/* ------------------------------------------------------------- */

/* Actual entry point */
int rmlint_main(void)
{
    /* Used only for infomessage */
    total_files = 0;
    abort_n = false;
    if(do_exit != true)
    {
        /* Init all modules that use global variables.. */
        if(!use_cwd)
        {
            rmlint_init();
        }
        md5c_c_init();
        list_c_init();
        filt_c_init();
        mode_c_init();
    }
    /* Jump to this location on exit (with do_exit=true) */
    setjmp(place);
    jmp_set = true;
    if(do_exit != true)
    {
        if(set->mode == 5)
        {
            if(set->cmd_orig)
            {
                check_cmd(set->cmd_orig);
            }
            if(set->cmd_path)
            {
                check_cmd(set->cmd_path);
            }
        }
        /* Open logfile */
        init_filehandler();
        /* Warn if started with sudo */
        if(!access("/bin/ls",R_OK|W_OK))
        {
            warning(YEL"WARN: "NCO"You're running rmlint with privileged rights - \n");
            warning("      Take care of what you're doing!\n\n");
        }
        /* Count files and do some init  */
        while(set->paths[cpindex] != NULL)
        {
            DIR *p = opendir(set->paths[cpindex]);
            if(p == NULL && errno == ENOTDIR)
            {
                /* The path is a file */
                struct stat buf;
                if(stat(set->paths[cpindex],&buf) == -1)
                {
                    continue;
                }
                list_append(set->paths[cpindex],(nuint_t)buf.st_size,buf.st_dev,buf.st_ino, true);
                total_files++;
            }
            else
            {
                /* The path points to a dir - recurse it! */
                info("Now scanning "YEL"\"%s\""NCO"..",set->paths[cpindex]);
                total_files += recurse_dir(set->paths[cpindex]);
                info(" done.\n");
                closedir(p);
            }
            cpindex++;
        }
        if(total_files < 2)
        {
            warning("No files in cache to search through => No duplicates.\n");
            die(0);
        }
        info("Now in total "YEL"%ld useable file(s)"NCO" in cache.\n", total_files);
        if(set->threads > total_files)
        {
            set->threads = total_files;
        }
        /* Till this point the list is unsorted
         * The filter alorithms requires the list to be size-sorted,
         * so it can easily filter unique sizes, and build "groups"
         * */
        info("Now mergesorting list based on filesize... ");
        list_sort(list_begin(),cmp_sz);
        info("done.\n");
        info("Now finding easy lint..%c",set->verbosity > 4 ? '.' : '\n');
        /* Apply the prefilter and outsort inique sizes */
        start_processing(list_begin());
        /* Exit! */
        die(EXIT_SUCCESS);
    }
    return ex_stat;
}
