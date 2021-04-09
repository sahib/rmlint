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

#ifndef RM_LOGGER_H
#define RM_LOGGER_H


#include "config.h"

/**
 * @file logger.h
 * @brief High level API for debug / error logging to STDERR
 *
 **/

#define rm_log_debug(...) g_log("rmlint", G_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define rm_log_info(...) g_log("rmlint", G_LOG_LEVEL_INFO, __VA_ARGS__)
#define rm_log_warning(...) g_log("rmlint", G_LOG_LEVEL_WARNING, __VA_ARGS__)
#define rm_log_error(...) g_log("rmlint", G_LOG_LEVEL_CRITICAL, __VA_ARGS__)

#define rm_log_perror(message)                                                \
    if(errno) {                                                               \
        rm_log_error_line(                                                    \
            "%s:%d: %s: %s", __FILE__, __LINE__, message, g_strerror(errno)); \
    }

#define rm_log_perrorf(message, ...)                                                     \
    if(errno) {                                                                          \
        int _errsv = errno;                                                              \
        char *msg = g_strdup_printf(message, __VA_ARGS__);                               \
        rm_log_error_line("%s:%d: %s: %s", __FILE__, __LINE__, msg, g_strerror(_errsv)); \
        g_free(msg);                                                                     \
    }

static inline GMutex *rm_log_get_mutex(void) {
    static GMutex RM_LOG_MTX;
    return &RM_LOG_MTX;
}

#define RM_LOG_INIT g_mutex_init(rm_log_get_mutex());

/* These colors should only be used with the rm_log_* macros below */
#define RED "\x1b[31;01m"
#define YELLOW "\x1b[33;01m"
#define RESET "\x1b[0m"
#define GREEN "\x1b[32;01m"
#define BLUE "\x1b[34;01m"

/* Stupid macros to make printing error lines easier */
#define rm_log_error_prefix() \
    rm_log_error(RED);        \
    rm_log_error(_("ERROR")); \
    rm_log_error(": " RESET);

#define rm_log_warning_prefix()   \
    rm_log_warning(YELLOW);       \
    rm_log_warning(_("WARNING")); \
    rm_log_warning(": " RESET);

#define rm_log_info_prefix() \
    rm_log_info(GREEN);      \
    rm_log_info(_("INFO"));  \
    rm_log_info(": " RESET);

#define rm_log_debug_prefix() \
    rm_log_debug(BLUE);       \
    rm_log_debug(_("DEBUG")); \
    rm_log_debug(": " RESET);

///////////////

#define rm_log_error_line(...)                       \
    g_mutex_lock(rm_log_get_mutex());                \
    rm_log_error_prefix() rm_log_error(__VA_ARGS__); \
    rm_log_error("\n");                              \
    g_mutex_unlock(rm_log_get_mutex());

#define rm_log_warning_line(...)                         \
    g_mutex_lock(rm_log_get_mutex());                    \
    rm_log_warning_prefix() rm_log_warning(__VA_ARGS__); \
    rm_log_warning("\n");                                \
    g_mutex_unlock(rm_log_get_mutex());

#define rm_log_info_line(...)                      \
    g_mutex_lock(rm_log_get_mutex());              \
    rm_log_info_prefix() rm_log_info(__VA_ARGS__); \
    rm_log_info("\n");                             \
    g_mutex_unlock(rm_log_get_mutex());

#define rm_log_debug_line(...)                       \
    g_mutex_lock(rm_log_get_mutex());                \
    rm_log_debug_prefix() rm_log_debug(__VA_ARGS__); \
    rm_log_debug("\n");                              \
    g_mutex_unlock(rm_log_get_mutex());

/* Domain for reporting errors. Needed by GOptions */
#define RM_ERROR_QUARK (g_quark_from_static_string("rmlint"))

/**
 * @brief
 *
 * @param
 * @retval
 **/

void rm_logger_callback(_UNUSED const gchar *log_domain,
                        GLogLevelFlags log_level,
                        const gchar *message,
                        _UNUSED gpointer user_data);

void rm_logger_set_pretty(const gboolean is_pretty);

void rm_logger_set_verbosity(const gint new_verbosity);

void rm_logger_incr_verbosity_by(const gint incr);

static inline gboolean rm_logger_louder(_UNUSED const char *option_name,
                                        _UNUSED const gchar *count,
                                        _UNUSED gpointer user_data,
                                        _UNUSED GError **error) {
    rm_logger_incr_verbosity_by(1);
    return TRUE;
}

static inline gboolean rm_logger_quieter(_UNUSED const char *option_name,
                                         _UNUSED const gchar *count,
                                         _UNUSED gpointer user_data,
                                         _UNUSED GError **error) {
    rm_logger_incr_verbosity_by(-1);
    return TRUE;
}

#endif /* end of include guard */
