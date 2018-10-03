/**
* This file is part of rmlint.
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
*  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
*  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
*
* Hosted on http://github.com/sahib/rmlint
**/

#include <string.h>     // memset
#include <stdbool.h>    // bool, true, false
#include <limits.h>     // PATH_MAX (maybe)
#include <glib.h>       // G_MAXUINT64, g_strdup, G_LOG_LEVEL_INFO, g_slist_free_full

#include "config.h"     // RM_DEFAULT_DIGEST, PATH_MAX (maybe), RmOff
#include "cfg.h"        // RmCfg
#include "pathtricia.h" // rm_trie_init
#include "path-funcs.h" // rm_path_free, rm_path_is_valid, rm_path_is_json, rm_path_prepend

/* Options not specified by commandline get a default option -
 * this is usually called before rm_cmd_parse_args */
void rm_cfg_set_default(RmCfg *cfg) {
    /* Set everything to 0 at first,
     * only non-null options are listed below.
     */
    memset(cfg, 0, sizeof(RmCfg));

    /* Traversal options */
    cfg->depth = PATH_MAX / 2;
    cfg->limits_specified = true;
    cfg->minsize = 1;
    cfg->maxsize = G_MAXUINT64;

    /* Lint Types */
    cfg->ignore_hidden = true;
    cfg->find_emptydirs = true;
    cfg->find_emptyfiles = true;
    cfg->find_duplicates = true;
    cfg->find_badids = true;
    cfg->find_badlinks = true;
    cfg->find_hardlinked_dupes = true;
    cfg->keep_hardlinked_dupes = false;
    cfg->build_fiemap = true;
    cfg->crossdev = true;
    cfg->list_mounts = true;

    /* Misc options */
    cfg->sort_criteria = g_strdup("pOma");

    cfg->checksum_type = RM_DEFAULT_DIGEST;
    cfg->with_color = true;
    cfg->with_stdout_color = true;
    cfg->with_stderr_color = true;
    cfg->threads = 16;
    cfg->threads_per_disk = 2;
    cfg->verbosity = G_LOG_LEVEL_INFO;
    cfg->follow_symlinks = false;

    /* Optimum buffer size based on /usr without dropping caches:
     * 4k  => 5.29 seconds
     * 8k  => 5.11 seconds
     * 16k => 5.04 seconds
     * 32k => 5.08 seconds
     * With dropped caches:
     * 4k  => 45.2 seconds
     * 16k => 45.0 seconds
     * Optimum buffer size using a rotational disk and paranoid hash:
     * 4k  => 16.5 seconds
     * 8k  => 16.5 seconds
     * 16k => 15.9 seconds
     * 32k => 15.8 seconds */
    cfg->read_buf_len = 16 * 1024;

    cfg->total_mem = 1024L * 1024 * 1024;
    cfg->sweep_size = 1024L * 1024 * 1024;
    cfg->sweep_count = 1024 * 16;

    cfg->skip_start_factor = 0.0;
    cfg->skip_end_factor = 1.0;

    cfg->use_absolute_start_offset = false;
    cfg->use_absolute_end_offset = false;
    cfg->skip_start_offset = 0;
    cfg->skip_end_offset = 0;
    cfg->mtime_window = -1;

    rm_trie_init(&cfg->file_trie);
}

bool rm_cfg_prepend_json(
    RmCfg *const cfg,
    const char *const path
) {
    g_assert(cfg);
    char *real_path;
    if(rm_path_is_valid(path, &real_path) && rm_path_is_json(real_path)) {
        rm_path_prepend(
            &cfg->json_paths,
            real_path,
            cfg->path_count++,
            false /* not preferred */
        );
        return true;
    }
    return false;
}

bool rm_cfg_prepend_path(
    RmCfg *const cfg,
    const char *const path,
    const bool preferred
) {
    g_assert(cfg);
    char *real_path;
    if(rm_path_is_valid(path, &real_path)) {
        rm_path_prepend(
            (cfg->replay && rm_path_is_json(path)) ?
                &cfg->json_paths : &cfg->paths,
            real_path,
            cfg->path_count++,
            preferred
        );
        return true;
    }
    return false;
}

void rm_cfg_free_paths(RmCfg *const cfg) {
    g_assert(cfg);
    g_slist_free_full(cfg->paths, (GDestroyNotify)rm_path_free);
    cfg->paths = NULL;
    g_slist_free_full(cfg->json_paths, (GDestroyNotify)rm_path_free);
    cfg->json_paths = NULL;
}
