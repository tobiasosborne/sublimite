#include "findui/findui.h"
#include "findui/private.h"
#include "editor/private.h"
#include "trace/trace.h"
#include "journal/journal.h"
#include "../vendor/xkbcommon/xkbcommon-keysyms.h"
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(x) EDIT_ASSERT(x)

typedef struct probe {
    pthread_t caller;
    atomic_uint scans, on_caller;
    atomic_bool park, entered, release;
} probe;
typedef struct fixture {
    edit_arena arena;
    work_pool *pool;
    piece_tree *tree;
    findui_panel panel;
    probe hook;
    work_msg saved;
    bool have_saved;
    size_t allocations, fail_allocation, published_ranges;
} fixture;

static void *arena_alloc(void *context, size_t size)
{ return edit_arena_alloc(context, size, 16); }
static void arena_free(void *context, void *pointer, size_t size)
{ (void)context; (void)pointer; (void)size; }
static void *test_alloc(void *context, size_t size)
{
    fixture *f = context; f->allocations++;
    if (f->fail_allocation == f->allocations) return NULL;
    return arena_alloc(&f->arena, size);
}
static void test_free(void *context, void *pointer, size_t size)
{ fixture *f = context; arena_free(&f->arena, pointer, size); }
static void scan_hook(void *context, uint32_t generation)
{
    probe *p = context;
    (void)generation;
    atomic_fetch_add(&p->scans, 1);
    if (atomic_load(&p->park)) {
        atomic_store(&p->entered, true);
        while (!atomic_load(&p->release)) sched_yield();
    }
    if (pthread_equal(pthread_self(), p->caller)) atomic_fetch_add(&p->on_caller, 1);
}
static void route(const work_msg *message, void *context)
{
    fixture *f = context;
    if (message->kind == FINDUI_MSG_ONE || message->kind == FINDUI_MSG_TWO) {
        f->saved = *message; f->have_saved = true;
        f->published_ranges += message->kind == FINDUI_MSG_TWO ? 2u : 1u;
    }
    (void)findui_accept(&f->panel, message);
}
static void pause_worker(void)
{ const struct timespec delay = {0, 100000}; (void)nanosleep(&delay, NULL); }
static void pump(fixture *f)
{
    CHECK(findui_service(&f->panel) <= FINDUI_MORE);
    (void)work_mailbox_drain(f->pool, route, f);
}
static void wait_result(fixture *f)
{
    for (size_t i = 0; i < 100000; i++) {
        pump(f);
        if (!findui_get_state(&f->panel).searching) return;
        pause_worker();
    }
    CHECK(false); /* find worker did not finish */
}
static void start(fixture *f, const uint8_t *text, size_t length,
                  size_t cache, size_t visible)
{
    memset(f, 0, sizeof *f);
    f->hook.caller = pthread_self();
    atomic_init(&f->hook.scans, 0); atomic_init(&f->hook.on_caller, 0);
    atomic_init(&f->hook.park, false); atomic_init(&f->hook.entered, false); atomic_init(&f->hook.release, false);
    CHECK(edit_arena_init(&f->arena, 16u * 1024u * 1024u) == 0);
    f->pool = edit_arena_alloc(&f->arena, sizeof *f->pool, _Alignof(work_pool));
    CHECK(f->pool && work_pool_init(f->pool, 1, 0) == 0);
    piece_allocator allocator = {f, test_alloc, test_free};
    f->tree = piece_create(&allocator);
    CHECK(f->tree && piece_init_copy(f->tree, text, length) == 0);
    findui_config config = {&f->arena, f->pool, cache, visible, scan_hook, &f->hook};
    CHECK(findui_init(&f->panel, &config) == FINDUI_OK);
    piece_snapshot *snapshot = piece_snapshot_take(f->tree);
    CHECK(snapshot && findui_set_source(&f->panel, snapshot, 1) == FINDUI_OK);
    piece_snapshot_release(snapshot);
    CHECK(findui_set_window(&f->panel, 0, length) == FINDUI_OK);
    CHECK(findui_show(&f->panel, true, true) == FINDUI_OK);
    CHECK(findui_set_options(&f->panel, (findui_options){false, true, false}) == FINDUI_OK);
}
static void finish(fixture *f)
{
    findui_code code;
    do { code = findui_dispose(&f->panel); if (code == FINDUI_MORE) pause_worker(); }
    while (code == FINDUI_MORE);
    CHECK(code == FINDUI_OK);
    work_pool_shutdown(f->pool);
    (void)work_mailbox_drain_bounded(f->pool, route, f, SIZE_MAX, 0);
    CHECK(atomic_load(&f->hook.on_caller) == 0);
    piece_destroy(f->tree); edit_arena_free(&f->arena);
}
static void query(fixture *f, const char *text)
{ CHECK(findui_set_query(&f->panel, (const uint8_t *)text, strlen(text)) == FINDUI_OK); wait_result(f); }
static void same(piece_tree *tree, const uint8_t *text, size_t length)
{
    uint8_t bytes[512];
    CHECK(length <= sizeof bytes && piece_len(tree) == length);
    CHECK(piece_read(tree, 0, bytes, length) == 0 && memcmp(bytes, text, length) == 0);
}

