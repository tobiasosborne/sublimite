#include <stdlib.h>
#include <unistd.h>

ssize_t os_probe_read(int fd, void *buf, size_t n) { return read(fd, buf, n); }
void os_probe_abort(void) { abort(); }
