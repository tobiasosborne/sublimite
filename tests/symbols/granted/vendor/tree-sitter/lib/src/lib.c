#include <stdlib.h>

/* Each module imports abort through its own grant. */
void ts_probe_abort(void) { abort(); }
