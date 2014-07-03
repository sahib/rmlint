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
 **/

#include <sys/mman.h>
#include <fcntl.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <ftw.h>
#include <signal.h>
#include <regex.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <dirent.h>

#include "rmlint.h"
#include "filter.h"
#include "mode.h"
#include "md5.h"
#include "list.h"
#include "defs.h"
#include "linttests.h"

typedef struct RmSchedulerTag {
    RmSession *session;
    GQueue *group;
} RmSchedulerTag;

/* Sort criteria for sorting by preferred path (first) then user-input criteria */
static long cmp_orig_criteria(RmFile *a, RmFile *b, gpointer user_data) {
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
                cmp = strcmp (rmlint_basename(a->path), rmlint_basename (b->path));
                break;
            case 'A':
                cmp = strcmp (rmlint_basename(b->path), rmlint_basename (a->path));
                break;
            case 'p':
                cmp = (long)a->pnum - (long)b->pnum;
                break;
            case 'P':
                cmp = (long)b->pnum - (long)a->pnum;
                break;
            }
            if (cmp) {
                return cmp;
            }
        }
    }
    return 0;
}

/* Compares the "fp" array of the RmFile a and b */
static int cmp_fingerprints(RmFile *_a, RmFile *_b) {
    int i, j;
    RmFile *a = ( _a->hardlinked_original ? _a->hardlinked_original : _a );
    RmFile *b = ( _b->hardlinked_original ? _b->hardlinked_original : _b );

    /* compare both fp-arrays */
    for(i = 0; i < 2; i++) {
        for(j = 0; j < MD5_LEN; j++) {
            if(a->fp[i][j] != b->fp[i][j]) {
                return  0;
            }
        }
    }
    /* Also compare the bytes which were read 'on the fly' */
    for(i = 0; i < BYTE_MIDDLE_SIZE; i++) {
        if(a->bim[i] != b->bim[i]) {
            return 0;
        }
    }
    /* Let it pass! */
    return 1;
}

/* Compare criteria of checksums */
static int cmp_f(RmFile *_a, RmFile *_b) {
    RmFile *a = ( _a->hardlinked_original ? _a->hardlinked_original : _a );
    RmFile *b = ( _b->hardlinked_original ? _b->hardlinked_original : _b );
    int i, fp_i, x;
    int is_empty[2][3] = { {1, 1, 1}, {1, 1, 1} };
    for(i = 0; i < MD5_LEN; i++) {
        if(a->md5_digest[i] != b->md5_digest[i]) {
            return 1;
        }
        if(a->md5_digest[i] != 0) {
            is_empty[0][0] = 0;
        }
        if(b->md5_digest[i] != 0) {
            is_empty[1][0] = 0;
        }
    }
    for(fp_i = 0; fp_i < 2; fp_i++) {
        for(i = 0; i < MD5_LEN; i++) {
            if(a->fp[fp_i][i] != b->fp[fp_i][i]) {
                return 1;
            }
            if(a->fp[fp_i][i] != 0) {
                is_empty[0][fp_i + 1] = 0;
            }
            if(b->fp[fp_i][i] != 0) {
                is_empty[1][fp_i + 1] = 0;
            }
        }
    }
    /* check for empty checkusm AND fingerprints - refuse and warn */
    for(x = 0; x < 2; x++) {
        if(is_empty[x][0] && is_empty[x][1] && is_empty[x][2]) {
            warning(YEL"\nWARN: "NCO"Refusing file with empty checksum and empty fingerprint.  Trying to compare:\n%s (lint type %d)\n%s (lint type %d)\n",
                    _a->path, _a->lint_type, _b->path, _b->lint_type );
            return 1;
        }
    }
    return 0;
}

