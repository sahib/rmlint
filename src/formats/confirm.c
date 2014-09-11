/*
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
 * Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include "../formats.h"

#include <glib.h>
#include <stdio.h>

typedef struct RmFmtHandlerConfirm {
    /* must be first */
    RmFmtHandler parent;
} RmFmtHandlerSummary;

static void rm_fmt_prog(
    RmSession *session,
    _U RmFmtHandler *parent,
    _U FILE *out,
    RmFmtProgressState state
) {
    if(state != RM_PROGRESS_STATE_INIT) {
        return;
    }

    RmSettings *settings = session->settings;

    char confirm;
    bool has_ppath = false;

    rm_log_warning(BLUE"Running rmlint with the following settings:\n"RESET);
    rm_log_warning("(Note "BLUE"[*]"RESET" hints below to change options)\n"RESET);

    /*---------------- lint types ---------------*/
    rm_log_warning("Looking for lint types:\n");
    if (settings->searchdup)	rm_log_warning("\t+ duplicates "RED"(rm)"RESET" [-U]\n");
    if (settings->findemptydirs)rm_log_warning("\t+ empty directories "RED"(rm)"RESET" [-Y]\n");
    if (settings->listemptyfiles)rm_log_warning("\t+ zero size files "RED"(rm)"RESET" [-K]\n");
    if (settings->findbadids)	rm_log_warning("\t+ files with bad UID/GID "BLUE"(chown)"RESET" [-L]\n");
    if (settings->nonstripped)	rm_log_warning("\t+ non-stripped binaries"BLUE"(strip)"RED"(slow)"RESET" [-A]\n");
    if (!settings->searchdup ||
            !settings->findemptydirs ||
            !settings->listemptyfiles ||
            !settings->findbadids ||
            !settings->nonstripped
       ) {
        rm_log_warning(RESET"\tNot looking for:\n");
        if (!settings->searchdup)	rm_log_warning("\t\tduplicates[-u];\n");
        if (!settings->findemptydirs)rm_log_warning("\t\tempty directories[-y];\n");
        if (!settings->listemptyfiles)rm_log_warning("\t\tzero size files[-k];\n");
        if (!settings->findbadids)	rm_log_warning("\t\tfiles with bad UID/GID[-l];\n");
        if (!settings->nonstripped)	rm_log_warning("\t\tnon-stripped binaries[-a];\n");
    }

    /*---------------- search paths ---------------*/
    rm_log_warning(RESET"Search paths:\n");
    for(int i = 0; settings->paths[i] != NULL; ++i) {
        if (settings->is_prefd[i]) {
            has_ppath = true;
            rm_log_warning (GREEN"\t(orig)\t+ %s\n"RESET, settings->paths[i] );
        } else {
            rm_log_warning("\t\t+ %s\n", settings->paths[i]);
        }
    }
    if ((settings->paths[1]) && !has_ppath) {
        rm_log_warning("\t[prefix one or more paths with // to flag location of originals]\n");
    }

    /*---------------- search tree options---------*/
    rm_log_warning("Tree search parameters:\n");
    rm_log_warning("\t%s hidden files and folders [-%s]\n"RESET,
                   settings->ignore_hidden ? "Excluding" : "Including",
                   settings->ignore_hidden ? "G" :  "g" );
    rm_log_warning("\t%s symlinked files and folders [-%s]\n"RESET,
                   settings->followlinks ? "Following" : "Excluding",
                   settings->followlinks ? "F" : "f" );
    rm_log_warning("\t%srossing filesystem / mount point boundaries [-%s]\n"RESET,
                   settings->samepart ? "Not c" : "C",
                   settings->samepart ? "S" : "s");

    if (settings->depth) rm_log_warning("\t Only search %i levels deep into search paths\n", settings->depth);

    /*---------------- file filters ---------------*/

    rm_log_warning("Filtering search based on:\n");

    if (settings->limits_specified) {
        char size_buf_min[128], size_buf_max[128];
        rm_util_size_to_human_readable(settings->minsize, size_buf_min, sizeof(size_buf_min));
        rm_util_size_to_human_readable(settings->maxsize, size_buf_max, sizeof(size_buf_max));
        rm_log_warning("\tFile size between %s and %s bytes\n", size_buf_min, size_buf_max);

    } else {
        rm_log_warning("\tNo file size limits [-z \"min-max\"]\n");
    }
    if (settings->must_match_original) {
        rm_log_warning("\tDuplicates must have at least one member in the "GREEN"(orig)"RESET" paths indicated above\n");
        if (!has_ppath)
            rm_log_error(RED"\tWarning: no "GREEN"(orig)"RED" paths specified for option -M --mustmatchorig (use //)\n"RESET);
    }

    if (settings->find_hardlinked_dupes) {
        rm_log_warning("\tHardlinked file sets will be treated as duplicates\n");
        rm_log_warning(RED"\t\tBUG"RESET": rmlint currently does not deduplicate hardlinked files with same basename\n");
    } else rm_log_warning("\tHardlinked file sets will not be deduplicated [-H]\n");

    /*---------------- originals selection ranking ---------*/

    rm_log_warning(RESET"Originals selected based on (decreasing priority):    [-D <criteria>]\n");
    if (has_ppath) rm_log_warning("\tpaths indicated "GREEN"(orig)"RESET" above\n");

    for (int i = 0; settings->sort_criteria[i]; ++i) {
        switch(settings->sort_criteria[i]) {
        case 'm':
            rm_log_warning("\tKeep oldest modified time\n");
            break;
        case 'M':
            rm_log_warning("\tKeep newest modified time\n");
            break;
        case 'p':
            rm_log_warning("\tKeep first-listed path (above)\n");
            break;
        case 'P':
            rm_log_warning("\tKeep last-listed path (above)\n");
            break;
        case 'a':
            rm_log_warning("\tKeep first alphabetically\n");
            break;
        case 'A':
            rm_log_warning("\tKeep last alphabetically\n");
            break;
        default:
            rm_log_error(RED"\tWarning: invalid originals ranking option '-D %c'\n"RESET, settings->sort_criteria[i]);
            break;
        }
    }

    if (settings->keep_all_originals) {
        rm_log_warning("\tNote: all originals in "GREEN"(orig)"RESET" paths will be kept\n");
    }
    rm_log_warning("\t      "RED"but"RESET" other lint in "GREEN"(orig)"RESET" paths may still be deleted\n");

    /*--------------- paranoia ---------*/

    if (settings->paranoid) {
        rm_log_warning("Note: paranoid (bit-by-bit) comparison will be used to verify duplicates "RED"(slow)\n"RESET);
    } else {
        rm_log_warning("Note: fingerprint and md5 comparison will be used to identify duplicates "RED"(very slight risk of false positives)"RESET" [-p]");
    }

    /*--------------- confirmation ---------*/

    if (settings->confirm_settings) {
        rm_log_warning(YELLOW"\n\nPress y or enter to continue, any other key to abort\n");

        if(scanf("%c", &confirm) == EOF) {
            rm_log_warning(RED"Reading your input failed."RESET);
        }

        if(!(confirm == 'y' || confirm == 'Y' || confirm == '\n')) {
            rm_log_error(RED"Aborting.\n"RESET);
            rm_session_clear(session);
            exit(EXIT_FAILURE);
        }
    }
}

static RmFmtHandlerSummary CONFIRM_HANDLER_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(CONFIRM_HANDLER_IMPL),
        .name = "confirm",
        .head = NULL,
        .elem = NULL,
        .prog = rm_fmt_prog,
        .foot = NULL
    },
};

RmFmtHandler *CONFIRM_HANDLER = (RmFmtHandler *) &CONFIRM_HANDLER_IMPL;
