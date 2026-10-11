#include "findui/private.h"
#include "find/visit.h"
#include "file/file.h"
#include <string.h>
#include <time.h>

static void scan_trace(const findui_slot *slot, uint32_t generation)
{ if (slot->hook) slot->hook(slot->hook_user, generation); }

/* Search produces a bounded private outbox. Publication is a continuation:
 * a full mailbox returns the sole bulk lane to its FIFO immediately. */
static bool publish(work_ctx *context, const work_msg *message)
{
    findui_slot *slot = context->arg;
    if (work_should_stop(context) || file_snapshot_faulted(slot->snapshot)) return false;
    if (!slot->outbox_count && work_publish(context, message)) return true;
    EDIT_ASSERT(slot->outbox_count < slot->outbox_capacity);
    slot->outbox[slot->outbox_count++] = *message;
    return true;
}
static void deliver(work_ctx *context, findui_slot *slot)
{
    while (slot->outbox_next < slot->outbox_count && !work_should_stop(context) &&
           !file_snapshot_faulted(slot->snapshot)) {
        if (!work_publish(context, &slot->outbox[slot->outbox_next])) {
            (void)work_continue(context); return;
        }
        slot->outbox_next++;
    }
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
static bool letter(uint8_t byte)
{ return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z'); }
static uint8_t other_case(uint8_t byte)
{ return (uint8_t)(byte >= 'a' && byte <= 'z' ? byte - ('a' - 'A') : byte + ('a' - 'A')); }

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
    if (!slot->options.regex) return FIND_OK;
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
typedef struct visitor_output {
    work_ctx *context;
    findui_batch *batch;
    size_t *batched;
    findui_slot *slot;
    find_visit selected;
    bool plan_failed;
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
static bool record_range(void *user,uint64_t ordinal,find_capture range)
{
    visitor_output *out=user;
    if (!findui_plan_add(&out->slot->plan,ordinal,range)) { out->plan_failed=true; return false; }
    return find_visit_add(&out->selected,ordinal,range);
}
static bool record_mask(void *user,uint64_t mask,uint64_t base,uint64_t ordinal)
{
    visitor_output *out=user;
    if (!findui_plan_add_mask(&out->slot->plan,mask,base,ordinal)) { out->plan_failed=true; return false; }
    return find_visit_mask(&out->selected,mask,base,ordinal);
}
static find_capture run_range(const findui_plan_run *run,uint64_t index)
{
    uint64_t delta=index*run->stride;
    return (find_capture){run->start+delta,run->end+delta};
}
static uint64_t ceiling_quotient(uint64_t n,uint64_t d)
{ return n/d+(n%d!=0?1u:0u); }
static void run_window(const findui_plan_run *run,uint64_t start,uint64_t end,uint64_t *lo,uint64_t *hi)
{
    *lo=0; *hi=run->count;
    if (!run->stride) {
        find_visit v={.window_start=start,.window_end=end};
        if (!find_visit_intersects(&v,(find_capture){run->start,run->end})) *hi=0;
        return;
    }
    bool empty=run->start==run->end;
    if (!empty && start>=end) { *hi=0; return; }
    if (empty) {
        if (start>run->start) *lo=ceiling_quotient(start-run->start,run->stride);
        if (end<run->start) *hi=0;
        else {
            uint64_t last=(end-run->start)/run->stride;
            if (last<run->count) *hi=last+1u;
        }
    } else {
        if (start>=run->end) *lo=(start-run->end)/run->stride+1u;
        if (end<=run->start) *hi=0;
        else {
            uint64_t limit=ceiling_quotient(end-run->start,run->stride);
            if (limit<*hi) *hi=limit;
        }
    }
    if (*lo>*hi) *lo=*hi;
}
static find_code retrieve(findui_slot *slot,visitor_output *out)
{
    const findui_plan *plan=&slot->plan_source->plan;
    slot->total=plan->total;
    if (slot->selection_request) {
        findui_plan_run run;
        if (!findui_plan_ordinal(plan,slot->desired_index,&run)) return FIND_ERR_MEMORY;
        if (!emit_range(out,slot->desired_index,run_range(&run,slot->desired_index-run.ordinal))) return FIND_CANCELLED;
        slot->scan_done=true; return FIND_OK;
    }
    size_t units=0;
    if (slot->page_request) {
        while (slot->retrieve_index<slot->page_first+slot->page_count && units++<FIND_POLL_UNITS) {
            findui_plan_run run;
            if (!findui_plan_ordinal(plan,slot->retrieve_index,&run)) return FIND_ERR_MEMORY;
            if (!emit_range(out,slot->retrieve_index,run_range(&run,slot->retrieve_index-run.ordinal))) return FIND_CANCELLED;
            slot->retrieve_index++;
        }
        slot->scan_done=slot->retrieve_index==slot->page_first+slot->page_count;
        return FIND_OK;
    }
    while (slot->plan_record<plan->records && units<FIND_POLL_UNITS) {
        if (!slot->run_active) {
            if (!findui_plan_read(plan,slot->plan_record,&slot->retrieve_run)) return FIND_ERR_MEMORY;
            findui_plan_run *run=&slot->retrieve_run;
            if (run->start>slot->window_end || (run->start==slot->window_end && run->start!=run->end)) break;
            run_window(run,slot->window_start,slot->window_end,&slot->run_lo,&slot->run_hi);
            slot->visible+=slot->run_hi-slot->run_lo;
            slot->retrieve_index=slot->run_lo; slot->run_active=true; units++;
        }
        while (slot->retrieve_index<slot->run_hi && slot->visible_emitted<slot->visible_capacity && units<FIND_POLL_UNITS) {
            const findui_plan_run *run=&slot->retrieve_run;
            if (!emit_range(out,run->ordinal+slot->retrieve_index,run_range(run,slot->retrieve_index))) return FIND_CANCELLED;
            slot->retrieve_index++; slot->visible_emitted++; units++;
        }
        if (slot->retrieve_index<slot->run_hi && slot->visible_emitted<slot->visible_capacity) return FIND_OK;
        slot->run_active=false; slot->plan_record++;
    }
    slot->scan_done=units<FIND_POLL_UNITS || slot->plan_record==plan->records;
    return FIND_OK;
}
void findui_worker_run(work_ctx *context)
{
    findui_slot *slot=context->arg;
    if (slot->publishing) { deliver(context,slot); return; }
    if (work_should_stop(context) || file_snapshot_faulted(slot->snapshot)) return;
    find_code code=FIND_OK; size_t error_offset=0;
    if (!slot->started) {
        slot->started=true;
        if (slot->plan_source) {
            slot->retrieve_index=slot->page_first;
            if (!slot->page_request && !slot->selection_request &&
                !findui_plan_window(&slot->plan_source->plan,slot->window_start,&slot->plan_record)) code=FIND_ERR_MEMORY;
        } else {
            code=program(slot,context,&slot->regex,&error_offset);
            if (code==FIND_OK && !findui_plan_reset(&slot->plan)) code=FIND_ERR_MEMORY;
            find_source source={.snapshot=slot->snapshot};
            if (code==FIND_OK && slot->regex)
                code=find_regex_stream_begin(&source,slot->regex,slot->scratch,FIND_MAX_SCRATCH_BYTES,slot->options.whole_word);
            if (code==FIND_OK) scan_trace(slot,context->generation);
        }
    }
    findui_batch batch={.owner=slot->owner}; size_t batched=0;
    visitor_output output={.context=context,.batch=&batch,.batched=&batched,.slot=slot};
    output.selected=(find_visit){.prefix_capacity=slot->cache_capacity,.visible_capacity=slot->visible_capacity,
        .wanted=slot->desired_index,.window_start=slot->window_start,.window_end=slot->window_end,
        .emit=emit_range,.user=&output,.visible=slot->visible};
    if (code==FIND_OK && slot->plan_source) code=retrieve(slot,&output);
    else if (code==FIND_OK) {
        /* All ranges enter the private plan; only bounded selected ranges
         * enter the UI mailbox. Source traversal never repeats for retrieval. */
        find_visit all={.prefix_capacity=SIZE_MAX,.wanted=FIND_UNSET,.emit=record_range,.emit_mask=record_mask,
                        .user=&output,.total=slot->total};
        find_control control={.work=context};
        code=slot->regex ? find_regex_stream_step(slot->scratch,&control,&all,&slot->scan_done)
                         : findui_literal_step(slot,context,&all);
        slot->total=all.total; slot->visible=output.selected.visible;
        if (output.plan_failed) code=FIND_ERR_MEMORY;
        if (code==FIND_OK && slot->scan_done && !findui_plan_seal(&slot->plan)) code=FIND_ERR_MEMORY;
    }
    if (work_should_stop(context) || file_snapshot_faulted(slot->snapshot)) return;
    if (!flush(context,&batch,&batched)) return;
    if (code==FIND_OK && !slot->scan_done) { (void)work_continue(context); return; }
    findui_done done={.owner=slot->owner,.count=code==FIND_OK?slot->total:0,
        .error_offset=error_offset,.code=(int32_t)code,.visible=slot->selection_request?FIND_UNSET:slot->visible};
    work_msg message={.kind=FINDUI_MSG_DONE,.generation=context->generation};
    memcpy(message.data,&done,sizeof done);
    if (publish(context,&message)) { slot->publishing=true; deliver(context,slot); }
}
