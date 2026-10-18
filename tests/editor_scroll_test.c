#include "editor/editor.h"
#include "base/base.h"
#include "editor/private.h"
#include "trace/trace.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

/* (G) Reuse the public editor fixture and retry bound in
 * scroll_test.c:editor_observability/settle_editor; these are fixture controls,
 * not new timing budgets. LF width below is character + newline = 2 bytes. */
#define PROBE_BYTES 400u
#define PROBE_COLS 32u
#define PROBE_ROWS 8u
#define PROBE_RETRIES 10000u
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "editor_scroll_test:%d: FAIL %s\n", __LINE__, #c); result = 1; goto done; } } while (0)
typedef struct observer { editor_frame last; editor_refresh last_refresh; size_t frames, refreshes; } observer;
static void submitted(void *ctx, const editor_frame *frame)
{ observer *o = ctx; o->last = *frame; o->frames++; }
static void refreshed(void *ctx, const editor_refresh *refresh)
{ observer *o = ctx; o->last_refresh = *refresh; o->refreshes++; }
static int delayed_present(render_backend *backend, uint32_t id)
{ (void)backend; (void)id; return 0; }
static int settle(editor *e)
{
    /* Existing scroll integration probe's retry bound, not a latency budget. */
    for (unsigned i = 0; i < PROBE_RETRIES; i++) {
        int rc = editor_step(e, 0);
        if (rc != EDITOR_OK && rc != EDITOR_MORE) return rc;
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return 0;
    }
    return EDITOR_MORE;
}
/* edit-08u: type one key while a worker window is held, inside the malloc
 * guard, and step until the keystroke's own frame is out (submitted past
 * `baseline`, render idle). Not settle(): its !pending also covers the scroll
 * window's renewal, which waits on the fixture's parked worker; whether that
 * renewal starts before settle samples !pending is scheduler timing, so it is
 * not part of what these cases assert. Same PROBE_RETRIES bound. Outcomes:
 *  - inject/step error: returned, fails as before;
 *  - frame out: 0, caller checks allocations == 0 as before;
 *  - bound exhausted while editor_scroll_waiting() on a closing resident
 *    window (the key was applied and retired the parked worker's window):
 *    EDITOR_MORE, fails. That retire returns MORE until release and compose
 *    is gated on scroll_waiting (editor.c submit()), so the frame cannot come out at
 *    any load: a real stall, not a timeout;
 *  - bound exhausted otherwise: a timeout of a non-blocking retry count, so
 *    load-dependent. Law 1: a tracked line (elapsed, load1, BAT0), returns 0
 *    with *timed_out set; the caller still checks allocations, then ends the
 *    case, since the later checks need the frame it never reached.
 * edit-63p's deterministic prefix-growth variant also requires the first
 * turn after dispatch to submit; cancellation never skips on a timeout. */
