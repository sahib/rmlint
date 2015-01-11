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

#include <stdio.h>
#include "checksum.h"
#include "utilities.h"

/* all available settings - see rmlint -h */
typedef struct RmSettings {
    gboolean color;
    gboolean samepart;
    gboolean ignore_hidden;
    gboolean followlinks;
    gboolean see_symlinks;
    gboolean findbadids;
    gboolean findbadlinks;
    gboolean searchdup;
    gboolean findemptydirs;
    gboolean nonstripped;
    gboolean listemptyfiles;
    gboolean keep_all_tagged;           /*  if set, will NOT delete dupes that are in paths tagged with                             //  */
    gboolean keep_all_untagged;         /*  if set, will NOT delete dupes that are in paths NOT tagged with                         //  */
    gboolean must_match_tagged;         /*  if set, will ONLY find dupe sets that have at least once file in a path tagged with     //  */
    gboolean must_match_untagged;       /*  if set, will ONLY find dupe sets that have at least once file in a path NOT tagged with //  */
    gboolean find_hardlinked_dupes;     /*  if set, will also search for hardlinked duplicates*/
    gboolean limits_specified;
    gboolean filter_mtime;
    gboolean match_basename;            /*  if set, dupes must have the same basename */
    gboolean match_with_extension;      /*  if set, dupes must have the same file extension (if any) */
    gboolean match_without_extension;   /*  if set, dupes must have the same basename minus the extension */
    gboolean merge_directories;         /*  if set, find identical directories full of duplicates */
    gboolean write_cksum_to_xattr;      /*  if set, checksums are written to the ext of hashed files */
    gboolean read_cksum_from_xattr;     /*  if set, checksums are tried to be read from the file exts */
    gboolean clear_xattr_fields;        /*  if set, all encountered ext fields are cleared */
    gboolean write_unfinished;          /*  if set, all unfinished checksum are written to json/xattr too */

    time_t min_mtime;
    gint depth;                      /*  max. depth to traverse, 0 means current dir */
    gint verbosity;                  /*  verbosity level (resembles G_LOG_LEVEL_* macros) */

    double skip_start_factor;       /*  Factor from 0.0 - 1.0, from where to start reading */
    double skip_end_factor;         /*  Factor from 0.0 - 1.0, where to stop reading       */

    gboolean use_absolute_start_offset; /*  Use factor for start offset or absolute offset     */
    gboolean use_absolute_end_offset;   /*  Use factor for end offset or absolute offset       */
    RmOff skip_start_offset;        /*  Offset from where to start reading a file          */
    RmOff skip_end_offset;          /*  Offset where to stop reading a file                */

    char **paths;
    char *is_prefd;                 /*  flag for each path; 1 if preferred/orig, 0 otherwise*/
    char *sort_criteria;            /*  sets criteria for ranking and selecting "original"*/
    char *iwd;                      /*  cwd when rmlint called */
    char *joined_argv;              /*  arguments rmlint was called with or NULL when not available */

    RmOff minsize;
    RmOff maxsize;
    RmOff threads;
    RmDigestType checksum_type;  /* determines the checksum algorithm used */
    RmOff paranoid_mem;          /* memory allocation for paranoid buffers */
} RmSettings;
/**
 * @brief Reset RmSettings to default settings and all other vars to 0.
 */
void rm_set_default_settings(RmSettings *settings);

#endif /* end of include guard */

