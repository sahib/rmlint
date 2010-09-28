#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "list.h"
#include "rmlint.h"

/*** List global ***/
iFile *start = NULL; /* A pointer to the first element in the list */
iFile *back  = NULL; /* A pointer to the last  element in the list */

UINT4 list_len = 0; /* The length of the list is stored here. (Not necessary) */

/** list_begin() - returns pointer to start of list **/
iFile *list_begin(void) { return start; }

/** list_end() - returns pointer to start of list **/
iFile *list_end(void) { return back; }

/** Checks if list is empty - Return True if empty. **/
bool list_isempty(void) { return (start) ? false : true; } 

/** Returns length of list **/
UINT4 list_getlen(void) { return list_len; } 


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


static void list_filldata(iFile *pointer, const char *n,UINT4 fs) 
{
	  /* Fill data */
      pointer->plen = strlen(n) + 2; 
      pointer->path = malloc(pointer->plen); 
      strncpy(pointer->path, n, pointer->plen);

      pointer->fsize = fs;
      pointer->dupflag = false;
      pointer->fpc = 0; 
      list_cleardigest(pointer->md5_digest);
}

/**
 * list_append()
 * 
 * Append will add a file to investigate to the ToDo list. 
 * If a list is empty (*start being NULL) a new one 
 * will be allocated. 
 * 
 * Errors: Returns if not enough memory.
 * Return value: None. 
 **/ 
 
void list_append(const char *n, UINT4 filesize)
{
   iFile *pointer;
   if(!n) return; 
   
   /* It's empty? Bring life to it! */
   if(start == NULL) 
   {
      /* Alloc memory */ 
      if((start = malloc(sizeof(iFile))) == NULL) 
      {
         error("Not enough memory!\n");
         return;
      }
      
      /** Copy the data **/
	  list_filldata(start,n,filesize);

	  /* Set end flag to the next elem */
      start->next=NULL;
      start->last=NULL;
      back = start; 
   }
   else
   {
	   /* Start from the beginning */
	   pointer = start; 
	   
	   /* Find a element with bigger size as filesize */	    
	   while(pointer->next && filesize > pointer->fsize)
		   pointer = pointer->next; 	
	   
	   if(pointer->next == NULL) 
		back = pointer; 
	   
		   if(pointer == start) /* Start */
		   {   			   
			   if((pointer->last = malloc(sizeof(iFile))) == NULL) 
			   {
				  error(RED"Not enough memory to append element :-(\n"NCO);
				  return;
			   }
			   pointer = pointer->last; 
			   
			   list_filldata(pointer,n,filesize);
			   
			   pointer->next = start;
			   pointer->last = NULL;
			  
			   start->last = pointer;  
			   start = pointer; 
				
		   }
		   else if(pointer->fsize) /* Insert somewhere in the middle */
		   {
			   iFile *tmp = NULL; 
			   if((tmp = malloc(sizeof(iFile))) == NULL) 
			   {
				  error(RED"Not enough memory to append element :-(\n"NCO);
				  return;
			   }		   
			   list_filldata(tmp,n,filesize); 
			  
			   tmp->next = pointer;
			   tmp->last = pointer->last; 
			   
			   pointer->last->next = tmp; 
			   pointer->last = tmp;		   
		   }   
   } 
   list_len++;
}
