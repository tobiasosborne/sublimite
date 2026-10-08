/* bench/piece_bench.c - buffer-kernel bench matrix (bead P1.4a, edit-4w1.16).
 *
 * FROZEN referee for the P1.4 competition: every variant implementing
 * src/piece/piece.h is measured by this file unchanged. Spec and gate
 * citations: docs/decisions/P1.4a.md. Machine-readable output, one per cell:
 *   BENCH row=<row> col=<col> p50=<v> p99=<v> unit=<ns|us|ms|B|MB|GB/s|count> n=<samples>
 *         gate=<expr|none> status=<PASS|MISS|TRACK|SKIP|TIMEOUT>
 * Each row runs in a forked child (isolation, hard backstop for a hung stub).
 * Flags: --quick (1/64 sizes and counts) --huge --rows=a,b --cap=SECS
 *        --corpus=DIR --selftest
 */
#include "piece/piece.h"
#include "harness.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* gates, formatting                                                   */
/* ------------------------------------------------------------------ */

typedef struct { double p50, p99; } gate_t; /* display unit; < 0 = no limit */

static const gate_t NOGATE = { -1.0, -1.0 };

static int gate_active(const gate_t *g) { return g->p50 >= 0.0 || g->p99 >= 0.0; }

static const char *gate_status(const gate_t *g, double p50, double p99, size_t n, int timed_out)
{
    if (n == 0 && !timed_out)
        return "SKIP";
    if (timed_out)
        return "TIMEOUT";
    if (!gate_active(g))
        return "TRACK";
    if (g->p50 >= 0.0 && p50 > g->p50)
        return "MISS";
    if (g->p99 >= 0.0 && p99 > g->p99)
        return "MISS";
    return "PASS";
}

static void gate_expr(char *b, size_t n, const gate_t *g, const char *unit)
{
    int w = 0;
    b[0] = '\0';
    if (!gate_active(g)) {
        snprintf(b, n, "none");
        return;
    }
    {
        const char *f50 = strcmp(unit, "B") == 0 ? "p50<=%.0f%s" : "p50<=%g%s";
        const char *f99 = strcmp(unit, "B") == 0 ? "%sp99<=%.0f%s" : "%sp99<=%g%s";
        if (g->p50 >= 0.0)
            w = snprintf(b, n, f50, g->p50, unit);
        if (g->p99 >= 0.0 && (size_t)w < n)
            snprintf(b + w, n - (size_t)w, f99, w ? "," : "", g->p99, unit);
    }
}

static int fmt_line(char *b, size_t n, const char *row, const char *col, double p50, double p99,
                    const char *unit, size_t ns, const char *gate, const char *st)
{
    const char *f = (strcmp(unit, "B") == 0 || strcmp(unit, "count") == 0) ? "%.0f" : "%.3f";
    char a[64], c[64];
    snprintf(a, sizeof a, f, p50);
    snprintf(c, sizeof c, f, p99);
    return snprintf(b, n, "BENCH row=%s col=%s p50=%s p99=%s unit=%s n=%zu gate=%s status=%s\n",
                    row, col, a, c, unit, ns, gate, st);
}

static double conv(double raw_ns, const char *unit)
{
    if (strcmp(unit, "us") == 0) return raw_ns / 1e3;
    if (strcmp(unit, "ms") == 0) return raw_ns / 1e6;
    return raw_ns; /* ns, B, ... */
}

typedef struct {
    char row[24], col[24], unit[8], status[10], gate[80];
    double p50, p99;
    size_t n;
} cellrec;

#define MAXCELLS 64
static cellrec g_cells[MAXCELLS];
static size_t g_ncells;
static int g_fail; /* a gated cell missed or timed out */

static void emit(const char *row, const char *col, double p50, double p99, const char *unit,
                 size_t n, const gate_t *g, int timed_out)
{
    char ge[80], line[400];
    const char *st = gate_status(g, p50, p99, n, timed_out);
    gate_expr(ge, sizeof ge, g, unit);
    fmt_line(line, sizeof line, row, col, p50, p99, unit, n, ge, st);
    fputs(line, stdout);
    if (strcmp(st, "MISS") == 0 || (strcmp(st, "TIMEOUT") == 0 && gate_active(g)))
        g_fail = 1;
    if (g_ncells < MAXCELLS) {
        cellrec *c = &g_cells[g_ncells++];
        snprintf(c->row, sizeof c->row, "%s", row);
        snprintf(c->col, sizeof c->col, "%s", col);
        snprintf(c->unit, sizeof c->unit, "%s", unit);
        snprintf(c->status, sizeof c->status, "%s", st);
        snprintf(c->gate, sizeof c->gate, "%s", ge);
        c->p50 = p50; c->p99 = p99; c->n = n;
    }
}

/* samples are ns; unit conversion here. */
static void emit_samples(const char *row, const char *col, bench_samples *s, const char *unit,
                         const gate_t *g, int timed_out)
{
    double p50 = conv((double)bench_p50(s), unit), p99 = conv((double)bench_p99(s), unit);
    emit(row, col, p50, p99, unit, s->n, g, timed_out);
}

static void emit_value(const char *row, const char *col, double raw, const char *unit,
                       const gate_t *g, int timed_out)
{
    double v = conv(raw, unit);
    emit(row, col, v, v, unit, 1, g, timed_out);
}

static void emit_skip(const char *row, const char *col)
{
    emit(row, col, 0, 0, "ns", 0, &NOGATE, 0);
}

