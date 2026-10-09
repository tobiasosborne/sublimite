#ifndef ZYGOTE_PROTOCOL_H
#define ZYGOTE_PROTOCOL_H
#include <stdint.h>
#include "x11/plat.h"
#include "render/render.h"
typedef struct zygote_record {
    uint64_t map_ns, ready_ns, present_ns, complete_ns;
    uint64_t rss_kib, hwm_kib, minor_faults, major_faults;
    uint32_t ready_frame, present_frame, complete_frame;
    int result, gl_result, mapped_verified, unmapped_verified;
} zygote_record;
render_hooks zygote_backend_hooks(void *context);
void zygote_platform_map(plat *p, void *context);
#endif
