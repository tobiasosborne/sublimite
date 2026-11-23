#ifndef CONFIG_TOML_H
#define CONFIG_TOML_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* config/toml: the TOML subset parser harvested from v1 src/config/parser.c
 * (LF-2, A21; docs/decisions/LF-2-toml.md). It owns no pool and allocates
 * nothing: v1's parser kept all state in fixed stack arrays, so here the
 * caller supplies that storage (config_toml_scratch) and its capacities.
 * No capacity derives from keys/actions (v1's CONFIG_ENTRY_CAP was
 * KEYS_ACTION_COUNT + 10); the settings bead that knows the action count
 * sizes the scratch.
 *
 * Supported (v1's subset): bare/quoted dotted keys, tables, single-line
 * basic/literal strings with Unicode escapes, decimal integers/floats,
 * booleans, multiline string arrays, comments. Duplicate keys/tables,
 * invalid UTF-8 (even inside comments; NUL is never EOF), numeric overflow
 * and unsupported TOML fail. No I/O, recursion, locale or globals.
 *
 * Floats convert as v1 did (G: inherited behaviour, kept for the
 * differential): digits accumulate in a long double, then scale by
 * powl(10, e). That is not correct rounding — a result can differ from
 * strtod's by one ulp (e.g. 0.002877, pinned in tests/config_toml_test.c).
 * Consumers to date use a float as a magnitude (font points), never as an
 * exact-equality key; a consumer that needs correct rounding must say so
 * in its bead, not assume it here.
 *
 * Transactional delivery: the document is validated whole before the first
 * visit call, so on any parser error (syntax, limit, value, argument) the
 * visitor has not run and the caller's output is untouched. A visitor that
 * returns nonzero stops delivery at that assignment and its code becomes
 * the result; earlier deliveries in that pass stand, so a caller needing
 * all-or-nothing against its own errors stages into fresh storage itself
 * (as v1's config_parse did). Borrowed paths and decoded strings in the
 * value are valid only during the callback. */

typedef enum config_toml_result {
    CONFIG_TOML_OK = 0, /* (P) zero is the C success return convention */
    CONFIG_TOML_ERR_SYNTAX,
    CONFIG_TOML_ERR_LIMIT,
    CONFIG_TOML_ERR_VALUE,
    CONFIG_TOML_ERR_ARG
} config_toml_result;

typedef enum config_toml_type {
    CONFIG_TOML_STRING,
    CONFIG_TOML_INTEGER,
    CONFIG_TOML_FLOAT,
    CONFIG_TOML_BOOLEAN,
    CONFIG_TOML_STRINGS
} config_toml_type;

typedef struct config_toml_value {
    config_toml_type type;
    int64_t integer;
    double number;
    bool boolean;
    const char *string;            /* CONFIG_TOML_STRING */
    const char *const *strings;    /* CONFIG_TOML_STRINGS, count entries */
    size_t count;
} config_toml_value;

/* code is the config_toml_result, or the visitor's nonzero return;
 * offset is the byte position of the first failure, always <= len. */
typedef struct config_toml_error {
    int code;
    size_t offset;
} config_toml_error;

typedef int (*config_toml_visit_fn)(void *ctx, const char *path,
                                    const config_toml_value *value);

/* Caller capacities. Every cap counts its object including the NUL where
 * one is stored: name_cap per dotted path, text_cap per value's decoded
 * strings (one array's strings share it, a NUL each), source_cap for the
 * whole document. entry_cap bounds assignments and table headers alike
 * (v1 bounded both by CONFIG_ENTRY_CAP). strings_cap bounds one array. */
typedef struct config_toml_caps {
    size_t name_cap;
    size_t entry_cap;
    size_t text_cap;
    size_t strings_cap;
    size_t source_cap;
} config_toml_caps;

/* Caller storage, sized by caps: table/path hold name_cap bytes each;
 * text holds text_cap; seen/declared hold entry_cap names of name_cap
 * bytes each (flat); scopes holds entry_cap; strings holds strings_cap
 * pointers. Contents are scratch: neither read on entry nor meaningful on
 * exit, and the regions must not overlap each other or the source. One
 * scratch serves one parse at a time. */
typedef struct config_toml_scratch {
    char *table;
    char *path;
    char *text;
    char *seen;
    char *declared;
    size_t *scopes;
    const char **strings;
    config_toml_caps caps;
} config_toml_scratch;

/* Parse bytes[0..len) and visit each assignment with its full dotted path.
 * bytes may be NULL only when len is 0 (an empty document). scratch, its
 * regions, visit and every cap except source_cap must be non-NULL/nonzero;
 * violations return CONFIG_TOML_ERR_ARG (v1's error contract) without
 * reading bytes. error may be NULL. */
int config_toml_parse(const uint8_t *bytes, size_t len,
                      config_toml_scratch *scratch,
                      config_toml_visit_fn visit, void *ctx,
                      config_toml_error *error);

#endif
