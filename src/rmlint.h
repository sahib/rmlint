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

#include "list.h"


typedef struct
{
	char is_ppath;
    char *path;
} rmlint_path;

/* all available settings see rmlint -h */
typedef struct
{
    char mode;
    char color;
    char collide;
    char samepart;
    char ignore_hidden;
    char followlinks;
    char casematch;
    char paranoid;
    char invmatch;
    char namecluster;
    char doldtmp;
    char findbadids;
    char searchdup;
    char findemptydirs;
    char nonstripped;
    char verbosity;
    char listemptyfiles;
    rmlint_path *paths;
    /*char *ppath_flags;         NEW - flag for each path to indiacte whether it is a "preferred" original path*/
    char *dpattern;
    char *fpattern;
    char *cmd_path;
    char *cmd_orig;
    char *junk_chars;
    char *output;
    int minsize;
    int maxsize;
    char keep_all_originals;   /*NEW - if set, will ONLY delete dupes that are not in ppath */
    char must_match_original;  /*NEW - if set, will ONLY search for dupe sets where at least one file is in ppath*/
    char invert_original;      /*NEW - if set, inverts selection so that paths _not_ prefixed with // are preferred*/
    nuint_t threads;
    nuint_t depth;
    nuint_t oldtmpdata;

} rmlint_settings;

/* pointer to settings */
rmlint_settings * set;
/* global var for flagging whether current path being walked is */
/* a "preferred" path                                           */
bool is_ppath;
/*nuint_t total_lint = 0;*/


/* These method are also useable from 'outside' */
char rmlint_parse_arguments(int argc, char **argv, rmlint_settings *sets);
void rmlint_set_default_settings(rmlint_settings *set);
int  rmlint_main(void);

/* Misc */
void die(int status);
void print(lint_t *begin);
void info(const char* format, ...);
void error(const char* format, ...);
void warning(const char* format, ...);
char *strdup_printf(const char *format, ...);
int  systemf(const char* format, ...);
int  get_cpindex(void);
nuint_t get_totalfiles(void);
bool get_doldtmp(void);

#endif
