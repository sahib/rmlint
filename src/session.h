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

/* TODO: lookup if all variables still needed. */
/* all available settings - see rmlint -h */
typedef struct RmSettings {
    bool color;
    bool samepart;
    bool ignore_hidden;
    bool followlinks;
    bool paranoid;
    bool namecluster;
    bool findbadids;
    bool findbadlinks;
    bool searchdup;
    bool findemptydirs;
    bool nonstripped;
    short verbosity;
    bool listemptyfiles;
    char **paths;
    char *is_prefd;              /* NEW - flag for each path; 1 if preferred/orig, 0 otherwise*/
    int  num_paths;              /* NEW - counter to make life easier when multi-threading the paths */
    char *cmd_path;
    char *cmd_orig;
    char *sort_criteria;         /* NEW - sets criteria for ranking and selecting "original"*/
    bool limits_specified;
    guint64 minsize;
    guint64 maxsize;
    bool keep_all_originals;     /* NEW - if set, will ONLY delete dupes that are not in ppath */
    bool must_match_original;    /* NEW - if set, will ONLY search for dupe sets where at least one file is in ppath */
    bool invert_original;        /* NEW - if set, inverts selection so that paths _not_ prefixed with // are preferred */
    bool find_hardlinked_dupes;  /* NEW - if set, will also search for hardlinked duplicates*/
    bool skip_confirm;           /* NEW - if set, bypasses user confirmation of input settings*/
    bool confirm_settings;       /* NEW - if set, pauses for user confirmation of input settings*/
    guint64 threads;
    short depth;
    RmDigestType checksum_type;  /* NEW - determines the checksum algorithm used */
    char *iwd;                   /* cwd when rmlint called */
    int argc;                    /* arguments rmlint was called with or NULL */
    const char **argv;           /* arguments rmlint was called with or NULL */
} RmSettings;

typedef struct RmFileTables {
    struct RmMountTable *mounts;
    GHashTable *dev_table;
    GHashTable *size_table;
    GHashTable *size_groups;
    GHashTable *node_table;
    GHashTable *name_table;
    GHashTable *orig_table;
    GQueue *file_queue;
    GList *other_lint[RM_LINT_TYPE_DUPE_CANDIDATE];
    GRecMutex lock;
} RmFileTables;

struct RmFmtTable;

typedef struct RmSession {
    RmSettings *settings;
    struct RmFileTables *tables;
    struct RmMountTable *mounts;
    struct RmFmtTable *formats;

    guint64 total_files;
    guint64 total_lint_size;
    guint64 dup_counter;
    guint64 dup_group_counter;

    volatile bool aborted;

    GTimer *timer;
    glong offset_fragments;
    glong offsets_read;
    glong offset_fails;
} RmSession;

void rm_set_default_settings(RmSettings *settings);
void rm_session_init(RmSession *session, RmSettings *settings);
void rm_session_clear(RmSession *session);

#endif /* end of include guard */

