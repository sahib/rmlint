/**
*  This file is part of rmlint.
*
*  rmlint is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
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
** Hosted on http://github.com/sahib/rmlint
*
**/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "list.h"
#include "rmlint.h"

/* list.c:
   1) Append, sort, ...
*/

lint_t *start = NULL; /* A pointer to the first element in the list */
lint_t *back  = NULL; /* A pointer to the last  element in the list */

/** list_begin() - returns pointer to start of list **/
lint_t *list_begin(void)
{
    return start;
}



/**
 * Free all contents from the list
 * and give back memory.
 **/

void list_clear(lint_t *begin)
{
    lint_t *ptr = begin;
    lint_t *tmp;

    while(ptr)
    {
        /* Save ptr to ftm */
        tmp = ptr;

        /* Next */
        ptr = ptr->next;

        if(ptr)
        {
            ptr->last = NULL;
        }

        /* free ressources */
        if(tmp->path)
        {
            free(tmp->path);
            tmp->path = NULL;
        }

        free(tmp);
        tmp = NULL;
    }
}



lint_t *list_remove(lint_t *ptr)
{
    lint_t *p,*n;
    if(ptr==NULL)
    {
        return NULL;
    }

    p=ptr->last;
    n=ptr->next;
    if(p&&n)
    {
        p->next = n;
        n->last = p;
    }
    else if(p&&!n)
    {
        p->next=NULL;
    }
    else if(!p&&n)
    {
        n->last=NULL;
    }

    free(ptr->path);
    ptr->path = NULL;
    free(ptr);
    ptr = NULL;

    return n;
}


/* Init of the element */
static void list_filldata(lint_t *pointer, const char *n,nuint_t fs, dev_t dev, ino_t node,  bool flag)
{
    /* Fill data */
    short   plen = strlen(n) + 2;
    pointer->path = malloc(plen);
    strncpy(pointer->path, n, plen);

    pointer->node    = node;
    pointer->dev     = dev;
    pointer->fsize   = fs;
    pointer->dupflag = flag;
    pointer->filter  = true;

    /* Make sure the fp arrays are filled with 0
     This is important if a file has a smaller size
     than the size read in for the fingerprint -
     The backsum might not be calculated then, what might
     cause inaccurate results.
    */
    memset(pointer->fp[0],0,MD5_LEN);
    memset(pointer->fp[1],0,MD5_LEN);

    /* Clear the md5 digest array too */
#if BYTE_IN_THE_MIDDLE
    memset(pointer->bim,0,BYTE_MIDDLE_SIZE);
#endif
    memset(pointer->md5_digest,0,MD5_LEN);
}


/* Sorts the list after the criteria specified by the (*cmp) callback  */
lint_t *list_sort(lint_t *begin, long (*cmp)(lint_t*,lint_t*))
{
    lint_t *p, *q, *e, *tail;
    lint_t *list = begin;
    int insize, nmerges, psize, qsize, i;

    /*
     * Silly special case: if `list' was passed in as NULL, return
     * NULL immediately.
     */
    if (!list)
    {
        return NULL;
    }

    insize = 1;

    while (1)
    {
        p = list;
        list = NULL;
        tail = NULL;

        nmerges = 0;  /* count number of merges we do in this pass */

        while (p)
        {
            nmerges++;  /* there exists a merge to be done */
            /* step `insize' places along from p */
            q = p;
            psize = 0;
            for (i = 0; i < insize; i++)
            {
                psize++;
                q = q->next;
                if (!q)
                {
                    break;
                }
            }

            /* if q hasn't fallen off end, we have two lists to merge */
            qsize = insize;

            /* now we have two lists; merge them */
            while (psize > 0 || (qsize > 0 && q))
            {

                /* decide whether next lint_t of merge comes from p or q */
                if (psize == 0)
                {
                    /* p is empty; e must come from q. */
                    e = q;
                    q = q->next;
                    qsize--;

                }
                else if (qsize == 0 || !q)
                {
                    /* q is empty; e must come from p. */
                    e = p;
                    p = p->next;
                    psize--;

                }
                else if (cmp(p,q) <= 0)
                {
                    /* First lint_t of p is lower (or same);
                     * e must come from p. */
                    e = p;
                    p = p->next;
                    psize--;

                }
                else
                {
                    /* First lint_t of q is lower; e must come from q. */
                    e = q;
                    q = q->next;
                    qsize--;

                }
                /* add the next lint_t to the merged list */
                if (tail)
                {
                    tail->next = e;
                }
                else
                {
                    list = e;
                }

                e->last = tail;

                tail = e;
            }

            /* now p has stepped `insize' places along, and q has too */
            p = q;
        }

        tail->next = NULL;

        /* If we have done only one merge, we're finished. */
        if (nmerges <= 1)
        {
            /* allow for nmerges==0, the empty list case */
            start=list;
            return list;
        }
        /* Otherwise repeat, merging lists twice the size */
        insize *= 2;
    }
}



void list_append(const char *n, nuint_t s, dev_t dev, ino_t node,  bool dupflag)
{
    lint_t *tmp = malloc(sizeof(lint_t));
    list_filldata(tmp,n,s,dev,node, dupflag);

    if(start == NULL)   /* INIT */
    {
        tmp->next=NULL;
        tmp->last=NULL;
        start=tmp;
        back=tmp;
    }
    else
    {
        lint_t *prev	= back;
        back=tmp;
        prev->next=tmp;
        tmp->last=prev;
        tmp->next=NULL;
    }
}
