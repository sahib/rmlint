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

#include "cfg.h"

/* Options not specified by commandline get a default option - this called before rm_cmd_parse_args */
void rm_cfg_set_default(RmCfg *cfg) {
    /* Set everything to 0 at first,
     * only non-null options are listed below.
     */
    memset(cfg, 0, sizeof(RmCfg));

    /* Traversal options */
    cfg->depth   = PATH_MAX / 2;
    cfg->minsize = 0;
    cfg->maxsize = G_MAXUINT64;

    /* Lint Types */
    cfg->ignore_hidden  = true;
    cfg->findemptydirs  = true;
    cfg->listemptyfiles = true;
    cfg->searchdup      = true;
    cfg->findbadids     = true;
    cfg->findbadlinks   = true;

    /* Misc options */
    cfg->sort_criteria = "pm";
    cfg->checksum_type = RMLINT_DEFAULT_DIGEST;
    cfg->color         = 1;
    cfg->threads       = 32;
    cfg->verbosity     = G_LOG_LEVEL_INFO;
    cfg->paranoid_mem  = 256 * 1024 * 1024;
    cfg->followlinks   = false;
    cfg->see_symlinks  = true;

    cfg->skip_start_factor = 0.0;
    cfg->skip_end_factor   = 1.0;

    cfg->use_absolute_start_offset = false;
    cfg->use_absolute_end_offset = false;
    cfg->skip_start_offset    = 0;
    cfg->skip_end_offset      = 0;
}
