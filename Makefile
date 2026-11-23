# sublimite v2 -- GNU Makefile (docs/PLAN_V2.md §2, §5).
#
# Layout: mk/layers.mk declares the 5 layers and their archives (no monolithic
# libedit.a), mk/grammars.mk the vendored tree-sitter table (a no-op until
# vendor/ is imported). Register tests and fuzzers in tests/lanes.mk.
#
# Targets: all  check  check-lints  check-layers  check-san  check-law2
#          check-contract  check-tsan  fuzz  fuzz-smoke  clean
#          contract-gate (oracle-driven; not part of check)
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
SAN_F   := -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -O1 -g
TSAN_F  := -fsanitize=thread -fno-omit-frame-pointer -O1 -g
FUZZ_F  := -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -O1 -g

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
# Lane lists are derived from the suite-lane table, never assigned by hand
# (docs/decisions/v2-he7.26.md): each table line is a variable named by the
# suite's path whose value is its lanes.
LANE_TABLE := $(sort $(filter tests/%.c fuzz/%.c,$(.VARIABLES)))
lane_suites = $(foreach s,$(LANE_TABLE),$(if $(filter $(1),$($(s))),$(s)))
LAYER_SUITES := $(call lane_suites,layers)
SAN_SUITES   := $(call lane_suites,san)
LAW2_SUITES  := $(call lane_suites,law2)
TSAN_SUITES  := $(call lane_suites,tsan)
FUZZ_SUITES  := $(call lane_suites,fuzz)
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
		$${ORACLE_TREE:+--build-dir "$$ORACLE_TREE/build/contract-oracle"} \
		--display "$$RECOVERY_DISPLAY" --case "$${RECOVERY_CASE:-pre-ack}"

# ---- objects (one tree per configuration) ----
define CFG_COMPILE
$(B)/$(1)/%.o: %.c
	@mkdir -p $$(@D)
	$$(CC_$(1)) $$(WARN) $$(CF_$(1)) $$(INC) $$(DEP) -DEDIT_APP_ID=\"$$(APP_ID)\" -c $$< -o $$@
endef
# A test-hooks object is its module source with EDIT_TEST_HOOKS; a suite
# links it ahead of the archives (tests/lanes.mk), which never contain it.
define CFG_TEST_HOOKS
$(B)/$(1)/%_test_hooks.o: %.c
	@mkdir -p $$(@D)
	$$(CC_$(1)) $$(WARN) $$(CF_$(1)) $$(INC) $$(DEP) -DEDIT_APP_ID=\"$$(APP_ID)\" -DEDIT_TEST_HOOKS -c $$< -o $$@
endef
$(foreach c,$(CFGS),$(eval $(call CFG_COMPILE,$(c))))
$(foreach c,$(CFGS),$(eval $(call CFG_TEST_HOOKS,$(c))))

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
	$(CC_RELEASE) $(REL_F) -pthread $< $(filter %_test_hooks.o,$^) $(LDFLAGS_$*) $(call libs_desc,rel) $(LDLIBS) -o $@
$(B)/san/tests/%: $(B)/san/tests/%.o $(call libs_desc,san)
	@mkdir -p $(@D)
	$(CC_SAN) $(SAN_F) -pthread $< $(filter %_test_hooks.o,$^) $(LDFLAGS_$*) $(call libs_desc,san) $(LDLIBS) -o $@
$(B)/tsan/tests/%: $(B)/tsan/tests/%.o $(call libs_desc,tsan)
	@mkdir -p $(@D)
	$(CC_SAN) $(TSAN_F) -pthread $< $(filter %_test_hooks.o,$^) $(LDFLAGS_$*) $(call libs_desc,tsan) $(LDLIBS) -o $@
# Work initialization faults are injected at the OS boundary in this suite.
WORK_TEST_WRAPS := os_wake_init os_mutex_init os_mutex_destroy os_mutex_lock \
                   os_cond_init os_cond_destroy os_thread_start os_thread_join
