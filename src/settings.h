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

#ifndef RM_SETTINGS_H
#define RM_SETTINGS_H

//#include <stdlib.h>
#include <stdio.h>
//#include <stdbool.h>
//#include <glib.h>

#include "checksum.h"
#include "utilities.h"
//#include "file.h"

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
    bool keep_all_tagged;        /* if set, will NOT delete dupes that are in paths tagged with // */
    bool keep_all_untagged;      /* if set, will NOT delete dupes that are in paths NOT tagged with // */
    bool must_match_tagged;      /* if set, will ONLY find dupe sets that have at least once file in a path tagged with // */
    bool must_match_untagged;    /* if set, will ONLY find dupe sets that have at least once file in a path NOT tagged with // */
    bool find_hardlinked_dupes;  /* if set, will also search for hardlinked duplicates*/
    bool confirm_settings;       /* if set, pauses for user confirmation of input settings*/
    bool limits_specified;
    bool lock_files;             /* if set, flock(2) each file before proceeding */
    bool filter_mtime;
    time_t min_mtime;
    bool match_basename;         /* if set, dupes must have the same basename */
    bool merge_directories;      /* if set, merge directories of duplicates (EXPERIMENTAL!) */

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
/**
 * @brief Reset RmSettings to default settings and all other vars to 0.
 */
void rm_set_default_settings(RmSettings *settings);

#endif /* end of include guard */

