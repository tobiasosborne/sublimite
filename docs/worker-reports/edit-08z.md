# edit-08z worker report

Implemented the Unicode raster-path integration for this bead. No `bd` or
`git` command was run. All changes are in this worktree.

## Changes

- `src/font/async.c`, `src/font/font.h`: INIT-reserved request/descriptor owner,
  embedded primary face, worker-only fallback files and scalar-resolution cache,
  general glyph rasterisation, immutable request arguments and authenticated
  mailbox adoption. Stable slots initially paint a pending placeholder and then
  adopt completed artwork. Existing borrowed atlas rectangles never change.
- `src/font/discovery.c`, `src/font/discovery.h`: reuse the cancellable isolated
  fontconfig parser for exact-codepoint discovery with verified charset coverage.
- `src/font/unicode.c`: optional worker scalar resolver; the existing foreground
  cache API remains compatible. Combining marks use the existing coverage union.
- `src/font/embedded.h`, `tools/embed_font.py`: reproducible byte-for-byte copy
  of the licensed vendor primary face, removing a launch-directory dependency.
- `src/editor/open.c`, `src/editor/private.h`: owner lifetime, renderer runtime
  reservations and requesting-row bits. Backend shutdown and pool join precede
  releasing font storage.
- `src/editor/editor.c`, `src/editor/fonts.c`: request pumping, adoption and
  damage only for still-visible users in registered rows. Font jobs remain
  background work, independent of pending-UI status and input submission.
- `src/editor/paint.c`: only the glyph lookup changed, including its tab-row
  registration and blank-space adapter. The minimap stale overlay was untouched.
- `tests/unicode_render_test.c`: production editor/null-grid acceptance for
  initial and injected accented Latin, Greek, CJK and emoji; cell widths,
  combining base preservation/additional ink, scalar/SIMD raster agreement,
  submission while the bulk lane is held, active release allocation guards,
  completion damage, chrome spaces, mailbox identity/duplicate handling,
  resident hits and preservation of earlier borrowed pixels during atlas append.
- `src/font/STATUS.md`, `docs/decisions/edit-08z.md`, this report, session worklog
  and handoff: implementation, lifetime and validation notes.

Raster and GL already support INIT-reserved descriptor/page growth and immutable
pixel borrows. The editor now supplies those reservations; their implementation
files required no changes. Storage-ceiling derivations are documented in the
[decision](../decisions/edit-08z.md); no new latency or memory target was invented.

## Red, before production changes

Power probe: `Discharging`, therefore observations are (M)[bat]. Source locations
in pasted output derive from the test's `__LINE__`; exit statuses are the observed
process result, with failure/success using the executable's return-code contract.

Command: `make build/tests/unicode_render_test`, then
`build/tests/unicode_render_test`. The build succeeded and the test failed because
its bitmap equalled the baked question-mark bitmap:

```text
unicode_render_test:38: FAIL different
unicode_render_test:68: FAIL bitmap(g, cell) == 0
unicode_render_test:82: FAIL render_unicode(false) == 0
exit=1
```

A regression added during chrome integration also caught a plain space being
turned into a question mark. Its red run, before fixing the glyph adapter:

```text
unicode_render_test:106: FAIL editor_grid(e)->cells[sizeof "a"].glyph_index == ' '
unicode_render_test:245: FAIL render_unicode(false) == 0
exit=1
```

## Green and verification

Final release command: `build/tests/unicode_render_test`:

```text
unicode_render_test: initial and injected é α 日 🙂; widths, combining overlay, atlas pixels and allocation guard passed
exit=0
```

Final sanitizer command:
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/unicode_render_test`:

```text
unicode_render_test: initial and injected é α 日 🙂; widths, combining overlay, atlas pixels and allocation guard passed
exit=0
```

`make -j"$(nproc)" all` passed with GCC release and the required warning flags.
The parallelism derives from the available CPU count, not a fabricated budget.
The embedded primary was independently compared with the vendor bytes and matched.

The inherited release editor allocation fixture passed with its guard active.
The new cold Unicode fixture holds the bulk worker through the matching input
submission, so the process-wide guard cannot count legitimate worker font I/O.
The standalone owner fixture also checks cold embedded requests and resident hits
under the active release guard. Sanitizer builds explicitly have an inert malloc
interposition guard; allocation evidence comes from the release runs.

An integration check exposed that reporting glyph jobs as pending UI work would
make the existing held-bulk-worker control fixture wait for its own held worker.
That status coupling was removed. The targeted `EDITOR_REVIEW_ONLY=p4-12`
sanitizer fixture passes; background glyph work does not delay UI quiescence.
`work_test` passes after the real standalone owner fixture satisfies its existing
pool-alignment source audit. The replay CLI shell contracts also pass with the
same sanitizer environment.

The font benchmark was run once. It passed as TRACK evidence on battery; power,
load and inherited sample/order-statistic output are in
`build/edit-08z/font-bench.log`. This is not a native typing-latency or Sublime
comparison claim.

## Full check and sandbox restrictions

Required command was run:
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=0 make -j"$(nproc)" check`.
The display setting is the user-specified coordinator display. Immediate lock
refusal avoids waiting on an unavailable display lease; the earlier invocation
without this override encountered an occupied display lock. The final invocation
acquired the lock and stopped at denied IPC setup:

