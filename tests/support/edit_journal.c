#include "edit_journal.h"

#include <stdlib.h>
#include <string.h>

typedef enum ej_phase { EJ_FREE, EJ_PRODUCING, EJ_ABANDONED, EJ_FINALIZED } ej_phase;

typedef struct ej_frame {
    journal_record_type type;
    uint64_t owner;
    size_t at, len;
} ej_frame;

struct journal_request {
    journal_command *command;
    journal_result result;
    bool owned;
};

struct journal_command {
    journal *j;
    ej_phase phase;
    uint64_t owner, members;
    size_t member_bytes, sequence_credit, credit, largest, max_member, used_member;
    journal_record_type final_type;
    size_t final_len;
    journal_request request;
    uint8_t window[JOURNAL_PAGE_BYTES - JOURNAL_HEADER_BYTES];
};

struct journal {
    /* Two owners suffice for the in-flight edit and its independent peer. */
    journal_command slots[2];
    journal_command *held;
    journal_record_type held_type, short_type;
    size_t held_len;
    ej_frame *frames;
    uint8_t *image;
    size_t byte_cap, record_cap, reserved_records;
    journal_condition condition;
    edit_journal_info used;
    edit_journal_claim refusal;
    journal_err begin_error, member_error;
    uint64_t generation;
    const uint8_t *original;
    size_t original_len;
    bool embargoed;
};

journal *ej_create(size_t bytes, size_t frames)
{
    journal *j = calloc(1, sizeof *j);
    ASSERT(j);
    j->image = malloc(bytes);
    j->frames = malloc(frames * sizeof *j->frames);
    ASSERT(j->image && j->frames);
    j->byte_cap = bytes;
    j->record_cap = frames;
    j->condition = JOURNAL_HEALTHY;
    j->refusal = EJ_CLAIMS;
    return j;
}

void ej_destroy(journal *j)
{
    ASSERT(j->used.open == 0 && j->used.requests == 0 && !j->held);
    ASSERT(j->used.reserved_bytes == 0 && j->reserved_records == 0);
    free(j->frames);
    free(j->image);
    free(j);
}

void ej_original(journal *j, uint64_t key, const uint8_t *bytes, size_t len)
{
    j->generation = key;
    j->original = bytes;
    j->original_len = len;
}

void ej_embargo(journal *j) { j->embargoed = true; }

void ej_refuse_begin(journal *j, edit_journal_claim claim, journal_err err)
{
    j->refusal = claim;
    j->begin_error = err;
}

void ej_refuse_member(journal *j, journal_err err) { j->member_error = err; }
void ej_short(journal *j, journal_record_type type) { j->short_type = type; }
void ej_health(journal *j, journal_condition state) { j->condition = state; }
edit_journal_info ej_info(const journal *j) { return j->used; }

bool journal_range_allowed(const journal *j, uint64_t generation)
{
    ((journal *)j)->used.range_checks++;
    return generation != 0 && generation == j->generation && !j->embargoed;
}

journal_condition journal_state(const journal *j)
{
    return j->held ? JOURNAL_GAP : j->condition;
}

static void refund(journal_command *c)
{
    c->j->used.reserved_bytes -= c->credit;
    c->j->reserved_records -= c->sequence_credit;
    c->credit = c->sequence_credit = 0;
}

static void accept(journal_command *c, journal_record_type type, size_t len)
{
    journal *j = c->j;
    size_t frame = JOURNAL_HEADER_BYTES + len;
    ASSERT(c->credit >= frame && c->sequence_credit);
    ASSERT(j->used.frames < j->record_cap && frame <= j->byte_cap - j->used.bytes);
    j->frames[j->used.frames++] = (ej_frame){type, c->owner, j->used.bytes, len};
    memcpy(j->image + j->used.bytes + JOURNAL_HEADER_BYTES, c->window, len);
    j->used.bytes += frame;
    j->used.reserved_bytes -= frame;
    j->reserved_records--;
    c->credit -= frame;
    c->sequence_credit--;
    j->used.inserts += type == JOURNAL_REC_INSERT || type == JOURNAL_REC_COMMAND_INSERT;
    j->used.deletes += type == JOURNAL_REC_DELETE || type == JOURNAL_REC_COMMAND_DELETE;
    j->used.ranges += type == JOURNAL_REC_RESTORE_BASE_RANGE ||
                      type == JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE;
    if (c->phase == EJ_FINALIZED) {
        ASSERT(c->credit == 0 && c->sequence_credit == 0);
        j->used.committed++;
        c->request.result = (journal_result){JOURNAL_OK, JOURNAL_VISIBLE,
            {1, j->used.frames, j->used.bytes}, 0};
        if (!c->request.owned) c->phase = EJ_FREE;
    }
}

