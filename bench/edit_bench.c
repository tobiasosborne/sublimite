#include "../tests/support/edit_fixture.h"

#include <inttypes.h>
#include <string.h>

static int compare(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

int main(void)
{
    /* (G) 1000 samples place ten observations at or above the p99 rank.
     * The append path is the flat journal double in this worker. */
    enum { samples = 1000 };
    const char *power = getenv("EDIT_BENCH_POWER");
    ECHECK(power && (strcmp(power, "[AC]") == 0 || strcmp(power, "[bat]") == 0));
    const char *load = getenv("EDIT_BENCH_LOAD");
    ECHECK(load);
    uint64_t ns[samples];
    edit_fixture f;
    journal *j = ej_create(samples * (JOURNAL_HEADER_BYTES + sizeof(uint64_t) + 1), samples);
    ef_open(&f, NULL, 0, undo_default_cap_bytes(), j);
    const uint8_t x = 'x';
    for (size_t i = 0; i < samples; i++) {
        /* The UI supplies a one-step allowance for queues that receive one
         * change per iteration. Each call is outside the sample window. */
        ECHECK(edit_maintain(f.edit, 1, 1, 1) == EDIT_OK);
        uint64_t start = os_now_ns();
        ECHECK(edit_replace(f.edit, EDIT_TYPE, (edit_splice){i, 0, &x, 1}, start) == EDIT_OK);
        ef_submit(f.edit);
        ns[i] = os_now_ns() - start;
    }
    qsort(ns, samples, sizeof *ns, compare);
    printf("edit_bench %s load=%s flat-journal type begin/replace/commit/final/submit "
           "p50=%" PRIu64 " ns p99=%" PRIu64 " ns n=%d\n", power, load,
           ns[samples / 2 - 1], ns[samples * 99 / 100 - 1], samples);
    ef_close(&f);
    return EXIT_SUCCESS;
}
