/**
* This file is part of rmlint.
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
*  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
*  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
*
* Hosted on http://github.com/sahib/rmlint
**/

#include <ctype.h>
#include <math.h>
#include <search.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "cfg.h"
#include "formats.h"
#include "preprocess.h"
#include "traverse.h"
#include "treemerge.h"
#include "utilities.h"

/* Set all options to defaults */
static void rm_cfg_set_default(RmCfg *cfg) {
    /* Set everything to 0 at first,
     * only non-null options are listed below.
     */
    memset(cfg, 0, sizeof(RmCfg));

    /* Traversal options */
    cfg->depth = PATH_MAX / 2;
    cfg->limits_specified = true;
    cfg->minsize = 1;
    cfg->maxsize = G_MAXUINT64;

    /* Lint Types */
    cfg->ignore_hidden = true;
    cfg->find_emptydirs = true;
    cfg->find_emptyfiles = true;
    cfg->find_duplicates = true;
    cfg->find_badids = true;
    cfg->find_badlinks = true;
    cfg->find_hardlinked_dupes = true;
    cfg->build_fiemap = true;
    cfg->crossdev = true;
    cfg->list_mounts = true;

    /* Misc options */
    cfg->sort_criteria = g_strdup("pOma");

    cfg->checksum_type = RM_DEFAULT_DIGEST;
    cfg->with_color = true;
    cfg->with_stdout_color = true;
    cfg->with_stderr_color = true;
    cfg->threads = 16;
    cfg->threads_per_disk = 2;
    cfg->verbosity = G_LOG_LEVEL_INFO;
    cfg->follow_symlinks = false;

    cfg->total_mem = (RmOff)1024 * 1024 * 1024;
    cfg->sweep_size = 1024 * 1024 * 1024;
    cfg->sweep_count = 1024 * 16;

    cfg->skip_start_factor = 0.0;
    cfg->skip_end_factor = 1.0;

    cfg->use_absolute_start_offset = false;
    cfg->use_absolute_end_offset = false;
    cfg->skip_start_offset = 0;
    cfg->skip_end_offset = 0;
    cfg->mtime_window = -1;

    cfg->verbosity_count = 2;
    cfg->paranoia_count = 0;
    cfg->output_cnt[0] = -1;
    cfg->output_cnt[1] = -1;
}

/*
 * For paths passed to rmlint from command line (or stdin), order is
 * important.  This procedure creates a new RmPath, which contains the
 * path and additional positional information, and adds it to cfg->paths.
 * In the special case of --replay, json paths are instead stored
 * in cfg->json_paths.
 */
static guint rm_cfg_add_path(RmCfg *cfg, bool is_prefd, const char *path) {
    int rc = 0;

#if HAVE_FACCESSAT
    rc = faccessat(AT_FDCWD, path, R_OK, AT_EACCESS);
#else
    rc = access(path, R_OK);
#endif

    if(rc != 0) {
        rm_log_warning_line(_("Can't open directory or file \"%s\": %s"), path,
                            strerror(errno));
        return 0;
    }

    /* calling realpath(path, NULL) uses malloc to allocate a buffer of up to
     * PATH_MAX bytes to hold the resolved pathname (deallocate buffer using
     * free()).
     */
    char *real_path = realpath(path, NULL);
    if(real_path == NULL) {
        rm_log_warning_line(_("Can't get real path for directory or file \"%s\": %s"),
                            path, strerror(errno));
        return 0;
    }

    RmPath *rmpath = g_slice_new(RmPath);
    rmpath->path = real_path;
    rmpath->idx = cfg->path_count++;
    rmpath->is_prefd = is_prefd;
    rmpath->treat_as_single_vol = strncmp(path, "//", 2) == 0;

    if(cfg->replay && g_str_has_suffix(rmpath->path, ".json")) {
        cfg->json_paths = g_slist_prepend(cfg->json_paths, rmpath);
        return 1;
    }

    cfg->paths = g_slist_prepend(cfg->paths, rmpath);
    return 1;
}

static void rm_path_free(RmPath *rmpath) {
    free(rmpath->path);
    g_slice_free(RmPath, rmpath);
}

static void rm_cfg_show_version(void) {
    fprintf(stderr, "version %s compiled: %s at [%s] \"%s\" (rev %s)\n", RM_VERSION,
            __DATE__, __TIME__, RM_VERSION_NAME, RM_VERSION_GIT_REVISION);

    /* Make a list of all supported features from the macros in config.h */
    /* clang-format off */
    struct {
        bool enabled : 1;
        const char *name;
    } features[] = {{.name = "mounts",         .enabled = HAVE_BLKID & HAVE_GIO_UNIX},
                    {.name = "nonstripped",    .enabled = HAVE_LIBELF},
                    {.name = "fiemap",         .enabled = HAVE_FIEMAP},
                    {.name = "sha512",         .enabled = HAVE_SHA512},
                    {.name = "bigfiles",       .enabled = HAVE_BIGFILES},
                    {.name = "intl",           .enabled = HAVE_LIBINTL},
                    {.name = "replay",         .enabled = HAVE_JSON_GLIB},
                    {.name = "xattr",          .enabled = HAVE_XATTR},
                    {.name = "btrfs-support",  .enabled = HAVE_BTRFS_H},
                    {.name = NULL,             .enabled = 0}};
    /* clang-format on */

    fprintf(stderr, _("compiled with:"));
    for(int i = 0; features[i].name; ++i) {
        fprintf(stderr, " %c%s", (features[i].enabled) ? '+' : '-', features[i].name);
    }

    fprintf(stderr, RESET "\n\n");
    fprintf(stderr, _("rmlint was written by Christopher <sahib> Pahl and Daniel "
                      "<SeeSpotRun> Thomas."));
    fprintf(stderr, "\n");
    fprintf(stderr, _("The code at https://github.com/sahib/rmlint is licensed under the "
                      "terms of the GPLv3."));
    fprintf(stderr, "\n");
    exit(0);
}

static void rm_cfg_show_manpage(void) {
    static const char *commands[] = {"man %s docs/rmlint.1.gz 2> /dev/null",
                                     "man %s rmlint", NULL};

    bool found_manpage = false;

    for(int i = 0; commands[i] && !found_manpage; ++i) {
        char cmd_buf[512] = {0};
        if(snprintf(cmd_buf, sizeof(cmd_buf), commands[i],
                    (RM_MANPAGE_USE_PAGER) ? "" : "-P cat") == -1) {
            continue;
        }

        if(system(cmd_buf) == 0) {
            found_manpage = true;
        }
    }

    if(!found_manpage) {
        rm_log_warning_line(_("You seem to have no manpage for rmlint."));
        rm_log_warning_line(_("Please run rmlint --help to show the regular help."));
        rm_log_warning_line(_(
            "Alternatively, visit https://rmlint.rtfd.org for the online documentation"));
    }

    exit(0);
}

