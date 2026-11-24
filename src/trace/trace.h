#ifndef TRACE_H
#define TRACE_H

#include "base/base.h"
#include "os/os.h"

/* Capacities inherited from the EDTRACE1 writer. The rings mask by capacity - 1. */
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
    TRACE_COMPOSE_BEGIN, /* editor composition only, preserves pipeline IDs */
    TRACE_COMPOSE_END,
    /* Startup stamps are retained separately from the wrapping frame rings.
     * Append only: existing dump event IDs remain unchanged. */
    TRACE_STARTUP_PROCESS_START,
    TRACE_STARTUP_DISPLAY_CONNECT,
    TRACE_STARTUP_EGL_PROBE_START,
    TRACE_STARTUP_EGL_PROBE_END,
    TRACE_STARTUP_RASTER_FALLBACK,
    TRACE_STARTUP_FONT_ATLAS_READY,
    TRACE_STARTUP_FILE_PREFIX_READ,
    TRACE_STARTUP_FIRST_LAYOUT,
    TRACE_STARTUP_FIRST_SUBMIT,
    TRACE_STARTUP_FIRST_PRESENT,
    TRACE_STARTUP_EGL_LOAD_START,
    TRACE_STARTUP_EGL_LOAD_END,
    /* frame_id is the frame rejected by the render adapter. */
    TRACE_RENDER_REJECTED,
    TRACE_EV_COUNT
};
/* One past the last startup stamp: the startup range is [PROCESS_START, END). */
#define TRACE_STARTUP_END TRACE_RENDER_REJECTED

typedef struct trace_rec {
    uint64_t ns;
    uint32_t frame_id;
    uint16_t ev;
    uint16_t thread;
} trace_rec;

/* The process starts with empty rings. Each timing ring has one writer at
 * a time. Register before ordinary timing records and release on the same
 * thread before returning from its entry point. Dump/reset require all trace
 * operations to be quiescent, e.g. after joining workers and on the UI thread. */

/* Claims a ring; idempotent until release. Returns its slot or -1 when the
 * pool is full. Callers must handle -1 when trace coverage is required. */
int trace_thread_register(void);

/* Returns the calling thread's slot for reuse, retaining its bounded history.
 * A later owner appends to that slot. Only a registered thread releases. */
void trace_thread_release(void);

/* Records an event at the current monotonic time or at a supplied timestamp.
 * Ordinary timing events require a slot. Startup events retain the first
 * nonzero stamp per event even on unregistered threads. */
void trace_record(enum trace_ev ev, uint32_t frame_id);
void trace_record_at(uint64_t ns, enum trace_ev ev, uint32_t frame_id);

/* Writes an EDTRACE1 dump at the file's current position. The caller owns
 * the handle and any required durability. Returns 0 on success, -1 on error. */
int trace_dump(os_file *f);

/* Single-producer ingress log (UI thread). Records have consecutive sequence
 * numbers and nondecreasing monotonic timestamps. Oldest records are
 * overwritten on wrap. Appends and timing recording allocate nothing. */
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
        struct { int32_t dx, dy; uint32_t mods; } wheel;      /* 8.8 fixed point, 256 per notch */
        struct { uint32_t w, h; } resize;
        struct { uint32_t focused; } focus;
        struct { uint32_t selection, length; } clipboard;
        struct { uint32_t watch_id, flags; } filechange;
        uint8_t raw[24];
    } p;
} trace_input_rec;

/* Typed append helpers. t0_ns is the ingress timestamp. utf8 may be NULL
 * only with utf8_len 0. */
void trace_input_key(uint64_t t0_ns, enum trace_in_kind kind, uint32_t keysym, uint32_t state,
                     int repeat, const char *utf8, size_t utf8_len);   /* utf8 clipped to 8 bytes */
void trace_input_pointer(uint64_t t0_ns, enum trace_in_kind kind, int32_t x, int32_t y,
                         uint32_t button, uint32_t mods);
void trace_input_wheel(uint64_t t0_ns, int32_t dx, int32_t dy, uint32_t mods);
void trace_input_resize(uint64_t t0_ns, uint32_t w, uint32_t h);
void trace_input_focus(uint64_t t0_ns, int focused);
void trace_input_clipboard(uint64_t t0_ns, uint32_t selection, uint32_t length);
void trace_input_filechange(uint64_t t0_ns, uint32_t watch_id, uint32_t flags);

/* A loaded dump owns both arrays. Timing records are grouped by slot, oldest
 * first within a slot. A slot number identifies storage, and can span owners. */
typedef struct trace_loaded {
    trace_rec *recs;
    size_t nrecs;
    int has_input;              /* 1 if the optional INPT section was present */
    trace_input_rec *in;
    size_t nin;
    uint64_t in_dropped;
    base_arena storage;         /* owns the two array views above */
} trace_loaded;

/* Reads a seekable file from offset zero without moving its current position.
 * The output must own no live dump. Returns 0 or -1 with *out zeroed on error. */
int trace_fmt_load_dump(os_file *f, trace_loaded *out);
void trace_fmt_dump_free(trace_loaded *dump);

/* Pairs first timestamps per frame into the caller's sample buffer. Skips
 * incomplete frames and negative gaps. If scratch space or sample capacity
 * is insufficient, returns -1 with *nlat == 0. */
int trace_fmt_latencies(const trace_rec *recs, size_t n, enum trace_ev from,
                        enum trace_ev to, uint64_t *lat, size_t cap, size_t *nlat);
/* Nearest rank, pct in 1..100. Sorts in place. Empty input returns 0. */
uint64_t trace_fmt_pct(uint64_t *values, size_t n, unsigned pct);

/* Replay: delivers records to a sink with their original spacing (scaled by speed)
 * or back to back (fast). The sink runs on the calling thread. */
typedef void (*trace_input_sink)(const trace_input_rec *ev, void *user);
typedef struct trace_replay_opts {
    int fast;        /* nonzero: no waiting */
    double speed;    /* > 0 and finite; 2.0 halves the gaps */
} trace_replay_opts;
/* opts and sink non-NULL, recs non-NULL unless n is 0. Returns 0 on success,
 * -1 when a record's deadline is unrepresentable. */
int trace_replay(const trace_input_rec *recs, size_t n, const trace_replay_opts *opts,
                 trace_input_sink sink, void *user);

#endif
