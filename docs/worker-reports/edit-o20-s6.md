# edit-o20 — worker s6

Implemented the requested file commands, cancellation and viewport scrolling.
No bd or git commands were run. Changes stay in this worktree; no protected
handoff, worklog, planning or requirements files were edited.

All reproduced results below are (M)[AC], transcribed from the commands' output;
BAT0 read `Not charging`, interpreted as AC by the repository handoff. Diagnostic
line numbers, action IDs, error codes, key labels, toolchain versions and bead
names are identifiers. Test sample/allocation counts derive from the existing
fixture/allocator loops; there are no newly invented measurement budgets.

## Files touched

- `src/editor/commands.c`: deferred New/Open/Save As setup, failure notices,
  atomic durable writing, title/path/controller/watch/base rebinding and
  reconciliation of other tabs holding the target.
- `src/editor/STATUS.md`: successful command scope and remaining transactional-save limitation.
- `src/editor/private.h`: prompt kind/path storage, queued file command and
  target watch/source lease.
- `src/editor/input.c`: common commands bypass find-field consumption, typed
  path prompts, UTF-8 prompt editing, Escape selection collapse, close-event
  routing, exact logical/visual row scrolling.
- `src/editor/editor.c`: queued setup prevents subsequent input from overtaking
  publication; pending/runnable state includes it.
- `src/editor/buffers.c`: maintenance admission, target-watch handling,
  resource retirement and reload/keep for rebound untitled buffers.
- `src/editor/paint.c`: prompt labels and prepaid UTF-8 path rendering; decimal
  go-to-line rendering stays unchanged.
- `src/keys/keys.c`: clear only implemented flags, including the equivalent
  XF86Open alias and the scroll directions.
- `tests/editor_test.c`: null-backend command tests in text/find focus, prompt
  editing/cancellation, open failures and empty-tab opening, Save As from
  untitled/COPY/MMAP sources, subsequent Ctrl+S/undo, clean/dirty target changes,
  shared-target base validity and logical/wrapped scroll preservation.
- `tests/keys_test.c`: executable-row expectations.
- `docs/decisions/edit-o20.md`: design choices and derivations.
- `docs/worker-reports/edit-o20-s6.md`: this report.

## Red runs

Each requested command test and the changed key-table expectations were run
before implementation with the gcc release build:

```text
$ build/tests/editor_test --o20-new
editor_test:3884: FAIL tabs_count(&e->tabs) == tabs + 1u
exit=1
$ build/tests/editor_test --o20-open
editor_test:3889: FAIL e->go.active
exit=1
$ build/tests/editor_test --o20-save-as
editor_test:3897: FAIL e->go.active
exit=1
$ build/tests/editor_test --o20-exit
editor_test:3917: FAIL rc == EDITOR_CLOSED
exit=1
$ build/tests/editor_test --o20-cancel
editor_test:3924: FAIL e->v.state.selection.anchor == e->v.state.selection.cursor
exit=1
$ build/tests/editor_test --o20-scroll
editor_test:3933: FAIL e->v.state.first_line == top + 1u
exit=1
$ build/tests/keys_test
keys_test: FAIL ctrl+q expected action=141 flags=0 value=0; got action=141
tests/keys_test.c:301: assertion failed: 0
exit=-6
```

Additional regressions were run before their fixes. Open after closing the last
tab and UTF-8 path rendering/editing failed:

```text
$ build/tests/editor_test --o20-open-empty
editor_test:3894: FAIL e->go.active
exit=1
$ build/tests/editor_test --o20-path-edit
editor_test:61: FAIL rc == EDITOR_OK || rc == EDITOR_MORE
editor_test:3899: FAIL press(e, key(XKB_KEY_eacute, 0, unicode)) == 0
exit=1
```

The clean target-watch regression exposed a null original-file handle in reload:

```text
$ build/tests/editor_test --o20-save-clean-watch
exit=-11
```

Saving over another tab's target exposed an unreconciled peer journal base:

```text
$ build/tests/editor_test --o20-save-shared
editor_test:3927: FAIL e->recovery_notice == NULL
```

## Green runs

The final `build/tests/editor_test --o20-all` run exits successfully in gcc release.
The individual requested command runs became green in gcc release, with text
and find focus tested in each case. The final combined command suite also
passed in release and clang ASan/UBSan. The following is the final sanitizer
run, with `ASAN_OPTIONS=detect_leaks=0`:

