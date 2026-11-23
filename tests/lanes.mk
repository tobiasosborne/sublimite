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

# os local streams and wait set (v2-0fv.13)
SAN_SUITES += tests/os_endpoint_test.c

# scan (v2-0fv.10)
SAN_SUITES += tests/scan_test.c
FUZZ_SUITES += fuzz/scan_fuzz.c

# utf8 (v2-0fv.11)
SAN_SUITES += tests/utf8_test.c tests/utf8_tables_test.c
FUZZ_SUITES += fuzz/utf8_fuzz.c

# indent (v2-0fv.12)
SAN_SUITES += tests/indent_test.c
FUZZ_SUITES += fuzz/indent_fuzz.c

# os fault ids (v2-0fv.2)
SAN_SUITES += tests/os_fault_test.c
LAW2_SUITES += tests/law2_os_fault_test.c
TSAN_SUITES += tests/os_fault_test.c
FUZZ_SUITES += fuzz/os_fuzz.c

# Per-suite link flags, LDFLAGS_<suite stem>, in every configuration. The fault
# suite injects native failures through these wrappers (CLAUDE.md: --wrap).
LDFLAGS_os_fault_test := -Wl,--wrap=mmap,--wrap=sigaction,--wrap=sysconf,--wrap=syscall \
                         -Wl,--wrap=sem_init,--wrap=sem_destroy,--wrap=sem_wait,--wrap=sem_post

# Per-suite test-hook objects (Makefile CFG_TEST_HOOKS): os built with its
# generation setter (A15) for the fault suites.
$(foreach c,rel san tsan,$(eval $(B)/$(c)/tests/os_fault_test: $(B)/$(c)/src/os/os_posix_test_hooks.o))
$(B)/rel/tests/law2_os_fault_test: $(B)/rel/src/os/os_posix_test_hooks.o
