#pragma once 
#ifndef rmlint_H
#define rmlint_H
  
#include <stdio.h>
  
typedef struct 
{
	char mode; 
	int depth; 
	char **paths; 
	char *pattern; 
	char *cmd;
	char followlinks; 
	int threads; 
	int verbosity; 
	char samepart;
	
} rmlint_settings; 

rmlint_settings set;

char rmlint_parse_arguments(int argc, char **argv, rmlint_settings *sets);
void rmlint_set_default_settings(rmlint_settings *set);
int  rmlint_main(void);
void die(int status); 

void error(const char* format, ...);
void warning(const char* format, ...);
void info(const char* format, ...);



#endif
