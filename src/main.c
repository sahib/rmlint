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
** Author: Christopher Pahl <sahib@online.de>:
** Hosted on http://github.com/sahib/rmlint
*
**/

#include <stdlib.h>
#include <string.h>

#include "rmlint.h"
#include "useridcheck.h"
#include "list.h"

int main(int argc, char **argv) {
    int exit_state = EXIT_FAILURE;

    RmSettings settings;
    rmlint_set_default_settings(&settings);

    RmSession session;
    // TODO: write some init.
    session.dup_counter = 0;
    session.total_lint_size = 0;
    session.total_files = 0;
    session.userlist = userlist_new();

    session.list = rm_file_list_new();
    session.settings = &settings;

    /* Parse commandline */
    if(rmlint_parse_arguments(argc, argv, &session) != 0) {
        /* Check settings */
        if (rmlint_echo_settings(session.settings)) {
            /* Do all the real work */
            exit_state = rmlint_main(&session);
        } else {
            error(RED"Aborting\n"NCO);
        }
    }

    rm_file_list_destroy(session.list);
    return exit_state;
}
