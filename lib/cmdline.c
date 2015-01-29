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
#include <ctype.h>
#include <math.h>
#include <time.h>

#include <errno.h>
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

/* Domain for reporting errors. Needed by GOptions */
#define RM_ERROR_QUARK (g_quark_from_static_string("rmlint"))

static void rm_cmd_show_version(void) {
    fprintf(
        stderr,
        "version %s compiled: %s at [%s] \"%s\" (rev %s)\n",
        RM_VERSION, __DATE__, __TIME__,
        RM_VERSION_NAME, RM_VERSION_GIT_REVISION
    );

    /* Make a list of all supported features from the macros in config.h */
    struct {
        bool enabled : 1;
        const char * name;
    } features[] = {
        {.name="mounts",      .enabled=HAVE_BLKID & HAVE_GIO_UNIX},
        {.name="nonstripped", .enabled=HAVE_LIBELF},
        {.name="fiemap",      .enabled=HAVE_FIEMAP},
        {.name="sha512",      .enabled=HAVE_SHA512},
        {.name="bigfiles",    .enabled=HAVE_BIGFILES},
        {.name="intl",        .enabled=HAVE_LIBINTL},
        {.name="json-cache",  .enabled=HAVE_JSON_GLIB},
        {.name="xattr",       .enabled=HAVE_XATTR},
        {.name=NULL,          .enabled=0}
    };

    fprintf(stderr, _("compiled with:"));
    for(int i = 0; features[i].name; ++i) {
        fprintf(stderr, " %c%s", (features[i].enabled) ? '+' : '-', features[i].name);
    }

    fprintf(stderr, RESET"\n\n");
    fprintf(stderr, _("rmlint was written by Christopher <sahib> Pahl and Daniel <SeeSpotRun> Thomas."));
    fprintf(stderr, "\n");
    fprintf(stderr, _("The code at https://github.com/sahib/rmlint is licensed under the terms of the GPLv3."));
    fprintf(stderr, "\n");
    exit(0);
}

static void rm_cmd_show_manpage(void) {
    static const char *commands[] = {
        "man %s docs/rmlint.1.gz 2> /dev/null",
        "man %s rmlint",
        NULL
    };

    bool found_manpage = false;

    for(int i = 0; commands[i] && !found_manpage; ++i) {
        char cmd_buf[512] = {0};
        if(snprintf(cmd_buf, sizeof(cmd_buf), commands[i], (RM_MANPAGE_USE_PAGER) ? "" : "-P cat") == -1) {
            continue;
        }

        if(system(cmd_buf) == 0) {
            found_manpage = true;
        }
    }

    if(!found_manpage) {
        rm_log_warning_line(_("You seem to have no manpage for rmlint."));
    }

    exit(0);
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

static int rm_cmd_compare_spec_elem(const void *fmt_a, const void *fmt_b) {
    return strcasecmp(((FormatSpec *)fmt_a)->id, ((FormatSpec *)fmt_b)->id);
}

static RmOff rm_cmd_size_string_to_bytes(const char *size_spec, GError **error) {
    if (size_spec == NULL) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Input size is empty"));
        return 0;
    }

    char *format = NULL;
    long double decimal = strtold(size_spec, &format);

    if (decimal == 0 && format == size_spec) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("This does not look like a number"));
        return 0;
    } else if (decimal < 0) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Negativ sizes are no good idea"));
        return 0;
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
        g_set_error(error, RM_ERROR_QUARK, 0, _("Given format specifier not found"));
        return 0;
    }
}

/* Size spec parsing implemented by qitta (http://github.com/qitta)
 * Thanks and go blame him if this breaks!
 */
static gboolean rm_cmd_size_range_string_to_bytes(const char *range_spec, RmOff *min, RmOff *max, GError **error) {
    *min = 0;
    *max = G_MAXULONG;

    gchar **split = g_strsplit(range_spec, "-", 2);

    if(split[0] != NULL) {
        *min = rm_cmd_size_string_to_bytes(split[0], error);
    }

    if(split[1] != NULL && *error == NULL) {
        *max = rm_cmd_size_string_to_bytes(split[1], error);
    }

    g_strfreev(split);

    if(*error == NULL && *max < *min) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Max is smaller than min"));
    }

    return (*error == NULL);
}

