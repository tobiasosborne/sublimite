#include <unistd.h>

ssize_t os_probe_read(int fd, void *buf, size_t n) { return read(fd, buf, n); }
