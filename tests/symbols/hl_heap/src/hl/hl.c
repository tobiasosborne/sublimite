#include <stdlib.h>

/* hl's own code holds no grant: the vendored runtime's heap is not inherited. */
void *hl_probe_heap(size_t n) { return malloc(n); }