typedef struct blocker { atomic_bool entered, release; } blocker;
static void block_job(work_ctx *context)
{
    blocker *b = context->arg;
    atomic_store(&b->entered, true);
    while (!atomic_load(&b->release) && !work_should_stop(context)) pause_worker();
}
static work_handle generation_handle(const work_pool *pool, uint32_t generation)
{
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++)
        if (atomic_load(&pool->slots[i].busy) && pool->slots[i].job.generation == generation)
            return (work_handle){i, atomic_load(&pool->slots[i].epoch)};
    return (work_handle){0, 0};
}
static void incremental_cancel(void)
{
    fixture f; start(&f, (const uint8_t *)"needle and NEEDLE", 17, 64, 64);
    blocker b; atomic_init(&b.entered, false); atomic_init(&b.release, false);
    work_handle blocked = work_submit(f.pool, (work_job){block_job, &b, 9000, WORK_BULK});
    CHECK(blocked.epoch);
    while (!atomic_load(&b.entered)) pause_worker();
    const char *text = "needle";
    CHECK(findui_set_query(&f.panel, (const uint8_t *)text, 1) == FINDUI_OK);
    for (size_t i = 1; i < strlen(text); i++) {
        findui_state old = findui_get_state(&f.panel);
        work_handle previous = generation_handle(f.pool, old.generation);
        CHECK(previous.epoch);
        CHECK(findui_edit_query(&f.panel, i, 0, (const uint8_t *)text + i, 1) == FINDUI_OK);
        findui_state state = findui_get_state(&f.panel);
        CHECK(state.generation == old.generation + 1);
        CHECK(state.cancel_requests == old.cancel_requests + 1);
        CHECK(atomic_load(&f.pool->slots[previous.slot].epoch) != previous.epoch);
        CHECK(atomic_load(&f.pool->slots[previous.slot].cancel_ns) ||
              atomic_load(&f.pool->slots[previous.slot].epoch) > previous.epoch + 1);
        CHECK(atomic_load(&f.hook.scans) == 0);
    }
    atomic_store(&b.release, true); wait_result(&f);
    CHECK(findui_get_state(&f.panel).match_count == 1);
    CHECK(atomic_load(&f.hook.scans) > 0 && atomic_load(&f.hook.on_caller) == 0);
    CHECK(f.have_saved);
    work_msg stale = f.saved;
    CHECK(findui_set_query(&f.panel, (const uint8_t *)"absent", 6) == FINDUI_OK);
    CHECK(findui_accept(&f.panel, &stale));
    CHECK(findui_get_state(&f.panel).match_count == 0);
    wait_result(&f); CHECK(findui_get_state(&f.panel).complete);
    CHECK(findui_get_state(&f.panel).match_count == 0);
    finish(&f);
    puts("findui_test: incremental cancellation, stale filtering, worker thread-id trace passed");
}
static void wrap_and_visible(void)
{
    fixture f; start(&f, (const uint8_t *)"a a a a a", 9, 2, 8);
    CHECK(findui_set_window(&f.panel, 6, 9) == FINDUI_OK); query(&f, "a");
    findui_state state = findui_get_state(&f.panel);
    CHECK(state.complete && state.match_count == 5 && state.cache_overflow);
    findui_range ranges[8]; size_t count = 0;
    CHECK(findui_highlights(&f.panel, 6, 9, ranges, 8, &count) == FINDUI_OK);
    CHECK(count == 2 && ranges[0].start == 6 && ranges[1].start == 8);
    CHECK(findui_highlights(&f.panel, 0, 9, ranges, 8, &count) == FINDUI_ERR_STALE);
    findui_range selected;
    CHECK(findui_next(&f.panel, -1, &selected) == FINDUI_MORE); wait_result(&f);
    CHECK(findui_get_state(&f.panel).selected.start == 8);
    CHECK(findui_next(&f.panel, 1, &selected) == FINDUI_OK && selected.start == 0);
    CHECK(findui_next(&f.panel, 1, &selected) == FINDUI_OK && selected.start == 2);
    CHECK(findui_next(&f.panel, 1, &selected) == FINDUI_MORE); wait_result(&f);
    CHECK(findui_get_state(&f.panel).selected.start == 4);
    query(&f, "missing");
    CHECK(findui_next(&f.panel, -1, &selected) == FINDUI_OK && selected.start == FIND_UNSET);
    finish(&f);

    start(&f, (const uint8_t *)"aaaa", 4, 8, 1); query(&f, "aa");
    CHECK(findui_highlights(&f.panel, 0, 4, ranges, 8, &count) == FINDUI_ERR_LIMIT);
    CHECK(count == 2);
    finish(&f);
    start(&f, (const uint8_t *)"aaa", 3, 8, 8); query(&f, "aa");
    CHECK(findui_highlights(&f.panel, 1, 2, ranges, 0, &count) == FINDUI_ERR_LIMIT && count == 1);
    CHECK(findui_highlights(&f.panel, 1, 2, ranges, 8, &count) == FINDUI_OK);
    CHECK(count == 1 && ranges[0].start == 1 && ranges[0].end == 2);
    finish(&f);
    puts("findui_test: wrap-around, uncached navigation, late-window highlights and clipping passed");
}
static void toggles(void)
{
    fixture f; start(&f, (const uint8_t *)"ab AB aBB\nz", 11, 64, 64);
    query(&f, "ab+"); CHECK(findui_get_state(&f.panel).match_count == 0);
    CHECK(findui_set_options(&f.panel, (findui_options){true, true, false}) == FINDUI_OK);
    wait_result(&f); CHECK(findui_get_state(&f.panel).match_count == 1);
    CHECK(findui_set_options(&f.panel, (findui_options){true, false, false}) == FINDUI_OK);
    wait_result(&f); CHECK(findui_get_state(&f.panel).match_count == 3);
    query(&f, "[a-z]+"); CHECK(findui_get_state(&f.panel).match_count == 4);
    query(&f, "[^a-z]+"); CHECK(findui_get_state(&f.panel).match_count == 3);
    query(&f, "["); CHECK(findui_get_state(&f.panel).search_error == FIND_ERR_SYNTAX);
    CHECK(!findui_get_state(&f.panel).complete && findui_get_state(&f.panel).match_count == 0);
    query(&f, "a{2}"); CHECK(findui_get_state(&f.panel).search_error == FIND_ERR_UNSUPPORTED);
    query(&f, "$"); CHECK(findui_get_state(&f.panel).match_count == 2);
    query(&f, ""); CHECK(findui_get_state(&f.panel).complete && !findui_get_state(&f.panel).match_count);
    finish(&f);
    const uint8_t bytes[] = {'a', 0, 'A', 0, 'a', 'a', ' '};
    start(&f, bytes, sizeof bytes, 64, 64);
    CHECK(findui_set_options(&f.panel, (findui_options){false, false, true}) == FINDUI_OK);
    const uint8_t needle[] = {'a', 0};
    CHECK(findui_set_query(&f.panel, needle, sizeof needle) == FINDUI_OK); wait_result(&f);
    CHECK(findui_get_state(&f.panel).match_count == 0); /* right neighbours are word bytes */
    query(&f, "a"); CHECK(findui_get_state(&f.panel).match_count == 2);
    finish(&f);
    puts("findui_test: regex/case/word toggles, malformed regex and byte queries passed");
}
static void replace_groups(void)
{
    const uint8_t original[] = "one One stone one\0one";
    const uint8_t expected[] = "X One stone X\0X";
    fixture f; start(&f, original, sizeof original - 1, 64, 64);
    CHECK(findui_set_options(&f.panel, (findui_options){false, true, true}) == FINDUI_OK);
    query(&f, "one"); CHECK(findui_get_state(&f.panel).match_count == 3);
    CHECK(findui_set_replacement(&f.panel, (const uint8_t *)"X", 1, 1) == FINDUI_OK);
    undo_log undo; CHECK(undo_init(&undo, f.tree, 128) == 0);
    undo_state before = {{4}}, after = {{9}}; size_t replaced = 0;
    CHECK(findui_replace_all(&f.panel, &undo, 2, 128, 1, &before, &after) == FINDUI_ERR_STALE);
    CHECK(findui_replace_all(&f.panel, &undo, 1, 8, 1, &before, &after) == FINDUI_ERR_LIMIT);
    CHECK(findui_replace_all(&f.panel, &undo, 1, 128, 1, &before, &after) == FINDUI_OK);
    CHECK(findui_edit_query(&f.panel, 0, 0, (const uint8_t *)"x", 1) == FINDUI_ERR_BUSY);
    CHECK(findui_replace_step(&f.panel, 0, 0, &replaced) == FINDUI_MORE && replaced == 0);
    CHECK(findui_replace_step(&f.panel, 1, 0, &replaced) == FINDUI_MORE && replaced == 1);
    CHECK(findui_replace_step(&f.panel, 1, 0, &replaced) == FINDUI_MORE && replaced == 1);
    CHECK(findui_replace_step(&f.panel, 1, 0, &replaced) == FINDUI_OK && replaced == 1);
    same(f.tree, expected, sizeof expected - 1);
    CHECK(undo_get_stats(&undo).undo_groups == 1);
    undo_change change;
    CHECK(undo_undo(&undo, 1, &change) == 0 && change.groups == 1 && change.state.bytes[0] == 4);
    same(f.tree, original, sizeof original - 1);
    CHECK(undo_redo(&undo, 1, &change) == 0 && change.groups == 1 && change.state.bytes[0] == 9);
    same(f.tree, expected, sizeof expected - 1);
    CHECK(findui_get_state(&f.panel).match_count == 0 && !findui_get_state(&f.panel).complete);
    undo_destroy(&undo); finish(&f);

    start(&f, (const uint8_t *)"a a", 3, 64, 64); query(&f, "a");
    CHECK(undo_init(&undo, f.tree, 128) == 0);
    CHECK(findui_set_replacement(&f.panel, (const uint8_t *)"zz", 2, 2) == FINDUI_OK);
    CHECK(findui_replace_one(&f.panel, &undo, 1, 128, 1, &before, &after) == FINDUI_OK);
    CHECK(findui_replace_step(&f.panel, 1, 0, &replaced) == FINDUI_OK && replaced == 1);
    same(f.tree, (const uint8_t *)"zz a", 4);
    CHECK(undo_undo(&undo, 1, &change) == 0 && change.groups == 1); same(f.tree, (const uint8_t *)"a a", 3);
    undo_destroy(&undo); finish(&f);

    start(&f, (const uint8_t *)"ab", 2, 64, 64);
    CHECK(findui_set_options(&f.panel, (findui_options){true, true, false}) == FINDUI_OK);
    query(&f, "$|^"); CHECK(findui_get_state(&f.panel).match_count == 2);
    CHECK(undo_init(&undo, f.tree, 128) == 0);
    CHECK(findui_set_replacement(&f.panel, (const uint8_t *)"!", 1, 1) == FINDUI_OK);
    CHECK(findui_replace_all(&f.panel, &undo, 1, 128, 1, &before, &after) == FINDUI_OK);
    CHECK(findui_replace_step(&f.panel, 8, 0, &replaced) == FINDUI_OK && replaced == 2);
    same(f.tree, (const uint8_t *)"!ab!", 4);
    CHECK(undo_undo(&undo, 1, &change) == 0 && change.groups == 1); same(f.tree, (const uint8_t *)"ab", 2);
    undo_destroy(&undo); finish(&f);
    puts("findui_test: replace-one/all, sliced single undo group, exact undo/redo and empty regex matches passed");
}
static void replacement_failure_and_cancel(void)
{
    size_t failures = 0, partial_matches = 0;
    uint8_t original[65536], verified[65536]; memset(original, 'a', sizeof original);
    for (size_t fault = 1; fault <= 12; fault++) {
        fixture f; start(&f, original, sizeof original, 64, 64);
        CHECK(findui_set_options(&f.panel, (findui_options){true, true, false}) == FINDUI_OK);
        query(&f, "a+");
        CHECK(findui_set_replacement(&f.panel, (const uint8_t *)"zz", 2, 2) == FINDUI_OK);
        undo_log undo; CHECK(undo_init(&undo, f.tree, 128) == 0);
        undo_state state = {{0}}; size_t replaced;
        CHECK(findui_replace_all(&f.panel, &undo, 1, 128, 0, &state, &state) == FINDUI_OK);
        f.fail_allocation = f.allocations + fault;
        findui_code code;
        do { code = findui_replace_step(&f.panel, 64, 0, &replaced); } while (code == FINDUI_MORE);
        f.fail_allocation = 0;
        CHECK(code == FINDUI_OK || code == FINDUI_ERR_UNDO);
        CHECK(!findui_get_state(&f.panel).replacing && undo_get_stats(&undo).undo_groups <= 1);
        if (code == FINDUI_ERR_UNDO) {
            failures++;
            if (piece_len(f.tree) < sizeof original) partial_matches++;
            CHECK(findui_get_state(&f.panel).undo_error == PIECE_ERR_NOMEM);
        }
        undo_change change;
        CHECK(undo_undo(&undo, 1, &change) == 0);
        CHECK(piece_len(f.tree) == sizeof original && piece_read(f.tree, 0, verified, sizeof verified) == 0);
        CHECK(memcmp(original, verified, sizeof original) == 0);
        undo_destroy(&undo); finish(&f);
    }
    CHECK(failures && partial_matches);
    fixture f; start(&f, (const uint8_t *)"a a a", 5, 64, 64); query(&f, "a");
    CHECK(findui_set_replacement(&f.panel, (const uint8_t *)"z", 1, 1) == FINDUI_OK);
    undo_log undo; CHECK(undo_init(&undo, f.tree, 128) == 0);
    undo_state state = {{0}}; size_t replaced;
    CHECK(findui_replace_all(&f.panel, &undo, 1, 128, 0, &state, &state) == FINDUI_OK);
    CHECK(findui_dispose(&f.panel) == FINDUI_ERR_BUSY);
    CHECK(findui_replace_step(&f.panel, 1, 0, &replaced) == FINDUI_MORE && replaced == 1);
    CHECK(findui_replace_cancel(&f.panel) == FINDUI_OK);
    same(f.tree, (const uint8_t *)"a a z", 5);
    undo_change change; CHECK(undo_undo(&undo, 1, &change) == 0 && change.groups == 1);
    same(f.tree, (const uint8_t *)"a a a", 5);
    undo_destroy(&undo); finish(&f);
    puts("findui_test: allocator faults and replacement cancellation preserve one exact undo group passed");
}
static void lifecycle_and_backpressure(void)
{
    uint8_t dense[600]; memset(dense, 'a', sizeof dense);
    fixture f; start(&f, dense, sizeof dense, 600, 600);
    CHECK(findui_set_query(&f.panel, (const uint8_t *)"a", 1) == FINDUI_OK);
    for (size_t i = 0; i < 100000 && !atomic_load(&f.pool->dropped_full); i++) pause_worker();
    CHECK(atomic_load(&f.pool->dropped_full) > 0);
    wait_result(&f);
    CHECK(findui_get_state(&f.panel).match_count == sizeof dense);
    CHECK(findui_get_state(&f.panel).cached_matches == sizeof dense);
    findui_range ranges[600]; size_t count;
    CHECK(findui_highlights(&f.panel, 0, sizeof dense, ranges, 600, &count) == FINDUI_OK && count == sizeof dense);
    CHECK(ranges[599].start == 599 && ranges[599].end == 600);
    uint64_t full = atomic_load(&f.pool->dropped_full);
    CHECK(findui_set_query(&f.panel, (const uint8_t *)"a", 1) == FINDUI_OK);
    for (size_t i = 0; i < 100000 && atomic_load(&f.pool->dropped_full) == full; i++) pause_worker();
    CHECK(atomic_load(&f.pool->dropped_full) > full);
    CHECK(findui_set_query(&f.panel, (const uint8_t *)"b", 1) == FINDUI_OK);
    wait_result(&f); CHECK(findui_get_state(&f.panel).match_count == 0);
    CHECK(findui_show(&f.panel, false, false) == FINDUI_OK);
    CHECK(!findui_get_state(&f.panel).complete);
    CHECK(findui_show(&f.panel, true, false) == FINDUI_OK); wait_result(&f);
    finish(&f);

    start(&f, (const uint8_t *)"ab ab", 5, 64, 64); query(&f, "ab");
    CHECK(piece_delete(f.tree, 0, 2, NULL) == 0 && piece_insert(f.tree, 0, (const uint8_t *)"cd", 2) == 0);
    piece_snapshot *snapshot = piece_snapshot_take(f.tree); CHECK(snapshot);
    CHECK(findui_set_source(&f.panel, snapshot, 2) == FINDUI_OK); piece_snapshot_release(snapshot);
    wait_result(&f); CHECK(findui_get_state(&f.panel).match_count == 1);
    CHECK(findui_get_state(&f.panel).selected.start == 3);
    CHECK(findui_edit_query(&f.panel, 0, 2, findui_get_state(&f.panel).query, 2) == FINDUI_OK);
    wait_result(&f); CHECK(findui_get_state(&f.panel).match_count == 1);
    CHECK(findui_set_window(&f.panel, 4, 4) == FINDUI_OK); wait_result(&f);
    CHECK(findui_highlights(&f.panel, 4, 4, ranges, 600, &count) == FINDUI_OK && count == 0);
    finish(&f);
    puts("findui_test: mailbox backpressure/cancel, source versions, alias edits and empty windows passed");
}
static void painting_and_no_malloc(void)
{
    fixture f; start(&f, (const uint8_t *)"a a", 3, 64, 64); query(&f, "a");
    enum { COLS = 48, ROWS = 5 };
    render_grid grid; render_cell cells[COLS * ROWS]; uint64_t dirty[1];
    render_glyph glyphs[95]; uint32_t slots[95]; uint8_t pixel = 255;
    for (uint32_t i = 0; i < 95; i++) {
        slots[i] = i; glyphs[i] = (render_glyph){i + 32, 0, 0, 0, 1, 1};
    }
    render_atlas_page page = {&pixel, 1, 1, 1, 1};
    for (size_t i = 0; i < COLS * ROWS; i++) cells[i] = (render_cell){0, RENDER_NO_SLOT, 1, 2, 0, 0};
    CHECK(render_grid_init(&grid, (render_dims){COLS, ROWS, 8, 16}, cells, COLS * ROWS, dirty, 1) == 0);
    grid.pages = &page; grid.page_count = 1; grid.glyphs = glyphs; grid.glyph_count = 95;
    CHECK(render_frame_begin(&grid, 1) == 0);
    findui_style style = {0xffffff, 0x112233, 0xffcc00, 0, 0xffffff, slots, 95};
    CHECK(findui_render(&f.panel, &grid, 2, 3, &style) == FINDUI_OK);
    CHECK(cells[0].bg == 2 && cells[COLS].bg == 2 && cells[2 * COLS].glyph_index == 'F');
    CHECK(render_grid_validate(&grid) == 0 && (dirty[0] & UINT64_C(28)) == UINT64_C(28));
    bool cursor = false;
    for (size_t i = 2 * COLS; i < 3 * COLS; i++) if (cells[i].attrs & RENDER_ATTR_CURSOR) cursor = true;
    CHECK(cursor);
    render_cell old = cells[2 * COLS];
    CHECK(findui_render(&f.panel, &grid, 4, 2, &style) == FINDUI_ERR_ARGUMENT);
    CHECK(memcmp(&old, &cells[2 * COLS], sizeof old) == 0);
    CHECK(findui_set_query(&f.panel, (const uint8_t *)"", 0) == FINDUI_OK);
    edit_malloc_guard_begin();
    for (size_t i = 0; i < 1000; i++) {
        CHECK(findui_edit_query(&f.panel, 0, 0, (const uint8_t *)"a", 1) == FINDUI_OK);
        CHECK(findui_edit_query(&f.panel, 0, 1, NULL, 0) == FINDUI_OK);
        CHECK(findui_render(&f.panel, &grid, 2, 3, &style) == FINDUI_OK);
    }
    size_t calls = edit_malloc_guard_end(); CHECK(!edit_malloc_guard_active() || calls == 0);
    CHECK(atomic_load(&f.hook.on_caller) == 0);
    finish(&f);
    puts("findui_test: bottom-row rendering, damage validation and typing allocation guard passed");
}
static void bounded_count_worker(void)
{
    uint8_t text[1024u*1024u]; memset(text,'a',sizeof text);
    fixture f; start(&f,text,sizeof text,8,4);
    query(&f,"a");
    findui_state state=findui_get_state(&f.panel);
    unsigned scans=atomic_load(&f.hook.scans);
    printf("P1R8 dense worker: count=%llu cached=%zu scans=%u\n",
           (unsigned long long)state.match_count,state.cached_matches,scans);
    fflush(stdout);
    CHECK(state.complete && state.match_count==sizeof text && state.cached_matches==8);
    CHECK(scans<=16); /* core counts discarded matches, no next() per byte */
    CHECK(state.visible_overflow && state.visible_matches==sizeof text);
    CHECK(findui_set_window(&f.panel,sizeof text-4,sizeof text)==FINDUI_OK);
    wait_result(&f);
    findui_range ranges[4]; size_t count;
    CHECK(findui_highlights(&f.panel,sizeof text-4,sizeof text,ranges,4,&count)==FINDUI_OK && count==4);
    CHECK(ranges[0].start==sizeof text-4 && ranges[3].end==sizeof text);
    findui_range selected;
    CHECK(findui_next(&f.panel,-1,&selected)==FINDUI_MORE);
    wait_result(&f); state=findui_get_state(&f.panel);
    CHECK(state.match_index==sizeof text-1 && state.selected.start==sizeof text-1);
    CHECK(findui_set_options(&f.panel,(findui_options){true,true,false})==FINDUI_OK);
    wait_result(&f); state=findui_get_state(&f.panel);
    CHECK(state.complete && state.match_count==sizeof text);
    CHECK(atomic_load(&f.hook.scans)<=32);
    finish(&f);
    puts("P1R8: PASS bounded core count, visible overflow, late window, lazy ordinal, regex");
}
static void filtered_regex_budget(void)
{
    uint8_t text[4096]; memset(text,'a',sizeof text);
    fixture f; start(&f,text,sizeof text,8,4);
    CHECK(findui_set_options(&f.panel,(findui_options){true,true,true})==FINDUI_OK);
    query(&f,"a*b|a");
    findui_state state=findui_get_state(&f.panel);
    printf("P1R9 filtered regex: complete=%d error=%d scans=%u\n",
           state.complete,(int)state.search_error,atomic_load(&f.hook.scans));
    fflush(stdout);
    CHECK(!state.complete && state.search_error==FIND_ERR_LIMIT && !state.match_count);
    finish(&f);
}
static void storage_retain(void *user) { (*(size_t *)user)++; }
static void storage_release(void *user) { CHECK(*(size_t *)user); (*(size_t *)user)--; }
static void full_source_lifetime(void)
{
    fixture f; start(&f, (const uint8_t *)"new", 3, 8, 8);
    edit_arena source_arena; CHECK(edit_arena_init(&source_arena, 4u * 1024u * 1024u) == 0);
    piece_allocator allocator = {&source_arena, arena_alloc, arena_free};
    piece_tree *old = piece_create(&allocator); CHECK(old && piece_init_copy(old, (const uint8_t *)"old", 3) == 0);
    size_t leases = 0;
    findui_source_lease lease = {&leases, 42, storage_retain, storage_release};
    piece_snapshot *snapshot = piece_snapshot_take(old); CHECK(snapshot);
    CHECK(findui_set_source_leased(&f.panel, snapshot, 2, &lease) == FINDUI_OK); piece_snapshot_release(snapshot);
    atomic_store(&f.hook.park, true);
    CHECK(findui_set_query(&f.panel, (const uint8_t *)"o", 1) == FINDUI_OK);
    while (!atomic_load(&f.hook.entered)) pause_worker();
    printf("S30 cancelled source: retained_storage_leases=%zu\n", leases); fflush(stdout);
    CHECK(leases > 0);
    snapshot = piece_snapshot_take(f.tree); CHECK(snapshot);
    CHECK(findui_set_source(&f.panel, snapshot, 3) == FINDUI_OK); piece_snapshot_release(snapshot);
    piece_destroy(old); /* cancelled worker still retains arena and allocator */
    CHECK(!findui_source_retired(&f.panel, &leases, 42));
    CHECK(findui_maintain(&f.panel) == FINDUI_OK);
    CHECK(!findui_source_retired(&f.panel, &leases, 42) && leases == 1);
    CHECK(findui_service(&f.panel) <= FINDUI_MORE && leases == 1);
    atomic_store(&f.hook.park, false); atomic_store(&f.hook.release, true);
    for (size_t i = 0; i < 100000 && !findui_source_retired(&f.panel, &leases, 42); i++) {
        pump(&f); CHECK(findui_maintain(&f.panel) == FINDUI_OK); pause_worker();
    }
    CHECK(findui_source_retired(&f.panel, &leases, 42) && leases == 0);
    edit_arena_free(&source_arena); /* retired source storage may now be evicted */
    wait_result(&f); CHECK(findui_get_state(&f.panel).revision == 3);
    query(&f, "n"); CHECK(findui_get_state(&f.panel).match_count == 1);
    finish(&f);
    puts("S30: PASS full arena lease, cancelled eviction/reload, maintenance-only retirement acknowledgement");
}
typedef struct host_probe {
    journal *journal;
    piece_tree *tree;
    uint64_t revision, records, lines;
    bool reject, fail_insert;
} host_probe;
static int host_admit(void *user, size_t operations, uint64_t deleted, size_t inserted)
{
    host_probe *host = user;
    CHECK(operations <= 32 && deleted == 5 && inserted == 1);
    return host->reject ? PIECE_ERR_NOMEM : 0;
}
static findui_mutation_result host_delete(void *user, undo_log *undo, uint64_t off, uint64_t bytes,
                                          uint64_t time, const undo_state *before, const undo_state *after)
{
    host_probe *host = user;
    int error = undo_delete(undo, off, bytes, UNDO_DELETE, time, before, after);
    if (!error) {
        host->revision++; host->records++; host->lines = piece_line_count(host->tree);
        CHECK(journal_delete(host->journal, 1, off, bytes) == JOURNAL_OK);
    }
    return (findui_mutation_result){error, error == 0};
}
static findui_mutation_result host_insert(void *user, undo_log *undo, uint64_t off, const uint8_t *bytes,
                                          size_t length, uint64_t time, const undo_state *before, const undo_state *after)
{
    host_probe *host = user;
    int error = host->fail_insert ? PIECE_ERR_NOMEM : undo_insert(undo, off, bytes, length, time, before, after);
    if (!error) {
        host->revision++; host->records++; host->lines = piece_line_count(host->tree);
        CHECK(journal_insert(host->journal, 1, off, bytes, length) == JOURNAL_OK);
    }
    return (findui_mutation_result){error, error == 0};
}
static int host_replay(void *tree, const journal_record *record)
{ return journal_apply_piece(tree, record); }
static void replacement_host_bookkeeping(void)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        fixture f; start(&f, (const uint8_t *)"a\n a\n", 5, 8, 8); query(&f, "a\n");
        CHECK(findui_set_replacement(&f.panel, (const uint8_t *)"X", 1, 1) == FINDUI_OK);
        undo_log undo; CHECK(undo_init(&undo, f.tree, 128) == 0);
        char path[] = "build/findui-host-XXXXXX"; int fd = mkstemp(path); CHECK(fd >= 0); CHECK(close(fd) == 0);
        journal *log; CHECK(journal_open(&log, path, f.pool, NULL) == JOURNAL_OK);
        CHECK(journal_insert(log, 1, 0, (const uint8_t *)"a\n a\n", 5) == JOURNAL_OK);
        host_probe host = {.journal = log, .tree = f.tree, .lines = 3, .reject = mode == 2, .fail_insert = mode == 1};
        findui_mutation_host adapter = {&host, host_admit, host_delete, host_insert};
        CHECK(findui_set_mutation_host(&f.panel, &adapter) == FINDUI_OK);
        undo_state state = {{0}}; size_t replaced;
        findui_code code = findui_replace_all(&f.panel, &undo, 1, 128, 0, &state, &state);
        if (mode == 2) {
            CHECK(code == FINDUI_ERR_UNDO && !findui_get_state(&f.panel).replacing);
            CHECK(undo_get_stats(&undo).records == 0); same(f.tree, (const uint8_t *)"a\n a\n", 5);
        } else {
            CHECK(code == FINDUI_OK);
            code = findui_replace_step(&f.panel, 8, 0, &replaced);
            CHECK(mode == 0 ? code == FINDUI_OK && replaced == 2 : code == FINDUI_ERR_UNDO && replaced == 0);
            CHECK(host.revision == (mode == 0 ? 4u : 1u) && host.records == host.revision);
            CHECK(host.lines == (mode == 0 ? 1u : 2u));
        }
        printf("S29 host mutation: mode=%u revision=%llu records=%llu lines=%llu\n", mode,
               (unsigned long long)host.revision, (unsigned long long)host.records,
               (unsigned long long)host.lines); fflush(stdout);
        uint8_t wire[16384]; fd = open(path, O_RDONLY); CHECK(fd >= 0);
        ssize_t got = read(fd, wire, sizeof wire); CHECK(got > 0); CHECK(close(fd) == 0);
        piece_allocator allocator = {&f, test_alloc, test_free}; piece_tree *recovered = piece_create(&allocator); CHECK(recovered);
        journal_replay_result result;
        CHECK(journal_replay_bytes(wire, (size_t)got, host_replay, recovered, &result) == JOURNAL_OK && !result.corrupt);
        uint8_t actual[8], replayed[8]; size_t length = (size_t)piece_len(f.tree);
        CHECK(piece_len(recovered) == length && piece_read(f.tree, 0, actual, length) == 0);
        CHECK(piece_read(recovered, 0, replayed, length) == 0 && memcmp(actual, replayed, length) == 0);
        piece_destroy(recovered);
        if (mode != 2) {
            recovered = piece_create(&allocator); CHECK(recovered);
            CHECK(journal_replay_bytes(wire, (size_t)got - 1, host_replay, recovered, &result) == JOURNAL_OK && result.corrupt);
            same(recovered, (const uint8_t *)(mode == 0 ? " X" : "a\n a\n"), mode == 0 ? 2 : 5);
            piece_destroy(recovered);
            undo_change change; CHECK(undo_undo(&undo, 1, &change) == 0 && change.groups == 1);
            same(f.tree, (const uint8_t *)"a\n a\n", 5);
        }
        journal_close(log); CHECK(unlink(path) == 0);
        undo_destroy(&undo); finish(&f);
    }
    puts("S29: PASS host preflight, newline bookkeeping, partial edit journal/recovery and undo");
}
static void replacement_large_match(void)
{
    uint8_t original[1024u * 1024u], actual[1024u * 1024u]; memset(original, 'a', sizeof original);
    fixture f; start(&f, original, sizeof original, 8, 8);
    CHECK(findui_set_options(&f.panel, (findui_options){true, true, false}) == FINDUI_OK);
    query(&f, "a+"); CHECK(findui_get_state(&f.panel).match_count == 1);
    CHECK(findui_set_replacement(&f.panel, (const uint8_t *)"z", 1, 1) == FINDUI_OK);
    undo_log undo; CHECK(undo_init(&undo, f.tree, 4096) == 0);
    undo_state state = {{0}}; size_t replaced;
    CHECK(findui_replace_all(&f.panel, &undo, 1, 4096, 0, &state, &state) == FINDUI_OK);
    findui_code code = findui_replace_step(&f.panel, 1, 0, &replaced);
    printf("S28 large match: code=%d replaced=%zu remaining_bytes=%llu\n", (int)code,
           replaced, (unsigned long long)piece_len(f.tree)); fflush(stdout);
    CHECK(code == FINDUI_MORE && replaced == 0 && piece_len(f.tree) >= sizeof original - 8192);
    CHECK(findui_replace_cancel(&f.panel) == FINDUI_OK);
    undo_change change; CHECK(undo_undo(&undo, 1, &change) == 0 && change.groups == 1);
    CHECK(piece_len(f.tree) == sizeof original && piece_read(f.tree, 0, actual, sizeof actual) == 0);
    CHECK(memcmp(actual, original, sizeof actual) == 0);
    CHECK(undo_redo(&undo, 1, &change) == 0 && change.groups == 1);
    CHECK(piece_len(f.tree) == sizeof original - 8192);
    CHECK(undo_undo(&undo, 1, &change) == 0);
    piece_snapshot *snapshot = piece_snapshot_take(f.tree); CHECK(snapshot);
    CHECK(findui_set_source(&f.panel, snapshot, 2) == FINDUI_OK); piece_snapshot_release(snapshot); wait_result(&f);
    CHECK(findui_replace_all(&f.panel, &undo, 2, 64, 0, &state, &state) == FINDUI_ERR_LIMIT);
    CHECK(findui_replace_all(&f.panel, &undo, 2, 4096, 0, &state, &state) == FINDUI_OK);
    do { code = findui_replace_step(&f.panel, 1, 0, &replaced); } while (code == FINDUI_MORE);
    CHECK(code == FINDUI_OK && piece_len(f.tree) == 1);
    CHECK(piece_read(f.tree, 0, actual, 1) == 0 && actual[0] == 'z');
    CHECK(undo_undo(&undo, 1, &change) == 0 && change.groups == 1);
    CHECK(piece_len(f.tree) == sizeof original && piece_read(f.tree, 0, actual, sizeof actual) == 0);
    CHECK(memcmp(actual, original, sizeof actual) == 0);
    undo_destroy(&undo); finish(&f);
    puts("S28: PASS bounded large-match deletion, admission, cancellation and exact undo/redo");
}
static void replacement_paged_noop(void)
{
    uint8_t text[32]; memset(text, 'a', sizeof text);
    fixture f; start(&f, text, sizeof text, 8, 64);
    CHECK(findui_set_options(&f.panel, (findui_options){true, true, false}) == FINDUI_OK); query(&f, "b?");
    undo_log undo; CHECK(undo_init(&undo, f.tree, 512) == 0);
    undo_state state = {{0}}; size_t replaced, total = 0; findui_code code;
    CHECK(findui_replace_all(&f.panel, &undo, 1, 512, 0, &state, &state) == FINDUI_OK);
    do { pump(&f); code = findui_replace_step(&f.panel, 8, 0, &replaced); total += replaced; if (code == FINDUI_MORE) pause_worker(); }
    while (code == FINDUI_MORE);
    findui_state result = findui_get_state(&f.panel);
    printf("S27 noop pages: complete=%d count=%llu cached=%zu total=%zu\n", result.complete,
           (unsigned long long)result.match_count, result.cached_matches, total); fflush(stdout);
    CHECK(code == FINDUI_OK && result.complete && result.match_count == 33 && result.cached_matches == 8 && total == 33);
    findui_range selected; CHECK(findui_next(&f.panel, 1, &selected) == FINDUI_OK && selected.start == 1);
    CHECK(undo_get_stats(&undo).undo_groups == 0); same(f.tree, text, sizeof text);
    undo_destroy(&undo); finish(&f);
}
static void replacement_beyond_cache(void)
{
    uint8_t original[5000], actual[5000]; memset(original, 'a', sizeof original);
    for (unsigned mode = 0; mode < 3; mode++) {
        fixture f; start(&f, original, sizeof original, 64, 8); query(&f, "a");
        CHECK(findui_set_replacement(&f.panel, (const uint8_t *)"z", 1, 1) == FINDUI_OK);
        undo_log undo; CHECK(undo_init(&undo, f.tree, 65536) == 0);
        undo_state state = {{0}}; size_t total = 0, replaced = 0;
        findui_code code = findui_replace_all(&f.panel, &undo, 1, 65536, 0, &state, &state);
        printf("S27 beyond cache: mode=%u begin=%d count=%llu\n", mode, (int)code,
               (unsigned long long)findui_get_state(&f.panel).match_count); fflush(stdout);
        CHECK(code == FINDUI_OK);
        if (mode == 2) f.fail_allocation = f.allocations + 1;
        do {
            pump(&f); code = findui_replace_step(&f.panel, 1, 0, &replaced); total += replaced;
            if (mode == 1 && total) { CHECK(findui_replace_cancel(&f.panel) == FINDUI_OK); code = FINDUI_OK; }
            if (code == FINDUI_MORE) pause_worker();
        } while (code == FINDUI_MORE);
        f.fail_allocation = 0;
        CHECK(mode == 2 ? code == FINDUI_ERR_UNDO : code == FINDUI_OK);
        CHECK(mode == 0 ? total == sizeof original : total <= 1);
        CHECK(piece_len(f.tree) == sizeof original && piece_read(f.tree, 0, actual, sizeof actual) == 0);
        for (size_t i = 0; i < sizeof actual; i++) CHECK(actual[i] == (i >= sizeof actual - total ? 'z' : 'a'));
        undo_change change; CHECK(undo_undo(&undo, 1, &change) == 0);
        CHECK(piece_len(f.tree) == sizeof original && piece_read(f.tree, 0, actual, sizeof actual) == 0);
        CHECK(memcmp(actual, original, sizeof actual) == 0);
        if (mode != 2) {
            CHECK(change.groups == 1 && undo_redo(&undo, 1, &change) == 0 && change.groups == 1);
            CHECK(piece_read(f.tree, 0, actual, sizeof actual) == 0);
            for (size_t i = 0; i < sizeof actual; i++) CHECK(actual[i] == (i >= sizeof actual - total ? 'z' : 'a'));
        }
        undo_destroy(&undo); finish(&f);
    }
    puts("S27: PASS paged replacement beyond cache, cancellation, fault and exact undo/redo");
}
static void first_offsets_independent(void)
{
    uint8_t text[5000]; memset(text, 'a', sizeof text);
    fixture f; start(&f, text, sizeof text, FIND_MAX_OFFSETS, 1);
    const uint64_t windows[][2] = {{0, 1}, {4999, 5000}, {4000, 4000}};
    for (size_t w = 0; w < sizeof windows / sizeof windows[0]; w++) {
        CHECK(findui_set_window(&f.panel, windows[w][0], windows[w][1]) == FINDUI_OK);
        query(&f, "a");
        findui_state state = findui_get_state(&f.panel);
        CHECK(state.complete && state.match_count == sizeof text && state.cached_matches == FIND_MAX_OFFSETS);
        for (size_t i = 1; i < FIND_MAX_OFFSETS; i++) {
            findui_range selected;
            CHECK(findui_next(&f.panel, 1, &selected) == FINDUI_OK && selected.start == i && selected.end == i + 1);
        }
    }
    finish(&f);
    puts("S26: PASS first 4096 offsets with initial, late and empty windows");
}
static void viewport_retains_count(void)
{
    uint8_t text[512]; memset(text, 'a', sizeof text);
    fixture f; start(&f, text, sizeof text, 8, 8); query(&f, "a");
    findui_state before = findui_get_state(&f.panel);
    CHECK(findui_set_window(&f.panel, 500, 508) == FINDUI_OK);
    findui_state after = findui_get_state(&f.panel);
    printf("S25 viewport: complete=%d->%d count=%llu->%llu selected=%llu->%llu\n",
           before.complete, after.complete, (unsigned long long)before.match_count,
           (unsigned long long)after.match_count, (unsigned long long)before.match_index,
           (unsigned long long)after.match_index); fflush(stdout);
    CHECK(after.complete && after.match_count == before.match_count && after.match_index == before.match_index);
    wait_result(&f);
    findui_range ranges[8]; size_t count;
    CHECK(findui_highlights(&f.panel, 500, 508, ranges, 8, &count) == FINDUI_OK && count == 8);
    CHECK(ranges[0].start == 500 && ranges[7].end == 508);
    CHECK(findui_set_window(&f.panel, 10, 18) == FINDUI_OK);
    CHECK(findui_get_state(&f.panel).complete && findui_get_state(&f.panel).match_count == 512);
    CHECK(findui_set_window(&f.panel, 20, 28) == FINDUI_OK); wait_result(&f);
    CHECK(findui_highlights(&f.panel, 20, 28, ranges, 8, &count) == FINDUI_OK && count == 8);
    finish(&f);
    puts("S25: PASS viewport retains exact count and selection");
}
static void foreground_probe(work_ctx *context)
{ atomic_store((atomic_bool *)context->arg, true); }
static void backpressure_yields(void)
{
    uint8_t text[1200]; memset(text, 'a', sizeof text);
    fixture f; start(&f, text, sizeof text, 600, 600);
    CHECK(findui_set_query(&f.panel, (const uint8_t *)"a", 1) == FINDUI_OK);
    for (size_t i = 0; i < 100000 && !atomic_load(&f.pool->dropped_full); i++) pause_worker();
    CHECK(atomic_load(&f.pool->dropped_full));
    atomic_bool serviced; atomic_init(&serviced, false);
    CHECK(work_submit(f.pool, (work_job){foreground_probe, &serviced, 123456, WORK_BULK}).epoch);
    for (size_t i = 0; i < 2000 && !atomic_load(&serviced); i++) pause_worker();
    printf("S24 saturated mailbox: next_bulk_started=%d\n", atomic_load(&serviced)); fflush(stdout);
    CHECK(atomic_load(&serviced));
    wait_result(&f); CHECK(findui_get_state(&f.panel).match_count == sizeof text);
    finish(&f);
    puts("S24: PASS full mailbox yields sole bulk lane without cancellation/draining");
}
static void whole_word_publication(void)
{
    uint8_t text[1200];
    for (size_t i = 0; i < sizeof text; i++) text[i] = i % 2 ? (uint8_t)' ' : (uint8_t)'a';
    for (unsigned regex = 0; regex < 2; regex++) {
        fixture f; start(&f, text, sizeof text, 8, 1);
        CHECK(findui_set_options(&f.panel, (findui_options){regex != 0, true, true}) == FINDUI_OK);
        query(&f, "a");
        findui_state state = findui_get_state(&f.panel);
        printf("S23 word publication: regex=%u count=%llu visible=%zu ranges=%zu\n", regex,
               (unsigned long long)state.match_count, state.visible_matches, f.published_ranges); fflush(stdout);
        CHECK(state.complete && state.match_count == 600 && state.visible_matches == 600 && state.visible_overflow);
        CHECK(f.published_ranges <= 8 + 1 + 1);
        finish(&f);
    }
    puts("S23: PASS bounded word publication with exact visible overflow");
}
static void whole_word_count_worker(void)
{
    uint8_t text[1024u * 1024u];
    for (size_t i = 0; i < sizeof text; i++) text[i] = i % 2 ? (uint8_t)' ' : (uint8_t)'a';
    fixture f; start(&f, text, sizeof text, 8, 4);
    CHECK(findui_set_window(&f.panel, 0, 8) == FINDUI_OK);
    CHECK(findui_set_options(&f.panel, (findui_options){false, false, true}) == FINDUI_OK);
    query(&f, "a");
    findui_state state = findui_get_state(&f.panel);
    printf("S22 word worker: count=%llu scans=%u\n", (unsigned long long)state.match_count,
           atomic_load(&f.hook.scans)); fflush(stdout);
    CHECK(state.complete && state.match_count == sizeof text / 2);
    CHECK(atomic_load(&f.hook.scans) <= 16);
    finish(&f);
    start(&f, (const uint8_t *)"xa a a", 6, 8, 8);
    CHECK(findui_set_options(&f.panel, (findui_options){false, false, true}) == FINDUI_OK);
    query(&f, "a a"); state = findui_get_state(&f.panel);
    CHECK(state.match_count == 1 && state.selected.start == 3 && state.selected.end == 6);
    finish(&f);
    puts("S22: PASS bounded literal whole-word visitor and rejected overlap");
}
static void whole_word_regex_worker(void)
{
    /* Derivation: each cached-offset slot contributes an 'a ' pair; this
     * exercises discarded candidates after the deliberately minimal caches. */
    uint8_t text[2 * FIND_MAX_OFFSETS];
    for (size_t i = 0; i < sizeof text; i++) text[i] = i % 2 ? (uint8_t)' ' : (uint8_t)'a';
    const char *patterns[] = {"a+", "[a]"}; /* mandatory prefix, then no prefix */
    for (size_t p = 0; p < sizeof patterns / sizeof *patterns; p++) {
        fixture f; start(&f, text, sizeof text, 1, 1); /* one slot per independent cache */
        CHECK(findui_set_options(&f.panel, (findui_options){true, true, true}) == FINDUI_OK);
        query(&f, patterns[p]);
        findui_state state = findui_get_state(&f.panel);
        printf("S22 regex word trace: pattern=%s count=%llu stages=%u ranges=%zu\n", patterns[p],
               (unsigned long long)state.match_count, atomic_load(&f.hook.scans), f.published_ranges);
        fflush(stdout);
        CHECK(state.complete && state.match_count == sizeof text / 2);
        CHECK(state.visible_matches == state.match_count && state.visible_overflow);
        /* A single persistent worker visitor owns matching and boundaries;
         * publication is bounded by prefix + visible + requested ordinal. */
        CHECK(atomic_load(&f.hook.scans) == 1);
        CHECK(f.published_ranges <= 1 + 1 + 1);
        finish(&f);
    }
    struct { const char *text, *pattern; uint64_t count, start, end; } cases[] = {
        {"xa a a", "a a", 1, 3, 6}, /* reject overlap, retry at start + one byte */
        {"a", "a?", 1, 0, 1}, /* rejected empty match at the word's end */
        {" ", "a?", 2, 0, 0}, /* accepted empty matches at both boundaries */
        {"A_ a", "[aA]", 1, 3, 4}, /* ASCII underscore is a word byte */
        {"a\na", "^a$", 2, 0, 1}, /* anchors inspect the complete source */
        {"\xff" "a", "a", 1, 1, 2} /* high bytes preserve ASCII boundaries */
    }; /* Expected ranges derive directly from the indexed bytes of each text. */
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        fixture f; start(&f, (const uint8_t *)cases[i].text, strlen(cases[i].text), 1, 1);
        CHECK(findui_set_options(&f.panel, (findui_options){true, true, true}) == FINDUI_OK);
        query(&f, cases[i].pattern);
        findui_state state = findui_get_state(&f.panel);
        CHECK(state.complete && state.match_count == cases[i].count);
        CHECK(state.selected.start == cases[i].start && state.selected.end == cases[i].end);
        finish(&f);
    }
    puts("S22: PASS persistent regex whole-word visitor, boundaries, empty matches and rejected overlap");
}
static void maximum_literals(void)
{
    uint8_t needle[FINDUI_QUERY_BYTES], text[2 * FINDUI_QUERY_BYTES];
    for (unsigned binary = 0; binary < 2; binary++) {
        for (size_t i = 0; i < sizeof needle; i++) needle[i] = binary ? (uint8_t)i : (uint8_t)'a';
        memcpy(text, needle, sizeof needle); memcpy(text + sizeof needle, needle, sizeof needle);
        for (unsigned sensitive = 0; sensitive < 2; sensitive++) {
            fixture f; start(&f, text, sizeof text, 8, 8);
            CHECK(findui_set_options(&f.panel, (findui_options){false, sensitive != 0, false}) == FINDUI_OK);
            CHECK(findui_set_query(&f.panel, needle, sizeof needle) == FINDUI_OK); wait_result(&f);
            findui_state state = findui_get_state(&f.panel);
            printf("S21 maximum literal: binary=%u case=%u complete=%d error=%d count=%llu\n",
                   binary, sensitive, state.complete, (int)state.search_error, (unsigned long long)state.match_count);
            fflush(stdout);
            CHECK(state.complete && state.match_count == 2 && state.cached_matches == 2);
            finish(&f);
        }
    }
    puts("S21: PASS maximum ASCII/binary literals in both case modes");
}
/* Regression for P4-modules-2 #21, fixed on main; retain the historical
 * opt-in switch for this default folded-query fixture. */
