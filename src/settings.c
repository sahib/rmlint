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

#include "settings.h"

/* Options not specified by commandline get a default option - this called before rm_cmd_parse_args */
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
    settings->sort_criteria = "m";
    settings->checksum_type = RM_DIGEST_SPOOKY;
    settings->color         = isatty(fileno(stdout));
    settings->threads       = 32;
    settings->verbosity     = G_LOG_LEVEL_INFO;
    settings->paranoid_mem  = 256 * 1024 * 1024;

    settings->skip_start_factor = 0.0;
    settings->skip_end_factor   = 1.0;
}
