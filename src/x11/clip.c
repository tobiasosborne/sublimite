/* X selections: bounded, asynchronous ICCCM INCR and MULTIPLE (P2.2b). */
#include "clip.h"
#include "input.h"
#include "trace/trace.h"
#include "work/work.h"
#include <xcb/xcbext.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <limits.h>

#define C(p) ((xcb_connection_t *)(p)->conn)
#define NSEL 2
#define NSERVE 16u
#define NJOB 8u
#define MAX_PAIRS 64u
#define CHUNK_MAX (256u * 1024u)
#define DATA_MAX (64u * 1024u * 1024u)
#define OWN_BUDGET (128u * 1024u * 1024u)
#define MAX_WAITERS 256u
#define NCHUNK 8u                         /* receive chunks in flight to the worker, all selections (P2.2h) */

#define SLICE_BYTES (1024u * 1024u)       /* (E) about 0.2 ms of memcpy plus socket write per UI slice */
typedef struct clip_blob { size_t refs, len, cap; uint8_t data[]; } clip_blob;
/* P2.2h: receive buffers are built by a src/work worker. A sink belongs to the worker while jobs are in flight
 * (infl > 0); the UI thread reads it only after the completion message of its last job. */
typedef struct clip_sink { clip_blob *buf; size_t len; bool failed; } clip_sink;
typedef struct clip_chunk {
    bool used, close, discard, string, freeing;
    xcb_get_property_reply_t *reply;     /* owned by the job; freed by the worker (or by destroy if the job never ran) */
    const uint8_t *data; size_t dlen;
    size_t ub, cap;                      /* UI-side worst-case bytes of this chunk, capacity the buffer must have */
    clip_sink *sink;
    void *ptr;                           /* freeing: the blob to release */
    uint32_t w;
} clip_chunk;
enum { MSG_CHUNK = 1, MSG_CLOSE = 2, MSG_FREE = 3, MSG_FAILED = 1, MSG_DISCARDED = 2 };
typedef struct clip_receive {
    uint32_t state;                  /* 0 idle, 1 notify, 2 property reply, 3 INCR chunk */
    uint32_t waiters;
    xcb_atom_t target, type;
    xcb_window_t win;
    uint32_t time, offset;
    bool incr, draining;
    uint64_t drain_end;
    xcb_get_property_cookie_t cookie;
    uint64_t deadline;
    clip_sink sink;                  /* growing receive buffer, built by the worker, accounted by cap */
    size_t cap;                      /* capacity charged to mem (planned by the UI thread, applied by the worker) */
    size_t len, ub_infl, raw;        /* worker-confirmed decoded bytes; worst case of chunks in flight; raw INCR bytes */
    unsigned infl;                   /* jobs (chunks and the close) not yet reported back */
    bool wfail, closed, need_close, close_discard, fin_ok;
    uint32_t lower;                  /* INCR advertised lower bound */
} clip_receive;
struct clip_job;
typedef struct clip_serve {
    clip_blob *blob;
    struct clip_job *job;
    xcb_window_t win;
    xcb_atom_t prop, target;
    uint64_t offset;
    uint64_t deadline;
    bool deleted, zero, checking;
    uint32_t write, barrier;
} clip_serve;
typedef struct clip_job {
    uint32_t state;                  /* 0 free, 1 MULTIPLE property, 2 checked writes, 3 writes in progress, 4 finished, notify held */
    uint64_t seq;                    /* arrival order */
    xcb_atom_t held_prop;
    size_t next;
    xcb_selection_request_event_t rq;
    clip_blob *blob;
    uint32_t stamp;
    uint64_t deadline;
    uint32_t cookie, barrier;
    uint32_t checks[MAX_PAIRS + 1u], nchecks;
    int32_t slots[MAX_PAIRS];
    xcb_atom_t pairs[MAX_PAIRS * 2u];
    size_t npairs;
    bool multiple;
} clip_job;
typedef struct x11_clip {
    clip_blob *own[NSEL];
    clip_blob *local_blob[NSEL];     /* retained association during partial completion emission */
    uint32_t failed[NSEL], lost[NSEL]; /* events deferred while the input ring is full */
    size_t mem, max_chunk, chunk;
    uint64_t job_seq;
    uint32_t local_ready[NSEL];       /* confirmed local pastes whose completion is not yet queued */
    uint64_t local_at[NSEL];
    uint32_t own_time[NSEL];
    bool confirmed[NSEL], retried[NSEL];
    bool uncertain[NSEL];
    uint32_t claiming[NSEL];          /* 1 timestamp property, 2 owner reply */
    uint32_t owner_cookie[NSEL], stamp_cookie[NSEL];
    uint64_t owner_deadline[NSEL];
    uint32_t local_requests[NSEL];
    bool clear_deferred[NSEL], clear_pending[NSEL];
    uint32_t clear_cookie[NSEL];
    uint64_t clear_deadline[NSEL];
    clip_receive rx[NSEL];
    clip_serve tx[NSERVE];
    clip_job jobs[NJOB];
    uint32_t timeout_ms, save_ms;
    clip_blob *got;                   /* paste buffer: shared with the owned blob for local pastes */
    size_t budget, slice, max_slice;
    size_t ui_bytes;                  /* bulk bytes copied, converted or realloc-moved on the UI thread (P2.2h) */
    work_pool *pool;                  /* initialized before runtime dispatch; one bulk worker */
    clip_chunk wjob[NCHUNK + NSEL];  /* [NCHUNK + w] is the reserved close job of selection w */
    unsigned frees_out;               /* deferred blob frees not yet reported (their bytes are still in mem) */
    unsigned jobs_out;                /* sum of rx[].infl */
    uint64_t max_poll_ns, max_poll_cpu_ns;   /* longest x11_clip_poll / x11_clip_event call: wall, and thread CPU (immune to preemption) */
    xcb_atom_t sel[NSEL], prop[NSEL], claim_prop[NSEL];
    xcb_atom_t utf8, targets, textplain, incr, timestamp, multiple, atom_pair, manager, save, save_prop, null_type;
    bool saving, save_done;
    uint8_t scratch[CHUNK_MAX];
} x11_clip;
#define CL(p) ((x11_clip *)(p)->clip)

static void note(x11_clip *c, size_t n) { c->slice += n; if (c->slice > c->max_slice) c->max_slice = c->slice; }
static uint64_t deadline_after(uint32_t ms) { return trace_now_ns() + (uint64_t)ms * UINT64_C(1000000); }
static bool older(uint32_t a, uint32_t b) { return a && b && (int32_t)(a - b) < 0; }
static int which_of(const x11_clip *c, xcb_atom_t sel) {
    for (int w = 0; w < NSEL; w++) if (c->sel[w] == sel) return w;
    return -1;
}
static bool defer_free(x11_clip *c, clip_blob *b);
static void blob_unref(x11_clip *c, clip_blob *b) {
    if (!b || --b->refs) return;
    if (b->cap >= SLICE_BYTES && defer_free(c, b)) return;      /* freeing tens of MiB is page-table work: not on the UI thread */
    c->mem -= b->cap; free(b);
}
static bool mem_fits(const x11_clip *c, size_t extra) { return extra <= c->budget && c->mem <= c->budget - extra; }
static bool slice_room(const x11_clip *c, size_t n) { return c->slice == 0 || c->slice + n <= SLICE_BYTES; }
static void set_got(x11_clip *c, clip_blob *b) {
    clip_blob *old = c->got;
    if (b) b->refs++;
    c->got = b; blob_unref(c, old);
}
static void tx_free(plat *p, clip_serve *s) {
    if (s->checking) { xcb_discard_reply(C(p), s->write); xcb_discard_reply(C(p), s->barrier); }
    xcb_window_t win = s->win;
    blob_unref(CL(p), s->blob);
    memset(s, 0, sizeof *s);
    if (win && win != p->win) {
        bool other = false;
        for (unsigned i = 0; i < NSERVE; i++) if (CL(p)->tx[i].blob && CL(p)->tx[i].win == win) other = true;
        if (!other) { uint32_t mask = 0; xcb_change_window_attributes(C(p), win, XCB_CW_EVENT_MASK, &mask); }
    }
}
static void job_release(plat *p, clip_job *j) {
    if (j->state == 1) xcb_discard_reply(C(p), j->cookie);
    if (j->state == 2 || j->state == 3) {
        if (j->state == 2) xcb_discard_reply(C(p), j->barrier);
        for (unsigned i = 0; i < j->nchecks; i++) if (j->checks[i]) xcb_discard_reply(C(p), j->checks[i]);
    }
    blob_unref(CL(p), j->blob); j->blob = NULL; j->nchecks = 0;
}
static void job_free(plat *p, clip_job *j) { job_release(p, j); memset(j, 0, sizeof *j); }

