# edit-2ghs (s8): opening/activating a PDF tab kills the editor with error -2

**Origin of -2:** `RENDER_ERR_BOUNDS` from `render_atlas_validate`, src/render/render.c:144-146 (the `!glyph->w || !glyph->h` descriptor check). It reaches `render_backend_submit` (render.c:324-325), then `submit` (src/editor/editor.c:404-405), then `editor_step` (editor.c:800), then `editor_run` (editor.c:824), then main.c:199 "error -2". The zero-sized descriptor was produced by `src/font/async.c`. A blank-ink settled glyph (here U+F227, bytes `ef 88 a7`, drawn blank by a fallback face) was stored as `(render_glyph){0}` in the bound table (async.c:607 pre-fix). The LRU eviction path did the same (async.c:566 pre-fix).

## Finding it

- The repro on :97 (1024x768) did **not** die (ran 10 s, rc 124 from timeout). On :99 (2880x1800) it died at once: `sublimite: error -2; journal: ...`, rc 1. The blank cluster is only inside the larger first viewport.
- gdb with breakpoints on all 90 `return <-2 code>` sites (release -O2, so many line attributions were false). The last hit before exit on the UI thread:
  ```
  #0  render_atlas_validate (g=0x7ffff7d66b50, bytes=0x7fffffffa540) at src/render/render.c:142
  #1  render_backend_submit (b=0x7fffffffa850, g=..., strips=..., count=1) at src/render/render.c:324
  #2  submit (e=0x7ffff7cc9040) at src/editor/editor.c:404
  #3  editor_step (e=..., timeout_ms=-1) at src/editor/editor.c:800
  #5  editor_run (e=0x7ffff7cc9040) at src/editor/editor.c:824
  #6  main at src/main.c:171
  ```
- A temporary trace (removed; render.c is byte-identical to the original) pinned the exact branch: `glyph 162/276 page=0 x=0 y=0 w=0 h=0 pw=760 ph=16 cell=8x16`. A second temporary trace in `font_async_event` gave `inkless slot 162 w1: ef 88 a7 result=0`.

## Fix

The fix is in the producer, src/font/async.c; the frozen render contract is unchanged. Each request gets an `inked` flag: true while pending (the `?` placeholder) or when the image has ink, false for a blank result or a vacated slot. Blank and vacant slots get `blank_descriptor()`, the baked ASCII space rectangle (page 0, full cell, no ink) under the slot's own glyph_index. Lookups answer NO_SLOT from `inked`. The new `font_async_inked()` (font.h) replaces the `.w == 0` test in `editor_font_event` (src/editor/fonts.c). There is no new allocation, and the only new storage is one bool per request. Details, the rejected alternative, the binary policy and the crash-loop findings are in docs/decisions/edit-2ghs.md.

## Files touched

- src/font/async.c: `inked` flag, `blank_descriptor`, `font_async_inked`, lookup, eviction and adoption
- src/font/font.h: `font_async_inked` declaration
- src/editor/fonts.c: blank-adoption retarget uses `font_async_inked`
- tests/editor_test.c: `binary_open_contract` (`--2ghs-binary`, also in the default suite). It generates a 256 KiB PDF-like fixture (header, NUL, random high bytes, newlines, NBSP/EN SPACE/IDEOGRAPHIC SPACE/ZWSP/U+F227) at 200x60 cells, runs 200 steps, settles, requires a submitted frame and a valid grid, then closes and reopens.
- tests/unicode_render_test.c: `blank_descriptors` (NBSP narrow and U+3000 wide settle blank, the table validates, re-lookup answers NO_SLOT), and a `render_grid_validate` after the width-changing eviction in `cache_exhaustion`.
- docs/decisions/edit-2ghs.md, this report.

## Red runs (original async.c/fonts.c)

```
$ build/tests/editor_test --2ghs-binary
editor_test: 2ghs launch 0 step 2 rc -2
editor_test:5363: FAIL rc == EDITOR_OK || rc == EDITOR_MORE
$ build/tests/unicode_render_test          (with a one-line font_async_inked shim, since the original lacks it)
unicode_render_test:450: FAIL render_grid_validate(&grid) == RENDER_OK
unicode_render_test:758: FAIL blank_descriptors() == 0
$ REDTMP=1 build/tests/unicode_render_test (blank test skipped, so the eviction assertion runs)
unicode_render_test:589: FAIL render_grid_validate(&grid) == RENDER_OK
unicode_render_test:760: FAIL cache_exhaustion(true) == 0
```

