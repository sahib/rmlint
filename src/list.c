#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "list.h"
#include "rmlint.h"

/*** List global ***/
iFile *start = NULL; /* A pointer to the first element in the list */
iFile *back  = NULL; /* A pointer to the last  element in the list */

/** Length of list stored here. This not needed for the implementation itself,
 * but for deciding how may threds to spawn / progress and stuff **/
uint32 len; 

/** list_begin() - returns pointer to start of list **/
iFile *list_begin(void) { return start; }

/** list_end() - returns pointer to start of list **/
iFile *list_end(void) { return back; }

/** Checks if list is empty - Return True if empty. **/
bool list_isempty(void) { return (start) ? false : true; } 

/** Make len visible to other files **/
uint32 list_len(void) { return len; }



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
    len=0; 
	start = NULL;
	back = NULL;
}



iFile *list_remove(iFile *ptr)
{
    iFile *p,*n; 
    
    if(ptr==NULL)
        return NULL; 

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
        back=p; 
    }
    else if(!p&&n)
    {
        n->last=NULL;
        start=n; 
    }
    len--; 
    return n; 
}


/* Init of the element */
static void list_filldata(iFile *pointer, const char *n,uint32 fs, dev_t dev, ino_t node, nlink_t l) 
{
      /* Fill data */
      pointer->plen = strlen(n) + 2; 
      pointer->path = malloc(pointer->plen); 
      strncpy(pointer->path, n, pointer->plen);

      pointer->node = node;
      pointer->dev = dev; 
      pointer->fsize = fs;
      pointer->dupflag = false;
      pointer->filter = true; 
      
      /* Make sure the fp arrays are filled with 0 
	 This is important if a file has a smaller size 
	 than the size read in for the fingerprint - 
 	 The backsum might not be calculated then, what might
 	 cause inaccurate results. 
	*/
      memset(pointer->fp[0],0,MD5_LEN);
      memset(pointer->fp[1],0,MD5_LEN);

      /* Clear the md5 digest array too */
      memset(pointer->md5_digest,0,MD5_LEN);
}


/* Sorts the list after the criteria specified by the (*cmp) callback  */
iFile *list_sort(int (*cmp)(iFile*,iFile*))
 {
    iFile *p, *q, *e, *tail;
    iFile *list = start; 
    int insize, nmerges, psize, qsize, i;

    /*
     * Silly special case: if `list' was passed in as NULL, return
     * NULL immediately.
     */
    if (!list)
	return NULL;

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
                if (!q) break;
            }

            /* if q hasn't fallen off end, we have two lists to merge */
            qsize = insize;

            /* now we have two lists; merge them */
            while (psize > 0 || (qsize > 0 && q))
            {

                /* decide whether next iFile of merge comes from p or q */
                if (psize == 0)
                {
					/* p is empty; e must come from q. */
					e = q; q = q->next; qsize--;

				} else if (qsize == 0 || !q) {
					/* q is empty; e must come from p. */
					e = p; p = p->next; psize--;

				} else if (cmp(p,q) <= 0) {
					/* First iFile of p is lower (or same);
					 * e must come from p. */
					e = p; p = p->next; psize--;

				} else {
					/* First iFile of q is lower; e must come from q. */
					e = q; q = q->next; qsize--;

				}
                /* add the next iFile to the merged list */
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
        len = 1; 
	}
	else 
	{
		iFile *prev	= back; 
		back=tmp; 
		prev->next=tmp; 	
		tmp->last=prev; 
		tmp->next=NULL;
        len++; 
	}
}

