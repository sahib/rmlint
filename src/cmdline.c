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
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#include <errno.h>
#include <getopt.h>
#include <search.h>
#include <sys/time.h>
#include <glib.h>

#include "cmdline.h"
#include "treemerge.h"
#include "traverse.h"
#include "preprocess.h"
#include "shredder.h"
#include "utilities.h"
#include "formats.h"

/* exit and return to calling method */
static void rm_cmd_die(RmSession *session, int status) {
    rm_session_clear(session);
    exit(status);
}

static void rm_cmd_show_usage(void) {
    fprintf(
        stderr,
        "Usage: rmlint [OPTIONS] [DIRS|FILES|-] [// [ORIGINAL_(DIRS|FILES)]]\n\n"
        "       See the manpage (man 1 rmlint or rmlint --help) for more detailed usage information.\n"
        "       or http://rmlint.rtfd.org/en/latest/rmlint.1.html for the online manpage for an online version\n\n"
    );
}

static void rm_cmd_show_version(void) {
    fprintf(
        stderr,
        "version %s compiled: %s at [%s] \"%s\" (rev %s)\n",
        RMLINT_VERSION, __DATE__, __TIME__,
        RMLINT_VERSION_NAME,
        RMLINT_VERSION_GIT_REVISION
    );

    struct {
        bool enabled : 1;
        const char * name;
    } features[] = {
        {.name="mounts",      .enabled=HAVE_BLKID & (HAVE_GETMNTENT | HAVE_GETMNTINFO)},
        {.name="nonstripped", .enabled=HAVE_LIBELF},
        {.name="fiemap",      .enabled=HAVE_FIEMAP},
        {.name="sha512",      .enabled=HAVE_SHA512},
        {.name="bigfiles",    .enabled=HAVE_BIGFILES},
        {.name="intl",        .enabled=HAVE_LIBINTL},
        {.name="json-cache",  .enabled=HAVE_JSON_GLIB},
        {.name="xattr",       .enabled=HAVE_XATTR}
    };

    int n_features = sizeof(features) / sizeof(features[0]);

    fprintf(stderr, "compiled with: ");

    for(int i = 0; i < n_features; ++i) {
        if(features[i].enabled) {
            fprintf(stderr, "+%s ", features[i].name);
        } else {
            fprintf(stderr, "-%s ", features[i].name);
        }
    }

    fprintf(stderr, RESET"\n");
}

