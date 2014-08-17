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

#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>

static GMutex PRINT_MUTEX;

/* Sort criteria for sorting by preferred path (first) then user-input criteria */
long cmp_orig_criteria(RmFile *a, RmFile *b, gpointer user_data) {
    RmSession *session = user_data;
    RmSettings *sets = session->settings;

    if (a->in_ppath != b->in_ppath) {
        return a->in_ppath - b->in_ppath;
    } else {
        int sort_criteria_len = strlen(sets->sort_criteria);
        for (int i = 0; i < sort_criteria_len; i++) {
            long cmp = 0;
            switch (sets->sort_criteria[i]) {
            case 'm':
                cmp = (long)(a->mtime) - (long)(b->mtime);
                break;
            case 'M':
                cmp = (long)(b->mtime) - (long)(a->mtime);
                break;
            case 'a':
                cmp = strcmp (rm_util_basename(a->path), rm_util_basename (b->path));
                break;
            case 'A':
                cmp = strcmp (rm_util_basename(b->path), rm_util_basename (a->path));
                break;
            case 'p':
                cmp = (long)a->path_index - (long)b->path_index;
                break;
            case 'P':
                cmp = (long)b->path_index - (long)a->path_index;
                break;
            }
            if (cmp) {
                return cmp;
            }
        }
    }
    return 0;
}

/* Simple wrapper around unlink() syscall */
static void remfile(const char *r_path) {
    if(r_path) {
        if(unlink(r_path) == -1) {
            rm_perror(YEL"WARN:"NCO" unlink():");
        }
    }
}

void log_print(RmSession *session, FILE *stream, const char *string) {
    if(stream && string) {
        if(session->settings->verbosity == 4) {
            fprintf(stdout, "%s", string);
        }
        fprintf(stream, "%s", string);
    }
}

static char *make_cmd_ready(RmSettings *sets, bool is_orig, const char *orig, const char *dupl) {
    char *repl_orig = NULL;
    if(!is_orig) {
        repl_orig = rm_util_strsub(sets->cmd_path, CMD_ORIG, orig);
    } else {
        repl_orig = rm_util_strsub(sets->cmd_orig, CMD_ORIG, orig);
    }

    if(repl_orig != NULL && !is_orig) {
        char *repl_dups = rm_util_strsub(repl_orig, CMD_DUPL, dupl);
        if(repl_dups != NULL) {
            free(repl_orig);
            repl_orig = repl_dups;
        }
    }
    return repl_orig;
}

void script_print(RmSession *session, char *string) {
    if(string != NULL) {
        if(session->settings->verbosity == 5) {
            fprintf(stdout, "%s", string);
        }
        fprintf(session->script_out, "%s", string);
        free(string);
    }
}

#define _sd_ g_strdup_printf