```text
== build/san/tests/base_test
0.0.1-dev
arena: ok
malloc guard: SKIP (AddressSanitizer owns malloc)
pool: ok
cpu: avx2=1 popcnt=1
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test: display lock timeout: display :99 (held by unknown)
make: *** [Makefile:105: check] Error 1
```

The process failed; full `make check` is **not green** in this sandbox. Every
sanitizer test executable was additionally invoked individually, with logs in
`build/edit-08z/` and outcomes in `suites.tsv`. Final targeted reruns are recorded
in `work_test-final.log`, `editor_test-final.log`, `editor-pending-controls.log`,
`harness-final.log` and the Unicode green logs above.

Direct socket probes demonstrate the restriction:

```text
AF_UNIX bind/listen: PermissionError(1, 'Operation not permitted')
X display connect: PermissionError(1, 'Operation not permitted')
```

The errno value derives from the operating system's EPERM result. IPC failures
therefore affect suites that otherwise could be headless; they cannot all pass
under this permission profile. No socket code was changed to hide that failure.

Display-dependent suites failed: `editor_close_test` (native close; injected close
passed), `file_kill_test` (live viewport/queued window fixtures), `raster_test`,
`refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`,
`x11_clip_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`,
`x11_stall_test`. The native cases in `editor_test` also failed at editor/display
initialisation. `gl_test` passed its scripted/pure checks and explicitly skipped
native EGL/Present diagnostics; `gl_gate_test` passed. Passing X unit checks do
not establish native display acceptance.

Denied Unix-socket cases: `cli_test`, `ipc_test`, `editor_large_test` (startup IPC),
`editor_p4_test` (IPC server setup), and the launcher/reload portion of
`editor_test`. The final `editor_test` run records those IPC/display failures;
its held-worker fixture passes separately.

Runnable headless suites passed: `base_test`, `file_test`, the `find_*` suites,
`findui_test`, `font_test`, `harness_test`, `indent_test`, `journal_kill_test`,
`journal_test`, `keys_test`, `layout_test`, `lineidx_test`, `minimap_test`, the
`piece_*` suites, `prewake_test`, `render_test`, `replay_test`, `runtime_test`,
`savectl_reload_test`, `savectl_test`, `scan_test`, `scroll_test`, `tabs_test`,
`trace_test`, `undo_test`, `unicode_render_test`, `utf8_tables_test`, `utf8_test`,
`view_test`, `work_foreground_test`, `work_test`, `wrap_test`. The pure
`x11_alignment_test`, `x11_display_guard_test`, `x11_input_test` and
`x11_xi2_test` also passed.

LeakSanitizer cannot run under this sandbox's ptrace environment. The coordinator
must rerun with leak checking and socket/display access, including the deferred
shell display-lock contract.

## Faces found on this box

Production fallback uses an exact scalar query, with file/index and coverage
validated on the worker. These paths were independently confirmed with
`fc-match -f '%{file}\n%{index}\n%{color}\n' ':charset=65e5:color=false'`
and the corresponding `:charset=1f642:color=false` query:

- Embedded primary: `vendor/DejaVuSansMono.ttf`; accented Latin and Greek are
  covered without a system face.
- CJK: `/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc`.
- Smiling emoji: `/usr/local/share/fonts/iosevka/SGr-Iosevka-Regular.ttc`.

Both reported collection indexes are the first face (fontconfig index zero),
and both report `color=False`. Those values are measured query output, not
assumptions. The older representative-emoji benchmark query selects
`/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf`; exact smiling-emoji discovery
selects Iosevka instead. Initial/injected bitmap acceptance passed on this box.

## Remaining and outside scope

Coordinator: rerun the complete check with socket access on the specified Xvfb
and re-run the next Astra vs Sublime drive on this build. No native display,
real latency-parity or drive verdict is claimed here.

Cache retirement/eviction remains outside the integration: inherited atlas,
request and probe ceilings produce settled missing artwork on refusal, while
resident pixels stay immutable. Full bidi/contextual shaping and colour emoji
remain outside this bead.

The existing minimap stale indicator was observed clearing in the tab strip when
the held index job finished. It is explicitly excluded from the text-row damage
assertion and was not fixed. Some inherited test diagnostics hardcode an AC tag;
this session's power probe reports battery, so those printed tags should not be
used to infer the power source. No unrelated test/performance code was changed.

