# sublimite v2 -- GNU Makefile (docs/PLAN_V2.md §2, §5).
#
# Layout: mk/layers.mk declares the 5 layers and their archives (no monolithic
# libedit.a), mk/grammars.mk the vendored tree-sitter table (a no-op until
# vendor/ is imported). Register tests and fuzzers in tests/lanes.mk.
#
# Targets: all  check  check-lints  check-layers  check-san  check-law2
#          check-contract  check-tsan  fuzz  fuzz-smoke  clean
#          contract-gate  contract-reds (oracle-driven; not part of check)
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
# A stand-alone diagnostic run launches only what the gate would: the reviewed binding the
# runner selects for ORACLE and ORACLE_TREE (run.py --binding; docs/decisions/v2-he7.33.md).
CONTRACT_BINDING = $$(python3 -B tests/contract/run.py --binding --role v1 \
    --exe oracle="$(abspath $(ORACLE))" --oracle-build "$(abspath $(ORACLE_TREE))/build/contract-oracle")
.PHONY: check-contract-oracle
check-contract-oracle: $(B)/xdriver
	@test -n "$(ORACLE_TREE)" && test -n "$(ORACLE)" && test -n "$(ORACLE_DISPLAY)" || { echo "Set ORACLE_TREE, ORACLE and ORACLE_DISPLAY (:130 through :139)" >&2; exit 2; }
	python3 -B tests/contract/build_oracle.py --jobs "$(ORACLE_JOBS)" "$(ORACLE_TREE)"
	python3 -B tests/contract/test_oracle_inspector.py --build-dir "$(abspath $(ORACLE_TREE))/build/contract-oracle" --driver "$(abspath $(B)/xdriver)" --display "$(ORACLE_DISPLAY)" --digests "$(CONTRACT_BINDING)"

.PHONY: check-contract-recovery
check-contract-recovery: $(B)/xdriver
	python3 -B tests/contract/cases/recovery.py --oracle "$(abspath $(ORACLE))" \
		--build-dir "$(abspath $(ORACLE_TREE))/build/contract-oracle" \
		--display "$$RECOVERY_DISPLAY" --case "$${RECOVERY_CASE:-pre-ack}" --digests "$(CONTRACT_BINDING)"

# Red receipts that verify their cause (tests/contract/reds.py, finding 17): every red, or REDS.
.PHONY: contract-reds
contract-reds: $(B)/xdriver
	python3 -B tests/contract/reds.py --oracle "$(abspath $(ORACLE))" \
	    --build-dir "$(abspath $(ORACLE_TREE))/build/contract-oracle" \
	    --display "$(CONTRACT_DISPLAY)" --digests "$(CONTRACT_BINDING)" $(REDS)

# ---- objects (one tree per configuration) ----
define CFG_COMPILE
$(B)/$(1)/%.o: %.c
	@mkdir -p $$(@D)
	$$(CC_$(1)) $$(WARN) $$(CF_$(1)) $$(INC) $$(DEP) -DEDIT_APP_ID=\"$$(APP_ID)\" -c $$< -o $$@
endef
# A test-hooks variant (CLAUDE.md) is src/<module>/<file>.c compiled with
# <MODULE>_TEST_HOOKS. Only the suites whose rules below name it link it,
# ahead of the archives; no archive or shipped binary contains it.
upper = $(shell printf '%s' '$(1)' | tr a-z A-Z)
define CFG_TEST_HOOKS
$(B)/$(1)/%_test_hooks.o: %.c
	@mkdir -p $$(@D)
	$$(CC_$(1)) $$(WARN) $$(CF_$(1)) $$(INC) $$(DEP) -DEDIT_APP_ID=\"$$(APP_ID)\" -D$$(call upper,$$(word 2,$$(subst /, ,$$*)))_TEST_HOOKS -c $$< -o $$@
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
                   os_cond_init os_cond_destroy \
                   os_waitset_init os_waitset_add os_thread_start os_thread_join
