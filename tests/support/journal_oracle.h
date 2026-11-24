/* Flat reference model for byte images known to have valid slot grammar.
 * At each final, walk back to its matching BEGIN, then compare the exact
 * member projection in physical order. No slot table or reader calls. */
#ifndef JOURNAL_ORACLE_H
#define JOURNAL_ORACLE_H

static uint64_t journal_oracle_word(const uint8_t *p, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static bool journal_oracle_text(uint64_t type)
{
    return type == JOURNAL_REC_INSERT || type == JOURNAL_REC_DELETE ||
           type == JOURNAL_REC_RESTORE_BASE_RANGE;
}

static bool journal_oracle_compare(const uint8_t *image, const size_t *offsets,
                                  size_t count, const journal_logical_plan *plan)
{
    const journal_operation *operation = plan->operations;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *record = image + offsets[i];
        uint64_t type = journal_oracle_word(record + 8, 4);
        uint64_t owner = journal_oracle_word(record + 24, 8);
        bool command = type == JOURNAL_REC_COMMAND_COMMIT;
        bool checkpoint = type == JOURNAL_REC_CHECKPOINT_COMMIT;
        if (!command && !checkpoint && !journal_oracle_text(type)) continue;
        /* Generators emit only text ops, slot frames and PAD. */
        if (!operation || operation->final != offsets[i] || operation->owner != owner ||
            operation->final_sequence != journal_oracle_word(record + 16, 8))
            return false;
        const journal_frame_ref *member = operation->frames;
        if (command || checkpoint) {
            uint64_t begin = command ? JOURNAL_REC_COMMAND_BEGIN : JOURNAL_REC_CHECKPOINT_BEGIN;
            size_t first = i;
            while (first > 0) {
                const uint8_t *prior = image + offsets[--first];
                if (journal_oracle_word(prior + 24, 8) == owner &&
                    journal_oracle_word(prior + 8, 4) == begin) break;
            }
            for (size_t j = first + 1; j < i; j++) {
                const uint8_t *prior = image + offsets[j];
                if (journal_oracle_word(prior + 24, 8) != owner) continue;
                uint64_t role = journal_oracle_word(prior + 8, 4);
                bool in_group = command ?
                    (role == JOURNAL_REC_COMMAND_INSERT || role == JOURNAL_REC_COMMAND_DELETE ||
                     role == JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE) :
                    (role == JOURNAL_REC_CHECKPOINT_BY_VALUE || role == JOURNAL_REC_CHECKPOINT_INSERT ||
                     role == JOURNAL_REC_CHECKPOINT_TARGET);
                if (!in_group) continue;
                if (!member || member->offset != offsets[j]) return false;
                member = member->next;
            }
        } else {
            if (!member || member->offset != offsets[i]) return false;
            member = member->next;
        }
        if (member) return false;
        operation = operation->next;
    }
    return operation == NULL;
}

static const journal_record_type journal_oracle_script[] = {
    JOURNAL_REC_COMMAND_BEGIN, JOURNAL_REC_COMMAND_DELETE,
    JOURNAL_REC_CHECKPOINT_BEGIN, JOURNAL_REC_CHECKPOINT_BY_VALUE,
    JOURNAL_REC_INSERT, JOURNAL_REC_CHECKPOINT_BEGIN,
    JOURNAL_REC_CHECKPOINT_BY_VALUE, JOURNAL_REC_CHECKPOINT_INSERT,
    JOURNAL_REC_COMMAND_BEGIN, JOURNAL_REC_COMMAND_INSERT,
    JOURNAL_REC_CHECKPOINT_COMMIT, JOURNAL_REC_COMMAND_COMMIT,
    JOURNAL_REC_COMMAND_BEGIN, JOURNAL_REC_COMMAND_DELETE
};

/* (P) Two owners suffice for both inter-owner slots and simultaneous
 * command and checkpoint slots on the same owner. One script per owner. */
enum {
    JOURNAL_ORACLE_OWNERS = 2,
    JOURNAL_ORACLE_STEPS = sizeof journal_oracle_script / sizeof *journal_oracle_script,
    JOURNAL_ORACLE_RECORDS = JOURNAL_ORACLE_OWNERS * JOURNAL_ORACLE_STEPS
};

/* Permute an append script with one lane per owner. Each lane is one
 * abandoned command, one superseded checkpoint and their replacements.
 * The schedule byte chooses the next unfinished lane by circular scan.
 * Every byte boundary can serve as the final visible prefix. */
static size_t journal_oracle_image(uint8_t *image, size_t cap, size_t *offsets,
                                  const uint8_t *schedule, size_t schedule_len)
{
    size_t step[JOURNAL_ORACLE_OWNERS] = {0};
    size_t len = JOURNAL_PREAMBLE_BYTES;
    size_t count = JOURNAL_ORACLE_RECORDS;
    journal_preamble_encode(image, 0, 0);
    for (size_t i = 0; i < count; i++) {
        size_t owner = schedule[i % schedule_len] % JOURNAL_ORACLE_OWNERS;
        if (step[owner] == JOURNAL_ORACLE_STEPS) owner ^= 1u;
        journal_record_type type = journal_oracle_script[step[owner]++];
        uint8_t payload[36] = {0}; /* Largest script schema is checkpoint descriptor. */
        size_t length = 0;
        if (type == JOURNAL_REC_COMMAND_DELETE) {
            payload[8] = 1;
            length = 16;
        } else if (type == JOURNAL_REC_CHECKPOINT_BY_VALUE) {
            /* First descriptor describes empty text. The replacement
             * pins the earlier standalone INSERT's boundary. */
            if (step[owner] > 4) {
                for (size_t j = i; j > 0; j--) {
                    const uint8_t *r = image + offsets[j - 1];
                    if (journal_oracle_word(r + 8, 4) == JOURNAL_REC_INSERT &&
                        journal_oracle_word(r + 24, 8) == owner + 1) {
                        memcpy(payload, r + 16, 8);
                        break;
                    }
                }
                payload[24] = 1;
            }
            length = sizeof payload;
        } else if (type == JOURNAL_REC_INSERT || type == JOURNAL_REC_COMMAND_INSERT ||
                   type == JOURNAL_REC_CHECKPOINT_INSERT) {
            payload[0] = type == JOURNAL_REC_COMMAND_INSERT ? 1 : 0;
            payload[8] = type == JOURNAL_REC_COMMAND_INSERT ? '!' : 'a';
            length = 9;
        }
        offsets[i] = len;
        size_t written = 0;
        if (journal_record_encode(image + len, cap - len, &written, type, i + 1,
                                  owner + 1, payload, length, len - JOURNAL_PREAMBLE_BYTES)
            != JOURNAL_CODEC_OK) return 0;
        len += written;
    }
    offsets[count] = len;
    return count;
}
#endif