void write_to_log(RmSession *session, const RmFile *file, bool orig, const RmFile *p_to_orig) {
    bool free_fullpath = true;
    RmSettings *sets = session->settings;

    const char *chown_cmd_baduid = "chown \"$user\"";
    const char *chown_cmd_badgid = "chgrp \"$group\"";
    const char *chown_cmd_badugid = "chown \"$user\":\"$group\"";

    if(session->log_out && session->script_out && sets->output_log) {
        char *fpath = realpath(file->path, NULL);
        if(!fpath) {
            if(file->lint_type != RM_LINT_TYPE_BLNK) {
                rm_error(YEL"WARN: "NCO"Unable to get full path [of %s] ", file->path);
                rm_perror("(write_to_log():mode.c)");
            }
            free_fullpath = false;
            fpath = (char *)file->path;
        } else {
            /* This is so scary. */
            /* See http://stackoverflow.com/questions/1250079/bash-escaping-single-quotes-inside-of-single-quoted-strings
             * for more info on this */
            char *tmp_copy = fpath;
            fpath = rm_util_strsub(fpath, "'", "'\"'\"'");
            free(tmp_copy);
        }
        if(file->lint_type == RM_LINT_TYPE_BLNK) {
            script_print(session, _sd_("rm -f '%s' # bad link pointing nowhere.\n", fpath));
            log_print(session, session->log_out, "BLNK");
        } else if(file->lint_type == RM_LINT_TYPE_BASE) {
            log_print(session, session->log_out, "BASE");
            script_print(session, _sd_("echo  '%s' # double basename.\n", fpath));
        } else if(file->lint_type == RM_LINT_TYPE_EDIR) {
            script_print(session, _sd_("rmdir '%s' # empty folder.\n", fpath));
            log_print(session, session->log_out, "EDIR");
        } else if(file->lint_type == RM_LINT_TYPE_NBIN) {
            script_print(session, _sd_("strip --strip-debug '%s' # binary with debugsymbols.\n", fpath));
            log_print(session, session->log_out, "NBIN");
        } else if(file->lint_type == RM_LINT_TYPE_BADUID) {
            script_print(session, _sd_("%s '%s' # bad uid\n", chown_cmd_baduid, fpath));
            log_print(session, session->log_out, "BUID");
        } else if(file->lint_type == RM_LINT_TYPE_BADGID) {
            script_print(session, _sd_("%s '%s' # bad gid\n", chown_cmd_badgid, fpath));
            log_print(session, session->log_out, "BGID");
        } else if(file->lint_type == RM_LINT_TYPE_BADUGID) {
            script_print(session, _sd_("%s '%s' # bad gid and uid\n", chown_cmd_badugid, fpath));
            log_print(session, session->log_out, "BGID");
        } else if(file->file_size == 0) {
            script_print(session, _sd_("rm -f '%s' # empty file.\n", fpath));
            log_print(session, session->log_out, "ZERO");
        } else if(orig == false) {
            log_print(session, session->log_out, "DUPL");
            if(sets->cmd_path) {
                char *opath = realpath(p_to_orig->path, NULL);
                if(opath != NULL) {
                    /*script_print(sets, _sd_(set->cmd_path,fpath,opath));*/
                    char *tmp_opath = rm_util_strsub(opath, "'", "'\"'\"'");
                    if(tmp_opath != NULL) {
                        script_print(session, make_cmd_ready(sets, false, tmp_opath, fpath));
                        script_print(session, _sd_("\n"));
                        g_free(tmp_opath);
                    }
                    g_free(opath);
                }
            } else {
                script_print(session, _sd_("rm -f '%s' # duplicate\n", fpath));
            }
        } else {
            log_print(session, session->log_out, "ORIG");
            if(sets->cmd_orig) {
                /*//script_print(sets, _sd_(set->cmd_orig,fpath));*/
                script_print(session, make_cmd_ready(sets, true, fpath, NULL));
                script_print(session, _sd_(" \n"));
            } else {
                script_print(session, _sd_("echo  '%s' # original\n", fpath));
            }
        }
#define INT_CAST long unsigned
        if(sets->verbosity == 4) {
            fprintf(stdout, "%s%s%s%lu%s%lu%s%lu\n", LOGSEP, fpath, LOGSEP, (INT_CAST)file->file_size, LOGSEP, (INT_CAST)file->dev, LOGSEP, (INT_CAST)file->node);
        }
        fprintf(session->log_out, "%s%s%s%lu%s%lu%s%lu\n", LOGSEP, fpath, LOGSEP, (INT_CAST)file->file_size, LOGSEP, (INT_CAST)file->dev, LOGSEP, (INT_CAST)file->node);
#undef INT_CAST
        if(free_fullpath && fpath && file->lint_type != RM_LINT_TYPE_BLNK) {
            g_free(fpath);
        }
    }
}

#undef _sd_

