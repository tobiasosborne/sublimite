# Windows cross build (edit-177r.1): llvm-mingw clang, x86_64-w64-windows-gnu (ucrt), from WSL.
# `make TARGET=win all|check|clean`. Output in build/win/{rel,san}. Linux targets are untouched.
WIN_TOOLCHAIN ?= $(HOME)/opt/llvm-mingw
WIN_CC  := $(WIN_TOOLCHAIN)/bin/x86_64-w64-mingw32-clang
WIN_RC  := $(WIN_TOOLCHAIN)/bin/x86_64-w64-mingw32-windres
WIN_WARN := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion
WIN_INC  := -Isrc
WIN_REL_F := -O2 -g -msse2
WIN_SAN_F := -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2
WIN_B := build/win
# asan runtime imports libc++.dll, which imports libunwind.dll; all three must sit beside the exe.
WIN_SAN_DLLS := libclang_rt.asan_dynamic-x86_64.dll libc++.dll libunwind.dll
# Modules compiled for Windows; W0.4 grows this list (src/<m>/*.c). base joins
# with its malloc guard only (edit-177r.3): base.c's arena and pool still call
# mmap and move onto os.h in W0.4.
WIN_MODULES := os
WIN_SRCS := $(foreach m,$(WIN_MODULES),$(wildcard src/$(m)/*.c)) $(wildcard src/base/malloc_guard*.c)
# Windows-only tests live in tests/win/; portable suites that already build for
# Windows are listed here by name (edit-177r.3: the os conformance suite).
WIN_TESTS := $(sort $(wildcard tests/win/*_test.c)) tests/os_test.c
WIN_REL_OBJ := $(WIN_SRCS:%.c=$(WIN_B)/rel/%.o)
WIN_SAN_OBJ := $(WIN_SRCS:%.c=$(WIN_B)/san/%.o)
WIN_TEST_REL := $(addprefix $(WIN_B)/rel/tests/,$(addsuffix .exe,$(basename $(notdir $(WIN_TESTS)))))
WIN_TEST_SAN := $(addprefix $(WIN_B)/san/tests/,$(addsuffix .exe,$(basename $(notdir $(WIN_TESTS)))))
# System import libraries os_win.c needs (NT native calls, BCryptGenRandom, SHGetKnownFolderPath).
WIN_LIBS := -lntdll -lbcrypt -lshell32 -lole32
# UTF-8 activeCodePage manifest (src/os/utf8.manifest, decision doc edit-177r.2
# §3.9), embedded as RT_MANIFEST 1 through a windres-compiled resource and linked
# into every Windows executable: the CRT's narrow argv, fopen and getenv become UTF-8.
WIN_MANIFEST_OBJ := $(WIN_B)/utf8_manifest.o
# Release builds route our own malloc/calloc/realloc/free through the counting
# guard (src/base/malloc_guard_win.c) at link time; the sanitizer runtime owns
# malloc in the san build, where the guard is compiled out and nothing is wrapped.
WIN_WRAP := -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free

.PHONY: all check clean win-compile
WIN_COMPILE_MODULES := base trace work piece undo scroll lineidx layout view utf8 scan find hl
all: $(WIN_TEST_REL)
# Objects reached only through pattern rules would be intermediates and deleted after each link.
.SECONDARY: $(WIN_REL_OBJ) $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)

win-compile: WIN_INC += -Ivendor/tree-sitter/lib/include
win-compile: $(patsubst %.c,$(WIN_B)/rel/%.o,$(foreach m,$(WIN_COMPILE_MODULES),$(wildcard src/$(m)/*.c)))

$(WIN_B)/rel/%.o: %.c
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) -MMD -MP -c $< -o $@
$(WIN_B)/san/%.o: %.c
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) -MMD -MP -c $< -o $@
$(WIN_MANIFEST_OBJ): src/os/utf8.rc src/os/utf8.manifest
	@mkdir -p $(@D)
	$(WIN_RC) -I src/os -i $< -o $@

# Static link; release has only system-DLL imports. The sanitizer exe needs the runtime DLLs beside it (tools/winrun.sh runs in place).
# Two source locations per test name: tests/win/<t>.c (Windows-only) or tests/<t>.c (portable suite); make takes the rule whose source exists.
$(WIN_B)/rel/tests/%_test.exe: tests/win/%_test.c $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) $< $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/rel/tests/%_test.exe: tests/%_test.c $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) $< $(WIN_REL_OBJ) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) $(WIN_WRAP) -static -o $@
$(WIN_B)/san/tests/%_test.exe: tests/win/%_test.c $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) $< $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/
$(WIN_B)/san/tests/%_test.exe: tests/%_test.c $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) $< $(WIN_SAN_OBJ) $(WIN_MANIFEST_OBJ) $(WIN_LIBS) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/

# check = release run + sanitizer run of every Windows test, plus the
# --expect-asan red case, which must exit nonzero with an ASan report.
# The os suite's reparse case reads OS_TEST_DATA_REPARSE: a compact-compressed
# (WOF reparse tag) file that tools/win_reparse_fixture.sh creates as a Windows
# process under %TEMP%\sublimite-test-reparse and removes afterwards.
# tools/winrun.sh forwards OS_TEST_CASE and OS_TEST_DATA_REPARSE through WSLENV.
check: $(WIN_TEST_REL) $(WIN_TEST_SAN)
	@set -e; fx=$$(tools/win_reparse_fixture.sh create); printf "reparse fixture: %s\n" "$$fx"; \
	  trap 'tools/win_reparse_fixture.sh remove' EXIT; \
	  for t in $(WIN_TEST_REL) $(WIN_TEST_SAN); do echo "== $$t"; OS_TEST_DATA_REPARSE="$$fx" tools/winrun.sh $$t; done
	@for t in $(filter %smoke_test.exe,$(WIN_TEST_SAN)); do echo "== $$t --expect-asan (must fail)"; \
	  if tools/winrun.sh $$t --expect-asan 2>$(WIN_B)/asan.err; then echo "ASan did not fire"; exit 1; fi; \
	  grep -q AddressSanitizer $(WIN_B)/asan.err || { echo "no ASan report"; cat $(WIN_B)/asan.err; exit 1; }; echo "ASan fired: ok"; done

clean:
	rm -rf $(WIN_B)

-include $(shell find $(WIN_B) -name '*.d' 2>/dev/null)