/* clang-format off */
static const struct FormatSpec {
    const char *id;
    unsigned base;
    unsigned exponent;
} SIZE_FORMAT_TABLE[] = {
    /* This list is sorted, so bsearch() can be used */
    {.id = "b",  .base = 512,  .exponent = 1},
    {.id = "c",  .base = 1,    .exponent = 1},
    {.id = "e",  .base = 1000, .exponent = 6},
    {.id = "eb", .base = 1024, .exponent = 6},
    {.id = "g",  .base = 1000, .exponent = 3},
    {.id = "gb", .base = 1024, .exponent = 3},
    {.id = "k",  .base = 1000, .exponent = 1},
    {.id = "kb", .base = 1024, .exponent = 1},
    {.id = "m",  .base = 1000, .exponent = 2},
    {.id = "mb", .base = 1024, .exponent = 2},
    {.id = "p",  .base = 1000, .exponent = 5},
    {.id = "pb", .base = 1024, .exponent = 5},
    {.id = "t",  .base = 1000, .exponent = 4},
    {.id = "tb", .base = 1024, .exponent = 4},
    {.id = "w",  .base = 2,    .exponent = 1}
};
/* clang-format on */

typedef struct FormatSpec FormatSpec;

static const int SIZE_FORMAT_TABLE_N = sizeof(SIZE_FORMAT_TABLE) / sizeof(FormatSpec);

static int rm_cfg_compare_spec_elem(const void *fmt_a, const void *fmt_b) {
    return strcasecmp(((FormatSpec *)fmt_a)->id, ((FormatSpec *)fmt_b)->id);
}

static RmOff rm_cfg_size_string_to_bytes(const char *size_spec, GError **error) {
    if(size_spec == NULL) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Input size is empty"));
        return 0;
    }

    char *format = NULL;
    long double decimal = strtold(size_spec, &format);

    if(decimal == 0 && format == size_spec) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("This does not look like a number"));
        return 0;
    } else if(decimal < 0) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Negativ sizes are no good idea"));
        return 0;
    } else if(*format) {
        format = g_strstrip(format);
    } else {
        return round(decimal);
    }

    FormatSpec key = {.id = format};
    FormatSpec *found = bsearch(&key, SIZE_FORMAT_TABLE, SIZE_FORMAT_TABLE_N,
                                sizeof(FormatSpec), rm_cfg_compare_spec_elem);

    if(found != NULL) {
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
static gboolean rm_cfg_size_range_string_to_bytes(const char *range_spec, RmOff *min,
                                                  RmOff *max, GError **error) {
    *min = 0;
    *max = G_MAXULONG;

    range_spec = (const char *)g_strstrip((char *)range_spec);
    gchar **split = g_strsplit(range_spec, "-", 2);

    if(*range_spec == '-') {
        /* Act like it was "0-..." */
        split[0] = g_strdup("0");
    }

    if(split[0] != NULL) {
        *min = rm_cfg_size_string_to_bytes(split[0], error);
    }

    if(split[1] != NULL && *error == NULL) {
        *max = rm_cfg_size_string_to_bytes(split[1], error);
    }

    g_strfreev(split);

    if(*error == NULL && *max < *min) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Max is smaller than min"));
    }

    return (*error == NULL);
}

static gboolean rm_cfg_parse_limit_sizes(_UNUSED const char *option_name,
                                         const gchar *range_spec,
                                         RmCfg *cfg,
                                         GError **error) {
    if(!rm_cfg_size_range_string_to_bytes(range_spec, &cfg->minsize, &cfg->maxsize,
                                          error)) {
        g_prefix_error(error, _("cannot parse --size: "));
        return false;
    } else {
        cfg->limits_specified = true;
        return true;
    }
}

static GLogLevelFlags VERBOSITY_TO_LOG_LEVEL[] = {[0] = G_LOG_LEVEL_CRITICAL,
                                                  [1] = G_LOG_LEVEL_ERROR,
                                                  [2] = G_LOG_LEVEL_WARNING,
                                                  [3] = G_LOG_LEVEL_MESSAGE |
                                                        G_LOG_LEVEL_INFO,
                                                  [4] = G_LOG_LEVEL_DEBUG};

static bool rm_cfg_read_paths_from_stdin(RmCfg *cfg, bool is_prefd) {
    char path_buf[PATH_MAX];
    char *tokbuf = NULL;
    bool all_paths_read = true;

    /* Still read all paths on errors, so the user knows all paths that failed */
    while(fgets(path_buf, PATH_MAX, stdin)) {
        all_paths_read &=
            rm_cfg_add_path(cfg, is_prefd, strtok_r(path_buf, "\n", &tokbuf));
    }

    return all_paths_read;
}

static bool rm_cfg_parse_output_pair(RmCfg *cfg, const char *pair, GError **error) {
    rm_assert_gentle(cfg);
    rm_assert_gentle(pair);

    char *separator = strchr(pair, ':');
    char *full_path = NULL;
    char format_name[100];
    memset(format_name, 0, sizeof(format_name));

    if(separator == NULL) {
        /* default to stdout */
        char *extension = strchr(pair, '.');
        if(extension == NULL) {
            full_path = "stdout";
            strncpy(format_name, pair, strlen(pair));
        } else {
            extension += 1;
            full_path = (char *)pair;
            strncpy(format_name, extension, strlen(extension));
        }
    } else {
        full_path = separator + 1;
        strncpy(format_name, pair, MIN((long)sizeof(format_name), separator - pair));
    }

    if(!rm_fmt_add(cfg->formats, format_name, full_path)) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Adding -o %s as output failed"), pair);
        return false;
    }

    return true;
}

static bool rm_cfg_parse_config_pair(RmCfg *cfg, const char *pair, GError **error) {
    char *domain = strchr(pair, ':');
    if(domain == NULL) {
        g_set_error(error, RM_ERROR_QUARK, 0,
                    _("No format (format:key[=val]) specified in '%s'"), pair);
        return false;
    }

    char *key = NULL, *value = NULL;
    char **key_val = g_strsplit(&domain[1], "=", 2);
    int len = g_strv_length(key_val);
    bool result = true;

    if(len < 1) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Missing key (format:key[=val]) in '%s'"),
                    pair);
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
    if(!rm_fmt_is_valid_key(cfg->formats, formatter, key)) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Invalid key `%s' for formatter `%s'"),
                    key, formatter);
        g_free(key);
        g_free(value);
        result = false;
    } else {
        rm_fmt_set_config_value(cfg->formats, formatter, key, value);
    }

    g_free(formatter);
    g_strfreev(key_val);
    return result;
}

static gboolean rm_cfg_parse_config(_UNUSED const char *option_name,
                                    const char *pair,
                                    RmCfg *cfg,
                                    _UNUSED GError **error) {
    return rm_cfg_parse_config_pair(cfg, pair, error);
}

static double rm_cfg_parse_clamp_factor(const char *string, GError **error) {
    char *error_loc = NULL;
    gdouble factor = g_strtod(string, &error_loc);

    if(error_loc != NULL && *error_loc != '\0' && *error_loc != '%') {
        g_set_error(error, RM_ERROR_QUARK, 0,
                    _("Unable to parse factor \"%s\": error begins at %s"), string,
                    error_loc);
        return 0;
    }

    if(error_loc != NULL && *error_loc == '%') {
        factor /= 100;
    }

    if(0 > factor || factor > 1) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("factor value is not in range [0-1]: %f"),
                    factor);
        return 0;
    }

    return factor;
}

static RmOff rm_cfg_parse_clamp_offset(const char *string, GError **error) {
    RmOff offset = rm_cfg_size_string_to_bytes(string, error);

    if(*error != NULL) {
        g_prefix_error(error, _("Unable to parse offset \"%s\": "), string);
        return 0;
    }

    return offset;
}

