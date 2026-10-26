#ifndef EDIT_TEST_CORE_VM_H
#define EDIT_TEST_CORE_VM_H
#include "os/os.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif
/* Guard-page fixtures need permission changes, absent from frozen os.h.
 * Keep this test-only operation here; storage lifetime goes through os.h. */
static inline int core_vm_protect(void *p, size_t n, bool accessible)
{
#if defined(_WIN32)
    unsigned char *next = p;
    size_t left = n;
    while (left) {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery(next, &info, sizeof info)) return -1;
        size_t step = info.RegionSize - (size_t)(next - (unsigned char *)info.BaseAddress);
        if (step > left) step = left;
        /* Undo decommits unused pool tails. They are already inaccessible;
         * VirtualProtect rejects a range containing even one reserved page. */
        if (info.State == MEM_COMMIT) {
            DWORD previous;
            if (!VirtualProtect(next, step, accessible ? PAGE_READWRITE : PAGE_NOACCESS, &previous)) return -1;
        } else if (accessible) return -1;
        next += step; left -= step;
    }
    return 0;
#else
    return mprotect(p, n, accessible ? PROT_READ | PROT_WRITE : PROT_NONE);
#endif
}
static inline void *core_vm_alloc(size_t n, bool accessible)
{
    void *p = NULL;
    if (os_vm_reserve(&p, n) != OS_OK) return NULL;
    if (os_vm_commit(p, n) != OS_OK || core_vm_protect(p, n, accessible) != 0) {
        os_vm_release(p, n); return NULL;
    }
    return p;
}
static inline int core_vm_free(void *p, size_t n)
{
    os_vm_release(p, n); return 0;
}
#endif
