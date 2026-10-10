# edit-dnc2 — slice 2 / session 9 worker report

Status: **partial; do not close edit-dnc**. The mapped first viewport now
submits from a bounded prefix before mapping completion and before full piece
metadata attachment. Full-size index setup is also past that viewport. Launching
the index before full piece attachment remains missing because the required
pre-attachment mapped source is unavailable through the allowed dependency APIs.
The final frozen-source full sanitizer check also remains incomplete: its bounded
run timed out waiting for the shared display lock. No timing-gate acceptance is
claimed.

## Scope and decisions

Changed `src/editor/buffers.c`, `src/editor/large.c`,
`tests/editor_large_test.c`, and attribution only in
`bench/editor_large_bench.c`, plus this report and
[the design record](../decisions/edit-dnc2.md).
`src/editor/open.c` required no change. No new globals. No changes to dependency
modules, `src/editor/input.c`, `tests/editor_p4_test.c`, HANDOFF, worklog, or
Makefile. Git was read-only and no bd command was run. Corpus fixtures were
read-only; generated evidence is under `build/edit-dnc2-evidence/` in this tree.

Mapped files larger than the file prefix use the existing two-tree preview and
adoption model. The preview and its temporary index are bounded by prefix size;
tabs and undo retain the stable full-tree identity. The estimated gutter uses
full-file size and prefix density. Input stays queued until adoption.

Mapped `editor_open`/`editor_add_buffer` still return the full tree and a prepared
warm snapshot: they drive the preview frame to retirement, wait for mapping if
necessary, then attach/adopt before returning. This retains immediate deep byte
jumps and full-source snapshots. The existing startup tests retain their checks.
Small mapped prepared buffers keep their existing startup behavior. Explicit
off-prefix IPC positioning first acquires/adopts the full source; its first
correct viewport must show the requested position.

Full index launch precedes the remaining adoption reservation and warm-snapshot
preparation. Warming still waits for exact index publication. Setup allocations
remain on open/maintenance; queued typing uses the existing reservations.
Full metadata attachment remains an indivisible foreground maintenance call
after the first frame. There is no claim of a shorter whole-open call or sliced
attachment.

## Per finding

* **G5 viewport blocked behind full attachment: fixed.** A regression written
  first inspects the actual submit callback on the mapped sparse fixture. It
  checks that the rendered first byte is the correct inverse control glyph,
  rendering binds the preview, and the stable full tree is still empty at
  submit. After open returns it checks stable tab/tree identity, full length,
  queued input and the full prepared warm snapshot.
* **Mapped prefix blocked behind mapping acquisition: fixed.** A second red
  assertion holds the bulk lane while acquisition uses the independent lane.
  Submission releases the hold, with a bounded fallback preventing a red-run
  hang. The assertion proves the viewport submits while mapping is still
  pending. Mapped acquisition now waits only for the prefix on this path.
* **Index worker before eager full attachment: missing.** Full-size index setup
  is after the first viewport and launch is before warm preparation, but it
  still follows `file_attach`. A separate opt-in regression waits until mapping
  is ready, proves the full tree is still empty, and requires a full-length
  running index. It remains red. `file_prefix` exposes only the bounded prefix;
  `file_snapshot_bytes` and backing binding require an attached snapshot, and
  `file_source` requires attachment without exposing spans. A later slice needs
  an authorized pre-attachment immutable file source/backing API and its index
  binding contract. No duplicate unguarded mapping or private file representation
  was introduced to bypass those contracts.
* **Startup preservation and typing: green.** Existing immediate deep-byte jump,
  mapped find before indexing, snapshot lifetime/fault/cancellation, exact gutter
  publication, prepared-buffer completion and typing-allocation checks pass.
  A review of the new preview-driving loop found an introduced IPC reentrancy
  risk. Its socket-backed regression first failed because a queued request
  replaced the active tab before mapped open returned. The existing `opening`
  owner now covers the preview-driving phase and defers IPC admission until
  return. The regression checks that the queued request is admitted afterward.
  Its private socket and lock fixture live inside this worktree.

## Pasted red runs

Release regression on the original implementation, exit 1 (M)[AC], before any
production change:

```text
editor_large_test:152: FAIL probe.before_attachment
editor_large_test:391: FAIL mapped_preview_first() == 0
```

After the first fix, before removing the mapped-acquisition barrier, exit 1
(M)[AC]:

```text
editor_large_test:165: FAIL probe.before_mapping
editor_large_test:405: FAIL mapped_preview_first() == 0
```

Introduced startup reentrancy regression, written before its admission fix,
release exit 1 (M)[AC]:

```text
editor_large_test:223: FAIL tabs_count(&e->tabs) == 2
editor_large_test:464: FAIL mapped_startup_ipc() == 0
```

The remaining early-index requirement is reproducible with
`DISPLAY=:99 EDIT_DISPLAY=:99 DNC2_REQUIRE_EARLY_INDEX=1 build/tests/editor_large_test`.
It is deliberately opt-in because it records unfinished work, not a passed
acceptance item. Final-source exit 1 (M)[AC]:

```text
editor_large_test:196: FAIL lineidx_len(prepared->index) == file_size(prepared->file) && lineidx_building(prepared->index)
editor_large_test:466: FAIL index_before_attachment_gap() == 0
```

Original G5 row captured before the first production change; ns fields are
(M)[AC], gate fields are (G). The shared box is loaded; printed pass bits are
harness observations, not certification:

