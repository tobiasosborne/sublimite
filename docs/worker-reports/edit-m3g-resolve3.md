# edit-m3g resolver 3

Resolved the seven conflict hunks in `src/editor/{editor.c,input.c,open.c,private.h}`
while retaining main's save/startup, clipboard, minimap pixel composition, and
shortcut behavior together with edit-m3g's display sizing and worker font zoom.
No `git` or `bd` commands were run; no rebase staging/continuation was performed.
The final [marker scan](../../build/edit-m3g-resolve3/markers.log) found no remaining
conflict markers in the source/document tree.

## Resolution

- `editor.c`: font mailbox authentication precedes journal routing; journal
  delivery still honors the save lease. Runnable/pending state includes both
  clipboard and font work, retaining the clipboard submission guard.
- `input.c`: Ctrl+S dispatches `editor_save_request`; font-size bindings dispatch
  `editor_font_zoom`.
- `private.h`: retained the clipboard owner and `EDITOR_ACTION_CLIP`, plus both
  font atlas versions and the font-size worker state.
- `open.c`: retained main's pixel region limits and startup restore path while
  using the branch's font-independent surface bounds and full atlas reserve.
- Minimap resize keeps one-pixel columns and two-pixel summary rows:
  `ceil(text_rows * cell_height / 2)`. Font zoom updates text geometry without
  replacing this summary geometry with text-sized cells.

Simply joining the conflict sides was insufficient: the existing font-size test
failed its first display-derived zoom after the rebuilt merge
([red output](../../build/edit-m3g-resolve3/font-size-merge.log)). Main's minimap
pixel width reserve had been derived from the initial font. A larger font exceeded
that reserve. Main's initial-font height reserve could also exceed a supplied
work-area surface bound or fail to cover later resized/zoomed geometry.

`font_size.c` now records the maximum cell metrics during its existing worker
initialization. `open.c` reserves the eight-column minimap strip using that
maximum cell width, capped at the surface width, and uses the font-independent
maximum surface height for pixel and summary storage. Both pixel buffers,
per-buffer minimap rows, and renderer region capacity remain prepaid before input.

Added a regression to `tests/font_size_test.c`: a 96-DPI launch with explicit
work-area bounds, resize to those bounds, and zoom through the maximum em and
reset. It checks two-pixel minimap geometry, summary/pixel capacity, renderer
validation, unchanged document length, and zero allocations when the release
counting allocator is active. Existing retained-atlas, worker completion,
coalesced reset, shutdown, lower-bound, DPI, and default/reset probes also pass.

## Build

The following build returned 0:

```sh
make -j4 all build/san/tests/editor_test build/san/tests/font_test \
  build/san/tests/minimap_test build/san/tests/font_size_test \
  build/san/tests/keys_test build/san/tests/runtime_test \
  build/san/tests/x11_font_size_test
make all
```

Release compilation used gcc; sanitizer compilation used clang ASan/UBSan.
Both used `-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`.
[Complete build log](../../build/edit-m3g-resolve3/build.log),
[final `make all`](../../build/edit-m3g-resolve3/make-all.log).

## Verification

All sanitizer runs used `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`. No ASan/UBSan findings appeared in these runs.

| Probe | Exit/result |
|---|---|
| `build/san/tests/editor_test --ck4-save` | 0; actual Ctrl+S save and abrupt-exit recovery |
| `--ck4-startup` | 0; same-file discovery and restored dirty bytes before paint |
| `--clipboard` | 0; all clipboard subprobes |
| `--minimap-flash` | 0; no orange frames or changed pending image |
| `--minimap-pixels` | 0; pixel composition |
| `--ck4-credit`, `--ck4-metadata`, `--ck4-target`, `--ck4-fifo` | All 0 |
| `--ck4-keep`, `--ck4-reload`, `--ck4-mapped-keep`, `--ck4-mapped-reload` | All 0 |
| `--selection-chords`, `--redo-or-repeat`, `--panel-notice` | All 0 |
| `build/san/tests/font_test` | 0; all passed |
| `build/san/tests/minimap_test` | 0; all passed |
| `build/san/tests/font_size_test` | 0; existing probes plus resize/maximum zoom |
| `build/san/tests/keys_test`, `build/san/tests/runtime_test` | Both 0 |
| `font_size_fuzz.c` fixed seed replay | 0; 16 resource, physical-size, nonfinite, and extreme inputs |
| `build/tests/font_size_test` | 0; release allocation guard active |
| Full `build/san/tests/editor_test` | 1; failures described below |
| `build/san/tests/x11_font_size_test` | 1; cannot open display |

Exact commands/statuses and per-probe logs:
[editor manifest](../../build/edit-m3g-resolve3/editor.tsv),
[module manifest](../../build/edit-m3g-resolve3/modules.tsv).
[Sanitizer font-size output](../../build/edit-m3g-resolve3/font-size.log),
[release font-size output](../../build/edit-m3g-resolve3/font-size-release.log).
The fixed seed harness and its strict-warning build log are retained in
`build/edit-m3g-resolve3/font-size-seed-main.c` and `seed-build.log`; this was
fixture replay, not a timed fuzz campaign.

## Full-suite/native limitations

Both native-capable commands were first attempted on `:99` through
`tools/with_display_lock.sh` with immediate lock refusal. That display was held
by an unknown owner; neither command reached its tests there
([manifest](../../build/edit-m3g-resolve3/native.tsv)).

Retried both on `:98`, also through the shared display lock. The full editor
binary ran its default aggregate and returned 1. Its native order, burst,
allocation, and close probes failed at `editor_open`. The reload/launcher probe
also reported `IPC_IO` (result 4) at the wait assertion. The aggregate returned
failure at `review_suite(NULL)`, so subsequent default-main cases were not reached.
The original edit-m3g report already recorded native-open and launcher `IPC_IO`
failures; the launcher failure's cause is not independently established here.

[Full editor output](../../build/edit-m3g-resolve3/editor-all-98.log),
[native font output](../../build/edit-m3g-resolve3/x11-font-size-98.log),
[retry manifest](../../build/edit-m3g-resolve3/native-retry.tsv).
A separate socket check confirmed both X socket connects (`:98`, `:99`) and a
fresh AF_UNIX bind fail with `EPERM`
([socket evidence](../../build/edit-m3g-resolve3/socket-access.log)).

The build and focused headless probes pass. Full editor/native acceptance remains
unpassed and requires a runnable X/socket environment; this report does not claim
native atlas adoption, screenshot/ink-height acceptance, or a complete suite pass.
