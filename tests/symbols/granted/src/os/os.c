#include <stdio.h>
#include <unistd.h>

ssize_t os_probe_read(int fd, void *buf, size_t n) { return read(fd, buf, n); }
FILE *os_probe_stream(int fd) { return fdopen(fd, "r"); }
