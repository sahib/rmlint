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

#include <errno.h>
#include <getopt.h>
#include <search.h>
#include <sys/time.h>
#include <glib.h>

#include "cmdline.h"
#include "preprocess.h"
#include "shredder.h"
#include "utilities.h"
#include "formats.h"

static void show_version(void) {
    fprintf(
        stderr,
        "rmlint-version %s compiled: [%s]-[%s] (rev %s)\n",
        RMLINT_VERSION, __DATE__, __TIME__, RMLINT_VERSION_GIT_REVISION
    );
}

static void show_help(void) {
    if(system("man rmlint") == 0) {
        return;
    }
    if(system("man doc/rmlint.1.gz") == 0) {
        return;
    }

    g_printerr("You have no manpage for rmlint.\n");
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

    if(access(path, R_OK) != 0) {
        rm_log_error(YELLOW"FATAL: "RESET"Can't open directory or file \"%s\": %s\n", path, strerror(errno));
        return FALSE;
    } else {
        settings->is_prefd = g_realloc(settings->is_prefd, sizeof(char) * (index + 1));
        settings->is_prefd[index] = is_pref;
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

static bool parse_output_pair(RmSession *session, const char *pair) {
    char *separator = strchr(pair, ':');
    if(separator == NULL) {
        g_printerr("No format specified in '%s'\n", pair);
        return false;
    }

    char *full_path = separator + 1;
    char format_name[100];
    memset(format_name, 0, sizeof(format_name));
    strncpy(format_name, pair, MIN((long)sizeof(format_name), separator - pair));

    if(!rm_fmt_add(session->formats, format_name, full_path)) {
        g_printerr("Adding -o %s as output failed.\n", pair);
        return false;
    }

    return true;
}

static bool parse_config_pair(RmSession *session, const char *pair) {
    char *domain = strchr(pair, ':');
    if(domain == NULL) {
        g_printerr("No format (format:key[=val]) specified in '%s'\n", pair);
        return false;
    }

    char *key = NULL, *value = NULL;
    char **key_val = g_strsplit(&domain[1], "=", 2);
    int len = g_strv_length(key_val);

    if(len < 1) {
        g_printerr("Missing key (format:key[=val]) in '%s'\n", pair);
        g_strfreev(key_val);
        return false;
    } 
    
    key = g_strdup(key_val[0]);
    if(len == 2) {
        value = g_strdup(key_val[1]);
    } else {
        value = g_strdup("1");
    }
    
    char *formatter = g_strndup(pair, domain - pair);
    rm_fmt_set_config_value(session->formats, formatter, key, value);

    g_strfreev(key_val);
    return true;
}


/* parse comma-separated strong of lint types and set settings accordingly */
typedef struct RmLintTypeOption {
    const char **names;
    bool **enable;
} RmLintTypeOption;

/* compare function for parsing lint type arguments */
int find_line_type_func(const void *v_input, const void *v_option) {
    const char *input = v_input;
    const RmLintTypeOption *option = v_option;

    for(int i = 0; option->names[i]; ++i) {
        if(strcmp(option->names[i], input) == 0) {
            return 0;
        }
    }
    return 1;
}

#define OPTS  (bool *[])
#define NAMES (const char *[])

void parse_lint_types(RmSettings *sets, const char *lint_string) {
    RmLintTypeOption option_table[] = {{
            .names = NAMES{"all", 0},
            .enable = OPTS{
                &sets->findbadids,
                &sets->findbadlinks,
                &sets->findemptydirs,
                &sets->listemptyfiles,
                &sets->namecluster,
                &sets->nonstripped,
                &sets->searchdup,
                0
            }
        }, {
            .names = NAMES{"defaults", 0},
            .enable = OPTS{
                &sets->findbadids,
                &sets->findbadlinks,
                &sets->findemptydirs,
                &sets->listemptyfiles,
                &sets->searchdup,
                0
            },
        }, {
            .names = NAMES{"none", 0},
            .enable = OPTS{0},
        }, {
            .names = NAMES{"badids", "bi", 0},
            .enable = OPTS{&sets->findbadids, 0}
        }, {
            .names = NAMES{"badlinks", "bl", 0},
            .enable = OPTS{&sets->findbadlinks, 0}
        }, {
            .names = NAMES{"emptydirs", "ed", 0},
            .enable = OPTS{&sets->findemptydirs, 0}
        }, {
            .names = NAMES{"emptyfiles", "ef", 0},
            .enable = OPTS{&sets->listemptyfiles, 0}
        }, {
            .names = NAMES{"nameclusters", "nc", 0},
            .enable = OPTS{&sets->namecluster, 0}
        }, {
            .names = NAMES{"nonstripped", "ns", 0},
            .enable = OPTS{&sets->nonstripped, 0}
        }, {
            .names = NAMES{"duplicates", "df", "dupes", 0},
            .enable = OPTS{&sets->searchdup, 0}
        },
    };

    RmLintTypeOption *all_opts = &option_table[0];

    /* split the comma-separates list of options */
    char **lint_types = g_strsplit(lint_string, " ", -1);

    /* iterate over the separated option strings */
    for(int index = 0; lint_types[index]; index++) {
        char *lint_type = lint_types[index];
        char sign = 0;
        if(strspn(lint_type, "+-")) {
            sign = (*lint_type == '+') ? +1 : -1;
        }


        if(index > 0 && sign == 0) {
            g_printerr(YELLOW"Warning: lint types after first should be prefixed with '+' or '-'\n"RESET);
            g_printerr(YELLOW"         or they would over-ride previously set options: [%s]\n"RESET, lint_type);
            continue;
        } else {
            lint_type += ABS(sign);
        }

        /* use lfind to find matching option from array */
        size_t elems = sizeof(option_table) / sizeof(RmLintTypeOption);
        RmLintTypeOption *option = lfind(
                                       lint_type, &option_table,
                                       &elems, sizeof(RmLintTypeOption),
                                       find_line_type_func
                                   );

        /* apply the found option */
        if(option == NULL) {
            g_printerr(YELLOW"Warning: lint type %s not recognised\n"RESET, lint_type);
            continue;
        }

        if (sign == 0) {
            /* not a + or - option - reset all options to off */
            for (int i = 0; all_opts->enable[i]; i++) {
                *all_opts->enable[i] = false;
            }
        }

        /* enable options as appropriate */
        for(int i = 0; option->enable[i]; i++) {
            *option->enable[i] = (sign == -1) ? false : true;
        }
    }

    /* clean up */
    g_strfreev(lint_types);
}

/* Parse the commandline and set arguments in 'settings' (glob. var accordingly) */
char rm_parse_arguments(int argc, const char **argv, RmSession *session) {
    RmSettings *sets = session->settings;

    int choice = -1;
    int verbosity_counter = 2;
    int output_flag_cnt = -1;
    int option_index = 0;
    int path_index = 0;

    /* Rememver arg[cv] for use in the script formatter */
    sets->argv = argv;
    sets->argc = argc;

    while(1) {
        static struct option long_options[] = {
            {"types"               ,  required_argument ,  0 ,  'T'},
            {"threads"             ,  required_argument ,  0 ,  't'},
            {"maxdepth"            ,  required_argument ,  0 ,  'd'},
            {"size"                ,  required_argument ,  0 ,  's'},
            {"sortcriteria"        ,  required_argument ,  0 ,  'S'},
            {"algorithm"           ,  required_argument ,  0 ,  'a'},
            {"output"              ,  required_argument ,  0 ,  'o'},
            {"loud"                ,  no_argument       ,  0 ,  'v'},
            {"quiet"               ,  no_argument       ,  0 ,  'V'},
            {"with-color"          ,  no_argument       ,  0 ,  'w'},
            {"no-with-color"       ,  no_argument       ,  0 ,  'W'},
            {"no-hidden"           ,  no_argument       ,  0 ,  'R'},
            {"hidden"              ,  no_argument       ,  0 ,  'r'},
            {"followlinks"         ,  no_argument       ,  0 ,  'f'},
            {"no-followlinks"      ,  no_argument       ,  0 ,  'F'},
            {"crossdev"            ,  no_argument       ,  0 ,  'x'},
            {"no-crossdev"         ,  no_argument       ,  0 ,  'X'},
            {"paranoid"            ,  no_argument       ,  0 ,  'p'},
            {"no-paranoid"         ,  no_argument       ,  0 ,  'P'},
            {"keepall//"           ,  no_argument       ,  0 ,  'k'},
            {"no-keepall//"        ,  no_argument       ,  0 ,  'K'},
            {"mustmatch//"         ,  no_argument       ,  0 ,  'M'},
            {"no-mustmatch//"      ,  no_argument       ,  0 ,  'm'},
            {"invert//"            ,  no_argument       ,  0 ,  'i'},
            {"no-invert//"         ,  no_argument       ,  0 ,  'I'},
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
                     argc, (char **)argv,
                     "T:t:d:s:o:S:a:c:vVwWrRfFXxpPkKmMiIlLqQhH",
                     long_options, &option_index
                 );

        /* Detect the end of the options. */
        if(choice == -1) {
            break;
        }
        switch(choice) {
        case '?':
            show_help();
            return 0;
        case 'c':
            parse_config_pair(session, optarg);
            break;
        case 'T':
            parse_lint_types(sets, optarg);
            break;
        case 't': {
            int parsed_threads = strtol(optarg, NULL, 10);
            if(parsed_threads > 0) {
                sets->threads = parsed_threads;
            } else {
                rm_log_error(RED"Invalid thread count supplied: %s\n"RESET, optarg);
            }
        }
        break;
        case 'a':
            sets->checksum_type = rm_string_to_digest_type(optarg);
            if(sets->checksum_type == RM_DIGEST_UNKNOWN) {
                rm_log_error(RED"Unknown hash algorithm: '%s'\n"RESET, optarg);
                die(session, EXIT_FAILURE);
            }
            break;
        case 'f':
            sets->followlinks = 1;
            break;
        case 'F':
            sets->followlinks = 0;
            break;
        case 'w':
            sets->color = isatty(fileno(stdout));
            break;
        case 'W':
            sets->color = 0;
            break;
        case 'H':
            show_version();
            die(session, EXIT_SUCCESS);
            break;
        case 'h':
            show_help();
            show_version();
            die(session, EXIT_SUCCESS);
            break;
        case 'l':
            sets->find_hardlinked_dupes = 1;
            break;
        case 'L':
            sets->find_hardlinked_dupes = 0;
            break;
        case 'o':
            if(output_flag_cnt < 0) {
                output_flag_cnt = 0;
            }
            output_flag_cnt += parse_output_pair(session, optarg);
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
            sets->samepart = 0;
            break;
        case 'X':
            sets->samepart = 1;
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
        case 'P':
            sets->paranoid = 0;
            break;
        default:
            return 0;
        }
    }

    if(output_flag_cnt == -1) {
        /* Set default outputs */
        rm_fmt_add(session->formats, "pretty", "stdout");
        rm_fmt_add(session->formats, "sh", "rmlint.log");
    } else if(output_flag_cnt == 0) {
        /* There was no valid output flag given, but the user tried */
        g_printerr("No valid -o flag encountered.\n");
        exit(EXIT_FAILURE);
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
        sets->is_prefd = g_malloc0(sizeof(char));
        sets->is_prefd[0] = 0;
        sets->num_paths++;
        if(!sets->paths[0]) {
            rm_log_error(YELLOW"FATAL: "RESET"Cannot get working directory: "YELLOW"%s\n"RESET, strerror(errno));
            rm_log_error("       Are you maybe in a dir that just had been removed?\n");
            g_free(sets->paths);
            return 0;
        }
    }
    return 1;
}

/* exit and return to calling method */
void die(RmSession *session, int status) {
    rm_session_clear(session);
    if(status) {
        rm_log_info("Abnormal exit\n");
    }

    exit(status);
}

char rm_echo_settings(RmSettings *settings) {
    char confirm;
    bool has_ppath = false;

    /* I've disabled this for now. Shouldn't we only print a summary
     * if the user has time to read it? */
    if(!settings->confirm_settings) {
        return 1;
    }

    rm_log_warning(BLUE"Running rmlint with the following settings:\n"RESET);
    rm_log_warning("(Note "BLUE"[*]"RESET" hints below to change options)\n"RESET);

    /*---------------- lint types ---------------*/
    rm_log_warning("Looking for lint types:\n");
    if (settings->searchdup)	rm_log_warning("\t+ duplicates "RED"(%s)"RESET" [-U]\n", settings->cmd_path ? "cmd" : "rm");
    if (settings->findemptydirs)rm_log_warning("\t+ empty directories "RED"(rm)"RESET" [-Y]\n");
    if (settings->listemptyfiles)rm_log_warning("\t+ zero size files "RED"(rm)"RESET" [-K]\n");
    if (settings->findbadids)	rm_log_warning("\t+ files with bad UID/GID "BLUE"(chown)"RESET" [-L]\n");
    if (settings->namecluster)	rm_log_warning("\t+ files with same name "GREEN"(info only)"RESET" [-N]\n");
    if (settings->nonstripped)	rm_log_warning("\t+ non-stripped binaries"BLUE"(strip)"RED"(slow)"RESET" [-A]\n");
    if (!settings->searchdup ||
            !settings->findemptydirs ||
            !settings->listemptyfiles ||
            !settings->findbadids ||
            !settings->namecluster ||
            !settings->nonstripped
       ) {
        rm_log_warning(RESET"\tNot looking for:\n");
        if (!settings->searchdup)	rm_log_warning("\t\tduplicates[-u];\n");
        if (!settings->findemptydirs)rm_log_warning("\t\tempty directories[-y];\n");
        if (!settings->listemptyfiles)rm_log_warning("\t\tzero size files[-k];\n");
        if (!settings->findbadids)	rm_log_warning("\t\tfiles with bad UID/GID[-l];\n");
        if (!settings->namecluster)	rm_log_warning("\t\tfiles with same name[-n];\n");
        if (!settings->nonstripped)	rm_log_warning("\t\tnon-stripped binaries[-a];\n");
    }

    /*---------------- search paths ---------------*/
    rm_log_warning(RESET"Search paths:\n");
    for(int i = 0; settings->paths[i] != NULL; ++i) {
        if (settings->is_prefd[i]) {
            has_ppath = true;
            rm_log_warning (GREEN"\t(orig)\t+ %s\n"RESET, settings->paths[i] );
        } else {
            rm_log_warning("\t\t+ %s\n", settings->paths[i]);
        }
    }
    if ((settings->paths[1]) && !has_ppath) {
        rm_log_warning("\t[prefix one or more paths with // to flag location of originals]\n");
    }

    /*---------------- search tree options---------*/
    rm_log_warning("Tree search parameters:\n");
    rm_log_warning("\t%s hidden files and folders [-%s]\n"RESET,
                   settings->ignore_hidden ? "Excluding" : "Including",
                   settings->ignore_hidden ? "G" :  "g" );
    rm_log_warning("\t%s symlinked files and folders [-%s]\n"RESET,
                   settings->followlinks ? "Following" : "Excluding",
                   settings->followlinks ? "F" : "f" );
    rm_log_warning("\t%srossing filesystem / mount point boundaries [-%s]\n"RESET,
                   settings->samepart ? "Not c" : "C",
                   settings->samepart ? "S" : "s");

    if (settings->depth) rm_log_warning("\t Only search %i levels deep into search paths\n", settings->depth);

    /*---------------- file filters ---------------*/

    rm_log_warning("Filtering search based on:\n");

    if (settings->limits_specified) {
        char size_buf_min[128], size_buf_max[128];
        rm_util_size_to_human_readable(settings->minsize, size_buf_min, sizeof(size_buf_min));
        rm_util_size_to_human_readable(settings->maxsize, size_buf_max, sizeof(size_buf_max));
        rm_log_warning("\tFile size between %s and %s bytes\n", size_buf_min, size_buf_max);

    } else {
        rm_log_warning("\tNo file size limits [-z \"min-max\"]\n");
    }
    if (settings->must_match_original) {
        rm_log_warning("\tDuplicates must have at least one member in the "GREEN"(orig)"RESET" paths indicated above\n");
        if (!has_ppath)
            rm_log_error(RED"\tWarning: no "GREEN"(orig)"RED" paths specified for option -M --mustmatchorig (use //)\n"RESET);
    }

    if (settings->find_hardlinked_dupes) {
        rm_log_warning("\tHardlinked file sets will be treated as duplicates (%s)\n", settings->cmd_path ? settings->cmd_path : "rm");
        rm_log_warning(RED"\t\tBUG"RESET": rmlint currently does not deduplicate hardlinked files with same basename\n");
    } else rm_log_warning("\tHardlinked file sets will not be deduplicated [-H]\n");

    /*---------------- originals selection ranking ---------*/

    rm_log_warning(RESET"Originals selected based on (decreasing priority):    [-D <criteria>]\n");
    if (has_ppath) rm_log_warning("\tpaths indicated "GREEN"(orig)"RESET" above\n");

    for (int i = 0; settings->sort_criteria[i]; ++i) {
        switch(settings->sort_criteria[i]) {
        case 'm':
            rm_log_warning("\tKeep oldest modified time\n");
            break;
        case 'M':
            rm_log_warning("\tKeep newest modified time\n");
            break;
        case 'p':
            rm_log_warning("\tKeep first-listed path (above)\n");
            break;
        case 'P':
            rm_log_warning("\tKeep last-listed path (above)\n");
            break;
        case 'a':
            rm_log_warning("\tKeep first alphabetically\n");
            break;
        case 'A':
            rm_log_warning("\tKeep last alphabetically\n");
            break;
        default:
            rm_log_error(RED"\tWarning: invalid originals ranking option '-D %c'\n"RESET, settings->sort_criteria[i]);
            break;
        }
    }

    if (settings->keep_all_originals) {
        rm_log_warning("\tNote: all originals in "GREEN"(orig)"RESET" paths will be kept\n");
    }
    rm_log_warning("\t      "RED"but"RESET" other lint in "GREEN"(orig)"RESET" paths may still be deleted\n");

    /*--------------- paranoia ---------*/

    if (settings->paranoid) {
        rm_log_warning("Note: paranoid (bit-by-bit) comparison will be used to verify duplicates "RED"(slow)\n"RESET);
    } else {
        rm_log_warning("Note: fingerprint and md5 comparison will be used to identify duplicates "RED"(very slight risk of false positives)"RESET" [-p]");
    }

    /*--------------- confirmation ---------*/

    if (settings->confirm_settings) {
        rm_log_warning(YELLOW"\n\nPress y or enter to continue, any other key to abort\n");

        if(scanf("%c", &confirm) == EOF) {
            rm_log_warning(RED"Reading your input failed."RESET);
        }

        return (confirm == 'y' || confirm == 'Y' || confirm == '\n');
    }

    return 1;
}

int rm_main(RmSession *session) {
    rm_fmt_set_state(session->formats, RM_PROGREENSS_STATE_TRAVERSE, 0, 0);
    session->total_files = rm_search_tree(session);

    rm_log_warning("List build finished at %.3f with %d files\n", g_timer_elapsed(session->timer, NULL), (int)session->total_files);

    if(session->total_files < 2) {
        rm_log_warning("No files in cache to search through => No duplicates.\n");
        die(session, EXIT_SUCCESS);
    }

    rm_log_info("Now in total "YELLOW"%ld useable file(s)"RESET" in cache.\n", session->total_files);
    if(session->settings->threads > session->total_files) {
        session->settings->threads = session->total_files + 1;
    }

    rm_fmt_set_state(session->formats, RM_PROGREENSS_STATE_PREPROCESS, 0, 0);
    guint64 other_lint = rm_preprocess(session);
    char lintbuf[128];

    rm_fmt_set_state(session->formats, RM_PROGREENSS_STATE_SHREDDER, 0, 0);
    rm_shred_run(session, session->tables->dev_table, session->tables->size_table);

    rm_fmt_set_state(session->formats, RM_PROGREENSS_STATE_SUMMARY, 0, 0);

    // TODO: remove this below and add a summary-formatter.
    rm_log_error("Dupe search finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));

    if(session->dup_counter == 0) {
        rm_log_error("\r                    ");
    } else {
        rm_log_error("\n");
    }

    rm_util_size_to_human_readable(session->total_lint_size, lintbuf, sizeof(lintbuf));
    rm_log_warning(
        "\n"RED"=> "RESET"In total "RED"%lu"RESET" files, whereof "RED"%lu"RESET" are duplicate(s) in "RED"%lu"RESET" groups",
        session->total_files, session->dup_counter, session->dup_group_counter
    );

    if(other_lint > 0) {
        rm_util_size_to_human_readable(other_lint, lintbuf, sizeof(lintbuf));
        rm_log_warning(RED"\n=> %lu"RESET" other suspicious items found ["GREEN"%s"RESET"]", other_lint, lintbuf);
    }

    rm_log_warning("\n");
    if(!session->aborted) {
        rm_util_size_to_human_readable(session->total_lint_size, lintbuf, sizeof(lintbuf));
        rm_log_warning(
            RED"=> "RESET"Totally "GREEN" %s "RESET" [%lu Bytes] can be removed.\n",
            lintbuf, session->total_lint_size
        );
    }

    rm_fmt_set_state(session->formats, RM_PROGREENSS_STATE_SUMMARY, 0, 0);
    rm_session_clear(session);
    return EXIT_SUCCESS;
}
