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
 * Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include <sys/stat.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <dirent.h>
#include <fcntl.h>
#include <setjmp.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>

#include "rmlint.h"
#include "mode.h"
#include "md5.h"
#include "list.h"
#include "traverse.h"
#include "filter.h"
#include "linttests.h"

/* Version string */
static void print_version(void) {
    fprintf(stderr, "Version %s compiled: [%s]-[%s] (rev %s)\n", RMLINT_VERSION, __DATE__, __TIME__, RMLINT_VERSION_GIT_REVISION);
    fprintf(stderr, "Author Christopher Pahl\n");
    fprintf(stderr, "Report bugs to https://github.com/sahib/rmlint/issues\n");
}

/* ------------------------------------------------------------- */

/* Help text */
static void print_help(void) {
    // TODO: Clean up helptext; rewrite man page.
    //       Or just write manpage do system("man rmlint") here.
    fprintf(stderr, 
            "Syntax: rmlint [[//]TargetDir[s]] [File[s]] [Options]\n"
            "\nGeneral options:\n\n"
            "\t-t --threads <t>\tSet the number of threads to <t> (Default: 4; May have only minor effect)\n"
            "\t-p --paranoid\t\tDo a byte-by-byte comparison additionally for duplicates. (Slow!) (Default: No.)\n"
            "\t-j --junk <junkchars>\tSearch for files having one letter of <junkchars> in their name. (Useful for finding names like 'Q@^3!'')\n"
            "\t-z --limit\t\tMinimum and maximum size of files in Bytes; example: \"20000;-1\" (Default: \"-1;-1\")\n"
            "\t-a --nonstripped\tSearch for nonstripped binaries (Binaries with debugsymbols) (Slow) (Default: No.)\n"
            "\t-n --namecluster\tSearch for files with the same name (do nothing but printing them) (Default: No.)\n"
            "\t-k --emptyfiles\t\tSearch for empty files (Default: Yes, use -K to disable)\n"
            "\t-y --emptydirs\t\tSearch for empty dirs (Default: Yes, use -Y to disable)\n"
            "\t-x --oldtmp <sec>\tSearch for files with a '~'/'.swp' suffix being min. <sec> seconds older than the corresponding file without the '~'/'.swp'; (Default: 60)\n"
            "\t\t\t\tNegative values are possible, what will find data younger than <sec>\n"
            "\t-u --dups\t\tSearch for duplicates (Default: Yes.)\n"
            "\t-l --badids\t\tSearch for files with bad IDs and GIDs (Default: Yes.)\n"
            "\t-M --mustmatchorig\tOnly look for duplicates of which one is in 'originals' paths. (Default: no)\n"
            "\t-O --keepallorig\tDon't delete any duplicates that are in 'originals' paths. (Default - just keep one)\n"
            "\t\tNote: for lint types other than duplicates, keepallorig option is ignored\n" /*TODO: does this need unifying??*/
            "\t-Q --invertorig\tPaths prefixed with // are non-originals and all other paths are originals\n"
            "\t-D --sortcriteria <criteria>\twhen selecting original, sort in order of <criteria>:\n"
            "\t\t\t\tm=keep lowest mtime (oldest), M=keep highest mtime (newest)\n"
            "\t\t\t\ta=keep first alphabetically,  A=keep last alphabetically\n"
            "\t\t\t\tp=keep first named path,      P=keep last named path\n"
            "\t\t\t\tNote: can have multiple criteria, eg \"-D am\" will choose first alphabetically; if tied then by mtime.\n"
            "\t\t\t\tNote also: original path criteria (specified using //) will always take first priority over \"-D\" options.\n"
            "\t-d --maxdepth <depth>\tOnly recurse up to this depth. (default: inf)\n"
            "\t-f --followlinks\tWhether symlinks are followed (Default: no). Note that rmlint will try to detect if symlinks\n"
            "\t\t\t\tresult in the same physical file being encountered twice and will ignore the second one.\n"
            "\t-H --findhardlinked\tFind hardlinked duplicates.  Default is to ignore duplicates which are hardlinked to each other.\n"
            "\t\t\t\tNote: currently, hardlinked files with the same basename are _always_ ignored, due to possible error with bind\n"
            "\t\t\t\tmounts pointing to the same physical file\n"
            "\t\t\t\tNote also: hardlinked duplicates _will_ be reported as part of GB count that can be freed up.\n"
            "\t-s --samepart\t\tNever cross mountpoints, stay on the same partition. (Default: off, ie DO cross mountpoints)\n"
            "\t-G --hidden\t\tAlso search through hidden files / directories (Default: No.)\n"
            "\t-m --mode <mode>\tTell rmlint how to deal with the duplicates it finds (only on duplicates!).:\n"
            "\n\t\t\t\tWhere modes are:\n\n"
            "\t\t\t\tlist\t- (RECOMMENDED) Lists found files & creates executable script to carry out\n"
            "\t\t\t\t\t actual removal (or other command given by -c/-C).\n"
            "\t\t\t\tlink\t- Replace file with a symlink to original.\n"
            "\t\t\t\tnoask\t- Full removal without asking.\n"
            "\t\t\t\tcmd\t- Takes the command given by -c/-C and executes it on the duplicate/original (careful!).\n"
            "\t\t\t\tDefault:\tlist\n\n"
            "\t-c --cmd_dup  <cmd>\tExecute a shellcommand on found duplicates when used with '-m cmd'\n"
            "\t-C --cmd_orig <cmd>\tExecute a shellcommand on original files when used with '-m cmd'\n\n"
            "\t\t\t\tExample: rmlint testdir -m cmd -C \"ls '<orig>'\" -c \"ls -lasi '<dupl>' #== '<orig>'\" -v5\n"
            "\t\t\t\tThis would print all found files (both duplicates and originals via the 'ls' utility\n"
            "\t\t\t\tThe <dupl> expands to the found duplicate, <orig> to the original.\n\n"
            "\t\t\t\tNote: If '-m cmd' is not given, rmlint's default commands are replaced with the ones from -cC\n"
            "\t\t\t\t      This is especially useful with -v5, so you can pipe your commands to sh in realtime.\n"
            "Regex options:\n\n"
            "\t-r --fregex <pat>\tChecks filenames against the pattern <pat>\n"
            "\t-R --dregex <pat>\tChecks dirnames against the pattern <pat>\n"
            "\t-i --invmatch\t\tInvert match - Only investigate when not containing <pat> (Default: No.)\n"
            "\t-e --matchcase\t\tMatches case of paths (Default: No.)\n"
            "\nMisc options:\n\n"
            "\t-h --help\t\tPrints this text and exits\n"
            "\t-o --output <o>\tOutputs logfile to <o>.log and script to <o>.sh   (use -o \"\" or \'\' to disable output files)\n"
            "\t\t\t\tExamples:\n"
            "\t\t\t\t\t-o \"\" => No Logfile\n"
            "\t\t\t\t\t-o \"la la.txt\" => Logfile to \"la la.txt.log\"\n"
            "\t-v --verbosity <v>\tSets the verbosity level to <v>\n"
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
            "\t-B --no-color\t\tDon't use colored output.\n"
            "\t-q --confirm-settings\tDisplays summary of settings and queries user for confirmation before running\n\n"
            "Additionally, the options b,p,f,s,e,g,i,c,n,a,y,x,u have a uppercase option (B,G,P,F,S,E,I,C,N,A,Y,X,U) that inverse it's effect.\n"
            "The corresponding long options have a \"no-\" prefix. E.g.: --no-emptydirs\n\n"
            "\nLicensed under the terms of the GPLv3 - See COPYRIGHT for more information\n"
            "Quick clues for adjusting settings are available by using the -q option.\n"
            "See the manpage, README or <http://sahib.github.com/rmlint/> for more information.\n"
        );
}

