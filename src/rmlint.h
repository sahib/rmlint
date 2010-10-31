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
        int depth;
        char **paths;
        char *dpattern;
        char *fpattern;
        char *cmd_path;
        char *cmd_orig;
        char *output; 
        int threads;
        int verbosity;

} rmlint_settings;

rmlint_settings set;

char rmlint_parse_arguments(int argc, char **argv, rmlint_settings *sets);
void rmlint_set_default_settings(rmlint_settings *set);
int  rmlint_main(void);

/** Misc **/
void die(int status);
void error(const char* format, ...);
void warning(const char* format, ...);
void info(const char* format, ...);
int  get_cpindex(void);
void print(iFile *begin);

#endif
