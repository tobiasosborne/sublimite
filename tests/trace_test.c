/* trace_test.c - timing ring gates (P0.3) and the input event ring + dump
 * section (P0.6). Records a synthetic sequence from 2 threads, dumps, reloads
 * through trace_fmt.h and checks the gate percentiles against known values.
 * Input ring: append/wrap/drop, seq monotonicity, dump round-trip (byte-exact),
 * legacy version-1 dumps, rejection of malformed sections, zero mallocs. */
#include "../src/trace/trace.h"
#include "../src/trace/trace_fmt.h"
#include "base/base.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define NFRAMES 100u
#define BASE_NS 1000000000ull

static int g_fail;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                 \
            fputc('\n', stderr);                          \
            g_fail = 1;                                   \
        }                                                 \
    } while (0)

/* Thread A: ingress T0 and device-done T5. Thread B: present-submitted T4 and
 * present-complete T6. Frame f: T0 = BASE + f*1e6, T4 = T0 + f*1000,
 * T5 = T0 + 2f*1000, T6 = T0 + 3f*1000. Known percentiles over f = 1..100:
 * G1 (T4-T0) p50 50000 p99 99000; G3 (T5-T0) p50 100000 p99 198000;
 * T6-T0 p50 150000 p99 297000. */
static void *thread_a(void *arg) {
    (void)arg;
    trace_thread_register();
    for (uint32_t f = 1; f <= NFRAMES; f++) {
        uint64_t t0 = BASE_NS + (uint64_t)f * 1000000u;
        trace_record_at(t0, TRACE_T0_INGRESS, f);
        trace_record_at(t0 + (uint64_t)f * 2000u, TRACE_T5_DEVICE_DONE, f);
    }
    return NULL;
}

static void *thread_b(void *arg) {
    (void)arg;
    trace_thread_register();
    for (uint32_t f = 1; f <= NFRAMES; f++) {
        uint64_t t0 = BASE_NS + (uint64_t)f * 1000000u;
        trace_record_at(t0 + (uint64_t)f * 1000u, TRACE_T4_PRESENT_SUBMITTED, f);
        trace_record_at(t0 + (uint64_t)f * 3000u, TRACE_T6_PRESENT_COMPLETE, f);
    }
    return NULL;
}

static void check_gate(const trace_rec *recs, size_t n, enum trace_ev from, enum trace_ev to,
                       const char *name, uint64_t want50, uint64_t want99) {
    uint64_t *lat = NULL;
    size_t nlat = 0;
    uint64_t p50, p99;
    CHECK(trace_fmt_latencies(recs, n, from, to, &lat, &nlat) == 0, "latencies %s", name);
    CHECK(nlat == NFRAMES, "%s n=%zu want %u", name, nlat, NFRAMES);
    p50 = trace_fmt_pct(lat, nlat, 50);
    p99 = trace_fmt_pct(lat, nlat, 99);
    CHECK(p50 == want50, "%s p50=%llu want %llu", name, (unsigned long long)p50, (unsigned long long)want50);
    CHECK(p99 == want99, "%s p99=%llu want %llu", name, (unsigned long long)p99, (unsigned long long)want99);
    printf("%-14s n=%zu p50=%llu ns p99=%llu ns\n", name, nlat,
           (unsigned long long)p50, (unsigned long long)p99);
    free(lat);
}

/* ---- input ring (P0.6) ---- */

#define IN_BASE 5000000000ull
#define DUMP_CAP (1u << 20)

static trace_input_rec g_copy[TRACE_INPUT_CAP];
static unsigned char g_bytes[DUMP_CAP];

