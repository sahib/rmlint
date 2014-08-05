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

#include <sys/time.h>

#include "rmlint.h"
#include "mode.h"
#include "list.h"
#include "traverse.h"
#include "filter.h"
#include "linttests.h"

/* Version string */
static void print_version(void) {
    fprintf(stderr, "rmlint-version %s compiled: [%s]-[%s] (rev %s)\n", RMLINT_VERSION, __DATE__, __TIME__, RMLINT_VERSION_GIT_REVISION);
}

/* ------------------------------------------------------------- */

/* Help text */
static void print_help(void) {
    if(system("man rmlint") == 0) {
        return;
    }
    if(system("man doc/rmlint.1.gz") == 0) {
        return;
    }

    g_printerr("You have no manpage for rmlint.\n");
}

/* ------------------------------------------------------------- */

/* Options not specified by commandline get a default option - this called before rm_parse_arguments */
void rm_set_default_settings(RmSettings *pset) {
    pset->mode                  = RM_MODE_LIST;       /* list only    */
    pset->paranoid              = 0;                  /* dont be bush */
    pset->depth                 = PATH_MAX / 2;       /* max tree depth*/
    pset->followlinks           = 0;                  /* fol. link    */
    pset->threads               = 16;                 /* Quad*quad.   */
    pset->verbosity             = G_LOG_LEVEL_INFO;   /* Most relev.  */
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
    pset->color                 = isatty(fileno(stdout));
    pset->findbadids            = 1;
    pset->output_log            = "rmlint.log";
    pset->output_script         = "rmlint.sh";
    pset->limits_specified      = 0;
    pset->checksum_type         = RM_DIGEST_SPOOKY;
    pset->minsize               = 0;
    pset->maxsize               = G_MAXUINT64;
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
    pset->collide               = 0;
    pset->num_paths             = 0;
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

/* Size spec parsing implemented by qitta (http://github.com/qitta)
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
        rm_error(YEL"FATAL: "NCO"Can't open directory or file \"%s\": %s\n", path, strerror(errno));
        return FALSE;
    } else {
        settings->is_ppath = g_realloc(settings->is_ppath, sizeof(char) * (index + 1));
        settings->is_ppath[index] = is_pref;
        settings->paths = g_realloc(settings->paths, sizeof(char *) * (index + 2));

        if (path[0] == '/') {
            /* It's the full path already*/
            settings->paths[index + 0] = g_strdup(path);
        } else {
            settings->paths[index + 0] = g_strdup_printf("%s%s", settings->iwd, path);
        }
        settings->paths[index + 1] = NULL;
        settings->num_paths++;
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
char rm_parse_arguments(int argc, char **argv, RmSession *session) {
    RmSettings *sets = session->settings;

    int choice = -1;
    int verbosity_counter = 2;
    int option_index = 0;
    int path_index = 0;

    while(1) {
        static struct option long_options[] = {
            {"threads"             ,  required_argument ,  0 ,  't'},
            {"mode"                ,  required_argument ,  0 ,  'm'},
            {"maxdepth"            ,  required_argument ,  0 ,  'd'},
            {"cmd-dup"             ,  required_argument ,  0 ,  'c'},
            {"cmd-orig"            ,  required_argument ,  0 ,  'C'},
            {"size"                ,  required_argument ,  0 ,  's'},
            {"sortcriteria"        ,  required_argument ,  0 ,  'S'},
            {"algorithm"           ,  required_argument ,  0 ,  'a'},
            {"output-script"       ,  optional_argument ,  0 ,  'o'},
            {"output-log"          ,  optional_argument ,  0 ,  'O'},
            {"loud"                ,  no_argument       ,  0 ,  'v'},
            {"quiet"               ,  no_argument       ,  0 ,  'V'},
            {"emptyfiles"          ,  no_argument       ,  0 ,  'e'},
            {"no-emptyfiles"       ,  no_argument       ,  0 ,  'E'},
            {"with-color"          ,  no_argument       ,  0 ,  'w'},
            {"no-with-color"       ,  no_argument       ,  0 ,  'W'},
            {"emptydirs"           ,  no_argument       ,  0 ,  'z'},
            {"no-emptydirs"        ,  no_argument       ,  0 ,  'Z'},
            {"namecluster"         ,  no_argument       ,  0 ,  'n'},
            {"no-namecluster"      ,  no_argument       ,  0 ,  'N'},
            {"nonstripped"         ,  no_argument       ,  0 ,  'b'},
            {"no-nonstripped"      ,  no_argument       ,  0 ,  'B'},
            {"no-hidden"           ,  no_argument       ,  0 ,  'R'},
            {"hidden"              ,  no_argument       ,  0 ,  'r'},
            {"badids"              ,  no_argument       ,  0 ,  'g'},
            {"no-badids"           ,  no_argument       ,  0 ,  'G'},
            {"dups"                ,  no_argument       ,  0 ,  'u'},
            {"no-dups"             ,  no_argument       ,  0 ,  'U'},
            {"followlinks"         ,  no_argument       ,  0 ,  'f'},
            {"no-followlinks"      ,  no_argument       ,  0 ,  'F'},
            {"crossdev"            ,  no_argument       ,  0 ,  'x'},
            {"no-crossdev"         ,  no_argument       ,  0 ,  'X'},
            {"paranoid"            ,  no_argument       ,  0 ,  'p'},
            {"no-paranoid"         ,  no_argument       ,  0 ,  'P'},
            {"keepallorig"         ,  no_argument       ,  0 ,  'k'},
            {"no-keepallorig"      ,  no_argument       ,  0 ,  'K'},
            {"mustmatchorig"       ,  no_argument       ,  0 ,  'm'},
            {"no-mustmatchorig"    ,  no_argument       ,  0 ,  'M'},
            {"invertorig"          ,  no_argument       ,  0 ,  'i'},
            {"no-invertorig"       ,  no_argument       ,  0 ,  'I'},
            {"hardlinked"          ,  no_argument       ,  0 ,  'l'},
            {"no-hardlinked"       ,  no_argument       ,  0 ,  'L'},
            {"confirm-settings"    ,  no_argument       ,  0 ,  'q'},
            {"no-confirm-settings" ,  no_argument       ,  0 ,  'Q'},
            {"help"                ,  no_argument       ,  0 ,  'h'},
            {"version"             ,  no_argument       ,  0 ,  'H'},
            {0, 0, 0, 0}
        };

        /* getopt_long stores the option index here. */
        choice = getopt_long(
                     argc, argv,
                     "t:m:d:c:C:s:o::O::S:a:vVeEwWzZnNbBrRgGuUfFXxpPkKmMiIlLqQhH",
                     long_options, &option_index
                 );

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
        case 'a':
            sets->checksum_type = rm_string_to_digest_type(optarg);
            if(sets->checksum_type == RM_DIGEST_UNKNOWN) {
                rm_error(RED"Unknown hash algorithm: '%s'\n", optarg);
                die(session, EXIT_FAILURE);
            }
            break;
        case 'e':
            sets->listemptyfiles = 1;
            break;
        case 'E':
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
        case 'w':
            sets->color = isatty(fileno(stdout));
            break;
        case 'W':
            sets->color = 0;
            break;
        case 'H':
            print_version();
            die(session, EXIT_SUCCESS);
            break;
        case 'h':
            print_help();
            print_version();
            die(session, EXIT_SUCCESS);
            break;
        case 'l':
            sets->find_hardlinked_dupes = 1;
            break;
        case 'L':
            sets->find_hardlinked_dupes = 0;
            break;
        case 'z':
            sets->findemptydirs = 1;
            break;
        case 'Z':
            sets->findemptydirs = 0;
            break;
        case 'b':
            sets->nonstripped = 1;
            break;
        case 'B':
            sets->nonstripped = 0;
            break;
        case 'o':
            sets->output_script = (optarg && *optarg) ? optarg : NULL;
            break;
        case 'O':
            sets->output_log = (optarg && *optarg) ? optarg : NULL;
            break;
        case 'c':
            sets->cmd_path = optarg;
            break;
        case 'C':
            sets->cmd_orig = optarg;
            break;
        case 'R':
            sets->ignore_hidden = 1;
            break;
        case 'r':
            sets->ignore_hidden = 0;
            break;
        case 'V':
            verbosity_counter--;
            break;
        case 'v':
            verbosity_counter++;
            break;
        case 'x':
            sets->samepart = 1;
            break;
        case 'X':
            sets->samepart = 0;
            break;
        case 'd':
            sets->depth = ABS(atoi(optarg));
            break;
        case 'S':
            sets->sort_criteria = optarg;
            break;
        case 'p':
            sets->paranoid = 1;
            break;
        case 'k':
            sets->keep_all_originals = 1;
            break;
        case 'K':
            sets->keep_all_originals = 1;
            break;
        case 'M':
            sets->must_match_original = 1;
            break;
        case 'i':
            sets->invert_original = 1;
            break;
        case 'I':
            sets->invert_original = 0;
            break;
        case 'Q':
            sets->confirm_settings = 0;
            break;
        case 'q':
            sets->confirm_settings = 1;
            break;
        case 's':
            sets->limits_specified = 1;
            parse_limit_sizes(session, optarg);
            break;
        case 'g':
            sets->findbadids = true;
            break;
        case 'G':
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
                rm_error(YEL"FATAL: "NCO"Invalid value for --mode [-m]\n");
                rm_error("       Available modes are: list | link | noask | cmd\n");
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

    /* Get current directory */
    char cwd_buf[PATH_MAX + 1];
    getcwd(cwd_buf, PATH_MAX);
    sets->iwd = g_strdup_printf("%s%s", cwd_buf, G_DIR_SEPARATOR_S);

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
        sets->num_paths++;
        if(!sets->paths[0]) {
            rm_error(YEL"FATAL: "NCO"Cannot get working directory: "YEL"%s\n"NCO, strerror(errno));
            rm_error("       Are you maybe in a dir that just had been removed?\n");
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
int die(RmSession *session, int status) {
    RmSettings *sets = session->settings;

    /* Free mem */
    if(sets->paths) {
        for(int i = 0; sets->paths[i]; ++i) {
            g_free(sets->paths[i]);
        }
        g_free(sets->paths);
    }

    g_free(sets->is_ppath);
    g_free(sets->iwd);

    if(status) {
        info("Abnormal exit\n");
    }

    /* Close logfile */
    if(session->log_out) {
        fclose(session->log_out);
    }

    /* Close scriptfile */
    if(session->script_out) {
        fprintf(
            session->script_out,
            "                      \n"
            "if [ -z $DO_REMOVE ]  \n"
            "then                  \n"
            "  %s %s;              \n"
            "  %s %s;              \n"
            "fi                    \n",
            (session->settings->output_script) ? "rm -rf" : "",
            (session->settings->output_script) ? (session->settings->output_script) : "",
            (session->settings->output_log) ? "rm -rf" : "",
            (session->settings->output_log) ? (session->settings->output_log) : ""
        );
        fclose(session->script_out);
    }

    exit(status);
    return status;
}

char rm_echo_settings(RmSettings *settings) {
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
            rm_error(RED"\tWarning: no "GRE"(orig)"RED" paths specified for option -M --mustmatchorig (use //)\n"NCO);
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
            rm_error(RED"\tWarning: invalid originals ranking option '-D %c'\n"NCO, settings->sort_criteria[i]);
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

    if (settings->output_script) {
        info("\tGenerate script %s to run later\n", settings->output_script);
    } else {
        info("\tWrite no script.\n");
    }
    if (settings->output_log) {
        info("\tGenerate log %s\n", settings->output_log);
    } else {
        info("\tWrite no log.\n");
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

        if(scanf("%c", &confirm) == EOF) {
            info(RED"Reading your input failed."NCO);
        }

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
    session->mounts = rm_mounts_table_new();
    session->list = rm_file_list_new(session->mounts);
    session->settings = settings;
    session->aborted = FALSE;
    session->activethreads = 0; /* foreground thread not counted as 1 */

    pthread_mutex_init(&session->threadlock , NULL);/*lock for manipulating activethreads var*/

    init_filehandler(session);
}

int rm_main(RmSession *session) {
    /* Used only for infomessage */
    struct timeval start, end;
    float secs_used;

    session->total_files = 0;

    if(session->settings->mode == RM_MODE_CMD) {
        if(session->settings->cmd_orig) {
            if(check_cmd(session->settings->cmd_orig) == 0) {
                die(session, EXIT_FAILURE);
            }
        }
        if(session->settings->cmd_path) {
            if(check_cmd(session->settings->cmd_path) == 0) {
                die(session, EXIT_FAILURE);
            }
        }
    }

    /* Warn if started with sudo. Hack: Just try to get read access to /bin/ls */
    if(!access("/bin/ls", R_OK | W_OK)) {
        warning(YEL"WARN: "NCO"You're running rmlint with privileged rights - \n");
        warning("      Take care of what you're doing!\n\n");
    }


    gettimeofday(&start, NULL);
    session->total_files = rm_search_tree(session);
    gettimeofday(&end, NULL);
    secs_used = (float)(end.tv_sec - start.tv_sec) + (float)(end.tv_usec - start.tv_usec) / 1000000.0;

    warning("List build time %.6f with %d files\n", secs_used, (int)session->total_files);

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

    return die(session, EXIT_SUCCESS);
}
