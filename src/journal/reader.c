/* Physical certification and logical indexing of a borrowed journal image. */
#include "journal/journal.h"
#include "os/os.h"

#include <string.h>

typedef struct index_entry {
    uint64_t key, qualifier;
    void *value;
} index_entry;
typedef struct lookup_table {
    index_entry *entries;
    size_t capacity, count;
} lookup_table;

typedef enum slot_phase {
    SLOT_CLOSED, SLOT_BEGUN, SLOT_MEMBERS, SLOT_TARGET
} slot_phase;
typedef struct slot {
    slot_phase phase;
    uint64_t begin, covered, text_length, literal_end;
    size_t binding, saved;
    journal_frame_ref *first, *last;
} slot;

typedef struct owner_entry {
    journal_owner_plan plan;
    slot command, checkpoint;
    uint64_t boundary;
    size_t binding, saved;
    journal_operation *last_command;
} owner_entry;

typedef enum token_phase { TOKEN_OPEN, TOKEN_FINISHED, TOKEN_CANCELLED } token_phase;
typedef struct token_entry {
    token_phase phase;
    journal_operation *prepared;
} token_entry;

typedef enum key_phase { KEY_RESERVED, KEY_STAGED, KEY_ACTIVE } key_phase;
typedef struct key_entry {
    key_phase phase;
    const token_entry *claim;
    size_t declaration, activation;
    uint64_t size;
    uint32_t flags;
} key_entry;

typedef struct indexed_operation {
    journal_operation plan;
    journal_record_type type;
    owner_entry *owner;
    const token_entry *token;
    uint64_t covered;
} indexed_operation;

typedef struct scan {
    const uint8_t *image;
    base_arena *arena;
    journal_logical_plan *plan;
    lookup_table owners, keys, tokens, finals;
    journal_operation *last;
} scan;

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t read64(const uint8_t *p)
{
    return read32(p) | (uint64_t)read32(p + 4) << 32;
}

/* The offset was indexed only after the full frame passed validation. */
static journal_frame indexed_frame(const scan *s, size_t offset)
{
    const uint8_t *p = s->image + offset;
    size_t length = read32(p + 4);
    return (journal_frame){
        .type = (journal_record_type)read32(p + 8), .sequence = read64(p + 16),
        .buffer_id = read64(p + 24), .payload = p + JOURNAL_HEADER_BYTES,
        .payload_len = length - JOURNAL_HEADER_BYTES, .total_len = length
    };
}

