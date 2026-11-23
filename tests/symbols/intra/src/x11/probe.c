#include <stddef.h>

extern size_t wayland_smoke(void);
size_t x11_probe_wl(void) { return wayland_smoke(); }