static void rm_cmd_show_help(bool use_pager) {
    static const char *commands[] = {
        "man %s docs/rmlint.1.gz 2> /dev/null",
        "man %s rmlint",
        NULL
    };

    bool found_manpage = false;

    for(int i = 0; commands[i] && !found_manpage; ++i) {
        char *command = g_strdup_printf(commands[i], (use_pager) ? "" : "-P cat");
        if(system(command) == 0) {
            found_manpage = true;
        }

        g_free(command);
    }

    if(!found_manpage) {
        rm_log_warning_line(_("You seem to have no manpage for rmlint."));
    }
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

static RmOff rm_cmd_size_string_to_bytes(const char *size_spec, const char **error) {
    if (size_spec == NULL) {
        return rm_cmd_size_format_error(error, _("Input size is empty"));
    }

    char *format = NULL;
    long double decimal = strtold(size_spec, &format);

    if (decimal == 0 && format == size_spec) {
        return rm_cmd_size_format_error(error, _("This does not look like a number"));
    } else if (decimal < 0) {
        return rm_cmd_size_format_error(error, _("Negativ sizes are no good idea"));
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
        return decimal * pow(found->base, found->exponent);
    } else {
        return rm_cmd_size_format_error(error, _("Given format specifier not found"));
    }
}

/* Size spec parsing implemented by qitta (http://github.com/qitta)
 * Thanks and go blame him if this breaks!
 */
static gboolean rm_cmd_size_range_string_to_bytes(const char *range_spec, RmOff *min, RmOff *max, const char **error) {
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
        tmp_error = _("Max is smaller than min");
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
        rm_log_error_line(_("cannot parse --limit: %s"), error);
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

static bool rm_cmd_add_path(RmSession *session, bool is_prefd, int index, const char *path) {
    RmSettings *settings = session->settings;
    if(faccessat(AT_FDCWD, path, R_OK, AT_EACCESS) != 0) {
        rm_log_warning_line(_("Can't open directory or file \"%s\": %s"), path, strerror(errno));
        return FALSE;
    } else {
        settings->is_prefd = g_realloc(settings->is_prefd, sizeof(char) * (index + 1));
        settings->is_prefd[index] = is_prefd;
        settings->paths = g_realloc(settings->paths, sizeof(char *) * (index + 2));

        char *abs_path = realpath(path, NULL);
        settings->paths[index + 0] = abs_path ? abs_path : g_strdup(path);
        settings->paths[index + 1] = NULL;
        return TRUE;
    }
}

static int rm_cmd_read_paths_from_stdin(RmSession *session,  bool is_prefd, int index) {
    int paths_added = 0;
    char path_buf[PATH_MAX];

    while(fgets(path_buf, PATH_MAX, stdin)) {
        paths_added += rm_cmd_add_path(session, is_prefd, index + paths_added, strtok(path_buf, "\n"));
    }

    return paths_added;
}

static bool rm_cmd_parse_output_pair(RmSession *session, const char *pair) {
    char *separator = strchr(pair, ':');
    char *full_path = NULL;
    char format_name[100];
    memset(format_name, 0, sizeof(format_name));

    if(separator == NULL) {
        /* default to stdout */
        full_path = "stdout";
        strncpy(format_name, pair, strlen(pair));
    } else {
        full_path = separator + 1;
        strncpy(format_name, pair, MIN((long)sizeof(format_name), separator - pair));
    }

    if(!rm_fmt_add(session->formats, format_name, full_path)) {
        rm_log_warning_line(_("Adding -o %s as output failed."), pair);
        return false;
    }

    return true;
}

static bool rm_cmd_parse_config_pair(RmSession *session, const char *pair) {
    char *domain = strchr(pair, ':');
    if(domain == NULL) {
        rm_log_warning_line(_("No format (format:key[=val]) specified in '%s'."), pair);
        return false;
    }

    char *key = NULL, *value = NULL;
    char **key_val = g_strsplit(&domain[1], "=", 2);
    int len = g_strv_length(key_val);

    if(len < 1) {
        rm_log_warning_line(_("Missing key (format:key[=val]) in '%s'."), pair);
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
    g_free(formatter);

    g_strfreev(key_val);
    return true;
}

static double rm_cmd_parse_clamp_factor(RmSession *session, const char *string) {
    char *error_loc = NULL;
    gdouble factor = g_strtod(string, &error_loc);

    if(error_loc != NULL && *error_loc != '\0' && *error_loc != '%') {
        rm_log_error_line(
            _("Unable to parse factor \"%s\": error begins at %s"),
            string, error_loc
        );
        rm_cmd_die(session, EXIT_FAILURE);
    }

    if(error_loc != NULL && *error_loc == '%') {
        factor /= 100;
    }

    if(0 > factor || factor > 1) {
        rm_log_error_line(
            _("factor value is not in range [0-1]: %f"),
            factor
        );
        rm_cmd_die(session, EXIT_FAILURE);
    }

    return factor;
}

static RmOff rm_cmd_parse_clamp_offset(RmSession *session, const char *string) {
    const char *error_msg = NULL;
    RmOff offset = rm_cmd_size_string_to_bytes(string, &error_msg);

    if(error_msg != NULL) {
        rm_log_error_line(
            _("Unable to parse offset \"%s\": %s"),
            string, error_msg
        );
        rm_cmd_die(session, EXIT_FAILURE);
    }

    return offset;
}

static void rm_cmd_parse_clamp_option(RmSession *session, const char *string, bool start_or_end) {
    if(strchr(string, '.') || g_str_has_suffix(string, "%")) {
        gdouble factor = rm_cmd_parse_clamp_factor(session, string);
        if(start_or_end) {
            session->settings->use_absolute_start_offset = false;
            session->settings->skip_start_factor = factor;
        } else {
            session->settings->use_absolute_end_offset = false;
            session->settings->skip_end_factor = factor;
        }
    } else {
        RmOff offset = rm_cmd_parse_clamp_offset(session, string);
        if(start_or_end) {
            session->settings->use_absolute_start_offset = true;
            session->settings->skip_start_offset = offset;
        } else {
            session->settings->use_absolute_end_offset = true;
            session->settings->skip_end_offset = offset;
        }
    }
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

static char rm_cmd_find_lint_types_sep(const char *lint_string) {
    if(*lint_string == '+' || *lint_string == '-') {
        lint_string++;
    }

    while(isalpha(*lint_string)) {
        lint_string++;
    }

    return *lint_string;
}

static void rm_cmd_parse_lint_types(RmSettings *settings, const char *lint_string) {
    RmLintTypeOption option_table[] = {{
            .names = NAMES{"all", 0},
            .enable = OPTS{
                &settings->findbadids,
                &settings->findbadlinks,
                &settings->findemptydirs,
                &settings->listemptyfiles,
                &settings->nonstripped,
                &settings->searchdup,
                &settings->merge_directories,
                0
            }
        }, {
            .names = NAMES{"minimal", 0},
            .enable = OPTS{
                &settings->findbadids,
                &settings->findbadlinks,
                &settings->searchdup,
                0
            },
        }, {
            .names = NAMES{"minimaldirs", 0},
            .enable = OPTS{
                &settings->findbadids,
                &settings->findbadlinks,
                &settings->merge_directories,
                0
            },
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
            .names = NAMES{"nonstripped", "ns", 0},
            .enable = OPTS{&settings->nonstripped, 0}
        }, {
            .names = NAMES{"duplicates", "df", "dupes", 0},
            .enable = OPTS{&settings->searchdup, 0}
        }, {
            .names = NAMES{"duplicatedirs", "dd", "dupedirs", 0},
            .enable = OPTS{&settings->merge_directories, 0}
        }
    };

    RmLintTypeOption *all_opts = &option_table[0];

    /* split the comma-separates list of options */
    char lint_sep[2] = {0, 0};
    lint_sep[0] = rm_cmd_find_lint_types_sep(lint_string);
    if(lint_sep[0] == 0) {
        lint_sep[0] = ',';
    }

    char **lint_types = g_strsplit(lint_string, lint_sep, -1);

    /* iterate over the separated option strings */
    for(int index = 0; lint_types[index]; index++) {
        char *lint_type = lint_types[index];
        char sign = 0;

        if(*lint_type == '+') {
            sign = +1;
        } else if(*lint_type == '-') {
            sign = -1;
        }

        if(index > 0 && sign == 0) {
            rm_log_warning(_("lint types after first should be prefixed with '+' or '-'"));
            rm_log_warning(_("or they would over-ride previously set options: [%s]"), lint_type);
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
            rm_log_warning(_("lint type '%s' not recognised"), lint_type);
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

    if(settings->merge_directories) {
        settings->ignore_hidden = false;
        settings->find_hardlinked_dupes = true;
    }

    /* clean up */
    g_strfreev(lint_types);
}

static bool rm_cmd_timestamp_is_plain(const char *stamp) {
    return strchr(stamp, 'T') ? false : true;
}

static time_t rm_cmd_parse_timestamp(RmSession *session, const char *string) {
    time_t result = 0;
    bool plain = rm_cmd_timestamp_is_plain(string);
    session->settings->filter_mtime = false;

    tzset();

    if(plain) {
        /* A simple integer is expected, just parse it as time_t */
        result = strtoll(string, NULL, 10);
    } else {
        /* Parse ISO8601 timestamps like 2006-02-03T16:45:09.000Z */
        result = rm_iso8601_parse(string);

        /* debug */
        {
            char time_buf[256];
            memset(time_buf, 0, sizeof(time_buf));
            rm_iso8601_format(time(NULL), time_buf, sizeof(time_buf));
            rm_log_debug("timestamp %ld understood as %s\n", result, time_buf);
        }
    }

    if(result <= 0) {
        rm_log_error_line(_("Unable to parse time spec \"%s\""), string);
        rm_cmd_die(session, EXIT_FAILURE);
        return 0;
    }

    /* Some sort of success. */
    session->settings->filter_mtime = true;

    if(result > time(NULL)) {
        /* Not critical, maybe there are some uses for this,
         * but print at least a small warning as indication.
         * */
        if(plain) {
            rm_log_warning_line(
                _("-n %lu is newer than current time (%lu)."),
                (long)result, (long)time(NULL)
            );
        } else {
            char time_buf[256];
            memset(time_buf, 0, sizeof(time_buf));
            rm_iso8601_format(time(NULL), time_buf, sizeof(time_buf));

            rm_log_warning_line(
                "-N %s is newer than current time (%s).",
                optarg, time_buf
            );
        }
    }

    return result;
}

static time_t rm_cmd_parse_timestamp_file(RmSession *session, const char *path) {
    time_t result = 0;
    bool plain = true;
    FILE *stamp_file = fopen(path, "r");

    /* Assume failure */
    session->settings->filter_mtime = false;

    if(stamp_file) {
        char stamp_buf[1024];
        memset(stamp_buf, 0, sizeof(stamp_buf));
        if(fgets(stamp_buf, sizeof(stamp_buf), stamp_file) != NULL) {
            result = rm_cmd_parse_timestamp(session, g_strstrip(stamp_buf));
            plain = rm_cmd_timestamp_is_plain(stamp_buf);
        }

        fclose(stamp_file);
    } else {
        /* Cannot read... */
        plain = false;
    }

    rm_fmt_add(session->formats, "stamp", path);
    if(!plain) {
        /* Enable iso8601 timestamp output */
        rm_fmt_set_config_value(
            session->formats, "stamp", g_strdup("iso8601"), g_strdup("true")
        );
    }

    return result;
}

static void rm_cmd_set_verbosity_from_cnt(RmSettings *settings, int verbosity_counter) {
    settings->verbosity = VERBOSITY_TO_LOG_LEVEL[CLAMP(
                              verbosity_counter,
                              0,
                              (int)(sizeof(VERBOSITY_TO_LOG_LEVEL) / sizeof(GLogLevelFlags)) - 1
                          )];
}

/* Parse the commandline and set arguments in 'settings' (glob. var accordingly) */
bool rm_cmd_parse_args(int argc, const char **argv, RmSession *session) {
    RmSettings *settings = session->settings;

    int choice = -1;
    int verbosity_counter = 2;
    int output_flag_cnt = -1;
    int option_index = 0;
    int path_index = 0;
    int follow_symlink_flags = 0;

    /* True when an error occured during reading paths */
    bool not_all_paths_read = false;

    /* Size string parsing error */
    const char *parse_error = NULL;

    /* Get current directory */
    char cwd_buf[PATH_MAX + 1];
    if(getcwd(cwd_buf, PATH_MAX) == NULL) {
        rm_log_perror("");
        return false;
    }

    settings->iwd = g_strdup_printf("%s%s", cwd_buf, G_DIR_SEPARATOR_S);

    /* set to true if -o or -O is specified */
    bool oO_specified[2] = {false, false};

    /* Free/Used Options:
       Free:  B DEFGHIJKLMNOPQRST VWX Z abcdefghijklmnopqrstuvwxyz
       Used: A C                                  
    */

    while(1) {
        static struct option long_options[] = {
            {"types"                      , required_argument , 0 , 'T'} ,
            {"threads"                    , required_argument , 0 , 't'} ,
            {"max-depth"                  , required_argument , 0 , 'd'} ,
            {"size"                       , required_argument , 0 , 's'} ,
            {"sortcriteria"               , required_argument , 0 , 'S'} ,
            {"algorithm"                  , required_argument , 0 , 'a'} ,
            {"output"                     , required_argument , 0 , 'o'} ,
            {"config"                     , required_argument , 0 , 'c'} ,
            {"add-output"                 , required_argument , 0 , 'O'} ,
            {"max-paranoid-mem"           , required_argument , 0 , 'u'} ,
            {"newer-than-stamp"           , required_argument , 0 , 'n'} ,
            {"newer-than"                 , required_argument , 0 , 'N'} ,
            {"clamp-low"                  , required_argument , 0 , 'q'} ,
            {"clamp-top"                  , required_argument , 0 , 'Q'} ,
            {"cache"                      , required_argument , 0,  'C'} ,
            {"progress"                   , no_argument       , 0 , 'g'} ,
            {"no-progress"                , no_argument       , 0 , 'G'} ,
            {"loud"                       , no_argument       , 0 , 'v'} ,
            {"quiet"                      , no_argument       , 0 , 'V'} ,
            {"with-color"                 , no_argument       , 0 , 'w'} ,
            {"no-with-color"              , no_argument       , 0 , 'W'} ,
            {"no-hidden"                  , no_argument       , 0 , 'R'} ,
            {"hidden"                     , no_argument       , 0 , 'r'} ,
            {"followlinks"                , no_argument       , 0 , 'f'} ,
            {"no-followlinks"             , no_argument       , 0 , 'F'} ,
            {"crossdev"                   , no_argument       , 0 , 'x'} ,
            {"no-crossdev"                , no_argument       , 0 , 'X'} ,
            {"paranoid"                   , no_argument       , 0 , 'p'} ,
            {"less-paranoid"              , no_argument       , 0 , 'P'} ,
            {"keep-all-tagged"            , no_argument       , 0 , 'k'} ,
            {"keep-all-untagged"          , no_argument       , 0 , 'K'} ,
            {"must-match-tagged"          , no_argument       , 0 , 'm'} ,
            {"must-match-untagged"        , no_argument       , 0 , 'M'} ,
            {"hardlinked"                 , no_argument       , 0 , 'l'} ,
            {"no-hardlinked"              , no_argument       , 0 , 'L'} ,
            {"match-basename"             , no_argument       , 0 , 'b'} ,
            {"no-match-basename"          , no_argument       , 0 , 'B'} ,
            {"match-extension"            , no_argument       , 0 , 'e'} ,
            {"no-match-extension"         , no_argument       , 0 , 'E'} ,
            {"match-without-extension"    , no_argument       , 0 , 'i'} ,
            {"no-match-without-extension" , no_argument       , 0 , 'I'} ,
            {"merge-directories"          , no_argument       , 0 , 'D'} ,
            {"xattr-write"                , no_argument       , 0 , 'z'} ,
            {"no-xattr-write"             , no_argument       , 0 , 'Z'} ,
            {"xattr-read"                 , no_argument       , 0 , 'j'} ,
            {"no-xattr-read"              , no_argument       , 0 , 'J'} ,
            {"xattr-clear"                , no_argument       , 0 , 'Y'} ,
            {"write-unfinished"           , no_argument       , 0 , 'U'} ,
            {"usage"                      , no_argument       , 0 , 'h'} ,
            {"help"                       , no_argument       , 0 , 'H'} ,
            {"version"                    , no_argument       , 0 , 'y'} ,
            {0, 0, 0, 0}
        };

        /* getopt_long stores the option index here. */
        choice = getopt_long(
                     argc, (char **)argv,
                     "T:t:d:s:o:O:S:a:u:n:N:c:q:Q:C:gvVwWrRfFXxpPkKmMlLhHybBeEiIDwWzZjJzYU",
                     long_options, &option_index
                 );

        /* Detect the end of the options. */
        if(choice == -1) {
            break;
        }
        switch(choice) {
        case '?':
            rm_cmd_show_help(false);
            rm_cmd_show_version();
            rm_cmd_die(session, EXIT_FAILURE);
            break;
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
                    rm_log_warning_line(_("Invalid thread count supplied: %s"), optarg);
                }
            }
            break;
        case 'C':
            g_queue_push_tail(&session->cache_list, optarg);
            break;
        case 'q':
            rm_cmd_parse_clamp_option(session, optarg, true);
            break;
        case 'Q':
            rm_cmd_parse_clamp_option(session, optarg, false);
            break;
        case 'a':
            settings->checksum_type = rm_string_to_digest_type(optarg);
            if(settings->checksum_type == RM_DIGEST_UNKNOWN) {
                rm_log_error_line(_("Unknown hash algorithm: '%s'"), optarg);
                rm_cmd_die(session, EXIT_FAILURE);
            } else if(settings->checksum_type == RM_DIGEST_BASTARD) {
                session->hash_seed1 = time(NULL) * (GPOINTER_TO_UINT(session));
                session->hash_seed2 = GPOINTER_TO_UINT(&session);
            }
            break;
        case 'f':
            settings->followlinks = true;
            break;
        case 'F':
            if(++follow_symlink_flags == 1) {
                settings->followlinks = false;
                settings->see_symlinks = false;
            } else {
                settings->followlinks = false;
                settings->see_symlinks = true;
            }
            break;
        case 'w':
            settings->color = isatty(fileno(stdout));
            break;
        case 'W':
            settings->color = false;
            break;
        case 'y':
            rm_cmd_show_version();
            rm_cmd_die(session, EXIT_SUCCESS);
            break;
        case 'H':
            rm_cmd_show_help(true);
            rm_cmd_show_version();
            rm_cmd_die(session, EXIT_SUCCESS);
            break;
        case 'h':
            rm_cmd_show_usage();
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
            rm_cmd_set_verbosity_from_cnt(settings, --verbosity_counter);
            break;
        case 'v':
            rm_cmd_set_verbosity_from_cnt(settings, ++verbosity_counter);
            break;
        case 'x':
            settings->samepart = false;
            break;
        case 'X':
            settings->samepart = true;
            break;
        case 'g':
            rm_fmt_add(session->formats, "progressbar", "stdout");
            rm_fmt_add(session->formats, "summary", "stdout");
            rm_fmt_add(session->formats, "sh", "rmlint.sh");
            output_flag_cnt = 1;
            break;
        case 'G':
            output_flag_cnt = -1;
            break;
        case 'd':
            settings->depth = ABS(strtol(optarg, NULL, 10));
            break;
        case 'D':
            settings->merge_directories = true;

            /* Pull in some options for convinience,
             * duplicate dir detection works better with them.
             *
             * They may be disabled explicitly though.
             */
            settings->find_hardlinked_dupes = true;
            settings->ignore_hidden = false;
            break;
        case 'z':
            settings->write_cksum_to_xattr = true;
            break;
        case 'Z':
            settings->write_cksum_to_xattr = false;
            break;
        case 'j':
            settings->read_cksum_from_xattr = true;
            break;
        case 'J':
            settings->read_cksum_from_xattr = false;
            break;
        case 'Y':
            settings->clear_xattr_fields = true;
            break;
        case 'U':
            settings->write_unfinished = true;
            break;
        case 'S':
            settings->sort_criteria = optarg;
            break;
        case 'p':
            settings->paranoid += 1;
            break;
        case 'u':
            settings->paranoid_mem = rm_cmd_size_string_to_bytes(optarg, &parse_error);
            if(parse_error != NULL) {
                rm_log_error_line(_("Invalid size description \"%s\": %s"), optarg, parse_error);
                rm_cmd_die(session, EXIT_FAILURE);
            }
            break;
        case 'N':
            settings->min_mtime = rm_cmd_parse_timestamp(session, optarg);
            break;
        case 'n':
            settings->min_mtime = rm_cmd_parse_timestamp_file(session, optarg);
            break;
        case 'k':
            settings->keep_all_tagged = true;
            if (settings->keep_all_untagged) {
                rm_log_error_line(_("can't specify both --keep-all-tagged and --keep-all-untagged; ignoring --keep-all-untagged"));
                settings->keep_all_untagged = false;
            }
            break;
        case 'K':
            settings->keep_all_untagged = true;
            if (settings->keep_all_tagged) {
                rm_log_error_line(_("can't specify both --keep-all-tagged and --keep-all-untagged; ignoring --keep-all-tagged"));
                settings->keep_all_tagged = false;
            }
            break;
        case 'm':
            settings->must_match_tagged = true;
            break;
        case 'M':
            settings->must_match_untagged = true;
            break;
        case 's':
            settings->limits_specified = true;
            rm_cmd_parse_limit_sizes(session, optarg);
            break;
        case 'P':
            settings->paranoid -= 1;
            break;
        case 'b':
            settings->match_basename = true;
            break;
        case 'B':
            settings->match_basename = false;
            break;
        case 'e':
            settings->match_with_extension = true;
            break;
        case 'E':
            settings->match_with_extension = false;
            break;
        case 'i':
            settings->match_without_extension = true;
            break;
        case 'I':
            settings->match_without_extension = false;
            break;
        default:
            return false;
        }
    }

    /* Handle the paranoia option */
    switch(settings->paranoid) {
    case -2:
        settings->checksum_type = RM_DIGEST_SPOOKY32;
        break;
    case -1:
        settings->checksum_type = RM_DIGEST_SPOOKY64;
        break;
    case 0:
        /* leave users choice of -a (default) */
        break;
    case 1:
        settings->checksum_type = RM_DIGEST_BASTARD;
        break;
    case 2:
#if HAVE_SHA512
        settings->checksum_type = RM_DIGEST_SHA512;
#else
        settings->checksum_type = RM_DIGEST_SHA256;
#endif
        break;
    case 3:
        settings->checksum_type = RM_DIGEST_PARANOID;
        break;
    default:
        rm_log_error_line(_("Only up to -ppp or down to -P flags allowed."));
        rm_cmd_die(session, EXIT_FAILURE);
        break;
    }

    /* Handle output flags */
    if(oO_specified[0] && oO_specified[1]) {
        rm_log_error_line(_("Specifiyng both -o and -O is not allowed."));
        rm_cmd_die(session, EXIT_FAILURE);
    } else if(output_flag_cnt == -1) {
        /* Set default outputs */
        rm_fmt_add(session->formats, "pretty", "stdout");
        rm_fmt_add(session->formats, "summary", "stdout");
        rm_fmt_add(session->formats, "sh", "rmlint.sh");
        rm_fmt_add(session->formats, "json", "rmlint.json");
    } else if(output_flag_cnt == 0) {
        /* There was no valid output flag given, but the user tried */
        rm_log_error_line(_("No valid -o flag encountered."));
        rm_cmd_die(session, EXIT_FAILURE);
    }

    if(settings->skip_start_factor >= settings->skip_end_factor) {
        rm_log_error_line(_("-q (--clamp-low) should be lower than -Q (--clamp-top)!"));
        rm_cmd_die(session, EXIT_FAILURE);
    }

    settings->verbosity = VERBOSITY_TO_LOG_LEVEL[CLAMP(
                              verbosity_counter,
                              0,
                              (int)(sizeof(VERBOSITY_TO_LOG_LEVEL) / sizeof(GLogLevelFlags)) - 1
                          )];

    bool is_prefd = false;

    /* Check the directory to be valid */
    while(optind < argc) {
        int read_paths = 0;
        const char *dir_path = argv[optind];

        if(strncmp(dir_path, "-", 1) == 0) {
            read_paths = rm_cmd_read_paths_from_stdin(session, is_prefd, path_index);
        } else if(strncmp(dir_path, "//", 2) == 0) {
            is_prefd = !is_prefd;
        } else {
            read_paths = rm_cmd_add_path(session, is_prefd, path_index, argv[optind]);
        }

        if(read_paths == 0) {
            not_all_paths_read = true;
        } else {
            path_index += read_paths;
        }

        optind++;
    }
    if(path_index == 0 && not_all_paths_read == false) {
        /* Still no path set? - use `pwd` */
        rm_cmd_add_path(session, is_prefd, path_index, settings->iwd);
    } else if(path_index == 0 && not_all_paths_read) {
        rm_log_error_line(_("No valid paths given."));
        rm_cmd_die(session, EXIT_FAILURE);
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
    int exit_state = EXIT_SUCCESS;

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_INIT);
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE);
    session->mounts = rm_mounts_table_new();
    if(session->mounts == NULL) {
        exit_state = EXIT_FAILURE;
        goto failure;
    }

    rm_traverse_tree(session);

    rm_log_debug(
        "List build finished at %.3f with %"LLU" files\n",
        g_timer_elapsed(session->timer, NULL), session->total_files
    );

    if(session->settings->merge_directories) {
        session->dir_merger = rm_tm_new(session);
    }

    if(session->total_files >= 1) {
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);
        rm_preprocess(session);

        if(session->settings->searchdup || session->settings->merge_directories) {
            rm_shred_run(session);

            rm_log_debug("Dupe search finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));
        }
    }

    if(session->settings->merge_directories) {
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_MERGE);
        rm_tm_finish(session->dir_merger);
    }

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PRE_SHUTDOWN);
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_SUMMARY);

failure:
    rm_session_clear(session);
    return exit_state;
}
