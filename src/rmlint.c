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

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

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



/**

Result of the testdir run should be (dont run with "-f"!):
----------------------------------------------------------

# testdir/recursed_a/one
X testdir/recursed_b/one

# testdir/recursed_a/two
X testdir/twice_one
X testdir/two

# testdir/recursed_b/three
X testdir/recursed_b/two_plus_one

# testdir/with spaces a
X testdir/with spaces b

*/

/* If more that one path given increment */
int  cpindex = 0;

/* Control flags */
bool do_exit = false,
     use_cwd = false,
     jmp_set = false,
     ex_stat = false;

/* Default commands */
const char *command_C = "ls \"%s\" -lasi";
const char *command_c = "ls \"%s\"";
const char *script_name = "rmlint.sh";


/* If die() is called rmlint will jump back to the end of main
 * rmlint does NOT call exit() or abort() on it's own - so you
 * may use it's methods also in your own programs - see main()
 * It still has various problems with calling rmlint_main() twice... :-( //ToDo
 * */
jmp_buf place;


/** Messaging **/
void error(const char* format, ...)
{
        if(set.verbosity > 0) {
                va_list args;
                va_start (args, format);
                vfprintf (stderr, format, args);
                va_end (args);
        }
}

void warning(const char* format, ...)
{
        if(set.verbosity > 1) {
                va_list args;
                va_start (args, format);
                vfprintf (stderr, format, args);
                va_end (args);
        }
}

void info(const char* format, ...)
{
        if(set.verbosity > 2) {
                va_list args;
                va_start (args, format);
                vfprintf (stdout, format, args);
                va_end (args);
        }
}

/* The nightmare of every secure program :))
 * this is used twice; 1x with a variable format..
 * Please gimme a note if I forgot to check sth. there. ;)
*/
int systemf(const char* format, ...)
{
        int ret = 0;
        char cmd_buf[1024];

        /* Build a command  */
        va_list args;
        va_start (args, format);
        vsnprintf (cmd_buf,1024,format, args);
        va_end (args);

        /* Now execute a shell command */
        if((ret = system(cmd_buf)) == -1) {
                perror("systemf(const char* format, ...)");
        }
        return ret;
}