static void human_table(const char *row, const char *context)
{
    size_t i;
    printf("\n== %s ==\n%-22s %12s %12s %-5s %7s  %-24s %s\n", row, "col", "p50", "p99", "unit",
           "n", "gate", "status");
    for (i = 0; i < g_ncells; i++) {
        const cellrec *c = &g_cells[i];
        printf("%-22s %12.3f %12.3f %-5s %7zu  %-24s %s\n", c->col, c->p50, c->p99, c->unit,
               c->n, c->gate, c->status);
    }
    if (context)
        printf("%s\n", context);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* selftest                                                            */
/* ------------------------------------------------------------------ */

static int st_fails;
#define CHECK(c) do { if (!(c)) { printf("selftest FAIL: %s (line %d)\n", #c, __LINE__); st_fails++; } } while (0)

static int write_file(const char *path, const char *s)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(s, f);
    fclose(f);
    return 0;
}

static int fabs_close(double a, double b) { double d = a - b; return d < 1e-9 && d > -1e-9; }

static int selftest(void)
{
    char b[512], tmpl[3][40];
    gate_t g = { -1, 50 }, g2 = { 63, 84 };
    FILE *p;
    int i;
    bench_samples s;
    uint64_t buf[5] = { 10, 20, 30, 40, 100000 };

    fmt_line(b, sizeof b, "code_1m", "insert", 1.5, 20.25, "us", 1000, "p99<=50us", "PASS");
    CHECK(strcmp(b, "BENCH row=code_1m col=insert p50=1.500 p99=20.250 unit=us n=1000 "
                    "gate=p99<=50us status=PASS\n") == 0);
    fmt_line(b, sizeof b, "r", "peak_mem", 4096.0, 4096.0, "B", 1, "none", "TRACK");
    CHECK(strstr(b, "p50=4096 p99=4096 unit=B") != NULL);
    gate_expr(b, sizeof b, &g, "us");
    CHECK(strcmp(b, "p99<=50us") == 0);
    gate_expr(b, sizeof b, &g2, "ms");
    CHECK(strcmp(b, "p50<=63ms,p99<=84ms") == 0);
    gate_expr(b, sizeof b, &NOGATE, "ms");
    CHECK(strcmp(b, "none") == 0);
    CHECK(strcmp(gate_status(&g, 1, 49.9, 10, 0), "PASS") == 0);
    CHECK(strcmp(gate_status(&g, 1, 50.1, 10, 0), "MISS") == 0);
    CHECK(strcmp(gate_status(&g2, 64, 1, 10, 0), "MISS") == 0);
    CHECK(strcmp(gate_status(&g2, 62, 83, 10, 0), "PASS") == 0);
    CHECK(strcmp(gate_status(&g, 1, 1, 10, 1), "TIMEOUT") == 0);
    CHECK(strcmp(gate_status(&NOGATE, 1, 1, 10, 0), "TRACK") == 0);
    CHECK(strcmp(gate_status(&g, 0, 0, 0, 0), "SKIP") == 0);
    CHECK(fabs_close(conv(1500.0, "us"), 1.5));
    bench_samples_init(&s, buf, 5);
    s.n = 5;
    CHECK(bench_p50(&s) == 30 && bench_p99(&s) == 100000);

    for (i = 0; i < 3; i++) {
        snprintf(tmpl[i], sizeof tmpl[i], "/tmp/piece_bench_st%d_%d.log", (int)getpid(), i);
    }
    write_file(tmpl[0], "noise\nBENCH row=build col=text_size p50=1000 p99=1000 unit=B n=1 gate=none status=TRACK\n"
                        "BENCH row=r col=insert p50=1 p99=10 unit=us n=9 gate=p99<=50us status=PASS\n"
                        "BENCH row=r col=x p50=1 p99=30 unit=ms n=9 gate=none status=TRACK\n");
    write_file(tmpl[1], "BENCH row=build col=text_size p50=1200 p99=1200 unit=B n=1 gate=none status=TRACK\n"
                        "BENCH row=r col=insert p50=1 p99=20 unit=us n=9 gate=p99<=50us status=PASS\n"
                        "BENCH row=r col=x p50=1 p99=40 unit=ms n=9 gate=none status=MISS\n");
    write_file(tmpl[2], "BENCH row=build col=text_size p50=2000 p99=2000 unit=B n=1 gate=none status=TRACK\n"
                        "BENCH row=r col=insert p50=1 p99=5 unit=us n=9 gate=p99<=50us status=PASS\n"
                        "BENCH row=r col=x p50=1 p99=50 unit=ms n=9 gate=none status=TRACK\n");
    {
        char cmd[400], out[4096];
        size_t got = 0;
        snprintf(cmd, sizeof cmd, "python3 -I tools/pareto.py A=%s B=%s C=%s 2>&1", tmpl[0], tmpl[1], tmpl[2]);
        p = popen(cmd, "r");
        out[0] = '\0';
        if (p) {
            got = fread(out, 1, sizeof out - 1, p);
            out[got] = '\0';
            pclose(p);
        }
        CHECK(strstr(out, "Non-dominated: A, C") != NULL);
        CHECK(strstr(out, "Dominated: B (by A)") != NULL);
        CHECK(strstr(out, "**MISS**") != NULL);
        CHECK(strstr(out, "C: wins 1 [r/insert]") != NULL);
    }
    for (i = 0; i < 3; i++)
        unlink(tmpl[i]);
    printf(st_fails ? "selftest: %d failure(s)\n" : "selftest: ok\n", st_fails);
    return st_fails != 0;
}

