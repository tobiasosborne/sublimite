#include "fif/private.h"
#include <string.h>

static bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static fif_code parse_list(fif_filter *filter, const char *list, bool excluded)
{
    if (!list) return FIF_OK;
    size_t length = strnlen(list, FIF_PATH_BYTES);
    if (length == FIF_PATH_BYTES) return FIF_ERR_FILTER;
    for (size_t at = 0; at < length;) {
        size_t first = at;
        while (at < length && list[at] != ',') at++;
        size_t end = at;
        if (at < length) at++;
        while (first < end && space(list[first])) first++;
        while (end > first && space(list[end - 1u])) end--;
        bool negative = excluded;
        if (first < end && (list[first] == '-' || list[first] == '+')) {
            negative = negative || list[first] == '-'; first++;
            while (first < end && space(list[first])) first++;
        }
        if (first == end) continue;
        while (end > first + 1u && list[end - 1u] == '/') end--;
        while (end > first + 1u && list[first] == '.' && list[first + 1u] == '/') first += 2u;
        size_t n = end - first;
        if (filter->count == FIF_MAX_RULES || n + 1u > sizeof filter->text - filter->used)
            return FIF_ERR_FILTER;
        fif_rule *rule = &filter->rules[filter->count++];
        *rule = (fif_rule){.offset = filter->used, .exclude = negative};
        memcpy(filter->text + filter->used, list + first, n);
        filter->text[filter->used + n] = 0;
        rule->glob = strpbrk(filter->text + filter->used, "*?[") != NULL;
        rule->slash = memchr(list + first, '/', n) != NULL;
        filter->used += n + 1u;
        if (!negative) filter->includes++;
    }
    return FIF_OK;
}
fif_code fif_filter_parse(fif_filter *filter, const char *include, const char *exclude)
{
    if (!filter) return FIF_ERR_ARGUMENT;
    memset(filter, 0, sizeof *filter);
    fif_code code = parse_list(filter, include, false);
    if (code == FIF_OK) code = parse_list(filter, exclude, true);
    if (code != FIF_OK) memset(filter, 0, sizeof *filter);
    return code;
}
static bool atom(const char *pattern, size_t *at, unsigned char byte, const work_ctx *context)
{
    unsigned char c = (unsigned char)pattern[(*at)++];
    if (c == '?') return byte != '/';
    if (c == '\\' && pattern[*at]) return byte == (unsigned char)pattern[(*at)++];
    if (c != '[') return c == byte;
    size_t begin = *at;
    bool negate = pattern[*at] == '!' || pattern[*at] == '^', matched = false;
    if (negate) (*at)++;
    while (pattern[*at] && pattern[*at] != ']') {
        if (*at % FIND_POLL_UNITS == 0 && context && work_should_stop(context)) return false;
        unsigned char lo = (unsigned char)pattern[(*at)++], hi = lo;
        if (lo == '\\' && pattern[*at]) lo = (unsigned char)pattern[(*at)++];
        if (pattern[*at] == '-' && pattern[*at + 1u] && pattern[*at + 1u] != ']') {
            (*at)++; hi = (unsigned char)pattern[(*at)++];
            if (hi == '\\' && pattern[*at]) hi = (unsigned char)pattern[(*at)++];
        } else hi = lo;
        if (byte >= lo && byte <= hi) matched = true;
    }
    if (!pattern[*at]) { *at = begin; return byte == '['; }
    (*at)++;
    return byte != '/' && (negate ? !matched : matched);
}
static bool glob_match(const char *pattern, const char *path, const work_ctx *context)
{
    if (!pattern || !path) return false;
    size_t pn = strnlen(pattern, FIF_PATH_BYTES), n = strnlen(path, FIF_PATH_BYTES);
    if (pn == FIF_PATH_BYTES || n == FIF_PATH_BYTES) return false;
    /* (E) Two rows of PATH_MAX booleans: the anchored glob DP needs only its
     * previous row. Polynomial bounded work, no recursion or heap allocation. */
    bool rows[2][PATH_MAX] = {{false}};
    size_t row = 0; rows[row][0] = true;
    for (size_t p = 0; p < pn;) {
        if (context && work_should_stop(context)) return false;
        bool *old = rows[row]; row ^= 1u; bool *next = rows[row];
        memset(next, 0, (n + 1u) * sizeof *next);
        if (pattern[p] == '*') {
            bool recursive = p + 1u < pn && pattern[p + 1u] == '*';
            p += recursive ? 2u : 1u;
            bool directory = recursive && pattern[p] == '/';
            if (directory) p++;
            bool reachable = old[0]; next[0] = old[0];
            for (size_t i = 1; i <= n; i++) {
                if (i % FIND_POLL_UNITS == 0 && context && work_should_stop(context)) return false;
                if (directory) {
                    next[i] = old[i] || (reachable && path[i - 1u] == '/');
                    reachable = reachable || old[i];
                } else next[i] = old[i] || (next[i - 1u] && (recursive || path[i - 1u] != '/'));
            }
        } else {
            size_t end = p;
            (void)atom(pattern, &end, 0, context);
            for (size_t i = 1; i <= n; i++) {
                if (i % FIND_POLL_UNITS == 0 && context && work_should_stop(context)) return false;
                size_t at = p;
                next[i] = old[i - 1u] && atom(pattern, &at, (unsigned char)path[i - 1u], context);
            }
            p = end;
        }
    }
    return rows[row][n];
}
bool fif_glob_match(const char *pattern, const char *path)
{ return glob_match(pattern, path, NULL); }
static bool rule_match(const fif_filter *filter, const fif_rule *rule, const char *path, const work_ctx *context)
{
    const char *pattern = filter->text + rule->offset;
    if (rule->glob) {
        const char *name = rule->slash ? path : strrchr(path, '/');
        if (!name) name = path;
        else if (!rule->slash) name++;
        return glob_match(pattern, name, context);
    }
    if (!strcmp(pattern, ".") || !strcmp(pattern, "./")) return true;
    size_t n = strlen(pattern);
    if (strncmp(pattern, path, n) == 0 && (path[n] == 0 || path[n] == '/')) return true;
    /* A plain basename exclusion also prunes a directory of that name. */
    if (!rule->slash) {
        for (const char *at = path; (at = strchr(at, '/')) != NULL; at++)
            if (!strncmp(at + 1, pattern, n) && (at[1u + n] == 0 || at[1u + n] == '/')) return true;
    }
    return false;
}
bool fif_filter_excludes(const fif_filter *filter, const char *path, const work_ctx *context)
{
    for (size_t i = 0; i < filter->count; i++)
        if (filter->rules[i].exclude && rule_match(filter, &filter->rules[i], path, context)) return true;
    return false;
}
bool fif_filter_matches_work(const fif_filter *filter, const char *path, const work_ctx *context)
{
    if (!filter || !path || strnlen(path, FIF_PATH_BYTES) == FIF_PATH_BYTES) return false;
    bool included = filter->includes == 0;
    for (size_t i = 0; i < filter->count; i++) {
        if (!rule_match(filter, &filter->rules[i], path, context)) continue;
        if (filter->rules[i].exclude) return false;
        included = true;
    }
    return included;
}

bool fif_filter_matches(const fif_filter *filter, const char *path)
{ return fif_filter_matches_work(filter, path, NULL); }
