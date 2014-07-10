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

#ifndef RMLINT_H
#define RMLINT_H

#include "defs.h"

char rm_parse_arguments(int argc, char **argv, RmSession *session);
char rm_echo_settings(RmSettings *settings);
void rm_set_default_settings(RmSettings *set);
void rm_session_init(RmSession *session, RmSettings *settings);
int  rm_main(RmSession *session);
int die(RmSession *session, int status);

#define debug(...) \
    g_log("rmlint", G_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define info(...) \
    g_log("rmlint", G_LOG_LEVEL_MESSAGE, __VA_ARGS__)
#define fyi(...) \
    g_log("rmlint", G_LOG_LEVEL_MESSAGE, __VA_ARGS__)
#define warning(...) \
    g_log("rmlint", G_LOG_LEVEL_WARNING, __VA_ARGS__)
#define error(...) \
    g_log("rmling", G_LOG_LEVEL_CRITICAL, __VA_ARGS__)


#endif
