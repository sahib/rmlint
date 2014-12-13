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
#include "../preprocess.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>


typedef struct RmFmtHandlerShScript {
    RmFmtHandler parent;
    RmFile *last_original;

    bool opt_use_ln;
    bool opt_symlinks_only;
} RmFmtHandlerShScript;

static const char *SH_SCRIPT_TEMPLATE_HEAD =
    "#!/bin/sh                                           \n"
    "# This file was autowritten by rmlint               \n"
    "# rmlint was executed from: %s                      \n"
    "# You command line was: %s                          \n"
    "                                                    \n"
    "ask() {                                             \n"
    "cat << EOF\n                                        \n"
    "This script will delete certain files rmlint found. \n"
    "It is highly advisable to view the script first!    \n"
    "                                                    \n"
    "Execute this script with -d to disable this message \n"
    "Hit enter to continue; CTRL-C to abort immediately  \n"
    "\nEOF\n                                             \n"
    "read dummy_var                                      \n"
    "}                                                   \n"
    "                                                    \n"
    "usage() {                                           \n"
    "cat << EOF\n                                        \n"
    "usage: $0 options                                   \n"
    "                                                    \n"
    "OPTIONS:                                            \n"
    "-h      Show this message                           \n"
    "-d      Do not ask before running                   \n"
    "-x      Keep rmlint.sh and rmlint.log               \n"
    "\nEOF\n                                             \n"
    "}                                                   \n"
    "                                                    \n"
    "DO_REMOVE=                                          \n"
    "DO_ASK=                                             \n"
    "                                                    \n"
    "while getopts “dhx” OPTION                          \n"
    "do                                                  \n"
    "  case $OPTION in                                   \n"
    "     h)                                             \n"
    "       usage                                        \n"
    "       exit 1                                       \n"
    "       ;;                                           \n"
    "     d)                                             \n"
    "       DO_ASK=false                                 \n"
    "       ;;                                           \n"
    "     x)                                             \n"
    "       DO_REMOVE=false                              \n"
    "       ;;                                           \n"
    "  esac                                              \n"
    "done                                                \n"
    "                                                    \n"
    "if [ -z $DO_ASK ]                                   \n"
    "then                                                \n"
    "  usage                                             \n"
    "  ask                                               \n"
    "fi                                                  \n"
    "                                                    \n"
    "user='%s'                                           \n"
    "group='%s'                                          \n"
    ;

static const char *SH_SCRIPT_TEMPLATE_FOOT =
    "                      \n"
    "if [ -z $DO_REMOVE ]  \n"
    "then                  \n"
    "  %s '%s';            \n"
    "fi                    \n"
    ;

static void rm_fmt_head(RmSession *session, RmFmtHandler *parent, FILE *out) {
    RmFmtHandlerShScript *self = (RmFmtHandlerShScript *)parent;

    self->opt_symlinks_only = rm_fmt_get_config_value(session->formats, "sh", "symlinks_only");
    self->opt_use_ln = rm_fmt_get_config_value(session->formats, "sh", "use_ln");

    if(fchmod(fileno(out), S_IRUSR | S_IWUSR | S_IXUSR) == -1) {
        rm_log_perror("Could not chmod +x sh script");
    }

    fprintf(
        out, SH_SCRIPT_TEMPLATE_HEAD,
        session->settings->iwd,
        (session->settings->joined_argv) ? (session->settings->joined_argv) : "[unknown]",
        rm_util_get_username(),
        rm_util_get_groupname()
    );
}

static char *rm_fmt_sh_escape_path(char *path) {
    return rm_util_strsub(path, "'", "'\"'\"'");
}

static void rm_fmt_elem(_U RmSession *session, _U RmFmtHandler *parent, FILE *out, RmFile *file) {
    RmFmtHandlerShScript *self = (RmFmtHandlerShScript *)parent;

    /* See http://stackoverflow.com/questions/1250079/bash-escaping-single-quotes-inside-of-single-quoted-strings
     * for more info on this
     * */
    char *dupe_path = rm_fmt_sh_escape_path(file->path);

    switch(file->lint_type) {
    case RM_LINT_TYPE_BLNK:
        fprintf(out, "rm -f '%s' # bad symlink pointing nowhere\n", dupe_path);
        break;
    case RM_LINT_TYPE_EDIR:
        fprintf(out, "rmdir '%s' # empty folder\n", dupe_path);
        break;
    case RM_LINT_TYPE_NBIN:
        fprintf(out, "strip --strip-debug '%s' # binary with debugsymbols\n", dupe_path);
        break;
    case RM_LINT_TYPE_BADUID:
        fprintf(out, "%s '%s' # bad uid\n", "chown \"$user\"", dupe_path);
        break;
    case RM_LINT_TYPE_BADGID:
        fprintf(out, "%s '%s' # bad gid\n", "chgrp \"$group\"", dupe_path);
        break;
    case RM_LINT_TYPE_BADUGID:
        fprintf(out, "%s '%s' # bad gid and uid\n", "chown \"$user\":\"$group\"", dupe_path);
        break;
    case RM_LINT_TYPE_EFILE:
        fprintf(out, "rm -f '%s' # empty file\n", dupe_path);
        break;
    case RM_LINT_TYPE_DUPE_DIR_CANDIDATE:
        if(file->is_original) {
            fprintf(out, "echo   '%s' # original directory\n", dupe_path);
        } else {
            fprintf(out, "rm -rf '%s' # duplicate directory\n", dupe_path);
        }
        break;
    case RM_LINT_TYPE_DUPE_CANDIDATE:
        if(file->is_original) {
            fprintf(out, "echo  '%s' # original\n", dupe_path);
            self->last_original = file;
        } else {
            if(self->opt_use_ln) {
                bool use_hardlink = false;
                if(self->last_original->dev == file->dev) {
                    use_hardlink = !self->opt_symlinks_only;
                }

                char *orig_path = rm_fmt_sh_escape_path(self->last_original->path);
                fprintf(
                    out, "rm -f '%s' && ln %s '%s' '%s' # duplicate\n",
                    dupe_path, (use_hardlink) ? "" : "-s" , orig_path, dupe_path
                );
                g_free(orig_path);
            } else {
                fprintf(out, "rm -f '%s' # duplicate\n", dupe_path);
            }
        }
        break;
    default:
        rm_log_warning("Warning: unknown type in encountered: %d\n", file->lint_type);
        break;
    }

    g_free(dupe_path);
}

static void rm_fmt_foot(_U RmSession *session, RmFmtHandler *parent, FILE *out) {
    if(0
            || strcmp(parent->path, "stdout") == 0
            || strcmp(parent->path, "stderr") == 0
            || strcmp(parent->path, "stdin") == 0
      ) {
        /* You will have a hard time deleting standard streams. */
        return;
    }

    fprintf(
        out, SH_SCRIPT_TEMPLATE_FOOT,
        "rm -f", parent->path
    );
}

static RmFmtHandlerShScript SH_SCRIPT_HANDLER_IMPL = {
    .parent = {
        .size = sizeof(SH_SCRIPT_HANDLER_IMPL),
        .name = "sh",
        .head = rm_fmt_head,
        .elem = rm_fmt_elem,
        .prog = NULL,
        .foot = rm_fmt_foot
    },
    .last_original = NULL
};

RmFmtHandler *SH_SCRIPT_HANDLER = (RmFmtHandler *) &SH_SCRIPT_HANDLER_IMPL;