static gboolean rm_cmd_parse_limit_sizes(
    _U const char *option_name,
    const gchar *range_spec,
    RmSession *session,
    GError **error
) {
    if(!rm_cmd_size_range_string_to_bytes(
                range_spec,
                &session->cfg->minsize,
                &session->cfg->maxsize,
                error
            )) {

        g_prefix_error(error, _("cannot parse --limit: "));
        return false;
    } else {
        session->cfg->limits_specified = true;
        return true;
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
    RmCfg *cfg = session->cfg;
    if(faccessat(AT_FDCWD, path, R_OK, AT_EACCESS) != 0) {
        rm_log_warning_line(_("Can't open directory or file \"%s\": %s"), path, strerror(errno));
        return FALSE;
    } else {
        cfg->is_prefd = g_realloc(cfg->is_prefd, sizeof(char) * (index + 1));
        cfg->is_prefd[index] = is_prefd;
        cfg->paths = g_realloc(cfg->paths, sizeof(char *) * (index + 2));

        char *abs_path = realpath(path, NULL);
        cfg->paths[index + 0] = abs_path ? abs_path : g_strdup(path);
        cfg->paths[index + 1] = NULL;
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

static bool rm_cmd_parse_output_pair(RmSession *session, const char *pair, GError **error) {
    g_assert(session);
    g_assert(pair);

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
        g_set_error(error, RM_ERROR_QUARK, 0, _("Adding -o %s as output failed"), pair);
        return false;
    }

    return true;
}

static bool rm_cmd_parse_config_pair(RmSession *session, const char *pair, GError **error) {
    char *domain = strchr(pair, ':');
    if(domain == NULL) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("No format (format:key[=val]) specified in '%s'"), pair);
        return false;
    }

    char *key = NULL, *value = NULL;
    char **key_val = g_strsplit(&domain[1], "=", 2);
    int len = g_strv_length(key_val);
    bool result = true;

    if(len < 1) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Missing key (format:key[=val]) in '%s'"), pair);
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
    if(!rm_fmt_is_valid_key(session->formats, formatter, key)) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Invalid key `%s' for formatter `%s'"), key, formatter);
        g_free(key);
        g_free(value);
        result = false;
    } else {
        rm_fmt_set_config_value(session->formats, formatter, key, value);
    }

    g_free(formatter);
    g_strfreev(key_val);
    return result;
}

static gboolean rm_cmd_parse_config(
    _U const char *option_name,
    const char *pair,
    RmSession *session,
    _U GError **error
) {
    return rm_cmd_parse_config_pair(session, pair, error);
}

static double rm_cmd_parse_clamp_factor(const char *string, GError **error) {
    char *error_loc = NULL;
    gdouble factor = g_strtod(string, &error_loc);

    if(error_loc != NULL && *error_loc != '\0' && *error_loc != '%') {
        g_set_error(
            error, RM_ERROR_QUARK, 0,
            _("Unable to parse factor \"%s\": error begins at %s"),
            string, error_loc
        );
        return 0;
    }

    if(error_loc != NULL && *error_loc == '%') {
        factor /= 100;
    }

    if(0 > factor || factor > 1) {
        g_set_error(
            error, RM_ERROR_QUARK, 0,
            _("factor value is not in range [0-1]: %f"),
            factor
        );
        return 0;
    }

    return factor;
}

static RmOff rm_cmd_parse_clamp_offset(const char *string, GError **error) {
    RmOff offset = rm_cmd_size_string_to_bytes(string, error);

    if(*error != NULL) {
        g_prefix_error(
            error, _("Unable to parse offset \"%s\": "), string
        );
        return 0;
    }

    return offset;
}

static void rm_cmd_parse_clamp_option(RmSession *session, const char *string, bool start_or_end, GError **error) {
    if(strchr(string, '.') || g_str_has_suffix(string, "%")) {
        gdouble factor = rm_cmd_parse_clamp_factor(string, error);
        if(start_or_end) {
            session->cfg->use_absolute_start_offset = false;
            session->cfg->skip_start_factor = factor;
        } else {
            session->cfg->use_absolute_end_offset = false;
            session->cfg->skip_end_factor = factor;
        }
    } else {
        RmOff offset = rm_cmd_parse_clamp_offset(string, error);
        if(start_or_end) {
            session->cfg->use_absolute_start_offset = true;
            session->cfg->skip_start_offset = offset;
        } else {
            session->cfg->use_absolute_end_offset = true;
            session->cfg->skip_end_offset = offset;
        }
    }
}

