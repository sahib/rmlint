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
#include "mounttable.h"

#define MAX_EMPTYDIR_DEPTH (PATH_MAX / 2) /* brute force option */

#define RM_MAX_LIST_BUILD_POOL_THREADS 20 /* for testing */

/* structure containing all settings relevant to traversal */
typedef struct RmTraverseSession {
    RmMountTable *disk_mapper_table;
    GThreadPool *list_build_pool;
    GThreadPool *traverse_pool;
    GHashTable *disk_queue_table;
    RmUserGroupList **userlist;
    int fts_flags;
    guint64 numfiles;
    RmSession *rm_session;
} RmTraverseSession;

/* defines data structure for rm_add_file_to_list(data,userdata);
 * contains all the info needed to create an RmFile */
typedef struct RmListBuilderBuffer {
    char *path;
    guint64 fsize;
    ino_t node;
    dev_t dev;
    time_t mtime;
    RmLintType linttype;
    bool is_ppath;
    unsigned long pnum;
} RmListBuilderBuffer;

/* define data structure for traverse_path(data,userdata)*/
typedef struct RmTraversePathBuffer {
    char *path;
    short depth;  // need this because recursive calls have different maxdepth to session->settings
    bool is_ppath;
    unsigned long pnum;
} RmTraversePathBuffer;


/* initialiser and destroyer for RmTraversePathBuffer*/
RmTraversePathBuffer *rm_traverse_path_buffer_new(char *path, short depth, bool is_ppath, unsigned long pnum) {
    RmTraversePathBuffer *self = g_new0(RmTraversePathBuffer, 1);
    self->path = g_strdup(path);
    self->depth = depth;
    self->is_ppath = is_ppath;
    self->pnum = pnum;
    return self;
}


void rm_traverse_path_buffer_free(gpointer data) {
    RmTraversePathBuffer *self = (gpointer)data;
    g_free(self->path);
    g_free(self);
}

/* initialiser and destroyer for RmListBuilderBuffer*/
RmListBuilderBuffer *rm_list_builder_buffer_new(char *path,
                        struct stat *stat_buf,
                        RmLintType linttype,
                        bool is_ppath,
                        unsigned long pnum) {
    RmListBuilderBuffer *self = g_new0(RmListBuilderBuffer, 1);
    self->path = g_strdup(path);
    self->fsize = stat_buf->st_size;
    self->node = stat_buf->st_ino;
    self->dev = stat_buf->st_dev;
    self->mtime = stat_buf->st_mtim.tv_sec;
    self->linttype = linttype;
    self->is_ppath = is_ppath;
    self->pnum = pnum;
    return self;
}

void rm_list_builder_buffer_free(RmListBuilderBuffer *self){
    g_free(self->path);
    g_free(self);
}


/* Return appropriate fts search flags based on settings. */
int fts_flags_from_settings(RmSettings *settings) {
    int self = 0;
    if (!settings->followlinks) {
        self |= FTS_COMFOLLOW | FTS_PHYSICAL;
    } else {
        self |= FTS_LOGICAL;
    }
    /* don't follow symlinks except those passed in command line */
    if (settings->samepart) {
        self |= FTS_XDEV;
    }

    self |= FTS_NOCHDIR;  /*TODO: we can have 1 running with CHDIR optimisations -
                              need threadpool threads to cooperate for this to work*/
    return self;
}

/* Threadpool worker for creating RmFiles and adding them to list
 * Why separate thread: to avoid slowing down the FTS traverses
 * Receives: RmListBuilderBuffers from FTS traversal threadpool
 * Work: Creates new RmFile from the info in the RmListBuilderBuffer and appends it to list
 * Signals to other threads: none */
void rm_add_file_to_list (gpointer data, gpointer user_data) {
    struct RmListBuilderBuffer *buf = (gpointer)data;
    RmFileList *list = (gpointer)user_data;

    RmFile *file = rm_file_new(buf->path, buf->fsize, buf->node, buf->dev, buf->mtime, buf->linttype, buf->is_ppath, buf->pnum);

    rm_file_list_append(list, file);
    rm_list_builder_buffer_free(buf);
}


/* structure for passing paths to traverser workers */
typedef struct RmPathQueue {
    GAsyncQueue *queue;
    bool asleep;
    GMutex lock;
    dev_t dev; /* Info only for debugging */
} RmPathQueue;

