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
#include "filemap.h"

#define MAX_EMPTYDIR_DEPTH (PATH_MAX / 2) /* brute force option */

#define RM_MAX_LIST_BUILD_POOL_THREADS 20 /* for testing */

/* structure containing all settings relevant to traversal */
typedef struct RmTraverseSession {
    GHashTable *disk_table;  /*TODO: combine disk_table into
            disk_mapper_table as disk_mapper_table->used_disks? */
    GThreadPool *traverse_pool;
    RmUserGroupList **userlist;
    int fts_flags;
    guint64 numfiles;
    RmSession *rm_session;
    GHashTable *paths;
} RmTraverseSession;


/* define data structure for traverse_path(data,userdata)*/
typedef struct RmTraversePathBuffer {
    struct stat stat_buf;
    char *path;
    short depth;  // need this because recursive calls have different maxdepth to session->settings
    bool is_ppath;
    unsigned long pnum;
    dev_t disk; //NOTE: this is disk dev_t, not partition dev_t
    int path_num_for_disk;
} RmTraversePathBuffer;


/* initialiser and destroyer for RmTraversePathBuffer*/
RmTraversePathBuffer *rm_traverse_path_buffer_new(char *path,
                                                  short depth,
                                                  bool is_ppath,
                                                  unsigned long pnum,
                                                  RmTraverseSession *trav_session) {
    RmTraversePathBuffer *self = g_new0(RmTraversePathBuffer, 1);
    RmSession *session = trav_session->rm_session;
    dev_t whole_disk = 0;

    self->path = g_strdup(path);
    self->depth = depth;
    self->is_ppath = is_ppath;
    self->pnum = pnum;

    //struct stat stat_buf;
    if ( stat(path, &self->stat_buf) == -1) {
        rm_perror(path);
        self->disk = 0;
        self->path_num_for_disk = 1;
    } else {
        whole_disk = rm_mounts_get_disk_id (session->mounts, self->stat_buf.st_dev);

        /* crude balancing algorithm: paths on each disk are numbered 1,2,3 etc; threadpool sort function will
         * process all the 1's first then move to the 2's etc.  This helps ensure disks get processed in parallel */
        gpointer disk_threads = g_hash_table_lookup(trav_session->disk_table, GINT_TO_POINTER(whole_disk));
        if(disk_threads == NULL) {
            /* create new counter for disk*/
            self->path_num_for_disk = 1;
        } else {
            self->path_num_for_disk = 1 + GPOINTER_TO_INT(disk_threads);
        }
        self->disk = whole_disk;
    }
    info("Adding %s as path #%d for device %02d:%02d\n", path, self->path_num_for_disk, major(whole_disk), minor(whole_disk));
    g_hash_table_replace(trav_session->disk_table, GINT_TO_POINTER(whole_disk), GINT_TO_POINTER(self->path_num_for_disk));

    return self;
}


void rm_traverse_path_buffer_free(gpointer data) {
    RmTraversePathBuffer *self = (gpointer)data;
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

    self |= FTS_NOCHDIR;  /*TODO: we can probably have 1 running with CHDIR optimisations -
                            but need threadpool threads to cooperate for this to work*/
    return self;
}



static int process_file(RmTraverseSession *traverse_session, struct stat *statp, char *path, bool is_ppath, unsigned long pnum, RmLintType file_type) {
    RmSession *session=traverse_session->rm_session;
    RmSettings *settings=session->settings;

    if (file_type == 0) {
        RmLintType gid_check;
        /*see if we can find a lint type*/
        if (settings->findbadids && (gid_check = uid_gid_check(statp, traverse_session->userlist))) {
            file_type = gid_check;
        } else if(settings->nonstripped && is_nonstripped(path)) {
            file_type = TYPE_NBIN;
        } else {
            guint64 file_size = statp->st_size;
            if(!settings->limits_specified || (settings->minsize <= file_size && file_size <= settings->maxsize)) {
                if(statp->st_size == 0) {
                    file_type = TYPE_EFILE;
                } else {
                    file_type = TYPE_DUPE_CANDIDATE;
                }
            } else {
                return 0;
            }
        }
    }

    RmFile *file = rm_file_new(path, statp->st_size, statp->st_ino, statp->st_dev, statp->st_mtim.tv_sec, file_type, is_ppath, pnum);

    if (file) {
        rm_file_list_append(session->list, file);
        { //TODO:  delete this debug message block
            if (file->disk_offsets) {
                info("%sRmFile %s has %d extents, start block %"PRId64", end %"PRId64"\n"NCO,
                    (g_sequence_get_length (file->disk_offsets) > 3 ? BLU : GRE),
                    file->path,
                    g_sequence_get_length (file->disk_offsets),
                    get_disk_offset(file->disk_offsets, 0) / 4096,
                    get_disk_offset(file->disk_offsets, file->fsize) / 4096
                    );
            } else {
                info(YEL"Unable to get offset info for RmFile %s\n"NCO, file->path);
            }
        }
    return 1;
    } else {
    return 0;
    }
}


