#include "trace_format.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

_Static_assert(TRACE_MAX_THREADS < 32u, "slot mask fits uint32_t");
#define TRACE_SLOT_MASK ((UINT32_C(1) << TRACE_MAX_THREADS) - UINT32_C(1))

typedef struct trace_ring {
    uint64_t total;   /* append cursor, shared by successive owners */
    trace_rec recs[TRACE_RING_CAP];   /* thread is filled from the slot at dump time */
} trace_ring;

/* The trace ring is the one permitted global. */
static trace_ring g_rings[TRACE_MAX_THREADS];
static _Atomic uint32_t g_claimed;   /* one bit per owned slot */
static trace_input_rec g_in[TRACE_INPUT_CAP];
static uint64_t g_in_total;
static _Thread_local trace_ring *tl_ring;
/* Derived capacity: one retained stamp for each startup enum member. */
#define TRACE_STARTUP_COUNT (TRACE_STARTUP_END - TRACE_STARTUP_PROCESS_START)
static _Atomic uint64_t g_startup[TRACE_STARTUP_COUNT];

static bool startup_event(enum trace_ev ev) {
    return ev >= TRACE_STARTUP_PROCESS_START && ev < TRACE_STARTUP_END;
}
static void startup_put(uint64_t ns, enum trace_ev ev) {
    uint64_t absent = 0;
    (void)atomic_compare_exchange_strong(&g_startup[ev - TRACE_STARTUP_PROCESS_START], &absent, ns);
}

int trace_thread_register(void) {
    if (tl_ring != NULL) return (int)(tl_ring - g_rings);

    uint32_t claimed = atomic_load_explicit(&g_claimed, memory_order_relaxed);
    for (;;) {
        uint32_t available = TRACE_SLOT_MASK & ~claimed;
        if (available == 0) return -1;
        unsigned i = 0;
        while ((available & (UINT32_C(1) << i)) == 0) i++;
        uint32_t bit = UINT32_C(1) << i;
        if (atomic_compare_exchange_weak_explicit(&g_claimed, &claimed, claimed | bit,
                memory_order_acquire, memory_order_relaxed)) {
            tl_ring = &g_rings[i];
            return (int)i;
        }
    }
}

void trace_thread_release(void) {
    trace_ring *r = tl_ring;
    ASSERT(r != NULL);
    uint32_t bit = UINT32_C(1) << (r - g_rings);
    tl_ring = NULL;
    /* Bitmap updates are RMWs, preserving release sequences across slots. */
    atomic_fetch_and_explicit(&g_claimed, ~bit, memory_order_release);
}

static inline void ring_put(trace_ring *r, uint64_t ns, enum trace_ev ev, uint32_t frame_id) {
    trace_rec *rec = &r->recs[r->total & (TRACE_RING_CAP - 1u)];
    rec->ns = ns;
    rec->frame_id = frame_id;
    rec->ev = (uint16_t)ev;
    r->total++;
}

void trace_record(enum trace_ev ev, uint32_t frame_id) {
    if (startup_event(ev) && atomic_load(&g_startup[ev - TRACE_STARTUP_PROCESS_START])) return;
    trace_record_at(os_now_ns(), ev, frame_id);
}

void trace_record_at(uint64_t ns, enum trace_ev ev, uint32_t frame_id) {
    if (startup_event(ev)) {
        startup_put(ns, ev);
        return;
    }
    trace_ring *r = tl_ring;
    if (r != NULL) ring_put(r, ns, ev, frame_id);
}

static int write_all(os_file *f, const void *data, size_t len) {
    const unsigned char *next = data;
    while (len) {
        size_t written;
        if (os_file_write(f, next, len, &written) != OS_OK || written == 0) return -1;
        next += written;
        len -= written;
    }
    return 0;
}

int trace_dump(os_file *f) {
    trace_rec startup[TRACE_STARTUP_COUNT];
    uint32_t startup_count = 0;
    for (size_t i = 0; i < TRACE_STARTUP_COUNT; i++) {
        uint64_t ns = atomic_load(&g_startup[i]);
        if (ns) startup[startup_count++] = (trace_rec){ns, 0, (uint16_t)(TRACE_STARTUP_PROCESS_START + i), 0};
    }

    uint32_t nrings = 0;
    for (unsigned i = 0; i < TRACE_MAX_THREADS; i++) {
        if (g_rings[i].total || (i == 0 && startup_count)) nrings++;
    }
    const trace_file_header header = {
        TRACE_FMT_MAGIC, TRACE_FMT_VERSION, nrings, sizeof(trace_rec), TRACE_RING_CAP,
    };
    if (write_all(f, &header, sizeof header) != 0) return -1;

    /* Stage one x86 base page of serialized records to amortize writes. */
    trace_rec batch[4096 / sizeof(trace_rec)];
    const size_t batch_cap = sizeof batch / sizeof batch[0];
    for (unsigned i = 0; i < TRACE_MAX_THREADS; i++) {
        const trace_ring *r = &g_rings[i];
        uint32_t retained = i == 0 ? startup_count : 0;
        if (!r->total && !retained) continue;
        uint64_t room = TRACE_RING_CAP - retained;
        uint64_t count = r->total < room ? r->total : room;
        uint64_t start = r->total - count;
        const trace_ring_header block = { i, (uint32_t)count + retained };
        if (write_all(f, &block, sizeof block) != 0 ||
            write_all(f, startup, retained * sizeof startup[0]) != 0) return -1;

        for (uint64_t k = 0; k < count;) {
            size_t n = count - k < batch_cap ? (size_t)(count - k) : batch_cap;
            for (size_t j = 0; j < n; j++) {
                batch[j] = r->recs[(start + k + j) & (TRACE_RING_CAP - 1u)];
                batch[j].thread = (uint16_t)i;
            }
            if (write_all(f, batch, n * sizeof batch[0]) != 0) return -1;
            k += n;
        }
    }

    uint64_t count = g_in_total < TRACE_INPUT_CAP ? g_in_total : TRACE_INPUT_CAP;
    const trace_input_header input = {
        TRACE_FMT_INPT_TAG, TRACE_FMT_VERSION, sizeof(trace_input_rec),
        (uint32_t)count, g_in_total - count,
    };
    if (write_all(f, &input, sizeof input) != 0) return -1;

    /* The input ring already stores wire records. Wrap splits it into two spans. */
    size_t first = (size_t)(input.dropped & (TRACE_INPUT_CAP - 1u));
    size_t head = TRACE_INPUT_CAP - first;
    if (head > input.count) head = input.count;
    if (write_all(f, &g_in[first], head * sizeof g_in[0]) != 0) return -1;
    return write_all(f, g_in, (input.count - head) * sizeof g_in[0]);
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
    ASSERT(utf8 != NULL || utf8_len == 0);
    trace_input_rec *r = in_slot(t0_ns, kind);
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
