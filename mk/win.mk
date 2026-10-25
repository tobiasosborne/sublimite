# Windows cross build (edit-177r.1): llvm-mingw clang, x86_64-w64-windows-gnu (ucrt), from WSL.
# `make TARGET=win all|check|clean`. Output in build/win/{rel,san}. Linux targets are untouched.
WIN_TOOLCHAIN ?= $(HOME)/opt/llvm-mingw
WIN_CC  := $(WIN_TOOLCHAIN)/bin/x86_64-w64-mingw32-clang
WIN_WARN := -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion
WIN_INC  := -Isrc
WIN_REL_F := -O2 -g -msse2
WIN_SAN_F := -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2
WIN_B := build/win
# asan runtime imports libc++.dll, which imports libunwind.dll; all three must sit beside the exe.
WIN_SAN_DLLS := libclang_rt.asan_dynamic-x86_64.dll libc++.dll libunwind.dll
# Modules compiled for Windows; W0.4 grows this list (src/<m>/*.c).
WIN_MODULES :=
WIN_SRCS := $(foreach m,$(WIN_MODULES),$(wildcard src/$(m)/*.c))
WIN_TESTS := $(sort $(wildcard tests/win/*_test.c))
WIN_REL_OBJ := $(WIN_SRCS:%.c=$(WIN_B)/rel/%.o)
WIN_SAN_OBJ := $(WIN_SRCS:%.c=$(WIN_B)/san/%.o)
WIN_TEST_REL := $(patsubst tests/win/%_test.c,$(WIN_B)/rel/tests/%_test.exe,$(WIN_TESTS))
WIN_TEST_SAN := $(patsubst tests/win/%_test.c,$(WIN_B)/san/tests/%_test.exe,$(WIN_TESTS))

.PHONY: all check clean
all: $(WIN_TEST_REL)

$(WIN_B)/rel/%.o: %.c
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) -MMD -MP -c $< -o $@
$(WIN_B)/san/%.o: %.c
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) -MMD -MP -c $< -o $@

# Static link; release has only system-DLL imports. The sanitizer exe needs the runtime DLLs beside it (tools/winrun.sh runs in place).
$(WIN_B)/rel/tests/%_test.exe: tests/win/%_test.c $(WIN_REL_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_REL_F) $(WIN_INC) $< $(WIN_REL_OBJ) -static -o $@
$(WIN_B)/san/tests/%_test.exe: tests/win/%_test.c $(WIN_SAN_OBJ)
	@mkdir -p $(@D)
	$(WIN_CC) $(WIN_WARN) $(WIN_SAN_F) $(WIN_INC) $< $(WIN_SAN_OBJ) -o $@
	@cp -u $(addprefix $(WIN_TOOLCHAIN)/x86_64-w64-mingw32/bin/,$(WIN_SAN_DLLS)) $(@D)/

# check = release run + sanitizer run of every Windows test, plus the
# --expect-asan red case, which must exit nonzero with an ASan report.
check: $(WIN_TEST_REL) $(WIN_TEST_SAN)
	@set -e; for t in $(WIN_TEST_REL) $(WIN_TEST_SAN); do echo "== $$t"; tools/winrun.sh $$t; done
	@for t in $(WIN_TEST_SAN); do echo "== $$t --expect-asan (must fail)"; \
	  if tools/winrun.sh $$t --expect-asan 2>$(WIN_B)/asan.err; then echo "ASan did not fire"; exit 1; fi; \
	  grep -q AddressSanitizer $(WIN_B)/asan.err || { echo "no ASan report"; cat $(WIN_B)/asan.err; exit 1; }; echo "ASan fired: ok"; done

clean:
	rm -rf $(WIN_B)

-include $(shell find $(WIN_B) -name '*.d' 2>/dev/null)