```text
$ build/san/tests/editor_test --o20-all
edit-o20 new: text and find focus PASS
edit-o20 open: text and find focus PASS
edit-o20 save-as: text and find focus PASS
edit-o20 exit: text and find focus PASS
edit-o20 cancel: text and find focus PASS
edit-o20 scroll: text and find focus PASS
edit-o20 open-empty: text and find focus PASS
edit-o20 path-edit: text and find focus PASS
edit-o20 save-source: text and find focus PASS
edit-o20 save-watch: text and find focus PASS
edit-o20 scroll-wrap: text and find focus PASS
edit-o20 save-clean-watch: text and find focus PASS
edit-o20 save-mapped: text and find focus PASS
edit-o20 save-shared: text and find focus PASS
exit=0
```

Release key-table/allocation evidence:

```text
$ build/tests/keys_test
keys_test: 256 independent binding rows passed
keys_test: 20000 lookups mallocs=0 guard=active
keys_test: all passed
exit=0
```

Release full headless editor suite passed before the final watch/shared-target
extensions. Final full headless ASan/UBSan editor suite passed after those
extensions, including the preceding go-to-line preview, exact seek, overflow,
accept/cancel, scroll restoration and soft-history tests:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --headless
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
editor_test: idle blink timeout and unfocused timer disarm passed
editor_test: queue depth one and coalesced edits passed
editor_test: headless passed
exit=0
```

The release command tests enforce the counting allocator on prompt editing and
Unicode rendering; its expected empty allocation count derives from the
repository typing-path rule (P). ASan owns malloc, so its guard is inert.
File setup occurs after the input frame retires, through the existing buffer,
file and save-controller APIs. New/Open/Save As preserve text undo state;
selection cancellation and scrolling create no text undo entries.

## Required builds and broader checks

The final `make all` passes with gcc release under the repository C standard and
warning flags. Toolchains observed: gcc 13.3.0 and clang 18.1.3 (version IDs).

```text
$ make all
make: Nothing to be done for 'all'.
exit=0
```

Full `make check` was attempted. The default run stopped at LeakSanitizer's
sandbox limitation:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:131: check] Error 1
```

With leak detection disabled, ASan and UBSan remain active. Full `make check`
then stopped in CLI socket setup:

```text
== build/san/tests/cli_test
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:131: check] Error 1
```

The Makefile's non-display inventory was derived from `TEST_SAN` minus
`DISPLAY_TEST_SAN`. It was checked through `make check` and, after its early
failures, the remaining binaries were run serially. Full non-display acceptance
is **not claimed**: the failures listed below remain outside this bead.

Additional sanitizer editor suites were run with the final library. Their
results are (M)[AC], command exit statuses rather than new acceptance counts:

```text
build/san/tests/editor_close_test --headless exit=0
build/san/tests/editor_scroll_test exit=0
build/san/tests/editor_session_test exit=0
build/san/tests/editor_hot_exit_test exit=0
build/san/tests/editor_large_test exit=1
build/san/tests/editor_p4_test --headless exit=1
```

`editor_close_test`, `editor_scroll_test`, `editor_session_test` and
`editor_hot_exit_test` pass. `editor_large_test` and headless `editor_p4_test`
reach their IPC portions and fail the sandbox's Unix-socket initialization;
the null backend alone does not remove that dependency.

All remaining non-display binaries completed serially. Exceptions are the
findui assertion, IPC permission, journal callback and missing corpus cases
below. The deliberately failing fixture probes printed by find/harness tests
are self-check inputs; their parent suites exited successfully.

## Outside scope; left unchanged

- `findui_test` aborts at its document-length assertion, including an isolated
  serial rerun. No findui implementation or test was edited:

  ```text
  tests/findui_test.c:121: assertion failed: length <= sizeof bytes && piece_len(tree) == length
  ```

- `ipc_test`, `editor_large_test` and headless `editor_p4_test` cannot initialize a Unix-domain server in this sandbox:

  ```text
  init rc=4 errno=1 (Operation not permitted)
  FAIL tests/ipc_test.c:130: init_rc==IPC_OK
  edit-2ty editor_large_test:215: FAIL ipc_server_init(&server, runtime) == IPC_OK
  editor_p4_test:555: FAIL ipc_server_init(&server, runtime) == IPC_OK
  ```

- `journal_kill_test` fails its first replay trial with `JOURNAL_CALLBACK`:

  ```text
  journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
  ```

- `lineidx_test` requires `/tmp/edit-corpus/sparse_10g.bin`, which is absent:

  ```text
  tests/lineidx_test.c:339: FATAL file_open_ready(f) && file_open_mode(f) == FILE_MODE_MMAP
  lineidx_test: 1 failure(s)
  ```

- `file_kill_test` includes native viewport/queued-frame subtests via
  `bench/file_bench.c` although the Makefile classifies it as non-display.
  Those subtests fail without the X socket. Its existing cancellation-only
  entry point passes under ASan/UBSan; full acceptance requires the display.

