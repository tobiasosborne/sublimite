/* Law 2 over the fault service (docs/decisions/wave1-os-fault-lease.md,
 * "All four are ordinary thread entries, with no C heap allocation"): first
 * registration with its installation, validated polls, a delivered and
 * recovered fault, a stale poll, retirement and generation exhaustion at full
 * width, under the live release allocation guard, with the shipped release
 * settings. Fails closed on an inert guard. */
#include "base/base.h"
#include "os/os.h"

/* os_posix.c, EDIT_TEST_HOOKS builds only (A15; tests/lanes.mk). */
os_err os_fault_test_next_gen(uint64_t next);

static bool guard_live(void)
{
    /* Positive control: a volatile function pointer forces a real call. */
    void *(*volatile allocate)(size_t) = malloc;
    base_malloc_guard_begin();
    void *p = allocate(1);
    size_t count = base_malloc_guard_end();
    bool allocated = p != NULL;
    free(p);
    bool active = base_malloc_guard_active();
    printf("guard positive control: active=%d count=%zu\n", (int)active, count);
    return active && allocated && count > 0;
}

static bool fault_window(void)
{
    size_t ps = os_page_size();
    os_file f = OS_FILE_INIT;
    os_map m = {0};
    /* Fixture setup may allocate; it stays outside the window. */
    ASSERT(os_file_open(&f, "/proc/self/exe", OS_OPEN_READ) == OS_OK);
    ASSERT(os_map_file(&m, &f, 0, 3 * ps) == OS_OK);
    os_file_close(&f);
    os_map first = {m.addr, ps}, second = {(const unsigned char *)m.addr + ps, ps},
           third = {(const unsigned char *)m.addr + 2 * ps, ps};

    base_malloc_guard_begin();
    os_fault_id id = OS_FAULT_ID_INIT, again = OS_FAULT_ID_INIT, top = OS_FAULT_ID_INIT,
                none = {7, 7};
    uint64_t before = 1, after = 0, stale = 0;
    bool ok = os_fault_register(&first, &id) == OS_OK;
    ok = ok && os_fault_poll(id, &before) == OS_OK;
    ok = ok && os_fault_test_trigger(&first, 1) == OS_OK;
    ok = ok && os_fault_poll(id, &after) == OS_OK;
    ok = ok && os_fault_retire(id) == OS_OK;
    ok = ok && os_fault_poll(id, &stale) == OS_E_NOENT;
    ok = ok && os_fault_register(&first, &again) == OS_OK && os_fault_retire(again) == OS_OK;
    ok = ok && os_fault_test_next_gen(UINT64_MAX) == OS_OK;
    ok = ok && os_fault_register(&second, &top) == OS_OK && top.gen == UINT64_MAX;
    ok = ok && os_fault_register(&third, &none) == OS_E_NOSPC && none.gen == 0;
    ok = ok && os_fault_retire(top) == OS_OK;
    size_t count = base_malloc_guard_end();

    bool active = base_malloc_guard_active();
    printf("fault service Law-2 window: active=%d count=%zu ok=%d epochs %llu->%llu\n",
           (int)active, count, (int)ok, (unsigned long long)before, (unsigned long long)after);
    /* A failed step may have left a registration over the view: never unmap
     * it then; the process exits. */
    if (!ok) return false;
    const volatile unsigned char *v = m.addr;
    bool zero = v[0] == 0;
    os_unmap(&m);
    return active && count == 0 && zero && before == 0 && after == 1;
}

int main(void)
{
    bool ok = guard_live() && fault_window();
    puts(ok ? "law2_os_fault_test: ok" : "law2_os_fault_test: FAIL");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