static bool pool_init(x11_clip *c);
int x11_clip_init(plat *p) {
    x11_clip *c = calloc(1, sizeof *c);
    if (!c) return PLAT_ERR_FAIL;
    const char *names[] = { "CLIPBOARD", "UTF8_STRING", "TARGETS", "EDIT_SELECTION", "EDIT_PRIMARY",
        "text/plain;charset=utf-8", "INCR", "TIMESTAMP", "MULTIPLE", "ATOM_PAIR", "CLIPBOARD_MANAGER",
        "SAVE_TARGETS", "EDIT_SAVE_TARGETS", "NULL", "EDIT_CLAIM_CLIPBOARD", "EDIT_CLAIM_PRIMARY" };
    xcb_atom_t *dst[] = { &c->sel[0], &c->utf8, &c->targets, &c->prop[0], &c->prop[1], &c->textplain,
        &c->incr, &c->timestamp, &c->multiple, &c->atom_pair, &c->manager, &c->save, &c->save_prop,
        &c->null_type, &c->claim_prop[0], &c->claim_prop[1] };
    xcb_intern_atom_cookie_t ck[16];
    for (unsigned i = 0; i < 16; i++) ck[i] = xcb_intern_atom(C(p), 0, (uint16_t)strlen(names[i]), names[i]);
    bool ok = true;
    for (unsigned i = 0; i < 16; i++) {
        xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(C(p), ck[i], NULL);
        if (!r || !r->atom) ok = false;
        *dst[i] = r ? r->atom : XCB_ATOM_NONE;
        free(r);
    }
    if (!ok) { free(c); return PLAT_ERR_FAIL; }
    c->sel[1] = XCB_ATOM_PRIMARY;
    /* Negotiate once at init (P2.2c); chunks also fit servers without BIG-REQUESTS. */
    (void)xcb_get_maximum_request_length(C(p));
    c->max_chunk = (size_t)xcb_get_setup(C(p))->maximum_request_length * 4u - 64u;
    if (c->max_chunk > CHUNK_MAX) c->max_chunk = CHUNK_MAX;
    c->chunk = c->max_chunk;
    c->timeout_ms = 5000; c->save_ms = 1000; c->budget = OWN_BUDGET;
    if (!pool_init(c)) { free(c); return PLAT_ERR_FAIL; }
    p->clip = c;
    return PLAT_OK;
}
void x11_clip_destroy(plat *p) {
    x11_clip *c = CL(p);
    if (!c) return;
    if (c->pool) { work_pool_shutdown(c->pool); free(c->pool); c->pool = NULL; }     /* joins: nothing touches a sink after this */
    for (unsigned i = 0; i < NCHUNK + NSEL; i++) { free(c->wjob[i].reply); c->wjob[i].reply = NULL; free(c->wjob[i].ptr); c->wjob[i].ptr = NULL; }
    for (unsigned i = 0; i < NJOB; i++) job_free(p, &c->jobs[i]);
    for (unsigned i = 0; i < NSERVE; i++) tx_free(p, &c->tx[i]);
    for (int w = 0; w < NSEL; w++) {
        if (c->claiming[w] == 2) xcb_discard_reply(C(p), c->owner_cookie[w]);
        if (c->rx[w].state == 2) xcb_discard_reply(C(p), c->rx[w].cookie.sequence);
        if (c->clear_pending[w]) xcb_discard_reply(C(p), c->clear_cookie[w]);
        if (c->rx[w].win) xcb_destroy_window(C(p), c->rx[w].win);
        free(c->rx[w].sink.buf); blob_unref(c, c->own[w]); blob_unref(c, c->local_blob[w]);
    }
    blob_unref(c, c->got); free(c); p->clip = NULL;
}

/* Consume a whole UTF-8 sequence even when only its Latin-1 replacement fits the output chunk. */
static size_t latin_chunk(const uint8_t *s, size_t len, uint8_t *out, size_t cap, size_t *used) {
    size_t n = 0, i = 0;
    while (i < len && n < cap) {
        uint8_t b = s[i];
        if (b < 0x80) { out[n++] = b; i++; }
        else if ((b & 0xe0) == 0xc0 && i + 1 < len && (s[i + 1] & 0xc0) == 0x80) {
            uint32_t cp = ((uint32_t)(b & 0x1f) << 6) | (s[i + 1] & 0x3fu);
            out[n++] = cp >= 0x80 && cp <= 0xff ? (uint8_t)cp : (uint8_t)'?'; i += 2;
        } else {
            out[n++] = '?';
            size_t sequence = (b & 0xf0) == 0xe0 ? 3u : (b & 0xf8) == 0xf0 ? 4u : 1u;
            i++;
            for (size_t k = 1; k < sequence && i < len && (s[i] & 0xc0) == 0x80; k++) i++;
        }
    }
    *used = i; return n;
}
size_t x11_utf8_to_latin1(const uint8_t *s, size_t len, uint8_t *out) {
    size_t used; return latin_chunk(s, len, out, len, &used);
}
size_t x11_latin1_to_utf8(const uint8_t *s, size_t len, uint8_t *out) {
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < 0x80) out[n++] = s[i];
        else { out[n++] = (uint8_t)(0xc0 | (s[i] >> 6)); out[n++] = (uint8_t)(0x80 | (s[i] & 0x3f)); }
    }
    return n;
}

bool x11_clip_notify_matches(const xcb_selection_notify_event_t *n, xcb_window_t win, xcb_atom_t selection,
                             xcb_atom_t target, xcb_atom_t property, uint32_t time) {
    return (n->response_type & 0x7f) == XCB_SELECTION_NOTIFY && n->requestor == win && n->selection == selection &&
           n->target == target && n->time == time && (!n->property || n->property == property);
}

/* The only reply parsing entry point: also exercised with exact-size fuzz allocations. */
bool x11_clip_decode_property(const uint8_t *bytes, size_t size, x11_clip_property *v) {
    memset(v, 0, sizeof *v);
    if (size < sizeof(xcb_get_property_reply_t)) return false;
    xcb_get_property_reply_t h;
    memcpy(&h, bytes, sizeof h);
    if (h.response_type != 1 || (h.format != 8 && h.format != 32) || !h.type) return false;
    size_t stride = h.format / 8u;
    if ((uint64_t)h.length * 4u > size - sizeof h || (uint64_t)h.value_len * stride > (uint64_t)h.length * 4u)
        return false;
    v->type = h.type; v->format = h.format; v->after = h.bytes_after;
    v->data = bytes + sizeof h; v->len = (size_t)h.value_len * stride;
    return true;
}
bool x11_clip_decode_pairs(const x11_clip_property *v, xcb_atom_t atom_pair, xcb_atom_t *out, size_t cap, size_t *n) {
    *n = 0;
    if (v->type != atom_pair || v->format != 32 || v->after || !v->len || v->len % 8u || v->len / 8u > cap) return false;
    memcpy(out, v->data, v->len); *n = v->len / 8u; return true;
}
int x11_clip_decode_transfer(const x11_clip_property *v, xcb_atom_t utf8, xcb_atom_t textplain,
                             xcb_atom_t incr, xcb_atom_t expected, bool allow_incr) {
    if (v->type == incr) {
        uint32_t lower;
        if (!allow_incr || v->format != 32 || v->len != 4 || v->after) return -1;
        memcpy(&lower, v->data, 4);
        return lower <= DATA_MAX ? 1 : -1;
    }
    if (v->format != 8 || (v->type != utf8 && v->type != textplain && v->type != XCB_ATOM_STRING) ||
        (expected && v->type != expected) || (v->after && (!v->len || v->len % 4u)) ||
        (uint64_t)v->after + v->len > DATA_MAX) return -1;
    return 0;
}
static bool reply_view(const xcb_get_property_reply_t *r, x11_clip_property *v) {
    if (!r) return false;
    return x11_clip_decode_property((const uint8_t *)r, sizeof *r + (size_t)r->length * 4u, v);
}

