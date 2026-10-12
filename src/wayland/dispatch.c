#include "wayland/private.h"
#include <string.h>

const char *wayland_display_select(const char *display, const char *safe, const char *allow)
{
    if (allow) return display;
    return safe && *safe ? safe : NULL;
}
const char *wayland_error_string(int code)
{
    switch (code) {
    case PLAT_ERR_SOCKET_PATH: return "Wayland socket path is too long for the Unix socket ABI (shorten WAYLAND_DISPLAY)";
    case PLAT_ERR_NO_DISPLAY: return "cannot connect to Wayland compositor (check WAYLAND_DISPLAY, WAYLAND_SOCKET and XDG_RUNTIME_DIR)";
    case PLAT_ERR_FAIL: return "Wayland setup or protocol failed (requires wl_compositor, xdg_wm_base and wl_seat)";
    default: return NULL;
    }
}

plat_backend plat_backend_select(const char *display, const char *wayland_display)
{ return display && *display ? PLAT_BACKEND_X11 : wayland_display && *wayland_display ? PLAT_BACKEND_WAYLAND : PLAT_BACKEND_X11; }

int plat_init_backend(plat *p, const plat_config *cfg, plat_backend backend)
{
    if (!p || !cfg) return PLAT_ERR_FAIL;
    return backend == PLAT_BACKEND_WAYLAND ? wayland_plat_init(p, cfg) : x11_plat_init(p, cfg);
}
int plat_init(plat *p, const plat_config *cfg) { return plat_init_backend(p, cfg, PLAT_BACKEND_X11); }

void plat_map(plat *p)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { wayland_plat_map(p); return; }
    x11_plat_map(p);
}

void plat_set_blink(plat *p, uint32_t ms)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { wayland_plat_set_blink(p, ms); return; }
    x11_plat_set_blink(p, ms);
}

int plat_set_present_events(plat *p, bool enabled)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { return wayland_plat_set_present_events(p, enabled); }
    return x11_plat_set_present_events(p, enabled);
}

void plat_set_repeat(plat *p, uint32_t delay_ms, uint32_t rate_hz)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { wayland_plat_set_repeat(p, delay_ms, rate_hz); return; }
    x11_plat_set_repeat(p, delay_ms, rate_hz);
}

bool plat_poll_event(plat *p, plat_event *out)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { return wayland_plat_poll_event(p, out); }
    return x11_plat_poll_event(p, out);
}

int plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { return wayland_plat_run_for(p, cb, timeout_ms); }
    return x11_plat_run_for(p, cb, timeout_ms);
}

int plat_run(plat *p, const plat_callbacks *cb)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { return wayland_plat_run(p, cb); }
    return x11_plat_run(p, cb);
}

void plat_shutdown(plat *p)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { wayland_plat_shutdown(p); return; }
    x11_plat_shutdown(p);
}

int plat_clip_set(plat *p, int which, const void *utf8, size_t len)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { return wayland_plat_clip_set(p, which, utf8, len); }
    return x11_plat_clip_set(p, which, utf8, len);
}

int plat_clip_request(plat *p, int which)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { return wayland_plat_clip_request(p, which); }
    return x11_plat_clip_request(p, which);
}

const uint8_t * plat_clip_data(const plat *p, size_t *len)
{
    if (p && p->backend == PLAT_BACKEND_WAYLAND) { return wayland_plat_clip_data(p, len); }
    return x11_plat_clip_data(p, len);
}
