/* Shared, non-gating scrolling contract. Storage/I/O stay outside submit. */
#ifndef EDIT_RENDER_PACE_H
#define EDIT_RENDER_PACE_H
#include "harness.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#define RENDER_PACE_FRAMES 600u
#define RENDER_PACE_STAGES 24u
typedef struct render_pace_stamp { char power[32], load[32]; } render_pace_stamp;
typedef struct render_pace_history {
    uint64_t previous_msc, previous_ns, missed, invalid, dropped, longest_ns;
    size_t frames, msc_intervals;
    bool baseline, msc_baseline, drops_available;
} render_pace_history;
typedef struct render_pace_frame {
    uint64_t msc, complete_ns, stage[RENDER_PACE_STAGES];
    bool msc_available, dropped, drops_available;
} render_pace_frame;
typedef int (*render_pace_frame_fn)(void *, bool, render_pace_frame *);

static inline const char *render_pace_skip_reason(const char *display)
{
    if (display == NULL || *display == '\0') return "no_DISPLAY";
    const char *colon = strrchr(display, ':');
    if (colon != NULL && strncmp(colon + 1, "99", 2) == 0 &&
        (colon[3] == '\0' || colon[3] == '.')) return "Xvfb_:99_has_no_real_vblank";
    return NULL;
}
static inline void render_pace_observe(render_pace_history *h, const render_pace_frame *f)
{
    h->drops_available = h->baseline ? h->drops_available && f->drops_available : f->drops_available;
    if (h->baseline) {
        h->frames++;
        if (f->complete_ns > h->previous_ns) {
            uint64_t interval = f->complete_ns - h->previous_ns;
            if (interval > h->longest_ns) h->longest_ns = interval;
        } else h->invalid++;
    }
    h->previous_ns = f->complete_ns;
    h->baseline = true;
    if (f->dropped) h->dropped++;
    if (!f->msc_available) { h->msc_baseline = false; return; }
    if (h->msc_baseline) {
        h->msc_intervals++;
        if (f->msc <= h->previous_msc) { h->invalid++; return; }
        h->missed += f->msc - h->previous_msc - 1;
    } else if (!f->msc) { h->invalid++; return; }
    h->previous_msc = f->msc;
    h->msc_baseline = true;
}

static inline void render_pace_stamp_read(render_pace_stamp *stamp)
{
    bench_battery_status(stamp->power, sizeof stamp->power);
    strcpy(stamp->load, "unknown");
    FILE *fp = fopen("/proc/loadavg", "r");
    if (fp != NULL) {
        if (fscanf(fp, "%31s", stamp->load) != 1) strcpy(stamp->load, "unknown");
        fclose(fp);
    }
}
static inline void render_pace_skip(FILE *fp, const char *name, const char *reason)
{
    render_pace_stamp stamp; render_pace_stamp_read(&stamp);
    fprintf(fp, "TRACK name=%s status=SKIP verdict=TRACK reason=%s frames=0 requested_frames=%u"
        " evidence=(M)%s power_status=\"%s\" load1=%s\n", name, reason,
        RENDER_PACE_FRAMES, bench__tag_from_power(stamp.power), stamp.power, stamp.load);
}
static inline void render_pace_stage_report(FILE *fp, const char *name, const char *stage,
                                           const bench_samples *s, const render_pace_stamp *stamp)
{
    fprintf(fp, "TRACK name=%s stage=%s status=TRACK verdict=TRACK n=%zu p50_ns=%" PRIu64
        " p99_ns=%" PRIu64 " evidence=(M)%s power_status=\"%s\" load1=%s\n",
        name, stage, s->n, bench_p50(s), bench_p99(s),
        bench__tag_from_power(stamp->power), stamp->power, stamp->load);
}
static inline void render_pace_summary(FILE *fp, const char *name, const render_pace_history *h,
                                      const render_pace_stamp *stamp, const char *error)
{
    fprintf(fp, "TRACK name=%s status=%s verdict=TRACK reason=%s frames=%zu requested_frames=%u"
        " msc_intervals=%zu missed_refreshes=", name, error ? "ERROR" : "TRACK",
        error ? error : "refresh_paced", h->frames, RENDER_PACE_FRAMES, h->msc_intervals);
    if (h->msc_intervals != 0) fprintf(fp, "%" PRIu64, h->missed);
    else fputs("unavailable", fp);
    fprintf(fp, " invalid=%" PRIu64 " dropped=", h->invalid);
    if (h->drops_available) fprintf(fp, "%" PRIu64, h->dropped);
    else fputs("unavailable", fp);
    fprintf(fp, " longest_stall_ms=%.3f"
        " stall_clock=completion_observation_MONOTONIC evidence=(M)%s power_status=\"%s\" load1=%s\n",
        (double)h->longest_ns / 1000000.0,
        bench__tag_from_power(stamp->power), stamp->power, stamp->load);
}
/* One untimed displayed baseline, then exactly 600 full-grid scrolls. Each
 * callback waits for its matching completion before the next submission:
 * Present (no ASYNC) / EGL interval 1, never a software sleep or fixed Hz. */