/* Help text */
static void print_help(void)
{
        fprintf(stderr, "Syntax: rmlint [TargetDir[s]] [File[s]] [Options]\n");
        fprintf(stderr, "\nGeneral options:\n\n"
                "\t-t --threads <t>\tSet the number of threads to <t> (Default: 4; May have only minor effect)\n"
                "\t-p --paranoid\t\tDo a byte-by-byte comparasion additionally. (Slow!) (Default: No.)\n"
                "\t-j --junk <junkchars>\tSearch for files having one letter of <junkchars> in their name. (Useful for finding names like 'Q@^3!')\n"
               );
        fprintf(stderr, "\t-a --nonstripped\tSearch for nonstripped binaries (Binaries with debugsymbols) (Slow) (Default: No.)\n"
                "\t-n --namecluster\tSearch for files with the same name (do nothing but printing them) (Default: No.)\n"
                "\t-y --emptydirs\t\tSearch for empty dirs (Default: Yes.)\n"
                "\t-x --oldtmp <sec>\tSearch for files with a '~' suffix being min. <sec> seconds older than the corresponding file without the '~' (Default: 60);\n"
                "\t-u --dups\t\tSearch for duplicates (Default: Yes.)\n"
               );
        fprintf(stderr,	"\t-d --maxdepth <depth>\tOnly recurse up to this depth. (default: inf)\n"
                "\t-f --followlinks\tWether links are followed (None is reported twice) [Only specify this if you really need to, Default: No.]\n"
                "\t-s --samepart\t\tNever cross mountpoints, stay on the same partition. (Default: No.)\n"
                "\t-G --hidden\t\tAlso search through hidden files / directories (Default: No.)\n"
                "\t-m --mode <mode>\tTell rmlint how to deal with the duplicates it finds (only on duplicates!).:\n"
               );
        fprintf(stderr,	"\n\t\t\t\tWhere modes are:\n\n"
                "\t\t\t\tlist  - Only list found files and exit.\n"
                "\t\t\t\tlink  - Replace file with a hard link to original.\n"
                "\t\t\t\task   - Ask for each file what to do\n"
                "\t\t\t\tnoask - Full removal without asking.\n"
                "\t\t\t\tcmd   - Takes the command given by -c and executes it on the file.\n"
                "\t\t\t\tDefault: list\n\n"
                "\t-c --cmd_dup  <cmd>\tExecute a shellcommand on found duplicates when used with '-m cmd'\n");
        fprintf(stderr,"\t-C --cmd_orig <cmd>\tExecute a shellcommand on original files when used with '-m cmd'\n\n"
                "\t\t\t\tExample: rmlint testdir -m cmd -C \"ls '%%s'\" -c \"ls -lasi '%%s'\" -v 1\n"
                "\t\t\t\tThis would print all found files (both duplicates and originals via the 'ls' utility\n"
                "\t\t\t\tThe %%s expands to the found duplicate, second to the 'original'.\n"
               );
        fprintf(stderr,	"Regex options:\n\n"
                "\t-r --fregex <pat>\tChecks filenames against the pattern <pat>\n"
                "\t-R --dregex <pat>\tChecks dirnames against the pattern <pat>\n"
                "\t-i --invmatch\t\tInvert match - Only investigate when not containing <pat> (Default: No.)\n"
                "\t-e --matchcase\t\tMatches case of paths (Default: No.)\n");
        fprintf(stderr,	"\nMisc options:\n\n"
                "\t-h --help\t\tPrints this text and exits\n"
                "\t-o --output [<o>]\tOutputs logfile to <o>. The <o> argument is optional, specify none to write no log.\n"
                "\t\t\t\tExamples:\n\n\t\t\t\t-o => No Logfile\n\t\t\t\t-o\"la la.txt\" => Logfile to \"la la.txt\"\n\n\t\t\t\tNote the missing whitespace. (Default \"rmlint.sh\")\n\n");
        fprintf(stderr,"\t-z --dump <id>\t\tOption with various weird meanings, most scientist postulated that it kills kittens (-> Some debug option).\n"
                "\t-v --verbosity <v>\tSets the verbosity level to <v>\n"
                "\t\t\t\tWhere:\n"
                "\t\t\t\t0 prints nothing\n"
                "\t\t\t\t1 prints only errors and results\n"
                "\t\t\t\t2 + prints warning\n"
                "\t\t\t\t3 + everything else\n"
                "\n"
               );
        fprintf(stderr,"Additionally, the options p,f,s,e,g,o,i,c,n,a,y,x,u have a uppercase option (O,G,P,F,S,E,I,C,N,A,Y,X,U) that inverse it's effect.\n"
                "The corresponding long options have a \"no-\" prefix. E.g.: --no-emptydirs\n"
               );
        fprintf(stderr, "\nVersion 0.43 (Compiled on %s @ %s) - Copyright Christopher <Sahib Bommelig> Pahl\n",__DATE__,__TIME__);
        fprintf(stderr, "Licensed under the terms of the GPLv3 - See COPYRIGHT for more information\n");
        fprintf(stderr, "See the manpage or the README for more information.\n");
        die(0);
}

/* Version string */
static void print_version(void)
{
        fprintf(stderr, "Version 0.76b Compiled: %s @ %s\n",__DATE__,__TIME__);
        die(0);
}

/* Options not specified by commandline get a default option - this called before rmlint_parse_arguments */
void rmlint_set_default_settings(rmlint_settings *set)
{
        set->mode  		 =  1; 		/* list only    */
        set->casematch   =  0; 		/* Insensitive  */
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
        set->cmd_path 	 =  NULL;   /* Cmd,if used  */
        set->cmd_orig    =  NULL;   /* Origcmd, -"- */
        set->junk_chars =   NULL;
        set->oldtmpdata    = 60;
        set->ignore_hidden = 1;
        set->findemptydirs = 1;
        set->namecluster   = 0;
        set->nonstripped   = 0;
        set->searchdup     = 1;
        set->output        = (char*)script_name;
}


