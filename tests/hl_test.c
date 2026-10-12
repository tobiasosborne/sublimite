/* edit-v0o.2 mailbox contract. Ranges are derived from the fixture bytes;
 * refresh comes from the caller, never from a guessed display rate. */
#include "hl/hl.h"
#include "hl/lang.h"
#include "hl/query.h"
#include "config/config.h"
#include "base/base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while (0)


/* (P) Fixture byte lengths bound these byte-indexed oracles; zero is the
 * default/unassigned scope and return success; one is the next row/identity. */
typedef struct scope_oracle {
    hl_scope *scopes;
    uint32_t *priorities;
    size_t length, captures;
    uint32_t generation;
} scope_oracle;
static int capture(void *payload, const hl_span *span)
{
    scope_oracle *o = payload;
    work_msg message;
    hl_span decoded;
    edit_malloc_guard_begin();
    int encoded = hl_span_encode(&message, o->generation, span);
    int read = hl_span_decode(&message, &decoded);
    size_t allocations = edit_malloc_guard_end();
    REQUIRE(encoded == HL_QUERY_OK && read == HL_QUERY_OK && !allocations);
    REQUIRE(message.generation == o->generation);
    REQUIRE(decoded.first_byte == span->first_byte && decoded.end_byte == span->end_byte);
    REQUIRE(decoded.scope == span->scope && decoded.priority == span->priority);
    REQUIRE(span->first_byte < span->end_byte && span->end_byte <= o->length);
    for (uint64_t i = span->first_byte; i < span->end_byte; ++i) {
        if (span->priority >= o->priorities[i]) {
            o->scopes[i] = span->scope; o->priorities[i] = span->priority;
        }
    }
    ++o->captures;
    return HL_QUERY_OK;
}
static int refused_capture(void *payload, const hl_span *span)
{
    (void)payload; (void)span;
    return HL_QUERY_STOP;
}
static int token(const char *text, const char *name, const scope_oracle *o, hl_scope expected)
{
    const char *at = strstr(text, name);
    REQUIRE(at);
    for (size_t i = (size_t)(at - text); i < (size_t)(at - text) + strlen(name); ++i)
        REQUIRE(o->scopes[i] == expected);
    return 0;
}
static int language_test(const TSLanguage *language, const char *text, bool rust)
{
    size_t length = strlen(text);
    /* (P) C11 VLAs: one entry per fixture byte, no arbitrary capture capacity. */
    hl_scope scopes[length]; uint32_t priorities[length];
    memset(scopes, 0, sizeof scopes); memset(priorities, 0, sizeof priorities);
    scope_oracle o = {scopes, priorities, length, 0, (uint32_t)length};
    TSParser *parser = ts_parser_new(); REQUIRE(parser);
    REQUIRE(ts_parser_set_language(parser, language));
    TSTree *tree = ts_parser_parse_string(parser, NULL, text, (uint32_t)length);
    REQUIRE(tree && !ts_node_has_error(ts_tree_root_node(tree)));
    piece_allocator allocator = piece_default_allocator();
    piece_tree *source = piece_create(&allocator); REQUIRE(source);
    REQUIRE(piece_init_copy(source, (const uint8_t *)text, length) == PIECE_OK);
    piece_snapshot *snapshot = piece_snapshot_take(source); REQUIRE(snapshot);
    hl_query query; hl_query_error error;
    REQUIRE(hl_query_init(&query, language, &error) == HL_QUERY_OK);
    for (uint32_t i = 0; i < ts_query_capture_count(query.query); ++i)
        REQUIRE(query.scopes[i] != HL_SCOPE_DEFAULT);
    REQUIRE(hl_query_run(&query, tree, snapshot, 0, UINT32_MAX, capture, &o) == HL_QUERY_OK);
    REQUIRE(o.captures && token(text, "// comment", &o, HL_SCOPE_COMMENT) == 0);
    REQUIRE(token(text, "return", &o, HL_SCOPE_KEYWORD) == 0);
    REQUIRE(token(text, "greet", &o, HL_SCOPE_FUNCTION) == 0);
    REQUIRE(token(text, "\"hello\"", &o, HL_SCOPE_STRING) == 0);
    REQUIRE(token(text, "value", &o, HL_SCOPE_VARIABLE) == 0);
    REQUIRE(token(text, rust ? "i32" : "int", &o, HL_SCOPE_TYPE) == 0);
    /* (P) Upstream Rust groups integer/float/bool under constant.builtin. */
    REQUIRE(token(text, "42", &o, rust ? HL_SCOPE_CONSTANT : HL_SCOPE_NUMBER) == 0);
    if (rust) {
        REQUIRE(token(text, "#[inline]", &o, HL_SCOPE_ATTRIBUTE) == 0);
        REQUIRE(token(text, "Some", &o, HL_SCOPE_TYPE) == 0);
    } else REQUIRE(token(text, "LIMIT", &o, HL_SCOPE_CONSTANT) == 0);
    REQUIRE(token(text, ";", &o, HL_SCOPE_PUNCTUATION) == 0);
    REQUIRE(hl_query_run(&query, tree, snapshot, 0, UINT32_MAX, refused_capture, NULL) == HL_QUERY_STOP);
    /* (P) first row is exactly the comment fixture; next row excludes it. */
    memset(scopes, 0, sizeof scopes); o.captures = 0;
    REQUIRE(hl_query_run(&query, tree, snapshot, 1u, UINT32_MAX, capture, &o) == HL_QUERY_OK);
    REQUIRE(token(text, "// comment", &o, HL_SCOPE_DEFAULT) == 0);
    REQUIRE(hl_query_run(&query, tree, snapshot, 1u, 0, capture, &o) == HL_QUERY_ARG);
    hl_query_destroy(&query);
    const char unknown[] = "(identifier) @future.capture";
    REQUIRE(hl_query_init_source(&query, language, unknown, sizeof unknown - 1u, &error) == HL_QUERY_OK);
    memset(scopes, 0, sizeof scopes);
    REQUIRE(hl_query_run(&query, tree, snapshot, 0, UINT32_MAX, capture, &o) == HL_QUERY_OK);
    REQUIRE(token(text, "greet", &o, HL_SCOPE_DEFAULT) == 0);
    hl_query_destroy(&query);
    const char invalid[] = "(no_such_node) @comment";
    REQUIRE(hl_query_init_source(&query, language, invalid, sizeof invalid - 1u, &error) == HL_QUERY_COMPILE);
    REQUIRE(error.type == TSQueryErrorNodeType); hl_query_destroy(&query);
    const char unsupported[] = "((identifier) @variable (#unknown? @variable))";
    REQUIRE(hl_query_init_source(&query, language, unsupported, sizeof unsupported - 1u, &error) == HL_QUERY_PREDICATE);
    hl_query_destroy(&query);
    ts_tree_delete(tree); ts_parser_delete(parser);
    piece_snapshot_release(snapshot); piece_destroy(source);
    return 0;
}

