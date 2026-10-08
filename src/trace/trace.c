#include "trace.h"

#include <stdatomic.h>
#include <time.h>

typedef struct trace_ring {
    uint64_t total;   /* events ever written to this ring */
    uint32_t index;   /* ring index, also stored in each record */
    trace_rec recs[TRACE_RING_CAP];
} trace_ring;

/* The trace ring is the one permitted global. */
static trace_ring g_rings[TRACE_MAX_THREADS];
static atomic_uint g_next_ring;
static _Thread_local trace_ring *tl_ring;

void trace_init(void) {
    for (uint32_t i = 0; i < TRACE_MAX_THREADS; i++) {
        g_rings[i].total = 0;
        g_rings[i].index = i;
    }
    atomic_store(&g_next_ring, 0u);
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
    return fflush(f) == 0 ? 0 : -1;
}
