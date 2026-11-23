#include <unistd.h>

/* A syscall wrapper outside os: the bypass the per-module grants close. */
ssize_t piece_probe_read(int fd, void *buf, size_t n) { return read(fd, buf, n); }
