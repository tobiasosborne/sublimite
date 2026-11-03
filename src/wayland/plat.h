#ifndef EDIT_WAYLAND_PLAT_H
#define EDIT_WAYLAND_PLAT_H
/* Shared ABI: callers need no Wayland headers and retain all plat_* entries. */
/* PLAT_EV_DROP shares this ABI; plat_drop_data is a no-data stub until edit-cxho.19. */
#include "x11/plat.h"
/* (P) Next platform setup error after NO_DISPLAY/FAIL; socket ABI bound. */
#define PLAT_ERR_SOCKET_PATH (-3)
/* GL worker uses the native handles only during exclusive init; UI afterwards.
 * Requests belong to present, outside the allocation-free input->submit slice.
 * Frame callbacks are scheduling hints, never GPU/device completion timestamps. */
typedef struct wayland_display_info {
    double dpi;
    uint32_t width, height, scale;
    /* Largest output in buffer pixels at the selected scale: per axis,
     * max over outputs of ceil(mode / output scale) * scale (edit-b6ai.16). */
    uint32_t max_width, max_height;
} wayland_display_info;
/* INIT only: highest-scale current wl_output mode. Physical buffer pixels;
 * DPI = conventional desktop base DPI times the compositor integer scale. */
int wayland_display_size(wayland_display_info *info);
/* Pure test/app guard: Wayland has no intrinsically private socket name.
 * Any non-NULL allow opts in; otherwise only an explicit alternate. */
const char *wayland_display_select(const char *display, const char *safe, const char *allow);
/* Setup diagnostics; other module errors retain their original return code. */
const char *wayland_error_string(int code);
/* Same drop-newest policy as X11; counter survives until shutdown. */
uint64_t wayland_input_dropped(const plat *p);
void *wayland_display(const plat *p);
void *wayland_egl_window(const plat *p);
void *wayland_surface(const plat *p);
/* Arms the frame callback plus, when bound, wp_presentation feedback. Without
 * wp_presentation (plat_present_available false) the frame callback's done is
 * the frame's completion (edit-b6ai.6; GL refuses that platform at init). */
int wayland_frame_arm(plat *p, uint32_t id);
void wayland_frame_cancel(plat *p);
/* With wp_presentation: matching feedback only. Without it: the armed frame
 * callback's done, ns = its dispatch time on CLOCK_MONOTONIC, msc = 0.
 * ready=false means still pending; discarded/error returns FAIL. The result
 * is retained until the next arm. */
int wayland_frame_result(plat *p, uint32_t id, bool *ready, uint64_t *ns, uint64_t *msc);
/* True only for armed id whose feedback was discarded (not a connection error). */
bool wayland_frame_discarded(const plat *p, uint32_t id);
/* Re-deliver on_present_complete for the armed frame on the next dispatch
 * turn: the raster backend's wl_buffer.release, whose completion waits for a
 * free buffer. No effect without an armed frame. UI only. */
void wayland_frame_notify(plat *p);
/* wl_shm proxy (raster buffer factory) or NULL when not advertised. */
void *wayland_shm(const plat *p);
/* Integer buffer scale applied to the surface; p->width/height are buffer
 * pixels and multiples of it. One before init or without set_buffer_scale. */
uint32_t wayland_buffer_scale(const plat *p);
#endif
