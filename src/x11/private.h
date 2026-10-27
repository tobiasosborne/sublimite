/* Backend state; native protocol and loop details never enter plat.h. */
#ifndef EDITOR_X11_PRIVATE_H
#define EDITOR_X11_PRIVATE_H
#include "plat.h"

typedef struct x11_plat {
    void *conn;
    uint32_t win, colormap, visual;
    uint8_t depth;
    bool argb, present_ok;
    uint8_t present_opcode;
    uint32_t present_eid;
    uint32_t wm_protocols, wm_delete;
    int timer_fd, work_fd, repeat_fd;
    void *in, *xi, *clip, *dnd;
    uint32_t last_time;
    uint8_t xkb_event;
    int32_t xkb_dev;
    bool core_wheel;
    uint32_t xi_scroll_ms;
} x11_plat;

/* Backend and white-box fixtures only; NULL before init/after shutdown. */
static inline x11_plat *x11_private(const plat *p)
{ return p ? p->backend_state : NULL; }
#endif
