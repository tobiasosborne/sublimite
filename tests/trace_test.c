#include "trace/trace.h"
#include "assert_death.h"

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static void open_scratch(os_file *f) {
    char path[] = "build/trace-test-XXXXXX";
    int fd = mkstemp(path);
    ASSERT(fd >= 0 && unlink(path) == 0);
    *f = os_file_from_fd(fd);
}

static void read_test_bytes(os_file *f, char *data, size_t len) {
    os_stat st;
    size_t got;
    ASSERT(os_file_stat(f, &st) == OS_OK && st.size == len);
    ASSERT(os_file_pread(f, data, len, 0, &got) == OS_OK && got == len);
}

static int load_bytes(const char *data, size_t len, trace_loaded *dump) {
    os_file f = OS_FILE_INIT;
    open_scratch(&f);
    if (len) {
        size_t written;
        ASSERT(os_file_write(&f, data, len, &written) == OS_OK && written == len);
    }
    int result = trace_fmt_load_dump(&f, dump);
    os_file_close(&f);
    return result;
}

static trace_loaded load_snapshot(void) {
    os_file f = OS_FILE_INIT;
    open_scratch(&f);
    ASSERT(trace_dump(&f) == 0);
    trace_loaded dump;
    ASSERT(trace_fmt_load_dump(&f, &dump) == 0);
    os_file_close(&f);
    return dump;
}

typedef struct trace_writer {
    uint32_t frame;
    int slot;
} trace_writer;

static void record_one(void *arg) {
    trace_writer *writer = arg;
    writer->slot = trace_thread_register();
    ASSERT(trace_thread_register() == writer->slot);
    trace_record_at(writer->frame, TRACE_T2_MUTATION_DONE, writer->frame);
    trace_thread_release();
    trace_record_at(writer->frame, TRACE_RENDER_REJECTED, writer->frame);
}

static void joined_writer(trace_writer *writer) {
    os_thread thread;
    ASSERT(os_thread_start(&thread, record_one, writer) == OS_OK);
    ASSERT(os_thread_join(&thread) == OS_OK);
}

static void test_sequential_reuse(void) {
    trace_reset();
    /* The first thread beyond capacity must still be able to record. */
    enum { THREADS = TRACE_MAX_THREADS + 1u };
    unsigned registered = 0;
    for (uint32_t frame = 1; frame <= THREADS; frame++) {
        trace_writer writer = {.frame = frame};
        joined_writer(&writer);
        if (writer.slot >= 0) registered++;
    }

    trace_loaded dump = load_snapshot();
    printf("trace sequential reuse: registered %u/%u; dumped %zu/%u\n",
           registered, THREADS, dump.nrecs, THREADS);
    ASSERT(registered == THREADS && dump.nrecs == THREADS);
    for (size_t i = 0; i < dump.nrecs; i++) {
        ASSERT(dump.recs[i].frame_id == i + 1u);
        ASSERT(dump.recs[i].ns == i + 1u);
        ASSERT(dump.recs[i].ev == TRACE_T2_MUTATION_DONE && dump.recs[i].thread == 0);
    }
    trace_fmt_dump_free(&dump);
}

typedef enum trace_race_phase {
    TRACE_RACE_START,
    TRACE_RACE_OCCUPIED,
    TRACE_RACE_CONTENDED,
} trace_race_phase;

/* Relaxed signaling controls the test schedule. The trace slot itself
 * supplies the happens-before edge between the two writers. */
static void record_first(void *arg) {
    _Atomic trace_race_phase *phase = arg;
    ASSERT(trace_thread_register() == 0);
    trace_record_at(1, TRACE_T0_INGRESS, 1);
    atomic_store_explicit(phase, TRACE_RACE_OCCUPIED, memory_order_relaxed);
    while (atomic_load_explicit(phase, memory_order_relaxed) != TRACE_RACE_CONTENDED) (void)sched_yield();
    trace_thread_release();
}

