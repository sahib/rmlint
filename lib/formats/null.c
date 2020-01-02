
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

/* You guessed it: it does nothing. */
typedef struct RmFmtHandlerNull { RmFmtHandler parent; } RmFmtHandlerNull;

static RmFmtHandlerNull NULL_HANDLER_IMPL = {
    /* Initialize parent */
    .parent =
        {
            .size = sizeof(NULL_HANDLER_IMPL),
            .name = "null",
            .head = NULL,
            .elem = NULL,
            .prog = NULL,
            .foot = NULL,
            .valid_keys = {NULL},
        },
};

RmFmtHandler *NULL_HANDLER = (RmFmtHandler *)&NULL_HANDLER_IMPL;