/* ------------------------------------------------------------------ */
/* options, PRNG, counting allocator, corpus                           */
/* ------------------------------------------------------------------ */

static int o_quick, o_huge;
static double o_cap = 60.0;
static const char *o_corpus = "/tmp/edit-corpus";

/* scale a count by 1/64 under --quick (floor at 4 so percentiles exist) */
static size_t sc(size_t n)
{
    if (!o_quick) return n;
    n /= 64;
    return n < 4 ? 4 : n;
}

static uint64_t g_rng;
static void seed(uint64_t s) { g_rng = s ? s : 0x9E3779B97F4A7C15ull; }
static uint64_t rnd(void) /* xorshift64* */
{
    g_rng ^= g_rng >> 12; g_rng ^= g_rng << 25; g_rng ^= g_rng >> 27;
    return g_rng * 0x2545F4914F6CDD1Dull;
}
static uint64_t rnd_range(uint64_t n) { return n ? (rnd() >> 11) % n : 0; }

/* printable ASCII, ~1 newline per 60 bytes */
static void gen_text(uint8_t *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        uint64_t r = rnd();
        b[i] = (r >> 20) % 60 == 0 ? (uint8_t)'\n' : (uint8_t)(32 + (r >> 40) % 95);
    }
}

static size_t g_cur, g_peak;
static void *ca_alloc(void *ctx, size_t sz)
{
    size_t c, pk;
    void *p;
    (void)ctx;
    if (sz == 0) sz = 16;
    p = aligned_alloc(16, (sz + 15) & ~(size_t)15);
    if (!p) return NULL;
    c = __atomic_add_fetch(&g_cur, sz, __ATOMIC_RELAXED);
    pk = __atomic_load_n(&g_peak, __ATOMIC_RELAXED);
    while (c > pk && !__atomic_compare_exchange_n(&g_peak, &pk, c, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
        ;
    return p;
}
static void ca_free(void *ctx, void *p, size_t sz)
{
    (void)ctx;
    if (sz == 0) sz = 16;
    if (p) __atomic_sub_fetch(&g_cur, sz, __ATOMIC_RELAXED);
    free(p);
}
static const piece_allocator g_alloc = { NULL, ca_alloc, ca_free };
static void reset_peak(void) { g_peak = __atomic_load_n(&g_cur, __ATOMIC_RELAXED); }

static unsigned g_acq, g_rel;
static void map_acq(void *c) { (void)c; __atomic_add_fetch(&g_acq, 1, __ATOMIC_RELAXED); }
static void map_rel(void *c) { (void)c; __atomic_add_fetch(&g_rel, 1, __ATOMIC_RELAXED); }
static const piece_map_hooks g_hooks = { NULL, map_acq, map_rel };

typedef struct {
    int mapped;
    const uint8_t *p;
    size_t len;
} source;

static int corpus_path(char *b, size_t n, const char *name)
{
    return snprintf(b, n, "%s/%s", o_corpus, name);
}

/* Loads/maps a corpus file; returns 0, or -1 when missing (row prints SKIP). */
static int source_open(source *s, const char *name, int mapped)
{
    char path[512];
    struct stat st;
    int fd;
    void *m;
    corpus_path(path, sizeof path, name);
    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) { close(fd); return -1; }
    s->mapped = mapped;
    s->len = (size_t)st.st_size;
    if (mapped) {
        /* warm the page cache with a sequential read (G7j/G5 are "warm"); mapping
         * page faults are NOT pre-populated and stay inside the measured cells. */
        static uint8_t wb[1 << 20];
        off_t o = 0;
        while (o < st.st_size) {
            ssize_t r = pread(fd, wb, sizeof wb, o);
            if (r <= 0) break;
            o += r;
        }
        m = mmap(NULL, s->len, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (m == MAP_FAILED) return -1;
    } else {
        uint8_t *b = malloc(s->len);
        size_t got = 0;
        if (!b) { close(fd); return -1; }
        while (got < s->len) {
            ssize_t r = pread(fd, b + got, s->len - got, (off_t)got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        close(fd);
        if (got != s->len) { free(b); return -1; }
        m = b;
    }
    s->p = m;
    return 0;
}

/* ------------------------------------------------------------------ */
/* world: one tree plus the bookkeeping the gates need                 */
/* ------------------------------------------------------------------ */

typedef struct {
    piece_tree *t;
    uint64_t typed, deleted;
    uint64_t expect_len;
} world;

static uint64_t g_deadline_ns;
static void cell_begin(void) { g_deadline_ns = bench_now_ns() + (uint64_t)(o_cap * 1e9); }
static int over(void) { return bench_now_ns() > g_deadline_ns; }

static void die(const char *msg)
{
    fprintf(stderr, "piece_bench: FATAL %s\n", msg);
    fflush(stdout);
    _exit(2);
}

static void check_rc(int rc, const char *what)
{
    if (rc != 0) {
        char m[96];
        snprintf(m, sizeof m, "%s returned %d", what, rc);
        die(m);
    }
}

/* open: create + init (init timed into *open_ns). Returns tree. */
static piece_tree *tree_open(const source *s, uint64_t *open_ns, uint64_t *t_start)
{
    piece_tree *t = piece_create(&g_alloc);
    uint64_t t0;
    int rc;
    if (!t) die("piece_create");
    t0 = bench_now_ns();
    rc = s->mapped ? piece_init_mapped(t, s->p, s->len, &g_hooks) : piece_init_copy(t, s->p, s->len);
    if (open_ns) *open_ns = bench_now_ns() - t0;
    if (t_start) *t_start = t0;
    check_rc(rc, "piece_init");
    return t;
}

static void world_open(world *w, const source *s, uint64_t *open_ns)
{
    memset(w, 0, sizeof *w);
    w->t = tree_open(s, open_ns, NULL);
    w->expect_len = s->len;
    if (piece_len(w->t) != s->len) die("piece_len != source length after init");
}

static void world_close(world *w)
{
    piece_destroy(w->t);
    w->t = NULL;
}

/* ------------------------------------------------------------------ */
/* op runners                                                          */
/* ------------------------------------------------------------------ */

enum { M_RANDOM, M_TYPING, M_UNDO };

static uint8_t g_ins[1 << 20];

/* Runs nops operations. si/sd/sr NULL = untimed (batch) pass. Returns 1 on cap timeout.
 * M_RANDOM: 50/50 insert/delete, 1-64 bytes, offsets uniform over current length.
 * M_TYPING: cursor starts at len/2; ~1 key in 20 is a backspace (1-byte delete),
 *           ~1 in 40 inserted bytes is a newline.
 * M_UNDO:   delete 1-64 bytes keeping the ref, then piece_insert_ref it back. sd times
 *           the delete, sr the insert_ref; content is verified outside the timers on
 *           timed passes. */
static int run_ops(world *w, int mode, size_t nops, bench_samples *si, bench_samples *sd,
                   bench_samples *sr, uint64_t *wall_ns)
{
    piece_tree *t = w->t;
    uint64_t cursor = w->expect_len / 2, t0 = bench_now_ns(), a, b;
    size_t i;
    int timed = si != NULL || sd != NULL || sr != NULL, to = 0;
    uint8_t before[64], after[64];

    for (i = 0; i < nops; i++) {
        uint64_t r = rnd();
        if ((timed ? (i & 63) == 0 : (i & 4095) == 0) && over()) { to = 1; break; }
        if (mode == M_TYPING) {
            if (r % 20 == 0 && cursor > 0) {
                a = bench_now_ns();
                check_rc(piece_delete(t, cursor - 1, 1, NULL), "piece_delete");
                b = bench_now_ns();
                if (sd) (void)bench_add(sd, b - a);
                cursor--; w->expect_len--; w->deleted++;
            } else {
                uint8_t ch = (r >> 8) % 40 == 0 ? (uint8_t)'\n' : (uint8_t)(32 + (r >> 20) % 95);
                a = bench_now_ns();
                check_rc(piece_insert(t, cursor, &ch, 1), "piece_insert");
                b = bench_now_ns();
                if (si) (void)bench_add(si, b - a);
                cursor++; w->expect_len++; w->typed++;
            }
        } else if (mode == M_RANDOM) {
            size_t len = 1 + (size_t)rnd_range(64);
            if ((r & 1) || w->expect_len < len) {
                uint64_t off = rnd_range(w->expect_len + 1);
                gen_text(g_ins, len);
                a = bench_now_ns();
                check_rc(piece_insert(t, off, g_ins, len), "piece_insert");
                b = bench_now_ns();
                if (si) (void)bench_add(si, b - a);
                w->expect_len += len; w->typed += len;
            } else {
                uint64_t off = rnd_range(w->expect_len - len + 1);
                a = bench_now_ns();
                check_rc(piece_delete(t, off, len, NULL), "piece_delete");
                b = bench_now_ns();
                if (sd) (void)bench_add(sd, b - a);
                w->expect_len -= len; w->deleted += len;
            }
        } else { /* M_UNDO */
            size_t len = 1 + (size_t)rnd_range(64);
            uint64_t off;
            piece_ref ref;
            if (w->expect_len < len) continue;
            off = rnd_range(w->expect_len - len + 1);
            if (timed) check_rc(piece_read(t, off, before, len), "piece_read");
            a = bench_now_ns();
            check_rc(piece_delete(t, off, len, &ref), "piece_delete");
            b = bench_now_ns();
            if (sd) (void)bench_add(sd, b - a);
            w->deleted += len;
            a = bench_now_ns();
            check_rc(piece_insert_ref(t, off, &ref), "piece_insert_ref");
            b = bench_now_ns();
            if (sr) (void)bench_add(sr, b - a);
            if (timed) {
                check_rc(piece_read(t, off, after, len), "piece_read");
                if (memcmp(before, after, len) != 0) die("undo reinsert restored different bytes");
            }
        }
    }
    if (wall_ns) *wall_ns = bench_now_ns() - t0;
    if (piece_len(t) != w->expect_len) die("piece_len != expected length after ops");
    return to;
}

/* Viewport: 300 rows from `off`, bounded to 300*256 bytes so a 1 GB single line
 * costs the same as a code file. Touches every byte it counts (memchr). */
static volatile uint64_t g_sink;
static size_t view_scan(piece_iter *it)
{
    const uint8_t *p;
    size_t n, rows = 0, bytes = 0;
    while (rows < 300 && bytes < 300 * 256 && piece_iter_next(it, &p, &n)) {
        while (n > 0 && rows < 300 && bytes < 300 * 256) {
            size_t lim = n < 300 * 256 - bytes ? n : 300 * 256 - bytes;
            const uint8_t *q = memchr(p, '\n', lim);
            size_t used = q ? (size_t)(q - p) + 1 : lim;
            if (q) rows++;
            p += used; n -= used; bytes += used;
        }
    }
    g_sink += rows + bytes;
    return rows;
}

/* read_viewport / byte_to_line / line_to_byte on a tree; returns a bitmask of kinds
 * that hit the cap (1 viewport, 2 byte_to_line, 4 line_to_byte). */
static int run_queries(piece_tree *t, size_t nview, size_t nq, bench_samples *sv,
                       bench_samples *sb, bench_samples *sl)
{
    uint64_t lc = piece_line_count(t), len = piece_len(t), a;
    size_t i;
    int to = 0;
    /* each query kind gets its own cap window so one slow kind cannot starve the others */
    cell_begin();
    for (i = 0; i < nview; i++) {
        uint64_t L = rnd_range(lc > 300 ? lc - 300 : 1), off = piece_line_to_byte(t, L);
        piece_iter it;
        if (over()) { to |= 1; break; }
        a = bench_now_ns();
        piece_iter_begin(&it, t, off);
        view_scan(&it);
        (void)bench_add(sv, bench_now_ns() - a);
    }
    cell_begin();
    for (i = 0; i < nq; i++) {
        uint64_t off = rnd_range(len), r;
        if (over()) { to |= 2; break; }
        a = bench_now_ns();
        r = piece_byte_to_line(t, off);
        (void)bench_add(sb, bench_now_ns() - a);
        g_sink += r;
    }
    cell_begin();
    for (i = 0; i < nq; i++) {
        uint64_t L = rnd_range(lc), r;
        if (over()) { to |= 4; break; }
        a = bench_now_ns();
        r = piece_line_to_byte(t, L);
        (void)bench_add(sl, bench_now_ns() - a);
        g_sink += r;
    }
    return to;
}

/* ------------------------------------------------------------------ */
/* G10f memory gates                                                   */
/* ------------------------------------------------------------------ */

/* Hard maxima (perf/01 §0.2 G10f): unedited copy 1.25 S + 64 KiB; unedited mmap
 * 32 B * ceil(S/64 KiB) + 2 MB. Edited: that base (the original is still owned)
 * + 96 B/piece + 1.25*(typed+deleted). The 64 B/undo term is the undo module's. */
static double mem_base(const source *s)
{
    if (s->mapped)
        return 32.0 * (double)((s->len + 65535) / 65536) + 2e6;
    return 1.25 * (double)s->len + 65536.0;
}
static double mem_edited(const source *s, uint64_t pieces, uint64_t typed, uint64_t deleted)
{
    return mem_base(s) + 96.0 * (double)pieces + 1.25 * (double)(typed + deleted);
}

static void emit_mem(const char *row, const source *s, const world *w, int track_only)
{
    gate_t g = NOGATE;
    uint64_t pc = piece_piece_count(w->t);
    if (!track_only)
        g.p99 = mem_edited(s, pc, w->typed, w->deleted);
    emit_value(row, "peak_mem", (double)g_peak, "B", &g, 0);
    emit_value(row, "pieces", (double)pc, "count", &NOGATE, 0);
}

/* ------------------------------------------------------------------ */
/* rows                                                                */
/* ------------------------------------------------------------------ */

static const gate_t G_OP = { -1.0, 50.0 };    /* §2.6 insert/delete p99 <= 50 us */
static const gate_t G_UNDO = { 63.0, 84.0 };  /* G9 10k-step undo, ms */
static const gate_t G_PASTE = { 5.0, 15.0 };  /* G9 1 MB paste, ms */
static const gate_t G_JUMP = { 30.0, 50.0 };  /* G7j unindexed jump, ms */

#define CTX_OPS "context: UI-thread slice ceiling 0.5 ms (perf s3); per-op gate 50 us (s2.6, inside G1)"
#define CTX_OPEN "context: G5 open warm 6 / 9 ms (s0.2), end-to-end incl. frame; not a gate here"

typedef struct {
    const char *name, *file;
    int mapped;
    int mode;            /* -1 = mini random phase (open rows) */
    size_t nops;         /* ops for the timed pass */
    size_t batch_reps;   /* untimed fresh-tree batch passes (0 = none) */
    int gate_batch;      /* batch_total gated by G_UNDO */
    size_t open_reps;
    const char *ctx;
} rowspec;

static void samples_alloc(bench_samples *s, size_t cap)
{
    uint64_t *b = malloc((cap ? cap : 1) * sizeof *b);
    if (!b) die("sample alloc");
    bench_samples_init(s, b, cap ? cap : 1);
}

static int row_skip_missing(const char *row, const char *file)
{
    (void)file;
    emit_skip(row, "open");
    fprintf(stderr, "piece_bench: row %s: corpus file %s/%s missing, SKIP\n", row, o_corpus, file);
    human_table(row, NULL);
    return 0;
}

static int row_generic(const rowspec *sp)
{
    source s;
    world w;
    bench_samples so, sf, si, sd, sr, sv, sb, sl, sbt;
    size_t i, nops, nq = sc(1000), nview = sc(300), reps = sp->open_reps;
    int to_open = 0, to_batch = 0, to_ops = 0, to_q = 0;
    int mini = sp->mode < 0, mode = mini ? M_RANDOM : sp->mode;
    const char *row = sp->name;
    double mem_open = 0;

    if (source_open(&s, sp->file, sp->mapped) != 0)
        return row_skip_missing(row, sp->file);
    nops = mini ? sc(2000) : sc(sp->nops);

    /* open + first_op, fresh tree per rep */
    samples_alloc(&so, reps); samples_alloc(&sf, reps);
    cell_begin();
    for (i = 0; i < reps; i++) {
        uint64_t t0, on;
        uint8_t ch = 'x';
        piece_tree *t;
        if (over()) { to_open = 1; break; }
        if (i == 0) reset_peak();
        t = tree_open(&s, &on, &t0);
        if (i == 0) mem_open = (double)g_peak;
        check_rc(piece_insert(t, 0, &ch, 1), "piece_insert(first_op)");
        (void)bench_add(&sf, bench_now_ns() - t0);
        (void)bench_add(&so, on);
        piece_destroy(t);
    }
    emit_samples(row, "open", &so, "ms", &NOGATE, to_open);
    emit_samples(row, "first_op", &sf, "ms", &NOGATE, to_open);
    {
        gate_t g = NOGATE;
        g.p99 = mem_base(&s);
        emit_value(row, "mem_open", mem_open, "B", &g, 0);
    }

    /* untimed batch passes, fresh tree each, identical op sequence (same seed) */
    if (sp->batch_reps) {
        samples_alloc(&sbt, sp->batch_reps);
        for (i = 0; i < sp->batch_reps && !to_batch; i++) {
            uint64_t wall;
            world_open(&w, &s, NULL);
            seed(0xB47C4 + 1);
            cell_begin();
            to_batch = run_ops(&w, mode, nops, NULL, NULL, NULL, &wall);
            (void)bench_add(&sbt, wall);
            world_close(&w);
        }
        emit_samples(row, "batch_total", &sbt, "ms", sp->gate_batch ? &G_UNDO : &NOGATE, to_batch);
    }

    /* timed pass: per-op samples, memory, then queries on the edited tree */
    samples_alloc(&si, nops); samples_alloc(&sd, nops); samples_alloc(&sr, nops);
    samples_alloc(&sv, nview); samples_alloc(&sb, nq); samples_alloc(&sl, nq);
    world_open(&w, &s, NULL);
    reset_peak();
    seed(0xB47C4 + 1);
    cell_begin();
    to_ops = run_ops(&w, mode, nops, &si, &sd, &sr, NULL);
    if (sp->mode != M_UNDO)
        emit_samples(row, "insert", &si, "us", &G_OP, to_ops && si.n == 0);
    if (sp->mode == M_UNDO) {
        emit_samples(row, "delete", &sd, "us", &G_OP, to_ops && sd.n == 0);
        emit_samples(row, "insert_ref", &sr, "us", &NOGATE, to_ops && sr.n == 0);
    } else {
        emit_samples(row, "delete", &sd, "us", &G_OP, to_ops && sd.n == 0);
    }
    if (to_ops) emit_value(row, "ops_done", (double)(si.n + sd.n), "count", &NOGATE, 1);
    emit_mem(row, &s, &w, 0);
    seed(0xB47C4 + 2);
    to_q = run_queries(w.t, nview, nq, &sv, &sb, &sl);
    emit_samples(row, "read_viewport", &sv, "us", &NOGATE, (to_q & 1) != 0);
    emit_samples(row, "byte_to_line", &sb, "us", &NOGATE, (to_q & 2) != 0);
    emit_samples(row, "line_to_byte", &sl, "us", &NOGATE, (to_q & 4) != 0);
    world_close(&w);
    human_table(row, sp->batch_reps ? CTX_OPS "\nnote: batch_total gate (G9) includes a full frame; kernel passing is necessary, not sufficient"
                                    : (mini ? CTX_OPEN "\n" CTX_OPS : CTX_OPS));
    return 0;
}

static int row_paste(const char *row)
{
    source s;
    world w;
    bench_samples sp, si;
    size_t i, reps = o_quick ? 5 : 20, plen = o_quick ? (1u << 20) / 64 : (1u << 20);
    int to = 0;
    if (source_open(&s, "ascii_code.c", 0) != 0)
        return row_skip_missing(row, "ascii_code.c");
    samples_alloc(&sp, reps); samples_alloc(&si, reps);
    seed(0xBA57E);
    cell_begin();
    for (i = 0; i < reps; i++) {
        uint64_t off, a;
        if (over()) { to = 1; break; }
        world_open(&w, &s, NULL);
        gen_text(g_ins, plen);
        off = rnd_range(w.expect_len + 1);
        if (i == reps - 1) reset_peak();
        a = bench_now_ns();
        check_rc(piece_insert(w.t, off, g_ins, plen), "piece_insert(paste)");
        (void)bench_add(&sp, bench_now_ns() - a);
        w.expect_len += plen; w.typed += plen;
        if (piece_len(w.t) != w.expect_len) die("paste length");
        if (i == reps - 1 || i + 1 == reps) {
            emit_samples(row, "paste_1m", &sp, "ms", &G_PASTE, 0);
            emit_mem(row, &s, &w, 0);
        }
        world_close(&w);
    }
    if (to) emit_samples(row, "paste_1m", &sp, "ms", &G_PASTE, 1);
    (void)si;
    human_table(row, "note: G9 1 MB paste 5 / 15 ms includes a full frame; kernel passing is necessary, not sufficient");
    return 0;
}

static int row_snapshots(const char *row)
{
    source s;
    world w;
    bench_samples si, sd, st, sv;
    size_t nedits = sc(10000), nsnap = sc(100), every, i, nlive = 0, k;
    piece_snapshot **snaps;
    int to = 0;
    uint64_t a;
    if (source_open(&s, "ascii_code.c", 0) != 0)
        return row_skip_missing(row, "ascii_code.c");
    every = nedits / nsnap ? nedits / nsnap : 1;
    snaps = calloc(nsnap + 1, sizeof *snaps);
    if (!snaps) die("alloc");
    samples_alloc(&si, nedits); samples_alloc(&sd, nedits);
    samples_alloc(&st, nsnap + 1); samples_alloc(&sv, nedits / 100 + 2);
    world_open(&w, &s, NULL);
    reset_peak();
    seed(0x5A9);
    cell_begin();
    for (i = 0; i < nedits && !to; i++) {
        if (i % every == 0 && nlive < nsnap) {
            a = bench_now_ns();
            snaps[nlive] = piece_snapshot_take(w.t);
            (void)bench_add(&st, bench_now_ns() - a);
            if (!snaps[nlive]) die("piece_snapshot_take returned NULL");
            nlive++;
        }
        to = run_ops(&w, M_RANDOM, 1, &si, &sd, NULL, NULL);
        if (i % 100 == 99 && nlive > 0) {
            piece_snapshot *sn = snaps[rnd_range(nlive)];
            uint64_t slc = piece_snapshot_line_count(sn);
            uint64_t L = rnd_range(slc > 300 ? slc - 300 : 1);
            uint64_t off = piece_snapshot_line_to_byte(sn, L);
            piece_iter it;
            a = bench_now_ns();
            piece_iter_begin_snapshot(&it, sn, off);
            view_scan(&it);
            (void)bench_add(&sv, bench_now_ns() - a);
        }
    }
    emit_samples(row, "insert", &si, "us", &G_OP, to && si.n == 0);
    emit_samples(row, "delete", &sd, "us", &G_OP, to && sd.n == 0);
    emit_samples(row, "snapshot_take", &st, "us", &NOGATE, to && st.n == 0);
    emit_samples(row, "read_viewport", &sv, "us", &NOGATE, to && sv.n == 0);
    emit_value(row, "peak_mem", (double)g_peak, "B", &NOGATE, 0);
    emit_value(row, "pathcopy_ref", 1920.0 * (double)(si.n + sd.n), "B", &NOGATE, 0);
    emit_value(row, "pieces", (double)piece_piece_count(w.t), "count", &NOGATE, 0);
    a = bench_now_ns();
    for (k = 0; k < nlive; k++) piece_snapshot_release(snaps[k]);
    emit_value(row, "snapshot_release_all", (double)(bench_now_ns() - a), "ms", &NOGATE, 0);
    world_close(&w);
    free(snaps);
    human_table(row, CTX_OPS "\ncontext: pathcopy_ref = 1920 B per edit while a snapshot is live (perf s2.6); peak_mem is TRACK, not gated");
    return 0;
}

static int row_line_jump(const char *row)
{
    source s;
    bench_samples so, sf, sb, sl;
    size_t i, reps = 5, nq = sc(200);
    uint64_t L0 = o_quick ? 10000000ull / 64 : 10000000ull, last = 0;
    int to = 0;
    piece_tree *t = NULL;
    if (source_open(&s, "log_1g.txt", 1) != 0)
        return row_skip_missing(row, "log_1g.txt");
    samples_alloc(&so, reps); samples_alloc(&sf, reps);
    samples_alloc(&sb, nq); samples_alloc(&sl, nq);
    cell_begin();
    for (i = 0; i < reps; i++) {
        uint64_t on, a, r;
        if (over()) { to = 1; break; }
        if (t) piece_destroy(t);
        t = tree_open(&s, &on, NULL);
        (void)bench_add(&so, on);
        a = bench_now_ns();
        r = piece_line_to_byte(t, L0);
        (void)bench_add(&sf, bench_now_ns() - a);
        last = r;
    }
    emit_samples(row, "open", &so, "ms", &NOGATE, 0);
    emit_samples(row, "line_to_byte_first", &sf, "ms", &G_JUMP, to);
    if (t && last > 0) {
        seed(0x11E);
        for (i = 0; i < nq && !over(); i++) {
            uint64_t L = L0 - L0 / 10 + rnd_range(L0 / 5 + 1), off = rnd_range(piece_len(t)), a, r;
            a = bench_now_ns();
            r = piece_line_to_byte(t, L);
            (void)bench_add(&sl, bench_now_ns() - a);
            g_sink += r;
            a = bench_now_ns();
            r = piece_byte_to_line(t, off);
            (void)bench_add(&sb, bench_now_ns() - a);
            g_sink += r;
        }
    }
    emit_samples(row, "line_to_byte", &sl, "ms", &NOGATE, 0);
    emit_samples(row, "byte_to_line", &sb, "ms", &NOGATE, 0);
    if (t) piece_destroy(t);
    human_table(row, "context: G7j unindexed jump 30 / 50 ms is Target A, warm, [AC, prov]; this box is on battery");
    return 0;
}

/* ------------------------------------------------------------------ */
/* driver                                                              */
/* ------------------------------------------------------------------ */

static const rowspec SPECS[] = {
    { "code_1m",          "ascii_code.c",  0, -1, 0,      0, 0, 20, NULL },
    { "log_1g",           "log_1g.txt",    1, -1, 0,      0, 0, 5,  NULL },
    { "oneline_1g",       "oneline_1g.txt",1, -1, 0,      0, 0, 5,  NULL },
    { "oneline_10g",      "oneline_10g.txt",1,-1, 0,      0, 0, 3,  NULL },
    { "dense_short",      "dense_short.txt",0,-1, 0,      0, 0, 10, NULL },
    { "random_edits_1e5", "log_1g.txt",    1, M_RANDOM, 100000, 3,  0, 3,  NULL },
    { "typing_1e4",       "ascii_code.c",  0, M_TYPING, 10000,  11, 0, 3,  NULL },
    { "undo_1e4",         "log_1g.txt",    1, M_UNDO,   10000,  11, 1, 3,  NULL },
    { "undo_1e5",         "log_1g.txt",    1, M_UNDO,   100000, 3,  0, 3,  NULL },
};
#define NSPECS (sizeof SPECS / sizeof SPECS[0])
static const char *ROWS[] = { "code_1m", "log_1g", "oneline_1g", "oneline_10g", "dense_short",
    "random_edits_1e5", "typing_1e4", "undo_1e4", "undo_1e5", "paste_1m", "snapshots_100",
    "line_jump_1e7" };
#define NROWS (sizeof ROWS / sizeof ROWS[0])

static int run_row(const char *name)
{
    size_t i;
    if (strcmp(name, "oneline_10g") == 0) {
        char p[512];
        corpus_path(p, sizeof p, "oneline_10g.txt");
        if (!o_huge || access(p, R_OK) != 0) {
            emit_skip(name, "open");
            fprintf(stderr, "piece_bench: row oneline_10g SKIP (%s)\n",
                    o_huge ? "file missing; never created by the bench" : "needs --huge");
            human_table(name, NULL);
            return 0;
        }
    }
    for (i = 0; i < NSPECS; i++)
        if (strcmp(SPECS[i].name, name) == 0)
            return row_generic(&SPECS[i]);
    if (strcmp(name, "paste_1m") == 0) return row_paste(name);
    if (strcmp(name, "snapshots_100") == 0) return row_snapshots(name);
    if (strcmp(name, "line_jump_1e7") == 0) return row_line_jump(name);
    return 2;
}

static int row_selected(const char *list, const char *name)
{
    size_t n = strlen(name);
    const char *p = list;
    if (!list) return 1;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == n && strncmp(p, name, n) == 0) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *rows = NULL;
    int i, rc_all = 0;
    size_t r;
    char bat[32];

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--quick") == 0) o_quick = 1;
        else if (strcmp(a, "--huge") == 0) o_huge = 1;
        else if (strcmp(a, "--selftest") == 0) return selftest();
        else if (strncmp(a, "--rows=", 7) == 0) rows = a + 7;
        else if (strncmp(a, "--cap=", 6) == 0) o_cap = atof(a + 6);
        else if (strncmp(a, "--corpus=", 9) == 0) o_corpus = a + 9;
        else { fprintf(stderr, "piece_bench: unknown arg %s\n", a); return 2; }
    }
    if (rows) {
        /* validate names */
        char *dup = strdup(rows), *tok, *sv = NULL;
        for (tok = strtok_r(dup, ",", &sv); tok; tok = strtok_r(NULL, ",", &sv)) {
            size_t k;
            int ok = 0;
            for (k = 0; k < NROWS; k++) if (strcmp(ROWS[k], tok) == 0) ok = 1;
            if (!ok) { fprintf(stderr, "piece_bench: unknown row %s\n", tok); return 2; }
        }
        free(dup);
    }
    bench_battery_status(bat, sizeof bat);
    printf("# piece_bench quick=%d huge=%d cap=%gs corpus=%s power=%s %s\n", o_quick, o_huge,
           o_cap, o_corpus, bat, bench_evidence_tag());
    fflush(stdout);

    for (r = 0; r < NROWS; r++) {
        pid_t pid;
        int status = 0, done = 0;
        uint64_t hard;
        if (!row_selected(rows, ROWS[r])) continue;
        fflush(stdout);
        pid = fork();
        if (pid < 0) { perror("fork"); return 2; }
        if (pid == 0) {
            int rc = run_row(ROWS[r]);
            fflush(stdout);
            _exit(rc != 0 ? rc : (g_fail ? 1 : 0));
        }
        hard = bench_now_ns() + (uint64_t)((o_cap * 12.0 + 60.0) * 1e9);
        while (!done) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) { done = 1; break; }
            if (bench_now_ns() > hard) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                printf("BENCH row=%s col=row_total p50=0 p99=0 unit=ms n=0 gate=none status=TIMEOUT\n", ROWS[r]);
                fprintf(stderr, "piece_bench: row %s killed after %gs backstop\n", ROWS[r], o_cap * 12.0 + 60.0);
                rc_all = 1;
                done = 2;
                break;
            }
            {
                struct timespec ts = { 0, 20 * 1000 * 1000 };
                nanosleep(&ts, NULL);
            }
        }
        if (done == 1) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) { /* ok */ }
            else {
                rc_all = 1;
                if (!WIFEXITED(status) || WEXITSTATUS(status) > 1)
                    fprintf(stderr, "piece_bench: row %s crashed or errored (status 0x%x)\n", ROWS[r], status);
            }
        }
        fflush(stdout);
    }
    printf("# piece_bench %s\n", rc_all ? "GATE MISS" : "all gated cells pass");
    return rc_all;
}