static int paranoid(const RmFile *p1, const RmFile *p2) {
    int result = 0, file_a, file_b;
    char *file_map_a, * file_map_b;
    if(!p1 || !p2)
        return 0;
    if(p1->fsize != p2->fsize)
        return 0;
    if((file_a = open(p1->path, MD5_FILE_FLAGS)) == -1) {
        perror(RED"ERROR:"NCO"sys:open()");
        return 0;
    }
    if((file_b = open(p2->path, MD5_FILE_FLAGS)) == -1) {
        perror(RED"ERROR:"NCO"sys:open()");
        return 0;
    }
    if(p1->fsize < MMAP_LIMIT && p1->fsize > MD5_IO_BLOCKSIZE >> 1) {
        file_map_a = mmap(NULL, (size_t)p1->fsize, PROT_READ, MAP_PRIVATE, file_a, 0);
        if(file_map_a != MAP_FAILED) {
            if(madvise(file_map_a, p1->fsize, MADV_SEQUENTIAL) == -1) {
                perror("madvise");
            }
            file_map_b = mmap(NULL, (size_t)p2->fsize, PROT_READ, MAP_PRIVATE, file_a, 0);
            if(file_map_b != MAP_FAILED) {
                if(madvise(file_map_b, p2->fsize, MADV_SEQUENTIAL) == -1)
                    perror("madvise");
                result = !memcmp(file_map_a, file_map_b, p1->fsize);
                munmap(file_map_b, p1->fsize);
            } else {
                perror("paranoid->mmap");
                result = 0;
            }
            munmap(file_map_a, p1->fsize);
        } else {
            perror("paranoid->mmap");
            result = 0;
        }
    } else { /* use fread() */
        guint64 blocksize = MD5_IO_BLOCKSIZE / 2;
        char *read_buf_a = g_alloca(blocksize);
        char *read_buf_b = g_alloca(blocksize);
        int read_a = -1, read_b = -1;
        while(read_a && read_b) {
            if((read_a = read(file_a, read_buf_a, blocksize) == -1)) {
                result = 0;
                break;
            }
            if((read_b = read(file_b, read_buf_b, blocksize) == -1)) {
                result = 0;
                break;
            }
            if(read_a == read_b) {
                if((result = !memcmp(read_buf_a, read_buf_b, read_a)) == 0) {
                    break;
                }
            } else {
                result = 0;
                break;
            }
        }
    }
    if(close(file_a) == -1)
        perror(RED"ERROR:"NCO"close()");
    if(close(file_b) == -1)
        perror(RED"ERROR:"NCO"close()");
    return result;
}

/* ------------------------------------------------------------- */

/* Callback from build_checksums */
static void *cksum_cb(void *vp) {
    RmSchedulerTag *tag = vp;

    /* Iterate over all files in group */
    for(GList *iter = tag->group->head; iter; iter = iter->next) {
        RmFile *iter_file = iter->data;
        if (!iter_file->hardlinked_original)
            /* do checksum unless this is a hardlink of a file which is
             * already going to be checksummed */
            md5_file(tag->session, iter->data);
        /* FUTURE OPTIMISATION: as-is, we _always_ do checksum of _one_ file
         * in each a group of hardlinks; but if the group contains _only_
         * hardlinks then we in theory don't need to checksum _any_ of them  */
    }

    /* Do not use g_queue_free(), that would delete all GLists in it */
    g_free(tag->group);
    return NULL;
}

static void build_fingerprints (RmSession *session, GQueue *group) {
    /* Prevent crashes (should not happen too often) */
    if(group == NULL || group->head == NULL) {
        return;
    }

    RmFile *file = group->head->data;
    guint64 grp_sz;

    /* The size read in to build a fingerprint */
    grp_sz = MD5_FPSIZE_FORM(file->fsize);

    /* Clamp it to some maximum (4KB) */
    grp_sz = (grp_sz > MD5_FP_MAX_RSZ) ? MD5_FP_MAX_RSZ : grp_sz;

    /* Calc fingerprints  */
    for(GList *iter = group->head; iter; iter = iter->next) {
        /* see md5.c for explanations */
        RmFile *iter_file = iter->data;
        if (!iter_file->hardlinked_original)
            /* do fingerprint unless this is a hardlink of a file which is
             * already going to be fingerprinted */
            md5_fingerprint(session, iter_file, grp_sz);
    }
}