static void record_second(void *arg) {
    _Atomic trace_race_phase *phase = arg;
    while (atomic_load_explicit(phase, memory_order_relaxed) != TRACE_RACE_OCCUPIED) (void)sched_yield();
    ASSERT(trace_thread_register() == 1);
    trace_record_at(2, TRACE_T1_DEQUEUE, 1);
    trace_thread_release();
    atomic_store_explicit(phase, TRACE_RACE_CONTENDED, memory_order_relaxed);

    int slot;
    while ((slot = trace_thread_register()) == 1) {
        trace_thread_release();
        (void)sched_yield();
    }
    ASSERT(slot == 0);
    trace_record_at(3, TRACE_T4_PRESENT_SUBMITTED, 1);
    trace_thread_release();
}

static void test_concurrent_reuse(void) {
    trace_reset();
    _Atomic trace_race_phase phase = TRACE_RACE_START;
    os_thread a, b;
    ASSERT(os_thread_start(&a, record_first, &phase) == OS_OK);
    ASSERT(os_thread_start(&b, record_second, &phase) == OS_OK);
    ASSERT(os_thread_join(&a) == OS_OK);
    ASSERT(os_thread_join(&b) == OS_OK);

    trace_loaded dump = load_snapshot();
    ASSERT(dump.nrecs == 3);
    ASSERT(dump.recs[0].thread == 0 && dump.recs[0].ns == 1);
    ASSERT(dump.recs[1].thread == 0 && dump.recs[1].ns == 3);
    ASSERT(dump.recs[2].thread == 1 && dump.recs[2].ns == 2);
    trace_fmt_dump_free(&dump);
}

static void hold_slot(void *arg) {
    atomic_uint *ready = arg;
    ASSERT(trace_thread_register() >= 0);
    atomic_fetch_add_explicit(ready, 1u, memory_order_relaxed);
    /* Hold the slot until the main thread has checked full-pool behavior. */
    while (atomic_load_explicit(ready, memory_order_relaxed) <= TRACE_MAX_THREADS) (void)sched_yield();
    trace_thread_release();
}

static void test_live_capacity(void) {
    trace_reset();
    atomic_uint ready = 0;
    os_thread threads[TRACE_MAX_THREADS];
    for (size_t i = 0; i < TRACE_MAX_THREADS; i++) {
        ASSERT(os_thread_start(&threads[i], hold_slot, &ready) == OS_OK);
    }
    while (atomic_load_explicit(&ready, memory_order_relaxed) != TRACE_MAX_THREADS) (void)sched_yield();
    ASSERT(trace_thread_register() == -1);
    atomic_fetch_add_explicit(&ready, 1u, memory_order_relaxed);
    for (size_t i = 0; i < TRACE_MAX_THREADS; i++) {
        ASSERT(os_thread_join(&threads[i]) == OS_OK);
    }
    ASSERT(trace_thread_register() == 0);
    trace_thread_release();
}

static void test_sparse_dump_and_reset(void) {
    trace_reset();
    ASSERT(trace_thread_register() == 0);
    trace_record_at(1, TRACE_T2_MUTATION_DONE, 1);
    trace_reset();
    ASSERT(trace_thread_register() == 0);
    trace_writer writer = {.frame = 2};
    joined_writer(&writer);
    ASSERT(writer.slot == 1);
    trace_thread_release();

    trace_loaded dump = load_snapshot();
    ASSERT(dump.nrecs == 1);
    ASSERT(dump.recs[0].thread == 1 && dump.recs[0].frame_id == writer.frame);
    ASSERT(dump.has_input && dump.nin == 0 && dump.in_dropped == 0);
    trace_fmt_dump_free(&dump);

    trace_reset();
    trace_record_at(3, TRACE_STARTUP_FIRST_SUBMIT, 0);
    dump = load_snapshot();
    ASSERT(dump.nrecs == 1 && dump.recs[0].thread == 0);
    ASSERT(dump.recs[0].ev == TRACE_STARTUP_FIRST_SUBMIT && dump.recs[0].ns == 3);
    trace_fmt_dump_free(&dump);
}