static void append(journal_command *c, journal_record_type type, size_t len)
{
    journal *j = c->j;
    ASSERT(journal_state(j) == JOURNAL_HEALTHY);
    if (j->short_type == type) {
        j->short_type = 0;
        j->held = c;
        j->held_type = type;
        j->held_len = len;
        return;
    }
    accept(c, type, len);
}

journal_err journal_reconcile(journal *j)
{
    if (!j->held) return j->condition == JOURNAL_HEALTHY ? JOURNAL_OK : JOURNAL_WAIT;
    journal_command *c = j->held;
    j->held = NULL;
    accept(c, j->held_type, j->held_len);
    if (c->phase == EJ_ABANDONED) {
        refund(c);
        c->phase = EJ_FREE;
    }
    return JOURNAL_OK;
}

journal_err journal_command_begin(journal *j, uint64_t owner, journal_member_claim claim,
                                  journal_record_type final_type, size_t final_len,
                                  journal_command **out)
{
    if (journal_state(j) != JOURNAL_HEALTHY) return JOURNAL_WAIT;
    bool grouped = final_type == JOURNAL_REC_COMMAND_COMMIT;
    size_t markers = grouped ? 2u : 1u; /* BEGIN and COMMIT, or one standalone. */
    size_t framing = markers * JOURNAL_HEADER_BYTES + final_len;
    ASSERT(claim.count ? grouped && !final_len && claim.largest_frame >= JOURNAL_HEADER_BYTES :
                         !grouped && !claim.framed_bytes && !claim.largest_frame);
    size_t largest = JOURNAL_HEADER_BYTES + final_len;
    if (claim.largest_frame > largest) largest = claim.largest_frame;
    ASSERT(largest <= JOURNAL_PAGE_BYTES);
    if (claim.framed_bytes > SIZE_MAX - framing || claim.count > SIZE_MAX - markers)
        return JOURNAL_LIMIT;
    size_t bytes = framing + (size_t)claim.framed_bytes;
    size_t records = markers + (size_t)claim.count;
    if (bytes > j->byte_cap - j->used.bytes - j->used.reserved_bytes ||
        records > j->record_cap - j->used.frames - j->reserved_records)
        return JOURNAL_LIMIT;
    journal_command *c = NULL;
    for (size_t i = 0; i < sizeof j->slots / sizeof *j->slots; i++) {
        ASSERT(j->slots[i].phase != EJ_PRODUCING || j->slots[i].owner != owner);
        if (j->slots[i].phase == EJ_FREE) c = &j->slots[i];
    }
    if (!c) return JOURNAL_NOMEM;
    /* Hold credits while testing each successive claim and refund the
     * prefix that was granted if a later claim is refused. */
    c->credit = c->sequence_credit = 0;
    c->j = j;
    for (edit_journal_claim claim_id = EJ_COMMAND; claim_id < EJ_CLAIMS; claim_id++) {
        if (j->refusal == claim_id) {
            j->refusal = EJ_CLAIMS;
            refund(c);
            return j->begin_error;
        }
        if (claim_id == EJ_BYTES) {
            j->used.reserved_bytes += bytes;
            c->credit = bytes;
        }
        if (claim_id == EJ_SEQUENCES) {
            j->reserved_records += records;
            c->sequence_credit = records;
        }
    }
    c->phase = EJ_PRODUCING;
    c->owner = owner;
    c->members = claim.count;
    c->member_bytes = (size_t)claim.framed_bytes;
    c->largest = largest;
    c->max_member = claim.largest_frame;
    c->used_member = 0;
    c->final_type = final_type;
    c->final_len = final_len;
    j->used.open++;
    if (largest > j->used.largest) j->used.largest = largest;
    if (grouped) append(c, JOURNAL_REC_COMMAND_BEGIN, 0);
    *out = c;
    return JOURNAL_OK;
}

uint8_t *journal_command_payload(journal_command *command)
{
    ASSERT(command->phase == EJ_PRODUCING);
    return command->j->held == command ? NULL : command->window;
}

journal_err journal_command_member(journal_command *c, journal_record_type type, size_t len)
{
    ASSERT(c->phase == EJ_PRODUCING && c->members && c->j->held != c);
    ASSERT(type == JOURNAL_REC_COMMAND_INSERT || type == JOURNAL_REC_COMMAND_DELETE ||
           type == JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE);
    size_t framed = JOURNAL_HEADER_BYTES + len;
    ASSERT(framed <= c->max_member && framed <= c->member_bytes);
    if (journal_state(c->j) != JOURNAL_HEALTHY) return JOURNAL_WAIT;
    journal_err err = c->j->member_error;
    c->j->member_error = JOURNAL_OK;
    if (err != JOURNAL_OK) return err;
    c->members--;
    c->member_bytes -= framed;
    if (framed > c->used_member) c->used_member = framed;
    append(c, type, len);
    return JOURNAL_OK;
}