static void known_default_long_query(void)
{
    uint8_t text[300], needle[300]; memset(text, 'A', sizeof text); memset(needle, 'a', sizeof needle);
    fixture f; start(&f, text, sizeof text, 8, 8);
    CHECK(findui_set_options(&f.panel, (findui_options){0}) == FINDUI_OK);
    CHECK(findui_set_query(&f.panel, needle, sizeof needle) == FINDUI_OK); wait_result(&f);
    findui_state state = findui_get_state(&f.panel);
    printf("KNOWN default folded query: length=300 complete=%d error=%d count=%llu\n",
           state.complete, (int)state.search_error, (unsigned long long)state.match_count); fflush(stdout);
    CHECK(state.complete && state.match_count == 1);
    finish(&f);
}
/* Sparse source exposes repeated window scans after a complete global count.
 * Inherited review fixture: each match occupies a byte pair at stride 800;
 * 5000 * 800 fits within the original four-MiB source. Window endpoints are
 * a late fixture offset and a span shorter than the match stride. */
static uint8_t *immutable_source(fixture *f, const uint8_t *bytes, size_t length)
{
    uint8_t *mapping = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(mapping != MAP_FAILED); memcpy(mapping, bytes, length);
    piece_allocator allocator = {f, test_alloc, test_free};
    piece_tree *tree = piece_create(&allocator); CHECK(tree);
    CHECK(piece_init_mapped(tree, mapping, length, NULL) == PIECE_OK);
    piece_snapshot *snapshot = piece_snapshot_take(tree); CHECK(snapshot);
    CHECK(findui_set_source(&f->panel, snapshot, 1) == FINDUI_OK);
    piece_snapshot_release(snapshot); piece_destroy(tree);
    return mapping;
}
static void sparse_window_work(void)
{
    size_t length = 4u * 1024u * 1024u;
    uint8_t *bytes = malloc(length); CHECK(bytes); memset(bytes, 'x', length);
    for (size_t i = 0; i < 5000u; i++) memcpy(bytes + i * 800u, "ab", 2);
    fixture f; start(&f, bytes, length, 64, 64);
    uint8_t *mapping = immutable_source(&f, bytes, length); free(bytes);
    query(&f, "ab"); CHECK(findui_get_state(&f.panel).match_count == 5000);
    /* Source-read work oracle: any repeated source traversal now faults.
     * Snapshot metadata and the independent live mutation tree remain valid. */
    CHECK(mprotect(mapping, length, PROT_NONE) == 0);
    unsigned before = atomic_load(&f.hook.scans);
    CHECK(findui_set_window(&f.panel, 3000000u, 3000100u) == FINDUI_OK); wait_result(&f);
    unsigned after = atomic_load(&f.hook.scans);
    printf("P4-3 section 29: sparse window new source-search stages=%u (M)[AC]\n", after - before); fflush(stdout);
    CHECK(after == before); /* plan retrieval must avoid another source scan */
    finish(&f); CHECK(munmap(mapping, length) == 0);
}

