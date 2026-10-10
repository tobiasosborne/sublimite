#include "findui/findui.h"
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(x) EDIT_ASSERT(x)

typedef struct probe {
    pthread_t caller;
    atomic_uint scans, on_caller;
} probe;
typedef struct fixture {
    edit_arena arena;
    work_pool *pool;
    piece_tree *tree;
    findui_panel panel;
    probe hook;
    work_msg saved;
    bool have_saved;
    size_t allocations, fail_allocation;
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
    if (pthread_equal(pthread_self(), p->caller)) atomic_fetch_add(&p->on_caller, 1);
}
static void route(const work_msg *message, void *context)
{
    fixture *f = context;
    if (message->kind == FINDUI_MSG_ONE || message->kind == FINDUI_MSG_TWO) {
        f->saved = *message; f->have_saved = true;
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
        findui_code code = findui_replace_step(&f.panel, 64, 0, &replaced);
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
int main(void)
{
    filtered_regex_budget();
    bounded_count_worker();
    incremental_cancel(); wrap_and_visible(); toggles(); replace_groups(); replacement_failure_and_cancel(); lifecycle_and_backpressure(); painting_and_no_malloc();
    puts("findui_test: all passed");
    return 0;
}
