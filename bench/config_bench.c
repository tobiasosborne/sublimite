#include "config/config.h"
#include "base/base.h"
#include "harness.h"
int main(void)
{
    const char source[] = "theme='dark'\nwrap=true\n[font]\nsize=12.5\nfamily='Mono'\n[keymap]\nredo='Ctrl+Y'\n";
    /* (P) one parse sample per byte in this representative settings fixture;
     * no performance budget exists for config parsing in the goal sheet. */
    uint64_t times[sizeof source - 1u]; bench_samples samples;
    bench_samples_init(&samples, times, sizeof times / sizeof times[0]);
    char power[sizeof("Discharging") + 1u]; bench_battery_status(power, sizeof power);
    FILE *load_file = fopen("/proc/loadavg", "r"); double load = 0;
    if (load_file) { if (fscanf(load_file, "%lf", &load) != 1) load = 0; fclose(load_file); }
    for (size_t i = 0; i < samples.cap; i++) {
        config_settings settings; config_error error;
        uint64_t start = bench_now_ns();
        EDIT_ASSERT(config_parse((const uint8_t *)source, sizeof source - 1u, &settings, &error) == CONFIG_OK);
        (void)bench_add(&samples, bench_now_ns() - start);
    }
    printf("config_bench: parse (M)%s power=%s load1=%g n=%zu "
        "derivation=one_sample_per_fixture_byte fixture_bytes=%zu p50_ns=%llu p99_ns=%llu verdict=TRACK\n",
        bench__tag_from_power(power), power, load, samples.n, sizeof source - 1u,
        (unsigned long long)bench_p50(&samples), (unsigned long long)bench_p99(&samples));
    return 0;
}
