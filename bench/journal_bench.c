/* journal runtime bench (L2-2b), ported from v1 bench/journal_bench.c at
 * the oracle pin and extended with the two A22 measurements. Every line
 * is an observation stamped (M)[bat]/[AC] with load; nothing here gates
 * (CLAUDE.md law 1). Modes:
 *   (none)          typing append p50/p99; UI work per record size (the
 *                   INSERT slice derivation); worker lease duration per
 *                   text size (the self-contained size derivation).
 *   --g1            Measurement 1 (A22, Q6): input-to-submit with the
 *                   append, ordinary typing, then under heavy writeback
 *                   from a child process the bench owns and reaps.
 *   --recovery-10g  Measurement 2 (A22, Q3): recovery of a
 *                   mixed-generation image with repeated ranges over a
 *                   10 GiB BASE.
 * Fixtures live only under the checkout's build/; both measurements run
 * df first and refuse to start without the fixture plus a margin free,
 * and remove their fixtures on success and on failure. Links the piece
 * model until the real tree lands on v2 (stated in each line). */
#include "base/base.h"
#include "journal/journal.h"
#include "os/os.h"
#include "undo/undo.h"
#include "work/work.h"

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "journal_bench:%d FAIL %s\n", __LINE__, #x); \
    cleanup(); exit(1); } } while (0)

static char dir_path[256];
static os_dir dir = OS_DIR_INIT;
static pid_t loader = -1;
static work_pool pool __attribute__((aligned(64)));

static void cleanup(void)
{
    if (loader > 0) {
        (void)kill(loader, SIGKILL);
        int st;
        while (waitpid(loader, &st, 0) < 0 && errno == EINTR) {}
        loader = -1;
    }
    if (!os_dir_is_open(&dir)) return;
    os_dir_iter it;
    if (os_dir_iter_open(&it, &dir) == OS_OK) {
        const char *name;
        char names[32][256];
        size_t n = 0;
        while (os_dir_next(&it, &name) == OS_OK && name && n < 32) strcpy(names[n++], name);
        os_dir_iter_close(&it);
        for (size_t i = 0; i < n; i++) (void)os_unlink_at(&dir, names[i]);
    }
    os_dir_close(&dir);
    (void)os_rmdir(dir_path);
}

static void stamp(const char *row)
{
    char power[32] = "unknown", load[32] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) {
        if (fgets(power, sizeof power, f)) power[strcspn(power, "\n")] = 0;
        fclose(f);
    }
    f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%31s", load) != 1) strcpy(load, "unknown");
        fclose(f);
    }
    const char *tag = strcmp(power, "Discharging") == 0 ? "bat" : "AC";
    printf("(M)[%s] row=%s BAT0=%s load1=%s piece=model\n", tag, row, power, load);
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static void report(const char *label, uint64_t *s, size_t n)
{
    qsort(s, n, sizeof *s, cmp_u64);
    printf("%s p50=%llu ns p99=%llu ns max=%llu ns (n=%zu)\n", label,
           (unsigned long long)s[n / 2], (unsigned long long)s[(n * 99) / 100],
           (unsigned long long)s[n - 1], n);
}

static void make_dir(const char *tag)
{
    (void)os_mkdir("build");
    int n = snprintf(dir_path, sizeof dir_path, "build/journal-bench-%s-%ld", tag, (long)getpid());
    REQUIRE(n > 0 && (size_t)n < sizeof dir_path);
    REQUIRE(os_mkdir(dir_path) == OS_OK);
    REQUIRE(os_dir_open(&dir, dir_path) == OS_OK);
}

/* Refuses to start unless need + margin bytes are free on build/. */
static void disk_hygiene(const char *what, uint64_t need, uint64_t margin)
{
    struct statvfs v;
    REQUIRE(statvfs("build", &v) == 0);
    uint64_t avail = (uint64_t)v.f_bavail * v.f_frsize;
    printf("df build/: %llu B free; %s needs %llu B + margin %llu B\n", (unsigned long long)avail,
           what, (unsigned long long)need, (unsigned long long)margin);
    if (avail < need + margin) {
        fprintf(stderr, "journal_bench: refusing %s: not enough free space\n", what);
        cleanup();
        exit(2);
    }
}

