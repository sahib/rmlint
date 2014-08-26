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

static void rm_fmt_head(RmSession *session, G_GNUC_UNUSED RmFmtHandler *parent, FILE *out) {
    if(fchmod(fileno(out), S_IRUSR | S_IWUSR | S_IXUSR) == -1) {
        rm_perror("Could not chmod +x sh script");
    }

    char *joined_argv = NULL;
    const char *argv_nul[session->settings->argc + 1];
    memset(argv_nul, 0, sizeof(argv_nul));
    memcpy(argv_nul, session->settings->argv, session->settings->argc * sizeof(char *));
    joined_argv = g_strjoinv(" ", (gchar **)argv_nul);

    fprintf(
        out, SH_SCRIPT_TEMPLATE_HEAD,
        session->settings->iwd,
        (joined_argv) ? (joined_argv) : "[unknown]",
        rm_util_get_username(),
        rm_util_get_groupname()
    );

    g_free(joined_argv);
}

static void rm_fmt_elem(RmSession *session, G_GNUC_UNUSED RmFmtHandler *parent, FILE *out, RmFile *file) {
    /* See http://stackoverflow.com/questions/1250079/bash-escaping-single-quotes-inside-of-single-quoted-strings
     * for more info on this
     * */
    char *fpath = rm_util_strsub(file->path, "'", "'\"'\"'");

    switch(file->lint_type) {
    case RM_LINT_TYPE_BLNK:
        fprintf(out, "rm -f '%s' # bad symlink pointing nowhere\n", fpath);
        break;
    case RM_LINT_TYPE_BASE:
        fprintf(out, "echo  '%s' # double basename\n", fpath);
        break;
    case RM_LINT_TYPE_EDIR:
        fprintf(out, "rmdir '%s' # empty folder\n", fpath);
        break;
    case RM_LINT_TYPE_NBIN:
        fprintf(out, "strip --strip-debug '%s' # binary with debugsymbols\n", fpath);
        break;
    case RM_LINT_TYPE_BADUID:
        fprintf(out, "%s '%s' # bad uid\n", "chown \"$user\"", fpath);
        break;
    case RM_LINT_TYPE_BADGID:
        fprintf(out, "%s '%s' # bad gid\n", "chgrp \"$group\"", fpath);
        break;
    case RM_LINT_TYPE_BADUGID:
        fprintf(out, "%s '%s' # bad gid and uid\n", "chown \"$user\":\"$group\"", fpath);
        break;
    case RM_LINT_TYPE_EFILE:
        fprintf(out, "rm -f '%s' # empty file\n", fpath);
        break;
    case RM_LINT_TYPE_DUPE_CANDIDATE:
        if(rm_file_tables_is_original(session->tables, file)) {
            fprintf(out, "echo  '%s' # original\n", fpath);
        } else {
            fprintf(out, "rm -f '%s' # duplicate\n", fpath);
        }
        break;
    default:
        g_printerr("Warning: unknown type in write_to_log %d\n", file->lint_type);
        break;
    }

    g_free(fpath);
}

static void rm_fmt_foot(G_GNUC_UNUSED RmSession *session, RmFmtHandler *parent, FILE *out) {
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
    }
};

RmFmtHandler *SH_SCRIPT_HANDLER = (RmFmtHandler *) &SH_SCRIPT_HANDLER_IMPL;
