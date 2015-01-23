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

void rm_session_init(RmSession *session, RmCfg *cfg) {
    memset(session, 0, sizeof(RmSession));
    session->timer = g_timer_new();
    g_queue_init(&session->cache_list);

    session->cfg = cfg;
    session->tables = rm_file_tables_new(session);
    session->formats = rm_fmt_open(session);

    session->verbosity_count = 2;
    session->paranoia_count  = 0;
    session->output_cnt[0]   = -1;
    session->output_cnt[1]   = -1;

    session->offsets_read = 0;
    session->offset_fragments = 0;
    session->offset_fails = 0;
}

void rm_session_clear(RmSession *session) {
    RmCfg *cfg = session->cfg;

    /* Free mem */
    if(cfg->paths) {
        g_strfreev(cfg->paths);
    }

    g_timer_destroy(session->timer);
    rm_file_tables_destroy(session->tables);
    rm_fmt_close(session->formats);

    if(session->mounts) {
        rm_mounts_table_destroy(session->mounts);
    }

    if(session->dir_merger) {
        rm_tm_destroy(session->dir_merger);
    }

    for(GList *iter = session->cache_list.head; iter; iter = iter->next) {
        g_free((char *)iter->data);
    }

    g_queue_clear(&session->cache_list);

    g_free(cfg->joined_argv);
    g_free(cfg->is_prefd);
    g_free(cfg->iwd);
}

void rm_session_abort(RmSession *session) {
    g_atomic_int_set(&session->aborted, 1);
}

bool rm_session_was_aborted(RmSession *session) {
    return g_atomic_int_get(&session->aborted);
}