static void test_wrap(void) {
    trace_reset();
    trace_record_at(1, TRACE_STARTUP_PROCESS_START, 0);
    trace_record_at(2, TRACE_STARTUP_PROCESS_START, 0);
    ASSERT(trace_thread_register() == 0);

    /* One past capacity drops one event, plus one slot-zero startup stamp. */
    for (uint32_t i = 0; i <= TRACE_RING_CAP; i++) trace_record_at(i, TRACE_T0_INGRESS, i);
    trace_thread_release();
    for (uint32_t i = 0; i <= TRACE_INPUT_CAP; i++) trace_input_resize(i, i, i);

    trace_loaded dump = load_snapshot();
    ASSERT(dump.nrecs == TRACE_RING_CAP && dump.recs[0].ns == 1);
    ASSERT(dump.recs[0].ev == TRACE_STARTUP_PROCESS_START && dump.recs[0].frame_id == 0);
    for (size_t i = 1; i < dump.nrecs; i++) {
        ASSERT(dump.recs[i].ns == i + 1u && dump.recs[i].frame_id == i + 1u);
        ASSERT(dump.recs[i].ev == TRACE_T0_INGRESS && dump.recs[i].thread == 0);
    }
    ASSERT(dump.nin == TRACE_INPUT_CAP && dump.in_dropped == 1);
    for (size_t i = 0; i < dump.nin; i++) {
        ASSERT(dump.in[i].seq == i + 1u && dump.in[i].t0_ns == i + 1u);
        ASSERT(dump.in[i].kind == TRACE_IN_RESIZE && dump.in[i].p.resize.w == i + 1u);
    }
    trace_fmt_dump_free(&dump);
}

static void test_latency_pairs(void) {
    /* Three complete frames yield gaps 1, 2, 3 ns. Duplicate endpoints
     * use the earliest timestamp. Incomplete and backward pairs are skipped. */
    const trace_rec recs[] = {
        {12, 3, TRACE_T0_INGRESS, 0}, {5, 1, TRACE_T0_INGRESS, 0}, {23, 4, TRACE_T0_INGRESS, 0},
        {10, 3, TRACE_T0_INGRESS, 0}, {1, 2, TRACE_T0_INGRESS, 0}, {9, 5, TRACE_T0_INGRESS, 0},
        {7, 1, TRACE_T4_PRESENT_SUBMITTED, 1}, {13, 3, TRACE_T4_PRESENT_SUBMITTED, 1},
        {6, 1, TRACE_T4_PRESENT_SUBMITTED, 1}, {3, 2, TRACE_T4_PRESENT_SUBMITTED, 1},
        {22, 4, TRACE_T4_PRESENT_SUBMITTED, 1}, {9, 6, TRACE_T4_PRESENT_SUBMITTED, 1},
    };
    uint64_t lat[3];
    size_t n;
    size_t nrecs = sizeof recs / sizeof recs[0];
    ASSERT(trace_fmt_latencies(recs, nrecs, TRACE_T0_INGRESS, TRACE_T4_PRESENT_SUBMITTED,
                              lat, sizeof lat / sizeof lat[0], &n) == 0);
    ASSERT(n == 3);
    ASSERT(trace_fmt_pct(lat, n, 50) == 2 && trace_fmt_pct(lat, n, 99) == 3);
    ASSERT(trace_fmt_latencies(recs, nrecs, TRACE_T0_INGRESS, TRACE_T4_PRESENT_SUBMITTED,
                              lat, sizeof lat / sizeof lat[0] - 1u, &n) == -1);
    ASSERT(n == 0);
}

typedef struct trace_sink_cursor {
    const trace_input_rec *next;
    size_t delivered;
} trace_sink_cursor;

static void replay_one(const trace_input_rec *rec, void *arg) {
    trace_sink_cursor *cursor = arg;
    ASSERT(rec == cursor->next);
    cursor->next++;
    cursor->delivered++;
}

static void test_timed_replay(void) {
    /* One millisecond of input spacing (10^6 ns) replays at half that gap
     * at double speed. There is no upper bound on scheduler delay. */
    const uint64_t gap = 1000000u;
    trace_input_rec input[] = {{.t0_ns = 0}, {.t0_ns = gap}};
    trace_replay_opts opts = {.speed = 2.0};
    trace_sink_cursor cursor = {.next = input};
    uint64_t before = os_now_ns();
    ASSERT(trace_replay(input, sizeof input / sizeof input[0], &opts, replay_one, &cursor) == 0);
    ASSERT(cursor.delivered == 2 && os_now_ns() - before >= gap / 2);

    opts.speed = 0.5;
    input[1].t0_ns = UINT64_MAX;
    cursor = (trace_sink_cursor){.next = input};
    ASSERT(trace_replay(input, sizeof input / sizeof input[0], &opts, replay_one, &cursor) == -1);
    ASSERT(cursor.delivered == 1);  /* the second deadline cannot be represented */
}

