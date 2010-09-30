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


static void *pool_exec(void *vp)
{
	iFile *t = (iFile *)vp; 
	MD5_CTX con; 
	
	md5_file(t->path, &con);
	memcpy(t->md5_digest,con.digest, MD5_LEN); 
	return NULL;
}



void fillpool(iFile *fp, UINT4 now)
{
	if(!init)
	{
		pool = malloc((set.threads+1) * sizeof(iFile)); 
		th   = malloc((set.threads+1) * sizeof(pthread_t));
		set.threads = (set.threads > list_getlen()-1) ? list_getlen() : set.threads; 
		init = true;
	}
	
	pool[c++] = fp; 
	if(!(now%set.threads) || !(list_getlen()-now)) 
	{
		
		int i;  
		for(i = 0; i < set.threads; i++)
			if(pthread_create(&th[i],NULL,pool_exec,(void*)pool[i]))
				perror(RED"PThread_create"NCO);
		
		for(i = 0; i < set.threads; i++) { 
			if(pthread_join(th[i],NULL))
				perror(RED"PThread_join"NCO);
			
		}
		
		c=0;
	}
}

void freepool(void)
{
	if(pool) free(pool);
	if(th)	 free(th); 
}

