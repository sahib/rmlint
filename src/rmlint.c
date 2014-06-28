/*
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
#include <glib.h>
#include <glib/gprintf.h>

#include "rmlint.h"
#include "mode.h"
#include "md5.h"
#include "list.h"
#include "traverse.h"
#include "filter.h"
#include "linttests.h"

/* If more that one path given increment */
int  cpindex = 0;

/* Control flags */
bool do_exit = false,
     use_cwd = false,
     jmp_set = false,
     ex_stat = false,
     abort_n = true;

guint64 total_files = 0;

/* Default commands */
const char *script_name = "rmlint";

/* If die() is called rmlint will jump back to the end of main
 * rmlint does NOT call exit() or abort() on it's own - so you
 * may use it's methods also in your own programs - see main()
 * It still has various problems with calling rmlint_main() twice... :-( //ToDo
 * */
jmp_buf place;

void rmlint_init(void) {
    do_exit = false;
    use_cwd = false;
    jmp_set = false;
    ex_stat = false;
    abort_n = true;
    total_files = 0;
}

guint64 get_totalfiles(void) {
    return total_files;
}

/* Make chained calls possible */
char * rm_col(char * string, const char * COL) {
    char * new = strsubs(string,COL,NULL);
    if(string) {
        free(string);
        string = NULL;
    }
    return new;
}

void msg_macro_print(FILE * stream, const char * string) {
    if(stream && string) {
        char * tmp = strdup(string);
        // TODO: Use g_log. This is braindead.
        // if(!set->color) {
        //     // TODO: Wow. Stupidy wins again.
        //     tmp = rm_col(rm_col(rm_col(rm_col(rm_col(tmp,RED),YEL),GRE),BLU),NCO);
        // }
        if(stream != NULL && tmp != NULL) {
            fprintf(stream,"%s",tmp);
            fflush(stream);
            free(tmp);
            tmp = NULL;
        }
    }
}

/** Messaging **/
void error(const char* format, ...) {
    if(1) {
        char * tmp;
        va_list args;
        va_start(args, format);
        if(g_vasprintf(&tmp, format, args) == -1) {
            return;
        }
        /* print, respect -B */
        msg_macro_print(stdout,tmp);
        va_end(args);
        if(tmp) {
            free(tmp);
        }
    }
}

void warning(const char* format, ...) {
    //if((set->verbosity > 1) && (set->verbosity < 4)) {
    if(1) {
        char * tmp;
        va_list args;
        va_start(args, format);
        if(g_vasprintf(&tmp, format, args) == -1) {
            return;
        }
        msg_macro_print(stderr,tmp);
        va_end(args);
        if(tmp) {
            free(tmp);
        }
    }
}

void info(const char* format, ...) {
    // if(((set->verbosity > 2) && (set->verbosity < 4)) || set->verbosity == 6) {
    if(1) {
        char * tmp;
        va_list args;
        va_start(args, format);
        if(g_vasprintf(&tmp, format, args) == -1) {
            return;
        }
        msg_macro_print(stdout,tmp);
        va_end(args);
        if(tmp) {
            free(tmp);
        }
    }
}

/* ------------------------------------------------------------- */

/* The nightmare of every secure program :))
 * this is used twice; 1x with a variable format..
 * Please gimme a note if I forgot to check sth. there. ;)
 */
int systemf(const char* format, ...) {
    va_list arg;
    char *cmd;
    int ret = 0;
    va_start(arg, format);
    if(g_vasprintf(&cmd, format, arg) == -1) {
        return -1;
    }

    va_end(arg);

    /* Now execute a shell command */
    if((ret = system(cmd)) == -1) {
        perror("systemf(const char* format, ...)");
    }
    g_free(cmd);
    return ret;
}

/* ------------------------------------------------------------- */

/* Version string */
static void print_version(void) {
    fprintf(stderr, "Version 1.0.6b compiled: [%s]-[%s]\n",__DATE__,__TIME__);
    fprintf(stderr, "Author Christopher Pahl; Report bugs to <sahib@online.de>\n");
    fprintf(stderr, "or use the Issuetracker at https://github.com/sahib/rmlint/issues\n");
}

