/* Fuzz mailbox decoding and C/Rust queries over input-derived random edits. */
#include "hl/hl.h"
#include "hl/lang.h"
#include "hl/query.h"
#include "base/base.h"
#include <string.h>


static TSPoint point(const uint8_t *text, size_t length)
{
    TSPoint p = {0};
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') { ++p.row; p.column = 0; } else ++p.column;
    }
    return p;
}
typedef struct fuzz_scopes { hl_scope *scopes; uint32_t *priorities; size_t length; } fuzz_scopes;
static int scope_capture(void *payload, const hl_span *span)
{
    fuzz_scopes *o = payload;
    EDIT_ASSERT(span->first_byte < span->end_byte && span->end_byte <= o->length);
    work_msg message; hl_span decoded;
    EDIT_ASSERT(hl_span_encode(&message, (uint32_t)o->length, span) == HL_QUERY_OK);
    EDIT_ASSERT(hl_span_decode(&message, &decoded) == HL_QUERY_OK);
    EDIT_ASSERT(decoded.scope == span->scope && decoded.priority == span->priority);
    for (uint64_t i = span->first_byte; i < span->end_byte; ++i) {
        if (span->priority >= o->priorities[i]) {
            o->scopes[i] = span->scope; o->priorities[i] = span->priority;
        }
    }
    return HL_QUERY_OK;
}
static void query_edit(const uint8_t *data, size_t size)
{
    /* (P) Controls are language, replacement start, replacement end. Remaining
     * bytes split into before/replacement halves; no guessed fuzz work limit. */
    enum { LANGUAGE, START, END, CONTROL_BYTES };
    if (size < CONTROL_BYTES || size > UINT32_MAX) return;
    const TSLanguage *language = data[LANGUAGE] & 1u ? hl_language_rust() : hl_language_c();
    const uint8_t *before = data + CONTROL_BYTES;
    size_t initial = (size - CONTROL_BYTES) / 2u; /* (P) the two source halves */
    const uint8_t *replacement = before + initial;
    size_t added = size - CONTROL_BYTES - initial;
    size_t start = data[START] % (initial + 1u), end = data[END] % (initial + 1u);
    if (start > end) { size_t swap = start; start = end; end = swap; }
    size_t final = initial - (end - start) + added;
    TSParser *parser = ts_parser_new(); EDIT_ASSERT(parser);
    EDIT_ASSERT(ts_parser_set_language(parser, language));
    TSTree *old = ts_parser_parse_string(parser, NULL, (const char *)before, (uint32_t)initial);
    EDIT_ASSERT(old);
    /* (P) Source plus two byte-scope oracles and alignment padding per array.
     * At least one allocation byte is needed even for an empty fixture. */
    size_t storage = (final + 1u) * (sizeof(uint8_t) + 2u * (sizeof(hl_scope) + sizeof(uint32_t)))
        + 2u * (_Alignof(hl_scope) + _Alignof(uint32_t));
    edit_arena arena; EDIT_ASSERT(edit_arena_init(&arena, storage) == 0);
    uint8_t *after = edit_arena_alloc(&arena, final + 1u, _Alignof(uint8_t)); EDIT_ASSERT(after);
    memcpy(after, before, start); memcpy(after + start, replacement, added);
    memcpy(after + start + added, before + end, initial - end);
    TSInputEdit edit = {(uint32_t)start, (uint32_t)end, (uint32_t)(start + added),
                       point(before, start), point(before, end), point(after, start + added)};
    ts_tree_edit(old, &edit);
    TSTree *incremental = ts_parser_parse_string(parser, old, (const char *)after, (uint32_t)final);
    TSTree *fresh = ts_parser_parse_string(parser, NULL, (const char *)after, (uint32_t)final);
    EDIT_ASSERT(incremental && fresh);
    piece_allocator allocator = piece_default_allocator();
    piece_tree *source = piece_create(&allocator); EDIT_ASSERT(source);
    EDIT_ASSERT(piece_init_copy(source, after, final) == PIECE_OK);
    piece_snapshot *snapshot = piece_snapshot_take(source); EDIT_ASSERT(snapshot);
    hl_query query; EDIT_ASSERT(hl_query_init(&query, language, NULL) == HL_QUERY_OK);
    fuzz_scopes a = {edit_arena_alloc(&arena, final * sizeof(hl_scope), _Alignof(hl_scope)),
                    edit_arena_alloc(&arena, final * sizeof(uint32_t), _Alignof(uint32_t)), final};
    fuzz_scopes b = {edit_arena_alloc(&arena, final * sizeof(hl_scope), _Alignof(hl_scope)),
                    edit_arena_alloc(&arena, final * sizeof(uint32_t), _Alignof(uint32_t)), final};
    EDIT_ASSERT(a.scopes && a.priorities && b.scopes && b.priorities);
    memset(a.scopes, 0, final * sizeof(hl_scope)); memset(a.priorities, 0, final * sizeof(uint32_t));
    memset(b.scopes, 0, final * sizeof(hl_scope)); memset(b.priorities, 0, final * sizeof(uint32_t));
    EDIT_ASSERT(hl_query_run(&query, incremental, snapshot, 0, UINT32_MAX, scope_capture, &a) == HL_QUERY_OK);
    EDIT_ASSERT(hl_query_run(&query, fresh, snapshot, 0, UINT32_MAX, scope_capture, &b) == HL_QUERY_OK);
    EDIT_ASSERT(!memcmp(a.scopes, b.scopes, final * sizeof(hl_scope)));
    hl_query_destroy(&query); piece_snapshot_release(snapshot); piece_destroy(source);
    ts_tree_delete(old); ts_tree_delete(incremental); ts_tree_delete(fresh); ts_parser_delete(parser);
    edit_arena_free(&arena);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    query_edit(data, size);
    work_msg message = {0};
    if (size > sizeof message) size = sizeof message;
    memcpy(&message, data, size);
    hl_result result;
    if (hl_result_decode(&message, &result)) {
        work_msg encoded;
        hl_result roundtrip;
        EDIT_ASSERT(hl_result_encode(&encoded, message.generation, &result));
        EDIT_ASSERT(hl_result_decode(&encoded, &roundtrip));
        EDIT_ASSERT(roundtrip.first_byte == result.first_byte);
        EDIT_ASSERT(roundtrip.end_byte == result.end_byte);
        EDIT_ASSERT(roundtrip.phase == result.phase);
        EDIT_ASSERT(hl_viewport_ready(&encoded, message.generation,
                    result.first_byte, result.end_byte) == (result.phase == HL_VIEWPORT_READY));
    }
    return 0;
}
