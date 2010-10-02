#pragma once 
#ifndef rmlint_H
#define rmlint_H
  
#include <stdio.h>
  
typedef struct 
{
	char mode; 
	char fingerprint; 
	char samepart;
	char prefilter; 
	char followlinks;
	char casematch; 
	char paranoid; 
	char invmatch; 
	int depth; 
	char **paths; 
	char *dpattern; 
	char *fpattern;
	char *cmd; 
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


#endif
