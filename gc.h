/* Simple garbage collector.
 * Not thread safe, and does not automatically collect. Do it
 * your damn self.
 * (C) 2012 Matthew Plant, GNU GPL Version 3
 */
#ifndef _GC_H_
#define _GC_H_

extern size_t GC_heap_size; /* In blocks. Not really useful */

void *GCMalloc (size_t);
void *GCRealloc (void *, size_t); /* AWFUL function. */
void GCFree (void *);
void GCollect (void); /* Garbage collection is explicit. */
void GCInit (void); /* Initialize the garbage collector. */

#endif
