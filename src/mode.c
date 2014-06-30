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
** Author: Christopher Pahl <sahib@online.de>:
** Hosted on http://github.com/sahib/rmlint
*
**/

/*
  mode.c:
  1) log routines
  2) finding double checksums
  3) implementation of different modes
*/

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdlib.h>

#include "rmlint.h"
#include "mode.h"
#include "md5.h"
#include "list.h"
#include "filter.h"
#include "useridcheck.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>

pthread_mutex_t mutex_printage =  PTHREAD_MUTEX_INITIALIZER;

gchar * strsubs(const char * string, const char * subs, const char * with) {
    gchar * result = NULL;
    if (string != NULL && string[0] != '\0') {
        gchar ** split = g_strsplit (string,subs,0);
        if (split != NULL) {
            result = g_strjoinv (with,split);
        }
        g_strfreev (split);
    }
    return result;
}

/* ------------------------------------------------------------- */

/* Simple wrapper around unlink() syscall */
static void remfile(const char *r_path) {
    if(r_path) {
        if(unlink(r_path) == -1) {
            perror(YEL"WARN:"NCO" unlink():");
        }
    }
}

/* ------------------------------------------------------------- */

void log_print(RmSession *session, FILE * stream, const char * string) {
    if(stream && string) {
        if(session->settings->verbosity == 4) {
            fprintf(stdout,"%s",string);
        }
        fprintf(stream,"%s",string);
    }
}

/* ------------------------------------------------------------- */

static char * make_cmd_ready(RmSettings * sets, bool is_orig, const char * orig, const char * dupl) {
    char * repl_orig = NULL;
    if(!is_orig) {
        repl_orig = strsubs(sets->cmd_path,CMD_ORIG,orig);
    } else {
        repl_orig = strsubs(sets->cmd_orig,CMD_ORIG,orig);
    }

    if(repl_orig != NULL && !is_orig) {
        char * repl_dups = strsubs(repl_orig,CMD_DUPL,dupl);
        if(repl_dups != NULL) {
            free(repl_orig);
            repl_orig = repl_dups;
        }
    }
    return repl_orig;
}

/* ------------------------------------------------------------- */

void script_print(RmSession * session, char * string) {
    if(string != NULL) {
        if(session->settings->verbosity == 5) {
            fprintf(stdout,"%s",string);
        }
        fprintf(session->script_out, "%s",string);
        free(string);
    }
}

/* ------------------------------------------------------------- */

#define _sd_ g_strdup_printf

/* ------------------------------------------------------------- */

