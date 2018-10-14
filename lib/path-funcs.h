/**
* This file is part of rmlint.
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
*  - Christopher Pahl <sahib>      2010-2017 (https://github.com/sahib)
*  - Daniel T.        <SeeSpotRun> 2014-2017 (https://github.com/SeeSpotRun)
*  - Michael Witten   <mfwitten>   2018-2018
*
* Hosted on http://github.com/sahib/rmlint
**/

#ifndef RM_PATH_FUNCS_H
#define RM_PATH_FUNCS_H

#include <errno.h>      // errno
#include <string.h>     // strerror
#include <stdbool.h>    // bool, true, false
#include <stdlib.h>     // free, realpath
#include <unistd.h>     // access, faccessat, R_OK
#include <sys/stat.h>   // struct stat, stat, S_ISREG
#include <glib.h>       // g_assert, g_str_has_suffix, GSList,
                        // g_slist_prepend, GDestroyNotify
#if HAVE_FACCESSAT
    #include <fcntl.h>  // AT_FDCWD, AT_EACCESS
#endif

#include "path.h"       // RmPath
#include "config.h"     // _, rm_log_warning_line

static INLINE
void rm_path_free(RmPath *const p) {
    g_assert(p);
    free(p->path);
    g_slice_free(RmPath, p);
}

static INLINE
bool rm_path_is_real(
    const char *const path,
    char **const real_path
) {
    g_assert(path);
    g_assert(real_path);

    if((*real_path = realpath(path, 0))) {
        return true;
    }

    rm_log_warning_line(
        _("Can't get real path for directory or file \"%s\": %s"),
        path, strerror(errno)
    );

    return false;
}

#if HAVE_FACCESSAT
    #define NOT_ACCESSIBLE(path) faccessat(AT_FDCWD, path, R_OK, AT_EACCESS)
#else
    #define NOT_ACCESSIBLE(path) access(path, R_OK)
#endif

static INLINE
bool rm_path_is_accessible(const char *const path) {
    g_assert(path);
    if(NOT_ACCESSIBLE(path)) {
        rm_log_warning_line(
            _("Can't open directory or file \"%s\": %s"),
            path, strerror(errno)
        );
        return false;
    }
    return true;
}

#undef NOT_ACCESSIBLE

static INLINE
bool rm_path_is_valid(
    const char *const path,
    char **const real_path
) {
    g_assert(path);
    g_assert(real_path);

    if(rm_path_is_real(path, real_path)) {
        return rm_path_is_accessible(*real_path);
    }

    rm_log_warning_line(_("Invalid path \"%s\""), path);
    return false;
}

static INLINE
bool rm_path_is_file(const char *const path) {
    g_assert(path);
    struct stat s;
    if(stat(path, &s)) {
        rm_log_warning_line(
            _("Could not get metadata for path \"%s\": %s"),
            path, strerror(errno)
        );
        return false;
    }
    return S_ISREG(s.st_mode);
}

static INLINE
bool rm_path_is_json(const char *const path) {
    g_assert(path);
    return rm_path_is_file(path) && g_str_has_suffix(path, ".json");
}

static INLINE
void rm_path_prepend(
    GSList **const list,
    char *const path,
    const unsigned int index,
    const bool preferred
) {
    g_assert(path);

    RmPath *p = g_slice_new(RmPath);
    p->path = path;
    p->index = index;
    p->is_prefd = preferred;
    p->treat_as_single_vol = (path[0] == '/') && (path[1] == '/');

    *list = g_slist_prepend(*list, p);
}

#endif /* end of include guard */
