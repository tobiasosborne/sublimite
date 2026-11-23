#include "config/toml.h"

#include <stdio.h>
#include <string.h>

/* Differential oracle: v1 tests/config_test.c parser_test() (oracle pin
 * 6fec2e8), ported from the settings level (config_parse, which staged into
 * a fresh config_settings) to the parser level. v1's transactional check
 * was "settings byte-identical after a failed parse"; here the recording
 * visitor's output is byte-identical and the visit count is zero, because
 * config_toml_parse validates the whole document before visiting. */

#define T(c) do { if (!(c)) { \
    fprintf(stderr, "config_toml_test:%d: FAIL %s\n", __LINE__, #c); return 1; \
} } while (0)
#define BYTES(s) (const uint8_t *)(s), sizeof(s) - 1u

/* (P) Scratch capacities for the differential runs mirror v1's parser
 * bounds (oracle src/config/config.h:10-15): decoded text 4096
 * (CONFIG_TEXT_CAP, Linux PATH_MAX), names bounded by the longest action
 * path (CONFIG_NAME_CAP = sizeof("keymap.split_selection_lines") = 29;
 * 64 here only widens the fixture, limit boundaries use the small caps
 * below), entries/strings bounded by the action count (128 is a fixture
 * bound above every fixture's assignment count, counted per fixture). */
#define BIG_NAME 64u
#define BIG_ENTRY 128u
#define BIG_TEXT 4096u
#define BIG_STRINGS 128u
static char big_table[BIG_NAME], big_path[BIG_NAME], big_text[BIG_TEXT];
static char big_seen[BIG_ENTRY * BIG_NAME], big_declared[BIG_ENTRY * BIG_NAME];
static size_t big_scopes[BIG_ENTRY];
static const char *big_strings[BIG_STRINGS];

static config_toml_scratch big_scratch(void)
{
    config_toml_scratch s = {
        .table = big_table, .path = big_path, .text = big_text,
        .seen = big_seen, .declared = big_declared, .scopes = big_scopes,
        .strings = big_strings,
        .caps = { .name_cap = BIG_NAME, .entry_cap = BIG_ENTRY,
                  .text_cap = BIG_TEXT, .strings_cap = BIG_STRINGS,
                  .source_cap = SIZE_MAX },
    };
    return s;
}

/* (P) Small caps: every limit boundary below is derived from these four
 * numbers by +-1 arithmetic stated at the check. */
#define SMALL_NAME 8u
#define SMALL_ENTRY 4u
#define SMALL_TEXT 16u
#define SMALL_STRINGS 2u
#define SMALL_SOURCE 64u
static char small_table[SMALL_NAME], small_path[SMALL_NAME], small_text[SMALL_TEXT];
static char small_seen[SMALL_ENTRY * SMALL_NAME], small_declared[SMALL_ENTRY * SMALL_NAME];
static size_t small_scopes[SMALL_ENTRY];
static const char *small_strings[SMALL_STRINGS];

static config_toml_scratch small_scratch(void)
{
    config_toml_scratch s = {
        .table = small_table, .path = small_path, .text = small_text,
        .seen = small_seen, .declared = small_declared, .scopes = small_scopes,
        .strings = small_strings,
        .caps = { .name_cap = SMALL_NAME, .entry_cap = SMALL_ENTRY,
                  .text_cap = SMALL_TEXT, .strings_cap = SMALL_STRINGS,
                  .source_cap = SMALL_SOURCE },
    };
    return s;
}

typedef struct oracle { size_t visited; } oracle;
static int visit(void *ctx, const char *path, const config_toml_value *v)
{
    oracle *o = ctx; o->visited++;
    if (!strcmp(path, "test.min")) T(v->type == CONFIG_TOML_INTEGER && v->integer == INT64_MIN);
    else if (!strcmp(path, "test.max")) T(v->type == CONFIG_TOML_INTEGER && v->integer == INT64_MAX);
    else if (!strcmp(path, "test.float")) T(v->type == CONFIG_TOML_FLOAT && v->number == 125.0);
    else if (!strcmp(path, "test.bool")) T(v->type == CONFIG_TOML_BOOLEAN && !v->boolean);
    else if (!strcmp(path, "test.array")) {
        T(v->type == CONFIG_TOML_STRINGS && v->count == 2u); /* (P) fixture has two elements */
        T(!strcmp(v->strings[0], "a\n\xe2\x98\x83") && !strcmp(v->strings[1], "b\\c"));
    } else if (!strcmp(path, "test.empty")) T(v->type == CONFIG_TOML_STRINGS && !v->count);
    else T(false);
    return 0;
}

