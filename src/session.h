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

#ifndef RM_SESSION_H
#define RM_SESSION_H

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <glib.h>

#include "checksum.h"
#include "utilities.h"
#include "file.h"

/* all available settings - see rmlint -h */
typedef struct RmSettings {
    bool color;
    bool samepart;
    bool ignore_hidden;
    bool followlinks;
    bool findbadids;
    bool findbadlinks;
    bool searchdup;
    bool findemptydirs;
    bool nonstripped;
    bool listemptyfiles;
    bool keep_all_originals;     /* if set, will ONLY delete dupes that are not in ppath */
    bool must_match_original;    /* if set, will ONLY search for dupe sets where at least one file is in ppath */
    bool find_hardlinked_dupes;  /* if set, will also search for hardlinked duplicates*/
    bool confirm_settings;       /* if set, pauses for user confirmation of input settings*/
    bool limits_specified;
    bool lock_files;             /* if set, flock(2) each file before proceeding */
    bool filter_mtime;
    time_t min_mtime;
    bool match_basename;         /* if set, dupes must have the same basename */

    int depth;
    int verbosity;
    int paranoid;

    char **paths;
    char *is_prefd;              /* flag for each path; 1 if preferred/orig, 0 otherwise*/
    char *sort_criteria;         /* sets criteria for ranking and selecting "original"*/
    char *iwd;                   /* cwd when rmlint called */
    char *joined_argv;           /* arguments rmlint was called with or NULL when not available */

    RmOff minsize;
    RmOff maxsize;
    RmOff threads;
    RmDigestType checksum_type;  /* determines the checksum algorithm used */
    RmOff paranoid_mem;        /* memory allocation for paranoid buffers */
} RmSettings;

typedef struct RmFileTables {
    struct RmMountTable *mounts;
    GHashTable *dev_table;
    GHashTable *size_groups;
    GHashTable *node_table;
    GHashTable *mtime_filter;
    GHashTable *basename_filter;
    GHashTable *orig_table;
    GQueue *file_queue;
    GList *other_lint[RM_LINT_TYPE_DUPE_CANDIDATE];
    GRecMutex lock;
} RmFileTables;

struct RmFmtTable;

typedef struct RmSession {
    RmSettings *settings;

    /* Stores for RmFile during traversal, preproces and shredder */
    struct RmFileTables *tables;

    /* Table of mountpoints used in the system */
    struct RmMountTable *mounts;

    /* Output formatting control */
    struct RmFmtTable *formats;

    /* Counters for printing useful statistics */
    RmOff total_files;
    RmOff total_filtered_files;
    RmOff total_lint_size;
    RmOff dup_counter;
    RmOff dup_group_counter;
    RmOff ignored_files;
    RmOff ignored_folders;
    RmOff other_lint_cnt;

    /* flag indicating if rmlint was aborted early */
    volatile bool aborted;

    GTimer *timer;

    /* Debugging counters */
    RmOff offset_fragments;
    RmOff offsets_read;
    RmOff offset_fails;

    /* Daniels paranoia */
    RmOff hash_seed1;
    RmOff hash_seed2;
} RmSession;

/**
 * @brief Reset RmSettings to default settings and all other vars to 0.
 */
void rm_set_default_settings(RmSettings *settings);

/**
 * @brief Initialize session according to settings.
 */
void rm_session_init(RmSession *session, RmSettings *settings);

/**
 * @brief Clear all memory allocated by rm_session_init.
 */
void rm_session_clear(RmSession *session);

/**
 * @brief Set the abort flag of RmSession.
 *
 * This flag is checked periodically on strategic points,
 * leading to an early but planned exit.
 *
 * Threadsafe.
 */
void rm_session_abort(RmSession *session);

/**
 * @brief Check if rmlint was aborted early.
 *
 * Threadsafe.
 */
bool rm_session_was_aborted(RmSession *session);

/* Maybe colors, for use outside of the rm_log macros,
 * in order to work with the --with-no-color option
 * */
#define MAYBE_RED(s)    ((s->settings->color) ? RED : "")
#define MAYBE_YELLOW(s) ((s->settings->color) ? YELLOW : "")
#define MAYBE_RESET(s)  ((s->settings->color) ? RESET : "")
#define MAYBE_GREEN(s)  ((s->settings->color) ? GREEN : "")
#define MAYBE_BLUE(s)   ((s->settings->color) ? BLUE : "")

#endif /* end of include guard */

