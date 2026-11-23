#include <stddef.h>

extern size_t app_smoke(void) __attribute__((weak));
extern size_t base_optional __attribute__((weak));
/* Mark the ELF symbol as data so nm reports v as well as the function's w. */
__asm__(".type base_optional, @object");
size_t os_probe_weak(void) { return app_smoke ? app_smoke() : 777; }
const size_t *os_probe_weak_object(void) { return &base_optional; }
