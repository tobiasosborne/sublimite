# Reviewed check-lane manifest. Paths are relative to the checkout.
# Register each new *_test.c or *_fuzz.c in the lane(s) that must run it.
LAYER_SUITES := tests/layer_L0_test.c tests/layer_L1_test.c tests/layer_L2_test.c \
                tests/layer_L3_test.c tests/layer_L4_test.c
SAN_SUITES := $(LAYER_SUITES) tests/isolation_test.c tests/os_test.c

# Wave 0 declarations. Remove the first-wave marker when adding the first suite.
# Wave 1 supplies the release guard and parsing/mutation code.
LAW2_SUITES :=
LAW2_EMPTY_WAVE := 1
TSAN_SUITES := tests/os_test.c
FUZZ_SUITES :=
FUZZ_EMPTY_WAVE := 1
