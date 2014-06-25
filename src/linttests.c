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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <fts.h>
#include <string.h>
#include <sys/stat.h>
#include <libgen.h>

/* Include for ELF processing */
#include <libelf.h>
#include <gelf.h>
#include "useridcheck.h"
#include "rmlint.h"

#include "defs.h"

/* ------------------------------------------------------------- */
/* Globals                                                       */
UserGroupList **global_ug_list;

/* ------------------------------------------------------------- */

void delete_userlist(void) {
    userlist_destroy(global_ug_list);
}

/* ------------------------------------------------------------- */

void linttests_c_init(void) {
    global_ug_list = userlist_new();
    atexit(delete_userlist);
}

/* ------------------------------------------------------------- */

char * rmlint_basename(char *filename) {
    char * base = strrchr(filename, '/');
    if(base != NULL) {
        /* Return a pointer to the part behind it
         * (which may be the empty string */
        return base + 1;
    }

    /* It's the full path anyway */
    return (char *)filename;
}

/* ------------------------------------------------------------- */

ino_t parent_node(const char *apath)
{
    char *dummy  = strdup( apath );
    char* parent_path = dirname(dummy);
    struct stat stat_buf;
    if(!stat(parent_path, &stat_buf)) {
        return stat_buf.st_ino;
    }
    else return -1;
}
/* ------------------------------------------------------------- */
/* checks uid and gid; returns 0 if both ok, else TYPE_ corresponding *
 * to RmFile->filter types                                            */
int uid_gid_check(FTSENT *fts_ent, RmSettings *settings) {
    if (settings->findbadids) {
        bool has_gid, has_uid;
        if (userlist_contains(global_ug_list, fts_ent->fts_statp->st_uid,
                              fts_ent->fts_statp->st_gid, &has_uid, &has_gid)) {
            if(has_gid == false)
                if(has_uid == false)
                    return TYPE_BADUGID;
                else
                    return TYPE_BADGID;
            else if (has_uid==false)
                return TYPE_BADUID;
        }
    }
    /* no bad gid or uid */
    return 0;
}

/* ------------------------------------------------------------- */

bool is_old_tmp(FTSENT *fts_ent, RmSettings *settings) {
    bool is_otmp=false;
    if (settings->doldtmp) {
        /* This checks only for *~ and .*.swp */
        size_t len = strlen(fts_ent->fts_path);
        if(fts_ent->fts_path[len-1] == '~' ||
                (len>3 && fts_ent->fts_path[len-1] == 'p'
                 &&fts_ent->fts_path[len-2] == 'w' &&fts_ent->fts_path[len-3] == 's'
                 &&fts_ent->fts_path[len-4] == '.')) {
            char *cpy = NULL;
            struct stat stat_buf;
            if(fts_ent->fts_path[len - 1] == '~') {
                cpy = strndup(fts_ent->fts_path,len-1);
            } else {
                char * p = strrchr(fts_ent->fts_path,'/');
                size_t p_len = p-fts_ent->fts_path;
                char * front = alloca(p_len+1);
                memset(front, '\0', p_len+1);
                strncpy(front, fts_ent->fts_path, p_len);
                cpy = strdup_printf("%s/%s",front,p+2);
                cpy[strlen(cpy)-4] = 0;
            }
            if(!stat(cpy, &stat_buf)) {
                if((fts_ent->fts_statp->st_mtim.tv_sec - stat_buf.st_mtime) >= (unsigned)settings->oldtmpdata) {
                    is_otmp = true;
                }
            }
            if(cpy) {
                free(cpy);
            }
        }

    }
    return is_otmp;
}

/* ------------------------------------------------------------- */

/* Method to test if a file is non stripped binary. Uses libelf*/
bool is_nonstripped(FTSENT *fts_ent, RmSettings *settings) {
    bool is_ns=false;
    if ((settings->nonstripped) && fts_ent->fts_path) {
        /* inspired by "jschmier"'s answer at http://stackoverflow.com/a/5159890 */
        int fd;
        /*char *escapedpath = strsubs(fts_ent->fts_path,"'","'\"'\"'");*/

        Elf *elf;       /* ELF pointer for libelf */
        Elf_Scn *scn;   /* section descriptor pointer */
        GElf_Shdr shdr; /* section header */

        /* Open ELF file to obtain file descriptor */
        if((fd = open(fts_ent->fts_path, O_RDONLY)) < 0) {
            warning("Error opening file %s for nostripped test\n", fts_ent->fts_path);
            return 0;
        }

        /* Protect program from using an older library */
        if(elf_version(EV_CURRENT) == EV_NONE) {
            error("ERROR - ELF Library is out of date!\n");
            exit(EXIT_FAILURE);
        }

        /* Initialize elf pointer for examining contents of file */
        elf = elf_begin(fd, ELF_C_READ, NULL);

        /* Initialize section descriptor pointer so that elf_nextscn()
         * returns a pointer to the section descriptor at index 1. */
        scn = NULL;

        /* Iterate through ELF sections */
        while((scn = elf_nextscn(elf, scn)) != NULL) {
            /* Retrieve section header */
            gelf_getshdr(scn, &shdr);

            /* If a section header holding a symbol table (.symtab)
             * is found, this ELF file has not been stripped. */
            if(shdr.sh_type == SHT_SYMTAB) {
                is_ns = true;
                break;
            }
        }

        elf_end(elf);
        close(fd);
    }
    return is_ns;
}

/* ------------------------------------------------------------- */

/* Cheap function to check if c is a char in str */
bool junkinbasename(char *path, RmSettings * settings) {
    if(settings->junk_chars != NULL ) {
        int i = 0, j = 0;
        char * base_name = rmlint_basename(path);
        for(; settings->junk_chars[i]; i++) {
            for(j=0; base_name[j]; j++) {
                if(base_name[j] == settings->junk_chars[i]) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* ------------------------------------------------------------- */