static void ignore(const work_msg *m, void *ud) { (void)m; (void)ud; }

static void settle(journal *j)
{
    for (;;) {
        (void)work_mailbox_drain(&pool, ignore, NULL);
        (void)journal_pump(j, os_now_ns());
        if (journal_get_status(j).transport == JOURNAL_TRANSPORT_IDLE) return;
    }
}

static journal *open_journal(const char *name, uint64_t sync_bytes, uint64_t interval)
{
    journal *j;
    journal_options o = {.budget_bytes = UINT64_C(1) << 40, .sync_bytes = sync_bytes,
                         .sync_interval_ns = interval};
    REQUIRE(journal_open(&j, &dir, name, &pool, NULL, &o) == JOURNAL_OK);
    uint8_t base[16];
    size_t bn = 0;
    journal_base b = {.generation_key = 0, .content_id = 1};
    REQUIRE(journal_base_encode(base, sizeof base, &bn, &b) == JOURNAL_CODEC_OK);
    REQUIRE(journal_append(j, JOURNAL_REC_BASE, 1, base, bn) == JOURNAL_OK);
    return j;
}

static void close_journal(journal *j)
{
    for (;;) {
        (void)work_mailbox_drain(&pool, ignore, NULL);
        if (journal_close(j) == JOURNAL_OK) return;
        (void)journal_pump(j, os_now_ns());
    }
}

/* (G) v1's barrier policy (journal.h at the pin: 64 KiB or 1 s), the
 * policy a session is expected to run with. */
#define POLICY_SYNC_BYTES (64u * 1024u)
#define POLICY_SYNC_NS UINT64_C(1000000000)

/* ---- default mode ---- */
/* (G) v1's SESSION_EDITS. */
#define TYPING_EDITS 100000u
static uint64_t samples[TYPING_EDITS];

static void bench_typing(void)
{
    journal *j = open_journal("typing.sjnl", POLICY_SYNC_BYTES, POLICY_SYNC_NS);
    stamp("typing_append");
    for (uint32_t i = 0; i < TYPING_EDITS; i++) {
        uint8_t ch = (uint8_t)('a' + i % 26);
        size_t progress = 0;
        uint64_t t0 = os_now_ns();
        REQUIRE(journal_insert_step(j, 1, i, &ch, 1, &progress) == JOURNAL_OK);
        samples[i] = os_now_ns() - t0;
        if ((i & 255u) == 255u) {
            (void)work_mailbox_drain(&pool, ignore, NULL);
            (void)journal_pump(j, os_now_ns());
        }
    }
    report("typing journal_insert_step (1 B, one pwrite_once)", samples, TYPING_EDITS);
    settle(j);
    close_journal(j);
}

/* UI work per record of n literal bytes: encode + CRC + one
 * pwrite_once, the cost one insert_step call pays per slice. */
static void bench_record_sizes(void)
{
    static uint8_t payload[JOURNAL_MAX_RECORD_BYTES];
    static const size_t sizes[] = {4096, 16384, 65536, 262144, 524288, 1048536};
    stamp("record_size");
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        journal *j = open_journal("sizes.sjnl", UINT64_C(1) << 40, UINT64_MAX);
        size_t n = sizes[k];
        memset(payload, 0, 8);
        memset(payload + 8, 'q', n);
        /* (G) 1,000 records: p99 is the 10th largest; at most 1 GiB of
         * page cache for the largest, removed after each size. */
        enum { REPS = 1000 };
        static uint64_t s[REPS];
        for (unsigned r = 0; r < REPS; r++) {
            uint64_t t0 = os_now_ns();
            REQUIRE(journal_append(j, JOURNAL_REC_INSERT, 1, payload, 8 + n) == JOURNAL_OK);
            s[r] = os_now_ns() - t0;
        }
        char label[96];
        snprintf(label, sizeof label, "record with %zu literal B", n);
        report(label, s, REPS);
        printf("  -> %.3f B/ns at p50, %.3f B/ns at p99\n", (double)n / (double)s[REPS / 2],
               (double)n / (double)s[(REPS * 99) / 100]);
        close_journal(j);
        REQUIRE(os_unlink_at(&dir, "sizes.sjnl") == OS_OK);
    }
}

