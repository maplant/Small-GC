#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gc.h"

static char *BSS_test;
static char *init_test = "Hello, world!";

int main(void) /* Adaptd from the BDWGC example. */
{
  int i;
  GCInit ();

  BSS_test = GCMalloc (100);
  init_test = GCMalloc (3571); 
  
  for (i = 0; i < 1000000; ++i) {
    int *p = (int*) GCMalloc (sizeof(int));
    int *q = (int*) GCMalloc (sizeof(int));

    *p = 2323;
    *q = 484838;
    *q += *p;
    *p += *q;
    
    p = (int *) GCRealloc (q, 2 * sizeof(int));
    if (i % 100000 == 0) {
      printf("Heap size = %zu\n", GC_heap_size);
      GCollect ();
    } 
  }

  GCollect ();
  printf("Heap size = %zu\n", GC_heap_size);

  BSS_test = NULL;
  init_test = NULL;
  GCollect ();
  printf("Heap size = %zu\n", GC_heap_size);
  return 0;
}
