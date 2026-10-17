#include "palette/palette.h"
#include "harness.h"

int main(void)
{
    palette_state p; palette_init(&p);
    /* (P) one filter sample per eligible action, each querying its full label.
     * The table count bounds sample storage. No latency budget is invented. */
    uint64_t times[PALETTE_ACTION_COUNT]; bench_samples samples;
    bench_samples_init(&samples, times, PALETTE_ACTION_COUNT);
    char power[sizeof "Not charging"]; bench_battery_status(power, sizeof power);
    const char *tag = bench__tag_from_power(power);
    printf("POWER status=%s %s; load stamped by invoking shell\n", power, tag);
    for (size_t i = 0; i < PALETTE_ACTION_COUNT; i++) {
        if (!p.rows[i].binding) continue;
        BENCH_TIME(&samples, (void)palette_filter(&p, p.rows[i].label, strlen(p.rows[i].label)));
    }
    printf("palette filter: n=%zu p50=%llu ns p99=%llu ns (M)%s; TRACK, no latency limit\n",
        samples.n, (unsigned long long)bench_p50(&samples), (unsigned long long)bench_p99(&samples), tag);
    return 0;
}