static inline void render_pace_run(FILE *fp, const char *name, const char *const *names,
                                  size_t stages, render_pace_frame_fn frame, void *user)
{
    render_pace_stamp stamp; render_pace_stamp_read(&stamp);
    render_pace_history h = {0};
    if (stages > RENDER_PACE_STAGES) {
        render_pace_summary(fp, name, &h, &stamp, "stage_capacity"); return;
    }
    /* All allocation precedes the callback's input -> submit guard. */
    uint64_t *values = calloc((stages + 1) * RENDER_PACE_FRAMES, sizeof *values);
    if (values == NULL) { render_pace_summary(fp, name, &h, &stamp, "storage"); return; }
    bench_samples samples[RENDER_PACE_STAGES + 1];
    for (size_t k = 0; k <= stages; k++)
        bench_samples_init(&samples[k], values + k * RENDER_PACE_FRAMES, RENDER_PACE_FRAMES);
    const char *error = NULL;
    for (unsigned i = 0; i <= RENDER_PACE_FRAMES; i++) {
        render_pace_frame f = {0};
        if (frame(user, i != 0, &f) != 0) { error = "frame_or_completion_error"; break; }
        if (i != 0) {
            uint64_t interval = f.complete_ns > h.previous_ns ? f.complete_ns - h.previous_ns : 0;
            (void)bench_add(&samples[stages], interval);
            for (size_t k = 0; k < stages; k++) (void)bench_add(&samples[k], f.stage[k]);
        }
        render_pace_observe(&h, &f);
    }
    render_pace_summary(fp, name, &h, &stamp, error);
    for (size_t k = 0; k <= stages; k++)
        render_pace_stage_report(fp, name, k == stages ? "display_interval" : names[k], &samples[k], &stamp);
    free(values);
    /* TRACK has no verdict return code, even for misses or diagnostic errors. */
}

typedef struct render_pace_fixture { unsigned calls, scrolls, fail_at; } render_pace_fixture;
static inline int render_pace_fixture_frame(void *user, bool scrolling, render_pace_frame *out)
{
    render_pace_fixture *fixture = user;
    unsigned call = ++fixture->calls;
    fixture->scrolls += scrolling;
    if (call == fixture->fail_at) return -1;
    out->msc_available = true;
    out->drops_available = true;
    out->msc = 100 + (uint64_t)call * 3;
    out->complete_ns = UINT64_C(1000000000) + (uint64_t)call * UINT64_C(20000000);
    out->stage[0] = scrolling ? call - 1u : UINT64_C(999999999);
    return 0;
}

