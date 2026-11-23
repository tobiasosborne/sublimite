# Reviewed check-lane manifest. Paths are relative to the checkout.
# Register each new *_test.c or *_fuzz.c in the lane(s) that must run it.
LAYER_SUITES := tests/layer_L0_test.c tests/layer_L1_test.c tests/layer_L2_test.c \
                tests/layer_L3_test.c tests/layer_L4_test.c
SAN_SUITES := $(LAYER_SUITES) tests/isolation_test.c tests/os_test.c tests/base_test.c tests/ipc_test.c

LAW2_SUITES := tests/law2_base_test.c
TSAN_SUITES := tests/os_test.c tests/base_test.c
FUZZ_SUITES := fuzz/base_fuzz.c

# trace (v2-0fv.4)
SAN_SUITES += tests/trace_test.c
LAW2_SUITES += tests/law2_trace_test.c
TSAN_SUITES += tests/trace_test.c
FUZZ_SUITES += fuzz/trace_fuzz.c

# work (v2-0fv.5)
SAN_SUITES += tests/work_test.c
LAW2_SUITES += tests/law2_work_test.c
TSAN_SUITES += tests/work_test.c
FUZZ_SUITES += fuzz/work_fuzz.c

# ipc (v2-0fv.6)
TSAN_SUITES += tests/ipc_test.c
FUZZ_SUITES += fuzz/ipc_fuzz.c

# scan (v2-0fv.10)
SAN_SUITES += tests/scan_test.c
FUZZ_SUITES += fuzz/scan_fuzz.c

# utf8 (v2-0fv.11)
SAN_SUITES += tests/utf8_test.c tests/utf8_tables_test.c
FUZZ_SUITES += fuzz/utf8_fuzz.c
