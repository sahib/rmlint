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

#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>

#include "useridcheck.h"
#include "rmlint.h"

/* /////////////////////////// */

UserGroupList ** userlist_new(void) {
    UserGroupList ** list = NULL;
    const size_t block_size = 256;
    size_t mem_count = 0, block_count = 0;
    struct passwd * node = NULL;
    setpwent();
    while((node = getpwent()) != NULL) {
        UserGroupList * item = malloc(sizeof(UserGroupList));
        item->gid = node->pw_gid;
        item->uid = node->pw_uid;
        if(block_count * block_size <= mem_count) {
            block_count++;
            list = realloc(list,(block_count + 1) * block_size * sizeof(UserGroupList*));
        }
        list[mem_count] = item;
        mem_count++;
    }
    if(list != NULL) {
        list[mem_count] = NULL;
    }
    endpwent();
    return list;
}

/* /////////////////////////// */

bool userlist_contains(UserGroupList ** list, unsigned long uid, unsigned gid, bool * valid_uid, bool * valid_gid) {
    bool rc = false;
    bool gid_found = false;
    bool uid_found = false;
    if(list != NULL) {
        int i = 0;
        for(i = 0; list[i] && rc == false; ++i) {
            if(list[i]->uid == uid)
                uid_found = true;
            if(list[i]->gid == gid)
                gid_found = true;
            rc = (gid_found & uid_found);
        }
    }
    if(valid_uid != NULL)
        *valid_uid = uid_found;
    if(valid_gid != NULL)
        *valid_gid = gid_found;
    return rc;
}

/* /////////////////////////// */

void userlist_destroy(UserGroupList ** list) {
    if(list != NULL) {
        int i = 0;
        for(i = 0; list[i]; ++i) {
            free(list[i]);
        }
        free(list);
    }
}

/* /////////////////////////// */

#if 0 /* Uncomment to compile directly */

#define bool2str(v) (v) ? "True" : "False"

int main(int argc, char * argv[]) {
    struct stat stat_buf;
    bool has_gid, has_uid;
    UserGroupList ** list = userlist_new();
    if(argc < 2) {
        puts("Usage: prog <path>");
        return EXIT_FAILURE;
    }
    if(stat(argv[1],&stat_buf) != 0) {
        return EXIT_FAILURE;
    }
    printf("File has UID %lu and GID %lu\n",
           (unsigned long)stat_buf.st_uid,
           (unsigned long)stat_buf.st_gid);
    userlist_contains(list,stat_buf.st_uid,stat_buf.st_gid,&has_uid,&has_gid);
    printf("=> Valid UID = %s\n",bool2str(has_uid));
    printf("=> Valid GID = %s\n",bool2str(has_gid));
    userlist_destroy(list);
    return EXIT_SUCCESS;
}
#endif

