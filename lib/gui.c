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

#include "gui.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "logger.h"

int rm_gui_launch(int argc, const char **argv) {
    const char *command = "shredder";

    const char *all_argv[512];
    const char **argp = &all_argv[0];
    memset(all_argv, 0, sizeof(all_argv));

    *argp++ = command;

    for(size_t i = 1; i < (size_t)argc && i < sizeof(all_argv) / 2; i++) {
        *argp++ = argv[i];
    }

    if(execvp(command, (char *const *)all_argv) == -1) {
        rm_log_warning("Executed: %s ", command);
        for(int j = 0; j < (argp - all_argv); j++) {
            rm_log_warning("%s ", all_argv[j]);
        }
        rm_log_warning("\n");
        rm_log_error_line("%s %d", g_strerror(errno), errno == ENOENT);
    } else {
        /* This is not reached anymore when execvp suceeded */
        return EXIT_SUCCESS;
    }

    rm_log_error_line("Could not launch gui");
    return EXIT_FAILURE;
}