static void build_checksums(RmSession *session, GQueue *group) {
    if(group == NULL || group->head == NULL) {
        if(group) {
            g_printerr("Warning: Empty group received. That's a bug.\n");
        }
        return;
    }

    RmSettings *set = session->settings;
    gulong byte_size = rm_file_list_byte_size(group);

    if(set->threads == 1 ||  byte_size < (2 * MD5_MTHREAD_SIZE)) {
        /* Just loop through this group and built the checksum */
        RmSchedulerTag tag;
        tag.session = session;
        tag.group = g_new0(GQueue, 1);
        memcpy(tag.group, group, sizeof(GQueue));

        cksum_cb((void *) &tag);
    } else { /* split group in subgroups and start a seperate thread for each */
        guint64  sz = 0;
        GList *ptr, *lst;
        ptr = lst = group->head;

        /* The refereces to all threads */
        gulong byte_size = rm_file_list_byte_size(group);

        size_t list_size = (byte_size / MD5_MTHREAD_SIZE + 2) * sizeof(pthread_t);
        pthread_t *thread_queue = malloc(list_size);
        RmSchedulerTag *tags = malloc(list_size);

        int thread_counter = 0;
        gint subgroup_len = 0;

        while(ptr) {
            sz += ((RmFile *)ptr->data)->fsize;
            if(sz >= MD5_MTHREAD_SIZE || ptr->next == NULL) {
                GQueue *subgroup = g_new0(GQueue, 1);
                subgroup->head = lst;
                subgroup->tail = ptr->next;
                subgroup->length = subgroup_len;
                subgroup_len = 0;

                /* Update */
                ptr = ptr->next;
                lst = ptr;

                tags[thread_counter].session = session;
                tags[thread_counter].group = subgroup;

                /* Now create the thread */
                if(pthread_create(&thread_queue[thread_counter], NULL, cksum_cb, &tags[thread_counter])) {
                    perror(RED"ERROR: "NCO"pthread_create in build_checksums()");
                }
                thread_counter++;
            } else {
                subgroup_len++;
                ptr = ptr->next;
            }
        }
        /* Make sure all threads are joined */
        for(int i = 0; i < thread_counter; i++) {
            if(pthread_join(thread_queue[i], NULL)) {
                perror(RED"ERROR: "NCO"pthread_join in build_checksums()");
            }
        }
        g_free(thread_queue);
        g_free(tags);
    }
}

static void free_island(GQueue *island) {
    for(GList *iter = island->head; iter; iter = iter->next) {
        rm_file_destroy((RmFile *)iter->data);
    }
    g_queue_clear(island);
}

static bool findmatches(RmSession *session, GQueue *group, int testlevel) {
    RmSettings *sets = session->settings;
    GList *i = group->head, *j = NULL;
    int returnval = 0;  /* not sure what we are using this for */

    if(i == NULL) {
        return false;
    }

    switch (testlevel) {
    case 1:
        /*fingerprint compare - calculate fingerprints*/
        build_fingerprints(session, group);
        break;
    case 2:
        /*md5 compare - calculate checksums*/
        build_checksums(session, group);
        break;
    case 3:
        break;
    default:
        break;
    }

    warning(NCO);
    while(i) {
        GQueue island = G_QUEUE_INIT;
        int num_orig = 0;
        int num_non_orig = 0;

        /*start new island of matched files  */
        /* first remove i from mainland */
        i = g_queue_pop_head_link(group);
        g_queue_push_head_link(&island, i);
        j = group->head;

        while(j) {
            int match = 0;
            switch (testlevel) {
            case 1:
                /*fingerprint compare*/
                match = (cmp_fingerprints(i->data, j->data) == 1);
                break;
            case 2:
                /*md5 compare*/
                match = (cmp_f(i->data, j->data) == 0);
                break;
            case 3:
                /* If we're bothering with paranoid users - Take the gatling! */
                match = ((sets->paranoid) ? paranoid(i->data, j->data) : 1);
                break;
            default:
                match = 0;
                break;
            }
            if (match) {
                /* move j from grp onto island*/
                /* first get pointer to j before we start messing with j*/
                GList *tmp = j->next;
                g_queue_unlink(group, j);
                g_queue_push_tail_link(&island, j);

                RmFile *current = j->data;
                num_orig += current->in_ppath;
                num_non_orig += !current->in_ppath;
                j = tmp;
            } else {
                j = j->next;
            }
        }

        /* So we have created an island of everything that matched i.
           Now check if it is singleton or if it fails the other
           criteria related to setting must_match_original or
           keep_all_originals
        */
        if (0
                || (g_queue_get_length(&island) <= 1)
                || ((sets->keep_all_originals == 1) && (num_non_orig == 0))
                || ((sets->must_match_original == 1) && (num_orig == 0))
           ) {
            free_island(&island);
        } else {
            if ((testlevel == 3) || (!sets->paranoid && (testlevel == 2))) {
                /* done testing; process the island */
                g_queue_sort(&island, (GCompareDataFunc) cmp_orig_criteria, session);
                returnval = (returnval || process_doop_groop(session, &island));
            } else {
                /* go to next level */
                returnval = (returnval || findmatches(session, &island, testlevel + 1));
            }
        }

        free_island(&island);
        i = group->head;
    }

    return returnval;
}