static void build_mixed(void) {
    trace_input_key(IN_BASE + 0, TRACE_IN_KEY_DOWN, 0x61, 0x1, 0, "a", 1);
    trace_input_key(IN_BASE + 1000, TRACE_IN_KEY_UP, 0x61, 0x1, 0, NULL, 0);
    trace_input_key(IN_BASE + 2000, TRACE_IN_KEY_DOWN, 0xe9, 0, 1,
                    "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 10);
    trace_input_pointer(IN_BASE + 3000, TRACE_IN_POINTER_MOVE, -5, 700, 0, 0x4);
    trace_input_pointer(IN_BASE + 4000, TRACE_IN_BUTTON_DOWN, 10, 20, 0x1, 0);
    trace_input_pointer(IN_BASE + 5000, TRACE_IN_BUTTON_UP, 10, 20, 0, 0);
    trace_input_wheel(IN_BASE + 6000, 0, -(3 << 16), 0);
    trace_input_resize(IN_BASE + 7000, 1280, 800);
    trace_input_focus(IN_BASE + 8000, 0);
    trace_input_focus(IN_BASE + 9000, 1);
    trace_input_clipboard(IN_BASE + 10000, 1, 4096);
    trace_input_filechange(IN_BASE + 11000, 7, 0x2);
}

/* Dumps the current state to a buffer; returns its length or 0 on failure. */
static size_t dump_to_bytes(unsigned char *buf, size_t cap) {
    FILE *f = tmpfile();
    long len;
    size_t got;
    if (f == NULL || trace_dump(f) != 0) { if (f) fclose(f); return 0; }
    len = ftell(f);
    rewind(f);
    if (len <= 0 || (size_t)len > cap) { fclose(f); return 0; }
    got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    return got == (size_t)len ? got : 0;
}

static int load_bytes(const unsigned char *buf, size_t n, trace_loaded *d) {
    FILE *f = fmemopen((void *)buf, n, "rb");
    int rc;
    if (f == NULL) return -1;
    rc = trace_fmt_load_dump(f, d);
    fclose(f);
    return rc;
}

static void test_ring_append_and_wrap(void) {
    size_t n;
    trace_reset();
    for (uint32_t i = 0; i < 5; i++) {
        trace_input_key(IN_BASE + i * 1000u, TRACE_IN_KEY_DOWN, 0x61u + i, 0, 0, NULL, 0);
    }
    CHECK(trace_input_count() == 5, "count=%llu want 5", (unsigned long long)trace_input_count());
    CHECK(trace_input_dropped() == 0, "dropped=%llu want 0", (unsigned long long)trace_input_dropped());
    n = trace_input_copy(g_copy, TRACE_INPUT_CAP);
    CHECK(n == 5, "copy n=%zu want 5", n);
    for (size_t i = 0; i < n; i++) {
        CHECK(g_copy[i].seq == i, "seq[%zu]=%llu", i, (unsigned long long)g_copy[i].seq);
        CHECK(g_copy[i].kind == TRACE_IN_KEY_DOWN, "kind[%zu]=%u", i, (unsigned)g_copy[i].kind);
        CHECK(g_copy[i].t0_ns == IN_BASE + i * 1000u, "t0[%zu]", i);
        CHECK(g_copy[i].p.key.keysym == 0x61u + i, "keysym[%zu]", i);
    }

    /* Wrap: CAP + 100 appends keep the newest CAP, drop the oldest 100. */
    trace_reset();
    for (uint32_t i = 0; i < TRACE_INPUT_CAP + 100u; i++) {
        trace_input_resize(IN_BASE + i, i, i + 1u);
    }
    CHECK(trace_input_count() == TRACE_INPUT_CAP + 100u, "wrap count");
    CHECK(trace_input_dropped() == 100u, "wrap dropped=%llu",
          (unsigned long long)trace_input_dropped());
    n = trace_input_copy(g_copy, TRACE_INPUT_CAP);
    CHECK(n == TRACE_INPUT_CAP, "wrap copy n=%zu", n);
    for (size_t i = 0; i < n; i++) {
        uint64_t want_seq = 100u + i;
        CHECK(g_copy[i].seq == want_seq, "wrap seq[%zu]=%llu", i, (unsigned long long)g_copy[i].seq);
        CHECK(g_copy[i].p.resize.w == (uint32_t)want_seq, "wrap payload[%zu]", i);
    }
}

static void test_seq_monotonic_and_fields(void) {
    size_t n;
    trace_reset();
    build_mixed();
    n = trace_input_copy(g_copy, TRACE_INPUT_CAP);
    CHECK(n == 12, "mixed n=%zu want 12", n);
    for (size_t i = 1; i < n; i++) {
        CHECK(g_copy[i].seq == g_copy[i - 1].seq + 1, "seq gap at %zu", i);
    }
    CHECK(g_copy[2].p.key.utf8_len == 8, "utf8 not clipped to 8: %u", (unsigned)g_copy[2].p.key.utf8_len);
    CHECK(g_copy[2].p.key.utf8[7] == 0xa9, "utf8 tail");
    CHECK(g_copy[3].p.pointer.x == -5 && g_copy[3].p.pointer.y == 700, "pointer x/y");
    CHECK(g_copy[6].p.wheel.dy == -(3 << 16), "wheel fixed point");
    CHECK(g_copy[7].p.resize.w == 1280 && g_copy[7].p.resize.h == 800, "resize");
    CHECK(g_copy[10].p.clipboard.selection == 1 && g_copy[10].p.clipboard.length == 4096, "clipboard");
    CHECK(g_copy[11].p.filechange.watch_id == 7 && g_copy[11].p.filechange.flags == 0x2, "filechange");
    CHECK(g_copy[8].kind == TRACE_IN_FOCUS && g_copy[8].p.focus.focused == 0, "focus out");
    CHECK(g_copy[9].p.focus.focused == 1, "focus in");
}

static void test_dump_roundtrip(void) {
    size_t len, n;
    trace_loaded d;
    trace_reset();
    build_mixed();
    n = trace_input_copy(g_copy, TRACE_INPUT_CAP);
    len = dump_to_bytes(g_bytes, sizeof g_bytes);
    CHECK(len > 0, "dump_to_bytes");
    CHECK(load_bytes(g_bytes, len, &d) == 0, "load_dump of new dump");
    CHECK(d.has_input == 1, "has_input");
    CHECK(d.nin == n, "nin=%zu want %zu", d.nin, n);
    CHECK(d.in_dropped == 0, "in_dropped");
    CHECK(d.nrecs == 0, "timing nrecs=%zu (rings reset)", d.nrecs);
    CHECK(d.nin == n && memcmp(d.in, g_copy, n * sizeof g_copy[0]) == 0,
          "input records not byte-exact after round trip");
    trace_fmt_dump_free(&d);
}

static void test_dump_roundtrip_wrapped(void) {
    size_t len;
    trace_loaded d;
    trace_reset();
    for (uint32_t i = 0; i < TRACE_INPUT_CAP + 3u; i++) {
        trace_input_focus(IN_BASE + i, (int)(i & 1u));
    }
    len = dump_to_bytes(g_bytes, sizeof g_bytes);
    CHECK(len > 0, "dump wrapped");
    CHECK(load_bytes(g_bytes, len, &d) == 0, "load wrapped");
    CHECK(d.nin == TRACE_INPUT_CAP, "wrapped nin=%zu", d.nin);
    CHECK(d.in_dropped == 3u, "wrapped dropped=%llu", (unsigned long long)d.in_dropped);
    CHECK(d.nin > 0 && d.in[0].seq == 3u, "first retained seq");
    trace_fmt_dump_free(&d);
}

static void test_legacy_v1_loads(void) {
    /* A dump as written before P0.6: header + one ring block, nothing after. */
    unsigned char buf[8 + 16 + 8 + 2 * 16];
    trace_rec recs[2];
    uint32_t hdr[4] = { 1u, 1u, 16u, 65536u };
    uint32_t blk[2] = { 0u, 2u };
    trace_loaded d;
    size_t off = 0;
    memset(recs, 0, sizeof recs);
    recs[0].ns = 111; recs[0].frame_id = 1; recs[0].ev = TRACE_T0_INGRESS;
    recs[1].ns = 222; recs[1].frame_id = 1; recs[1].ev = TRACE_T4_PRESENT_SUBMITTED;
    memcpy(buf + off, "EDTRACE1", 8); off += 8;
    memcpy(buf + off, hdr, sizeof hdr); off += sizeof hdr;
    memcpy(buf + off, blk, sizeof blk); off += sizeof blk;
    memcpy(buf + off, recs, sizeof recs); off += sizeof recs;
    CHECK(load_bytes(buf, off, &d) == 0, "legacy v1 load");
    CHECK(d.nrecs == 2 && d.nrecs > 0 && d.recs[1].ns == 222, "legacy v1 records");
    CHECK(d.has_input == 0 && d.nin == 0, "legacy v1 has no input section");
    trace_fmt_dump_free(&d);
    /* And the legacy loader entry point still works on it. */
    {
        FILE *f = fmemopen(buf, off, "rb");
        trace_rec *r = NULL;
        size_t nr = 0;
        CHECK(f != NULL && trace_fmt_load(f, &r, &nr) == 0 && nr == 2, "trace_fmt_load legacy");
        if (f) fclose(f);
        free(r);
    }
}

static void test_malformed_rejected(void) {
    size_t len, sec;
    uint32_t nrings;
    trace_loaded d;
    unsigned char *m = g_bytes;
    uint32_t bad;
    trace_reset();
    build_mixed();
    len = dump_to_bytes(g_bytes, sizeof g_bytes);
    CHECK(len > 0, "dump for malformed tests");
    /* Layout: magic(8) hdr(16) then one empty 8-byte block per used ring, then
     * the INPT section: tag, version, rec_size, count (4 each), dropped (8),
     * then 48-byte records. */
    memcpy(&nrings, m + 12, 4);
    sec = 24u + 8u * nrings;
    CHECK(load_bytes(g_bytes, len - 1, &d) != 0, "truncated record must be rejected");
    CHECK(load_bytes(g_bytes, len - 48 - 1, &d) != 0, "partial record must be rejected");
    CHECK(load_bytes(g_bytes, sec + 4, &d) != 0, "truncated section header must be rejected");
    bad = 32u; memcpy(m + sec + 8, &bad, 4);
    CHECK(load_bytes(g_bytes, len, &d) != 0, "bad rec_size must be rejected");
    bad = 48u; memcpy(m + sec + 8, &bad, 4);
    bad = 1000000u; memcpy(m + sec + 12, &bad, 4);
    CHECK(load_bytes(g_bytes, len, &d) != 0, "huge count must be rejected");
    bad = TRACE_INPUT_CAP + 1u; memcpy(m + sec + 12, &bad, 4);
    CHECK(load_bytes(g_bytes, len, &d) != 0, "count > cap must be rejected");
    bad = 13u; memcpy(m + sec + 12, &bad, 4);
    CHECK(load_bytes(g_bytes, len, &d) != 0, "count beyond file must be rejected");
    bad = 11u; memcpy(m + sec + 12, &bad, 4);
    CHECK(load_bytes(g_bytes, len, &d) != 0, "trailing bytes must be rejected");
    bad = 12u; memcpy(m + sec + 12, &bad, 4);
    {
        uint64_t s = 5u;
        memcpy(m + sec + 24 + 48, &s, 8);
        CHECK(load_bytes(g_bytes, len, &d) != 0, "non-consecutive seq must be rejected");
        s = 1u;
        memcpy(m + sec + 24 + 48, &s, 8);
    }
    {
        uint16_t k = 0u;
        memcpy(m + sec + 24 + 16, &k, 2);
        CHECK(load_bytes(g_bytes, len, &d) != 0, "kind 0 must be rejected");
        k = 11u;
        memcpy(m + sec + 24 + 16, &k, 2);
        CHECK(load_bytes(g_bytes, len, &d) != 0, "kind past KIND_COUNT must be rejected");
        k = TRACE_IN_KEY_DOWN;
        memcpy(m + sec + 24 + 16, &k, 2);
    }
    CHECK(load_bytes(g_bytes, len, &d) == 0, "restored dump must load again");
    trace_fmt_dump_free(&d);
    {
        uint32_t tag = 0x58585858u;
        memcpy(m + sec, &tag, 4);
        CHECK(load_bytes(g_bytes, len, &d) != 0, "unknown section must be rejected");
    }
}

static void test_no_malloc(void) {
    size_t mallocs;
    trace_reset();
    edit_malloc_guard_begin();
    for (uint32_t i = 0; i < 10000u; i++) {
        switch (i % 4u) {
        case 0: trace_input_key(IN_BASE + i, TRACE_IN_KEY_DOWN, 0x61u, 0, 0, "a", 1); break;
        case 1: trace_input_pointer(IN_BASE + i, TRACE_IN_POINTER_MOVE, (int32_t)i, 3, 0, 0); break;
        case 2: trace_input_resize(IN_BASE + i, i, i); break;
        default: trace_input_clipboard(IN_BASE + i, 1, i); break;
        }
    }
    mallocs = edit_malloc_guard_end();
    CHECK(trace_input_count() == 10000u, "no-malloc count");
    if (edit_malloc_guard_active()) {
        CHECK(mallocs == 0, "input append did %zu mallocs in 10000 events", mallocs);
        printf("input append: %zu mallocs in 10000 events\n", mallocs);
    } else {
        printf("input append: malloc guard inactive (ASan build), count check skipped\n");
    }
}

int main(void) {
    pthread_t a, b;
    FILE *f;
    trace_rec *recs = NULL;
    size_t n = 0;

    trace_init();
    CHECK(pthread_create(&a, NULL, thread_a, NULL) == 0, "create a");
    CHECK(pthread_create(&b, NULL, thread_b, NULL) == 0, "create b");
    pthread_join(a, NULL);
    pthread_join(b, NULL);

    /* Exercise the real clock path on the main thread (not part of the gates). */
    trace_thread_register();
    trace_record(TRACE_T1_DEQUEUE, 999999u);

    f = tmpfile();
    CHECK(f != NULL, "tmpfile");
    CHECK(trace_dump(f) == 0, "trace_dump");
    rewind(f);
    CHECK(trace_fmt_load(f, &recs, &n) == 0, "reload");
    fclose(f);
    CHECK(n == 2u * NFRAMES * 2u + 1u, "record count %zu", n);

    check_gate(recs, n, TRACE_T0_INGRESS, TRACE_T4_PRESENT_SUBMITTED, "G1 T4-T0", 50000u, 99000u);
    check_gate(recs, n, TRACE_T0_INGRESS, TRACE_T5_DEVICE_DONE, "G3 T5-T0", 100000u, 198000u);
    check_gate(recs, n, TRACE_T0_INGRESS, TRACE_T6_PRESENT_COMPLETE, "T6-T0", 150000u, 297000u);

    {
        uint64_t t = trace_now_ns();
        CHECK(t > BASE_NS, "trace_now_ns not monotonic-looking");
    }
    free(recs);

    test_ring_append_and_wrap();
    test_seq_monotonic_and_fields();
    test_dump_roundtrip();
    test_dump_roundtrip_wrapped();
    test_legacy_v1_loads();
    test_malformed_rejected();
    test_no_malloc();

    trace_reset();
    if (g_fail) {
        printf("trace_test: FAIL\n");
        return 1;
    }
    printf("trace_test: PASS\n");
    return 0;
}