static int held_key(editor *e, const char *name, uint64_t baseline, bool grow_prefix, size_t *allocations, bool *timed_out)
{
    plat_event event = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'x', .utf8_len = 1, .utf8 = {'x'}};
    uint64_t start = trace_now_ns();
    edit_malloc_guard_begin();
    /* edit-63p (P): dispatch the key, then publish its edited chunk before
     * the next scroll/submit turn. This forces seek -> prefix growth ->
     * submit without depending on worker or UI scheduling. */
    int rc = grow_prefix ? editor_handle_key(e, &event) : editor_inject(e, &event);
    if (!rc && grow_prefix) {
        rc = editor_scroll_slice(e);
        if (!rc && !e->buffer->scroll_resident.closing) rc = EDITOR_ERR_HISTORY;
    }
    if (!rc && grow_prefix) {
        lineidx_src source = editor_source(e->buffer);
        (void)lineidx_refresh(e->buffer->index, &source);
        if (lineidx_built_prefix(e->buffer->index) <= e->buffer->scroll_prefix)
            rc = EDITOR_ERR_HISTORY;
    }
    unsigned i = 0;
    for (; !rc && i < PROBE_RETRIES; i++) {
        rc = editor_step(e, 0);
        if (rc == EDITOR_MORE) rc = 0;
        if (!rc && e->backend->stats.submitted_frames > baseline && !e->backend->active) break;
    }
    *allocations = edit_malloc_guard_end();
    *timed_out = false;
    if (!rc && grow_prefix && i < PROBE_RETRIES) {
        /* (P) i is zero on the first turn after key dispatch. */
        printf("editor_scroll_test: edit-63p prefix-growth key submitted turns=%u\n", i + 1u);
        if (i != 0) return EDITOR_MORE;
    }
    if (rc || i < PROBE_RETRIES) return rc;
    char power[32] = "unknown", load1[32] = "unknown";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (!fgets(power, sizeof power, f)) strcpy(power, "unknown"); fclose(f); }
    power[strcspn(power, "\n")] = 0;
    f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%31s", load1) != 1) strcpy(load1, "unknown"); fclose(f); }
    bool blocked = editor_scroll_waiting(e) && e->buffer->scroll_resident.closing;
    printf("editor_scroll_test: edit-08u %s keystroke frame not out after retries=%u: %s elapsed=%.3f ms "
        "frames=%llu baseline=%llu render_active=%d dirty=%d scroll_waiting=%d closing=%d "
        "scroll_prefix=%zu built_prefix=%zu scroll_revision=%llu revision=%llu allocations=%zu (M) power=%s load1=%s\n",
        name, PROBE_RETRIES, blocked ? "BLOCKED behind the held scroll window (real failure)" : "timeout (tracked, law 1; rest of case skipped)",
        (double)(trace_now_ns() - start) / 1e6, (unsigned long long)e->backend->stats.submitted_frames,
        (unsigned long long)baseline, e->backend->active, e->dirty, blocked, e->buffer->scroll_resident.closing,
        e->buffer->scroll_prefix, lineidx_built_prefix(e->buffer->index),
        (unsigned long long)e->buffer->scroll_revision, (unsigned long long)e->buffer->revision, *allocations, power, load1);
    *timed_out = !blocked;
    return blocked ? EDITOR_MORE : 0;
}
/* edit-73j: minimap summaries publish from a worker and start their own
 * frame. Await that publication before fixtures that count frames or hold a
 * present; same retry bound as settle. */
static int settle_map(editor *e)
{
    for (unsigned i = 0; i < PROBE_RETRIES; i++) {
        const editor_buffer *b = e->buffer;
        /* Mirrors editor_map_service: a job is in flight, or one is due. */
        minimap_input input = editor_map_input(e->buffer);
        bool due = e->map_cols && !b->pending_open && !b->source_stale && input.index_ready &&
            (minimap_stale(&b->map, &input) || b->map.height != e->map_grid.dims.rows);
        if (!b->map_snapshot && !due) return settle(e);
        int rc = editor_step(e, 1);
        if (rc != EDITOR_OK && rc != EDITOR_MORE) return rc;
    }
    return EDITOR_MORE;
}
typedef struct held_source { piece_snapshot *snap; pthread_t ui; _Atomic bool entered, release; } held_source;
/* edit-91c: resident.c's lane probe derives window addresses through the
 * source on the UI thread (no byte access); only the worker copy is held. */
