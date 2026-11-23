#include <stdio.h>

/* fdopen carries two grants (os and hl); each module passes on its own line. */
FILE *hl_probe_stream(int fd) { return fdopen(fd, "a"); }
