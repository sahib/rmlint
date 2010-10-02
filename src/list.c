#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "list.h"
#include "rmlint.h"

/*** List global ***/
iFile *start = NULL; /* A pointer to the first element in the list */
iFile *back  = NULL; /* A pointer to the last  element in the list */

uint32 list_len = 0; /* The length of the list is stored here. (Not necessary) */

/** list_begin() - returns pointer to start of list **/
iFile *list_begin(void) { return start; }

/** list_end() - returns pointer to start of list **/
iFile *list_end(void) { return back; }

/** Checks if list is empty - Return True if empty. **/
bool list_isempty(void) { return (start) ? false : true; } 

/** Returns length of list **/
uint32 list_getlen(void) { return list_len; } 


/** 
 * Free all contents from the list 
 * and give back memory.   
 **/ 

void list_clear(void) 
{
	iFile *ptr = start; 
	
	while(ptr)  
	{	
		/* Save ptr to ftm */
		start = ptr; 

		/* Next */
		ptr = ptr->next;
			
		/* free ressources */
		if(start)
		{
			if(start->path) free(start->path);
			start->path = NULL;
			free(start); 
			start = NULL;  
		}
	}
	list_len = 0;
	start = NULL;
}

iFile *list_remove(iFile *ptr)
{
	if(start)
	{
		list_len--;
		if(ptr == start) /* We're at the first element */
		{		
			iFile *tmp = ptr->next; 
			if(tmp == NULL) 
			{
				/* Make list empty */
				free(start); 
				start = NULL;
				back = NULL;
				return NULL;
			}
			/* Free the element */
			tmp->last = NULL;
			
			if(start->path) free(start->path);
			start->path = NULL;
			
			free(start); 
			start = tmp; 

			return start; 
		}
		else if(ptr == back)
		{
			iFile *tmp = back->last;
			iFile *tmp2 = back; 
			 
			tmp->next = NULL; 
			tmp2  = back; 
			back = tmp; 
			
			if(tmp2->path) free(tmp2->path);
			tmp2->path = NULL;
			
			free(tmp2); 
		}
		else
		{ 
			iFile *tmp  = ptr;
			iFile *tmp2 = ptr->last; 
					
			ptr->last->next = ptr->next;  
			ptr = ptr->next; 
			ptr->last = tmp2; 
			
			if(tmp->path) free(tmp->path);
			tmp->path = NULL;
			
			free(tmp); 
			tmp = NULL;
					
			return ptr; 
		}
	}
	return NULL; 
}




/**
 * list_pop() 
 * Remove the first element of the list.
 * 
 * Return value: None. 
 **/ 

void list_pop(void)
{
	/* List empty? */ 
	if(start)
	{
		iFile *pointer = start->next;
		free(start->path);
		start->path = NULL; 
		
		free(start); 
		start = pointer;
		start->last = NULL; 
		list_len--;
	} 
}

static void list_cleardigest(unsigned char* dig) 
{
	int i = 0; 
	for(; i < MD5_LEN; i++)
		dig[i] = 0; 
}


static void list_filldata(iFile *pointer, const char *n,uint32 fs, dev_t dev, ino_t node, nlink_t l) 
{
	  /* Fill data */
      pointer->plen = strlen(n) + 2; 
      pointer->path = malloc(pointer->plen); 
      strncpy(pointer->path, n, pointer->plen);

	  pointer->node = node;
	  pointer->dev = dev; 
	  pointer->links = l; 
      pointer->fsize = fs;
      pointer->dupflag = false;
      pointer->fpc = 0; 
      list_cleardigest(pointer->md5_digest);
}



void list_append(const char *n, uint32 s, dev_t dev, ino_t node, nlink_t l)
{
	iFile *tmp = malloc(sizeof(iFile));
	list_filldata(tmp,n,s,dev,node,l);
	
	if(start == NULL) /* INIT */ 
	{
		tmp->next=NULL;
		tmp->last=NULL;
		start=tmp;
		back=tmp; 
	}
	else 
	{
		/* Find a elem with bigger/same size, and insert tmp before it */
		iFile *curr = start; 
		iFile *prev	= start; 
		while(curr && curr->fsize < s)
		{
			prev=curr; 
			curr=curr->next; 
		}

		if(curr != NULL)
		{
			if(prev==curr)
			{
				tmp->last = NULL;
				tmp->next = start; 
				start->last = tmp; 
				start=tmp; 
			}
			else
			{
				tmp->last=prev;
				tmp->next=curr;
				curr->last=tmp;
				prev->next=tmp; 
			}
		}
		else /* New elem is bigger than everything else */
		{
			back=tmp; 
			
			prev->next=tmp; 
			
			tmp->last=prev; 
			tmp->next=NULL;  
		}
	}
	list_len++;
}

