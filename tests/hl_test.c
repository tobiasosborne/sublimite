/* edit-v0o.2 mailbox contract. Ranges are derived from the fixture bytes;
 * refresh comes from the caller, never from a guessed display rate. */
#include "hl/hl.h"
#include "base/base.h"

#include <stdio.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while (0)

int main(void)
{
    const char text[] = "prefix\nviewport\nsuffix\n";
    const uint64_t first = sizeof "prefix\n" - 1u;
    const uint64_t end = first + sizeof "viewport\n" - 1u;
    /* Derivation: the probe uses the fixture's byte length as a nonzero
     * synthetic refresh input; it asserts an identity, not a timing gate. */
    const uint64_t refresh = sizeof text - 1u;
    const uint32_t generation = (uint32_t)sizeof text;
    uint64_t budget = 0;
    REQUIRE(hl_frame_budget_ns(refresh, &budget));
    REQUIRE(budget == refresh);
    REQUIRE(!hl_frame_budget_ns(0, &budget)); /* no measured refresh available */
    REQUIRE(!hl_frame_budget_ns(refresh, NULL));

    work_msg message = {0};
    hl_result result = {first, end, HL_VIEWPORT_READY};
    hl_result decoded = {0};
    REQUIRE(!hl_viewport_ready(&message, generation, first, end));
    edit_malloc_guard_begin();
    REQUIRE(hl_result_encode(&message, generation, &result));
    REQUIRE(hl_result_decode(&message, &decoded));
    REQUIRE(hl_viewport_ready(&message, generation, first, end));
    REQUIRE(!hl_viewport_ready(&message, generation + 1u, first, end));
    REQUIRE(!hl_viewport_ready(&message, generation, 0, end));
    REQUIRE(!hl_viewport_ready(&message, generation, first, end + 1u));
    REQUIRE(!hl_viewport_ready(&message, generation, end, first));
    size_t allocations = edit_malloc_guard_end();
    REQUIRE(allocations == 0); /* typing-path allocation law */
    REQUIRE(decoded.first_byte == first && decoded.end_byte == end);

    result.phase = HL_DOCUMENT_READY;
    REQUIRE(hl_result_encode(&message, generation, &result));
    REQUIRE(!hl_viewport_ready(&message, generation, first, end));
    result.phase = HL_PARSE_FAILED;
    REQUIRE(hl_result_encode(&message, generation, &result));
    REQUIRE(!hl_viewport_ready(&message, generation, first, end));
    result.phase = HL_PARSE_FAILED + 1u; /* first value beyond assigned phases */
    REQUIRE(!hl_result_encode(&message, generation, &result));
    memcpy(message.data + sizeof(uint64_t) + sizeof(uint64_t),
           &result.phase, sizeof result.phase);
    REQUIRE(!hl_result_decode(&message, &decoded));
    result.phase = HL_VIEWPORT_READY;
    result.first_byte = end;
    result.end_byte = first;
    REQUIRE(!hl_result_encode(&message, generation, &result));
    message.kind = 0; /* unassigned mailbox kind */
    REQUIRE(!hl_result_decode(&message, &decoded));
    REQUIRE(!hl_result_encode(NULL, generation, &result));
    REQUIRE(!hl_result_decode(NULL, &decoded));
    REQUIRE(!hl_result_decode(&message, NULL));
    puts("hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard");
    return 0;
}
