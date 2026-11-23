/* piece fuzzer (v2-0fv.7): the differential state machine of
 * tests/piece_harness.h, driven by the input. Every operation runs on the
 * implementation under test and on the flat model (tests/piece_model.h),
 * with base refusal armed around the IUT call when the operation's top bit
 * is set; any difference traps.
 *
 * Input: [mode][len 2][fill][ops...], each op 9 bytes (ph_step):
 *   mode bit 0: open over a mapped memfd instead of a private copy;
 *   len: initial text length, at most 3 x PIECE_CHUNK. */
#include "../tests/piece_harness.h"

#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint8_t scratch[PH_SCRATCH];
static uint8_t init[3 * PIECE_CHUNK];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ph_in in = {data, size};
    unsigned mode = ph_u8(&in) & 1u;
    size_t n = ((size_t)ph_u8(&in) << 8 | ph_u8(&in)) * 3u;
    if (n > sizeof init) n = sizeof init;
    ph_fill(init, n, ph_u8(&in));
    ph_mapping map;
    ph_state st;
    ph_failures = 0;
    if (ph_open(&st, init, n, n ? mode : 0u, &map)) {
        while (ph_step(&st, &in, scratch) && ph_failures == 0) {}
        ph_check_snaps(&st, "final snapshots");
    }
    ph_close(&st);
    if (ph_failures) __builtin_trap();
    return 0;
}
