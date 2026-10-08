# bench/ — microbenchmarks behind 00-hardware-profile.md

Build: `gcc -O2 -pthread <file>.c -o <name> [-lxcb] [-lxcb-shm] [$(pkg-config --cflags --libs gtk+-3.0)]`

| File | Measures | Profile section |
|---|---|---|
| `membench.c` | pointer-chase latency per working-set size; scalar seq read, memcpy, 1/4/8 threads | base profile (on battery) |
| `scanbench.c` | SSE2/AVX2 newline-count + ASCII-flag scan, memchr, 64 KiB chunk; clock ramp after 2 s idle | Addendum 1 |
| `xcbwin.c`, `gtkwin.c` | native window create → first Expose / first GTK draw; whole-process wall | Addendum 1 |
| `rasterbench.c` | full-frame naive CPU raster + XShm upload to a pixmap (sync round-trip), after 15 s idle and warmed, ST/MT4/MT8; one-line strip. Args: `W H` | Addendum 2 |
| `rasterbench2.c` | SSE2 blend kernel, coverage-adjustable atlas, raster only. Args: `coverage% simd(0/1) [W H]` | Addendum 3 |
| `review.sh`, `angle-*.txt` | Codex reviewer harness and the per-round review angles | 02-review-*.md |

**Battery rerun TODO:** unplug, confirm `cat /sys/class/power_supply/BAT0/status` = Discharging, then run scanbench, `rasterbench 2880 1800`, `rasterbench2 25 1`, and append results as Addendum 4.
