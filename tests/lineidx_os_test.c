/* Clock-role regression for edit-177r.4. Compile this implementation with
 * independent deterministic slice/accounting clocks; no production hook. */
#include "os/os.h"
#include "base/base.h"
static uint64_t slice_clock, cpu_clock, slice_reads, cpu_reads;
static uint64_t test_slice_ns(void) { slice_reads++; slice_clock += 500000; return slice_clock; }
static uint64_t test_cpu_ns(void) { cpu_reads++; return cpu_clock; }
#define os_slice_ns test_slice_ns
#define os_thread_cpu_ns test_cpu_ns
#include "../src/lineidx/lineidx.c"
#undef os_slice_ns
#undef os_thread_cpu_ns

static unsigned span_calls;
static size_t one_byte(void *ctx, uint64_t off, const uint8_t **p)
{
    (void)ctx; (void)off;
    static const uint8_t byte = 'x';
    span_calls++; *p = &byte; return 1;
}
int main(void)
{
    (void)test_slice_ns; (void)test_cpu_ns;
    /* (G) UI_CPU_NS is 500000 ns in lineidx.c. Each fake slice read advances
     * that amount, while accounting stays frozen as GetThreadTimes can.
     * An edited chunk must yield after its first span and preserve progress. */
    lineidx *x = lineidx_create(LINEIDX_CHUNK);
    EDIT_ASSERT(x && lineidx_edit(x, 0, 1, 1) == 0);
    lineidx_src src = {NULL, LINEIDX_CHUNK, one_byte, NULL};
    EDIT_ASSERT(lineidx_refresh(x, &src) == 0);
    EDIT_ASSERT(slice_reads >= 2 && span_calls == 1 && x->refresh_pos == 1);
    EDIT_ASSERT(cpu_reads == 0);
    lineidx_destroy(x);

    /* A cancelled invocation records accounting delta, independent of the
     * fast slice clock. (G) The values are synthetic ns, 19 - 7 = 12. */
    work_pool pool = {0}; work_slot slot = {0};
    atomic_init(&slot.epoch, 2);
    lineidx_job job = {.cpu_begin = 7};
    work_ctx ctx = {&pool, &slot, 1, 0, 0, &job, false};
    cpu_clock = 19;
    EDIT_ASSERT(worker_stop(&ctx));
    EDIT_ASSERT(cpu_reads == 1 && job.cancel_cpu_ns == 12);
    /* Worker deadlines must also begin in the slice clock's domain, even
     * when its epoch differs from the accounting clock. (G) The worker's
     * existing 4000000 ns budget / 500000 ns fake step admits eight spans. */
    slice_clock = 0; cpu_clock = UINT64_C(100000000000); span_calls = 0;
    atomic_store(&slot.epoch, 1);
    uint16_t length = LINEIDX_CHUNK - 1u; uint32_t result = 0;
    job = (lineidx_job){.src = src, .n = 1, .lengths = &length, .res = &result};
    ctx.continue_ = false;
    build_fn(&ctx);
    EDIT_ASSERT(job.cpu_begin == cpu_clock && span_calls == 8 && ctx.continue_);
    puts("lineidx_os_test: slice yields with frozen accounting; cancellation uses CPU delta");
    return 0;
}