static int count_only(void *ctx, const char *path, const config_toml_value *v)
{
    (void)path; (void)v;
    (*(size_t *)ctx)++;
    return 0;
}

static int good_document(void)
{
    /* v1 fixture verbatim: decimal extrema from int64_t's range, float is
     * 1.25 * 10^2, Unicode escape encodes the snowman scalar, literal
     * string keeps its backslash. */
    const char source[] = "# comment\r\n[test]\r\nmin = -9223372036854775808\n"
        "max = +9_223_372_036_854_775_807\nfloat = 1.2_5e+2\nbool = false\n"
        "array = [\n # comment\n\"a\\n\\u2603\", 'b\\c',\n]\nempty = []";
    oracle o = {0}; config_toml_error error; config_toml_scratch sc = big_scratch();
    T(config_toml_parse(BYTES(source), &sc, visit, &o, &error) == CONFIG_TOML_OK);
    T(error.code == CONFIG_TOML_OK);
    T(o.visited == 6u); /* (P) min/max/float/bool/array/empty */
    return 0;
}

static int bad_documents(void)
{
    /* v1's bad[] list verbatim (parser-level: duplicates, int64/float
     * overflow, number/string/array syntax, unsupported TOML). */
    const char *bad[] = {
        "a=1\na=2", "[a]\n[a]", "a=1\n[a]", "a=1\na.b=2", "a.b=1\na=2",
        "a.b=1\n[a]", "[a]\nb.c=1\n[a.b]",
        "[a.b]\nx=1\n[a]\nb.c=2",
        "a = 9223372036854775808", "a = -9223372036854775809", "a=1e99999", "a=1e-99999",
        "a=01", "a=1_", "a=1__2", "a=.5", "a=1.", "a=1e", "a=+", "a=--1",
        "a=truex", "a=False", "a='unterminated", "a=\"\\q\"", "a=\"\\u0000\"",
        "a=\"\\uD800\"", "a=\"\\U00110000\"", "a=\"\\u123\"", "a=\"raw\nline\"",
        "a=[1]", "a=['a' 'b']", "a=[['a']]", "a=['a'", "a={b=1}", "[[a]]",
        "a=0x10", "a=nan", "a=1979-05-27", "[a] junk", "a=1 junk", "a=1\r", "a..b=1"
    };
    config_toml_scratch sc = big_scratch(); config_toml_error error;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        size_t visited = 0;
        int rc = config_toml_parse((const uint8_t *)bad[i], strlen(bad[i]),
                                    &sc, count_only, &visited, &error);
        T(rc != CONFIG_TOML_OK);
        T(error.code == rc && error.offset <= strlen(bad[i]));
        T(visited == 0); /* transactional: nothing was handed out */
    }
    const uint8_t invalid[] = {'a', '=', '\'', 0xff, '\''};
    size_t visited = 0;
    T(config_toml_parse(invalid, sizeof invalid, &sc, count_only, &visited, &error)
        == CONFIG_TOML_ERR_SYNTAX);
    T(visited == 0);
    T(error.offset == 3u); /* (P) 0xff is byte index 3 of the fixture */
    const uint8_t nul[] = {'#', 0};
    T(config_toml_parse(nul, sizeof nul, &sc, count_only, &visited, &error)
        == CONFIG_TOML_ERR_SYNTAX);
    T(error.offset == 1u); /* (P) NUL is byte index 1, even inside a comment */
    return 0;
}

static int error_positions(void)
{
    /* Hand-traced against v1 parser.c fail(): the first error wins and its
     * offset is the byte position where the failing check runs. */
    config_toml_scratch sc = big_scratch(); config_toml_error error;
    size_t visited = 0;
    const char empty_segment[] = "a..b=1";
    T(config_toml_parse(BYTES(empty_segment), &sc, count_only, &visited, &error)
        == CONFIG_TOML_ERR_SYNTAX);
    T(error.offset == 2u); /* (P) second '.' at index 2 starts an empty segment */
    const char junk[] = "[a] junk";
    T(config_toml_parse(BYTES(junk), &sc, count_only, &visited, &error)
        == CONFIG_TOML_ERR_SYNTAX);
    T(error.offset == 4u); /* (P) 'j' at index 4 follows the table header */
    /* A later error never overwrites the first: both faults present, the
     * leading-zero fault (value ends at index 5) is reported, not the
     * trailing junk. */
    const char two_faults[] = "a = 01 junk";
    T(config_toml_parse(BYTES(two_faults), &sc, count_only, &visited, &error)
        == CONFIG_TOML_ERR_SYNTAX);
    T(error.offset == 6u); /* (P) digits "01" end at index 6 where the check runs */
    T(visited == 0);
    return 0;
}

