# edit-5ih rebase resolver report

Resolved the eight conflict hunks in `src/editor/editor.c`, `open.c`,
`private.h`, and `src/font/font.c`. No `git` or `bd` commands were run;
rebase/index bookkeeping was left to the caller. The source tree contains no
remaining conflict markers.

## Integrated behavior

- Main's `editor_font_size` remains the single atlas lifecycle: two immutable
  versions (adopted and detached replacement), with `e->fonts` aliasing the
  adopted async Unicode owner. Removed the branch's separate selection atlas,
  glyph/page descriptors, worker message, and resize/adoption path.
- Family/file requests add a generation to the existing requested/prepared/
  applied pixel-size state. The same foreground worker resolves Fontconfig,
  loads the primary/TTC face, validates unrounded monospace advances, and
  rebuilds ASCII. Zoom/reset can supersede preparation without dropping the
  family request. A family adoption counts once even if size work coalesces.
- A prepared primary descriptor is lent to the version's async owner, so
  Unicode and ASCII use the same selected face and legacy/true-em size mode.
  Main's worker fallback discovery, glyph mailbox authentication, completion
  damage tracking, and pending-owner retirement remain in place.
- Adoption requires mailbox receipt and physical font-job completion, an idle
  backend, and completed frame/layout/input work. Reuse also waits for the
  detached owner's Unicode jobs to finish and drain. Backend shutdown and pool
  joins precede async owner and primary-byte release. This replaces the old
  branch-specific publication-before-worker-return behavior with main's lease
  rules.
- Grid, view, minimap, pixel viewport, renderer limits, point/DPI sizing,
  Ctrl zoom/reset, and startup stage instrumentation follow main's lifecycle.
  Font selection uses the existing maximum-cell, ASCII, glyph scratch and
  minimap reservations. Faces exceeding those cell bounds fall back to
  embedded DejaVu rather than growing storage during input.
- `EDIT_FONT_FAMILY`, `EDIT_FONT_FILE`, file precedence, `#decimal` TTC indices,
  exact-family matching, empty selection restore, and worker-only once-per-
  editor fallback diagnostics are retained. P6.4/config-file hot reload remains
  outside this resolver's scope.
- Resolved paths survive zoom and superseded preparation. Selected files read
  directly into worker arenas with `open`/`read`, avoiding recurring stdio heap
  allocations. UI request copying and atlas adoption allocate no heap memory.

The selected-face loader now owns only request/primary bytes and diagnostics;
`font_size.c` owns atlas production. Added the detached-owner
`font_async_set_primary` handoff. Updated the font-selection test and benchmark
for the unified state, and fixed two `bench/hl_bench.c` callers that lacked the
branch-added font-pattern argument. Module STATUS notes and the public request
comment describe the final lifecycle.

## Verification

Final `make -j4 all` exited **0** with GCC and the project's
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion` flags. The targeted
sanitizer build also exited **0** (Clang ASan + UBSan). Test runs used
`ASAN_OPTIONS=detect_leaks=0`; native/editor runs used `DISPLAY=:99` and
`tools/with_display_lock.sh`. No native tests used the real display.

| Final sanitizer command | Exit / result |
|---|---|
| `build/san/tests/font_test` | 0, PASS |
| `build/san/tests/font_selection_test` | 0, PASS |
| `build/san/tests/font_size_test` | 0, PASS |
| `build/san/tests/unicode_render_test` | 0, PASS |
| `build/san/tests/x11_font_size_test` | 1, native display initialization unavailable |
| `build/san/tests/editor_test` (all) | 1, inherited launcher/native failures below |
| `build/san/tests/editor_test --minimap-flash` | 0, PASS |
| `build/san/tests/editor_test --clipboard` | 0, PASS |
| `build/san/tests/editor_test --ck4-save` | 0, PASS |

These cover every `tests/font*_test.c` in this tree, plus the native font-size
probe. `editor_test` has no separate font/zoom CLI flags; the font-size,
font-selection and Unicode suites exercise those probes.

Final release `font_selection_test`, `font_size_test`, and
`unicode_render_test` all exit **0** with the malloc guard active. The selected
font test checks zero allocations during adoption, zoom/reset and input while
both atlas versions retain pending glyph jobs. The separate release typing
probe also passes:

```text
EDITOR_ALLOC_ONLY=null build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
```

Font-selection coverage retains syntax/overflow, proportional advances hidden
by pixel rounding, installed family, missing/malformed/proportional fallback,
file precedence, TTC face/geometry, baked-pixel equality, environment selection,
and held-frame input preservation. Added combined tests at 96 and 243 DPI:
selected é/α glyph pixels match independent rasterization of the selected
primary; zoom/reset preserves that primary; two family swaps leave glyph jobs
in both versions; a third waits safely while typing still submits; outstanding
selection shutdown joins before bytes are released. The held-frame test also
coalesces zoom/reset while the family request is pending and adopts it once.

Main's font-size tests pass DPI/Xft precedence, point reset, retained atlas,
coalescing, outstanding-job shutdown, lower-bound renderer capacity and
maximum-zoom/minimap reservations. Its Unicode suite passes initial/injected
é/α/日/🙂, combining artwork, glyph mailbox/retirement, true-em zoom pixels and
completion damage checks.

The final minimap probe reports 95 keys, 96 frames, `orange_frames=0`,
`changed_pending=0`, `chrome_markers=0`, `mallocs=0`. Clipboard round-trip,
bulk/replacement/undo, PRIMARY and no-tab probes pass; `--ck4-save` passes its
abrupt-exit recovery check. No ASan or UBSan diagnostics appear in final logs.

## Remaining verification limitations

The full editor suite is **not passed**. Its failures match those already
recorded in [main's m3g resolver report](edit-m3g-resolve4.md):

```text
reload launcher result=4  # IPC_IO
editor_test:1350: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:741: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:767: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:1049: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:1705: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:2586: FAIL review_suite(NULL) == 0
```

The native failures occur in native input order/burst, allocation and close
fixtures. The default driver stops at `review_suite(NULL)`, so later default
cases are not reached; the requested explicit probes were run independently.
The launcher/reload error is separately unresolved; it is not attributed to
the display failure or claimed to be fixed here.

The native font-size test fails its initial
`editor_runtime_display_size(&display) == 0` assertion at line 18. Native font
acceptance and the full editor suite require a follow-up in a working display/
launcher environment. Leak checking was intentionally disabled as requested.

Build logs, final command manifests (`san-results.json`, `release-results.json`)
and individual outputs are retained under `build/edit-5ih-resolve-evidence/`.