/* Pure regression fixtures: no display, sleep or measured performance. */
static inline int render_pace_self_check(void)
{
    int failed = 0;
#define RENDER_PACE_CHECK(name, expression) do { bool ok_ = (expression); \
    printf("SELF-CHECK render_pace=%s %s\n", name, ok_ ? "PASS" : "FAIL"); \
    failed |= !ok_; } while (0)
    RENDER_PACE_CHECK("Xvfb_SKIP", render_pace_skip_reason(":99") != NULL &&
        render_pace_skip_reason(":99.0") != NULL &&
        render_pace_skip_reason("unix:99") != NULL &&
        render_pace_skip_reason("localhost:99.1") != NULL &&
        render_pace_skip_reason(NULL) != NULL &&
        render_pace_skip_reason(":199") == NULL);
    render_pace_history h = {0};
    render_pace_frame f = {.msc = 100, .complete_ns = UINT64_C(1000000000), .msc_available = true};
    render_pace_observe(&h, &f);
    f.msc = 101; f.complete_ns += UINT64_C(10000000); render_pace_observe(&h, &f);
    f.msc = 104; f.complete_ns += UINT64_C(50000000); render_pace_observe(&h, &f);
    f.complete_ns += UINT64_C(10000000); render_pace_observe(&h, &f);
    f.msc = 103; f.complete_ns += UINT64_C(10000000); render_pace_observe(&h, &f);
    f.msc = 105; f.complete_ns += UINT64_C(10000000); render_pace_observe(&h, &f);
    RENDER_PACE_CHECK("MSC_and_stall", h.frames == 5 && h.msc_intervals == 5 &&
        h.missed == 2 && h.invalid == 2 && h.longest_ns == UINT64_C(50000000));
    uint64_t values[RENDER_PACE_FRAMES]; bench_samples s;
    bench_samples_init(&s, values, RENDER_PACE_FRAMES);
    for (uint64_t i = 1; i <= RENDER_PACE_FRAMES; i++) (void)bench_add(&s, i);
    RENDER_PACE_CHECK("600_p99", s.n == 600 && bench_p50(&s) == 300 && bench_p99(&s) == 594);
    FILE *fp = tmpfile();
    if (fp == NULL) return 1;
    render_pace_stamp stamp = {"Full", "3.25"};
    render_pace_summary(fp, "synthetic", &h, &stamp, NULL);
    render_pace_stage_report(fp, "synthetic", "submit", &s, &stamp);
    render_pace_skip(fp, "synthetic", render_pace_skip_reason(":99"));
    rewind(fp); char output[2048] = {0};
    size_t got = fread(output, 1, sizeof output - 1, fp); fclose(fp);
    output[got] = '\0';
    RENDER_PACE_CHECK("TRACK_output", strstr(output, "status=TRACK verdict=TRACK") &&
        strstr(output, "missed_refreshes=2") && strstr(output, "longest_stall_ms=50.000") &&
        strstr(output, "p50_ns=300 p99_ns=594") && strstr(output, "evidence=(M)[AC]") &&
        strstr(output, "load1=3.25") && strstr(output, "status=SKIP verdict=TRACK") &&
        strstr(output, "Xvfb_:99_has_no_real_vblank") && !strstr(output, "PASS") && !strstr(output, "gate_"));
    h = (render_pace_history){0}; f = (render_pace_frame){.complete_ns = 1};
    render_pace_observe(&h, &f); f.complete_ns = 2; render_pace_observe(&h, &f);
    RENDER_PACE_CHECK("MSC_unavailable", h.frames == 1 && h.msc_intervals == 0 && h.missed == 0);
    fp = tmpfile(); if (fp == NULL) return 1;
    render_pace_fixture fixture = {0}; const char *names[] = {"submit"};
    render_pace_run(fp, "synthetic", names, 1, render_pace_fixture_frame, &fixture);
    rewind(fp); got = fread(output, 1, sizeof output - 1, fp); output[got] = '\0';
    RENDER_PACE_CHECK("600_scrolls_no_gate", fixture.calls == 601 && fixture.scrolls == 600 &&
        strstr(output, "frames=600 requested_frames=600") && strstr(output, "missed_refreshes=1200") &&
        strstr(output, "p50_ns=300 p99_ns=594") && !strstr(output, "PASS") && !strstr(output, "FAIL"));
    fclose(fp); fp = tmpfile(); if (fp == NULL) return 1;
    fixture = (render_pace_fixture){.fail_at = 4};
    render_pace_run(fp, "synthetic", names, 1, render_pace_fixture_frame, &fixture);
    rewind(fp); got = fread(output, 1, sizeof output - 1, fp); output[got] = '\0'; fclose(fp);
    RENDER_PACE_CHECK("error_is_TRACK", fixture.calls == 4 && strstr(output, "status=ERROR verdict=TRACK") &&
        strstr(output, "frames=2 requested_frames=600") && !strstr(output, "pass="));
#undef RENDER_PACE_CHECK
    return failed;
}
#endif
