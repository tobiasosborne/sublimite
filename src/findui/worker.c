#include "findui/private.h"
#include "find/visit.h"
#include <string.h>
#include <time.h>

static void scan_trace(const findui_slot *slot, uint32_t generation)
{ if (slot->hook) slot->hook(slot->hook_user, generation); }

/* Retrying a full mailbox preserves exact counts/ranges. Sleeping here uses
 * no CPU slice and is interrupted logically by the next cancellation poll.
 * No locks, UI callbacks or UI-owned result storage are touched by the job. */
static bool publish(work_ctx *context, const work_msg *message)
{
    while (!work_should_stop(context)) {
        if (work_publish(context, message)) return true;
        const struct timespec delay = {0, 100000};
        (void)nanosleep(&delay, NULL);
    }
    return false;
}
static bool flush(work_ctx *context, findui_batch *batch, size_t *count)
{
    if (!*count) return true;
    work_msg message = {.kind = *count == 1 ? FINDUI_MSG_ONE : FINDUI_MSG_TWO,
                        .generation = context->generation};
    memcpy(message.data, batch, sizeof *batch);
    *count = 0;
    return publish(context, &message);
}
static bool intersects(findui_range range, uint64_t start, uint64_t end)
{
    return range.start == range.end ? range.start >= start && range.start <= end
                                   : start < end && range.start < end && range.end > start;
}
static bool letter(uint8_t byte)
{ return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z'); }
static uint8_t other_case(uint8_t byte)
{ return (uint8_t)(byte >= 'a' && byte <= 'z' ? byte - ('a' - 'A') : byte + ('a' - 'A')); }
static bool word(uint8_t byte)
{ return letter(byte) || (byte >= '0' && byte <= '9') || byte == '_'; }

/* The frozen core owns syntax validation. This transformer only rewrites
 * successfully validated expressions; escaped letters remain literal atoms,
 * and class negation is applied AFTER expanding ASCII case membership. */
typedef struct builder {
    work_ctx *context;
    uint8_t *bytes;
    size_t length, units;
    find_code code;
} builder;
static void tick(builder *b)
{
    b->units++;
    if (b->units >= FIND_POLL_UNITS) {
        b->units = 0;
        if (work_should_stop(b->context)) b->code = FIND_CANCELLED;
    }
}
static void put(builder *b, uint8_t byte)
{
    tick(b);
    if (b->code != FIND_OK) return;
    if (b->length == FINDUI_PATTERN_BYTES) { b->code = FIND_ERR_LIMIT; return; }
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
static find_code program(findui_slot *slot, work_ctx *context, find_regex **regex,
                         size_t *error_offset)
{
    *regex = NULL;
    if (!slot->options.regex && slot->options.match_case) return FIND_OK;
    if (work_should_stop(context)) return FIND_CANCELLED;
    if (slot->options.regex) {
        find_code code = find_regex_compile(slot->program, find_regex_bytes(),
                                            slot->query, slot->query_length, regex, error_offset);
        if (code != FIND_OK || slot->options.match_case) return code;
    }
    builder b = {context, slot->pattern, 0, 0, FIND_OK};
    for (size_t at = 0; at < slot->query_length && b.code == FIND_OK;) {
        uint8_t byte = slot->query[at++];
        if (!slot->options.regex) literal_atom(&b, byte);
        else if (byte == '[') folded_class(&b, slot->query, &at);
        else if (byte == '\\') literal_atom(&b, slot->query[at++]);
        else if (letter(byte)) literal_atom(&b, byte);
        else put(&b, byte);
    }
    if (work_should_stop(context)) return FIND_CANCELLED;
    if (b.code != FIND_OK) return b.code;
    find_code code = find_regex_compile(slot->program, find_regex_bytes(), b.bytes, b.length,
                                        regex, error_offset);
    /* Any syntax error came from validating the original expression above.
     * A rewritten program limit is represented at the original query's end. */
    if (code != FIND_OK) *error_offset = slot->query_length;
    return code;
}
static find_code whole_word(const findui_slot *slot, work_ctx *context,
                            findui_range range, uint64_t length, bool *accepted)
{
    *accepted = true;
    if (!slot->options.whole_word) return FIND_OK;
    uint8_t byte;
    if (range.start) {
        if (work_should_stop(context)) return FIND_CANCELLED;
        scan_trace(slot, context->generation);
        if (piece_snapshot_read(slot->snapshot, range.start - 1, &byte, 1)) return FIND_ERR_ARGUMENT;
        if (word(byte)) *accepted = false;
    }
    if (range.end < length) {
        if (work_should_stop(context)) return FIND_CANCELLED;
        scan_trace(slot, context->generation);
        if (piece_snapshot_read(slot->snapshot, range.end, &byte, 1)) return FIND_ERR_ARGUMENT;
        if (word(byte)) *accepted = false;
    }
    return FIND_OK;
}
typedef struct visitor_output {
    work_ctx *context;
    findui_batch *batch;
    size_t *batched;
} visitor_output;
static bool emit_range(void *user,uint64_t ordinal,find_capture range)
{
    visitor_output *out=user;
    if (*out->batched && ordinal!=out->batch->ordinal+*out->batched &&
        !flush(out->context,out->batch,out->batched)) return false;
    if (!*out->batched) out->batch->ordinal=ordinal;
    out->batch->ranges[(*out->batched)++]=(findui_range){range.start,range.end};
    return (*out->batched!=2 && ordinal!=0) || flush(out->context,out->batch,out->batched);
}
void findui_worker_run(work_ctx *context)
{
    findui_slot *slot = context->arg;
    find_regex *regex;
    size_t error_offset = 0;
    find_code code = program(slot, context, &regex, &error_offset);
    find_source source = {.snapshot = slot->snapshot};
    find_control control = {.work = context};
    uint64_t offset = 0, total = 0, length = piece_snapshot_len(slot->snapshot);
    uint64_t regex_budget=find_regex_work_budget(regex,length);
    findui_batch batch = {.owner = slot->owner};
    size_t batched = 0;
    uint64_t visible=FIND_UNSET;
    if (code==FIND_OK && !slot->options.whole_word) {
        visitor_output output={context,&batch,&batched};
        find_visit visit={.prefix_capacity=slot->cache_capacity,
                          .visible_capacity=slot->visible_capacity,
                          .wanted=slot->desired_index,.window_start=slot->window_start,
                          .window_end=slot->window_end,.emit=emit_range,.user=&output};
        scan_trace(slot,context->generation);
        code=regex ? find_regex_visit(&source,regex,slot->scratch,FIND_MAX_SCRATCH_BYTES,&control,&visit)
                   : find_literal_visit(&source,slot->query,slot->query_length,&control,&visit);
        total=visit.total; visible=visit.visible;
    }
    while (slot->options.whole_word && code == FIND_OK && !work_should_stop(context)) {
        find_match match;
        scan_trace(slot, context->generation);
        code = regex ? find_regex_next_budget(&source, regex, offset, slot->scratch,
                                              FIND_MAX_SCRATCH_BYTES, &control, &regex_budget, &match)
                     : find_literal_next(&source, slot->query, slot->query_length,
                                         offset, &control, &match);
        if (code != FIND_OK || !match.matched) break;
        findui_range range = {match.whole.start, match.whole.end};
        bool accepted;
        code = whole_word(slot, context, range, length, &accepted);
        if (code != FIND_OK) break;
        if (accepted) {
            if (total == UINT64_MAX) { code = FIND_ERR_LIMIT; break; }
            bool wanted = total < slot->cache_capacity || total == slot->desired_index ||
                          intersects(range, slot->window_start, slot->window_end);
            if (wanted) {
                if (batched && total != batch.ordinal + batched && !flush(context, &batch, &batched)) return;
                if (!batched) batch.ordinal = total;
                batch.ranges[batched++] = range;
                /* Flush a first match immediately for incremental feedback. */
                if ((batched == 2 || total == 0) && !flush(context, &batch, &batched)) return;
            }
            total++;
        }
        /* Rejected candidates must not hide a later overlapping, valid word.
         * Accepted results retain the frozen leftmost/non-overlap policy. */
        offset = accepted ? range.end : range.start;
        if (!accepted || range.start == range.end) {
            if (offset == length) break;
            offset++;
        }
    }
    if (work_should_stop(context)) return;
    if (!flush(context, &batch, &batched)) return;
    findui_done done = {.owner=slot->owner,.count=code==FIND_OK ? total : 0,
                       .error_offset=error_offset,.code=(int32_t)code,.visible=visible};
    work_msg message = {.kind = FINDUI_MSG_DONE, .generation = context->generation};
    memcpy(message.data, &done, sizeof done);
    (void)publish(context, &message);
}
