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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include "logger.h"

#include <stdio.h>
#include <string.h>

static gboolean with_stderr_color = TRUE;

static GLogLevelFlags VERBOSITY_TO_LOG_LEVEL[] = {
    [0] = G_LOG_LEVEL_ERROR,
    [1] = G_LOG_LEVEL_CRITICAL,
    [2] = G_LOG_LEVEL_WARNING,
    [3] = G_LOG_LEVEL_INFO,
    [4] = G_LOG_LEVEL_DEBUG};

static gint verbosity = 2;
static GLogLevelFlags min_log_level = G_LOG_LEVEL_WARNING;

static char *remove_color_escapes(char *message) {
    char *dst = message;
    for(char *src = message; src && *src; src++) {
        if(*src == '\x1b') {
            src = strchr(src, 'm');
        } else {
            *dst++ = *src;
        }
    }

    if(dst) {
        *dst = 0;
    }
    return message;
}

void rm_logger_callback(_UNUSED const gchar *log_domain,
                        GLogLevelFlags log_level,
                        const gchar *message,
                        _UNUSED gpointer user_data) {
    if(min_log_level >= log_level) {
        if(!with_stderr_color) {
            message = remove_color_escapes((char *)message);
        }
        fputs(message, stderr);
    }
}

void rm_logger_set_verbosity(const gint new_verbosity) {
    verbosity = new_verbosity;
    min_log_level = VERBOSITY_TO_LOG_LEVEL[CLAMP(
        verbosity,
        0,
        (int)(sizeof(VERBOSITY_TO_LOG_LEVEL) / sizeof(GLogLevelFlags)) - 1)];
}

void rm_logger_set_pretty(const gboolean is_pretty) {
    with_stderr_color = is_pretty;
}

void rm_logger_incr_verbosity_by(const gint incr) {
    verbosity += incr;
    rm_logger_set_verbosity(verbosity);
}
