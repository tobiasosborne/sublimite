#include "config/toml.h"
#include "utf8/utf8.h"
#include <string.h>
#include <math.h>
#include <float.h>

/* Harvested from v1 src/config/parser.c (LF-2). Changes from v1, each in
 * docs/decisions/LF-2-toml.md: capacities and storage come from the caller
 * instead of CONFIG_*_CAP constants; the work_ctx cancellation poll (which
 * imported VIEW_SCAN_BOUND from view.h) is cut; delivery is a second pass
 * over the same bytes so a parser error reaches no visitor. Everything
 * else — grammar, first-error-wins offsets, numeric conversion — is v1's. */

typedef struct parser {
    const uint8_t *bytes;
    size_t len, at, entries, tables, used;
    config_toml_scratch *scratch;
    config_toml_visit_fn visit; /* NULL during the validation pass */
    void *ctx;
    config_toml_error error;
} parser;

static char *seen_at(parser *p, size_t i)
{
    return p->scratch->seen + i * p->scratch->caps.name_cap;
}
static char *declared_at(parser *p, size_t i)
{
    return p->scratch->declared + i * p->scratch->caps.name_cap;
}

static int fail(parser *p, int code)
{
    if (!p->error.code) p->error = (config_toml_error){code, p->at};
    return code;
}
static uint8_t peek(parser *p)
{
    return p->at < p->len ? p->bytes[p->at] : 0;
}
static bool digit(uint8_t c) { return c >= '0' && c <= '9'; }
static bool bare(uint8_t c)
{
    return digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
}
static void spaces(parser *p) { while (peek(p) == ' ' || peek(p) == '\t') p->at++; }
static void comment(parser *p)
{
    if (peek(p) == '#') while (p->at < p->len) {
        uint8_t c = peek(p);
        if (p->error.code || c == '\n' || c == '\r') break;
        p->at++;
    }
}
static int newline(parser *p)
{
    if (peek(p) == '\r') { p->at++; if (peek(p) != '\n') return fail(p, CONFIG_TOML_ERR_SYNTAX); }
    if (peek(p) == '\n') p->at++;
    return CONFIG_TOML_OK;
}
static int trivia(parser *p)
{
    for (;;) {
        spaces(p); comment(p);
        if (peek(p) != '\n' && peek(p) != '\r') return p->error.code;
        if (newline(p)) return p->error.code;
    }
}
static int append(parser *p, uint8_t c)
{
    if (p->used >= p->scratch->caps.text_cap) return fail(p, CONFIG_TOML_ERR_LIMIT);
    p->scratch->text[p->used++] = (char)c; return CONFIG_TOML_OK;
}
static int hex_digit(uint8_t c)
{
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int toml_string(parser *p, const char **out)
{
    uint8_t quote = peek(p); size_t start = p->used; p->at++;
    while (p->at < p->len && peek(p) != quote) {
        uint8_t c = peek(p); if (p->error.code) return p->error.code; p->at++;
        if (c < 0x20u && c != '\t') return fail(p, CONFIG_TOML_ERR_SYNTAX);
        if (c == 0x7fu) return fail(p, CONFIG_TOML_ERR_SYNTAX);
        if (c == '\\' && quote == '"') {
            if (p->at == p->len) return fail(p, CONFIG_TOML_ERR_SYNTAX);
            c = peek(p); p->at++;
            switch (c) {
            case 'b': c = '\b'; break;
            case 't': c = '\t'; break;
            case 'n': c = '\n'; break;
            case 'f': c = '\f'; break;
            case 'r': c = '\r'; break;
            case '"': case '\\': break;
            case 'u': case 'U': {
                /* (P) TOML Unicode escapes have four/eight hex digits. */
                size_t count = c == 'u' ? 4u : 8u; uint32_t cp = 0;
                for (size_t i = 0; i < count; i++) {
                    int h = hex_digit(peek(p));
                    if (p->at == p->len || h < 0) return fail(p, CONFIG_TOML_ERR_SYNTAX);
                    cp = (cp << 4) | (uint32_t)h; p->at++;
                }
                if (!cp || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu))
                    return fail(p, CONFIG_TOML_ERR_SYNTAX);
                uint8_t encoded[4]; size_t count_bytes = utf8_encode(cp, encoded);
                for (size_t i = 0; i < count_bytes; i++) if (append(p, encoded[i])) return p->error.code;
                continue;
            }
            default: return fail(p, CONFIG_TOML_ERR_SYNTAX);
            }
        }
        if (append(p, c)) return p->error.code;
    }
    if (p->at == p->len) return fail(p, CONFIG_TOML_ERR_SYNTAX);
    p->at++;
    if (append(p, 0)) return p->error.code;
    *out = p->scratch->text + start; return CONFIG_TOML_OK;
}
static int key(parser *p, char *out)
{
    size_t name_cap = p->scratch->caps.name_cap;
    size_t used = 0;
    for (;;) {
        spaces(p); size_t start = p->at, n; const char *s;
        if (peek(p) == '"' || peek(p) == '\'') {
            p->used = 0;
            if (toml_string(p, &s)) return p->error.code;
            n = strlen(s);
            /* Quoted keys are supported within the bare-key namespace;
             * literal dots in a segment are outside this subset. */
            for (size_t i = 0; i < n; i++) if (!bare((uint8_t)s[i])) return fail(p, CONFIG_TOML_ERR_SYNTAX);
        } else {
            while (bare(peek(p))) p->at++;
            if (p->error.code) return p->error.code;
            n = p->at - start; s = (const char *)p->bytes + start;
        }
        if (!n) return fail(p, CONFIG_TOML_ERR_SYNTAX);
        if (n >= name_cap - used) return fail(p, CONFIG_TOML_ERR_LIMIT);
        memcpy(out + used, s, n); used += n; out[used] = 0;
        spaces(p);
        if (peek(p) != '.') return CONFIG_TOML_OK;
        if (used + 1u >= name_cap) return fail(p, CONFIG_TOML_ERR_LIMIT);
        out[used++] = '.'; p->at++;
    }
}
static bool prefix(const char *a, const char *b)
{
    size_t n = strlen(a); return !strncmp(a, b, n) && b[n] == '.';
}
static int remember(parser *p, bool table)
{
    char *path = p->scratch->path;
    for (size_t i = 0; i < p->entries; i++) {
        const char *s = seen_at(p, i);
        if (!strcmp(s, path) || prefix(s, path) || (!table && prefix(path, s)))
            return fail(p, CONFIG_TOML_ERR_SYNTAX);
        /* A dotted key defines its intermediate tables. A parent introduced
         * solely by a child table header may still be declared later. */
        if (table && prefix(path, s) && strlen(path) > p->scratch->scopes[i])
            return fail(p, CONFIG_TOML_ERR_SYNTAX);
    }
    for (size_t i = 0; i < p->tables; i++) {
        if (!strcmp(declared_at(p, i), path) || (!table && prefix(path, declared_at(p, i))))
            return fail(p, CONFIG_TOML_ERR_SYNTAX);
        if (!table && prefix(declared_at(p, i), path) && strlen(declared_at(p, i)) > strlen(p->scratch->table))
            return fail(p, CONFIG_TOML_ERR_SYNTAX);
    }
    /* key() and the table-prefix join already bound path to name_cap
     * including its NUL; re-check at the copy so the bound, not the
     * invariant, is what the memcpy trusts (A15: input-producible
     * lengths are return codes). */
    size_t path_len = strlen(path);
    if (path_len >= p->scratch->caps.name_cap) return fail(p, CONFIG_TOML_ERR_LIMIT);
    if (table) {
        if (p->tables == p->scratch->caps.entry_cap) return fail(p, CONFIG_TOML_ERR_LIMIT);
        memcpy(declared_at(p, p->tables++), path, path_len + 1u);
    } else {
        if (p->entries == p->scratch->caps.entry_cap) return fail(p, CONFIG_TOML_ERR_LIMIT);
        p->scratch->scopes[p->entries] = strlen(p->scratch->table);
        memcpy(seen_at(p, p->entries++), path, path_len + 1u);
    }
    return CONFIG_TOML_OK;
}
static int digits(parser *p, char *buf, size_t *used)
{
    if (!digit(peek(p))) return fail(p, CONFIG_TOML_ERR_SYNTAX);
    while (digit(peek(p))) {
        if (*used >= p->scratch->caps.text_cap - 1u) return fail(p, CONFIG_TOML_ERR_LIMIT);
        buf[(*used)++] = (char)peek(p); p->at++;
        if (peek(p) == '_') { p->at++; if (!digit(peek(p))) return fail(p, CONFIG_TOML_ERR_SYNTAX); }
    }
    return CONFIG_TOML_OK;
}
static int number(parser *p, config_toml_value *v)
{
    /* The token buffer is the decoded-text scratch: a numeric value
     * produces no decoded strings, and v1 bounded the token by the same
     * CONFIG_TEXT_CAP. */
    char *buf = p->scratch->text;
    size_t text_cap = p->scratch->caps.text_cap;
    size_t n = 0; bool negative = false;
    if (peek(p) == '+' || peek(p) == '-') { negative = peek(p) == '-'; buf[n++] = (char)peek(p); p->at++; }
    size_t integer_start = n;
    if (digits(p, buf, &n)) return p->error.code;
    if (n - integer_start > 1u && buf[integer_start] == '0') return fail(p, CONFIG_TOML_ERR_SYNTAX);
    bool floating = false;
    if (peek(p) == '.') {
        floating = true;
        if (n >= text_cap - 1u) return fail(p, CONFIG_TOML_ERR_LIMIT);
        buf[n++] = '.'; p->at++;
        if (digits(p, buf, &n)) return p->error.code;
    }
    if (peek(p) == 'e' || peek(p) == 'E') {
        floating = true;
        if (n >= text_cap - 1u) return fail(p, CONFIG_TOML_ERR_LIMIT);
        buf[n++] = 'e'; p->at++;
        if (peek(p) == '+' || peek(p) == '-') {
            if (n >= text_cap - 1u) return fail(p, CONFIG_TOML_ERR_LIMIT);
            buf[n++] = (char)peek(p); p->at++;
        }
        if (digits(p, buf, &n)) return p->error.code;
    }
    if (!floating) {
        uint64_t value = 0, limit = (uint64_t)INT64_MAX + (negative ? 1u : 0u);
        for (size_t i = integer_start; i < n; i++) {
            uint64_t d = (uint64_t)(buf[i] - '0');
            if (value > (limit - d) / 10u) return fail(p, CONFIG_TOML_ERR_VALUE);
            value = value * 10u + d;
        }
        v->type = CONFIG_TOML_INTEGER;
        v->integer = negative ? value == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)value : (int64_t)value;
        return CONFIG_TOML_OK;
    }
    long double value = 0; long fraction = 0, exponent = 0; bool after_dot = false;
    size_t i = integer_start;
    for (; i < n && buf[i] != 'e'; i++) {
        if (buf[i] == '.') { after_dot = true; continue; }
        value = value * 10 + (buf[i] - '0');
        if (!isfinite(value)) return fail(p, CONFIG_TOML_ERR_VALUE);
        if (after_dot) fraction++;
    }
    if (i < n) {
        i++; bool minus = false;
        if (buf[i] == '+' || buf[i] == '-') { minus = buf[i] == '-'; i++; }
        for (; i < n; i++) {
            /* (P) bounded by long double exponent range plus decoded token
             * length; bigger exponents cannot compensate this mantissa. */
            if (exponent > (long)LDBL_MAX_10_EXP + (long)n) return fail(p, CONFIG_TOML_ERR_VALUE);
            exponent = exponent * 10 + buf[i] - '0';
        }
        if (minus) exponent = -exponent;
    }
    bool nonzero = value != 0;
    value *= powl(10, exponent - fraction);
    v->number = (double)(negative ? -value : value); v->type = CONFIG_TOML_FLOAT;
    if (!isfinite(v->number) || (nonzero && v->number == 0)) return fail(p, CONFIG_TOML_ERR_VALUE);
    return CONFIG_TOML_OK;
}
static int value(parser *p, config_toml_value *v)
{
    p->used = 0;
    if (peek(p) == '"' || peek(p) == '\'') { v->type = CONFIG_TOML_STRING; return toml_string(p, &v->string); }
    if (peek(p) == '[') {
        v->type = CONFIG_TOML_STRINGS; v->strings = p->scratch->strings; p->at++;
        if (trivia(p)) return p->error.code;
        while (peek(p) != ']') {
            if (v->count == p->scratch->caps.strings_cap) return fail(p, CONFIG_TOML_ERR_LIMIT);
            if (peek(p) != '"' && peek(p) != '\'') return fail(p, CONFIG_TOML_ERR_SYNTAX);
            if (toml_string(p, &p->scratch->strings[v->count++]) || trivia(p)) return p->error.code;
            if (peek(p) == ']') break;
            if (peek(p) != ',') return fail(p, CONFIG_TOML_ERR_SYNTAX);
            p->at++;
            if (trivia(p)) return p->error.code;
        }
        p->at++; return CONFIG_TOML_OK;
    }
    if (p->len - p->at >= 4u && !memcmp(p->bytes + p->at, "true", 4)) {
        v->type = CONFIG_TOML_BOOLEAN; v->boolean = true; p->at += 4u; return CONFIG_TOML_OK;
    }
    if (p->len - p->at >= 5u && !memcmp(p->bytes + p->at, "false", 5)) {
        v->type = CONFIG_TOML_BOOLEAN; p->at += 5u; return CONFIG_TOML_OK;
    }
    return number(p, v);
}