static void record_fixture_events(void) {
    trace_reset();
    ASSERT(trace_thread_register() == 0);
    trace_record_at(1, TRACE_STARTUP_PROCESS_START, 0);
    trace_record_at(2, TRACE_T0_INGRESS, 1);
    trace_record_at(3, TRACE_T4_PRESENT_SUBMITTED, 1);
    trace_thread_release();
    trace_input_key(2, TRACE_IN_KEY_DOWN, 'a', 1, 1, "abcdefghi", 9);
    trace_input_key(3, TRACE_IN_KEY_UP, 'a', 1, 0, NULL, 0);
    trace_input_pointer(4, TRACE_IN_POINTER_MOVE, -5, 7, 0, 4);
    trace_input_pointer(5, TRACE_IN_BUTTON_DOWN, 10, 20, 1, 0);
    trace_input_pointer(6, TRACE_IN_BUTTON_UP, 10, 20, 1, 0);
    trace_input_wheel(7, 0, -256, 0);
    trace_input_resize(8, 80, 24);
    trace_input_focus(9, 0);
    trace_input_clipboard(10, 1, 4);
    trace_input_filechange(11, 7, 2);
}

/* Wire offsets from EDTRACE1's 24-byte file header, 8-byte ring header,
 * three timing records (startup/ingress/submit), and 24-byte INPT header. */
enum {
    TIMING_END = 24 + 8 + 3 * sizeof(trace_rec),
    INPUT_START = TIMING_END + 24,
    FIXTURE_INPUT_COUNT = TRACE_IN_FILECHANGE,  /* one of each v1 kind, key through file change */
    FIXTURE_SIZE = INPUT_START + FIXTURE_INPUT_COUNT * sizeof(trace_input_rec),
};

static void test_legacy_v1_loads(char fixture[FIXTURE_SIZE]) {
    int fd = open("fuzz/corpus/trace/v1-6fec2e8.edtrace", O_RDONLY | O_CLOEXEC);
    ASSERT(fd >= 0);
    os_file f = os_file_from_fd(fd);
    read_test_bytes(&f, fixture, FIXTURE_SIZE);
    trace_loaded dump;
    ASSERT(trace_fmt_load_dump(&f, &dump) == 0);
    char first_byte;
    size_t got;
    ASSERT(os_file_read(&f, &first_byte, 1, &got) == OS_OK && got == 1);
    ASSERT(first_byte == fixture[0]);  /* loader leaves the caller's position alone */
    os_file_close(&f);
    ASSERT(dump.nrecs == 3 && dump.recs[0].ev == TRACE_STARTUP_PROCESS_START);
    ASSERT(dump.recs[1].ns == 2 && dump.recs[1].ev == TRACE_T0_INGRESS);
    ASSERT(dump.recs[2].ns == 3 && dump.recs[2].ev == TRACE_T4_PRESENT_SUBMITTED);
    ASSERT(dump.has_input && dump.in_dropped == 0 && dump.nin == FIXTURE_INPUT_COUNT);
    for (size_t i = 0; i < dump.nin; i++) {
        ASSERT(dump.in[i].seq == i && dump.in[i].kind == i + 1u);
    }
    ASSERT(dump.in[0].p.key.utf8_len == sizeof dump.in[0].p.key.utf8);
    ASSERT(memcmp(dump.in[0].p.key.utf8, "abcdefgh", sizeof dump.in[0].p.key.utf8) == 0);
    ASSERT(dump.in[2].p.pointer.x == -5 && dump.in[5].p.wheel.dy == -256);
    ASSERT(dump.in[6].p.resize.w == 80 && dump.in[7].p.focus.focused == 0);
    ASSERT(dump.in[8].p.clipboard.length == 4 && dump.in[9].p.filechange.watch_id == 7);

    trace_sink_cursor cursor = {.next = dump.in};
    const trace_replay_opts fast = {.fast = 1, .speed = 1.0};
    ASSERT(trace_replay(dump.in, dump.nin, &fast, replay_one, &cursor) == 0);
    ASSERT(cursor.delivered == dump.nin);
    trace_fmt_dump_free(&dump);

    /* Before INPT was introduced the dump ended at this ring boundary. */
    ASSERT(load_bytes(fixture, TIMING_END, &dump) == 0);
    ASSERT(dump.nrecs == 3 && !dump.has_input && dump.in == NULL && dump.nin == 0);
    trace_fmt_dump_free(&dump);

    record_fixture_events();
    open_scratch(&f);
    ASSERT(trace_dump(&f) == 0);
    char bytes[FIXTURE_SIZE];
    read_test_bytes(&f, bytes, sizeof bytes);
    os_file_close(&f);
    ASSERT(memcmp(bytes, fixture, FIXTURE_SIZE) == 0);
}

