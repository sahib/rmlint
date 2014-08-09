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

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <grp.h>

#include <fts.h>
#include <string.h>
#include <sys/stat.h>
#include <libgen.h>

/* Include for ELF processing */
#include <libelf.h>
#include <gelf.h>

#include "cmdline.h"
#include "linttests.h"
#include "defs.h"

char *rm_basename(const char *filename) {
    char *base = strrchr(filename, '/');
    if(base != NULL) {
        /* Return a pointer to the part behind it
         * (which may be the empty string)
         * */
        return base + 1;
    }

    /* It's the full path anyway */
    return (char *)filename;
}

ino_t parent_node(const char *path) {
    char *dummy  = g_strdup(path);
    char *parent_path = dirname(dummy);
    g_free(dummy);

    struct stat stat_buf;
    if(!stat(parent_path, &stat_buf)) {
        return stat_buf.st_ino;
    } else {
        return -1;
    }
}

/* checks uid and gid; returns 0 if both ok, else RM_LINT_TYPE_ corresponding *
 * to RmFile->filter types                                            */
RmLintType uid_gid_check(struct stat *statp, RmUserGroupNode **userlist) {
    bool has_gid, has_uid;
    if (rm_userlist_contains(userlist, statp->st_uid, statp->st_gid, &has_uid, &has_gid)) {
        if(has_gid == false && has_uid == false) {
            return RM_LINT_TYPE_BADUGID;
        } else
        if(has_gid == false && has_uid == true) {
                return RM_LINT_TYPE_BADGID;
        } else
        if(has_gid == true && has_uid == false) {
            return RM_LINT_TYPE_BADUID;
        } 
    }

    return RM_LINT_TYPE_UNKNOWN;
}

/* Method to test if a file is non stripped binary. Uses libelf*/
bool is_nonstripped(const char *path) {
    bool is_ns = false;
    g_return_val_if_fail(path, false);

    /* inspired by "jschmier"'s answer at http://stackoverflow.com/a/5159890 */
    int fd;

    /* ELF handle */
    Elf *elf;       

    /* section descriptor pointer */
    Elf_Scn *scn;   

    /* section header */
    GElf_Shdr shdr; 

    /* Open ELF file to obtain file descriptor */
    if((fd = open(path, O_RDONLY)) == -1) {
        warning("Error opening file '%s' for nonstripped test: ", path);
        rm_perror("");
        return 0;
    }

    /* Protect program from using an older library */
    if(elf_version(EV_CURRENT) == EV_NONE) {
        rm_error("ERROR - ELF Library is out of date!\n");
        return false;
    }

    /* Initialize elf pointer for examining contents of file */
    elf = elf_begin(fd, ELF_C_READ, NULL);

    /* Initialize section descriptor pointer so that elf_nextscn()
     * returns a pointer to the section descriptor at index 1. 
     * */
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

    return is_ns;
}

char * get_username(void) {
    struct passwd *user = getpwuid(geteuid());
    if(user) {
        return user->pw_name;
    } else {
        return NULL;
    }
}

char *get_groupname(void) {
    struct passwd *user = getpwuid(geteuid());
    struct group *grp = getgrgid(user->pw_gid);
    if(grp) {
        return grp->gr_name;
    } else {
        return NULL;
    }
}

RmUserGroupNode **rm_userlist_new(void) {
    struct passwd *node = NULL;
    struct group *grp = NULL;

    GArray *array = g_array_new(TRUE, TRUE, sizeof(RmUserGroupNode *));

    setpwent();
    while((node = getpwent()) != NULL) {
        RmUserGroupNode * item = g_malloc0(sizeof(RmUserGroupNode));
        item->gid = node->pw_gid;
        item->uid = node->pw_uid;
        g_array_append_val(array, item);
    }

    /* add all groups, not just those that are user primary gid's */
    while((grp = getgrent()) != NULL) {
        RmUserGroupNode * item = g_malloc0(sizeof(RmUserGroupNode));
        item->gid = grp->gr_gid;
        item->uid = 0;
        g_array_append_val(array, item);
    }

    endpwent();
    endgrent();
    return (RmUserGroupNode **)g_array_free(array, false);
}

bool rm_userlist_contains(RmUserGroupNode **list, unsigned long uid, unsigned gid, bool *valid_uid, bool *valid_gid) {
    g_assert(list);

    bool rc = false;
    bool gid_found = false;
    bool uid_found = false;

    for(int i = 0; list[i] && rc == false; ++i) {
        if(list[i]->uid == uid) {
            uid_found = true;
        }
        if(list[i]->gid == gid) {
            gid_found = true;
        }

        rc = (gid_found && uid_found);
    }

    if(valid_uid != NULL) {
        *valid_uid = uid_found;
    }

    if(valid_gid != NULL) {
        *valid_gid = gid_found;
    }
    return rc;
}

void rm_userlist_destroy(RmUserGroupNode **list) {
    for(int i = 0; list[i]; ++i) {
        g_free(list[i]);
    }
    g_free(list);
}

#ifdef _RM_COMPILE_MAIN_USERLIST

#define yes(v) (v) ? "True" : "False"

int main(int argc, char *argv[]) {
    struct stat stat_buf;
    bool has_gid, has_uid;
    RmUserGroupNode **list = rm_userlist_new();
    if(argc < 2) {
        puts("Usage: prog <path>");
        return EXIT_FAILURE;
    }
    if(stat(argv[1], &stat_buf) != 0) {
        return EXIT_FAILURE;
    }
    printf("File has UID %lu and GID %lu\n",
           (unsigned long)stat_buf.st_uid,
           (unsigned long)stat_buf.st_gid
    );
    rm_userlist_contains(list, stat_buf.st_uid, stat_buf.st_gid, &has_uid, &has_gid);
    printf("=> Valid UID = %s\n", yes(has_uid));
    printf("=> Valid GID = %s\n", yes(has_gid));
    rm_userlist_destroy(list);
    return EXIT_SUCCESS;
}
#endif