/* ------------------------------------------------------------- */

/* Help text */
static void print_help(void) {
    fprintf(stderr, "Syntax: rmlint [[//]TargetDir[s]] [File[s]] [Options]\n");
    fprintf(stderr, "\nGeneral options:\n\n"
            "\t-t --threads <t>\tSet the number of threads to <t> (Default: 4; May have only minor effect)\n"
            "\t-p --paranoid\t\tDo a byte-by-byte comparison additionally for duplicates. (Slow!) (Default: No.)\n"
            "\t-j --junk <junkchars>\tSearch for files having one letter of <junkchars> in their name. (Useful for finding names like 'Q@^3!'')\n"
           );
    fprintf(stderr,"\t-z --limit\t\tMinimum and maximum size of files in Bytes; example: \"20000;-1\" (Default: \"-1;-1\")\n");
    fprintf(stderr, "\t-a --nonstripped\tSearch for nonstripped binaries (Binaries with debugsymbols) (Slow) (Default: No.)\n"
            "\t-n --namecluster\tSearch for files with the same name (do nothing but printing them) (Default: No.)\n"
            "\t-k --emptyfiles\t\tSearch for empty files (Default: Yes, use -K to disable)\n"
            "\t-y --emptydirs\t\tSearch for empty dirs (Default: Yes, use -Y to disable)\n"
            "\t-x --oldtmp <sec>\tSearch for files with a '~'/'.swp' suffix being min. <sec> seconds older than the corresponding file without the '~'/'.swp'; (Default: 60)\n");
    fprintf(stderr,"\t\t\t\tNegative values are possible, what will find data younger than <sec>\n"
            "\t-u --dups\t\tSearch for duplicates (Default: Yes.)\n"
            "\t-l --badids\t\tSearch for files with bad IDs and GIDs (Default: Yes.)\n"
           );
    fprintf(stderr,"\t-M --mustmatchorig\tOnly look for duplicates of which one is in 'originals' paths. (Default: no)\n"
            "\t-O --keepallorig\tDon't delete any duplicates that are in 'originals' paths. (Default - just keep one)\n"
            "\t\tNote: for lint types other than duplicates, keepallorig option is ignored\n" /*TODO: does this need unifying??*/
            "\t-Q --invertorig\tPaths prefixed with // are non-originals and all other paths are originals\n"
           );
    fprintf(stderr, "\t-D --sortcriteria <criteria>\twhen selecting original, sort in order of <criteria>:\n"
            "\t\t\t\tm=keep lowest mtime (oldest), M=keep highest mtime (newest)\n"
            "\t\t\t\ta=keep first alphabetically,  A=keep last alphabetically\n"
            "\t\t\t\tp=keep first named path,      P=keep last named path\n"
            "\t\t\t\tNote: can have multiple criteria, eg \"-D am\" will choose first alphabetically; if tied then by mtime.\n"
            "\t\t\t\tNote also: original path criteria (specified using //) will always take first priority over \"-D\" options.\n"
           );
    fprintf(stderr, "\t-d --maxdepth <depth>\tOnly recurse up to this depth. (default: inf)\n"
            "\t-f --followlinks\tWhether symlinks are followed (Default: no). Note that rmlint will try to detect if symlinks\n"
            "\t\t\t\tresult in the same physical file being encountered twice and will ignore the second one.\n"
            "\t-H --findhardlinked\tFind hardlinked duplicates.  Default is to ignore duplicates which are hardlinked to each other.\n"
           );
    fprintf(stderr,"\t\t\t\tNote: currently, hardlinked files with the same basename are _always_ ignored, due to possible error with bind\n"
            "\t\t\t\tmounts pointing to the same physical file\n"
            "\t\t\t\tNote also: hardlinked duplicates _will_ be reported as part of GB count that can be freed up.\n"
            "\t-s --samepart\t\tNever cross mountpoints, stay on the same partition. (Default: off, ie DO cross mountpoints)\n"
            "\t-G --hidden\t\tAlso search through hidden files / directories (Default: No.)\n"
           );
    fprintf(stderr,"\t-m --mode <mode>\tTell rmlint how to deal with the duplicates it finds (only on duplicates!).:\n"
            "\n\t\t\t\tWhere modes are:\n\n"
            "\t\t\t\tlist\t- (RECOMMENDED) Lists found files & creates executable script to carry out\n"
            "\t\t\t\t\t actual removal (or other command given by -c/-C).\n"
            "\t\t\t\tlink\t- Replace file with a symlink to original.\n"
            "\t\t\t\task\t- Ask for each file what to do.\n"
            "\t\t\t\tnoask\t- Full removal without asking.\n"
            "\t\t\t\tcmd\t- Takes the command given by -c/-C and executes it on the duplicate/original (careful!).\n"
            "\t\t\t\tDefault:\tlist\n\n"
           );
    fprintf(stderr,"\t-c --cmd_dup  <cmd>\tExecute a shellcommand on found duplicates when used with '-m cmd'\n"
            "\t-C --cmd_orig <cmd>\tExecute a shellcommand on original files when used with '-m cmd'\n\n"
            "\t\t\t\tExample: rmlint testdir -m cmd -C \"ls '<orig>'\" -c \"ls -lasi '<dupl>' #== '<orig>'\" -v5\n"
            "\t\t\t\tThis would print all found files (both duplicates and originals via the 'ls' utility\n");
    fprintf(stderr,"\t\t\t\tThe <dupl> expands to the found duplicate, <orig> to the original.\n\n"
            "\t\t\t\tNote: If '-m cmd' is not given, rmlint's default commands are replaced with the ones from -cC\n"
            "\t\t\t\t      This is especially useful with -v5, so you can pipe your commands to sh in realtime.\n"
           );
    fprintf(stderr, "Regex options:\n\n"
            "\t-r --fregex <pat>\tChecks filenames against the pattern <pat>\n"
            "\t-R --dregex <pat>\tChecks dirnames against the pattern <pat>\n"
            "\t-i --invmatch\t\tInvert match - Only investigate when not containing <pat> (Default: No.)\n"
            "\t-e --matchcase\t\tMatches case of paths (Default: No.)\n");
    fprintf(stderr, "\nMisc options:\n\n"
            "\t-h --help\t\tPrints this text and exits\n"
            "\t-o --output <o>\tOutputs logfile to <o>.log and script to <o>.sh   (use -o \"\" or \'\' to disable output files)\n"
            "\t\t\t\tExamples:\n"
            "\t\t\t\t\t-o \"\" => No Logfile\n"
            "\t\t\t\t\t-o \"la la.txt\" => Logfile to \"la la.txt.log\"\n"
           );
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
    fprintf(stderr,"\t-B --no-color\t\tDon't use colored output.\n"
            "\t-q --skip_confirm\tSkips user confirmation of settings before running\n\n");
    fprintf(stderr,"Additionally, the options b,p,f,s,e,g,i,c,n,a,y,x,u have a uppercase option (B,G,P,F,S,E,I,C,N,A,Y,X,U) that inverse it's effect.\n"
            "The corresponding long options have a \"no-\" prefix. E.g.: --no-emptydirs\n\n"
           );
    fprintf(stderr, "\nLicensed under the terms of the GPLv3 - See COPYRIGHT for more information\n");
    fprintf(stderr, "See the manpage, README or <http://sahib.github.com/rmlint/> for more information.\n");
}

