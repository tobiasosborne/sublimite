/* plat.h - shared platform window/event-loop interface. X11 and Wayland
 * implement it through runtime dispatch. Startup stamps reuse trace ids with frame_id 0:
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
    uint32_t time_ms;       /* raw native server time (ms), informational */
    /* ---- P2.2 ---- */
    uint32_t keysym;        /* KEY: keysym with the event's modifiers applied */
    uint8_t  utf8[PLAT_UTF8_MAX]; /* KEY: text to insert, printable only, else len 0 (see decisions/P2.2.md) */
    uint8_t  utf8_len;
    bool     repeat;        /* KEY: synthesised by our repeat timer */
    uint16_t mods;          /* PLAT_MOD_* effective modifiers */
    uint32_t buttons;       /* BUTTON/MOTION/WHEEL: bit n-1 set = button n held */
    int32_t  dx, dy;        /* WHEEL: PLAT_WHEEL_UNIT per notch; +dy = content scrolls down (wheel toward user) */
    bool     smooth;        /* WHEEL: from XI2 smooth-scroll valuators (else core buttons 4-7) */
    bool     clip_ok;       /* CLIPBOARD: data available via plat_clip_data; false = failed/lost */
    uint8_t  clip_which;    /* CLIPBOARD: PLAT_CLIP_* */
    uint64_t t0_ns;         /* monotonic ns of the physical event (never later than ingest) */
} plat_event;

typedef struct plat_callbacks {
    void *ud;
    void (*on_event)(void *ud, const plat_event *ev);
    void (*on_blink)(void *ud);                       /* blink timer fired */
    void (*on_work)(void *ud);                        /* work wake readable */
    void (*on_present_complete)(void *ud, uint32_t serial, uint64_t ust, uint64_t msc); /* T6 */
} plat_callbacks;

struct os_wake;
/* (P) MAXIMUM_WAIT_OBJECTS - message queue - blink/repeat/present/clipboard. */
#define PLAT_WORK_SOURCE_CAP (64u - 1u - 4u)

typedef struct plat_config {
    const char *title;
    uint32_t width, height;
    bool headless;          /* tests without DISPLAY: init returns PLAT_ERR_NO_DISPLAY */
    const struct os_wake *work_wake; /* borrowed; NULL if none */
    uint64_t exec_ns;       /* monotonic ns at process start (0 = unknown) */
#if defined(_WIN32)
    /* Alternative to work_wake. UI-only callback snapshots borrowed native
     * level-triggered handles on every wait, including native modal loops.
     * At most PLAT_WORK_SOURCE_CAP; SIZE_MAX reports invalid/overflow. */
    size_t (*work_sources)(void *ctx, void **handles, size_t capacity);
    void *work_sources_ctx;
#endif
} plat_config;

typedef enum plat_backend { PLAT_BACKEND_X11, PLAT_BACKEND_WAYLAND, PLAT_BACKEND_WIN32 } plat_backend;

/* Runtime policy: X11 wins when both environment variables are nonempty. */
plat_backend plat_backend_select(const char *display, const char *wayland_display);

typedef struct plat {
    plat_backend backend;
    void *backend_state;   /* owned by the selected backend */
    uint32_t width, height;
    bool focused, quit;
    uint64_t iterations;   /* loop wakeups, for G11 */
} plat;

typedef struct plat_display_info {
    double dpi;
    uint32_t work_width, work_height;
} plat_display_info;
/* INIT only: X resources/active RandR output/EWMH work area, or Wayland
 * current output DPI and physical buffer dimensions. Returns PLAT_* errors. */
int plat_display_size(plat_backend backend, plat_display_info *info);
/* INIT only: mode timing in ns; zero when unknown or no platform exists. */
uint64_t plat_display_interval_ns(const plat *p);
/* Borrowed native handles, sampled during exclusive renderer initialization. */
typedef struct plat_x11_native {
    void *connection;
    uint32_t window, visual;
    uint8_t depth;
    bool argb;
} plat_x11_native;
plat_x11_native plat_native_x11(const plat *p);
void *plat_native_win32(const plat *p); /* borrowed HWND; NULL for other backends */
bool plat_present_available(const plat *p);

int  plat_init(plat *p, const plat_config *cfg);
int  plat_init_backend(plat *p, const plat_config *cfg, plat_backend backend);
void plat_map(plat *p);
void plat_set_blink(plat *p, uint32_t ms);  /* 0 disarms; ignored while unfocused */
/* X11: sets or deletes _GTK_THEME_VARIANT="dark", the hint Muffin/Mutter read to
 * draw the dark decoration variant (edit-29y2). Wayland: no-op. */
void plat_set_dark(plat *p, bool dark);
/* Raster owns completion on its private connection; avoid duplicate UI wakes. */
int  plat_set_present_events(plat *p, bool enabled);
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
/* Request ownership; bytes (UTF-8) are copied (malloc, not the typing path).
 * PLAT_OK means queued, not confirmed: the loop checks ownership asynchronously,
 * retries stale timestamps with CurrentTime, and reports failure as CLIPBOARD code 2.
 * A local request during confirmation is deferred until that check completes. */
int  plat_clip_set(plat *p, int which, const void *utf8, size_t len);
/* Asynchronous request; result arrives as PLAT_EV_CLIPBOARD, then plat_clip_data(). */
int  plat_clip_request(plat *p, int which);
/* Last received data, associated with the completion currently being delivered.
 * Remains valid until the next request completion is popped/delivered; merely
 * queueing another request (including in its callback) does not invalidate it.
 * NUL is not guaranteed. */
const uint8_t *plat_clip_data(const plat *p, size_t *len);
#endif
