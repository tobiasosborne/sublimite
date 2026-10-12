#include "hl/query.h"
#include "hl/hl.h"
#include "hl/lang.h"
#include <limits.h>
#include <string.h>
#include "query_c.inc"
#include "query_rust.inc"
#include "query_python.inc"
#include "query_javascript.inc"
#include "query_json.inc"
#include "query_toml.inc"
#include "query_yaml.inc"
#include "query_julia.inc"
#include "query_typescript.inc"
#include "query_markdown.inc"
#include "query_markdown_inline.inc"
#include "query_latex.inc"
#include "query_bash.inc"

/* (P) Zero denotes empty/default/success; one advances a byte/ordinal or
 * removes the string terminator. All sizes/counts come from the query ABI.
 * Predicate modes encode the exact expressions in the vendored queries.
 * Rust's all-caps expression ends in $' (end assertion then apostrophe), so
 * it can never match. Preserve that upstream behavior instead of fixing it. */
/* (P) Predicate shapes derive from operator + capture + string arguments;
 * is-not-local has operator + property. Regex delimiters ^( and )$ each
 * occupy two bytes, plus at least one literal alternative byte. */
/* (P) Predicate steps are borrowed from TSQuery for its whole lifetime.
 * Validate each upstream predicate before running; counts derive from ABI. */
struct hl_predicate { const TSQueryPredicateStep *steps; uint32_t count; };

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
    /* Dotted captures inherit the common theme family; private predicate
     * captures and unknown names retain default. */
    static const struct { const char *name; hl_scope scope; } families[] = {
        {"comment",HL_SCOPE_COMMENT}, {"keyword.operator",HL_SCOPE_OPERATOR},
        {"keyword",HL_SCOPE_KEYWORD}, {"string",HL_SCOPE_STRING},
        {"number",HL_SCOPE_NUMBER}, {"constant",HL_SCOPE_CONSTANT},
        {"function",HL_SCOPE_FUNCTION}, {"type",HL_SCOPE_TYPE},
        {"variable",HL_SCOPE_VARIABLE}, {"punctuation",HL_SCOPE_PUNCTUATION},
        {"boolean",HL_SCOPE_CONSTANT}, {"character",HL_SCOPE_STRING},
        {"text.literal",HL_SCOPE_STRING}, {"text.title",HL_SCOPE_TYPE},
        {"text.uri",HL_SCOPE_STRING}, {"text.reference",HL_SCOPE_VARIABLE}
    };
    for (size_t i=0;i<sizeof families/sizeof families[0];i++) {
        size_t n=strlen(families[i].name);
        if (length>=n && !memcmp(name,families[i].name,n) &&
            (length==n || name[n]=='.')) return families[i].scope;
    }
    return HL_SCOPE_DEFAULT;
}
static bool query_string_is(const TSQuery *query, uint32_t id, const char *text)
{
    uint32_t length;
    const char *value = ts_query_string_value_for_id(query, id, &length);
    return strlen(text) == length && !memcmp(value, text, length);
}
static bool literal_alternatives(const char *regex, size_t length)
{
    if (length<5u || memcmp(regex,"^(",2u) || memcmp(regex+length-2u,")$",2u)) return false;
    bool empty=true;
    for (size_t at=2u;at<length-2u;at++) {
        char ch=regex[at];
        if (ch=='|') { if (empty) return false; empty=true; }
        else {
            if (!(ch>='A' && ch<='Z') && !(ch>='a' && ch<='z') &&
                !(ch>='0' && ch<='9') && ch!='_') return false;
            empty=false;
        }
    }
    return !empty;
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
        *predicate = (struct hl_predicate){steps,count};
        for (uint32_t at=0;at<count;) {
            uint32_t end=at;
            while (end<count && steps[end].type!=TSQueryPredicateStepTypeDone) end++;
            if (end==count || steps[at].type!=TSQueryPredicateStepTypeString) goto unsupported;
            uint32_t n=end-at;
            if (query_string_is(query->query,steps[at].value_id,"is-not?")) {
                /* Highlights-only has no locals query, so no local bindings. */
                if (n!=2u || steps[at+1u].type!=TSQueryPredicateStepTypeString ||
                    !query_string_is(query->query,steps[at+1u].value_id,"local")) goto unsupported;
            } else {
                if (n<3u || steps[at+1u].type!=TSQueryPredicateStepTypeCapture) goto unsupported;
                for (uint32_t j=at+2u;j<end;j++)
                    if (steps[j].type!=TSQueryPredicateStepTypeString) goto unsupported;
                if (query_string_is(query->query,steps[at].value_id,"match?")) {
                    if (n!=3u) goto unsupported;
                    uint32_t regex_length;
                    const char *regex=ts_query_string_value_for_id(query->query,steps[at+2u].value_id,&regex_length);
                    /* Exact expressions in vendored queries: uppercase classes,
                     * anchored alternatives, or a leading dash. */
                    if (!(query_string_is(query->query,steps[at+2u].value_id,"^[A-Z]") ||
                          query_string_is(query->query,steps[at+2u].value_id,"^[A-Z][A-Z\\d_]*$") ||
                          query_string_is(query->query,steps[at+2u].value_id,"^[A-Z][A-Z\\d_]+$'") ||
                          query_string_is(query->query,steps[at+2u].value_id,"^[A-Z][A-Z_]*$") ||
                          query_string_is(query->query,steps[at+2u].value_id,"^[A-Z_][A-Z\\d_]+$") ||
                          query_string_is(query->query,steps[at+2u].value_id,"^-") ||
                          literal_alternatives(regex,regex_length))) goto unsupported;
                } else if (query_string_is(query->query,steps[at].value_id,"eq?") && n!=3u) goto unsupported;
                else if (!query_string_is(query->query,steps[at].value_id,"eq?") &&
                           !query_string_is(query->query,steps[at].value_id,"any-of?") &&
                           !query_string_is(query->query,steps[at].value_id,"has-ancestor?")) goto unsupported;
            }
            at=end+1u;
        }
    }
    return HL_QUERY_OK;
