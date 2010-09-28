#include <stdlib.h>
#include "rmlint.h"

int main(int argc, char **argv)
{
	rmlint_set_default_settings(&set); 
	if(rmlint_parse_arguments(argc,argv,&set) == 0) 
		return -1; 
	
	return rmlint_main(); 
}
