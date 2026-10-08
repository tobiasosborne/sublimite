/* input.h - X-server-free input translation (P2.2): xcb key events + xkb state -> plat_event,
 * X time -> CLOCK_MONOTONIC mapping, our own key repeat, fixed event queue. No malloc after setup. */
#ifndef EDITOR_X11_INPUT_H
#define EDITOR_X11_INPUT_H
#include "plat.h"
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

#define X11_QUEUE_CAP 256u   /* power of two */

/* X server time (32-bit ms, wraps ~49.7 d) -> monotonic ns. See docs/decisions/P2.2.md. */
typedef struct x11_clock {
    bool     valid;
    uint32_t ref_ms;
    uint64_t ref_ns;
} x11_clock;
uint64_t x11_clock_map(x11_clock *c, uint32_t x_ms, uint64_t now_ns);

typedef struct x11_input {
    struct xkb_context *ctx;        /* owned by x11.c's plat setup (unref'd in destroy) or NULL in tests */
    struct xkb_keymap *keymap;      /* owned */
    struct xkb_state  *state;       /* owned; re-synchronised from each event's core state */
    struct xkb_compose_table *ctab; /* owned or NULL */
    struct xkb_compose_state *cstate;
    uint32_t mod_idx[7];            /* PLAT_MOD bit order; XKB_MOD_INVALID if absent */
    x11_clock clock;
    uint8_t down[32];               /* keycode bitmap: keys currently held (drops server repeats) */
    uint32_t rep_delay_ms, rep_rate_hz;
    bool     rep_active;
    uint32_t rep_key;               /* keycode being repeated */
    uint32_t rep_state;             /* core state to translate repeats with */
    uint64_t rep_next_ns;
    plat_event q[X11_QUEUE_CAP];
    uint32_t qh, qt;                /* head (read), tail (write), free-running */
    uint32_t q_dropped;
} x11_input;

/* Takes ownership of keymap (may be replaced later) and ctab (may be NULL). Returns 0 or -1. */
int  x11_input_init(x11_input *in, struct xkb_keymap *keymap, struct xkb_compose_table *ctab);
int  x11_input_set_keymap(x11_input *in, struct xkb_keymap *keymap); /* cancels repeat */
void x11_input_destroy(x11_input *in);

/* Core event state bits (X): shift 1, lock 2, control 4, mod1..5 = 8,16,32,64,128, buttons 256.., group <<13. */
uint16_t x11_mods_from_state(x11_input *in, uint32_t state);

/* Translate one key press/release. Returns true and fills *out when an event should be delivered
 * (false: server-side autorepeat dup, or swallowed by a compose sequence). Updates repeat state. */
bool x11_input_key(x11_input *in, const xcb_key_press_event_t *ev, bool press, uint64_t now_ns, plat_event *out);
/* Pure part: keysym/utf8/mods for a keycode+state (no repeat or compose side effects). */
void x11_translate(x11_input *in, uint32_t keycode, uint32_t state, plat_event *out);

/* Our repeat timer. Deadline of next repeat in ns (0 = none). */
uint64_t x11_repeat_deadline(const x11_input *in);
/* Emits one repeat event if due (call in a loop until false). */
bool x11_repeat_poll(x11_input *in, uint64_t now_ns, plat_event *out);
void x11_repeat_cancel(x11_input *in);

/* Button translation (core events). Buttons 4-7 become WHEEL when core_wheel (no XI2), else dropped. */
bool x11_input_button(x11_input *in, const xcb_button_press_event_t *ev, bool press, uint64_t now_ns,
                      bool core_wheel, plat_event *out);

bool x11_q_push(x11_input *in, const plat_event *ev);
bool x11_q_pop(x11_input *in, plat_event *out);
#endif