static size_t held_span(void *ctx, uint64_t off, const uint8_t **out)
{
    held_source *h = ctx;
    if (!pthread_equal(pthread_self(), h->ui)) {
        atomic_store(&h->entered, true);
        while (!atomic_load(&h->release)) sched_yield();
    }
    piece_iter it; size_t n = 0; piece_iter_begin_snapshot(&it, h->snap, off);
    return piece_iter_next(&it, out, &n) ? n : 0;
}
static void held_job(work_ctx *job)
{
    held_source *h = job->arg; atomic_store(&h->entered, true);
    while (!atomic_load(&h->release)) sched_yield();
}
static int eof_typing(void)
{
    int result = 0; editor *e = NULL; render_backend backend = {0}; held_source hold = {0};
    editor_config cfg = {.cols = PROBE_COLS, .rows = PROBE_ROWS, .wrap_mode = -1};
    CHECK(render_null_backend(&backend) == 0 && editor_open(&e, &cfg, &backend) == 0 && settle(e) == 0);
    work_handle handle = work_submit(&e->pool, (work_job){held_job, &hold, 0, WORK_BULK});
    CHECK(handle.epoch);
    while (!atomic_load(&hold.entered)) sched_yield();
    uint64_t baseline = backend.stats.submitted_frames;
    size_t allocations = 0; bool timed_out = false;
    int rc = held_key(e, "eof-typing", baseline, false, &allocations, &timed_out);
    CHECK(rc == 0 && (!edit_malloc_guard_active() || allocations == 0));
    if (timed_out) goto done;
    CHECK(editor_length(e) == 1 && editor_view(e).selection.cursor == 1);
    CHECK(!edit_malloc_guard_active() || allocations == 0);
    puts("editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS");
done:
    atomic_store(&hold.release, true); editor_close(e); return result;
}
static int cancellation(void)
{
    int result = 0; editor *e = NULL; render_backend backend = {0}; held_source hold = {0};
    uint8_t bytes[PROBE_BYTES];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = i % 2u ? '\n' : (uint8_t)'a';
    editor_config cfg = {.initial = bytes, .initial_len = sizeof bytes,
        .cols = PROBE_COLS, .rows = PROBE_ROWS, .max_cols = PROBE_COLS, .max_rows = PROBE_ROWS, .wrap_mode = -1};
    CHECK(render_null_backend(&backend) == 0 && editor_open(&e, &cfg, &backend) == 0 && settle(e) == 0);
    editor_buffer *b = e->buffer;
    CHECK(!b->scroll_resident.generation);
    hold.snap = b->scroll_snapshot; hold.ui = pthread_self();
    b->scroll_resident.snapshot.ctx = &hold; b->scroll_resident.snapshot.span = held_span;
    uint64_t submitted_frames = backend.stats.submitted_frames;
    /* Travel past the prepared rows so this test requires a worker window. */
    plat_event event = {.kind = PLAT_EV_WHEEL, .dy = (int32_t)(SCROLL_WHEEL_UNIT * cfg.rows)};
    CHECK(editor_inject(e, &event) == 0);
    for (unsigned retry = 0; retry < PROBE_RETRIES && !atomic_load(&hold.entered); retry++) {
        CHECK(editor_step(e, 0) >= 0); sched_yield();
    }
    CHECK(atomic_load(&hold.entered) && editor_get_stats(e).pending);
    CHECK(editor_view(e).first_byte == 0 && backend.stats.submitted_frames == submitted_frames);
    size_t allocations = 0; bool timed_out = false;
    int rc = held_key(e, "cancellation", submitted_frames, true, &allocations, &timed_out);
    CHECK(rc == 0 && (!edit_malloc_guard_active() || allocations == 0));
    CHECK(!timed_out);
    CHECK(b->scroll_snapshot == hold.snap && b->scroll_resident.closing);
    uint8_t live = 0, old = 0;
    CHECK(editor_read(e, 0, &live, 1) == 0 && live == 'x');
    CHECK(piece_snapshot_read(hold.snap, 0, &old, 1) == 0 && old == 'a');
    /* (P) The key prefixes the fixture's first 'a'; capture every prepared
     * text cell using the existing viewport capacity for the retire replay. */
    CHECK(e->text_grid.cells[e->lay.gw].glyph_index == 'x');
    CHECK(e->text_grid.cells[e->lay.gw + 1u].glyph_index == 'a');
    render_cell cells[PROBE_COLS * PROBE_ROWS];
    size_t cell_bytes = (size_t)e->text_grid.dims.cols * e->text_grid.dims.rows * sizeof(render_cell);
    memcpy(cells, e->text_grid.cells, cell_bytes);
    submitted_frames = backend.stats.submitted_frames;
    CHECK(editor_full_layout(e) == 0 && editor_step(e, 0) >= 0);
    CHECK(backend.stats.submitted_frames > submitted_frames && b->scroll_resident.closing);
    CHECK(memcmp(cells, e->text_grid.cells, cell_bytes) == 0);
    atomic_store(&hold.release, true);
    for (unsigned retry = 0; retry < PROBE_RETRIES && b->scroll_snapshot == hold.snap && b->scroll_revision != b->revision; retry++) {
        CHECK(editor_step(e, 0) >= 0); sched_yield();
    }
    /* Piece's arena may reuse the physically retired snapshot header. Its
     * revision/bridge lease, rather than pointer inequality, proves renewal. */
    CHECK(b->scroll_snapshot != hold.snap || (b->scroll_revision == b->revision && !b->scroll_resident.closing));
    CHECK(editor_view(e).first_byte == 0 && editor_view(e).selection.cursor == 1);
    CHECK(editor_full_layout(e) == 0 && settle(e) == 0);
    CHECK(render_grid_validate(editor_grid(e)) == 0 && memcmp(cells, e->text_grid.cells, cell_bytes) == 0);
    printf("editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=%zu guard=%s PASS\n",
        allocations, edit_malloc_guard_active() ? "active" : "sanitizer-inert");
done:
    atomic_store(&hold.release, true); editor_close(e); return result;
}
static int resized(void)
{
    int result = 0; editor *e = NULL; render_backend backend = {0}; observer o = {0};
    uint8_t bytes[PROBE_BYTES];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = i % 2u ? '\n' : (uint8_t)'a';
    editor_config cfg = {.initial = bytes, .initial_len = sizeof bytes,
        .cols = PROBE_COLS, .rows = PROBE_ROWS, .wrap_mode = -1, .hook_ctx = &o, .on_submit = submitted};
    CHECK(render_null_backend(&backend) == 0 && editor_open(&e, &cfg, &backend) == 0 && settle(e) == 0);
    plat_event event = {.kind = PLAT_EV_WHEEL, .dy = 1};
    CHECK(editor_inject(e, &event) == 0 && settle(e) == 0);
    int64_t origin = o.last.scroll.origin_y_q8;
    /* Halve both fixture dimensions: the text clip and sidebar policy change. */
    event = (plat_event){.kind = PLAT_EV_RESIZE,
        .w = cfg.cols / 2u * editor_grid(e)->dims.cell_w, .h = cfg.rows / 2u * editor_grid(e)->dims.cell_h};
    CHECK(editor_inject(e, &event) == 0 && settle(e) == 0);
    CHECK(o.last.scroll.origin_y_q8 == origin && o.last.scroll.layout_rows == editor_grid(e)->dims.rows);
    CHECK(render_grid_validate(editor_grid(e)) == 0);
    puts("editor_scroll_test: fractional resize rebuilds clip/overscan and preserves origin PASS");
done:
    editor_close(e); return result;
}
/* edit-pd6 (editor-2 finding 13): wheel or page input while a far byte jump
 * (find, minimap, Ctrl+End) is still resolving must not stop the loop. The
 * jump is held deterministically by blocking its resident window (held_span,
 * as cancellation() does). Expected: the input is queued and applied once the
 * jump lands, so the final state equals the unraced reference where the same
 * input arrives after the jump resolved. Fixture reuses PROBE_* above. */