/* parse comma-separated strong of lint types and set cfg accordingly */
typedef struct RmLintTypeOption {
    const char **names;
    gboolean **enable;
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

#define OPTS  (gboolean *[])
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

static gboolean rm_cmd_parse_lint_types(
    _U const char *option_name,
    const char *lint_string,
    RmSession *session,
    _U GError **error
) {
    RmCfg *cfg = session->cfg;

    RmLintTypeOption option_table[] = {{
            .names = NAMES{"all", 0},
            .enable = OPTS{
                &cfg->find_badids,
                &cfg->find_badlinks,
                &cfg->find_emptydirs,
                &cfg->find_emptyfiles,
                &cfg->find_nonstripped,
                &cfg->find_duplicates,
                &cfg->merge_directories,
                0
            }
        }, {
            .names = NAMES{"minimal", 0},
            .enable = OPTS{
                &cfg->find_badids,
                &cfg->find_badlinks,
                &cfg->find_duplicates,
                0
            },
        }, {
            .names = NAMES{"minimaldirs", 0},
            .enable = OPTS{
                &cfg->find_badids,
                &cfg->find_badlinks,
                &cfg->merge_directories,
                0
            },
        }, {
            .names = NAMES{"defaults", 0},
            .enable = OPTS{
                &cfg->find_badids,
                &cfg->find_badlinks,
                &cfg->find_emptydirs,
                &cfg->find_emptyfiles,
                &cfg->find_duplicates,
                0
            },
        }, {
            .names = NAMES{"none", 0},
            .enable = OPTS{0},
        }, {
            .names = NAMES{"badids", "bi", 0},
            .enable = OPTS{&cfg->find_badids, 0}
        }, {
            .names = NAMES{"badlinks", "bl", 0},
            .enable = OPTS{&cfg->find_badlinks, 0}
        }, {
            .names = NAMES{"emptydirs", "ed", 0},
            .enable = OPTS{&cfg->find_emptydirs, 0}
        }, {
            .names = NAMES{"emptyfiles", "ef", 0},
            .enable = OPTS{&cfg->find_emptyfiles, 0}
        }, {
            .names = NAMES{"nonstripped", "ns", 0},
            .enable = OPTS{&cfg->find_nonstripped, 0}
        }, {
            .names = NAMES{"duplicates", "df", "dupes", 0},
            .enable = OPTS{&cfg->find_duplicates, 0}
        }, {
            .names = NAMES{"duplicatedirs", "dd", "dupedirs", 0},
            .enable = OPTS{&cfg->merge_directories, 0}
        }
    };

    RmLintTypeOption *all_opts = &option_table[0];

    /* initialize all options to disabled by default */
    for (int i = 0; all_opts->enable[i]; i++) {
        *all_opts->enable[i] = false;
    }

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

        if(sign == 0) {
            sign = +1;
        } else {
            /* get rid of prefix. */
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

        /* enable options as appropriate */
        for(int i = 0; option->enable[i]; i++) {
            *option->enable[i] = (sign == -1) ? false : true;
        }
    }

    if(cfg->merge_directories) {
        cfg->ignore_hidden = false;
        cfg->find_hardlinked_dupes = true;
    }

    /* clean up */
    g_strfreev(lint_types);
    return true;
}

static bool rm_cmd_timestamp_is_plain(const char *stamp) {
    return strchr(stamp, 'T') ? false : true;
}

static gboolean rm_cmd_parse_timestamp(
    _U const char *option_name, const gchar *string, RmSession *session, GError **error
) {
    time_t result = 0;
    bool plain = rm_cmd_timestamp_is_plain(string);
    session->cfg->filter_mtime = false;

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
        g_set_error(
            error, RM_ERROR_QUARK, 0,
            _("Unable to parse time spec \"%s\""), string
        );
        return false;
    }

    /* Some sort of success. */
    session->cfg->filter_mtime = true;

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
                string, time_buf
            );
        }
    }

    /* Remember our result */
    session->cfg->min_mtime = result;
    return true;
}

static gboolean rm_cmd_parse_timestamp_file(
    const char *option_name, const gchar *timestamp_path, RmSession *session, GError **error
) {
    bool plain = true, success = false;
    FILE *stamp_file = fopen(timestamp_path, "r");

    /* Assume failure */
    session->cfg->filter_mtime = false;

    if(stamp_file) {
        char stamp_buf[1024];
        memset(stamp_buf, 0, sizeof(stamp_buf));

        if(fgets(stamp_buf, sizeof(stamp_buf), stamp_file) != NULL) {
            success = rm_cmd_parse_timestamp(
                          option_name, g_strstrip(stamp_buf), session, error
                      );
            plain = rm_cmd_timestamp_is_plain(stamp_buf);
        }

        fclose(stamp_file);
    } else {
        /* Cannot read... */
        plain = false;
    }

    if(!success) {
        return false;
    }

    rm_fmt_add(session->formats, "stamp", timestamp_path);
    if(!plain) {
        /* Enable iso8601 timestamp output */
        rm_fmt_set_config_value(
            session->formats, "stamp", g_strdup("iso8601"), g_strdup("true")
        );
    }

    return success;
}

static void rm_cmd_set_verbosity_from_cnt(RmCfg *cfg, int verbosity_counter) {
    cfg->verbosity = VERBOSITY_TO_LOG_LEVEL[CLAMP(
            verbosity_counter,
            0,
            (int)(sizeof(VERBOSITY_TO_LOG_LEVEL) / sizeof(GLogLevelFlags)) - 1
                                            )];
}

static void rm_cmd_set_paranoia_from_cnt(RmCfg *cfg, int paranoia_counter, GError **error) {
    /* Handle the paranoia option */
    switch(paranoia_counter) {
    case -2:
        cfg->checksum_type = RM_DIGEST_SPOOKY;
        break;
    case -1:
        cfg->checksum_type = RM_DIGEST_BASTARD;
        break;
    case 0:
        /* leave users choice of -a (default) */
        break;
    case 1:
#if HAVE_SHA512
        cfg->checksum_type = RM_DIGEST_SHA512;
#else
        cfg->checksum_type = RM_DIGEST_SHA256;
#endif
        break;
    case 2:
        cfg->checksum_type = RM_DIGEST_PARANOID;
        break;
    default:
        g_set_error(error, RM_ERROR_QUARK, 0, _("Only up to -pp or down to -PP flags allowed"));
        break;
    }
}

static void rm_cmd_on_error(_U GOptionContext *context, _U GOptionGroup *group, RmSession *session, GError **error) {
    if(error != NULL) {
        rm_log_error_line("%s.", (*error)->message);
        g_clear_error(error);
        session->cmdline_parse_error = true;
    }
}

