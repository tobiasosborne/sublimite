#ifndef ZYGOTE_PROTOCOL_H
#define ZYGOTE_PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "x11/plat.h"
#include "render/render.h"
typedef struct zygote_record {
    uint64_t map_ns, ready_ns, present_ns, device_ns, complete_ns;
    uint64_t init_ns, displayed_msc;
    uint64_t rss_kib, hwm_kib, minor_faults, major_faults;
    uint32_t ready_frame, present_frame, device_frame, complete_frame;
    uint32_t cols, rows, width, height;
    int result, gl_result, mapped_verified, unmapped_verified;
    bool native_gl;
    char device[256], driver[256];
} zygote_record;
/* Every executable checks both keys; no inherited environment alone opts in. */
static inline bool zygote_display_allowed(const char *d, const char *ed, const char *allow, bool real)
{
    if (!d || !*d || !ed || strcmp(d, ed)) return false;
    return real ? allow && !strcmp(allow, "1") : !strcmp(d, ":99");
}
static inline bool zygote_display_check(bool real)
{
    return zygote_display_allowed(getenv("DISPLAY"), getenv("EDIT_DISPLAY"),
                                  getenv("EDIT_ALLOW_REAL_DISPLAY"), real);
}
render_hooks zygote_backend_hooks(void *context);
void zygote_platform_map(plat *p, void *context);
#endif