static int parse_pass(const uint8_t *bytes, size_t len, config_toml_scratch *scratch,
                      config_toml_visit_fn visit, void *ctx, config_toml_error *error_out)
{
    parser p = {.bytes = bytes, .len = len, .scratch = scratch, .visit = visit, .ctx = ctx};
    scratch->table[0] = 0;
    /* Validate UTF-8/control bytes even inside comments. NUL is never EOF. */
    for (size_t at = 0; at < len;) {
        utf8_step step = utf8_decode(bytes + at, len - at);
        if (!step.valid || (step.cp < 0x20u && step.cp != '\t' && step.cp != '\r' && step.cp != '\n') || step.cp == 0x7fu) {
            p.at = at; fail(&p, CONFIG_TOML_ERR_SYNTAX); goto done;
        }
        at += step.len;
    }
    p.at = 0;
    while (p.at < len && !p.error.code) {
        if (trivia(&p) || p.at == len) break;
        bool table = peek(&p) == '[';
        if (table) p.at++;
        if (key(&p, scratch->path)) break;
        if (table) {
            if (peek(&p) != ']') { fail(&p, CONFIG_TOML_ERR_SYNTAX); break; }
            p.at++;
            if (remember(&p, true)) break;
            /* Same bound as remember(): path fits name_cap with its NUL,
             * re-checked here so the copy trusts the check, not key(). */
            size_t path_len = strlen(scratch->path);
            if (path_len >= scratch->caps.name_cap) { fail(&p, CONFIG_TOML_ERR_LIMIT); break; }
            memcpy(scratch->table, scratch->path, path_len + 1u);
        } else {
            if (*scratch->table) {
                size_t a = strlen(scratch->table), b = strlen(scratch->path);
                if (a + b + 1u >= scratch->caps.name_cap) { fail(&p, CONFIG_TOML_ERR_LIMIT); break; }
                memmove(scratch->path + a + 1u, scratch->path, b + 1u); memcpy(scratch->path, scratch->table, a); scratch->path[a] = '.';
            }
            if (remember(&p, false)) break;
            if (peek(&p) != '=') { fail(&p, CONFIG_TOML_ERR_SYNTAX); break; }
            p.at++; spaces(&p); config_toml_value v = {0};
            if (value(&p, &v) || p.error.code) break;
            /* Validate the complete assignment before handing it out. */
            spaces(&p); comment(&p);
            if (p.error.code) break;
            if (p.at < len && peek(&p) != '\r' && peek(&p) != '\n') { fail(&p, CONFIG_TOML_ERR_SYNTAX); break; }
            if (p.visit) {
                int rc = p.visit(p.ctx, scratch->path, &v);
                if (rc) { fail(&p, rc); break; }
            }
        }
        spaces(&p); comment(&p);
        if (p.at < len && peek(&p) != '\r' && peek(&p) != '\n') { fail(&p, CONFIG_TOML_ERR_SYNTAX); break; }
        if (newline(&p)) break;
    }
done:
    *error_out = p.error;
    return p.error.code;
}

