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
#include <errno.h>
#include <stdlib.h>
#include "treesearch.h"
#include "filter.h"
#include <sys/stat.h>

static int process_file (FTSENT *ent, bool is_ppath, int pnum, int RmFileype) {
    struct timespec mtime=ent->fts_statp->st_mtim;

    //size_t level;

    /*TODO: regex check if filename exclude*/
    if (!RmFileype) {
        /*see if we can find a lint type*/
        if (junkinstr(rmlint_basename(ent->fts_path)))
            RmFileype=TYPE_JNK_FILENAME;
        else if (set->findbadids) {
            bool has_gid, has_uid;
            if(userlist_contains(global_ug_list,ent->fts_statp->st_uid,ent->fts_statp->st_gid,&has_uid,&has_gid) == false) {
                if(has_gid == false)
                    if(has_uid == false)
                        RmFileype=TYPE_BADUGID;
                    else
                        RmFileype=TYPE_BADGID;
                else if (has_uid=false)
                    RmFileype=TYPE_BADUID;
            }
        } else if(set->doldtmp) {
            /* This checks only for *~ and .*.swp */
            size_t len = strlen(ent->fts_path);
            if(ent->fts_path[len-1] == '~' ||
                    (len>3 && ent->fts_path[len-1] == 'p'
                     &&ent->fts_path[len-2] == 'w' &&ent->fts_path[len-3] == 's'
                     &&ent->fts_path[len-4] == '.')) {
                char *cpy = NULL;
                struct stat stat_buf;
                if(ent->fts_path[len - 1] == '~') {
                    cpy = strndup(ent->fts_path,len-1);
                } else {
                    char * p = strrchr(ent->fts_path,'/');
                    size_t p_len = p-ent->fts_path;
                    char * front = alloca(p_len+1);
                    memset(front, '\0', p_len+1);
                    strncpy(front, ent->fts_path, p_len);
                    cpy = g_strdup_printf("%s/%s",front,p+2);
                    cpy[strlen(cpy)-4] = 0;
                }
                if(!stat(cpy, &stat_buf)) {
                    if(mtime.tv_sec - stat_buf.st_mtime >= set->oldtmpdata) {
                        RmFileype=TYPE_OTMP;
                    }
                }
                if(cpy) {
                    free(cpy);
                }
            }
        } else if(set->nonstripped) {
            if(check_binary_to_be_stripped(ent->fts_path)) {
                RmFileype = TYPE_NBIN;
            }
        }

    }

    if (RmFileype) {
        info("Adding lint type %d %s\n",RmFileype, ent->fts_path);
        list_append(ent->fts_path, ent->fts_statp->st_size ,
                    mtime, ent->fts_statp->st_dev,
                    ent->fts_statp->st_ino, RmFileype, is_ppath, pnum );
    } else {

    }

    if (RmFileype==TYPE_DUPE_CANDIDATE) {
        switch (ent->fts_info) {
        case FTS_F:         /* regular file */
        case FTS_NSOK:      /* no stat(2) requested */
        case FTS_SL:        /* symbolic link */
        case FTS_DEFAULT:   /* none of the above */
            info("Adding normal file %s\n",ent->fts_path);
            list_append(ent->fts_path, ent->fts_statp->st_size ,
                        mtime,
                        ent->fts_statp->st_dev,
                        ent->fts_statp->st_ino, 1, is_ppath, pnum );
            break;
        default:
            break;
        } /* end switch(p->fts_info)*/
    }

    return 1;
}

/* Traverse the file hierarchies named in PATHS, the last entry of which
 * is NULL.  FTS_FLAGS controls how fts works.
 * Return true if successful.  */

