/* trace.h - per-thread lock-free event ring for the keystroke pipeline (P0.3).
 *
 * Single writer per ring: each thread calls trace_thread_register() once and
 * then trace_record() from that thread only. No locks, no allocation after
 * trace_init(). trace_dump() and trace_reset() must be called while no thread
 * is recording (after the workers are quiescent). */
#ifndef EDITOR_TRACE_TRACE_H
#define EDITOR_TRACE_TRACE_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#define TRACE_RING_CAP 65536u
#define TRACE_MAX_THREADS 16u

enum trace_ev {
    TRACE_T0_INGRESS = 0,
    TRACE_T1_DEQUEUE,
    TRACE_T2_MUTATION_DONE,
    TRACE_T3_RENDER_DONE,
    TRACE_T4_PRESENT_SUBMITTED,
    TRACE_T5_DEVICE_DONE,
    TRACE_T6_PRESENT_COMPLETE,
    TRACE_EV_COUNT
};

typedef struct trace_rec {
    uint64_t ns;
    uint32_t frame_id;
    uint16_t ev;
    uint16_t thread;
} trace_rec;

/* Resets all rings and the registration counter. Call once, before threads start. */
void trace_init(void);

/* Claims a ring for the calling thread. Idempotent per thread.
 * Returns the ring index (0..15), or -1 if all rings are taken. */
int trace_thread_register(void);

/* Records an event stamped with CLOCK_MONOTONIC. Silently drops the event if the
 * calling thread is not registered. */
void trace_record(enum trace_ev ev, uint32_t frame_id);

/* Records an event with an explicit timestamp (for replay and tests). */
void trace_record_at(uint64_t ns, enum trace_ev ev, uint32_t frame_id);

/* CLOCK_MONOTONIC in nanoseconds. */
uint64_t trace_now_ns(void);

/* Writes all rings in the binary dump format (see trace_fmt.h). Returns 0 on success. */
int trace_dump(FILE *f);

/* Clears all recorded events (timing and input); ring registrations are kept. */
void trace_reset(void);


/* ---- input event log (P0.6) ----
 * Every ingress event is appended to one fixed ring with its T0 timestamp and a
 * monotonically increasing seq. Single producer (the UI thread); overwrite-oldest;
 * no allocation. Readers (trace_dump, trace_input_copy) run while the producer is
 * quiescent. Records are 48 bytes and zero-filled, so dumps are byte-exact. */
#define TRACE_INPUT_CAP 16384u

enum trace_in_kind {
    TRACE_IN_INVALID = 0,
    TRACE_IN_KEY_DOWN,
    TRACE_IN_KEY_UP,
    TRACE_IN_POINTER_MOVE,
    TRACE_IN_BUTTON_DOWN,
    TRACE_IN_BUTTON_UP,
    TRACE_IN_WHEEL,
    TRACE_IN_RESIZE,
    TRACE_IN_FOCUS,
    TRACE_IN_CLIPBOARD,
    TRACE_IN_FILECHANGE,
    TRACE_IN_KIND_COUNT
};

typedef struct trace_input_rec {
    uint64_t seq;
    uint64_t t0_ns;
    uint16_t kind;
    uint16_t flags;
    uint32_t reserved;
    union {
        struct { uint32_t keysym, state; uint8_t utf8[8]; uint8_t utf8_len, repeat; } key;
        struct { int32_t x, y; uint32_t button, mods; } pointer;
        struct { int32_t dx, dy; uint32_t mods; } wheel;      /* 8.8 fixed point: PLAT_WHEEL_UNIT, 256 per notch (src/x11/plat.h) */
        struct { uint32_t w, h; } resize;
        struct { uint32_t focused; } focus;
        struct { uint32_t selection, length; } clipboard;
        struct { uint32_t watch_id, flags; } filechange;
        uint8_t raw[24];
    } p;
} trace_input_rec;

_Static_assert(sizeof(trace_input_rec) == 48, "trace_input_rec must be 48 bytes");

/* Typed append helpers. t0_ns is the ingress timestamp (trace_now_ns() in production). */
void trace_input_key(uint64_t t0_ns, enum trace_in_kind kind, uint32_t keysym, uint32_t state,
                     int repeat, const char *utf8, size_t utf8_len);   /* utf8 clipped to 8 bytes */
void trace_input_pointer(uint64_t t0_ns, enum trace_in_kind kind, int32_t x, int32_t y,
                         uint32_t button, uint32_t mods);
void trace_input_wheel(uint64_t t0_ns, int32_t dx, int32_t dy, uint32_t mods);
void trace_input_resize(uint64_t t0_ns, uint32_t w, uint32_t h);
void trace_input_focus(uint64_t t0_ns, int focused);
void trace_input_clipboard(uint64_t t0_ns, uint32_t selection, uint32_t length);
void trace_input_filechange(uint64_t t0_ns, uint32_t watch_id, uint32_t flags);

/* Events ever appended since init/reset, and how many of them were overwritten. */
uint64_t trace_input_count(void);
uint64_t trace_input_dropped(void);
/* Copies up to cap retained records, oldest first. Returns the number copied. */
size_t trace_input_copy(trace_input_rec *dst, size_t cap);

/* Replay: delivers records to a sink with their original spacing (scaled by speed)
 * or back to back (fast). The sink runs on the calling thread. */
typedef void (*trace_input_sink)(const trace_input_rec *ev, void *user);
typedef struct trace_replay_opts {
    int fast;        /* nonzero: no waiting */
    double speed;    /* > 0 and finite; 2.0 halves the gaps */
} trace_replay_opts;
/* Returns 0 on success, -1 on bad arguments. */
int trace_replay(const trace_input_rec *recs, size_t n, const trace_replay_opts *opts,
                 trace_input_sink sink, void *user);

#endif
