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

#include <glib.h>
#include <error.h>
#include <fts.h>

#include <sys/stat.h>
#include <unistd.h>

#include "preprocess.h"
#include "utilities.h"
#include "file.h"

#define MAX_EMPTYDIR_DEPTH (PATH_MAX / 2) /* brute force option */

#define RM_MAX_LIST_BUILD_POOL_THREADS 20 /* for testing */

///////////////////////////////////////////
// Buffer for starting traversal threads //
///////////////////////////////////////////

/* define data structure for traverse_path(data,userdata)*/
typedef struct RmTraversePathBuffer {
    struct stat *stat_buf;
    char *path;
    short depth;  // NOTE: need this because mountpoints may have different maxdepth to session->settings
    bool is_prefd;
    guint64 path_index;
    dev_t disk; // NOTE: this is disk dev_t, not partition dev_t
    int path_num_for_disk;
} RmTraversePathBuffer;


/* initialiser and destroyer for RmTraversePathBuffer*/
RmTraversePathBuffer *rm_traverse_path_buffer_new(char *path,
        short depth,
        bool is_ppath,
        unsigned long pnum) {
    RmTraversePathBuffer *self = g_new0(RmTraversePathBuffer, 1);
    self->path = g_strdup(path);
    self->depth = depth;
    self->is_prefd = is_ppath;
    self->path_index = pnum;
    self->stat_buf = g_new0(struct stat, 1);
    if ( stat(path, self->stat_buf) == -1) {
        rm_perror(path);
        g_free(self->stat_buf);
        g_assert(self->stat_buf == NULL);
    }
    return self;
}


void rm_traverse_path_buffer_free(gpointer data) {
    RmTraversePathBuffer *self = (gpointer)data;
    g_free(self->path);
    if (self->stat_buf) {
        g_free(self->stat_buf);
    }
    g_free(self);
}

//////////////////////
// Traverse Session //
//////////////////////


/* structure containing all settings relevant to traversal */
typedef struct RmTraverseSession {
    GHashTable *disks_to_traverse;
    GThreadPool *traverse_pool;
    RmUserGroupNode **userlist;
    int fts_flags;
    guint64 numfiles;
    guint64 ignored_files;
    guint64 ignored_folders;
    RmSession *rm_session;
    GHashTable *paths;
    GMutex *mutex;
} RmTraverseSession;



/* Return appropriate fts search flags based on settings. */
int fts_flags_from_settings(RmSettings *settings) {
    int self = 0;
    if (!settings->followlinks) {
        /* don't follow symlinks except those passed in command line */
        self |= FTS_COMFOLLOW | FTS_PHYSICAL;
    } else {
        self |= FTS_LOGICAL;
    } //TODO: this probably leads to false positives for empty dirs - safer to handle it in traverse_path()



    if (settings->samepart) {
        //self |= FTS_XDEV; NOTE: this was causing mountpoints to be flagged as empty dirs; moved this to manual check during traverse_path().
        //TODO: remove
    }

    self |= FTS_NOCHDIR;  /*TODO: we can probably have 1 running with CHDIR optimisations -
                            but need threadpool threads to cooperate for this to work*/
    return self;
}



static int process_file(RmTraverseSession *traverse_session, struct stat *statp, char *path, bool is_ppath, unsigned long pnum, RmLintType file_type) {
    RmSession *session = traverse_session->rm_session;
    RmSettings *settings = session->settings;

    if (file_type == 0) {
        RmLintType gid_check;
        /*see if we can find a lint type*/
        if (settings->findbadids && (gid_check = rm_util_uid_gid_check(statp, traverse_session->userlist))) {
            file_type = gid_check;
        } else if(settings->nonstripped && rm_util_is_nonstripped(path)) {
            file_type = RM_LINT_TYPE_NBIN;
        } else {
            guint64 file_size = statp->st_size;
            if(!settings->limits_specified || (settings->minsize <= file_size && file_size <= settings->maxsize)) {
                if(statp->st_size == 0) {
                    if (!settings->listemptyfiles) {
                        return 0;
                    }
                    file_type = RM_LINT_TYPE_EFILE;
                } else {
                    file_type = RM_LINT_TYPE_DUPE_CANDIDATE;
                }
            } else {
                return 0;
            }
        }
    }

    RmFile *file = rm_file_new(path, statp, file_type, settings->checksum_type, is_ppath, pnum);

    if (file) {
        return rm_file_list_insert(session, file);
    } else {
        rm_error("Problem creating RmFile %s\n", path);
        return 0;
    }
}


