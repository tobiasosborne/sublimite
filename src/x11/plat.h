/* plat.h - platform window/event-loop interface (P2.1). X11 implements it now,
 * Wayland later. Startup stamps reuse trace ids with frame_id 0:
 * T0=process exec (plat_config.exec_ns), T1=connect done, T2=window created,
 * T3=(unused), T4=map requested. */
#ifndef EDITOR_X11_PLAT_H
#define EDITOR_X11_PLAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLAT_OK 0
#define PLAT_ERR_NO_DISPLAY (-1)
#define PLAT_ERR_FAIL (-2)

typedef enum plat_ev_kind {
    PLAT_EV_EXPOSE, PLAT_EV_RESIZE, PLAT_EV_FOCUS, PLAT_EV_CLOSE,
    PLAT_EV_KEY, PLAT_EV_BUTTON, PLAT_EV_MOTION,
    PLAT_EV_WHEEL,          /* P2.2: dx/dy in 1/256 notch, smooth flag */
    PLAT_EV_CLIPBOARD,      /* P2.2: code 0 = requested data arrived (clip_ok), 1 = request failed, 2 = we lost ownership */
    PLAT_EV_KEYMAP          /* P2.2: keymap/layout changed */
} plat_ev_kind;

/* Modifier bits in plat_event.mods (platform-neutral, not X bits). */
#define PLAT_MOD_SHIFT (1u << 0)
#define PLAT_MOD_CTRL  (1u << 1)
#define PLAT_MOD_ALT   (1u << 2)
#define PLAT_MOD_SUPER (1u << 3)
#define PLAT_MOD_CAPS  (1u << 4)
#define PLAT_MOD_NUM   (1u << 5)
#define PLAT_MOD_ALTGR (1u << 6)   /* ISO_Level3_Shift */

/* Selections. */
#define PLAT_CLIP_CLIPBOARD 0
#define PLAT_CLIP_PRIMARY   1

/* Pointer button numbers in plat_event.code for KIND_BUTTON: 1 left, 2 middle, 3 right, 8 back, 9 forward.
 * Core buttons 4-7 never surface as BUTTON; they become WHEEL events. */
#define PLAT_WHEEL_UNIT 256     /* wheel/smooth delta fixed point: 256 = one notch (~one line) */
#define PLAT_UTF8_MAX 8

typedef struct plat_event {
    plat_ev_kind kind;
    uint32_t w, h;          /* RESIZE/EXPOSE */
    bool focused;           /* FOCUS */
    bool press;             /* KEY/BUTTON */
    uint32_t code;          /* raw keycode or button */
    uint32_t state;         /* modifier/button mask */
    int32_t x, y;
    uint32_t time_ms;       /* raw X server time (ms), informational */
    /* ---- P2.2 ---- */
    uint32_t keysym;        /* KEY: xkb keysym with the event's modifiers applied */
    uint8_t  utf8[PLAT_UTF8_MAX]; /* KEY: text to insert, printable only, else len 0 (see decisions/P2.2.md) */
    uint8_t  utf8_len;
    bool     repeat;        /* KEY: synthesised by our repeat timer */
    uint16_t mods;          /* PLAT_MOD_* effective modifiers */
    uint32_t buttons;       /* BUTTON/MOTION/WHEEL: bit n-1 set = button n held */
    int32_t  dx, dy;        /* WHEEL: PLAT_WHEEL_UNIT per notch; +dy = content scrolls down (wheel toward user) */
    bool     smooth;        /* WHEEL: from XI2 smooth-scroll valuators (else core buttons 4-7) */
    bool     clip_ok;       /* CLIPBOARD: data available via plat_clip_data; false = failed/lost */
    uint8_t  clip_which;    /* CLIPBOARD: PLAT_CLIP_* */
    uint64_t t0_ns;         /* CLOCK_MONOTONIC ns of the physical event (X time mapped, never later than ingest) */
} plat_event;

typedef struct plat_callbacks {
    void *ud;
    void (*on_event)(void *ud, const plat_event *ev);
    void (*on_blink)(void *ud);                       /* timerfd fired */
    void (*on_work)(void *ud);                        /* work eventfd readable */
    void (*on_present_complete)(void *ud, uint32_t serial, uint64_t ust, uint64_t msc); /* T6 */
} plat_callbacks;

typedef struct plat_config {
    const char *title;
    uint32_t width, height;
    bool headless;          /* tests without DISPLAY: init returns PLAT_ERR_NO_DISPLAY */
    int work_eventfd;       /* -1 if none */
    uint64_t exec_ns;       /* CLOCK_MONOTONIC at process start (0 = unknown) */
} plat_config;

typedef struct plat {
    void *conn;             /* xcb_connection_t* */
    uint32_t win, colormap, visual;
    uint8_t depth;
    bool argb, present_ok, focused, quit;
    uint8_t present_opcode;
    uint32_t wm_protocols, wm_delete;
    int timer_fd, work_fd;
    uint32_t width, height;
    uint64_t iterations;    /* loop wakeups, for G11 */
    /* ---- P2.2 ---- */
    int repeat_fd;          /* timerfd for our key repeat */
    void *in;               /* x11_input* (xkb state, repeat, queue); allocated at init, not on the typing path */
    void *xi;               /* xi2 state or NULL if XI2 unavailable */
    void *clip;             /* clipboard state */
    uint32_t last_time;     /* last X server timestamp seen (for selection ownership) */
    uint8_t xkb_event;      /* XKB extension first-event code, 0 if none */
    int32_t xkb_dev;        /* core keyboard device id */
    bool core_wheel;        /* core buttons 4-7 are wheel (no XI2 scroll devices) */
    uint32_t xi_scroll_ms;  /* time of last XI2 scroll event, to drop its emulated core buttons */
} plat;

int  plat_init(plat *p, const plat_config *cfg);
void plat_map(plat *p);
void plat_set_blink(plat *p, uint32_t ms);  /* 0 disarms; ignored while unfocused */
void plat_quit(plat *p);
int  plat_run(plat *p, const plat_callbacks *cb);
/* timeout_ms < 0 blocks forever (only for the real app); >=0 bounded run for tests. */
int  plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms);
void plat_shutdown(plat *p);

/* ---- input (P2.2) ---- */
/* Key repeat is ours: X autorepeat is not used (XKB detectable-autorepeat drops server repeats).
 * delay_ms before the first repeat, rate_hz after; rate 0 disables. Defaults 400 ms / 30 Hz. */
void plat_set_repeat(plat *p, uint32_t delay_ms, uint32_t rate_hz);
/* Pops one queued input event (non-blocking); false when empty. plat_run_for drains this queue into on_event. */
bool plat_poll_event(plat *p, plat_event *out);

/* ---- clipboard (P2.2) ---- */
/* Take ownership of a selection; the bytes (UTF-8) are copied (malloc, not the typing path). */
int  plat_clip_set(plat *p, int which, const void *utf8, size_t len);
/* Asynchronous request; result arrives as PLAT_EV_CLIPBOARD, then plat_clip_data(). */
int  plat_clip_request(plat *p, int which);
/* Last received data (valid until the next request completes); NUL not guaranteed. */
const uint8_t *plat_clip_data(const plat *p, size_t *len);
#endif
