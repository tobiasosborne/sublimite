/* libFuzzer: random buffer + random edits + build/cancel + queries vs a naive model. */
#include "lineidx/lineidx.h"
#include "base/base.h"
#include "trace/trace.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FAIL() __builtin_trap()
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
    if (size < 4) return 0;
    uint64_t n = ((uint64_t)data[0] << 8 | data[1]) * 9u % MAXN;
    uint32_t density = data[2] % 64u + 1u;
    uint8_t *b = malloc(MAXN * 2);
    uint32_t s = 2463534242u ^ data[3];
    for (uint64_t k = 0; k < n; k++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        b[k] = (s % density == 0) ? '\n' : (s & 0x100) ? (uint8_t)(s >> 24) : 'a';
    }
    lineidx *x = lineidx_create(n);
    if (!x) FAIL();
    size_t i = 4;
    while (i < size) {
        uint8_t op = data[i++] % 6;
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
            flat *snap = malloc(sizeof *snap);
            uint8_t *copy = malloc(n ? n : 1);
            memcpy(copy, b, n);
            snap->b = copy; snap->n = n;
            lineidx_src ss = { snap, n, flat_span, free_snap };
            if (lineidx_build_start(x, &pool, &ss) != 0) { free_snap(snap); }
            for (unsigned k = 0; k < polls % 8u; k++) lineidx_poll(x);
            if (how & 1) lineidx_build_cancel(x);
            else if (how & 2) {                         /* wait until done */
                for (int k = 0; k < 2000000 && !lineidx_complete(x); k++) { lineidx_poll(x); nanosleep(&(struct timespec){0, 20000}, NULL); }
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
            if (!q.exact && q.value && b[q.value - 1] != '\n' && q.value != n) { /* estimate must sit on a boundary if one lay nearby */ }
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
            lineidx_result q = lineidx_seek_line(x, &cs, ln, budget);
            if (q.value > n) FAIL();
            if (q.exact && q.value != m_l2b(b, n, ln)) FAIL();
        } else {                                        /* poll */
            lineidx_poll(x);
        }
    }
done:
    lineidx_destroy(x);
    free(b);
    return 0;
}
