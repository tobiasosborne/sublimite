#ifndef TESTS_EDIT_JOURNAL_H
#define TESTS_EDIT_JOURNAL_H

#include "journal/journal.h"

/* The six separable begin claims from the runtime contract. */
typedef enum edit_journal_claim {
    EJ_COMMAND, EJ_BYTES, EJ_SEQUENCES, EJ_WINDOW, EJ_REQUEST, EJ_IMAGE_PIN,
    EJ_CLAIMS
} edit_journal_claim;

typedef struct edit_journal_info {
    size_t frames, bytes, largest, committed, open, requests, reserved_bytes;
    size_t inserts, deletes, ranges, aborts, finals, range_checks;
} edit_journal_info;

journal *ej_create(size_t bytes, size_t frames);
void ej_destroy(journal *j);
void ej_original(journal *j, uint64_t key, const uint8_t *bytes, size_t len);
void ej_embargo(journal *j);
void ej_refuse_begin(journal *j, edit_journal_claim claim, journal_err err);
void ej_refuse_member(journal *j, journal_err err);
void ej_short(journal *j, journal_record_type type);
void ej_health(journal *j, journal_condition state);
edit_journal_info ej_info(const journal *j);
/* Read accepted logical effects from the physical ledger, omitting any
 * unfinished group. Bytes are decoded independently of the wire codec. */
size_t ej_read(const journal *j, uint64_t owner, const uint8_t *base, size_t len,
               uint8_t *out, size_t cap);

#endif