/* Paging reuses the immutable original ranges even while replacement changes
 * the live tree. Fixture geometry derives from sparse_window_work above;
 * history reservation reuses replacement_beyond_cache's admission. */
static void sparse_page_work(void)
{
    size_t length = 4u * 1024u * 1024u;
    uint8_t *bytes = malloc(length); CHECK(bytes); memset(bytes, 'x', length);
    for (size_t i = 0; i < 5000u; i++) memcpy(bytes + i * 800u, "ab", 2);
    fixture f; start(&f, bytes, length, 64, 1);
    uint8_t *mapping = immutable_source(&f, bytes, length); free(bytes);
    query(&f, "ab");
    CHECK(mprotect(mapping, length, PROT_NONE) == 0);
    unsigned before = atomic_load(&f.hook.scans);
    CHECK(findui_set_replacement(&f.panel, (const uint8_t *)"ab", 2, 2) == FINDUI_OK);
    undo_log undo; CHECK(undo_init(&undo, f.tree, 65536) == 0);
    undo_state state = {{0}}; size_t replaced, total = 0;
    CHECK(findui_replace_all(&f.panel, &undo, 1, 65536, 0, &state, &state) == FINDUI_OK);
    findui_code code;
    do {
        pump(&f); code = findui_replace_step(&f.panel, 64, 0, &replaced); total += replaced;
        if (code == FINDUI_MORE) pause_worker();
    } while (code == FINDUI_MORE);
    CHECK(code == FINDUI_OK && total == 5000u);
    unsigned after = atomic_load(&f.hook.scans);
    printf("P4-3 sparse replacement new source-search stages=%u (M)=scan-hook delta\n", after - before);
    fflush(stdout); CHECK(after == before);
    for (size_t i = 0; i < total; i++) {
        uint8_t pair[2]; CHECK(piece_read(f.tree, i * 800u, pair, sizeof pair) == PIECE_OK);
        CHECK(memcmp(pair, "ab", sizeof pair) == 0);
    }
    undo_destroy(&undo); finish(&f); CHECK(munmap(mapping, length) == 0);
}
static void irregular_sparse_plan(void)
{
    /* Alternating inherited sparse strides (800, then 800 + one byte) force
     * record boundaries beyond the prepaid spool block. Case alternation
     * exercises folded literals and folded regex through the same plan. */
    size_t length = 4u * 1024u * 1024u;
    uint8_t *bytes = malloc(length); CHECK(bytes); memset(bytes, 'x', length);
    uint64_t offsets[5000];
    for (size_t i = 0; i < sizeof offsets / sizeof *offsets; i++) {
        offsets[i] = i * 800u + i / 2u;
        memcpy(bytes + offsets[i], i % 2u ? "AB" : "ab", 2);
    }
    for (unsigned regex = 0; regex < 2; regex++) {
        fixture f; start(&f, bytes, length, 64, 64);
        uint8_t *mapping = immutable_source(&f, bytes, length);
        CHECK(findui_set_options(&f.panel, (findui_options){regex != 0, false, false}) == FINDUI_OK);
        query(&f, regex ? "[a]b" : "ab");
        CHECK(findui_get_state(&f.panel).match_count == sizeof offsets / sizeof *offsets);
        findui_impl *impl = f.panel.private_;
        CHECK(impl->plan && impl->plan->plan.records > FINDUI_PLAN_RUNS);
        unsigned before = atomic_load(&f.hook.scans);
        CHECK(mprotect(mapping, length, PROT_NONE) == 0);
        /* A late window and neighbouring matches derive from the final cache
         * page's start, rather than a fabricated byte/ordinal target. */
        size_t count = impl->config.visible_capacity;
        size_t first = sizeof offsets / sizeof *offsets - count;
        CHECK(findui_set_window(&f.panel, offsets[first], offsets[first + count - 1u] + 2u) == FINDUI_OK);
        wait_result(&f);
        findui_range ranges[64]; size_t got = 0;
        CHECK(findui_highlights(&f.panel, offsets[first], offsets[first + count - 1u] + 2u,
                               ranges, sizeof ranges / sizeof *ranges, &got) == FINDUI_OK);
        CHECK(got == count);
        for (size_t i = 0; i < got; i++) CHECK(ranges[i].start == offsets[first + i] && ranges[i].end == offsets[first + i] + 2u);
        findui_range selected; CHECK(findui_next(&f.panel, -1, &selected) == FINDUI_MORE); wait_result(&f);
        CHECK(findui_get_state(&f.panel).selected.start == offsets[sizeof offsets / sizeof *offsets - 1u]);
        CHECK(atomic_load(&f.hook.scans) == before);
        finish(&f); CHECK(munmap(mapping, length) == 0);
    }
    free(bytes);
    puts("P4-3 irregular folded literal/regex plan: protected source window and ordinal passed");
}
typedef struct search_lane_probe { findui_slot *slot; atomic_bool ran, unfinished; } search_lane_probe;
static void observe_search_lane(work_ctx *context)
{
    search_lane_probe *p = context->arg;
    /* Both callbacks run on the sole bulk worker: this reads worker-owned
     * state without a race or reliance on UI mailbox delivery. */
    atomic_store(&p->unfinished, !p->slot->publishing);
    atomic_store(&p->ran, true);
}
static void search_stage_yields(void)
{
    /* Reuse the sparse source and minimal caches; no mailbox saturation can
     * explain yielding. The final expression holds one match across a source
     * longer than a scan slice, exercising an in-flight regex candidate. */
    size_t length = 4u * 1024u * 1024u;
    uint8_t *bytes = malloc(length); CHECK(bytes); memset(bytes, 'x', length);
    const char *patterns[] = {"ab", "[a]b", "x+"};
    for (size_t i = 0; i < sizeof patterns / sizeof *patterns; i++) {
        fixture f; start(&f, bytes, length, 1, 1);
        CHECK(findui_set_options(&f.panel, (findui_options){i != 0, true, false}) == FINDUI_OK);
        atomic_store(&f.hook.park, true);
        CHECK(findui_set_query(&f.panel, (const uint8_t *)patterns[i], strlen(patterns[i])) == FINDUI_OK);
        while (!atomic_load(&f.hook.entered)) pause_worker();
        findui_impl *impl = f.panel.private_;
        findui_slot *slot = impl->slots[0].snapshot ? &impl->slots[0] : &impl->slots[1];
        search_lane_probe p = {.slot = slot}; atomic_init(&p.ran, false); atomic_init(&p.unfinished, false);
        CHECK(work_submit(f.pool, (work_job){observe_search_lane, &p, 0, WORK_BULK}).epoch);
        atomic_store(&f.hook.release, true);
        while (!atomic_load(&p.ran)) pause_worker();
        printf("P4-3 matcher yields before completion: pattern=%s unfinished=%d (M)=bulk marker\n",
               patterns[i], atomic_load(&p.unfinished)); fflush(stdout);
        CHECK(atomic_load(&p.unfinished));
        wait_result(&f);
        CHECK(findui_get_state(&f.panel).complete);
        CHECK(findui_get_state(&f.panel).match_count == (i == 2 ? 1u : 0u));
        finish(&f);
    }
    free(bytes);
}

