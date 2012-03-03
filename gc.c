#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

/**
 * gc.c: An example garbage collecter written purely in C using a
 * mark and sweep algorithm. Also the functions aren't wrappers
 * around the standard library allocation functions; they actually
 * work independently using linux system calls (thanks K&R!).
 *
 * This uses a lot of modified K&R example code, but it's mostly
 * my own. Actually I should probably check that.
 *
 * Not only is this incredibly dependent on Unix, it's even more
 * dependent on Linux.
 *
 * Notice how slow it is? Oh I totally bet you do! Also, it is worth
 * mentioning it might not actually work.
 *
 * Lasciate ogne speranza, voi ch'intrate.
 *
 * (C) 2012 Matthew Plant, thanks to K&R. Put under the GPL version 3
 */

typedef union header {
  /* TODO: add a pointer in the header to the last address that
   * marked the data in the previous collection cycle. Also, adding
   * an "atomic" flag would be good. 
   */
  struct {
    _Bool marked; /* Screw single bits! */
    size_t size;
    void **prev_reff;
    union header *next;
  };
  long double align; /* Most restrictive type, maybe? */
} header_t;

static header_t base;
static header_t *usedp, *freep; 
static unsigned long stack_bottom;

size_t GC_heap_size = 0; /* The heap size, in blocks. */

void GCFree (void *);
static header_t *morecore (size_t);
static void add_to_free_list (header_t *);

void GCInit (void)
{
  static _Bool initted;
  FILE *statfp;

  if (initted)
    return;

  initted = true;
  /* Yes, it is THAT dependent on Linux. */ 
  statfp = fopen ("/proc/self/stat", "r");
  assert (statfp != NULL);
  fscanf (statfp,
	  "%*d %*s %*c %*d %*d %*d %*d %*d %*u "
	  "%*lu %*lu %*lu %*lu %*lu %*lu %*ld %*ld "
	  "%*ld %*ld %*ld %*ld %*llu %*lu %*ld "
	  "%*lu %*lu %*lu %lu", &stack_bottom);
  fclose (statfp);
  usedp = NULL;
  base.next = freep = &base;
  base.size = 0;
}

_init (void)
{
  GCInit ();
}

void *GCMalloc (size_t alloc_size)
{
  size_t num_units;
  header_t *p, *prevp;

  num_units = (alloc_size + sizeof (header_t) - 1) / sizeof (header_t) + 1;  
  prevp = freep;

  for (p = prevp->next;; prevp = p, p = p->next) {
    if (p->size >= num_units) { /* Big enough. */
      if (p->size == num_units) /* Exact size. */
	prevp->next = p->next;
      else {
	p->size -= num_units;
	p += p->size;
	p->size = num_units;
      }
      freep = prevp;
      /* Add to p to the used list. */
      if (usedp == NULL)	
	usedp = p->next = p;
      else {
	p->next = usedp->next;
	usedp->next = p;
      }
      p->marked = false;
      p->prev_reff = NULL;
      GC_heap_size += num_units;
      return (void *) (p + 1);
    }
    if (p == freep) { /* Not enough memory. */
      p = morecore (num_units);
      if (p == NULL) /* Request for more memory failed. */
	return NULL;
    }
  }
}

void *GCRealloc (void *ap, size_t new_size)
{
  void *np;
  header_t *bp;

  if (ap == NULL)
    return GCMalloc (new_size);

  /* An awful method, we do it this way for simplicity. */
  bp = (header_t *)ap - 1;
  np = GCMalloc (new_size);
  memcpy (np, ap, new_size > bp->size ? bp->size : new_size);

  return np;
}

