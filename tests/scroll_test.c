#include "scroll/scroll.h"
#include "base/base.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

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
    CHECK(scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == 150007 - 11 && s.approximate);
    uint64_t start = s.first_byte;
    CHECK(scroll_pixels(&s, 701) == 0 && scroll_resolve(&s, index, &src, 0) == 0);
    CHECK(scroll_wheel(&s, 256) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == start + 33);
    CHECK(scroll_wheel(&s, -256) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == start);
    uint64_t cursor = start + 11 * 7 + 3, row_before = (cursor - start) / 11;
    /* Publication can land BETWEEN an event transition and its resolution. */
    CHECK(scroll_wheel(&s, 256) == 0);
    for (unsigned slice = 0; slice < 20000 && !lineidx_complete(index); slice++)
        (void)lineidx_seek_line(index, &src, UINT64_MAX, sizeof bytes);
    CHECK(lineidx_complete(index));
    CHECK(scroll_resolve(&s, index, &src, 0) == 0 && !s.approximate);
    CHECK(s.first_byte == start + 33 && s.first_line == start / 11 + 3 && s.subrow_q8 == 701);
    CHECK(scroll_wheel(&s, -256) == 0 && scroll_resolve(&s, index, &src, 0) == 0);
    CHECK((cursor - s.first_byte) / 11 == row_before);
    CHECK(scroll_seek_line(&s, 12345) == 0 && scroll_resolve(&s, index, &src, 0) == 0);
    CHECK(s.first_line == 12345 && s.first_byte == 12345 * 11);
    scroll_state before = s;
    CHECK(scroll_resolve(&s, index, &src, SCROLL_SCAN_BUDGET + 1u) == SCROLL_ERR_ARG && memcmp(&s, &before, sizeof s) == 0);
    edit_malloc_guard_begin();
    guarding = true;
    for (unsigned i = 0; i < 10000; i++) {
        CHECK(scroll_wheel(&s, (i & 1u) ? -17 : 17) == 0);
        CHECK(scroll_resolve(&s, index, &src, 0) == 0);
        CHECK(scroll_follow(&s, s.first_line + 7) == 0);
        CHECK(scroll_resolve(&s, index, &src, 0) == 0);
        CHECK(scroll_follow_cursor(&s, index, &src, s.first_byte + 11 * 7) == 0);
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
    CHECK(scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == 12);
    CHECK(scroll_key_motion(&s, SCROLL_HOME, &motion) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == 0);
    for (unsigned i = 0; i < 5; i++) {
        CHECK(scroll_key_motion(&s, SCROLL_PAGE_DOWN, &motion) == 0);
        CHECK(scroll_resolve(&s, index, &src, 0) == 0);
    }
    CHECK(s.first_byte == 12 && s.subrow_q8 == 0);
    CHECK(scroll_wheel(&s, -256) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == 6);
    /* Estimated line label is zero here; physical wheel scrolling must still
     * reach the real beginning and clip its sub-row remainder. */
    CHECK(scroll_wheel(&s, -257) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == 0 && s.subrow_q8 == 0);
    CHECK(scroll_follow_cursor(&s, index, &src, 18) == 0 && s.first_byte == 12);
    CHECK(scroll_follow_cursor(&s, index, &src, 0) == 0 && s.first_byte == 0);
    puts("scroll_test: unindexed page/document ends PASS");
    result = 0;
cleanup:
    lineidx_destroy(index);
    return result;
}
static size_t broken_span(void *ctx, uint64_t off, const uint8_t **out)
{ (void)ctx; (void)off; (void)out; return 0; }
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
    CHECK(scroll_seek_byte(&s, 1024) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == 1024);
    CHECK(scroll_seek_byte(&s, 180000) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == 1024);
    CHECK(scroll_seek_byte(&s, 180000) == 0);
    scroll_state before = s;
    lineidx_src broken = {&f, sizeof bytes, broken_span, NULL};
    CHECK(scroll_resolve(&s, index, &broken, 0) == SCROLL_ERR_SOURCE && memcmp(&s, &before, sizeof s) == 0);
    CHECK(scroll_resolve(&s, index, &src, 0) == 0);
    edit_malloc_guard_begin();
    guarding = true;
    CHECK(scroll_seek_line(&s, 1) == 0 && scroll_resolve(&s, index, &src, 1) == 0);
    CHECK(lineidx_built_prefix(index) == 0); /* seek respects the one-byte budget */
    CHECK(scroll_wheel(&s, 1) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.subrow_q8 == 51);
    CHECK(scroll_wheel(&s, -256) == 0 && scroll_resolve(&s, index, &src, 0) == 0 && s.first_byte == 0 && s.subrow_q8 == 0);
    for (unsigned i = 0; i < 1000; i++) {
        CHECK(scroll_wheel(&s, (i & 1u) ? -1 : 1) == 0);
        CHECK(scroll_resolve(&s, index, &src, 0) == 0);
        CHECK(scroll_resize(&s, (scroll_config){1, 17, 0}) == 0);
        CHECK(scroll_resolve(&s, index, &src, 0) == 0);
        CHECK(scroll_follow(&s, 1) == 0 && scroll_resolve(&s, index, &src, 0) == 0);
        CHECK(scroll_follow_cursor(&s, index, &src, s.first_byte) == 0);
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
    CHECK(scroll_follow_cursor(&s, index, &src, 1024) == 0 && s.first_byte == 1024);
    CHECK(scroll_pixels(&s, 701) == 0 && scroll_resolve(&s, index, &src, 0) == 0);
    before = s;
    CHECK(scroll_follow_cursor(&s, index, &src, 150000) == SCROLL_MORE);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    /* A full bounded walk from the anchor proves even a newline-free row. */
    CHECK(scroll_follow_cursor(&s, index, &src, 1024 + SCROLL_SCAN_BUDGET) == 0);
    CHECK(s.first_byte == 1024 && s.subrow_q8 == 0);
    /* No chunks have been published: change only the borrowed test fixture. */
    CHECK(lineidx_built_prefix(index) == 0);
    bytes[149999] = '\n';
    CHECK(scroll_follow_cursor(&s, index, &src, 150005) == 0 && s.first_byte == 150000);
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
    CHECK(scroll_follow_cursor(&s, index, &src, 150000) == 0);
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
    if (accumulation() || keys_and_follow() || correction() || unindexed_ends() || follow_proof() || bounded_and_allocations()) return 1;
    puts("scroll_test: PASS");
    return 0;
}