typedef struct query_job {
    const TSLanguage *language;
    const char *text;
    const piece_snapshot *snapshot;
    int status;
} query_job;
static int publish_span(void *payload, const hl_span *span)
{
    work_ctx *context = payload;
    work_msg message;
    int rc = hl_span_encode(&message, context->generation, span);
    if (rc) return rc;
    return work_publish(context, &message) ? HL_QUERY_OK : HL_QUERY_STOP;
}
static void query_worker_job(work_ctx *context)
{
    query_job *job = context->arg;
    hl_query query;
    /* Initialize allocating query/parser state on the real worker thread. */
    job->status = hl_query_init(&query, job->language, NULL);
    if (job->status) return;
    TSParser *parser = ts_parser_new();
    TSTree *tree = NULL;
    if (!parser || !ts_parser_set_language(parser, job->language)) job->status = HL_QUERY_ARG;
    else {
        tree = ts_parser_parse_string(parser, NULL, job->text, (uint32_t)strlen(job->text));
        job->status = tree ? hl_query_run(&query, tree, job->snapshot, 0, UINT32_MAX,
                                         publish_span, context) : HL_QUERY_READ;
    }
    ts_tree_delete(tree);
    if (parser) ts_parser_delete(parser);
    hl_query_destroy(&query);
    if (!job->status) {
        work_msg message;
        hl_result complete = {0, strlen(job->text), HL_DOCUMENT_READY};
        if (!hl_result_encode(&message, context->generation, &complete) ||
            !work_publish(context, &message)) job->status = HL_QUERY_STOP;
    }
}
typedef struct received_scopes {
    uint32_t generation;
    size_t length, spans;
    bool completed, failed, saw_function;
} received_scopes;
static void receive_scopes(const work_msg *message, void *payload)
{
    received_scopes *received = payload;
    if (message->generation != received->generation || received->completed) { received->failed = true; return; }
    if (message->kind == HL_SPAN_KIND) {
        hl_span span;
        if (hl_span_decode(message, &span) || span.end_byte > received->length) received->failed = true;
        else if (span.scope == HL_SCOPE_FUNCTION) received->saw_function = true;
        ++received->spans;
    } else {
        hl_result complete;
        if (!hl_result_decode(message, &complete) || complete.phase != HL_DOCUMENT_READY ||
            complete.first_byte != 0 || complete.end_byte != received->length) received->failed = true;
        received->completed = true;
    }
}
static int mailbox_query_test(const TSLanguage *language, const char *text)
{
    piece_allocator allocator = piece_default_allocator();
    piece_tree *source = piece_create(&allocator); REQUIRE(source);
    REQUIRE(piece_init_copy(source, (const uint8_t *)text, strlen(text)) == PIECE_OK);
    piece_snapshot *snapshot = piece_snapshot_take(source); REQUIRE(snapshot);
    edit_arena arena;
    /* (P) One pool plus its alignment padding, backed by src/base. */
    REQUIRE(edit_arena_init(&arena, sizeof(work_pool) + _Alignof(work_pool)) == 0);
    work_pool *pool = edit_arena_alloc(&arena, sizeof(work_pool), _Alignof(work_pool)); REQUIRE(pool);
    REQUIRE(work_pool_init(pool, 1u, 0) == 0); /* (P) one bulk worker, no raster worker */
    query_job job = {language, text, snapshot, HL_QUERY_ARG};
    uint32_t generation = (uint32_t)strlen(text); /* (P) fixture-derived identity */
    work_handle handle = work_submit(pool, (work_job){query_worker_job, &job, generation, WORK_BULK});
    REQUIRE(handle.epoch);
    received_scopes received = {generation, strlen(text), 0, false, false, false};
    while (!work_handle_finished(pool, handle) || work_mailbox_pending(pool)) {
        (void)work_mailbox_receive(pool, handle, generation, receive_scopes, &received);
        sched_yield();
    }
    REQUIRE(job.status == HL_QUERY_OK && received.completed && !received.failed);
    REQUIRE(received.spans && received.saw_function);
    work_pool_shutdown(pool); edit_arena_free(&arena);
    piece_snapshot_release(snapshot); piece_destroy(source);
    return 0;
}

