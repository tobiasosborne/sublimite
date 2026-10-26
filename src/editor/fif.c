/* edit-n95: find in files UI. The find panel gains a Where field; Return runs
 * the src/fif engine (edit-34l) on WORK_BULK; drained messages only record
 * counts; the UI service assembles Sublime-format text into the derived,
 * read-only "Find Results" tab within WORK_DRAIN_BUDGET_NS per turn; Enter,
 * double-click and F4/Shift+F4 open hits through editor_open_request.
 * Decisions and conventions: docs/decisions/edit-n95.md. */
#include "editor/private.h"
#include "trace/trace.h"
#include "../../vendor/xkbcommon/xkbcommon-keysyms.h"
#include <string.h>
#include <unistd.h>

#define FIF_NONE UINT32_MAX
/* (P) Sublime's results format right-aligns line numbers in five columns. */
#define FIF_NUMBER_WIDTH 5u
/* (P) Decimal digits of UINT64_MAX. */
#define FIF_DIGITS 20u
static const char ellipsis[] = " ...";
/* edit-5vz: the row (and, without its LF, the fallback notice) that ends
 * results the results buffer refused to store. */
static const char full_row[] = "Search truncated (results buffer full)\n";
static const char full_notice[] = "Search truncated (results buffer full)";

int editor_fif_init(editor *e)
{
    editor_fif *f = &e->fif;
    /* (E) One record per engine default hit (fif_limits zero selects
     * FIF_DEFAULT_HITS): four uint64 arrays and one uint32 array, plus one
     * max_align_t of padding per array (P). */
    const size_t n = FIF_DEFAULT_HITS, arrays = 5u;
    size_t bytes = n * (4u * sizeof(uint64_t) + sizeof(uint32_t)) + arrays * _Alignof(max_align_t);
    if (edit_arena_init(&f->arena, bytes)) return EDITOR_ERR_MEMORY;
    f->line_start = edit_arena_alloc(&f->arena, n * sizeof(uint64_t), _Alignof(max_align_t));
    f->line_end = edit_arena_alloc(&f->arena, n * sizeof(uint64_t), _Alignof(max_align_t));
    f->mark = edit_arena_alloc(&f->arena, n * sizeof(uint64_t), _Alignof(max_align_t));
    f->header = edit_arena_alloc(&f->arena, n * sizeof(uint64_t), _Alignof(max_align_t));
    f->mark_length = edit_arena_alloc(&f->arena, n * sizeof(uint32_t), _Alignof(max_align_t));
    if (!f->line_start || !f->line_end || !f->mark || !f->header || !f->mark_length) return EDITOR_ERR_MEMORY;
    f->last_file = f->visited = FIF_NONE;
    return 0;
}
static void slot_finish(editor *e, editor_fif_slot *slot)
{
    if (!slot->live) return;
    work_cancel(&e->pool, slot->search);
    /* Keep draining until both physical leases finished and their mail is
     * gone (edit-34l duty); the poll interval reuses findpanel's close loop. */
    while (fif_job_dispose(&slot->job) == FIF_ERR_BUSY) {
        (void)work_mailbox_drain(&e->pool, editor_route_work, e);
        (void)os_wake_wait(work_pool_wake(&e->pool), INT64_C(1000000));
    }
    *slot = (editor_fif_slot){0};
}
void editor_fif_close(editor *e)
{
    slot_finish(e, &e->fif.current);
    slot_finish(e, &e->fif.retiring);
    edit_arena_free(&e->fif.arena);
}
static uint32_t available(const editor_fif *f)
{
    return f->done_received ? (uint32_t)f->done.hits : f->received;
}
bool editor_fif_receive(editor *e, const work_msg *msg)
{
    if (msg->kind != FIF_MSG_HIT && msg->kind != FIF_MSG_FILE && msg->kind != FIF_MSG_DONE) return false;
    editor_fif *f = &e->fif;
    /* Obsolete generations (a retiring job, including its companion DONE)
     * are consumed and ignored. */
    if (!f->current.live || msg->generation != f->current.generation) return true;
    if (msg->kind == FIF_MSG_HIT) {
        fif_hit hit; memcpy(&hit, msg->data, sizeof hit);
        if (hit.index >= f->received) f->received = hit.index + 1u;
    } else if (msg->kind == FIF_MSG_DONE) {
        memcpy(&f->done, msg->data, sizeof f->done);
        f->done_received = true; f->running = false;
    }
    return true;
}
bool editor_fif_results_active(const editor *e)
{
    const tabs_tab *tab = tabs_count(&e->tabs) ? tabs_active(&e->tabs) : NULL;
    return e->fif.results_id && tab && tab->id == e->fif.results_id &&
        e->buffer && e->buffer->derived && e->buffer->id == e->fif.results_id;
}
static bool assembling(const editor *e)
{
    const editor_fif *f = &e->fif;
    return f->current.live && !f->full && editor_fif_results_active(e) &&
        (f->assembled < available(f) || (f->done_received && !f->summary_written));
}
bool editor_fif_runnable(const editor *e)
{
    return assembling(e) && e->action == EDITOR_ACTION_NONE && !view_busy(&e->v) && !e->find_replace_active;
}
bool editor_fif_pending(const editor *e)
{
    const editor_fif *f = &e->fif;
    return (f->current.live && !f->done_received) || assembling(e) || f->retiring.live;
}
static size_t put(uint8_t *out, size_t at, const void *bytes, size_t n)
{
    if (n) memcpy(out + at, bytes, n);
    return at + n;
}
static size_t put_text(uint8_t *out, size_t at, const char *text) { return put(out, at, text, strlen(text)); }
static size_t put_number(uint8_t *out, size_t at, uint64_t value, size_t width)
{
    char digits[FIF_DIGITS]; size_t count = 0;
    do { digits[count++] = (char)('0' + value % 10u); value /= 10u; } while (value);
    for (size_t pad = count; pad < width; pad++) out[at++] = ' ';
    while (count) out[at++] = (uint8_t)digits[--count];
    return at;
}
/* edit-5vz: the status-row notice (edit-btj), shown until the next mutation. */
static void notice(editor *e, const char *text)
{
    e->recovery_notice = text; e->recovery_notice_mutations = e->stats.mutations;
    (void)editor_begin_frame(e);
}
/* edit-5vz: an append the piece allocator or the line index refuses leaves
 * the tree unchanged (piece.h, editor_results_append): it sets *full and is
 * no error. An rc with the tree changed is the shared change bookkeeping's
 * (layout, scroll, frame), which every edit propagates. */
