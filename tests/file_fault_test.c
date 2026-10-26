/* Preserve file_bus's private registry regressions against its OS successor.
 * Include the implementation only in this test TU, as os_win_test does; the
 * normal OS object has no test hooks, and os.h remains frozen. */
#if !defined(_WIN32)
#include <sys/mman.h>
#include <sys/types.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "os/os.h"
static void *file_fault_mmap(void *, size_t, int, int, int, off_t);
#define mmap file_fault_mmap
#include "../src/os/os_posix.c"
#undef mmap

static struct {
    int failures, slot, replacement, blocked;
    os_map next;
    os_thread retire;
    _Atomic int armed, started, done;
} file_fault_test_state;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "file_fault_test:%d: FAIL %s\n", __LINE__, #c); file_fault_test_state.failures++; } } while (0)
static void retire_mapping(void *ctx)
{
    (void)ctx;
    atomic_store(&file_fault_test_state.started, 1);
    os_fault_retire(file_fault_test_state.slot);
    atomic_store(&file_fault_test_state.done, 1);
}
static void *file_fault_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    if ((flags & MAP_FIXED) && atomic_exchange(&file_fault_test_state.armed, 0)) {
        CHECK(os_thread_start(&file_fault_test_state.retire, retire_mapping, NULL) == OS_OK);
        while (!atomic_load(&file_fault_test_state.started)) os_yield();
        fault_slot *slot = &os_fault_service.slots[file_fault_test_state.slot];
        while ((atomic_load_explicit(&slot->state, memory_order_acquire) & SLOT_PHASE) == SLOT_LIVE) os_yield();
        file_fault_test_state.blocked =
            (atomic_load_explicit(&slot->state, memory_order_acquire) & SLOT_PHASE) == SLOT_RETIRING &&
            !atomic_load(&file_fault_test_state.done);
        CHECK(os_fault_register(&file_fault_test_state.next, &file_fault_test_state.replacement) == OS_OK);
        CHECK(file_fault_test_state.replacement != file_fault_test_state.slot);
    }
    return mmap(addr, len, prot, flags, fd, off);
}
int main(void)
{
    /* (E) Reuse os_test's existing 120 s outer process hang guard. */
    alarm(120);
    size_t page = os_page_size();
    uint8_t *foreign = NULL;
    /* Same six-page fixture as the old file registry probe: one registered
     * page, two foreign neighbours, a two-page replacement, one guard. */
    CHECK(os_vm_reserve((void **)&foreign, 6u * page) == OS_OK);
    if (!foreign) return 1;
    memset(foreign, 'Q', 6u * page);
    CHECK(pthread_once(&os_fault_service.once, fault_install) == 0);
    CHECK(os_fault_service.installed);
    fault_slot *reserved = &os_fault_service.slots[0];
    atomic_store(&reserved->state, SLOT_RESERVED);
    reserved->addr = 1; reserved->len = (uintptr_t)foreign + 2u * page;
    CHECK(!fault_recover((uintptr_t)foreign + page));
    CHECK(foreign[0] == 'Q' && foreign[page] == 'Q' && foreign[2u * page] == 'Q');
    os_fault_retire(0);
    os_map first = {.addr = foreign, .len = page};
    file_fault_test_state.next = (os_map){.addr = foreign + 3u * page, .len = 2u * page};
    file_fault_test_state.replacement = -1;
    CHECK(os_fault_register(&first, &file_fault_test_state.slot) == OS_OK);
    atomic_store(&file_fault_test_state.armed, 1);
    CHECK(fault_recover((uintptr_t)foreign + page / 2u));
    CHECK(os_thread_join(&file_fault_test_state.retire) == OS_OK);
    CHECK(file_fault_test_state.blocked && atomic_load(&file_fault_test_state.done));
    CHECK(foreign[0] == 0 && foreign[page] == 'Q' && foreign[2u * page] == 'Q' && foreign[3u * page] == 'Q');
    os_fault_retire(file_fault_test_state.replacement);
    os_vm_release(foreign, 6u * page);
    puts("file_fault_test: OS reserved-slot publication, pinned retirement/reuse, foreign neighbours: ok");
    return file_fault_test_state.failures ? 1 : 0;
}
#else
#include <stdio.h>
int main(void) { puts("file_fault_test: SKIP POSIX white-box registry; Windows os_win_test covers retire_admitted"); return 0; }
#endif