static int query_test(void)
{
    /* (P) Numeric literals here are syntax fixtures, not timing/count budgets. */
    const char c[] = "// comment\nint greet(int value) { const char *s = \"hello\"; return value + 42 + LIMIT; }\n";
    const char rust[] = "// comment\n#[inline]\nfn greet(value: i32) -> i32 { let s = \"hello\"; let x = Some; return 42; }\n";
    REQUIRE(language_test(hl_language_c(), c, false) == 0);
    REQUIRE(language_test(hl_language_rust(), rust, true) == 0);
    REQUIRE(mailbox_query_test(hl_language_c(), c) == 0);
    REQUIRE(mailbox_query_test(hl_language_rust(), rust) == 0);
    for (hl_scope scope = HL_SCOPE_COMMENT; scope < HL_SCOPE_COUNT; scope++) {
        const char *key = hl_scope_key(scope); REQUIRE(key);
        REQUIRE(config_theme_color(CONFIG_DARK, key));
        REQUIRE(config_theme_color(CONFIG_LIGHT, key));
    }
    REQUIRE(!strcmp(hl_scope_key(HL_SCOPE_DEFAULT), "editor.foreground"));
    work_msg message = {0}; hl_span span = {0};
    REQUIRE(hl_span_decode(&message, &span) == HL_QUERY_ARG);
    span = (hl_span){0, sizeof "x" - 1u, HL_SCOPE_KEYWORD, 0};
    REQUIRE(hl_span_encode(&message, 0, &span) == HL_QUERY_OK);
    uint32_t invalid_scope = HL_SCOPE_COUNT;
    /* (P) Scope follows the two fixed-width byte endpoints on the wire. */
    memcpy(message.data + sizeof(uint64_t) + sizeof(uint64_t), &invalid_scope, sizeof invalid_scope);
    REQUIRE(hl_span_decode(&message, &span) == HL_QUERY_ARG);
    span.scope = HL_SCOPE_COUNT;
    REQUIRE(hl_span_encode(&message, 0, &span) == HL_QUERY_ARG);
    span = (hl_span){0, 0, HL_SCOPE_DEFAULT, 0}; /* (P) empty ranges carry no capture */
    REQUIRE(hl_span_encode(&message, 0, &span) == HL_QUERY_ARG);
    puts("hl_test: PASS C/Rust parse, queries, predicates, scopes, row range, pointer-free spans, worker publication/completion, themes");
    return 0;
}