/* Traverse the file hierarchies 0named in session->paths[pathnum]. *
 * Returns number of files added to list                           */
void traverse_path(gpointer data, gpointer userdata) {
    RmTraverseSession *traverse_session = (gpointer)userdata;
    RmTraversePathBuffer *traverse_path_args = (gpointer)data;

    char *path = traverse_path_args->path;
    char is_ppath = traverse_path_args->is_prefd;
    short depth = traverse_path_args->depth;
    unsigned long pnum = traverse_path_args->path_index;
    guint64 numfiles = 0;
    RmSession *session = traverse_session->rm_session;
    RmSettings *settings = session->settings;

    if (path == NULL) {
        rm_error("Error: no path defined for traverse_path");
        return;
    }

    if(g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        /* Normal file - process directly without FTS overhead*/
        numfiles += process_file(traverse_session,
                                 traverse_path_args->stat_buf,
                                 path,
                                 is_ppath,
                                 pnum,
                                 0);
    } else if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        rm_error("Error trying to process path %s - not a dir\n", path);
        return;
    } else {
        info(BLU"Starting traversal of %s (index %d on disk %02d:%02d)\n"NCO, path, traverse_path_args->path_num_for_disk,
             major(traverse_path_args->disk),
             minor(traverse_path_args->disk) ); /*TODO: cleanup*/
        int fts_flags = traverse_session->fts_flags;
        /* build char** structure for passing to fts */
        char *ftspaths[2];
        ftspaths[0] = path;
        ftspaths[1] = NULL;


        FTS *ftsp;
        /* Initialize ftsp */
        if ((ftsp = fts_open(ftspaths, fts_flags, NULL)) == NULL) {
            rm_error("fts_open failed");
            return;
        }

        FTSENT *p, *chp;
        chp = fts_children(ftsp, 0);
        if (chp == NULL) {
            warning("fts_children: can't initialise");
            return;
        }

        /* start main processing */
        char is_emptydir[MAX_EMPTYDIR_DEPTH];
        bool have_open_emptydirs = false;
        bool clear_emptydir_flags = false;
        memset(&is_emptydir[0], 'N', sizeof(is_emptydir) - 1);
        is_emptydir[sizeof(is_emptydir) - 1] = '\0';

        while (!traverse_session->rm_session->aborted && (p = fts_read(ftsp)) != NULL) {
            /* check for hidden file or folder */
            if ( 1
                    && settings->ignore_hidden
                    && p->fts_level > 0
                    && p->fts_name[0] == '.') {
                /* ignoring hidden folders*/
                if (p->fts_info == FTS_D) {
                    fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                    g_mutex_lock(traverse_session->mutex);
                    if (traverse_session->ignored_folders++ == 1) {
                        info("Not descending into %s because it is a hidden folder (suppressing future warnings)\n", p->fts_path);
                    }
                    g_mutex_unlock(traverse_session->mutex);
                } else {
                    g_mutex_lock(traverse_session->mutex);
                    traverse_session->ignored_files++;
                    g_mutex_unlock(traverse_session->mutex);
                }
                clear_emptydir_flags = true; /*flag current dir as not empty*/
            } else {
                switch (p->fts_info) {
                case FTS_D:         /* preorder directory */
                    if ( 1
                            && p->fts_level > 0
                            && g_hash_table_contains (traverse_session->paths, p->fts_path)
                       ) {
                        fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                        clear_emptydir_flags = true; /*flag current dir as not empty*/
                        info(BLU"Traverse of %s skipping %s because it will be traversed in another thread\n"NCO, path, p->fts_path);
                    } else if (depth != 0 && p->fts_level >= depth) {
                        /* continuing into folder would exceed maxdepth*/
                        fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                        clear_emptydir_flags = true; /*flag current dir as not empty*/
                        info("Not descending into %s because max depth reached\n", p->fts_path);
                    } else if (settings->samepart && p->fts_dev != chp->fts_dev) {
                        /* continuing into folder would cross file systems*/
                        fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                        clear_emptydir_flags = true; /*flag current dir as not empty*/
                        info("Not descending into %s because it is a different filesystem\n", p->fts_path);
                    } else {
                        /* recurse dir; assume empty until proven otherwise */
                        //info("descending into %s (parent %s)\n",p->fts_path,path);
                        is_emptydir[ (p->fts_level + 1) ] = 'E';
                        have_open_emptydirs = true;
                    }
                    break;
                case FTS_DC:        /* directory that causes cycles */
                    warning(RED"Warning: filesystem loop detected at %s (skipping)\n"NCO,
                            p->fts_path);
                    clear_emptydir_flags = true; /*current dir not empty*/
                    break;
                case FTS_DNR:       /* unreadable directory */
                    error( 0, p->fts_errno, "Warning: cannot read directory %s\n", p->fts_path);
                    clear_emptydir_flags = true; /*current dir not empty*/
                    break;
                case FTS_DOT:       /* dot or dot-dot */
                    break;
                case FTS_DP:        /* postorder directory */
                    if ( 1
                            && is_emptydir[ (p->fts_level + 1) ] == 'E'
                            && settings->findemptydirs
                       ) {
                        numfiles += process_file(traverse_session, p->fts_statp, p->fts_path, is_ppath, pnum, RM_LINT_TYPE_EDIR);
                    }
                    break;
                case FTS_ERR:       /* error; errno is set */
                    warning(RED"Warning: error %d in fts_read for %s (skipping)\n"NCO, errno, p->fts_path);
                    clear_emptydir_flags = true; /*current dir not empty*/
                    break;
                case FTS_INIT:      /* initialized only */
                    break;
                case FTS_SLNONE:    /* symbolic link without target */
                    if (settings->findbadlinks) {
                        numfiles += process_file(traverse_session, p->fts_statp, p->fts_path, is_ppath, pnum, RM_LINT_TYPE_BLNK);
                    }
                    clear_emptydir_flags = true; /*current dir not empty*/
                    break;
                case FTS_W:         /* whiteout object */
                    clear_emptydir_flags = true; /*current dir not empty*/
                    break;
                case FTS_NS:        /* stat(2) failed */
                    clear_emptydir_flags = true; /*current dir not empty*/
                    warning(RED"Warning: cannot stat file %s (skipping)\n"NCO, p->fts_path);
                    break;
                case FTS_SL:        /* symbolic link */
                    clear_emptydir_flags = true; /*current dir not empty*/
                    break;
                case FTS_NSOK:      /* no stat(2) requested */
                case FTS_F:         /* regular file */
                case FTS_DEFAULT:   /* any file type not explicitly described by one of the above*/
                    clear_emptydir_flags = true; /*current dir not empty*/
                    numfiles += process_file(traverse_session, p->fts_statp, p->fts_path, is_ppath, pnum, 0); /* this is for any of FTS_NSOK, FTS_SL, FTS_F, FTS_DEFAULT*/
                    break;
                default:
                    /* unknown case; assume current dir not empty but otherwise do nothing */
                    clear_emptydir_flags = true;
                    rm_error(RED"Unknown fts_info flag %d for file %s\n"NCO, p->fts_info, p->fts_path);
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
            }
        } /*end while ((p = fts_read(ftsp)) != NULL)*/
        if (errno != 0) {
            rm_error("Error '%s': fts_read failed on %s\n", g_strerror(errno), ftsp->fts_path);
        }

        fts_close(ftsp);

        info(GRE"Finished traversing path %s, got %lu files.\n"NCO, traverse_path_args->path, (unsigned long)numfiles);
        //rm_traverse_path_buffer_free(traverse_path_args);
    }

    g_mutex_lock(traverse_session->mutex);
    traverse_session->numfiles += numfiles;
    g_mutex_unlock(traverse_session->mutex);
    rm_traverse_path_buffer_free(traverse_path_args);
}