int config_toml_parse(const uint8_t *bytes, size_t len, config_toml_scratch *scratch,
                      config_toml_visit_fn visit, void *ctx, config_toml_error *error)
{
    if (error) *error = (config_toml_error){0};
    if ((!bytes && len) || !visit || !scratch || !scratch->table || !scratch->path ||
        !scratch->text || !scratch->seen || !scratch->declared || !scratch->scopes ||
        !scratch->strings || !scratch->caps.name_cap || !scratch->caps.entry_cap ||
        !scratch->caps.text_cap || !scratch->caps.strings_cap ||
        scratch->caps.entry_cap > SIZE_MAX / scratch->caps.name_cap) {
        if (error) error->code = CONFIG_TOML_ERR_ARG;
        return CONFIG_TOML_ERR_ARG;
    }
    if (len > scratch->caps.source_cap) {
        if (error) error->code = CONFIG_TOML_ERR_LIMIT;
        return CONFIG_TOML_ERR_LIMIT;
    }
    /* Pass 1 validates the whole document; pass 2 repeats the identical
     * deterministic parse with delivery enabled, so only the visitor can
     * fail it. A parser error therefore reaches no visitor. */
    config_toml_error result = {0};
    int rc = parse_pass(bytes, len, scratch, NULL, NULL, &result);
    if (!rc) rc = parse_pass(bytes, len, scratch, visit, ctx, &result);
    if (error) *error = result;
    return rc;
}