## Rebase continuation: glyph completion and minimap fills (2026-10-11)

Cause: `editor_font_event()` damaged visible users of an adopted slot by calling
`editor_begin_frame()`. That helper clears `caret_only`, including when it reuses
an already dirty caret frame. `editor_compose()` then took its ordinary path,
which calls `minimap_fill()` unconditionally. Glyph pixels and atlas descriptors
changed, but neither the text revision nor the minimap input changed. A glyph
completion during the explicit drain between the two G11 blink submissions could
therefore advance `minimap_fills` after the test captured its baseline. Runtime
atlas rebinding itself does not invalidate the map; the frame's composition scope
was the cause.

Fix: add a separate `glyph_only` composition flag. Adoption retains this scope
for a clean frame, an existing glyph frame, or a caret frame. Ordinary frame
requests clear it, so text edits, layout changes and index publication still get
normal composition. Blink/focus refresh preserves an existing glyph-only frame.
The limited compose path copies changed text rows while retaining minimap and
chrome cells; the font handler already damages rows containing the adopted slot,
including Unicode tab titles. Pixel-only damage does not set the raster
cursor-only hint: unchanged cell metadata must still redraw the new artwork.
No layout restart, minimap invalidation or heap allocation was added.

Added a deterministic null-backend regression to `unicode_render_test`: hold the
bulk worker while the placeholder frame and map settle, wait for actual mailbox
publication, then adopt alongside idle, pending caret, blink, text input, focus,
and Unicode-title cases. It checks the exact dirty-row union, unchanged retained
cells, unchanged minimap cells/revision/staleness and zero additional fills when
the text did not change. The input case checks that normal map filling remains
enabled. Existing Unicode bitmap, width, combining, scalar/SIMD, mailbox and
allocation checks remain present.

The regression was run red with the original font event handler restored, then
the production fix was restored and rebuilt. Assertions were not relaxed:

```text
ASAN_OPTIONS=detect_leaks=0 build/san/tests/unicode_render_test
unicode_render_test:222: FAIL e->stats.minimap_fills == fills
unicode_render_test:307: FAIL glyph_minimap_repaint() == 0
```

Final sanitizer and release Unicode runs both exit 0:

```text
unicode_render_test: minimap completion mode=0 fills=0 damage=4
unicode_render_test: minimap completion mode=1 fills=0 damage=4
unicode_render_test: minimap completion mode=2 fills=0 damage=6
unicode_render_test: minimap completion mode=3 fills=2 damage=7
unicode_render_test: minimap completion mode=4 fills=0 damage=6
unicode_render_test: minimap completion mode=5 fills=0 damage=5
unicode_render_test: initial and injected é α 日 🙂; widths, combining overlay, atlas pixels and allocation guard passed
```

Here damage is the union of submitted dirty-row bits in hexadecimal. Row 2 holds
the glyphs, row 1 holds the caret, and row 0 holds the title. Mode 3 includes a
real text edit; its two fills cover the input and ready-map publication.

Other final outputs:

```text
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --minimap-flash
edit-g11 null typing: keys=95 frames=96 orange_frames=0 stale_frames=95 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=18972

EDITOR_ALLOC_ONLY=null ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
EDITOR_ALLOC_ONLY=null build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active

font_test: all passed
minimap_test: density, hit round trip, stale, malloc guard: ok
GraphemeBreakTest: 1187 cases, 0 mismatches, 0 chunked mismatches
utf8_test: all passed
utf8 tables test: ok (Unicode 15.1.0, table bytes 16720)
layout_test: all passed
wrap_test: all passed
```

`make -j4 all build/san/tests/editor_test build/san/tests/unicode_render_test`
exited 0 after the final edits. Compilation uses the unchanged Makefile's
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion` flags. The release Unicode
and 10,000-key runs exercise the active malloc guard.

The exact requested full command,
`ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test`, was run before and after
the fix. Both exit 1 at IPC/display-dependent review fixtures, before reaching
the reported line-259 assertion. Final failure output:

```text
reload launcher result=4
editor_test:1031: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:422: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:448: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:730: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:1386: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:2000: FAIL review_suite(NULL) == 0
```

`ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --raster-blink` exits 1
at `editor_test:229: FAIL editor_open(&e, &cfg, &b) == 0`. Direct AF_UNIX probes
for `/tmp/.X11-unix/X99` connect and temporary IPC bind both return
`[Errno 1] Operation not permitted`. The full suite and native G11 raster blink
case therefore still need an environment with socket/display access; they are
not claimed green here. The null-backend regression, all Unicode tests,
`--minimap-flash` and build are green. No git or bd commands were run.

Logs: `build/edit-08z/minimap-rebase/` contains the red/green Unicode, release
Unicode, flash, malloc guard, module checks, full editor, raster blink and final
build outputs.
