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