$(foreach c,rel san tsan,$(B)/$(c)/tests/work_test): private LDLIBS += $(foreach s,$(WORK_TEST_WRAPS),-Wl,--wrap=$(s))
# Fault-service faults are injected beneath os (v2-0fv.2): per-suite link flags,
# LDFLAGS_<suite stem>, consumed by the test link rules.
LDFLAGS_os_fault_test := -Wl,--wrap=mmap,--wrap=sem_wait,--wrap=sem_post
# Test-hooks variants (CFG_TEST_HOOKS), linked only into the fault suites:
# os_posix.c with OS_TEST_HOOKS (the generation setter, A15).
$(foreach c,rel san tsan,$(eval $(B)/$(c)/tests/os_fault_test: $(B)/$(c)/src/os/os_posix_test_hooks.o))
$(B)/rel/tests/law2_os_fault_test: $(B)/rel/src/os/os_posix_test_hooks.o
# ipc's peer-identity decision (v2-0fv.13) and its endpoint isolation
# (v2-0fv.20) are reached through test-only link seams (tests/ipc_seam.h);
# the shipped binary has no override and reads no test variable.
IPC_SEAM := -Wl,--wrap=os_stream_peer_uid,--wrap=os_listen_local,--wrap=os_connect_local,--wrap=os_dir_open
$(foreach c,rel san tsan,$(B)/$(c)/tests/ipc_test) $(B)/fuzz/ipc_fuzz: private LDLIBS += $(IPC_SEAM)
$(foreach c,rel san tsan,$(eval $(B)/$(c)/tests/ipc_test: $(B)/$(c)/src/ipc/ipc_posix_test_hooks.o))
$(B)/fuzz/ipc_fuzz: $(B)/fuzz/src/ipc/ipc_posix_test_hooks.o
# Work tests also exercise the private admission and mailbox limits.
$(foreach c,rel san tsan,$(eval $(B)/$(c)/tests/work_test: $(B)/$(c)/src/work/work_test_hooks.o))
# Meter actual motion-decoder bytes through the same L1 call in each test lane.
LDFLAGS_viewport_test := -Wl,--wrap=utf8_grapheme_step
# The UTF-8 fixtures check the private scalar width and backward step as well.
$(foreach c,rel san tsan,$(eval $(B)/$(c)/tests/utf8_test: $(B)/$(c)/src/utf8/utf8_test_hooks.o))
$(B)/fuzz/utf8_fuzz: $(B)/fuzz/src/utf8/utf8_test_hooks.o
# piece (v2-0fv.7, .17): base refusal is injected at its one reservation path,
# and a fault (nonzero epoch) or a failing poll at os_fault_poll (Lines tests);
# piece_test, piece_shape_test and piece_fuzz share both seams.
LDFLAGS_piece_test := -Wl,--wrap=os_vm_reserve,--wrap=os_vm_release,--wrap=os_fault_poll
LDFLAGS_piece_shape_test := $(LDFLAGS_piece_test)
$(B)/fuzz/piece_fuzz: private LDLIBS += -Wl,--wrap=os_vm_reserve,--wrap=os_vm_release,--wrap=os_fault_poll
$(B)/fuzz/piece_fuzz: $(B)/fuzz/src/piece/piece_test_hooks.o
# piece_test and piece_shape_test run the real tree (libtext.a) with the
# PIECE_TEST_HOOKS height setter and white-box counters (v2-0fv.17); the flat
# model is linked only as the reference, inside piece_test.c.
$(foreach c,rel san tsan,$(foreach t,piece_test piece_shape_test,$(eval $(B)/$(c)/tests/$(t): $(B)/$(c)/src/piece/piece_test_hooks.o)))
# The find suites and fuzzer link the real tree from libtext.a (v2-0fv.17).
FIND_TW_SUITES := find_bestof_test find_p1r_test find_simd_filter_verify_test
$(foreach c,rel san tsan,$(foreach t,$(FIND_TW_SUITES),$(eval $(B)/$(c)/tests/$(t): $(B)/$(c)/src/find/find_test_hooks.o)))
LDFLAGS_find_bestof_test := -Wl,--wrap=work_should_stop,--wrap=piece_iter_init,--wrap=piece_iter_next
LDFLAGS_find_fault_test := -Wl,--wrap=os_fault_poll,--wrap=piece_iter_next
LDFLAGS_find_p1r_test := -Wl,--wrap=work_should_stop
LDFLAGS_find_simd_filter_verify_test := -Wl,--wrap=base_cpu_has_avx2
LDFLAGS_find_timeout_test := -Wl,--wrap=work_should_stop
# journal codec (L2-2a): CRC32C hardware/portable selection is forced in the
# suite through the base CPU predicate (no env switch, harvest 2.3).
LDFLAGS_journal_codec_test := -Wl,--wrap=base_cpu_has_sse42
# file (L2-1): sync-barrier and fault-registry failures are injected at the
# os seam; the six save step hooks link from the FILE_TEST_HOOKS variant.
LDFLAGS_file_test := -Wl,--wrap=os_file_sync,--wrap=os_dir_sync,--wrap=os_fault_register,--wrap=os_rename_at,--wrap=os_fault_poll,--wrap=os_fault_retire,--wrap=os_unmap,--wrap=os_stat_at,--wrap=os_file_pread,--wrap=openat
$(foreach c,rel san tsan,$(eval $(B)/$(c)/tests/file_test: $(B)/$(c)/src/file/file_test_hooks.o))
$(foreach c,rel san tsan,$(eval $(B)/$(c)/tests/find_bench_test: $(B)/$(c)/bench/find_bench))
# Undo (v2-0fv.8) runs on the real tree (v2-0fv.17). Its suite observes the
# span/count arrays offered to the batch planner and can refuse a replay step;
# it shares piece_harness.h, so it links the piece hooks object too.
LDFLAGS_undo_test := -Wl,--wrap=os_vm_reserve,--wrap=os_vm_release,--wrap=os_fault_poll,--wrap=piece_replace_spans,--wrap=piece_replace,--wrap=piece_span_nl_join
$(foreach c,rel san tsan,$(eval $(B)/$(c)/tests/undo_test: $(B)/$(c)/src/piece/piece_test_hooks.o))
# Local stream and wait-set faults are injected beneath os (v2-0fv.13).
OS_ENDPOINT_TEST_WRAPS := recv send accept4 connect bind epoll_wait timerfd_settime
$(foreach c,rel san tsan,$(B)/$(c)/tests/os_endpoint_test): private LDLIBS += $(foreach s,$(OS_ENDPOINT_TEST_WRAPS),-Wl,--wrap=$(s))
# Watch and metadata-copy faults are injected beneath os (L2-0a).
OS_PERSIST_TEST_WRAPS := inotify_init1 inotify_add_watch inotify_rm_watch read fchmod fchown fstat \
                         flistxattr fgetxattr fsetxattr fremovexattr
