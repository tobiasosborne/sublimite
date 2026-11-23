#include <stdlib.h>

/* The heap is granted to the vendored runtime only (Law 2: pools come from base). */
void *app_probe_heap(size_t n) { return malloc(n); }