/* (P) Numeric identities here follow C/API contracts: zero is success,
 * an empty count or the first byte; one is failure or one byte/element.
 * Source and query lengths come from sizeof/ftell, never guessed capacities.
 * Parser/query allocations occur only in this standalone worker-side probe. */
static int grammar_probe(const char *name, const TSLanguage *language,
                         const char *fixture, size_t fixture_length,
                         const char *query_path)
{
    REQUIRE(language != NULL);
    const uint32_t abi = ts_language_abi_version(language);
    REQUIRE(abi >= TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION);
    REQUIRE(abi <= TREE_SITTER_LANGUAGE_VERSION);
    REQUIRE(fixture_length <= UINT32_MAX);
    TSParser *parser = ts_parser_new();
    REQUIRE(parser != NULL);
    REQUIRE(ts_parser_set_language(parser, language));
    TSTree *tree = ts_parser_parse_string(parser, NULL, fixture,
                                        (uint32_t)fixture_length);
    REQUIRE(tree != NULL);
    const bool has_error = ts_node_has_error(ts_tree_root_node(tree));
    REQUIRE(!has_error); /* rejects ERROR and missing nodes in any descendant */

    FILE *file = fopen(query_path, "rb");
    REQUIRE(file != NULL);
    REQUIRE(fseek(file, 0, SEEK_END) == 0);
    const long query_length = ftell(file);
    REQUIRE(query_length > 0 && (unsigned long)query_length <= UINT32_MAX);
    REQUIRE(fseek(file, 0, SEEK_SET) == 0);
    char *source = malloc((size_t)query_length);
    REQUIRE(source != NULL);
    const size_t read_length = fread(source, 1u, (size_t)query_length, file);
    const int close_status = fclose(file);
    if (read_length != (size_t)query_length || close_status != 0) {
        free(source);
        return 1;
    }
    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    TSQuery *query = ts_query_new(language, source, (uint32_t)query_length,
                                 &error_offset, &error_type);
    free(source);
    if (query == NULL) {
        fprintf(stderr, "%s query: error=%d byte=%u\n", name,
                (int)error_type, error_offset);
        return 1;
    }
    REQUIRE(error_type == TSQueryErrorNone);
    printf("hl_test: %s ABI=%u; fixture without ERROR; highlights query compiled\n",
           name, abi);
    const uint32_t capture_count = ts_query_capture_count(query);
    for (uint32_t capture = 0; capture < capture_count; ++capture) {
        uint32_t length = 0;
        const char *capture_name = ts_query_capture_name_for_id(query, capture,
                                                               &length);
        printf("  @%.*s\n", (int)length, capture_name);
    }
    ts_query_delete(query);
    hl_query production;
    REQUIRE(hl_query_init(&production,language,NULL)==HL_QUERY_OK);
    piece_allocator allocator=piece_default_allocator();
    piece_tree *piece=piece_create(&allocator); REQUIRE(piece);
    REQUIRE(piece_init_copy(piece,(const uint8_t *)fixture,fixture_length)==PIECE_OK);
    piece_snapshot *snapshot=piece_snapshot_take(piece); REQUIRE(snapshot);
    /* (P) Oracle is exactly one scope/priority per fixture byte. */
    hl_scope scopes[fixture_length]; uint32_t priorities[fixture_length];
    memset(scopes,0,sizeof scopes); memset(priorities,0,sizeof priorities);
    scope_oracle oracle={scopes,priorities,fixture_length,0,(uint32_t)fixture_length};
    REQUIRE(hl_query_run(&production,tree,snapshot,0,UINT32_MAX,capture,&oracle)==HL_QUERY_OK);
    REQUIRE(oracle.captures);
    printf("hl_test: %s embedded production query ran, captures=%zu (P fixture-derived)\n",name,oracle.captures);
    hl_query_destroy(&production);
    piece_snapshot_release(snapshot); piece_destroy(piece);
    ts_tree_delete(tree); ts_parser_delete(parser);
    return 0;
}

