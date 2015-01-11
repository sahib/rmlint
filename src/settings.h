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
    gboolean keep_all_tagged;           
    gboolean keep_all_untagged;         
    gboolean must_match_tagged;         
    gboolean must_match_untagged;       
    gboolean find_hardlinked_dupes;     
    gboolean limits_specified;
    gboolean filter_mtime;
    gboolean match_basename;            
    gboolean match_with_extension;      
    gboolean match_without_extension;   
    gboolean merge_directories;         
    gboolean write_cksum_to_xattr;      
    gboolean read_cksum_from_xattr;     
    gboolean clear_xattr_fields;        
    gboolean write_unfinished;          

    time_t min_mtime;
    gint depth;                   
    gint verbosity;               

    gdouble skip_start_factor;    
    gdouble skip_end_factor;      

    gboolean use_absolute_start_offset; 
    gboolean use_absolute_end_offset;   
    RmOff skip_start_offset;     
    RmOff skip_end_offset;       

    char **paths;
    char *is_prefd;              
    char *sort_criteria;         
    char *iwd;                   
    char *joined_argv;           

    RmOff minsize;
    RmOff maxsize;
    RmOff threads;
    RmDigestType checksum_type;  
    RmOff paranoid_mem;          
} RmSettings;
/**
 * @brief Reset RmSettings to default settings and all other vars to 0.
 */
void rm_set_default_settings(RmSettings *settings);

#endif /* end of include guard */

