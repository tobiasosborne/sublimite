/* Fuzz the mailbox protocol; snapshot edit parsing is in hl_edit_fuzz.c. */
#include "hl/hl.h"
#include "base/base.h"
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    work_msg message = {0};
    if (size > sizeof message) size = sizeof message;
    memcpy(&message, data, size);
    hl_result result;
    if (hl_result_decode(&message, &result)) {
        work_msg encoded;
        hl_result roundtrip;
        EDIT_ASSERT(hl_result_encode(&encoded, message.generation, &result));
        EDIT_ASSERT(hl_result_decode(&encoded, &roundtrip));
        EDIT_ASSERT(roundtrip.first_byte == result.first_byte);
        EDIT_ASSERT(roundtrip.end_byte == result.end_byte);
        EDIT_ASSERT(roundtrip.phase == result.phase);
        EDIT_ASSERT(hl_viewport_ready(&encoded, message.generation,
                    result.first_byte, result.end_byte) == (result.phase == HL_VIEWPORT_READY));
    }
    return 0;
}