void write_to_log(RmSession * session, const RmFile *file, bool orig, const RmFile * p_to_orig) {
    bool free_fullpath = true;
    RmSettings *sets = session->settings;

    const char * chown_cmd_baduid = "chown \"$user\"";
    const char * chown_cmd_badgid = "chgrp \"$group\"";
    const char * chown_cmd_badugid = "chown \"$user\":\"$group\"";

    if(session->log_out && session->script_out && sets->output) {
        int i = 0;
        char *fpath = realpath(file->path, NULL);
        if(!fpath) {
            if(file->dupflag != TYPE_BLNK) {
                error(YEL"WARN: "NCO"Unable to get full path [of %s] ", file->path);
                perror("(write_to_log():mode.c)");
            }
            free_fullpath = false;
            fpath = (char*)file->path;
        } else {
            /* This is so scary. */
            /* See http://stackoverflow.com/questions/1250079/bash-escaping-single-quotes-inside-of-single-quoted-strings for more info on this */
            char *tmp_copy = fpath;
            fpath = strsubs(fpath,"'","'\"'\"'");
            free(tmp_copy);
        }
        if(file->dupflag == TYPE_BLNK) {
            script_print(session, _sd_("rm -f '%s' # bad link pointing nowhere.\n", fpath));
            log_print(session, session->log_out,"BLNK");
        } else if(file->dupflag == TYPE_BASE) {
            log_print(session, session->log_out,"BASE");
            script_print(session, _sd_("echo  '%s' # double basename.\n", fpath));
        } else if(file->dupflag == TYPE_EDIR) {
            script_print(session, _sd_("rmdir '%s' # empty folder.\n", fpath));
            log_print(session, session->log_out,"EDIR");
        } else if(file->dupflag == TYPE_NBIN) {
            script_print(session, _sd_("strip --strip-debug '%s' # binary with debugsymbols.\n", fpath));
            log_print(session, session->log_out,"NBIN");
        } else if(file->dupflag == TYPE_BADUID) {
            script_print(session, _sd_("%s '%s' # bad uid\n",chown_cmd_baduid, fpath));
            log_print(session, session->log_out,"BUID");
        } else if(file->dupflag == TYPE_BADGID) {
            script_print(session, _sd_("%s '%s' # bad gid\n",chown_cmd_badgid, fpath));
            log_print(session, session->log_out,"BGID");
        } else if(file->dupflag == TYPE_BADUGID) {
            script_print(session, _sd_("%s '%s' # bad gid and uid\n",chown_cmd_badugid, fpath));
            log_print(session, session->log_out,"BGID");
        } else if(file->fsize == 0) {
            script_print(session, _sd_("rm -f '%s' # empty file.\n", fpath));
            log_print(session, session->log_out,"ZERO");
        } else if(orig==false) {
            log_print(session, session->log_out,"DUPL");
            if(sets->cmd_path) {
                char *opath = realpath(p_to_orig->path, NULL);
                if(opath != NULL) {
                    /*script_print(sets, _sd_(set->cmd_path,fpath,opath));*/
                    char * tmp_opath = strsubs(opath,"'","'\"'\"'");
                    if(tmp_opath != NULL) {
                        script_print(session, make_cmd_ready(sets, false,tmp_opath,fpath));
                        script_print(session, _sd_("\n"));
                        g_free(tmp_opath);
                    }
                    g_free(opath);
                }
            } else {
                script_print(session, _sd_("rm -f '%s' # duplicate\n",fpath));
            }
        } else {
            log_print(session, session->log_out,"ORIG");
            if(sets->cmd_orig) {
                /*//script_print(sets, _sd_(set->cmd_orig,fpath));*/
                script_print(session, make_cmd_ready(sets, true,fpath,NULL));
                script_print(session, _sd_(" \n"));
            } else {
                script_print(session, _sd_("echo  '%s' # original\n",fpath));
            }
        }
        log_print(session, session->log_out,LOGSEP);
        for(i = 0; i < 16; i++) {
            if(sets->verbosity == 4) {
                fprintf(stdout,"%02x", file->md5_digest[i]);
            }
            fprintf(session->log_out,"%02x", file->md5_digest[i]);
        }
#define INT_CAST long unsigned
        if(sets->verbosity == 4) {
            fprintf(stdout,"%s%s%s%lu%s%lu%s%lu%s\n", LOGSEP,fpath, LOGSEP, (INT_CAST)file->fsize, LOGSEP, (INT_CAST)file->dev, LOGSEP, (INT_CAST)file->node,LOGSEP);
        }
        fprintf(session->log_out,"%s%s%s%lu%s%lu%s%lu%s\n", LOGSEP,fpath, LOGSEP, (INT_CAST)file->fsize, LOGSEP, (INT_CAST)file->dev, LOGSEP, (INT_CAST)file->node,LOGSEP);
#undef INT_CAST
        if(free_fullpath && fpath && file->dupflag != TYPE_BLNK) {
            g_free(fpath);
        }
    }
}

#undef _sd_