static int python_probe(void)
{
    /* (P) Fixture constants spell language syntax, not work/time budgets. */
    const char fixture[] =
        "@decorate\n"
        "def greet(name: str):\n"
        "    # scanner must handle indentation and interpolation\n"
        "    message = f\"hello {name}\"\n"
        "    if name is not None:\n"
        "        print(message)\n"
        "    return message\n";
    return grammar_probe("python", hl_language_python(), fixture,
                         sizeof fixture - 1u,
                         "vendor/tree-sitter-python/queries/highlights.scm");
}

static int julia_probe(void)
{
    const char fixture[] =
        "module Greetings\n"
        "#= scanner must handle nested #= comments =# too =#\n"
        "function greet(name::String)\n"
        "    message = \"hello $name\"\n"
        "    println(message)\n"
        "    return message\n"
        "end\n"
        "end\n";
    return grammar_probe("julia", hl_language_julia(), fixture,
                         sizeof fixture - 1u,
                         "vendor/tree-sitter-julia/queries/highlights.scm");
}

static int javascript_probe(void)
{
    /* (P) Fixture literals spell syntax, not budgets. Length excludes the C terminator. */
    const char fixture[] =
        "/* template substitution, regex and automatic semicolon insertion */\n"
        "export function greet(name) {\n"
        "  const pattern = /hello+/u;\n"
        "  const message = `hello ${name}`\n"
        "  return pattern.test(message) ? message : null;\n"
        "}\n";
    return grammar_probe("javascript", hl_language_javascript(), fixture,
                         sizeof fixture - 1u,
                         "vendor/tree-sitter-javascript/queries/highlights.scm");
}

static int typescript_probe(void)
{
    /* (P) Fixture literals spell syntax, not budgets. Length excludes the C terminator. */
    const char fixture[] =
        "interface Greeting { name: string; }\n"
        "export function greet<T extends Greeting>(value: T): string {\n"
        "  const message: string = `hello ${value.name}`;\n"
        "  return message;\n"
        "}\n";
    return grammar_probe("typescript", hl_language_typescript(), fixture,
                         sizeof fixture - 1u,
                         "vendor/tree-sitter-typescript/queries/highlights.scm");
}