static bool event_room(const plat *p) {
    const x11_input *in = p->in;
    return in && in->qt - in->qh < X11_QUEUE_CAP;
}
static void result(plat *p, int w, bool ok, uint32_t code) {
    if (!event_room(p)) {
        /* Success batches retain their blobs/counts at the caller. Failures and
         * ownership notifications carry no data and can be retained as counts. */
        if (code == 1) CL(p)->failed[w]++;
        else if (code == 2) CL(p)->lost[w]++;
        return;
    }
    plat_event e = { .kind = PLAT_EV_CLIPBOARD, .clip_which = (uint8_t)w, .clip_ok = ok, .code = code };
    x11_push_event(p, &e);
}
static void claim(plat *p, int w, uint32_t time) {
    x11_clip *c = CL(p);
    c->own_time[w] = time;
    if (!time) {
        c->stamp_cookie[w] = xcb_change_property(C(p), XCB_PROP_MODE_REPLACE, p->win, c->claim_prop[w],
                                                XCB_ATOM_INTEGER, 8, 0, NULL).sequence;
        c->claiming[w] = 1;
    } else {
        xcb_set_selection_owner(C(p), p->win, c->sel[w], time);
        c->owner_cookie[w] = xcb_get_selection_owner(C(p), c->sel[w]).sequence;
        c->claiming[w] = 2;
    }
    c->owner_deadline[w] = deadline_after(c->timeout_ms);
    xcb_flush(C(p));
}
static bool set_fits(const x11_clip *c, int which, size_t len) {
    /* A blob nobody else holds is released by the replacement, so it does not count against the new one. */
    size_t reclaim = c->own[which] && c->own[which]->refs == 1 ? c->own[which]->cap : 0;
    return len <= DATA_MAX && len <= c->budget && c->mem - reclaim <= c->budget - len;
}
static void on_work_msg(const work_msg *m, void *ud);
static bool make_room(plat *p, int which, size_t len) {
    x11_clip *c = CL(p);
    if (set_fits(c, which, len)) return true;
    if (c->jobs_out && c->pool) (void)work_mailbox_drain(c->pool, on_work_msg, c);       /* completed frees give mem back */
    /* got remains borrowed until a later paste completes, even with an empty queue. */
    /* Admission never waits for a worker. The caller can retry after the
     * completion mailbox returns the deferred bytes to the budget. */
    return set_fits(c, which, len);
}
static void install(plat *p, int which, clip_blob *b) {
    x11_clip *c = CL(p);
    if (c->claiming[which] == 2) xcb_discard_reply(C(p), c->owner_cookie[which]);
    if (c->clear_pending[which]) xcb_discard_reply(C(p), c->clear_cookie[which]);
    c->clear_pending[which] = c->clear_deferred[which] = false; c->clear_deadline[which] = 0;
    blob_unref(c, c->own[which]); c->mem += b->len; c->own[which] = b;
    c->confirmed[which] = false; c->retried[which] = false; c->uncertain[which] = false;
    claim(p, which, p->last_time);
}
x11_clip_buf *x11_clip_buf_new(size_t len) {
    if (len > DATA_MAX) return NULL;
    clip_blob *b = malloc(sizeof *b + len);
    if (b) { b->refs = 1; b->len = len; b->cap = len; }
    return b;
}
uint8_t *x11_clip_buf_data(x11_clip_buf *b) { return b->data; }
void x11_clip_buf_free(x11_clip_buf *b) { free(b); }
int x11_clip_set_buf(plat *p, int which, x11_clip_buf *b) {
    x11_clip *c = CL(p);
    if (!b) return PLAT_ERR_FAIL;
    if (!c || which < 0 || which >= NSEL || b->len > DATA_MAX || !make_room(p, which, b->len)) { free(b); return PLAT_ERR_FAIL; }
    install(p, which, b);
    return PLAT_OK;
}
int plat_clip_set(plat *p, int which, const void *utf8, size_t len) {
    x11_clip *c = CL(p);
    if (!c || which < 0 || which >= NSEL || (len && !utf8) || len > DATA_MAX) return PLAT_ERR_FAIL;
    if (!make_room(p, which, len)) return PLAT_ERR_FAIL;
    clip_blob *b = x11_clip_buf_new(len);
    if (!b) return PLAT_ERR_FAIL;
    if (len) { memcpy(b->data, utf8, len); c->ui_bytes += len; }     /* synchronous by contract: the caller's buffer is borrowed */
    install(p, which, b);
    return PLAT_OK;
}


/* ---- receive worker (P2.2h) ----
 * The UI thread plans capacity and charges it to mem, then hands a reply to the worker; the worker grows the buffer,
 * expands Latin-1, copies, and finally shrinks or frees it. It reports through the pool mailbox only. */
static void put_u64(uint8_t *d, uint64_t v) { memcpy(d, &v, sizeof v); }
static uint64_t get_u64(const uint8_t *d) { uint64_t v; memcpy(&v, d, sizeof v); return v; }
static void chunk_job(work_ctx *ctx) {
    clip_chunk *k = ctx->arg; clip_sink *s = k->sink;
    uint8_t flags = 0; size_t cap = 0;
    if (k->freeing) {
        free(k->ptr); k->ptr = NULL;
        work_msg m = { .kind = MSG_FREE, .generation = ctx->generation };
        (void)work_publish(ctx, &m); return;
    }
    if (k->close) {
        if (!s->failed && !k->discard) {
            if (!s->buf) {                                    /* nothing arrived: an empty blob */
                s->buf = malloc(sizeof *s->buf);
                if (s->buf) { s->buf->refs = 1; s->buf->len = s->buf->cap = 0; } else s->failed = true;
            }
            if (s->buf) {
                clip_blob *b = s->buf; b->len = s->len;
                clip_blob *t = realloc(b, sizeof *b + b->len);     /* give the unused tail back */
                if (t) b = t;
                b->cap = t ? b->len : b->cap; s->buf = b; cap = b->cap;
            }
        }
        if (s->failed || k->discard) { free(s->buf); s->buf = NULL; s->len = 0; s->failed = false; flags |= MSG_DISCARDED; }
    } else if (!s->failed) {
        size_t n = k->dlen;
        if (k->string) for (size_t i = 0; i < k->dlen; i++) if (k->data[i] >= 0x80) n++;
        if (n > DATA_MAX - s->len) s->failed = true;
        else {
            if (!s->buf || s->buf->cap < k->cap) {
                clip_blob *b = realloc(s->buf, sizeof *b + k->cap);
                if (!b) s->failed = true;
                else { if (!s->buf) { b->refs = 1; b->len = 0; } b->cap = k->cap; s->buf = b; }
            }
            if (!s->failed && s->len + n > s->buf->cap) s->failed = true;
            if (!s->failed) {
                if (k->string) (void)x11_latin1_to_utf8(k->data, k->dlen, s->buf->data + s->len);
                else if (n) memcpy(s->buf->data + s->len, k->data, n);
                s->len += n;
            }
        }
        if (s->failed) flags |= MSG_FAILED;
    } else flags |= MSG_FAILED;
    free(k->reply); k->reply = NULL; k->data = NULL;
    work_msg m = { .kind = k->close ? MSG_CLOSE : MSG_CHUNK, .generation = ctx->generation };
    put_u64(m.data + 8, s->len); put_u64(m.data + 16, cap); put_u64(m.data + 24, (uint64_t)k->w);
    m.data[1] = flags;
    (void)work_publish(ctx, &m);
}
static void on_work_msg(const work_msg *m, void *ud) {
    x11_clip *c = ud;
    unsigned slot = m->generation;
    if (slot >= NCHUNK + NSEL) return;
    clip_chunk *k = &c->wjob[slot];
    if (m->kind == MSG_FREE) { c->mem -= k->cap; k->used = false; c->jobs_out--; c->frees_out--; return; }
    clip_receive *r = &c->rx[k->w];
    uint8_t flags = m->data[1];
    r->infl--; c->jobs_out--;
    if (m->kind == MSG_CHUNK) {
        r->ub_infl -= k->ub; r->len = (size_t)get_u64(m->data + 8);
        if (flags & MSG_FAILED) r->wfail = true;
    } else {
        size_t cap = (size_t)get_u64(m->data + 16);
        if ((flags & MSG_DISCARDED) && !k->discard) r->wfail = true;       /* the worker gave up: report failure */
        if (flags & MSG_DISCARDED) { c->mem -= r->cap; r->cap = 0; r->len = 0; r->ub_infl = 0; r->closed = false; }
        else { c->mem -= r->cap - cap; r->cap = cap; }
    }
    k->used = false;
}
static bool pool_init(x11_clip *c) {
    work_pool *wp = aligned_alloc(_Alignof(work_pool), sizeof *wp);
    if (!wp) return false;
    memset(wp, 0, sizeof *wp);
    if (work_pool_init(wp, 1, 0) != 0) { free(wp); return false; }
    c->pool = wp; return true;
}
static bool pool_ready(const x11_clip *c) { return c->pool != NULL; }
static unsigned chunk_free_slot(const x11_clip *c) {
    for (unsigned i = 0; i < NCHUNK; i++) if (!c->wjob[i].used) return i;
    return NCHUNK + NSEL;
}
static bool defer_free(x11_clip *c, clip_blob *b) {
    if (!c->pool) return false;
    unsigned slot = chunk_free_slot(c);
    if (slot == NCHUNK + NSEL) return false;
    clip_chunk *k = &c->wjob[slot];
    memset(k, 0, sizeof *k);
    k->freeing = true; k->ptr = b; k->cap = b->cap;
    work_job job = { .fn = chunk_job, .arg = k, .generation = slot, .cls = WORK_BULK };
    if (work_submit(c->pool, job).epoch == 0) return false;
    k->used = true; c->jobs_out++; c->frees_out++; return true;      /* mem is released when the worker reports the free */
}
static bool submit(x11_clip *c, unsigned slot, clip_receive *r) {
    work_job job = { .fn = chunk_job, .arg = &c->wjob[slot], .generation = slot, .cls = WORK_BULK };
    if (work_submit(c->pool, job).epoch == 0) return false;
    c->wjob[slot].used = true; r->infl++; c->jobs_out++; return true;
}
/* Queue the close job (shrink, or free on discard) behind every chunk of this receive. */
static bool rx_close(x11_clip *c, int w, bool discard) {
    clip_receive *r = &c->rx[w];
    if (!pool_ready(c)) return false;
    unsigned slot = NCHUNK + (unsigned)w;
    clip_chunk *k = &c->wjob[slot];
    memset(k, 0, sizeof *k);
    k->close = true; k->discard = discard; k->sink = &r->sink; k->w = (uint32_t)w;
    if (!submit(c, slot, r)) return false;
    r->closed = true; return true;
}
/* Drop the buffer behind the worker (INCR rejected mid-transfer): the receive keeps draining meanwhile. */
static void rx_discard(x11_clip *c, int w) {
    clip_receive *r = &c->rx[w];
    if (r->closed) return;
    if (!r->infl && !r->sink.buf) { c->mem -= r->cap; r->cap = r->len = 0; return; }
    if (!rx_close(c, w, true)) { r->need_close = true; r->close_discard = true; }
}

