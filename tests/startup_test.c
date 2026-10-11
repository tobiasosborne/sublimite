/* edit-2ts: probe latency must be absent from first paint's dependencies.
 * The watchdog reuses editor_test.c's settle liveness bound: (G) 5 s =
 * 5000000000 ns / 1000000000 ns per second. It is not a startup timing gate. */
#include "editor/private.h"
#include "trace/trace_fmt.h"
#include "base/base.h"
#include <errno.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <poll.h>

#define T(c) do { if (!(c)) { fprintf(stderr, "startup_test: FAIL %s\n", #c); return 1; } } while (0)
#define WATCHDOG_NS UINT64_C(5000000000) /* existing settle watchdog, derived above */
_Static_assert(TRACE_T6_PRESENT_COMPLETE == 6 && TRACE_COMPOSE_BEGIN == 7 &&
               TRACE_COMPOSE_END == 8 && TRACE_STARTUP_PROCESS_START == 9,
               "startup stages append after main's stable timing event IDs");
typedef struct probe {
    char name[sizeof "held EGL probe"];
    sem_t painted, attach_gate;
    atomic_bool entered, finished, timed_out, attached, attach_entered;
    pthread_t ui;
    size_t submits;
    bool succeed, first_submit_while_held;
    int (*null_init)(render_backend *, const render_config *);
} probe;
static int held_probe(render_backend *b, const render_config *cfg)
{
    probe *p = (void *)b->info.name; /* name is the first member of this fixture */
    if (!cfg->background_init) return RENDER_ERR_ARG;
    atomic_store(&p->entered, true);
    struct timespec until; (void)clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += (time_t)(WATCHDOG_NS / UINT64_C(1000000000)); /* SI ns/s */
    int rc;
    do { rc = sem_timedwait(&p->painted, &until); } while (rc && errno == EINTR);
    atomic_store(&p->timed_out, rc != 0);
    int result = p->succeed && !rc ? p->null_init(b, cfg) : RENDER_ERR_UNSUPPORTED;
    atomic_store(&p->finished, true);
    return result;
}
static int finish_probe(render_backend *b)
{
    probe *p = (void *)b->info.name;
    if (!trace_startup_get(TRACE_STARTUP_FIRST_PRESENT) || pthread_equal(pthread_self(), p->ui)) return RENDER_ERR_STATE;
    atomic_store(&p->attach_entered, true);
    struct timespec until; (void)clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += (time_t)(WATCHDOG_NS / UINT64_C(1000000000));
    int rc;
    do { rc = sem_timedwait(&p->attach_gate, &until); } while (rc && errno == EINTR);
    if (rc) return RENDER_ERR_INIT;
    atomic_store(&p->attached, true);
    return RENDER_OK;
}
static void submitted(void *ctx, const editor_frame *frame)
{
    probe *p = ctx; (void)frame;
    if (!p->submits) p->first_submit_while_held = !atomic_load(&p->finished);
    p->submits++;
}
static int settle(editor *e)
{
    uint64_t deadline = trace_now_ns() + WATCHDOG_NS;
    do {
        T(editor_step(e, 0) >= 0);
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return 0;
        T(trace_now_ns() < deadline);
        sched_yield();
    } while (true);
}
static int held_case(bool succeed)
{
    trace_reset();
    /* Harness origin precedes fixture preparation and headless open, not OS
     * exec. The actual executable records its process origin at main entry. */
    trace_record(TRACE_STARTUP_PROCESS_START, 0);
    probe p = {.name = "held EGL probe", .succeed = succeed};
    atomic_init(&p.entered, false); atomic_init(&p.finished, false); atomic_init(&p.timed_out, false); atomic_init(&p.attached, false);
    atomic_init(&p.attach_entered, false); p.ui = pthread_self();
    T(sem_init(&p.painted, 0, 0) == 0); T(sem_init(&p.attach_gate, 0, 0) == 0);
    render_backend b = {0}; T(render_null_backend(&b) == 0);
    p.null_init = b.ops.init;
    b.info.name = p.name; b.info.capabilities |= RENDER_CAP_GPU; b.ops.init = held_probe;
    if (succeed) b.finish_init = finish_probe;
    const uint8_t text[] = "first painted text\n";
    char path[] = "build/startup-source-XXXXXX";
    int fd = mkstemp(path); T(fd >= 0);
    T(write(fd, text, sizeof text - 1) == (ssize_t)(sizeof text - 1)); T(close(fd) == 0);
    editor_config cfg = {.path = path, .cols = 32, .rows = 8, .raster_fallback = true,
        .start_empty = succeed,
        .hook_ctx = &p, .on_submit = submitted}; /* geometry reused from the initial red fixture */
    editor *e = NULL;
    int rc = editor_open(&e, &cfg, &b);
    if (rc) { fprintf(stderr, "startup_test: FAIL editor_open=%d; probe timed_out=%d; first submits=%zu\n",
        rc, atomic_load(&p.timed_out), p.submits); unlink(path); sem_destroy(&p.painted); return 1; }
    if (succeed) { uint64_t id; T(editor_add_buffer(e, path, NULL, 0, &id) == 0); }
    uint64_t deadline = trace_now_ns() + WATCHDOG_NS;
    while (!atomic_load(&p.entered)) { T(trace_now_ns() < deadline); sched_yield(); }
    T(!atomic_load(&p.finished)); T(settle(e) == 0);
    T(p.submits && p.first_submit_while_held && !atomic_load(&p.finished));
    T(b.info.capabilities & RENDER_CAP_HEADLESS); T(!(b.info.capabilities & RENDER_CAP_GPU));
    uint8_t readback[sizeof text]; T(editor_read(e, 0, readback, sizeof text - 1) == 0);
    T(memcmp(text, readback, sizeof text - 1) == 0);
    const render_grid *grid = &e->text_grid; bool ink = false;
    size_t prefix = sizeof "first" - 1; /* actual leading word of the file fixture */
    for (size_t col = 0; col + prefix <= grid->dims.cols; col++) {
        bool match = true;
        for (size_t i = 0; i < prefix; i++)
            if (grid->cells[col + i].glyph_index != text[i] || grid->cells[col + i].atlas_slot == RENDER_NO_SLOT) match = false;
        ink |= match;
    }
    T(ink);
    /* Typing still settles before the held probe is released. The release
     * build has an active process-wide allocation guard; sanitizer is inert. */
    plat_event key = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'x', .utf8 = {'x'}, .utf8_len = 1};
    edit_malloc_guard_begin();
    T(editor_inject(e, &key) == 0); T(settle(e) == 0);
    size_t allocations = edit_malloc_guard_end(); T(allocations == 0);
    T(editor_length(e) == sizeof text); T(!atomic_load(&p.finished));
    uint32_t bootstrap_last = b.last_frame;
    T(sem_post(&p.painted) == 0);
    if (succeed) {
        deadline = trace_now_ns() + WATCHDOG_NS;
        while (!atomic_load(&p.attach_entered)) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield(); }
        T(!(b.info.capabilities & RENDER_CAP_GPU));
        key.keysym = 'y'; key.utf8[0] = 'y';
        edit_malloc_guard_begin(); T(editor_inject(e, &key) == 0); T(settle(e) == 0);
        allocations += edit_malloc_guard_end(); T(allocations == 0);
        T(!atomic_load(&p.attached)); T(sem_post(&p.attach_gate) == 0);
    }
    deadline = trace_now_ns() + WATCHDOG_NS;
    while (e->startup.pending) { T(editor_step(e, 0) >= 0); T(trace_now_ns() < deadline); sched_yield(); }
    T(atomic_load(&p.finished)); T(!atomic_load(&p.timed_out));
    T(settle(e) == 0);
    if (succeed) {
        T(atomic_load(&p.attached));
        T(b.info.capabilities & RENDER_CAP_GPU); T(b.last_frame > bootstrap_last);
        T(e->retired_backend.initialized); T(editor_get_stats(e).backend_init_error == 0);
    } else {
        T(!(b.info.capabilities & RENDER_CAP_GPU)); T(!e->retired_backend.initialized);
        T(editor_get_stats(e).backend_init_error == RENDER_ERR_UNSUPPORTED);
    }
    /* Model work_publish's notification arriving after the UI has consumed
     * its payload and finalized selection. A completed probe must not leave
     * the shared epoll set permanently readable at idle. The eventfd unit is
     * one publication; poll timeout zero means a nonblocking readiness check. */
    uint64_t wake = 1;
    int wake_fd = work_pool_eventfd(&e->startup_pool);
    T(write(wake_fd, &wake, sizeof wake) == (ssize_t)sizeof wake);
    T(editor_step(e, 0) >= 0);
    struct pollfd ready = {wake_fd, POLLIN, 0};
    T(poll(&ready, 1, 0) == 0);
    editor_close(e); T(!b.initialized); T(unlink(path) == 0); T(sem_destroy(&p.painted) == 0); T(sem_destroy(&p.attach_gate) == 0);
    const enum trace_ev required[] = {TRACE_STARTUP_PROCESS_START, TRACE_STARTUP_EGL_PROBE_START,
        TRACE_STARTUP_EGL_PROBE_END, TRACE_STARTUP_RASTER_FALLBACK, TRACE_STARTUP_FONT_ATLAS_READY,
        TRACE_STARTUP_FILE_PREFIX_READ, TRACE_STARTUP_FIRST_LAYOUT, TRACE_STARTUP_FIRST_SUBMIT, TRACE_STARTUP_FIRST_PRESENT};
    for (size_t i = 0; i < sizeof required / sizeof required[0]; i++) T(trace_startup_get(required[i]) != 0);
    T(trace_startup_get(TRACE_STARTUP_FIRST_SUBMIT) < trace_startup_get(TRACE_STARTUP_EGL_PROBE_END));
    T(trace_startup_get(TRACE_STARTUP_FIRST_PRESENT) < trace_startup_get(TRACE_STARTUP_EGL_PROBE_END));
    T(trace_startup_get(TRACE_STARTUP_DISPLAY_CONNECT) == 0); /* no display is opened */
    FILE *dump = fopen("build/startup.trace", "wb"); T(dump != NULL); T(trace_dump(dump) == 0); T(fclose(dump) == 0);
    dump = fopen("build/startup.trace", "rb"); T(dump != NULL);
    trace_loaded loaded; T(trace_fmt_load_dump(dump, &loaded) == 0); T(fclose(dump) == 0);
    trace_fmt_startup_report(stdout, loaded.recs, loaded.nrecs); trace_fmt_dump_free(&loaded);
    if (succeed) puts("startup_test: finish-init runs on worker after bootstrap; typing progresses while finish held; late wake drained");
    printf("startup_test: %s probe: text and typing submitted/presented while held; allocations=%zu guard=%s\n",
        succeed ? "successful" : "failing", allocations, edit_malloc_guard_active() ? "active" : "ASan-inert");
    return 0;
}
static int retained_stamps(void)
{
    trace_reset();
    /* Synthetic timestamps derive from enum positions, not a timing budget. */
    uint64_t origin = (uint64_t)TRACE_EV_COUNT;
    trace_record_at(origin, TRACE_STARTUP_PROCESS_START, 0);
    trace_record_at(origin + (uint64_t)TRACE_STARTUP_FIRST_SUBMIT, TRACE_STARTUP_FIRST_SUBMIT, 0);
    trace_record_at(origin + (uint64_t)TRACE_EV_COUNT, TRACE_STARTUP_FIRST_SUBMIT, 0);
    for (uint32_t i = 0; i < TRACE_RING_CAP; i++) trace_record_at(origin + i, TRACE_T0_INGRESS, i);
    trace_record_at(origin, TRACE_COMPOSE_BEGIN, 1);
    trace_record_at(origin + 1, TRACE_COMPOSE_END, 1);
    T(trace_startup_get(TRACE_COMPOSE_BEGIN) == 0);
    T(trace_startup_get(TRACE_COMPOSE_END) == 0);
    FILE *f = tmpfile(); T(f != NULL); T(trace_dump(f) == 0); rewind(f);
    trace_loaded loaded; T(trace_fmt_load_dump(f, &loaded) == 0); T(fclose(f) == 0);
    size_t found = 0;
    for (size_t i = 0; i < loaded.nrecs; i++) if (loaded.recs[i].ev == TRACE_STARTUP_FIRST_SUBMIT) {
        T(loaded.recs[i].ns == origin + (uint64_t)TRACE_STARTUP_FIRST_SUBMIT); found++;
    }
    T(found == 1); /* derived: first-write-only startup stamp for this stage */
    size_t compose = 0;
    for (size_t i = 0; i < loaded.nrecs; i++) {
        if (loaded.recs[i].ev == TRACE_COMPOSE_BEGIN || loaded.recs[i].ev == TRACE_COMPOSE_END) {
            T(loaded.recs[i].frame_id == 1); compose++;
        }
    }
    T(compose == 2); /* composition remains in the frame ring, outside startup */
    trace_fmt_dump_free(&loaded); trace_reset(); T(trace_startup_get(TRACE_STARTUP_FIRST_SUBMIT) == 0);
    puts("startup_test: startup stamps survive frame-ring wrap and round-trip; reset clears them");
    return 0;
}
int main(void)
{
    char battery[64] = "unknown";
    FILE *status = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (status) { if (fgets(battery, sizeof battery, status)) battery[strcspn(battery, "\n")] = 0; fclose(status); }
    printf("startup measurement stamp (M)%s: BAT0=%s; origin=headless open harness; ms=ns/1000000 (SI)\n",
        !strcmp(battery, "Discharging") ? "[bat]" : !strcmp(battery, "unknown") ? "[unknown]" : "[AC]", battery); fflush(stdout);
    trace_init(); T(trace_thread_register() >= 0);
    T(held_case(false) == 0); T(held_case(true) == 0); T(retained_stamps() == 0);
    return 0;
}
