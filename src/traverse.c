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
 *
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <glib.h>
#include <error.h>

#include <sys/stat.h>
#include <unistd.h>
#include "list.h"
#include "rmlint.h"
#include "filter.h"
#include "linttests.h"

#define MAX_EMPTYDIR_DEPTH (PATH_MAX / 2) /* brute force option */

#define RM_MAX_LIST_BUILD_POOL_THREADS 20 /* for testing */


typedef struct RmListBuilderBuffer {
    char *path;
    guint64 fsize;
    ino_t node;
    dev_t dev;
    time_t mtime;
    RmLintType linttype;
    bool is_ppath;
    unsigned pnum;
} RmListBuilderBuffer;


/* Threadpool worker for creating RmFiles and adding them to list
 * Why: to avoid slowing down the FTS traverses
 * Receives: RmListBuilderBuffers from FTS threads
 * Work: Creates new RmFile from the info in the RmListBuilderBuffer and appends it to list
 * Signals to other threads: none */
void rm_add_file_to_list (gpointer data, gpointer user_data) {
    struct RmListBuilderBuffer *buf = (gpointer)data;
    RmFileList *list = (gpointer)user_data;
    RmFile *file = rm_file_new(buf->path, buf->fsize, buf->node, buf->dev, buf->mtime, buf->linttype, buf->is_ppath, buf->pnum);
    rm_file_list_append(list, file);
    g_free(buf->path);
    g_free(buf);
}


static int process_file (RmSession *session, FTSENT *ent, bool is_ppath, int pnum, RmLintType file_type) {
    RmSettings *settings = session->settings;
    GThreadPool *pool = session->list_build_pool;
    GError *g_err = NULL;

    struct RmListBuilderBuffer *buf = NULL;

    const char *path = session->settings->paths[pnum];

    struct stat stat_buf;
    struct stat *statp = NULL;

    if(ent == NULL) {
        /* if this is direct file add from command-line argument, then we didn't get here via FTS, *
         * so we don't have statp yet; get it!*/
        stat(path, &stat_buf);
        statp = &stat_buf;
    } else {
        statp = ent->fts_statp;
    }

    if (file_type == 0) {
        RmLintType gid_check;

        /*see if we can find a lint type*/
        if ((gid_check = uid_gid_check(path, statp, session))) {
            file_type = gid_check;
        } else if(is_nonstripped(path, statp, settings)) {
            file_type = TYPE_NBIN;
        } else if(statp->st_size == 0) {
            file_type = TYPE_EFILE;
        } else {
            guint64 file_size = statp->st_size;
            if(!settings->limits_specified || (settings->minsize <= file_size && file_size <= settings->maxsize)) {
                file_type = TYPE_DUPE_CANDIDATE;
            } else {
                return 0;
            }
        }
    }


    if(ent == NULL) {
        buf = g_malloc0 ( sizeof (struct RmListBuilderBuffer));
        buf->path = g_strdup(path);
        buf->fsize = statp->st_size;
        buf->node = statp->st_ino;
        buf->dev = statp->st_dev;
        buf->mtime = statp->st_mtim.tv_sec;
        buf->linttype = file_type;
        buf->is_ppath = is_ppath;
        buf->pnum = pnum;
        if (!g_thread_pool_push (pool, buf, &g_err)) {
            rm_error("Error %d %s pushing RmFile %s to list build pool", g_err->code, g_err->message, path);
        }

    } else {
        switch (ent->fts_info) {
        case FTS_D:         /* preorder directory */
        case FTS_DC:        /* directory that causes cycles */
        case FTS_DNR:       /* unreadable directory */
        case FTS_DOT:       /* dot or dot-dot */
        case FTS_DP:        /* postorder directory */
        case FTS_ERR:       /* error; errno is set */
        case FTS_INIT:      /* initialized only */
        case FTS_SLNONE:    /* symbolic link without target */
        case FTS_W:         /* whiteout object */
        case FTS_NS:        /* stat(2) failed */
        case FTS_NSOK:      /* no stat(2) requested */
            /* don't add entry for above types */
            break;
        case FTS_F:         /* regular file */
        case FTS_SL:        /* symbolic link */
        case FTS_DEFAULT:   /* none of the above */
            /* TODO: clean this up (code here is almost identical to above) */
            buf = g_malloc0 ( sizeof (struct RmListBuilderBuffer));
            buf->path = g_strdup(ent->fts_path);
            buf->fsize = statp->st_size;
            buf->node = statp->st_ino;
            buf->dev = statp->st_dev;
            buf->mtime = statp->st_mtim.tv_sec;
            buf->linttype = file_type;
            buf->is_ppath = is_ppath;
            buf->pnum = pnum;
            if (!g_thread_pool_push (pool, buf, &g_err)) {
                rm_error("Error %d %s pushing RmFile %s to list build pool", g_err->code, g_err->message, ent->fts_path);
            }
            break;
        default:
            break;
        } /* end switch(p->fts_info)*/
    }
    return 1;
}


typedef struct RmTraversePathBuffer {
    int  pathnum;
    int fts_flags;
    RmSession *session;
} RmTraversePathBuffer;