/* One insert_step call at the slice: the typing path's largest unit. */
static void bench_slice(void)
{
    static uint8_t big[JOURNAL_INSERT_SLICE_BYTES];
    memset(big, 's', sizeof big);
    journal *j = open_journal("slice.sjnl", UINT64_C(1) << 40, UINT64_MAX);
    stamp("insert_step_slice");
    enum { REPS = 1000 }; /* (G) as record_size */
    static uint64_t s[REPS];
    for (unsigned r = 0; r < REPS; r++) {
        size_t progress = 0;
        uint64_t t0 = os_now_ns();
        REQUIRE(journal_insert_step(j, 1, 0, big, sizeof big, &progress) == JOURNAL_OK);
        s[r] = os_now_ns() - t0;
    }
    char label[96];
    snprintf(label, sizeof label, "journal_insert_step of one %u B slice", JOURNAL_INSERT_SLICE_BYTES);
    report(label, s, REPS);
    close_journal(j);
    REQUIRE(os_unlink_at(&dir, "slice.sjnl") == OS_OK);
}

/* The lease a self-contained checkpoint of n bytes holds the append
 * position for: from journal_checkpoint_start to the adoption that
 * returns BUSY appends to OK, as the UI sees it. */
static void bench_lease(piece_reserve *r)
{
    static uint8_t text[8u << 20];
    for (size_t i = 0; i < sizeof text; i++) text[i] = (uint8_t)(' ' + i % 90);
    static const size_t sizes[] = {65536, 131072, 196608, 262144, 393216, 524288, 1048576, 4194304};
    stamp("checkpoint_lease");
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        journal *j = open_journal("lease.sjnl", UINT64_C(1) << 40, UINT64_MAX);
        piece_tree *t;
        REQUIRE(piece_open_copy(r, text, sizes[k], &t) == PIECE_OK);
        piece_snapshot *snap;
        REQUIRE(piece_snapshot_take(t, &snap) == PIECE_OK);
        /* (G) 1,000 leases: p99 is then the 10th largest, not the 2nd. */
        enum { REPS = 1000 };
        static uint64_t s[REPS];
        for (unsigned rep = 0; rep < REPS; rep++) {
            journal_checkpoint_request req = {.buffer_id = 1, .text = snap,
                                              .covered_sequence = journal_get_status(j).written_sequence};
            uint64_t t0 = os_now_ns();
            REQUIRE(journal_checkpoint_start(j, &req) == JOURNAL_OK);
            while (journal_get_status(j).transport == JOURNAL_TRANSPORT_LEASE)
                (void)work_mailbox_drain(&pool, ignore, NULL);
            s[rep] = os_now_ns() - t0;
            settle(j);
        }
        char label[96];
        snprintf(label, sizeof label, "lease for a %zu B checkpoint", sizes[k]);
        report(label, s, REPS);
        printf("  -> %.3f B/ns at p50, %.3f B/ns at p99\n", (double)sizes[k] / (double)s[REPS / 2],
               (double)sizes[k] / (double)s[(REPS * 99) / 100]);
        piece_snapshot_release(snap);
        piece_close(t);
        close_journal(j);
        REQUIRE(os_unlink_at(&dir, "lease.sjnl") == OS_OK);
    }
}

/* ---- Measurement 1: input-to-submit with the append ---- */
/* (G) 10,000 keystrokes per scenario at one per 2 ms: 20 s each, faster
 * than any typist so the page cache never idles cold between keys. */
#define G1_KEYS 10000u
#define G1_GAP_NS 2000000u
static uint64_t g1[G1_KEYS], g1_append[G1_KEYS];

static uint64_t meminfo_kb(const char *key)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[256];
    uint64_t v = 0;
    while (f && fgets(line, sizeof line, f))
        if (strncmp(line, key, strlen(key)) == 0) v = strtoull(line + strlen(key), NULL, 10);
    if (f) fclose(f);
    return v;
}

