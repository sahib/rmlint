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

#ifndef rmlint_H
#define rmlint_H

#include <stdio.h>
#include "list.h"

typedef struct {
        char mode;
        char samepart;
        char ignore_hidden;
        char followlinks;
        char casematch;
        char paranoid;
        char invmatch;
        char namecluster;
        char oldtmpdata;
        char searchdup;
        char findemptydirs;
        char nonstripped;
        int  depth;
        char **paths;
        char *dpattern;
        char *fpattern;
        char *cmd_path;
        char *cmd_orig;
        char *junk_chars;
        char *output;
        int threads;
        int verbosity;

} rmlint_settings;

/* global var storing all settings */
rmlint_settings set;

/* These method are also useable from 'outside' */
char rmlint_parse_arguments(int argc, char **argv, rmlint_settings *sets);
void rmlint_set_default_settings(rmlint_settings *set);
int  rmlint_main(void);

/* Misc */
void die(int status);
void print(iFile *begin);
void info(const char* format, ...);
void error(const char* format, ...);
void warning(const char* format, ...);
int  systemf(const char* format, ...);
int  get_cpindex(void);

#endif