static void rm_cfg_parse_clamp_option(RmCfg *cfg, const char *string, bool start_or_end,
                                      GError **error) {
    if(strchr(string, '.') || g_str_has_suffix(string, "%")) {
        gdouble factor = rm_cfg_parse_clamp_factor(string, error);
        if(start_or_end) {
            cfg->use_absolute_start_offset = false;
            cfg->skip_start_factor = factor;
        } else {
            cfg->use_absolute_end_offset = false;
            cfg->skip_end_factor = factor;
        }
    } else {
        RmOff offset = rm_cfg_parse_clamp_offset(string, error);
        if(start_or_end) {
            cfg->use_absolute_start_offset = true;
            cfg->skip_start_offset = offset;
        } else {
            cfg->use_absolute_end_offset = true;
            cfg->skip_end_offset = offset;
        }
    }
}

/* parse comma-separated strong of lint types and set cfg accordingly */
typedef struct RmLintTypeOption {
    const char **names;
    gboolean **enable;
} RmLintTypeOption;

/* compare function for parsing lint type arguments */
int rm_cfg_find_line_type_func(const void *v_input, const void *v_option) {
    const char *input = v_input;
    const RmLintTypeOption *option = v_option;

    for(int i = 0; option->names[i]; ++i) {
        if(strcmp(option->names[i], input) == 0) {
            return 0;
        }
    }
    return 1;
}

#define OPTS (gboolean *[])
#define NAMES (const char *[])

static char rm_cfg_find_lint_types_sep(const char *lint_string) {
    if(*lint_string == '+' || *lint_string == '-') {
        lint_string++;
    }

    while(isalpha((unsigned char)*lint_string)) {
        lint_string++;
    }

    return *lint_string;
}

static gboolean rm_cfg_parse_lint_types(_UNUSED const char *option_name,
                                        const char *lint_string,
                                        RmCfg *cfg,
                                        _UNUSED GError **error) {
    RmLintTypeOption option_table[] = {
        {.names = NAMES{"all", 0},
         .enable = OPTS{&cfg->find_badids, &cfg->find_badlinks, &cfg->find_emptydirs,
                        &cfg->find_emptyfiles, &cfg->find_nonstripped,
                        &cfg->find_duplicates, &cfg->merge_directories, 0}},
        {
            .names = NAMES{"minimal", 0},
            .enable =
                OPTS{&cfg->find_badids, &cfg->find_badlinks, &cfg->find_duplicates, 0},
        },
        {
            .names = NAMES{"minimaldirs", 0},
            .enable =
                OPTS{&cfg->find_badids, &cfg->find_badlinks, &cfg->merge_directories, 0},
        },
        {
            .names = NAMES{"defaults", 0},
            .enable = OPTS{&cfg->find_badids, &cfg->find_badlinks, &cfg->find_emptydirs,
                           &cfg->find_emptyfiles, &cfg->find_duplicates, 0},
        },
        {
            .names = NAMES{"none", 0}, .enable = OPTS{0},
        },
        {.names = NAMES{"badids", "bi", 0}, .enable = OPTS{&cfg->find_badids, 0}},
        {.names = NAMES{"badlinks", "bl", 0}, .enable = OPTS{&cfg->find_badlinks, 0}},
        {.names = NAMES{"emptydirs", "ed", 0}, .enable = OPTS{&cfg->find_emptydirs, 0}},
        {.names = NAMES{"emptyfiles", "ef", 0}, .enable = OPTS{&cfg->find_emptyfiles, 0}},
        {.names = NAMES{"nonstripped", "ns", 0},
         .enable = OPTS{&cfg->find_nonstripped, 0}},
        {.names = NAMES{"duplicates", "df", "dupes", 0},
         .enable = OPTS{&cfg->find_duplicates, 0}},
        {.names = NAMES{"duplicatedirs", "dd", "dupedirs", 0},
         .enable = OPTS{&cfg->merge_directories, 0}}};

    RmLintTypeOption *all_opts = &option_table[0];

    /* initialize all options to disabled by default */
    for(int i = 0; all_opts->enable[i]; i++) {
        *all_opts->enable[i] = false;
    }

    /* split the comma-separates list of options */
    char lint_sep[2] = {0, 0};
    lint_sep[0] = rm_cfg_find_lint_types_sep(lint_string);
    if(lint_sep[0] == 0) {
        lint_sep[0] = ',';
    }

    char **lint_types = g_strsplit(lint_string, lint_sep, -1);

    /* iterate over the separated option strings */
    for(int index = 0; lint_types[index]; index++) {
        char *lint_type = lint_types[index];
        int sign = 0;

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
        RmLintTypeOption *option =
            lfind(lint_type, &option_table, &elems, sizeof(RmLintTypeOption),
                  rm_cfg_find_line_type_func);

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
        cfg->cache_file_structs = true;
    }

    /* clean up */
    g_strfreev(lint_types);
    return true;
}

static bool rm_cfg_timestamp_is_plain(const char *stamp) {
    return strchr(stamp, 'T') ? false : true;
}

static gboolean rm_cfg_parse_timestamp(_UNUSED const char *option_name,
                                       const gchar *string, RmCfg *cfg, GError **error) {
    gdouble result = 0;
    bool plain = rm_cfg_timestamp_is_plain(string);
    cfg->filter_mtime = false;

    if(plain) {
        /* timespec might include sub-second fraction */
        result = strtod(string, NULL);
    } else {
        /* Parse ISO8601 timestamps like 2006-02-03T16:45:09.000Z */
        result = rm_iso8601_parse(string);

        /* debug */
        {
            char time_buf[256];
            memset(time_buf, 0, sizeof(time_buf));
            rm_iso8601_format(time(NULL), time_buf, sizeof(time_buf));
            rm_log_debug_line("timestamp %s understood as %f", time_buf, result);
        }
    }

    if(FLOAT_SIGN_DIFF(result, 0.0, MTIME_TOL) != 1) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Unable to parse time spec \"%s\""),
                    string);
        return false;
    }

    /* Some sort of success. */
    cfg->filter_mtime = true;

    time_t now = time(NULL);
    if((time_t)result > now) {
        /* Not critical, maybe there are some uses for this,
         * but print at least a small warning as indication.
         * */
        if(plain) {
            rm_log_warning_line(_("-n %lu is newer than current time (%lu)."),
                                (long)result, (long)now);
        } else {
            char time_buf[256];
            memset(time_buf, 0, sizeof(time_buf));
            rm_iso8601_format(time(NULL), time_buf, sizeof(time_buf));

            rm_log_warning_line("-N %s is newer than current time (%s) [%lu > %lu]",
                                string, time_buf, (time_t)result, now);
        }
    }

    /* Remember our result */
    cfg->min_mtime = result;
    return true;
}

static gboolean rm_cfg_parse_timestamp_file(const char *option_name,
                                            const gchar *timestamp_path, RmCfg *cfg,
                                            GError **error) {
    bool plain = true, success = false;
    FILE *stamp_file = fopen(timestamp_path, "r");

    /* Assume failure */
    cfg->filter_mtime = false;

    if(stamp_file) {
        char stamp_buf[1024];
        memset(stamp_buf, 0, sizeof(stamp_buf));

        if(fgets(stamp_buf, sizeof(stamp_buf), stamp_file) != NULL) {
            success =
                rm_cfg_parse_timestamp(option_name, g_strstrip(stamp_buf), cfg, error);
            plain = rm_cfg_timestamp_is_plain(stamp_buf);
        }

        fclose(stamp_file);
    } else {
        /* Cannot read... */
        plain = false;
    }

    if(!success) {
        return false;
    }

    rm_fmt_add(cfg->formats, "stamp", timestamp_path);
    if(!plain) {
        /* Enable iso8601 timestamp output */
        rm_fmt_set_config_value(cfg->formats, "stamp", g_strdup("iso8601"),
                                g_strdup("true"));
    }

    return success;
}