## Green runs

```
$ build/tests/editor_test --2ghs-binary
edit-2ghs binary file opens, paints and reopens          (rc 0)
$ build/tests/unicode_render_test
unicode_render_test: blank images keep a valid descriptor table
... all passed                                            (rc 0)
```

## Verification

All runs were on a box stamped `Not charging` (BAT0), uptime load average 8.28 / 13.55 / 11.30 at 11:54. No timings are claimed.

| command | exit |
|---|---|
| `make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{editor,font,layout,raster,unicode_render,render}_test` | 0 |
| `ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/font_test` | 0 |
| same, `layout_test` | 0 |
| same, `render_test` | 0 |
| same, `unicode_render_test` | 0 |
| same, `raster_test` | 0 |
| same, `editor_test`, full suite: includes "edit-2ghs binary file opens, paints and reopens"; "editor_test: all passed" | 0 |
| release `EDITOR_ALLOC_ONLY=null build/tests/editor_test`: "null 10000 keys mallocs=0 guard=active" | 0 |
| release `EDITOR_ALLOC_ONLY=raster build/tests/editor_test`: "raster 10000 keys mallocs=0 guard=active" | 0 |
| release `build/tests/editor_test --2ghs-binary` | 0 |
| `sh tools/test_gl_optional_native.sh`: SKIP, Xvfb/EGL unavailable (result=-10, no DRI3) | 0 |
| bead repro on :99 (2880x1800): `build/sublimite /home/tobias/Downloads/main-1.pdf`, alive after 6 s, SIGTERM, clean exit | 0 |
| same with `EDIT_BACKEND=gl`: EGL refused on Xvfb, falls back to raster, alive, SIGTERM | 0 |
| manual drive on :99: 300 x Page_Down + Ctrl+End through the 2 MB PDF (exercises LRU eviction), screenshot shows replacement glyphs to `%%EOF`, SIGTERM | 0 |
| relaunch in the same profile with README.md: session restores tabs `main-1.pdf` and `README.md`; Ctrl+PageUp/PageDown/PageUp activates the PDF tab, alive, SIGTERM | 0 |

main-1.pdf is unchanged (2,113,109 B, mtime Aug 28 01:26). The test unlinks its fixture on success. The red run left one `build/2ghs-binary-*`, which I removed, because the T() early return skips the unlink.

**GL:** the native GL backend cannot run on Xvfb here (EGL/Present unavailable, code -10). What was not run is a real GLX/EGL display. What was established: the failing check is `render_backend_submit`'s shared validation, which runs before `ops.submit` for every backend. The GL renderer (src/gl/renderer.inc:1050-1056) reads every descriptor generically, and a page-0 space rectangle is an ordinary valid entry. So the same fix covers Tobias's GL display. It still needs a check on his real display.

## What remains / out of scope (reported, not fixed)

1. **No crash-loop guard for paint failures after a successful restore.** A restore that fails during install is set aside with a notice (recovery.c:529-533, open.c:288-300). But any error that ends `editor_run` after restore leaves the journal in place (main.c:172-199), and the next launch restores the same tabs. A tab whose paint fails therefore kills every launch once it is activated; that was Tobias's follow-on symptom. A guard needs a journal marker such as "restored session has presented a frame for tab X". That is a separate bead.
2. **Any other render-contract violation still ends the editor.** `submit` returns every `render_backend_submit` error. A blanket degrade (drop the frame) would freeze the window without a visible error, so I did not add one. Worth deciding at the coordinator level.
3. **The repro depends on viewport size.** It does not reproduce on :97 (1024x768). Future regressions in this area need a large viewport (the test uses 200x60 cells).
4. **Binary-content policy:** no detection was added. Files always open as text with replacement glyphs, as Sublime does. Any ratio threshold would be an underived number. See the decision doc.
