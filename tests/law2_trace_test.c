#include "base/base.h"
#include "trace/trace.h"

#include <stdio.h>
#include <stdlib.h>

static bool test_guard_active(void) {
    /* Force one real allocation at release optimization. */
    void *(*volatile allocate)(size_t) = malloc;
    base_malloc_guard_begin();
    void *p = allocate(1);
    size_t count = base_malloc_guard_end();
    bool active = base_malloc_guard_active();
    bool allocated = p != NULL;
    free(p);
    printf("trace guard positive control: active=%d count=%zu\n", (int)active, count);
    return active && allocated && count == 1;
}

static bool report_window(const char *name, size_t count) {
    bool active = base_malloc_guard_active();
    printf("trace %s Law-2 window: active=%d count=%zu\n", name, (int)active, count);
    return active && count == 0;
}

int main(void) {
    if (!test_guard_active() || trace_thread_register() < 0) return EXIT_FAILURE;

    base_malloc_guard_begin();
    trace_record(TRACE_STARTUP_PROCESS_START, 0);
    trace_record(TRACE_T1_DEQUEUE, 1);
    size_t cold = base_malloc_guard_end();

    base_malloc_guard_begin();
    trace_record(TRACE_STARTUP_PROCESS_START, 0);
    /* One past the larger capacity forces wrap in both rings. */
    _Static_assert(TRACE_RING_CAP >= TRACE_INPUT_CAP, "loop wraps both rings");
    for (uint32_t i = 1; i <= TRACE_RING_CAP + 1u; i++) {
        trace_record(TRACE_T4_PRESENT_SUBMITTED, i);
        trace_record_at(i, TRACE_T2_MUTATION_DONE, i);
        trace_input_key(i, TRACE_IN_KEY_DOWN, 'a', 0, 0, "a", 1);
    }
    const uint64_t last = TRACE_RING_CAP + 1u;
    trace_input_pointer(last, TRACE_IN_POINTER_MOVE, 0, 0, 0, 0);
    trace_input_wheel(last, 0, 0, 0);
    trace_input_resize(last, 1, 1);
    trace_input_focus(last, 1);
    trace_input_clipboard(last, 1, 0);
    trace_input_filechange(last, 1, 0);
    size_t wrapped = base_malloc_guard_end();
    trace_thread_release();

    bool cold_ok = report_window("cold record", cold);
    bool wrapped_ok = report_window("wrapped records and input", wrapped);
    if (!cold_ok || !wrapped_ok) return EXIT_FAILURE;

    puts("law2_trace_test: PASS");
    return EXIT_SUCCESS;
}
