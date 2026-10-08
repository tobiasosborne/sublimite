/* libFuzzer: arbitrary bytes into the trace dump loader; accepted dumps are then
 * replayed --fast into a no-op sink. Must never overrun, leak or crash. */
#include "trace/trace_fmt.h"

#include <stdio.h>

static void nop_sink(const trace_input_rec *e, void *user) {
    (*(uint64_t *)user) += e->seq + e->kind;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    trace_loaded d;
    FILE *f;
    if (size == 0) return 0;
    f = fmemopen((void *)data, size, "rb");
    if (f == NULL) return 0;
    if (trace_fmt_load_dump(f, &d) == 0) {
        trace_replay_opts o = { 1, 1.0 };
        uint64_t acc = 0;
        if (trace_replay(d.in, d.nin, &o, nop_sink, &acc) != 0) __builtin_trap();
        trace_fmt_dump_free(&d);
    }
    fclose(f);
    return 0;
}