static gboolean rm_cmd_parse_algorithm(
    _U const char *option_name,
    const gchar *value,
    RmSession *session,
    GError **error
) {
    RmCfg *cfg = session->cfg;
    cfg->checksum_type = rm_string_to_digest_type(value);

    if(cfg->checksum_type == RM_DIGEST_UNKNOWN) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Unknown hash algorithm: '%s'"), value);
        return false;
    } else if(cfg->checksum_type == RM_DIGEST_BASTARD) {
        session->hash_seed1 = time(NULL) * (GPOINTER_TO_UINT(session));
        session->hash_seed2 = GPOINTER_TO_UINT(&session);
    }
    return true;
}

static gboolean rm_cmd_parse_small_output(
    _U const char *option_name, const gchar *output_pair, RmSession *session, _U GError **error
) {
    session->output_cnt[0] = MAX(session->output_cnt[0], 0);
    session->output_cnt[0] += rm_cmd_parse_output_pair(session, output_pair, error);
    return true;
}

static gboolean rm_cmd_parse_large_output(
    _U const char *option_name, const gchar *output_pair, RmSession *session, _U GError **error
) {
    session->output_cnt[1] = MAX(session->output_cnt[1], 0);
    session->output_cnt[1] += rm_cmd_parse_output_pair(session, output_pair, error);
    return true;
}

static gboolean rm_cmd_parse_paranoid_mem(
    _U const char *option_name, const gchar *size_spec, RmSession *session, GError **error
) {
    RmOff size = rm_cmd_size_string_to_bytes(size_spec, error);

    if(error != NULL) {
        g_prefix_error(
            error, _("Invalid size description \"%s\": "), size_spec
        );
        return false;
    } else {
        session->cfg->paranoid_mem = size;
        return true;
    }
}

static gboolean rm_cmd_parse_clamp_low(
    _U const char *option_name, const gchar *spec, RmSession *session, _U GError **error
) {
    rm_cmd_parse_clamp_option(session, spec, true, error);
    return (error && *error == NULL);
}

static gboolean rm_cmd_parse_clamp_top(
    _U const char *option_name, const gchar *spec, RmSession *session, _U GError **error
) {
    rm_cmd_parse_clamp_option(session, spec, false, error);
    return (error && *error == NULL);
}