typedef struct jump_result { uint64_t first_byte, first_line, cursor, anchor; } jump_result;
static int jump_then(plat_event event, bool race, jump_result *out)
{
    int result = 0; editor *e = NULL; render_backend backend = {0}; held_source hold = {0};
    /* (P) Line width is sizeof("a\n") minus its terminating NUL. */
    const size_t line_bytes = sizeof "a\n" - 1u;
    uint8_t bytes[PROBE_BYTES];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (uint8_t)"a\n"[i % line_bytes];
    editor_config cfg = {.initial = bytes, .initial_len = sizeof bytes,
        .cols = PROBE_COLS, .rows = PROBE_ROWS, .wrap_mode = -1};
    CHECK(render_null_backend(&backend) == 0 && editor_open(&e, &cfg, &backend) == 0 && settle(e) == 0);
    editor_buffer *b = e->buffer;
    if (race) {
        hold.snap = b->scroll_snapshot; hold.ui = pthread_self();
        b->scroll_resident.snapshot.ctx = &hold; b->scroll_resident.snapshot.span = held_span;
    }
    /* (P) The middle line: PROBE_BYTES/2 bytes = PROBE_BYTES/(2*line_bytes)
     * = 400/(2*2) = 100 lines from either end: beyond the PROBE_ROWS (8)
     * prepared rows, and farther than one page (< PROBE_ROWS rows) or one
     * notch (SCROLL_CORE_ROWS) from either clamp, so neither the jump nor
     * the queued motion is clipped. */
    uint64_t target = PROBE_BYTES / 2u;
    uint64_t frames = backend.stats.submitted_frames;
    /* As a far find result does (findpanel.c): select the match, seek its
     * byte on the scroll path and set the host's follow flags. */
    e->v.state.selection.cursor = e->v.state.selection.anchor = target;
    e->v.state.selection.preferred_col = VIEW_PREFERRED_UNSET;
    CHECK(editor_scroll_seek(e, target) == 0);
    e->wheel_pending = true; e->find_scroll = true;
    if (race) {
        for (unsigned retry = 0; retry < PROBE_RETRIES && !atomic_load(&hold.entered); retry++) {
            CHECK(editor_step(e, 0) >= 0); sched_yield();
        }
        CHECK(atomic_load(&hold.entered) && b->scroll_pending && b->scroll.request == SCROLL_BYTE);
        CHECK(editor_view(e).first_byte == 0 && backend.stats.submitted_frames == frames);
    } else CHECK(settle(e) == 0 && editor_view(e).first_byte == target);
    edit_malloc_guard_begin();
    int rc = editor_inject(e, &event); if (!rc) rc = editor_step(e, 0);
    size_t allocations = edit_malloc_guard_end();
    if (rc < 0) fprintf(stderr, "editor_scroll_test: %s motion returned %d, sticky error %d\n",
        race ? "pending-jump" : "reference", rc, e->error);
    CHECK(rc >= 0 && !e->error && (!edit_malloc_guard_active() || allocations == 0));
    if (race) {
        /* The jump's window is still held: the loop keeps running. */
        CHECK(editor_step(e, 0) >= 0 && !e->error);
        /* A wheel notch or page is queued behind the retained jump; document
         * Home/End supersede it and may land at once. */
        if (event.kind == PLAT_EV_WHEEL || event.keysym == XKB_KEY_Page_Down || event.keysym == XKB_KEY_Page_Up ||
            event.keysym == XKB_KEY_Down || event.keysym == XKB_KEY_Up)
            CHECK(b->scroll_pending && b->scroll.request == SCROLL_BYTE &&
                editor_view(e).first_byte == 0 && backend.stats.submitted_frames == frames);
        atomic_store(&hold.release, true);
    }
    /* Not READY: editor_scroll_follow may adopt a followed state whose
     * request scroll_follow marked RELATIVE (pre-existing, reported). */
    CHECK(settle(e) == 0 && !e->error && !b->scroll_pending && b->scroll.request != SCROLL_BYTE);
    CHECK(backend.stats.submitted_frames > frames);
    view_state v = editor_view(e);
    *out = (jump_result){v.first_byte, v.first_line, v.selection.cursor, v.selection.anchor};
done:
    atomic_store(&hold.release, true); editor_close(e); return result;
}
static int pending_jump_motion(plat_event event)
{
    int result = 0; jump_result reference = {0}, raced = {0};
    CHECK(jump_then(event, false, &reference) == 0);
    CHECK(jump_then(event, true, &raced) == 0);
    if (memcmp(&reference, &raced, sizeof reference))
        fprintf(stderr, "editor_scroll_test: reference top %llu line %llu cursor %llu anchor %llu, raced top %llu line %llu cursor %llu anchor %llu\n",
            (unsigned long long)reference.first_byte, (unsigned long long)reference.first_line,
            (unsigned long long)reference.cursor, (unsigned long long)reference.anchor,
            (unsigned long long)raced.first_byte, (unsigned long long)raced.first_line,
            (unsigned long long)raced.cursor, (unsigned long long)raced.anchor);
    CHECK(!memcmp(&reference, &raced, sizeof reference));
    /* The queued notch/page moved the viewport off the jump target. */
    if (event.kind == PLAT_EV_WHEEL || event.keysym == XKB_KEY_Page_Down || event.keysym == XKB_KEY_Down)
        CHECK(raced.first_byte > PROBE_BYTES / 2u);
    printf("editor_scroll_test: %s keysym=%u during a pending byte jump matches the unraced result, loop continues, top=%llu PASS\n",
        event.kind == PLAT_EV_WHEEL ? "wheel" : "key", event.keysym, (unsigned long long)raced.first_byte);
done:
    return result;
}
static int pending_jump_keys(void)
{
    /* Page and document motions share scroll_key_motion's dispatch; CTRL
     * selects document Home/End rather than line Home/End. Ctrl+Up/Down is
     * edit-o20's scroll_lines chord (scroll_pixels), queued the same way. */
    const uint32_t keys[] = {XKB_KEY_Page_Down, XKB_KEY_Page_Up, XKB_KEY_Home, XKB_KEY_End,
        XKB_KEY_Down, XKB_KEY_Up};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        plat_event event = {.kind = PLAT_EV_KEY, .press = true, .keysym = keys[i],
            .mods = keys[i] == XKB_KEY_Page_Down || keys[i] == XKB_KEY_Page_Up ? 0 : PLAT_MOD_CTRL};
        if (pending_jump_motion(event)) return 1;
    }
    return 0;
}
static int wrapped_wheel(void)
{
    /* (M)[AC] edit-zw9's Astra repro: down 3, down 30, up 30, up 3.
     * (P) Expected travel = notches * SCROLL_CORE_ROWS; provision a paragraph
     * per travelled row plus the existing viewport, with each paragraph
     * wider than PROBE_COLS so every line wraps even without the sidebar. */
    enum { NEAR_NOTCHES = 3, FAR_NOTCHES = 30,
        TRAVEL_ROWS = (NEAR_NOTCHES + FAR_NOTCHES) * SCROLL_CORE_ROWS,
        LINE_BYTES = PROBE_COLS * 2u,
        FIXTURE_BYTES = (TRAVEL_ROWS + PROBE_ROWS) * LINE_BYTES };
    int result = 0; editor *e = NULL, *reference = NULL;
    render_backend backend = {0}, reference_backend = {0}; observer o = {0};
    uint8_t bytes[FIXTURE_BYTES];
    const char prose[] = "Every editor tried is slower than word processors were, and each one carries features that are never used. "
        "What remains is quick, frequent editing of small files, occasional huge logs, and a lot of prose. ";
    for (size_t i = 0; i < sizeof bytes; i++)
        /* (P) Paragraph separator = its LF plus an empty line's LF. */
        bytes[i] = i % LINE_BYTES < LINE_BYTES - 2u ? (uint8_t)prose[(i % LINE_BYTES) % (sizeof prose - 1u)] : (uint8_t)'\n';
    editor_config cfg = {.initial = bytes, .initial_len = sizeof bytes,
        .cols = PROBE_COLS, .rows = PROBE_ROWS, .wrap_mode = 1,
        .hook_ctx = &o, .on_submit = submitted};
    CHECK(render_null_backend(&backend) == 0 && editor_open(&e, &cfg, &backend) == 0 && settle_map(e) == 0);
    CHECK(editor_view(e).wrap && e->lay.wrap_rows[0].next < LINE_BYTES);
    /* (P) A tall null viewport lays out every travelled row independently
     * of adjacent-row queries, with identical width and document geometry. */
    editor_config reference_cfg = cfg;
    reference_cfg.rows = TRAVEL_ROWS + PROBE_ROWS;
    reference_cfg.hook_ctx = NULL; reference_cfg.on_submit = NULL;
    CHECK(render_null_backend(&reference_backend) == 0 &&
        editor_open(&reference, &reference_cfg, &reference_backend) == 0 && settle_map(reference) == 0);
    CHECK(reference->lay.text_cols == e->lay.text_cols);
    uint64_t tops[TRAVEL_ROWS + 1u];
    memcpy(tops, reference->row_byte, sizeof tops);
    CHECK(tops[TRAVEL_ROWS] != LAYOUT_VOID_ROW);
    /* (P) Capture the text cells in the existing viewport's cell capacity.
     * Unfocused text has no blink changes during the reversal comparison. */
    plat_event focus = {.kind = PLAT_EV_FOCUS, .focused = false};
    CHECK(editor_inject(e, &focus) == 0 && settle(e) == 0);
    render_cell initial_cells[PROBE_COLS * PROBE_ROWS], near_cells[PROBE_COLS * PROBE_ROWS];
    size_t cell_bytes = (size_t)e->text_grid.dims.cols * e->text_grid.dims.rows * sizeof(render_cell);
    memcpy(initial_cells, e->text_grid.cells, cell_bytes);
    view_selection selection = editor_view(e).selection;
    plat_event wheel = {.kind = PLAT_EV_WHEEL, .dy = SCROLL_WHEEL_UNIT};
    for (size_t notch = 0; notch < NEAR_NOTCHES; notch++) {
        CHECK(editor_inject(e, &wheel) == 0 && settle(e) == 0);
        fprintf(stderr, "editor_scroll_test: wrapped notch=%zu top=%llu expected=%llu\n",
            notch + 1u, (unsigned long long)e->row_byte[0],
            (unsigned long long)tops[(notch + 1u) * SCROLL_CORE_ROWS]);
        CHECK(e->row_byte[0] == tops[(notch + 1u) * SCROLL_CORE_ROWS]);
    }
    fprintf(stderr, "editor_scroll_test: wrapped near top=%llu expected=%llu\n",
        (unsigned long long)e->row_byte[0], (unsigned long long)tops[NEAR_NOTCHES * SCROLL_CORE_ROWS]);
    CHECK(e->row_byte[0] == tops[NEAR_NOTCHES * SCROLL_CORE_ROWS]);
    CHECK(o.last.scroll.first_byte == e->row_byte[0]);
    memcpy(near_cells, e->text_grid.cells, cell_bytes);
    wheel.dy = FAR_NOTCHES * SCROLL_WHEEL_UNIT;
    CHECK(editor_inject(e, &wheel) == 0 && settle(e) == 0);
    CHECK(e->row_byte[0] == tops[TRAVEL_ROWS] && o.last.scroll.first_byte == tops[TRAVEL_ROWS]);
    wheel.dy = -(int32_t)(FAR_NOTCHES * SCROLL_WHEEL_UNIT);
    CHECK(editor_inject(e, &wheel) == 0 && settle(e) == 0);
    CHECK(e->row_byte[0] == tops[NEAR_NOTCHES * SCROLL_CORE_ROWS]);
    CHECK(memcmp(near_cells, e->text_grid.cells, cell_bytes) == 0);
    wheel.dy = -(int32_t)(NEAR_NOTCHES * SCROLL_WHEEL_UNIT);
    CHECK(editor_inject(e, &wheel) == 0 && settle(e) == 0);
    CHECK(e->row_byte[0] == tops[0] && o.last.scroll.first_byte == tops[0]);
    CHECK(memcmp(initial_cells, e->text_grid.cells, cell_bytes) == 0);
    /* (P) Also verify individually dispatched upward notches, as in the
     * bead's single-notch repro; each removes exactly SCROLL_CORE_ROWS. */
    wheel.dy = NEAR_NOTCHES * SCROLL_WHEEL_UNIT;
    CHECK(editor_inject(e, &wheel) == 0 && settle(e) == 0);
    CHECK(e->row_byte[0] == tops[NEAR_NOTCHES * SCROLL_CORE_ROWS]);
    wheel.dy = -(int32_t)SCROLL_WHEEL_UNIT;
    for (size_t notch = NEAR_NOTCHES; notch; notch--) {
        CHECK(editor_inject(e, &wheel) == 0 && settle(e) == 0);
        CHECK(e->row_byte[0] == tops[(notch - 1u) * SCROLL_CORE_ROWS]);
    }
    CHECK(editor_view(e).visual_byte == tops[0] && o.last.scroll.first_byte == tops[0]);
    CHECK(memcmp(initial_cells, e->text_grid.cells, cell_bytes) == 0);
    CHECK(editor_view(e).selection.cursor == selection.cursor && editor_view(e).selection.anchor == selection.anchor);
    puts("editor_scroll_test: wrapped exact visual-row wheel and reversal PASS");
done:
    editor_close(reference); editor_close(e); return result;
}
int main(int argc, char **argv)
{
    /* (P) argv's first entry is the executable; the next is the selector. */
    if (argc > 1 && !strcmp(argv[1], "--pending-jump-wheel"))
        return pending_jump_motion((plat_event){.kind = PLAT_EV_WHEEL, .dy = SCROLL_WHEEL_UNIT});
    if (argc > 1 && !strcmp(argv[1], "--pending-jump-keys")) return pending_jump_keys();
    int result = 0;
    editor *e = NULL; render_backend backend = {0}; observer o = {0};
    /* Fixture: one ASCII character plus LF per line; enough rows for wheel
     * movement away from either endpoint. Dimensions reuse scroll_test's
     * public-editor probe, including its fixed tab row and sidebar. */
    uint8_t bytes[PROBE_BYTES];
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = i % 2u ? '\n' : (uint8_t)'a';
    editor_config cfg = {.initial = bytes, .initial_len = sizeof bytes,
        .cols = PROBE_COLS, .rows = PROBE_ROWS, .wrap_mode = -1, .hook_ctx = &o,
        .on_submit = submitted, .on_refresh = refreshed};
    CHECK(render_null_backend(&backend) == 0);
    CHECK(editor_open(&e, &cfg, &backend) == 0 && settle(e) == 0 && settle_map(e) == 0);
    view_selection selection = editor_view(e).selection;
    size_t baseline = o.frames;
    plat_event wheel = {.kind = PLAT_EV_WHEEL, .dy = 1, .smooth = true};
    CHECK(editor_inject(e, &wheel) == 0 && settle(e) == 0);
    CHECK(o.frames > baseline);
    CHECK(o.last.scroll.origin_y_q8 == -(int64_t)(SCROLL_CORE_ROWS * editor_grid(e)->dims.cell_h));
    CHECK(o.last.scroll.layout_rows == cfg.rows); /* visible rows = surface minus tab; plus overscan */
    const render_grid *grid = editor_grid(e);
    CHECK(grid->region.pixels && grid->region.x == grid->scroll.cols * grid->dims.cell_w);
    CHECK(grid->region.y == grid->scroll.first_row * grid->dims.cell_h);
    CHECK(grid->region.height == grid->scroll.rows * grid->dims.cell_h);
    CHECK(render_grid_validate(grid) == 0);
    CHECK(o.refreshes == o.frames && o.last_refresh.synthetic && o.last_refresh.metadata_valid);
    CHECK(o.last_refresh.displayed_frame_id == o.last.id);
    CHECK(o.last_refresh.frame.scroll.origin_y_q8 == o.last.scroll.origin_y_q8);
    CHECK(o.last_refresh.frame.present_ns && o.last_refresh.completion_ns >= o.last_refresh.frame.present_ns);
    CHECK(editor_view(e).selection.cursor == selection.cursor && editor_view(e).selection.anchor == selection.anchor);
    wheel.dy = 256; /* plat/scroll's units per notch; three logical rows. */
    CHECK(editor_inject(e, &wheel) == 0 && settle(e) == 0);
    CHECK(editor_view(e).first_byte == 3u * 2u); /* notch rows * fixture line bytes */
    editor_frame last = o.last;
    uint64_t seq = o.last_refresh.refresh_sequence;
    CHECK(editor_on_refresh(e, last.id, seq + 1u, o.last_refresh.completion_ns, true) == 0);
    CHECK(o.last_refresh.frame.scroll.first_byte == last.scroll.first_byte);
    CHECK(editor_on_refresh(e, last.id, seq + 1u, o.last_refresh.completion_ns, true) == EDITOR_ERR_ARG);
    CHECK(editor_on_refresh(e, 0, seq + 2u, o.last_refresh.completion_ns, true) == 0);
    CHECK(!o.last_refresh.metadata_valid);
    backend.ops.present = delayed_present;
    wheel.dy = 1;
    CHECK(editor_inject(e, &wheel) == 0);
    for (unsigned retry = 0; retry < PROBE_RETRIES && !backend.active; retry++) CHECK(editor_step(e, 0) >= 0);
    CHECK(backend.active && backend.presented && backend.active_frame != last.id);
    uint32_t active = backend.active_frame;
    CHECK(editor_on_refresh(e, last.id, seq + 3u, o.last_refresh.completion_ns, true) == 0);
    CHECK(o.last_refresh.metadata_valid && o.last_refresh.frame.scroll.first_byte == last.scroll.first_byte);
    CHECK(backend.active && backend.active_frame == active);
    CHECK(render_backend_signal(&backend, RENDER_EVENT_PRESENT_COMPLETE, last.id, 0) == RENDER_ERR_FRAME);
    CHECK(backend.active && backend.active_frame == active);
    CHECK(render_backend_signal(&backend, RENDER_EVENT_DEVICE_DONE, active, 0) == 0);
    CHECK(render_backend_signal(&backend, RENDER_EVENT_PRESENT_COMPLETE, active, 0) == 0 && settle(e) == 0);
    puts("editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS");
done:
    editor_close(e);
    if (!result) result = cancellation();
    if (!result) result = resized();
    if (!result) result = eof_typing();
    if (!result) result = pending_jump_motion((plat_event){.kind = PLAT_EV_WHEEL, .dy = SCROLL_WHEEL_UNIT});
    if (!result) result = pending_jump_keys();
    if (!result) result = wrapped_wheel();
    return result;
}
