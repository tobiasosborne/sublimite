#include "config/private.h"
#include "utf8/utf8.h"
#include <string.h>
#include <math.h>
#include <float.h>

typedef struct parser {
    const uint8_t *bytes;
    size_t len, at, entries, tables, used;
    char table[CONFIG_NAME_CAP], path[CONFIG_NAME_CAP], text[CONFIG_TEXT_CAP];
    char seen[CONFIG_ENTRY_CAP][CONFIG_NAME_CAP];
    size_t scopes[CONFIG_ENTRY_CAP]; /* explicit table prefix at assignment */
    char declared[CONFIG_ENTRY_CAP][CONFIG_NAME_CAP];
    config_visit_fn visit;
    void *ctx;
    config_error error;
    const work_ctx *control;
    size_t next_check;
} parser;
static int fail(parser *p, int code)
{
    if (!p->error.code) p->error = (config_error){code, p->at};
    return code;
}
static uint8_t peek(parser *p)
{
    if (p->control && p->at >= p->next_check) {
        /* (P) reuse view's existing byte-work boundary, not a new duration.
         * UTF-8 decoding advances across it; >= also handles such crossings. */
        p->next_check = p->at + VIEW_SCAN_BOUND;
        if (work_should_stop(p->control)) {
            fail(p, CONFIG_ERR_BUSY); p->at = p->len; return 0;
        }
    }
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
    if (peek(p) == '\r') { p->at++; if (peek(p) != '\n') return fail(p, CONFIG_ERR_SYNTAX); }
    if (peek(p) == '\n') p->at++;
    return CONFIG_OK;
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
    if (p->used >= sizeof p->text) return fail(p, CONFIG_ERR_LIMIT);
    p->text[p->used++] = (char)c; return CONFIG_OK;
}
static int hex(uint8_t c)
{
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int string(parser *p, const char **out)
{
    uint8_t quote = peek(p); size_t start = p->used; p->at++;
    while (p->at < p->len && peek(p) != quote) {
        uint8_t c = peek(p); if (p->error.code) return p->error.code; p->at++;
        if (c < 0x20u && c != '\t') return fail(p, CONFIG_ERR_SYNTAX);
        if (c == 0x7fu) return fail(p, CONFIG_ERR_SYNTAX);
        if (c == '\\' && quote == '"') {
            if (p->at == p->len) return fail(p, CONFIG_ERR_SYNTAX);
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
                    int h = hex(peek(p));
                    if (p->at == p->len || h < 0) return fail(p, CONFIG_ERR_SYNTAX);
                    cp = (cp << 4) | (uint32_t)h; p->at++;
                }
                if (!cp || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu))
                    return fail(p, CONFIG_ERR_SYNTAX);
                uint8_t encoded[4]; size_t count_bytes = utf8_encode(cp, encoded);
                for (size_t i = 0; i < count_bytes; i++) if (append(p, encoded[i])) return p->error.code;
                continue;
            }
            default: return fail(p, CONFIG_ERR_SYNTAX);
            }
        }
        if (append(p, c)) return p->error.code;
    }
    if (p->at == p->len) return fail(p, CONFIG_ERR_SYNTAX);
    p->at++;
    if (append(p, 0)) return p->error.code;
    *out = p->text + start; return CONFIG_OK;
}
static int key(parser *p, char *out)
{
    size_t used = 0;
    for (;;) {
        spaces(p); size_t start = p->at, n; const char *s;
        if (peek(p) == '"' || peek(p) == '\'') {
            p->used = 0;
            if (string(p, &s)) return p->error.code;
            n = strlen(s);
            /* Quoted keys are supported within the bare-key namespace;
             * literal dots in a segment are outside this subset. */
            for (size_t i = 0; i < n; i++) if (!bare((uint8_t)s[i])) return fail(p, CONFIG_ERR_SYNTAX);
        } else {
            while (bare(peek(p))) p->at++;
            if (p->error.code) return p->error.code;
            n = p->at - start; s = (const char *)p->bytes + start;
        }
        if (!n) return fail(p, CONFIG_ERR_SYNTAX);
        if (n >= CONFIG_NAME_CAP - used) return fail(p, CONFIG_ERR_LIMIT);
        memcpy(out + used, s, n); used += n; out[used] = 0;
        spaces(p);
        if (peek(p) != '.') return CONFIG_OK;
        if (used + 1u >= CONFIG_NAME_CAP) return fail(p, CONFIG_ERR_LIMIT);
        out[used++] = '.'; p->at++;
    }
}
static bool prefix(const char *a, const char *b)
{
    size_t n = strlen(a); return !strncmp(a, b, n) && b[n] == '.';
}
static int remember(parser *p, bool table)
{
    for (size_t i = 0; i < p->entries; i++) {
        const char *s = p->seen[i];
        if (!strcmp(s, p->path) || prefix(s, p->path) || (!table && prefix(p->path, s)))
            return fail(p, CONFIG_ERR_SYNTAX);
        /* A dotted key defines its intermediate tables. A parent introduced
         * solely by a child table header may still be declared later. */
        if (table && prefix(p->path, s) && strlen(p->path) > p->scopes[i])
            return fail(p, CONFIG_ERR_SYNTAX);
    }
    for (size_t i = 0; i < p->tables; i++) {
        if (!strcmp(p->declared[i], p->path) || (!table && prefix(p->path, p->declared[i])))
            return fail(p, CONFIG_ERR_SYNTAX);
        if (!table && prefix(p->declared[i], p->path) && strlen(p->declared[i]) > strlen(p->table))
            return fail(p, CONFIG_ERR_SYNTAX);
    }
    if (table) {
        if (p->tables == CONFIG_ENTRY_CAP) return fail(p, CONFIG_ERR_LIMIT);
        strcpy(p->declared[p->tables++], p->path);
    } else {
        if (p->entries == CONFIG_ENTRY_CAP) return fail(p, CONFIG_ERR_LIMIT);
        p->scopes[p->entries] = strlen(p->table);
        strcpy(p->seen[p->entries++], p->path);
    }
    return CONFIG_OK;
}
static int digits(parser *p, char *buf, size_t *used)
{
    if (!digit(peek(p))) return fail(p, CONFIG_ERR_SYNTAX);
    while (digit(peek(p))) {
        if (*used >= CONFIG_TEXT_CAP - 1u) return fail(p, CONFIG_ERR_LIMIT);
        buf[(*used)++] = (char)peek(p); p->at++;
        if (peek(p) == '_') { p->at++; if (!digit(peek(p))) return fail(p, CONFIG_ERR_SYNTAX); }
    }
    return CONFIG_OK;
}
static int number(parser *p, config_value *v)
{
    char buf[CONFIG_TEXT_CAP]; size_t n = 0; bool negative = false;
    if (peek(p) == '+' || peek(p) == '-') { negative = peek(p) == '-'; buf[n++] = (char)peek(p); p->at++; }
    size_t integer_start = n;
    if (digits(p, buf, &n)) return p->error.code;
    if (n - integer_start > 1u && buf[integer_start] == '0') return fail(p, CONFIG_ERR_SYNTAX);
    bool floating = false;
    if (peek(p) == '.') {
        floating = true;
        if (n >= sizeof buf - 1u) return fail(p, CONFIG_ERR_LIMIT);
        buf[n++] = '.'; p->at++;
        if (digits(p, buf, &n)) return p->error.code;
    }
    if (peek(p) == 'e' || peek(p) == 'E') {
        floating = true;
        if (n >= sizeof buf - 1u) return fail(p, CONFIG_ERR_LIMIT);
        buf[n++] = 'e'; p->at++;
        if (peek(p) == '+' || peek(p) == '-') {
            if (n >= sizeof buf - 1u) return fail(p, CONFIG_ERR_LIMIT);
            buf[n++] = (char)peek(p); p->at++;
        }
        if (digits(p, buf, &n)) return p->error.code;
    }
    if (!floating) {
        uint64_t value = 0, limit = (uint64_t)INT64_MAX + (negative ? 1u : 0u);
        for (size_t i = integer_start; i < n; i++) {
            uint64_t d = (uint64_t)(buf[i] - '0');
            if (value > (limit - d) / 10u) return fail(p, CONFIG_ERR_VALUE);
            value = value * 10u + d;
        }
        v->type = CONFIG_INTEGER;
        v->integer = negative ? value == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)value : (int64_t)value;
        return CONFIG_OK;
    }
    long double value = 0; int fraction = 0, exponent = 0; bool after_dot = false;
    size_t i = integer_start;
    for (; i < n && buf[i] != 'e'; i++) {
        if (buf[i] == '.') { after_dot = true; continue; }
        value = value * 10 + (buf[i] - '0');
        if (!isfinite(value)) return fail(p, CONFIG_ERR_VALUE);
        if (after_dot) fraction++;
    }
    if (i < n) {
        i++; bool minus = false;
        if (buf[i] == '+' || buf[i] == '-') { minus = buf[i] == '-'; i++; }
        for (; i < n; i++) {
            /* (P) bounded by long double exponent range plus decoded token
             * length; bigger exponents cannot compensate this mantissa. */
            if (exponent > LDBL_MAX_10_EXP + (int)CONFIG_TEXT_CAP) return fail(p, CONFIG_ERR_VALUE);
            exponent = exponent * 10 + buf[i] - '0';
        }
        if (minus) exponent = -exponent;
    }
    bool nonzero = value != 0;
    value *= powl(10, exponent - fraction);
    v->number = (double)(negative ? -value : value); v->type = CONFIG_FLOAT;
    if (!isfinite(v->number) || (nonzero && v->number == 0)) return fail(p, CONFIG_ERR_VALUE);
    return CONFIG_OK;
}
static int value(parser *p, config_value *v)
{
    p->used = 0;
    if (peek(p) == '"' || peek(p) == '\'') { v->type = CONFIG_STRING; return string(p, &v->string); }
    if (peek(p) == '[') {
        v->type = CONFIG_STRINGS; p->at++;
        if (trivia(p)) return p->error.code;
        while (peek(p) != ']') {
            if (v->count == CONFIG_BINDING_CAP) return fail(p, CONFIG_ERR_LIMIT);
            if (peek(p) != '"' && peek(p) != '\'') return fail(p, CONFIG_ERR_SYNTAX);
            if (string(p, &v->strings[v->count++]) || trivia(p)) return p->error.code;
            if (peek(p) == ']') break;
            if (peek(p) != ',') return fail(p, CONFIG_ERR_SYNTAX);
            p->at++;
            if (trivia(p)) return p->error.code;
        }
        p->at++; return CONFIG_OK;
    }
    if (p->len - p->at >= 4u && !memcmp(p->bytes + p->at, "true", 4)) {
        v->type = CONFIG_BOOLEAN; v->boolean = true; p->at += 4u; return CONFIG_OK;
    }
    if (p->len - p->at >= 5u && !memcmp(p->bytes + p->at, "false", 5)) {
        v->type = CONFIG_BOOLEAN; p->at += 5u; return CONFIG_OK;
    }
    return number(p, v);
}
int config_toml_parse_worker(const uint8_t *bytes, size_t len, config_visit_fn visit, void *ctx,
                             config_error *error, const work_ctx *control)
{
    if (error) *error = (config_error){0};
    if ((!bytes && len) || !visit) { if (error) error->code = CONFIG_ERR_ARG; return CONFIG_ERR_ARG; }
    if (len > CONFIG_SOURCE_CAP) { if (error) error->code = CONFIG_ERR_LIMIT; return CONFIG_ERR_LIMIT; }
    if (control && work_should_stop(control)) {
        if (error) error->code = CONFIG_ERR_BUSY;
        return CONFIG_ERR_BUSY;
    }
    parser p = {.bytes = bytes, .len = len, .visit = visit, .ctx = ctx, .control = control};
    /* Validate UTF-8/control bytes even inside comments. NUL is never EOF. */
    for (size_t at = 0; at < len;) {
        p.at = at; (void)peek(&p); if (p.error.code) goto done;
        utf8_step step = utf8_decode(bytes + at, len - at);
        if (!step.valid || (step.cp < 0x20u && step.cp != '\t' && step.cp != '\r' && step.cp != '\n') || step.cp == 0x7fu) {
            p.at = at; fail(&p, CONFIG_ERR_SYNTAX); goto done;
        }
        at += step.len;
    }
    p.at = 0; p.next_check = 0;
    while (p.at < len && !p.error.code) {
        if (trivia(&p) || p.at == len) break;
        bool table = peek(&p) == '[';
        if (table) p.at++;
        if (key(&p, p.path)) break;
        if (table) {
            if (peek(&p) != ']') { fail(&p, CONFIG_ERR_SYNTAX); break; }
            p.at++;
            if (remember(&p, true)) break;
            strcpy(p.table, p.path);
        } else {
            if (*p.table) {
                size_t a = strlen(p.table), b = strlen(p.path);
                if (a + b + 1u >= sizeof p.path) { fail(&p, CONFIG_ERR_LIMIT); break; }
                memmove(p.path + a + 1u, p.path, b + 1u); memcpy(p.path, p.table, a); p.path[a] = '.';
            }
            if (remember(&p, false)) break;
            if (peek(&p) != '=') { fail(&p, CONFIG_ERR_SYNTAX); break; }
            p.at++; spaces(&p); config_value v = {0};
            if (value(&p, &v) || p.error.code) break;
            /* Validate the complete assignment before handing it out. */
            spaces(&p); comment(&p);
            if (p.error.code) break;
            if (p.at < len && peek(&p) != '\r' && peek(&p) != '\n') { fail(&p, CONFIG_ERR_SYNTAX); break; }
            int rc = p.visit(p.ctx, p.path, &v);
            if (rc) { fail(&p, rc); break; }
        }
        spaces(&p); comment(&p);
        if (p.at < len && peek(&p) != '\r' && peek(&p) != '\n') { fail(&p, CONFIG_ERR_SYNTAX); break; }
        if (newline(&p)) break;
    }
done:
    if (error) *error = p.error;
    return p.error.code;
}
int config_toml_parse(const uint8_t *bytes, size_t len, config_visit_fn visit, void *ctx, config_error *error)
{
    return config_toml_parse_worker(bytes, len, visit, ctx, error, NULL);
}