/* Parse the commandline and set arguments in 'settings' (glob. var accordingly) */
char rmlint_parse_arguments(int argc, char **argv, rmlint_settings *sets)
{
        int c,lp=0;
        while (1) {
                static struct option long_options[] = {
                        {"threads",        required_argument, 0, 't'},
                        {"dregex",         required_argument, 0, 'R'},
                        {"fregex",  	   required_argument, 0, 'r'},
                        {"mode",           required_argument, 0, 'm'},
                        {"maxdepth",	   required_argument, 0, 'd'},
                        {"cmd_dup",        required_argument, 0, 'c'},
                        {"cmd_orig",       required_argument, 0, 'C'},
                        {"junk",           required_argument, 0, 'j'},
                        {"verbosity",      required_argument, 0, 'v'},
                        {"output",         optional_argument, 0, 'o'},
                        {"emptydirs",      no_argument,       0, 'y'},
                        {"no-emptydirs",   no_argument,       0, 'Y'},
                        {"namecluster",    no_argument, 	  0, 'n'},
                        {"no-namecluster", no_argument, 	  0, 'N'},
                        {"nonstripped",    no_argument,       0, 'a'},
                        {"no-nonstripped", no_argument,       0, 'A'},
                        {"oldtmp",         required_argument, 0, 'x'},
                        {"no-oldtmp",      no_argument,       0, 'X'},
                        {"no-hidden",      no_argument,       0, 'g'},
                        {"hidden",         no_argument,       0, 'G'},
                        {"dups",		   no_argument, 	  0, 'u'},
                        {"no-dups",        no_argument,       0, 'U'},
                        {"matchcase",      no_argument, 	  0, 'e'},
                        {"ignorecase",     no_argument, 	  0, 'E'},
                        {"followlinks",    no_argument, 	  0, 'f'},
                        {"ignorelinks",    no_argument, 	  0, 'F'},
                        {"invertmatch",    no_argument, 	  0, 'i'},
                        {"normalmatch",    no_argument, 	  0, 'I'},
                        {"samepart",       no_argument,	      0, 's'},
                        {"allpart",        no_argument,	      0, 'S'},
                        {"paranoid",       no_argument,	      0, 'p'},
                        {"naive",          no_argument,	      0, 'P'},
                        {"help",           no_argument, 	  0, 'h'},
                        {"version",        no_argument,       0, 'V'},
                        {0, 0, 0, 0}
                };

                /* getopt_long stores the option index here. */
                int option_index = 0;

                c = getopt_long (argc, argv, "m:R:r:o::j:uUVyhYnNaAx:XgGpPfFeEsSiIc:C:t:d:v:",long_options, &option_index);

                /* Detect the end of the options. */
                if (c == -1) {
                        break;
                }

                switch (c) {
                case '?':
                        return 0;
                case 't':
                        sets->threads = atoi(optarg);
                        if(!sets->threads || sets->threads < 0) {
                                sets->threads = 4;
                        }
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
                case 'V':
                        print_version();
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
                        sets->oldtmpdata = 0;
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
                                die(0);
                                return 0;
                        }

                        break;
                default:
                        return 0;
                }
        }

        while(optind < argc) {
                int p = open(argv[optind],O_RDONLY);
                if(p == -1) {
                        error(YEL"FATAL: "NCO"Can't open directory \"%s\": %s\n", argv[optind], strerror(errno));
                        return 0;
                } else {
                        close(p);
                        sets->paths	  = realloc(sets->paths,sizeof(char*)*(lp+2));
                        sets->paths[lp++] = argv[optind];
                        sets->paths[lp  ] = NULL;
                }
                optind++;
        }

        if(lp == 0) {
                /* Still no path set? */
                sets->paths = malloc(sizeof(char*)<<1);
                sets->paths[0] = getcwd(NULL,0);
                sets->paths[1] = NULL;
                if(!sets->paths[0]) {
                        error(YEL"FATAL: "NCO"Cannot get working directory: "YEL"%s\n"NCO, strerror(errno));
                        error("       Are you maybe in a dir that just had been removed?\n");
                        if(sets->paths) {
                                free(sets->paths);
                        }
                        return 0;
                }
                use_cwd = true;
        }
        return 1;
}