static void rx_convert(plat *p, int w, xcb_atom_t target) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    r->state = 1; r->target = target; r->type = 0; r->offset = 0; r->incr = false; r->raw = 0; r->lower = 0;
    rx_discard(c, w);
    /* Every conversion phase (UTF8_STRING, then the STRING fallback) gets its own deadline. */
    r->deadline = deadline_after(c->timeout_ms);
    /* Preserve P2.2's CurrentTime conversion behavior: an unrelated old key event
     * must not make a well-behaved owner refuse a later programmatic paste. */
    r->time = XCB_CURRENT_TIME;
    xcb_delete_property(C(p), r->win, c->prop[w]);
    xcb_convert_selection(C(p), r->win, c->sel[w], target, c->prop[w], r->time);
    xcb_flush(C(p));
}
static void redispatch(plat *p, int w, unsigned n) {
    for (unsigned i = 0; i < n; i++) if (plat_clip_request(p, w) != PLAT_OK) result(p, w, false, 1);
}
int plat_clip_request(plat *p, int which) {
    x11_clip *c = CL(p);
    if (!c || which < 0 || which >= NSEL) return PLAT_ERR_FAIL;
    if (c->local_requests[which] + c->local_ready[which] + c->rx[which].waiters + c->failed[which] >= MAX_WAITERS)
        return PLAT_ERR_FAIL;
    if (c->claiming[which] || c->clear_pending[which] || c->local_blob[which]) {      /* ownership is being (re)checked: hold the paste */
        if (c->local_requests[which] == MAX_WAITERS) return PLAT_ERR_FAIL;
        c->local_requests[which]++; return PLAT_OK;
    }
    if (c->confirmed[which] && c->own[which]) {
        /* Completion is queued at delivery time (x11_clip_poll), never here: each completion event must see
         * its own selection's data in the single plat_clip_data buffer. */
        if (c->local_ready[which] == MAX_WAITERS) return PLAT_ERR_FAIL;
        c->local_ready[which]++; c->local_at[which] = trace_now_ns() | 1u; return PLAT_OK;
    }
    clip_receive *r = &c->rx[which];
    if (r->waiters == MAX_WAITERS || r->draining) return PLAT_ERR_FAIL;
    r->waiters++;
    if (r->state) return PLAT_OK;      /* one conversion, separate completion events */
    /* A fresh X resource ID isolates this conversion from late Notify/Property events
     * for a timed-out predecessor. Only two such windows can exist at once. */
    r->win = xcb_generate_id(C(p));
    if (!r->win || r->win == UINT32_MAX) { r->win = 0; r->waiters--; return PLAT_ERR_FAIL; }
    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(C(p), 0, r->win, p->win, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                      XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &mask);
    r->deadline = deadline_after(c->timeout_ms);
    rx_convert(p, which, c->utf8); return PLAT_OK;
}
const uint8_t *plat_clip_data(const plat *p, size_t *len) {
    const x11_clip *c = CL(p); *len = c && c->got ? c->got->len : 0; return c && c->got ? c->got->data : NULL;
}
/* Complete a receive whose worker jobs have all reported back. */
static void rx_complete(plat *p, int w) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    bool ok = r->fin_ok && !r->wfail;
    if (ok && !r->sink.buf) {                                    /* an empty transfer never reached the worker */
        clip_blob *e = malloc(sizeof *e);
        if (e) { e->refs = 1; e->len = e->cap = 0; r->sink.buf = e; } else ok = false;
    }
    /* Keep the receive and its blob until all accepted waiters have room.
     * No later selection may replace got while these events are queued. */
    if (!event_room(p)) return;
    if (ok) set_got(c, r->sink.buf);
    else { free(r->sink.buf); r->sink.buf = NULL; c->mem -= r->cap; r->cap = 0; }
    while (r->waiters && event_room(p)) { result(p, w, ok, ok ? 0u : 1u); r->waiters--; }
    if (r->waiters) return;
    if (ok) blob_unref(c, r->sink.buf);
    xcb_window_t request_win = r->win;
    memset(r, 0, sizeof *r);
    xcb_delete_property(C(p), request_win, c->prop[w]);
    xcb_destroy_window(C(p), request_win); xcb_flush(C(p));
}
/* State 5: finished from the peer's side, waiting for the worker to close (shrink or free) the buffer. */
static void rx_advance(plat *p, int w) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    if (r->need_close && rx_close(c, w, r->close_discard)) r->need_close = false;
    if (!r->need_close && !r->infl && (!p->in || ((x11_input *)p->in)->qh == ((x11_input *)p->in)->qt)) rx_complete(p, w);
}
static void rx_finish(plat *p, int w, bool ok) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    if (r->state == 2) xcb_discard_reply(C(p), r->cookie.sequence);
    r->state = 5; r->deadline = 0; r->fin_ok = ok;
    if (!r->closed && (r->infl || r->sink.buf)) { r->need_close = true; r->close_discard = !ok; }
    rx_advance(p, w);
}
static void rx_get(plat *p, int w) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    /* Delete explicitly only after the entire property has been read. */
    r->cookie = xcb_get_property(C(p), 0, r->win, c->prop[w], XCB_GET_PROPERTY_TYPE_ANY, r->offset,
                                 (uint32_t)((c->max_chunk + 3u) / 4u));
    r->state = 2; xcb_flush(C(p));
}
/* Hand one property reply to the worker. The UI thread only decodes the header, plans and charges capacity, and
 * submits. On success the reply belongs to the job (*reply is cleared). */
