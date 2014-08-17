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
 * Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 */

#include <sys/mman.h>
#include <fcntl.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include <ftw.h>
#include <signal.h>
#include <regex.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <dirent.h>

#include "cmdline.h"
#include "preprocess.h"
#include "postprocess.h"
#include "shredder.h"
//#include "defs.h"
#include "utilities.h"
#include "traverse.h"

//typedef struct RmSchedulerTag {
//    RmSession *session;
//    GQueue *group;
//} RmSchedulerTag;



//static void rm_file_table_destroy_queue(GQueue *queue) {
//    g_queue_free_full(queue, (GDestroyNotify)rm_file_destroy);
//}

RmFileTable *rm_file_table_new(RmSession *session) {

    RmFileTable *table = g_slice_new0(RmFileTable);

    table->dev_table = g_hash_table_new_full(
                           g_direct_hash, g_direct_equal,
                           NULL, NULL //TODO (GDestroyNotify)main_free_func
                       );

    table->size_table = g_hash_table_new( NULL, NULL);

    RmSettings *settings = session->settings;
    g_assert(settings);
    g_assert(settings->namecluster == 0);

    if (session->settings->namecluster) {
        table->name_table = g_hash_table_new_full(
                                g_str_hash, g_str_equal,
                                g_free, (GDestroyNotify)g_list_free );
    } else {
        g_assert(table->name_table ==  NULL);
    }

    /* table->other_lint needs no initialising*/
    table->mounts = session->mounts;

    g_rec_mutex_init(&table->lock);
    return table;
}

void rm_file_table_destroy(RmFileTable *table) {
    g_rec_mutex_lock(&table->lock);
    {
        g_hash_table_unref(table->dev_table);
        g_hash_table_unref(table->size_table);
        rm_mounts_table_destroy(table->mounts);
        if (table->name_table) {
            g_hash_table_unref(table->name_table);
        }
        for (uint i = 0; i < sizeof(table->other_lint) / sizeof(table->other_lint[0]); i++) {
            g_list_free(table->other_lint[i]); //TODO: free_full
        }
    }
    g_rec_mutex_unlock(&table->lock);
    g_rec_mutex_clear(&table->lock);
    g_slice_free(RmFileTable, table);
}

void rm_file_table_insert(RmFileTable *table, RmFile *file) {
    g_assert(file);
    g_assert(table);

    g_rec_mutex_lock(&table->lock);

    if (file->lint_type < RM_LINT_TYPE_DUPE_CANDIDATE) {
        table->other_lint[file->lint_type] = g_list_prepend(table->other_lint[file->lint_type], file);
    } else {

        file->disk = rm_mounts_get_disk_id(table->mounts, file->dev);

        /* TODO: save lookup; traverse should already have this info */
        bool nonrotational = rm_mounts_is_nonrotational(table->mounts, file->disk);
        if(!nonrotational /*TODO: && offset_sort_optimisation */
                && ( file->lint_type == RM_LINT_TYPE_DUPE_CANDIDATE
                     || file->lint_type == RM_LINT_TYPE_NBIN ) ) {
            file->disk_offsets = rm_offset_create_table(file->path);
            file->phys_offset = rm_offset_lookup(file->disk_offsets, 0);
        }
        g_hash_table_insert(
            table->size_table,
            GUINT_TO_POINTER(file->file_size), //TODO: check overflow for >4GB files
            g_hash_table_lookup(
                table->size_table, GUINT_TO_POINTER(file->file_size)) + 1
        );
        if (table->name_table) {
            //insert file into hashtable of basename lists
            char *basename = g_strdup(rm_util_basename(file->path));
            g_hash_table_insert(table->name_table,
                                basename,
                                g_list_prepend(g_hash_table_lookup(
                                                   table->name_table,
                                                   basename),
                                               file)
                               );
        }

        GQueue *dev_list = g_hash_table_lookup(table->dev_table, GUINT_TO_POINTER(file->dev));
        if(dev_list == NULL) {
            dev_list = g_queue_new();
            g_hash_table_insert(table->dev_table, GUINT_TO_POINTER(file->disk), dev_list);
        }
        g_queue_push_head(dev_list, file);
        debug("Added Inode: %d Offset: %" PRId64 " file: %s\n", (int)file->inode, file->phys_offset, file->path);
    }

    g_rec_mutex_unlock(&table->lock);
}


static void handle_double_base_file(RmSession *session, RmFile *file) {
    char *abs_path = realpath(file->path, NULL);
    file->lint_type = RM_LINT_TYPE_BASE;
    rm_error("   %sls%s %s\n", (session->settings->verbosity != 1) ? GRE : "", NCO, abs_path);
    write_to_log(session, file, false, NULL);
    g_free(abs_path);
}

