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

#include "rmlint.h"
#include "useridcheck.h"
#include "list.h"


static void logging_callback(
    G_GNUC_UNUSED const gchar *log_domain,
    GLogLevelFlags log_level,
    const gchar *message,
    gpointer user_data) {
    RmSession *session = user_data;
    if(session->settings->verbosity >= log_level) {
        g_printerr("%s", message);
    }
}

static volatile int CTRLC_COUNTER = 0;
static volatile RmSession *SESSION_POINTER = NULL;

static void signal_handler(int signum) {
    switch(signum) {
    case SIGINT:
        if(CTRLC_COUNTER++ == 0) {
            SESSION_POINTER->aborted = TRUE;
            warning(GRE"\nINFO: "NCO"Received Interrupt, stopping...\n");
        } else {
            warning(GRE"\nINFO: "NCO"Received second Interrupt, stopping hard.\n");
            die((RmSession *)SESSION_POINTER, EXIT_FAILURE);
        }
        break;
    case SIGFPE:
    case SIGABRT:
    case SIGSEGV:
        error(RED"FATAL: "NCO"Aborting due to a fatal error. (signal received: %s)\n", g_strsignal(signum));
    default:
        error(RED"FATAL: "NCO"Please file a bug report (See rmlint -h)\n");
        break;
    }
}

int main(int argc, char **argv) {
    int exit_state = EXIT_FAILURE;

    RmSettings settings;
    rmlint_set_default_settings(&settings);

    RmSession session;
    rm_session_init(&session, &settings);

    /* call logging_callback on every message */
    g_log_set_default_handler(logging_callback, &session);

    /* Make printing umlauts work */
    setlocale(LC_ALL, "");

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
    if(rmlint_parse_arguments(argc, argv, &session) != 0) {
        /* Check settings */
        if (rmlint_echo_settings(session.settings)) {
            /* Do all the real work */
            exit_state = rmlint_main(&session);
        } else {
            error(RED"Aborting.\n"NCO);
        }
    }

    rm_file_list_destroy(session.list);
    return exit_state;
}
