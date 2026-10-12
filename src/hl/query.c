#include "hl/query.h"
#include "hl/lang.h"
#include <limits.h>
#include <string.h>
#include "query_c.inc"
#include "query_rust.inc"

/* (P) Zero denotes empty/default/success; one advances a byte/ordinal or
 * removes the string terminator. All sizes/counts come from the query ABI.
 * Predicate modes encode the exact expressions in the vendored queries.
 * Rust's all-caps expression ends in $' (end assertion then apostrophe), so
 * it can never match. Preserve that upstream behavior instead of fixing it. */
enum predicate_mode { PRED_NONE, PRED_UPPER, PRED_CAPS, PRED_NEVER };
struct hl_predicate { enum predicate_mode mode; uint32_t capture; };

const char *hl_scope_key(hl_scope scope)
{
    static const char *const keys[] = {
        "editor.foreground", "syntax.comment", "syntax.keyword", "syntax.string",
        "syntax.number", "syntax.constant", "syntax.function", "syntax.type",
        "syntax.variable", "syntax.operator", "syntax.punctuation", "syntax.attribute"
    };
    _Static_assert(sizeof keys / sizeof keys[0] == HL_SCOPE_COUNT, "scope keys complete");
    return scope >= HL_SCOPE_DEFAULT && scope < HL_SCOPE_COUNT ? keys[scope] : NULL;
}
static hl_scope capture_scope(const char *name, uint32_t length)
{
    static const struct { const char *name; hl_scope scope; } map[] = {
        {"comment", HL_SCOPE_COMMENT}, {"comment.documentation", HL_SCOPE_COMMENT},
        {"keyword", HL_SCOPE_KEYWORD}, {"string", HL_SCOPE_STRING}, {"escape", HL_SCOPE_STRING},
        {"number", HL_SCOPE_NUMBER}, {"constant", HL_SCOPE_CONSTANT},
        {"constant.builtin", HL_SCOPE_CONSTANT}, {"function", HL_SCOPE_FUNCTION},
        {"function.special", HL_SCOPE_FUNCTION}, {"function.method", HL_SCOPE_FUNCTION},
        {"function.macro", HL_SCOPE_FUNCTION}, {"type", HL_SCOPE_TYPE},
        {"type.builtin", HL_SCOPE_TYPE}, {"constructor", HL_SCOPE_TYPE},
        {"variable", HL_SCOPE_VARIABLE}, {"variable.parameter", HL_SCOPE_VARIABLE},
        {"variable.builtin", HL_SCOPE_VARIABLE}, {"property", HL_SCOPE_VARIABLE},
        {"label", HL_SCOPE_VARIABLE}, {"operator", HL_SCOPE_OPERATOR},
        {"delimiter", HL_SCOPE_PUNCTUATION}, {"punctuation.bracket", HL_SCOPE_PUNCTUATION},
        {"punctuation.delimiter", HL_SCOPE_PUNCTUATION}, {"attribute", HL_SCOPE_ATTRIBUTE}
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; ++i)
        if (strlen(map[i].name) == length && !memcmp(map[i].name, name, length)) return map[i].scope;
    return HL_SCOPE_DEFAULT;
}
static bool query_string_is(const TSQuery *query, uint32_t id, const char *text)
{
    uint32_t length;
    const char *value = ts_query_string_value_for_id(query, id, &length);
    return strlen(text) == length && !memcmp(value, text, length);
}
void hl_query_destroy(hl_query *query)
{
    if (!query) return;
    if (query->cursor) ts_query_cursor_delete(query->cursor);
    ts_query_delete(query->query);
    edit_arena_free(&query->arena);
    memset(query, 0, sizeof *query);
}
int hl_query_init_source(hl_query *query, const TSLanguage *language,
                        const char *source, size_t length, hl_query_error *error)
{
    if (!query) return HL_QUERY_ARG;
    memset(query, 0, sizeof *query);
    if (error) *error = (hl_query_error){0};
    if (!language || !source || length > UINT32_MAX) return HL_QUERY_ARG;
    hl_query_error local = {0};
    query->query = ts_query_new(language, source, (uint32_t)length, &local.offset, &local.type);
    if (error) *error = local;
    if (!query->query) return HL_QUERY_COMPILE;
    query->cursor = ts_query_cursor_new();
    if (!query->cursor) { hl_query_destroy(query); return HL_QUERY_MEMORY; }
    size_t captures = ts_query_capture_count(query->query);
    size_t patterns = ts_query_pattern_count(query->query);
    /* (P) Exact query metadata sizes, plus at most alignment-1 padding. */
    size_t alignment = _Alignof(struct hl_predicate);
    if (captures > (SIZE_MAX - alignment) / sizeof(hl_scope) ||
        patterns > (SIZE_MAX - alignment - captures * sizeof(hl_scope)) / sizeof(struct hl_predicate)) {
        hl_query_destroy(query); return HL_QUERY_MEMORY;
    }
    size_t bytes = captures * sizeof(hl_scope) + patterns * sizeof(struct hl_predicate) + alignment;
    if (edit_arena_init(&query->arena, bytes)) { hl_query_destroy(query); return HL_QUERY_MEMORY; }
    query->scopes = edit_arena_alloc(&query->arena, captures * sizeof(hl_scope), _Alignof(hl_scope));
    query->predicates = edit_arena_alloc(&query->arena, patterns * sizeof(struct hl_predicate), alignment);
    if (!query->scopes || !query->predicates) { hl_query_destroy(query); return HL_QUERY_MEMORY; }
    for (uint32_t i = 0; i < captures; ++i) {
        uint32_t name_length;
        const char *name = ts_query_capture_name_for_id(query->query, i, &name_length);
        query->scopes[i] = capture_scope(name, name_length);
    }
    for (uint32_t i = 0; i < patterns; ++i) {
        uint32_t count;
        const TSQueryPredicateStep *steps = ts_query_predicates_for_pattern(query->query, i, &count);
        struct hl_predicate *predicate = &query->predicates[i];
        *predicate = (struct hl_predicate){0};
        if (!count) continue;
        /* (P) match? + capture + regex + Done = four upstream steps. */
        if (count != 4u || steps[0].type != TSQueryPredicateStepTypeString ||
            !query_string_is(query->query, steps[0].value_id, "match?") ||
            steps[1].type != TSQueryPredicateStepTypeCapture ||
            steps[2].type != TSQueryPredicateStepTypeString ||
            steps[3].type != TSQueryPredicateStepTypeDone) goto unsupported;
        predicate->capture = steps[1].value_id;
        if (query_string_is(query->query, steps[2].value_id, "^[A-Z]")) predicate->mode = PRED_UPPER;
        else if (query_string_is(query->query, steps[2].value_id, "^[A-Z][A-Z\\d_]*$")) predicate->mode = PRED_CAPS;
        else if (query_string_is(query->query, steps[2].value_id, "^[A-Z][A-Z\\d_]+$'")) predicate->mode = PRED_NEVER;
        else goto unsupported;
    }
    return HL_QUERY_OK;
unsupported:
    hl_query_destroy(query); return HL_QUERY_PREDICATE;
}
int hl_query_init(hl_query *query, const TSLanguage *language, hl_query_error *error)
{
    if (language == hl_language_c())
        return hl_query_init_source(query, language, highlights_c, sizeof highlights_c - 1u, error);
    if (language == hl_language_rust())
        return hl_query_init_source(query, language, highlights_rust, sizeof highlights_rust - 1u, error);
    return hl_query_init_source(query, language, NULL, 0, error);
}
static int match_predicate(const struct hl_predicate *predicate, const TSQueryMatch *match,
                           const piece_snapshot *snapshot, bool *accepted)
{
    *accepted = predicate->mode == PRED_NONE;
    if (predicate->mode == PRED_NONE || predicate->mode == PRED_NEVER) return HL_QUERY_OK;
    for (uint16_t i = 0; i < match->capture_count; ++i) {
        if (match->captures[i].index != predicate->capture) continue;
        TSNode node = match->captures[i].node;
        uint64_t first = ts_node_start_byte(node), end = ts_node_end_byte(node);
        uint8_t byte;
        if (first == end) return HL_QUERY_OK;
        if (piece_snapshot_read(snapshot, first, &byte, sizeof byte) != PIECE_OK) return HL_QUERY_READ;
        if (byte < 'A' || byte > 'Z') return HL_QUERY_OK;
        if (predicate->mode == PRED_CAPS) {
            /* (P) Worker scratch is the same Linux page-sized chunk as hl input;
             * no identifier/source bound. Scan all bytes in successive chunks. */
            uint8_t buffer[4096];
            for (uint64_t at = first + 1u; at < end;) {
                size_t length = end - at < sizeof buffer ? (size_t)(end - at) : sizeof buffer;
                if (piece_snapshot_read(snapshot, at, buffer, length) != PIECE_OK) return HL_QUERY_READ;
                for (size_t j = 0; j < length; ++j)
                    if (!((buffer[j] >= 'A' && buffer[j] <= 'Z') ||
                          (buffer[j] >= '0' && buffer[j] <= '9') || buffer[j] == '_')) return HL_QUERY_OK;
                at += length;
            }
        }
        *accepted = true;
    }
    return HL_QUERY_OK;
}
int hl_query_run(hl_query *query, const TSTree *tree, const piece_snapshot *snapshot,
                 uint32_t first_row, uint32_t end_row, hl_span_fn emit, void *payload)
{
    if (!query || !query->query || !query->cursor || !tree || !snapshot || !emit || first_row > end_row)
        return HL_QUERY_ARG;
    if (first_row == end_row) return HL_QUERY_OK;
    uint64_t first = piece_snapshot_line_to_byte(snapshot, first_row);
    uint64_t end = piece_snapshot_line_to_byte(snapshot, end_row);
    if (first == end) return HL_QUERY_OK;
    if (piece_snapshot_len(snapshot) > UINT32_MAX) return HL_QUERY_ARG;
    ts_query_cursor_set_byte_range(query->cursor, (uint32_t)first, (uint32_t)end);
    ts_query_cursor_exec(query->cursor, query->query, ts_tree_root_node(tree));
    TSQueryMatch match;
    while (ts_query_cursor_next_match(query->cursor, &match)) {
        bool accepted;
        int rc = match_predicate(&query->predicates[match.pattern_index], &match, snapshot, &accepted);
        if (rc != HL_QUERY_OK) return rc;
        if (!accepted) continue;
        for (uint16_t i = 0; i < match.capture_count; ++i) {
            const TSQueryCapture *capture = &match.captures[i];
            hl_span span = {ts_node_start_byte(capture->node), ts_node_end_byte(capture->node),
                           query->scopes[capture->index], match.pattern_index};
            if (span.first_byte < first) span.first_byte = first;
            if (span.end_byte > end) span.end_byte = end;
            if (span.first_byte >= span.end_byte) continue;
            rc = emit(payload, &span);
            if (rc != HL_QUERY_OK) return rc;
        }
    }
    return ts_query_cursor_did_exceed_match_limit(query->cursor) ? HL_QUERY_LIMIT : HL_QUERY_OK;
}
/* (P) Wire offsets derive from preceding fixed-width fields, never padding. */
#define SPAN_END sizeof(uint64_t)
#define SPAN_SCOPE (SPAN_END + sizeof(uint64_t))
#define SPAN_PRIORITY (SPAN_SCOPE + sizeof(uint32_t))
_Static_assert(SPAN_PRIORITY + sizeof(uint32_t) <= WORK_MSG_DATA, "span fits mailbox");
static bool span_valid(const hl_span *span)
{
    return span && span->first_byte < span->end_byte &&
        span->scope >= HL_SCOPE_DEFAULT && span->scope < HL_SCOPE_COUNT;
}
int hl_span_encode(work_msg *message, uint32_t generation, const hl_span *span)
{
    if (!message || !span_valid(span)) return HL_QUERY_ARG;
    uint32_t scope = (uint32_t)span->scope;
    memset(message, 0, sizeof *message);
    message->kind = HL_SPAN_KIND; message->generation = generation;
    memcpy(message->data, &span->first_byte, sizeof span->first_byte);
    memcpy(message->data + SPAN_END, &span->end_byte, sizeof span->end_byte);
    memcpy(message->data + SPAN_SCOPE, &scope, sizeof scope);
    memcpy(message->data + SPAN_PRIORITY, &span->priority, sizeof span->priority);
    return HL_QUERY_OK;
}
int hl_span_decode(const work_msg *message, hl_span *span)
{
    if (!message || !span || message->kind != HL_SPAN_KIND) return HL_QUERY_ARG;
    hl_span decoded; uint32_t scope;
    memcpy(&decoded.first_byte, message->data, sizeof decoded.first_byte);
    memcpy(&decoded.end_byte, message->data + SPAN_END, sizeof decoded.end_byte);
    memcpy(&scope, message->data + SPAN_SCOPE, sizeof scope);
    if (scope >= HL_SCOPE_COUNT) return HL_QUERY_ARG;
    decoded.scope = (hl_scope)scope;
    memcpy(&decoded.priority, message->data + SPAN_PRIORITY, sizeof decoded.priority);
    if (!span_valid(&decoded)) return HL_QUERY_ARG;
    *span = decoded; return HL_QUERY_OK;
}
