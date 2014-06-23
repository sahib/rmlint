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

#include "defs.h"


/* pointer to settings */
rmlint_settings * set;



/* These method are also useable from 'outside' */
char is_ppath(const char* apath);
char rmlint_parse_arguments(int argc, char **argv, rmlint_settings *sets);
char rmlint_echo_settings(rmlint_settings *settings);
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
