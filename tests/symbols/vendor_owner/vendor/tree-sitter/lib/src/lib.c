#include <unistd.h>

/* Attributed to vendor_ts: the diagnostic names vendor_ts, never hl. */
ssize_t ts_probe_read(int fd, void *buf, size_t n) { return read(fd, buf, n); }
