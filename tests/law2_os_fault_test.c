/* Law 2 over the fault service (docs/decisions/wave1-os-fault-lease.md,
 * "All four are ordinary thread entries, with no C heap allocation"): first
 * registration with its installation, validated polls, a real fault and its
 * recovery, a stale poll, retirement and generation exhaustion at full width,
 * under the live release allocation guard, with the shipped release
 * settings. Fails closed on an inert guard. */
#include "base/base.h"
#include "os/os.h"

#include <stdlib.h>

/* The OS_TEST_HOOKS variant of os_posix.c (A15; tests/lanes.mk). */
void os_fault_test_next_gen(uint64_t next);

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
    /* Fixture setup may allocate; it stays outside the window. An unnamed
     * file under build/ of three pages, mapped, then emptied: every page of
     * the view faults when read. */
    os_dir d = OS_DIR_INIT;
    os_file f = OS_FILE_INIT;
    os_map m = {0};
    os_err rc = os_dir_open(&d, "build");
    ASSERT(rc == OS_OK);
    rc = os_file_open_anon(&f, &d);
    ASSERT(rc == OS_OK);
    os_dir_close(&d);
    rc = os_file_set_size(&f, 3 * ps);
    ASSERT(rc == OS_OK);
    rc = os_map_file(&m, &f, 0, 3 * ps);
    ASSERT(rc == OS_OK);
    rc = os_file_set_size(&f, 0);
    ASSERT(rc == OS_OK);
    os_file_close(&f);
    const volatile unsigned char *v = m.addr;
    os_map first = {m.addr, ps}, second = {(const unsigned char *)m.addr + ps, ps},
           third = {(const unsigned char *)m.addr + 2 * ps, ps};

    base_malloc_guard_begin();
    os_fault_id id = OS_FAULT_ID_INIT, again = OS_FAULT_ID_INIT, top = OS_FAULT_ID_INIT,
                none = {7, 7};
    uint64_t before = 1, after = 0, stale = 0;
    bool ok = os_fault_register(&first, &id) == OS_OK;
    ok = ok && os_fault_poll(id, &before) == OS_OK;
    ok = ok && v[1] == 0; /* A real fault, recovered. */
    ok = ok && os_fault_poll(id, &after) == OS_OK;
    if (ok) os_fault_retire(id);
    ok = ok && os_fault_poll(id, &stale) == OS_E_NOENT;
    ok = ok && os_fault_register(&first, &again) == OS_OK;
    if (ok) os_fault_retire(again);
    if (ok) os_fault_test_next_gen(UINT64_MAX);
    ok = ok && os_fault_register(&second, &top) == OS_OK && top.gen == UINT64_MAX;
    ok = ok && os_fault_register(&third, &none) == OS_E_NOSPC && none.gen == 0;
    if (ok) os_fault_retire(top);
    size_t count = base_malloc_guard_end();

    bool active = base_malloc_guard_active();
    printf("fault service Law-2 window: active=%d count=%zu ok=%d epochs %llu->%llu\n",
           (int)active, count, (int)ok, (unsigned long long)before, (unsigned long long)after);
    /* A failed step may have left a registration over the view: never unmap
     * it then; the process exits. */
    if (!ok) return false;
    os_unmap(&m);
    return active && count == 0 && before == 0 && after == 1;
}

int main(void)
{
    bool ok = guard_live() && fault_window();
    puts(ok ? "law2_os_fault_test: ok" : "law2_os_fault_test: FAIL");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
