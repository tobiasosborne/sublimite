#ifndef EDIT_CONFIG_H
#define EDIT_CONFIG_H
#include "keys/keys.h"
#include "view/view.h"

/* (P) PATH_MAX on the supported Linux target; keys/families share this
 * decoded text bound. Entry/array limits derive from all actions plus the
 * scalar settings: font size/family/file, theme, tabs width/spaces, wrap,
 * trailing whitespace (edit-hgd), plus the font.fallbacks array (edit-6e2). */
#define CONFIG_TEXT_CAP 4096u
/* (P) longest supported action path plus its NUL. Generic keys use the same
 * explicit subset limit; adding a longer action extends this bound. */
#define CONFIG_NAME_CAP sizeof("keymap.split_selection_lines")
#define CONFIG_ENTRY_CAP ((size_t)KEYS_ACTION_COUNT + 10u) /* (P) named scalars above (8 incl. whitespace.trailing) + menu.visible + font.fallbacks */
#define CONFIG_BINDING_CAP ((size_t)KEYS_ACTION_COUNT)
/* (P) maximum serialized settings: each entry can contain a path bounded by
 * CONFIG_NAME_CAP and a value bounded by CONFIG_TEXT_CAP; escape expansion is
 * ten source bytes per decoded ASCII byte (TOML \U form: slash + U + eight
 * hex digits). This explicit subset bound also bounds whitespace/comments. */
#define CONFIG_SOURCE_CAP (CONFIG_ENTRY_CAP * (CONFIG_NAME_CAP + CONFIG_TEXT_CAP) * 10u)
enum { CONFIG_OK, CONFIG_ERR_SYNTAX, CONFIG_ERR_LIMIT, CONFIG_ERR_VALUE,
       CONFIG_ERR_IO, CONFIG_ERR_BUSY, CONFIG_ERR_ARG };
typedef enum config_type { CONFIG_STRING, CONFIG_INTEGER, CONFIG_FLOAT,
                           CONFIG_BOOLEAN, CONFIG_STRINGS } config_type;
typedef struct config_value {
    config_type type;
    int64_t integer;
    double number;
    bool boolean;
    const char *string;
    const char *strings[CONFIG_BINDING_CAP];
    size_t count;
} config_value;
typedef struct config_error { int code; size_t offset; } config_error;
/* Borrowed decoded strings last only for the callback. Unknown settings can
 * be visited. A failed parse may already have called the visitor; config_parse
 * itself is transactional. No allocation, recursion, locale or I/O. Supported:
 * bare/quoted dotted keys, tables, single-line basic/literal strings (Unicode
 * escapes), decimal integers/floats, booleans, multiline string arrays/comments.
 * Duplicate keys/tables, invalid UTF-8, overflow and unsupported TOML fail. */
typedef int (*config_visit_fn)(void *, const char *, const config_value *);
int config_toml_parse(const uint8_t *, size_t, config_visit_fn, void *, config_error *);
typedef enum config_theme { CONFIG_DARK, CONFIG_LIGHT } config_theme;
typedef enum config_trailing { CONFIG_TRAILING_NONE, CONFIG_TRAILING_ALL,
                               CONFIG_TRAILING_SELECTION } config_trailing;
typedef struct config_settings {
    double font_points; /* zero = use DPI consumer's default; never pixels */
    char font_family[CONFIG_TEXT_CAP]; /* empty = font consumer default */
    char font_file[CONFIG_TEXT_CAP];   /* optional path with trailing #TTC-index */
    /* edit-6e2: ordered fallback entries joined by '\n' (no entry contains
     * one). (P) The parser decodes every string of one array into a single
     * CONFIG_TEXT_CAP buffer with a NUL per entry, so the joined form fits.
     * Empty = per-scalar fontconfig discovery only. */
    char font_fallbacks[CONFIG_TEXT_CAP];
    config_theme theme;
    config_trailing trailing;
    bool trailing_warning; /* invalid value accepted as none; runtime reports it */
    uint32_t tab_width; /* zero = per-buffer detected/default indentation */
    bool tab_spaces, tabs_set, wrap, wrap_set;
    bool menu_visible; /* edit-yey: menu.visible, default false (hidden) */
    keys_binding bindings[CONFIG_BINDING_CAP];
    size_t binding_count;
} config_settings;
void config_defaults(config_settings *);
/* Invalid whitespace.trailing accepts none and sets trailing_warning.
 * Syntax/other value errors remain transactional failures. */
int config_parse(const uint8_t *, size_t, config_settings *, config_error *);
int config_path(char *, size_t);
keys_result config_keys_lookup(const config_settings *, keys_state *, uint32_t,
                               uint16_t, const keys_binding **);
/* Idle view only; caller relayouts in the same frame generation. */
int config_apply_view(const config_settings *, view *, struct layout *);
typedef struct config_token { const char *name; uint32_t rgb; } config_token;
/* Provisional names/colors until docs/design/visual-spec.md is available and
 * approved. RGB values are palette data, not measured contrast claims. */
const config_token *config_theme_tokens(config_theme, size_t *);
uint32_t config_theme_color(config_theme, const char *);

typedef struct config_runtime config_runtime;
/* UI-owned, allocating setup/close outside typing. path=NULL uses XDG with
 * HOME fallback. Worker reads/parses; work mailbox installs immutable results.
 * A private worker pool keeps config I/O independent of editor bulk work. */
int config_runtime_open(config_runtime **, const char *);
void config_runtime_close(config_runtime *);
int config_runtime_eventfd(const config_runtime *);
int config_runtime_watchfd(const config_runtime *);
/* Off typing: polls shared file inotify, drains config's own mailbox, retries
 * saturation. No sleep/debounce/frame delay; valid publication becomes current
 * in this call. Returns true if the caller should relayout this generation. */
bool config_runtime_tick(config_runtime *);
const config_settings *config_runtime_current(const config_runtime *);
uint64_t config_runtime_generation(const config_runtime *);
/* Includes CONFIG_ERR_VALUE for an accepted trailing-whitespace fallback. */
config_error config_runtime_error(const config_runtime *);
bool config_runtime_busy(const config_runtime *);
#endif