/* Callback from scheduler that actually does the work for ONE group */
static void *scheduler_cb(void *tag_pointer) {
    /* cast from *void */
    RmSchedulerTag *tag = tag_pointer;
    GQueue *group = tag->group;

    if(group == NULL || group->head == NULL) {
        return NULL;
    }
    /* start matching (start at level 1 (fingerprint filter)) then
     * recursively escalates to higher levels */
    findmatches(tag->session, group, 1);
    return NULL;
}

/* Joins the threads launched by scheduler */
static void scheduler_jointhreads(pthread_t *threads, guint64 n) {
    for(guint64 i = 0; i < n; i++) {
        if(pthread_join(threads[i], NULL)) {
            perror(RED"ERROR: "NCO"pthread_join in scheduler()");
        }
    }
}

/* Distributes the groups on the ressources */
static void start_scheduler(RmSession *session) {
    RmFileList *list = session->list;
    RmSettings *sets = session->settings;

    /* There might be at max. sets->threads tags at the same time. */
    pthread_t threads[sets->threads + 1];
    RmSchedulerTag tags[sets->threads + 1];

    /* If size of certain group exceeds limit start an own thread, else run in 'foreground'
     * Run max set->threads at the same time. */
    unsigned nrun = 0;
    GSequenceIter *iter = rm_file_list_get_iter(list);

    while(!g_sequence_iter_is_end(iter)) {
        GQueue *group = g_sequence_get(iter);
        gulong byte_size = rm_file_list_byte_size(group);

        if(byte_size > THREAD_SHEDULER_MTLIMIT && sets->threads > 1) { /* Group exceeds limit */
            tags[nrun].group = group;
            tags[nrun].session = session;

            if(pthread_create(&threads[nrun], NULL, scheduler_cb, &tags[nrun])) {
                perror(RED"ERROR: "NCO"pthread_create in scheduler()");
            }
            if(nrun >= sets->threads - 1) {
                scheduler_jointhreads(threads, nrun + 1);
                nrun = 0;
                continue;
            }
            nrun++;
        } else { /* run in foreground */
            RmSchedulerTag tag;
            tag.group = group;
            tag.session = session;
            scheduler_cb(&tag);
        }
        iter = g_sequence_iter_next(iter);
    }
    scheduler_jointhreads(threads, nrun);
}

/* Takes num and converts into some human readable string. 1024 -> 1KB */
static void size_to_human_readable(guint64 num, char *in) {
    if(num < 512) {
        sprintf(in, "%ld B", (unsigned long)num);
    } else if(num < 1048576) {
        sprintf(in, "%.2f KB", (float)(num / 1024.0));
    } else if(num < 1073741824 / 2) {
        sprintf(in, "%.2f MB", (float)(num / (1024.0 * 1024.0)));
    } else {
        sprintf(in, "%.2f GB", (float)(num / (1024.0 * 1024.0 * 1024.0)));
    }
}

static void handle_double_base_file(RmSession *session, RmFile *file) {
    char *abs_path = realpath(file->path, NULL);
    file->lint_type = TYPE_BASE;
    error("   %sls%s %s\n", (session->settings->verbosity != 1) ? GRE : "", NCO, abs_path);
    write_to_log(session, file, false, NULL);
    g_free(abs_path);
}