void GCollect (void)
{
  _Bool unmarked;
  header_t *p, *pm, *prevp;
  unsigned long i, j, stack_top;
  extern unsigned long etext, end; /* Don't need edata. */

  if (usedp == NULL)
    return;

  __asm__ ("movl %%ebp, %0"
  	   : "=r" (stack_top));

  /* Go through all the blocks and check if the address that
   * previously marked them still references them. All prev_reffs
   * are guaranteed not to be in the heap. 
   */
  unmarked = false;
  for (p = usedp->next;; p = p->next) {
    if (p->prev_reff != NULL && *p->prev_reff == p)
      p->marked = true;
    else
      unmarked = true;
    if (p == usedp)
      break;
  }
  if (!unmarked)
    return;

  /* Mark. */
  /* Oh my god, this is so terribly costly that it makes me want
   * to vomit. Not to mention the code itself. Just terrible!
   */
  for (i = 0; (stack_bottom - i) > stack_top; i++) {
    /* Now that we've moved a byte downward, check the entire
     * used list. Again.
     */
    unmarked = false;
    for (p = usedp->next;; p = p->next) {
      if (!p->marked) { /* The current block is unmarked. */
	if (*((void **) (stack_bottom - i)) >= (void *) (p + 1) &&
	    *((void **) (stack_bottom - i)) <= (void *) (p + p->size + 1)) {
	  /* Mark the item and check its memory. */
	  p->marked = true;
	  p->prev_reff = (void **) (stack_bottom - i);
	  for (j = (unsigned long) (p + 1); j < ((unsigned long) p + p->size + 1); j++) {
	    for (pm = usedp->next;; pm = pm->next) {
	      if (!pm->marked) {
		if (*((void **) j) >= (void *) (pm + 1) &&
		    *((void **) j) <= (void *) (pm + pm->size + 1)) {
		  pm->marked = true;
		  break;
		} else
		  unmarked = true;
	      }
	      if (pm == usedp) {
		if (!unmarked)
		  return; /* Everything is marked! Yay! */
		break;
	      }
	    }
	  }
	  break;
	} else
	  unmarked = true; /* There were some unmarked items. */
      }
      if (p == usedp) {
	if (!unmarked)
	  return;
	break;
      }
    }
  }

  /* Now we do the same thing, but for initialized and unitialized
   * data segments! Yay! Not even sure if this works honestly. It
   * Might?
   */
  //  printf ("%lu -> %lu\n", &etext, &end);
  for (i = (unsigned long) &etext; i < (unsigned long) &end; i++) {
    unmarked = false;
    for (p = usedp->next;; p = p->next) {
      if (!p->marked) {
	if (*((void **) i) >= (void *) (p + 1) &&
	    *((void **) i) <= (void *) (p + p->size + 1)) {
	  p->marked = true;
	  p->prev_reff = (void **) i;
	  for (j = (unsigned long) (p + 1); j < ((unsigned long) p + p->size + 1); j++) {
	    for (pm = usedp->next;; pm = pm->next) {
	      if (!pm->marked) {
		if (*((void **) j) >= (void *) (pm + 1) &&
		    *((void **) j) <= (void *) (pm + pm->size + 1)) {
		  pm->marked = true;
		  break;
		} else
		  unmarked = true;
	      }
	    }
	    if (pm == usedp) {
	      if (!unmarked)
		return;
	      break;
	    }
	  }
	}
	break;
      } else
	unmarked = true;
      if (p == usedp) {
	if (!unmarked)
	  return;
	break;
      }
    }
  }

  /* Sweep. */
  prevp = usedp;
  for (p = usedp->next;; prevp = p, p = p->next) {
    if (!p->marked) {
      if (p->next == p) { /* p is the last item left */
	usedp = NULL;
	return;
      }
      prevp->next = p->next;
      usedp = prevp;
      add_to_free_list (p);
      p = prevp;
      continue;
    }
    if (p == usedp)
      break;
  }

  /* Unmark all blocks. */
  for (p = usedp->next;; p = p->next) {
    p->marked = false;
    if (p == usedp)
      break;
  }
}

void GCFree (void *ap)
{
  header_t *bp, *p, *prevp;

  bp = (header_t *) ap - 1;

  /* Remove ap from the used list. */
  if (usedp != NULL) {
    /* Look for bp on the used list and remove it. */
    prevp = usedp;
    for (p = usedp->next;; prevp = p, p = p->next) {
      if (p == bp) { /* Found it. */
	if (bp->next == bp) /* bp is the only item on the used list. */
	  usedp = NULL;
	else {
	  prevp->next = p->next; /* Item is now removed. */
	  usedp = prevp;
	}
	break;
      }
      if (p == usedp) { /* We've wrapped around the used list. */
	fprintf (stderr, "I, like, haven't see this memory before. Dude.\n");
	abort ();
      }
    }
  }

  add_to_free_list (bp);
}

static void add_to_free_list (header_t *bp)
{
  header_t *p;

  GC_heap_size -= bp->size;
  
  for (p = freep; !(bp > p && bp < p->next); p = p->next) {
    if (p >= p->next && (bp > p || bp < p->next))
      break;
  }
  if (bp + bp->size == p->next) {
    bp->size += p->next->size;
    bp->next = p->next->next;
  } else
    bp->next = p->next;

  if (p + p->size == bp) {
    p->size += bp->size;
    p->next = bp->next;
  } else
    p->next = bp;

  freep = p;
}

#define MIN_ALLOC_SIZE 1024

static header_t *morecore (size_t num_units)
{
  void *vp;
  header_t *up;

  if (num_units < MIN_ALLOC_SIZE)
    num_units = MIN_ALLOC_SIZE;

  vp = sbrk (num_units * sizeof (header_t));
  if (vp == (void *) -1)
    return NULL;
  up = (header_t *) vp;
  up->size = num_units;
  GC_heap_size += num_units;
  add_to_free_list (up);
  return freep;
}

