# Windows cross build (edit-177r.1): llvm-mingw clang, x86_64-w64-windows-gnu (ucrt), from WSL.
# `make TARGET=win all|check|clean`. Output in build/win/{rel,san}. Linux targets are untouched.
WIN_TOOLCHAIN ?= $(HOME)/opt/llvm-mingw
WIN_CC  := $(WIN_TOOLCHAIN)/bin/x86_64-w64-mingw32-clang
WIN_RC  := $(WIN_TOOLCHAIN)/bin/x86_64-w64-mingw32-windres
WIN_WARN := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion
WIN_INC  := -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src
WIN_REL_F := -O2 -g -msse2
WIN_SAN_F := -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2
WIN_B := build/win
# asan runtime imports libc++.dll, which imports libunwind.dll; all three must sit beside the exe.
WIN_SAN_DLLS := libclang_rt.asan_dynamic-x86_64.dll libc++.dll libunwind.dll
# W0 core modules plus the pure render-grid and theme dependencies of their suites.
WIN_MODULES := os base trace work piece undo scroll lineidx layout view utf8 scan find hl render raster d3d11 font journal file win32 savectl ipc
WIN_TS_SRCS := vendor/tree-sitter/lib/src/lib.c
WIN_TS_SRCS += $(wildcard vendor/tree-sitter-*/src/parser.c vendor/tree-sitter-*/src/scanner.c)
WIN_TS_SRCS += $(wildcard vendor/tree-sitter-typescript/typescript/src/*.c vendor/tree-sitter-markdown/tree-sitter-markdown/src/*.c vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/*.c)
WIN_SRCS := $(foreach m,$(WIN_MODULES),$(wildcard src/$(m)/*.c)) $(WIN_TS_SRCS) src/config/theme.c
WIN_CORE_TESTS := base trace trace_pairs work work_foreground utf8 utf8_tables scan piece piece_bounds piece_mem piece_mt undo lineidx lineidx_os layout view find find_bench find_bestof find_p1r find_simd_filter_verify find_timeout hl hl_parse hl_worker
WIN_TESTS := $(sort $(wildcard tests/win/*_test.c)) tests/os_test.c tests/font_test.c tests/font_gsub_test.c tests/journal_os_test.c tests/journal_safety_test.c tests/file_test.c $(addprefix tests/,$(addsuffix _test.c,$(WIN_CORE_TESTS)))
WIN_REL_OBJ := $(WIN_SRCS:%.c=$(WIN_B)/rel/%.o)
WIN_SAN_OBJ := $(WIN_SRCS:%.c=$(WIN_B)/san/%.o)
WIN_TEST_REL := $(addprefix $(WIN_B)/rel/tests/,$(addsuffix .exe,$(basename $(notdir $(WIN_TESTS)))))
WIN_TEST_SAN := $(addprefix $(WIN_B)/san/tests/,$(addsuffix .exe,$(basename $(notdir $(WIN_TESTS)))))
# OS imports plus D3D11/DXGI and interface GUIDs. Shader compiler is runtime-loaded.
WIN_LIBS := -lntdll -lbcrypt -lshell32 -lole32 -ld3d11 -ldxgi -ldxguid -ldwmapi -ladvapi32 -luser32 -lshcore
# UTF-8 activeCodePage manifest (src/os/utf8.manifest, decision doc edit-177r.2
# §3.9), embedded as RT_MANIFEST 1 through a windres-compiled resource and linked
# into every Windows executable: the CRT's narrow argv, fopen and getenv become UTF-8.
WIN_MANIFEST_OBJ := $(WIN_B)/utf8_manifest.o
# Release builds route our own malloc/calloc/realloc/free through the counting
# guard (src/base/malloc_guard_win.c) at link time; the sanitizer runtime owns
# malloc in the san build, where the guard is compiled out and nothing is wrapped.
WIN_WRAP := -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free

.PHONY: all check clean win-compile check-savectl-production
WIN_AR := $(WIN_TOOLCHAIN)/bin/llvm-ar
WIN_REL_LIB := $(WIN_B)/rel/libcore.a
WIN_SAN_LIB := $(WIN_B)/san/libcore.a
# COFF weak defaults can satisfy ordinary references before archive extraction.
# Link the opt-in wrapper objects explicitly into the grammar conformance
# suite. Unlike ELF, lld's COFF weak defaults also absorb --undefined requests.
WIN_HL_OPTIN := julia typescript markdown markdown_inline latex bash
$(WIN_B)/rel/tests/hl_test.exe: WIN_TEST_FLAGS := $(foreach l,$(WIN_HL_OPTIN),$(WIN_B)/rel/src/hl/lang_$(l).o)
$(WIN_B)/san/tests/hl_test.exe: WIN_TEST_FLAGS := $(foreach l,$(WIN_HL_OPTIN),$(WIN_B)/san/src/hl/lang_$(l).o)
# ASan inlines layout fixtures into a 0x15fd58-byte main frame (objdump),
# exceeding PE's default 1 MiB reserve. Match this host's Linux 8192 KiB stack.
$(WIN_B)/san/tests/layout_test.exe: WIN_TEST_FLAGS := -Wl,--stack,8388608
WIN_COMPILE_MODULES := base trace work piece undo scroll lineidx layout view utf8 scan find hl journal
all: $(WIN_TEST_REL)
$(WIN_TEST_REL) $(WIN_TEST_SAN): mk/win.mk
# Objects reached only through pattern rules would be intermediates and deleted after each link.
.SECONDARY: $(WIN_REL_OBJ) $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)

win-compile: $(WIN_REL_OBJ)

# W2.4b: filesystem units only; editor linking follows in W2.4d.
.PHONY: win-editor-fs
win-editor-fs: $(addprefix $(WIN_B)/rel/src/editor/,$(addsuffix .o,recovery restore runtime retain session))

$(WIN_REL_LIB): $(WIN_REL_OBJ)
	$(WIN_AR) rcs $@ $^
$(WIN_SAN_LIB): $(WIN_SAN_OBJ)
	$(WIN_AR) rcs $@ $^

# Match Linux's generated-code warning exceptions and each grammar's ABI headers.
$(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/,$(WIN_TS_SRCS:.c=.o))): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN))

# Grammars: per language, the runtime's relaxation plus only the warnings that
# language's generated parser/scanner trips, and its own parser ABI headers.
# C and Rust need nothing beyond the runtime's relaxation.
TS_C_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-c/src/,parser.o))
$(TS_C_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN))
$(TS_C_OBJ): WIN_INC := -Ivendor/tree-sitter-c/src $(WIN_INC)
TS_RUST_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-rust/src/,parser.o scanner.o))
$(TS_RUST_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN))
$(TS_RUST_OBJ): WIN_INC := -Ivendor/tree-sitter-rust/src $(WIN_INC)
# Python upstream scanner narrows array sizes and discards array_pop values.
TS_PYTHON_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-python/src/,parser.o scanner.o))
$(TS_PYTHON_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN)) -Wno-unused-value
$(TS_PYTHON_OBJ): WIN_INC := -Ivendor/tree-sitter-python/src $(WIN_INC)
# Julia scanner callbacks leave unused ABI parameters; use its own parser ABI.
TS_JULIA_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-julia/src/,parser.o scanner.o))
$(TS_JULIA_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN)) -Wno-unused-parameter
$(TS_JULIA_OBJ): WIN_INC := -Ivendor/tree-sitter-julia/src $(WIN_INC)
# Javascript uses its own parser ABI headers and upstream scanner warnings.
TS_JAVASCRIPT_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-javascript/src/,parser.o scanner.o))
$(TS_JAVASCRIPT_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN)) -Wno-unused-parameter
$(TS_JAVASCRIPT_OBJ): WIN_INC := -Ivendor/tree-sitter-javascript/src $(WIN_INC)
# Typescript uses its own parser ABI headers and upstream scanner warnings.
TS_TYPESCRIPT_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-typescript/typescript/src/,parser.o scanner.o))
$(TS_TYPESCRIPT_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN)) -Wno-unused-parameter
$(TS_TYPESCRIPT_OBJ): WIN_INC := -Ivendor/tree-sitter-typescript/typescript/src $(WIN_INC)
# Json uses its own parser ABI headers and upstream scanner warnings.
TS_JSON_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-json/src/,parser.o))
$(TS_JSON_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN))
$(TS_JSON_OBJ): WIN_INC := -Ivendor/tree-sitter-json/src $(WIN_INC)
# Toml uses its own parser ABI headers and upstream scanner warnings.
TS_TOML_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-toml/src/,parser.o scanner.o))
$(TS_TOML_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN)) -Wno-unused-parameter
$(TS_TOML_OBJ): WIN_INC := -Ivendor/tree-sitter-toml/src $(WIN_INC)
# Yaml uses its own parser ABI headers and upstream scanner warnings.
TS_YAML_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-yaml/src/,parser.o scanner.o))
$(TS_YAML_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN)) -Wno-unused-value -Wno-unused-but-set-variable -Wno-implicit-fallthrough
$(TS_YAML_OBJ): WIN_INC := -Ivendor/tree-sitter-yaml/src $(WIN_INC)
# Markdown block and inline grammars keep upstream's nested layout.
TS_MARKDOWN_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-markdown/tree-sitter-markdown/src/,parser.o scanner.o))
$(TS_MARKDOWN_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN))
$(TS_MARKDOWN_OBJ): WIN_INC := -Ivendor/tree-sitter-markdown/tree-sitter-markdown/src $(WIN_INC)
TS_MARKDOWN_INLINE_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/,parser.o scanner.o))
$(TS_MARKDOWN_INLINE_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN))
$(TS_MARKDOWN_INLINE_OBJ): WIN_INC := -Ivendor/tree-sitter-markdown/tree-sitter-markdown-inline/src $(WIN_INC)
# LaTeX scanner callbacks are stateless; its generated keyword lexer is empty.
TS_LATEX_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-latex/src/,parser.o scanner.o))
$(TS_LATEX_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN)) -Wno-unused-parameter -Wno-unused-label -Wno-unused-but-set-variable
$(TS_LATEX_OBJ): WIN_INC := -Ivendor/tree-sitter-latex/src $(WIN_INC)
# Bash scanner discards array_pop values.
TS_BASH_OBJ := $(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/vendor/tree-sitter-bash/src/,parser.o scanner.o))
$(TS_BASH_OBJ): WIN_WARN := $(filter-out -Wconversion -Wshadow,$(WIN_WARN)) -Wno-unused-value
$(TS_BASH_OBJ): WIN_INC := -Ivendor/tree-sitter-bash/src $(WIN_INC)


$(WIN_B)/rel/%.o: %.c
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) -MMD -MP -c $< -o $@
$(WIN_B)/san/%.o: %.c
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) -MMD -MP -c $< -o $@
$(WIN_MANIFEST_OBJ): src/os/utf8.rc src/os/utf8.manifest
	@mkdir -p $(@D)
	$(WIN_RC) -I src/os -i $< -o $@

# Supplemental white-box suite: API shims and scheduling hooks exist only in
# its translation unit. Normal os_win.o has no test indirection or hooks.
$(WIN_B)/rel/tests/os_win_test.exe: tests/win/os_win_test.c src/os/os_win.c $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) -MMD -MP -MF $(@:.exe=.d) -MT $@ $< $(filter-out %/os_win.o,$(WIN_REL_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/san/tests/os_win_test.exe: tests/win/os_win_test.c src/os/os_win.c $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) -MMD -MP -MF $(@:.exe=.d) -MT $@ $< $(filter-out %/os_win.o,$(WIN_SAN_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/

# D3D11 white-box regression suite, like the OS native shim suite above.
$(WIN_B)/rel/tests/d3d11_win_test.exe: tests/win/d3d11_win_test.c src/d3d11/d3d11_win.c $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) $< $(filter-out %/d3d11_win.o,$(WIN_REL_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/san/tests/d3d11_win_test.exe: tests/win/d3d11_win_test.c src/d3d11/d3d11_win.c $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) $< $(filter-out %/d3d11_win.o,$(WIN_SAN_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/

# White-box journal transport shim; production has no per-syscall indirection.
$(WIN_B)/rel/tests/journal_transport_test.exe: tests/win/journal_transport_test.c src/journal/journal.c $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) $< $(filter-out %/journal/journal.o,$(WIN_REL_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/san/tests/journal_transport_test.exe: tests/win/journal_transport_test.c src/journal/journal.c $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) $< $(filter-out %/journal/journal.o,$(WIN_SAN_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/

$(WIN_B)/rel/tests/journal_safety_test.exe: tests/journal_safety_test.c src/journal/journal.c $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) $< $(filter-out %/journal/journal.o,$(WIN_REL_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/san/tests/journal_safety_test.exe: tests/journal_safety_test.c src/journal/journal.c $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) $< $(filter-out %/journal/journal.o,$(WIN_SAN_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/

# Static link; release has only system-DLL imports. The sanitizer exe needs the runtime DLLs beside it (tools/winrun.sh runs in place).
# Font white-box regressions keep all shims out of the production objects.
$(WIN_B)/rel/tests/font_dwrite_r2_test.exe: tests/win/font_dwrite_r2_test.c tests/win/font_dwrite_oracle.h tests/win/font_fixture.h src/font/dwrite_win.c src/font/discovery_win.c $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) $< $(filter-out %/dwrite_win.o %/discovery_win.o,$(WIN_REL_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/san/tests/font_dwrite_r2_test.exe: tests/win/font_dwrite_r2_test.c tests/win/font_dwrite_oracle.h tests/win/font_fixture.h src/font/dwrite_win.c src/font/discovery_win.c $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) $< $(filter-out %/dwrite_win.o %/discovery_win.o,$(WIN_SAN_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/
# Two source locations per test name: tests/win/<t>.c (Windows-only) or tests/<t>.c (portable suite); make takes the rule whose source exists.
$(WIN_B)/rel/tests/%_test.exe: tests/win/%_test.c $(WIN_REL_LIB) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) -MMD -MP -MF $(@:.exe=.d) -MT $@ $< $(WIN_TEST_FLAGS) $(WIN_REL_LIB) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/rel/tests/%_test.exe: tests/%_test.c $(WIN_REL_LIB) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) -MMD -MP -MF $(@:.exe=.d) -MT $@ $< $(WIN_TEST_FLAGS) $(WIN_REL_LIB) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/san/tests/%_test.exe: tests/win/%_test.c $(WIN_SAN_LIB) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) -MMD -MP -MF $(@:.exe=.d) -MT $@ $< $(WIN_TEST_FLAGS) $(WIN_SAN_LIB) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/
$(WIN_B)/san/tests/%_test.exe: tests/%_test.c $(WIN_SAN_LIB) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) -MMD -MP -MF $(@:.exe=.d) -MT $@ $< $(WIN_TEST_FLAGS) $(WIN_SAN_LIB) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/

# Included test/CLI translation units are part of the executable's source.
$(WIN_B)/rel/tests/journal_kill_test.exe $(WIN_B)/san/tests/journal_kill_test.exe: tests/journal_hot_exit_test.c
$(WIN_B)/rel/tests/journal_ack_test.exe $(WIN_B)/san/tests/journal_ack_test.exe: tools/journal_replay.c

# check = release run + sanitizer run of every Windows test, plus the
# --expect-asan red case, which must exit nonzero with an ASan report.
# The os suite's reparse case reads OS_TEST_DATA_REPARSE: a compact-compressed
# (WOF reparse tag) file that tools/win_reparse_fixture.sh creates as a Windows
# process under a per-run %TEMP%\sublimite-test-reparse-<id> and removes afterwards.
# tools/winrun.sh forwards OS_TEST_CASE and OS_TEST_DATA_REPARSE through WSLENV.
check: $(WIN_TEST_REL) $(WIN_TEST_SAN)
	@set -e; fx=$$(tools/win_reparse_fixture.sh create); printf "reparse fixture: %s\n" "$$fx"; \
	  trap "tools/win_reparse_fixture.sh remove \"$$fx\"" EXIT; status=0; \
	  for t in $(WIN_TEST_REL) $(WIN_TEST_SAN); do \
	    echo "== $$t"; \
	    if OS_TEST_DATA_REPARSE="$$fx" tools/winrun.sh $$t; then echo "RESULT $$t exit=0"; \
	    else rc=$$?; echo "RESULT $$t exit=$$rc"; status=1; fi; \
	  done; \
	  for t in $(filter %smoke_test.exe,$(WIN_TEST_SAN)); do echo "== $$t --expect-asan (must fail)"; \
	    if tools/winrun.sh $$t --expect-asan 2>$(WIN_B)/asan.err; then echo "ASan did not fire"; status=1; \
	    elif grep -q AddressSanitizer $(WIN_B)/asan.err; then echo "ASan fired: ok"; \
	    else echo "no ASan report"; cat $(WIN_B)/asan.err; status=1; fi; \
	  done; exit $$status

	@echo "Windows journal: transport, retained-BASE transactions, wire/CRC/PAD, PAINT, sharing states and process-death oracles passed"
	@echo "Windows journal: SKIP editor-dependent group/retire/session/recovery/dataloss integration and first-paint closed-tab restart (editor/platform not ported)"
	@echo "Windows journal: SKIP src/file save integration (file module not ported); journal_save_prepare/finish/cancel run in journal_safety_test"
	@echo "Windows check: $(words $(WIN_TEST_REL)) release + $(words $(WIN_TEST_SAN)) sanitizer test binaries passed; ASan negative control passed"

clean:
	rm -rf $(WIN_B)


# File shims replace only the file implementation objects in this test TU (edit-pf3l).
$(WIN_B)/rel/tests/file_win_test.exe: tests/win/file_win_test.c src/file/file.c src/file/file_win.c tests/file_win_cases.inc tests/file_os_cases.inc $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) $< $(filter-out %/file.o %/file_win.o,$(WIN_REL_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/san/tests/file_win_test.exe: tests/win/file_win_test.c src/file/file.c src/file/file_win.c tests/file_win_cases.inc tests/file_os_cases.inc $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) $< $(filter-out %/file.o %/file_win.o,$(WIN_SAN_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/

# file_test's platform-selected and shared cases are included by its source.
$(WIN_B)/rel/tests/file_test.exe $(WIN_B)/san/tests/file_test.exe: tests/file_os_cases.inc tests/file_win_cases.inc


# Platform contract suite includes its implementation for native API fixtures (edit-upwq).
$(WIN_B)/rel/tests/win32_plat_test.exe: tests/win/win32_plat_test.c src/win32/win32_plat.c src/win32/win32_clip_win.c $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) $< $(filter-out %/win32_plat.o %/win32_clip_win.o,$(WIN_REL_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/san/tests/win32_plat_test.exe: tests/win/win32_plat_test.c src/win32/win32_plat.c src/win32/win32_clip_win.c $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) $< $(filter-out %/win32_plat.o %/win32_clip_win.o,$(WIN_SAN_OBJ)) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/


# W2.3's controller units refuse unavailable file/journal operations; with
# file and journal in the Windows build they run against the production symbols.
ifneq ($(filter file,$(WIN_MODULES)),)
ifneq ($(filter journal,$(WIN_MODULES)),)
$(foreach c,rel san,$(addprefix $(WIN_B)/$(c)/tests/,savectl_test.exe savectl_reload_test.exe)): WIN_TEST_FLAGS += -DEDIT_WIN_SAVECTL_REAL_DEPS
endif
endif
# Integration gate for the W2.1/W2.2 dependency landing (edit-1kqw).
check-savectl-production: $(WIN_B)/rel/tests/savectl_test.exe $(WIN_B)/san/tests/savectl_test.exe
	@set -e; for t in $^; do echo "== $$t"; tools/winrun.sh $$t; done

-include $(shell find $(WIN_B) -name '*.d' 2>/dev/null)
