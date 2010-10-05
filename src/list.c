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
			
			if(ptr)
			{
				ptr->last = tmp2; 
			}
			
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



int cmp(iFile *a, iFile *b) {
    return a->fsize - b->fsize;
}




iFile *listsort(iFile *list, int is_circular, int is_double)
 {
    iFile *p, *q, *e, *tail, *oldhead;
    int insize, nmerges, psize, qsize, i;

    /*
     * Silly special case: if `list' was passed in as NULL, return
     * NULL immediately.
     */
    if (!list)
	return NULL;

    insize = 1;

    while (1) {
        p = list;
	oldhead = list;		       /* only used for circular linkage */
        list = NULL;
        tail = NULL;

        nmerges = 0;  /* count number of merges we do in this pass */

        while (p) {
            nmerges++;  /* there exists a merge to be done */
            /* step `insize' places along from p */
            q = p;
            psize = 0;
            for (i = 0; i < insize; i++) {
                psize++;
		if (is_circular)
		    q = (q->next == oldhead ? NULL : q->next);
		else
		    q = q->next;
                if (!q) break;
            }

            /* if q hasn't fallen off end, we have two lists to merge */
            qsize = insize;

            /* now we have two lists; merge them */
            while (psize > 0 || (qsize > 0 && q)) {

                /* decide whether next iFile of merge comes from p or q */
                if (psize == 0) {
		    /* p is empty; e must come from q. */
		    e = q; q = q->next; qsize--;
		    if (is_circular && q == oldhead) q = NULL;
		} else if (qsize == 0 || !q) {
		    /* q is empty; e must come from p. */
		    e = p; p = p->next; psize--;
		    if (is_circular && p == oldhead) p = NULL;
		} else if (cmp(p,q) <= 0) {
		    /* First iFile of p is lower (or same);
		     * e must come from p. */
		    e = p; p = p->next; psize--;
		    if (is_circular && p == oldhead) p = NULL;
		} else {
		    /* First iFile of q is lower; e must come from q. */
		    e = q; q = q->next; qsize--;
		    if (is_circular && q == oldhead) q = NULL;
		}

                /* add the next iFile to the merged list */
		if (tail) {
		    tail->next = e;
		} else {
		    list = e;
		}
		if (is_double) {
		    /* Maintain reverse pointers in a doubly linked list. */
		    e->last = tail;
		}
		tail = e;
            }

            /* now p has stepped `insize' places along, and q has too */
            p = q;
        }
	if (is_circular) {
	    tail->next = list;
	    if (is_double)
		list->last = tail;
	} else
	    tail->next = NULL;

        /* If we have done only one merge, we're finished. */
        if (nmerges <= 1)   /* allow for nmerges==0, the empty list case */
            return list;

        /* Otherwise repeat, merging lists twice the size */
        insize *= 2;
    }
}

void list_msort(void)
{
	start = listsort(start,0,1); 
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
		iFile *prev	= back; 
	

			back=tmp; 
			
			prev->next=tmp; 
			
			tmp->last=prev; 
			tmp->next=NULL;  
		
	}
	list_len++;
}