static gboolean rm_cmd_parse_cache(
    _U const char *option_name, const gchar *cache_path, RmSession *session, GError **error
) {
    if(!g_file_test(cache_path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error(error, RM_ERROR_QUARK, 0, "There is no cache at `%s'", cache_path);
        return false;
    }

    g_queue_push_tail(&session->cache_list, g_strdup(cache_path));
    return true;
}

static gboolean rm_cmd_parse_progress(
    _U const char *option_name, _U const gchar *value, RmSession *session, _U GError **error
) {
    rm_fmt_clear(session->formats);
    rm_fmt_add(session->formats, "progressbar", "stdout");
    rm_fmt_add(session->formats, "summary", "stdout");
    rm_fmt_add(session->formats, "sh", "rmlint.sh");
    rm_fmt_add(session->formats, "json", "rmlint.json");

    /* Set verbosity to minimal */
    rm_cmd_set_verbosity_from_cnt(session->cfg, 1);
    return true;
}

static void rm_cmd_set_default_outputs(RmSession *session) {
    rm_fmt_add(session->formats, "pretty", "stdout");
    rm_fmt_add(session->formats, "summary", "stdout");
    rm_fmt_add(session->formats, "sh", "rmlint.sh");
    rm_fmt_add(session->formats, "json", "rmlint.json");
}

static gboolean rm_cmd_parse_no_progress(
    _U const char *option_name, _U const gchar *value, RmSession *session, _U GError **error
) {
    rm_fmt_clear(session->formats);
    rm_cmd_set_default_outputs(session);
    rm_cmd_set_verbosity_from_cnt(session->cfg, session->verbosity_count);
    return true;
}

static gboolean rm_cmd_parse_loud(
    _U const char *option_name, _U const gchar *count, RmSession *session, _U GError **error
) {
    rm_cmd_set_verbosity_from_cnt(session->cfg, ++session->verbosity_count);
    return true;
}

static gboolean rm_cmd_parse_quiet(
    _U const char *option_name, _U const gchar *count, RmSession *session, _U GError **error
) {
    rm_cmd_set_verbosity_from_cnt(session->cfg, --session->verbosity_count);
    return true;
}

static gboolean rm_cmd_parse_paranoid(
    _U const char *option_name, _U const gchar *count, RmSession *session, _U GError **error
) {
    rm_cmd_set_paranoia_from_cnt(session->cfg, ++session->paranoia_count, error);
    return true;
}

static gboolean rm_cmd_parse_less_paranoid(
    _U const char *option_name, _U const gchar *count, RmSession *session, _U GError **error
) {
    rm_cmd_set_paranoia_from_cnt(session->cfg, --session->paranoia_count, error);
    return true;
}

static gboolean rm_cmd_parse_partial_hidden(
    _U const char *option_name, _U const gchar *count, RmSession *session, _U GError **error
) {
    RmCfg *cfg = session->cfg;
    cfg->ignore_hidden = false;
    cfg->partial_hidden = true;

    return true;
}

static gboolean rm_cmd_parse_no_partial_hidden(
    _U const char *option_name, _U const gchar *count, RmSession *session, _U GError **error
) {
    RmCfg *cfg = session->cfg;
    cfg->ignore_hidden = true;
    cfg->partial_hidden = false;

    return true;
}

static gboolean rm_cmd_parse_merge_directories(
    _U const char *option_name, _U const gchar *count, RmSession *session, _U GError **error
) {
    RmCfg *cfg = session->cfg;
    cfg->merge_directories = true;

    /* Pull in some options for convinience,
     * duplicate dir detection works better with them.
     *
     * They may be disabled explicitly though.
     */
    cfg->find_hardlinked_dupes = true;
    cfg->follow_symlinks = false;
    cfg->see_symlinks = true;
    rm_cmd_parse_partial_hidden(NULL, NULL, session, error);
    return true;
}

static bool rm_cmd_set_cwd(RmCfg *cfg) {
    /* Get current directory */
    char cwd_buf[PATH_MAX + 1];
    memset(cwd_buf, 0, sizeof(cwd_buf));

    if(getcwd(cwd_buf, PATH_MAX) == NULL) {
        rm_log_perror("");
        return false;
    }

    cfg->iwd = g_strdup_printf("%s%s", cwd_buf, G_DIR_SEPARATOR_S);
    return true;
}


static bool rm_cmd_set_cmdline(RmCfg *cfg, int argc, char **argv) {
    /* Copy commandline rmlint was invoked with by copying argv into a
     * NULL padded array and join that with g_strjoinv. GLib made me lazy.
     */
    const char *argv_nul[argc + 1];
    memset(argv_nul, 0, sizeof(argv_nul));
    memcpy(argv_nul, argv, argc * sizeof(const char *));
    cfg->joined_argv = g_strjoinv(" ", (gchar **)argv_nul);

    /* This cannot fail currently */
    return true;
}

static bool rm_cmd_set_paths(RmSession *session, char **paths) {
    int path_index = 0;
    bool is_prefd = false;
    bool not_all_paths_read = false;

    RmCfg *cfg = session->cfg;

    /* Check the directory to be valid */
    for(int i = 0; paths && paths[i]; ++i) {
        int read_paths = 0;
        const char *dir_path = paths[i];

        if(strncmp(dir_path, "-", 1) == 0) {
            read_paths = rm_cmd_read_paths_from_stdin(session, is_prefd, path_index);
        } else if(strncmp(dir_path, "//", 2) == 0) {
            is_prefd = !is_prefd;
        } else {
            read_paths = rm_cmd_add_path(session, is_prefd, path_index, paths[i]);
        }

        if(read_paths == 0) {
            not_all_paths_read = true;
        } else {
            path_index += read_paths;
        }
    }

    g_strfreev(paths);

    if(path_index == 0 && not_all_paths_read == false) {
        /* Still no path set? - use `pwd` */
        rm_cmd_add_path(session, is_prefd, path_index, cfg->iwd);
    } else if(path_index == 0 && not_all_paths_read) {
        return false;
    }

    return true;
}

static bool rm_cmd_set_outputs(RmSession *session, GError **error) {
    if(session->output_cnt[0] >= 0 && session->output_cnt[1] >= 0) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Specifiyng both -o and -O is not allowed"));
        return false;
    } else if(session->output_cnt[0] < 0 && session->output_cnt[1] < 0 && !rm_fmt_len(session->formats)) {
        rm_cmd_set_default_outputs(session);
    }

    return true;
}

