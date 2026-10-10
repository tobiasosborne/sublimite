/* libFuzzer: random buffer + random edits + build/cancel + queries vs a naive model. */
#include "lineidx/lineidx.h"
#include "base/base.h"
#include "trace/trace.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FAIL() do { fprintf(stderr, "lineidx_fuzz:%d: failure\n", __LINE__); __builtin_trap(); } while (0)
#define NEED(k) do { if (i + (k) > size) goto done; } while (0)
#define MAXN (600u * 1024u)

typedef struct { const uint8_t *b; uint64_t n; } flat;
static size_t flat_span(void *ctx, uint64_t off, const uint8_t **p)
{
    const flat *f = ctx;
    if (off >= f->n) return 0;
    uint64_t r = f->n - off;
    if (r > 20000) r = 20000;
    *p = f->b + off;
    return (size_t)r;
}
static void free_snap(void *ctx) { flat *f = ctx; free((void *)f->b); free(f); }

static work_pool pool;
static void discard_message(const work_msg *m, void *ctx) { (void)m; (void)ctx; }
static void drain(void) { (void)work_mailbox_drain(&pool, discard_message, NULL); }
static void wait_for(lineidx *x, bool complete)
{
    struct timespec begin, now;
    if (clock_gettime(CLOCK_MONOTONIC, &begin) != 0) FAIL();
    for (;;) {
        lineidx_poll(x);
        drain();
        if (complete ? lineidx_complete(x) : !lineidx_building(x)) return;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) FAIL();
        if (now.tv_sec - begin.tv_sec >= 10) FAIL();
        nanosleep(&(struct timespec){0, 20000}, NULL);
    }
}
int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    trace_init();
    if (work_pool_init(&pool, 1, 0) != 0) FAIL();
    return 0;
}