void traverse_pathlist(gpointer data, gpointer userdata) {
    GList *path_buffers = (gpointer)data;
    RmTraverseSession *traverse_session = (gpointer)userdata;
    g_list_foreach(path_buffers,
                   traverse_path,
                   traverse_session);
}


/* initialise RmTraverseSession */
RmTraverseSession *traverse_session_init(RmSession *session) {
    RmSettings *settings = session->settings;
    RmTraverseSession *self = g_new(RmTraverseSession, 1);
    self->rm_session = session;
    self->numfiles = 0;
    self->ignored_folders = 0;
    self->ignored_files = 0;

    self->fts_flags = fts_flags_from_settings(settings);
    self->userlist = rm_userlist_new();

    /* create empty table of disk pathqueues */
    self->disks_to_traverse = g_hash_table_new_full(g_direct_hash,
                              g_direct_equal,
                              NULL,
                              (GDestroyNotify)g_list_free);

    self->paths = g_hash_table_new_full(g_str_hash,
                                        g_str_equal,
                                        NULL,
                                        NULL );
    self->mutex = g_new0(GMutex, 1);
    g_mutex_init(self->mutex);

    return self;
}

/* detructor for RmTraverseSession */
guint64 traverse_session_join_and_free(RmTraverseSession *traverse_session) {
    if (traverse_session) {
        /* join and destroy threadpools */
        if (traverse_session->traverse_pool) {
            g_thread_pool_free (traverse_session->traverse_pool, false, true);
        }
        if (traverse_session->disks_to_traverse) {
            g_hash_table_destroy(traverse_session->disks_to_traverse);
        }
        if(traverse_session->userlist) {
            rm_userlist_destroy(traverse_session->userlist);
        }
        if(traverse_session->paths) {
            g_hash_table_destroy(traverse_session->paths);
        }

        g_mutex_clear(traverse_session->mutex);
        g_free(traverse_session->mutex);

        info("Found %" G_GUINT64_FORMAT " files, ignored %" G_GUINT64_FORMAT
             " hidden files and %" G_GUINT64_FORMAT " hidden folders\n",
             traverse_session->numfiles,
             traverse_session->ignored_files,
             traverse_session->ignored_folders );


        guint64 numfiles = traverse_session->numfiles;
        g_free(traverse_session);
        return numfiles;
    }
    return 0;
}