- Ordinary Ctrl+S on an untitled buffer still takes the existing save-failure
  path. Ctrl+Shift+S now supplies its path; changing the ordinary Save command
  was not requested in this bead.

## Tracked module benches

Power was read immediately before execution: BAT0 `Not charging` (M)[AC].
Keys and the editor's supported null-only font-baseline lane passed. Counts,
geometry, durations and existing TRACK targets below derive from the bench's
existing loops, atlas/viewport geometry and monotonic endpoints, as labeled in
the output; they are not new budgets. The initial `EDIT_BACKEND=null` invocation
was rejected by the bench's existing argument contract; the supported lane
below was then used with that variable unset.

```text
$ build/bench/keys_bench
POWER status=Not charging [AC] load1=7.77; keys measurements TRACK only
BENCH keys_lookup TRACK n=1000000 p50=0.067 us p99=0.156 us (M)[AC] load1=7.77 no limit (no derivation for 1 us)
STREAM (M)[AC] load1=7.77 matches=436925 prefixes=250575 cancelled=125575 misses=186925 mallocs=0 guard=active gate_mallocs=0(G)
exit=0
$ env -u EDIT_BACKEND build/bench/editor_bench --track '--font-baseline=DejaVu Sans Mono'
STAMP (M)[AC] BAT0=Not charging load1=4.59 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
NEW_FONT_BASELINE family=DejaVu Sans Mono cell=16x30 grid=180x60 pixels=2880x1800 swaps=1 (M)[AC] derivation=worker_atlas_cell,viewport_div_cell,adopted_generations
FONT_RASTER atlas_ns=532368 glyph_max_ns=14168 glyphs=95 (M)[AC] derivation=monotonic_end_minus_start,maximum_per_glyph,ASCII_last_minus_first_plus_one
POSITION null byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
BENCH name=editor_null_serial_injection_submit n=10000 required_n=10000 p50=901380 p99=1539486 ci95_p50=[901137,901693] ci95_p99=[1521058,1633707] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=4.59 verdict=TRACK
BENCH name=editor_null_serial_dequeue_submit n=10000 required_n=10000 p50=877819 p99=1487322 ci95_p50=[877576,878091] ci95_p99=[1464160,1571334] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=4.59 verdict=TRACK
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=1034350 p99=1806401 ci95_p50=[1034073,1034648] ci95_p99=[1776646,1901676] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=4.59 verdict=TRACK
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=4.41 shared box
NEW_FONT_BASELINE family=DejaVu Sans Mono cell=8x15 grid=360x120 pixels=2880x1800 swaps=1 (M)[AC] derivation=worker_atlas_cell,viewport_div_cell,adopted_generations
FONT_RASTER atlas_ns=392216 glyph_max_ns=10359 glyphs=95 (M)[AC] derivation=monotonic_end_minus_start,maximum_per_glyph,ASCII_last_minus_first_plus_one
BENCH name=editor_null_100tabs_ingress_T5_G3 n=10000 required_n=10000 p50=603602 p99=639657 ci95_p50=[603498,603701] ci95_p99=[635996,651315] gate_p50=5000000 gate_p99=5560000 dropped=0 (M)[AC] power=Not charging load1=4.41 verdict=TRACK
BENCH name=editor_null_100tabs_minimap_inside_frame n=10000 required_n=10000 p50=43486 p99=47465 ci95_p50=[43486,43487] ci95_p99=[47340,47614] gate_p50=0 gate_p99=500000 dropped=0 (M)[AC] power=Not charging load1=4.41 verdict=TRACK
G3 null (M)[AC] load1=4.41 tabs=100 A=2880x1800 switches=10000 allocations=0 minimap_fills=10202 stale=0 mode=TRACK
exit=0
```

## What remains

No requested command executor or successful-path Save As controller/base
rebind is left missing. Save As performs synchronous maintenance rather than
Ctrl+S's asynchronous acknowledgement. The deeper remaining journal work is
pre-write retention/PREPARE for a different target, followed by transactional
association publication. This implementation writes first and then rotates a
complete checkpoint. A crash in between preserves the preceding journal and
the durable new file, but an overwritten target already serving as another
tab's BASE can make that preceding journal conflict and be set aside; its edits
are not guaranteed replayable. Normal same-path Save As uses the existing
Ctrl+S transaction. This limitation is also recorded in `src/editor/STATUS.md`;
no new journal protocol was introduced in this bead.

Display suites fail inside this sandbox for lack of the X socket. The
coordinator must rerun them on Xvfb :99, including file_kill_test's native
subtests, and rerun leak detection outside ptrace. The independent non-display
failures above also need coordinator follow-up; none was repaired here.