static int find_double_bases(RmSession *session) {
    bool header_printed = true;
    int num_found = 0;
    RmSettings *sets = session->settings;

    GHashTable *found_table = g_hash_table_new(g_direct_hash, g_direct_equal);

    RmFile *fi = NULL;
    while((fi = rm_file_list_iter_all(session->list, fi))) {
        if(fi->lint_type == TYPE_BASE) {
            continue;
        }

        bool node_handled = false;

        RmFile *fj = rm_file_list_iter_all(session->list, fi);
        while((fj = rm_file_list_iter_all(session->list, fj))) {
            /* compare basenames */
            if(1
                    && !strcmp(rmlint_basename(fi->path), rmlint_basename(fj->path))
                    && fi->node != fj->node
                    && fj->lint_type != TYPE_BASE
              ) {
                if(header_printed) {
                    error("\n%s#"NCO" Double basename(s):\n", (sets->verbosity > 1) ? GRE : NCO);
                    header_printed = false;
                }

                if(!node_handled) {
                    node_handled = true;
                    handle_double_base_file(session, fi);

                    /* At this point files with same inode and device are NOT handled yet.
                       Therefore this foolish, but well working approach is made.
                       (So it works also with more than one dir in the cmd)  */

                    RmFile *fx = fj;
                    while((fx = rm_file_list_iter_all(session->list, fx))) {
                        if(fx->node == fj->node) {
                            fx->lint_type = TYPE_BASE;
                        }
                    }
                }

                handle_double_base_file(session, fj);
                num_found++;

                g_hash_table_insert(found_table, fj, NULL /* value is not important */);
            }
        }
    }

    if(sets->collide) {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init(&iter, found_table);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            RmFile *file = key;
            rm_file_list_remove(session->list, file);
        }
    }
    g_hash_table_destroy(found_table);
    return num_found;
}

static long cmp_sort_lint_type(RmFile *a, RmFile *b, gpointer user_data) {
    (void) user_data;
    if (a->lint_type == TYPE_EDIR && a->lint_type == TYPE_EDIR)
        return (long)strcmp(b->path, a->path);
    else
        return ((long)a->lint_type - (long)b->lint_type);
}

static const char *TYPE_TO_DESCRIPTION[] = {
    [TYPE_UNKNOWN]      = "",
    [TYPE_BLNK]         = "Bad link(s)",
    [TYPE_OTMP]         = "Old Tempfile(s)",
    [TYPE_EDIR]         = "Empty dir(s)",
    [TYPE_JNK_DIRNAME]  = "Junk dirname(s)",
    [TYPE_JNK_FILENAME] = "Junk filename(s)",
    [TYPE_NBIN]         = "Non stripped binarie(s)",
    [TYPE_BADUID]       = "Bad UID(s)",
    [TYPE_BADGID]       = "Bad GID(s)",
    [TYPE_BADUGID]      = "Bad UID and GID(s)",
    [TYPE_EFILE]        = "Empty file(s)",
    [TYPE_DUPE_CANDIDATE] = "Duplicate(s)"
};

static const char *TYPE_TO_COMMAND[] = {
    [TYPE_UNKNOWN]      = "",
    [TYPE_BLNK]         = "rm",
    [TYPE_OTMP]         = "rm",
    [TYPE_EDIR]         = "rmdir",
    [TYPE_JNK_DIRNAME]  = "ls",
    [TYPE_JNK_FILENAME] = "ls",
    [TYPE_NBIN]         = "strip --strip-debug",
    [TYPE_BADUID]       = "chown %s",
    [TYPE_BADGID]       = "chgrp %s",
    [TYPE_BADUGID]      = "chown %s:%s",
    [TYPE_EFILE]        = "rm",
    [TYPE_DUPE_CANDIDATE] = "ls"
};

