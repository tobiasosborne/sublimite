#ifndef EDIT_WAYLAND_PLAT_H
#define EDIT_WAYLAND_PLAT_H
/* Shared ABI: callers need no Wayland headers and retain all plat_* entries. */
#include "x11/plat.h"
/* GL worker uses the native handles only during exclusive init; UI afterwards.
 * Requests belong to present, outside the allocation-free input->submit slice.
 * Frame callbacks are scheduling hints, never GPU/device completion timestamps. */
void *wayland_egl_window(const plat *p);
void *wayland_surface(const plat *p);
int wayland_frame_arm(plat *p, uint32_t id);
void wayland_frame_cancel(plat *p);
/* Matching wp_presentation feedback only. ready=false means still pending;
 * discarded/error returns FAIL. The result is retained until the next arm. */
int wayland_frame_result(plat *p, uint32_t id, bool *ready, uint64_t *ns, uint64_t *msc);
#endif
