# sublimite v2 -- GNU Makefile (docs/PLAN_V2.md §2, §5).
#
# Layout: mk/layers.mk declares the 5 layers and their archives (no monolithic
# libedit.a), mk/grammars.mk the vendored tree-sitter table (a no-op until
# vendor/ is imported). Register tests and fuzzers in tests/lanes.mk.
#
# Targets: all  check  check-lints  check-layers  check-san  check-law2
#          check-tsan  fuzz  fuzz-smoke  clean
#
# Independent objects use the available CPUs; an explicit make -j still wins.
ifeq ($(MAKELEVEL)$(filter -j% --jobserver%,$(MAKEFLAGS)),0)
MAKEFLAGS += -j$(shell nproc)
endif

CC_RELEASE ?= gcc
CC_SAN     ?= clang
# clang 18 on this box selects a half-installed gcc-14 dir and cannot find
# -lstdc++ (libFuzzer needs it); pin gcc 13 (same pin as v1's Makefile).
GCC_DIR    ?= $(firstword $(wildcard /usr/lib/gcc/x86_64-linux-gnu/13))
CLANG_GCC  := $(if $(GCC_DIR),--gcc-install-dir=$(GCC_DIR),)
AR         ?= ar

# App identity: every OS path and IPC leaf is built from EDIT_APP_ID, never a
# literal (tools/check_isolation.py), so v2 cannot touch v1's XDG/IPC names.
APP_ID := sublimite-v2
ifneq ($(APP_ID),sublimite-v2)
$(error APP_ID must be sublimite-v2)
endif
WARN   := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wswitch-enum -D_GNU_SOURCE -pthread
INC    := -Isrc -Ivendor/tree-sitter/lib/include
DEP    := -MMD -MP

REL_F   := -O2 -g -msse2
SAN_F   := -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g
TSAN_F  := -fsanitize=thread -fno-omit-frame-pointer -O1 -g
FUZZ_F  := -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -O1 -g

# Configuration trees: build/rel (gcc -O2, Law-2/G1 lane + shipped binary),
# build/san (clang ASan+UBSan), build/tsan (clang TSan), build/fuzz (libFuzzer).
CFGS      := rel san tsan fuzz
CC_rel    := $(CC_RELEASE)
CC_san    := $(CC_SAN)
CC_tsan   := $(CC_SAN)
CC_fuzz   := $(CC_SAN)
CF_rel    := $(REL_F)
CF_san    := $(SAN_F)
CF_tsan   := $(TSAN_F)
CF_fuzz   := $(FUZZ_F)

