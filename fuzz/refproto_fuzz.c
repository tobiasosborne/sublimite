/* Tool-only wire/numeric/clock helpers; live preflight is covered on :99 by
 * refproto_test and refwin_test. No X connection or process-global fixture. */
#include "../tools/refwin_protocol.h"
#include "base/base.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint32_t words[REFPROTO_WORDS] = {0}, encoded[REFPROTO_WORDS];
    size_t copied = size < sizeof words ? size : sizeof words;
    if (copied) memcpy(words,data,copied);
    refproto_row row;
    refproto_unpack(words,&row); refproto_pack(&row,encoded);
    EDIT_ASSERT(memcmp(words,encoded,18u*sizeof(uint32_t)) == 0);
    EDIT_ASSERT(encoded[18] == 0 && encoded[19] == 0);

    refproto_clock a = {row.inject,row.msc}, b = {row.t4,row.t5};
    uint64_t period = 0;
    if (!refproto_period(a,b,&period)) {
        EDIT_ASSERT(b.ns > a.ns && b.msc > a.msc);
        EDIT_ASSERT(period == (b.ns-a.ns)/(b.msc-a.msc));
        EDIT_ASSERT(period >= UINT64_C(1000000) && period <= UINT64_C(1000000000));
    }
    EDIT_ASSERT(refproto_period_matches(row.phase,row.period) ==
                refproto_period_matches(row.period,row.phase));
    EDIT_ASSERT(refproto_period_matches(row.period,row.period) == (row.period != 0));

    char number[256];
    copied = size < sizeof number-1u ? size : sizeof number-1u;
    if (copied) memcpy(number,data,copied);
    number[copied] = 0;
    uint64_t value = 0;
    if (!refproto_number(number,row.period,&value)) EDIT_ASSERT(value <= row.period);
    return 0;
}