bool matches_toplevel_path ( char *filepath, RmSettings *settings) {
    for ( int i = 0; settings->paths[i] != NULL; i++) {
        if (strcmp(filepath, settings->paths[i]) == 0)
            return true;
    }
    return false;
}


/* Traverse the file hierarchies named in session->paths[pathnum]. *
 * Returns number of files added to list                           */
static guint64 traverse_path(RmTraversePathBuffer *traverse_path_args) {
    RmSession *session = traverse_path_args->session;

    int  pathnum = traverse_path_args->pathnum;
    int fts_flags = traverse_path_args->fts_flags;
    guint64 numfiles = 0;

    RmSettings *settings = session->settings;

    char is_ppath = settings->is_ppath[pathnum];
    char *paths[2];

    FTS *ftsp;
    FTSENT *p, *chp;


    info("Now scanning "YEL"\"%s\""NCO" (%spreferred path)...\n",
         settings->paths[pathnum],
         settings->is_ppath[pathnum] ? "" : "non-"
        );

    if (settings->paths[pathnum]) {
        /* convert into char** structure for passing to fts */
        paths[0] = settings->paths[pathnum];
        paths[1] = NULL;
    } else {
        rm_error("Error: no paths defined for traverse_files");
        numfiles = -1;
        goto cleanup;
    }

    if ((ftsp = fts_open(paths, fts_flags, NULL)) == NULL) {
        rm_error("fts_open failed");
        numfiles = -1;
        goto cleanup;
    }

    /* Initialize ftsp */
    chp = fts_children(ftsp, 0);
    if (chp == NULL) {
        warning("fts_children: can't initialise");
        numfiles = -1;
        goto cleanup;
    } else {
        char is_emptydir[MAX_EMPTYDIR_DEPTH];
        bool have_open_emptydirs = false;
        bool clear_emptydir_flags = false;
        memset(&is_emptydir[0], 'N', sizeof(is_emptydir) - 1);
        is_emptydir[sizeof(is_emptydir) - 1] = '\0';

        while (!session->aborted && (p = fts_read(ftsp)) != NULL) {
            switch (p->fts_info) {
            case FTS_D:         /* preorder directory */
                /* TODO: Don't recurse if p->fts_name matches any settings->paths */
                if ( 0
                        ||  ( 1
                              && p->fts_level > 0
                              && matches_toplevel_path (p->fts_path, settings )
                            )
                        || (settings->depth != 0 && p->fts_level >= settings->depth)
                        /* continuing into folder would exceed maxdepth*/
                        || (settings->ignore_hidden && p->fts_level > 0 && p->fts_name[0] == '.')
                   ) {
                    fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                    clear_emptydir_flags = true; /*flag current dir as not empty*/
                } else {
                    is_emptydir[ (p->fts_level + 1) ] = 'E';
                    have_open_emptydirs = true;
                    /* assume dir is empty until proven otherwise */
                }
                break;
            case FTS_DC:        /* directory that causes cycles */
                warning(RED"Warning: filesystem loop detected at %s (skipping)\n"NCO,
                        p->fts_path);
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_DNR:       /* unreadable directory */
                error( 0, p->fts_errno, "Warning: cannot read directory %s", p->fts_path);
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_DOT:       /* dot or dot-dot */
                break;
            case FTS_DP:        /* postorder directory */
                if ( is_emptydir[ (p->fts_level + 1) ] == 'E') {
                    numfiles += process_file(session, p, is_ppath, pathnum, TYPE_EDIR);
                }
                break;
            case FTS_ERR:       /* error; errno is set */
                warning(RED"Warning: error %d in fts_read for %s (skipping)\n"NCO, errno, p->fts_path);
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_INIT:      /* initialized only */
                break;
            case FTS_SLNONE:    /* symbolic link without target */
                warning(RED"Warning: symlink without target: %s\n"NCO, p->fts_path);
                numfiles += process_file(session, p, is_ppath, pathnum, TYPE_BLNK);
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_W:         /* whiteout object */
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_NS:        /* stat(2) failed */
                clear_emptydir_flags = true; /*current dir not empty*/
                warning(RED"Warning: cannot stat file %s (skipping)\n", p->fts_path);
                break;
            case FTS_SL:        /* symbolic link */
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            case FTS_NSOK:      /* no stat(2) requested */
            case FTS_F:         /* regular file */
            case FTS_DEFAULT:   /* any file type not explicitly described by one of the above*/
                clear_emptydir_flags = true; /*current dir not empty*/
                numfiles += process_file(session, p, is_ppath, pathnum, 0); /* this is for any of FTS_NSOK, FTS_SL, FTS_F, FTS_DEFAULT*/
            default:
                clear_emptydir_flags = true; /*current dir not empty*/
                break;
            } /* end switch(p->fts_info)*/
            if (clear_emptydir_flags) {
                /* non-empty dir found above; need to clear emptydir flags for all open levels*/
                if (have_open_emptydirs) {
                    memset(&is_emptydir[0], 'N', sizeof(is_emptydir) - 1);
                    have_open_emptydirs = false;
                }
                clear_emptydir_flags = false;
            }

            /*current dir may not be empty; by association, all open dirs are non-empty*/

        } /*end while ((p = fts_read(ftsp)) != NULL)*/
    }
    if (errno != 0) {
        rm_error("Error '%s': fts_read failed on %s", g_strerror(errno), ftsp->fts_path);
        numfiles = -1;
    }

    fts_close(ftsp);

cleanup:
    g_atomic_int_dec_and_test(&session->activethreads);
    return numfiles;
}