/* User  may specify in -c/-C a command that get's excuted on every hit - check for being a safe one */
static void check_cmd(const char *cmd)
{
        int i = 0, ps = 0;
        int len = strlen(cmd);
        for(; i < len; i++) {
                if(cmd[i] == '%' && i+1 != len) {
                        if(cmd[i+1] == 's') {
                                ps++;
                                continue;
                        }
                }
        }
        if(ps > 1) {
                error(YEL"FATAL: "NCO"--command [-c]: Only \"%%s\" is allowed!\n");
                error("       Example: rmlint -c \"ls '%%s'\"\n");
                die(0);
        }
}


/* This is only used to pass the current dir to eval_file */
int  get_cpindex(void)
{
        return cpindex;
}

/* exit and return to calling method */
void die(int status)
{
        /* Free mem */
        if(use_cwd) {
                if(set.paths[0]) {
                        free(set.paths[0]);
                }
        }
        if(set.paths) {
                free(set.paths);
        }
	
        if(status) {
                info("Abnormal exit\n");
        }

		/* Close logfile */
        if(get_logstream()) {
                fclose(get_logstream());
        }

        /* Prepare to jump to return */
        do_exit = true;
        ex_stat = status;
        if(jmp_set) {
                longjmp(place,status);
        }
}

/* Sort criteria for sizesort */
static long cmp_sz(iFile *a, iFile *b)
{
        return a->fsize - b->fsize;
}


/* Actual entry point */
int rmlint_main(void)
{
		/* Used only for infomessage */
        uint32 total_files = 0;

		/* Jump to this location on exit (with do_exit=true) */
        setjmp(place);
        jmp_set = true;
        if(do_exit != true) {
                if(set.mode == 5) {

                        if(!set.cmd_orig && !set.cmd_path) {
                                set.cmd_orig = (char*)command_C;
                                set.cmd_path = (char*)command_c;
                        } else {
                                if(set.cmd_orig) {
                                        check_cmd(set.cmd_orig);
                                }
                                if(set.cmd_path) {
                                        check_cmd(set.cmd_path);
                                }
                        }
                }
                /* Open logfile */
                init_filehandler();

                /* Warn if started with sudo */
                if(!access("/bin/ls",R_OK|W_OK)) {
                        warning(YEL"WARN: "NCO"You're running rmlint with privileged rights - \n");
                        warning("      Take care of what you're doing!\n\n");
                }
                /* Count files and do some init  */
                while(set.paths[cpindex] != NULL) {
                        DIR *p = opendir(set.paths[cpindex]);
                        if(p == NULL && errno == ENOTDIR) {
                                /* The path is a file */
                                struct stat buf;
                                if(stat(set.paths[cpindex],&buf) == -1) {
                                        continue;
                                }

                                list_append(set.paths[cpindex],(uint32)buf.st_size,buf.st_dev,buf.st_ino, true);
                                total_files++;

                        } else {
                                /* The path points to a dir - recurse it! */
                                info("Now scanning "YEL"\"%s\""NCO"..",set.paths[cpindex]);
                                total_files += recurse_dir(set.paths[cpindex]);
                                info(" done.\n");
                                closedir(p);
                        }
                        cpindex++;
                }

                if(total_files < 2) {
                        warning("No files in cache to search through => No duplicates.\n");
                        die(0);
                }

                info("In total "YEL"%ld useable files"NCO" in cache.\n", total_files);

                if(set.threads > total_files) {
                        set.threads = total_files;
                }
			

                /* Till this point the list is unsorted
                 * The filter alorithms requires the list to be size-sorted,
                 * so it can easily filter unique sizes, and build "groupisles"
                 * */
                info("Now mergesorting list based on filesize... ");
                list_sort(list_begin(),cmp_sz);
                info("done.\n");

                info("Now finding easy lint: \n");

                /* Apply the prefilter and outsort inique sizes */
                start_processing(list_begin());

                /* Exit! */
                die(EXIT_SUCCESS);
        }
        return ex_stat;
}