static void rm_cfg_set_verbosity_from_cnt(RmCfg *cfg, int verbosity_counter) {
    cfg->verbosity = VERBOSITY_TO_LOG_LEVEL[CLAMP(
        verbosity_counter,
        1,
        (int)(sizeof(VERBOSITY_TO_LOG_LEVEL) / sizeof(GLogLevelFlags)) - 1)];
}

static void rm_cfg_set_paranoia_from_cnt(RmCfg *cfg, int paranoia_counter,
                                         GError **error) {
    /* Handle the paranoia option */
    switch(paranoia_counter) {
    case -2:
        cfg->checksum_type = RM_DIGEST_XXHASH;
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
        if(error && *error == NULL) {
            g_set_error(error, RM_ERROR_QUARK, 0,
                        _("Only up to -pp or down to -PP flags allowed"));
        }
        break;
    }
}

static void rm_cfg_on_error(_UNUSED GOptionContext *context, _UNUSED GOptionGroup *group,
                            RmCfg *cfg, GError **error) {
    if(error != NULL) {
        rm_log_error_line("%s.", (*error)->message);
        g_clear_error(error);
        cfg->cmdline_parse_error = true;
    }
}

static gboolean rm_cfg_parse_algorithm(_UNUSED const char *option_name,
                                       const gchar *value,
                                       RmCfg *cfg,
                                       GError **error) {
    cfg->checksum_type = rm_string_to_digest_type(value);

    if(cfg->checksum_type == RM_DIGEST_UNKNOWN) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Unknown hash algorithm: '%s'"), value);
        return false;
    } else if(cfg->checksum_type == RM_DIGEST_BASTARD) {
        cfg->hash_seed1 = time(NULL) * (GPOINTER_TO_UINT(cfg));
        cfg->hash_seed2 = GPOINTER_TO_UINT(&cfg);
    }
    return true;
}

static gboolean rm_cfg_parse_small_output(_UNUSED const char *option_name,
                                          const gchar *output_pair, RmCfg *cfg,
                                          _UNUSED GError **error) {
    cfg->output_cnt[0] = MAX(cfg->output_cnt[0], 0);
    cfg->output_cnt[0] += rm_cfg_parse_output_pair(cfg, output_pair, error);
    return true;
}

static gboolean rm_cfg_parse_large_output(_UNUSED const char *option_name,
                                          const gchar *output_pair, RmCfg *cfg,
                                          _UNUSED GError **error) {
    cfg->output_cnt[1] = MAX(cfg->output_cnt[1], 0);
    cfg->output_cnt[1] += rm_cfg_parse_output_pair(cfg, output_pair, error);
    return true;
}

static gboolean rm_cfg_parse_mem(const gchar *size_spec, GError **error, RmOff *target) {
    RmOff size = rm_cfg_size_string_to_bytes(size_spec, error);

    if(*error != NULL) {
        g_prefix_error(error, _("Invalid size description \"%s\": "), size_spec);
        return false;
    } else {
        *target = size;
        return true;
    }
}

static gboolean rm_cfg_parse_limit_mem(_UNUSED const char *option_name,
                                       const gchar *size_spec, RmCfg *cfg,
                                       GError **error) {
    return (rm_cfg_parse_mem(size_spec, error, &cfg->total_mem));
}

static gboolean rm_cfg_parse_sweep_size(_UNUSED const char *option_name,
                                        const gchar *size_spec, RmCfg *cfg,
                                        GError **error) {
    return (rm_cfg_parse_mem(size_spec, error, &cfg->sweep_size));
}

static gboolean rm_cfg_parse_sweep_count(_UNUSED const char *option_name,
                                         const gchar *size_spec, RmCfg *cfg,
                                         GError **error) {
    return (rm_cfg_parse_mem(size_spec, error, &cfg->sweep_count));
}

static gboolean rm_cfg_parse_clamp_low(_UNUSED const char *option_name, const gchar *spec,
                                       RmCfg *cfg, _UNUSED GError **error) {
    rm_cfg_parse_clamp_option(cfg, spec, true, error);
    return (error && *error == NULL);
}

static gboolean rm_cfg_parse_clamp_top(_UNUSED const char *option_name, const gchar *spec,
                                       RmCfg *cfg, _UNUSED GError **error) {
    rm_cfg_parse_clamp_option(cfg, spec, false, error);
    return (error && *error == NULL);
}

static gboolean rm_cfg_parse_progress(_UNUSED const char *option_name,
                                      _UNUSED const gchar *value, RmCfg *cfg,
                                      _UNUSED GError **error) {
    rm_fmt_clear(cfg->formats);
    rm_fmt_add(cfg->formats, "progressbar", "stdout");
    rm_fmt_add(cfg->formats, "summary", "stdout");

    cfg->progress_enabled = true;

    return true;
}

static void rm_cfg_set_default_outputs(RmCfg *cfg) {
    rm_fmt_add(cfg->formats, "pretty", "stdout");
    rm_fmt_add(cfg->formats, "summary", "stdout");

    if(cfg->replay) {
        rm_fmt_add(cfg->formats, "sh", "rmlint.replay.sh");
        rm_fmt_add(cfg->formats, "json", "rmlint.replay.json");
    } else {
        rm_fmt_add(cfg->formats, "sh", "rmlint.sh");
        rm_fmt_add(cfg->formats, "json", "rmlint.json");
    }
}

static gboolean rm_cfg_parse_no_progress(_UNUSED const char *option_name,
                                         _UNUSED const gchar *value, RmCfg *cfg,
                                         _UNUSED GError **error) {
    rm_fmt_clear(cfg->formats);
    rm_cfg_set_default_outputs(cfg);
    rm_cfg_set_verbosity_from_cnt(cfg, cfg->verbosity_count);
    return true;
}

static gboolean rm_cfg_parse_loud(_UNUSED const char *option_name,
                                  _UNUSED const gchar *count, RmCfg *cfg,
                                  _UNUSED GError **error) {
    rm_cfg_set_verbosity_from_cnt(cfg, ++cfg->verbosity_count);
    return true;
}

static gboolean rm_cfg_parse_quiet(_UNUSED const char *option_name,
                                   _UNUSED const gchar *count, RmCfg *cfg,
                                   _UNUSED GError **error) {
    rm_cfg_set_verbosity_from_cnt(cfg, --cfg->verbosity_count);
    return true;
}

static gboolean rm_cfg_parse_paranoid(_UNUSED const char *option_name,
                                      _UNUSED const gchar *count, RmCfg *cfg,
                                      _UNUSED GError **error) {
    rm_cfg_set_paranoia_from_cnt(cfg, ++cfg->paranoia_count, error);
    return true;
}

static gboolean rm_cfg_parse_less_paranoid(_UNUSED const char *option_name,
                                           _UNUSED const gchar *count, RmCfg *cfg,
                                           _UNUSED GError **error) {
    rm_cfg_set_paranoia_from_cnt(cfg, --cfg->paranoia_count, error);
    return true;
}

