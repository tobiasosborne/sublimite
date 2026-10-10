/* Exercise clipboard ownership with an allocator that only promises fundamental
 * alignment: extended pool allocation must bypass calloc entirely. */
#include "work/work.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static void *fundamental_calloc(size_t n, size_t size) {
    if (size == sizeof(work_pool)) return NULL;
    return calloc(n, size);
}
#define calloc fundamental_calloc
#include "../src/x11/clip.c"
#undef calloc
int main(void) {
    x11_clip c = {0};
    if (!pool_init(&c)) {
        fputs("x11_alignment_test: FAIL §6 clipboard pool uses fundamental allocator\n", stderr);
        return 1;
    }
    int bad = (uintptr_t)c.pool % _Alignof(work_pool) != 0;
    work_pool_shutdown(c.pool); free(c.pool);
    puts(bad ? "x11_alignment_test: FAIL §6 pool alignment" : "x11_alignment_test: §6 ok");
    return bad;
}
