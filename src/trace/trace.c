#include "trace.h"
#include "trace_fmt.h"

#include <stdatomic.h>
#include <string.h>
#include <time.h>

typedef struct trace_ring {
    uint64_t total;   /* events ever written to this ring */
    uint32_t index;   /* ring index, also stored in each record */
    trace_rec recs[TRACE_RING_CAP];
} trace_ring;

/* The trace ring is the one permitted global. */
static trace_ring g_rings[TRACE_MAX_THREADS];
static atomic_uint g_next_ring;
static trace_input_rec g_in[TRACE_INPUT_CAP];
static uint64_t g_in_total;
static _Thread_local trace_ring *tl_ring;

void trace_init(void) {
    for (uint32_t i = 0; i < TRACE_MAX_THREADS; i++) {
        g_rings[i].total = 0;
        g_rings[i].index = i;
    }
    atomic_store(&g_next_ring, 0u);
    g_in_total = 0;
}

int trace_thread_register(void) {
    if (tl_ring != NULL) {
        return (int)tl_ring->index;
    }
    unsigned idx = atomic_fetch_add(&g_next_ring, 1u);
    if (idx >= TRACE_MAX_THREADS) {
        return -1;
    }
    tl_ring = &g_rings[idx];
    return (int)idx;
}

static inline void ring_put(trace_ring *r, uint64_t ns, enum trace_ev ev, uint32_t frame_id) {
    trace_rec *rec = &r->recs[r->total & (TRACE_RING_CAP - 1u)];
    rec->ns = ns;
    rec->frame_id = frame_id;
    rec->ev = (uint16_t)ev;
    rec->thread = (uint16_t)r->index;
    r->total++;
}

uint64_t trace_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

void trace_record(enum trace_ev ev, uint32_t frame_id) {
    trace_ring *r = tl_ring;
    if (r == NULL) {
        return;
    }
    ring_put(r, trace_now_ns(), ev, frame_id);
}

void trace_record_at(uint64_t ns, enum trace_ev ev, uint32_t frame_id) {
    trace_ring *r = tl_ring;
    if (r == NULL) {
        return;
    }
    ring_put(r, ns, ev, frame_id);
}

void trace_reset(void) {
    for (uint32_t i = 0; i < TRACE_MAX_THREADS; i++) {
        g_rings[i].total = 0;
    }
    g_in_total = 0;
}

int trace_dump(FILE *f) {
    unsigned used = atomic_load(&g_next_ring);
    if (used > TRACE_MAX_THREADS) {
        used = TRACE_MAX_THREADS;
    }
    /* Header and per-ring blocks: layout is defined in trace_fmt.h. */
    char magic[8] = { 'E', 'D', 'T', 'R', 'A', 'C', 'E', '1' };
    uint32_t hdr[4];
    hdr[0] = 1u;                       /* version */
    hdr[1] = used;                     /* nrings */
    hdr[2] = (uint32_t)sizeof(trace_rec);
    hdr[3] = TRACE_RING_CAP;
    if (fwrite(magic, 1, sizeof magic, f) != sizeof magic) return -1;
    if (fwrite(hdr, sizeof hdr[0], 4, f) != 4) return -1;

    for (unsigned i = 0; i < used; i++) {
        const trace_ring *r = &g_rings[i];
        uint64_t count = r->total < TRACE_RING_CAP ? r->total : TRACE_RING_CAP;
        uint64_t start = r->total - count;
        uint32_t blk[2] = { r->index, (uint32_t)count };
        if (fwrite(blk, sizeof blk[0], 2, f) != 2) return -1;
        for (uint64_t k = 0; k < count; k++) {
            const trace_rec *rec = &r->recs[(start + k) & (TRACE_RING_CAP - 1u)];
            if (fwrite(rec, sizeof *rec, 1, f) != 1) return -1;
        }
    }
    {
        /* INPT section: tag, version, rec_size, count, dropped, records (trace_fmt.h). */
        uint64_t count = g_in_total < TRACE_INPUT_CAP ? g_in_total : TRACE_INPUT_CAP;
        uint64_t dropped = g_in_total - count;
        uint32_t sh[4] = { TRACE_FMT_INPT_TAG, 1u, (uint32_t)sizeof(trace_input_rec), (uint32_t)count };
        if (fwrite(sh, sizeof sh[0], 4, f) != 4) return -1;
        if (fwrite(&dropped, sizeof dropped, 1, f) != 1) return -1;
        for (uint64_t k = 0; k < count; k++) {
            const trace_input_rec *rec = &g_in[(dropped + k) & (TRACE_INPUT_CAP - 1u)];
            if (fwrite(rec, sizeof *rec, 1, f) != 1) return -1;
        }
    }
    return fflush(f) == 0 ? 0 : -1;
}

