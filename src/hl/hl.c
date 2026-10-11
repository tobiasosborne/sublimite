#include "hl/hl.h"
#include <string.h>

/* Protocol offsets derive from the preceding fixed-width fields, excluding
 * C struct padding. The transport has WORK_MSG_DATA bytes available. */
#define END_OFFSET sizeof(uint64_t)
#define PHASE_OFFSET (END_OFFSET + sizeof(uint64_t))
_Static_assert(PHASE_OFFSET + sizeof(uint32_t) <= WORK_MSG_DATA,
               "highlight result must fit the worker mailbox");

static bool result_valid(const hl_result *result)
{
    return result && result->first_byte <= result->end_byte &&
        (result->phase == HL_VIEWPORT_READY ||
         result->phase == HL_DOCUMENT_READY || result->phase == HL_PARSE_FAILED);
}

bool hl_frame_budget_ns(uint64_t refresh_interval_ns, uint64_t *budget_ns)
{
    if (!budget_ns || !refresh_interval_ns) return false;
    *budget_ns = refresh_interval_ns;
    return true;
}

bool hl_result_encode(work_msg *message, uint32_t generation, const hl_result *result)
{
    if (!message || !result_valid(result)) return false;
    memset(message, 0, sizeof *message);
    message->kind = HL_RESULT_KIND;
    message->generation = generation;
    memcpy(message->data, &result->first_byte, sizeof result->first_byte);
    memcpy(message->data + END_OFFSET, &result->end_byte, sizeof result->end_byte);
    memcpy(message->data + PHASE_OFFSET, &result->phase, sizeof result->phase);
    return true;
}

bool hl_result_decode(const work_msg *message, hl_result *result)
{
    if (!message || !result || message->kind != HL_RESULT_KIND) return false;
    hl_result decoded;
    memcpy(&decoded.first_byte, message->data, sizeof decoded.first_byte);
    memcpy(&decoded.end_byte, message->data + END_OFFSET, sizeof decoded.end_byte);
    memcpy(&decoded.phase, message->data + PHASE_OFFSET, sizeof decoded.phase);
    if (!result_valid(&decoded)) return false;
    *result = decoded;
    return true;
}

bool hl_viewport_ready(const work_msg *message, uint32_t generation,
                       uint64_t first_byte, uint64_t end_byte)
{
    hl_result result;
    return first_byte <= end_byte && hl_result_decode(message, &result) &&
        message->generation == generation && result.phase == HL_VIEWPORT_READY &&
        result.first_byte <= first_byte && result.end_byte >= end_byte;
}