/* edit-m15: use the null editor host to check dispatch and clipboard routing,
 * not only the standalone query API. Geometry and settle bound reuse the
 * editor mouse/panel fixtures; byte expectations derive from these strings. */
static void panel_settle(editor *e)
{
    uint64_t deadline=trace_now_ns()+UINT64_C(5000000000);
    do {
        int rc=editor_step(e,0); CHECK(rc==EDITOR_OK || rc==EDITOR_MORE);
        editor_stats stats=editor_get_stats(e);
        if (!stats.pending && !stats.render_active) return;
        pause_worker();
    } while (trace_now_ns()<deadline);
    CHECK(false);
}
static void panel_key(editor *e, uint32_t symbol, uint16_t mods)
{
    plat_event ev={.kind=PLAT_EV_KEY,.press=true,.keysym=symbol,.mods=mods};
    CHECK(editor_inject(e,&ev)==EDITOR_OK); panel_settle(e);
}
static void panel_query(editor *e, const char *text, size_t cursor)
{
    findui_state state=findui_get_state(&e->find);
    CHECK(state.query_length==strlen(text) && state.query_cursor==cursor);
    CHECK(!memcmp(state.query,text,state.query_length));
}
static void panel_editing_chords(void)
{
    const char text[]="alpha beta";
    render_backend backend={0}; CHECK(render_null_backend(&backend)==0);
    editor_config cfg={.initial=(const uint8_t *)text,.initial_len=sizeof text-1,
        .cols=40,.rows=8,.wrap_mode=-1};
    editor *e=NULL; CHECK(editor_open(&e,&cfg,&backend)==0); panel_settle(e);
    panel_key(e,'a',PLAT_MOD_CTRL); panel_key(e,'c',PLAT_MOD_CTRL);
    panel_key(e,XKB_KEY_End,PLAT_MOD_CTRL);
    view_selection selected=editor_view(e).selection;
    uint64_t revision=e->buffer->revision;
    panel_key(e,'f',PLAT_MOD_CTRL);
    edit_malloc_guard_begin();
    panel_key(e,'v',PLAT_MOD_CTRL);
    CHECK(edit_malloc_guard_end()==0);
    same(e->tree,(const uint8_t *)text,sizeof text-1);
    panel_query(e,text,sizeof text-1);
    edit_malloc_guard_begin();
    panel_key(e,XKB_KEY_Left,PLAT_MOD_CTRL);
    panel_query(e,text,sizeof "alpha "-1);
    panel_key(e,XKB_KEY_Left,PLAT_MOD_CTRL); panel_query(e,text,0);
    panel_key(e,XKB_KEY_Right,PLAT_MOD_CTRL); panel_query(e,text,sizeof "alpha"-1);
    panel_key(e,XKB_KEY_End,0); panel_key(e,XKB_KEY_BackSpace,PLAT_MOD_CTRL);
    panel_query(e,"alpha ",sizeof "alpha "-1);
    panel_key(e,'a',PLAT_MOD_CTRL); CHECK(e->find_field_all);
    panel_key(e,'v',PLAT_MOD_CTRL); panel_query(e,text,sizeof text-1);
    /* (P) Field undo has no history yet; undo/redo remain field no-ops.
     * Delete at the field end cannot change either field or document.
     * Window commands formerly pinned here are exercised below. */
    const uint32_t consumed[]={'z','y',XKB_KEY_Delete};
    for (size_t i=0;i<sizeof consumed/sizeof *consumed;i++) {
        panel_key(e,consumed[i],PLAT_MOD_CTRL); panel_query(e,text,sizeof text-1);
        same(e->tree,(const uint8_t *)text,sizeof text-1);
        CHECK(tabs_count(&e->tabs)==1 && e->buffer->revision==revision);
        CHECK(e->v.state.selection.cursor==selected.cursor && e->v.state.selection.anchor==selected.anchor);
    }
    const struct { uint32_t sym; uint16_t mods; } other_chords[]={
        {'v',PLAT_MOD_CTRL|PLAT_MOD_ALT}, {'a',PLAT_MOD_SUPER},
        {XKB_KEY_Return,PLAT_MOD_ALT}, {XKB_KEY_Tab,PLAT_MOD_ALT},
    };
    for (size_t i=0;i<sizeof other_chords/sizeof *other_chords;i++) {
        panel_key(e,other_chords[i].sym,other_chords[i].mods); panel_query(e,text,sizeof text-1);
        CHECK(e->buffer->revision==revision && e->v.state.selection.cursor==selected.cursor &&
            e->v.state.selection.anchor==selected.anchor);
    }
    const uint32_t toggles[]={'r','c','w'};
    findui_options options=findui_get_state(&e->find).options;
    for (size_t i=0;i<sizeof toggles/sizeof *toggles;i++) {
        panel_key(e,toggles[i],PLAT_MOD_ALT); panel_key(e,toggles[i],PLAT_MOD_ALT);
    }
    findui_options restored=findui_get_state(&e->find).options;
    CHECK(options.regex==restored.regex && options.match_case==restored.match_case &&
        options.whole_word==restored.whole_word);
    /* (P) Copy/cut apply to the field's select-all, never the document. */
    panel_key(e,'a',PLAT_MOD_CTRL); panel_key(e,'c',PLAT_MOD_CTRL);
    panel_query(e,text,sizeof text-1);
    panel_key(e,'x',PLAT_MOD_CTRL); panel_query(e,"",0);
    panel_key(e,'v',PLAT_MOD_CTRL); panel_query(e,text,sizeof text-1);
    const char unicode[]="é 中"; /* combining cluster, NBSP, CJK word */
    CHECK(findui_set_query(&e->find,(const uint8_t *)unicode,sizeof unicode-1)==FINDUI_OK);
    e->find_field_all=false;
    panel_key(e,XKB_KEY_Left,PLAT_MOD_CTRL); panel_query(e,unicode,sizeof "é "-1);
    panel_key(e,XKB_KEY_BackSpace,PLAT_MOD_CTRL); panel_query(e,"中",0);
    panel_key(e,'a',PLAT_MOD_CTRL); panel_key(e,XKB_KEY_BackSpace,PLAT_MOD_CTRL); panel_query(e,"",0);
    panel_key(e,'h',PLAT_MOD_CTRL); panel_key(e,XKB_KEY_Tab,0);
    panel_key(e,'v',PLAT_MOD_CTRL);
    findui_state state=findui_get_state(&e->find);
    CHECK(state.replacement_focus && state.replacement_length==sizeof text-1);
    CHECK(!memcmp(state.replacement,text,sizeof text-1)); panel_query(e,"",0);
    panel_key(e,XKB_KEY_BackSpace,PLAT_MOD_CTRL);
    state=findui_get_state(&e->find); CHECK(state.replacement_length==sizeof "alpha "-1);
    panel_key(e,XKB_KEY_Tab,0);
    uint8_t full[FINDUI_QUERY_BYTES]; memset(full,'q',sizeof full);
    CHECK(findui_set_query(&e->find,full,sizeof full)==FINDUI_OK); e->find_field_all=false;
    panel_key(e,'v',PLAT_MOD_CTRL); state=findui_get_state(&e->find);
    CHECK(state.query_length==sizeof full && !memcmp(state.query,full,sizeof full));
    same(e->tree,(const uint8_t *)text,sizeof text-1); CHECK(e->buffer->revision==revision);
    CHECK(edit_malloc_guard_end()==0);
    panel_key(e,XKB_KEY_Escape,0); CHECK(!e->find_visible);
    panel_key(e,'v',PLAT_MOD_CTRL);
    same(e->tree,(const uint8_t *)"alpha betaalpha beta",2u*(sizeof text-1));
    editor_close(e);
    puts("edit-m15 find panel: clipboard/query chords isolated from document passed");
}