/* ------------------------------------------------------------- */

/* Options not specified by commandline get a default option - this called before rmlint_parse_arguments */
void rmlint_set_default_settings(RmSettings *pset) {
    pset->mode                  = RM_MODE_LIST;       /* list only    */
    pset->paranoid              = 0;                  /* dont be bush */
    pset->depth                 = 0;                  /* inf depth    */
    pset->followlinks           = 0;                  /* fol. link    */
    pset->threads               = 16;                 /* Quad*quad.   */
    pset->verbosity             = 2;                  /* Most relev.  */
    pset->samepart              = 0;                  /* Stay parted  */
    pset->paths                 = NULL;               /* Startnode    */
    pset->is_ppath              = NULL;               /* Startnode    */
    pset->cmd_path              = NULL;               /* Cmd,if used  */
    pset->cmd_orig              = NULL;               /* Origcmd, -"- */
    pset->ignore_hidden         = 1;
    pset->findemptydirs         = 1;
    pset->namecluster           = 0;
    pset->nonstripped           = 0;
    pset->searchdup             = 1;
    pset->color                 = 1;
    pset->findbadids            = 1;
    pset->output                = "rmlint";
    pset->limits_specified      = 0;
    pset->minsize               = -1;
    pset->maxsize               = -1;
    pset->listemptyfiles        = 1;
    pset->keep_all_originals    = 0;                  /* Keep just one file from ppath "originals" indicated by "//" */
    pset->must_match_original   = 0;                  /* search for any dupes, not just ones which include ppath members*/
    pset->invert_original       = 0;                  /* search for any dupes, not just ones which include ppath members*/
    pset->find_hardlinked_dupes = 0;                  /* ignore hardlinked dupes*/
    pset->sort_criteria         = "m";                /* default ranking order for choosing originals - keep oldest mtime*/
    pset->confirm_settings      = 0;                  /* default setting is no user confirmation of settings */

    /* There is no cmdline option for this one    *
     * It controls wether 'other lint' is also    *
     * investigated to be replicas of other files */
    pset->collide                                    = 0;

}