LDFLAGS_os_persist_test := $(foreach s,$(OS_PERSIST_TEST_WRAPS),-Wl,--wrap=$(s))
# The watch fuzzer forges kernel records beneath os (L2-0a): read(2) of the
# watch descriptor is the input; add and rm_watch are observed, not faked.
OS_WATCH_FUZZ_WRAPS := read inotify_add_watch inotify_rm_watch
$(B)/fuzz/os_watch_fuzz: private LDLIBS += $(foreach s,$(OS_WATCH_FUZZ_WRAPS),-Wl,--wrap=$(s))
$(B)/fuzz/%_fuzz: $(B)/fuzz/fuzz/%_fuzz.o $(call libs_desc,fuzz)
	@mkdir -p $(@D)
	$(CC_SAN) $(CLANG_GCC) -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -pthread $< $(filter %_test_hooks.o,$^) $(call libs_desc,fuzz) $(LDLIBS) -o $@

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

define FIND_BENCH
$(B)/$(1)/bench/find_bench: $(B)/$(1)/bench/find_bench.o $(call libs_of,$(1),L1 L0)
	$$(CC_$(1)) $$(CF_$(1)) -pthread $$^ $$(LDLIBS) -o $$@
endef
$(foreach c,rel san tsan,$(eval $(call FIND_BENCH,$(c))))
$(B)/rel/bench/piece_bench: $(B)/rel/bench/piece_bench.o $(call libs_of,rel,L1 L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

$(B)/rel/bench/undo_bench: $(B)/rel/bench/undo_bench.o $(call libs_of,rel,L1 L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

$(B)/rel/bench/file_bench: $(B)/rel/bench/file_bench.o $(call libs_of,rel,L2 L1 L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

# ---- check lanes (docs/PLAN_V2.md §5) ----
# Sequential local lanes, fail-fast. Each lane can build in parallel.
$(B)/rel/bench/viewport_bench: $(B)/rel/bench/viewport_bench.o $(call libs_of,rel,L4 L1 L0)
	$(CC_RELEASE) $(REL_F) -pthread $^ $(LDLIBS) -o $@

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
	python3 -B tests/contract/cases/large_undo.py --target "$(abspath $(ORACLE))" --display "$(CONTRACT_DISPLAY)" --digests "$(CONTRACT_BINDING)"

.PHONY: check-contract-sandbox
check-contract-sandbox:
	python3 -B tests/contract/test_sandbox.py

.PHONY: check-contract-inspect
check-contract-inspect:
	python3 -B tests/contract/test_inspect.py

.PHONY: check-contract-files
check-contract-files: $(B)/xdriver
	python3 -B tests/contract/cases/files.py --digests "$(CONTRACT_BINDING)" \
	    --build-dir "$(abspath $(ORACLE_TREE))/build/contract-oracle" --display "$(CONTRACT_DISPLAY)"

.PHONY: check-contract-navigation
check-contract-navigation: $(B)/xdriver
	@test -n "$(ORACLE_TREE)" && test -n "$(ORACLE)" && test -n "$(CONTRACT_DISPLAY)" || { echo "Set ORACLE_TREE, ORACLE and CONTRACT_DISPLAY (:170 through :179)" >&2; exit 2; }
	python3 -B tests/contract/cases/navigation.py --build-dir "$(abspath $(ORACLE_TREE))/build/contract-oracle" --display "$(CONTRACT_DISPLAY)" --digests "$(CONTRACT_BINDING)"

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
	python3 tools/check_no_c_includes.py --build-dir "$(B)" --tests $(LINT_TEST_SRCS) --fuzz $(LINT_FUZZ_SRCS)

# set -e: the first failing binary fails the lane; check-suites is the inventory.
run_test_binaries = set -e; for t in $(1); do echo "== $$t"; $(RUN)"$$t"; done

# Isolated --whole-archive link per layer against lower archives only, then run.
check-layers: check-suites $(LAYER_TEST_BINS) $(LAYER_L5_BIN)
	@$(call run_test_binaries,$(LAYER_TEST_BINS) $(LAYER_L5_BIN)); \
	echo "check-layers: $(words $(LAYER_TEST_BINS) $(LAYER_L5_BIN)) isolated layer links passed (L0-L4 tests, L5 via main.o)"

# Link-level symbol checks. Use the same member order as the archive recipes.
symbol_layer_libs := $(foreach l,$(LAYERS),$($(l)_LIB))
symbol_inputs = --nm '$(or $(NM),nm)' --module-map $(LAYER_MODULES) --main $(B)/rel/src/main.o \
    $(foreach l,$(LAYERS),--archive $(patsubst L%,%,$(l)) $(call lib_of,rel,$(l)) \
        $(patsubst $(B)/rel/%,%,$($(l)_OBJ_rel)))

.PHONY: check-symbols check-exports check-module-ldlibs
check-lints: check-symbols check-exports
check-symbols: $(call libs_desc,rel) $(B)/rel/src/main.o
	python3 tools/check_symbols.py $(symbol_inputs) --layer-libs $(symbol_layer_libs)
	+$(MAKE) --no-print-directory -f Makefile -f tests/symbols/fixtures.mk check-symbol-fixtures

check-exports: $(call libs_desc,rel) $(B)/rel/src/main.o
	python3 -B tools/check_exports.py $(symbol_inputs) $(addprefix --module ,os base ipc trace work scan utf8 indent find config dnd viewport)

$(LAYER_TEST_BINS) $(LAYER_L5_BIN): | check-module-ldlibs
check-module-ldlibs:
	python3 tools/check_symbols.py --ldlibs-only --layer-libs $(symbol_layer_libs)

# ASan + UBSan over the registered sanitizer suites (layer tests included).
check-san: check-suites $(TEST_SAN)
	@$(call run_test_binaries,$(TEST_SAN)); echo "$@: $(words $(TEST_SAN)) test binaries passed"

# The release base suite proves interposition with a deliberate allocation
# and requires a live guard with a zero count for every Law-2 window.
check-law2: check-suites $(TEST_LAW2)
	@$(call run_test_binaries,$(TEST_LAW2)); echo "$@: $(words $(TEST_LAW2)) release suites passed"

# TSan is a wave-end / per-touched-module gate.
check-tsan: check-suites $(TEST_TSAN)
	@$(call run_test_binaries,$(TEST_TSAN)); echo "$@: $(words $(TEST_TSAN)) threaded suites passed"

fuzz: check-suites $(FUZZ_BIN)
	@echo "$@: $(words $(FUZZ_BIN)) fuzzers built"

# Replay each fuzzer over its committed seed corpus fuzz/corpus/<name>/ only
# (-runs=0: no generated inputs, so no invented iteration budget). check-suites
# requires a readable seed in each corpus.
fuzz-smoke: check-suites $(FUZZ_BIN)
	@set -e; for f in $(FUZZ_BIN); do m=$${f#$(B)/fuzz/}; corpus=fuzz/corpus/$${m%_fuzz}; \
	  echo "== $$f $$corpus"; $(RUN)"$$f" -runs=0 "$$corpus"; done; \
	echo "$@: $(words $(FUZZ_BIN)) fuzzers replayed their seeds"

clean:
	rm -rf $(B)

# Fixture runners can create separate build trees under B. Their dependency
# paths are relative to their fixture roots, so import only our configurations.
DEP_DIRS := $(wildcard $(addprefix $(B)/,$(CFGS)))
-include $(if $(DEP_DIRS),$(shell find $(DEP_DIRS) -name '*.d'))
