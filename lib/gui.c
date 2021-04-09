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

#include <unistd.h>

#include "logger.h"

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

int rm_gui_launch(int argc, const char **argv) {
    const char *commands[] = {"python3", "python", NULL};
    const char **command = &commands[0];

    GError *error = NULL;
    gchar *bootstrap_path = NULL;
    int bootstrap_fd =
        g_file_open_tmp(".shredder-bootstrap.py.XXXXXX", &bootstrap_path, &error);

    if(bootstrap_fd < 0) {
        rm_log_error_line("Could not bootstrap gui: Unable to create tempfile: %s",
                          error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    if(write(bootstrap_fd, RM_PY_BOOTSTRAP, sizeof(RM_PY_BOOTSTRAP)) < 0) {
        rm_log_warning_line("Could not bootstrap gui: Unable to write to tempfile: %s",
                            g_strerror(errno));
        return EXIT_FAILURE;
    }

    close(bootstrap_fd);

    while(*command) {
        const char *all_argv[512];
        const char **argp = &all_argv[0];
        memset(all_argv, 0, sizeof(all_argv));

        *argp++ = *command;
        *argp++ = bootstrap_path;

        for(size_t i = 1; i < (size_t)argc && i < sizeof(all_argv) / 2; i++) {
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
            /* This is not reached anymore when execvp suceeded */
            return EXIT_SUCCESS;
        }

        /* Try next command... */
        command++;
    }

    rm_log_error_line("Could not launch gui");
    return EXIT_FAILURE;
}