/* Traverse the file hierarchies 0named in session->paths[pathnum]. *
 * Returns number of files added to list                           */
void traverse_path(gpointer data, gpointer userdata) {
    RmTraverseSession *traverse_session = (gpointer)userdata;
    RmTraversePathBuffer *traverse_path_args = (gpointer)data;

    char *path = traverse_path_args->path;
    char is_ppath = traverse_path_args->is_ppath;
    short depth = traverse_path_args->depth;
    unsigned long pnum = traverse_path_args->pnum;
    guint64 numfiles=0;
    RmSession *session = traverse_session->rm_session;
    RmSettings *settings=session->settings;

    if (path == NULL) {
        rm_error("Error: no path defined for traverse_path");
        return;
    }

    if(g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            /* Normal file - process directly without FTS overhead*/
        numfiles += process_file(traverse_session,
                                &traverse_path_args->stat_buf,
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
            if ( 1
                && settings->ignore_hidden
                && p->fts_level > 0
                && p->fts_name[0] == '.') {
                /* ignoring hidden folders*/
                if (p->fts_info == FTS_D) {
                    fts_set(ftsp, p, FTS_SKIP); /* do not recurse */
                    info("Not descending into %s because it is a hidden folder\n", p->fts_path);
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
                    } else {
                        /* recurse dir; assume empty until proven otherwise */
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
                    error( 0, p->fts_errno, "Warning: cannot read directory %s", p->fts_path);
                    clear_emptydir_flags = true; /*current dir not empty*/
                    break;
                case FTS_DOT:       /* dot or dot-dot */
                    break;
                case FTS_DP:        /* postorder directory */
                    if ( 1
                        && is_emptydir[ (p->fts_level + 1) ] == 'E'
                        && settings->findemptydirs
                        ) {
                        numfiles += process_file(traverse_session, p->fts_statp, p->fts_path, is_ppath, pnum, TYPE_EDIR);
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
                    numfiles += process_file(traverse_session, p->fts_statp, p->fts_path, is_ppath, pnum, TYPE_BLNK);
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
            rm_error("Error '%s': fts_read failed on %s", g_strerror(errno), ftsp->fts_path);
        }

        fts_close(ftsp);

        info(GRE"Finished traversing path %s, got %lu files.\n"NCO, traverse_path_args->path, (unsigned long)numfiles);
        //rm_traverse_path_buffer_free(traverse_path_args);
    }
    pthread_mutex_lock(&traverse_session->rm_session->threadlock);
    traverse_session->numfiles += numfiles;
    pthread_mutex_unlock(&traverse_session->rm_session->threadlock);

}


/* simple load leveller for threadpool; compares the
 * path_num_for_disk values and gives preference to the
 * lower.  This should ensure that every disk gets at least one
 * traverser thread before any other disk gets two */
gint load_leveller(gconstpointer a, gconstpointer b) {
    const RmTraversePathBuffer *abuf = (gconstpointer)a;
    const RmTraversePathBuffer *bbuf = (gconstpointer)b;
    return abuf->path_num_for_disk - bbuf->path_num_for_disk;
}


/* initialise RmTraverseSession */
RmTraverseSession *traverse_session_init(RmSession *session) {
    RmSettings *settings = session->settings;
    RmTraverseSession *self = g_new(RmTraverseSession, 1);
    self->rm_session = session;
    settings = self->rm_session->settings;
    self->numfiles = 0;
    self->fts_flags = fts_flags_from_settings(settings);

    /* create empty table of disk pathqueues */
    self->disk_table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                               NULL,
                                                               NULL);
    self->userlist = userlist_new();
    /* initialise and launch list builder pool */
    GError *g_err = NULL;
    self->traverse_pool = g_thread_pool_new (traverse_path,          /* func */
                               self,                             /* userdata */
                               -1,      /* will later set to number of disks */
                               false, /* don't share the thread pool threads */
                               &g_err);
    if (g_err != NULL) {
        rm_error("Error %d creating thread pool traverse_session->traverse_pool\n", g_err->code);
        return NULL;
    }

    g_thread_pool_set_sort_function (self->traverse_pool, (GCompareDataFunc)load_leveller, NULL);

    self->paths=g_hash_table_new_full(g_str_hash, g_str_equal, NULL, rm_traverse_path_buffer_free);

    return self;
}

/* detructor for RmTraverseSession */
guint64 traverse_session_join_and_free(RmTraverseSession *traverse_session){
    if (traverse_session) {
        /* join and destroy threadpools */
        if (traverse_session->traverse_pool) {
            g_thread_pool_free (traverse_session->traverse_pool, false, true);
        }
        if (traverse_session->disk_table) {
            g_hash_table_destroy(traverse_session->disk_table);
        }
        if(traverse_session->userlist) {
            userlist_destroy(traverse_session->userlist);
        }
        if(traverse_session->paths) {
            g_hash_table_destroy(traverse_session->paths);
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
    unsigned short num_disks = 0;

    /* Code won't work with less than 2 threads */
    settings->threads = MAX(1, settings->threads);

    /* set up RmTraverseSession */
    RmTraverseSession *traverse_session = traverse_session_init(session);
    if (traverse_session == NULL) {
        rm_error("NO SESSION");
        /*TODO: die gracefully*/
    }

    /* put list of paths into traverse_session->paths hashtable*/
    /* TODO: do this directly from input args, bypassing settings->paths */
    for(unsigned long idx = 0;  settings->paths[idx]!= NULL; ++idx) {
        if (!g_hash_table_contains (traverse_session->paths, settings->paths[idx]) ) {
            RmTraversePathBuffer *new_path = rm_traverse_path_buffer_new(settings->paths[idx],
                                           settings->depth,
                                           settings->is_ppath[idx],
                                           idx,
                                           traverse_session);
            if (new_path->path_num_for_disk == 1) {num_disks++;}
            g_hash_table_insert(traverse_session->paths, settings->paths[idx], new_path);
        }
    }

    if (!settings->samepart) {
        /* iterate through mountpoints to check (by string matching) if they are subdirs of
           any of the traverse_session->paths elements; if yes then add to
           traverse_session->paths using the same is_ppath and pnum values as the closest
           matched input path */
        GList *mt_pt;
        for (mt_pt = session->mounts->mounted_paths;
             mt_pt;
             mt_pt=mt_pt->next) {
            char *path = g_strdup(mt_pt->data);
            short depth_diff = 0;
            /* progressively drop char's off the end of path until we match a search path */
            for (unsigned short idx = strlen(path)-1; idx > 1; idx--) {
                if (path[idx] == '/') {
                    depth_diff++;
                    path[idx+1] = 0;
                    RmTraversePathBuffer *bestmatch = (gpointer)g_hash_table_lookup(traverse_session->paths, path);
                    if (bestmatch == NULL) {
                        path[idx] = 0;
                        bestmatch = (gpointer)g_hash_table_lookup(traverse_session->paths, path);
                    }
                    if (1
                        && bestmatch != NULL
                        && bestmatch->depth - depth_diff > 0
                        ) {
                        /* add the mountpoint to the traverse list hashtable*/
                        RmTraversePathBuffer *new_path = rm_traverse_path_buffer_new((gpointer)mt_pt->data,
                                                       bestmatch->depth - depth_diff,
                                                       bestmatch->is_ppath,
                                                       bestmatch->pnum,
                                                       traverse_session);
                        info(BLU"Adding mountpoint %s as subdir of %s with depth difference %d\n"NCO,
                                  new_path->path, bestmatch->path, depth_diff);
                        if (new_path->path_num_for_disk == 1) {
                            /* this is the first traverser to reference this disk */
                            num_disks++;
                        }
                        g_hash_table_insert(traverse_session->paths, new_path->path, new_path);
                        break; /* move on to next mount point */
                    }
                }
            } /* end for idx */
            g_free(path);
        } /* end for mt_pt */
    } /* end if !settings->samepart */

    g_thread_pool_set_max_threads(traverse_session->traverse_pool, MIN(num_disks,settings->threads), NULL);

    /* push pathbufs to traverse pool in increasing order of pathbuf->path_num_for_disk.
     * This is to ensure the first few traversing threads are spread across different disks.
     * Note: while in theory the load_leveller should take care of this, there is a race
     * situation at the start of pushing to the pool which makes it necessary to feed the
     * paths in in this way.   The load_leveller is still needed for the case where the
     * path list is longer than the number of disks */
    bool done=false;
    GHashTableIter paths;
    gpointer key, value;
    for (int path_num_for_disk=1; !done; path_num_for_disk++) {
        done=true; /* assume finished unless we find some more paths matching path_num_for_disk*/
        g_hash_table_iter_init(&paths, traverse_session->paths);
        while (g_hash_table_iter_next (&paths, &key, &value)) {
            RmTraversePathBuffer *pathbuf = (gpointer)value;
            if (pathbuf->path_num_for_disk == path_num_for_disk) {
                done=false;
                info("Pushing %s to traversing pool as path #%d for %srotational disk %s (%02d:%02d)\n",
                         pathbuf->path,
                         pathbuf->path_num_for_disk,
                         rm_mounts_is_nonrotational(session->mounts, pathbuf->disk)
                            ? "non-" : "",
                         rm_mounts_get_name(session->mounts, pathbuf->disk),
                         major(pathbuf->disk), minor(pathbuf->disk));
                g_thread_pool_push(traverse_session->traverse_pool, pathbuf, NULL);
            }
        }
    }

    guint64 numfiles = traverse_session_join_and_free(traverse_session);
    return numfiles;
}