void add_path_to_traverse_queue_table (RmTraverseSession *traverse_session, RmTraversePathBuffer *traverse_path_buffer) {

    /* get disk ID */
    dev_t path_disk = 0;
    if (traverse_path_buffer->stat_buf != NULL) {
        path_disk = rm_mounts_get_disk_id(traverse_session->rm_session->mounts,
                                          traverse_path_buffer->stat_buf->st_dev);
    }

    /* if it's a new disk then create new list, else add to existing list*/
    GList *disk_pathlist = g_hash_table_lookup(traverse_session->disks_to_traverse, GINT_TO_POINTER(path_disk));
    if (!disk_pathlist) {
        /* insert new list into hash table */
        disk_pathlist = g_list_append(disk_pathlist, traverse_path_buffer);
        g_hash_table_insert (traverse_session->disks_to_traverse,
                             GINT_TO_POINTER(path_disk),
                             disk_pathlist);
    } else {
        /* add path to list */
        disk_pathlist = g_list_append(disk_pathlist, traverse_path_buffer);
    }


    info("Added %s as path #%d for device %02d:%02d\n",
         traverse_path_buffer->path,
         g_list_length(disk_pathlist),
         major(path_disk),
         minor(path_disk));
}


/*--------------------------------------------------------------------*/
/* Traverse file hierarchies based on settings contained in session->settings;
 * add the files found into session->list;
 * Return file count if successful.  */