static void reject_bytes(const char *data, size_t len) {
    trace_loaded dump;
    ASSERT(load_bytes(data, len, &dump) == -1);
    ASSERT(dump.recs == NULL && dump.in == NULL && dump.nrecs == 0 &&
           dump.nin == 0 && !dump.has_input && dump.in_dropped == 0 && dump.storage.base == NULL);
}

static void test_malformed(char fixture[FIXTURE_SIZE]) {
    /* Every strict prefix except the timing-only boundary cuts a wire
     * field or a declared record. */
    for (size_t len = 0; len < FIXTURE_SIZE; len++) {
        if (len != TIMING_END) reject_bytes(fixture, len);
    }

    char bytes[FIXTURE_SIZE + 1];
    memcpy(bytes, fixture, FIXTURE_SIZE);
    bytes[FIXTURE_SIZE] = 0;
    reject_bytes(bytes, sizeof bytes);  /* trailing byte */
    const struct {
        size_t offset;
        uint32_t invalid;
    } cases[] = {
        {0, 0},                           /* magic */
        {8, 0},                           /* version */
        {12, TRACE_MAX_THREADS + 1u},      /* ring count */
        {16, 0},                          /* timing record size */
        {20, 0},                          /* timing capacity */
        {24, TRACE_MAX_THREADS},          /* slot */
        {28, TRACE_RING_CAP + 1u},         /* declared block count */
        {TIMING_END, 0},                  /* tag */
        {TIMING_END + 4, 0},              /* INPT version */
        {TIMING_END + 8, 0},              /* INPT record size */
        {TIMING_END + 12, TRACE_INPUT_CAP + 1u},
        {TIMING_END + 12, TRACE_IN_KIND_COUNT},
        {TIMING_END + 12, TRACE_IN_KIND_COUNT - 2u},
        {INPUT_START + 16, TRACE_IN_INVALID},
        {INPUT_START + 16, TRACE_IN_KIND_COUNT},
        {INPUT_START + sizeof(trace_input_rec), 0},      /* repeated sequence */
        {INPUT_START + sizeof(trace_input_rec) + 8, 0},  /* decreasing timestamp */
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memcpy(bytes, fixture, FIXTURE_SIZE);
        memcpy(bytes + cases[i].offset, &cases[i].invalid, sizeof cases[i].invalid);
        reject_bytes(bytes, FIXTURE_SIZE);
    }
    memcpy(bytes, fixture, FIXTURE_SIZE);
    bytes[INPUT_START + offsetof(trace_input_rec, p.key.utf8_len)] = 9;
    reject_bytes(bytes, FIXTURE_SIZE);
    memcpy(bytes, fixture, FIXTURE_SIZE);
    memset(bytes + TIMING_END + 16, 0xff, sizeof(uint64_t));
    reject_bytes(bytes, FIXTURE_SIZE);
}

/* A percentile outside 1..100 is a caller bug (A15). */
static void percentile_zero(void) {
    uint64_t value = 1;
    (void)trace_fmt_pct(&value, 1, 0);
}

int main(void) {
    char fixture[FIXTURE_SIZE];
    /* Before any thread exists: the death child forks a one-thread process. */
    ASSERT(assert_death(percentile_zero, "src/trace/trace_stats.c", "pct >= 1 && pct <= 100"));
    test_sequential_reuse();
    test_concurrent_reuse();
    test_live_capacity();
    test_sparse_dump_and_reset();
    test_wrap();
    test_latency_pairs();
    test_timed_replay();
    test_legacy_v1_loads(fixture);
    test_malformed(fixture);
    puts("trace_test: PASS");
    return 0;
}
