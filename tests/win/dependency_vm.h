#ifndef EDIT_DEPENDENCY_VM_H
#define EDIT_DEPENDENCY_VM_H
#include <windows.h>
#include <stddef.h>
/* Test-only anonymous mappings and inaccessible-page oracles. */
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 0
#define MAP_ANONYMOUS 0
#define MAP_FAILED ((void *)-1)
static inline void *mmap(void *addr, size_t n, int prot, int flags, int fd, size_t off)
{
    (void)addr; (void)prot; (void)flags; (void)fd; (void)off;
    void *p = VirtualAlloc(NULL, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return p ? p : MAP_FAILED;
}
static inline int mprotect(void *p, size_t n, int prot)
{
    DWORD old;
    return VirtualProtect(p, n, prot == PROT_NONE ? PAGE_NOACCESS : PAGE_READWRITE, &old) ? 0 : -1;
}
static inline int munmap(void *p, size_t n)
{ (void)n; return VirtualFree(p, 0, MEM_RELEASE) ? 0 : -1; }
#endif