/* ------------------------------------------------------------- */

/* Options not specified by commandline get a default option - this called before rmlint_parse_arguments */
void rmlint_set_default_settings(RmSettings *pset) {
    pset->mode                  = 1;                  /* list only    */
    pset->casematch             = 0;                  /* Insensitive  */
    pset->invmatch              = 0;                  /* Normal mode  */
    pset->paranoid              = 0;                  /* dont be bush */
    pset->depth                 = 0;                  /* inf depth    */
    pset->followlinks           = 0;                  /* fol. link    */
    pset->threads               = 16;                 /* Quad*quad.   */
    pset->verbosity             = 2;                  /* Most relev.  */
    pset->samepart              = 0;                  /* Stay parted  */
    pset->paths                 = NULL;               /* Startnode    */
    pset->is_ppath              = NULL;               /* Startnode    */
    pset->dpattern              = NULL;               /* DRegpattern  */
    pset->fpattern              = NULL;               /* FRegPattern  */
    pset->cmd_path              = NULL;               /* Cmd,if used  */
    pset->cmd_orig              = NULL;               /* Origcmd, -"- */
    pset->junk_chars            = NULL;               /* You have to set this   */
    pset->oldtmpdata            = 60;                 /* Remove 1min old buffers */
    pset->doldtmp               = true;               /* Remove 1min old buffers */
    pset->ignore_hidden         = 1;
    pset->findemptydirs         = 1;
    pset->namecluster           = 0;
    pset->nonstripped           = 0;
    pset->searchdup             = 1;
    pset->color                 = 1;
    pset->findbadids            = 1;
    pset->output                = (char*)script_name;
    pset->minsize               = -1;
    pset->maxsize               = -1;
    pset->listemptyfiles        = 1;
    pset->keep_all_originals    = 0;                  /* Keep just one file from ppath "originals" indicated by "//" */
    pset->must_match_original   = 0;                  /* search for any dupes, not just ones which include ppath members*/
    pset->invert_original       = 0;                  /* search for any dupes, not just ones which include ppath members*/
    pset->find_hardlinked_dupes = 0;                  /* ignore hardlinked dupes*/
    pset->sort_criteria         = "m";                /* default ranking order for choosing originals - keep oldest mtime*/
    pset->skip_confirm          = 0;                  /* default setting is to ask user to confirm input settings at start */

    /* There is no cmdline option for this one    *
     * It controls wether 'other lint' is also    *
     * investigated to be replicas of other files */
    pset->collide = 0;

}