guint64 rm_search_tree(RmSession *session) {
    RmSettings *settings = session->settings;

    /* Code won't work with less than 1 threads */
    settings->threads = MAX(1, settings->threads);

    /* set up RmTraverseSession */
    RmTraverseSession *traverse_session = traverse_session_init(session);
    if (traverse_session == NULL) {
        rm_error("NO SESSION");
        /*TODO: die gracefully*/
        return -1;
    }

    /* put list of paths into traverse_session->paths hashtable*/
    /* TODO: do this directly from input args, bypassing settings->paths */
    for(unsigned long idx = 0;  settings->paths[idx] != NULL; ++idx) {

        /* remove trailing / from paths */
        unsigned len = strlen(settings->paths[idx]);
        if (len > 1 && settings->paths[idx][len - 1] == '/') {
            settings->paths[idx][len - 1] = 0;
        }

        if (!g_hash_table_contains (traverse_session->paths, settings->paths[idx]) ) {
            RmTraversePathBuffer *new_path = rm_traverse_path_buffer_new(settings->paths[idx],
                                             settings->depth,
                                             settings->is_prefd[idx],
                                             idx);
            info(BLU"Adding path %s\n"NCO, new_path->path);
            g_hash_table_insert (traverse_session->paths, new_path->path, new_path);
            //TODO: combine preceding with following
            add_path_to_traverse_queue_table(traverse_session, new_path);
        }
    }

    if (!settings->samepart) {
        /* iterate through mountpoints to check (by string matching) if they are subdirs of
           any of the traverse_session->paths elements; if yes then add to
           traverse_session->paths using the same is_ppath and pnum values as the closest
           matched input path */
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init (&iter, session->mounts->part_table);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
            RmPartitionInfo *part_info = value;
            /* check if mt_pt already in traverse path hashtable */
            if (!g_hash_table_contains (traverse_session->paths, part_info->name) ) {
                char *path = g_strdup(part_info->name);
                short depth_diff = 0;
                /* progressively drop char's off the end of path until we match a search path */
                for (unsigned short len = strlen(path) - 1; len > 1; len--) {
                    if (path[len] == '/') {
                        depth_diff++;
                        path[MAX(len, 1)] = 0;
                        RmTraversePathBuffer *bestmatch = (gpointer)g_hash_table_lookup(
                                                              traverse_session->paths,
                                                              path);
                        if (1
                                && bestmatch != NULL
                                && bestmatch->depth - depth_diff > 0
                           ) {
                            /* add the mountpoint to the traverse list hashtable*/
                            RmTraversePathBuffer *new_path = rm_traverse_path_buffer_new((gpointer)part_info->name,
                                                             bestmatch->depth - depth_diff,
                                                             bestmatch->is_prefd,
                                                             bestmatch->path_index
                                                                                        );
                            info(BLU"Adding mountpoint %s as subdir of %s with depth difference %d\n"NCO,
                                 new_path->path, bestmatch->path, depth_diff);

                            g_hash_table_insert (traverse_session->paths, new_path->path, new_path);
                            //TODO: combine preceding with following
                            add_path_to_traverse_queue_table(traverse_session, new_path);
                            break; /* move on to next mount point */
                        }
                    }
                } /* end for idx */
                g_free(path);
            }
        } /* end for mt_pt */
    } /* end if !settings->samepart */

    GError *g_err = NULL;
    unsigned num_disks = g_hash_table_size(traverse_session->disks_to_traverse);
    traverse_session->traverse_pool = g_thread_pool_new (traverse_pathlist, /* func */
                                      traverse_session,                                /* userdata */
                                      MIN(settings->threads, num_disks),      /* max number threads*/
                                      false,                /* don't share the thread pool threads */
                                      &g_err);
    if (g_err != NULL) {
        rm_error("Error %d creating thread pool traverse_session->traverse_pool\n", g_err->code);
        return 0;
    }

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, traverse_session->disks_to_traverse);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        GList *list = value;
        if (list) {
            g_thread_pool_push (traverse_session->traverse_pool,
                                list,
                                NULL);
        }
    }

    guint64 numfiles = traverse_session_join_and_free(traverse_session);
    return numfiles;
}