int traverse_path (RmSettings  *settings, int  pathnum, int fts_flags) {
    int numfiles = 0;
    int dir_file_counter = 0;
    char is_ppath = settings->is_ppath[pathnum];
    char** paths=malloc(sizeof(char*)*2);
    FTS *ftsp;
    FTSENT *p, *chp;

    if (settings->paths[pathnum]) {
        /* convert into char** structure for passing to fts */
        paths[0]=settings->paths[pathnum];
        paths[1]=NULL;
    } else {
        error("Error: no paths defined for traverse_files");
        return -1;
    }

    if ((ftsp = fts_open(paths, fts_flags, NULL)) == NULL) {
        error("fts_open failed");
        return -1;
    }

    /* Initialize ftsp */
    chp = fts_children(ftsp, 0);
    if (chp == NULL) {
        return 0;  /* no files to traverse */
    }
    while (!iAbort && (p = fts_read(ftsp)) != NULL) {
        dir_file_counter++;
        switch (p->fts_info) {
        case FTS_D:         /* preorder directory */
            if(junkinstr(rmlint_basename(p->fts_path))) {
                process_file(p, is_ppath, pathnum, TYPE_JNK_DIRNAME);
            }
            if (
                (settings->depth!=0 && p->fts_level>=settings->depth) ||
                /* continuing into folder would exceed maxdepth*/
                (settings->ignore_hidden && p->fts_level > 0 && p->fts_name[0] == '.')
                /* ignoring hidden folders */
            ) {
                fts_set(ftsp,p,FTS_SKIP);
            } else {
                dir_file_counter=0;
            }
            break;
        case FTS_DC:        /* directory that causes cycles */
            warning(RED"Warning: filesystem loop detected between:\nskipping:\t%s\n(same as):\t%s\n"NCO,
                    p->fts_path, p->fts_cycle->fts_path );
            break;
        case FTS_DNR:       /* unreadable directory */
            warning(RED"Warning: cannot read directory %s (skipping)\n"NCO, p->fts_path);
            break;
        case FTS_DOT:       /* dot or dot-dot */
            break;
        case FTS_DP:        /* postorder directory */
            if (dir_file_counter==0) {
                numfiles += process_file(p, is_ppath, pathnum, TYPE_EDIR);
            }
            break;
        case FTS_ERR:       /* error; errno is set */
            warning(RED"Warning: error %d in fts_read for %s (skipping)\n"NCO, errno, p->fts_path);
            break;
        case FTS_INIT:      /* initialized only */
            break;
        case FTS_SLNONE:    /* symbolic link without target */
            warning(RED"Warning: symlink without target: %s\n"NCO, errno, p->fts_path);
            numfiles += process_file(p, is_ppath, pathnum, TYPE_BLNK);
            break;
        case FTS_W:         /* whiteout object */
            break;
        case FTS_NS:        /* stat(2) failed */
            warning(RED"Warning: cannot stat file %s (skipping)\n", p->fts_path);
        case FTS_NSOK:      /* no stat(2) requested */
        case FTS_SL:        /* symbolic link */
        case FTS_F:         /* regular file */
        case FTS_DEFAULT:   /* any file type not explicitly described by one of the above*/
            numfiles += process_file(p, is_ppath, pathnum, 0); /* this is for any of FTS_NSOK, FTS_SL, FTS_F, FTS_DEFAULT*/
        default:
            break;
        } /* end switch(p->fts_info)*/
    } /*end while ((p = fts_read(ftsp)) != NULL)*/

    if (errno != 0) {
        error ("Error %d: fts_read failed: %s", 0, errno, ftsp->fts_path);
        numfiles = -1;
    }

    fts_close(ftsp);
    return numfiles;

    return numfiles;
}

/*--------------------------------------------------------------------*/
/* Traverse file hierarchies based on settings contained in SETTINGS;
 * add the files found into LIST
 * Return file count if successful.  */

int rmlint_search_tree( RmSettings *settings) { /*, rmlint_filelist *list)*/
    int numfiles=0;
    int cpindex=0;
    /* Set Bit flags for fts options.  */
    int bit_flags = 0;
    if (!settings->followlinks)
        bit_flags|=FTS_COMFOLLOW | FTS_PHYSICAL;
    /* don't follow symlinks except those passed in command line*/
    if (settings->samepart)
        bit_flags|=FTS_XDEV;

    while(settings->paths[cpindex] != NULL) {
        /* The path points to a dir - recurse it! */
        info("Now scanning "YEL"\"%s\""NCO"..",settings->paths[cpindex]);
        if (settings->is_ppath[cpindex])
            info("(preferred path)");
        else
            info("(non-preferred path)");
        numfiles += traverse_path (settings, cpindex, bit_flags);
        info(" done: %d files added.\n", numfiles);

        cpindex++;
    }

    /* TODO: free up memory */
    info ("Exiting rmlint_search_tree with %d files added", numfiles);
    return (numfiles);
}