static void handle_other_lint(RmSession *session, GSequenceIter *first, GQueue *first_group) {
    RmLintType flag = TYPE_UNKNOWN;
    RmSettings *sets = session->settings;
    const char *user = get_username();
    const char *group = get_groupname();

    for(GList *iter = first_group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        if(file->lint_type >= TYPE_OTHER_LINT) {
            error("Unknown filetype: %d (thats a bug)\n", file->lint_type);
            continue;
        }

        if(flag != file->lint_type) {
            if(sets->verbosity > 1) {
                error(YEL"\n# "NCO);
            } else {
                error("\n# ");
            }

            error("%s", TYPE_TO_DESCRIPTION[file->lint_type]);
            error(": \n"NCO);
            flag = file->lint_type;
        }

        if(sets->verbosity > 1) {
            error(GRE);
        }

        error("   ");
        const char *format = TYPE_TO_COMMAND[file->lint_type];
        switch(file->lint_type) {
        case TYPE_BADUID:
            error(format, user);
            break;
        case TYPE_BADGID:
            error(format, group);
            break;
        case TYPE_BADUGID:
            error(format, user, group);
            break;
        default:
            error("%s", format);
        }

        if(sets->verbosity > 1) {
            error(NCO);
        }
        error(" %s\n", file->path);
        if(sets->output) {
            write_to_log(session, file, false, NULL);
        }
    }
    rm_file_list_clear(first);
}

/* This the actual main() of rmlint */
void start_processing(RmSession *session) {
    char lintbuf[128] = {0};
    guint64 other_lint = 0;

    RmSettings *settings = session->settings;
    RmFileList *list = session->list;

    if(settings->namecluster) {
        other_lint += find_double_bases(session);
        error("\n");
    }

    GSequenceIter *first = rm_file_list_get_iter(list);
    rm_file_list_sort_group(first, (GCompareDataFunc)cmp_sort_lint_type, NULL);
    GQueue *first_group = g_sequence_get(first);

    if(rm_file_list_byte_size(first_group) == 0) {
        other_lint += g_queue_get_length(first_group);
        handle_other_lint(session, first, first_group);
    }

    info("\nNow sorting list based on filesize... ");
    gsize rem_counter = rm_file_list_sort_groups(list, settings);
    info("done.\n");

    if(settings->searchdup == 0) {
        /* rmlint was originally supposed to find duplicates only
           So we have to free list that whould have been used for
           dup search before dieing */
        die(session, EXIT_SUCCESS);
    }

    info("Now attempting to find duplicates. This may take a while...\n");
    /* actually this was done already above while building the list */
    info("Now removing files with unique sizes from list...");
    info(""YEL"%ld item(s) less"NCO" in list.", rem_counter);
    // info(NCO"\nNow removing "GRE"%ld"NCO" empty files / bad links / junk names from list...\n"NCO, emptylist.len);

    /* Now make sure groups are sorted by their location on the disk - TODO? can remove this because was already sorted above?*/
    info(" done. \nNow doing fingerprints and full checksums.%c\n", settings->verbosity > 4 ? '.' : '\n');

    error("%s Duplicate(s):", (settings->verbosity > 1) ? YEL"#"NCO : "#");

    /* Groups are splitted, now give it to the scheduler
     * The scheduler will do another filterstep, build checkusm
     * and compare 'em. The result is printed afterwards */
    start_scheduler(session);
    if(session->dup_counter == 0) {
        error("\r                    ");
    } else {
        error("\n");
    }

    size_to_human_readable(session->total_lint_size, lintbuf);
    warning(
        "\n"RED"=> "NCO"In total "RED"%lu"NCO" files, whereof "RED"%lu"NCO" are duplicate(s)",
        session->total_files, session->dup_counter
    );

    if(other_lint > 0) {
        size_to_human_readable(other_lint, lintbuf);
        warning(RED"\n=> %lu"NCO" other suspicious items found ["GRE"%s"NCO"]", other_lint, lintbuf);
    }

    warning("\n");
    if(!session->aborted) {
        warning(
            RED"=> "NCO"Totally "GRE" %s "NCO" [%lu Bytes] can be removed.\n",
            lintbuf, session->total_lint_size
        );
    }
    if((settings->mode == 1 || settings->mode == 2) && session->dup_counter) {
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

    if(session->log_out == NULL && settings->output) {
        error(RED"\nERROR: "NCO);
        fflush(stdout);
        perror("Unable to write log - target file:");
        perror(settings->output);
        putchar('\n');
    } else if(settings->output) {
        warning("A log has been written to "BLU"%s.log"NCO".\n", settings->output);
        warning("A ready to use shellscript to "BLU"%s.sh"NCO".\n", settings->output);
    }
}
