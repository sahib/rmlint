/**
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
** Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include "../lib/api.h"

static char *remove_color_escapes(char *message) {
    char *dst = message;
    for(char *src = message; src && *src; src++) {
        if(*src == '\x1b') {
            src = strchr(src, 'm');
        } else {
            *dst++ = *src;
        }
    }

    if(dst) *dst = 0;
    return message;
}

static void logging_callback(
    _U const gchar *log_domain,
    GLogLevelFlags log_level,
    const gchar *message,
    gpointer user_data) {

    RmSession *session = user_data;
    if(session->cfg->verbosity >= log_level) {
        if(!session->cfg->with_stderr_color) {
            message = remove_color_escapes((char *)message);
        }
        fputs(message, stderr);
    }
}

/* Global variables, signal handlers cannot be passed userdata */
static volatile int CTRLC_COUNTER = 0;
static volatile RmSession *SESSION_POINTER = NULL;

static void signal_handler(int signum) {
    switch(signum) {
    case SIGINT:
        if(CTRLC_COUNTER++ == 0) {
            rm_session_abort((RmSession *)SESSION_POINTER);
            rm_log_warning(GREEN"\nINFO: "RESET);
            rm_log_warning(_("Received Interrupt, stopping..."));
            rm_log_warning("\n");
        } else {
            rm_log_warning(GREEN"\nINFO: "RESET);
            rm_log_warning(_("Received second Interrupt, stopping hard."));
            rm_log_warning("\n");
            rm_session_clear((RmSession *)SESSION_POINTER);
            exit(EXIT_FAILURE);
        }
        break;
    case SIGFPE:
    case SIGABRT:
    case SIGSEGV:
        rm_log_error_line(_("Aborting due to a fatal error. (signal received: %s)"), g_strsignal(signum));
        rm_log_error_line(_("Please file a bug report (See rmlint -h)"));
    default:
        exit(EXIT_FAILURE);
        break;
    }
}

static void i18n_init(void) {
#if HAVE_LIBINTL
    /* Tell gettext where to search for .mo files */
    bindtextdomain(RM_GETTEXT_PACKAGE, INSTALL_PREFIX"/share/locale");
    bind_textdomain_codeset(RM_GETTEXT_PACKAGE, "UTF-8");

    /* Make printing umlauts work */
    setlocale(LC_ALL, "");

    /* Say we're the textdomain "rmlint"
     * so gettext can find us in
     * /usr/share/locale/de/LC_MESSAGEs/rmlint.mo
     * */
    textdomain(RM_GETTEXT_PACKAGE);
#endif
}

int main(int argc, const char **argv) {
    int exit_state = EXIT_FAILURE;

    RmCfg cfg;
    rm_cfg_set_default(&cfg);

    RmSession session;
    rm_session_init(&session, &cfg);

    /* call logging_callback on every message */
    g_log_set_default_handler(logging_callback, &session);

    i18n_init();

    /* Register signals */
    SESSION_POINTER = &session;

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = signal_handler;

    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);

    /* Parse commandline */
    if(rm_cmd_parse_args(argc, (char **)argv, &session) != 0) {
        /* Do all the real work */
        exit_state = rm_cmd_main(&session);
    }

    rm_session_clear(&session);
    return exit_state;
}