/* Check if this is the 'preferred' dir */
bool check_if_preferred(const char *dir) {
    if(dir != NULL) {
        size_t length = strlen(dir);
        if(length >= 2 && dir[0] == '/' && dir[1] == '/')
            return TRUE;
    }
    return FALSE;
}

static const struct FormatSpec {
    const char *id;
    unsigned base;
    unsigned exponent;
} SIZE_FORMAT_TABLE[] = {
    /* This list is sorted, so bsearch() can be used */
    {.id = "b"  , .base = 512  , .exponent = 1},
    {.id = "c"  , .base = 1    , .exponent = 1},
    {.id = "e"  , .base = 1000 , .exponent = 6},
    {.id = "eb" , .base = 1024 , .exponent = 6},
    {.id = "g"  , .base = 1000 , .exponent = 3},
    {.id = "gb" , .base = 1024 , .exponent = 3},
    {.id = "k"  , .base = 1000 , .exponent = 1},
    {.id = "kb" , .base = 1024 , .exponent = 1},
    {.id = "m"  , .base = 1000 , .exponent = 2},
    {.id = "mb" , .base = 1024 , .exponent = 2},
    {.id = "p"  , .base = 1000 , .exponent = 5},
    {.id = "pb" , .base = 1024 , .exponent = 5},
    {.id = "t"  , .base = 1000 , .exponent = 4},
    {.id = "tb" , .base = 1024 , .exponent = 4},
    {.id = "w"  , .base = 2    , .exponent = 1}
};

typedef struct FormatSpec FormatSpec;

static const int SIZE_FORMAT_TABLE_N = sizeof(SIZE_FORMAT_TABLE) / sizeof(FormatSpec);

static int size_format_error(const char **error, const char *msg) {
    if(error) {
        *error = msg;
    }
    return 0;
}

static int compare_spec_elem(const void *fmt_a, const void *fmt_b) {
    return strcasecmp(((FormatSpec *)fmt_a)->id, ((FormatSpec *)fmt_b)->id);
}