static bool rx_append(x11_clip *c, int w, clip_receive *r, const x11_clip_property *v, xcb_get_property_reply_t **reply) {
    bool str = v->type == XCB_ATOM_STRING;
    size_t ub = str ? v->len * 2u : v->len;                      /* Latin-1 doubles at worst; the worker counts exactly */
    if (r->len + r->ub_infl + ub > DATA_MAX) {
        if (!str) return false;                                  /* exact for UTF-8; Latin-1 is enforced by the worker */
        ub = DATA_MAX - (r->len + r->ub_infl < DATA_MAX ? r->len + r->ub_infl : DATA_MAX);
    }
    unsigned slot = chunk_free_slot(c);
    if (slot == NCHUNK + NSEL || !pool_ready(c)) return false;
    size_t need = r->len + r->ub_infl + ub;
    if (need > r->cap || !r->cap) {
        size_t cap = r->cap ? r->cap : 4096;
        while (cap < need) cap *= 2;
        if (cap > DATA_MAX) cap = DATA_MAX;
        /* realloc may hold old and new at once: charge the full new capacity against the shared budget first. */
        if (!mem_fits(c, cap)) { cap = need > 4096 ? need : 4096; if (!mem_fits(c, cap)) return false; }
        c->mem += cap - r->cap; r->cap = cap;
    }
    clip_chunk *k = &c->wjob[slot];
    memset(k, 0, sizeof *k);
    k->reply = *reply; k->data = v->data; k->dlen = v->len; k->string = str; k->ub = ub; k->cap = r->cap;
    k->sink = &r->sink; k->w = (uint32_t)w;
    if (!submit(c, slot, r)) return false;
    *reply = NULL; r->ub_infl += ub;
    note(c, v->len);                                             /* the socket read that produced it still ran here */
    r->raw += v->len; return true;
}
static void rx_reject_incr(plat *p, int w) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    for (unsigned i = 0; i < r->waiters; i++) result(p, w, false, 1);
    r->waiters = 0; rx_discard(c, w);
    r->incr = r->draining = true; r->state = 3; r->offset = 0;
    r->deadline = r->drain_end = deadline_after(c->timeout_ms);
    xcb_delete_property(C(p), r->win, c->prop[w]); xcb_flush(C(p));
}
static void rx_reply(plat *p, int w, xcb_get_property_reply_t **replyp) {
    x11_clip *c = CL(p); clip_receive *r = &c->rx[w];
    x11_clip_property v;
    if (!reply_view(*replyp, &v)) { rx_finish(p, w, false); return; }
    if (r->draining) {
        /* Delete each chunk without retaining its contents. This is bounded by a total
         * drain deadline, even for a peer that sends endless malformed chunks. */
        xcb_delete_property(C(p), r->win, c->prop[w]); xcb_flush(C(p));
        if (!v.len && !v.after) rx_finish(p, w, false);
        else { r->state = 3; r->offset = 0; r->deadline = r->drain_end; }
        return;
    }
    int kind = x11_clip_decode_transfer(&v, c->utf8, c->textplain, c->incr, r->type, !r->incr && !r->offset);
    if (kind == 1) {
        /* The advertised size is a lower bound, never an allocation instruction. */
        memcpy(&r->lower, v.data, 4); r->raw = 0;
        r->incr = true; r->state = 3; r->deadline = deadline_after(c->timeout_ms);
        xcb_delete_property(C(p), r->win, c->prop[w]); xcb_flush(C(p)); return;
    }
    if (kind < 0 || !rx_append(c, w, r, &v, replyp)) {
        if (r->incr || v.type == c->incr) rx_reject_incr(p, w); else rx_finish(p, w, false);
        return;
    }
    r->type = v.type;
    if (v.after) {
        if (v.len / 4u > UINT32_MAX - r->offset) { rx_finish(p, w, false); return; }
        r->offset += (uint32_t)(v.len / 4u); rx_get(p, w); return;
    }
    if (!r->incr) { rx_finish(p, w, true); return; }
    /* The INCR integer is a lower bound on the bytes that will arrive: an earlier terminator is a truncation. */
    if (!v.len) { rx_finish(p, w, r->raw >= r->lower); return; }
    r->offset = 0; r->state = 3; r->deadline = deadline_after(c->timeout_ms);
    xcb_delete_property(C(p), r->win, c->prop[w]); xcb_flush(C(p));
}

static void notify(plat *p, const xcb_selection_request_event_t *rq, xcb_atom_t prop) {
    xcb_selection_notify_event_t n = { .response_type = XCB_SELECTION_NOTIFY, .time = rq->time,
        .requestor = rq->requestor, .selection = rq->selection, .target = rq->target, .property = prop };
    xcb_send_event(C(p), 0, rq->requestor, 0, (const char *)&n); xcb_flush(C(p));
}
/* Replies for the same request tuple (requestor, selection, target, time) must arrive in request order
 * (ICCCM): a failure or early finish waits behind every older job with that tuple. */
static bool same_tuple(const xcb_selection_request_event_t *a, const xcb_selection_request_event_t *b) {
    return a->requestor == b->requestor && a->selection == b->selection && a->target == b->target && a->time == b->time;
}
static bool tuple_blocked(const x11_clip *c, const clip_job *j) {
    for (unsigned k = 0; k < NJOB; k++) {
        const clip_job *o = &c->jobs[k];
        if (o != j && o->state && o->seq < j->seq && same_tuple(&o->rq, &j->rq)) return true;
    }
    return false;
}
static void release_held(plat *p) {
    x11_clip *c = CL(p);
    for (;;) {
        clip_job *best = NULL;
        for (unsigned k = 0; k < NJOB; k++) {
            clip_job *o = &c->jobs[k];
            if (o->state == 4 && !tuple_blocked(c, o) && (!best || o->seq < best->seq)) best = o;
        }
        if (!best) return;
        notify(p, &best->rq, best->held_prop); job_free(p, best);
    }
}
static void job_finish(plat *p, clip_job *j, xcb_atom_t prop) {
    if (tuple_blocked(CL(p), j)) { job_release(p, j); j->state = 4; j->held_prop = prop; j->deadline = 0; return; }
    notify(p, &j->rq, prop); job_free(p, j); release_held(p);
}
/* An immediate refusal also queues behind older same-tuple jobs that are still running. */
static void job_transfers(plat *p, clip_job *j, bool announced);
static void refuse(plat *p, const xcb_selection_request_event_t *rq) {
    x11_clip *c = CL(p);
    clip_job probe; memset(&probe, 0, sizeof probe); probe.rq = *rq; probe.seq = c->job_seq + 1u;
    if (tuple_blocked(c, &probe)) {
        for (unsigned i = 0; i < NJOB; i++) if (!c->jobs[i].state) {
            clip_job *j = &c->jobs[i]; j->rq = *rq; j->seq = ++c->job_seq; j->state = 4; j->held_prop = XCB_ATOM_NONE; return;
        }
        /* No refusal storage remains. Retire the older indistinguishable
         * requests in arrival order before notifying this refusal. Already
         * finished replies keep their checked result through release_held. */
        for (;;) {
            clip_job *oldest = NULL;
            for (unsigned i = 0; i < NJOB; i++) {
                clip_job *j = &c->jobs[i];
                if (j->state && same_tuple(&j->rq, rq) && (!oldest || j->seq < oldest->seq)) oldest = j;
            }
            if (!oldest) break;
            xcb_atom_t prop = oldest->state == 4 ? oldest->held_prop : XCB_ATOM_NONE;
            job_transfers(p, oldest, false);
            job_finish(p, oldest, prop);
        }
    }
    notify(p, rq, XCB_ATOM_NONE);
}
static int tx_new(plat *p, clip_job *j, xcb_atom_t target, xcb_atom_t prop) {
    x11_clip *c = CL(p);
    for (unsigned i = 0; i < NSERVE; i++)
        if (c->tx[i].blob && c->tx[i].win == j->rq.requestor && c->tx[i].prop == prop) return -1;
    for (unsigned i = 0; i < NSERVE; i++) if (!c->tx[i].blob) {
        if (j->rq.requestor != p->win) {
            uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
            xcb_change_window_attributes(C(p), j->rq.requestor, XCB_CW_EVENT_MASK, &mask);
        }
        clip_serve *s = &c->tx[i]; s->blob = j->blob; s->blob->refs++;
        s->job = j; s->win = j->rq.requestor; s->prop = prop; s->target = target;
        s->deadline = deadline_after(c->timeout_ms);
        return (int)i;
    }
    return -1;
}
static unsigned convert(plat *p, clip_job *j, xcb_atom_t target, xcb_atom_t prop, int *slot) {
    x11_clip *c = CL(p); *slot = -1;
    if (!prop || target == c->multiple) return 0;
    for (unsigned i = 0; i < NSERVE; i++)
        if (c->tx[i].blob && c->tx[i].win == j->rq.requestor && c->tx[i].prop == prop) return 0;
    xcb_void_cookie_t ck;
    if (target == c->targets) {
        xcb_atom_t t[] = { c->targets, c->timestamp, c->multiple, c->utf8, c->textplain, XCB_ATOM_STRING, c->save };
        uint32_t nt = j->rq.selection == c->sel[0] ? 7u : 6u;
        ck = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, prop, XCB_ATOM_ATOM, 32, nt, t);
    } else if (target == c->timestamp) {
        ck = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, prop, XCB_ATOM_INTEGER, 32, 1, &j->stamp);
    } else if (target == c->save && j->rq.selection == c->sel[0]) {
        ck = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, prop, c->null_type, 8, 0, NULL);
    } else if (target == c->utf8 || target == c->textplain || target == XCB_ATOM_STRING) {
        if (j->blob->len > c->chunk) {
            *slot = tx_new(p, j, target, prop);
            if (*slot < 0) return 0;
            /* STRING's byte count can shrink, so use zero as its valid lower bound. */
            uint32_t lower = target == XCB_ATOM_STRING ? 0 : (uint32_t)j->blob->len;
            ck = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, prop, c->incr, 32, 1, &lower);
        } else {
            const uint8_t *data = j->blob->data; size_t len = j->blob->len;
            if (target == XCB_ATOM_STRING) { len = x11_utf8_to_latin1(data, len, c->scratch); data = c->scratch; }
            note(c, len);
            ck = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, prop, target, 8, (uint32_t)len, data);
        }
    } else return 0;
    return ck.sequence;
}
/* Convert pairs while the slice budget lasts; the rest continues from x11_clip_poll (state 3). */
static void job_start_writes(plat *p, clip_job *j) {
    x11_clip *c = CL(p);
    for (; j->next < j->npairs; j->next++) {
        size_t i = j->next;
        xcb_atom_t target = j->pairs[2u * i], prop = j->pairs[2u * i + 1u];
        bool textual = target == c->utf8 || target == c->textplain || target == XCB_ATOM_STRING;
        if (textual && !slice_room(c, j->blob->len < c->chunk ? j->blob->len : 0)) break;
        bool conflict = j->multiple && prop == j->rq.property;
        for (size_t k = 0; k < i; k++) if (prop == j->pairs[2u * k + 1u] && prop) conflict = true;
        j->checks[i] = conflict ? 0 : convert(p, j, target, prop, &j->slots[i]);
        if (conflict) j->slots[i] = -1;
        if (!j->checks[i]) j->pairs[2u * i + 1u] = XCB_ATOM_NONE;
        j->nchecks = (unsigned)i + 1u;
    }
    j->deadline = deadline_after(c->timeout_ms);
    if (j->next < j->npairs) { j->state = 3; return; }
    j->state = 2; j->barrier = xcb_get_input_focus(C(p)).sequence; xcb_flush(C(p));
}
static void serve(plat *p, const xcb_selection_request_event_t *rq) {
    x11_clip *c = CL(p); int w = which_of(c, rq->selection);
    if (w < 0 || !c->own[w] || rq->owner != p->win || older(rq->time, c->own_time[w]) ||
        (rq->target == c->multiple && !rq->property)) { refuse(p, rq); return; }
    clip_job *j = NULL;
    for (unsigned i = 0; i < NJOB; i++) if (!c->jobs[i].state) { j = &c->jobs[i]; break; }
    if (!j) { refuse(p, rq); return; }
    j->rq = *rq; j->blob = c->own[w]; j->blob->refs++; j->stamp = c->own_time[w]; j->seq = ++c->job_seq; j->next = 0;
    j->deadline = deadline_after(c->timeout_ms); j->multiple = rq->target == c->multiple;
    if (j->multiple) {
        j->state = 1;
        j->cookie = xcb_get_property(C(p), 0, rq->requestor, rq->property, XCB_GET_PROPERTY_TYPE_ANY, 0, MAX_PAIRS * 2u).sequence;
        xcb_flush(C(p));
    } else {
        j->npairs = 1; j->pairs[0] = rq->target; j->pairs[1] = rq->property ? rq->property : rq->target;
        job_start_writes(p, j);
    }
}

