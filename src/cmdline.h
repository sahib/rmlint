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

#include <errno.h>
#include <stdbool.h>

#include "defs.h"
#include "checksum.h"


typedef enum RmHandleMode {
    RM_MODE_LIST = 1,
    RM_MODE_NOASK = 3,
    RM_MODE_LINK = 4,
    RM_MODE_CMD = 5
} RmHandleMode;


/* TODO: lookup if all variables still needed. */
/* all available settings see rmlint -h */
typedef struct RmSettings {
    RmHandleMode mode;
    bool color;
    bool collide;
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
    char verbosity;
    bool listemptyfiles;
    char **paths;
    char *is_ppath;              /* NEW - flag for each path; 1 if preferred/orig, 0 otherwise*/
    int  num_paths;              /* NEW - counter to make life easier when multi-threading the paths */
    char *cmd_path;
    char *cmd_orig;
    char *output_script;
    char *output_log;
    char *sort_criteria;         /* NEW - sets criteria for ranking and selecting "original"*/
    bool limits_specified;
    guint64 minsize;
    guint64 maxsize;
    bool keep_all_originals;     /* NEW - if set, will ONLY delete dupes that are not in ppath */
    bool must_match_original;    /* NEW - if set, will ONLY search for dupe sets where at least one file is in ppath*/
    bool invert_original;        /* NEW - if set, inverts selection so that paths _not_ prefixed with // are preferred*/
    bool find_hardlinked_dupes;  /* NEW - if set, will also search for hardlinked duplicates*/
    bool skip_confirm;           /* NEW - if set, bypasses user confirmation of input settings*/
    bool confirm_settings;       /* NEW - if set, pauses for user confirmation of input settings*/
    guint64 threads;
    short depth;
    RmDigestType checksum_type;  /* NEW - determines the checksum algorithm used */
    char *iwd;                   /* cwd when rmlint called */
} RmSettings;


typedef struct RmFileTable RmFileTable;
typedef struct RmMountTable RmMountTable;

typedef struct RmSession {
    RmFileTable *table;
    RmSettings *settings;
    RmMountTable *mounts;

    guint64 total_files;
    guint64 total_lint_size;
    guint64 dup_counter;
    guint64 dup_group_counter;

    FILE *script_out;
    FILE *log_out;

    volatile bool aborted;
} RmSession;

#include "checksum.h"
#include "traverse.h"
#include "config.h"


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
#define rm_error(...) \
    g_log("rmlint", G_LOG_LEVEL_CRITICAL, __VA_ARGS__)

#define rm_perror(message)                                                      \
    if(errno) {                                                                 \
        rm_error("%s:%d: %s: %s\n", __FILE__, __LINE__, message, g_strerror(errno)); \
    }                                                                           \
 

#endif
