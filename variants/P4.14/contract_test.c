/* Driver validation unit tests: no socket, environment mutation or window. */
#define main zygote_driver_main
#include "../../bench/zygote_bench.c"
#undef main
#include <assert.h>
int main(void)
{
    assert(zygote_display_allowed(":99", ":99", NULL, false));
    assert(!zygote_display_allowed(":98", ":98", NULL, false));
    assert(!zygote_display_allowed(":98", ":98", NULL, true));
    assert(!zygote_display_allowed(":98", ":98", "yes", true));
    assert(!zygote_display_allowed(":98", ":99", "1", true));
    assert(!zygote_display_allowed("", "", "1", true));
    assert(zygote_display_allowed(":98", ":98", "1", true));
    zygote_record r = {.map_ns = 110, .ready_ns = 120, .present_ns = 130,
        .device_ns = 140, .complete_ns = 150, .ready_frame = 1, .present_frame = 1,
        .device_frame = 1, .complete_frame = 1, .mapped_verified = 1,
        .unmapped_verified = 1, .native_gl = true, .displayed_msc = 5,
        .device = "test renderer", .driver = "test version"};
    assert(zygote_record_valid(&r, 100, true)); /* fence later than CPU ready */
    r.device_frame = 2; assert(!zygote_record_valid(&r, 100, true)); r.device_frame = 1;
    r.device_ns = 125; assert(!zygote_record_valid(&r, 100, true)); r.device_ns = 140;
    r.ready_ns = 145; assert(!zygote_record_valid(&r, 100, true)); r.ready_ns = 120;
    r.displayed_msc = 0; assert(!zygote_record_valid(&r, 100, true)); r.displayed_msc = 5;
    r.driver[0] = 0; assert(!zygote_record_valid(&r, 100, true)); r.driver[0] = 't';
    r.native_gl = false; assert(!zygote_record_valid(&r, 100, true));
    assert(zygote_record_valid(&r, 100, false));
    /* Failed startup before fork must also remove its private directory. */
    zygote_variant cleanup = {.output = -1, .runtime_created = true};
    (void)snprintf(cleanup.runtime, sizeof cleanup.runtime, "/tmp/zygote-contract-XXXXXX");
    assert(mkdtemp(cleanup.runtime));
    zygote_cleanup(&cleanup);
    assert(access(cleanup.runtime, F_OK) != 0 && errno == ENOENT);
    puts("zygote endpoint contract: PASS CPU readiness, native fence/Present identity, dual display guard");
    return 0;
}