static bool handle_item(RmSession *session, RmFile *file_path, RmFile *file_orig) {
    char *path = (file_path) ? file_path->path : NULL;
    char *orig = (file_orig) ? file_orig->path : NULL;
    RmSettings *sets = session->settings;

    /* What set->mode are we in? */
    switch(sets->mode) {
    case RM_MODE_LIST:
        break;
    case RM_MODE_NOASK: {
        /* Just remove it */
        if(path == NULL) {
            break;
        }
        warning(RED"   rm -rf "NCO"\"%s\"\n", path);
        remfile(path);
    }
    break;
    case RM_MODE_LINK: {
        /* Replace the file with a neat symlink */
        if(path == NULL) {
            break;
        } else {
            char *full_orig_path = realpath(orig, NULL);
            if(full_orig_path) {
                char *full_dupl_path = realpath(path, NULL);
                if(full_dupl_path) {
                    rm_error(NCO"   ln -s "NCO"\"%s\" "NCO"\"%s\"\n", full_orig_path, full_dupl_path);
                    remfile(full_dupl_path);
                    if(symlink(full_orig_path , full_dupl_path) == -1) {
                        rm_perror(YEL"WARN: "NCO"symlink:");
                    }
                    free(full_dupl_path);
                }
                free(full_orig_path);
            }
        }
    }
    break;
    case RM_MODE_CMD: {
        /* Exec a command on it */
        char *tmp_opath = rm_util_strsub(orig, "'", "'\"'\"'");
        char *tmp_fpath = rm_util_strsub(path, "'", "'\"'\"'");
        if(tmp_opath && tmp_fpath) {
            char *cmd = NULL;
            if(path) {
                cmd = make_cmd_ready(sets, false, tmp_opath, tmp_fpath);
            } else {
                cmd = make_cmd_ready(sets, true, tmp_opath, NULL);
            }
            if(cmd != NULL) {
                int rc = system(cmd);
                g_free(cmd);
                if(WIFSIGNALED(rc) && (WTERMSIG(rc) == SIGINT || WTERMSIG(rc) == SIGQUIT)) {
                    return true;
                }
            }
            free(tmp_opath);
            free(tmp_fpath);
        }
    }
    break;
    default:
        rm_error(RED"ERROR: "NCO"Invalid set->mode. This is a program error :(");
        return true;
    }
    return false;
}

/* ------------------------------------------------------------- */

void init_filehandler(RmSession *session) {
    if(session->settings->output_script) {
        session->script_out = fopen(session->settings->output_script, "w");
    }

    if(session->settings->output_log) {
        session->log_out = fopen(session->settings->output_log, "w");
    }

    char *cwd = getcwd(NULL, 0);

    if(session->script_out) {
        /* Make the file executable */
        if(fchmod(fileno(session->script_out), S_IRUSR | S_IWUSR | S_IXUSR) == -1) {
            rm_perror(YEL"WARN: "NCO"chmod");
        }
        /* Write a basic header */
        fprintf(session->script_out,
                "#!/bin/sh\n"
                "#This file was autowritten by 'rmlint'\n"
                "#rmlint was executed from: %s\n"
                "\n"
                "ask() {\n"
                "cat << EOF\n"
                "This script will delete certain files rmlint found.\n"
                "It is highly advisable to view the script (or log) first!\n"
                "\n"
                "Execute this script with -d to disable this message\n"
                "Hit enter to continue; CTRL-C to abort immediately\n"
                "EOF\n"
                "read dummy_var\n"
                "}\n"
                "\n"
                "usage()\n"
                "{\n"
                "cat << EOF\n"
                "usage: $0 options\n"
                "\n"
                "OPTIONS:\n", cwd);
        fprintf(session->script_out,
                "-h      Show this message\n"
                "-d      Do not ask before running\n"
                "-x      Keep rmlint.sh and rmlint.log\n"
                "EOF\n"
                "}\n"
                "\n"
                "DO_REMOVE=\n"
                "DO_ASK=\n"
                "\n"
                "while getopts “dhx” OPTION\n"
                "do\n"
                "  case $OPTION in\n"
                "     h)\n"
                "       usage\n"
                "       exit 1\n"
                "       ;;\n"
                "     d)\n"
                "       DO_ASK=false\n"
                "       ;;\n"
                "     x)\n"
                "       DO_REMOVE=false\n"
                "       ;;\n"
                "  esac\n"
                "done\n"
                "\n"
                "if [ -z $DO_ASK ]\n"
                "then\n"
                "  usage\n"
                "  ask \n"
                "fi\n");
        fprintf(session->script_out, "user='%s'\ngroup='%s'\n", rm_util_get_username(), rm_util_get_groupname());
    }

    if(session->log_out) {
        fprintf(session->log_out, "# This file was autowritten by 'rmlint'\n");
        fprintf(session->log_out, "# rmlint was executed from: %s\n", cwd);
        fprintf(session->log_out, "# \n# Entries are listed like this: \n");
        fprintf(session->log_out, "#  lint_type | md5sum | path | size | devID | inode\n");
        fprintf(session->log_out, "#  -------------------------------------------\n");
        fprintf(session->log_out, "#  lint_type : What type of lint found:\n");
        fprintf(session->log_out, "#            BLNK: Bad link pointing nowhere\n"
                "#           BASE: Double basename\n"
                "#           EDIR: Empty directory\n");
        fprintf(session->log_out,
                "#           BLNK: Bad link pointing nowhere\n"
                "#           ZERO: Empty file\n"
                "#           NBIN: Nonstripped binary\n"
                "#           BGID: File with Bad GroupID\n"
                "#           BUID: File with Bad UserID\n"
                "#           ORIG: File that has a duplicate, but supposed to be a original\n"
                "#           DUPL: File that is supposed to be a duplicate\n"
                "#\n");
        fprintf(session->log_out, "# path    : The full path to the found file\n");
        fprintf(session->log_out, "# size    : total size in byte as a decimal integer\n");
        fprintf(session->log_out, "# devID   : The ID of the device where the file is located\n");
        fprintf(session->log_out, "# inode   : The Inode of the file (see man 2 stat)\n");
        fprintf(session->log_out, "# The '//' inbetween each word is the seperator.\n");

        g_free(cwd);
    } else {
        rm_perror("error during log or script file");
    }
}

