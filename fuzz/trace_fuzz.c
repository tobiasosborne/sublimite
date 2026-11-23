#include "trace/trace.h"

#include <assert.h>
#include <stdlib.h>

static void count_event(const trace_input_rec *rec, void *arg) {
    (void)rec;
    size_t *count = arg;
    (*count)++;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char path[] = "build/trace-fuzz-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && os_unlink(path) == OS_OK);
    os_file f = os_file_from_fd(fd);
    for (size_t copied = 0; copied < size;) {
        size_t written;
        assert(os_file_write(&f, data + copied, size - copied, &written) == OS_OK && written != 0);
        copied += written;
    }

    trace_loaded dump;
    if (trace_fmt_load_dump(&f, &dump) == 0) {
        const trace_replay_opts fast = {.fast = 1, .speed = 1.0};
        size_t seen = 0;
        assert(trace_replay(dump.in, dump.nin, &fast, count_event, &seen) == 0);
        assert(seen == dump.nin);
        trace_fmt_dump_free(&dump);
    }
    os_file_close(&f);
    return 0;
}