static bool handle_item(RmSession * session, RmFile *file_path, RmFile *file_orig) {
    char *path = (file_path) ? file_path->path : NULL;
    char *orig = (file_orig) ? file_orig->path : NULL;
    RmSettings *sets = session->settings;

    /* What set->mode are we in? */
    switch(sets->mode) {
    case 1:
    case 2:
        break;
    case 3: {
        /* Just remove it */
        if(path == NULL) {
            break;
        }
        warning(RED"   rm -rf "NCO"\"%s\"\n", path);
        remfile(path);
    }
    break;
    case 4: {
        /* Replace the file with a neat symlink */
        if(path == NULL) {
            break;
        } else {
            char * full_orig_path = realpath(orig, NULL);
            if(full_orig_path) {
                char * full_dupl_path = realpath(path, NULL);
                if(full_dupl_path) {
                    error(NCO"   ln -s "NCO"\"%s\" "NCO"\"%s\"\n", full_orig_path, full_dupl_path);
                    remfile(full_dupl_path);
                    if(symlink(full_orig_path ,full_dupl_path) == -1) {
                        perror(YEL"WARN: "NCO"symlink:");
                    }
                    free(full_dupl_path);
                }
                free(full_orig_path);
            }
        }
    }
    break;
    case 5: {
        /* Exec a command on it */
        int ret = 0;
        char * tmp_opath = strsubs(orig,"'","'\"'\"'");
        char * tmp_fpath = strsubs(path,"'","'\"'\"'");
        if(tmp_opath && tmp_fpath) {
            const char * cmd = NULL;
            if(path) {
                cmd = make_cmd_ready(sets, false,tmp_opath,tmp_fpath);
            } else {
                cmd = make_cmd_ready(sets, true,tmp_opath,NULL);
            }
            if(cmd != NULL) {
                ret = system(cmd);
                if(WIFSIGNALED(ret) && (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT)) {
                    return true;
                }
                free((char*)cmd);
                cmd = NULL;
            }
            free(tmp_opath);
            free(tmp_fpath);
        }
    }
    break;
    default:
        error(RED"ERROR: "NCO"Invalid set->mode. This is a program error :(");
        return true;
    }
    return false;
}

/* ------------------------------------------------------------- */

void init_filehandler(RmSession * session) {
    if(session->settings->output) {
        char *sc_name = g_strdup_printf("%s.sh", session->settings->output);
        char *lg_name = g_strdup_printf("%s.log",session->settings->output);
        session->script_out = fopen(sc_name, "w");
        session->log_out    = fopen(lg_name, "w");
        if(session->script_out && session->log_out) {
            char *cwd = getcwd(NULL,0);
            /* Make the file executable */
            if(fchmod(fileno(session->script_out), S_IRUSR|S_IWUSR|S_IXUSR) == -1) {
                perror(YEL"WARN: "NCO"chmod");
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
                    "OPTIONS:\n",cwd);
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
            fprintf(session->script_out, "user='%s'\ngroup='%s'\n",get_username(),get_groupname());
            fprintf(session->log_out,"#This file was autowritten by 'rmlint'\n");
            fprintf(session->log_out,"#rmlint was executed from: %s\n",cwd);
            fprintf(session->log_out, "#\n# Entries are listed like this: \n");
            fprintf(session->log_out, "# dupflag | md5sum | path | size | devID | inode\n");
            fprintf(session->log_out, "# -------------------------------------------\n");
            fprintf(session->log_out, "# dupflag : What type of lint found:\n");
            fprintf(session->log_out, "#           BLNK: Bad link pointing nowhere\n"
                    "#           OTMP: Old tmp data (e.g: test.txt~)\n"
                    "#           BASE: Double basename\n"
                    "#           EDIR: Empty directory\n");
            fprintf(session->log_out,
                    "#           BLNK: Bad link pointing nowhere\n"
                    "#           JNKD: Dirname containg one char of a user defined string\n"
                    "#           JNKF: Filename containg one char of a user defined string\n"
                    "#           ZERO: Empty file\n"
                    "#           NBIN: Nonstripped binary\n"
                    "#           BGID: File with Bad GroupID\n"
                    "#           BUID: File with Bad UserID\n"
                    "#           ORIG: File that has a duplicate, but supposed to be a original\n"
                    "#           DUPL: File that is supposed to be a duplicate\n"
                    "#\n");
            fprintf(session->log_out, "# md5sum  : The md5-checksum of the file (not equal with output of `md5sum`, because only parts are read!)\n");
            fprintf(session->log_out, "# path    : The full path to the found file\n");
            fprintf(session->log_out, "# size    : total size in byte as a decimal integer\n");
            fprintf(session->log_out, "# devID   : The ID of the device where the file is located\n");
            fprintf(session->log_out, "# inode   : The Inode of the file (see man 2 stat)\n");
            fprintf(session->log_out, "# The '//' inbetween each word is the seperator.\n");
            if(cwd) {
                free(cwd);
            }
        } else {
            perror(NULL);
        }
        fflush(session->script_out);
        fflush(session->log_out);
        g_free(sc_name);
        g_free(lg_name);
    }
}

