#include <stdlib.h>

/* The runtime's default allocator: a vendor_ts grant, not an hl one. */
void *ts_probe_parser_new(void) { return malloc(64); }
void ts_probe_parser_delete(void *parser) { free(parser); }