static int flush(editor *e, const uint8_t *bytes, size_t length, bool *full)
{
    uint64_t before = piece_len(e->tree);
    int rc = editor_results_append(e, bytes, length);
    if (!rc || piece_len(e->tree) != before) return rc;
    *full = true; return 0;
}
/* edit-5vz: stop assembly and the search; append the truncation row, or
 * show it as a notice when the buffer refuses even the row. */
static int truncate_full(editor *e)
{
    editor_fif *f = &e->fif;
    f->full = f->summary_written = true;
    (void)editor_fif_cancel(e);
    uint8_t *out = e->indent_bytes;
    size_t used = 0;
    if (f->assembled) out[used++] = '\n';
    used = put_text(out, used, full_row);
    bool refused = false;
    int rc = flush(e, out, used, &refused);
    if (refused) notice(e, full_notice);
    return rc;
}
/* Owner appends: line maps are recorded in results-buffer bytes, monotonic.
 * edit-5vz: maps of hits staged after the last stored append (from batch)
 * are dropped with their bytes when an append is refused. */
static int assemble(editor *e, uint64_t deadline, bool *full)
{
    editor_fif *f = &e->fif;
    uint8_t *out = e->indent_bytes;
    const size_t cap = EDITOR_STAGE_BYTES;
    /* (P) Largest single-hit output: separator LF, a header (path below
     * PATH_MAX, ':' and LF), the number field, ": ", the context cap, the
     * truncation mark and LF. */
    const size_t worst = 1u + FIF_PATH_BYTES + 2u + FIF_DIGITS + 2u + FIF_DEFAULT_CONTEXT + sizeof ellipsis - 1u + 1u;
    uint64_t base = piece_len(e->tree);
    size_t used = 0;
    uint32_t limit = available(f), batch = f->assembled;
    int rc = 0;
    while (f->assembled < limit) {
        fif_result_view hit;
        if (fif_results(&f->current.job, f->assembled, &hit) != FIF_OK) break;
        if (used + worst > cap) {
            rc = flush(e, out, used, full); if (rc) return rc;
            if (*full) { f->assembled = batch; return 0; }
            base = piece_len(e->tree); used = 0; batch = f->assembled;
        }
        uint32_t k = f->assembled;
        bool same_line = f->last_file == hit.hit.file_index && f->last_line == hit.hit.line;
        if (same_line && k) {
            f->line_start[k] = f->line_start[k - 1u]; f->line_end[k] = f->line_end[k - 1u];
            f->header[k] = UINT64_MAX;
        } else {
            f->header[k] = UINT64_MAX;
            if (hit.hit.file_index != f->last_file) {
                if (f->last_file != FIF_NONE) out[used++] = '\n';
                f->header[k] = base + used;
                size_t length = strnlen(hit.path, FIF_PATH_BYTES - 1u);
                used = put(out, used, hit.path, length);
                out[used++] = ':'; out[used++] = '\n';
                f->last_file = hit.hit.file_index; f->files++;
            }
            f->line_start[k] = base + used;
            used = put_number(out, used, hit.hit.line, FIF_NUMBER_WIDTH);
            out[used++] = ':'; out[used++] = ' ';
            used = put(out, used, hit.context, hit.context_length);
            if (hit.context_truncated) used = put_text(out, used, ellipsis);
            f->line_end[k] = base + used;
            out[used++] = '\n';
            f->last_line = hit.hit.line;
        }
        /* Context bytes start after the number field and ": " (P). */
        uint64_t context = f->line_start[k] + FIF_NUMBER_WIDTH + 2u;
        uint64_t digits = 1u;
        for (uint64_t v = hit.hit.line; v >= 10u; v /= 10u) digits++;
        if (digits > FIF_NUMBER_WIDTH) context += digits - FIF_NUMBER_WIDTH;
        uint64_t column = hit.hit.column - 1u;
        if (column < hit.context_length) {
            uint64_t room = hit.context_length - column;
            f->mark[k] = context + column;
            f->mark_length[k] = (uint32_t)(hit.hit.byte_length < room ? hit.hit.byte_length : room);
        } else { f->mark[k] = context + hit.context_length; f->mark_length[k] = 0; }
        f->assembled++;
        if (trace_now_ns() >= deadline) break;
    }
    if (used) { rc = flush(e, out, used, full); if (*full) f->assembled = batch; }
    return rc;
}
static int summary(editor *e, bool *full)
{
    editor_fif *f = &e->fif;
    uint8_t *out = e->indent_bytes;
    size_t used = 0;
    if (f->assembled) out[used++] = '\n';
    used = put_number(out, used, f->assembled, 0);
    used = put_text(out, used, f->assembled == 1u ? " match" : " matches");
    if (f->files == 1u) used = put_text(out, used, " in 1 file\n");
    else {
        used = put_text(out, used, " across ");
        used = put_number(out, used, f->files, 0);
        used = put_text(out, used, " files\n");
    }
    const fif_done *d = &f->done;
    if (d->code == FIF_ERR_IO) used = put_text(out, used, "Search failed: the Where folder cannot be read\n");
    else if (d->code == FIF_ERR_SEARCH) used = put_text(out, used, "Search failed: invalid pattern\n");
    else if (d->code != FIF_OK && d->code != FIF_CANCELLED) used = put_text(out, used, "Search failed\n");
    if (d->truncated) used = put_text(out, used, "Search truncated: result limits reached\n");
    if (d->cancelled) used = put_text(out, used, "Search cancelled\n");
    if (d->errors && d->code != FIF_ERR_IO) {
        used = put_number(out, used, d->errors, 0);
        used = put_text(out, used, d->errors == 1u ? " unreadable entry\n" : " unreadable entries\n");
    }
    return flush(e, out, used, full);
}
/* Highlight the visible hits' match ranges with the find-match style. */
static void fill_ranges(editor *e)
{
    editor_fif *f = &e->fif;
    if (!editor_fif_results_active(e) || e->find_replace_active || layout_busy(&e->lay) || e->full_pending) return;
    uint64_t start = e->v.state.wrap ? e->v.state.visual_byte : e->v.state.first_byte;
    uint64_t end = e->lay.pos; if (end < start) end = start;
    size_t lo = 0, hi = f->assembled;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (f->mark[mid] + f->mark_length[mid] <= start) lo = mid + 1u; else hi = mid;
    }
    size_t count = 0;
    for (size_t k = lo; k < f->assembled && f->mark[k] < end && count < LAYOUT_CLUSTER_CACHE; k++) {
        if (!f->mark_length[k]) continue;
        e->find_ranges[count++] = (findui_range){f->mark[k], f->mark[k] + f->mark_length[k]};
    }
    e->find_range_count = count; f->ranges_owned = true;
}
int editor_fif_service(editor *e)
{
    editor_fif *f = &e->fif;
    if (f->retiring.live && fif_job_finished(&f->retiring.job) && fif_job_dispose(&f->retiring.job) == FIF_OK)
        f->retiring = (editor_fif_slot){0};
    if (!editor_fif_results_active(e)) {
        /* Results marks are results-buffer offsets: never paint them elsewhere. */
        if (f->ranges_owned) { e->find_range_count = 0; f->ranges_owned = false; }
        return 0;
    }
    if (!f->current.live) return 0;
    if (editor_fif_runnable(e)) {
        uint64_t deadline = trace_now_ns() + WORK_DRAIN_BUDGET_NS;
        bool full = false;
        int rc = assemble(e, deadline, &full); if (rc) return rc;
        if (!full && f->done_received && !f->summary_written && f->assembled == available(f) && trace_now_ns() < deadline) {
            f->summary_written = true;
            rc = summary(e, &full); if (rc) return rc;
        }
        if (full) { rc = truncate_full(e); if (rc) return rc; }
    }
    fill_ranges(e);
    return 0;
}
bool editor_fif_cancel(editor *e)
{
    editor_fif *f = &e->fif;
    if (!f->current.live || !f->running) return false;
    work_cancel(&e->pool, f->current.search);
    f->running = false; /* DONE (cancelled) still arrives through the companion */
    return true;
}
static void default_where(editor *e)
{
    editor_fif *f = &e->fif;
    if (f->where_length) return;
    const char *path = e->buffer ? e->buffer->path : "";
    const char *slash = *path ? strrchr(path, '/') : NULL;
    if (slash) {
        size_t n = slash == path ? 1u : (size_t)(slash - path);
        if (n < sizeof f->where) { memcpy(f->where, path, n); f->where_length = n; }
    } else if (getcwd(f->where, sizeof f->where)) f->where_length = strlen(f->where);
    f->where_cursor = f->where_length;
}
void editor_fif_show(editor *e)
{
    editor_fif *f = &e->fif;
    (void)editor_fif_cancel(e);
    default_where(e);
    f->mode = true; f->where_focus = false; f->where_all = false;
}
int editor_fif_start(editor *e)
{
    editor_fif *f = &e->fif;
    findui_state state = findui_get_state(&e->find);
    if (!state.query_length) { e->stats.rejected_commands++; return 0; }
    /* Where: the first comma-separated token is the folder; the rest is the
     * engine's include/-exclude filter (fif.h grammar). */
    size_t at = 0, end, stop;
    while (at < f->where_length && (f->where[at] == ' ' || f->where[at] == '\t')) at++;
    for (end = at; end < f->where_length && f->where[end] != ','; end++) {}
    for (stop = end; stop > at && (f->where[stop - 1u] == ' ' || f->where[stop - 1u] == '\t'); stop--) {}
    char root[PATH_MAX], filter[PATH_MAX];
    const char *include = NULL;
    if (stop == at) { e->stats.rejected_commands++; return 0; }
    memcpy(root, f->where + at, stop - at); root[stop - at] = 0;
    if (end < f->where_length) {
        size_t n = f->where_length - end - 1u;
        memcpy(filter, f->where + end + 1u, n); filter[n] = 0;
        for (size_t i = 0; i < n && !include; i++) if (filter[i] != ' ' && filter[i] != '\t') include = filter;
    }
    if (f->current.live && f->retiring.live) {
        if (!fif_job_finished(&f->retiring.job) || fif_job_dispose(&f->retiring.job) != FIF_OK) {
            e->stats.rejected_commands++; return 0;
        }
        f->retiring = (editor_fif_slot){0};
    }
    fif_request request = {.root = root, .query = state.query, .query_length = state.query_length,
        .options = {state.options.regex, state.options.match_case, state.options.whole_word},
        .include = include};
    fif_job job = {0};
    if (fif_job_init(&job, &request) != FIF_OK) { e->stats.rejected_commands++; return 0; }
    /* The in-buffer finder stops tracking: it would otherwise re-search the
     * results buffer on the single bulk lane after every append. Ctrl+F or
     * F3 reopens it with the same query. The panel closes before the results
     * tab opens (the text geometry settles for the old buffer first). */
    (void)findui_show(&e->find, false, false);
    e->find_visible = false; f->mode = f->where_focus = f->where_all = false;
    int rc = editor_text_geometry(e);
    if (rc) { (void)fif_job_dispose(&job); return rc; }
    /* edit-5vz: the fresh results buffer exists before anything is submitted.
     * A refused open (activation busy, no tab or buffer slot) submits
     * nothing: the old results text and the old search stay. */
    rc = editor_results_open(e, &f->results_id);
    if (rc) {
        (void)fif_job_dispose(&job);
        if (rc != EDITOR_ERR_CAPACITY && rc != EDITOR_ERR_ARG) return rc;
        e->stats.rejected_commands++; notice(e, "find in files: no results tab available");
        return 0;
    }
    if (f->current.live) {
        if (f->running) work_cancel(&e->pool, f->current.search);
        f->retiring = f->current; f->current = (editor_fif_slot){0};
    }
    f->current.job = job;
    f->received = f->assembled = f->files = 0; f->last_line = 0;
    f->last_file = f->visited = FIF_NONE;
    f->done = (fif_done){0}; f->done_received = f->summary_written = f->full = false;
    if (!++f->generation) f->generation++;
    work_handle handle = fif_submit(&e->pool, &f->current.job, f->generation);
    if (!handle.epoch) {
        (void)fif_job_dispose(&f->current.job); f->current = (editor_fif_slot){0};
        e->stats.rejected_commands++; notice(e, "find in files could not start");
        return 0;
    }
    f->current.search = handle; f->current.generation = f->generation; f->current.live = true;
    f->running = true;
    return 0;
}
/* Where field editing; the query field keeps findpanel's editor. */
static void where_edit(editor_fif *f, size_t at, size_t erase, const uint8_t *bytes, size_t add)
{
    if (add > sizeof f->where - 1u - (f->where_length - erase)) return;
    memmove(f->where + at + add, f->where + at + erase, f->where_length - at - erase);
    if (add) memcpy(f->where + at, bytes, add);
    f->where_length = f->where_length - erase + add; f->where_cursor = at + add;
    f->where[f->where_length] = 0;
}
int editor_fif_paste(editor *e, const uint8_t *bytes, size_t length)
{
    editor_fif *f = &e->fif;
    size_t kept = 0;
    for (size_t i = 0; i < length; i++) if (bytes[i] != '\r' && bytes[i] != '\n') kept++;
    if (kept > sizeof f->where - 1u) { e->stats.rejected_commands++; return 0; }
    if (f->where_all) { f->where_length = f->where_cursor = 0; f->where_all = false; }
    for (size_t i = 0; i < length; i++)
        if (bytes[i] != '\r' && bytes[i] != '\n') where_edit(f, f->where_cursor, 0, bytes + i, 1);
    return editor_begin_frame(e);
}
bool editor_fif_key(editor *e, const plat_event *ev)
{
    editor_fif *f = &e->fif;
    const uint8_t *bytes = (const uint8_t *)f->where;
    size_t length = f->where_length, cursor = f->where_cursor;
    bool ctrl = (ev->mods & PLAT_MOD_CTRL) != 0;
    bool text = ev->utf8_len && !(ev->mods & (PLAT_MOD_CTRL | PLAT_MOD_SUPER)) && ev->utf8[0] >= ' ';
    if (ctrl && (ev->keysym == 'a' || ev->keysym == 'A')) { f->where_all = true; f->where_cursor = length; return true; }
    if (f->where_all && (ev->keysym == XKB_KEY_BackSpace || ev->keysym == XKB_KEY_Delete || text)) {
        f->where_length = 0; f->where_all = false;
        where_edit(f, 0, 0, ev->utf8, text ? ev->utf8_len : 0);
        return true;
    }
    if (ev->keysym == XKB_KEY_Left) f->where_cursor = f->where_all ? 0 : cursor ? utf8_grapheme_prev(bytes, cursor) : 0;
    else if (ev->keysym == XKB_KEY_Right)
        f->where_cursor = f->where_all ? length : cursor < length ? cursor + utf8_grapheme_next(bytes + cursor, length - cursor) : length;
    else if (ev->keysym == XKB_KEY_Home) f->where_cursor = 0;
    else if (ev->keysym == XKB_KEY_End) f->where_cursor = length;
    else if (ev->keysym == XKB_KEY_BackSpace && cursor) {
        size_t start = utf8_grapheme_prev(bytes, cursor); where_edit(f, start, cursor - start, NULL, 0);
    } else if (ev->keysym == XKB_KEY_Delete && cursor < length)
        where_edit(f, cursor, utf8_grapheme_next(bytes + cursor, length - cursor), NULL, 0);
    else if (text) where_edit(f, cursor, 0, ev->utf8, ev->utf8_len);
    f->where_all = false;
    return true;
}
static void paint_text(editor *e, uint32_t row, uint32_t column, const uint8_t *bytes, size_t length,
                       uint32_t fg, uint32_t bg, size_t cursor, bool selected)
{
    render_grid *g = &e->grid;
    for (size_t i = 0; i <= length && column < g->dims.cols; i++, column++) {
        uint8_t byte = i < length ? bytes[i] : ' ';
        render_cell cell = {0, RENDER_NO_SLOT, selected && i < length ? e->layout_cfg.sel_fg : fg,
                            selected && i < length ? e->layout_cfg.sel_bg : bg, 0, 0};
        if (byte < 32u || byte > 126u) byte = '?';
        uint32_t slot = e->find_ascii[byte - 32u];
        if (slot != RENDER_NO_SLOT && g->glyphs && slot < g->glyph_count) {
            cell.atlas_slot = slot; cell.glyph_index = g->glyphs[slot].glyph_index;
        }
        if (i == cursor) { cell.fg = config_theme_color(e->preferences.theme, "caret.foreground");
            cell.bg = config_theme_color(e->preferences.theme, "caret.background"); cell.attrs = RENDER_ATTR_CURSOR; }
        g->cells[(size_t)row * g->dims.cols + column] = cell;
    }
}
/* Over findui's rows: row 0 keeps Find + toggles; row 1 becomes Where;
 * row 2 the find-in-files key hints. Colours are findpanel's style. */
