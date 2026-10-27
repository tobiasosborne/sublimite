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
int wayland_frame_arm(plat *p, uint32_t id);
void wayland_frame_cancel(plat *p);
/* Matching wp_presentation feedback only. ready=false means still pending;
 * discarded/error returns FAIL. The result is retained until the next arm. */
int wayland_frame_result(plat *p, uint32_t id, bool *ready, uint64_t *ns, uint64_t *msc);
#endif
