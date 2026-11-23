#include <stdio.h>

/* fdopen carries two grants (os and vendor_ts); each module passes on its own line. */
FILE *ts_probe_stream(int fd) { return fdopen(fd, "a"); }
