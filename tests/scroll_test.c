#include "scroll/scroll.h"
#include "base/base.h"
#include "editor/editor.h"
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
typedef struct wrapped_fixture {
    uint64_t bytes;
    uint32_t width, calls;
    bool pending, fail, malformed, eof;
} wrapped_fixture;
static int wrapped_row(void *ctx, uint64_t ordinal, scroll_visual_row *out)
{
    wrapped_fixture *f = ctx;
    f->calls++;
    if (f->pending) return SCROLL_MORE;
    if (f->fail) return SCROLL_ERR_SOURCE;
    uint64_t rows = (f->bytes + f->width - 1u) / f->width;
    if (!rows) rows = 1;
    bool past = ordinal >= rows;
    if (past && !f->eof) return SCROLL_ERR_SOURCE;
    if (past) ordinal = rows - 1;
    uint64_t start = ordinal * f->width;
    uint64_t end = start + f->width;
    if (end > f->bytes) end = f->bytes;
    *out = (scroll_visual_row){.ordinal = ordinal, .byte = start,
        .end = end, .column = start, .indent = ordinal ? 2u : 0u};
    if (f->malformed) out->ordinal++;
    return past ? SCROLL_EOF : SCROLL_OK;
}
static int wrapped_locate(void *ctx, uint64_t byte, bool trailing, scroll_visual_row *out)
{
    wrapped_fixture *f = ctx;
    if (byte && (byte == f->bytes || (trailing && byte % f->width == 0))) byte--;
    return wrapped_row(ctx, byte / f->width, out);
}
static int wrapped_navigation(void)
{
    int result = 1;
    bool guarding = false;
    wrapped_fixture f = {200, 5, 0, false, false, false, false};
    scroll_visual_source source = {&f, 200, 40, 1, wrapped_row, wrapped_locate, true};
    scroll_visual_row seed;
    CHECK(wrapped_row(&f, 0, &seed) == 0);
    scroll_visual s;
    scroll_visual_resolver r = {0};
    view_key motion;
    CHECK(scroll_visual_init(&s, (scroll_config){4, 17, 1}, &source, seed) == 0);
    edit_malloc_guard_begin(); guarding = true;
    CHECK(scroll_visual_wheel(&s, 1) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0);
    CHECK(s.first.byte == 0 && s.viewport.subrow_q8 == 51);
    CHECK(scroll_visual_wheel(&s, 255) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0);
    CHECK(s.first.ordinal == 3 && s.first.byte == 15 && s.first.indent == 2 && s.viewport.subrow_q8 == 0);
    CHECK(scroll_visual_key_motion(&s, SCROLL_PAGE_DOWN, &motion) == 0 && motion == VIEW_PAGE_DOWN);
    scroll_visual before = s;
    f.calls = 0;
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 1) == SCROLL_MORE && f.calls == 0);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    f.pending = true;
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == SCROLL_MORE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    f.pending = false;
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0);
    CHECK(s.first.ordinal == 7 && s.first.byte == 35);
    /* Width reflow uses the old byte anchor, not the obsolete row ordinal. */
    f.width = 10; source.rows = 20; source.generation++;
    CHECK(scroll_visual_resize(&s, (scroll_config){4, 17, 1}, &source) == 0);
    before = s; f.calls = 0;
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == SCROLL_MORE && f.calls == 1);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0);
    CHECK(s.first.ordinal == 3 && s.first.byte == 30);
    CHECK(scroll_visual_follow_slice(&s, &r, 30, true, 2, 0) == 0);
    CHECK(s.first.ordinal == 1); /* trailing cursor belongs to row 2 */
    CHECK(scroll_visual_follow_slice(&s, &r, 70, false, 2, 0) == 0);
    CHECK(s.first.ordinal == 5); /* leading cursor belongs to row 7 */
    before = s; f.calls = 0;
    CHECK(scroll_visual_follow_slice(&s, &r, 200, false, 1, 0) == SCROLL_MORE && f.calls == 1);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    CHECK(scroll_visual_follow_slice(&s, &r, 200, false, 1, 0) == 0);
    CHECK(s.first.ordinal == 16 && s.first.byte == 160);
    CHECK(scroll_visual_wheel(&s, INT32_MAX) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0 && s.first.ordinal == 16 && s.viewport.subrow_q8 == 0);
    CHECK(scroll_visual_key_motion(&s, SCROLL_HOME, &motion) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0 && s.first.byte == 0);
    CHECK(scroll_visual_key_motion(&s, SCROLL_END, &motion) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0 && s.first.byte == 160);
    CHECK(scroll_visual_key_motion(&s, SCROLL_PAGE_UP, &motion) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0 && s.first.byte == 120);
    CHECK(scroll_visual_resize(&s, (scroll_config){50, 9, 20}, &source) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 2, 0) == 0 && s.first.byte == 0);
    CHECK(scroll_visual_resize(&s, (scroll_config){4, 17, 1}, &source) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 2, 0) == 0);
    for (unsigned i = 0; i < 1000; i++) {
        CHECK(scroll_visual_wheel(&s, (i & 1u) ? -256 : 256) == 0);
        CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0);
    }
    CHECK(scroll_visual_follow_slice(&s, &r, 100, false, 1, 0) == SCROLL_MORE);
    /* New wheel intent cancels a partially resolved follow. */
    CHECK(scroll_visual_wheel(&s, 256) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0 && s.first.ordinal == 3);
    before = s; f.fail = true;
    CHECK(scroll_visual_follow_slice(&s, &r, 100, false, 2, 0) == SCROLL_ERR_SOURCE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    f.fail = false; f.malformed = true;
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == SCROLL_ERR_SOURCE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    f.malformed = false;
    /* A lazy wrap producer must not require a whole-buffer visual count. */
    source.rows = 1; source.exact = false; source.generation++; f.eof = true;
    CHECK(scroll_visual_resize(&s, (scroll_config){4, 17, 1}, &source) == 0);
    int rc = SCROLL_MORE;
    for (unsigned i = 0; i < 10 && rc == SCROLL_MORE; i++) {
        f.calls = 0;
        rc = scroll_visual_resolve_slice(&s, &r, 1, 0);
        CHECK(f.calls <= 1);
    }
    CHECK(rc == 0 && s.first.ordinal == 3 && !s.source.exact);
    CHECK(scroll_visual_key_motion(&s, SCROLL_END, &motion) == 0);
    CHECK(scroll_visual_wheel(&s, -256) == 0);
    rc = SCROLL_MORE;
    for (unsigned i = 0; i < 10 && rc == SCROLL_MORE; i++) rc = scroll_visual_resolve_slice(&s, &r, 1, 0);
    CHECK(rc == 0 && s.first.ordinal == 0);
    CHECK(scroll_visual_key_motion(&s, SCROLL_END, &motion) == 0);
    CHECK(scroll_visual_key_motion(&s, SCROLL_PAGE_DOWN, &motion) == 0);
    rc = SCROLL_MORE;
    for (unsigned i = 0; i < 10 && rc == SCROLL_MORE; i++) rc = scroll_visual_resolve_slice(&s, &r, 1, 0);
    CHECK(rc == 0 && s.first.ordinal == 4);
    CHECK(scroll_visual_key_motion(&s, SCROLL_END, &motion) == 0);
    CHECK(scroll_visual_follow_slice(&s, &r, 70, false, 2, 0) == SCROLL_MORE);
    CHECK(scroll_visual_follow_slice(&s, &r, 70, false, 2, 0) == 0);
    CHECK(s.first.ordinal == 5);
    CHECK(scroll_visual_key_motion(&s, SCROLL_END, &motion) == 0);
    rc = SCROLL_MORE;
    for (unsigned i = 0; i < 10 && rc == SCROLL_MORE; i++) rc = scroll_visual_resolve_slice(&s, &r, 1, 0);
    CHECK(rc == 0 && s.first.ordinal == 16 && s.source.rows == 20 && s.source.exact);
    /* Discover EOF by bottom lookahead, not just by stepping past last row. */
    CHECK(scroll_visual_resize(&s, (scroll_config){4, 17, 1}, &source) == 0);
    rc = SCROLL_MORE;
    for (unsigned i = 0; i < 10 && rc == SCROLL_MORE; i++) rc = scroll_visual_resolve_slice(&s, &r, 1, 0);
    CHECK(rc == 0 && !s.source.exact);
    CHECK(scroll_visual_wheel(&s, 256) == 0);
    rc = SCROLL_MORE;
    for (unsigned i = 0; i < 10 && rc == SCROLL_MORE; i++) rc = scroll_visual_resolve_slice(&s, &r, 1, 0);
    CHECK(rc == 0 && s.first.ordinal == 16 && s.source.exact);
    CHECK(scroll_visual_key_motion(&s, SCROLL_HOME, &motion) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0);
    CHECK(scroll_visual_pixels(&s, 16 * 256) == 0);
    source.rows = 20; source.exact = true;
    CHECK(scroll_visual_resize(&s, (scroll_config){4, 5, 1}, &source) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 2, 0) == 0);
    CHECK(s.first.ordinal == 3 && s.viewport.subrow_q8 == 256);
    size_t allocations = edit_malloc_guard_end(); guarding = false;
    CHECK(allocations == 0);
    printf("scroll_test: wrapped wheel/page/reflow/EOF/follow affinity, bounded pending/error/cancel PASS; allocations=%zu guard=%s\n",
        allocations, edit_malloc_guard_active() ? "active" : "sanitizer-inert");
    result = 0;
