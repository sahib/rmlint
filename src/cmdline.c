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

/* exit and return to calling method */
static void rm_cmd_die(RmSession *session, int status) {
    rm_session_clear(session);
    if(status) {
        rm_log_info("Abnormal exit\n");
    }

    exit(status);
}

static void rm_cmd_show_version(void) {
    fprintf(
        stderr,
        "rmlint-version %s compiled: [%s]-[%s] (rev %s)\n",
        RMLINT_VERSION, __DATE__, __TIME__, RMLINT_VERSION_GIT_REVISION
    );
}

static void rm_cmd_show_help(void) {
    if(system("man doc/rmlint.1.gz") == 0) {
        return;
    }

    if(system("man rmlint") == 0) {
        return;
    }
    rm_log_error("You seem to have no manpage for rmlint.\n");
}

/* Check if this is the 'preferred' dir */
bool rm_cmd_check_if_preferred(const char *dir) {
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

static int rm_cmd_size_format_error(const char **error, const char *msg) {
    if(error) {
        *error = msg;
    }
    return 0;
}

static int rm_cmd_compare_spec_elem(const void *fmt_a, const void *fmt_b) {
    return strcasecmp(((FormatSpec *)fmt_a)->id, ((FormatSpec *)fmt_b)->id);
}

guint64 rm_cmd_size_string_to_bytes(const char *size_spec, const char **error) {
    if (size_spec == NULL) {
        return rm_cmd_size_format_error(error, "Input size is NULL");
    }

    char *format = NULL;
    long double decimal = strtold(size_spec, &format);

    if (decimal == 0 && format == size_spec) {
        return rm_cmd_size_format_error(error, "This does not look like a number");
    } else if (decimal < 0) {
        return rm_cmd_size_format_error(error, "Negativ sizes are no good idea");
    } else if (*format) {
        format = g_strstrip(format);
    } else {
        return round(decimal);
    }

    FormatSpec key = {.id = format};
    FormatSpec *found = bsearch(
                            &key, SIZE_FORMAT_TABLE,
                            SIZE_FORMAT_TABLE_N, sizeof(FormatSpec),
                            rm_cmd_compare_spec_elem
                        );

    if (found != NULL) {
        /* No overflow check */
        return decimal * powl(found->base, found->exponent);
    } else {
        return rm_cmd_size_format_error(error, "Given format specifier not found");
    }
}

/* Size spec parsing implemented by qitta (http://github.com/qitta)
 * Thanks and go blame him if this breaks!
 */
static gboolean rm_cmd_size_range_string_to_bytes(const char *range_spec, guint64 *min, guint64 *max, const char **error) {
    *min = 0;
    *max = G_MAXULONG;

    const char *tmp_error = NULL;
    gchar **split = g_strsplit(range_spec, "-", 2);

    if(split[0] != NULL) {
        *min = rm_cmd_size_string_to_bytes(split[0], &tmp_error);
    }

    if(split[1] != NULL && tmp_error == NULL) {
        *max = rm_cmd_size_string_to_bytes(split[1], &tmp_error);
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

static void rm_cmd_parse_limit_sizes(RmSession *session, char *range_spec) {
    const char *error = NULL;
    if(!rm_cmd_size_range_string_to_bytes(
                range_spec,
                &session->settings->minsize,
                &session->settings->maxsize,
                &error
            )) {
        rm_log_error(RED"Error while parsing --limit: %s\n"RESET, error);
        rm_cmd_die(session, EXIT_FAILURE);
    }
}

static GLogLevelFlags VERBOSITY_TO_LOG_LEVEL[] = {
    [0] = G_LOG_LEVEL_CRITICAL,
    [1] = G_LOG_LEVEL_ERROR,
    [2] = G_LOG_LEVEL_WARNING,
    [3] = G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO,
    [4] = G_LOG_LEVEL_DEBUG
};

static bool rm_cmd_add_path(RmSession *session, int index, const char *path) {
    RmSettings *settings = session->settings;
    bool is_pref = rm_cmd_check_if_preferred(path);

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
        return TRUE;
    }
}

static int rm_cmd_read_paths_from_stdin(RmSession *session, int index) {
    int paths_added = 0;
    char path_buf[PATH_MAX];

    while(fgets(path_buf, PATH_MAX, stdin)) {
        paths_added += rm_cmd_add_path(session, index + paths_added, strtok(path_buf, "\n"));
    }

    return paths_added;
}

static bool rm_cmd_parse_output_pair(RmSession *session, const char *pair) {
    char *separator = strchr(pair, ':');
    if(separator == NULL) {
        rm_log_error(RED"No format specified in '%s'\n"RESET, pair);
        return false;
    }

    char *full_path = separator + 1;
    char format_name[100];
    memset(format_name, 0, sizeof(format_name));
    strncpy(format_name, pair, MIN((long)sizeof(format_name), separator - pair));

    if(!rm_fmt_add(session->formats, format_name, full_path)) {
        rm_log_warning(YELLOW"Adding -o %s as output failed.\n"RESET, pair);
        return false;
    }

    return true;
}

static bool rm_cmd_parse_config_pair(RmSession *session, const char *pair) {
    char *domain = strchr(pair, ':');
    if(domain == NULL) {
        rm_log_warning(YELLOW"No format (format:key[=val]) specified in '%s'\n"RESET, pair);
        return false;
    }

    char *key = NULL, *value = NULL;
    char **key_val = g_strsplit(&domain[1], "=", 2);
    int len = g_strv_length(key_val);

    if(len < 1) {
        rm_log_warning(YELLOW"Missing key (format:key[=val]) in '%s'\n"RESET, pair);
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
int rm_cmd_find_line_type_func(const void *v_input, const void *v_option) {
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

static void rm_cmd_parse_lint_types(RmSettings *settings, const char *lint_string) {
    RmLintTypeOption option_table[] = {{
            .names = NAMES{"all", 0},
            .enable = OPTS{
                &settings->findbadids,
                &settings->findbadlinks,
                &settings->findemptydirs,
                &settings->listemptyfiles,
                &settings->namecluster,
                &settings->nonstripped,
                &settings->searchdup,
                0
            }
        }, {
            .names = NAMES{"defaults", 0},
            .enable = OPTS{
                &settings->findbadids,
                &settings->findbadlinks,
                &settings->findemptydirs,
                &settings->listemptyfiles,
                &settings->searchdup,
                0
            },
        }, {
            .names = NAMES{"none", 0},
            .enable = OPTS{0},
        }, {
            .names = NAMES{"badids", "bi", 0},
            .enable = OPTS{&settings->findbadids, 0}
        }, {
            .names = NAMES{"badlinks", "bl", 0},
            .enable = OPTS{&settings->findbadlinks, 0}
        }, {
            .names = NAMES{"emptydirs", "ed", 0},
            .enable = OPTS{&settings->findemptydirs, 0}
        }, {
            .names = NAMES{"emptyfiles", "ef", 0},
            .enable = OPTS{&settings->listemptyfiles, 0}
        }, {
            .names = NAMES{"nameclusters", "nc", 0},
            .enable = OPTS{&settings->namecluster, 0}
        }, {
            .names = NAMES{"nonstripped", "ns", 0},
            .enable = OPTS{&settings->nonstripped, 0}
        }, {
            .names = NAMES{"duplicates", "df", "dupes", 0},
            .enable = OPTS{&settings->searchdup, 0}
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
            rm_log_warning(YELLOW"Warning: lint types after first should be prefixed with '+' or '-'\n"RESET);
            rm_log_warning(YELLOW"         or they would over-ride previously set options: [%s]\n"RESET, lint_type);
            continue;
        } else {
            lint_type += ABS(sign);
        }

        /* use lfind to find matching option from array */
        size_t elems = sizeof(option_table) / sizeof(RmLintTypeOption);
        RmLintTypeOption *option = lfind(
                                       lint_type, &option_table,
                                       &elems, sizeof(RmLintTypeOption),
                                       rm_cmd_find_line_type_func
                                   );

        /* apply the found option */
        if(option == NULL) {
            rm_log_warning(YELLOW"Warning: lint type %s not recognised\n"RESET, lint_type);
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
bool rm_cmd_parse_args(int argc, const char **argv, RmSession *session) {
    RmSettings *settings = session->settings;

    int choice = -1;
    int verbosity_counter = 2;
    int output_flag_cnt = -1;
    int option_index = 0;
    int path_index = 0;

    /* set to true if -o or -O is specified */
    bool oO_specified[2] = {false, false};

    while(1) {
        static struct option long_options[] = {
            {"types"               ,  required_argument ,  0 ,  'T'},
            {"threads"             ,  required_argument ,  0 ,  't'},
            {"max-depth"           ,  required_argument ,  0 ,  'd'},
            {"size"                ,  required_argument ,  0 ,  's'},
            {"sortcriteria"        ,  required_argument ,  0 ,  'S'},
            {"algorithm"           ,  required_argument ,  0 ,  'a'},
            {"output"              ,  required_argument ,  0 ,  'o'},
            {"add-output"          ,  required_argument ,  0 ,  'O'},
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
                     "T:t:d:s:o:O:S:a:c:vVwWrRfFXxpPkKmMlLqQhH",
                     long_options, &option_index
                 );

        /* Detect the end of the options. */
        if(choice == -1) {
            break;
        }
        switch(choice) {
        case '?':
            rm_cmd_show_help();
            return false;
        case 'c':
            rm_cmd_parse_config_pair(session, optarg);
            break;
        case 'T':
            rm_cmd_parse_lint_types(settings, optarg);
            break;
        case 't': {
            int parsed_threads = strtol(optarg, NULL, 10);
            if(parsed_threads > 0) {
                settings->threads = parsed_threads;
            } else {
                rm_log_error(RED"Invalid thread count supplied: %s\n"RESET, optarg);
            }
        }
        break;
        case 'a':
            settings->checksum_type = rm_string_to_digest_type(optarg);
            if(settings->checksum_type == RM_DIGEST_UNKNOWN) {
                rm_log_error(RED"Unknown hash algorithm: '%s'\n"RESET, optarg);
                rm_cmd_die(session, EXIT_FAILURE);
            }
            break;
        case 'f':
            settings->followlinks = true;
            break;
        case 'F':
            settings->followlinks = false;
            break;
        case 'w':
            settings->color = isatty(fileno(stdout));
            break;
        case 'W':
            settings->color = false;
            break;
        case 'H':
            rm_cmd_show_version();
            rm_cmd_die(session, EXIT_SUCCESS);
            break;
        case 'h':
            rm_cmd_show_help();
            rm_cmd_show_version();
            rm_cmd_die(session, EXIT_SUCCESS);
            break;
        case 'l':
            settings->find_hardlinked_dupes = true;
            break;
        case 'L':
            settings->find_hardlinked_dupes = false;
            break;
        case 'O':
            oO_specified[1] = true;
            output_flag_cnt = -1;
            rm_cmd_parse_output_pair(session, optarg);
            break;
        case 'o':
            oO_specified[0] = true;
            if(output_flag_cnt < 0) {
                output_flag_cnt = false;
            }
            output_flag_cnt += rm_cmd_parse_output_pair(session, optarg);
            break;
        case 'R':
            settings->ignore_hidden = true;
            break;
        case 'r':
            settings->ignore_hidden = false;
            break;
        case 'V':
            verbosity_counter--;
            break;
        case 'v':
            verbosity_counter++;
            break;
        case 'x':
            settings->samepart = false;
            break;
        case 'X':
            settings->samepart = true;
            break;
        case 'd':
            settings->depth = ABS(atoi(optarg));
            break;
        case 'S':
            settings->sort_criteria = optarg;
            break;
        case 'p':
            settings->paranoid = true;
            break;
        case 'k':
            settings->keep_all_originals = true;
            break;
        case 'K':
            settings->keep_all_originals = true;
            break;
        case 'M':
            settings->must_match_original = true;
            break;
            break;
        case 'Q':
            settings->confirm_settings = false;
            break;
        case 'q':
            settings->confirm_settings = true;
            break;
        case 's':
            settings->limits_specified = true;
            rm_cmd_parse_limit_sizes(session, optarg);
            break;
        case 'P':
            settings->paranoid = false;
            break;
        default:
            return false;
        }
    }

    if(oO_specified[0] && oO_specified[1]) {
        rm_log_error(RED"Specifiyng both -o and -O is not allowed.\n"RESET);
        exit(EXIT_FAILURE);
    } else if(output_flag_cnt == -1) {
        /* Set default outputs */
        rm_fmt_add(session->formats, "pretty", "stdout");
        rm_fmt_add(session->formats, "summary", "stdout");
        rm_fmt_add(session->formats, "sh", "rmlint.sh");
    } else if(output_flag_cnt == 0) {
        /* There was no valid output flag given, but the user tried */
        rm_log_error("No valid -o flag encountered.\n");
        exit(EXIT_FAILURE);
    }

    settings->verbosity = VERBOSITY_TO_LOG_LEVEL[CLAMP(
                          verbosity_counter, 0, G_LOG_LEVEL_DEBUG
                      )];

    /* Get current directory */
    char cwd_buf[PATH_MAX + 1];
    getcwd(cwd_buf, PATH_MAX);
    settings->iwd = g_strdup_printf("%s%s", cwd_buf, G_DIR_SEPARATOR_S);

    /* Check the directory to be valid */
    while(optind < argc) {
        const char *dir_path = argv[optind];
        if(strlen(dir_path) == 1 && *dir_path == '-') {
            path_index += rm_cmd_read_paths_from_stdin(session, path_index);
        } else {
            path_index += rm_cmd_add_path(session, path_index, argv[optind]);
        }
        optind++;
    }
    if(path_index == 0) {
        /* Still no path set? - use `pwd` */
        rm_cmd_add_path(session, path_index, settings->iwd);
    }

    /* Copy commandline rmlint was invoked with by copying argv into a 
     * NULL padded array and join that with g_strjoinv. GLib made me lazy.
     */
    const char *argv_nul[argc + 1];
    memset(argv_nul, 0, sizeof(argv_nul));
    memcpy(argv_nul, argv, argc * sizeof(const char *));
    settings->joined_argv = g_strjoinv(" ", (gchar **)argv_nul);

    return true;
}

int rm_cmd_main(RmSession *session) {
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_INIT, 0, 0);
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE, 0, 0);
    rm_traverse_tree(session);

    rm_log_debug(
        "List build finished at %.3f with %lu files\n",
        g_timer_elapsed(session->timer, NULL), session->total_files
    );

    if(session->total_files >= 1) {
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS, 0, 0);
        rm_preprocess(session);

        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_SHREDDER, 0, 0);
        rm_shred_run(session);

        rm_log_debug("Dupe search finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));
    }

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_SUMMARY, 0, 0);
    rm_session_clear(session);
    return EXIT_SUCCESS;
}