LDLIBS := -lm -ldl
# Per-module extra libraries: a one-line file src/<m>/LDLIBS (e.g. "-lxcb -lxkbcommon") is appended to every link.
LDLIBS += $(foreach f,$(wildcard src/*/LDLIBS),$(shell cat $(f)))

B := build

include mk/grammars.mk
-include tests/lanes.mk
include mk/layers.mk

# Include unlinked translation units under src/, tests/ and fuzz/. The test
# fixture trees have their own runners and fuzz/corpus/ contains input data.
C_FILES := -name '*.c' \( -type f -o -xtype f \) -print
lint_sources = $(if $(wildcard $(1)),$(shell find $(1) $(2) $(C_FILES))\
    $(if $(filter-out 0,$(.SHELLSTATUS)),$(error failed to enumerate translation units under $(1))))
LINT_SRC_SRCS := $(call lint_sources,src)
LINT_TEST_SRCS := $(call lint_sources,tests,\( -path tests/lint -o -path tests/symbols -o -path tests/lanes \) -prune -o)
LINT_FUZZ_SRCS := $(call lint_sources,fuzz,-path fuzz/corpus -prune -o)
LINT_REL_SRCS := $(LINT_SRC_SRCS) $(LINT_TEST_SRCS)
LINT_OBJS := $(LINT_REL_SRCS:%.c=$(B)/rel/%.o) $(LINT_FUZZ_SRCS:%.c=$(B)/fuzz/%.o)

TEST_SAN  := $(patsubst tests/%.c,$(B)/san/tests/%,$(SAN_SUITES))
TEST_REL  := $(patsubst tests/%.c,$(B)/rel/tests/%,$(filter-out $(LAYER_SUITES),$(sort $(SAN_SUITES) $(LAW2_SUITES) $(TSAN_SUITES))))
TEST_LAW2 := $(patsubst tests/%.c,$(B)/rel/tests/%,$(LAW2_SUITES))
TEST_TSAN := $(patsubst tests/%.c,$(B)/tsan/tests/%,$(TSAN_SUITES))
FUZZ_BIN  := $(patsubst fuzz/%.c,$(B)/fuzz/%,$(FUZZ_SUITES))
EDIT_BIN  := $(B)/$(APP_ID)
# Pull the interposer from libcore.a even when allocation is only in a DSO.
REL_GUARD := -Wl,--undefined=base_malloc_guard_active

# Per-task test timeout: unset (none). Set TEST_TIMEOUT=<secs> only with a
# derivation beside it (CLAUDE.md law 4); no silent retries either way.
RUN = $(if $(TEST_TIMEOUT),timeout $(TEST_TIMEOUT) )

.PHONY: all check check-lints check-layout check-suites check-includes \
        check-layers check-san check-law2 check-tsan fuzz fuzz-smoke clean
.SECONDARY:
.DEFAULT_GOAL := all

all: $(EDIT_BIN) $(call libs_desc,rel) $(LAYER_TEST_BINS) $(LAYER_L5_BIN) $(TEST_REL)

# Persistent X11 contract helper and independently linked conformance client.
.PHONY: xdriver check-xdriver
xdriver: $(B)/xdriver
$(B)/xdriver $(B)/xdriver-test-client: | $(B)
$(B)/xdriver: tests/contract/xdriver.c
	$(CC_RELEASE) $(WARN) $(REL_F) $< -lxcb -lxkbcommon-x11 -lxkbcommon -ldl -o $@
$(B)/xdriver-test-client: tests/contract/xdriver_test_client.c
	$(CC_RELEASE) $(WARN) $(REL_F) $< -lxcb -o $@
$(B):
	mkdir -p $@
check-xdriver: $(B)/xdriver $(B)/xdriver-test-client
	python3 -B tests/contract/test_xdriver.py

ORACLE_JOBS ?= 1
.PHONY: check-contract-oracle
check-contract-oracle: $(B)/xdriver
	@test -n "$(ORACLE_TREE)" && test -n "$(ORACLE_DISPLAY)" || { echo "Set ORACLE_TREE and ORACLE_DISPLAY (:130 through :139)" >&2; exit 2; }
	python3 -B tests/contract/build_oracle.py --jobs "$(ORACLE_JOBS)" "$(ORACLE_TREE)"
	python3 -B tests/contract/test_oracle_inspector.py --build-dir "$(ORACLE_TREE)/build/contract-oracle" --driver "$(abspath $(B)/xdriver)" --display "$(ORACLE_DISPLAY)"

.PHONY: check-contract-recovery
check-contract-recovery: $(B)/xdriver
	python3 -B tests/contract/cases/recovery.py --oracle "$$ORACLE" \
		--display "$$RECOVERY_DISPLAY" --case "$${RECOVERY_CASE:-pre-ack}"

# ---- objects (one tree per configuration) ----
define CFG_COMPILE
$(B)/$(1)/%.o: %.c
	@mkdir -p $$(@D)
	$$(CC_$(1)) $$(WARN) $$(CF_$(1)) $$(INC) $$(DEP) -DEDIT_APP_ID=\"$$(APP_ID)\" -c $$< -o $$@
endef
$(foreach c,$(CFGS),$(eval $(call CFG_COMPILE,$(c))))

# The release binary owns the allocation hooks. Sanitizers own them in all
# other configurations.
$(B)/rel/src/base/malloc_guard.o: private CF_rel += -DBASE_MALLOC_GUARD_RELEASE

# ---- binaries ----
# Strict topological link: main.o, then L5 .. L0; a downward-only DAG needs no
# --start-group.
$(EDIT_BIN): $(B)/rel/src/main.o $(call libs_desc,rel)
	@mkdir -p $(@D)
	$(CC_RELEASE) $(REL_F) -pthread $< $(REL_GUARD) $(call libs_desc,rel) $(LDLIBS) -o $@

# Grammar archive members link only when referenced; the default editor carries
# none until hl dispatches to them. `make build/sublimite-v2-<lang>` forces one
# opt-in wrapper in (mk/grammars.mk HL_OPTIN).
ifneq ($(HL_OPTIN),)
$(addprefix $(EDIT_BIN)-,$(HL_OPTIN)): $(EDIT_BIN)-%: $(B)/rel/src/main.o $(call libs_desc,rel)
	$(CC_RELEASE) $(REL_F) -pthread $< $(REL_GUARD) -Wl,--undefined=hl_language_$* $(call libs_desc,rel) $(LDLIBS) -o $@
endif

$(B)/rel/tests/%: $(B)/rel/tests/%.o $(call libs_desc,rel)
	@mkdir -p $(@D)
	$(CC_RELEASE) $(REL_F) -pthread $< $(call libs_desc,rel) $(LDLIBS) -o $@
$(B)/san/tests/%: $(B)/san/tests/%.o $(call libs_desc,san)
	@mkdir -p $(@D)
	$(CC_SAN) $(SAN_F) -pthread $< $(call libs_desc,san) $(LDLIBS) -o $@
$(B)/tsan/tests/%: $(B)/tsan/tests/%.o $(call libs_desc,tsan)
	@mkdir -p $(@D)
	$(CC_SAN) $(TSAN_F) -pthread $< $(call libs_desc,tsan) $(LDLIBS) -o $@
$(B)/fuzz/%_fuzz: $(B)/fuzz/fuzz/%_fuzz.o $(call libs_desc,fuzz)
	@mkdir -p $(@D)
	$(CC_SAN) $(CLANG_GCC) -fsanitize=fuzzer,address,undefined -pthread $< $(call libs_desc,fuzz) $(LDLIBS) -o $@

$(B)/rel/bench/base_bench: $(B)/rel/bench/base_bench.o $(call lib_of,rel,L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

# ---- check lanes (docs/PLAN_V2.md §5) ----
# Sequential local lanes, fail-fast. Each lane can build in parallel.
check:
	@$(MAKE) --no-print-directory check-lints
	@$(MAKE) --no-print-directory check-layers
	@$(MAKE) --no-print-directory check-san
	@$(MAKE) --no-print-directory check-law2
	@echo "check: ok"

# X3 large delete + undo on the stock v1 oracle; not part of `check` (docs/decisions/v2-he7.15.md).
# Usage: make check-contract-large-undo ORACLE=/abs/v1/build/sublimite CONTRACT_DISPLAY=:140
.PHONY: check-contract-large-undo
check-contract-large-undo: $(B)/xdriver $(B)/xdriver-test-client
	python3 -B tests/contract/test_large_undo.py --display "$(CONTRACT_DISPLAY)"
	python3 -B tests/contract/cases/large_undo.py --target "$(ORACLE)" --display "$(CONTRACT_DISPLAY)"

.PHONY: check-contract-sandbox
check-contract-sandbox:
	python3 -B tests/contract/test_sandbox.py

.PHONY: check-contract-inspect
check-contract-inspect:
	python3 -B tests/contract/test_inspect.py

.PHONY: check-contract-files
check-contract-files: $(B)/xdriver
	@PYTHONPATH=tests/contract python3 -B -m cases.files \
	    --oracle "$(ORACLE)" --display "$(CONTRACT_DISPLAY)"

.PHONY: check-contract-navigation
check-contract-navigation: $(B)/xdriver
	@test -n "$(ORACLE_TREE)" && test -n "$(CONTRACT_DISPLAY)" || { echo "Set ORACLE_TREE and CONTRACT_DISPLAY (:170 through :179)" >&2; exit 2; }
	python3 -B tests/contract/cases/navigation.py --build-dir "$(abspath $(ORACLE_TREE))/build/contract-oracle" --display "$(CONTRACT_DISPLAY)"

check-lints: check-layout check-includes
	python3 tools/check_isolation.py
	python3 -B tests/isolation/run.py --build-dir "$(B)"
	TMPDIR="$(abspath $(B))" python3 tests/lint/run.py
	python3 tests/lanes/run.py --build-dir "$(B)"

check-layout: check-suites
	python3 tools/check_layout.py registry

# Direct lane invocations also check the inventory, even with cached binaries.
check-suites:
	@python3 tools/check_layout.py suites \
	    --required-layers $(LAYER_TESTS_SRC) --layers $(LAYER_SUITES) --san $(SAN_SUITES) \
	    --law2 $(LAW2_SUITES) --law2-empty-wave "$(LAW2_EMPTY_WAVE)" \
	    --tsan $(TSAN_SUITES) --tsan-empty-wave "$(TSAN_EMPTY_WAVE)" \
	    --fuzz $(FUZZ_SUITES) --fuzz-empty-wave "$(FUZZ_EMPTY_WAVE)" \
	    --tests-on-disk $(filter %_test.c,$(LINT_TEST_SRCS)) \
	    --fuzz-on-disk $(filter %_fuzz.c,$(LINT_FUZZ_SRCS))

# The include lints use the .d files emitted when compiling these objects.
check-includes: $(LINT_OBJS)
	python3 tools/check_dag.py --build-dir "$(B)" --layers $(LAYER_MODULES)
	python3 tools/check_layout.py includes --build-dir "$(B)" --tests $(LINT_TEST_SRCS) --fuzz $(LINT_FUZZ_SRCS)

run_test_binaries = for t in $(1); do echo "== $$t"; $(RUN)"$$t"; n=$$((n+1)); done
assert_suite_count = if [ "$$n" -ne "$(words $(1))" ]; then \
	echo "$@: FAIL ran $$n of $(words $(1)) declared suites"; exit 1; fi
declared_empty = echo "$@: declared empty (first suites due in Wave $(1))"

# Isolated --whole-archive link per layer against lower archives only, then run.
check-layers: check-suites $(LAYER_TEST_BINS) $(LAYER_L5_BIN)
	@set -e; n=0; $(call run_test_binaries,$(LAYER_TEST_BINS) $(LAYER_L5_BIN)); \
	$(call assert_suite_count,$(LAYER_SUITES) src/main.c); \
	echo "check-layers: $$n isolated layer links passed (L0-L4 tests, L5 via main.o)"

# Link-level module DAG. Use the same member order as the archive recipes.
symbol_modules := $(foreach l,$(LAYERS),$(foreach m,$($(l)_MODS),$(l):$(m)))
symbol_layer_libs := $(foreach l,$(LAYERS),$($(l)_LIB))

.PHONY: check-symbols check-module-ldlibs
check-lints: check-symbols
check-symbols: $(call libs_desc,rel) $(B)/rel/src/main.o
	python3 tools/check_symbols.py --nm '$(or $(NM),nm)' --module-map $(symbol_modules) \
	    --layer-libs $(symbol_layer_libs) --main $(B)/rel/src/main.o \
	    $(foreach l,$(LAYERS),--archive $(patsubst L%,%,$(l)) $(call lib_of,rel,$(l)) \
	        $(patsubst $(B)/rel/%,%,$($(l)_OBJ_rel)))
	+$(MAKE) --no-print-directory -f Makefile -f tests/symbols/fixtures.mk check-symbol-fixtures

$(LAYER_TEST_BINS) $(LAYER_L5_BIN): | check-module-ldlibs
check-module-ldlibs:
	python3 tools/check_symbols.py --ldlibs-only --layer-libs $(symbol_layer_libs)

# ASan + UBSan over the registered sanitizer suites (layer tests included).
check-san: check-suites $(TEST_SAN)
	@set -e; n=0; $(call run_test_binaries,$(TEST_SAN)); \
	$(call assert_suite_count,$(SAN_SUITES)); echo "$@: $$n test binaries passed"

# The release base suite proves interposition with a deliberate allocation
# and requires a live guard with a zero count for every Law-2 window.
check-law2: check-suites $(TEST_LAW2)
	@set -e; n=0; $(call run_test_binaries,$(TEST_LAW2)); \
	$(call assert_suite_count,$(LAW2_SUITES)); \
	if [ "$$n" -eq 0 ]; then $(call declared_empty,$(LAW2_EMPTY_WAVE)); else echo "$@: $$n release suites passed"; fi

# TSan is a wave-end / per-touched-module gate.
check-tsan: check-suites $(TEST_TSAN)
	@set -e; n=0; $(call run_test_binaries,$(TEST_TSAN)); \
	$(call assert_suite_count,$(TSAN_SUITES)); \
	if [ "$$n" -eq 0 ]; then $(call declared_empty,$(TSAN_EMPTY_WAVE)); else echo "$@: $$n threaded suites passed"; fi

fuzz: check-suites $(FUZZ_BIN)
	@$(if $(FUZZ_BIN),echo "$@: $(words $(FUZZ_BIN)) fuzzers built",$(call declared_empty,$(FUZZ_EMPTY_WAVE)))

# Replay each fuzzer over its committed seed corpus fuzz/corpus/<name>/ only
# (-runs=0: no generated inputs, so no invented iteration budget).
fuzz-smoke: check-suites $(FUZZ_BIN)
	@set -e; n=0; for f in $(FUZZ_BIN); do m=$${f#$(B)/fuzz/}; corpus=fuzz/corpus/$${m%_fuzz}; \
	  if [ ! -d "$$corpus" ]; then echo "$@: FAIL $$corpus missing (commit minimal seeds)"; exit 1; fi; \
	  echo "== $$f $$corpus"; $(RUN)"$$f" -runs=0 "$$corpus"; n=$$((n+1)); done; \
	$(call assert_suite_count,$(FUZZ_SUITES)); \
	if [ "$$n" -eq 0 ]; then $(call declared_empty,$(FUZZ_EMPTY_WAVE)); else echo "$@: $$n fuzzers replayed their seeds"; fi

clean:
	rm -rf $(B)

# Fixture runners can create separate build trees under B. Their dependency
# paths are relative to their fixture roots, so import only our configurations.
DEP_DIRS := $(wildcard $(addprefix $(B)/,$(CFGS)))
-include $(if $(DEP_DIRS),$(shell find $(DEP_DIRS) -name '*.d'))