static uint64_t read_u64_file(const char *path)
{
    FILE *f = fopen(path, "r");
    unsigned long long v = 0;
    if (f) {
        if (fscanf(f, "%llu", &v) != 1) v = 0;
        fclose(f);
    }
    return v;
}

/* The writeback load: a child the bench owns, sweeping a file larger
 * than the dirty limit with 1 MiB writes and no sync, so the kernel is
 * always writing back and throttling writers. The file is unlinked as
 * soon as it is open: no failure can leave it behind. */
static void start_writeback(uint64_t sweep)
{
    loader = fork();
    REQUIRE(loader >= 0);
    if (loader) return;
    os_file f = OS_FILE_INIT;
    if (os_file_open_at(&f, &dir, "writeback.bin", OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_EXCL) != OS_OK)
        _exit(1);
    (void)os_unlink_at(&dir, "writeback.bin");
    static uint8_t chunk[1u << 20];
    memset(chunk, 0xa5, sizeof chunk);
    for (uint64_t off = 0;; off = (off + sizeof chunk) % sweep) {
        size_t done = 0;
        if (os_file_pwrite(&f, chunk, sizeof chunk, off, &done) != OS_OK) _exit(1);
        chunk[0]++;
    }
}

static void g1_scenario(const char *row, piece_reserve *r)
{
    journal *j = open_journal("g1.sjnl", POLICY_SYNC_BYTES, POLICY_SYNC_NS);
    piece_tree *t;
    REQUIRE(piece_open_copy(r, NULL, 0, &t) == PIECE_OK);
    undo_log *u;
    REQUIRE(undo_create(&u, t, SIZE_MAX, NULL, NULL) == UNDO_OK);
    stamp(row);
    const undo_state st = {0};
    bool guard = base_malloc_guard_active();
    size_t mallocs = 0;
    for (uint32_t i = 0; i < G1_KEYS; i++) {
        uint64_t next = os_now_ns() + G1_GAP_NS;
        uint8_t ch = (uint8_t)('a' + i % 26);
        uint64_t len = piece_length(piece_tree_view(t));
        if (guard) base_malloc_guard_begin();
        uint64_t t0 = os_now_ns(); /* input */
        REQUIRE(undo_begin(u, &st, true) == UNDO_OK);
        REQUIRE(undo_replace(u, len, 0, &ch, 1) == UNDO_OK);
        REQUIRE(undo_commit(u, &st) == UNDO_OK);
        uint64_t t1 = os_now_ns();
        size_t progress = 0;
        REQUIRE(journal_insert_step(j, 1, len, &ch, 1, &progress) == JOURNAL_OK);
        uint64_t t2 = os_now_ns(); /* submit: the edit is in the page cache */
        if (guard) mallocs += base_malloc_guard_end();
        g1[i] = t2 - t0;
        g1_append[i] = t2 - t1;
        /* Outside input-to-submit: the post-submit maintenance. */
        (void)work_mailbox_drain(&pool, ignore, NULL);
        (void)journal_pump(j, os_now_ns());
        (void)piece_replenish(t);
        (void)undo_maintain(u, 64);
        while (os_now_ns() < next) {}
    }
    char label[128];
    snprintf(label, sizeof label, "%s input-to-submit (undo edit + journal append)", row);
    report(label, g1, G1_KEYS);
    snprintf(label, sizeof label, "%s   of which journal append", row);
    report(label, g1_append, G1_KEYS);
    printf("%s malloc guard active=%d count=%zu\n", row, (int)guard, mallocs);
    settle(j);
    undo_destroy(u);
    piece_close(t);
    close_journal(j);
    REQUIRE(os_unlink_at(&dir, "g1.sjnl") == OS_OK);
}