static int find_double_bases(RmSession *session) {
    // TODO:  Finish re-write of this (got part way then realised I didn't fully understand what we are trying to do */
    bool header_printed = false;
    int num_found = 0;

    GHashTableIter iter;
    gpointer key, value;

    GHashTable *name_table = session->table->size_table;

    g_hash_table_iter_init(&iter, name_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {

        bool node_handled = false;
        GList *list = value;
        g_assert(list);

        if (list->next) {
            /* list is at least 2 files long */
            while (list) {
                RmFile *file = list->data;
                if(!header_printed) {
                    rm_error("\n%s#"NCO" Double basename(s):\n", GRE);
                    header_printed = true;
                }

                if(!node_handled) {
                    node_handled = true;
                    handle_double_base_file(session, file);
                    num_found++;

// TODO: do we need this?
//                    /* At this point files with same inode and device are NOT handled yet.
//                       Therefore this foolish, but well working approach is made.
//                       (So it works also with more than one dir in the cmd)  */
//
//                    RmFile *fx = fj;
//                    while((fx = rm_file_list_iter_all(session->list, fx))) {
//                        if(fx->inode == fj->inode) {
//                            fx->lint_type = RM_LINT_TYPE_BASE;
//                        }
//                    }
                }

                list = list->next;
            }
        } else {
            /* only one file in list */
            g_hash_table_remove(name_table, &key);
        }
    }

//    if(sets->collide) {
//        GHashTableIter iter;
//        gpointer key, value;
//
//        g_hash_table_iter_init(&iter, name_table);
//        while (g_hash_table_iter_next(&iter, &key, &value)) {
//            RmFile *file = key;
//            rm_file_list_remove(session->table, file);
//        }
//    }
    g_hash_table_destroy(name_table);
    return num_found;
}

//static long cmp_sort_lint_type(RmFile *a, RmFile *b, gpointer user_data) {
//    (void) user_data;
//    if (a->lint_type == RM_LINT_TYPE_EDIR && b->lint_type == RM_LINT_TYPE_EDIR)
//        return (long)strcmp(b->path, a->path);
//    else
//        //TODO: change rm_file_table_insert so this is not neccessary
//        return ((long)a->lint_type - (long)b->lint_type);
//}

static const char *RM_LINT_TYPE_TO_DESCRIPTION[] = {
    [RM_LINT_TYPE_UNKNOWN]      = "",
    [RM_LINT_TYPE_BLNK]         = "Bad link(s)",
    [RM_LINT_TYPE_EDIR]         = "Empty dir(s)",
    [RM_LINT_TYPE_NBIN]         = "Non stripped binarie(s)",
    [RM_LINT_TYPE_BADUID]       = "Bad UID(s)",
    [RM_LINT_TYPE_BADGID]       = "Bad GID(s)",
    [RM_LINT_TYPE_BADUGID]      = "Bad UID and GID(s)",
    [RM_LINT_TYPE_EFILE]        = "Empty file(s)",
    [RM_LINT_TYPE_DUPE_CANDIDATE] = "Duplicate(s)"
};

static const char *RM_LINT_TYPE_TO_COMMAND[] = {
    [RM_LINT_TYPE_UNKNOWN]      = "",
    [RM_LINT_TYPE_BLNK]         = "rm",
    [RM_LINT_TYPE_EDIR]         = "rmdir",
    [RM_LINT_TYPE_NBIN]         = "strip --strip-debug",
    [RM_LINT_TYPE_BADUID]       = "chown %s",
    [RM_LINT_TYPE_BADGID]       = "chgrp %s",
    [RM_LINT_TYPE_BADUGID]      = "chown %s:%s",
    [RM_LINT_TYPE_EFILE]        = "rm",
    [RM_LINT_TYPE_DUPE_CANDIDATE] = "ls"
};

static uint handle_other_lint(RmSession *session) {
    uint num_handled = 0;

    RmLintType flag = RM_LINT_TYPE_UNKNOWN;
    RmSettings *sets = session->settings;
    RmFileTable *table = session->table;

    const char *user = rm_util_get_username();
    const char *group = rm_util_get_groupname();

    for (uint type = 0; type < RM_LINT_TYPE_DUPE_CANDIDATE; ++type) {
        GList *list = table->other_lint[type];
        for(GList *iter = list; iter; iter = iter->next) {
            /* TODO: RM_LINT_TYPE_EDIR list needs sorting into reverse alphabetical order */
            RmFile *file = iter->data;
            g_assert(file);
            g_assert(type == file->lint_type);
            g_assert(type < RM_LINT_TYPE_DUPE_CANDIDATE);
            num_handled++;
            if(flag != file->lint_type) {
                rm_error(YEL"\n# "NCO);
                rm_error("%s", RM_LINT_TYPE_TO_DESCRIPTION[file->lint_type]);
                rm_error(": \n"NCO);
                flag = file->lint_type;
            }

            rm_error(GRE);
            rm_error("   ");

            const char *format = RM_LINT_TYPE_TO_COMMAND[type];
            switch(type) {
            case RM_LINT_TYPE_BADUID:
                rm_error(format, user);
                break;
            case RM_LINT_TYPE_BADGID:
                rm_error(format, group);
                break;
            case RM_LINT_TYPE_BADUGID:
                rm_error(format, user, group);
                break;
            default:
                rm_error("%s", format);
            }

            rm_error(NCO);
            rm_error(" %s\n", file->path);
            if(sets->output_log) {
                write_to_log(session, file, false, NULL);
            }
        }
        g_list_free_full(list, (GDestroyNotify)rm_file_destroy);
    }
    return num_handled;
}

/* This does preprocessing including handling of "other lint" (non-dupes) */
void do_pre_processing(RmSession *session) {
    char lintbuf[128] = {0};
    guint64 other_lint = 0;

    RmSettings *settings = session->settings;

    if(settings->namecluster) {
        other_lint += find_double_bases(session);
        rm_error("\n");
    }

    other_lint += handle_other_lint(session);



    if(settings->searchdup == 0) {
        /* rmlint was originally supposed to find duplicates only
           So we have to free list that whould have been used for
           dup search before dieing */
        die(session, EXIT_SUCCESS);
    }

    //info("\nNow sorting list based on filesize... ");
    //gsize rem_counter = rm_file_list_sort_groups(list, session);
    //info("done.\n");

    //info("Now attempting to find duplicates. This may take a while...\n");
    //info("Now removing files with unique sizes from list...");
    //info(""YEL"%ld item(s) less"NCO" in list.", rem_counter);
    //info(" done. \nNow doing fingerprints and full checksums.\n");
    rm_error("\n%s Duplicate(s):\n", YEL"#"NCO);

    /* Groups are splitted, now give it to the scheduler
     * The scheduler will do another filterstep, build checkusm
     * and compare 'em. The result is printed afterwards */

    shred_run(session);
    if(session->dup_counter == 0) {
        rm_error("\r                    ");
    } else {
        rm_error("\n");
    }

    rm_util_size_to_human_readable(session->total_lint_size, lintbuf, sizeof(lintbuf));
    warning(
        "\n"RED"=> "NCO"In total "RED"%lu"NCO" files, whereof "RED"%lu"NCO" are duplicate(s)",
        session->total_files, session->dup_counter
    );

    if(other_lint > 0) {
        rm_util_size_to_human_readable(other_lint, lintbuf, sizeof(lintbuf));
        warning(RED"\n=> %lu"NCO" other suspicious items found ["GRE"%s"NCO"]", other_lint, lintbuf);
    }

    warning("\n");
    if(!session->aborted) {
        warning(
            RED"=> "NCO"Totally "GRE" %s "NCO" [%lu Bytes] can be removed.\n",
            lintbuf, session->total_lint_size
        );
    }
    if(settings->mode == RM_MODE_LIST && session->dup_counter) {
        warning(RED"=> "NCO"Nothing removed yet!\n");
    }
    warning("\n");
    if(settings->verbosity == 6) {
        info("Now calculation finished.. now writing end of log...\n");
        info(
            RED"=> "NCO"In total "RED"%lu"NCO" files, whereof "RED"%lu"NCO" are duplicate(s)\n",
            session->total_files, session->dup_counter
        );
        if(!session->aborted) {
            info(
                RED"=> "NCO"In total "GRE" %s "NCO" ["BLU"%lu"NCO" Bytes] can be removed without dataloss.\n",
                lintbuf, session->total_lint_size
            );
        }
    }

    if(session->log_out == NULL && settings->output_log) {
        rm_error(RED"\nERROR: "NCO);
        fflush(stdout);
        rm_perror("Unable to write log - target file:");
        rm_perror(settings->output_log);
        putchar('\n');
    } else if(settings->output_log && settings->output_script) {
        warning("A log has been written to "BLU"%s "NCO".\n", settings->output_script);
        warning("A ready to use shellscript to "BLU"%s"NCO".\n", settings->output_log);
    }
}