typedef struct recorder {
    char log[256]; /* (P) fixture: two short "path;" records fit */
    size_t used, visited;
} recorder;
static int record(void *ctx, const char *path, const config_toml_value *v)
{
    (void)v;
    recorder *r = ctx;
    size_t n = strlen(path);
    memcpy(r->log + r->used, path, n); r->used += n; r->log[r->used++] = ';';
    r->log[r->used] = 0; r->visited++;
    return 0;
}

static int transactional(void)
{
    config_toml_scratch sc = big_scratch(); config_toml_error error;
    recorder r; memset(&r, 0xa5, sizeof r); r.used = 0; r.visited = 0; r.log[0] = 0;
    const char good_then_bad[] = "a = 1\nb = "; /* valid prefix, then no value */
    recorder before = r;
    T(config_toml_parse(BYTES(good_then_bad), &sc, record, &r, &error) != CONFIG_TOML_OK);
    T(!memcmp(&before, &r, sizeof r)); /* caller's output untouched */
    const char good[] = "a = 1\nb = 2\n";
    T(config_toml_parse(BYTES(good), &sc, record, &r, &error) == CONFIG_TOML_OK);
    T(!strcmp(r.log, "a;b;") && r.visited == 2u); /* (P) two assignments, in order */
    return 0;
}

static int fail_on_second(void *ctx, const char *path, const config_toml_value *v)
{
    (void)path; (void)v;
    size_t *visited = ctx;
    return ++*visited == 2u ? 77 : 0; /* (P) caller-chosen nonzero code */
}

static int visit_errors(void)
{
    config_toml_scratch sc = big_scratch(); config_toml_error error;
    const char good[] = "a = 1\nb = 2\nc = 3\n";
    size_t visited = 0;
    T(config_toml_parse(BYTES(good), &sc, fail_on_second, &visited, &error) == 77);
    T(error.code == 77 && visited == 2u); /* visitor stopped the delivery pass */
    return 0;
}

static int arguments(void)
{
    config_toml_scratch sc = big_scratch(); config_toml_error error;
    size_t visited = 0;
    T(config_toml_parse(NULL, 0, &sc, count_only, &visited, &error) == CONFIG_TOML_OK);
    T(visited == 0);
    T(config_toml_parse(NULL, 1, &sc, count_only, &visited, &error) == CONFIG_TOML_ERR_ARG);
    /* (P) nonempty NULL span, v1 config_test.c:69 */
    const char src[] = "a=1";
    T(config_toml_parse(BYTES(src), &sc, NULL, &visited, &error) == CONFIG_TOML_ERR_ARG);
    T(config_toml_parse(BYTES(src), NULL, count_only, &visited, &error) == CONFIG_TOML_ERR_ARG);
    config_toml_scratch no_text = sc; no_text.text = NULL;
    T(config_toml_parse(BYTES(src), &no_text, count_only, &visited, &error) == CONFIG_TOML_ERR_ARG);
    config_toml_scratch no_cap = sc; no_cap.caps.name_cap = 0;
    T(config_toml_parse(BYTES(src), &no_cap, count_only, &visited, &error) == CONFIG_TOML_ERR_ARG);
    T(config_toml_parse(BYTES(src), &sc, count_only, &visited, NULL) == CONFIG_TOML_OK);
    T(visited == 1u); /* error output is optional */
    return 0;
}