static void measurement_g1(piece_reserve *r)
{
    /* (P) The sweep exceeds the kernel's dirty limit, dirty_ratio % of
     * available memory (MemAvailable approximates dirtyable memory), so
     * the child keeps dirtying fresh pages while writeback runs. */
    uint64_t ratio = read_u64_file("/proc/sys/vm/dirty_ratio");
    uint64_t dirty_bytes = read_u64_file("/proc/sys/vm/dirty_bytes");
    uint64_t avail = meminfo_kb("MemAvailable:") * 1024u;
    uint64_t limit = dirty_bytes ? dirty_bytes : avail / 100u * (ratio ? ratio : 20u);
    uint64_t gib = UINT64_C(1) << 30;
    uint64_t sweep = (limit / gib + 1u) * gib;
    printf("writeback load: dirty limit %llu B (dirty_ratio %llu%%, MemAvailable %llu B) -> sweep %llu B\n",
           (unsigned long long)limit, (unsigned long long)ratio, (unsigned long long)avail,
           (unsigned long long)sweep);
    /* (G) 4 GiB margin: what the rest of the box keeps free during the
     * run, over 14x the build tree's 280 MiB (M, du -sh build after a
     * full make all, 2026-11-24 00:41 CET). */
    disk_hygiene("writeback sweep", sweep, 4u * gib);
    g1_scenario("g1_ordinary", r);
    start_writeback(sweep);
    os_sleep_ns(UINT64_C(5000000000)); /* (G) 5 s for the dirty set to reach its limit */
    printf("Dirty before scenario: %llu kB, Writeback: %llu kB\n",
           (unsigned long long)meminfo_kb("Dirty:"), (unsigned long long)meminfo_kb("Writeback:"));
    g1_scenario("g1_writeback", r);
    printf("Dirty after scenario: %llu kB, Writeback: %llu kB\n",
           (unsigned long long)meminfo_kb("Dirty:"), (unsigned long long)meminfo_kb("Writeback:"));
    (void)kill(loader, SIGKILL);
    int st;
    while (waitpid(loader, &st, 0) < 0 && errno == EINTR) {}
    loader = -1;
}

/* ---- Measurement 2: 10 GiB recovery ---- */
typedef struct mapped {
    os_map map;
    os_fault_id fault;
    _Atomic int owners;
} mapped;

static void map_acquire(void *ctx) { atomic_fetch_add(&((mapped *)ctx)->owners, 1); }
static void map_release(void *ctx)
{
    mapped *m = ctx;
    if (atomic_fetch_sub(&m->owners, 1) == 1) {
        os_fault_retire(m->fault);
        os_unmap(&m->map);
    }
}

static mapped base_map;

/* A mapped opener as L2 file will provide one: map, register, open. */
static piece_err open_mapped(void *ctx, piece_reserve *r, os_file *fd, uint64_t size, piece_tree **out)
{
    (void)ctx;
    if (size < (UINT64_C(1) << 20)) {
        /* small sources: a private copy, as the default opener */
        static uint8_t buf[1u << 20];
        size_t got = 0;
        if (os_file_pread(fd, buf, (size_t)size, 0, &got) != OS_OK || got != size) return PIECE_ERR_NOMEM;
        return piece_open_copy(r, buf, (size_t)size, out);
    }
    mapped *m = &base_map;
    if (os_map_file(&m->map, fd, 0, (size_t)size) != OS_OK) return PIECE_ERR_NOMEM;
    if (os_fault_register(&m->map, &m->fault) != OS_OK) {
        os_unmap(&m->map);
        return PIECE_ERR_NOMEM;
    }
    atomic_store(&m->owners, 1);
    piece_backing b = {.bytes = m->map.addr, .len = (size_t)size, .fault = m->fault, .ctx = m,
                       .acquire = map_acquire, .release = map_release};
    piece_err e = piece_open_mapped(r, &b, out);
    map_release(m); /* the tree holds its own owner now */
    return e;
}

static uint64_t vm_hwm_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    uint64_t v = 0;
    while (f && fgets(line, sizeof line, f))
        if (strncmp(line, "VmHWM:", 6) == 0) v = strtoull(line + 6, NULL, 10);
    if (f) fclose(f);
    return v;
}

static uint64_t rng_next(uint64_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 7;
    *x ^= *x << 17;
    return *x;
}

