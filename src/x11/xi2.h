/* xi2.h - minimal XInput 2 over generic xcb (P2.2). libxcb-xinput has no headers/dev symlink here, so
 * requests are built by hand from the XI2 protocol spec and events decoded from raw bytes. Everything
 * in this header is pure (no X connection): request builders, QueryDevice parser, event decoder. */
#ifndef EDITOR_X11_XI2_H
#define EDITOR_X11_XI2_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define XI2_MAX_DEVS 16
#define XI2_MIN_MAJOR 2
#define XI2_MIN_MINOR 1                /* smooth scrolling arrived in 2.1 */

#define XI2_MAX_AXES 4                 /* scroll axes kept per device (any direction mix); extras are ignored */

typedef struct xi2_axis {
    uint16_t num;                      /* valuator number */
    uint8_t dir;                       /* 0 vertical, 1 horizontal */
    bool preferred;                    /* XIScrollFlagPreferred: wins when several axes of one direction move together */
    double inc;                        /* valuator units per notch */
    double last;                       /* last seen value (history) */
    double rem;                        /* sub-unit remainder not yet emitted, in [-0.5, 0.5] */
    bool last_ok;
} xi2_axis;

typedef struct xi2_dev {
    uint16_t source;                   /* master pointer id (scroll class sourceid) */
    uint32_t naxes;
    xi2_axis axis[XI2_MAX_AXES];
} xi2_dev;

typedef struct xi2 {
    uint8_t opcode;                    /* XInputExtension major opcode (= GE "extension" field) */
    bool active;
    uint32_t ndev;
    xi2_dev dev[XI2_MAX_DEVS];
} xi2;

/* Request builders. Return byte length. Buffers: 8 / 8 / 20 bytes. */
size_t xi2_build_query_version(uint8_t opcode, uint8_t *out, uint16_t major, uint16_t minor);
size_t xi2_build_query_device(uint8_t opcode, uint8_t *out, uint16_t deviceid /* 0 = all */);
/* Select XI_DeviceChanged + XI_Motion on window for all master devices. */
size_t xi2_build_select_events(uint8_t opcode, uint8_t *out, uint32_t window);

/* Parse a full QueryDevice reply (32-byte header + body); records scroll classes and seeds each axis's history
 * from the ValuatorClass current value. Returns scroll classes found (>=0) or -1 on malformed input. All-or-nothing:
 * on -1 the table in *x is untouched. Never reads out of bounds. */
int xi2_parse_query_device(xi2 *x, const uint8_t *reply, size_t len);

typedef struct xi2_result {
    bool device_changed;               /* rescan devices (QueryDevice), valuator history reset */
    bool motion;                       /* pointer position valid */
    bool wheel;                        /* dx/dy valid (PLAT_WHEEL_UNIT per notch) */
    int32_t x, y;                      /* window-relative, integer part */
    int32_t dx, dy;
    uint32_t time_ms;
    uint32_t buttons;                  /* bit n-1 = button n */
    uint32_t mods;                     /* X modifier mask (effective) | group << 13 */
} xi2_result;

/* Decode one raw generic event. Wire layout is 32 + 4*length bytes; libxcb's xcb_poll_for_event inserts a 4-byte
 * full_sequence after byte 32 of GE events, so pass xcb_layout=true with len = 36 + 4*length for those. Returns
 * true if it was an XI2 event we understand. Safe on arbitrary bytes. A
 * rejected event (false) leaves *x untouched (valuator history commits only after the whole event validated). */
bool xi2_decode(xi2 *x, const uint8_t *ev, size_t len, bool xcb_layout, xi2_result *r);
bool xi2_has_scroll(const xi2 *x);
#endif