/* Parse the commandline and set arguments in 'settings' (glob. var accordingly) */
bool rm_cmd_parse_args(int argc, char **argv, RmSession *session) {
    RmCfg *cfg = session->cfg;

    /* List of paths we got passed (or NULL) */
    char **paths = NULL;

    /* General error variable */
    GError *error = NULL;
    GOptionContext *option_parser = NULL;

#define FUNC(name) ((GOptionArgFunc)rm_cmd_parse_##name)
    const int DISABLE = G_OPTION_FLAG_REVERSE,
              EMPTY   = G_OPTION_FLAG_NO_ARG,
              HIDDEN  = G_OPTION_FLAG_HIDDEN;

    /* Free/Used Options:
       Free: abBcCdDeEfFgGHhiI  kKlLmMnNoOpPqQrRsStTuUvVwWxX
       Used                   jJ                            yYzZ
    */
    const GOptionEntry main_option_entries[] = {
        /* Option with required arguments */
        {"max-depth"        ,  'd' ,  0 ,  G_OPTION_ARG_INT      ,  &cfg->depth          ,  _("Specify max traversal depth")   ,  "N"          },
        {"sortcriteria"     ,  'S' ,  0 ,  G_OPTION_ARG_STRING   ,  &cfg->sort_criteria  ,  _("Original criteria")             ,  "[ampAMP]"   },
        {"types"            ,  'T' ,  0 ,  G_OPTION_ARG_CALLBACK ,  FUNC(lint_types)     ,  _("Specify lint types")            ,  "T"          },
        {"size"             ,  's' ,  0 ,  G_OPTION_ARG_CALLBACK ,  FUNC(limit_sizes)    ,  _("Specify size limits")           ,  "m-M"        },
        {"algorithm"        ,  'a' ,  0 ,  G_OPTION_ARG_CALLBACK ,  FUNC(algorithm)      ,  _("Choose hash algorithm")         ,  "A"          },
        {"output"           ,  'o' ,  0 ,  G_OPTION_ARG_CALLBACK ,  FUNC(small_output)   ,  _("Add output (override default)") ,  "FMT[:PATH]" },
        {"add-output"       ,  'O' ,  0 ,  G_OPTION_ARG_CALLBACK ,  FUNC(large_output)   ,  _("Add output (add to defaults)")  ,  "FMT[:PATH]" },
        {"newer-than-stamp" ,  'n' ,  0 ,  G_OPTION_ARG_CALLBACK ,  FUNC(timestamp_file) ,  _("Newer than stamp file")         ,  "PATH"       },
        {"newer-than"       ,  'N' ,  0 ,  G_OPTION_ARG_CALLBACK ,  FUNC(timestamp)      ,  _("Newer than timestamp")          ,  "STAMP"      },
        {"config"           ,  'c' ,  0 ,  G_OPTION_ARG_CALLBACK ,  FUNC(config)         ,  _("Configure a formatter")         ,  "FMT:K[=V]"  },
        {"cache"            ,  'C' ,  0 ,  G_OPTION_ARG_CALLBACK ,  FUNC(cache)          ,  _("Add json cache file")           ,  "PATH"       },

        /* Non-trvial switches */
        {"progress" ,  'g' ,  EMPTY ,  G_OPTION_ARG_CALLBACK ,  FUNC(progress) ,  _("Enable progressbar")                   ,  NULL},
        {"loud"     ,  'v' ,  EMPTY ,  G_OPTION_ARG_CALLBACK ,  FUNC(loud)     ,  _("Be more verbose (-vvv for much more)") ,  NULL},
        {"quiet"    ,  'V' ,  EMPTY ,  G_OPTION_ARG_CALLBACK ,  FUNC(quiet)    ,  _("Be less verbose (-VVV for much less)") ,  NULL},

        /* Trivial boolean options */
        {"no-with-color"              , 'W' , DISABLE  , G_OPTION_ARG_NONE     , &cfg->with_color              , _("Be not that colorful")                               , NULL},
        {"hidden"                     , 'r' , DISABLE  , G_OPTION_ARG_NONE     , &cfg->ignore_hidden           , _("Find hidden files")                                  , NULL},
        {"followlinks"                , 'f' , 0        , G_OPTION_ARG_NONE     , &cfg->follow_symlinks         , _("Follow symlinks")                                    , NULL},
        {"no-followlinks"             , 'F' , DISABLE  , G_OPTION_ARG_NONE     , &cfg->follow_symlinks         , _("Ignore symlinks")                                    , NULL},
        {"crossdev"                   , 'x' , 0        , G_OPTION_ARG_NONE     , &cfg->crossdev                , _("Do not cross mounpoints")                            , NULL},
        {"paranoid"                   , 'p' , EMPTY    , G_OPTION_ARG_CALLBACK , FUNC(paranoid)                , _("Use more paranoid hashing")                          , NULL},
        {"keep-all-tagged"            , 'k' , 0        , G_OPTION_ARG_NONE     , &cfg->keep_all_tagged         , _("Keep all tagged files")                              , NULL},
        {"keep-all-untagged"          , 'K' , 0        , G_OPTION_ARG_NONE     , &cfg->keep_all_untagged       , _("Keep all untagged files")                            , NULL},
        {"must-match-tagged"          , 'm' , 0        , G_OPTION_ARG_NONE     , &cfg->must_match_tagged       , _("Must have twin in tagged dir")                       , NULL},
        {"must-match-untagged"        , 'M' , 0        , G_OPTION_ARG_NONE     , &cfg->must_match_untagged     , _("Must have twin in untagged dir")                     , NULL},
        {"match-basename"             , 'b' , 0        , G_OPTION_ARG_NONE     , &cfg->match_basename          , _("Only find twins with same basename")                 , NULL},
        {"match-extension"            , 'e' , 0        , G_OPTION_ARG_NONE     , &cfg->match_with_extension    , _("Only find twins with same extension")                , NULL},
        {"match-without-extension"    , 'i' , 0        , G_OPTION_ARG_NONE     , &cfg->match_without_extension , _("Only find twins with same basename minus extension") , NULL},
        {"merge-directories"          , 'D' , EMPTY    , G_OPTION_ARG_CALLBACK , FUNC(merge_directories)       , _("Find duplicate directories")                         , NULL},
        {"no-hardlinked"              , 'L' , DISABLE  , G_OPTION_ARG_NONE     , &cfg->find_hardlinked_dupes   , _("Ignore hardlink twins")                              , NULL},
        {"partial-hidden"             , 0   , EMPTY    , G_OPTION_ARG_CALLBACK , FUNC(partial_hidden)          , _("Find hidden files in duplicate folders only")        , NULL},

        /* Callback */
        {"show-man" ,  'H' ,  EMPTY ,  G_OPTION_ARG_CALLBACK ,  rm_cmd_show_manpage ,  _("Show the manpage")            ,  NULL},
        {"version"  ,  0   ,  EMPTY ,  G_OPTION_ARG_CALLBACK ,  rm_cmd_show_version ,  _("Show the version & features") ,  NULL},

        /* Special case: accumulate leftover args (paths) in &paths */
        {G_OPTION_REMAINING ,  0 ,  0 ,  G_OPTION_ARG_FILENAME_ARRAY ,  &paths ,  ""   ,  NULL},
        {NULL               ,  0 ,  0 ,  0                           ,  NULL   ,  NULL ,  NULL}
    };

    const GOptionEntry inversed_option_entries[] = {
        {"no-hidden"                  , 'R' , 0       | HIDDEN , G_OPTION_ARG_NONE     , &cfg->ignore_hidden           , "Ignore hidden files"                 , NULL},
        {"with-color"                 , 'w' , 0       | HIDDEN , G_OPTION_ARG_NONE     , &cfg->with_color              , "Be colorful like a unicorn"          , NULL},
        {"hardlinked"                 , 'l' , 0       | HIDDEN , G_OPTION_ARG_NONE     , &cfg->find_hardlinked_dupes   , _("Report hardlinks as duplicates")   , NULL},
        {"no-crossdev"                , 'X' , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->crossdev                , "Cross mountpoints"                   , NULL},
        {"less-paranoid"              , 'P' , EMPTY   | HIDDEN , G_OPTION_ARG_CALLBACK , FUNC(less_paranoid)           , "Use less paranoid hashing algorithm" , NULL},
        {"see-symlinks"               , '@' , 0       | HIDDEN , G_OPTION_ARG_NONE     , &cfg->see_symlinks            , "Treat symlinks a regular files"      , NULL},
        {"no-match-basename"          , 'B' , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->match_basename          , "Disable --match-basename filter"     , NULL},
        {"no-match-extension"         , 'E' , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->match_with_extension    , "Disable --match-extension"           , NULL},
        {"no-match-without-extension" , 'I' , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->match_without_extension , "Disable --match-without-extension"   , NULL},
        {"no-progress"                , 'G' , EMPTY   | HIDDEN , G_OPTION_ARG_CALLBACK , FUNC(no_progress)             , "Disable progressbar"                 , NULL},
        {"no-xattr-read"              ,  0  , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->read_cksum_from_xattr   , "Disable --xattr-read"                , NULL},
        {"no-xattr-write"             ,  0  , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->write_cksum_to_xattr    , "Disable --xattr-write"               , NULL},
        {"no-partial-hidden"          ,  0  , EMPTY   | HIDDEN , G_OPTION_ARG_CALLBACK , FUNC(no_partial_hidden)       , "Invert --partial-hidden"             , NULL},
        {NULL                         ,  0  , 0                , 0                     , NULL                          , NULL                                  , NULL}
    };

    const GOptionEntry unusual_option_entries[] = {
        {"clamp-low"        ,  'q' ,  HIDDEN ,  G_OPTION_ARG_CALLBACK ,  FUNC(clamp_low)             ,  "Limit lower reading barrier"                ,  "P" },
        {"clamp-top"        ,  'Q' ,  HIDDEN ,  G_OPTION_ARG_CALLBACK ,  FUNC(clamp_top)             ,  "Limit upper reading barrier"                ,  "P" },
        {"max-paranoid-mem" ,  'u' ,  HIDDEN ,  G_OPTION_ARG_CALLBACK ,  FUNC(paranoid_mem)          ,  "Specify max. memory to use for -pp"         ,  "S" },
        {"threads"          ,  't' ,  HIDDEN ,  G_OPTION_ARG_INT64    ,  &cfg->threads               ,  "Specify max. number of threads"             ,  "N" },
        {"write-unfinished" ,  'U' ,  HIDDEN ,  G_OPTION_ARG_NONE     ,  &cfg->write_unfinished      ,  "Output unfinished checksums"                ,  NULL},
        {"xattr-write"      ,   0  ,  HIDDEN ,  G_OPTION_ARG_NONE     ,  &cfg->write_cksum_to_xattr  ,  "Cache checksum in file attributes"          ,  NULL},
        {"xattr-read"       ,   0  ,  HIDDEN ,  G_OPTION_ARG_NONE     ,  &cfg->read_cksum_from_xattr ,  "Read cached checksums from file attributes" ,  NULL},
        {"xattr-clear"      ,   0  ,  HIDDEN ,  G_OPTION_ARG_NONE     ,  &cfg->clear_xattr_fields    ,  "Clear xattrs from all seen files"           ,  NULL},
        {NULL               ,   0  ,  HIDDEN ,  0                     ,  NULL                        ,  NULL                                         ,  NULL}
    };

    /* Initialize default verbosity */
    rm_cmd_set_verbosity_from_cnt(cfg, session->verbosity_count);

    if(!rm_cmd_set_cwd(cfg)) {
        g_set_error(&error, RM_ERROR_QUARK, 0, _("Cannot set current working directory"));
        goto failure;
    }

    if(!rm_cmd_set_cmdline(cfg, argc, argv)) {
        g_set_error(&error, RM_ERROR_QUARK, 0, _("Cannot join commandline"));
        goto failure;
    }

    ////////////////////
    // OPTION PARSING //
    ////////////////////

    option_parser = g_option_context_new(
                        _("[TARGET_DIR_OR_FILES …] [//] [TAGGED_TARGET_DIR_OR_FILES …] [-]")
                    );
    g_option_context_set_translation_domain(option_parser, RM_GETTEXT_PACKAGE);

    GOptionGroup *main_group = g_option_group_new(
                                   "rmlint", "main", "Most useful main options", session, NULL
                               );
    GOptionGroup *inversion_group = g_option_group_new(
                                        "inversed", "inverted", "Options that enable defaults", session, NULL
                                    );
    GOptionGroup *unusual_group = g_option_group_new(
                                      "unusual", "unusual", "Unusual options", session, NULL
                                  );

    g_option_group_add_entries(main_group, main_option_entries);
    g_option_group_add_entries(main_group, inversed_option_entries);
    g_option_group_add_entries(main_group, unusual_option_entries);

    g_option_context_add_group(option_parser, inversion_group);
    g_option_context_add_group(option_parser, unusual_group);
    g_option_context_set_main_group(option_parser, main_group);
    g_option_context_set_summary(
        option_parser,
        _("rmlint finds space waste and other broken things on your filesystem and offers to remove it.\n"
          "It is especially good at finding duplicates and offers a big varierty of options to handle them."
         )
    );
    g_option_context_set_description(
        option_parser,
        _("Only the most important options and options that alter the defaults are shown above.\n"
          "See the manpage (man 1 rmlint or rmlint --show-man) for far more detailed usage information,\n"
          "or http://rmlint.rtfd.org/en/latest/rmlint.1.html for the online manpage.\n"
          "Complementary tutorials can be found at: http://rmlint.rtfd.org"
         )
    );

    g_option_group_set_error_hook(main_group, (GOptionErrorFunc)rm_cmd_on_error);

    if(!g_option_context_parse(option_parser, &argc, &argv, &error)) {
        goto failure;
    }

    /* Silent fixes of invalid numberic input */
    cfg->threads = CLAMP(cfg->threads, 1, 128);
    cfg->depth = CLAMP(cfg->depth, 1, PATH_MAX / 2 + 1);

    if(cfg->partial_hidden && !cfg->merge_directories) {
        /* --partial-hidden only makes sense with --merge-directories.
         * If the latter is not specfified, ignore it all together */
        cfg->ignore_hidden = true;
        cfg->partial_hidden = false;
    }

    /* Overwrite color if we do not print to a terminal directly */
    if(cfg->with_color) {
        cfg->with_stdout_color = isatty(fileno(stdout));
        cfg->with_stderr_color = isatty(fileno(stdout));
        cfg->with_color = (cfg->with_stderr_color | cfg->with_stderr_color);
    } else {
        cfg->with_stdout_color = cfg->with_stderr_color = 0;
    }

    if(cfg->keep_all_tagged && cfg->keep_all_untagged) {
        error = g_error_new(
                    RM_ERROR_QUARK, 0,
                    _("can't specify both --keep-all-tagged and --keep-all-untagged")
                );

    } else if(cfg->skip_start_factor >= cfg->skip_end_factor) {
        error = g_error_new(
                    RM_ERROR_QUARK, 0,
                    _("-q (--clamp-low) should be lower than -Q (--clamp-top)")
                );
    } else if(!rm_cmd_set_paths(session, paths)) {
        error = g_error_new(
                    RM_ERROR_QUARK, 0,
                    _("No valid paths given.")
                );
    } else if(!rm_cmd_set_outputs(session, &error)) {
        /* Something wrong with the outputs */
    }

failure:
    if(error != NULL) {
        rm_cmd_on_error(NULL, NULL, session, &error);
    }

    g_option_context_free(option_parser);
    return !(session->cmdline_parse_error);
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
        "List build finished at %.3f with %d files\n",
        g_timer_elapsed(session->timer, NULL), session->total_files
    );

    if(session->cfg->merge_directories) {
        session->dir_merger = rm_tm_new(session);
    }

    if(session->total_files >= 1) {
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);
        rm_preprocess(session);

        if(session->cfg->find_duplicates || session->cfg->merge_directories) {
            rm_shred_run(session);

            rm_log_debug("Dupe search finished at time %.3f\n", g_timer_elapsed(session->timer, NULL));
        }
    }

    if(session->cfg->merge_directories) {
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_MERGE);
        rm_tm_finish(session->dir_merger);
    }

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PRE_SHUTDOWN);
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_SUMMARY);

failure:
    return exit_state;
}
