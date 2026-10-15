#include "find/options.h"

static bool letter(uint8_t byte)
{ return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z'); }
static uint8_t other_case(uint8_t byte)
{ return (uint8_t)(byte >= 'a' && byte <= 'z' ? byte - ('a' - 'A') : byte + ('a' - 'A')); }

/* The frozen core owns syntax validation. This transformer only rewrites
 * successfully validated expressions; escaped letters remain literal atoms,
 * and class negation is applied AFTER expanding ASCII case membership. */
typedef struct builder {
    const work_ctx *context;
    uint8_t *bytes;
    size_t length, units, capacity;
    find_code code;
} builder;
static void tick(builder *b)
{
    b->units++;
    if (b->units >= FIND_POLL_UNITS) {
        b->units = 0;
        if (b->context && work_should_stop(b->context)) b->code = FIND_CANCELLED;
    }
}
static void put(builder *b, uint8_t byte)
{
    tick(b);
    if (b->code != FIND_OK) return;
    if (b->length == b->capacity) { b->code = FIND_ERR_LIMIT; return; }
    b->bytes[b->length++] = byte;
}
static void class_atom(builder *b, uint8_t byte)
{
    if (byte == '[' || byte == ']' || byte == '\\' || byte == '-' || byte == '^') put(b, '\\');
    put(b, byte);
}
static void literal_atom(builder *b, uint8_t byte)
{
    if (letter(byte)) {
        put(b, '['); put(b, byte); put(b, other_case(byte)); put(b, ']');
    } else {
        if (byte == '[' || byte == ']' || byte == '\\' || byte == '(' || byte == ')' ||
            byte == '|' || byte == '.' || byte == '*' || byte == '+' || byte == '?' ||
            byte == '^' || byte == '$' || byte == '{' || byte == '}') put(b, '\\');
        put(b, byte);
    }
}
static uint8_t class_byte(const uint8_t *query, size_t *at)
{
    uint8_t byte = query[(*at)++];
    if (byte == '\\') byte = query[(*at)++];
    return byte;
}
static void folded_class(builder *b, const uint8_t *query, size_t *at)
{
    bool bits[256] = {false};
    bool negate = query[*at] == '^';
    if (negate) (*at)++;
    while (query[*at] != ']') {
        uint8_t low = class_byte(query, at), high = low;
        if (query[*at] == '-' && query[*at + 1] != ']') {
            (*at)++; high = class_byte(query, at);
        }
        for (unsigned v = low; v <= (unsigned)high; v++) { tick(b); bits[v] = true; }
    }
    (*at)++;
    for (unsigned v = 'a'; v <= (unsigned)'z'; v++) {
        unsigned upper = v - (unsigned)('a' - 'A');
        if (bits[v] || bits[upper]) { bits[v] = true; bits[upper] = true; }
    }
    put(b, '['); if (negate) put(b, '^');
    for (unsigned v = 0; v < 256; v++) {
        tick(b);
        if (!bits[v]) continue;
        unsigned end = v;
        while (end + 1 < 256 && bits[end + 1]) { tick(b); end++; }
        class_atom(b, (uint8_t)v);
        if (end - v >= 2) { put(b, '-'); class_atom(b, (uint8_t)end); }
        else if (end != v) class_atom(b, (uint8_t)end);
        v = end;
    }
    put(b, ']');
}
find_code find_options_compile(void *program, size_t program_bytes,
    const uint8_t *query, size_t query_length, bool match_case,
    uint8_t *pattern, size_t pattern_capacity, const work_ctx *context,
    find_regex **regex, size_t *error_offset)
{
    *regex = NULL;
    if (context && work_should_stop(context)) return FIND_CANCELLED;
    find_code code = find_regex_compile(program, program_bytes, query, query_length,
                                        regex, error_offset);
    if (code != FIND_OK || match_case) return code;
    builder b = {.context = context, .bytes = pattern,
                 .capacity = pattern_capacity, .code = FIND_OK};
    for (size_t at = 0; at < query_length && b.code == FIND_OK;) {
        uint8_t byte = query[at++];
        if (byte == '[') folded_class(&b, query, &at);
        else if (byte == '\\') literal_atom(&b, query[at++]);
        else if (letter(byte)) literal_atom(&b, byte);
        else put(&b, byte);
    }
    if (context && work_should_stop(context)) return FIND_CANCELLED;
    if (b.code != FIND_OK) return b.code;
    code = find_regex_compile(program, program_bytes, b.bytes, b.length,
                              regex, error_offset);
    if (code != FIND_OK && error_offset) *error_offset = query_length;
    return code;
}
