/* replay.c - trace_replay: re-inject recorded input with its original timing (P0.6).
 * Waits on absolute CLOCK_MONOTONIC deadlines (start + (t0 - t0_first) / speed), so
 * sink latency and scheduler jitter do not accumulate and delivery is never early. */
#include "trace.h"

#include <errno.h>
#include <math.h>
#include <time.h>

int trace_replay(const trace_input_rec *recs, size_t n, const trace_replay_opts *opts,
                 trace_input_sink sink, void *user) {
    uint64_t start;
    if (opts == NULL || sink == NULL) return -1;
    if (!(opts->speed > 0.0) || !isfinite(opts->speed)) return -1;
    if (n > 0 && recs == NULL) return -1;
    start = trace_now_ns();
    for (size_t i = 0; i < n; i++) {
        if (!opts->fast) {
            uint64_t gap = recs[i].t0_ns >= recs[0].t0_ns ? recs[i].t0_ns - recs[0].t0_ns : 0;
            double scaled = (double)gap / opts->speed;
            uint64_t due;
            if (scaled > 3.0e18) scaled = 3.0e18;   /* stay inside uint64 */
            due = start + (uint64_t)scaled;
            struct timespec ts;
            ts.tv_sec = (time_t)(due / 1000000000u);
            ts.tv_nsec = (long)(due % 1000000000u);
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
            }
        }
        sink(&recs[i], user);
    }
    return 0;
}
