# sublimite v2 -- GNU Makefile (docs/PLAN_V2.md §2, §5).
#
# Layout: mk/layers.mk declares the 5 layers and their archives (no monolithic
# libedit.a), mk/grammars.mk the vendored tree-sitter table (a no-op until
# vendor/ is imported). Drop files in src/<module>/, tests/<m>_test.c,
# bench/<m>_bench.c, fuzz/<m>_fuzz.c; globs pick them up.
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
WARN   := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wswitch-enum -D_GNU_SOURCE -pthread -DEDIT_APP_ID=\"$(APP_ID)\"
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
include mk/layers.mk

TESTS      := $(sort $(wildcard tests/*_test.c))
FUZZS      := $(sort $(wildcard fuzz/*_fuzz.c))
# The dependency lints cover every translation unit in these directories,
# including sources that are not linked by a check lane.
LINT_REL_SRCS := $(shell find $(wildcard src tests) -name '*.c' \( -type f -o -xtype f \))
LINT_FUZZ_SRCS := $(if $(wildcard fuzz),$(shell find fuzz -name '*.c' \( -type f -o -xtype f \)))
LINT_OBJS := $(LINT_REL_SRCS:%.c=$(B)/rel/%.o) $(LINT_FUZZ_SRCS:%.c=$(B)/fuzz/%.o)
# Law-2 + G1 suites run on the release (gcc -O2) tree: the allocation guard is
# live there, and the suite must fail closed if it is inert.
LAW2_TESTS := $(filter tests/law2_%_test.c tests/g1_%_test.c,$(TESTS))
# Threaded suites (docs/PLAN_V2.md §5.4); present ones run under TSan.
TSAN_SUITES := work piece journal savectl font_async hl find_session app_session
TSAN_TESTS  := $(filter $(TSAN_SUITES:%=tests/%_test.c),$(TESTS))

TEST_SAN  := $(patsubst tests/%.c,$(B)/san/tests/%,$(TESTS))
TEST_REL  := $(patsubst tests/%.c,$(B)/rel/tests/%,$(filter-out $(LAYER_TESTS_SRC),$(TESTS)))
TEST_LAW2 := $(patsubst tests/%.c,$(B)/rel/tests/%,$(LAW2_TESTS))
TEST_TSAN := $(patsubst tests/%.c,$(B)/tsan/tests/%,$(TSAN_TESTS))
FUZZ_BIN  := $(patsubst fuzz/%.c,$(B)/fuzz/%,$(FUZZS))
EDIT_BIN  := $(B)/$(APP_ID)

# Per-task test timeout: unset (none). Set TEST_TIMEOUT=<secs> only with a
# derivation beside it (CLAUDE.md law 4); no silent retries either way.
RUN = $(if $(TEST_TIMEOUT),timeout $(TEST_TIMEOUT) )

.PHONY: all check check-lints check-layers check-san check-law2 check-tsan fuzz fuzz-smoke clean
.SECONDARY:
.DEFAULT_GOAL := all

all: $(EDIT_BIN) $(call libs_desc,rel) $(LAYER_TEST_BINS) $(LAYER_L5_BIN) $(TEST_REL)

# ---- objects (one tree per configuration) ----
define CFG_COMPILE
$(B)/$(1)/%.o: %.c
	@mkdir -p $$(@D)
	$$(CC_$(1)) $$(WARN) $$(CF_$(1)) $$(INC) $$(DEP) -c $$< -o $$@
endef
$(foreach c,$(CFGS),$(eval $(call CFG_COMPILE,$(c))))

# ---- binaries ----
# Strict topological link: main.o, then L5 .. L0; a downward-only DAG needs no
# --start-group.
$(EDIT_BIN): $(B)/rel/src/main.o $(call libs_desc,rel)
	@mkdir -p $(@D)
	$(CC_RELEASE) $(REL_F) -pthread $< $(call libs_desc,rel) $(LDLIBS) -o $@

# Grammar archive members link only when referenced; the default editor carries
# none until hl dispatches to them. `make build/sublimite-v2-<lang>` forces one
# opt-in wrapper in (mk/grammars.mk HL_OPTIN).
ifneq ($(HL_OPTIN),)
$(addprefix $(EDIT_BIN)-,$(HL_OPTIN)): $(EDIT_BIN)-%: $(B)/rel/src/main.o $(call libs_desc,rel)
	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_$* $(call libs_desc,rel) $(LDLIBS) -o $@
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

# ---- check lanes (docs/PLAN_V2.md §5) ----
# Sequential lanes, fail-fast; each lane is internally parallel. A lane with no
# suites yet says so loudly rather than passing silently.
check:
	@$(MAKE) --no-print-directory check-lints
	@$(MAKE) --no-print-directory check-layers
	@$(MAKE) --no-print-directory check-san
	@$(MAKE) --no-print-directory check-law2
	@echo "check: ok"

.PHONY: check-contract-sandbox
check-contract-sandbox:
	python3 -B tests/contract/test_sandbox.py

.PHONY: check-contract-inspect
check-contract-inspect:
	python3 -B tests/contract/test_inspect.py

# The include lints use the .d files emitted when compiling these objects.
check-lints: $(LINT_OBJS)
	python3 tools/check_dag.py --build-dir "$(B)" --layers $(LAYER_MODULES)
	python3 tools/check_no_c_includes.py --build-dir "$(B)"
	python3 tools/check_isolation.py
	python3 tests/lint/run.py

# Isolated --whole-archive link per layer against lower archives only, then run.
check-layers: $(LAYER_TEST_BINS) $(LAYER_L5_BIN)
	@set -e; for t in $(LAYER_TEST_BINS) $(LAYER_L5_BIN); do echo "== $$t"; $(RUN)$$t; done; \
	echo "check-layers: 6 isolated layer links passed (L0-L4 tests, L5 via main.o)"

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

# ASan + UBSan over every tests/*_test.c (layer tests included).
check-san: $(TEST_SAN)
	@set -e; n=0; for t in $(TEST_SAN); do echo "== $$t"; $(RUN)$$t; n=$$((n+1)); done; echo "check-san: $$n test binaries passed"

# Release (gcc -O2) Law-2 + G1 lane; suites tests/law2_*_test.c, tests/g1_*_test.c
# must themselves fail closed when edit_malloc_guard reports inert.
check-law2: $(TEST_LAW2)
	@set -e; n=0; for t in $(TEST_LAW2); do echo "== $$t"; $(RUN)$$t; n=$$((n+1)); done; \
	if [ $$n -eq 0 ]; then echo "check-law2: NOTE no Law-2/G1 suites yet (tests/law2_*_test.c, tests/g1_*_test.c land in Wave 1); nothing ran"; else echo "check-law2: $$n release suites passed"; fi

# TSan over the threaded suites that exist (wave-end / per-touched-module gate).
check-tsan: $(TEST_TSAN)
	@set -e; n=0; for t in $(TEST_TSAN); do echo "== $$t"; $(RUN)$$t; n=$$((n+1)); done; \
	if [ $$n -eq 0 ]; then echo "check-tsan: NOTE no threaded suites yet ($(TSAN_SUITES)); nothing ran"; else echo "check-tsan: $$n threaded suites passed"; fi

fuzz: $(FUZZ_BIN)
	@echo "fuzz: $(words $(FUZZ_BIN)) fuzzers built"

# Replay each fuzzer over its committed seed corpus fuzz/corpus/<name>/ only
# (-runs=0: no generated inputs, so no invented iteration budget).
fuzz-smoke: $(FUZZ_BIN)
	@set -e; n=0; for f in $(FUZZ_BIN); do m=$$(basename $$f _fuzz); \
	  if [ ! -d fuzz/corpus/$$m ]; then echo "fuzz-smoke: FAIL fuzz/corpus/$$m missing (commit minimal seeds)"; exit 1; fi; \
	  echo "== $$f fuzz/corpus/$$m"; $(RUN)$$f -runs=0 fuzz/corpus/$$m; n=$$((n+1)); done; \
	if [ $$n -eq 0 ]; then echo "fuzz-smoke: NOTE no fuzzers yet (fuzz/<m>_fuzz.c); nothing ran"; else echo "fuzz-smoke: $$n fuzzers replayed their seeds"; fi

clean:
	rm -rf $(B)

-include $(shell find $(B) -name '*.d' 2>/dev/null)
