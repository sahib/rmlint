#ifndef rmlint_H
#define rmlint_H

#include <stdio.h>
#include "list.h"

typedef struct {
        char mode;
        char samepart;
        char dump; 
        char ignore_hidden; 
        char followlinks;
        char casematch;
        char paranoid;
        char invmatch;
		char oldtmpdata;
		char findemptydirs;
        int  depth;
        char **paths;
        char *dpattern;
        char *fpattern;
        char *cmd_path;
        char *cmd_orig;
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