cleanup:
    if (guarding) (void)edit_malloc_guard_end();
    return result;
}
static int renderer_origin(void)
{
    int result = 1;
    scroll_state s;
    scroll_frame_plan p, before;
    uint32_t row;
    uint64_t within;
    CHECK(scroll_init(&s, (scroll_config){4, 17, 1}, (scroll_extent){100, 20, true}) == 0);
    CHECK(scroll_plan_frame(&s, 80, 68, NULL, &p) == 0);
    CHECK(p.origin_y_q8 == 0 && p.layout_rows == 5 && p.full_damage);
    before = p;
    CHECK(scroll_wheel(&s, 1) == 0);
    CHECK(scroll_plan_frame(&s, 80, 68, &before, &p) == 0);
    CHECK(p.origin_y_q8 == -51 && p.clip_height == 68 && p.full_damage);
    CHECK(scroll_frame_hit(&p, 0, &row, &within) == 0 && row == 0 && within == 51);
    CHECK(scroll_frame_hit(&p, 68 * 256 - 1, &row, &within) == 0 && row == 4 && within == 50);
    /* Last laid-out row covers the fractional lower edge. */
    CHECK((int64_t)p.layout_rows * 17 * 256 + p.origin_y_q8 >= 68 * 256);
    before = p;
    CHECK(scroll_plan_frame(&s, 80, 68, &before, &p) == 0 && !p.full_damage);
    before = p;
    CHECK(scroll_frame_hit(&p, 68 * 256, &row, &within) == SCROLL_ERR_ARG);
    CHECK(scroll_frame_hit(&p, -1, &row, &within) == SCROLL_ERR_ARG);
    CHECK(scroll_plan_frame(&s, 80, 69, NULL, &p) == SCROLL_ERR_ARG);
    CHECK(memcmp(&p, &before, sizeof p) == 0);
    CHECK(scroll_wheel(&s, -1) == 0);
    CHECK(scroll_plan_frame(&s, 80, 68, &before, &p) == 0 && p.origin_y_q8 == 0 && p.full_damage);
    puts("scroll_test: Q8 origin/clip/overscan, inverse hit transform and damage PASS");
    result = 0;
cleanup:
    return result;
}
static int huge_visual_row(void *ctx, uint64_t ordinal, scroll_visual_row *out)
{
    (void)ctx;
    *out = (scroll_visual_row){.ordinal = ordinal, .byte = ordinal,
        .end = ordinal, .line_start = ordinal, .line = ordinal};
    return 0;
}
static int huge_visual_locate(void *ctx, uint64_t byte, bool trailing, scroll_visual_row *out)
{ (void)trailing; return huge_visual_row(ctx, byte, out); }
static int wrapped_integer_limits(void)
{
    int result = 1;
    scroll_visual_source source = {NULL, LINEIDX_MAX_LEN, UINT64_MAX, 1,
        huge_visual_row, huge_visual_locate, true};
    scroll_visual_row seed = {0};
    scroll_visual s;
    scroll_visual_resolver r = {0};
    view_key motion;
    CHECK(scroll_visual_init(&s, (scroll_config){4, 17, 1}, &source, seed) == 0);
    CHECK(scroll_visual_key_motion(&s, SCROLL_END, &motion) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0);
    CHECK(s.first.ordinal == UINT64_MAX - 4u);
    CHECK(scroll_visual_pixels(&s, -1) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 1, 0) == 0);
    CHECK(scroll_visual_resize(&s, (scroll_config){4, 1, 1}, &source) == 0);
    CHECK(scroll_visual_resolve_slice(&s, &r, 2, 0) == 0);
    CHECK(s.first.ordinal == UINT64_MAX - 4u && s.viewport.subrow_q8 == 0);
    puts("scroll_test: wrapped resize carry saturates at integer-limit EOF PASS");
    result = 0;
