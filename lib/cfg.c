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
*  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
*  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
*
* Hosted on http://github.com/sahib/rmlint
**/
#include "cfg.h"

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "logger.h"
#include "utilities.h"

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
    cfg->see_symlinks = true;
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

    cfg->clamp_is_used = false;

    cfg->skip_start_factor = 0.0;
    cfg->skip_end_factor = 1.0;

    cfg->use_absolute_start_offset = false;
    cfg->use_absolute_end_offset = false;
    cfg->skip_start_offset = 0;
    cfg->skip_end_offset = 0;
    cfg->mtime_window = -1;

    rm_trie_init(&cfg->file_trie);
}

static RmPath *rm_path_new(char *real_path, bool is_prefd, guint idx,
                           bool treat_as_single_vol, bool realpath_worked, RmTrie *tree) {
    RmPath *ret = g_slice_new(RmPath);
    ret->path = real_path;
    ret->is_prefd = is_prefd;
    ret->idx = idx;
    ret->treat_as_single_vol = treat_as_single_vol;
    ret->realpath_worked = realpath_worked;
    if(tree) {
        ret->node = rm_trie_insert(tree, real_path, NULL);
    }
    return ret;
}

guint rm_cfg_add_path(RmCfg *cfg, bool is_prefd, const char *path) {
    int rc = access(path, R_OK);

    if(rc != 0) {
        /* We have to check here if it's maybe a symbolic link.
         * Do this by checking with readlink() - if it succeeds
         * it is most likely a symbolic link. We do not really need
         * the link path, so we just a size-one array.
         *
         * faccessat() cannot be trusted, since it works differently
         * on different platforms (i.e. between glibc and musl)
         * (lesson learned, see https://github.com/sahib/rmlint/pull/444)
         * */
        char dummy[1] = {0};
        rc = readlink(path, dummy, 1);
        if(rc < 0) {
            rm_log_warning_line(_("Can't open directory or file \"%s\": %s"), path,
                                strerror(errno));
            return 0;
        }
    }

    bool realpath_worked = true;
    char *real_path = realpath(path, NULL);
    if(real_path == NULL) {
        rm_log_debug_line(_("Can't get real path for directory or file \"%s\": %s"), path,
                          strerror(errno));

        /* Continue with the path we got,
         * this is likely a bad symbolic link */
        real_path = rm_canonicalize_filename(path, NULL);
        realpath_worked = false;
    }

    bool is_json = cfg->replay && g_str_has_suffix(real_path, ".json");
    RmTrie *tree = is_json ? NULL : &cfg->file_trie;
    bool treat_as_single_vol = (strncmp(path, "//", 2) == 0);

    RmPath *rmpath = rm_path_new(real_path, is_prefd, cfg->path_count,
                                 treat_as_single_vol, realpath_worked, tree);

    if(is_json) {
        cfg->json_paths = g_slist_prepend(cfg->json_paths, rmpath);
        return 1;
    }

    cfg->path_count++;
    cfg->paths = g_slist_prepend(cfg->paths, rmpath);
    return 1;
}

bool rm_cfg_is_traversed(RmCfg *cfg, RmNode *node, bool *is_prefd,
                         unsigned long *path_index, bool *is_hidden,
                         bool *is_on_subvol_fs, short *depth) {
    // Note:  depends on cfg->paths having all preferred paths at start
    // of list for is_prefd to work!!!

    // set defaults
    *is_prefd = FALSE;
    *path_index = 0;
    *is_on_subvol_fs = FALSE;
    *is_hidden = FALSE;
    *depth = 0;

    // search up tree from target (node) to see if we encounter a cmdline search path
    for(short d = 0; node != NULL && d <= cfg->depth; d++) {
        for(GSList *iter = cfg->paths; iter; iter = iter->next) {
            RmPath *path = iter->data;
            if(path->node == node) {
                *is_prefd = path->is_prefd;
                *path_index = path->idx;
                *is_on_subvol_fs = path->treat_as_single_vol;
                *depth = d;
                return TRUE;
            }
        }
        if(node->basename && node->basename[0] == '.') {
            // file is hidden relative to search paths
            *is_hidden = TRUE;
            if(cfg->ignore_hidden) {
                // file is hidden from search path
                return FALSE;
            }
        }
        // TODO: partial hidden

        node = node->parent;
    }
    return FALSE;
}

void rm_cfg_free_paths(RmCfg *cfg) {
    g_slist_free_full(cfg->paths, (GDestroyNotify)rm_path_free);
    cfg->paths = NULL;
    g_slist_free_full(cfg->json_paths, (GDestroyNotify)rm_path_free);
    cfg->json_paths = NULL;
}