static uint64_t hash_word(uint64_t x)
{
    /* SplitMix64's finalizer mixes sparse keys and sequential sequences. */
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static index_entry *find_entry(const lookup_table *table, uint64_t key, uint64_t qualifier)
{
    size_t mask = table->capacity - 1;
    size_t at = (size_t)hash_word(key ^ hash_word(qualifier)) & mask;
    while (table->entries[at].value &&
           (table->entries[at].key != key || table->entries[at].qualifier != qualifier))
        at = (at + 1) & mask;
    return &table->entries[at];
}

static void *lookup(const lookup_table *table, uint64_t key, uint64_t qualifier)
{
    return table->capacity ? find_entry(table, key, qualifier)->value : NULL;
}

static bool index_add(scan *s, lookup_table *table, uint64_t key, uint64_t qualifier, void *value)
{
    if (table->count == table->capacity / 2) {
        size_t old = table->capacity;
        if (old > SIZE_MAX / sizeof(index_entry) / 2) return false;
        size_t cap = old ? old * 2 : 2; /* (P) Initially one entry at half load. */
        index_entry *entries = base_arena_alloc(s->arena, cap * sizeof *entries,
                                                _Alignof(index_entry));
        if (!entries) return false;
        memset(entries, 0, cap * sizeof *entries);
        index_entry *prior = table->entries;
        table->capacity = cap;
        table->entries = entries;
        for (size_t i = 0; i < old; i++) {
            if (prior[i].value)
                *find_entry(table, prior[i].key, prior[i].qualifier) = prior[i];
        }
    }
    *find_entry(table, key, qualifier) = (index_entry){ key, qualifier, value };
    table->count++;
    return true;
}

static void *allocate(scan *s, size_t bytes, size_t align)
{
    void *p = base_arena_alloc(s->arena, bytes, align);
    if (p) memset(p, 0, bytes);
    return p;
}
#define NEW(scan_arg, type) ((type *)allocate((scan_arg), sizeof(type), _Alignof(type)))

static owner_entry *add_owner(scan *s, uint64_t id)
{
    owner_entry *owner = NEW(s, owner_entry);
    if (!owner || !index_add(s, &s->owners, id, 0, owner)) return NULL;
    owner->plan.owner = id;
    owner->plan.next = s->plan->owners;
    s->plan->owners = &owner->plan;
    return owner;
}

static key_entry *add_key(scan *s, uint64_t key)
{
    key_entry *entry = NEW(s, key_entry);
    if (!entry || !index_add(s, &s->keys, key, 0, entry)) return NULL;
    return entry;
}

static void high_water(uint64_t *high, uint64_t value)
{
    if (*high < value) *high = value;
}

typedef enum record_role { STANDALONE, COMMAND_SLOT, CHECKPOINT_SLOT } record_role;
static record_role role_of(journal_record_type type)
{
    switch (type) {
    case JOURNAL_REC_COMMAND_BEGIN:
    case JOURNAL_REC_COMMAND_COMMIT:
    case JOURNAL_REC_COMMAND_INSERT:
    case JOURNAL_REC_COMMAND_DELETE:
    case JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE:
        return COMMAND_SLOT;
    case JOURNAL_REC_CHECKPOINT_BEGIN:
    case JOURNAL_REC_CHECKPOINT_COMMIT:
    case JOURNAL_REC_CHECKPOINT_BY_VALUE:
    case JOURNAL_REC_CHECKPOINT_INSERT:
    case JOURNAL_REC_CHECKPOINT_TARGET:
        return CHECKPOINT_SLOT;
    case JOURNAL_REC_PAD:
    case JOURNAL_REC_GENERATION:
    case JOURNAL_REC_BASE:
    case JOURNAL_REC_TARGET:
    case JOURNAL_REC_INSERT:
    case JOURNAL_REC_DELETE:
    case JOURNAL_REC_RESTORE_BASE_RANGE:
    case JOURNAL_REC_SAVE:
    case JOURNAL_REC_VIEW:
    case JOURNAL_REC_TABS:
        return STANDALONE;
    }
    return STANDALONE;
}

static bool is_command(journal_record_type type)
{
    return type == JOURNAL_REC_INSERT || type == JOURNAL_REC_DELETE ||
           type == JOURNAL_REC_RESTORE_BASE_RANGE || type == JOURNAL_REC_COMMAND_COMMIT;
}

static bool is_begin(journal_record_type type)
{
    return type == JOURNAL_REC_COMMAND_BEGIN || type == JOURNAL_REC_CHECKPOINT_BEGIN;
}

static bool is_commit(journal_record_type type)
{
    return type == JOURNAL_REC_COMMAND_COMMIT || type == JOURNAL_REC_CHECKPOINT_COMMIT;
}

static bool slot_step(slot *pending, const journal_frame *f, record_role role)
{
    if (!pending) return false;
    if (is_begin(f->type)) {
        *pending = (slot){ .phase = SLOT_BEGUN, .begin = f->sequence };
        return true;
    }
    bool complete = pending->literal_end == pending->text_length;
    switch (pending->phase) {
    case SLOT_CLOSED:
        return false;
    case SLOT_BEGUN:
        if (role == COMMAND_SLOT) {
            if (is_commit(f->type)) return false;
        } else {
            if (f->type != JOURNAL_REC_CHECKPOINT_BY_VALUE ||
                read64(f->payload) >= pending->begin) return false;
            pending->text_length = read64(f->payload + 24);
        }
        pending->phase = SLOT_MEMBERS;
        return true;
    case SLOT_TARGET:
        return is_commit(f->type);
    case SLOT_MEMBERS:
        if (role == COMMAND_SLOT) return true;
        if (is_commit(f->type)) return complete;
        if (f->type == JOURNAL_REC_CHECKPOINT_BY_VALUE) return false;
        if (f->type == JOURNAL_REC_CHECKPOINT_TARGET) {
            if (!complete) return false;
            pending->phase = SLOT_TARGET;
            return true;
        }
        break;
    }
    size_t length = f->payload_len - 8;
    if (read64(f->payload) != pending->literal_end ||
        length > pending->text_length - pending->literal_end) return false;
    pending->literal_end += length;
    return true;
}

static journal_frame_ref *record_ref(scan *s, size_t offset)
{
    journal_frame_ref *ref = NEW(s, journal_frame_ref);
    if (ref) ref->offset = offset;
    return ref;
}

static bool range_eligible(const key_entry *key, const journal_frame *f)
{
    return key && key->phase == KEY_ACTIVE && !(key->flags & JOURNAL_GENERATION_VALUE_ONLY) &&
           read64(f->payload + 16) + read64(f->payload + 24) <= key->size;
}

static void range_at_member(scan *s, const journal_frame *f, journal_frame_ref *ref)
{
    uint64_t id = read64(f->payload + 8);
    high_water(&s->plan->generation_high_water, id);
    const key_entry *key = lookup(&s->keys, id, 0);
    if (!range_eligible(key, f)) {
        ref->use = JOURNAL_UNRESOLVED;
        return;
    }
    ref->declaration = key->declaration;
    ref->activation = key->activation;
}

static bool index_member(scan *s, slot *pending, const journal_frame *f, size_t offset)
{
    journal_frame_ref *ref = record_ref(s, offset);
    if (!ref) return false;
    if (f->type == JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE) range_at_member(s, f, ref);
    if (pending->last) pending->last->next = ref;
    else pending->first = ref;
    pending->last = ref;
    return true;
}

static bool index_declaration(scan *s, const journal_frame *f, journal_operation *op)
{
    const uint8_t *payload = f->payload;
    uint64_t id = read64(payload);
    uint32_t flags = read32(payload + 64);
    high_water(&s->plan->generation_high_water, id);
    key_entry *key = lookup(&s->keys, id, 0);
    if (!key) {
        key = add_key(s, id);
        if (!key) return false;
        key->phase = KEY_ACTIVE;
    }
    if (key->declaration) {
        journal_frame old = indexed_frame(s, key->declaration);
        /* Fixed identity and prefix witness precede flags; location follows. */
        if (old.payload_len != f->payload_len || memcmp(payload, old.payload, 64) ||
            memcmp(payload + 68, old.payload + 68, f->payload_len - 68) ||
            (key->flags & ~flags)) {
            op->use = JOURNAL_UNRESOLVED;
            return true;
        }
        op->declaration = key->declaration;
    } else {
        key->declaration = op->final;
        key->size = read64(payload + 32);
    }
    key->flags = flags;
    switch (key->phase) {
    case KEY_RESERVED:
    case KEY_STAGED:
        key->phase = KEY_STAGED;
        op->use = JOURNAL_STAGED;
        break;
    case KEY_ACTIVE:
        break;
    }
    return true;
}

static bool valid_boundary(scan *s, uint64_t owner, uint64_t sequence)
{
    if (!sequence) return true;
    const indexed_operation *at = lookup(&s->finals, sequence, 0);
    return at && at->plan.owner == owner &&
           (at->type == JOURNAL_REC_BASE || is_command(at->type));
}

static bool index_prepare(scan *s, indexed_operation *op, const journal_save *save)
{
    const struct journal_save_prepared *p = &save->prepared;
    high_water(&s->plan->generation_high_water, p->previous_key);
    high_water(&s->plan->generation_high_water, p->new_key);
    key_entry *key = lookup(&s->keys, p->new_key, 0);
    if (key) {
        op->plan.use = JOURNAL_UNRESOLVED;
        return true;
    }
    /* Reserve the mentioned key even if the transaction evidence conflicts. */
    key = add_key(s, p->new_key);
    if (!key) return false;
    if (p->new_key == p->previous_key || lookup(&s->tokens, op->plan.owner, save->token) ||
        !valid_boundary(s, op->plan.owner, p->snapshot_sequence)) {
        op->plan.use = JOURNAL_UNRESOLVED;
        return true;
    }
    token_entry *token = NEW(s, token_entry);
    if (!token || !index_add(s, &s->tokens, op->plan.owner, save->token, token))
        return false;
    token->prepared = &op->plan;
    key->claim = token;
    op->token = token;
    op->covered = p->snapshot_sequence;
    op->plan.displaced = op->plan.final;
    return true;
}

static bool same_path(const uint8_t *a, size_t na, const uint8_t *b, size_t nb)
{
    return na == nb && memcmp(a, b, na) == 0;
}

static bool finish_matches(scan *s, const key_entry *key, const token_entry *token,
                           const struct journal_save_prepared *p,
                           const struct journal_save_finished *f)
{
    if (!key || key->claim != token || key->phase != KEY_STAGED ||
        p->new_key != f->installed_key || p->snapshot_sequence != f->snapshot_sequence ||
        p->saved_content_id != f->saved_content_id ||
        !same_path(p->path, p->path_len, f->path, f->path_len)) return false;
    journal_frame frame = indexed_frame(s, key->declaration);
    journal_generation decl;
    if (journal_generation_decode(&frame, &decl) != JOURNAL_CODEC_OK) return false;
    return decl.identity.volume == p->witness.volume &&
           decl.identity.index_low == p->witness.index_low &&
           decl.identity.index_high == p->witness.index_high &&
           decl.identity.size == p->witness.size &&
           same_path(decl.location, decl.location_len, p->path, p->path_len);
}

static void index_terminal(scan *s, indexed_operation *op, const journal_save *save)
{
    journal_operation *out = &op->plan;
    if (save->phase == JOURNAL_SAVE_FINISHED)
        high_water(&s->plan->generation_high_water, save->finished.installed_key);
    token_entry *token = lookup(&s->tokens, out->owner, save->token);
    if (token) out->prepared = token->prepared;
    if (!token) {
        out->use = JOURNAL_UNRESOLVED;
        return;
    }
    switch (token->phase) {
    case TOKEN_OPEN:
        break;
    case TOKEN_FINISHED:
    case TOKEN_CANCELLED:
        out->use = JOURNAL_UNRESOLVED;
        return;
    }
    if (save->phase == JOURNAL_SAVE_FINISHED) {
        journal_frame frame = indexed_frame(s, token->prepared->final);
        journal_save prep;
        key_entry *key = lookup(&s->keys, save->finished.installed_key, 0);
        if (journal_save_decode(&frame, &prep) != JOURNAL_CODEC_OK ||
            !finish_matches(s, key, token, &prep.prepared, &save->finished)) {
            out->use = JOURNAL_UNRESOLVED;
            return;
        }
        token->phase = TOKEN_FINISHED;
        key->activation = out->final;
        key->phase = KEY_ACTIVE;
        out->declaration = key->declaration;
        out->displaced = token->prepared->final;
        out->saved_test = JOURNAL_INSTALLED_WITNESS;
        op->covered = save->finished.snapshot_sequence;
    } else {
        uint64_t preserved = save->cancelled.checkpoint_commit_sequence;
        if (preserved) {
            const indexed_operation *at = lookup(&s->finals, preserved, 0);
            if (!at || at->type != JOURNAL_REC_CHECKPOINT_COMMIT ||
                at->plan.owner != out->owner || at->plan.use != JOURNAL_ELIGIBLE) {
                out->use = JOURNAL_UNRESOLVED;
                return;
            }
            out->checkpoint = &at->plan;
            op->covered = at->covered;
        }
        token->phase = TOKEN_CANCELLED;
        out->saved_test = JOURNAL_EXPECTED_BINDING;
    }
    token->prepared->terminal = out;
    /* The terminal is evidence for a conditional binding and saved decision. */
    op->owner->binding = out->binding = out->final;
    op->owner->saved = out->saved = out->final;
}

static bool index_save(scan *s, indexed_operation *op, const journal_frame *f)
{
    journal_save save;
    if (journal_save_decode(f, &save) != JOURNAL_CODEC_OK) {
        op->plan.use = JOURNAL_UNRESOLVED;
        return true;
    }
    switch (save.phase) {
    case JOURNAL_SAVE_PREPARED:
        return index_prepare(s, op, &save);
    case JOURNAL_SAVE_FINISHED:
    case JOURNAL_SAVE_CANCELLED:
        index_terminal(s, op, &save);
        return true;
    }
    return true;
}

static void index_command(scan *s, indexed_operation *op)
{
    journal_operation *out = &op->plan;
    for (const journal_frame_ref *member = out->frames; member; member = member->next) {
        journal_frame f = indexed_frame(s, member->offset);
        if (f.type != JOURNAL_REC_RESTORE_BASE_RANGE &&
            f.type != JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE) continue;
        const key_entry *key = lookup(&s->keys, read64(f.payload + 8), 0);
        if (member->use != JOURNAL_ELIGIBLE || !range_eligible(key, &f))
            out->use = JOURNAL_UNRESOLVED;
    }
    op->owner->boundary = out->final_sequence;
}

static void index_checkpoint(scan *s, indexed_operation *op, const slot *pending)
{
    journal_operation *out = &op->plan;
    journal_frame descriptor = indexed_frame(s, pending->first->offset);
    uint64_t covered = read64(descriptor.payload);
    op->covered = covered;
    if (covered != pending->covered) {
        out->use = JOURNAL_UNRESOLVED;
        return;
    }
    bool binding_current = pending->binding == op->owner->binding;
    if (pending->phase == SLOT_TARGET && binding_current)
        op->owner->binding = out->binding = pending->last->offset;
    if (binding_current && pending->saved == op->owner->saved) {
        op->owner->saved = out->saved = pending->first->offset;
        out->saved_test = JOURNAL_EXPECTED_BINDING;
    }
}

static void index_base(scan *s, indexed_operation *op, const journal_frame *f,
                       journal_frame_ref *ref)
{
    uint64_t id = read64(f->payload);
    high_water(&s->plan->generation_high_water, id);
    op->owner->boundary = op->covered = f->sequence;
    if (!id) return;
    const key_entry *key = lookup(&s->keys, id, 0);
    if (!key || key->phase != KEY_ACTIVE) {
        ref->use = op->plan.use = JOURNAL_UNRESOLVED;
        return;
    }
    ref->declaration = key->declaration;
    ref->activation = key->activation;
}

static bool index_operation(scan *s, owner_entry *owner, const slot *pending,
                             const journal_frame *f, size_t offset)
{
    indexed_operation *op = NEW(s, indexed_operation);
    if (!op) return false;
    journal_frame_ref *ref = pending ? pending->first : record_ref(s, offset);
    if (!ref) return false;
    op->type = f->type;
    op->owner = owner;
    op->plan = (journal_operation){ .owner = f->buffer_id, .final_sequence = f->sequence,
        .begin_sequence = pending ? pending->begin : f->sequence, .final = offset, .frames = ref };
    if (is_command(f->type)) {
        if (f->type == JOURNAL_REC_RESTORE_BASE_RANGE) range_at_member(s, f, ref);
        index_command(s, op);
    } else {
        switch (f->type) {
        case JOURNAL_REC_GENERATION:
            if (!index_declaration(s, f, &op->plan)) return false;
            break;
        case JOURNAL_REC_BASE:
            index_base(s, op, f, ref);
            break;
        case JOURNAL_REC_TARGET:
            owner->binding = op->plan.binding = offset;
            break;
        case JOURNAL_REC_CHECKPOINT_COMMIT:
            index_checkpoint(s, op, pending);
            break;
        case JOURNAL_REC_SAVE:
            if (!index_save(s, op, f)) return false;
            break;
        case JOURNAL_REC_TABS:
            for (size_t i = 8; i < f->payload_len; i += JOURNAL_TABS_ENTRY_BYTES)
                high_water(&s->plan->buffer_high_water, read64(f->payload + i));
            break;
        case JOURNAL_REC_VIEW:
        case JOURNAL_REC_PAD:
        case JOURNAL_REC_INSERT:
        case JOURNAL_REC_DELETE:
        case JOURNAL_REC_RESTORE_BASE_RANGE:
        case JOURNAL_REC_COMMAND_BEGIN:
        case JOURNAL_REC_COMMAND_COMMIT:
        case JOURNAL_REC_COMMAND_INSERT:
        case JOURNAL_REC_COMMAND_DELETE:
        case JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE:
        case JOURNAL_REC_CHECKPOINT_BEGIN:
        case JOURNAL_REC_CHECKPOINT_BY_VALUE:
        case JOURNAL_REC_CHECKPOINT_INSERT:
        case JOURNAL_REC_CHECKPOINT_TARGET:
            break;
        }
    }
    if ((f->type == JOURNAL_REC_BASE || is_command(f->type) ||
         f->type == JOURNAL_REC_CHECKPOINT_COMMIT) &&
        !index_add(s, &s->finals, f->sequence, 0, op)) return false;
    if (s->last) s->last->next = &op->plan;
    else s->plan->operations = &op->plan;
    s->last = &op->plan;
    return true;
}

static journal_codec_err scan_record(scan *s, const journal_frame *f, size_t offset)
{
    record_role role = role_of(f->type);
    bool begin = is_begin(f->type), commit = is_commit(f->type);
    owner_entry *owner = f->buffer_id ? lookup(&s->owners, f->buffer_id, 0) : NULL;
    if (!owner && (begin || (f->buffer_id && role == STANDALONE))) {
        owner = add_owner(s, f->buffer_id);
        if (!owner) return JOURNAL_CODEC_NO_SPACE;
    }
    slot *pending = NULL;
    if (role != STANDALONE) {
        if (owner) pending = role == COMMAND_SLOT ? &owner->command : &owner->checkpoint;
        if (!slot_step(pending, f, role)) return JOURNAL_CODEC_INVALID;
    } else if (owner && is_command(f->type)) {
        owner->command = (slot){0};
    }
    high_water(&s->plan->buffer_high_water, f->buffer_id);
    if (begin) {
        pending->covered = owner->boundary;
        pending->binding = owner->binding;
        pending->saved = owner->saved;
    } else if (role == STANDALONE || commit) {
        if (!index_operation(s, owner, pending, f, offset)) return JOURNAL_CODEC_NO_SPACE;
    } else if (!index_member(s, pending, f, offset)) {
        return JOURNAL_CODEC_NO_SPACE;
    }
    if (commit) *pending = (slot){0};
    return JOURNAL_CODEC_OK;
}

/* Physical verdicts come only from the preamble, frames and slot grammar. */
static journal_codec_err scan_image(scan *s, size_t length, journal_stream_result *prefix)
{
    *prefix = (journal_stream_result){0};
    journal_codec_err err = journal_preamble_decode(s->image, length, &prefix->preamble);
    if (err != JOURNAL_CODEC_OK) return err;
    s->plan->generation_high_water = prefix->preamble.generation_high_water;
    s->plan->buffer_high_water = prefix->preamble.buffer_high_water;
    size_t offset = JOURNAL_PREAMBLE_BYTES;
    while (offset < length) {
        if (prefix->last_sequence == UINT64_MAX) {
            err = JOURNAL_CODEC_SEQUENCE;
            break;
        }
        journal_frame frame;
        err = journal_record_decode(s->image + offset, length - offset,
                                    offset - JOURNAL_PREAMBLE_BYTES,
                                    prefix->last_sequence + 1, &frame);
        if (err != JOURNAL_CODEC_OK) break;
        if (frame.type != JOURNAL_REC_PAD) {
            err = scan_record(s, &frame, offset);
            if (err != JOURNAL_CODEC_OK) break;
            prefix->records++;
            prefix->last_sequence = frame.sequence;
        }
        offset += frame.total_len;
    }
    prefix->valid_bytes = offset;
    return err;
}

static bool add_origin(scan *s, const indexed_operation *indexed,
                        const journal_operation *source, journal_origin_test test)
{
    journal_origin *origin = NEW(s, journal_origin);
    if (!origin) return false;
    journal_owner_plan *owner = &indexed->owner->plan;
    *origin = (journal_origin){ .test = test, .source = source, .decision = &indexed->plan,
        .covered = indexed->covered, .older = owner->origins };
    owner->origins = origin;
    return true;
}

/* Pass two threads the shared ordered commands and per-owner candidates
 * through the already certified offsets. A terminal suppresses its
 * PREPARED candidates even when a later observation rejects the binding. */
static bool describe_plan(scan *s)
{
    for (const journal_operation *op = s->plan->operations; op; op = op->next) {
        const indexed_operation *indexed = (const indexed_operation *)op;
        if (is_command(indexed->type)) {
            owner_entry *owner = indexed->owner;
            if (owner->last_command) owner->last_command->next_owner_command = op;
            else owner->plan.commands = op;
            owner->last_command = (journal_operation *)op;
        } else if (indexed->type == JOURNAL_REC_BASE) {
            if (!add_origin(s, indexed, op, JOURNAL_VERIFIED_BASE)) return false;
        } else if (op->use == JOURNAL_ELIGIBLE) {
            if (indexed->type == JOURNAL_REC_CHECKPOINT_COMMIT) {
                if (!add_origin(s, indexed, op, JOURNAL_INDEPENDENT_TEXT)) return false;
            } else if (indexed->type == JOURNAL_REC_SAVE) {
                if (indexed->token && indexed->token->phase == TOKEN_OPEN) {
                    if (!add_origin(s, indexed, op, JOURNAL_EXPECTED_BINDING) ||
                        !add_origin(s, indexed, op, JOURNAL_INSTALLED_WITNESS)) return false;
                } else if (op->declaration) {
                    if (!add_origin(s, indexed, op, JOURNAL_INSTALLED_WITNESS)) return false;
                } else if (op->checkpoint) {
                    if (!add_origin(s, indexed, op->checkpoint, JOURNAL_INDEPENDENT_TEXT)) return false;
                }
            }
        }
    }
    return true;
}

journal_codec_err journal_plan_read(const uint8_t *image, size_t length,
                                    base_arena *workspace, journal_logical_plan *out)
{
    ASSERT(out && workspace && (image || !length));
    base_arena_mark_t mark = base_arena_mark(workspace);
    *out = (journal_logical_plan){0};
    scan s = { .image = image, .arena = workspace, .plan = out };
    journal_codec_err err = scan_image(&s, length, &out->prefix);
    if (err != JOURNAL_CODEC_NO_SPACE && out->prefix.valid_bytes && !describe_plan(&s))
        err = JOURNAL_CODEC_NO_SPACE;
    if (err == JOURNAL_CODEC_NO_SPACE || !out->prefix.valid_bytes) {
        base_arena_reset_to_mark(workspace, mark);
        *out = (journal_logical_plan){0};
    }
    return err;
}

static bool add_bytes(size_t *total, size_t count, size_t stride)
{
    if (count > (SIZE_MAX - *total) / stride) return false;
    *total += count * stride;
    return true;
}

#define ALLOCATION_COST(type) (sizeof(type) + _Alignof(type) - 1)

size_t journal_plan_workspace_size(size_t records, size_t buffers,
                                   size_t keys, size_t tokens)
{
    /* (P) At half load the final capacity is less than four buckets
     * per inserted entry. The preceding geometric capacities sum to
     * less than that again, with at most one allocation per entry.
     * R also bounds the final-sequence table, one frame ref and one
     * operation per record, and at most two origins per operation. */
    size_t table = 8 * sizeof(index_entry) + _Alignof(index_entry) - 1;
    size_t total = 1; /* Distinguish the empty-image bound from overflow. */
    if (!add_bytes(&total, records, table + ALLOCATION_COST(indexed_operation) +
                                    ALLOCATION_COST(journal_frame_ref) +
                                    2 * ALLOCATION_COST(journal_origin)) ||
        !add_bytes(&total, buffers, table + ALLOCATION_COST(owner_entry)) ||
        !add_bytes(&total, keys, table + ALLOCATION_COST(key_entry)) ||
        !add_bytes(&total, tokens, table + ALLOCATION_COST(token_entry))) return 0;
    return total;
}