static int limits(void)
{
    config_toml_scratch sc = small_scratch(); config_toml_error error;
    size_t visited = 0;
    /* Source: len == source_cap parses, source_cap + 1 is LIMIT before any
     * byte is read (v1 passed a fictitious length the same way). */
    const uint8_t one[] = {'a'};
    T(config_toml_parse(one, SMALL_SOURCE + 1u, &sc, count_only, &visited, &error)
        == CONFIG_TOML_ERR_LIMIT);
    /* Decoded text: text_cap - 1 bytes plus NUL exactly fills the buffer;
     * one more byte is LIMIT. (P) from SMALL_TEXT. */
    char text_src[SMALL_TEXT + sizeof "a=''"];
    strcpy(text_src, "a='"); memset(text_src + 3, 'x', SMALL_TEXT - 1u);
    strcpy(text_src + 3 + SMALL_TEXT - 1u, "'");
    sc.caps.source_cap = SIZE_MAX;
    T(config_toml_parse((const uint8_t *)text_src, strlen(text_src), &sc,
                        count_only, &visited, &error) == CONFIG_TOML_OK);
    memset(text_src + 3, 'x', SMALL_TEXT); strcpy(text_src + 3 + SMALL_TEXT, "'");
    T(config_toml_parse((const uint8_t *)text_src, strlen(text_src), &sc,
                        count_only, &visited, &error) == CONFIG_TOML_ERR_LIMIT);
    sc = small_scratch();
    /* Name: name_cap - 1 chars plus NUL fits; name_cap chars is LIMIT. */
    char key_src[SMALL_NAME + sizeof "=true"];
    memset(key_src, 'k', SMALL_NAME - 1u); strcpy(key_src + SMALL_NAME - 1u, "=true");
    T(config_toml_parse((const uint8_t *)key_src, strlen(key_src), &sc,
                        count_only, &visited, &error) == CONFIG_TOML_OK);
    memset(key_src, 'k', SMALL_NAME); strcpy(key_src + SMALL_NAME, "=true");
    T(config_toml_parse((const uint8_t *)key_src, strlen(key_src), &sc,
                        count_only, &visited, &error) == CONFIG_TOML_ERR_LIMIT);
    /* Entries: entry_cap assignments fit; one more is LIMIT. Keys k0..k9
     * are 2 chars, inside SMALL_NAME. */
    char many[256]; size_t written = 0;
    for (size_t i = 0; i < SMALL_ENTRY; i++)
        written += (size_t)snprintf(many + written, sizeof many - written, "k%zu=true\n", i);
    sc.caps.source_cap = SIZE_MAX;
    T(config_toml_parse((const uint8_t *)many, written, &sc, count_only, &visited, &error)
        == CONFIG_TOML_OK);
    written += (size_t)snprintf(many + written, sizeof many - written, "k%zu=true\n", (size_t)SMALL_ENTRY);
    T(config_toml_parse((const uint8_t *)many, written, &sc, count_only, &visited, &error)
        == CONFIG_TOML_ERR_LIMIT);
    /* String array: strings_cap elements fit; one more is LIMIT. */
    const char fits[] = "a=['x','y']", over[] = "a=['x','y','z']";
    T(config_toml_parse(BYTES(fits), &sc, count_only, &visited, &error) == CONFIG_TOML_OK);
    T(config_toml_parse(BYTES(over), &sc, count_only, &visited, &error) == CONFIG_TOML_ERR_LIMIT);
    return 0;
}

static int capture_float(void *ctx, const char *path, const config_toml_value *v)
{
    (void)path;
    T(v->type == CONFIG_TOML_FLOAT);
    *(double *)ctx = v->number;
    return 0;
}

static int float_rounding(void)
{
    /* (G) Inherited v1 behaviour, not correct rounding: digits accumulate
     * in a long double and scale by powl(10, e), so the result can sit
     * one ulp from strtod's correctly rounded value. Pin 0.002877, where
     * the two differ (M: this algorithm vs strtod, glibc powl, 2026-11-24):
     * v1's path yields 0x1.791819d2391d6p-9, the correctly rounded double
     * (and the C literal below) is one ulp less. Consumers to date use a
     * float as a magnitude (font points), never an exact-equality key. */
    config_toml_scratch sc = big_scratch(); config_toml_error error;
    const char src[] = "x = 0.002877";
    double parsed = 0;
    T(config_toml_parse(BYTES(src), &sc, capture_float, &parsed, &error) == CONFIG_TOML_OK);
    T(parsed == 0x1.791819d2391d6p-9);
    T(parsed != 0.002877);
    return 0;
}

static int mutations(void)
{
    /* (P) every byte position and every possible octet in a representative
     * settings document; no invented fuzz duration/iteration budget
     * (v1 config_test.c:86-96, at parser level). */
    const char seed[] = "theme='light'\n[font]\nsize=12.5\nfamily='Mono'\n";
    uint8_t mutated[sizeof seed - 1u];
    config_toml_scratch sc = big_scratch(); config_toml_error error;
    for (size_t at = 0; at < sizeof mutated; at++)
        for (unsigned byte = 0; byte <= UINT8_MAX; byte++) {
            memcpy(mutated, seed, sizeof mutated); mutated[at] = (uint8_t)byte;
            size_t visited = 0;
            int rc = config_toml_parse(mutated, sizeof mutated, &sc,
                                       count_only, &visited, &error);
            T(error.code == rc && error.offset <= sizeof mutated);
            if (rc) T(visited == 0);
        }
    return 0;
}

int main(void)
{
    T(good_document() == 0);
    T(bad_documents() == 0);
    T(error_positions() == 0);
    T(transactional() == 0);
    T(visit_errors() == 0);
    T(arguments() == 0);
    T(float_rounding() == 0);
    T(limits() == 0);
    T(mutations() == 0);
    puts("config_toml_test: TOML types, malformed input, transactional delivery, "
         "caller-sized limits and exhaustive single-byte mutations passed");
    return 0;
}