static int json_probe(void)
{
    /* (P) Fixture literals spell syntax, not budgets. Length excludes the C terminator. */
    const char fixture[] =
        "{\"greeting\": \"hello\\nworld\", \"enabled\": true, \"missing\": null, \"names\": [\"reader\"]}\n";
    return grammar_probe("json", hl_language_json(), fixture,
                         sizeof fixture - 1u,
                         "vendor/tree-sitter-json/queries/highlights.scm");
}

static int toml_probe(void)
{
    /* (P) Fixture literals spell syntax, not budgets. Length excludes the C terminator. */
    const char fixture[] =
        "# tables, arrays and multiline strings exercise the scanner\n"
        "[greeting]\n"
        "name = \"reader\"\n"
        "enabled = true\n"
        "names = [\"reader\", \"writer\"]\n"
        "message = \"\"\"hello\n"
        "world\"\"\"\n";
    return grammar_probe("toml", hl_language_toml(), fixture,
                         sizeof fixture - 1u,
                         "vendor/tree-sitter-toml/queries/highlights.scm");
}

static int yaml_probe(void)
{
    /* (P) Fixture literals spell syntax, not budgets. Length excludes the C terminator. */
    const char fixture[] =
        "# indentation, aliases, block and flow collections\n"
        "greeting: &message\n"
        "  name: reader\n"
        "  enabled: true\n"
        "  names: [reader, writer]\n"
        "  text: |\n"
        "    hello\n"
        "    world\n"
        "copy: *message\n";
    return grammar_probe("yaml", hl_language_yaml(), fixture,
                         sizeof fixture - 1u,
                         "vendor/tree-sitter-yaml/queries/highlights.scm");
}

static int markdown_probe(void)
{
    const char fixture[] =
        "# Heading\n\n"
        "A *small* document with **strong** emphasis.\n\n"
        "```bash\n"
        "echo hello\n"
        "```\n";
    REQUIRE(grammar_probe("markdown", hl_language_markdown(), fixture,
        sizeof fixture - 1u,
        "vendor/tree-sitter-markdown/tree-sitter-markdown/queries/highlights.scm") == 0);
    return grammar_probe("markdown_inline", hl_language_markdown_inline(), fixture,
        sizeof fixture - 1u,
        "vendor/tree-sitter-markdown/tree-sitter-markdown-inline/queries/highlights.scm");
}

static int latex_probe(void)
{
    const char fixture[] =
        "\\documentclass{article}\n"
        "\\begin{document}\n"
        "\\section{Greeting}\n"
        "% scanner must handle verbatim content\n"
        "Hello \\textbf{world}, $x + y$.\n"
        "\\begin{verbatim}\n"
        "raw { text } % literal\n"
        "\\end{verbatim}\n"
        "\\end{document}\n";
    return grammar_probe("latex", hl_language_latex(), fixture,
        sizeof fixture - 1u,
        "vendor/tree-sitter-latex/queries/highlights.scm");
}

static int bash_probe(void)
{
    const char fixture[] =
        "#!/bin/bash\n"
        "greet() {\n"
        "    local name=\"$USER\"\n"
        "    if [[ -n \"$name\" ]]; then\n"
        "        cat <<EOF\n"
        "hello ${name}\n"
        "EOF\n"
        "    fi\n"
        "}\n"
        "greet | cat\n";
    return grammar_probe("bash", hl_language_bash(), fixture,
        sizeof fixture - 1u,
        "vendor/tree-sitter-bash/queries/highlights.scm");
}

