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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/api.h"
#include "../lib/config.h"
#include "../lib/gui.h"
#include "../lib/logger.h"
#include "../lib/hash-utility.h"
#include "../lib/reflink.h"

#if !GLIB_CHECK_VERSION(2, 36, 0)
#include <glib-object.h>
#endif

static void signal_handler(int signum) {
    switch(signum) {
    case SIGINT:
        rm_session_abort();
        break;
    case SIGFPE:
    case SIGABRT:
    case SIGSEGV:
        /* logging messages might have unexpected effects in a signal handler,
         * but that's probably the least thing we have to worry about in case of
         * a segmentation fault.
         */
        rm_log_error_line(_("Aborting due to a fatal error. (signal received: %s)"),
                          g_strsignal(signum));
        rm_log_error_line(_("Please file a bug report (See rmlint -h)"));
        exit(1);
    default:
        break;
    }
}

static void i18n_init(void) {
#if HAVE_LIBINTL
    /* Tell gettext where to search for .mo files */
    bindtextdomain(RM_GETTEXT_PACKAGE, INSTALL_PREFIX "/share/locale");
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

static void maybe_run_alt_main(int argc, const char **argv, char *match_first,
                               char *alt_main_name, int (*alt_main)(int, const char **)) {
    if(argc < 2) {
        return;
    }
    if(g_strcmp0(match_first, argv[1]) == 0) {
        argv[1] = alt_main_name;
        exit(alt_main(argc - 1, &argv[1]));
    }
    for(int i = 2; i < argc; i++) {
        if(g_strcmp0(match_first, argv[i]) == 0) {
            rm_log_error_line("%s must be first argument", match_first);
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, const char **argv) {
#if !GLIB_CHECK_VERSION(2, 36, 0)
    /* Very old glib. Debian, Im looking at you. */
    g_type_init();
#endif

    int exit_state = EXIT_FAILURE;

    RM_LOG_INIT;
    /* call logging_callback on every message */
    g_log_set_default_handler(rm_logger_callback, NULL);

    maybe_run_alt_main(argc, argv, "--gui", "shredder", &rm_gui_launch);

    maybe_run_alt_main(argc, argv, "--hash", "rmlint-hasher", &rm_hasher_main);

    maybe_run_alt_main(argc, argv, "--is-reflink", "rmlint-is-reflink", &rm_is_reflink_main);

    i18n_init();

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = signal_handler;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);


    RmCfg cfg;
    rm_cfg_set_default(&cfg);
    RmSession session;
    rm_session_init(&session, &cfg);


    /* Parse commandline */
    if(rm_cmd_parse_args(argc, (char **)argv, &session) != 0) {
        /* Do all the real work */
        if(cfg.dedupe) {
            exit_state = rm_session_dedupe_main(&cfg);
        } else {
            exit_state = rm_cmd_main(&session);
        }
    }

    rm_session_clear(&session);
    return exit_state;
}
