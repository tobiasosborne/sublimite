#ifndef EDIT_HL_QUERY_H
#define EDIT_HL_QUERY_H
#include "base/base.h"
#include "piece/piece.h"
#include "work/work.h"
#include <tree_sitter/api.h>
#include <limits.h>

/* (P) Sequential identities for the named theme keys; zero means foreground.
 * Scope ids are stable within this protocol, not Sublime scope selectors. */
typedef enum hl_scope {
    HL_SCOPE_DEFAULT = 0, HL_SCOPE_COMMENT, HL_SCOPE_KEYWORD, HL_SCOPE_STRING,
    HL_SCOPE_NUMBER, HL_SCOPE_CONSTANT, HL_SCOPE_FUNCTION, HL_SCOPE_TYPE,
    HL_SCOPE_VARIABLE, HL_SCOPE_OPERATOR, HL_SCOPE_PUNCTUATION,
    HL_SCOPE_ATTRIBUTE, HL_SCOPE_COUNT
} hl_scope;
const char *hl_scope_key(hl_scope scope);
enum { HL_QUERY_OK, HL_QUERY_ARG, HL_QUERY_COMPILE, HL_QUERY_MEMORY,
       HL_QUERY_PREDICATE, HL_QUERY_READ, HL_QUERY_LIMIT, HL_QUERY_STOP };
typedef struct hl_query_error { uint32_t offset; TSQueryError type; } hl_query_error;
typedef struct hl_query {
    TSQuery *query;
    TSQueryCursor *cursor;
    edit_arena arena;
    hl_scope *scopes;
    struct hl_predicate *predicates;
} hl_query;
/* Worker-only initialization: compiles embedded vendored highlights, or a
 * caller-supplied query. TSQuery/cursor allocation stays off the UI thread.
 * Exact vendored text predicates, any-of/ancestor and highlights-only local
 * metadata are supported; unsupported predicates fail closed. Initialize inactive
 * storage;
 * failure leaves it destroyable. Lifetime/serialization match hl_worker. */
int hl_query_init(hl_query *query, const TSLanguage *language, hl_query_error *error);
int hl_query_init_source(hl_query *query, const TSLanguage *language,
                        const char *source, size_t length, hl_query_error *error);
void hl_query_destroy(hl_query *query);
/* (P) Sibling mailbox kind: ASCII 'H','S', packed by CHAR_BIT bits per byte. */
#define HL_SPAN_KIND (((uint32_t)'H' << CHAR_BIT) | (uint32_t)'S')
typedef struct hl_span {
    uint64_t first_byte, end_byte;
    hl_scope scope;
    uint32_t priority; /* upstream pattern index; later pattern wins overlap */
} hl_span;
int hl_span_encode(work_msg *message, uint32_t generation, const hl_span *span);
int hl_span_decode(const work_msg *message, hl_span *span);
/* Worker-only: completed tree and matching immutable snapshot are borrowed.
 * Query [first_row,end_row), clip ranges to those rows' bytes. Emit overlapping
 * capture spans in upstream match order; later pattern index wins; equal
 * priority uses last emitted capture. Unknown captures explicitly emit default
 * foreground. Consumer must reject stale transport generations, stage a whole
 * run, and install only after success/completion; empty runs clear old syntax.
 * After success the worker publishes an existing hl_result for the covered
 * bytes/phase with the same generation as the spans; that closes the batch.
 * Callback can encode/publish using work_publish; on backpressure STOP, discard
 * staged output and rerun (no partial completion is reported). Callback error
 * is propagated; cursor match-limit overflow fails closed. */
typedef int (*hl_span_fn)(void *payload, const hl_span *span);
int hl_query_run(hl_query *query, const TSTree *tree, const piece_snapshot *snapshot,
                 uint32_t first_row, uint32_t end_row, hl_span_fn emit, void *payload);
#endif