static void clear_query(plat *p, int w) {
    x11_clip *c = CL(p);
    if (!c->clear_pending[w]) {
        c->clear_pending[w] = true;
        c->clear_cookie[w] = xcb_get_selection_owner(C(p), c->sel[w]).sequence;
        c->clear_deadline[w] = deadline_after(c->timeout_ms); xcb_flush(C(p));
    }
}
static bool clip_event(plat *p, const xcb_generic_event_t *e, plat_event *ev);
static uint64_t cpu_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static void time_note(x11_clip *c, uint64_t wall, uint64_t cpu) {
    if (wall > c->max_poll_ns) c->max_poll_ns = wall;
    if (cpu > c->max_poll_cpu_ns) c->max_poll_cpu_ns = cpu;
}
bool x11_clip_event(plat *p, const xcb_generic_event_t *e, plat_event *ev) {
    x11_clip *c = CL(p);
    if (!c) return false;
    uint64_t t0 = trace_now_ns(), u0 = cpu_ns();
    bool r = clip_event(p, e, ev);
    time_note(c, trace_now_ns() - t0, cpu_ns() - u0);
    return r;
}
static bool clip_event(plat *p, const xcb_generic_event_t *e, plat_event *ev) {
    x11_clip *c = CL(p);
    c->slice = 0;
    uint8_t t = e->response_type & 0x7f;
    if (t == XCB_SELECTION_REQUEST) { serve(p, (const xcb_selection_request_event_t *)e); return false; }
    if (t == XCB_SELECTION_CLEAR) {
        const xcb_selection_clear_event_t *x = (const xcb_selection_clear_event_t *)e;
        /* Only the server generates Clear for an ownership change; a SendEvent copy (bit 0x80) is a forgery. */
        if (e->response_type & 0x80) return false;
        int w = which_of(c, x->selection);
        if (w < 0 || x->owner != p->win || !c->own[w] || older(x->time, c->own_time[w])) return false;
        if (c->claiming[w]) { c->clear_deferred[w] = true; return false; }
        /* X accepts equal timestamps. A previous owner's Clear can share the new
         * claim's millisecond; confirm that we actually lost it before releasing data. */
        if (!x->time || !c->own_time[w] || c->uncertain[w] || x->time == c->own_time[w]) { clear_query(p, w); return false; }
        blob_unref(c, c->own[w]); c->own[w] = NULL; c->confirmed[w] = false;
        *ev = (plat_event){ .kind = PLAT_EV_CLIPBOARD, .clip_which = (uint8_t)w, .code = 2 };
        return true;
    }
    if (t == XCB_SELECTION_NOTIFY) {
        const xcb_selection_notify_event_t *x = (const xcb_selection_notify_event_t *)e;
        if (c->saving && x11_clip_notify_matches(x, p->win, c->manager, c->save, c->save_prop, XCB_CURRENT_TIME)) {
            c->save_done = true; return false;
        }
        int w = which_of(c, x->selection);
        if (w < 0) return false;
        clip_receive *r = &c->rx[w];
        if (r->state != 1 || !x11_clip_notify_matches(x, r->win, c->sel[w], r->target, c->prop[w], r->time)) return false;
        if (!x->property) {
            if (r->target == c->utf8) rx_convert(p, w, XCB_ATOM_STRING);
            else rx_finish(p, w, false);
        } else rx_get(p, w);
    }
    if (t == XCB_PROPERTY_NOTIFY) {
        const xcb_property_notify_event_t *x = (const xcb_property_notify_event_t *)e;
        for (int w = 0; w < NSEL; w++) {
            if (x->window == p->win && x->atom == c->claim_prop[w] && x->state == XCB_PROPERTY_NEW_VALUE &&
                c->claiming[w] == 1 && x->sequence == (uint16_t)c->stamp_cookie[w]) claim(p, w, x->time);
            clip_receive *r = &c->rx[w];
            if (x->window == r->win && x->atom == c->prop[w] && x->state == XCB_PROPERTY_NEW_VALUE && r->state == 3)
                rx_get(p, w);
        }
        if (x->state == XCB_PROPERTY_DELETE)
            for (unsigned i = 0; i < NSERVE; i++) if (c->tx[i].blob && c->tx[i].win == x->window && c->tx[i].prop == x->atom)
                c->tx[i].deleted = true;
    }
    return false;
}

/* Checked void requests become pollable after the following reply passes their sequence. */
static bool checked_ok(plat *p, unsigned seq) {
    if (!seq) return false;
    void *reply = NULL; xcb_generic_error_t *error = NULL;
    int done = xcb_poll_for_reply(C(p), seq, &reply, &error);
    bool ok = done && !error;
    free(reply); free(error); return ok;
}
static bool barrier_done(plat *p, unsigned seq, bool *ok) {
    void *reply = NULL; xcb_generic_error_t *error = NULL;
    if (!xcb_poll_for_reply(C(p), seq, &reply, &error)) return false;
    *ok = reply && !error; free(reply); free(error); return true;
}
/* Hand held local pastes on once ownership is settled: confirmed -> deliver from the shared blob in
 * x11_clip_poll; otherwise ask the real owner. */
static void flush_local(plat *p, int w) {
    x11_clip *c = CL(p);
    if (c->claiming[w] || c->clear_pending[w] || c->local_blob[w]) return;
    unsigned requests = c->local_requests[w]; c->local_requests[w] = 0;
    if (!requests) return;
    if (c->confirmed[w] && c->own[w]) {
        c->local_ready[w] += requests; c->local_at[w] = trace_now_ns() | 1u;
    } else redispatch(p, w, requests);
}
static void owner_finish(plat *p, int w, bool ok) {
    x11_clip *c = CL(p);
    c->claiming[w] = 0; c->owner_deadline[w] = 0; c->confirmed[w] = ok && c->own[w];
    /* Keep the bounded buffer on an uncertain failure: the claim may have succeeded,
     * or a query may have raced. Only SelectionClear or a replacement releases it. */
    c->uncertain[w] = !ok;
    if (!ok) result(p, w, false, 2);
    /* A Clear that arrived while claiming may be newer than a successful query: check again even on success,
     * and keep local pastes held until that answer is in. */
    if (c->clear_deferred[w]) {
        c->clear_deferred[w] = false;
        if (c->own[w]) clear_query(p, w);
    }
    flush_local(p, w);
}
/* Queue the completions of confirmed local pastes. Runs only when the input queue is empty, one selection per
 * call, so each batch of events sees its own selection in the single plat_clip_data buffer. */
