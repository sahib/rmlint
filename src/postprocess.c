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

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>

#include "traverse.h"
#include "preprocess.h"
#include "postprocess.h"
#include "utilities.h"
#include "formats.h"

// TODO: move this code and get rid of postprocess.c?
void process_island(RmSession *session, GQueue *group) {
    session->dup_group_counter++;

    bool tagged_original = false;
    RmFile *original_file = NULL;

    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        if (
            ((file->is_prefd) && (session->settings->keep_all_originals)) ||
            ((file->is_prefd) && (!tagged_original))
        ) {
            rm_file_tables_remember_original(session->tables, file);
            if(!tagged_original) {
                tagged_original = true;
                original_file = file;
            }
        }
    }

    if(!tagged_original) {
        /* tag first file as the original */
        original_file = group->head->data;
        rm_file_tables_remember_original(session->tables, original_file);
    }

    /* Hand it over to the printing module */
    rm_fmt_write(session->formats, original_file);
    for(GList *iter = group->head; iter; iter = iter->next) {
        if(iter->data != original_file) {
            rm_fmt_write(session->formats, iter->data);
        }
    }
}
