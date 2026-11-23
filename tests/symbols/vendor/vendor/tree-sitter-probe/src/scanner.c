#include <stdlib.h>

/* A grammar scanner allocates its state through the same vendor_ts grant. */
static const int probe_language = 1;
const void *tree_sitter_probe(void) { return &probe_language; }
void *tree_sitter_probe_external_scanner_create(void) { return calloc(1, 16); }
