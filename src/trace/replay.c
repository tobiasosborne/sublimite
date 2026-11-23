#include "trace.h"

#include <math.h>

int trace_replay(const trace_input_rec *recs, size_t n, const trace_replay_opts *opts,
                 trace_input_sink sink, void *user) {
    ASSERT(opts != NULL && sink != NULL && (n == 0 || recs != NULL));
    ASSERT(opts->speed > 0.0 && isfinite(opts->speed));

    /* Absolute monotonic deadlines prevent sink cost from accumulating. */
    uint64_t start = opts->fast ? 0 : os_now_ns();
    for (size_t i = 0; i < n; i++) {
        if (!opts->fast) {
            uint64_t gap = recs[i].t0_ns >= recs[0].t0_ns ? recs[i].t0_ns - recs[0].t0_ns : 0;
            double scaled = (double)gap / opts->speed;
            if (scaled >= 0x1p64) return -1;  /* 2^64 is one past UINT64_MAX */
            uint64_t delay = (uint64_t)scaled;
            if (delay > UINT64_MAX - start) return -1;
            uint64_t due = start + delay;
            uint64_t now;
            while ((now = os_now_ns()) < due) os_sleep_ns(due - now);
        }
        sink(&recs[i], user);
    }
    return 0;
}
