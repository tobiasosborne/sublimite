# GNU Makefile. Globs everything: drop files in src/<m>/, tests/<m>_test.c,
# bench/<m>_bench.c, fuzz/<m>_fuzz.c, tools/<name>.c; never edit this file.
CC_RELEASE ?= gcc
CC_SAN     ?= clang
# clang 18 on this box selects a half-installed gcc-14 dir and cannot find -lstdc++ (libFuzzer needs it); pin gcc 13. See docs/toolchain.md.
GCC_DIR    ?= $(firstword $(wildcard /usr/lib/gcc/x86_64-linux-gnu/13))
CLANG_GCC  := $(if $(GCC_DIR),--gcc-install-dir=$(GCC_DIR),)
AR         ?= ar

WARN   := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread
INC    := -Isrc
DEP    := -MMD -MP
REL_F  := -O2 -g -msse2
SAN_F  := -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g
FUZZ_F := -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -O1 -g
LDLIBS := -lm -ldl
# Per-module extra libraries: a one-line file src/<m>/LDLIBS (e.g. "-lxcb -lxkbcommon") is appended to every link.
LDLIBS += $(foreach f,$(wildcard src/*/LDLIBS),$(shell cat $(f)))

B := build
SRCS   := $(sort $(wildcard src/*/*.c))
MAINC  := $(wildcard src/main.c)
TESTS  := $(sort $(wildcard tests/*_test.c))
BENCHS := $(sort $(wildcard bench/*_bench.c))
FUZZS  := $(sort $(wildcard fuzz/*_fuzz.c))
TOOLS  := $(sort $(wildcard tools/*.c))

REL_OBJ  := $(SRCS:%.c=$(B)/rel/%.o)
SAN_OBJ  := $(SRCS:%.c=$(B)/san/%.o)
FUZZ_OBJ := $(SRCS:%.c=$(B)/fuzz/%.o)

REL_LIB  := $(B)/libedit.a
SAN_LIB  := $(B)/san/libedit.a
FUZZ_LIB := $(B)/fuzz/libedit.a

TEST_REL  := $(patsubst tests/%.c,$(B)/tests/%,$(TESTS))
TEST_SAN  := $(patsubst tests/%.c,$(B)/san/tests/%,$(TESTS))
BENCH_BIN := $(patsubst bench/%.c,$(B)/bench/%,$(BENCHS))
FUZZ_BIN  := $(patsubst fuzz/%.c,$(B)/fuzz/%,$(FUZZS))
TOOL_BIN  := $(patsubst tools/%.c,$(B)/tools/%,$(TOOLS))
EDIT_BIN  := $(if $(MAINC),$(B)/edit)

.PHONY: all lib check check-sh bench fuzz clean
.SECONDARY:
all: $(REL_LIB) $(EDIT_BIN) $(TOOL_BIN) $(TEST_REL) $(BENCH_BIN)
lib: $(REL_LIB)

# ---- objects (one tree per configuration) ----
$(B)/rel/%.o: %.c
	@mkdir -p $(@D)
	$(CC_RELEASE) $(WARN) $(REL_F) $(INC) $(DEP) -c $< -o $@
$(B)/san/%.o: %.c
	@mkdir -p $(@D)
	$(CC_SAN) $(WARN) $(SAN_F) $(INC) $(DEP) -c $< -o $@
$(B)/fuzz/%.o: %.c
	@mkdir -p $(@D)
	$(CC_SAN) $(WARN) $(FUZZ_F) $(INC) $(DEP) -c $< -o $@

# ---- libraries ----
$(REL_LIB): $(REL_OBJ)
	@mkdir -p $(@D)
	rm -f $@; $(AR) rcs $@ $^
$(SAN_LIB): $(SAN_OBJ)
	@mkdir -p $(@D)
	rm -f $@; $(AR) rcs $@ $^
$(FUZZ_LIB): $(FUZZ_OBJ)
	@mkdir -p $(@D)
	rm -f $@; $(AR) rcs $@ $^

# ---- binaries ----
$(B)/edit: $(B)/rel/src/main.o $(REL_LIB)
	$(CC_RELEASE) $(REL_F) -pthread $< $(REL_LIB) $(LDLIBS) -o $@
$(B)/tools/%: $(B)/rel/tools/%.o $(REL_LIB)
	@mkdir -p $(@D)
	$(CC_RELEASE) $(REL_F) -pthread $< $(REL_LIB) $(LDLIBS) -o $@
$(B)/tests/%: $(B)/rel/tests/%.o $(REL_LIB)
	@mkdir -p $(@D)
	$(CC_RELEASE) $(REL_F) -pthread $< $(REL_LIB) $(LDLIBS) -o $@
$(B)/bench/%: $(B)/rel/bench/%.o $(REL_LIB)
	@mkdir -p $(@D)
	$(CC_RELEASE) $(REL_F) -pthread $< $(REL_LIB) $(LDLIBS) -o $@
$(B)/san/tests/%: $(B)/san/tests/%.o $(SAN_LIB)
	@mkdir -p $(@D)
	$(CC_SAN) $(SAN_F) -pthread $< $(SAN_LIB) $(LDLIBS) -o $@
$(B)/fuzz/%_fuzz: $(B)/fuzz/fuzz/%_fuzz.o $(FUZZ_LIB)
	@mkdir -p $(@D)
	$(CC_SAN) $(CLANG_GCC) -fsanitize=fuzzer,address,undefined -pthread $< $(FUZZ_LIB) $(LDLIBS) -o $@

# ---- phony drivers ----
# Test binaries are built in sanitizer config under build/san/tests/.
check: $(TEST_SAN) $(B)/tools/replay
	@set -e; for t in $(TEST_SAN); do echo "== $$t"; $$t; done; echo "check: $(words $(TEST_SAN)) test binaries passed"
	@echo "== tools/test_replay_cli.sh"; sh tools/test_replay_cli.sh $(B)

# Shell contract tests that need release benches (slow, ~1 min): run on demand.
check-sh: $(B)/tools/replay $(B)/bench/piece_bench
	sh tools/test_replay_cli.sh $(B)
	sh tools/test_bench_variant.sh

# Benches: release build, each must exit 0 (gate met).
# Per-bench default args: optional one-line file bench/<name>.args (e.g. piece_bench.args = --quick;
# the full piece matrix is run by tools/bench_variant.sh on an idle box).
bench: $(BENCH_BIN)
	@set -e; for b in $(BENCH_BIN); do a=bench/$$(basename $$b).args; args=$$(cat $$a 2>/dev/null || true); echo "== $$b $$args"; $$b $$args; done; echo "bench: $(words $(BENCH_BIN)) benches passed"

fuzz: $(FUZZ_BIN)
	@echo "fuzz: $(words $(FUZZ_BIN)) fuzzers built"

clean:
	rm -rf $(B)

-include $(shell find $(B) -name '*.d' 2>/dev/null)