void journal_command_abort(journal_command *c)
{
    ASSERT(c->phase == EJ_PRODUCING);
    c->j->used.open--;
    c->j->used.aborts++;
    if (c->j->held == c) {
        size_t held = JOURNAL_HEADER_BYTES + c->j->held_len;
        c->j->used.reserved_bytes -= c->credit - held;
        c->j->reserved_records -= c->sequence_credit - 1;
        c->credit = held;
        c->sequence_credit = 1;
        c->phase = EJ_ABANDONED;
    } else {
        refund(c);
        c->phase = EJ_FREE;
    }
}

journal_request *journal_command_final(journal_command *c)
{
    ASSERT(c->phase == EJ_PRODUCING && c->members == 0 && c->member_bytes == 0 &&
           c->used_member == c->max_member);
    c->phase = EJ_FINALIZED;
    c->j->used.open--;
    c->j->used.finals++;
    c->j->used.requests++;
    c->request = (journal_request){c, {JOURNAL_IO, JOURNAL_POSSIBLE, {0}, 0}, true};
    append(c, c->final_type, c->final_len);
    return &c->request;
}

journal_result journal_request_result(const journal_request *request)
{
    ASSERT(request->owned);
    return request->result;
}

void journal_request_release(journal_request *request)
{
    ASSERT(request->owned);
    journal_command *c = request->command;
    c->j->used.requests--;
    request->owned = false;
    if (c->j->held != c) c->phase = EJ_FREE;
}

static uint64_t read_le(const uint8_t *p)
{
    uint64_t v = 0;
    for (size_t i = sizeof v; i != 0; i--) v = (v << 8) | p[i - 1];
    return v;
}

static void apply_frame(const journal *j, const ej_frame *f, uint8_t *text,
                        size_t *len, size_t cap)
{
    const uint8_t *p = j->image + f->at + JOURNAL_HEADER_BYTES;
    size_t off = (size_t)read_le(p);
    ASSERT(off <= *len);
    if (f->type == JOURNAL_REC_DELETE || f->type == JOURNAL_REC_COMMAND_DELETE) {
        ASSERT(f->len == 2 * sizeof(uint64_t));
        size_t n = (size_t)read_le(p + 8);
        ASSERT(n && n <= *len - off);
        memmove(text + off, text + off + n, *len - off - n);
        *len -= n;
        return;
    }
    const uint8_t *src = p + 8;
    ASSERT(f->len > sizeof(uint64_t));
    size_t n = f->len - sizeof(uint64_t);
    if (f->type == JOURNAL_REC_RESTORE_BASE_RANGE || f->type == JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE) {
        ASSERT(f->len == 4 * sizeof(uint64_t) && read_le(p + 8) == j->generation);
        size_t key_off = (size_t)read_le(p + 16);
        n = (size_t)read_le(p + 24);
        ASSERT(key_off <= j->original_len && n <= j->original_len - key_off);
        src = j->original + key_off;
    }
    ASSERT(n && n <= cap - *len);
    memmove(text + off + n, text + off, *len - off);
    memcpy(text + off, src, n);
    *len += n;
}

size_t ej_read(const journal *j, uint64_t owner, const uint8_t *base, size_t len,
               uint8_t *out, size_t cap)
{
    ASSERT(len <= cap);
    if (len) memcpy(out, base, len);
    size_t group = SIZE_MAX;
    for (size_t i = 0; i < j->used.frames; i++) {
        const ej_frame *f = &j->frames[i];
        if (f->owner != owner) continue;
        if (f->type == JOURNAL_REC_COMMAND_BEGIN) {
            group = i + 1;
        } else if (f->type == JOURNAL_REC_COMMAND_COMMIT) {
            ASSERT(group != SIZE_MAX);
            for (size_t k = group; k < i; k++) {
                if (j->frames[k].owner == owner) apply_frame(j, &j->frames[k], out, &len, cap);
            }
            group = SIZE_MAX;
        } else if (f->type == JOURNAL_REC_INSERT || f->type == JOURNAL_REC_DELETE ||
                   f->type == JOURNAL_REC_RESTORE_BASE_RANGE) {
            apply_frame(j, f, out, &len, cap);
        }
    }
    return len;
}
