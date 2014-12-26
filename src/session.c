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
#include "formats.h"
#include "traverse.h"
#include "preprocess.h"

void rm_session_init(RmSession *session, RmSettings *settings) {
    memset(session, 0, sizeof(RmSession));
    session->timer = g_timer_new();

    session->settings = settings;
    session->tables = rm_file_tables_new(session);
    session->formats = rm_fmt_open(session);

    session->offsets_read = 0;
    session->offset_fragments = 0;
    session->offset_fails = 0;
}

void rm_session_clear(RmSession *session) {
    RmSettings *settings = session->settings;

    /* Free mem */
    if(settings->paths) {
        g_strfreev(settings->paths);
    }

    g_timer_destroy(session->timer);
    rm_file_tables_destroy(session->tables);
    rm_fmt_close(session->formats);
    rm_mounts_table_destroy(session->mounts);

    if(session->dir_merger) {
        rm_tm_destroy(session->dir_merger);
    }

    g_free(settings->joined_argv);
    g_free(settings->is_prefd);
    g_free(settings->iwd);
}

static GMutex ABORT_MTX;

void rm_session_abort(RmSession *session) {
    g_mutex_lock(&ABORT_MTX);
    {
        session->aborted = true;
    }
    g_mutex_unlock(&ABORT_MTX);
}

bool rm_session_was_aborted(RmSession *session) {
    bool was_aborted = false;
    g_mutex_lock(&ABORT_MTX);
    {
        was_aborted = session->aborted;
    }
    g_mutex_unlock(&ABORT_MTX);
    return was_aborted;
}
