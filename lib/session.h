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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#ifndef RM_SESSION_H
#define RM_SESSION_H

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"     // INLINE
#include "treemerge.h"  // RmTreeMerger

typedef struct RmFileTables {
    /* List of all files found during traversal */
    GQueue *all_files;

    /* GSList of GList's, one for each file size */
    GSList *size_groups;

    /* Used for finding inode matches */
    GHashTable *node_table;

    /* Used for finding path doubles */
    GHashTable *unique_paths_table;

    /*array of lists, one for each "other lint" type */
    GList *other_lint[RM_LINT_TYPE_DUPE_CANDIDATE];

    /* lock for access to *list during traversal */
    GMutex lock;
} RmFileTables;

struct RmFmtTable;

typedef struct RmSession {
    RmCfg *cfg;

    /* Stores for RmFile during traversal, preproces and shredder */
    struct RmFileTables *tables;

    /* Table of mountpoints used in the system */
    struct RmMountTable *mounts;

    /* Output formatting control */
    struct RmFmtTable *formats;

    /* Treemerging for -D */
    struct RmTreeMerger *dir_merger;

    /* Shredder session */
    struct RmShredTag *shredder;

    /* Disk Scheduler */
    struct _RmMDS *mds;

    /* Cache of already compiled GRegex patterns */
    GPtrArray *pattern_cache;

    /* Counters for printing useful statistics */
    volatile gint total_files;
    volatile gint ignored_files;
    volatile gint ignored_folders;

    RmOff total_filtered_files;
    RmOff total_lint_size;
    RmOff shred_bytes_remaining;
    RmOff shred_bytes_total;
    RmOff shred_files_remaining;
    RmOff shred_bytes_after_preprocess;
    RmOff dup_counter;
    RmOff dup_group_counter;
    RmOff other_lint_cnt;

    RmOff duplicate_bytes;
    RmOff unique_bytes;
    RmOff original_bytes;
    RmOff shred_bytes_read;

    GTimer *timer_since_proc_start;

    /* flag indicating if rmlint was aborted early */
    volatile gint aborted;

    /* timer used for debugging and profiling messages */
    GTimer *timer;

    /* Debugging counters */
    RmOff offset_fragments;
    RmOff offsets_read;
    RmOff offset_fails;

    /* Daniels paranoia */
    RmOff hash_seed;

    /* count used for determining the verbosity level */
    int verbosity_count;

    /* count used for determining the paranoia level */
    int paranoia_count;

    /* count for -o and -O; initialized to -1 */
    int output_cnt[2];

    /* true if a cmdline parse error happened */
    bool cmdline_parse_error;

    /* true once shredder finished running */
    bool shredder_finished;

    /* true once traverse finished running */
    bool traverse_finished;

    /*  When run with --equal this holds the exit code for rmlint
     *  (the exit code is determined by the _equal formatter) */
    int equal_exit_code;
} RmSession;

/**
 * @brief Initialize session according to cfg.
 */
void rm_session_init(RmSession *session, RmCfg *cfg);

/**
 * @brief Clear all memory allocated by rm_session_init.
 */
void rm_session_clear(RmSession *session);

/**
 * @brief Set the global abort flag.
 *
 * This flag is checked periodically on strategic points,
 * leading to an early but planned exit.
 *
 * Threadsafe.
 */
static INLINE void rm_session_abort(void) {
    extern volatile int rm_session_abort_count;
    g_atomic_int_add(&rm_session_abort_count, 1);
}

/**
 * @brief Check if rmlint was aborted early.
 *
 * Threadsafe.
 */
static INLINE bool rm_session_was_aborted(void) {
    extern volatile int rm_session_abort_count;
    const gint n = g_atomic_int_get(&rm_session_abort_count);

    if (n) {
        void rm_session_acknowledge_abort(gint);
        rm_session_acknowledge_abort(n);
    }

    return n;
}

/**
 * @brief Check the kernel version of the Linux kernel.
 *
 * @param session Session to ask. Version is cached in the session.
 * @param major The major version it should have at least.
 * @param minor The minor version it should have at least.
 *
 * @return True if the kernel is recent enough.
 */
bool rm_session_check_kernel_version(int need_major, int need_minor);

/**
 * @brief Trigger rmlint in --dedupe mode.
 *
 * @return exit_status for exit()
 */
int rm_session_dedupe_main(RmCfg *cfg);


/**
 * @brief Trigger rmlint in --is-reflink mode.
 *
 * @return 0 if is reflink, 1 if not, ?? if can't tell
 */
int rm_session_is_reflink_main(RmCfg *cfg);

/* Maybe colors, for use outside of the rm_log macros,
 * in order to work with the --with-no-color option
 *
 * MAYBE_COLOR checks the file we output too.
 * If it is stderr or stdout it consults the respective setting automatically.
 * */
#define MAYBE_COLOR(o, s, col)                           \
    (!s->cfg->with_color                                 \
         ? ""                                            \
         : (fileno(o) == 1                               \
                ? (s->cfg->with_stdout_color ? col : "") \
                : (fileno(o) == 2 ? (s->cfg->with_stderr_color ? col : "") : "")))

#define MAYBE_RED(o, s) MAYBE_COLOR(o, s, RED)
#define MAYBE_YELLOW(o, s) MAYBE_COLOR(o, s, YELLOW)
#define MAYBE_RESET(o, s) MAYBE_COLOR(o, s, RESET)
#define MAYBE_GREEN(o, s) MAYBE_COLOR(o, s, GREEN)
#define MAYBE_BLUE(o, s) MAYBE_COLOR(o, s, BLUE)

#endif /* end of include guard */