cleanup:
    return result;
}
typedef struct frame_observer {
    uint32_t submitted_id, presented_id;
    size_t submissions, presentations;
} frame_observer;
static void observe_submit(void *ctx, const editor_frame *frame)
{
    frame_observer *o = ctx;
    o->submitted_id = frame->id; o->submissions++;
}
static void observe_present(void *ctx, const editor_frame *frame)
{
    frame_observer *o = ctx;
    o->presented_id = frame->id; o->presentations++;
}
static int settle_editor(editor *e)
{
    for (unsigned i = 0; i < 10000; i++) {
        int rc = editor_step(e, 0);
        if (rc != EDITOR_OK && rc != EDITOR_MORE) return rc;
        editor_stats stats = editor_get_stats(e);
        if (!stats.pending && !stats.render_active) return 0;
    }
    return EDITOR_MORE;
}
/* Capability probe, not a displayed-cadence verdict. --require-editor-scroll
 * preserves the failing integration check for the editor owner's follow-up. */
static int editor_observability(bool require_scroll)
{
    int result = 1;
    editor *e = NULL;
    render_backend backend = {0};
    frame_observer observer = {0};
    uint8_t bytes[400];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = i % 10 == 9 ? '\n' : (uint8_t)'a';
    editor_config config = {.initial = bytes, .initial_len = sizeof bytes,
        .cols = 32, .rows = 8, .wrap_mode = -1, .hook_ctx = &observer,
        .on_submit = observe_submit, .on_present = observe_present};
    CHECK(render_null_backend(&backend) == 0);
    CHECK(editor_open(&e, &config, &backend) == 0 && settle_editor(e) == 0);
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false};
    CHECK(editor_inject(e, &focus) == 0 && settle_editor(e) == 0);
    CHECK(observer.submissions && observer.submissions == observer.presentations);
    CHECK(observer.submitted_id == observer.presented_id);
    size_t baseline = observer.submissions;
    plat_event wheel = {.kind = PLAT_EV_WHEEL, .dy = 1, .smooth = true};
    CHECK(editor_inject(e, &wheel) == 0 && settle_editor(e) == 0);
    wheel.dy = 256;
    CHECK(editor_inject(e, &wheel) == 0 && settle_editor(e) == 0);
    if (require_scroll) CHECK(observer.submissions > baseline);
    else {
        /* The absence is explicit and must not be mistaken for a G3z pass. */
        CHECK(observer.submissions == observer.presentations);
        CHECK(observer.submitted_id == observer.presented_id);
        printf("scroll_test: public editor/null frame IDs correlate at submit/present; wheel frames=%s; displayed G3z UNAVAILABLE (hook required) PASS\n",
            observer.submissions > baseline ? "available" : "absent");
    }
    result = 0;
cleanup:
    editor_close(e);
    return result;
}
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--require-editor-scroll") == 0)
        return editor_observability(true);
    if (argc != 1) return 2;
    if (renderer_origin()) return 1;
    if (wrapped_navigation()) return 1;
    if (wrapped_integer_limits()) return 1;
    if (editor_observability(false)) return 1;
    if (resident_failure_and_retirement() || resident_navigation() || complete_operation_budget() || follow_operation_budget() || delegated_source_errors() || accumulation() || keys_and_follow() || correction() || unindexed_ends() || follow_proof() || bounded_and_allocations()) return 1;
    puts("scroll_test: PASS");
    return 0;
}