void editor_fif_paint(editor *e, uint32_t first_row, uint32_t rows)
{
    editor_fif *f = &e->fif;
    if (!f->mode || rows < 2u) return;
    const config_theme t = e->preferences.theme;
    const uint32_t fg = config_theme_color(t, "editor.foreground"), bg = config_theme_color(t, "chrome.background"),
        accent = config_theme_color(t, "chrome.foreground");
    render_grid *g = &e->grid;
    if (f->where_focus) {
        for (uint32_t c = 0; c < g->dims.cols; c++) {
            render_cell *cell = &g->cells[(size_t)first_row * g->dims.cols + c];
            if (cell->attrs & RENDER_ATTR_CURSOR) { cell->attrs = 0; cell->fg = fg; cell->bg = bg; }
        }
    }
    uint32_t row = first_row + 1u;
    for (uint32_t c = 0; c < g->dims.cols; c++)
        g->cells[(size_t)row * g->dims.cols + c] = (render_cell){0, RENDER_NO_SLOT, fg, bg, 0, 0};
    static const char label[] = "Where: ";
    paint_text(e, row, 0, (const uint8_t *)label, sizeof label - 1u, accent, bg, SIZE_MAX, false);
    uint32_t start = (uint32_t)(sizeof label - 1u);
    uint32_t width = g->dims.cols > start + 1u ? g->dims.cols - start - 1u : 0;
    size_t scroll = f->where_cursor > width ? f->where_cursor - width : 0;
    paint_text(e, row, start, (const uint8_t *)f->where + scroll, f->where_length - scroll, fg, bg,
               f->where_focus ? f->where_cursor - scroll : SIZE_MAX, f->where_focus && f->where_all);
    if (rows >= 3u) {
        row++;
        static const char hints[] = "Enter: find in files  Tab: field  Esc: close";
        for (uint32_t c = 0; c < g->dims.cols; c++)
            g->cells[(size_t)row * g->dims.cols + c] = (render_cell){0, RENDER_NO_SLOT, fg, bg, 0, 0};
        paint_text(e, row, 0, (const uint8_t *)hints, sizeof hints - 1u, fg, bg, SIZE_MAX, false);
    }
}
static int open_hit(editor *e, uint32_t k)
{
    editor_fif *f = &e->fif;
    fif_result_view hit;
    if (fif_results(&f->current.job, k, &hit) != FIF_OK || strlen(hit.path) >= IPC_PATH_CAP) {
        e->stats.rejected_commands++; return 0;
    }
    f->visited = k;
    ipc_request request = {.count = 1};
    request.paths[0] = (ipc_path){hit.path, hit.hit.line > UINT32_MAX ? UINT32_MAX : (uint32_t)hit.hit.line,
                                  hit.hit.column > UINT32_MAX ? UINT32_MAX : (uint32_t)hit.hit.column};
    e->open_byte_column = true;
    int rc = editor_open_request(e, &request, 0);
    e->open_byte_column = false;
    /* A vanished or unreadable file refuses the navigation, not the editor. */
    if (rc) e->stats.rejected_commands++;
    return 0;
}
static int open_header(editor *e, uint32_t k)
{
    fif_result_view hit;
    if (fif_results(&e->fif.current.job, k, &hit) != FIF_OK || strlen(hit.path) >= IPC_PATH_CAP) return 0;
    ipc_request request = {.count = 1};
    request.paths[0] = (ipc_path){hit.path, 1u, 1u};
    e->open_byte_column = true;
    int rc = editor_open_request(e, &request, 0);
    e->open_byte_column = false;
    if (rc) e->stats.rejected_commands++;
    return 0;
}
/* Hit index of the results line holding byte, FIF_NONE when none; *header
 * is set when byte is on the file header line that precedes hit index. */