/* edit-cxho.2 (P): reuse panel_editing_chords geometry and panel_settle's
 * existing watchdog. Counts/offsets derive from the fixture strings and tab
 * operations. Zero denotes absence/first index, one removes a C terminator
 * or advances a tab/byte; minus one selects the existing no-wrap mode.
 * No new capacity or timing budget. */
static void panel_text(editor *e, const char *text)
{
    for (size_t i=0;i<strlen(text);i++) {
        plat_event ev={.kind=PLAT_EV_KEY,.press=true,.keysym=(uint8_t)text[i],
            .utf8={(uint8_t)text[i]},.utf8_len=sizeof text[i]};
        CHECK(editor_inject(e,&ev)==EDITOR_OK); panel_settle(e);
    }
}
static editor *panel_host(render_backend *backend, const char *path)
{
    const char text[]="alpha beta alpha";
    CHECK(render_null_backend(backend)==0);
    editor_config cfg={.path=path,.initial=path ? NULL : (const uint8_t *)text,
        .initial_len=path ? 0 : sizeof text-1u,.cols=40,.rows=8,.wrap_mode=-1};
    editor *e=NULL; CHECK(editor_open(&e,&cfg,backend)==0); panel_settle(e);
    return e;
}
/* edit-cxho.3 (P): coordinates come from live grid cells and painted labels;
 * offsets/lengths come from the literals, zero is success/first cell, one
 * removes a NUL or selects the primary mouse button. Geometry/watchdog reuse
 * panel_host/panel_settle. No timing or capacity budget is introduced. */