guint64 size_string_to_bytes(const char *size_spec, const char **error) {
    if (size_spec == NULL) {
        return size_format_error(error, "Input size is NULL");
    }

    char *format = NULL;
    long double decimal = strtold(size_spec, &format);

    if (decimal == 0 && format == size_spec) {
        return size_format_error(error, "This does not look like a number");
    } else if (decimal < 0) {
        return size_format_error(error, "Negativ sizes are no good idea");
    } else if (*format) {
        format = g_strstrip(format);
    } else {
        return round(decimal);
    }

    FormatSpec key = {.id = format};
    FormatSpec *found = bsearch(
                            &key, SIZE_FORMAT_TABLE,
                            SIZE_FORMAT_TABLE_N, sizeof(FormatSpec),
                            compare_spec_elem
                        );

    if (found != NULL) {
        /* No overflow check */
        return decimal * powl(found->base, found->exponent);
    } else {
        return size_format_error(error, "Given format specifier not found");
    }
}

/* Size spec parsing implemented by qitta.
 * (http://github.com/qitta)
 * Thanks and go blame him if this breaks!
 */
static gboolean size_range_string_to_bytes(const char *range_spec, guint64 *min, guint64 *max, const char **error) {
    *min = 0;
    *max = G_MAXULONG;

    const char *tmp_error = NULL;
    gchar **split = g_strsplit(range_spec, "-", 2);

    if(split[0] != NULL) {
        *min = size_string_to_bytes(split[0], &tmp_error);
    }

    if(split[1] != NULL && tmp_error == NULL) {
        *max = size_string_to_bytes(split[1], &tmp_error);
    }

    g_strfreev(split);

    if(*max < *min) {
        tmp_error = "Max is smaller than min";
    }

    if(error != NULL) {
        *error = tmp_error;
    }

    return (tmp_error == NULL);
}

static void parse_limit_sizes(RmSession *session, char *range_spec) {
    const char *error = NULL;
    if(!size_range_string_to_bytes(
                range_spec,
                &session->settings->minsize,
                &session->settings->maxsize,
                &error
            )) {
        g_printerr(RED"Error while parsing --limit: %s\n", error);
        die(session, EXIT_FAILURE);
    }
}

static GLogLevelFlags VERBOSITY_TO_LOG_LEVEL[] = {
    [0] = G_LOG_LEVEL_CRITICAL,
    [1] = G_LOG_LEVEL_ERROR,
    [2] = G_LOG_LEVEL_WARNING,
    [3] = G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO,
    [4] = G_LOG_LEVEL_DEBUG
};

static bool add_path(RmSession *session, int index, const char *path) {
    RmSettings *settings = session->settings;
    bool is_pref = check_if_preferred(path);

    if(is_pref) {
        path += 2;  /* skip first two characters ie "//" */
    }

    if(g_access(path, R_OK) != 0) {
        error(YEL"FATAL: "NCO"Can't open directory \"%s\": %s\n", path, strerror(errno));
        return FALSE;
    } else {
        settings->is_ppath = g_realloc(settings->is_ppath, sizeof(char) * (index + 1));
        settings->is_ppath[index] = is_pref;
        settings->paths = g_realloc(settings->paths, sizeof(char *) * (index + 2));
        settings->paths[index] = g_strdup(path);
        settings->paths[index + 1] = NULL;
        return TRUE;
    }
}

static int read_paths_from_stdin(RmSession *session, int index) {
    int paths_added = 0;
    char path_buf[PATH_MAX];

    while(fgets(path_buf, PATH_MAX, stdin)) {
        paths_added += add_path(session, index + paths_added, strtok(path_buf, "\n"));
    }

    return paths_added;
}