$(foreach c,rel san tsan,$(B)/$(c)/tests/work_test): private LDLIBS += $(foreach s,$(WORK_TEST_WRAPS),-Wl,--wrap=$(s))
# Fault-service faults are injected beneath os (v2-0fv.2): per-suite link flags,
# LDFLAGS_<suite stem>, consumed by the test link rules.
LDFLAGS_os_fault_test := -Wl,--wrap=mmap,--wrap=sigaction,--wrap=sysconf,--wrap=syscall \
                         -Wl,--wrap=sem_init,--wrap=sem_destroy,--wrap=sem_wait,--wrap=sem_post
# Test-hook objects (A15 generation setter) only for the fault suites.
$(foreach c,rel san tsan,$(eval $(B)/$(c)/tests/os_fault_test: $(B)/$(c)/src/os/os_posix_test_hooks.o))
$(B)/rel/tests/law2_os_fault_test: $(B)/rel/src/os/os_posix_test_hooks.o
# ipc's peer-identity decision is reached through a test-only link seam
# (tests/ipc_peer_seam.h); the shipped binary has no override (v2-0fv.13).
IPC_PEER_SEAM := -Wl,--wrap=os_stream_peer_uid
$(foreach c,rel san tsan,$(B)/$(c)/tests/ipc_test) $(B)/fuzz/ipc_fuzz: private LDLIBS += $(IPC_PEER_SEAM)
# Local stream and wait-set faults are injected beneath os (v2-0fv.13).
OS_ENDPOINT_TEST_WRAPS := recv send accept4 connect bind epoll_wait timerfd_settime
$(foreach c,rel san tsan,$(B)/$(c)/tests/os_endpoint_test): private LDLIBS += $(foreach s,$(OS_ENDPOINT_TEST_WRAPS),-Wl,--wrap=$(s))
$(B)/fuzz/%_fuzz: $(B)/fuzz/fuzz/%_fuzz.o $(call libs_desc,fuzz)
	@mkdir -p $(@D)
	$(CC_SAN) $(CLANG_GCC) -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -pthread $< $(call libs_desc,fuzz) $(LDLIBS) -o $@

