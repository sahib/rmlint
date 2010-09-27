#include <stdlib.h>
#include "rmlint.h"

int main(int argc, char **argv)
{
	rmlint_set_default_settings(&set); 
	rmlint_parse_arguments(argc,argv,&set); 
	return rmlint_main(); 
}
