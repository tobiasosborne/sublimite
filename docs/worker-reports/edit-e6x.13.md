Completed P2.3d. Arena exhaustion now exits stb through a C error boundary, restores the arena, and returns `FONT_ERR_NOMEM`. The font remains usable. ASan/UBSan, release, `make check`, `make bench`, and fuzz smoke checks passed (M)[AC]. The benchmark gate is unchanged.

Files changed:

- [src/font/font.c](/home/tobias/Projects/editor/src/font/font.c)
- [src/font/font.h](/home/tobias/Projects/editor/src/font/font.h)
- [tests/font_test.c](/home/tobias/Projects/editor/tests/font_test.c)
- [docs/decisions/P2.3d.md](/home/tobias/Projects/editor/docs/decisions/P2.3d.md)

Red run output, before the fix (M)[AC]:

```text
font_test: wide W scratch exhaustion
AddressSanitizer:DEADLYSIGNAL
==13==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
==13==The signal is caused by a WRITE memory access.
    #2 in stbtt__rasterize_sorted_edges vendor/stb_truetype.h:3322:7
    #7 in font_raster_glyph src/font/font.c:167:5
==13==ABORTING
```

Green run output (M)[AC]:

```text
ASan/UBSan:
font_test: wide W exhaustion returns FONT_ERR_NOMEM; normal arena succeeds
font_test: bitmap/scratch exhaustion sweep and arena rollback passed
font_test: all passed

Release:
font_test: mallocs during exhaustion sweep: 0
font_test: mallocs during 600 rasterisations: 0
font_test: all passed
```

`make check` tail (M)[AC]. An initial live X11 failure passed in isolation and on the full rerun:

```text
x11_stall_test: clipboard set return 0.027 ms (M)[AC], budget 5 ms (G)[AC]: ok
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 14 test binaries passed
```

Bench output, times in ns:

```text
BENCH name=font_raster n=20000 p50=2258 p99=4241 gate_p50=50000 dropped=0 tag=(G)(M)[AC] loaded
BENCH name=font_fallback_discover n=1 ns=10658989 gate=none fontconfig=1 cjk=/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc emoji=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf tag=(M)[AC] loaded
BENCH name=font_raster_cjk_4E2D n=20000 p50=2563 p99=5167 gate=none dropped=0 tag=(M)[AC] loaded
```