/*
 * ToDo: Better sort() :) 
 * 
The structures are the AROS 'Exec list' headers - this is a double linked list with head and tail nodes, where the head and tail nodes overlap and are stored in the same 'header'.

struct MinNode
{
    struct MinNode * mln_Succ,
                   * mln_Pred;
};

struct MinList
{
    struct MinNode * mlh_Head,
                   * mlh_Tail,
                   * mlh_TailPred;
};

Items are placed into a list by embedding the MinNode struct into the object - it doesn't have to be at the start of the object either. But it has the minor drawback that an object can only belong to one list (per MinNode struct) at a time. It does improve locality of reference for list scans though, and saves having an extra redundant pointer.

Then comes the main algorithm. This is apparently a 'natural merge sort'. It scans for runs of already-sorted list items, and keeps track of them in a stack. It then merges runs which have been merged the same number of times - which is a simple calculation and leads to the proper merging sequence.

Special casing the empty list means we don't have to handle any special cases either.

#define MAX_DEPTH (32)

struct stack {
    struct MinNode *head;
    struct MinNode *tail;
    unsigned int size;
};

void SortList(struct MinList *list, LISTCOMP func)
{
    struct stack stack[MAX_DEPTH];
    struct stack *sp = &stack[MAX_DEPTH];
    const struct stack *twostack = &stack[MAX_DEPTH-2];
    struct MinNode *current;

    if (IsListEmpty(list))
        return;

    current = list->mlh_Head;
    while (current != (struct MinNode *)&list->mlh_Tail) {
        struct MinNode *sublist, *next;

        // find a consecutive run of sorted items
        sublist = current;
        next = current->mln_Succ;
        while (next != (struct MinNode *)&list->mlh_Tail) {
            if (func(current, next) > 0)
                break;
            current = next;
            next = current->mln_Succ;
        }

        // insert it into the stack
        sp -= 1;
        sp->head = sublist;
        sp->tail = current;
        sp->size = 1;

        // merge same-merged runs
        while (sp <= twostack
               && sp[0].size == sp[1].size) {

            mergelistslinked(sp[1].head, sp[1].tail, sp[0].head, sp[0].tail, &sp[1].head, &sp[1].tail, func);

            sp[1].size += 1;
            sp += 1;
        }

        current = next;
    }

    // now merge everything left from bottom-up
    while (sp <= twostack) {
        mergelistslinked(sp[1].head, sp[1].tail, sp[0].head, sp[0].tail, &sp[1].head, &sp[1].tail, func);

        sp[1].size += 1;
        sp += 1;
    }

    // no merges - we were already sorted
    if (sp->size != 1) {
        list->mlh_Head = sp->head;
        sp->head->mln_Pred = (struct MinNode *)&list->mlh_Head;
        list->mlh_TailPred = sp->tail;
        sp->tail->mln_Succ = (struct MinNode *)&list->mlh_Tail;
    }
}

And the last bit is the two-way merge. The compiler will inline this nicely so it isn't passing so many arguments around on the stack.

I also tried just treating it as a single-linked list and fixing the back pointers later on. But I guess because of cache effects that proved to be measurably slower, whereas keeping the previous pointers consistent as we go is almost free.

static inline void mergelistslinked(struct MinNode *head1, struct MinNode *tail1,
				    struct MinNode *head2, struct MinNode *tail2,
				    struct MinNode **headp, struct MinNode **tailp, LISTCOMP func)
{
    struct MinNode *head = NULL;
    struct MinNode *tail = (struct MinNode *)&head;

    while (1) {
        if (func(head1, head2) < 0) {
            tail->mln_Succ = head1;
            head1->mln_Pred = tail;
            tail = head1;
            if (head1 == tail1) {
                tail->mln_Succ = head2;
                head2->mln_Pred = tail;
                tail = tail2;
                goto done;
            }
            head1 = head1->mln_Succ;
        } else {
            tail->mln_Succ = head2;
            head2->mln_Pred = tail;
            tail = head2;
            if (head2 == tail2) {
                tail->mln_Succ = head1;
                head1->mln_Pred = tail;
                tail = tail1;
                goto done;
            }
            head2 = head2->mln_Succ;
        }
    }
 done:
    *headp = head;
    *tailp = tail;
}

Timings - 10M nodes

And some simple timings vs glist_sort() which is a simple recursive implementation. Sorting 10M nodes with integers as keys.

Each list is pointing to the same physical objects and using the same comparison function.

struct testnode {
    struct MinNode ln;

    int value;
};

static int cmpnode(void *ap, void *bp)
{
    struct testnode *a = ap;
    struct testnode *b = bp;

    return a->value - b->>alue;
}

4 different scenarios. The items are inserted in-order, then in reverse order, then randomly, and finally they are randomised, and then inserted in order. In-order should be much much faster than glist because it doesn't know if the list is sorted until it drops the last stack frame. Reverse order should be algorithmically worst-case for this implementation, but not make any real difference to glist. And you'd expect random might be a bit better since you will on average have > 1 node in the root list. You wouldn't expect the randomised version to be a lot different to random, although they will have different cache effects.

The times only include the sorting with no setup included.

There are 5 different implementations.

SortList
    Is the MinNode based sorting algorithm above. 
SortGList
    Is the same algorithm as above, but using GList nodes rather than MinNodes. 
g_list_sort
    The glib g_list_sort() call. 
qsort(mn)
    The list items and their key are first copied to an array, the array sorted using libc's qsort() function, and then the list is reconstructed based on the sorted array. 
qsort(GList)
    The same, but using GList nodes as the source and output. 

All results tabulated and sorted based on the last case, as this is the most realstic 'worst case' of input data. i.e. the sort nodes are in order (in memory), but their data isn't (qsort only is there only for comparison).

Implementation       qsort (mn)  qsort (GList)  SortList  SortGlist  g_list_sort  qsort only*

Pre-Sorted                 4.6            5.6      0.14       0.28       4.85         3.9
Inverse-Sorted             6.5            7.5      1.62       2.18       5.08         5.8
Random Insert             11.2           10.3     13.31      17.29      19.76         7.9
Randomised Insert          9.4            9.7     10.85      25.86      32.97         7.9

                          Sorting 10M nodes

  * - only timing the qsort step of the qsort timings - without any setup
      or rebuilding time

Not quite what I expected. Pre-sorted is of course far-faster using the natural merge sort than anything else. I'm not really sure why inverse-sorted is so fast though - apart from locality of reference being very good.

Random Insert and Randomised Insert are interesting cases.

Firstly, for the GList versions randomised insert is much much worse than the random insert. I can only suggest that the first case is much better because as the list gets more sorted the memory access patterns improve in ways that the CPU can take advantage of. They all suffer significantly from the fact that GList has internal list nodes, with a separate data pointer always.

SortList has the opposite result. Ordered nodes are worse! My guess is that because the nodes are ordered exactly sequentially in memory, as they get more sorted you are more often comparing sets of lists with fixed memory offsets - which leads to cache thrashing. I thought this doesn't happen so much with GList because the nodes are allocated using a custom allocator which packs the nodes a bit smaller (no size overhead), and because they occasionally need another super-block, the address offset and list index do not maintain a fixed relationship to each other in a sorted list. But I changed the code to use the same allocator for the min nodes, and observed no noticable difference.
Timings - 100K nodes

Sorting 10M nodes is probably a bit more than your average bit of software needs to do. So if we scale it down a bit (but still something we can time) to 100K nodes, we get something approaching a normal use case.

Implementation     SortList  SortGlist  qsort (mn)  qsort (GList)  g_list_sort  qsort only*

Pre-Sorted           0.002      0.003       0.032          0.041        0.020     0.024
Inverse-Sorted       0.006      0.009       0.041          0.050        0.023     0.033
Random Insert        0.042      0.055       0.069          0.069        0.069     0.049
Randomised Insert    0.029      0.067       0.061          0.067        0.101     0.049

                         Sorting 100K nodes

  * - only timing the qsort step of the qsort timings - without any setup
      or rebuilding time

This has more of an effect of testing the pure algorithm and data structure too, by reducing cache effects.

Still, g_list_sort is still about 3x slower than SortList for the last case. And interestingly - qsort of just the array doesn't beat merge-sorting a double-linked list?
Conclusions

With very large data sets, the cache impacts performance more than anything else. In this situation, the use of internal nodes as GList uses is worst off by far, because every key access requires at least 2 accesses which cannot be anywhere near each other in memory. Even SortList cannot beat the overheads of the setup time for qsort() because once setup, the sorting is executed on a much smaller set of data with better locality.

Even with a much smaller data set, g_list isn't very fast. This is a combination of algorithm and data structure - but mostly data structure. A better algorithm can only attain 150% speedup, a better algorithm and better data structure can obtain a 300% improvement. That is why it is even beaten in some cases by copying the key and node pointer to an array, and sorting that. Here the qsort versions don't do so well as in the larger data set though - the overhead of setting up the array is just overhead, the cache was being effective without needing to extract the keys into an auxilliary table. And interestingly the sort itself is slower - so it is dependent on the algorithm too.
For another time ...

I've also tested many variations on the merge function and the controller function. For example so that the comparison done to find consecutive runs is used to calculate bottom merge instead, and thus converting the code into a normal merge sort. This slows down the sorted case slightly but does speed up the final case a few (worthwhile) %. And the code is marginally simpler than the one above.

Other ideas may be to compare 4 items rather than 2 in the outer loop, or use a 4-way merge for the inner loops. These would add some code complexity however. 
 */