RmPathQueue *rm_path_queue_new(dev_t dev) {
    RmPathQueue *self = g_new(RmPathQueue, 1);
    self->queue = g_async_queue_new_full(rm_traverse_path_buffer_free);
    self->asleep = false; /* true until traverse pool picks it up */
    g_mutex_init(&self->lock);
    self->dev = dev;
    return self;
}

void rm_path_queue_destroy(gpointer data){
    RmPathQueue *self = (gpointer)data;
    g_mutex_clear(&self->lock);
    g_async_queue_unref(self->queue);
    g_free(self);
}


static int process_file(RmTraverseSession *traverse_session, FTSENT *ent, char *path, bool is_ppath, unsigned long pnum, RmLintType file_type) {

    RmSettings *settings=traverse_session->rm_session->settings;
    GError *g_err = NULL;

    struct RmListBuilderBuffer *buf = NULL;

    struct stat stat_buf;
    struct stat *statp = NULL;

    if(ent == NULL) {
        /* if this is direct file add from command-line argument, then we didn't get here via FTS, *
         * so we don't have statp yet; get it!*/
        stat(path, &stat_buf);
        statp = &stat_buf;
    } else {
        statp = ent->fts_statp;
        path = ent->fts_path;
    }

    if (file_type == 0) {
        RmLintType gid_check;
        /*see if we can find a lint type*/
        if (settings->findbadids && (gid_check = uid_gid_check(statp, traverse_session->userlist))) {
            file_type = gid_check;
        } else if(is_nonstripped(path)) {
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
        buf = rm_list_builder_buffer_new(path,statp,file_type,is_ppath,pnum);
        if (!g_thread_pool_push (traverse_session->list_build_pool, buf, &g_err)) {
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
            buf = rm_list_builder_buffer_new(ent->fts_path,statp,file_type,is_ppath,pnum);
            if (!g_thread_pool_push (traverse_session->list_build_pool, buf, &g_err)) {
                rm_error("Error %d %s pushing RmFile %s to list build pool", g_err->code, g_err->message, ent->fts_path);
            }
            break;
        default:
            break;
        } /* end switch(p->fts_info)*/
    }
    return 1;
}


/* quick check */
bool matches_toplevel_path ( char *filepath, RmTraverseSession *traverse_session) {
    RmSettings *settings = traverse_session->rm_session->settings;
    for ( int i = 0; settings->paths[i] != NULL; i++) {
        if (strcmp(filepath, settings->paths[i]) == 0)
            return true;
    }
    /*TODO: check extra paths encountered */
    return false;
}


/* Push a path to the appropriate queue within a table of device queues.
 * If there is no appropriate device queue for the path, then (a) create it, (b) add it to
 * the table, and (c) push the new queue to the fts traversal threadpool queue */
void push_path_to_queue(char *path,
                        bool have_dev, dev_t dev,
                        short depth,
                        bool is_ppath,
                        unsigned long pnum,
                        RmTraverseSession *trav_session) {

    /* TODO: if depth <> settings->depth then add dev.node to hashtable to prevent loops */

    /*build a RmTraversePathBuffer structure to hold the required data for traverse_path() */
    RmTraversePathBuffer *path_data = rm_traverse_path_buffer_new(path, depth, is_ppath, pnum);

    /* lookup disk associated with the file */
    if (!have_dev) {
        struct stat stat_buf;
        if ( stat(path, &stat_buf) == -1) {
            rm_perror(path);
            return;
        } else {
            dev = stat_buf.st_dev;
        }
    }

    dev_t whole_disk = rm_mounts_get_disk_id (trav_session->disk_mapper_table, dev);
    info("Adding %s to traverse table for device %02d:%02d\n", path, major(whole_disk), minor(whole_disk));

    RmPathQueue *path_queue = g_hash_table_lookup(trav_session->disk_queue_table, GINT_TO_POINTER(whole_disk));
    if(path_queue == NULL) {
        /* create new path queue for this device and add this path to the queue*/
        path_queue = rm_path_queue_new(whole_disk);
        g_async_queue_push(path_queue->queue, (gpointer)path_data);

        /* put a permanent reference to queue into device_table, and push to processing pool*/
        g_hash_table_insert(trav_session->disk_queue_table, GINT_TO_POINTER(whole_disk), path_queue);
        g_thread_pool_push(trav_session->traverse_pool, path_queue, NULL);

    } else {
        g_async_queue_push(path_queue->queue, (gpointer)path_data);
        /* if path_queue has already been processed and is asleep, push if back into the traverse pool */
        if ( g_mutex_trylock(&path_queue->lock) ) {
            /* got lock, so wasn't active, either finished or still in queue*/
            if (path_queue->asleep) {
                /* stale queue (finished processing) - put it back into pool*/
                path_queue->asleep = false;
                g_thread_pool_push(trav_session->traverse_pool, path_queue, NULL);
            }
            g_mutex_unlock(&path_queue->lock);
        }
    }
}


/* Traverse the file hierarchies named in session->paths[pathnum]. *
 * Returns number of files added to list                           */
int traverse_path(RmTraversePathBuffer *traverse_path_args, RmTraverseSession *traverse_session ) {
    char *path = traverse_path_args->path;
    char is_ppath = traverse_path_args->is_ppath;
    short depth = traverse_path_args->depth;
    unsigned long pnum = traverse_path_args->pnum;
    guint64 numfiles=0;
    RmSession *session = traverse_session->rm_session;
    RmSettings *settings=session->settings;

    if (path == NULL) {
        rm_error("Error: no path defined for traverse_path");
        return 0;
    }

    if(g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            /* Normal file - process directly without FTS overhead*/
        return process_file(traverse_session,
                                NULL,
                                path,
                                is_ppath,
                                pnum,
                                0);
    } else if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        rm_error("Error trying to process path %s - not a dir\n", path);
        return 0;
    }

    int fts_flags = traverse_session->fts_flags;

    /* build char** structure for passing to fts */
    char *ftspaths[2];
    ftspaths[0] = path;
    ftspaths[1] = NULL;


    FTS *ftsp;
    /* Initialize ftsp */
    if ((ftsp = fts_open(ftspaths, fts_flags, NULL)) == NULL) {
        rm_error("fts_open failed");
        return 0;
    }

    FTSENT *p, *chp;
    chp = fts_children(ftsp, 0);
    if (chp == NULL) {
        warning("fts_children: can't initialise");
        return 0;
    }

    /* start main processing */
    char is_emptydir[MAX_EMPTYDIR_DEPTH];
    bool have_open_emptydirs = false;
    bool clear_emptydir_flags = false;
    memset(&is_emptydir[0], 'N', sizeof(is_emptydir) - 1);
    is_emptydir[sizeof(is_emptydir) - 1] = '\0';

    while (!traverse_session->rm_session->aborted && (p = fts_read(ftsp)) != NULL) {
        switch (p->fts_info) {
        case FTS_D:         /* preorder directory */
            if ( 0
                    ||  ( 1
                          && p->fts_level > 0
                          && matches_toplevel_path (p->fts_path, traverse_session)
                        )
                    /* we've been here before */
                    || (depth != 0 && p->fts_level >= depth)
                    /* continuing into folder would exceed maxdepth*/
                    || (settings->ignore_hidden && p->fts_level > 0 && p->fts_name[0] == '.')
                    /* not recursing hidden folders*/
               ) {
                fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                clear_emptydir_flags = true; /*flag current dir as not empty*/
            } else if ((p->fts_dev != chp->fts_dev) && false) {
                /* we have encountered a new device, which might be on a different disk;
                 * push this path back to push_path_to_queue for re-dispatch */
                /* have disabled this (?GThreadPool doesn't seem to like recursive pushes?)
                 * TODO: fix                                                         */
                push_path_to_queue(p->fts_path, true, p->fts_dev, depth - p->fts_level, is_ppath, pnum, traverse_session);
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
                numfiles += process_file(traverse_session, p, NULL, is_ppath, pnum, TYPE_EDIR);
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
            numfiles += process_file(traverse_session, p, NULL, is_ppath, pnum, TYPE_BLNK);
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
            numfiles += process_file(traverse_session, p, NULL, is_ppath, pnum, 0); /* this is for any of FTS_NSOK, FTS_SL, FTS_F, FTS_DEFAULT*/
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
    if (errno != 0) {
        rm_error("Error '%s': fts_read failed on %s", g_strerror(errno), ftsp->fts_path);
    }

    fts_close(ftsp);
    return numfiles;

}




/* GThreadPool worker; pop paths (which map to a single disk) from
 * a GAsyncQueue and send them for FTS processing */
void traverse_path_list(gpointer data, gpointer userdata) {
    RmPathQueue *path_queue = (gpointer)data;
    RmTraverseSession *traverse_session = (gpointer)userdata;
    dev_t dev;

    GAsyncQueue *queue = path_queue->queue;
    g_mutex_lock(&path_queue->lock); /* lock only needed to we can't read the asleep bit
                                     * until after while loop has finished */
    RmTraversePathBuffer *path_data;
    while ( (path_data = g_async_queue_try_pop(queue)) != NULL ) {
        guint64 numfiles = traverse_path(path_data, traverse_session);
        dev=path_queue->dev;
        info(GRE"Finished traversing path %s on device %02d:%02d...\n"NCO, path_data->path,
             major(dev), minor(dev));
        rm_traverse_path_buffer_free(path_data);

        pthread_mutex_lock(&traverse_session->rm_session->threadlock);
        traverse_session->numfiles += numfiles;
        pthread_mutex_unlock(&traverse_session->rm_session->threadlock);
    }
    path_queue->asleep=true;
    g_mutex_unlock(&path_queue->lock);
    info(BLU"Finished traversing all paths on device %02d:%02d... putting queue to sleep\n"NCO,
             major(dev), minor(dev));

    /* NOTE: once we exit, the queue for this disk is "asleep" in trav_session->disk_queue_table
     * but can be woken up again if another path on the same disk gets sent to push_path_to_queue */
}




/* initialise RmTraverseSession */
RmTraverseSession *traverse_session_init(RmSession *session) {
    RmSettings *settings = session->settings;
    RmTraverseSession *self = g_new(RmTraverseSession, 1);
    self->rm_session = session;
    settings = self->rm_session->settings;
    self->numfiles = 0;
    self->fts_flags = fts_flags_from_settings(settings);


    /* create table of disks associated with mountpoint dev's */
    self->disk_mapper_table = rm_mounts_table_new();

    /* create empty table of disk pathqueues */
    self->disk_queue_table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                               NULL,
                                                               rm_path_queue_destroy);

    self->userlist = userlist_new();
    /* initialise and launch list builder pool */
    GError *g_err = NULL;
    self->list_build_pool = g_thread_pool_new (rm_add_file_to_list,
                               session->list,
                               settings->threads, /*TODO: check corner case of threads=1*/
                               true, /* share the thread pool */
                               &g_err);
    if (g_err != NULL) {
        rm_error("Error %d creating thread pool traverse_session->list_build_pool\n", g_err->code);
        return NULL;
    }
    g_err = NULL;

    /* initialise and launch traverser pool */
    self->traverse_pool = g_thread_pool_new (traverse_path_list,
                               self,
                               settings->threads,
                               true, /* share the thread pool */
                               &g_err);
    if (g_err != NULL) {
        rm_error("Error %d creating thread pool traverse_session->traverse_pool\n", g_err->code);
        return NULL;
    }

    return self;
}

/* detructor for RmTraverseSession */
guint64 traverse_session_join_and_free(RmTraverseSession *traverse_session){
    if (traverse_session) {
        /* join and destroy threadpools */
        if (traverse_session->traverse_pool) {
            g_thread_pool_free (traverse_session->traverse_pool, false, true);
        }
        if (traverse_session->list_build_pool) {
            g_thread_pool_free (traverse_session->list_build_pool, false, true);
        }

        if (traverse_session->disk_mapper_table) {
            rm_mounts_table_destroy(traverse_session->disk_mapper_table);
        }
        if (traverse_session->disk_queue_table) {
            g_hash_table_destroy(traverse_session->disk_queue_table);
        }
        if(traverse_session->userlist) {
            userlist_destroy(traverse_session->userlist);
        }
        guint64 numfiles=traverse_session->numfiles;
        g_free(traverse_session);
        return numfiles;
    }
    return 0;
}




/*--------------------------------------------------------------------*/
/* Traverse file hierarchies based on settings contained in session->settings;
 * add the files found into session->list;
 * Return file count if successful.  */

int rm_search_tree(RmSession *session) {
    RmSettings *settings = session->settings;

    /* Code won't work with less than 2 threads */
    settings->threads = MAX(2, settings->threads);

    /* set up RmTraverseSession */
    RmTraverseSession *traverse_session = traverse_session_init(session);
    if (traverse_session == NULL) {
        rm_error("NO SESSION");
        /*TODO: die */
    }

    /* iterate through list of paths and send to traverser threadpool*/
    for(int idx = 0; settings->paths[idx] != NULL; ++idx) {
        push_path_to_queue(settings->paths[idx],
                           false,
                           0,
                           settings->depth,
                           settings->is_ppath[idx],
                           idx,
                           traverse_session);
    }

    guint64 numfiles = traverse_session_join_and_free(traverse_session);
    return numfiles;
}

