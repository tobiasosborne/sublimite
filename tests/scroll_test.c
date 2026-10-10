#include "scroll/scroll.h"
#include "base/base.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "scroll_test:%d: FAIL %s\n", __LINE__, #x); goto cleanup; } } while (0)
typedef struct flat { const uint8_t *bytes; size_t size; } flat;
static size_t span(void *ctx, uint64_t off, const uint8_t **out)
{
    flat *f = ctx;
    if (off >= f->size) return 0;
    *out = f->bytes + off;
    size_t n = f->size - (size_t)off;
    return n > 193 ? 193 : n; /* deliberately fragmented */
}
static int settle_resolve(scroll_state *s, lineidx *index, const lineidx_src *src, uint64_t budget)
{
    scroll_resolver r = {0};
    int rc = SCROLL_MORE;
    for (unsigned i = 0; i < 100000 && rc == SCROLL_MORE; i++)
        rc = scroll_resolve_slice(s, &r, index, src, budget, 0);
    return rc;
}
static int settle_follow(scroll_state *s, lineidx *index, const lineidx_src *src, uint64_t cursor)
{
    scroll_resolver r = {0};
    int rc = SCROLL_MORE;
    for (unsigned i = 0; i < 100000 && rc == SCROLL_MORE; i++)
        rc = scroll_follow_cursor_slice(s, &r, index, src, cursor, 0, 0);
    return rc;
}
static int accumulation(void)
{
    scroll_state s;
    CHECK(scroll_init(&s, (scroll_config){24, 17, 3}, (scroll_extent){1000000, 10000, true}) == 0);
    CHECK(scroll_seek_line(&s, 100) == 0);
    for (unsigned i = 0; i < 256; i++) CHECK(scroll_wheel(&s, 1) == 0);
    CHECK(s.first_line == 103 && s.subrow_q8 == 0);
    for (unsigned i = 0; i < 257; i++) CHECK(scroll_wheel(&s, -1) == 0);
    CHECK(s.first_line == 99 && s.subrow_q8 == 17u * 253u);
    CHECK(scroll_wheel(&s, 1) == 0);
    CHECK(s.first_line == 100 && s.subrow_q8 == 0);
    for (unsigned i = 0; i < 10000; i++) {
        CHECK(scroll_pixels(&s, 1) == 0);
        CHECK(scroll_pixels(&s, -1) == 0);
    }
    CHECK(s.first_line == 100 && s.subrow_q8 == 0);
    CHECK(scroll_wheel(&s, 256) == 0 && s.first_line == 103);
    CHECK(scroll_pixels(&s, INT64_MIN) == 0 && s.first_line == 0 && s.subrow_q8 == 0);
    CHECK(scroll_pixels(&s, INT64_MAX) == 0 && s.first_line == 9976 && s.subrow_q8 == 0);
    CHECK(scroll_pixels(&s, -1) == 0 && s.first_line == 9975 && s.subrow_q8 == 4351);
    puts("scroll_test: accumulation/reversal/extreme deltas PASS");
    return 0;
cleanup:
    return 1;
}
static int keys_and_follow(void)
{
    scroll_state s; view_key motion = VIEW_TYPE;
    CHECK(scroll_init(&s, (scroll_config){10, 16, 2}, (scroll_extent){1000, 31, true}) == 0);
    CHECK(scroll_key_motion(&s, SCROLL_PAGE_UP, &motion) == 0 && motion == VIEW_PAGE_UP && s.first_line == 0);
    for (unsigned i = 0; i < 5; i++) CHECK(scroll_key_motion(&s, SCROLL_PAGE_DOWN, &motion) == 0);
    CHECK(motion == VIEW_PAGE_DOWN && s.first_line == 21);
    CHECK(scroll_key_motion(&s, SCROLL_END, &motion) == 0 && motion == VIEW_DOC_END && s.first_line == 21);
    CHECK(scroll_key_motion(&s, SCROLL_HOME, &motion) == 0 && motion == VIEW_DOC_HOME && s.first_line == 0);
    CHECK(scroll_seek_line(&s, 10) == 0);
    CHECK(scroll_follow(&s, 12) == 0 && s.first_line == 10);
    CHECK(scroll_follow(&s, 11) == 0 && s.first_line == 9);
    CHECK(scroll_follow(&s, 18) == 0 && s.first_line == 11);
    CHECK(scroll_pixels(&s, 1) == 0);
    CHECK(scroll_follow(&s, 13) == 0 && s.first_line == 11 && s.subrow_q8 == 0);
    CHECK(scroll_pixels(&s, 1) == 0);
    CHECK(scroll_follow(&s, 15) == 0 && s.first_line == 11 && s.subrow_q8 == 1);
    /* A pure scroll never consumes a cursor or calls follow. */
    CHECK(scroll_wheel(&s, 256) == 0 && s.first_line == 14);
    CHECK(scroll_resize(&s, (scroll_config){1, 20, UINT32_MAX}) == 0);
    CHECK(scroll_follow(&s, 4) == 0 && s.first_line == 4 && s.subrow_q8 == 0);
    CHECK(scroll_set_extent(&s, (scroll_extent){0, 1, true}) == 0 && s.first_line == 0);
    CHECK(scroll_key_motion(&s, SCROLL_END, &motion) == 0 && s.first_line == 0);
    scroll_state before = s;
    CHECK(scroll_resize(&s, (scroll_config){0, 1, 0}) == SCROLL_ERR_ARG && memcmp(&s, &before, sizeof s) == 0);
    CHECK(scroll_key_motion(&s, (scroll_key)99, &motion) == SCROLL_ERR_ARG);
    CHECK(scroll_init(NULL, (scroll_config){1, 1, 0}, (scroll_extent){0, 1, true}) == SCROLL_ERR_ARG);
    CHECK(scroll_init(&s, (scroll_config){1, UINT32_MAX, 0}, (scroll_extent){0, 1, true}) == SCROLL_ERR_ARG);
    CHECK(scroll_init(&s, (scroll_config){1, 1, 0}, (scroll_extent){LINEIDX_MAX_LEN, UINT64_MAX, true}) == 0);
    CHECK(scroll_seek_line(&s, UINT64_MAX - 20) == 0);
    CHECK(scroll_pixels(&s, 19 * 256) == 0 && s.first_line == UINT64_MAX - 1);
    CHECK(scroll_pixels(&s, INT64_MAX) == 0 && s.first_line == UINT64_MAX - 1 && s.subrow_q8 == 0);
    puts("scroll_test: page/document ends/follow margins/resize/invalid PASS");
    return 0;
cleanup:
    return 1;
}
static int correction(void)
{
    int result = 1;
    bool guarding = false;
    uint8_t bytes[3u * LINEIDX_CHUNK];
    memset(bytes, 'x', sizeof bytes);
    for (size_t i = 10; i < sizeof bytes; i += 11) bytes[i] = '\n';
    flat f = {bytes, sizeof bytes};
    lineidx_src src = {&f, sizeof bytes, span, NULL};
    lineidx *index = lineidx_create(sizeof bytes);
    CHECK(index);
    scroll_state s;
    CHECK(scroll_init(&s, (scroll_config){24, 17, 3}, (scroll_extent){sizeof bytes, 2000, false}) == 0);
    CHECK(scroll_seek_byte(&s, 150005) == 0);
    CHECK(settle_resolve(&s, index, &src, 0) == 0 && s.first_byte == 150007 - 11 && s.approximate);
    uint64_t start = s.first_byte;
    CHECK(scroll_pixels(&s, 701) == 0 && settle_resolve(&s, index, &src, 0) == 0);
    CHECK(scroll_wheel(&s, 256) == 0 && settle_resolve(&s, index, &src, 0) == 0 && s.first_byte == start + 33);
    CHECK(scroll_wheel(&s, -256) == 0 && settle_resolve(&s, index, &src, 0) == 0 && s.first_byte == start);
    uint64_t cursor = start + 11 * 7 + 3, row_before = (cursor - start) / 11;
    /* Publication can land BETWEEN an event transition and its resolution. */
    CHECK(scroll_wheel(&s, 256) == 0);
    for (unsigned slice = 0; slice < 20000 && !lineidx_complete(index); slice++)
        (void)lineidx_seek_line(index, &src, UINT64_MAX, sizeof bytes);
    CHECK(lineidx_complete(index));
    CHECK(settle_resolve(&s, index, &src, 0) == 0 && !s.approximate);
    CHECK(s.first_byte == start + 33 && s.first_line == start / 11 + 3 && s.subrow_q8 == 701);
    CHECK(scroll_wheel(&s, -256) == 0 && settle_resolve(&s, index, &src, 0) == 0);
    CHECK((cursor - s.first_byte) / 11 == row_before);
    CHECK(scroll_seek_line(&s, 12345) == 0 && settle_resolve(&s, index, &src, 0) == 0);
    CHECK(s.first_line == 12345 && s.first_byte == 12345 * 11);
    scroll_state before = s;
    CHECK(settle_resolve(&s, index, &src, SCROLL_SCAN_BUDGET + 1u) == SCROLL_ERR_ARG && memcmp(&s, &before, sizeof s) == 0);
    edit_malloc_guard_begin();
    guarding = true;
    for (unsigned i = 0; i < 10000; i++) {
        CHECK(scroll_wheel(&s, (i & 1u) ? -17 : 17) == 0);
        CHECK(settle_resolve(&s, index, &src, 0) == 0);
        CHECK(scroll_follow(&s, s.first_line + 7) == 0);
        CHECK(settle_resolve(&s, index, &src, 0) == 0);
        CHECK(settle_follow(&s, index, &src, s.first_byte + 11 * 7) == 0);
    }
    size_t allocations = edit_malloc_guard_end();
    guarding = false;
    CHECK(allocations == 0);
    printf("scroll_test: byte/index correction preserves cursor row PASS; allocations=%zu guard=%s\n", allocations, edit_malloc_guard_active() ? "active" : "sanitizer-inert");
    result = 0;
cleanup:
    if (guarding) (void)edit_malloc_guard_end();
    lineidx_destroy(index);
    return result;
}
static int unindexed_ends(void)
{
    int result = 1;
    const uint8_t bytes[] = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj";
    flat f = {bytes, sizeof bytes - 1};
    lineidx_src src = {&f, f.size, span, NULL};
    lineidx *index = lineidx_create(f.size);
    CHECK(index);
    scroll_state s; view_key motion;
    CHECK(scroll_init(&s, (scroll_config){4, 16, 1}, (scroll_extent){f.size, 1, false}) == 0);
    CHECK(scroll_key_motion(&s, SCROLL_END, &motion) == 0 && motion == VIEW_DOC_END);
    CHECK(settle_resolve(&s, index, &src, 0) == 0 && s.first_byte == 12);
    CHECK(scroll_key_motion(&s, SCROLL_HOME, &motion) == 0 && settle_resolve(&s, index, &src, 0) == 0 && s.first_byte == 0);
    for (unsigned i = 0; i < 5; i++) {
        CHECK(scroll_key_motion(&s, SCROLL_PAGE_DOWN, &motion) == 0);
        CHECK(settle_resolve(&s, index, &src, 0) == 0);
    }
    CHECK(s.first_byte == 12 && s.subrow_q8 == 0);
    CHECK(scroll_wheel(&s, -256) == 0 && settle_resolve(&s, index, &src, 0) == 0 && s.first_byte == 6);
    /* Estimated line label is zero here; physical wheel scrolling must still
     * reach the real beginning and clip its sub-row remainder. */
    CHECK(scroll_wheel(&s, -257) == 0 && settle_resolve(&s, index, &src, 0) == 0 && s.first_byte == 0 && s.subrow_q8 == 0);
    CHECK(settle_follow(&s, index, &src, 18) == 0 && s.first_byte == 12);
    CHECK(settle_follow(&s, index, &src, 0) == 0 && s.first_byte == 0);
    puts("scroll_test: unindexed page/document ends PASS");
    result = 0;
cleanup:
    lineidx_destroy(index);
    return result;
}
static size_t broken_span(void *ctx, uint64_t off, const uint8_t **out)
{ (void)ctx; (void)off; (void)out; return 0; }
typedef struct cold_source {
    flat data;
    pthread_t ui;
    _Atomic size_t ui_calls;
} cold_source;
static size_t cold_span(void *ctx, uint64_t off, const uint8_t **out)
{
    cold_source *f = ctx;
    if (pthread_equal(pthread_self(), f->ui)) atomic_fetch_add(&f->ui_calls, 1);
    return span(&f->data, off, out);
}
static int resident_navigation(void)
{
    int result = 1;
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool);
    scroll_resident resident;
    bool initialized = false, guarding = false;
    lineidx *index = NULL;
    void *mapping = MAP_FAILED;
    int fd = -1;
    uint8_t bytes[2u * SCROLL_SCAN_BUDGET];
    memset(bytes, 'x', sizeof bytes);
    for (size_t i = 9; i < sizeof bytes; i += 10) bytes[i] = '\n';
    fd = memfd_create("scroll-cold", 0);
    CHECK(fd >= 0 && write(fd, bytes, sizeof bytes) == (ssize_t)sizeof bytes);
    mapping = mmap(NULL, sizeof bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    CHECK(mapping != MAP_FAILED);
    CHECK(madvise(mapping, sizeof bytes, MADV_DONTNEED) == 0);
    cold_source f = {{mapping, sizeof bytes}, pthread_self(), 0};
    lineidx_src src = {&f, sizeof bytes, cold_span, NULL};
    CHECK(pool && work_pool_init(pool, 1, 0) == 0);
    initialized = true;
    CHECK(scroll_resident_init(&resident, pool, &src) == 0);
    index = lineidx_create(sizeof bytes);
    CHECK(index);
    scroll_state s;
    CHECK(scroll_init(&s, (scroll_config){4, 17, 1}, (scroll_extent){sizeof bytes, 1, false}) == 0);
    CHECK(scroll_seek_byte(&s, 100005) == 0);
    struct rusage before, after;
    CHECK(getrusage(RUSAGE_THREAD, &before) == 0);
    scroll_resolver resolver = {0};
    scroll_state pending = s;
    edit_malloc_guard_begin(); guarding = true;
    int rc = scroll_resolve_resident(&s, &resolver, index, &resident, 0, 0);
    CHECK(rc == SCROLL_MORE && memcmp(&s, &pending, sizeof s) == 0);
    for (unsigned i = 0; i < 100000 && rc == SCROLL_MORE; i++) {
        struct timespec delay = {0, 100000};
        (void)nanosleep(&delay, NULL);
        rc = scroll_resolve_resident(&s, &resolver, index, &resident, 0, 0);
        if (rc == SCROLL_MORE) CHECK(memcmp(&s, &pending, sizeof s) == 0);
    }
    CHECK(rc == SCROLL_OK);
    size_t allocations = edit_malloc_guard_end(); guarding = false;
    CHECK(allocations == 0);
    CHECK(getrusage(RUSAGE_THREAD, &after) == 0);
    CHECK(atomic_load(&f.ui_calls) == 0);
    CHECK(after.ru_majflt == before.ru_majflt);
    CHECK(s.first_byte == 100000);
    puts("scroll_test: resident worker navigation, foreground major faults=0 PASS");
    result = 0;
cleanup:
    if (guarding) (void)edit_malloc_guard_end();
    lineidx_destroy(index);
    if (initialized) { (void)scroll_resident_close(&resident); work_pool_shutdown(pool); if (scroll_resident_close(&resident)) result = 1; }
    free(pool);
    if (mapping != MAP_FAILED) (void)munmap(mapping, sizeof bytes);
    if (fd >= 0) (void)close(fd);
    return result;
}
typedef struct held_source {
    flat data;
    _Atomic bool entered, release;
    uint64_t fail_at;
} held_source;
static size_t held_span(void *ctx, uint64_t off, const uint8_t **out)
{
    held_source *f = ctx;
    atomic_store(&f->entered, true);
    while (!atomic_load(&f->release)) {
        struct timespec pause = {0, 100000};
        (void)nanosleep(&pause, NULL);
    }
    if (off >= f->fail_at) { *out = NULL; return 0; }
    size_t n = span(&f->data, off, out);
    if (n > f->fail_at - off) n = (size_t)(f->fail_at - off);
    return n;
}
static int resident_failure_and_retirement(void)
{
    int result = 1;
    work_pool *pool = aligned_alloc(_Alignof(work_pool), sizeof *pool);
    lineidx *index = NULL;
    bool initialized = false, resident_initialized = false;
    scroll_resident resident;
    uint8_t bytes[4096];
    memset(bytes, 'x', sizeof bytes);
    for (size_t i = 9; i < sizeof bytes; i += 10) bytes[i] = '\n';
    held_source f = {{bytes, sizeof bytes}, false, true, 33};
    lineidx_src src = {&f, sizeof bytes, held_span, NULL};
    CHECK(pool && work_pool_init(pool, 1, 0) == 0);
    initialized = true;
    index = lineidx_create(sizeof bytes);
    CHECK(index);
    for (unsigned cancel = 0; cancel < 2; cancel++) {
        atomic_store(&f.entered, false); atomic_store(&f.release, cancel == 0);
        f.fail_at = cancel ? UINT64_MAX : 33;
        CHECK(scroll_resident_init(&resident, pool, &src) == 0);
        resident_initialized = true;
        scroll_state s;
        CHECK(scroll_init(&s, (scroll_config){4, 17, 1}, (scroll_extent){sizeof bytes, 410, false}) == 0);
        CHECK(scroll_seek_line(&s, 100) == 0);
        scroll_state before = s;
        scroll_resolver resolver = {0};
        int rc = scroll_resolve_resident(&s, &resolver, index, &resident, 0, 0);
        CHECK(rc == SCROLL_MORE && memcmp(&s, &before, sizeof s) == 0);
        if (cancel) {
            for (unsigned i = 0; i < 10000 && !atomic_load(&f.entered); i++) {
                struct timespec pause = {0, 100000}; (void)nanosleep(&pause, NULL);
            }
            CHECK(atomic_load(&f.entered));
            CHECK(scroll_resident_close(&resident) == SCROLL_MORE);
            CHECK(scroll_resolve_resident(&s, &resolver, index, &resident, 0, 0) == SCROLL_ERR_ARG);
            CHECK(memcmp(&s, &before, sizeof s) == 0);
            atomic_store(&f.release, true);
        } else {
            for (unsigned i = 0; i < 10000 && rc == SCROLL_MORE; i++) {
                struct timespec pause = {0, 100000}; (void)nanosleep(&pause, NULL);
                rc = scroll_resolve_resident(&s, &resolver, index, &resident, 0, 0);
                CHECK(memcmp(&s, &before, sizeof s) == 0);
            }
            CHECK(rc == SCROLL_ERR_SOURCE);
            CHECK(lineidx_built_prefix(index) == 0);
        }
        int closed = SCROLL_MORE;
        for (unsigned i = 0; i < 10000 && closed == SCROLL_MORE; i++) {
            closed = scroll_resident_close(&resident);
            struct timespec pause = {0, 100000}; (void)nanosleep(&pause, NULL);
        }
        CHECK(closed == 0);
        resident_initialized = false;
    }
    puts("scroll_test: partial worker read discarded; cancellation retains source until physical retirement PASS");
    result = 0;
cleanup:
    atomic_store(&f.release, true);
    if (resident_initialized) (void)scroll_resident_close(&resident);
    if (initialized) work_pool_shutdown(pool);
    lineidx_destroy(index); free(pool);
    return result;
}
typedef struct counted_source { flat data; size_t calls; } counted_source;
static size_t one_byte_span(void *ctx, uint64_t off, const uint8_t **out)
{
    counted_source *f = ctx;
    f->calls++;
    if (off >= f->data.size) return 0;
    *out = f->data.bytes + off;
    return 1;
}
static int complete_operation_budget(void)
{
    int result = 1;
    uint8_t bytes[2u * SCROLL_SCAN_BUDGET];
    memset(bytes, 'x', sizeof bytes);
    counted_source f = {{bytes, sizeof bytes}, 0};
    lineidx_src src = {&f, sizeof bytes, one_byte_span, NULL};
    lineidx *index = lineidx_create(sizeof bytes);
    CHECK(index);
    scroll_state s;
    CHECK(scroll_init(&s, (scroll_config){4, 17, 1}, (scroll_extent){sizeof bytes, 1, false}) == 0);
    CHECK(scroll_seek_byte(&s, sizeof bytes - 1u) == 0);
    scroll_state before = s;
    CHECK(scroll_resolve(&s, index, &src, 0) == SCROLL_MORE);
    CHECK(f.calls <= 256u);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    f.calls = 0;
    CHECK(scroll_resolve(&s, index, &src, 7) == SCROLL_MORE);
    CHECK(f.calls <= 7u);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    scroll_resolver resolver = {0};
    int rc = SCROLL_MORE;
    unsigned slices = 0;
    while (rc == SCROLL_MORE && slices++ < 100000) {
        f.calls = 0;
        rc = scroll_resolve_slice(&s, &resolver, index, &src, 7, 0);
        CHECK(f.calls <= 7u);
        if (rc == SCROLL_MORE) CHECK(memcmp(&s, &before, sizeof s) == 0);
    }
    CHECK(rc == SCROLL_OK && slices > 1u && s.first_byte == 0);
    puts("scroll_test: complete-operation callback/byte budget and resumable alignment/clipping PASS");
    result = 0;
cleanup:
    lineidx_destroy(index);
    return result;
}
static int follow_operation_budget(void)
{
    int result = 1;
    uint8_t bytes[4096];
    memset(bytes, 'x', sizeof bytes);
    for (size_t i = 9; i < sizeof bytes; i += 10) bytes[i] = '\n';
    counted_source f = {{bytes, sizeof bytes}, 0};
    lineidx_src src = {&f, sizeof bytes, one_byte_span, NULL};
    lineidx *index = lineidx_create(sizeof bytes);
    CHECK(index);
    for (unsigned indexed = 0; indexed < 2; indexed++) {
        if (indexed) {
            for (unsigned i = 0; i < 1000 && !lineidx_complete(index); i++)
                (void)lineidx_seek_line(index, &src, UINT64_MAX, SCROLL_SCAN_BUDGET);
            CHECK(lineidx_complete(index));
        }
        scroll_state s;
        CHECK(scroll_init(&s, (scroll_config){4, 17, 1},
                          (scroll_extent){sizeof bytes, 410, indexed != 0}) == 0);
        scroll_state before = s;
        scroll_resolver r = {0};
        f.calls = 0;
        CHECK(scroll_follow_cursor_slice(&s, &r, index, &src, 1000, 7, 1) == SCROLL_MORE);
        CHECK(f.calls == 0 && memcmp(&s, &before, sizeof s) == 0);
        int rc = SCROLL_MORE;
        unsigned slices = 0;
        while (rc == SCROLL_MORE && slices++ < 10000) {
            f.calls = 0;
            rc = scroll_follow_cursor_slice(&s, &r, index, &src, 1000, 7, 0);
            CHECK(f.calls <= 7u);
            if (rc == SCROLL_MORE) CHECK(memcmp(&s, &before, sizeof s) == 0);
        }
        CHECK(rc == 0 && s.first_byte == 980 && slices > 1u);
        /* A changed intent cancels the continuation, including its query seed. */
        CHECK(scroll_seek_byte(&s, 3000) == 0);
        CHECK(scroll_resolve_slice(&s, &r, index, &src, 7, 0) == SCROLL_MORE);
        CHECK(scroll_seek_byte(&s, 0) == 0);
        int restarted = SCROLL_MORE;
        for (unsigned i = 0; i < 1000 && restarted == SCROLL_MORE; i++)
            restarted = scroll_resolve_slice(&s, &r, index, &src, 7, 0);
        CHECK(restarted == 0 && s.first_byte == 0);
    }
    puts("scroll_test: indexed/unindexed follow, delegated reads and expired deadline share slice PASS");
    result = 0;
cleanup:
    lineidx_destroy(index);
    return result;
}
typedef struct failing_source {
    flat data;
    uint64_t fail_at;
    bool null_pointer;
} failing_source;
static size_t failing_span(void *ctx, uint64_t off, const uint8_t **out)
{
    failing_source *f = ctx;
    if (off >= f->fail_at) {
        *out = NULL;
        return f->null_pointer ? 1u : 0u;
    }
    size_t n = span(&f->data, off, out);
    if (n > f->fail_at - off) n = (size_t)(f->fail_at - off);
    return n;
}
static int delegated_source_errors(void)
{
    int result = 1;
    const uint8_t bytes[] = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj";
    failing_source f = {{bytes, sizeof bytes - 1}, UINT64_MAX, false};
    lineidx_src src = {&f, f.data.size, failing_span, NULL};
    lineidx *index = lineidx_create(f.data.size);
    CHECK(index);
    for (unsigned indexed = 0; indexed < 2; indexed++) {
        if (indexed) {
            f.fail_at = UINT64_MAX;
            for (unsigned i = 0; i < 100 && !lineidx_complete(index); i++)
                (void)lineidx_seek_line(index, &src, UINT64_MAX, LINEIDX_CHUNK);
            CHECK(lineidx_complete(index));
        }
        for (unsigned partial = 0; partial < 2; partial++) {
            scroll_state s;
            CHECK(scroll_init(&s, (scroll_config){4, 16, 1},
                              (scroll_extent){f.data.size, 10, indexed != 0}) == 0);
            CHECK(scroll_seek_line(&s, 2) == 0);
            scroll_state before = s;
            f.fail_at = partial ? 2u : 0u;
            CHECK(scroll_resolve(&s, index, &src, 0) == SCROLL_ERR_SOURCE);
            CHECK(memcmp(&s, &before, sizeof s) == 0);
            CHECK(lineidx_built_prefix(index) == indexed);
        }
    }
    /* Idle relabelling and cursor queries must propagate a partial read too. */
    f.fail_at = UINT64_MAX;
    scroll_state s;
    CHECK(scroll_init(&s, (scroll_config){4, 16, 1}, (scroll_extent){f.data.size, 10, true}) == 0);
    CHECK(scroll_seek_line(&s, 2) == 0 && scroll_resolve(&s, index, &src, 0) == 0);
    scroll_state before = s;
    f.fail_at = 2;
    CHECK(scroll_resolve(&s, index, &src, 0) == SCROLL_ERR_SOURCE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    CHECK(scroll_follow_cursor(&s, index, &src, 16) == SCROLL_ERR_SOURCE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    /* A positive count with a NULL pointer is also a source failure. */
    f.null_pointer = true;
    CHECK(scroll_resolve(&s, index, &src, 0) == SCROLL_ERR_SOURCE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    puts("scroll_test: delegated indexed/unindexed/partial/NULL source failures preserve state PASS");
    result = 0;
cleanup:
    lineidx_destroy(index);
    return result;
}
static int bounded_and_allocations(void)
{
    int result = 1;
    bool guarding = false;
    uint8_t bytes[3u * LINEIDX_CHUNK];
    memset(bytes, 'x', sizeof bytes); bytes[1023] = '\n';
    flat f = {bytes, sizeof bytes};
    lineidx_src src = {&f, sizeof bytes, span, NULL};
    lineidx *index = lineidx_create(sizeof bytes);
    CHECK(index);
    scroll_state s;
    CHECK(scroll_init(&s, (scroll_config){1, 17, 0}, (scroll_extent){sizeof bytes, 100, false}) == 0);
    CHECK(scroll_follow_cursor(&s, index, &src, 150000) == SCROLL_MORE && s.first_byte == 0);
    CHECK(scroll_seek_byte(&s, 1024) == 0 && settle_resolve(&s, index, &src, 0) == 0 && s.first_byte == 1024);
    CHECK(scroll_seek_byte(&s, 180000) == 0 && settle_resolve(&s, index, &src, 0) == 0 && s.first_byte == 1024);
    CHECK(scroll_seek_byte(&s, 180000) == 0);
    scroll_state before = s;
    lineidx_src broken = {&f, sizeof bytes, broken_span, NULL};
    CHECK(settle_resolve(&s, index, &broken, 0) == SCROLL_ERR_SOURCE && memcmp(&s, &before, sizeof s) == 0);
    CHECK(settle_resolve(&s, index, &src, 0) == 0);
    edit_malloc_guard_begin();
    guarding = true;
    CHECK(scroll_seek_line(&s, 1) == 0 && settle_resolve(&s, index, &src, 1) == 0);
    CHECK(lineidx_built_prefix(index) == 0); /* seek respects the one-byte budget */
    CHECK(scroll_wheel(&s, 1) == 0 && settle_resolve(&s, index, &src, 0) == 0 && s.subrow_q8 == 51);
    CHECK(scroll_wheel(&s, -256) == 0 && settle_resolve(&s, index, &src, 0) == 0 && s.first_byte == 0 && s.subrow_q8 == 0);
    for (unsigned i = 0; i < 1000; i++) {
        CHECK(scroll_wheel(&s, (i & 1u) ? -1 : 1) == 0);
        CHECK(settle_resolve(&s, index, &src, 0) == 0);
        CHECK(scroll_resize(&s, (scroll_config){1, 17, 0}) == 0);
        CHECK(settle_resolve(&s, index, &src, 0) == 0);
        CHECK(scroll_follow(&s, 1) == 0 && settle_resolve(&s, index, &src, 0) == 0);
        CHECK(settle_follow(&s, index, &src, s.first_byte) == 0);
    }
    size_t allocations = edit_malloc_guard_end();
    guarding = false;
    CHECK(allocations == 0);
    printf("scroll_test: bounded long-line/source errors/partial seeks PASS; allocations=%zu guard=%s\n", allocations, edit_malloc_guard_active() ? "active" : "sanitizer-inert");
    result = 0;
cleanup:
    if (guarding) (void)edit_malloc_guard_end();
    lineidx_destroy(index);
    return result;
}
static int follow_proof(void)
{
    int result = 1;
    bool guarding = false;
    uint8_t bytes[3u * LINEIDX_CHUNK];
    memset(bytes, 'x', sizeof bytes); bytes[1023] = '\n';
    flat f = {bytes, sizeof bytes};
    lineidx_src src = {&f, sizeof bytes, span, NULL};
    lineidx *index = lineidx_create(sizeof bytes);
    CHECK(index);
    scroll_state s, before;
    edit_malloc_guard_begin();
    guarding = true;
    CHECK(scroll_init(&s, (scroll_config){1, 17, 0}, (scroll_extent){sizeof bytes, 100, false}) == 0);
    before = s;
    CHECK(scroll_follow_cursor(&s, index, &src, 150000) == SCROLL_MORE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    CHECK(settle_follow(&s, index, &src, 1024) == 0 && s.first_byte == 1024);
    CHECK(scroll_pixels(&s, 701) == 0 && settle_resolve(&s, index, &src, 0) == 0);
    before = s;
    CHECK(scroll_follow_cursor(&s, index, &src, 150000) == SCROLL_MORE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    /* A full bounded walk from the anchor proves even a newline-free row. */
    CHECK(settle_follow(&s, index, &src, 1024 + SCROLL_SCAN_BUDGET) == 0);
    CHECK(s.first_byte == 1024 && s.subrow_q8 == 0);
    /* No chunks have been published: change only the borrowed test fixture. */
    CHECK(lineidx_built_prefix(index) == 0);
    bytes[149999] = '\n';
    CHECK(settle_follow(&s, index, &src, 150005) == 0 && s.first_byte == 150000);
    /* A proven cursor seed does not prove distant preceding margin rows. */
    CHECK(scroll_init(&s, (scroll_config){5, 17, 1}, (scroll_extent){sizeof bytes, 100, false}) == 0);
    before = s;
    CHECK(scroll_follow_cursor(&s, index, &src, 150005) == SCROLL_MORE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    /* The cursor slot is reachable, but filling the viewport at EOF needs
     * one more preceding row beyond the bounded backtracking window. */
    bytes[149999] = 'x';
    for (size_t i = 188000; i <= 188030; i += 10) bytes[i] = '\n';
    CHECK(scroll_follow_cursor(&s, index, &src, 190000) == SCROLL_MORE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    memset(bytes, 'x', sizeof bytes); bytes[1023] = '\n';
    CHECK(scroll_init(&s, (scroll_config){1, 17, 0}, (scroll_extent){sizeof bytes, 100, false}) == 0);
    before = s;
    CHECK(!lineidx_seek_line(index, &src, UINT64_MAX, 1).exact);
    CHECK(lineidx_built_prefix(index) == 0); /* one byte cannot build a chunk */
    for (unsigned slice = 0; slice < 20000 && lineidx_built_prefix(index) == 0; slice++)
        (void)lineidx_seek_line(index, &src, UINT64_MAX, LINEIDX_CHUNK);
    CHECK(lineidx_built_prefix(index) == 1);
    CHECK(scroll_follow_cursor(&s, index, &src, 150000) == SCROLL_MORE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    for (unsigned slice = 0; slice < 20000 && !lineidx_complete(index); slice++)
        (void)lineidx_seek_line(index, &src, UINT64_MAX, sizeof bytes);
    CHECK(lineidx_complete(index));
    CHECK(settle_follow(&s, index, &src, 150000) == 0);
    CHECK(s.first_byte == 1024 && s.first_line == 1 && !s.approximate);
    size_t allocations = edit_malloc_guard_end();
    guarding = false;
    CHECK(allocations == 0);
    printf("scroll_test: bounded cursor proof/margins/publication retry PASS; allocations=%zu guard=%s\n", allocations, edit_malloc_guard_active() ? "active" : "sanitizer-inert");
    result = 0;
cleanup:
    if (guarding) (void)edit_malloc_guard_end();
    lineidx_destroy(index);
    return result;
}
int main(void)
{
    if (resident_failure_and_retirement() || resident_navigation() || complete_operation_budget() || follow_operation_budget() || delegated_source_errors() || accumulation() || keys_and_follow() || correction() || unindexed_ends() || follow_proof() || bounded_and_allocations()) return 1;
    puts("scroll_test: PASS");
    return 0;
}