int main(void)
{
    REQUIRE(query_test() == 0);
    edit_malloc_guard_begin();
    const TSLanguage *python = hl_language_python();
    const TSLanguage *julia = hl_language_julia();
    const TSLanguage *javascript = hl_language_javascript();
    const TSLanguage *typescript = hl_language_typescript();
    const TSLanguage *json = hl_language_json();
    const TSLanguage *toml = hl_language_toml();
    const TSLanguage *yaml = hl_language_yaml();
    const TSLanguage *markdown = hl_language_markdown();
    const TSLanguage *markdown_inline = hl_language_markdown_inline();
    const TSLanguage *latex = hl_language_latex();
    const TSLanguage *bash = hl_language_bash();
    const size_t language_allocations = edit_malloc_guard_end();
    REQUIRE(language_allocations == 0); /* (P) typing-path allocation law */
    REQUIRE(python != NULL && julia != NULL);
    REQUIRE(python_probe() == 0);
    REQUIRE(julia_probe() == 0);
    REQUIRE(javascript != NULL);
    REQUIRE(javascript_probe() == 0);
    REQUIRE(typescript != NULL);
    REQUIRE(typescript_probe() == 0);
    REQUIRE(json != NULL);
    REQUIRE(json_probe() == 0);
    REQUIRE(toml != NULL);
    REQUIRE(toml_probe() == 0);
    REQUIRE(yaml != NULL);
    REQUIRE(yaml_probe() == 0);
    REQUIRE(markdown != NULL && markdown_inline != NULL && latex != NULL && bash != NULL);
    REQUIRE(markdown_probe() == 0);
    REQUIRE(latex_probe() == 0);
    REQUIRE(bash_probe() == 0);
    const char text[] = "prefix\nviewport\nsuffix\n";
    const uint64_t first = sizeof "prefix\n" - 1u;
    const uint64_t end = first + sizeof "viewport\n" - 1u;
    /* Derivation: the probe uses the fixture's byte length as a nonzero
     * synthetic refresh input; it asserts an identity, not a timing gate. */
    const uint64_t refresh = sizeof text - 1u;
    const uint32_t generation = (uint32_t)sizeof text;
    uint64_t budget = 0;
    REQUIRE(hl_frame_budget_ns(refresh, &budget));
    REQUIRE(budget == refresh);
    REQUIRE(!hl_frame_budget_ns(0, &budget)); /* no measured refresh available */
    REQUIRE(!hl_frame_budget_ns(refresh, NULL));

    work_msg message = {0};
    hl_result result = {first, end, HL_VIEWPORT_READY};
    hl_result decoded = {0};
    REQUIRE(!hl_viewport_ready(&message, generation, first, end));
    edit_malloc_guard_begin();
    REQUIRE(hl_result_encode(&message, generation, &result));
    REQUIRE(hl_result_decode(&message, &decoded));
    REQUIRE(hl_viewport_ready(&message, generation, first, end));
    REQUIRE(!hl_viewport_ready(&message, generation + 1u, first, end));
    REQUIRE(!hl_viewport_ready(&message, generation, 0, end));
    REQUIRE(!hl_viewport_ready(&message, generation, first, end + 1u));
    REQUIRE(!hl_viewport_ready(&message, generation, end, first));
    size_t allocations = edit_malloc_guard_end();
    REQUIRE(allocations == 0); /* typing-path allocation law */
    REQUIRE(decoded.first_byte == first && decoded.end_byte == end);

    result.phase = HL_DOCUMENT_READY;
    REQUIRE(hl_result_encode(&message, generation, &result));
    REQUIRE(!hl_viewport_ready(&message, generation, first, end));
    result.phase = HL_PARSE_FAILED;
    REQUIRE(hl_result_encode(&message, generation, &result));
    REQUIRE(!hl_viewport_ready(&message, generation, first, end));
    result.phase = HL_PARSE_FAILED + 1u; /* first value beyond assigned phases */
    REQUIRE(!hl_result_encode(&message, generation, &result));
    memcpy(message.data + sizeof(uint64_t) + sizeof(uint64_t),
           &result.phase, sizeof result.phase);
    REQUIRE(!hl_result_decode(&message, &decoded));
    result.phase = HL_VIEWPORT_READY;
    result.first_byte = end;
    result.end_byte = first;
    REQUIRE(!hl_result_encode(&message, generation, &result));
    message.kind = 0; /* unassigned mailbox kind */
    REQUIRE(!hl_result_decode(&message, &decoded));
    REQUIRE(!hl_result_encode(NULL, generation, &result));
    REQUIRE(!hl_result_decode(NULL, &decoded));
    REQUIRE(!hl_result_decode(&message, NULL));
    puts("hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard");
    return 0;
}