static gboolean rm_cfg_parse_partial_hidden(_UNUSED const char *option_name,
                                            _UNUSED const gchar *count, RmCfg *cfg,
                                            _UNUSED GError **error) {
    cfg->ignore_hidden = false;
    cfg->partial_hidden = true;

    return true;
}

static gboolean rm_cfg_parse_see_symlinks(_UNUSED const char *option_name,
                                          _UNUSED const gchar *count, RmCfg *cfg,
                                          _UNUSED GError **error) {
    cfg->see_symlinks = true;
    cfg->follow_symlinks = false;

    return true;
}

static gboolean rm_cfg_parse_follow_symlinks(_UNUSED const char *option_name,
                                             _UNUSED const gchar *count, RmCfg *cfg,
                                             _UNUSED GError **error) {
    cfg->see_symlinks = false;
    cfg->follow_symlinks = true;

    return true;
}

static gboolean rm_cfg_parse_no_partial_hidden(_UNUSED const char *option_name,
                                               _UNUSED const gchar *count,
                                               RmCfg *cfg,
                                               _UNUSED GError **error) {
    cfg->ignore_hidden = true;
    cfg->partial_hidden = false;

    return true;
}

static gboolean rm_cfg_parse_merge_directories(_UNUSED const char *option_name,
                                               _UNUSED const gchar *_,
                                               RmCfg *cfg,
                                               _UNUSED GError **error) {
    cfg->merge_directories = true;

    /* Pull in some options for convinience,
     * duplicate dir detection works better with them.
     *
     * They may be disabled explicitly though.
     */
    cfg->follow_symlinks = false;
    cfg->see_symlinks = true;
    rm_cfg_parse_partial_hidden(NULL, NULL, cfg, error);

    /* Keep RmFiles after shredder. */
    cfg->cache_file_structs = true;

    return true;
}

static gboolean rm_cfg_parse_honour_dir_layout(_UNUSED const char *option_name,
                                               _UNUSED const gchar *_,
                                               RmCfg *cfg,
                                               _UNUSED GError **error) {
    cfg->honour_dir_layout = true;
    return true;
}

static gboolean rm_cfg_parse_permissions(_UNUSED const char *option_name,
                                         const gchar *perms, RmCfg *cfg, GError **error) {
    if(perms == NULL) {
        cfg->permissions = R_OK | W_OK;
    } else {
        while(*perms) {
            switch(*perms++) {
            case 'r':
                cfg->permissions |= R_OK;
                break;
            case 'w':
                cfg->permissions |= W_OK;
                break;
            case 'x':
                cfg->permissions |= X_OK;
                break;
            default:
                g_set_error(error, RM_ERROR_QUARK, 0,
                            _("Permissions string needs to be one or many of [rwx]"));
                return false;
            }
        }
    }

    return true;
}

static gboolean rm_cfg_check_lettervec(const char *option_name, const char *criteria,
                                       const char *valid, GError **error) {
    for(int i = 0; criteria[i]; ++i) {
        if(strchr(valid, criteria[i]) == NULL) {
            g_set_error(error, RM_ERROR_QUARK, 0, _("%s may only contain [%s], not `%c`"),
                        option_name, valid, criteria[i]);
            return false;
        }
    }

    return true;
}

static gboolean rm_cfg_parse_sortby(_UNUSED const char *option_name,
                                    const gchar *criteria, RmCfg *cfg, GError **error) {
    if(!rm_cfg_check_lettervec(option_name, criteria, "moanspMOANSP", error)) {
        return false;
    }

    /* Remember the criteria string */
    strncpy(cfg->rank_criteria, criteria, sizeof(cfg->rank_criteria));

    /* ranking the files depends on caching them to the end of the program */
    cfg->cache_file_structs = true;

    return true;
}

static gboolean rm_cfg_parse_rankby(_UNUSED const char *option_name,
                                    const gchar *criteria, RmCfg *cfg, GError **error) {
    g_free(cfg->sort_criteria);

    cfg->sort_criteria = rm_pp_compile_patterns(cfg, criteria, error);

    if(error && *error != NULL) {
        return false;
    }

    if(!rm_cfg_check_lettervec(option_name, cfg->sort_criteria, "dlamprxhoDLAMPRXHO",
                               error)) {
        return false;
    }

    return true;
}

static gboolean rm_cfg_parse_replay(_UNUSED const char *option_name,
                                    _UNUSED const gchar *x, RmCfg *cfg,
                                    _UNUSED GError **error) {
    cfg->replay = true;
    cfg->cache_file_structs = true;
    return true;
}

static gboolean rm_cfg_parse_equal(_UNUSED const char *option_name,
                                   _UNUSED const gchar *x, RmCfg *cfg,
                                   _UNUSED GError **error) {
    rm_cfg_parse_merge_directories(NULL, NULL, cfg, error);
    rm_cfg_parse_lint_types(NULL, "df,dd", cfg, error);
    cfg->run_equal_mode = true;

    /* See issue #233; partial hidden needs to be disabled */
    cfg->partial_hidden = false;
    cfg->ignore_hidden = false;

    /* See issue #234 fore more discussion on this */
    cfg->limits_specified = true;
    cfg->minsize = 0;

    rm_fmt_clear(cfg->formats);
    rm_fmt_add(cfg->formats, "_equal", "stdout");
    return true;
}

