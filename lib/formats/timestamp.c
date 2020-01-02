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

#include "../formats.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct RmFmtHandlerTimestamp {
    /* must be first */
    RmFmtHandler parent;
} RmFmtHandlerSummary;

static void rm_fmt_prog(RmSession *session,
                        _UNUSED RmFmtHandler *parent,
                        _UNUSED FILE *out,
                        RmFmtProgressState state) {
    if(state != RM_PROGRESS_STATE_INIT) {
        return;
    }

    if(rm_fmt_get_config_value(session->formats, "stamp", "iso8601")) {
        char time_buf[256];
        memset(time_buf, 0, sizeof(time_buf));
        rm_iso8601_format(time(NULL), time_buf, sizeof(time_buf));
        fprintf(out, "%s", time_buf);
    } else {
        /* Just write out current time */
        fprintf(out, "%" LLU "", (guint64)time(NULL));
    }
}

static RmFmtHandlerSummary TIMESTAMP_HANDLER_IMPL = {
    /* Initialize parent */
    .parent =
        {
            .size = sizeof(TIMESTAMP_HANDLER_IMPL),
            .name = "stamp",
            .head = NULL,
            .elem = NULL,
            .prog = rm_fmt_prog,
            .foot = NULL,
            .valid_keys = {"iso8601", NULL},
        },
};

RmFmtHandler *TIMESTAMP_HANDLER = (RmFmtHandler *)&TIMESTAMP_HANDLER_IMPL;