```text
STAMP (M)[AC] BAT0=Not charging load1=2.22 loaded TRACK shared box
BENCH name=editor_large_sparse_10g_G5_open_first_viewport n=3 p50=9250560 p99=9409883 ci95=[8075916,9409883] gate_p50=6000000 gate_p99=9000000 pass=0 power=[AC]
```

## Pasted green runs

Final release `build/tests/editor_large_test`, exit 0 (M)[AC]; the final
ASan/UBSan large-file binary prints the same output and also exits 0 (M)[AC]:

```text
edit-dnc2 startup: queued IPC open waits for mapped preview adoption and is admitted on a later turn
edit-dnc2 G5: correct mapped prefix submitted before full attachment; stable tree, queued input and prepared warm snapshot retained
edit-dnc G6: mapped find before index, window crossings/nonoverlap, dense saturation, bounded offsets and fault refusal passed
edit-9yd: unindexed 1 GiB gutter is dim; first publication frame is exact; theme and allocation checks passed
edit-dnc G7: mid-frame index arrival publishes exact gutter in the same frame; zero typing allocations
editor_large_test: ok
```

## Final loaded back-to-back comparison

The preserved baseline binary runs immediately before the final binary with
`--attribute-sparse`. Each includes the same existing G5 row and correctness
check. All ns values are (M)[AC]; gate fields are (G). The sample is diagnostic
TRACK evidence on the shared box, not a gate claim. The observed G5 p50/p99
changes from 8.520194/9.330951 ms to 4.208069/4.725098 ms (M)[AC]. Reference:
6/9 ms (G). Sparse indexing remains outside a calibrated fixture-size G7 gate.

```text
BEFORE
STAMP (M)[AC] BAT0=Not charging load1=2.16 loaded TRACK shared box
BENCH name=editor_large_sparse_10g_G5_open_first_viewport n=3 p50=8520194 p99=9330951 ci95=[7671572,9330951] gate_p50=6000000 gate_p99=9000000 pass=0 power=[AC]
AFTER
STAMP (M)[AC] BAT0=Not charging load1=2.59 loaded TRACK shared box
BENCH name=editor_large_sparse_10g_G5_open_first_viewport n=3 p50=4208069 p99=4725098 ci95=[3586839,4725098] gate_p50=6000000 gate_p99=9000000 pass=1 power=[AC]
ATTR sparse_10g sample=2 viewport_before_attachment=1 (M)[AC]
ATTR sparse_10g sample=2 phase=viewport_attachment ns=2926396 (M)[AC]
ATTR sparse_10g sample=2 phase=viewport_index_submit ns=5578910 (M)[AC]
```

The former `index_submit_viewport` phase is unavailable on the new ordering
because submission now precedes index launch. Added attribution prints that
ordering explicitly. The bench retrospectively attributes an initial viewport
submitted inside `editor_open` to the file owner obtained when open returns;
the original submit timestamp is preserved.
The existing index phase marker is at entry to warm preparation, after build
submission and reservation; its interval includes that reservation and is not
an exact work-queue timestamp.

## Verification

* Final gcc 13 `DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all`: exit 0 (M)[AC].
  C11 and `-Wall -Wextra -Werror -Wshadow -Wconversion` remain enabled.
* Final release large-file suite and clang 18 ASan/UBSan large-file suite:
  exit 0 (M)[AC]. Release typing allocation guards remain active; ASan's guard
  is inert, so the sanitizer run is not allocation evidence.
* Earlier complete `make check` runs with socket/Xvfb access: exit 0 (M)[AC],
  including 61 test binaries, replay CLI and display-lock contracts (M)[AC].
  Those full runs began before the final IPC startup refinement. The final
  frozen-source full check passed the editor, core, raster and wrap checks, but
  its 660-second bound (G) expired while waiting for the shared `:99` lock at
  `x11_clip_review_test`: exit 124 (M)[AC]. Thus final full-suite acceptance is
  **missing**; the coordinator must rerun it. The first sandbox-only attempt
  failed at CLI Unix-socket setup. Earlier display-lock contract tests emitted
  expected nested timeout/failure messages and then reported PASS.
* Module `editor_fuzz -max_total_time=60 -max_len=1024`: exit 0, 19497 runs in
  61 seconds (M)[AC], against 60 seconds (G), with ASan/UBSan enabled.
  Its existing model primarily exercises in-memory editing; mapped ordering is
  covered by the dedicated regression above.
* LeakSanitizer was disabled with `ASAN_OPTIONS=detect_leaks=0` for sanitizer
  tests and fuzz, as required by the sandbox. The coordinator must rerun with
  leaks enabled. All display-backed commands use `:99`; none use `:0`.
* `git diff --check` is clean. No new globals, no typing-path malloc added.

Frozen-source whole-suite ending (pasted); the process was terminated by its
bounded timeout, not by a test assertion:

```text
wrap_test: 10000 relayouts mallocs=0 guard=ASan-inert
wrap_test: 10000 typing edits mallocs=0 guard=ASan-inert
wrap_test: all passed
== build/san/tests/x11_clip_review_test
waiting for display :99 (held by 62811)
make: *** [Makefile:105: check] Terminated
```

Missing acceptance is the full-index launch before full metadata attachment,
a completed frozen-source full sanitizer suite, leak-enabled coordinator
verification, and any timing-gate verdict. The design record identifies the
dependency API needed for the remaining launch order. No source changes were
added after the minute-30 scope cutoff (G); the final phase was verification
and completion of this report.