static void panel_click(editor *e, uint32_t row, uint32_t column)
{
    plat_event ev = {.kind = PLAT_EV_BUTTON, .press = true, .code = 1,
        .x = (int32_t)(column * e->grid.dims.cell_w),
        .y = (int32_t)(row * e->grid.dims.cell_h)};
    CHECK(editor_inject(e, &ev) == EDITOR_OK); panel_settle(e);
    ev.press = false;
    CHECK(editor_inject(e, &ev) == EDITOR_OK); panel_settle(e);
}
static uint32_t panel_toggle_column(editor *e)
{
    const render_grid *g = editor_grid(e);
    size_t start = (size_t)(g->dims.rows - e->panel_rows) * g->dims.cols;
    for (uint32_t c = 0; c < g->dims.cols; c++)
        if (g->cells[start + c].glyph_index == '[') return c;
    CHECK(false); return 0;
}
static void panel_focus_rows(void)
{
    render_backend backend = {0}; editor *e = panel_host(&backend, NULL);
    panel_key(e, 'h', PLAT_MOD_CTRL); panel_text(e, "alpha");
    panel_key(e, XKB_KEY_Tab, 0); panel_text(e, "beta");
    uint32_t row = e->grid.dims.rows - e->panel_rows;
    panel_click(e, row, (uint32_t)(sizeof "Find: a" - 1u));
    panel_text(e, "X"); panel_query(e, "aXlpha", sizeof "aX" - 1u);
    panel_click(e, row + 1u, (uint32_t)(sizeof "Replace: b" - 1u));
    panel_text(e, "Y");
    findui_state state = findui_get_state(&e->find);
    CHECK(state.replacement_length == sizeof "bYeta" - 1u &&
        state.replacement_cursor == sizeof "bY" - 1u);
    CHECK(!memcmp(state.replacement, "bYeta", state.replacement_length));
    /* Ctrl+H must select the query even when the replacement was last active. */
    panel_click(e, e->tab_rows, e->lay.gw);
    panel_key(e, 'h', PLAT_MOD_CTRL); panel_text(e, "alpha");
    panel_query(e, "alpha", sizeof "alpha" - 1u);
    state = findui_get_state(&e->find);
    CHECK(!state.replacement_focus && state.replacement_length == sizeof "bYeta" - 1u);
    CHECK(!memcmp(state.replacement, "bYeta", state.replacement_length));
    panel_key(e, 'f', PLAT_MOD_CTRL); panel_text(e, "alpha");
    uint32_t toggle = panel_toggle_column(e);
    row = e->grid.dims.rows - e->panel_rows;
    edit_malloc_guard_begin();
    panel_click(e, row, toggle);
    CHECK(findui_get_state(&e->find).options.regex != state.options.regex);
    panel_click(e, row, toggle); /* restore, so later clicks retain the same summary width */
    panel_click(e, row, toggle + (uint32_t)(sizeof "[  ] " - 1u));
    CHECK(findui_get_state(&e->find).options.match_case != state.options.match_case);
    panel_click(e, row, toggle + (uint32_t)(sizeof "[  ] [aa] " - 1u));
    CHECK(findui_get_state(&e->find).options.whole_word != state.options.whole_word);
    CHECK(edit_malloc_guard_end() == 0); /* (P) no allocations on the input path */
    panel_query(e, "alpha", sizeof "alpha" - 1u);
    editor_close(e);
    puts("edit-cxho.3 find panel: query/replacement cell clicks and option toggles passed");
}
static void panel_focus_document_areas(void)
{
    const bool in_minimap[] = {false, true}; /* (P) gutter and minimap hit regions */
    for (size_t i = 0; i < sizeof in_minimap / sizeof *in_minimap; i++) {
        render_backend backend = {0}; editor *e = panel_host(&backend, NULL);
        panel_key(e, 'f', PLAT_MOD_CTRL); panel_text(e, "alpha");
        panel_click(e, e->tab_rows, in_minimap[i] ? e->text_grid.dims.cols : 0);
        panel_text(e, "x"); panel_query(e, "alpha", sizeof "alpha" - 1u);
        const char *expected = in_minimap[i] ? "xalpha beta alpha" : "x";
        same(e->tree, (const uint8_t *)expected, strlen(expected));
        /* Tab changes retain document focus, then retain field focus. */
        uint64_t added;
        CHECK(editor_add_buffer(e, NULL, (const uint8_t *)"other", sizeof "other" - 1u, &added) == EDITOR_OK);
        panel_settle(e); panel_text(e, "y");
        same(e->tree, (const uint8_t *)"yother", sizeof "yother" - 1u);
        panel_query(e, "alpha", sizeof "alpha" - 1u);
        panel_key(e, 'f', PLAT_MOD_CTRL); panel_key(e, XKB_KEY_Page_Up, PLAT_MOD_CTRL);
        panel_text(e, "beta"); panel_query(e, "beta", sizeof "beta" - 1u);
        same(e->tree, (const uint8_t *)expected, strlen(expected));
        editor_close(e);
    }
    puts("edit-cxho.3 find panel: gutter/minimap document focus and tab focus retention passed");
}
static void panel_matches(editor *e, uint64_t count)
{
    findui_state state=findui_get_state(&e->find);
    CHECK(e->find_visible && state.open && state.complete && !state.searching);
    CHECK(e->find_id==e->buffer->id && e->find_buffer_revision==e->buffer->revision);
    CHECK(state.match_count==count);
    /* Highlight byte ranges must belong to the newly active source. */
    findui_range ranges[sizeof "alpha beta alpha"]; size_t got=0;
    CHECK(findui_highlights(&e->find,0,editor_length(e),ranges,
        sizeof ranges/sizeof *ranges,&got)==FINDUI_OK);
    CHECK(got==count);
    for (size_t i=0;i<got;i++) {
        uint8_t actual[sizeof "alpha"-1u];
        CHECK(ranges[i].end-ranges[i].start==sizeof actual);
        CHECK(editor_read(e,ranges[i].start,actual,sizeof actual)==EDITOR_OK);
        CHECK(!memcmp(actual,"alpha",sizeof actual));
    }
}
static void panel_window_commands(const char *which)
{
    /* Saved files exercise immediate close; untitled nonempty buffers
     * correctly prompt even before their first mutation. */
    char first_path[]="build/find-panel-first-XXXXXX", second_path[]="build/find-panel-second-XXXXXX";
    const char first[]="alpha beta alpha", second[]="beta alpha";
    int fd=mkstemp(first_path); CHECK(fd>=0);
    CHECK(write(fd,first,sizeof first-1u)==(ssize_t)(sizeof first-1u)); CHECK(close(fd)==0);
    fd=mkstemp(second_path); CHECK(fd>=0);
    CHECK(write(fd,second,sizeof second-1u)==(ssize_t)(sizeof second-1u)); CHECK(close(fd)==0);
    render_backend backend={0}; editor *e=panel_host(&backend,first_path);
    uint64_t original=e->buffer->id, other;
    CHECK(editor_add_buffer(e,second_path,NULL,0,&other)==EDITOR_OK);
    panel_settle(e); panel_key(e,'f',PLAT_MOD_CTRL);
    edit_malloc_guard_begin(); panel_text(e,"alpha");
    CHECK(edit_malloc_guard_end()==0); /* (P) binding no-malloc typing rule */
    panel_query(e,"alpha",sizeof "alpha"-1u); panel_matches(e,1);
    if (!strcmp(which,"switch")) {
        panel_key(e,XKB_KEY_Tab,PLAT_MOD_CTRL);
        CHECK(e->buffer->id==original); panel_matches(e,2);
        /* Release ends MRU cycling; both directions and adjacent selection
         * must retarget instead of changing query focus via plain Tab. */
        plat_event release={.kind=PLAT_EV_KEY,.keysym=XKB_KEY_Control_L};
        CHECK(editor_inject(e,&release)==EDITOR_OK); panel_settle(e);
        panel_key(e,XKB_KEY_Tab,PLAT_MOD_CTRL|PLAT_MOD_SHIFT);
        CHECK(e->buffer->id==other); panel_matches(e,1);
        CHECK(editor_inject(e,&release)==EDITOR_OK); panel_settle(e);
        panel_key(e,XKB_KEY_Prior,PLAT_MOD_CTRL);
        CHECK(e->buffer->id==original); panel_matches(e,2);
        panel_key(e,XKB_KEY_Next,PLAT_MOD_CTRL);
        CHECK(e->buffer->id==other); panel_matches(e,1);
        panel_key(e,'1',PLAT_MOD_ALT);
        CHECK(e->buffer->id==original); panel_matches(e,2);
    } else if (!strcmp(which,"goto")) {
        panel_key(e,'g',PLAT_MOD_CTRL);
        CHECK(e->go.active && e->go.kind==KEYS_ACTION_GOTO_LINE && e->find_visible);
        panel_key(e,XKB_KEY_Escape,0); CHECK(!e->go.active && e->find_visible);
    } else {
        if (!strcmp(which,"menu")) {
            panel_key(e,'f',PLAT_MOD_ALT);
            CHECK(e->menu.open==menu_mnemonic('f'));
            const menu_def *file_menu=&menu_table()[e->menu.open];
            size_t item=0;
            while (item<file_menu->count && file_menu->items[item]!=KEYS_ACTION_CLOSE) item++;
            CHECK(item<file_menu->count);
            for (size_t i=0;i<item;i++) panel_key(e,XKB_KEY_Down,0);
            panel_key(e,XKB_KEY_Return,0);
        } else if (!strcmp(which,"palette")) {
            panel_key(e,'p',PLAT_MOD_CTRL|PLAT_MOD_SHIFT);
            CHECK(e->palette_open && e->find_visible);
            /* Select the actual Close command in the existing palette model;
             * Enter still travels through the normal injected key path. */
            size_t selected=0;
            while (selected<e->palette->count &&
                e->palette->rows[e->palette->matches[selected]].binding->action!=KEYS_ACTION_CLOSE) selected++;
            CHECK(selected<e->palette->count); e->palette->selected=selected;
            panel_key(e,XKB_KEY_Return,0); CHECK(!e->palette_open);
        } else panel_key(e,'w',PLAT_MOD_CTRL);
        CHECK(tabs_count(&e->tabs)==1 && e->buffer->id==original); panel_matches(e,2);
        panel_key(e,'t',PLAT_MOD_CTRL|PLAT_MOD_SHIFT);
        CHECK(tabs_count(&e->tabs)==2 && e->buffer->id==other); panel_matches(e,1);
        panel_key(e,'w',PLAT_MOD_CTRL); panel_key(e,'w',PLAT_MOD_CTRL);
        CHECK(!tabs_count(&e->tabs) && e->buffer==e->empty); panel_matches(e,0);
        CHECK(!e->find_range_count && !e->find_select_pending);
    }
    panel_query(e,"alpha",sizeof "alpha"-1u);
    editor_close(e); CHECK(unlink(first_path)==0); CHECK(unlink(second_path)==0);
    printf("edit-cxho.2 find panel: %s passed\n",which);
}
static void panel_save_and_prompt(void)
{
    char path[]="build/find-panel-save-XXXXXX";
    const char text[]="alpha", saved[]="xalpha";
    int fd=mkstemp(path); CHECK(fd>=0);
    CHECK(write(fd,text,sizeof text-1u)==(ssize_t)(sizeof text-1u)); CHECK(close(fd)==0);
    render_backend backend={0}; editor *e=panel_host(&backend,path);
    panel_text(e,"x"); CHECK(editor_tab(e,0)->modified);
    uint64_t revision=e->buffer->revision;
    panel_key(e,'f',PLAT_MOD_CTRL); panel_text(e,"alpha");
    /* (P) There is document undo history here: field undo/redo must keep it. */
    panel_key(e,'z',PLAT_MOD_CTRL); panel_key(e,'y',PLAT_MOD_CTRL);
    same(e->tree,(const uint8_t *)saved,sizeof saved-1u);
    CHECK(e->buffer->revision==revision && editor_tab(e,0)->modified);
    panel_key(e,'s',PLAT_MOD_CTRL);
    CHECK(editor_flush(e)==EDITOR_OK); panel_settle(e);
    CHECK(!editor_tab(e,0)->modified && e->buffer->revision==revision);
    CHECK(!savectl_get_model(e->buffer->save).modified); panel_matches(e,1);
    uint8_t actual[sizeof saved]; fd=open(path,O_RDONLY); CHECK(fd>=0);
    CHECK(read(fd,actual,sizeof actual)==(ssize_t)(sizeof saved-1u)); CHECK(close(fd)==0);
    CHECK(!memcmp(actual,saved,sizeof saved-1u));
    panel_key(e,XKB_KEY_Escape,0); panel_text(e,"y");
    panel_key(e,'f',PLAT_MOD_CTRL); panel_key(e,'w',PLAT_MOD_CTRL);
    CHECK(tabs_count(&e->tabs)==1 && e->close_prompt.owner==e->buffer->id && e->find_visible);
    panel_key(e,XKB_KEY_Escape,0); CHECK(!e->close_prompt.owner && e->find_visible);
    panel_key(e,'w',PLAT_MOD_CTRL); panel_key(e,'s',0);
    CHECK(editor_flush(e)==EDITOR_OK); panel_settle(e);
    CHECK(!tabs_count(&e->tabs) && e->find_visible && !e->close_prompt.saving);
    panel_matches(e,0);
    editor_close(e); CHECK(unlink(path)==0);
    puts("edit-cxho.2 find panel: save revision/modified state and close prompt passed");
}