static gpointer traverse_path_list(gpointer data) {
    guint64 numfiles = 0;
    for(GList *iter = data; iter; iter = iter->next) {
        RmTraversePathBuffer *buffer = iter->data;
        numfiles += traverse_path(buffer);
    }
    g_list_free_full(data, g_free);
    pthread_exit(GINT_TO_POINTER(numfiles));
}


/*--------------------------------------------------------------------*/
/* Traverse file hierarchies based on settings contained in session->settings;
 * add the files found into session->list;
 * Return file count if successful.  */

int rm_search_tree(RmSession *session) {
    RmSettings *settings = session->settings;
    guint64 numfiles = 0;
    GError *g_err = NULL;

    /* initialise and launch list builder pool */
    session->list_build_pool = g_thread_pool_new (rm_add_file_to_list,
                                                  session->list,
                                                  settings->threads, /*TODO: rationalise this */
                                                  true, /* share the thread pool */
                                                  &g_err);
    if (g_err != NULL) {
        rm_error("Error %d creating thread pool session->list_build_pool\n", g_err->code);
        return -1;
    }

    pthread_t *thread_ids = g_malloc0((settings->num_paths + 1) * sizeof(pthread_t));
    GHashTable *thread_table = g_hash_table_new(g_direct_hash, g_direct_equal);

    /* Set Bit flags for fts options.  */
    int bit_flags = 0 ;
    if (!settings->followlinks) {
        bit_flags |= FTS_COMFOLLOW | FTS_PHYSICAL;
    } else {
        bit_flags |= FTS_LOGICAL;
    }

    /* don't follow symlinks except those passed in command line */
    if (settings->samepart) {
        bit_flags |= FTS_XDEV;
    }

    /* Code below depends on this */
    settings->threads = MAX(1, settings->threads);

    GList *first_used_list = NULL;

    for(int idx = 0; settings->paths[idx] != NULL; ++idx) {
        if(g_file_test(settings->paths[idx], G_FILE_TEST_IS_REGULAR)) {
            /* Normal file */
            numfiles += process_file(session, NULL, settings->is_ppath[idx], idx, 0);
        } else {
            /* Directory - Traversing needed */
            struct stat stat_buf;
            stat(settings->paths[idx], &stat_buf);

            RmTraversePathBuffer *thread_data = g_new0(RmTraversePathBuffer, 1);
            thread_data->session = session;
            thread_data->pathnum = idx;
            thread_data->fts_flags = bit_flags;

            GList *directories = g_hash_table_lookup(thread_table, GINT_TO_POINTER(stat_buf.st_dev));
            bool one_more = g_atomic_int_get(&session->activethreads) < (gint)settings->threads;

            if(directories == NULL && one_more) {
                directories = g_list_prepend(directories, thread_data);
                g_hash_table_insert(thread_table, GINT_TO_POINTER(stat_buf.st_dev), directories);
                g_atomic_int_inc(&session->activethreads);
                if(first_used_list == NULL) {
                    first_used_list = directories;
                }
            } else if(directories != NULL) {
                directories = g_list_prepend(directories, thread_data);
                g_hash_table_insert(thread_table, GINT_TO_POINTER(stat_buf.st_dev), directories);
            } else {
                /* append, so we do not need to change the head of the list */
                first_used_list = g_list_append(first_used_list, thread_data);
            }

            first_used_list = directories;
        }
    }

    GHashTableIter iter;
    gpointer key = NULL;
    GList *value = NULL;

    g_hash_table_iter_init(&iter, thread_table);
    for(int idx = 0; g_hash_table_iter_next(&iter, &key, (gpointer) &value); idx++) {
        /* If more threads are active, forbid calling chdir behind our back */
        if(g_atomic_int_get(&session->activethreads) > 1) {
            for(GList *iter = value; iter; iter = iter->next) {
                RmTraversePathBuffer *buffer = iter->data;
                buffer->fts_flags |= FTS_NOCHDIR;
            }
        }

        /* TODO: GThreadPool this: */
        if (pthread_create(&thread_ids[idx], NULL, traverse_path_list, value)) {
            rm_error("Error launching traverse_path thread");
        }
    }

    for(int idx = 0; thread_ids[idx]; idx++) {
        gpointer return_data;
        pthread_join(thread_ids[idx], &return_data);
        numfiles += GPOINTER_TO_INT(return_data);
    }

    g_free(thread_ids);
    g_hash_table_unref(thread_table);

    /* wait for list builder pool to finish */
    g_thread_pool_free(session->list_build_pool, false, true);
      /*TODO: do we need to wait or can rmlint continue and let the list build finish in the background?
        (in which case change last arg to false above) */

    return numfiles;
}