static bool deliver_local(plat *p, int w) {
    x11_clip *c = CL(p);
    if (!c->local_ready[w]) return false;
    if (!c->local_blob[w]) {
        if (c->confirmed[w] && c->own[w] && !c->claiming[w] && !c->clear_pending[w]) {
            c->local_blob[w] = c->own[w]; c->local_blob[w]->refs++;
        } else {
            c->local_requests[w] += c->local_ready[w]; c->local_ready[w] = 0; c->local_at[w] = 0;
            flush_local(p, w); return true;
        }
    }
    if (!event_room(p)) return false;
    set_got(c, c->local_blob[w]);
    while (c->local_ready[w] && event_room(p)) { result(p, w, true, 0); c->local_ready[w]--; }
    if (!c->local_ready[w]) {
        c->local_at[w] = 0; blob_unref(c, c->local_blob[w]); c->local_blob[w] = NULL;
        flush_local(p, w);
    }
    return true;
}
static void tx_send(plat *p, clip_serve *s) {
    x11_clip *c = CL(p);
    if (s->zero) { tx_free(p, s); return; }
    size_t left = s->blob->len - (size_t)s->offset, n = left < c->chunk ? left : c->chunk;
    const uint8_t *data = s->blob->data + (size_t)s->offset;
    size_t consumed = n;
    if (s->target == XCB_ATOM_STRING) { n = latin_chunk(data, left, c->scratch, c->chunk, &consumed); data = c->scratch; }
    note(c, n);
    s->write = xcb_change_property_checked(C(p), XCB_PROP_MODE_APPEND, s->win, s->prop, s->target, 8, (uint32_t)n, data).sequence;
    s->barrier = xcb_get_input_focus(C(p)).sequence; s->checking = true;
    s->offset += consumed; s->zero = n == 0; s->deleted = false;
    s->deadline = deadline_after(c->timeout_ms); xcb_flush(C(p));
}
static void job_transfers(plat *p, clip_job *j, bool announced) {
    for (unsigned i = 0; i < NSERVE; i++) {
        clip_serve *t = &CL(p)->tx[i];
        if (t->blob && t->job == j) {
            if (announced) { t->job = NULL; t->deadline = deadline_after(CL(p)->timeout_ms); }
            else tx_free(p, t);
        }
    }
}
static void job_poll(plat *p, clip_job *j, uint64_t now, bool *progress) {
    x11_clip *c = CL(p);
    if (!j->state || j->state == 4) return;
    if (now >= j->deadline) {
        job_transfers(p, j, false);
        job_finish(p, j, XCB_ATOM_NONE); *progress = true; return;
    }
    if (j->state == 3) {
        if (!slice_room(c, j->blob->len < c->chunk ? j->blob->len : 0)) { *progress = true; return; }
        job_start_writes(p, j); *progress = true; return;
    }
    if (j->state == 1) {
        void *reply = NULL; xcb_generic_error_t *error = NULL;
        if (!xcb_poll_for_reply(C(p), j->cookie, &reply, &error)) return;
        *progress = true; j->state = 0;
        x11_clip_property v;
        bool ok = !error && reply_view(reply, &v) && x11_clip_decode_pairs(&v, c->atom_pair, j->pairs, MAX_PAIRS, &j->npairs);
        free(reply); free(error);
        if (!ok) { job_finish(p, j, XCB_ATOM_NONE); return; }
        job_start_writes(p, j); return;
    }
    bool ok = false;
    if (!barrier_done(p, j->barrier, &ok)) return;
    *progress = true;
    /* The extra final write in MULTIPLE is checked separately, before its notify. */
    if (j->nchecks > j->npairs) {
        ok = checked_ok(p, j->checks[j->npairs]) && ok;
        job_transfers(p, j, ok);
        job_finish(p, j, ok ? j->rq.property : XCB_ATOM_NONE); return;
    }
    for (size_t i = 0; i < j->npairs; i++) {
        bool written = checked_ok(p, j->checks[i]) && ok;
        if (!written) {
            j->pairs[2u * i + 1u] = XCB_ATOM_NONE;
            if (j->slots[i] >= 0) tx_free(p, &c->tx[(unsigned)j->slots[i]]);
        }
        j->checks[i] = 0;
    }
    if (!j->multiple) {
        job_transfers(p, j, j->pairs[1] != XCB_ATOM_NONE);
        job_finish(p, j, j->pairs[1]); return;
    }
    j->checks[j->npairs] = xcb_change_property_checked(C(p), XCB_PROP_MODE_REPLACE, j->rq.requestor, j->rq.property,
                                                      c->atom_pair, 32, (uint32_t)(j->npairs * 2u), j->pairs).sequence;
    j->nchecks++; j->barrier = xcb_get_input_focus(C(p)).sequence; xcb_flush(C(p));
}
static bool clip_poll(plat *p);
bool x11_clip_poll(plat *p) {
    x11_clip *c = CL(p); if (!c) return false;
    uint64_t t0 = trace_now_ns(), u0 = cpu_ns();
    bool r = clip_poll(p);
    time_note(c, trace_now_ns() - t0, cpu_ns() - u0);
    return r;
}
static bool clip_poll(plat *p) {
    x11_clip *c = CL(p);
    c->slice = 0;
    /* Deliver already queued completions before replacing the single plat_clip_data
     * buffer with another selection's result (both receives can finish together). */
    if (!c->saving && p->in && ((x11_input *)p->in)->qh != ((x11_input *)p->in)->qt) return false;
    bool progress = false; uint64_t now = trace_now_ns();
    if (c->jobs_out && c->pool && work_mailbox_drain(c->pool, on_work_msg, c)) progress = true;
    for (int w = 0; w < NSEL; w++) {
        while (c->failed[w] && event_room(p)) { c->failed[w]--; result(p, w, false, 1); progress = true; }
        while (c->lost[w] && event_room(p)) { c->lost[w]--; result(p, w, false, 2); progress = true; }
    }
    if (progress && p->in && ((x11_input *)p->in)->qh != ((x11_input *)p->in)->qt) return true;
    for (int w = 0; w < NSEL; w++)
        if (c->local_ready[w] && deliver_local(p, w)) return true;     /* its events are queued: drain them first */
    for (int w = 0; w < NSEL; w++) {
        if (c->claiming[w] && now >= c->owner_deadline[w]) {
            if (c->claiming[w] == 2) xcb_discard_reply(C(p), c->owner_cookie[w]);
            owner_finish(p, w, false); progress = true;
        }
        if (c->claiming[w] == 2) {
            void *reply = NULL; xcb_generic_error_t *error = NULL;
            if (xcb_poll_for_reply(C(p), c->owner_cookie[w], &reply, &error)) {
                xcb_get_selection_owner_reply_t *r = reply;
                bool ok = r && !error && r->owner == p->win;
                free(reply); free(error); progress = true;
                if (!ok && !c->retried[w] && c->own[w]) { c->retried[w] = true; claim(p, w, 0); }
                else owner_finish(p, w, ok);
            }
        }
        if (!c->saving && p->in && ((x11_input *)p->in)->qh != ((x11_input *)p->in)->qt) return true;
        if (c->clear_pending[w]) {
            void *reply = NULL; xcb_generic_error_t *error = NULL;
            if (now >= c->clear_deadline[w]) {
                xcb_discard_reply(C(p), c->clear_cookie[w]); c->clear_pending[w] = false;
                c->clear_deadline[w] = 0; progress = true; flush_local(p, w);
            } else if (xcb_poll_for_reply(C(p), c->clear_cookie[w], &reply, &error)) {
                xcb_get_selection_owner_reply_t *owner = reply;
                if (owner && !error && owner->owner != p->win && c->own[w]) {
                    blob_unref(c, c->own[w]); c->own[w] = NULL; c->confirmed[w] = false;
                    result(p, w, false, 2);
                }
                free(reply); free(error); c->clear_pending[w] = false; c->clear_deadline[w] = 0; progress = true;
                flush_local(p, w);
            }
        }
        clip_receive *r = &c->rx[w];
        if (r->state && r->state != 5 && now >= r->deadline) { rx_finish(p, w, false); progress = true; }
        if (r->wfail && r->state && r->state != 5 && !r->draining) {     /* the worker refused a chunk (limit / OOM) */
            r->wfail = false; progress = true;
            if (r->state == 2) { xcb_discard_reply(C(p), r->cookie.sequence); r->state = r->incr ? 3u : 1u; }
            if (r->incr) rx_reject_incr(p, w); else rx_finish(p, w, false);
        }
        if (r->need_close && r->state != 5 && rx_close(c, w, r->close_discard)) r->need_close = false;
        if (r->state == 5) {
            unsigned before = r->infl; rx_advance(p, w);
            if (!c->rx[w].state || before != r->infl) progress = true;
            if (!c->saving && p->in && ((x11_input *)p->in)->qh != ((x11_input *)p->in)->qt) return true;
            continue;
        }
        if (r->state == 2 && !slice_room(c, c->max_chunk)) progress = true;      /* budget spent: next slice */
        else if (r->state == 2 && chunk_free_slot(c) == NCHUNK + NSEL) { /* worker backlog: the 1 ms wake retries */ }
        else if (r->state == 2) {
            void *reply = NULL; xcb_generic_error_t *error = NULL;
            if (xcb_poll_for_reply(C(p), r->cookie.sequence, &reply, &error)) {
                r->state = r->incr ? 3u : 1u; progress = true;
                xcb_get_property_reply_t *rep = reply;
                if (error) rx_finish(p, w, false); else rx_reply(p, w, &rep);
                free(rep); free(error);
                if (!c->saving && p->in && ((x11_input *)p->in)->qh != ((x11_input *)p->in)->qt)
                    return true; /* deliver completed data before replacing it; partial reads stay fair */
            }
        }
    }
    for (unsigned i = 0; i < NJOB; i++) job_poll(p, &c->jobs[i], now, &progress);
    for (unsigned i = 0; i < NSERVE; i++) {
        clip_serve *s = &c->tx[i]; if (!s->blob || s->job) continue;
        if (now >= s->deadline) { tx_free(p, s); progress = true; continue; }
        if (s->checking) {
            bool ok;
            if (!barrier_done(p, s->barrier, &ok)) continue;
            ok = checked_ok(p, s->write) && ok; s->checking = false; progress = true;
            if (!ok) { tx_free(p, s); continue; }
        }
        if (s->deleted) { if (slice_room(c, c->chunk)) tx_send(p, s); progress = true; }
    }
    return progress;
}
void x11_clip_set_limits(plat *p, size_t chunk, uint32_t timeout_ms, uint32_t save_timeout_ms) {
    x11_clip *c = CL(p); if (!c) return;
    if (chunk) c->chunk = chunk > c->max_chunk ? c->max_chunk : chunk;
    if (timeout_ms) c->timeout_ms = timeout_ms;
    if (save_timeout_ms) c->save_ms = save_timeout_ms == UINT32_MAX ? 0 : save_timeout_ms;
}
size_t x11_clip_mem(const plat *p) { const x11_clip *c = CL(p); return c ? c->mem : 0; }
void x11_clip_set_budget(plat *p, size_t bytes) { x11_clip *c = CL(p); if (c && bytes) c->budget = bytes; }
size_t x11_clip_max_slice(plat *p, bool reset) {
    x11_clip *c = CL(p); if (!c) return 0;
    size_t m = c->max_slice; if (reset) c->max_slice = 0; return m;
}
size_t x11_clip_ui_bytes(plat *p, bool reset) {
    x11_clip *c = CL(p); if (!c) return 0;
    size_t m = c->ui_bytes; if (reset) c->ui_bytes = 0; return m;
}
uint64_t x11_clip_max_poll_ns(plat *p, bool reset, uint64_t *cpu_ns_out) {
    x11_clip *c = CL(p); if (!c) return 0;
    uint64_t m = c->max_poll_ns;
    if (cpu_ns_out) *cpu_ns_out = c->max_poll_cpu_ns;
    if (reset) c->max_poll_ns = c->max_poll_cpu_ns = 0;
    return m;
}
size_t x11_clip_busy(const plat *p) {
    const x11_clip *c = CL(p); if (!c) return 0;
    size_t n = 0;
    for (int w = 0; w < NSEL; w++) if (c->rx[w].state || c->local_ready[w] || c->failed[w] || c->lost[w]) n++;
    n += c->frees_out;
    for (unsigned i = 0; i < NSERVE; i++) if (c->tx[i].blob) n++;
    for (unsigned i = 0; i < NJOB; i++) if (c->jobs[i].state) n++;
    return n;
}
uint64_t x11_clip_deadline(const plat *p) {
    const x11_clip *c = CL(p); if (!c) return 0;
    uint64_t d = 0;
#define EARLIER(v) do { uint64_t x = (v); if (x && (!d || x < d)) d = x; } while (0)
    for (int w = 0; w < NSEL; w++) {
        if (c->failed[w] || c->lost[w]) EARLIER(trace_now_ns() | 1u);
        EARLIER(c->local_at[w]); EARLIER(c->owner_deadline[w]);
        EARLIER(c->clear_deadline[w]); EARLIER(c->rx[w].deadline);
    }
    if (c->jobs_out) EARLIER(trace_now_ns() + UINT64_C(1000000));      /* worker results are polled (no wake fd in x11.c) */
    for (unsigned i = 0; i < NSERVE; i++) EARLIER(c->tx[i].deadline);
    for (unsigned i = 0; i < NJOB; i++) EARLIER(c->jobs[i].deadline);
#undef EARLIER
    return d;
}