static bool rm_cfg_set_cwd(RmCfg *cfg) {
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

static bool rm_cfg_set_cmdline(RmCfg *cfg, int argc, char **argv) {
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

static bool rm_cfg_set_paths(RmCfg *cfg, char **paths) {
    bool is_prefd = false;
    bool all_paths_valid = true;

    /* Check the directory to be valid */
    for(int i = 0; paths && paths[i]; ++i) {
        if(strcmp(paths[i], "-") == 0) {
            /* option '-' means read paths from stdin */
            all_paths_valid &= rm_cfg_read_paths_from_stdin(cfg, is_prefd);
        } else if(strcmp(paths[i], "//") == 0) {
            /* the '//' separator separates non-preferred paths from preferred */
            is_prefd = !is_prefd;
        } else {
            all_paths_valid &= rm_cfg_add_path(cfg, is_prefd, paths[i]);
        }
    }

    g_strfreev(paths);

    if(cfg->path_count == 0 && all_paths_valid) {
        /* Still no path set? - use `pwd` */
        rm_cfg_add_path(cfg, is_prefd, cfg->iwd);
    }

    /* Only return success if everything is fine. */
    return all_paths_valid;
}

static bool rm_cfg_set_outputs(RmCfg *cfg, GError **error) {
    if(cfg->output_cnt[0] >= 0 && cfg->output_cnt[1] >= 0) {
        g_set_error(error, RM_ERROR_QUARK, 0,
                    _("Specifiyng both -o and -O is not allowed"));
        return false;
    } else if(cfg->output_cnt[0] < 0 && cfg->progress_enabled == false) {
        rm_cfg_set_default_outputs(cfg);
    }

    return true;
}

static char *rm_cfg_find_own_executable_path(RmCfg *cfg, char **argv) {
    if(cfg->full_argv0_path == NULL) {
        /* Note: this check will only work on linux! */
        char exe_path[PATH_MAX] = {0};
        if(readlink("/proc/self/exe", exe_path, sizeof(exe_path)) != -1) {
            return g_strdup(exe_path);
        }

        if(strchr(argv[0], '/')) {
            return realpath(argv[0], NULL);
        }

        /* More checks might be added here in future. */
    }

    return NULL;
}

void rm_cfg_init(RmCfg *cfg) {
    rm_cfg_set_default(cfg);

    rm_trie_init(&cfg->file_trie);

    cfg->pattern_cache = g_ptr_array_new_full(0, (GDestroyNotify)g_regex_unref);
}

void rm_cfg_clear(RmCfg *cfg) {
    rm_fmt_close(cfg->formats);

    g_free(cfg->sort_criteria);

    g_slist_free_full(cfg->paths, (GDestroyNotify)rm_path_free);
    g_slist_free_full(cfg->json_paths, (GDestroyNotify)rm_path_free);

    g_free(cfg->joined_argv);
    g_free(cfg->full_argv0_path);
    g_free(cfg->iwd);

    g_ptr_array_free(cfg->pattern_cache, TRUE);

    rm_trie_destroy(&cfg->file_trie);

    memset(cfg, 0, sizeof(RmCfg));
}

/* Parse the commandline and set arguments in 'settings' (glob. var accordingly) */
bool rm_cfg_parse_args(int argc, char **argv, RmCfg *cfg) {
    if(rm_util_strv_contains((const gchar *const *)argv, "--gui")) {
        cfg->run_gui = TRUE;
        return true;
    }

    /* List of paths we got passed (or NULL) */
    char **paths = NULL;

    /* General error variable */
    GError *error = NULL;
    GOptionContext *option_parser = NULL;

#define FUNC(name) ((GOptionArgFunc)rm_cfg_parse_##name)
    const int DISABLE = G_OPTION_FLAG_REVERSE, EMPTY = G_OPTION_FLAG_NO_ARG,
              HIDDEN = G_OPTION_FLAG_HIDDEN, OPTIONAL = G_OPTION_FLAG_OPTIONAL_ARG;

    /* Free/Used Options:
       Used: abBcCdDeEfFgGHhiI  kKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ|
       Free:                  jJ                                |
    */

    /* clang-format off */
    const GOptionEntry main_option_entries[] = {
        /* Option with required arguments */
        {"max-depth"        , 'd' , 0        , G_OPTION_ARG_INT      , &cfg->depth          , _("Specify max traversal depth")          , "N"}                   ,
        {"rank-by"          , 'S' , 0        , G_OPTION_ARG_CALLBACK , FUNC(rankby)         , _("Select originals by given  criteria")  , "[dlamprxDLAMPRX]"}    ,
        {"sort-by"          , 'y' , 0        , G_OPTION_ARG_CALLBACK , FUNC(sortby)         , _("Sort rmlint output by given criteria") , "[moansMOANS]"}        ,
        {"types"            , 'T' , 0        , G_OPTION_ARG_CALLBACK , FUNC(lint_types)     , _("Specify lint types")                   , "T"}                   ,
        {"size"             , 's' , 0        , G_OPTION_ARG_CALLBACK , FUNC(limit_sizes)    , _("Specify size limits")                  , "m-M"}                 ,
        {"algorithm"        , 'a' , 0        , G_OPTION_ARG_CALLBACK , FUNC(algorithm)      , _("Choose hash algorithm")                , "A"}                   ,
        {"output"           , 'o' , 0        , G_OPTION_ARG_CALLBACK , FUNC(small_output)   , _("Add output (override default)")        , "FMT[:PATH]"}          ,
        {"add-output"       , 'O' , 0        , G_OPTION_ARG_CALLBACK , FUNC(large_output)   , _("Add output (add to defaults)")         , "FMT[:PATH]"}          ,
        {"newer-than-stamp" , 'n' , 0        , G_OPTION_ARG_CALLBACK , FUNC(timestamp_file) , _("Newer than stamp file")                , "PATH"}                ,
        {"newer-than"       , 'N' , 0        , G_OPTION_ARG_CALLBACK , FUNC(timestamp)      , _("Newer than timestamp")                 , "STAMP"}               ,
        {"config"           , 'c' , 0        , G_OPTION_ARG_CALLBACK , FUNC(config)         , _("Configure a formatter")                , "FMT:K[=V]"}           ,

        /* Non-trvial switches */
        {"progress" , 'g' , EMPTY , G_OPTION_ARG_CALLBACK , FUNC(progress) , _("Enable progressbar")                   , NULL} ,
        {"loud"     , 'v' , EMPTY , G_OPTION_ARG_CALLBACK , FUNC(loud)     , _("Be more verbose (-vvv for much more)") , NULL} ,
        {"quiet"    , 'V' , EMPTY , G_OPTION_ARG_CALLBACK , FUNC(quiet)    , _("Be less verbose (-VVV for much less)") , NULL} ,
        {"replay"   , 'Y' , EMPTY , G_OPTION_ARG_CALLBACK , FUNC(replay)   , _("Re-output a json file")                , "path/to/rmlint.json"} ,
        {"equal"    ,  0 ,  EMPTY , G_OPTION_ARG_CALLBACK , FUNC(equal)    , _("Test for equality of PATHS")           , "PATHS"}           ,

        /* Trivial boolean options */
        {"no-with-color"            , 'W'  , DISABLE   , G_OPTION_ARG_NONE      , &cfg->with_color               , _("Be not that colorful")                                                 , NULL}     ,
        {"hidden"                   , 'r'  , DISABLE   , G_OPTION_ARG_NONE      , &cfg->ignore_hidden            , _("Find hidden files")                                                    , NULL}     ,
        {"followlinks"              , 'f'  , EMPTY     , G_OPTION_ARG_CALLBACK  , FUNC(follow_symlinks)          , _("Follow symlinks")                                                      , NULL}     ,
        {"no-followlinks"           , 'F'  , DISABLE   , G_OPTION_ARG_NONE      , &cfg->follow_symlinks          , _("Ignore symlinks")                                                      , NULL}     ,
        {"paranoid"                 , 'p'  , EMPTY     , G_OPTION_ARG_CALLBACK  , FUNC(paranoid)                 , _("Use more paranoid hashing")                                            , NULL}     ,
        {"no-crossdev"              , 'x'  , DISABLE   , G_OPTION_ARG_NONE      , &cfg->crossdev                 , _("Do not cross mounpoints")                                              , NULL}     ,
        {"keep-all-tagged"          , 'k'  , 0         , G_OPTION_ARG_NONE      , &cfg->keep_all_tagged          , _("Keep all tagged files")                                                , NULL}     ,
        {"keep-all-untagged"        , 'K'  , 0         , G_OPTION_ARG_NONE      , &cfg->keep_all_untagged        , _("Keep all untagged files")                                              , NULL}     ,
        {"must-match-tagged"        , 'm'  , 0         , G_OPTION_ARG_NONE      , &cfg->must_match_tagged        , _("Must have twin in tagged dir")                                         , NULL}     ,
        {"must-match-untagged"      , 'M'  , 0         , G_OPTION_ARG_NONE      , &cfg->must_match_untagged      , _("Must have twin in untagged dir")                                       , NULL}     ,
        {"match-basename"           , 'b'  , 0         , G_OPTION_ARG_NONE      , &cfg->match_basename           , _("Only find twins with same basename")                                   , NULL}     ,
        {"match-extension"          , 'e'  , 0         , G_OPTION_ARG_NONE      , &cfg->match_with_extension     , _("Only find twins with same extension")                                  , NULL}     ,
        {"match-without-extension"  , 'i'  , 0         , G_OPTION_ARG_NONE      , &cfg->match_without_extension  , _("Only find twins with same basename minus extension")                   , NULL}     ,
        {"merge-directories"        , 'D'  , EMPTY     , G_OPTION_ARG_CALLBACK  , FUNC(merge_directories)        , _("Find duplicate directories")                                           , NULL}     ,
        {"honour-dir-layout"        , 'j'  , EMPTY     , G_OPTION_ARG_CALLBACK  , FUNC(honour_dir_layout)        , _("Only find directories with same file layout")                          , NULL}     ,
        {"perms"                    , 'z'  , OPTIONAL  , G_OPTION_ARG_CALLBACK  , FUNC(permissions)              , _("Only use files with certain permissions")                              , "[RWX]+"} ,
        {"no-hardlinked"            , 'L'  , DISABLE   , G_OPTION_ARG_NONE      , &cfg->find_hardlinked_dupes    , _("Ignore hardlink twins")                                                , NULL}     ,
        {"partial-hidden"           , 0    , EMPTY     , G_OPTION_ARG_CALLBACK  , FUNC(partial_hidden)           , _("Find hidden files in duplicate folders only")                          , NULL}     ,
        {"mtime-window"             , 'Z'  , 0         , G_OPTION_ARG_DOUBLE    , &cfg->mtime_window             , _("Consider duplicates only equal when mtime differs at max. T seconds")  , "T"}      ,
        {"btrfs-clone"              , 0    , 0         , G_OPTION_ARG_NONE      , &cfg->btrfs_clone              , _("Clone extents from source to dest, if extents match")                  , NULL}     ,
        {"is-clone"                 , 0    , 0         , G_OPTION_ARG_NONE      , &cfg->is_clone                 , _("Test if two files are already clones")                                 , NULL}     ,
        {"btrfs-readonly"           , 'r'  , 0         , G_OPTION_ARG_NONE      , &cfg->btrfs_readonly           , _("(btrfs-clone option) also clone to read-only snapshots (needs root)")  , NULL}     ,
        {"hash"                     , 0    , 0         , G_OPTION_ARG_NONE      , &cfg->hash                     , _("Calculate checksums (`rmlint --hash -a sha1 x` is like  `sha1sum x`")  , NULL}     ,

        /* Callback */
        {"show-man" , 'H' , EMPTY , G_OPTION_ARG_CALLBACK , rm_cfg_show_manpage , _("Show the manpage")            , NULL} ,
        {"version"  , 0   , EMPTY , G_OPTION_ARG_CALLBACK , rm_cfg_show_version , _("Show the version & features") , NULL} ,
        /* Dummy option for --help output only: */
        {"gui"         , 0 , 0 , G_OPTION_ARG_NONE , NULL   , _("If installed, start the optional gui with all following args")                 , NULL} ,

        /* Special case: accumulate leftover args (paths) in &paths */
        {G_OPTION_REMAINING , 0 , 0 , G_OPTION_ARG_FILENAME_ARRAY , &paths , ""   , NULL}   ,
        {NULL               , 0 , 0 , 0                           , NULL   , NULL , NULL}
    };

    const GOptionEntry inversed_option_entries[] = {
        {"no-hidden"                  , 'R' , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->ignore_hidden           , "Ignore hidden files"                 , NULL} ,
        {"with-color"                 , 'w' , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->with_color              , "Be colorful like a unicorn"          , NULL} ,
        {"hardlinked"                 , 'l' , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->find_hardlinked_dupes   , _("Report hardlinks as duplicates")   , NULL} ,
        {"crossdev"                   , 'X' , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->crossdev                , "Cross mountpoints"                   , NULL} ,
        {"less-paranoid"              , 'P' , EMPTY | HIDDEN   , G_OPTION_ARG_CALLBACK , FUNC(less_paranoid)           , "Use less paranoid hashing algorithm" , NULL} ,
        {"see-symlinks"               , '@' , EMPTY | HIDDEN   , G_OPTION_ARG_CALLBACK , FUNC(see_symlinks)            , "Treat symlinks a regular files"      , NULL} ,
        {"unmatched-basename"         , 'B',  HIDDEN           , G_OPTION_ARG_NONE     , &cfg->unmatched_basenames     , "Only find twins with differing names", NULL} ,
        {"no-match-extension"         , 'E' , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->match_with_extension    , "Disable --match-extension"           , NULL} ,
        {"no-match-extension"         , 'E' , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->match_with_extension    , "Disable --match-extension"           , NULL} ,
        {"no-match-without-extension" , 'I' , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->match_without_extension , "Disable --match-without-extension"   , NULL} ,
        {"no-progress"                , 'G' , EMPTY | HIDDEN   , G_OPTION_ARG_CALLBACK , FUNC(no_progress)             , "Disable progressbar"                 , NULL} ,
        {"no-xattr-read"              , 0   , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->read_cksum_from_xattr   , "Disable --xattr-read"                , NULL} ,
        {"no-xattr-write"             , 0   , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->write_cksum_to_xattr    , "Disable --xattr-write"               , NULL} ,
        {"no-partial-hidden"          , 0   , EMPTY | HIDDEN   , G_OPTION_ARG_CALLBACK , FUNC(no_partial_hidden)       , "Invert --partial-hidden"             , NULL} ,
        {NULL                         , 0   , 0                , 0                     , NULL                          , NULL                                  , NULL}
    };

    const GOptionEntry unusual_option_entries[] = {
        {"clamp-low"              , 'q' , HIDDEN           , G_OPTION_ARG_CALLBACK , FUNC(clamp_low)              , "Limit lower reading barrier"                                 , "P"}    ,
        {"clamp-top"              , 'Q' , HIDDEN           , G_OPTION_ARG_CALLBACK , FUNC(clamp_top)              , "Limit upper reading barrier"                                 , "P"}    ,
        {"limit-mem"              , 'u' , HIDDEN           , G_OPTION_ARG_CALLBACK , FUNC(limit_mem)              , "Specify max. memory usage target"                            , "S"}    ,
        {"sweep-size"             , 'u' , HIDDEN           , G_OPTION_ARG_CALLBACK , FUNC(sweep_size)             , "Specify max. bytes per pass when scanning disks"             , "S"}    ,
        {"sweep-files"            , 'u' , HIDDEN           , G_OPTION_ARG_CALLBACK , FUNC(sweep_count)            , "Specify max. file count per pass when scanning disks"        , "S"}    ,
        {"threads"                , 't' , HIDDEN           , G_OPTION_ARG_INT64    , &cfg->threads                , "Specify max. number of hasher threads"                       , "N"}    ,
        {"threads-per-disk"       , 0   , HIDDEN           , G_OPTION_ARG_INT      , &cfg->threads_per_disk       , "Specify number of reader threads per physical disk"          , NULL}   ,
        {"write-unfinished"       , 'U' , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->write_unfinished       , "Output unfinished checksums"                                 , NULL}   ,
        {"xattr-write"            , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->write_cksum_to_xattr   , "Cache checksum in file attributes"                           , NULL}   ,
        {"xattr-read"             , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->read_cksum_from_xattr  , "Read cached checksums from file attributes"                  , NULL}   ,
        {"xattr-clear"            , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->clear_xattr_fields     , "Clear xattrs from all seen files"                            , NULL}   ,
        {"with-fiemap"            , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->build_fiemap           , "Use fiemap(2) to optimize disk access patterns"              , NULL}   ,
        {"without-fiemap"         , 0   , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->build_fiemap           , "Do not use fiemap(2) in order to save memory"                , NULL}   ,
        {"shred-always-wait"      , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->shred_always_wait      , "Always waits for file increment to finish hashing"           , NULL}   ,
        {"fake-pathindex-as-disk" , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->fake_pathindex_as_disk , "Pretends each input path is a separate physical disk"        , NULL}   ,
        {"fake-holdback"          , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->cache_file_structs     , "Hold back all files to the end before outputting."           , NULL}   ,
        {"fake-fiemap"            , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->fake_fiemap            , "Create faked fiemap data for all files"                      , NULL}   ,
        {"fake-abort"             , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->fake_abort             , "Simulate interrupt after 10% shredder progress"              , NULL}   ,
        {"buffered-read"          , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->use_buffered_read      , "Default to buffered reading calls (fread) during reading."   , NULL}   ,
        {"shred-never-wait"       , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->shred_never_wait       , "Never waits for file increment to finish hashing"            , NULL}   ,
        {"no-mount-table"         , 0   , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->list_mounts            , "Do not try to optimize by listing mounted volumes"           , NULL}   ,
        {NULL                     , 0   , HIDDEN           , 0                     , NULL                         , NULL                                                          , NULL}
    };

    /* clang-format on */

    /* Initialize default verbosity */
    rm_cfg_set_verbosity_from_cnt(cfg, cfg->verbosity_count);

    if(!rm_cfg_set_cwd(cfg)) {
        g_set_error(&error, RM_ERROR_QUARK, 0, _("Cannot set current working directory"));
        goto failure;
    }

    if(!rm_cfg_set_cmdline(cfg, argc, argv)) {
        g_set_error(&error, RM_ERROR_QUARK, 0, _("Cannot join commandline"));
        goto failure;
    }

    /* Attempt to find out path to own executable.
     * This is used in the shell script to call the executable
     * for special modes like --btrfs-clone or --equal.
     * We want to make sure the installed version has this
     * */
    cfg->full_argv0_path = rm_cfg_find_own_executable_path(cfg, argv);

    ////////////////////
    // OPTION PARSING //
    ////////////////////

    option_parser = g_option_context_new(
        _("[TARGET_DIR_OR_FILES …] [//] [TAGGED_TARGET_DIR_OR_FILES …] [-]"));
    g_option_context_set_translation_domain(option_parser, RM_GETTEXT_PACKAGE);

    GOptionGroup *main_group =
        g_option_group_new("rmlint", "main", "Most useful main options", cfg, NULL);
    GOptionGroup *inversion_group = g_option_group_new(
        "inversed", "inverted", "Options that enable defaults", cfg, NULL);
    GOptionGroup *unusual_group =
        g_option_group_new("unusual", "unusual", "Unusual options", cfg, NULL);

    g_option_group_add_entries(main_group, main_option_entries);
    g_option_group_add_entries(main_group, inversed_option_entries);
    g_option_group_add_entries(main_group, unusual_option_entries);

    g_option_context_add_group(option_parser, inversion_group);
    g_option_context_add_group(option_parser, unusual_group);
    g_option_context_set_main_group(option_parser, main_group);
    g_option_context_set_summary(option_parser,
                                 _("rmlint finds space waste and other broken things on "
                                   "your filesystem and offers to remove it.\n"
                                   "It is especially good at finding duplicates and "
                                   "offers a big variety of options to handle them."));
    g_option_context_set_description(
        option_parser,
        _("Only the most important options and options that alter the defaults are shown "
          "above.\n"
          "See the manpage (man 1 rmlint or rmlint --show-man) for far more detailed "
          "usage information,\n"
          "or http://rmlint.rtfd.org/en/latest/rmlint.1.html for the online manpage.\n"
          "Complementary tutorials can be found at: http://rmlint.rtfd.org"));

    g_option_group_set_error_hook(main_group, (GOptionErrorFunc)rm_cfg_on_error);

    if(!g_option_context_parse(option_parser, &argc, &argv, &error)) {
        goto failure;
    }

    /* Silent fixes of invalid numeric input */
    cfg->threads = CLAMP(cfg->threads, 1, 128);
    cfg->depth = CLAMP(cfg->depth, 1, PATH_MAX / 2 + 1);

    if(cfg->partial_hidden && !cfg->merge_directories) {
        /* --partial-hidden only makes sense with --merge-directories.
         * If the latter is not specfified, ignore it all together */
        cfg->ignore_hidden = true;
        cfg->partial_hidden = false;
    }

    if(cfg->honour_dir_layout && !cfg->merge_directories) {
        rm_log_warning_line(_(
            "--honour-dir-layout (-j) makes no sense without --merge-directories (-D)"));
    }

    if(cfg->progress_enabled) {
        if(!rm_fmt_has_formatter(cfg->formats, "sh")) {
            rm_fmt_add(cfg->formats, "sh", "rmlint.sh");
        }

        if(!rm_fmt_has_formatter(cfg->formats, "json")) {
            rm_fmt_add(cfg->formats, "json", "rmlint.json");
        }
    }

    if(cfg->hash) {
        rm_fmt_clear(cfg->formats);
        rm_fmt_add(cfg->formats, "hash", "stdout");
    }

    /* Overwrite color if we do not print to a terminal directly */
    if(cfg->with_color) {
        cfg->with_stdout_color = isatty(fileno(stdout));
        cfg->with_stderr_color = isatty(fileno(stderr));
        cfg->with_color = (cfg->with_stdout_color | cfg->with_stderr_color);
    } else {
        cfg->with_stdout_color = cfg->with_stderr_color = 0;
    }

    if(cfg->keep_all_tagged && cfg->keep_all_untagged) {
        error = g_error_new(
            RM_ERROR_QUARK, 0,
            _("can't specify both --keep-all-tagged and --keep-all-untagged"));
    } else if(cfg->skip_start_factor >= cfg->skip_end_factor) {
        error = g_error_new(RM_ERROR_QUARK, 0,
                            _("-q (--clamp-low) should be lower than -Q (--clamp-top)"));
    } else if(!rm_cfg_set_paths(cfg, paths)) {
        error =
            g_error_new(RM_ERROR_QUARK, 0, _("Not all given paths are valid. Aborting"));
    } else if(!rm_cfg_set_outputs(cfg, &error)) {
        /* Something wrong with the outputs */
    } else if(cfg->follow_symlinks && cfg->see_symlinks) {
        rm_log_error("Program error: Cannot do both follow_symlinks and see_symlinks");
        rm_assert_gentle_not_reached();
    }

failure:
    if(error != NULL) {
        rm_cfg_on_error(NULL, NULL, cfg, &error);
    }

    if(cfg->progress_enabled) {
        /* Set verbosity to minimal */
        rm_cfg_set_verbosity_from_cnt(cfg, 1);
    }

    g_option_context_free(option_parser);
    return !(cfg->cmdline_parse_error);
}