/* ------------------------------------------------------------- */

#define NOT_DUP_FLAGGED(ptr) !(ptr->dupflag <= false)
bool print_newline = true;

bool process_doop_groop(RmSession *session, GQueue *group) {
    /* This does the final processing on a dupe group. All required   */
    /* comparisons have been done (including paranoid if required) so */
    /* now it's just a matter of deciding which originals to keep and */
    /* which files to delete.                                         */

    /* If a specific directory is specified to have the 'originals':  */
    /* Find the (first) element being in this directory and swap it with the first */
    /* --> First one is the original otherwise */

    RmSettings * sets = session->settings;
    GList *i = group->head;
    bool tagged_original = false;
    RmFile *original = NULL;

    while(i) {
        RmFile *fi = i->data;
        if (
            ((fi->in_ppath) && (sets->keep_all_originals)) ||
            ((fi->in_ppath) && (!tagged_original))
        ) {
            fi->filter = false;
            if (!tagged_original) {
                tagged_original = true;
                original = fi;
            }
        } else {
            /*tag as duplicate*/
            fi->filter = true;
        }
        i = i->next;
    }
    if (!tagged_original) {
        /* tag first file as the original*/
        original = group->head->data;
        original->filter = false;
    }

    /* Make sure no group is printed / logged at the same time (== chaos) */
    pthread_mutex_lock(&mutex_printage);

    /* Now do the actual printout.. */
    i = group->head;
    while(i) {
        RmFile *fi = i->data;
        if(!fi->filter) {
            /* original(s) of a duplicate set*/
            if((sets->mode == 1 || sets->mode == 4 || (sets->mode == 5 && sets->cmd_orig == NULL && sets->cmd_path == NULL)) && sets->verbosity > 1) {
                if(print_newline) {
                    warning("\n");
                    print_newline = false;
                }
                if(sets->mode != 4) {
                    error(GRE"   ls "NCO"%s\n",fi->path);
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
        if(fi->filter) {
            /* duplicates(s) of a duplicate sets*/
            if(sets->mode == 1 || (sets->mode == 5 && !sets->cmd_orig && !sets->cmd_path)) {
                if(sets->paranoid) {
                    /* If byte by byte was succesful print a blue "x" */
                    warning(BLU"   %-1s "NCO,"rm");
                } else {
                    warning(YEL"   %-1s "NCO,"rm");
                }
                if(sets->verbosity > 1) {
                    error("%s\n",fi->path);
                } else {
                    error("   rm %s\n",fi->path);
                }
            }
            write_to_log(session, fi, false, original);
            session->dup_counter++;
            session->total_lint_size += fi->fsize;
            if(handle_item(session, fi, original)) {
                pthread_mutex_unlock(&mutex_printage);
                return true;
            }
        }
        i = i->next;
    }
    pthread_mutex_unlock(&mutex_printage);
    return false;
}