static uint64_t m_lines(const uint8_t *b, uint64_t n) { uint64_t c = 1; for (uint64_t i = 0; i < n; i++) c += b[i] == '\n'; return c; }
static uint64_t m_l2b(const uint8_t *b, uint64_t n, uint64_t line)
{
    if (line == 0) return 0;
    uint64_t s = 0;
    for (uint64_t i = 0; i < n; i++) if (b[i] == '\n' && ++s == line) return i + 1;
    return n;
}
static uint64_t m_b2l(const uint8_t *b, uint64_t n, uint64_t off)
{
    uint64_t c = 0;
    if (off > n) off = n;
    for (uint64_t i = 0; i < off; i++) c += b[i] == '\n';
    return c;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    drain();
    if (size < 4) return 0;
    uint64_t n = ((uint64_t)data[0] << 8 | data[1]) * 9u % MAXN;
    uint32_t density = data[2] % 64u + 1u;
    uint8_t *b = malloc(MAXN * 2);
    uint32_t s = 2463534242u ^ data[3];
    for (uint64_t k = 0; k < n; k++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        b[k] = (s % density == 0) ? '\n' : (s & 0x100) ? (uint8_t)(s >> 24) : 'a';
    }
    /* Each edit consumes eight input bytes and adds at most two chunks.
     * Reserve on the allocating path for this input's entire edit sequence. */
    lineidx *x = lineidx_create_reserved(n, size);
    if (!x) FAIL();
    size_t i = 4;
    while (i < size) {
        drain();
        uint8_t op = data[i++] % 7;
        flat cur = { b, n };
        lineidx_src cs = { &cur, n, flat_span, NULL };
        if (op == 0) {                                  /* edit */
            NEED(7);
            uint64_t off = ((uint64_t)data[i] << 8 | data[i + 1]) * 13u % (n + 1);
            uint64_t del = ((uint64_t)data[i + 2] << 8 | data[i + 3]) * (data[i + 4] & 1 ? 3u : 1u);
            uint64_t il = ((uint64_t)data[i + 5] << 8) % 70000u + data[i + 6];
            i += 7;
            if (del > n - off) del = n - off;
            if (n - del + il > MAXN) il = 0;
            uint8_t *nb = malloc(MAXN * 2);
            memcpy(nb, b, off);
            for (uint64_t k = 0; k < il; k++) nb[off + k] = (k % 17 == 0) ? '\n' : (k % 29 == 0) ? 0xC3 : 'i';
            memcpy(nb + off + il, b + off + del, n - off - del);
            free(b); b = nb;
            if (lineidx_edit(x, off, del, il) != 0) FAIL();
            n = n - del + il;
            if (lineidx_len(x) != n) FAIL();
        } else if (op == 1) {
            lineidx_refresh(x, &cs);
        } else if (op == 2) {                           /* build, then poll / cancel / wait */
            NEED(2);
            uint8_t how = data[i++], polls = data[i++];
            lineidx_build_cancel(x);
            wait_for(x, false);                         /* one source lease at a time */
            flat *snap = malloc(sizeof *snap);
            uint8_t *copy = malloc(n ? n : 1);
            memcpy(copy, b, n);
            snap->b = copy; snap->n = n;
            lineidx_src ss = { snap, n, flat_span, free_snap };
            if (lineidx_build_start_owned(x, &pool, &ss, sizeof *snap + (size_t)(n ? n : 1)) != 0) { free_snap(snap); FAIL(); }
            for (unsigned k = 0; k < polls % 8u; k++) lineidx_poll(x);
            if (how & 1) lineidx_build_cancel(x);
            else if (how & 2) {                         /* wait until done */
                wait_for(x, true);
                if (!lineidx_complete(x)) FAIL();
            }
        } else if (op == 3) {                           /* queries vs naive */
            NEED(4);
            uint64_t lc = m_lines(b, n);
            uint64_t ln = ((uint64_t)data[i] << 8 | data[i + 1]) % (lc + 3);
            uint64_t off = ((uint64_t)data[i + 2] << 8 | data[i + 3]) * 9u % (n + 3);
            i += 4;
            lineidx_poll(x);
            lineidx_result q = lineidx_line_to_byte(x, &cs, ln);
            if (q.value > n) FAIL();
            if (q.exact && q.value != m_l2b(b, n, ln)) FAIL();
            if (!q.exact && q.value && b[q.value - 1] != '\n') FAIL();
            q = lineidx_byte_to_line(x, &cs, off);
            if (q.exact && q.value != m_b2l(b, n, off)) FAIL();
            lineidx_result c = lineidx_line_count(x);
            if (c.exact && c.value != lc) FAIL();
            if (lineidx_complete(x) && (!c.exact || !q.exact)) FAIL();
        } else if (op == 4) {                           /* seek with budget */
            NEED(4);
            uint64_t lc = m_lines(b, n);
            uint64_t ln = ((uint64_t)data[i] << 8 | data[i + 1]) % (lc + 3);
            uint64_t budget = (uint64_t)data[i + 2] * data[i + 3] * 600u;
            i += 4;
            bool no_prefix = lineidx_built_prefix(x) == 0;
            uint64_t bytes = lineidx_scanned_bytes(x);
            lineidx_result q = lineidx_seek_line(x, &cs, ln, budget);
            if (no_prefix && lineidx_scanned_bytes(x) - bytes > budget) FAIL();
            if (q.value > n) FAIL();
            if (q.exact && q.value != m_l2b(b, n, ln)) FAIL();
            if (!q.exact && q.value && b[q.value - 1] != '\n') FAIL();
        } else if (op == 6) {                            /* worker seek / cancel */
            NEED(3);
            uint64_t lc = m_lines(b, n);
            uint64_t target = ((uint64_t)data[i] << 8 | data[i + 1]) % (lc + 3u);
            uint8_t how = data[i + 2]; i += 3;
            lineidx_build_cancel(x); wait_for(x, false);
            flat *snap = malloc(sizeof *snap);
            uint8_t *copy = malloc(n ? n : 1u);
            memcpy(copy, b, n); snap->b = copy; snap->n = n;
            lineidx_src ss = {snap, n, flat_span, free_snap};
            if (lineidx_seek_start_owned(x, &pool, &ss, target, sizeof *snap + (size_t)(n ? n : 1u)) != 0) {
                free_snap(snap); FAIL();
            }
            if (how & 1u) {
                lineidx_build_cancel(x);
                if (lineidx_seek_result(x, NULL)) FAIL();
            } else {
                struct timespec begin, now;
                if (clock_gettime(CLOCK_MONOTONIC, &begin) != 0) FAIL();
                lineidx_result q;
                while (!lineidx_seek_result(x, &q)) {
                    drain();
                    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec - begin.tv_sec >= 10) FAIL();
                    nanosleep(&(struct timespec){0, 20000}, NULL);
                }
                if (!q.exact || q.value != m_l2b(b, n, target)) FAIL();
            }
        } else {                                        /* poll */
            lineidx_poll(x);
        }
    }
done:
    lineidx_destroy(x);
    drain();
    free(b);
    return 0;
}
