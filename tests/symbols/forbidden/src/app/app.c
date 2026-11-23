#include <stddef.h>

extern size_t x11_smoke(void);
size_t app_probe_x11(void) { return x11_smoke(); }
