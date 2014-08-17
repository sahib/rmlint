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

#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "session.h"
#include "traverse.h"
#include "preprocess.h"
#include "postprocess.h"

/* Options not specified by commandline get a default option - this called before rm_parse_arguments */
void rm_set_default_settings(RmSettings *settings) {
    /* Set everything to 0 at first,
     * only non-null options are listed below.
     */
    memset(settings, 0, sizeof(RmSettings));

    /* Traversal options */
    settings->depth   = PATH_MAX / 2;
    settings->minsize = 0;
    settings->maxsize = G_MAXUINT64;

    /* Lint Types */
    settings->ignore_hidden  = true;
    settings->findemptydirs  = true;
    settings->listemptyfiles = true;
    settings->searchdup      = true;
    settings->findbadids     = true;
    settings->findbadlinks   = true;

    /* Misc options */
    settings->output_log    = "rmlint.log";
    settings->output_script = "rmlint.sh";
    settings->sort_criteria = "m";
    settings->mode          = RM_MODE_LIST;
    settings->checksum_type = RM_DIGEST_SPOOKY;
    settings->color         = isatty(fileno(stdout));
    settings->threads       = 32;
    settings->verbosity     = G_LOG_LEVEL_INFO;
}

void rm_session_init(RmSession *session, RmSettings *settings) {
    memset(session, 0, sizeof(RmSession));

    session->settings = settings;
    init_filehandler(session);

    session->mounts = rm_mounts_table_new();
    session->table = rm_file_table_new(session);
}

void rm_session_clear(RmSession *session) {
    RmSettings *sets = session->settings;

    /* Free mem */
    if(sets->paths) {
        for(int i = 0; sets->paths[i]; ++i) {
            g_free(sets->paths[i]);
        }
        g_free(sets->paths);
    }

    g_free(sets->is_prefd);
    g_free(sets->iwd);

    /* Close logfile */
    if(session->log_out) {
        fclose(session->log_out);
    }

    rm_file_table_destroy(session->table);

    /* Close scriptfile */
    if(session->script_out) {
        fprintf(
            session->script_out,
            "                      \n"
            "if [ -z $DO_REMOVE ]  \n"
            "then                  \n"
            "  %s %s;              \n"
            "  %s %s;              \n"
            "fi                    \n",
            (session->settings->output_script) ? "rm -rf" : "",
            (session->settings->output_script) ? (session->settings->output_script) : "",
            (session->settings->output_log) ? "rm -rf" : "",
            (session->settings->output_log) ? (session->settings->output_log) : ""
        );
        fclose(session->script_out);
    }
}