static uint32_t locate(const editor_fif *f, uint64_t byte, bool *header)
{
    *header = false;
    size_t lo = 0, hi = f->assembled;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (f->line_start[mid] <= byte) lo = mid + 1u; else hi = mid;
    }
    if (lo && byte <= f->line_end[lo - 1u]) {
        size_t k = lo - 1u;
        while (k && f->line_start[k - 1u] == f->line_start[k] && f->mark[k] > byte) k--;
        return (uint32_t)k;
    }
    if (lo < f->assembled && f->header[lo] != UINT64_MAX && byte >= f->header[lo] && byte < f->line_start[lo]) {
        *header = true; return (uint32_t)lo;
    }
    return FIF_NONE;
}
int editor_fif_open_at_caret(editor *e)
{
    editor_fif *f = &e->fif;
    if (!f->current.live) return 0;
    bool header; uint32_t k = locate(f, e->v.state.selection.cursor, &header);
    if (k == FIF_NONE) return 0;
    return header ? open_header(e, k) : open_hit(e, k);
}
int editor_fif_step(editor *e, int direction)
{
    editor_fif *f = &e->fif;
    if (!f->current.live) return 0;
    uint32_t count = editor_fif_results_active(e) ? f->assembled : available(f);
    if (!count) return 0;
    uint32_t from = f->visited;
    if (editor_fif_results_active(e)) {
        uint64_t caret = e->v.state.selection.cursor;
        bool header; uint32_t k = locate(f, caret, &header);
        if (k != FIF_NONE && !header) from = k;
        else {
            /* Between lines: the next hit after the caret, or the previous. */
            size_t lo = 0, hi = count;
            while (lo < hi) { size_t mid = lo + (hi - lo) / 2u; if (f->line_start[mid] <= caret) lo = mid + 1u; else hi = mid; }
            if (direction > 0) return open_hit(e, lo < count ? (uint32_t)lo : 0);
            return open_hit(e, lo ? (uint32_t)(lo - 1u) : count - 1u);
        }
    }
    uint32_t next;
    if (from == FIF_NONE || from >= count) next = direction > 0 ? 0 : count - 1u;
    else if (direction > 0) next = from + 1u < count ? from + 1u : 0;
    else next = from ? from - 1u : count - 1u;
    return open_hit(e, next);
}