int main(void)
{
    const char *panel_case=getenv("EDIT_CXHO_PANEL");
    if (panel_case) {
        if (!strcmp(panel_case,"focus")) { panel_focus_rows(); panel_focus_document_areas(); }
        else if (!strcmp(panel_case,"save")) panel_save_and_prompt();
        else if (!strcmp(panel_case,"field")) panel_editing_chords();
        else panel_window_commands(panel_case);
        return 0;
    }
    if (getenv("EDIT_M15_PANEL")) { panel_editing_chords(); return 0; }
    panel_editing_chords();
    panel_focus_rows(); panel_focus_document_areas();
    panel_window_commands("close"); panel_window_commands("switch");
    panel_window_commands("goto"); panel_window_commands("menu"); panel_window_commands("palette");
    panel_save_and_prompt();
    if (getenv("EDIT_LEZ_REGEX_WORD")) { whole_word_regex_worker(); return 0; }
    if (getenv("P4_SPARSE_WINDOW")) { sparse_window_work(); return 0; }
    if (getenv("P4_SPARSE_PAGES")) { sparse_page_work(); return 0; }
    if (getenv("P4_SEARCH_YIELD")) { search_stage_yields(); return 0; }
    sparse_window_work();
    sparse_page_work(); irregular_sparse_plan(); search_stage_yields();
    replacement_paged_noop();
    full_source_lifetime();
    replacement_host_bookkeeping();
    replacement_large_match();
    replacement_beyond_cache();
    first_offsets_independent();
    viewport_retains_count();
    backpressure_yields();
    whole_word_publication();
    whole_word_count_worker();
    whole_word_regex_worker();
    maximum_literals();
    if (getenv("EDIT_YQU_KNOWN_FAILURES")) known_default_long_query();
    filtered_regex_budget();
    bounded_count_worker();
    incremental_cancel(); wrap_and_visible(); toggles(); replace_groups(); replacement_failure_and_cancel(); lifecycle_and_backpressure(); painting_and_no_malloc();
    puts("findui_test: all passed");
    return 0;
}
