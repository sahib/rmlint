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
 *  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include "config.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <search.h>
#include <sys/time.h>

#include "cmdline.h"
#include "formats.h"
#include "hash-utility.h"
#include "md-scheduler.h"
#include "preprocess.h"
#include "replay.h"
#include "shredder.h"
#include "traverse.h"
#include "treemerge.h"
#include "utilities.h"

/* define paranoia levels */
static const RmDigestType RM_PARANOIA_LEVELS[] = {RM_DIGEST_METRO,
                                                  RM_DIGEST_METRO256,
                                                  RM_DIGEST_HIGHWAY256,
                                                  RM_DEFAULT_DIGEST,
                                                  RM_DIGEST_PARANOID,
                                                  RM_DIGEST_PARANOID};
static const int RM_PARANOIA_NORMAL = 3;  /*  must be index of RM_DEFAULT_DIGEST */
static const int RM_PARANOIA_MAX = 5;

static void rm_cmd_show_version(void) {
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

static void rm_cmd_show_manpage(void) {
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

/*
* Debian and Ubuntu based distributions fuck up setuptools
* by expecting packages to be installed to dist-packages and not site-packages
* like expected by setuptools. This breaks a lot of packages with the reasoning
* to reduce conflicts between system and user packages:
*
*    https://stackoverflow.com/questions/9387928/whats-the-difference-between-dist-packages-and-site-packages
*
* We try to work around this by manually installing dist-packages to the
* sys.path by first calling a small bootstrap script.
*/
static const char RM_PY_BOOTSTRAP[] =
    ""
    "# This is a bootstrap script for the rmlint-gui.                              \n"
    "# See the src/rmlint.c in rmlint's source for more info.                      \n"
    "import sys, os, site                                                          \n"
    "                                                                              \n"
    "# Also default to dist-packages on debian(-based):                            \n"
    "sites = site.getsitepackages()                                                \n"
    "sys.path.extend([d.replace('dist-packages', 'site-packages') for d in sites]) \n"
    "sys.path.extend(sites)                                                        \n"
    "                                                                              \n"
    "# Cleanup self:                                                               \n"
    "try:                                                                          \n"
    "    os.remove(sys.argv[0])                                                    \n"
    "except:                                                                       \n"
    "    print('Note: Could not remove bootstrap script at ', sys.argv[0])         \n"
    "                                                                              \n"
    "# Run shredder by importing the main:                                         \n"
    "try:                                                                          \n"
    "    import shredder                                                           \n"
    "    shredder.run_gui()                                                        \n"
    "except ImportError as err:                                                    \n"
    "    print('Failed to load shredder:', err)                                    \n"
    "    print('This might be due to a corrupted install; try reinstalling.')      \n";

static void rm_cmd_start_gui(int argc, const char **argv) {
    const char *commands[] = {"python3", "python", NULL};
    const char **command = &commands[0];

    GError *error = NULL;
    gchar *bootstrap_path = NULL;
    int bootstrap_fd =
        g_file_open_tmp(".shredder-bootstrap.py.XXXXXX", &bootstrap_path, &error);

    if(bootstrap_fd < 0) {
        rm_log_warning("Could not bootstrap gui: Unable to create tempfile: %s",
                       error->message);
        g_error_free(error);
        return;
    }

    if(write(bootstrap_fd, RM_PY_BOOTSTRAP, sizeof(RM_PY_BOOTSTRAP)) < 0) {
        rm_log_warning_line("Could not bootstrap gui: Unable to write to tempfile: %s",
                            g_strerror(errno));
        return;
    }

    close(bootstrap_fd);

    while(*command) {
        const char *all_argv[512];
        const char **argp = &all_argv[0];
        memset(all_argv, 0, sizeof(all_argv));

        *argp++ = *command;
        *argp++ = bootstrap_path;

        for(size_t i = 0; i < (size_t)argc && i < sizeof(all_argv) / 2; i++) {
            *argp++ = argv[i];
        }

        if(execvp(*command, (char *const *)all_argv) == -1) {
            rm_log_warning("Executed: %s ", *command);
            for(int j = 0; j < (argp - all_argv); j++) {
                rm_log_warning("%s ", all_argv[j]);
            }
            rm_log_warning("\n");
            rm_log_error_line("%s %d", g_strerror(errno), errno == ENOENT);
        } else {
            /* This is not reached anymore when execve suceeded */
            break;
        }

        /* Try next command... */
        command++;
    }
}

static int rm_cmd_maybe_switch_to_gui(int argc, const char **argv) {
    for(int i = 0; i < argc; i++) {
        if(g_strcmp0("--gui", argv[i]) == 0) {
            argv[i] = "shredder";
            rm_cmd_start_gui(argc - i - 1, &argv[i + 1]);

            /* We returned? Something's wrong */
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

static int rm_cmd_maybe_switch_to_hasher(int argc, const char **argv) {
    for(int i = 0; i < argc; i++) {
        if(g_strcmp0("--hash", argv[i]) == 0) {
            argv[i] = argv[0];
            exit(rm_hasher_main(argc - i, &argv[i]));
        }
    }

    return EXIT_SUCCESS;
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

static int rm_cmd_compare_spec_elem(const void *fmt_a, const void *fmt_b) {
    return strcasecmp(((FormatSpec *)fmt_a)->id, ((FormatSpec *)fmt_b)->id);
}

static RmOff rm_cmd_size_string_to_bytes(const char *size_spec, GError **error) {
    if(size_spec == NULL) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Input size is empty"));
        return 0;
    }

    // We have a small issue here:
    // 1) strold can not parse all of a guin64.
    // 2) strtoull can only parse integers.
    //
    // We "solve" this by parsing with strtoull until the dot,
    // and continue with strtold after. If we hit an overflow
    // during multiplication later we warn the user and abort.

    // Copy and strip the string:
    char size_spec_copy[512] = {0};
    memset(size_spec_copy, 0, sizeof(size_spec_copy));
    strncpy(size_spec_copy, size_spec, sizeof(size_spec_copy)-1);
    char *size_spec_stripped = g_strstrip(size_spec_copy);

    if(*size_spec_stripped == 0) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Size is empty"));
        return 0;
    }

    if(*size_spec_stripped == '-') {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Negative sizes are no good idea"));
        return 0;
    }

    // Actual number before the dot.
    RmOff base_size = 1;

    // size_factor to multiply the result with (in case of a suffix like »mb«)
    double size_factor = 1;

    // possible fraction given for a size after a dot.
    double fraction = 0;

    char *size_format = size_spec_stripped;
    for(; *size_format && (*size_format == '.' || g_ascii_isdigit(*size_format)); size_format++);

    // Set to true when either a format suffix or a fraction was encountered.
    bool need_multiply = false;

    if(*size_format != 0) {
        need_multiply = true;
        FormatSpec key = {.id = size_format};
        FormatSpec *found = bsearch(
            &key,
            SIZE_FORMAT_TABLE,
            SIZE_FORMAT_TABLE_N,
            sizeof(FormatSpec),
            rm_cmd_compare_spec_elem
        );

        if(found != NULL) {
            size_factor *= pow(found->base, found->exponent);
        } else {
            g_set_error(error, RM_ERROR_QUARK, 0, _("Given size_format specifier not found"));
            return 0;
        }

        // Set to zero to separate it from the after-dot part.
        *size_format = 0;
    }

    char *dot = strchr(size_spec_stripped, '.');
    if(dot != NULL) {
        need_multiply = true;

        // Parse with strtoull since we only accept numbers after the dot.
        // (not 1e10 or negative number like strtod would support them)
        char *first_error = NULL;
        RmOff fraction_num = strtoull(&dot[1], &first_error, 10);
        if(first_error != NULL && *first_error != 0) {
            g_set_error(error, RM_ERROR_QUARK, 0, _("Failed to parse size fraction"));
            return 0;
        }

        if(fraction_num == ULONG_MAX && errno == ERANGE) {
            g_set_error(error, RM_ERROR_QUARK, 0, _("Fraction is too big for uint64"));
            return 0;
        }

        // Make sure number parsing doesn't see the dot.
        *dot = 0;

        // Convert the integer (e.g. 523) to a fraction (e.g. 0.523).
        fraction = ((double)fraction_num) / pow(10, 1 + ((int)log10(fraction_num)));
    }

    if(*size_spec_stripped != 0) {
        char *first_error = NULL;
        base_size = strtoull(size_spec_stripped, &first_error, 10);
        if(first_error != NULL && *first_error != 0) {
            g_set_error(error, RM_ERROR_QUARK, 0, _("This does not look like a number"));
            return 0;
        }

        if(base_size == ULONG_MAX && errno == ERANGE) {
            g_set_error(error, RM_ERROR_QUARK, 0, _("Size is too big for uint64"));
            return 0;
        }
    }

    RmOff result = base_size;
    if(need_multiply) {
        // Only multiply if we really have to.
        result = (result + fraction) * size_factor;

        // Check if an overflow happened during multiplication.
        if(result < base_size) {
            g_set_error(error, RM_ERROR_QUARK, 0, _("Size factor would overflow size (max. 2**64 allowed)"));
            return 0;
        }
    }

    return result;
}

/* Size spec parsing implemented by qitta (http://github.com/qitta)
 * Thanks and go blame him if this breaks!
 */
static gboolean rm_cmd_size_range_string_to_bytes(const char *range_spec, RmOff *min,
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
        *min = rm_cmd_size_string_to_bytes(split[0], error);

        if(split[1] != NULL && *error == NULL) {
            *max = rm_cmd_size_string_to_bytes(split[1], error);
        }
    }

    g_strfreev(split);

    if(*error == NULL && *max < *min) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Max is smaller than min"));
        return false;
    }

    return true;
}

static gboolean rm_cmd_parse_limit_sizes(_UNUSED const char *option_name,
                                         const gchar *range_spec,
                                         RmSession *session,
                                         GError **error) {
    if(!rm_cmd_size_range_string_to_bytes(range_spec, &session->cfg->minsize,
                                          &session->cfg->maxsize, error)) {
        g_prefix_error(error, _("cannot parse --size: "));
        return false;
    } else {
        session->cfg->limits_specified = true;
        return true;
    }
}

static gboolean rm_cmd_parse_xattr(_UNUSED const char *option_name,
                                   _UNUSED const gchar *_,
                                   RmSession *session,
                                   _UNUSED GError **error) {
    session->cfg->write_cksum_to_xattr = true;
    session->cfg->read_cksum_from_xattr= true;
    session->cfg->clear_xattr_fields = false;
    session->cfg->write_unfinished = true;
    return true;
}

static GLogLevelFlags VERBOSITY_TO_LOG_LEVEL[] = {[0] = G_LOG_LEVEL_CRITICAL,
                                                  [1] = G_LOG_LEVEL_ERROR,
                                                  [2] = G_LOG_LEVEL_WARNING,
                                                  [3] = G_LOG_LEVEL_MESSAGE |
                                                        G_LOG_LEVEL_INFO,
                                                  [4] = G_LOG_LEVEL_DEBUG};
static bool rm_cmd_read_paths_from_stdin(RmSession *session, bool is_prefd,
                                         bool null_separated) {
    char delim = null_separated ? 0 : '\n';

    size_t buf_len = PATH_MAX;
    char *path_buf = malloc(buf_len * sizeof(char));

    bool all_paths_read = true;

    int path_len;

    /* Still read all paths on errors, so the user knows all paths that failed */
    while((path_len = getdelim(&path_buf, &buf_len, delim, stdin)) >= 0) {
        if(path_len > 0) {
            /* replace returned delimiter with null */
            if (path_buf[path_len - 1] == delim) {
                path_buf[path_len - 1] = 0;
            }
            all_paths_read &= rm_cfg_add_path(session->cfg, is_prefd, path_buf);
        }
    }

    free(path_buf);
    return all_paths_read;
}

static bool rm_cmd_parse_output_pair(RmSession *session, const char *pair,
                                     GError **error) {
    g_assert(session);
    g_assert(pair);

    char *separator = strchr(pair, ':');
    char *full_path = NULL;
    char format_name[100];
    memset(format_name, 0, sizeof(format_name));

    if(separator == NULL) {
        /* default to stdout */
        char *extension = strchr(pair, '.');
        if(extension == NULL) {
            full_path = "stdout";
            strncpy(format_name, pair, sizeof(format_name)-1);
        } else {
            extension += 1;
            full_path = (char *)pair;
            strncpy(format_name, extension, sizeof(format_name)-1);
        }
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

static bool rm_cmd_parse_config_pair(RmSession *session, const char *pair,
                                     GError **error) {
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
    if(!rm_fmt_is_valid_key(session->formats, formatter, key)) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Invalid key `%s' for formatter `%s'"),
                    key, formatter);
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

static gboolean rm_cmd_parse_config(_UNUSED const char *option_name,
                                    const char *pair,
                                    RmSession *session,
                                    _UNUSED GError **error) {
    return rm_cmd_parse_config_pair(session, pair, error);
}

static double rm_cmd_parse_clamp_factor(const char *string, GError **error) {
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

static RmOff rm_cmd_parse_clamp_offset(const char *string, GError **error) {
    RmOff offset = rm_cmd_size_string_to_bytes(string, error);

    if(*error != NULL) {
        g_prefix_error(error, _("Unable to parse offset \"%s\": "), string);
        return 0;
    }

    return offset;
}

static void rm_cmd_parse_clamp_option(RmSession *session, const char *string,
                                      bool start_or_end, GError **error) {
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

#define OPTS (gboolean *[])
#define NAMES (const char *[])

static char rm_cmd_find_lint_types_sep(const char *lint_string) {
    if(*lint_string == '+' || *lint_string == '-') {
        lint_string++;
    }

    while(isalpha((unsigned char)*lint_string)) {
        lint_string++;
    }

    return *lint_string;
}

static gboolean rm_cmd_parse_lint_types(_UNUSED const char *option_name,
                                        const char *lint_string,
                                        RmSession *session,
                                        _UNUSED GError **error) {
    RmCfg *cfg = session->cfg;

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
    lint_sep[0] = rm_cmd_find_lint_types_sep(lint_string);
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
                  rm_cmd_find_line_type_func);

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

static bool rm_cmd_timestamp_is_plain(const char *stamp) {
    return strchr(stamp, 'T') ? false : true;
}

static gboolean rm_cmd_parse_timestamp(_UNUSED const char *option_name,
                                       const gchar *string, RmSession *session,
                                       GError **error) {
    gdouble result = 0;
    bool plain = rm_cmd_timestamp_is_plain(string);
    session->cfg->filter_mtime = false;

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
    session->cfg->filter_mtime = true;

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
    session->cfg->min_mtime = result;
    return true;
}

static gboolean rm_cmd_parse_timestamp_file(const char *option_name,
                                            const gchar *timestamp_path,
                                            RmSession *session, GError **error) {
    bool plain = true, success = false;
    FILE *stamp_file = fopen(timestamp_path, "r");

    /* Assume failure */
    session->cfg->filter_mtime = false;

    if(stamp_file) {
        char stamp_buf[1024];
        memset(stamp_buf, 0, sizeof(stamp_buf));

        if(fgets(stamp_buf, sizeof(stamp_buf), stamp_file) != NULL) {
            success = rm_cmd_parse_timestamp(
                option_name,
                g_strstrip(stamp_buf),
                session,
                error
            );

            if(!success) {
                return false;
            }

            plain = rm_cmd_timestamp_is_plain(stamp_buf);
        }

        fclose(stamp_file);
    } else {
        /* Cannot read a stamp file, assume we gonna creae it. */
        plain = false;
        success = true;
        rm_log_info_line(_("No stamp file at `%s`, will create one after this run."), timestamp_path);
    }

    rm_fmt_add(session->formats, "stamp", timestamp_path);
    if(!plain) {
        /* Enable iso8601 timestamp output */
        rm_fmt_set_config_value(session->formats, "stamp", g_strdup("iso8601"),
                                g_strdup("true"));
    }

    return success;
}

static void rm_cmd_set_verbosity_from_cnt(RmCfg *cfg, int verbosity_counter) {
    cfg->verbosity = VERBOSITY_TO_LOG_LEVEL[CLAMP(
        verbosity_counter,
        1,
        (int)(sizeof(VERBOSITY_TO_LOG_LEVEL) / sizeof(GLogLevelFlags)) - 1)];
}

static void rm_cmd_set_paranoia_from_cnt(RmCfg *cfg, int paranoia_counter,
                                         GError **error) {
    /* Handle the paranoia option */
    int index = paranoia_counter + RM_PARANOIA_NORMAL;

    if(index < 0 || index > RM_PARANOIA_MAX) {
        if(error && *error == NULL) {
            g_set_error(error, RM_ERROR_QUARK, 0,
                        _("Only up to -%.*s or down to -%.*s flags allowed"),
                        RM_PARANOIA_MAX - RM_PARANOIA_NORMAL, "ppppp", RM_PARANOIA_NORMAL,
                        "PPPPP");
        }
    } else {
        cfg->checksum_type = RM_PARANOIA_LEVELS[index];
    }
}

static void rm_cmd_on_error(_UNUSED GOptionContext *context, _UNUSED GOptionGroup *group,
                            RmSession *session, GError **error) {
    if(error != NULL) {
        rm_log_error_line("%s.", (*error)->message);
        g_clear_error(error);
        session->cmdline_parse_error = true;
    }
}

static gboolean rm_cmd_parse_algorithm(_UNUSED const char *option_name,
                                       const gchar *value,
                                       RmSession *session,
                                       GError **error) {
    RmCfg *cfg = session->cfg;
    cfg->checksum_type = rm_string_to_digest_type(value);

    if(cfg->checksum_type == RM_DIGEST_UNKNOWN) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Unknown hash algorithm: '%s'"), value);
        return false;
    }
    return true;
}

static gboolean rm_cmd_parse_small_output(_UNUSED const char *option_name,
                                          const gchar *output_pair, RmSession *session,
                                          _UNUSED GError **error) {
    session->output_cnt[0] = MAX(session->output_cnt[0], 0);
    session->output_cnt[0] += rm_cmd_parse_output_pair(session, output_pair, error);
    return true;
}

static gboolean rm_cmd_parse_large_output(_UNUSED const char *option_name,
                                          const gchar *output_pair, RmSession *session,
                                          _UNUSED GError **error) {
    session->output_cnt[1] = MAX(session->output_cnt[1], 0);
    session->output_cnt[1] += rm_cmd_parse_output_pair(session, output_pair, error);
    return true;
}

static gboolean rm_cmd_parse_mem(const gchar *size_spec, GError **error, RmOff *target) {
    RmOff size = rm_cmd_size_string_to_bytes(size_spec, error);

    if(*error != NULL) {
        g_prefix_error(error, _("Invalid size description \"%s\": "), size_spec);
        return false;
    } else {
        *target = size;
        return true;
    }
}

static gboolean rm_cmd_parse_limit_mem(_UNUSED const char *option_name,
                                       const gchar *size_spec, RmSession *session,
                                       GError **error) {
    return (rm_cmd_parse_mem(size_spec, error, &session->cfg->total_mem));
}

static gboolean rm_cmd_parse_read_buf_len(_UNUSED const char *option_name,
                                          const gchar *size_spec, RmSession *session,
                                          GError **error) {
    return (rm_cmd_parse_mem(size_spec, error, &session->cfg->read_buf_len));
}

static gboolean rm_cmd_parse_sweep_size(_UNUSED const char *option_name,
                                        const gchar *size_spec, RmSession *session,
                                        GError **error) {
    return (rm_cmd_parse_mem(size_spec, error, &session->cfg->sweep_size));
}

static gboolean rm_cmd_parse_sweep_count(_UNUSED const char *option_name,
                                         const gchar *size_spec, RmSession *session,
                                         GError **error) {
    return (rm_cmd_parse_mem(size_spec, error, &session->cfg->sweep_count));
}

static gboolean rm_cmd_parse_clamp_low(_UNUSED const char *option_name, const gchar *spec,
                                       RmSession *session, _UNUSED GError **error) {
    rm_cmd_parse_clamp_option(session, spec, true, error);
    return (error && *error == NULL);
}

static gboolean rm_cmd_parse_clamp_top(_UNUSED const char *option_name, const gchar *spec,
                                       RmSession *session, _UNUSED GError **error) {
    rm_cmd_parse_clamp_option(session, spec, false, error);
    return (error && *error == NULL);
}

static gboolean rm_cmd_parse_progress(_UNUSED const char *option_name,
                                      _UNUSED const gchar *value, RmSession *session,
                                      _UNUSED GError **error) {
    rm_fmt_remove_by_name(session->formats, "pretty");

    rm_fmt_add(session->formats, "progressbar", "stdout");
    rm_fmt_add(session->formats, "summary", "stdout");

    session->cfg->progress_enabled = true;

    return true;
}

static void rm_cmd_set_default_outputs(RmSession *session) {
    rm_fmt_add(session->formats, "pretty", "stdout");
    rm_fmt_add(session->formats, "summary", "stdout");

    if(session->cfg->replay) {
        rm_fmt_add(session->formats, "sh", "rmlint.replay.sh");
        rm_fmt_add(session->formats, "json", "rmlint.replay.json");
    } else {
        rm_fmt_add(session->formats, "sh", "rmlint.sh");
        rm_fmt_add(session->formats, "json", "rmlint.json");
    }
}

static gboolean rm_cmd_parse_no_progress(_UNUSED const char *option_name,
                                         _UNUSED const gchar *value, RmSession *session,
                                         _UNUSED GError **error) {
    rm_fmt_clear(session->formats);
    rm_cmd_set_default_outputs(session);
    rm_cmd_set_verbosity_from_cnt(session->cfg, session->verbosity_count);
    return true;
}

static gboolean rm_cmd_parse_loud(_UNUSED const char *option_name,
                                  _UNUSED const gchar *count, RmSession *session,
                                  _UNUSED GError **error) {
    rm_cmd_set_verbosity_from_cnt(session->cfg, ++session->verbosity_count);
    return true;
}

static gboolean rm_cmd_parse_quiet(_UNUSED const char *option_name,
                                   _UNUSED const gchar *count, RmSession *session,
                                   _UNUSED GError **error) {
    rm_cmd_set_verbosity_from_cnt(session->cfg, --session->verbosity_count);
    return true;
}

static gboolean rm_cmd_parse_paranoid(_UNUSED const char *option_name,
                                      _UNUSED const gchar *count, RmSession *session,
                                      _UNUSED GError **error) {
    rm_cmd_set_paranoia_from_cnt(session->cfg, ++session->paranoia_count, error);
    return true;
}

static gboolean rm_cmd_parse_less_paranoid(_UNUSED const char *option_name,
                                           _UNUSED const gchar *count, RmSession *session,
                                           _UNUSED GError **error) {
    rm_cmd_set_paranoia_from_cnt(session->cfg, --session->paranoia_count, error);
    return true;
}

static gboolean rm_cmd_parse_partial_hidden(_UNUSED const char *option_name,
                                            _UNUSED const gchar *count,
                                            RmSession *session, _UNUSED GError **error) {
    RmCfg *cfg = session->cfg;
    cfg->ignore_hidden = false;
    cfg->partial_hidden = true;

    return true;
}

static gboolean rm_cmd_parse_see_symlinks(_UNUSED const char *option_name,
                                          _UNUSED const gchar *count, RmSession *session,
                                          _UNUSED GError **error) {
    RmCfg *cfg = session->cfg;
    cfg->see_symlinks = true;
    cfg->follow_symlinks = false;

    return true;
}

static gboolean rm_cmd_parse_follow_symlinks(_UNUSED const char *option_name,
                                             _UNUSED const gchar *count,
                                             RmSession *session, _UNUSED GError **error) {
    RmCfg *cfg = session->cfg;
    cfg->see_symlinks = false;
    cfg->follow_symlinks = true;

    return true;
}

static gboolean rm_cmd_parse_no_partial_hidden(_UNUSED const char *option_name,
                                               _UNUSED const gchar *count,
                                               RmSession *session,
                                               _UNUSED GError **error) {
    RmCfg *cfg = session->cfg;
    cfg->ignore_hidden = true;
    cfg->partial_hidden = false;

    return true;
}

static gboolean rm_cmd_parse_merge_directories(_UNUSED const char *option_name,
                                               _UNUSED const gchar *_,
                                               RmSession *session,
                                               _UNUSED GError **error) {
    RmCfg *cfg = session->cfg;
    cfg->merge_directories = true;

    /* Pull in some options for convenience,
     * duplicate dir detection works better with them.
     *
     * They may be disabled explicitly though.
     */
    cfg->follow_symlinks = false;
    cfg->see_symlinks = true;
    rm_cmd_parse_partial_hidden(NULL, NULL, session, error);

    /* Keep RmFiles after shredder. */
    cfg->cache_file_structs = true;

    return true;
}

static gboolean rm_cmd_parse_hidden(_UNUSED const char *option_name,
                                               _UNUSED const gchar *_,
                                               RmSession *session,
                                               _UNUSED GError **error) {
    session->cfg->ignore_hidden = false;
    session->cfg->partial_hidden = false;
    return true;
}

static gboolean rm_cmd_parse_no_hidden(_UNUSED const char *option_name,
                                               _UNUSED const gchar *_,
                                               RmSession *session,
                                               _UNUSED GError **error) {
    session->cfg->ignore_hidden = true;
    session->cfg->partial_hidden = false;
    return true;
}

static gboolean rm_cmd_parse_honour_dir_layout(_UNUSED const char *option_name,
                                               _UNUSED const gchar *_,
                                               RmSession *session,
                                               _UNUSED GError **error) {
    session->cfg->honour_dir_layout = true;
    return true;
}

static gboolean rm_cmd_parse_permissions(_UNUSED const char *option_name,
                                         const gchar *perms, RmSession *session,
                                         GError **error) {
    RmCfg *cfg = session->cfg;

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

static gboolean rm_cmd_check_lettervec(const char *option_name, const char *criteria,
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

static gboolean rm_cmd_parse_sortby(_UNUSED const char *option_name,
                                    const gchar *criteria, RmSession *session,
                                    GError **error) {
    RmCfg *cfg = session->cfg;
    if(!rm_cmd_check_lettervec(option_name, criteria, "moanspMOANSP", error)) {
        return false;
    }

    /* Remember the criteria string */
    strncpy(cfg->rank_criteria, criteria, sizeof(cfg->rank_criteria)-1);

    /* ranking the files depends on caching them to the end of the program */
    cfg->cache_file_structs = true;

    return true;
}

static gboolean rm_cmd_parse_rankby(_UNUSED const char *option_name,
                                    const gchar *criteria, RmSession *session,
                                    GError **error) {
    RmCfg *cfg = session->cfg;

    g_free(cfg->sort_criteria);

    cfg->sort_criteria = rm_pp_compile_patterns(session, criteria, error);

    if(error && *error != NULL) {
        return false;
    }

    if(!rm_cmd_check_lettervec(option_name, cfg->sort_criteria, "dlamprxhoDLAMPRXHO",
                               error)) {
        return false;
    }

    return true;
}

static gboolean rm_cmd_parse_replay(_UNUSED const char *option_name,
                                    const gchar *json_path, RmSession *session,
                                    _UNUSED GError **error) {
    session->cfg->replay = true;
    session->cfg->cache_file_structs = true;
    rm_cfg_add_path(session->cfg, false, json_path);
    return true;
}

static gboolean rm_cmd_parse_equal(_UNUSED const char *option_name,
                                   _UNUSED const gchar *x, RmSession *session,
                                   _UNUSED GError **error) {
    rm_cmd_parse_merge_directories(NULL, NULL, session, error);
    rm_cmd_parse_lint_types(NULL, "df,dd", session, error);
    session->cfg->run_equal_mode = true;

    /* See issue #233; partial hidden needs to be disabled */
    session->cfg->partial_hidden = false;
    session->cfg->ignore_hidden = false;

    /* See issue #234 fore more discussion on this */
    session->cfg->limits_specified = true;
    session->cfg->minsize = 0;

    rm_fmt_clear(session->formats);
    rm_fmt_add(session->formats, "_equal", "stdout");
    return true;
}

static gboolean rm_cmd_parse_btrfs_clone(_UNUSED const char *option_name,
                                   _UNUSED const gchar *x, RmSession *session,
                                   _UNUSED GError **error) {
    rm_log_warning_line("option --btrfs-clone is deprecated, use --dedupe");
    session->cfg->dedupe = true;
    return true;
}

static gboolean rm_cmd_parse_btrfs_readonly(_UNUSED const char *option_name,
                                   _UNUSED const gchar *x, RmSession *session,
                                   _UNUSED GError **error) {
    session->cfg->dedupe_readonly = true;
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
    bool is_prefd = false;
    bool all_paths_valid = true;
    bool stdin_paths_preferred = false;

    RmCfg *cfg = session->cfg;

    /* Check the directory to be valid */
    for(int i = 0; paths && paths[i]; ++i) {
        if(strcmp(paths[i], "-") == 0) {
            cfg->read_stdin = true;
            /* remember whether to treat stdin paths as preferred paths */
            stdin_paths_preferred = is_prefd;
        } else if(strcmp(paths[i], "//") == 0) {
            /* the '//' separator separates non-preferred paths from preferred */
            is_prefd = !is_prefd;
        } else {
            all_paths_valid &= rm_cfg_add_path(cfg, is_prefd, paths[i]);
        }
    }

    g_strfreev(paths);

    if(cfg->read_stdin || cfg->read_stdin0) {
        /* option '-' means read paths from stdin */
        all_paths_valid &=
            rm_cmd_read_paths_from_stdin(session, stdin_paths_preferred, cfg->read_stdin0);
    }

    if(g_slist_length(cfg->paths) == 0 && all_paths_valid) {
        /* Still no path set? - use `pwd` */
        rm_cfg_add_path(session->cfg, is_prefd, cfg->iwd);
    }

    /* Only return success if everything is fine. */
    return all_paths_valid;
}

static bool rm_cmd_set_outputs(RmSession *session, GError **error) {
    if(session->output_cnt[0] >= 0 && session->output_cnt[1] >= 0) {
        g_set_error(error, RM_ERROR_QUARK, 0,
                    _("Specifiyng both -o and -O is not allowed"));
        return false;
    } else if(session->output_cnt[0] < 0 && session->cfg->progress_enabled == false) {
        rm_cmd_set_default_outputs(session);
    }

    return true;
}

static char * rm_cmd_find_own_executable_path(RmSession *session, char **argv) {
    RmCfg *cfg = session->cfg;
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

/* Parse the commandline and set arguments in 'settings' (glob. var accordingly) */
bool rm_cmd_parse_args(int argc, char **argv, RmSession *session) {
    RmCfg *cfg = session->cfg;

    /* Handle --gui before all other processing,
     * since we need to pass other args to the python interpreter.
     * This is not possible with GOption alone.
     */
    if(rm_cmd_maybe_switch_to_gui(argc, (const char **)argv) == EXIT_FAILURE) {
        rm_log_error_line(_("Could not start graphical user interface."));
        return false;
    }

    if(rm_cmd_maybe_switch_to_hasher(argc, (const char **)argv) == EXIT_FAILURE) {
        return false;
    }

    /* List of paths we got passed (or NULL) */
    char **paths = NULL;

    /* General error variable */
    GError *error = NULL;
    GOptionContext *option_parser = NULL;

#define FUNC(name) ((GOptionArgFunc)rm_cmd_parse_##name)
    const int DISABLE = G_OPTION_FLAG_REVERSE, EMPTY = G_OPTION_FLAG_NO_ARG,
              HIDDEN = G_OPTION_FLAG_HIDDEN, OPTIONAL = G_OPTION_FLAG_OPTIONAL_ARG;

    /* Free short options: AJ
     * (beside special characters and numbers)
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
        {"xattr"            , 'C' , EMPTY    , G_OPTION_ARG_CALLBACK , FUNC(xattr)          , _("Enable xattr based caching")           , ""}                    ,

        /* Non-trivial switches */
        {"progress" , 'g' , EMPTY , G_OPTION_ARG_CALLBACK , FUNC(progress) , _("Enable progressbar")                   , NULL} ,
        {"loud"     , 'v' , EMPTY , G_OPTION_ARG_CALLBACK , FUNC(loud)     , _("Be more verbose (-vvv for much more)") , NULL} ,
        {"quiet"    , 'V' , EMPTY , G_OPTION_ARG_CALLBACK , FUNC(quiet)    , _("Be less verbose (-VVV for much less)") , NULL} ,
        {"replay"   , 'Y' , 0     , G_OPTION_ARG_CALLBACK , FUNC(replay)   , _("Re-output a json file")                , "path/to/rmlint.json"} ,
        {"equal"    ,  0 ,  EMPTY , G_OPTION_ARG_CALLBACK , FUNC(equal)    , _("Test for equality of PATHS")           , "PATHS"}           ,

        /* Trivial boolean options */
        {"no-with-color"            , 'W'  , DISABLE   , G_OPTION_ARG_NONE      , &cfg->with_color               , _("Be not that colorful")                                                 , NULL}     ,
        {"hidden"                   , 'r'  , EMPTY     , G_OPTION_ARG_CALLBACK  , FUNC(hidden)                   , _("Find hidden files")                                                    , NULL}     ,
        {"followlinks"              , 'f'  , EMPTY     , G_OPTION_ARG_CALLBACK  , FUNC(follow_symlinks)          , _("Follow symlinks")                                                      , NULL}     ,
        {"no-followlinks"           , 'F'  , DISABLE   , G_OPTION_ARG_NONE      , &cfg->follow_symlinks          , _("Ignore symlinks")                                                      , NULL}     ,
        {"paranoid"                 , 'p'  , EMPTY     , G_OPTION_ARG_CALLBACK  , FUNC(paranoid)                 , _("Use more paranoid hashing")                                            , NULL}     ,
        {"no-crossdev"              , 'x'  , DISABLE   , G_OPTION_ARG_NONE      , &cfg->crossdev                 , _("Do not cross mountpoints")                                             , NULL}     ,
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
        {"keep-hardlinked"          , 0    , 0         , G_OPTION_ARG_NONE      , &cfg->keep_hardlinked_dupes    , _("Keep hardlink that are linked to any original")                        , NULL}     ,
        {"partial-hidden"           , 0    , EMPTY     , G_OPTION_ARG_CALLBACK  , FUNC(partial_hidden)           , _("Find hidden files in duplicate folders only")                          , NULL}     ,
        {"mtime-window"             , 'Z'  , 0         , G_OPTION_ARG_DOUBLE    , &cfg->mtime_window             , _("Consider duplicates only equal when mtime differs at max. T seconds")  , "T"}      ,
        {"stdin0"                   , '0'  , 0         , G_OPTION_ARG_NONE      , &cfg->read_stdin0              , _("Read null-separated file list from stdin")                             , NULL}     ,
        {"no-backup"                , 0    , 0         , G_OPTION_ARG_NONE      , &cfg->no_backup                , _("Do not create backups of previous result files")                       , NULL}     ,

        /* COW filesystem deduplication support */
        {"dedupe"                   , 0    , 0         , G_OPTION_ARG_NONE      , &cfg->dedupe                   , _("Dedupe matching extents from source to dest (if filesystem supports)") , NULL}     ,
        {"dedupe-xattr"             , 0    , 0         , G_OPTION_ARG_NONE      , &cfg->dedupe_check_xattr       , _("Check extended attributes to see if the file is already deduplicated") , NULL}     ,
        {"dedupe-readonly"          , 0    , 0         , G_OPTION_ARG_NONE      , &cfg->dedupe_readonly          , _("(--dedupe option) even dedupe read-only snapshots (needs root)")       , NULL}     ,
        {"is-reflink"               , 0    , 0         , G_OPTION_ARG_NONE      , &cfg->is_reflink               , _("Test if two files are reflinks (share same data extents)")             , NULL}     ,

        /* Callback */
        {"show-man" , 'H' , EMPTY , G_OPTION_ARG_CALLBACK , rm_cmd_show_manpage , _("Show the manpage")            , NULL} ,
        {"version"  , 0   , EMPTY , G_OPTION_ARG_CALLBACK , rm_cmd_show_version , _("Show the version & features") , NULL} ,
        /* Dummy option for --help output only: */
        {"gui"         , 0 , 0 , G_OPTION_ARG_NONE , NULL   , _("If installed, start the optional gui with all following args")                 , NULL},
        {"hash"        , 0 , 0 , G_OPTION_ARG_NONE , NULL   , _("Work like sha1sum for all supported hash algorithms (see also --hash --help)") , NULL},

        /* Special case: accumulate leftover args (paths) in &paths */
        {G_OPTION_REMAINING , 0 , 0 , G_OPTION_ARG_FILENAME_ARRAY , &paths , ""   , NULL}   ,
        {NULL               , 0 , 0 , 0                           , NULL   , NULL , NULL}
    };

    const GOptionEntry inversed_option_entries[] = {
        {"no-hidden"                  , 'R' , EMPTY | HIDDEN   , G_OPTION_ARG_CALLBACK , FUNC(no_hidden)               , "Ignore hidden files"                 , NULL} ,
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
        {"read-buffer-len"        , 0   , HIDDEN           , G_OPTION_ARG_CALLBACK , FUNC(read_buf_len)           , "Specify read buffer length in bytes"                         , "S"}    ,
        {"sweep-size"             , 0   , HIDDEN           , G_OPTION_ARG_CALLBACK , FUNC(sweep_size)             , "Specify max. bytes per pass when scanning disks"             , "S"}    ,
        {"sweep-files"            , 0   , HIDDEN           , G_OPTION_ARG_CALLBACK , FUNC(sweep_count)            , "Specify max. file count per pass when scanning disks"        , "S"}    ,
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
        {"no-sse"                 , 0   , HIDDEN           , G_OPTION_ARG_NONE     , &cfg->no_sse                 , "Don't use SSE accelerations"                                 , NULL}   ,
        {"no-mount-table"         , 0   , DISABLE | HIDDEN , G_OPTION_ARG_NONE     , &cfg->list_mounts            , "Do not try to optimize by listing mounted volumes"           , NULL}   ,
        {NULL                     , 0   , HIDDEN           , 0                     , NULL                         , NULL                                                          , NULL}
    };

    const GOptionEntry deprecated_option_entries[] = {
        {"btrfs-clone"              , 0    , EMPTY | HIDDEN      , G_OPTION_ARG_CALLBACK      , FUNC(btrfs_clone)         , "Deprecated, use --dedupe instead"                  , NULL},
        {"btrfs-readonly"           , 0    , EMPTY | HIDDEN      , G_OPTION_ARG_CALLBACK      , FUNC(btrfs_readonly)      , "Deprecated, use --dedupe-readonly instead"         , NULL},
        {NULL                       , 0    , HIDDEN              , 0                          , NULL                      , NULL                                                , NULL}
    };

    /* clang-format on */

    /* Initialize default verbosity */
    rm_cmd_set_verbosity_from_cnt(cfg, session->verbosity_count);

    if(!rm_cmd_set_cwd(cfg)) {
        g_set_error(&error, RM_ERROR_QUARK, 0, _("Cannot set current working directory"));
        goto cleanup;
    }

    if(!rm_cmd_set_cmdline(cfg, argc, argv)) {
        g_set_error(&error, RM_ERROR_QUARK, 0, _("Cannot join commandline"));
        goto cleanup;
    }

    /* Attempt to find out path to own executable.
     * This is used in the shell script to call the executable
     * for special modes like --dedupe or --equal.
     * We want to make sure the installed version has this
     * */
    cfg->full_argv0_path = rm_cmd_find_own_executable_path(session, argv);

    ////////////////////
    // OPTION PARSING //
    ////////////////////

    /* TODO: move subcommands to separate option parser
     * e.g.
     * Usage:
     * rmlint [options] <paths>...
     * rmlint --subcommand [options]
     * Subcommands (must be first arg):
     *      --dedupe      Dedupe matching extents from source to dest (if filesystem supports)
     *      --is-reflink  Test if two files are reflinks
     *      --gui         Launch rmlint gui
     * For help on subcommands use rmlint --<subcommand> --help
     *
     */
    option_parser = g_option_context_new(
        _("[TARGET_DIR_OR_FILES …] [//] [TAGGED_TARGET_DIR_OR_FILES …] [-]"));
    g_option_context_set_translation_domain(option_parser, RM_GETTEXT_PACKAGE);

    GOptionGroup *main_group =
        g_option_group_new("rmlint", "main", "Most useful main options", session, NULL);
    GOptionGroup *inversion_group = g_option_group_new(
        "inversed", "inverted", "Options that enable defaults", session, NULL);
    GOptionGroup *unusual_group =
        g_option_group_new("unusual", "unusual", "Unusual options", session, NULL);
    GOptionGroup *deprecated_group =
        g_option_group_new("deprecated", "deprecated", "Deprecated options", session, NULL);

    g_option_group_add_entries(main_group, main_option_entries);
    g_option_group_add_entries(main_group, inversed_option_entries);
    g_option_group_add_entries(main_group, unusual_option_entries);
    g_option_group_add_entries(deprecated_group, deprecated_option_entries);

    g_option_context_add_group(option_parser, inversion_group);
    g_option_context_add_group(option_parser, unusual_group);
    g_option_context_add_group(option_parser, deprecated_group);
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

    g_option_group_set_error_hook(main_group, (GOptionErrorFunc)rm_cmd_on_error);

    if(!g_option_context_parse(option_parser, &argc, &argv, &error)) {
        goto cleanup;
    }

    if(!rm_cmd_set_paths(session, paths)) {
        error = g_error_new(RM_ERROR_QUARK, 0, _("Not all given paths are valid. Aborting"));
        goto cleanup;
    }

    if(cfg->replay && (cfg->dedupe || cfg->is_reflink)) {
        error = g_error_new(
            RM_ERROR_QUARK, 0,
            _("--replay (-Y) is incompatible with --dedupe or --is-reflink")
        ); goto cleanup;
    }

    if(cfg->dedupe) {
        /* dedupe session; regular rmlint configs are ignored */
        goto cleanup;
    }

    /* Silent fixes of invalid numeric input */
    cfg->threads = CLAMP(cfg->threads, 1, 128);
    cfg->depth = CLAMP(cfg->depth, 1, PATH_MAX / 2 + 1);

    if(cfg->partial_hidden && !cfg->merge_directories) {
        /* --partial-hidden only makes sense with --merge-directories.
         * If the latter is not specfified, ignore it all together
         * and act as if --no-hidden was specified. */
        cfg->ignore_hidden = true;
        cfg->partial_hidden = false;
    }

    if(cfg->honour_dir_layout && !cfg->merge_directories) {
        rm_log_warning_line(_("--honour-dir-layout (-j) makes no sense without --merge-directories (-D)"));
        rm_log_warning_line(_("Note that not having duplicate directories enabled as lint type (e.g via -T df)"));
        rm_log_warning_line(_("will also disable --merge-directories and trigger this warning."));
    }

    if(cfg->progress_enabled) {
        if(!rm_fmt_has_formatter(session->formats, "sh")) {
            rm_fmt_add(session->formats, "sh", "rmlint.sh");
        }

        if(!rm_fmt_has_formatter(session->formats, "json")) {
            rm_fmt_add(session->formats, "json", "rmlint.json");
        }
    }

    /* Overwrite color if we do not print to a terminal directly */
    if(cfg->with_color) {
        cfg->with_stdout_color = isatty(fileno(stdout));
        cfg->with_stderr_color = isatty(fileno(stderr));
        cfg->with_color = (cfg->with_stdout_color | cfg->with_stderr_color);
    } else {
        cfg->with_stdout_color = cfg->with_stderr_color = 0;
    }

    g_assert(!(cfg->follow_symlinks && cfg->see_symlinks));

    if(cfg->keep_all_tagged && cfg->keep_all_untagged) {
        error = g_error_new(
            RM_ERROR_QUARK, 0,
            _("can't specify both --keep-all-tagged and --keep-all-untagged"));
    } else if(cfg->skip_start_factor >= cfg->skip_end_factor) {
        error = g_error_new(RM_ERROR_QUARK, 0,
                            _("-q (--clamp-low) should be lower than -Q (--clamp-top)"));
    } else if(!rm_cmd_set_outputs(session, &error)) {
        /* Something wrong with the outputs */
    } else if(cfg->keep_all_tagged && cfg->must_match_untagged) {
        error = \
            g_error_new(
                RM_ERROR_QUARK, 0,
                _(
                    "-k and -M should not be specified at the same time " \
                    "(see also: https://github.com/sahib/rmlint/issues/244)"
                )
        );
    } else if(cfg->keep_all_untagged && cfg->must_match_tagged) {
        error = \
            g_error_new(
                RM_ERROR_QUARK, 0,
                _(
                    "-K and -m should not be specified at the same time " \
                    "(see also: https://github.com/sahib/rmlint/issues/244)"
                )
        );
    }

#if HAVE_BUILTIN_CPU_SUPPORTS && HAVE_MM_CRC32_U64
    rm_digest_enable_sse(!cfg->no_sse && __builtin_cpu_supports("sse4.2"));
#endif

cleanup:
    if(error != NULL) {
        rm_cmd_on_error(NULL, NULL, session, &error);
    }

    if(cfg->progress_enabled) {
        /* Set verbosity to minimal */
        rm_cmd_set_verbosity_from_cnt(session->cfg, 1);
    }

    g_option_context_free(option_parser);
    return !(session->cmdline_parse_error);
}

static int rm_cmd_replay_main(RmSession *session) {
    /* User chose to replay some json files. */
    RmParrotCage cage;
    rm_parrot_cage_open(&cage, session);

    bool one_valid_json = false;
    RmCfg *cfg = session->cfg;

    for(GSList *iter = cfg->json_paths; iter; iter = iter->next) {
        RmPath *jsonpath = iter->data;

        if(!rm_parrot_cage_load(&cage, jsonpath->path, jsonpath->is_prefd)) {
            rm_log_warning_line("Loading %s failed.", jsonpath->path);
        } else {
            one_valid_json = true;
        }
    }

    if(!one_valid_json) {
        rm_log_error_line(_("No valid .json files given, aborting."));
        return EXIT_FAILURE;
    }

    rm_parrot_cage_close(&cage);
    rm_fmt_flush(session->formats);
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PRE_SHUTDOWN);
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_SUMMARY);

    return EXIT_SUCCESS;
}

int rm_cmd_main(RmSession *session) {
    int exit_state = EXIT_SUCCESS;
    RmCfg *cfg = session->cfg;

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_INIT);

    if(cfg->replay) {
        return rm_cmd_replay_main(session);
    }

    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_TRAVERSE);

    if(cfg->list_mounts) {
        session->mounts = rm_mounts_table_new(cfg->fake_fiemap);
    }

    if(session->mounts == NULL) {
        rm_log_debug_line("No mount table created.");
    }

    session->mds = rm_mds_new(cfg->threads, session->mounts, cfg->fake_pathindex_as_disk);

    rm_traverse_tree(session);

    rm_log_debug_line("List build finished at %.3f with %d files",
                      g_timer_elapsed(session->timer, NULL), session->total_files);

    if(cfg->merge_directories) {
        g_assert(cfg->cache_file_structs);

        /* Currently we cannot use -D and the cloning on btrfs, since this assumes the same layout
         * on two dupicate directories which is likely not a valid assumption.
         * Emit a warning if the raw -D is used in conjunction with that.
         * */
        const char *handler_key = rm_fmt_get_config_value(session->formats, "sh", "handler");
        const char *clone_key = rm_fmt_get_config_value(session->formats, "sh", "clone");
        if(
            cfg->honour_dir_layout == false && (
                (handler_key != NULL && strstr(handler_key, "clone") != NULL) ||
                clone_key != NULL
            )
        ) {
            rm_log_error_line(_("Using -D together with -c sh:clone is currently not possible. Sorry."));
            rm_log_error_line(_("Either do not use -D, or attempt to run again with -Dj."));
            return EXIT_FAILURE;
        }

        RmTreeMerger *t = session->dir_merger = rm_tm_new(session);
        if(!t) {
            rm_log_error_line(_("Failed to complete setup for merging directories"));
            return EXIT_FAILURE;
        }
    }

    if(session->total_files < 2 && session->cfg->run_equal_mode) {
        rm_log_warning_line(_("Not enough files for --equal (need at least two to compare)"));
        return EXIT_FAILURE;
    }

    /* some optimisations for rmlint --equal */
    if(cfg->run_equal_mode && session->total_files == 2) {
        /* check if the two files are hardlinks or reflinks or some such */
        g_assert(cfg->paths);
        RmPath *a = cfg->paths->data;
        g_assert(cfg->paths->next);
        RmPath *b = cfg->paths->next->data;
        switch(rm_util_link_type(a->path, b->path)) {
        case RM_LINK_HARDLINK:
        case RM_LINK_REFLINK:
        case RM_LINK_PATH_DOUBLE:
        case RM_LINK_SAME_FILE:
            session->equal_exit_code = EXIT_SUCCESS;
            cfg->find_duplicates = FALSE;
            cfg->merge_directories = FALSE;
            rm_log_debug_line("got match via rm_offsets_match");
            break;
        default:
            break;
        }
    }

    if(session->total_files >= 1) {

        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PREPROCESS);
        rm_preprocess(session);

        if(cfg->find_duplicates || cfg->merge_directories) {
            rm_shred_run(session);

            rm_log_debug_line("Dupe search finished at time %.3f",
                              g_timer_elapsed(session->timer, NULL));
        } else {
            /* Clear leftovers */
            rm_file_tables_clear(session);
        }
    }

    if(cfg->merge_directories) {
        rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_MERGE);
        rm_tm_finish(session->dir_merger);
    }

    rm_fmt_flush(session->formats);
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_PRE_SHUTDOWN);
    rm_fmt_set_state(session->formats, RM_PROGRESS_STATE_SUMMARY);

    if(session->shred_bytes_remaining != 0) {
        rm_log_error_line("BUG: Number of remaining bytes is %" LLU
                          " (not 0). Please report this.",
                          session->shred_bytes_remaining);
        exit_state = EXIT_FAILURE;
    }

    if(session->shred_files_remaining != 0) {
        rm_log_error_line("BUG: Number of remaining files is %" LLU
                          " (not 0). Please report this.",
                          session->shred_files_remaining);
        exit_state = EXIT_FAILURE;
    }

    if(exit_state == EXIT_SUCCESS && cfg->run_equal_mode) {
        return session->equal_exit_code;
    }

    if(exit_state == EXIT_SUCCESS && rm_session_was_aborted()) {
        exit_state = EXIT_FAILURE;
    }

    return exit_state;
}