bool process_island(RmSession *session, GQueue *group) {
    /* This does the final processing on a dupe group. All required   */
    /* comparisons have been done (including paranoid if required) so */
    /* now it's just a matter of deciding which originals to keep and */
    /* which files to delete.                                         */

    /* If a specific directory is specified to have the 'originals':  */
    /* Find the (first) element being in this directory and swap it with the first */
    /* --> First one is the original otherwise */
    bool return_val = false;

    RmSettings *sets = session->settings;
    GList *i = group->head;
    bool tagged_original = false;
    RmFile *original = NULL;

    GHashTable *orig_table = g_hash_table_new(NULL, NULL);

    while(i) {
        RmFile *fi = i->data;
        if (
            ((fi->in_ppath) && (sets->keep_all_originals)) ||
            ((fi->in_ppath) && (!tagged_original))
        ) {
            g_hash_table_insert(orig_table, fi, GUINT_TO_POINTER(1));
            if (!tagged_original) {
                tagged_original = true;
                original = fi;
            }
        }
        i = i->next;
    }
    if (!tagged_original) {
        /* tag first file as the original*/
        original = group->head->data;
        g_hash_table_insert(orig_table, original, GUINT_TO_POINTER(1));
    }

    g_mutex_lock(&PRINT_MUTEX);

    /* Now do the actual printout.. */
    i = group->head;
    while(i) {
        RmFile *fi = i->data;
        if(g_hash_table_contains(orig_table, fi)) {
            /* original(s) of a duplicate set*/
            if(0
                    || sets->mode == RM_MODE_LIST
                    || sets->mode == RM_MODE_LINK
                    || (sets->mode == RM_MODE_CMD && sets->cmd_orig == NULL && sets->cmd_path == NULL)
              ) {
                if(sets->mode != RM_MODE_LINK) {
                    rm_error(GRE"   ls "NCO"%s\n", fi->path);
                }
            }
            write_to_log(session, fi, true, NULL);
            handle_item(session, NULL, fi);
        }
        i = i->next;
    }

    i = group->head;
    while(i) {
        RmFile *fi = i->data;
        if(!g_hash_table_contains(orig_table, fi)) {
            /* duplicates(s) of a duplicate sets*/
            if(0
                    || sets->mode == RM_MODE_LIST
                    || (sets->mode == RM_MODE_CMD && !sets->cmd_orig && !sets->cmd_path)
              ) {
                if(sets->paranoid) {
                    /* If byte by byte was succesful print a blue "x" */
                    warning(BLU"   %-1s "NCO, "rm");
                } else {
                    warning(YEL"   %-1s "NCO, "rm");
                }
                rm_error("%s\n", fi->path);
            }
            write_to_log(session, fi, false, original);
            session->dup_counter++;
            session->total_lint_size += fi->file_size;
            if(handle_item(session, fi, original)) {
                return_val = true;
                break;
            }
        }
        i = i->next;
    }

    g_mutex_unlock(&PRINT_MUTEX);

    g_hash_table_unref(orig_table);

    return return_val;
}
