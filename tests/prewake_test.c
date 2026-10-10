/* Collect the existing per-candidate contract into make all/check. */
#define PREWAKE_VARIANT 0
#define PREWAKE_IMPL "a/prewake.c"
#define main prewake_test_a
#define prewake_hint prewake_hint_a
#define prewake_activity prewake_activity_a
#define fake prewake_fake_a
#define prewake_contract_warm prewake_warm_a
#define prewake_contract_spin prewake_spin_a
#include "../bench/prewake/contract.c"
#undef PREWAKE_VARIANT
#undef PREWAKE_IMPL
#undef main
#undef prewake_hint
#undef prewake_activity
#undef fake
#undef prewake_contract_warm
#undef prewake_contract_spin

#define PREWAKE_VARIANT 1
#define PREWAKE_IMPL "b/prewake.c"
#define main prewake_test_b
#define prewake_hint prewake_hint_b
#define prewake_activity prewake_activity_b
#define prewake_trigger prewake_trigger_b
#define fake prewake_fake_b
#define prewake_contract_warm prewake_warm_b
#define prewake_contract_spin prewake_spin_b
#include "../bench/prewake/contract.c"
#undef PREWAKE_VARIANT
#undef PREWAKE_IMPL
#undef main
#undef prewake_hint
#undef prewake_activity
#undef prewake_trigger
#undef fake
#undef prewake_contract_warm
#undef prewake_contract_spin

#define PREWAKE_VARIANT 2
#define PREWAKE_IMPL "c/prewake.c"
#define main prewake_test_c
#define prewake_hint prewake_hint_c
#define prewake_activity prewake_activity_c
#define prewake_trigger prewake_trigger_c
#define fake prewake_fake_c
#define prewake_contract_warm prewake_warm_c
#define prewake_contract_spin prewake_spin_c
#include "../bench/prewake/contract.c"
#undef main

int main(void)
{
    return prewake_test_a() || prewake_test_b() || prewake_test_c();
}
