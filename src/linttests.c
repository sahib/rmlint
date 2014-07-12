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

char *rm_basename(char *filename) {
    char *base = strrchr(filename, '/');
    if(base != NULL) {
        /* Return a pointer to the part behind it
         * (which may be the empty string */
        return base + 1;
    }

    /* It's the full path anyway */
    return (char *)filename;
}

/* returns pointer to full path name - note this allocate space,
 * so calling procedure needs to free this */
char *rm_fullname(const char *iwd, const char *filename) {
    char *result;
    if (filename[0] == '/') {
        /* It's the full path anyway */
        result = strdup(filename);
        }
    else {
        result = malloc(strlen(filename)+strlen(iwd)+1);
        strcpy(result, iwd);
        strcat(result, filename);
    }
    return (char *)result;
}

ino_t parent_node(const char *apath) {
    char *dummy  = strdup( apath );
    char *parent_path = dirname(dummy);
    struct stat stat_buf;
    if(!stat(parent_path, &stat_buf)) {
        return stat_buf.st_ino;
    } else {
        return -1;
    }
}

/* checks uid and gid; returns 0 if both ok, else TYPE_ corresponding *
 * to RmFile->filter types                                            */
int uid_gid_check(G_GNUC_UNUSED const char *path, struct stat *statp, RmSession *session) {
    if (session->settings->findbadids) {
        bool has_gid, has_uid;
        if (userlist_contains(
                    session->userlist, statp->st_uid,
                    statp->st_gid, &has_uid, &has_gid)
           ) {
            if(has_gid == false) {
                if(has_uid == false) {
                    return TYPE_BADUGID;
                } else {
                    return TYPE_BADGID;
                }
            } else if (has_uid == false) {
                return TYPE_BADUID;
            }
        }
    }
    /* no bad gid or uid */
    return 0;
}

/* Method to test if a file is non stripped binary. Uses libelf*/
bool is_nonstripped(const char *path, G_GNUC_UNUSED struct stat *statp,  RmSettings *settings) {
    bool is_ns = false;
    if ((settings->nonstripped) && path) {
        /* inspired by "jschmier"'s answer at http://stackoverflow.com/a/5159890 */
        int fd;

        Elf *elf;       /* ELF pointer for libelf */
        Elf_Scn *scn;   /* section descriptor pointer */
        GElf_Shdr shdr; /* section header */
        static char CWD_BUF[PATH_MAX];

        char *abs_path = g_build_filename(getcwd(CWD_BUF, PATH_MAX), rm_basename((char *)path), NULL);
        /* TODO: will the above work for all cases, eg NOCHDIR case with multi threads*/

        /* Open ELF file to obtain file descriptor */
        if((fd = open(abs_path, O_RDONLY)) < 0) {
            warning("Error opening file '%s' for nostripped test: ", path);
            perror("");
            g_free(abs_path);
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
        g_free(abs_path);
    }
    return is_ns;
}
