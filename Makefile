# GNU Makefile. Globs everything: drop files in src/<m>/, tests/<m>_test.c,
# bench/<m>_bench.c, fuzz/<m>_fuzz.c, tools/<name>.c. Vendored runtimes are explicit.
CC_RELEASE ?= gcc
CC_SAN     ?= clang
# clang 18 on this box selects a half-installed gcc-14 dir and cannot find -lstdc++ (libFuzzer needs it); pin gcc 13. See docs/toolchain.md.
GCC_DIR    ?= $(firstword $(wildcard /usr/lib/gcc/x86_64-linux-gnu/13))
CLANG_GCC  := $(if $(GCC_DIR),--gcc-install-dir=$(GCC_DIR),)
AR         ?= ar

WARN   := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread
INC    := -Isrc -Ivendor/tree-sitter/lib/include
# P6.1: optional raw Wayland client; generated protocol glue is vendored.
# Override WAYLAND_AVAILABLE= for a dependency-free build check.
WAYLAND_AVAILABLE ?= $(shell printf '\043include <wayland-client.h>\n\043include <wayland-egl.h>\n\043include <xkbcommon/xkbcommon.h>\n' | $(CC_RELEASE) -x c -E - >/dev/null 2>&1 && echo yes)
ifneq ($(WAYLAND_AVAILABLE),)
INC += -DEDIT_HAVE_WAYLAND
WAYLAND_LIBS := -lwayland-client -lwayland-egl
# The mock protocol test is optional independently of the client.
WAYLAND_SERVER_AVAILABLE := $(shell printf '\043include <wayland-server.h>\n' | $(CC_RELEASE) -x c -E - >/dev/null 2>&1 && echo yes)
ifneq ($(WAYLAND_SERVER_AVAILABLE),)
INC += -DEDIT_HAVE_WAYLAND_SERVER
WAYLAND_TEST_LIBS := -lwayland-server
endif
endif
DEP    := -MMD -MP
REL_F  := -O2 -g -msse2
SAN_F  := -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g
FUZZ_F := -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -O1 -g
LDLIBS := -lm -ldl $(WAYLAND_LIBS)
# Per-module extra libraries: a one-line file src/<m>/LDLIBS (e.g. "-lxcb -lxkbcommon") is appended to every link.
LDLIBS += $(foreach f,$(wildcard src/*/LDLIBS),$(shell cat $(f)))

B := build
TS_SRC := vendor/tree-sitter/lib/src/lib.c
# Generated grammar sources share the runtime archives; one line per language.
TS_SRC += vendor/tree-sitter-c/src/parser.c
TS_SRC += vendor/tree-sitter-rust/src/parser.c vendor/tree-sitter-rust/src/scanner.c
SRCS   := $(sort $(wildcard src/*/*.c)) $(TS_SRC)
SRCS += vendor/tree-sitter-python/src/parser.c vendor/tree-sitter-python/src/scanner.c
SRCS += vendor/tree-sitter-julia/src/parser.c vendor/tree-sitter-julia/src/scanner.c
SRCS += vendor/tree-sitter-javascript/src/parser.c vendor/tree-sitter-javascript/src/scanner.c
SRCS += vendor/tree-sitter-typescript/typescript/src/parser.c vendor/tree-sitter-typescript/typescript/src/scanner.c
SRCS += vendor/tree-sitter-json/src/parser.c
SRCS += vendor/tree-sitter-toml/src/parser.c vendor/tree-sitter-toml/src/scanner.c
SRCS += vendor/tree-sitter-yaml/src/parser.c vendor/tree-sitter-yaml/src/scanner.c
SRCS += vendor/tree-sitter-markdown/tree-sitter-markdown/src/parser.c vendor/tree-sitter-markdown/tree-sitter-markdown/src/scanner.c
SRCS += vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/parser.c vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/scanner.c
SRCS += vendor/tree-sitter-latex/src/parser.c vendor/tree-sitter-latex/src/scanner.c
SRCS += vendor/tree-sitter-bash/src/parser.c vendor/tree-sitter-bash/src/scanner.c
MAINC  := $(wildcard src/main.c)
TESTS  := $(sort $(wildcard tests/*_test.c))
BENCHS := $(sort $(wildcard bench/*_bench.c))
FUZZS  := $(sort $(wildcard fuzz/*_fuzz.c))
TOOLS  := $(sort $(wildcard tools/*.c))

REL_OBJ  := $(SRCS:%.c=$(B)/rel/%.o)
SAN_OBJ  := $(SRCS:%.c=$(B)/san/%.o)
FUZZ_OBJ := $(SRCS:%.c=$(B)/fuzz/%.o)

# Upstream's amalgamation uses narrowing conversions and shadowed locals.
# Relax only those warnings for this object in every configuration; retain
# C11, Wall/Wextra/Werror, project optimization and sanitizer flags.
TS_OBJ := $(foreach config,rel san fuzz,$(addprefix $(B)/$(config)/,$(TS_SRC:.c=.o)))
$(TS_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN))
# Generated parsers also have unused labels/parameters; handwritten scanners
# use the same vendored-only flags. Keep project src/ warnings unchanged.
GRAMMAR_OBJ := $(filter %/parser.o %/scanner.o,$(TS_OBJ))
$(GRAMMAR_OBJ): WARN := -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-label -D_GNU_SOURCE -pthread
# Python upstream scanner narrows array sizes and discards array_pop values.
TS_PYTHON_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-python/src/,parser.o scanner.o))
$(TS_PYTHON_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-value
$(TS_PYTHON_OBJ): INC := -Ivendor/tree-sitter-python/src $(INC)
# Julia scanner callbacks leave unused ABI parameters; use its own parser ABI.
TS_JULIA_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-julia/src/,parser.o scanner.o))
$(TS_JULIA_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-parameter
$(TS_JULIA_OBJ): INC := -Ivendor/tree-sitter-julia/src $(INC)
# Javascript uses its own parser ABI headers and upstream scanner warnings.
TS_JAVASCRIPT_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-javascript/src/,parser.o scanner.o))
$(TS_JAVASCRIPT_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-parameter
$(TS_JAVASCRIPT_OBJ): INC := -Ivendor/tree-sitter-javascript/src $(INC)
# Typescript uses its own parser ABI headers and upstream scanner warnings.
TS_TYPESCRIPT_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-typescript/typescript/src/,parser.o scanner.o))
$(TS_TYPESCRIPT_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-parameter
$(TS_TYPESCRIPT_OBJ): INC := -Ivendor/tree-sitter-typescript/typescript/src $(INC)
# Json uses its own parser ABI headers and upstream scanner warnings.
TS_JSON_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-json/src/,parser.o))
$(TS_JSON_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN))
$(TS_JSON_OBJ): INC := -Ivendor/tree-sitter-json/src $(INC)
# Toml uses its own parser ABI headers and upstream scanner warnings.
TS_TOML_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-toml/src/,parser.o scanner.o))
$(TS_TOML_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-parameter
$(TS_TOML_OBJ): INC := -Ivendor/tree-sitter-toml/src $(INC)
# Yaml uses its own parser ABI headers and upstream scanner warnings.
TS_YAML_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-yaml/src/,parser.o scanner.o))
$(TS_YAML_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-value -Wno-unused-but-set-variable -Wno-implicit-fallthrough
$(TS_YAML_OBJ): INC := -Ivendor/tree-sitter-yaml/src $(INC)

# Generated grammar/scanner warnings are relaxed only for vendored objects.
TS_MARKDOWN_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-markdown/tree-sitter-markdown/src/,parser.o scanner.o))
$(TS_MARKDOWN_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN))
$(TS_MARKDOWN_OBJ): INC := -Ivendor/tree-sitter-markdown/tree-sitter-markdown/src $(INC)
TS_MARKDOWN_INLINE_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/,parser.o scanner.o))
$(TS_MARKDOWN_INLINE_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN))
$(TS_MARKDOWN_INLINE_OBJ): INC := -Ivendor/tree-sitter-markdown/tree-sitter-markdown-inline/src $(INC)
TS_LATEX_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-latex/src/,parser.o scanner.o))
$(TS_LATEX_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-parameter -Wno-unused-label -Wno-unused-but-set-variable
$(TS_LATEX_OBJ): INC := -Ivendor/tree-sitter-latex/src $(INC)
TS_BASH_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-bash/src/,parser.o scanner.o))
$(TS_BASH_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-value
$(TS_BASH_OBJ): INC := -Ivendor/tree-sitter-bash/src $(INC)

REL_LIB  := $(B)/libedit.a
SAN_LIB  := $(B)/san/libedit.a
FUZZ_LIB := $(B)/fuzz/libedit.a

TEST_REL  := $(patsubst tests/%.c,$(B)/tests/%,$(TESTS))
TEST_SAN  := $(patsubst tests/%.c,$(B)/san/tests/%,$(TESTS))
# Display ownership includes focus, selection, Present and live raster/GL tests.
# Keep other module tests outside the cross-worktree lane; new editor/X11 tests match
# automatically. All callers share tools/with_display_lock.sh.
DISPLAY_TEST_SAN := $(filter $(B)/san/tests/cli_test $(B)/san/tests/editor% $(B)/san/tests/x11% $(B)/san/tests/ref% $(B)/san/tests/gl% $(B)/san/tests/raster_test,$(TEST_SAN))
BENCH_BIN := $(patsubst bench/%.c,$(B)/bench/%,$(BENCHS))
# P4.14 copies stale editor internals; retain its driver build in all.
DEFAULT_BENCH_BIN := $(filter-out $(B)/bench/zygote_bench,$(BENCH_BIN))
# edit-cx8: display benches take the same lock. On a shared Xvfb another
# worktree's suite (XTEST keys, setxkbmap) otherwise types into the bench
# window mid-row: foreign input sequences, keymap-worker allocations.
DISPLAY_BENCH := $(filter $(B)/bench/editor% $(B)/bench/x11% $(B)/bench/gl% $(B)/bench/raster% $(B)/bench/prewake% $(B)/bench/zygote% $(B)/bench/file_bench,$(BENCH_BIN))
FUZZ_BIN  := $(patsubst fuzz/%.c,$(B)/fuzz/%,$(FUZZS))
TOOL_BIN  := $(patsubst tools/%.c,$(B)/tools/%,$(TOOLS))
EDIT_BIN  := $(if $(MAINC),$(B)/sublimite)

$(B)/tests/wayland_test $(B)/tests/wayland_gl_test $(B)/san/tests/wayland_test $(B)/san/tests/wayland_gl_test $(B)/fuzz/wayland_fuzz: LDLIBS += $(WAYLAND_TEST_LIBS)

.PHONY: all lib check check-sh bench bench-zygote fuzz clean
.SECONDARY:
all: $(REL_LIB) $(EDIT_BIN) $(TOOL_BIN) $(TEST_REL) $(BENCH_BIN)
lib: $(REL_LIB)

# ---- objects (one tree per configuration) ----
$(B)/rel/%.o: %.c
	@mkdir -p $(@D)
	$(CC_RELEASE) $(WARN) $(REL_F) $(INC) $(GUARD) $(DEP) -c $< -o $@
$(B)/san/%.o: %.c
	@mkdir -p $(@D)
	$(CC_SAN) $(WARN) $(SAN_F) $(INC) $(GUARD) $(DEP) -c $< -o $@

# Real-display guard (2026-10-09): every test and bench object force-includes
# tests/display_guard.h (DISPLAY :0 -> $EDIT_DISPLAY or :99 unless EDIT_ALLOW_REAL_DISPLAY),
# and check/bench run binaries on a safe display: a private Xvfb from xvfb-run is kept,
# anything that names the real display 0 (or no DISPLAY) becomes $${EDIT_DISPLAY:-:99}.
SAFE_DISPLAY = d=$${DISPLAY:-}; if [ -z "$${EDIT_ALLOW_REAL_DISPLAY:-}" ]; then case "$$d" in ""|:0|:0.*|unix:0|unix:0.*|localhost:0|localhost:0.*) d=$${EDIT_DISPLAY:-:99};; esac; fi
GUARD_OBJ := $(TESTS:%.c=$(B)/rel/%.o) $(TESTS:%.c=$(B)/san/%.o) $(BENCHS:%.c=$(B)/rel/%.o)
$(GUARD_OBJ): GUARD := -include tests/display_guard.h
$(GUARD_OBJ): tests/display_guard.h
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
# Julia exceeds the assigned grammar size goal; archive extraction is opt-in.
$(B)/sublimite-julia: $(B)/rel/src/main.o $(REL_LIB)
	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_julia $(REL_LIB) $(LDLIBS) -o $@
# TypeScript exceeds the assigned total binary goal when linked; archive extraction is opt-in.
$(B)/sublimite-typescript: $(B)/rel/src/main.o $(REL_LIB)
	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_typescript $(REL_LIB) $(LDLIBS) -o $@
# Explicit grammar extraction follows the sibling worker; large grammars stay opt-in.
$(B)/sublimite-markdown: $(B)/rel/src/main.o $(REL_LIB)
	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_markdown $(REL_LIB) $(LDLIBS) -o $@
$(B)/sublimite-markdown_inline: $(B)/rel/src/main.o $(REL_LIB)
	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_markdown_inline $(REL_LIB) $(LDLIBS) -o $@
$(B)/sublimite-latex: $(B)/rel/src/main.o $(REL_LIB)
	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_latex $(REL_LIB) $(LDLIBS) -o $@
$(B)/sublimite-bash: $(B)/rel/src/main.o $(REL_LIB)
	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_bash $(REL_LIB) $(LDLIBS) -o $@
$(B)/sublimite: $(B)/rel/src/main.o $(REL_LIB)
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
	@set -e; $(SAFE_DISPLAY); for t in $(TEST_SAN); do echo "== $$t"; case " $(DISPLAY_TEST_SAN) " in *" $$t "*) DISPLAY=$$d sh tools/with_display_lock.sh $$t;; *) DISPLAY=$$d $$t;; esac; done; echo "check: $(words $(TEST_SAN)) test binaries passed"
	@echo "== tools/test_replay_cli.sh"; sh tools/test_replay_cli.sh $(B)
	@echo "== tools/test_display_lock.sh contract"; sh tools/test_display_lock.sh contract $(B)/san/tests/refwin_test

# Shell contract tests that need release benches (slow, ~1 min): run on demand.
check-sh: $(B)/tools/replay $(B)/bench/piece_bench
	sh tools/test_replay_cli.sh $(B)
	sh tools/test_bench_variant.sh

# Benches: release build, each must exit 0 (gate met).
# Per-bench default args: optional one-line file bench/<name>.args (e.g. piece_bench.args = --quick;
# the full piece matrix is run by tools/bench_variant.sh on an idle box).
bench: $(DEFAULT_BENCH_BIN)
	@set -e; $(SAFE_DISPLAY); for b in $(DEFAULT_BENCH_BIN); do a=bench/$$(basename $$b).args; args=$$(cat $$a 2>/dev/null || true); echo "== $$b $$args"; case " $(DISPLAY_BENCH) " in *" $$b "*) DISPLAY=$$d sh tools/with_display_lock.sh $$b $$args;; *) DISPLAY=$$d $$b $$args;; esac; done; echo "bench: $(words $(DEFAULT_BENCH_BIN)) benches passed"

# Explicit experiment only: compile failures remain visible until it is ported.
bench-zygote: $(REL_LIB)
	@echo "P4.14 zygote experiment is stale: variants/P4.14/editor.c and */open.c copy an older src/editor API (editor.lines); see docs/decisions/P4.14.md"
	@$(SAFE_DISPLAY); DISPLAY=$$d sh tools/zygote_bench.sh --run

fuzz: $(FUZZ_BIN)
	@echo "fuzz: $(words $(FUZZ_BIN)) fuzzers built"

clean:
	rm -rf $(B)

-include $(shell find $(B) -name '*.d' 2>/dev/null)