$(B)/rel/bench/base_bench: $(B)/rel/bench/base_bench.o $(call lib_of,rel,L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

$(B)/rel/bench/work_bench: $(B)/rel/bench/work_bench.o $(call lib_of,rel,L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

$(B)/trace_bench: bench/trace_bench.c $(call lib_of,rel,L0)
	$(CC_RELEASE) $(WARN) $(REL_F) $(INC) $^ $(LDLIBS) -o $@

$(B)/rel/bench/ipc_bench: $(B)/rel/bench/ipc_bench.o $(call lib_of,rel,L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

$(B)/rel/bench/scan_bench: $(B)/rel/bench/scan_bench.o $(call libs_of,rel,L1 L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

$(B)/rel/bench/utf8_bench: $(B)/rel/bench/utf8_bench.o $(call libs_of,rel,L1 L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

$(B)/rel/bench/indent_bench: $(B)/rel/bench/indent_bench.o $(call libs_of,rel,L1 L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

# ---- check lanes (docs/PLAN_V2.md §5) ----
# Sequential local lanes, fail-fast. Each lane can build in parallel.
check:
	@$(MAKE) --no-print-directory check-lints
	@$(MAKE) --no-print-directory check-layers
	@$(MAKE) --no-print-directory check-san
	@$(MAKE) --no-print-directory check-law2
	@$(MAKE) --no-print-directory check-contract
	@echo "check: ok"

# Contract self-tests that need no oracle (docs/decisions/v2-he7.16.md): the runner's own
# self-test, then every harness-target case of tests/contract/expectations.json through the
# runner, each invoked once. A missing display, mock or test is an ERROR, never a skip.
# The runner hands out one free display per invocation from CONTRACT_CHECK_DISPLAYS.
CONTRACT_CHECK_DISPLAYS ?= :120-:129
.PHONY: check-contract
check-contract: $(B)/xdriver $(B)/xdriver-test-client
	python3 -B tests/contract/test_run.py
	python3 -B tests/contract/run.py --role harness --displays "$(CONTRACT_CHECK_DISPLAYS)" \
	    --out "$(B)/contract-check"

# Wave 0 oracle gate: build or validate the inspected v1 tree, then run every v1-required
# case once and write the gate record (gate.json + summary.txt) under $(B)/contract-gate/.
# Usage: make contract-gate ORACLE_TREE=/abs/disposable/v1 ORACLE=/abs/stock/v1/build/sublimite
GATE_DISPLAYS ?= :190-:199
.PHONY: contract-gate
contract-gate: $(B)/xdriver $(B)/xdriver-test-client
	@test -n "$(ORACLE_TREE)" && test -n "$(ORACLE)" || { echo "Set ORACLE_TREE and ORACLE" >&2; exit 2; }
	python3 -B tests/contract/build_oracle.py --jobs "$(ORACLE_JOBS)" "$(ORACLE_TREE)"
	python3 -B tests/contract/run.py --role v1 --exe oracle="$(abspath $(ORACLE))" \
	    --oracle-build "$(abspath $(ORACLE_TREE))/build/contract-oracle" \
	    --displays "$(GATE_DISPLAYS)" --out "$(B)/contract-gate"

# X3 large delete + undo on the stock v1 oracle; not part of `check` (docs/decisions/v2-he7.15.md).
# Diagnostic: on the pin the case reports X3.over.roundtrip as FAIL with the A8 signature and
# exits 3; only contract-gate turns that signature into the reviewed XFAIL.
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
	python3 -B tests/contract/cases/files.py \
	    --build-dir "$(abspath $(ORACLE_TREE))/build/contract-oracle" --display "$(CONTRACT_DISPLAY)"

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
	    --law2 $(LAW2_SUITES) --tsan $(TSAN_SUITES) --fuzz $(FUZZ_SUITES) \
	    --tests-on-disk $(filter %_test.c,$(LINT_TEST_SRCS)) \
	    --fuzz-on-disk $(filter %_fuzz.c,$(LINT_FUZZ_SRCS))

# The include lints use the .d files emitted when compiling these objects.
check-includes: $(LINT_OBJS)
	python3 tools/check_dag.py --build-dir "$(B)" --layers $(LAYER_MODULES)
	python3 tools/check_layout.py includes --build-dir "$(B)" --tests $(LINT_TEST_SRCS) --fuzz $(LINT_FUZZ_SRCS)

run_test_binaries = for t in $(1); do echo "== $$t"; $(RUN)"$$t"; n=$$((n+1)); done
assert_suite_count = if [ "$$n" -ne "$(words $(1))" ]; then \
	echo "$@: FAIL ran $$n of $(words $(1)) declared suites"; exit 1; fi

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
	$(call assert_suite_count,$(LAW2_SUITES)); echo "$@: $$n release suites passed"

# TSan is a wave-end / per-touched-module gate.
check-tsan: check-suites $(TEST_TSAN)
	@set -e; n=0; $(call run_test_binaries,$(TEST_TSAN)); \
	$(call assert_suite_count,$(TSAN_SUITES)); echo "$@: $$n threaded suites passed"

fuzz: check-suites $(FUZZ_BIN)
	@echo "$@: $(words $(FUZZ_BIN)) fuzzers built"

# Replay each fuzzer over its committed seed corpus fuzz/corpus/<name>/ only
# (-runs=0: no generated inputs, so no invented iteration budget).
fuzz-smoke: check-suites $(FUZZ_BIN)
	@set -e; n=0; for f in $(FUZZ_BIN); do m=$${f#$(B)/fuzz/}; corpus=fuzz/corpus/$${m%_fuzz}; \
	  if [ ! -d "$$corpus" ]; then echo "$@: FAIL $$corpus missing (commit minimal seeds)"; exit 1; fi; \
	  echo "== $$f $$corpus"; $(RUN)"$$f" -runs=0 "$$corpus"; n=$$((n+1)); done; \
	$(call assert_suite_count,$(FUZZ_SUITES)); echo "$@: $$n fuzzers replayed their seeds"

clean:
	rm -rf $(B)

# Fixture runners can create separate build trees under B. Their dependency
# paths are relative to their fixture roots, so import only our configurations.
DEP_DIRS := $(wildcard $(addprefix $(B)/,$(CFGS)))
-include $(if $(DEP_DIRS),$(shell find $(DEP_DIRS) -name '*.d'))