/* Shutdown is the only bounded wait. Keep serving while the manager fetches data;
 * do not call application callbacks, or synchronously wait for any X reply. */
void x11_clip_save_on_exit(plat *p) {
    x11_clip *c = CL(p);
    if (!c || !c->save_ms || !c->own[0]) return;
    uint64_t end = deadline_after(c->save_ms);
    unsigned owner = 0, actual_owner = 0;
    bool probing = true, actual_probing = true, started = false;
    c->saving = true; c->save_done = false; xcb_flush(C(p));
    while (!c->save_done && trace_now_ns() < end && !xcb_connection_has_error(C(p))) {
        xcb_generic_event_t *e;
        while (trace_now_ns() < end && ((e = xcb_poll_for_queued_event(C(p))) || (e = xcb_poll_for_event(C(p))))) {
            plat_event ev; x11_clip_event(p, e, &ev); free(e);
        }
        if (!started && !c->claiming[0]) {
            actual_owner = xcb_get_selection_owner(C(p), c->sel[0]).sequence;
            owner = xcb_get_selection_owner(C(p), c->manager).sequence;
            started = true; xcb_flush(C(p));
        }
        if (started && actual_probing) {
            void *reply = NULL; xcb_generic_error_t *error = NULL;
            if (xcb_poll_for_reply(C(p), actual_owner, &reply, &error)) {
                xcb_get_selection_owner_reply_t *r = reply;
                bool ours = r && !error && r->owner == p->win;
                free(reply); free(error); actual_probing = false;
                if (!ours) break;
            }
        }
        if (started && !actual_probing && probing) {
            void *reply = NULL; xcb_generic_error_t *error = NULL;
            if (xcb_poll_for_reply(C(p), owner, &reply, &error)) {
                xcb_get_selection_owner_reply_t *r = reply;
                bool found = r && !error && r->owner;
                free(reply); free(error); probing = false;
                if (!found) break;
                xcb_atom_t t[] = { c->utf8, c->textplain, XCB_ATOM_STRING };
                xcb_change_property(C(p), XCB_PROP_MODE_REPLACE, p->win, c->save_prop, XCB_ATOM_ATOM, 32, 3, t);
                xcb_convert_selection(C(p), p->win, c->manager, c->save, c->save_prop, XCB_CURRENT_TIME); xcb_flush(C(p));
            }
        }
        bool progress = x11_clip_poll(p);
        if (c->save_done) break;
        /* Reply polls can queue events; draining again prevents a sleep with buffered work. */
        e = xcb_poll_for_queued_event(C(p));
        if (e) { plat_event ev; x11_clip_event(p, e, &ev); free(e); continue; }
        if (progress) continue;
        uint64_t now = trace_now_ns(), d = x11_clip_deadline(p);
        uint64_t wake = d && d < end ? d : end;
        uint64_t wait_ms = wake <= now ? 0 : (wake - now + UINT64_C(999999)) / UINT64_C(1000000);
        int ms = wait_ms > INT_MAX ? INT_MAX : (int)wait_ms;
        struct pollfd fd = { xcb_get_file_descriptor(C(p)), POLLIN, 0 };
        (void)poll(&fd, 1, ms);
    }
    if (started && probing) xcb_discard_reply(C(p), owner);
    if (started && actual_probing) xcb_discard_reply(C(p), actual_owner);
    c->saving = false;
}
