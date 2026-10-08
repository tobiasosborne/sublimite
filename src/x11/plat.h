/* plat.h - platform window/event-loop interface (P2.1). X11 implements it now,
 * Wayland later. Startup stamps reuse trace ids with frame_id 0:
 * T0=process exec (plat_config.exec_ns), T1=connect done, T2=window created,
 * T3=(unused), T4=map requested. */
#ifndef EDITOR_X11_PLAT_H
#define EDITOR_X11_PLAT_H
#include <stdbool.h>
#include <stdint.h>

#define PLAT_OK 0
#define PLAT_ERR_NO_DISPLAY (-1)
#define PLAT_ERR_FAIL (-2)

typedef enum plat_ev_kind {
    PLAT_EV_EXPOSE, PLAT_EV_RESIZE, PLAT_EV_FOCUS, PLAT_EV_CLOSE,
    PLAT_EV_KEY, PLAT_EV_BUTTON, PLAT_EV_MOTION
} plat_ev_kind;

typedef struct plat_event {
    plat_ev_kind kind;
    uint32_t w, h;          /* RESIZE/EXPOSE */
    bool focused;           /* FOCUS */
    bool press;             /* KEY/BUTTON */
    uint32_t code;          /* raw keycode or button */
    uint32_t state;         /* modifier/button mask */
    int32_t x, y;
    uint32_t time_ms;
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
} plat;

int  plat_init(plat *p, const plat_config *cfg);
void plat_map(plat *p);
void plat_set_blink(plat *p, uint32_t ms);  /* 0 disarms; ignored while unfocused */
void plat_quit(plat *p);
int  plat_run(plat *p, const plat_callbacks *cb);
/* timeout_ms < 0 blocks forever (only for the real app); >=0 bounded run for tests. */
int  plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms);
void plat_shutdown(plat *p);
#endif