/* Parse the commandline and set arguments in 'settings' (glob. var accordingly) */
char rmlint_parse_arguments(int argc, char **argv, RmSession *session) {
    RmSettings *sets = session->settings;

    int choice = -1;
    int verbosity_counter = 2;
    int option_index = 0;
    int path_index = 0;

    while(1) {
        static struct option long_options[] = {
            {"threads"          ,  required_argument ,  0 ,  't'},
            {"mode"             ,  required_argument ,  0 ,  'm'},
            {"maxdepth"         ,  required_argument ,  0 ,  'd'},
            {"cmd_dup"          ,  required_argument ,  0 ,  'c'},
            {"cmd_orig"         ,  required_argument ,  0 ,  'C'},
            {"limit"            ,  required_argument ,  0 ,  'z'},
            {"output"           ,  required_argument ,  0 ,  'o'},
            {"sortcriteria"     ,  required_argument ,  0 ,  'D'},
            {"verbosity"        ,  no_argument       ,  0 ,  'v'},
            {"emptyfiles"       ,  no_argument       ,  0 ,  'k'},
            {"no-emptyfiles"    ,  no_argument       ,  0 ,  'K'},
            {"emptydirs"        ,  no_argument       ,  0 ,  'y'},
            {"color"            ,  no_argument       ,  0 ,  'b'},
            {"no-color"         ,  no_argument       ,  0 ,  'B'},
            {"no-emptydirs"     ,  no_argument       ,  0 ,  'Y'},
            {"namecluster"      ,  no_argument       ,  0 ,  'n'},
            {"no-namecluster"   ,  no_argument       ,  0 ,  'N'},
            {"nonstripped"      ,  no_argument       ,  0 ,  'a'},
            {"no-nonstripped"   ,  no_argument       ,  0 ,  'A'},
            {"no-hidden"        ,  no_argument       ,  0 ,  'g'},
            {"hidden"           ,  no_argument       ,  0 ,  'G'},
            {"badids"           ,  no_argument       ,  0 ,  'l'},
            {"no-badids"        ,  no_argument       ,  0 ,  'L'},
            {"dups"             ,  no_argument       ,  0 ,  'u'},
            {"no-dups"          ,  no_argument       ,  0 ,  'U'},
            {"matchcase"        ,  no_argument       ,  0 ,  'e'},
            {"ignorecase"       ,  no_argument       ,  0 ,  'E'},
            {"followlinks"      ,  no_argument       ,  0 ,  'f'},
            {"ignorelinks"      ,  no_argument       ,  0 ,  'F'},
            {"samepart"         ,  no_argument       ,  0 ,  's'},
            {"allpart"          ,  no_argument       ,  0 ,  'S'},
            {"paranoid"         ,  no_argument       ,  0 ,  'p'},
            {"naive"            ,  no_argument       ,  0 ,  'P'},
            {"keepallorig"      ,  no_argument       ,  0 ,  'O'},
            {"mustmatchorig"    ,  no_argument       ,  0 ,  'M'},
            {"invertorig"       ,  no_argument       ,  0 ,  'Q'},
            {"findhardlinked"   ,  no_argument       ,  0 ,  'H'},
            {"confirm-settings" ,  no_argument       ,  0 ,  'q'},
            {"help"             ,  no_argument       ,  0 ,  'h'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        choice = getopt_long(argc, argv, "aAbBcC:d:D:fFgGhHkKlLm:MnNo:OpPqQsSt:uUvVyYz:Z", long_options, &option_index);
        /* Detect the end of the options. */
        if(choice == -1) {
            break;
        }
        switch(choice) {
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
        case 'h':
            print_help();
            print_version();
            die(session, EXIT_SUCCESS);
            break;
        case 'H':
            sets->find_hardlinked_dupes = 1;
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
        case 'o':
            if (*optarg) {
                sets->output = optarg;
            } else {
                sets->output = NULL;
            }
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
        case 'V':
            verbosity_counter--;
            break;
        case 'v':
            verbosity_counter++;
            break;
        case 's':
            sets->samepart = 1;
            break;
        case 'S':
            sets->samepart = 0;
            break;
        case 'd':
            sets->depth = ABS(atoi(optarg));
            break;
        case 'D':
            sets->sort_criteria = optarg;
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
            sets->confirm_settings = 1;
            break;
        case 'z':
            sets->limits_specified = 1;
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
                sets->mode = RM_MODE_LIST;
            }
            if(!strcasecmp(optarg, "noask")) {
                sets->mode = RM_MODE_NOASK;
            }
            if(!strcasecmp(optarg, "link")) {
                sets->mode = RM_MODE_LINK;
            }
            if(!strcasecmp(optarg, "cmd")) {
                sets->mode = RM_MODE_CMD;
            }

            if(!sets->mode) {
                error(YEL"FATAL: "NCO"Invalid value for --mode [-m]\n");
                error("       Available modes are: list | link | noask | cmd\n");
                die(session, EXIT_FAILURE);
                return 0;
            }
            break;
        default:
            return 0;
        }
    }

    sets->verbosity = VERBOSITY_TO_LOG_LEVEL[CLAMP(
                          verbosity_counter, 0, G_LOG_LEVEL_DEBUG
                      )];

    /* Check the directory to be valid */
    while(optind < argc) {
        const char *dir_path = argv[optind];
        if(strlen(dir_path) == 1 && *dir_path == '-') {
            path_index += read_paths_from_stdin(session, path_index);
        } else {
            path_index += add_path(session, path_index, argv[optind]);
        }
        optind++;
    }
    if(path_index == 0) {
        /* Still no path set? - use `pwd` */
        sets->paths = g_malloc0(sizeof(char *) * 2);
        sets->paths[0] = getcwd(NULL, 0);
        sets->paths[1] = NULL;
        sets->is_ppath = g_malloc0(sizeof(char));
        sets->is_ppath[0] = 0;
        if(!sets->paths[0]) {
            error(YEL"FATAL: "NCO"Cannot get working directory: "YEL"%s\n"NCO, strerror(errno));
            error("       Are you maybe in a dir that just had been removed?\n");
            g_free(sets->paths);
            return 0;
        }
    }
    return 1;
}

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

/* exit and return to calling method */
void die(RmSession *session, int status) {
    RmSettings *sets = session->settings;

    /* Free mem */
    if(sets->paths) {
        for(int i = 0; sets->paths[i]; ++i) {
            g_free(sets->paths[i]);
        }
        g_free(sets->paths);
    }

    g_free(sets->is_ppath);

    if(status) {
        info("Abnormal exit\n");
    }
    /* Close logfile */
    if(session->log_out) {
        fclose(session->log_out);
    }
    /* Close scriptfile */
    if(session->script_out) {
        fprintf(session->script_out,
                "                      \n"
                "if [ -z $DO_REMOVE ]  \n"
                "then                  \n"
                "  rm -f rmlint.log    \n"  /*TODO: fix this to match "-o" command line options*/
                "  rm -f rmlint.sh     \n"
                "fi                    \n"
               );
        fclose(session->script_out);
    }

    if(session->userlist) {
        userlist_destroy(session->userlist);
    }

    exit(status);
}

char rmlint_echo_settings(RmSettings *settings) {
    char confirm;
    int save_verbosity = settings->verbosity;
    bool has_ppath = false;

    /* I've disabled this for now. Shouldn't we only print a summary
     * if the user has time to read it? */
    if(!settings->confirm_settings) {
        return 1;
    }

    if ((settings->confirm_settings) && (settings->verbosity < 3))
        settings->verbosity = G_LOG_LEVEL_INFO; /* need verbosity at least 3 if user is going to confirm settings*/

    info (BLU"Running rmlint with the following settings:\n"NCO);
    info ("(Note "BLU"[*]"NCO" hints below to change options)\n"NCO);

    /*---------------- lint types ---------------*/
    info ("Looking for lint types:\n");
    if (settings->searchdup)		info ("\t+ duplicates "RED"(%s)"NCO" [-U]\n", settings->cmd_path ? "cmd" : "rm");
    if (settings->findemptydirs)	info ("\t+ empty directories "RED"(rm)"NCO" [-Y]\n");
    if (settings->listemptyfiles)	info ("\t+ zero size files "RED"(rm)"NCO" [-K]\n");
    if (settings->findbadids)		info ("\t+ files with bad UID/GID "BLU"(chown)"NCO" [-L]\n");
    if (settings->namecluster)		info ("\t+ files with same name "GRE"(info only)"NCO" [-N]\n");
    if (settings->nonstripped)		info ("\t+ non-stripped binaries"BLU"(strip)"RED"(slow)"NCO" [-A]\n");
    if (!settings->searchdup ||
            !settings->findemptydirs ||
            !settings->listemptyfiles ||
            !settings->findbadids ||
            !settings->namecluster ||
            !settings->nonstripped
       ) {
        info (NCO"\tNot looking for:\n");
        if (!settings->searchdup)		info ("\t\tduplicates[-u];\n");
        if (!settings->findemptydirs)	info ("\t\tempty directories[-y];\n");
        if (!settings->listemptyfiles)	info ("\t\tzero size files[-k];\n");
        if (!settings->findbadids)		info ("\t\tfiles with bad UID/GID[-l];\n");
        if (!settings->namecluster)		info ("\t\tfiles with same name[-n];\n");
        if (!settings->nonstripped)		info ("\t\tnon-stripped binaries[-a];\n");
    }

    /*---------------- search paths ---------------*/
    info(NCO"Search paths:\n");
    for(int i = 0; settings->paths[i] != NULL; ++i) {
        if (settings->is_ppath[i]) {
            has_ppath = true;
            warning (GRE"\t(orig)\t+ %s\n"NCO, settings->paths[i] );
        } else {
            info("\t\t+ %s\n", settings->paths[i]);
        }
    }
    if ((settings->paths[1]) && !has_ppath) {
        info("\t[prefix one or more paths with // to flag location of originals]\n");
    }

    /*---------------- search tree options---------*/
    info ("Tree search parameters:\n");
    info ("\t%s hidden files and folders [-%s]\n"NCO,
          settings->ignore_hidden ? "Excluding" : "Including",
          settings->ignore_hidden ? "G" :  "g" );
    info ("\t%s symlinked files and folders [-%s]\n"NCO,
          settings->followlinks ? "Following" : "Excluding",
          settings->followlinks ? "F" : "f" );
    info ("\t%srossing filesystem / mount point boundaries [-%s]\n"NCO,
          settings->samepart ? "Not c" : "C",
          settings->samepart ? "S" : "s");

    if (settings->depth) info("\t Only search %i levels deep into search paths\n", settings->depth);

    /*---------------- file filters ---------------*/

    info("Filtering search based on:\n");

    if (settings->limits_specified) {
        char size_buf_min[128], size_buf_max[128];
        size_to_human_readable(settings->minsize, size_buf_min, sizeof(size_buf_min));
        size_to_human_readable(settings->maxsize, size_buf_max, sizeof(size_buf_max));
        info("\tFile size between %s and %s bytes\n", size_buf_min, size_buf_max);

    } else {
        info("\tNo file size limits [-z \"min-max\"]\n");
    }
    if (settings->must_match_original) {
        info("\tDuplicates must have at least one member in the "GRE"(orig)"NCO" paths indicated above\n");
        if (!has_ppath)
            error(RED"\tWarning: no "GRE"(orig)"RED" paths specified for option -M --mustmatchorig (use //)\n"NCO);
    }

    if (settings->find_hardlinked_dupes) {
        info("\tHardlinked file sets will be treated as duplicates (%s)\n", settings->cmd_path ? settings->cmd_path : "rm");
        info(RED"\t\tBUG"NCO": rmlint currently does not deduplicate hardlinked files with same basename\n");
    } else info("\tHardlinked file sets will not be deduplicated [-H]\n");

    /*---------------- originals selection ranking ---------*/

    info(NCO"Originals selected based on (decreasing priority):    [-D <criteria>]\n");
    if (has_ppath) info("\tpaths indicated "GRE"(orig)"NCO" above\n");

    for (int i = 0; settings->sort_criteria[i]; ++i) {
        switch(settings->sort_criteria[i]) {
        case 'm':
            info("\tKeep oldest modified time\n");
            break;
        case 'M':
            info("\tKeep newest modified time\n");
            break;
        case 'p':
            info("\tKeep first-listed path (above)\n");
            break;
        case 'P':
            info("\tKeep last-listed path (above)\n");
            break;
        case 'a':
            info("\tKeep first alphabetically\n");
            break;
        case 'A':
            info("\tKeep last alphabetically\n");
            break;
        default:
            error(RED"\tWarning: invalid originals ranking option '-D %c'\n"NCO, settings->sort_criteria[i]);
            break;
        }
    }

    if (settings->keep_all_originals) {
        info("\tNote: all originals in "GRE"(orig)"NCO" paths will be kept\n");
    }
    info("\t      "RED"but"NCO" other lint in "GRE"(orig)"NCO" paths may still be deleted\n");

    /*---------------- action mode ---------------*/
    if (settings->mode == RM_MODE_LIST) {
        /* same mode for duplicates and everything else */
        info ("Action for all Lint types:\n");
    } else {
        /* different mode for duplicated vs everything else */
        info("Action for Duplicates:\n\t");
        switch (settings->mode) {
        case RM_MODE_NOASK:
            info(RED"Delete files without asking\n"NCO);
            break;
        case RM_MODE_LINK:
            info(YEL"Replace duplicates with symlink to original\n"NCO);
            break;
        case RM_MODE_CMD:
            info(YEL"Execute command:\n\t\tdupe:'%s'\n\t\torig:'%s'\n"NCO, settings->cmd_path, settings->cmd_orig);
            break;
        default:
            break;
        }
        info ("Action for all other Lint types:\n");
    }

    if (settings->output) {
        info("\tGenerate script %s.sh to run later\n", settings->output);
    } else {
        info("\tDo nothing\n");
    }

    /*--------------- paranoia ---------*/

    if (settings->paranoid) {
        info("Note: paranoid (bit-by-bit) comparison will be used to verify duplicates "RED"(slow)\n"NCO);
    } else {
        info("Note: fingerprint and md5 comparison will be used to identify duplicates "RED"(very slight risk of false positives)"NCO" [-p]");
    }

    /*--------------- confirmation ---------*/

    if (settings->confirm_settings) {
        info(YEL"\n\nPress y or enter to continue, any other key to abort\n");

        scanf("%c", &confirm);
        settings->verbosity = save_verbosity;
        return (confirm == 'y' || confirm == 'Y' || confirm == '\n');
    }

    settings->verbosity = save_verbosity;
    return 1;
}

void rm_session_init(RmSession *session, RmSettings *settings) {
    session->dup_counter = 0;
    session->total_lint_size = 0;
    session->total_files = 0;
    session->userlist = userlist_new();
    session->list = rm_file_list_new();
    session->settings = settings;
    session->aborted = FALSE;

    init_filehandler(session);
}

int rmlint_main(RmSession *session) {
    /* Used only for infomessage */
    session->total_files = 0;

    if(session->settings->mode == RM_MODE_CMD) {
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

    /* Warn if started with sudo. Hack: Just try to get read access to /bin/ls */
    if(!access("/bin/ls", R_OK | W_OK)) {
        warning(YEL"WARN: "NCO"You're running rmlint with privileged rights - \n");
        warning("      Take care of what you're doing!\n\n");
    }
    session->total_files = rmlint_search_tree(session);

    if(session->total_files < 2) {
        warning("No files in cache to search through => No duplicates.\n");
        die(session, 0);
    }

    info("Now in total "YEL"%ld useable file(s)"NCO" in cache.\n", session->total_files);
    if(session->settings->threads > session->total_files) {
        session->settings->threads = session->total_files + 1;
    }
    /* Till this point the list is unsorted
     * The filter alorithms requires the list to be size-sorted,
     * so it can easily filter unique sizes, and build "groups"
     * */
    info("Now finding easy lint...\n");

    /* Apply the prefilter and outsort inique sizes */
    start_processing(session);

    die(session, EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
