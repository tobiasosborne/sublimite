#include "findui/findui.h"
#include <string.h>
#include <time.h>

#define MODEL_BYTES 256u
#define MODEL_QUERY 24u
#define MODEL_GROUPS 128u
#define MUST(x) EDIT_ASSERT(x)

typedef struct token { bool allowed[256]; uint8_t anchor; size_t minimum, maximum; } token;
typedef struct history { uint8_t before[MODEL_BYTES], after[MODEL_BYTES]; size_t bn, an; } history;
typedef struct model {
    uint8_t bytes[MODEL_BYTES], query[MODEL_QUERY], replacement[4];
    size_t length, qn, rn, history_count, history_cursor;
    findui_options options;
    findui_range matches[MODEL_BYTES + 1];
    size_t count, selected;
    uint64_t window_start, window_end;
    bool valid;
    history groups[MODEL_GROUPS];
} model;
static bool alpha(uint8_t byte)
{ return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z'); }
static bool word(uint8_t byte)
{ return alpha(byte) || (byte >= '0' && byte <= '9') || byte == '_'; }
static uint8_t fold(uint8_t byte)
{ return byte >= 'A' && byte <= 'Z' ? (uint8_t)(byte + ('a' - 'A')) : byte; }
static void case_class(bool *allowed)
{
    for (unsigned lower = 'a'; lower <= (unsigned)'z'; lower++) {
        unsigned upper = lower - (unsigned)('a' - 'A');
        if (allowed[lower] || allowed[upper]) allowed[lower] = allowed[upper] = true;
    }
}
static bool parse(const model *m, token *tokens, size_t *count)
{
    *count = 0;
    for (size_t at = 0; at < m->qn;) {
        token *t = &tokens[(*count)++]; memset(t, 0, sizeof *t);
        t->minimum = t->maximum = 1;
        uint8_t byte = m->query[at++];
        if (byte == '^' || byte == '$') t->anchor = byte;
        else if (byte == '.' ) {
            for (size_t i = 0; i < 256; i++) t->allowed[i] = i != '\n';
        } else if (byte == '[') {
            bool negate = at < m->qn && m->query[at] == '^', any = false;
            if (negate) at++;
            while (at < m->qn && m->query[at] != ']') {
                uint8_t low = m->query[at++];
                if (low == '\\') { if (at == m->qn) return false; low = m->query[at++]; }
                uint8_t high = low;
                if (at + 1 < m->qn && m->query[at] == '-' && m->query[at + 1] != ']') {
                    at++; high = m->query[at++];
                    if (high == '\\') { if (at == m->qn) return false; high = m->query[at++]; }
                    if (high < low) return false;
                }
                for (unsigned i = low; i <= (unsigned)high; i++) t->allowed[i] = true;
                any = true;
            }
            if (!any || at == m->qn) return false;
            at++;
            if (!m->options.match_case) case_class(t->allowed);
            if (negate) for (size_t i = 0; i < 256; i++) t->allowed[i] = !t->allowed[i];
        } else {
            if (byte == '*' || byte == '+' || byte == '?') return false;
            if (byte == '\\') { if (at == m->qn) return false; byte = m->query[at++]; }
            t->allowed[byte] = true;
            if (!m->options.match_case) case_class(t->allowed);
        }
        if (at < m->qn && (m->query[at] == '*' || m->query[at] == '+' || m->query[at] == '?')) {
            if (t->anchor) return false;
            uint8_t repeat = m->query[at++];
            t->minimum = repeat == '+' ? 1 : 0;
            t->maximum = repeat == '?' ? 1 : MODEL_BYTES;
        }
    }
    return true;
}
/* Independent endpoint-set matcher for concatenation, classes, quantifiers
 * and multiline anchors. Input edits/templates stay within this grammar.
 * Uses no find functions and does not share the production transformer. */
static bool anchored(const model *m, const token *tokens, size_t count, size_t start, size_t *end)
{
    bool current[MODEL_BYTES + 1] = {false}, next[MODEL_BYTES + 1];
    current[start] = true;
    for (size_t ti = 0; ti < count; ti++) {
        memset(next, 0, sizeof next); const token *t = &tokens[ti];
        for (size_t pos = start; pos <= m->length; pos++) if (current[pos]) {
            if (t->anchor) {
                if ((t->anchor == '^' && (!pos || m->bytes[pos - 1] == '\n')) ||
                    (t->anchor == '$' && (pos == m->length || m->bytes[pos] == '\n'))) next[pos] = true;
            } else {
                size_t consumed = 0;
                if (!t->minimum) next[pos] = true;
                while (consumed < t->maximum && pos + consumed < m->length && t->allowed[m->bytes[pos + consumed]]) {
                    consumed++; if (consumed >= t->minimum) next[pos + consumed] = true;
                }
            }
        }
        memcpy(current, next, sizeof current);
    }
    for (size_t pos = m->length + 1; pos-- > start;) if (current[pos]) { *end = pos; return true; }
    return false;
}
static void enumerate(model *m)
{
    m->count = 0; m->valid = true;
    if (!m->qn) return;
    token tokens[MODEL_QUERY]; size_t tn = 0;
    if (m->options.regex && !parse(m, tokens, &tn)) { m->valid = false; return; }
    for (size_t start = 0; start <= m->length;) {
        size_t end = start; bool hit = false;
        if (m->options.regex) hit = anchored(m, tokens, tn, start, &end);
        else if (m->qn <= m->length - start) {
            hit = true;
            for (size_t i = 0; i < m->qn; i++) {
                uint8_t a = m->bytes[start + i], b = m->query[i];
                if (!m->options.match_case) { a = fold(a); b = fold(b); }
                if (a != b) { hit = false; break; }
            }
            end = start + m->qn;
        }
        if (hit && m->options.whole_word && ((start && word(m->bytes[start - 1])) ||
                                            (end < m->length && word(m->bytes[end])))) hit = false;
        if (hit) {
            MUST(m->count < MODEL_BYTES + 1);
            m->matches[m->count++] = (findui_range){start, end};
            if (end > start) { start = end; continue; }
        }
        if (start == m->length) break;
        start++;
    }
}
static void *allocate(void *context, size_t size)
{ return edit_arena_alloc(context, size, 16); }
static void release(void *context, void *pointer, size_t size)
{ (void)context; (void)pointer; (void)size; }
static void route(const work_msg *message, void *context)
{ (void)findui_accept(context, message); }
static void pause_worker(void)
{ const struct timespec time = {0, 10000}; (void)nanosleep(&time, NULL); }
static void wait_result(findui_panel *panel, work_pool *pool)
{
    for (size_t i = 0; i < 100000; i++) {
        MUST(findui_service(panel) <= FINDUI_MORE);
        (void)work_mailbox_drain(pool, route, panel);
        if (!findui_get_state(panel).searching) return;
        pause_worker();
    }
    MUST(false); /* find worker timeout */
}
static void bind(findui_panel *panel, piece_tree *tree, uint64_t revision)
{
    piece_snapshot *snapshot = piece_snapshot_take(tree); MUST(snapshot);
    MUST(findui_set_source(panel, snapshot, revision) == FINDUI_OK); piece_snapshot_release(snapshot);
}
static void compare(findui_panel *panel, work_pool *pool, piece_tree *tree, model *m)
{
    wait_result(panel, pool); enumerate(m);
    uint8_t actual[MODEL_BYTES];
    MUST(piece_len(tree) == m->length && piece_read(tree, 0, actual, m->length) == 0 &&
         memcmp(actual, m->bytes, m->length) == 0);
    findui_state state = findui_get_state(panel);
    MUST(state.query_length == m->qn && memcmp(state.query, m->query, m->qn) == 0);
    MUST(state.complete == m->valid && state.match_count == m->count);
    if (!m->valid) { MUST(state.search_error != FIND_OK); return; }
    MUST(state.cached_matches == m->count && !state.visible_overflow);
    if (m->count) {
        MUST(state.match_index == m->selected);
        MUST(state.selected.start == m->matches[m->selected].start && state.selected.end == m->matches[m->selected].end);
    } else MUST(state.match_index == FINDUI_NO_INDEX);
    findui_range highlights[MODEL_BYTES + 1]; size_t count;
    MUST(findui_highlights(panel, m->window_start, m->window_end, highlights, MODEL_BYTES + 1, &count) == FINDUI_OK);
    size_t expected = 0;
    for (size_t i = 0; i < m->count; i++) {
        findui_range range = m->matches[i];
        bool overlaps = range.start == range.end ? range.start >= m->window_start && range.start <= m->window_end
                                                 : m->window_start < m->window_end && range.start < m->window_end && range.end > m->window_start;
        if (!overlaps) continue;
        if (range.start < m->window_start) range.start = m->window_start;
        if (range.end > m->window_end) range.end = m->window_end;
        MUST(expected < count && highlights[expected].start == range.start && highlights[expected].end == range.end);
        expected++;
    }
    MUST(count == expected);
}
static bool replace_model(model *m, bool all)
{
    uint8_t before[MODEL_BYTES]; size_t bn = m->length; memcpy(before, m->bytes, bn);
    bool changed = false;
    size_t count = all ? m->count : (m->count ? 1u : 0u);
    for (size_t left = count; left; left--) {
        findui_range range = m->matches[all ? left - 1 : m->selected];
        size_t at = (size_t)range.start, end = (size_t)range.end;
        MUST(m->length - (end - at) + m->rn <= MODEL_BYTES);
        memmove(m->bytes + at + m->rn, m->bytes + end, m->length - end);
        memcpy(m->bytes + at, m->replacement, m->rn); m->length = m->length - (end - at) + m->rn;
        changed = changed || at != end || m->rn != 0;
    }
    if (changed) {
        MUST(m->history_cursor < MODEL_GROUPS);
        history *group = &m->groups[m->history_cursor++];
        group->bn = bn; group->an = m->length;
        memcpy(group->before, before, bn); memcpy(group->after, m->bytes, m->length);
        m->history_count = m->history_cursor;
    }
    return changed;
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 384) size = 384;
    edit_arena arena; MUST(edit_arena_init(&arena, 16u * 1024u * 1024u) == 0);
    model *m = edit_arena_alloc(&arena, sizeof *m, _Alignof(model)); MUST(m); memset(m, 0, sizeof *m);
    const uint8_t alphabet[] = {'a', 'b', 'A', 'B', ' ', '_', '\n', 0};
    m->length = 32;
    for (size_t i = 0; i < m->length; i++) m->bytes[i] = alphabet[size ? data[i % size] % sizeof alphabet : i % sizeof alphabet];
    m->options.match_case = true; m->window_end = m->length;
    piece_allocator allocator = {&arena, allocate, release}; piece_tree *tree = piece_create(&allocator);
    MUST(tree && piece_init_copy(tree, m->bytes, m->length) == 0);
    undo_log undo; MUST(undo_init(&undo, tree, 65536) == 0);
    work_pool *pool = edit_arena_alloc(&arena, sizeof *pool, _Alignof(work_pool)); MUST(pool);
    MUST(work_pool_init(pool, 1, 0) == 0);
    findui_panel panel = {0}; findui_config config = {&arena, pool, 512, 512, NULL, NULL};
    MUST(findui_init(&panel, &config) == FINDUI_OK);
    uint64_t revision = 1; bind(&panel, tree, revision);
    MUST(findui_set_window(&panel, m->window_start, m->window_end) == FINDUI_OK);
    MUST(findui_show(&panel, true, true) == FINDUI_OK);
    MUST(findui_set_options(&panel, m->options) == FINDUI_OK);
    for (size_t i = 0; i + 2 < size; i += 3) {
        uint8_t op = data[i] % 14u, a = data[i + 1], b = data[i + 2];
        bool search_changed = false;
        if (op == 0 && m->qn < MODEL_QUERY) {
            size_t at = a % (m->qn + 1); uint8_t byte = alphabet[b % sizeof alphabet];
            MUST(findui_edit_query(&panel, at, 0, &byte, 1) == FINDUI_OK);
            memmove(m->query + at + 1, m->query + at, m->qn - at); m->query[at] = byte; m->qn++; search_changed = true;
        } else if (op == 1 && m->qn) {
            size_t at = a % m->qn; MUST(findui_edit_query(&panel, at, 1, NULL, 0) == FINDUI_OK);
            memmove(m->query + at, m->query + at + 1, m->qn - at - 1); m->qn--; search_changed = true;
        } else if (op == 2) MUST(findui_set_query_cursor(&panel, a % (m->qn + 1)) == FINDUI_OK);
        else if (op >= 3 && op <= 5) {
            if (op == 3) m->options.regex = !m->options.regex;
            if (op == 4) m->options.match_case = !m->options.match_case;
            if (op == 5) m->options.whole_word = !m->options.whole_word;
            MUST(findui_set_options(&panel, m->options) == FINDUI_OK); search_changed = true;
        } else if (op == 6) {
            const char *patterns[] = {"a", "ab", "a+", "a?", ".*", "[ab]+", "[^aB]*", "[A-b]", "^a", "b$", "\\+", "[a-z]+"};
            const char *pattern = patterns[a % (sizeof patterns / sizeof *patterns)];
            m->qn = strlen(pattern); memcpy(m->query, pattern, m->qn);
            MUST(findui_set_query(&panel, m->query, m->qn) == FINDUI_OK); search_changed = true;
        } else if (op == 7) {
            m->rn = a % 4u;
            for (size_t j = 0; j < m->rn; j++) m->replacement[j] = alphabet[(b + j) % sizeof alphabet];
            MUST(findui_set_replacement(&panel, m->replacement, m->rn, m->rn) == FINDUI_OK);
        } else if ((op == 8 || op == 9) && m->valid && m->count) {
            bool all = op == 9; size_t count = all ? m->count : 1;
            if (m->length + count * m->rn <= MODEL_BYTES) {
                undo_state before = {{0}}, after = {{0}};
                findui_code code = all ? findui_replace_all(&panel, &undo, revision, 65536, i, &before, &after)
                                      : findui_replace_one(&panel, &undo, revision, 65536, i, &before, &after);
                MUST(code == FINDUI_OK); size_t total = 0, replaced;
                do { code = findui_replace_step(&panel, 1u + b % 4u, 0, &replaced); total += replaced; }
                while (code == FINDUI_MORE);
                MUST(code == FINDUI_OK && total == count);
                if (replace_model(m, all)) { revision++; bind(&panel, tree, revision); search_changed = true; }
            }
        } else if (op == 10 || op == 11) {
            undo_change change; bool reverse = op == 10;
            MUST((reverse ? undo_undo(&undo, 1, &change) : undo_redo(&undo, 1, &change)) == 0);
            bool available = reverse ? m->history_cursor != 0 : m->history_cursor < m->history_count;
            MUST(change.groups == (available ? 1u : 0u));
            if (available) {
                history *group = &m->groups[reverse ? --m->history_cursor : m->history_cursor++];
                m->length = reverse ? group->bn : group->an;
                memcpy(m->bytes, reverse ? group->before : group->after, m->length);
                revision++; bind(&panel, tree, revision); search_changed = true;
            }
        } else if (op == 12 && m->valid) {
            findui_range selected; int direction = (a & 1u) ? 1 : -1;
            MUST(findui_next(&panel, direction, &selected) == FINDUI_OK);
            if (m->count) m->selected = direction == 1 ? (m->selected + 1) % m->count
                                                      : (m->selected ? m->selected - 1 : m->count - 1);
        } else if (op == 13) {
            uint64_t start = a % (m->length + 1), end = b % (m->length + 1);
            if (start > end) { uint64_t swap = start; start = end; end = swap; }
            m->window_start = start; m->window_end = end;
            MUST(findui_set_window(&panel, start, end) == FINDUI_OK);
        }
        if (search_changed) m->selected = 0;
        compare(&panel, pool, tree, m);
        MUST(undo_get_stats(&undo).undo_groups == m->history_cursor);
        MUST(undo_get_stats(&undo).redo_groups == m->history_count - m->history_cursor);
    }
    while (findui_dispose(&panel) == FINDUI_MORE) pause_worker();
    work_pool_shutdown(pool); (void)work_mailbox_drain_bounded(pool, route, &panel, SIZE_MAX, 0);
    undo_destroy(&undo); piece_destroy(tree); edit_arena_free(&arena);
    return 0;
}
