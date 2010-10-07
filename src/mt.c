/**
*  This file is part of rmlint.
*
*  rmlint is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
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
** Hosted at the time of writing (Do 30. Sep 18:32:19 CEST 2010):
*  http://github.com/sahib/rmlint
*   
**/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h> 

#include "rmlint.h"
#include "mt.h"
#include "list.h"

int c = 0; 
bool init = false; 
iFile **pool = NULL; 
pthread_t *th = NULL;


void freepool(void)
{
	if(pool) free(pool);
	if(th)	 free(th); 
}