void parse_limit_sizes(RmSession * session, char * limit_string) {
    // TODO: Make this support multipliers, i.e. 4M for 4 * (1024 * 1024)
    char * ptr = limit_string;
    if(ptr != NULL) {
        char * semicol = strchr(ptr,';');
        if(semicol != NULL) {
            semicol[0] = '\0';
            semicol++;
            session->settings->maxsize = strtol(semicol,NULL,10);
        }
        session->settings->minsize = strtol(ptr,NULL,10);
    }
}

/* Check if this is the 'preferred' dir */
int check_if_preferred(const char * dir) {
    if(dir != NULL) {
        size_t length = strlen(dir);
        if(length >= 2 && dir[0] == '/' && dir[1] == '/')
            return 1;
    }
    return 0;
}

/* Parse the commandline and set arguments in 'settings' (glob. var accordingly) */
char rmlint_parse_arguments(int argc, char **argv, RmSession *session) {
    RmSettings * sets = session->settings;

    int c,lp=0;
    rmlint_init();

    while(1) {
        static struct option long_options[] = {
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
            {"output",         required_argument, 0, 'o'},
            {"sortcriteria",   required_argument, 0, 'D'},
            {"emptyfiles",     no_argument,       0, 'k'},
            {"no-emptyfiles",  no_argument,       0, 'K'},
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
            {"keepallorig",    no_argument,       0, 'O'},
            {"mustmatchorig",  no_argument,       0, 'M'},
            {"invertorig",     no_argument,       0, 'Q'},
            {"findhardlinked", no_argument,       0, 'H'},
            {"skip_confirm",   no_argument,       0, 'q'},
            {"help",           no_argument,       0, 'h'},
            {"version",        no_argument,       0, 'V'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;
        c = getopt_long(argc, argv, "aAbBcC:d:D:eEfFgGhHiIj:kKlLm:MnNo:OpPqQr:R:sSt:uUv:Vx:XyYz:Z",long_options, &option_index);
        /* Detect the end of the options. */
        if(c == -1) {
            break;
        }
        switch(c) {
        case '?':
            return 0;
        case 't':
            sets->threads = atoi(optarg);
            if(!sets->threads) {
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
            print_version();
            break;
        case 'h':
            print_help();
            print_version();
            die(session, EXIT_SUCCESS);
            break;
        case 'H':
            sets->find_hardlinked_dupes = 1;
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
            if (strlen(optarg) != 0)
                sets->output = optarg;
            else
                sets->output = NULL;
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
        case 'D':
            sets->sort_criteria = optarg;
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
        case 'O':
            sets->keep_all_originals = 1;
            break;
        case 'M':
            sets->must_match_original = 1;
            break;
        case 'Q':
            sets->invert_original = 1;
            break;
        case 'q':
            sets->skip_confirm = 1;
            break;
        case 'z':
            parse_limit_sizes(session, optarg);
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
            if(!strcasecmp(optarg, "list")) {
                sets->mode = 1;
            }
            if(!strcasecmp(optarg, "ask")) {
                sets->mode = 2;
            }
            if(!strcasecmp(optarg, "noask")) {
                sets->mode = 3;
            }
            if(!strcasecmp(optarg, "link")) {
                sets->mode = 4;
            }
            if(!strcasecmp(optarg, "cmd")) {
                sets->mode = 5;
            }
            if(!sets->mode) {
                error(YEL"FATAL: "NCO"Invalid value for --mode [-m]\n");
                error("       Available modes are: ask | list | link | noask | cmd\n");
                die(session, EXIT_FAILURE);
                return 0;
            }
            break;
        default:
            return 0;
        }
    }
    /* Check the directory to be valid */
    while(optind < argc) {
        char * theDir = argv[optind];
        int p;
        int isPref = check_if_preferred(theDir);
        if(isPref) {
            theDir += 2;  /* skip first two characters ie "//" */
        }
        p = open(theDir,O_RDONLY);
        if(p == -1) {
            error(YEL"FATAL: "NCO"Can't open directory \"%s\": %s\n", theDir, strerror(errno));
            return 0;
        } else {
            close(p);
            sets->paths = g_realloc(sets->paths,sizeof(char*)*(lp+2));
            sets->is_ppath = g_realloc(sets->is_ppath,sizeof(char)*(lp+1));
            sets->is_ppath[lp] = isPref;
            sets->paths[lp++] = theDir;
            sets->paths[lp] = NULL;
        }
        optind++;
    }
    if(lp == 0) {
        /* Still no path set? - use `pwd` */
        sets->paths = malloc(sizeof(char*)*2);
        sets->paths[0] = getcwd(NULL,0);
        sets->paths[1] = NULL;
        sets->is_ppath = g_malloc0(sizeof(char) * 1);
        sets->is_ppath[0] = 0;
        if(!sets->paths[0]) {
            error(YEL"FATAL: "NCO"Cannot get working directory: "YEL"%s\n"NCO, strerror(errno));
            error("       Are you maybe in a dir that just had been removed?\n");
            g_free(sets->paths);
            return 0;
        }
        use_cwd = true;
    }
    return 1;
}

/* ------------------------------------------------------------- */

/* User  may specify in -cC a command that get's excuted on every hit - check for being a safe one */
static int check_cmd(const char *cmd) {
    gboolean invalid = FALSE;
    int len = strlen(cmd);
    for(int i = 0; i < (len - 1); i++) {
        if(cmd[i] == '%' && cmd[i + 1] != '%') {
            invalid = TRUE;
            continue;
        }
    }
    if(invalid) {
        puts(YEL"FATAL: "NCO"--command [-cC]: printfstyle markups (e.g. %s) are not allowed!");
        puts(YEL"       "NCO"                 Escape '%' with '%%' to get around.");
        return 0;
    }

    return 1;
}

/* ------------------------------------------------------------- */

/* This is only used to pass the current dir to eval_file */
int  get_cpindex(void) {
    return cpindex;
}

/* ------------------------------------------------------------- */

/* exit and return to calling method */
void die(RmSession *session, int status) {
    RmSettings * sets = session->settings;

    /* Free mem */
    if(use_cwd) {
        g_free(sets->paths[0]);
    }

    g_free(sets->paths);
    g_free(sets->is_ppath);

    if(status) {
        info("Abnormal exit\n");
    }
    /* Close logfile */
    if(get_logstream()) {
        fclose(get_logstream());
    }
    /* Close scriptfile */
    if(get_scriptstream()) {
        fprintf(get_scriptstream(),
                "                      \n"
                "if [ -z $DO_REMOVE ]  \n"
                "then                  \n"
                "  rm -f rmlint.log    \n"  /*TODO: fix this to match "-o" command line options*/
                "  rm -f rmlint.sh     \n"
                "fi                    \n"
               );
        fclose(get_scriptstream());
    }

    /* Prepare to jump to return */
    do_exit = true;
    ex_stat = status;
    if(jmp_set) {
        longjmp(place,status);
    }
    if(abort_n) {
        exit(status);
    }
}

char rmlint_echo_settings(RmSettings *settings) {
    char confirm;
    int save_verbosity=settings->verbosity;

    bool has_ppath=false;
    if ((!settings->skip_confirm) && (settings->verbosity<2))
        settings->verbosity=2;  /* need verbosity at least 2 if user is going to confirm settings*/

    warning (BLU"Running rmlint with the following settings:\n"NCO);
    warning ("(Note "BLU"[*]"NCO" hints below to change options)\n"NCO);

    /*---------------- lint types ---------------*/
    warning ("Looking for lint types:\n");
    if (settings->searchdup)		warning ("\t+ duplicates "RED"(%s)"NCO" [-U]\n", settings->cmd_path?"cmd":"rm");
    if (settings->findemptydirs)	warning ("\t+ empty directories "RED"(rm)"NCO" [-Y]\n");
    if (settings->listemptyfiles)	warning ("\t+ zero size files "RED"(rm)"NCO" [-K]\n");
    if (settings->findbadids)		warning ("\t+ files with bad UID/GID "BLU"(chown)"NCO" [-L]\n");
    if (settings->namecluster)		warning ("\t+ files with same name "GRE"(info only)"NCO" [-N]\n");
    if (settings->nonstripped)		warning ("\t+ non-stripped binaries"BLU"(strip)"RED"(slow)"NCO" [-A]\n");
    if (settings->doldtmp)			warning ("\t+ tmp files more than %i second older than original "RED"(rm)"NCO"[-x t]/[-X]\n", settings->oldtmpdata);
    if (settings->junk_chars)		warning ("\t+ junk characters "RED"%s"NCO" in file or dir name\n", settings->junk_chars);
    if (!settings->searchdup ||
            !settings->findemptydirs ||
            !settings->listemptyfiles ||
            !settings->findbadids ||
            !settings->namecluster ||
            !settings->nonstripped ||
            !settings->doldtmp ||
            !settings->junk_chars) {
        warning (NCO"\tNot looking for:\n");
        if (!settings->searchdup)		warning ("\t\tduplicates[-u];\n");
        if (!settings->findemptydirs)	warning ("\t\tempty directories[-y];\n");
        if (!settings->listemptyfiles)	warning ("\t\tzero size files[-k];\n");
        if (!settings->findbadids)		warning ("\t\tfiles with bad UID/GID[-l];\n");
        if (!settings->namecluster)		warning ("\t\tfiles with same name[-n];\n");
        if (!settings->nonstripped)		warning ("\t\tnon-stripped binaries[-a];\n");
        if (!settings->doldtmp)			warning ("\t\told tmp files[-x <time>];\n");
        if (!settings->junk_chars)		warning ("\t\tjunk characters in filenames[-j \"chars\"];\n");
    }

    /*---------------- search paths ---------------*/
    warning(NCO"Search paths:\n");
    for(int i = 0; settings->paths[i] != NULL; ++i) {
        if (settings->is_ppath[i]) {
            has_ppath=true;
            warning (GRE"\t(orig)\t+ %s\n"NCO, settings->paths[i] );
        } else warning ("\t\t+ %s\n", settings->paths[i] );
    }
    if ((settings->paths[1]) && !has_ppath) warning("\t[prefix one or more paths with // to flag location of originals]\n");

    /*---------------- search tree options---------*/
    warning ("Tree search parameters:\n");
    warning ("\t%s hidden files and folders [-%s]\n"NCO,
             settings->ignore_hidden ? "Excluding" : "Including",
             settings->ignore_hidden ? "G" :  "g" );
    warning ("\t%s symlinked files and folders [-%s]\n"NCO,
             settings->followlinks ?"Following" : "Excluding",
             settings->followlinks ?"F" : "f" );
    warning ("\t%srossing filesystem / mount point boundaries [-%s]\n"NCO,
             settings->samepart ? "Not c" : "C",
             settings->samepart ? "S" : "s");
    if (settings->dpattern) {
        warning("\tDirectory name must%s match regex '%s' (case %ssensitive)\n",
                settings->invmatch ? " not" : "",
                settings->dpattern,
                settings->casematch ? "" : "in" );
    } else warning ("\tNo regex filter for directory name [-R regex]\n");

    if (settings->depth) warning("\t Only search %i levels deep into search paths\n",settings->depth);

    /*---------------- file filters ---------------*/

    warning("Filtering search based on:\n");
    if (settings->fpattern) {
        warning("\tFile name must%s match regex '%s'	 (case %ssensitive)\n",
                settings->invmatch ? " not" : "",
                settings->fpattern,
                settings->casematch ? "" : "in" );
    } else warning ("\tNo regex filter for directory name [-r regex]\n");

    if ( (settings->minsize !=-1) && (settings->maxsize !=-1) )
        warning("\tFile size between %i and %i bytes\n", settings->minsize, settings->maxsize);
    else if (settings->minsize !=-1)
        warning("\tFile size at least %i bytes\n", settings->minsize);
    else if (settings->maxsize !=-1)
        warning("\tFile size no bigger than %i bytes\n", settings->maxsize);
    else
        warning("\tNo file size limits [-z \"min;max\"]");
    if (settings->must_match_original) {
        warning("\tDuplicates must have at least one member in the "GRE"(orig)"NCO" paths indicated above\n");
        if (!has_ppath)
            error(RED"\tWarning: no "GRE"(orig)"RED" paths specified for option -M --mustmatchorig (use //)\n"NCO);
    }

    if (settings->find_hardlinked_dupes) {
        warning("\tHardlinked file sets will be treated as duplicates (%s)\n",settings->cmd_path ? settings->cmd_path : "rm");
        warning(RED"\t\tBUG"NCO": rmlint currently does not deduplicate hardlinked files with same basename\n");
    } else warning("\tHardlinked file sets will not be deduplicated [-H]\n");

    /*---------------- originals selection ranking ---------*/

    warning(NCO"Originals selected based on (decreasing priority):    [-D <criteria>]\n");
    if (has_ppath) warning("\tpaths indicated "GRE"(orig)"NCO" above\n");

    for (int i = 0; settings->sort_criteria[i]; ++i) {
        switch(settings->sort_criteria[i]) {
        case 'm':
            warning("\tKeep oldest modified time\n");
            break;
        case 'M':
            warning("\tKeep newest modified time\n");
            break;
        case 'p':
            warning("\tKeep first-listed path (above)\n");
            break;
        case 'P':
            warning("\tKeep last-listed path (above)\n");
            break;
        case 'a':
            warning("\tKeep first alphabetically\n");
            break;
        case 'A':
            warning("\tKeep last alphabetically\n");
            break;
        default:
            error(RED"\tWarning: invalid originals ranking option '-D %c'\n"NCO, settings->sort_criteria[i]);
            break;
        }
    }

    if (settings->keep_all_originals) {
        warning("\tNote: all originals in "GRE"(orig)"NCO" paths will be kept\n");
    }
    warning("\t      "RED"but"NCO" other lint in "GRE"(orig)"NCO" paths may still be deleted\n");

    /*---------------- action mode ---------------*/
    if (settings->mode==1) {
        /*same mode for duplicates and everything else*/
        warning ("Action for all Lint types:\n");
    } else {
        /*different mode for duplicated vs everything else*/
        warning ("Action for Duplicates:\n\t");
        // TODO: Enumerate this.
        switch (settings->mode) {
        case 2:
            warning("Ask user what to do with each file\n");
            break;
        case 3:
            warning(RED"Delete files without asking\n"NCO);
            break;
        case 4:
            warning(YEL"Replace duplicates with symlink to original\n"NCO);
            break;
        case 5:
            warning(YEL"Execute command:\n\t\tdupe:'%s'\n\t\torig:'%s'\n"NCO, settings->cmd_path, settings->cmd_orig);
            break;
        default:
            break;
        }
        warning ("Action for all other Lint types:\n");
    }

    if (settings->output)
        warning("\tGenerate script %s.sh to run later\n", settings->output);
    else warning("\tDo nothing\n");

    /*--------------- paranoia ---------*/

    if (settings->paranoid) {
        warning("Note: paranoid (bit-by-bit) comparison will be used to verify duplicates "RED"(slow)\n"NCO);
    } else {
        warning("Note: fingerprint and md5 comparison will be used to identify duplicates "RED"(very slight risk of false positives)"NCO" [-p]");
    }

    /*--------------- confirmation ---------*/

    if (!settings->skip_confirm) {
        warning(YEL"\n\nPress y or enter to continue, any other key to abort\n");
        warning(YEL"\tNOTE: passing option -q bypasses this confirmation\n");

        scanf("%c", &confirm);
        settings->verbosity=save_verbosity;
        return (confirm=='y' || confirm=='Y' || confirm=='\n');
    }

    settings->verbosity=save_verbosity;
    return 1;
}

/* ------------------------------------------------------------- */

int rmlint_main(RmSession *session) {
    /* Used only for infomessage */
    total_files = 0;
    abort_n = false;
    if(do_exit != true) {
        /* Init all modules that use global variables.. */
        if(!use_cwd) {
            rmlint_init();
        }
        filt_c_init();
        mode_c_init();
        linttests_c_init();
    }

    /* Jump to this location on exit (with do_exit=true) */
    setjmp(place);

    jmp_set = true;
    if(do_exit != true) {
        if(session->settings->mode == 5) {
            if(session->settings->cmd_orig) {
                if(check_cmd(session->settings->cmd_orig) == 0) {
                    die(session, EXIT_FAILURE);
                }
            }
            if(session->settings->cmd_path) {
                check_cmd(session->settings->cmd_path);
                die(session, EXIT_FAILURE);
            }
        }

        /* Open logfile */
        init_filehandler(session->settings);

        /* Warn if started with sudo. Hack: Just try to get read access to /bin/ls */
        if(!access("/bin/ls", R_OK | W_OK)) {
            warning(YEL"WARN: "NCO"You're running rmlint with privileged rights - \n");
            warning("      Take care of what you're doing!\n\n");
        }
        total_files = rmlint_search_tree(session);

        if(total_files < 2) {
            warning("No files in cache to search through => No duplicates.\n");
            die(session, 0);
        } else {
            info("Returned %d files\n", total_files);
        }

        info("Now in total "YEL"%ld useable file(s)"NCO" in cache.\n", total_files);
        if(session->settings->threads > total_files) {
            session->settings->threads = total_files + 1;
        }
        /* Till this point the list is unsorted
         * The filter alorithms requires the list to be size-sorted,
         * so it can easily filter unique sizes, and build "groups"
         * */
        info("Now finding easy lint..%c", session->settings->verbosity > 4 ? '.' : '\n');

        /* Apply the prefilter and outsort inique sizes */
        start_processing(session);
        die(session, EXIT_SUCCESS);
    }

    return ex_stat;
}
