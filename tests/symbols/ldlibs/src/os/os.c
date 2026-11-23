#include <stddef.h>

extern size_t app_smoke(void);
size_t os_probe_upward(void) { return app_smoke(); }