unsupported:
    hl_query_destroy(query); return HL_QUERY_PREDICATE;
}
int hl_query_init(hl_query *query, const TSLanguage *language, hl_query_error *error)
{
    const char *name=hl_language_name(language);
    if (name) {
#define QUERY(lang) if (!strcmp(name,#lang)) return hl_query_init_source(query,language,highlights_##lang,sizeof highlights_##lang-1u,error)
        QUERY(c); QUERY(rust); QUERY(python); QUERY(javascript); QUERY(json);
        QUERY(toml); QUERY(yaml); QUERY(julia); QUERY(typescript); QUERY(markdown);
        QUERY(markdown_inline); QUERY(latex); QUERY(bash);
#undef QUERY
    }
    return hl_query_init_source(query,language,NULL,0,error);
}
static int node_equals(TSNode node, const piece_snapshot *snapshot,
                       const char *text, size_t length, bool *equal)
{
    uint64_t first=ts_node_start_byte(node), end=ts_node_end_byte(node);
    *equal=false;
    if (end-first!=length) return HL_QUERY_OK;
    /* (P) page scratch matches hl_request input; unbounded chunk comparison. */
    uint8_t bytes[sizeof ((hl_request *)0)->input];
    for (size_t at=0;at<length;) {
        size_t n=length-at<sizeof bytes ? length-at : sizeof bytes;
        if (piece_snapshot_read(snapshot,first+at,bytes,n)!=PIECE_OK) return HL_QUERY_READ;
        if (memcmp(bytes,text+at,n)) return HL_QUERY_OK;
        at+=n;
    }
    *equal=true; return HL_QUERY_OK;
}
static int regex_match(TSNode node, const piece_snapshot *snapshot,
                       const char *regex, size_t length, bool *accepted)
{
    *accepted=false;
    uint64_t first=ts_node_start_byte(node), end=ts_node_end_byte(node);
    if (first==end) return HL_QUERY_OK;
    if (length>=5u && !memcmp(regex,"^(",2u)) {
        for (size_t start=2u;start<length-2u;) {
            size_t stop=start;
            while (stop<length-2u && regex[stop]!='|') stop++;
            int rc=node_equals(node,snapshot,regex+start,stop-start,accepted);
            if (rc || *accepted) return rc;
            start=stop+1u;
        }
        return HL_QUERY_OK;
    }
    uint8_t byte;
    if (piece_snapshot_read(snapshot,first,&byte,sizeof byte)!=PIECE_OK) return HL_QUERY_READ;
    if (!strcmp(regex,"^-")) { *accepted=byte=='-'; return HL_QUERY_OK; }
    if (regex[length-1u]=='\'') return HL_QUERY_OK; /* preserved Rust end-assertion quirk */
    bool js=!strcmp(regex,"^[A-Z_][A-Z\\d_]+$");
    if (!(byte>='A' && byte<='Z') && !(js && byte=='_')) return HL_QUERY_OK;
    if (!strcmp(regex,"^[A-Z]")) { *accepted=true; return HL_QUERY_OK; }
    if (js && end-first<2u) return HL_QUERY_OK; /* '+' requires following byte */
    bool digits=strcmp(regex,"^[A-Z][A-Z_]*$")!=0;
    uint8_t bytes[sizeof ((hl_request *)0)->input];
    for (uint64_t at=first+1u;at<end;) {
        size_t n=end-at<sizeof bytes ? (size_t)(end-at) : sizeof bytes;
        if (piece_snapshot_read(snapshot,at,bytes,n)!=PIECE_OK) return HL_QUERY_READ;
        for (size_t i=0;i<n;i++) if (!(bytes[i]>='A' && bytes[i]<='Z') &&
            bytes[i]!='_' && !(digits && bytes[i]>='0' && bytes[i]<='9')) return HL_QUERY_OK;
        at+=n;
    }
    *accepted=true; return HL_QUERY_OK;
}
static int match_predicate(const hl_query *query, const struct hl_predicate *predicate,
                           const TSQueryMatch *match, const piece_snapshot *snapshot, bool *accepted)
{
    *accepted=true;
    const TSQueryPredicateStep *steps=predicate->steps;
    for (uint32_t at=0;at<predicate->count;) {
        uint32_t end=at;
        while (steps[end].type!=TSQueryPredicateStepTypeDone) end++;
        if (query_string_is(query->query,steps[at].value_id,"is-not?")) { at=end+1u; continue; }
        bool found=false;
        for (uint16_t i=0;i<match->capture_count;i++) {
            if (match->captures[i].index!=steps[at+1u].value_id) continue;
            TSNode node=match->captures[i].node;
            bool pass=false;
            for (uint32_t j=at+2u;j<end;j++) {
                uint32_t length;
                const char *value=ts_query_string_value_for_id(query->query,steps[j].value_id,&length);
                int rc=HL_QUERY_OK;
                if (query_string_is(query->query,steps[at].value_id,"match?"))
                    rc=regex_match(node,snapshot,value,length,&pass);
                else if (query_string_is(query->query,steps[at].value_id,"has-ancestor?")) {
                    for (TSNode parent=ts_node_parent(node);!ts_node_is_null(parent);parent=ts_node_parent(parent))
                        if (strlen(ts_node_type(parent))==length && !memcmp(ts_node_type(parent),value,length)) { pass=true; break; }
                } else rc=node_equals(node,snapshot,value,length,&pass);
                if (rc) return rc;
                if (pass) break;
            }
            if (!pass) { *accepted=false; return HL_QUERY_OK; }
            found=true;
        }
        if (!found) { *accepted=false; return HL_QUERY_OK; }
        at=end+1u;
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
        int rc = match_predicate(query, &query->predicates[match.pattern_index], &match, snapshot, &accepted);
        if (rc != HL_QUERY_OK) return rc;
        if (!accepted) continue;
        for (uint16_t i = 0; i < match.capture_count; ++i) {
            const TSQueryCapture *capture = &match.captures[i];
            uint32_t capture_length;
            const char *capture_name=ts_query_capture_name_for_id(query->query,capture->index,&capture_length);
            if (capture_length && capture_name[0]=='_') continue;
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