static void declare(journal *j, const char *name, uint64_t *key)
{
    char path[OS_PATH_MAX], dirabs[OS_PATH_MAX];
    REQUIRE(os_path_canonical(dir_path, dirabs, sizeof dirabs) == OS_OK);
    int n = snprintf(path, sizeof path, "%s/%s", dirabs, name);
    REQUIRE(n > 0 && (size_t)n < sizeof path);
    os_file f = OS_FILE_INIT;
    REQUIRE(os_file_open(&f, path, OS_OPEN_READ) == OS_OK);
    journal_generation g = {0};
    REQUIRE(journal_capture(&f, &g) == OS_OK);
    os_file_close(&f);
    REQUIRE(journal_new_generation_key(j, key) == JOURNAL_OK);
    g.key = *key;
    g.location = (const uint8_t *)path;
    g.location_len = strlen(path);
    static uint8_t payload[8192];
    size_t pn = 0;
    REQUIRE(journal_generation_encode(payload, sizeof payload, &pn, &g) == JOURNAL_CODEC_OK);
    REQUIRE(journal_append(j, JOURNAL_REC_GENERATION, 0, payload, pn) == JOURNAL_OK);
}

static void measurement_recovery(piece_reserve *r)
{
    /* (G) the PLAN large-file class: 10 GiB. The BASE is sparse (its
     * first MiB written, enough for the prefix witness and for nothing
     * else to be read), yet the df check demands its full size so no
     * filesystem without holes can be filled. (G) 16 MiB second
     * generation, written: by-value restores read it. */
    const uint64_t big = UINT64_C(10) << 30, small = UINT64_C(16) << 20;
    disk_hygiene("10 GiB recovery fixture", big + small, UINT64_C(4) << 30);
    os_file f = OS_FILE_INIT;
    REQUIRE(os_file_open_at(&f, &dir, "base10g.bin", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL) == OS_OK);
    static uint8_t chunk[1u << 20];
    for (size_t i = 0; i < sizeof chunk; i++) chunk[i] = (uint8_t)('a' + i % 26);
    size_t done = 0;
    REQUIRE(os_file_pwrite(&f, chunk, sizeof chunk, 0, &done) == OS_OK && done == sizeof chunk);
    REQUIRE(os_file_set_size(&f, big) == OS_OK);
    os_file_close(&f);
    REQUIRE(os_file_open_at(&f, &dir, "gen2.bin", OS_OPEN_RDWR | OS_OPEN_CREATE | OS_OPEN_EXCL) == OS_OK);
    for (uint64_t off = 0; off < small; off += sizeof chunk) {
        REQUIRE(os_file_pwrite(&f, chunk, sizeof chunk, off, &done) == OS_OK && done == sizeof chunk);
    }
    os_file_close(&f);

    journal *j = open_journal("rec.sjnl", UINT64_C(1) << 40, UINT64_MAX);
    uint64_t kbig, ksmall;
    declare(j, "base10g.bin", &kbig);
    declare(j, "gen2.bin", &ksmall);
    uint8_t payload[16];
    size_t pn = 0;
    journal_base bb = {.generation_key = kbig, .content_id = 2};
    REQUIRE(journal_base_encode(payload, sizeof payload, &pn, &bb) == JOURNAL_CODEC_OK);
    REQUIRE(journal_append(j, JOURNAL_REC_BASE, 2, payload, pn) == JOURNAL_OK);
    /* (G) The mixed image: 1,000 of each kind. Deletes and by-reference
     * restores of 1 MiB from 16 repeated ranges of the BASE, by-value
     * restores of 64 KiB from the second generation, 100 B inserts. */
    enum { EACH = 1000, RANGES = 16 };
    uint64_t len = big, x = 0x9e3779b97f4a7c15u, by_value = 0, by_ref = 0;
    for (unsigned i = 0; i < 4u * EACH; i++) {
        uint64_t rnd = rng_next(&x);
        switch (i % 4) {
        case 0: {
            uint64_t n = UINT64_C(1) << 20;
            REQUIRE(journal_append_delete(j, 2, rnd % (len - n), n) == JOURNAL_OK);
            len -= n;
            break;
        }
        case 1: {
            uint64_t n = UINT64_C(1) << 20, src = (rnd % RANGES) * (big / RANGES);
            journal_restore rs = {.destination = (rnd >> 20) % len, .generation_key = kbig,
                                  .source_offset = src, .len = n};
            REQUIRE(journal_append_restore(j, 2, &rs) == JOURNAL_OK);
            len += n;
            by_ref += n;
            break;
        }
        case 2: {
            uint64_t n = 65536, src = (rnd % (small / n)) * n;
            journal_restore rs = {.destination = (rnd >> 20) % len, .generation_key = ksmall,
                                  .source_offset = src, .len = n};
            REQUIRE(journal_append_restore(j, 2, &rs) == JOURNAL_OK);
            len += n;
            by_value += n;
            break;
        }
        case 3: {
            size_t progress = 0;
            REQUIRE(journal_insert_step(j, 2, (rnd >> 8) % len, chunk, 100, &progress) == JOURNAL_OK);
            len += 100;
            break;
        }
        }
    }
    journal_request_sync(j);
    settle(j);
    uint64_t image = journal_get_status(j).file_bytes;
    close_journal(j);
    struct statvfs v0, v1;
    REQUIRE(statvfs("build", &v0) == 0);
    stamp("recovery_10g");
    uint64_t hwm0 = vm_hwm_kb();
    journal_recovery rec;
    uint64_t t0 = os_now_ns();
    REQUIRE(journal_recover(&rec, &dir, "rec.sjnl", r, open_mapped, NULL) == JOURNAL_OK);
    uint64_t t1 = os_now_ns();
    REQUIRE(statvfs("build", &v1) == 0);
    uint64_t hwm1 = vm_hwm_kb();
    const journal_recovered_buffer *b = NULL;
    for (size_t i = 0; i < rec.buffer_count; i++)
        if (rec.buffers[i].id == 2) b = &rec.buffers[i];
    REQUIRE(b && b->verdict == JOURNAL_BUFFER_RECOVERED);
    uint64_t text = piece_length(piece_tree_view(b->tree));
    uint64_t add = piece_add_length(b->tree);
    printf("recovery_10g: verdict=%d time=%llu ns image=%llu B text=%llu B (model %llu B)\n",
           (int)rec.verdict, (unsigned long long)(t1 - t0), (unsigned long long)image,
           (unsigned long long)text, (unsigned long long)len);
    printf("recovery_10g: ADD=%llu B (by-value restores %llu B + inserts %u B; by-reference %llu B took none)\n",
           (unsigned long long)add, (unsigned long long)by_value, 100u * EACH,
           (unsigned long long)by_ref);
    printf("recovery_10g: recovery arena used=%zu B reserved=%zu B\n", rec.arena_.used, rec.arena_.size);
    printf("recovery_10g: RSS peak (VmHWM) before=%llu kB after=%llu kB\n", (unsigned long long)hwm0,
           (unsigned long long)hwm1);
    printf("recovery_10g: temporary disk %lld B (free space before - after)\n",
           (long long)((int64_t)((uint64_t)v0.f_bavail * v0.f_frsize) -
                       (int64_t)((uint64_t)v1.f_bavail * v1.f_frsize)));
    printf("recovery_10g: piece nodes n/a (piece model: a flat span array, no nodes)\n");
    REQUIRE(text == len);
    journal_recovery_release(&rec);
}

int main(int argc, char **argv)
{
    bool g1mode = argc == 2 && strcmp(argv[1], "--g1") == 0;
    bool recmode = argc == 2 && strcmp(argv[1], "--recovery-10g") == 0;
    REQUIRE(argc == 1 || g1mode || recmode);
    REQUIRE(work_pool_init(&pool, 1, 0) == 0);
    piece_reserve *r;
    REQUIRE(piece_reserve_create(&r) == PIECE_OK);
    make_dir(g1mode ? "g1" : recmode ? "rec" : "default");
    if (g1mode) measurement_g1(r);
    else if (recmode) measurement_recovery(r);
    else {
        bench_typing();
        bench_record_sizes();
        bench_slice();
        bench_lease(r);
    }
    piece_reserve_destroy(r);
    work_pool_shutdown(&pool);
    cleanup();
    return 0;
}
