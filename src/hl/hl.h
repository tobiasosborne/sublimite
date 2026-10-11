#ifndef EDIT_HL_H
#define EDIT_HL_H

#include "work/work.h"
#include <stdbool.h>
#include <stdint.h>

/* Mailbox kind is the ASCII bytes 'H', 'L' packed into an integer. No
 * pointers cross this boundary: syntax trees remain worker-owned. */
#define HL_RESULT_KIND (((uint32_t)'H' << 8u) | (uint32_t)'L')
enum hl_phase {
    HL_VIEWPORT_READY = 1, /* first valid phase; zero is unassigned */
    HL_DOCUMENT_READY,    /* follows viewport publication */
    HL_PARSE_FAILED
};
typedef struct hl_result {
    uint64_t first_byte, end_byte; /* half-open snapshot byte range */
    uint32_t phase;
} hl_result;

/* (P) one-frame slice = one caller-supplied refresh interval, in ns.
 * Zero means refresh unknown and is refused; no guessed rate or fallback.
 * This is only the budget contract; no parser worker exists yet. */
bool hl_frame_budget_ns(uint64_t refresh_interval_ns, uint64_t *budget_ns);

/* Encode before work_publish; generation identifies the immutable snapshot.
 * Decode/readiness run on the UI, without allocation or worker-state access.
 * Plain text remains the fallback until a current viewport result covers the
 * requested range. A document result cannot substitute for that publication.
 * Future grammar/query code must also validate its own scope data. */
bool hl_result_encode(work_msg *message, uint32_t generation, const hl_result *result);
bool hl_result_decode(const work_msg *message, hl_result *result);
bool hl_viewport_ready(const work_msg *message, uint32_t generation,
                       uint64_t first_byte, uint64_t end_byte);

#endif
