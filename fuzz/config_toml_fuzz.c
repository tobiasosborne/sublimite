#include "config/toml.h"

#include <stdio.h>
#include <string.h>

/* Port of v1 fuzz/config_fuzz.c's parser half (the settings half belongs
 * to the future config settings bead). Invariants: the result always
 * equals error.code with offset inside the input; the visitor never
 * fails, so any nonzero result is a parser error and transactional
 * delivery means the visit count stayed zero; every borrowed string
 * stays inside the caller's caps. */

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "config_toml_fuzz: %s:%d: %s\n", __FILE__, __LINE__, #c); \
    __builtin_trap(); } } while (0)

/* (P) Big caps mirror v1's parser bounds (oracle src/config/config.h):
 * decoded text 4096 (CONFIG_TEXT_CAP, Linux PATH_MAX); name/entry/string
 * counts are fixture bounds above the seed documents' assignment counts.
 * Small caps exercise every LIMIT boundary at +-1 of tiny numbers. */
#define NAME_CAP 64u
#define ENTRY_CAP 128u
#define TEXT_CAP 4096u
#define STRINGS_CAP 128u
static char table_buf[NAME_CAP], path_buf[NAME_CAP], text_buf[TEXT_CAP];
static char seen_buf[ENTRY_CAP * NAME_CAP], declared_buf[ENTRY_CAP * NAME_CAP];
static size_t scopes_buf[ENTRY_CAP];
static const char *strings_buf[STRINGS_CAP];

typedef struct counter {
    size_t visited, name_cap, text_cap, strings_cap;
    const char *text;
} counter;

static int visit(void *ctx, const char *path, const config_toml_value *v)
{
    counter *c = ctx;
    c->visited++;
    CHECK(strlen(path) < c->name_cap);
    if (v->type == CONFIG_TOML_STRING) {
        CHECK(v->string >= c->text && v->string < c->text + c->text_cap);
        CHECK(strlen(v->string) < c->text_cap);
    }
    if (v->type == CONFIG_TOML_STRINGS) {
        CHECK(v->count <= c->strings_cap);
        for (size_t i = 0; i < v->count; i++) {
            CHECK(v->strings[i] >= c->text && v->strings[i] < c->text + c->text_cap);
            CHECK(strlen(v->strings[i]) < c->text_cap);
        }
    }
    return CONFIG_TOML_OK;
}

static void run(const uint8_t *data, size_t size, config_toml_caps caps)
{
    config_toml_scratch scratch = {
        .table = table_buf, .path = path_buf, .text = text_buf,
        .seen = seen_buf, .declared = declared_buf, .scopes = scopes_buf,
        .strings = strings_buf, .caps = caps,
    };
    counter c = { .name_cap = caps.name_cap, .text_cap = caps.text_cap,
                  .strings_cap = caps.strings_cap, .text = text_buf };
    config_toml_error error;
    int rc = config_toml_parse(data, size, &scratch, visit, &c, &error);
    CHECK(rc == error.code && error.offset <= size);
    if (rc) CHECK(c.visited == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    config_toml_caps big = { .name_cap = NAME_CAP, .entry_cap = ENTRY_CAP,
                             .text_cap = TEXT_CAP, .strings_cap = STRINGS_CAP,
                             .source_cap = SIZE_MAX };
    run(data, size, big);
    /* (P) small caps: 8/4/16/2, the same boundary fixtures as the unit
     * test's limit checks, so mutations cross LIMIT edges often. */
    config_toml_caps small = { .name_cap = 8u, .entry_cap = 4u,
                               .text_cap = 16u, .strings_cap = 2u,
                               .source_cap = SIZE_MAX };
    run(data, size, small);
    return 0;
}
