# Toolchain (captured 2026-10-08)

```
gcc:   gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
clang: Ubuntu clang version 18.1.3 (1ubuntu1)
make:  GNU Make 4.3
xcb: 1.15
xcb-shm: 1.15
xcb-xkb: 1.15
xcb-present: 1.15
xkbcommon: 1.6.0
xkbcommon-x11: 1.6.0
egl: 1.5
gl: 1.2
glx: 1.4
wayland-client: 1.22.0
wayland-egl: 18.1.0
freetype2: 26.1.20
fontconfig: 2.15.0
```

Packages installed on 2026-10-08 (apt):

```
sudo apt install libxcb-xkb-dev libxkbcommon-x11-dev libxcb-present-dev
```

## Build layout

- `make all` release (gcc, `-O2 -g -msse2`): `build/libedit.a`, tools, tests, benches. Objects in `build/rel/`.
- `make check` clang ASan/UBSan, objects and test binaries in `build/san/`; runs every test.
- `make bench` release benches (`build/bench/`), non-zero exit = gate miss.
- `make fuzz` builds `build/fuzz/<m>_fuzz` (libFuzzer); run manually.
- Adding a module: drop files in `src/<m>/`, `tests/<m>_test.c`, `bench/<m>_bench.c`, `fuzz/<m>_fuzz.c`. The Makefile globs.
- `tools/ci.sh` runs the full battery.

## libFuzzer link
clang 18 selects `/usr/lib/gcc/x86_64-linux-gnu/14` (partial install, no libstdc++) and `-fsanitize=fuzzer` fails with `cannot find -lstdc++`. The Makefile passes `--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/13` on fuzz links. Workers building fuzzers by hand need the same flag.

## Windows cross toolchain (edit-177r.1) — PARTIAL, not yet verified

- Release chosen: llvm-mingw `20260922` (published 2026-09-22; 33 days old on 2026-10-25), asset `llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz`.
- sha256 (M, of the downloaded file): `bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21`.
- Install (no sudo):
  ```
  mkdir -p ~/opt/dl && cd ~/opt/dl && curl -LO https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz
  sha256sum -c <<< "bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21  llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz"
  cd ~/opt && tar xf dl/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz && ln -sfn llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64 llvm-mingw
  ```
- Build: `make TARGET=win all|check|clean` (`mk/win.mk`, `WIN_TOOLCHAIN` overrides `~/opt/llvm-mingw`); output in `build/win/{rel,san}`.
- Run: `tools/winrun.sh <exe> [args...]` (runs the exe in place; see below).
- Clang version (M): 23.1.2, target x86_64-w64-windows-gnu. sha256 matches GitHub's published digest.
- Release smoke exe imports (M, `llvm-readobj --coff-imports`): KERNEL32.dll and the UCRT api-sets api-ms-win-crt-{stdio,runtime,locale,heap,private,string,math,environment}-l1-1-0.dll. All system DLLs. Sanitizer exe additionally imports `libclang_rt.asan_dynamic-x86_64.dll` (in `<toolchain>/x86_64-w64-mingw32/bin/`; the Makefile copies it next to the exe and winrun.sh copies sibling DLLs).
- LeakSanitizer on this target (M, 2026-10-25, clang 23.1.2): NOT available. A leaking exe built with `-fsanitize=address` and run with `ASAN_OPTIONS=detect_leaks=1` prints `AddressSanitizer: detect_leaks is not supported on this platform.` and exits 1; with the default options the leak goes unreported (exit 0). So leak checking stays a Linux-only check.
- Run in place: `/mnt/c` is mounted read-only by Tobias's choice (`/etc/wsl.conf` automount option `ro`). Never write to or remount `/mnt/*`. `tools/winrun.sh` therefore runs the exe in place from its build directory (WSL interop starts it from the `\\wsl.localhost` path), passes args, propagates the exit code and writes nothing. A test that needs NTFS scratch files creates them itself as a Windows process under `%TEMP%\sublimite-test-*` and deletes them afterwards.
- Sanitizer exe runtime DLLs, copied beside it by mk/win.mk from `<toolchain>/x86_64-w64-mingw32/bin/`: `libclang_rt.asan_dynamic-x86_64.dll` imports `libc++.dll`, which imports `libunwind.dll`. Missing ones give exit 53 (0xC0000135).

## ThinkPad X1 Carbon Gen 11, fresh Linux Mint 22.3 (Ubuntu 24.04 base), 2026-10-26

Captured while bringing the tree up on the reinstalled laptop (Fable main session). Native X11 (Cinnamon) session, 12 threads, 30 GB, 1.8 TB disk.

```
gcc:   gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0      (preinstalled)
clang: Ubuntu clang version 18.1.3 (1ubuntu1)           (apt clang-18; ~/.local/bin/clang -> /usr/bin/clang-18, same for llvm-symbolizer)
make 4.3, python3 3.12.3, Xvfb 21.1.12, ImageMagick 6.9.12, x11-xkb-utils 7.7 (preinstalled)
pkg-config: xcb 1.15, xkbcommon 1.6.0, egl 1.5, gl 1.2, glx 1.4, wayland-client 1.22.0, wayland-egl 18.1.0, wayland-server 1.22.0, freetype2 26.1.20, fontconfig 2.15.0
```

Packages installed (apt, Tobias ran it; everything the Makefile and the per-module `src/*/LDLIBS` link against):

```
sudo apt install clang-18 libclang-rt-18-dev libxcb1-dev libxcb-shm0-dev libxcb-xkb-dev libxcb-present-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-randr0-dev libxcb-xtest0 libxkbcommon-dev libxkbcommon-x11-dev libegl-dev libgl-dev libglx-dev mesa-common-dev libwayland-dev libwayland-bin
```

Test-suite fixtures outside the repo, and where they came from:

- `tests/font_selection_test.c` and `tests/font_cli_test.c` select the installed family **Go Mono** (Debian package `fonts-go`). Installed per user without root: `apt-get download fonts-go`, `dpkg -x`, `Go-Mono*.ttf` into `~/.fonts`, `fc-cache -f`. `sudo apt install fonts-go` gives the same files system-wide.
- `~/.fonts/NotoEmoji-wght.ttf` (monochrome emoji stopgap, edit-v16): restored from the old laptop's squashfs backup (`~/Documents/x1-backup-2026-10-26/home.sqfs`, `unsquashfs -f` of single paths needs no root).
- `tools/test_display_lock.sh contract` (the last step of `make check`) uses **ripgrep** (`rg`). Installed per user: `apt-get download ripgrep`, `dpkg -x`, `usr/bin/rg` into `~/.local/bin`. `sudo apt install ripgrep` is the system-wide equivalent.
- Iosevka (`/usr/local/share/fonts/iosevka` on the old box) is NOT on this box; no test needs it (only `tests/font_selection_test.c:178` parses the name).
- Xvfb `:95` (coordinator landings) and `:99` (test guard target) as `Xvfb :N -screen 0 2880x1800x24 -dpi 100 -nolisten tcp -noreset`; `/tmp/edit-corpus` from `tools/mkcorpus` plus `truncate -s 10G sparse_10g.bin` (all_a_1g.txt is only for find_bench/findui_bench).
- The llvm-mingw Windows cross toolchain (above) lives on Spiral, not here.
