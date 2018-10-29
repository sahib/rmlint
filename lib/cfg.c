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

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "cfg.h"

static void rm_path_free(RmPath *rmpath) {
    free(rmpath->path);
    g_slice_free(RmPath, rmpath);
}

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

    cfg->total_mem = (RmOff)1024 * 1024 * 1024;
    cfg->sweep_size = 1024 * 1024 * 1024;
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

guint rm_cfg_add_path(RmCfg *cfg, bool is_prefd, const char *path) {
    int rc = 0;

#if HAVE_FACCESSAT
    rc = faccessat(AT_FDCWD, path, R_OK, AT_EACCESS);
#else
    rc = access(path, R_OK);
#endif

    if(rc != 0) {
        rm_log_warning_line(_("Can't open directory or file \"%s\": %s"), path,
                            strerror(errno));
        return 0;
    }

    char *real_path = realpath(path, NULL);
    if(real_path == NULL) {
        rm_log_warning_line(_("Can't get real path for directory or file \"%s\": %s"),
                            path, strerror(errno));
        return 0;
    }

    RmPath *rmpath = g_slice_new(RmPath);
    rmpath->path = real_path;
    rmpath->is_prefd = is_prefd;
    rmpath->idx = cfg->path_count++;
    rmpath->treat_as_single_vol = strncmp(path, "//", 2) == 0;

    if(cfg->replay && g_str_has_suffix(rmpath->path, ".json")) {
        cfg->json_paths = g_slist_prepend(cfg->json_paths, rmpath);
        return 1;
    }

    cfg->paths = g_slist_prepend(cfg->paths, rmpath);
    return 1;
}

void rm_cfg_free_paths(RmCfg *cfg) {
    g_slist_free_full(cfg->paths, (GDestroyNotify)rm_path_free);
    cfg->paths = NULL;
    g_slist_free_full(cfg->json_paths, (GDestroyNotify)rm_path_free);
    cfg->json_paths = NULL;
}