/* ---- input ring ---- */

static inline trace_input_rec *in_slot(uint64_t t0_ns, enum trace_in_kind kind) {
    trace_input_rec *r = &g_in[g_in_total & (TRACE_INPUT_CAP - 1u)];
    memset(r, 0, sizeof *r);
    r->seq = g_in_total;
    r->t0_ns = t0_ns;
    r->kind = (uint16_t)kind;
    g_in_total++;
    return r;
}

void trace_input_key(uint64_t t0_ns, enum trace_in_kind kind, uint32_t keysym, uint32_t state,
                     int repeat, const char *utf8, size_t utf8_len) {
    trace_input_rec *r = in_slot(t0_ns, kind);
    if (utf8 == NULL) utf8_len = 0;
    if (utf8_len > sizeof r->p.key.utf8) utf8_len = sizeof r->p.key.utf8;
    r->p.key.keysym = keysym;
    r->p.key.state = state;
    if (utf8_len > 0) memcpy(r->p.key.utf8, utf8, utf8_len);
    r->p.key.utf8_len = (uint8_t)utf8_len;
    r->p.key.repeat = repeat ? 1u : 0u;
}

void trace_input_pointer(uint64_t t0_ns, enum trace_in_kind kind, int32_t x, int32_t y,
                         uint32_t button, uint32_t mods) {
    trace_input_rec *r = in_slot(t0_ns, kind);
    r->p.pointer.x = x;
    r->p.pointer.y = y;
    r->p.pointer.button = button;
    r->p.pointer.mods = mods;
}

void trace_input_wheel(uint64_t t0_ns, int32_t dx, int32_t dy, uint32_t mods) {
    trace_input_rec *r = in_slot(t0_ns, TRACE_IN_WHEEL);
    r->p.wheel.dx = dx;
    r->p.wheel.dy = dy;
    r->p.wheel.mods = mods;
}

void trace_input_resize(uint64_t t0_ns, uint32_t w, uint32_t h) {
    trace_input_rec *r = in_slot(t0_ns, TRACE_IN_RESIZE);
    r->p.resize.w = w;
    r->p.resize.h = h;
}

void trace_input_focus(uint64_t t0_ns, int focused) {
    trace_input_rec *r = in_slot(t0_ns, TRACE_IN_FOCUS);
    r->p.focus.focused = focused ? 1u : 0u;
}

void trace_input_clipboard(uint64_t t0_ns, uint32_t selection, uint32_t length) {
    trace_input_rec *r = in_slot(t0_ns, TRACE_IN_CLIPBOARD);
    r->p.clipboard.selection = selection;
    r->p.clipboard.length = length;
}

void trace_input_filechange(uint64_t t0_ns, uint32_t watch_id, uint32_t flags) {
    trace_input_rec *r = in_slot(t0_ns, TRACE_IN_FILECHANGE);
    r->p.filechange.watch_id = watch_id;
    r->p.filechange.flags = flags;
}

uint64_t trace_input_count(void) { return g_in_total; }

uint64_t trace_input_dropped(void) {
    return g_in_total > TRACE_INPUT_CAP ? g_in_total - TRACE_INPUT_CAP : 0;
}

size_t trace_input_copy(trace_input_rec *dst, size_t cap) {
    uint64_t have = g_in_total < TRACE_INPUT_CAP ? g_in_total : TRACE_INPUT_CAP;
    uint64_t first = g_in_total - have;
    size_t n = have < cap ? (size_t)have : cap;
    for (size_t i = 0; i < n; i++) {
        dst[i] = g_in[(first + i) & (TRACE_INPUT_CAP - 1u)];
    }
    return n;
}
