# edit-1um worker report

Moved the dirty external-source reload/keep prompt from the first document
row into the existing bottom status row. The first-line glyphs remain visible
while the source is suspended. Kept the established later save-overlay
precedence to preserve the required displayed `saving` acknowledgement.

## Files touched

- `src/editor/paint.c`: bottom-row prompt cells and damage marking only.
- `tests/editor_test.c`: null placement/restoration/allocation regression,
  focused dispatch and full-suite registration; clean-reload submitted-frame
  probe follows the bottom-row placement; the existing unsupported-command
  notice test now expects the source notice in bottom status, clipped to the
  actual grid width, with the first row intact.
- `docs/decisions/edit-1um.md`: placement, precedence and fixture derivations.
- `docs/worker-reports/edit-1um-s6.md`: this report.

## Red run

The fixture first establishes the first-line glyphs without a notice, then
sets the same stale-source flag used by detection. The failure below is the
glyph assertion after the prompt appears, before changing paint.c.
Captured diagnostic line numbers and exit codes are measured outputs (M)[AC],
not budgets. BAT0 reported `Not charging` (AC).

```text
$ make build/tests/editor_test
[release build passed]
$ build/tests/editor_test --source-prompt-placement
editor_test:1961: FAIL e->grid.cells[first_row + e->lay.gw + c].glyph_index == (uint8_t)first[c]
exit: 1
```

## Green run

```text
$ make build/tests/editor_test
[release build passed]
$ build/tests/editor_test --source-prompt-placement
edit-1um: null source prompt preserves first-line glyphs, uses bottom status, restores row and allocates nothing: PASS
exit: 0
$ make all
[passed]
exit: 0
```

Final release rebuild (`make all`) also passed after updating the existing
notice assertion. The sanitizer test binary rebuilt with Clang, ASan and
UBSan. With `ASAN_OPTIONS=detect_leaks=0`:

```text
$ build/san/tests/editor_test --source-prompt-placement
edit-1um: null source prompt preserves first-line glyphs, uses bottom status, restores row and allocates nothing: PASS
exit: 0
$ build/san/tests/editor_test --panel-notice
editor_test: panel shortcuts show a generation-only status and restore the bottom row
exit: 0
$ build/san/tests/editor_test --clean-source-change
edit-ret: clean open/save append reload, copied/mapped and inactive tabs, view preservation/clamping, undo reset and no submitted prompt passed
exit: 0
$ build/san/tests/editor_test --headless
[all headless cases passed]
editor_test: headless passed
exit: 0
$ build/san/tests/editor_p4_test --73j-null
[null indentation, queued groups, decorations, tabs, wrap, minimap and index tests passed]
exit: 0
$ build/san/tests/editor_session_test
editor_session_test: headless save/repeated/new-target, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed
exit: 0
$ sh tools/test_replay_cli.sh build
test_replay_cli: all passed
exit: 0
```

The zero-allocation result (M)[AC] comes from the existing counting allocator
around stale-source layout through null-backend submit; the required zero
is the no-allocation rule. Fixture geometry and offsets are derived in the
test (E). No budget, duration or repetition count was added.

## Broader validation and outside-scope findings

An unmodified `make check` built all sanitizer binaries, then stopped in
`base_test` because LeakSanitizer reports that it cannot work under ptrace.
Subsequent checks disabled leak detection only; ASan/UBSan remained enabled.
The default check then waited on the shared display lock and was interrupted.
The non-display Makefile selection was exercised with:

```sh
ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=0 make check 'TEST_SAN=$(filter-out $(B)/san/tests/cli_test $(B)/san/tests/editor% $(B)/san/tests/x11% $(B)/san/tests/ref% $(B)/san/tests/gl% $(B)/san/tests/raster_test,$(patsubst tests/%.c,$(B)/san/tests/%,$(TESTS)))'
```

That run stopped on `dataloss_test`'s concurrent-save oracle. Its serial rerun
passed without code changes. A continuing serial runner then executed every
binary in that same selection, recording each exit, rather than stopping at
the first failure. All selected binaries passed on that run except
`file_kill_test`, `ipc_test`, and `journal_kill_test`:

```text
dataloss_test: two_process_save disk=[BAbase] A(stale=0 modified=0) B(err=0 stale=0 modified=0 text=[BAbase])
dataloss_test:537: FAIL a_won || b_won
dataloss_test:603: FAIL two_process_save() == 0
[serial rerun passed with disk=[Abase], B(err=5 modified=1 text=[Bbase])]
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
```

`file_kill_test` is display-dependent despite the Makefile selection: its
`view_init` imported from `bench/file_bench.c` calls `plat_init` and opens a
raster viewport. IPC startup also fails in `editor_p4_test --headless`.
A temporary Unix socket probe inside this worktree returned
`[Errno 1] Operation not permitted` on bind. Connection to
`/tmp/.X11-unix/X99` returned the same error. The raster editor display check
(`editor_test --backend-only` with `EDIT_BACKEND=raster`, `DISPLAY=:99`)
failed at editor_open. Display suites cannot run inside this sandbox because
the X socket is inaccessible; the coordinator must rerun them on Xvfb :99.

`journal_kill_test` reproduced the same failure on a standalone serial rerun.
Its reported return code is JOURNAL_CALLBACK, per the existing enum. This is
an outside-scope replay-oracle failure; it was not fixed. Full `make check`
therefore cannot be reported green in this environment.

The release editor benchmark was run with its existing serial workload and
default fixture count, without adding a budget. Its null row reported
`structural=OK`, `allocations=0` (M)[AC]; the later native row failed editor_open
because of the display restriction. Recorded samples, in nanoseconds, derive
directly from that harness's timestamp differences:

```text
STAMP (M)[AC] BAT0=Not charging load1=2.98 shared box
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=1162114 p99=1962168 ci95_p50=[1160647,1163084] ci95_p99=[1346762,1997382] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=2.98 verdict=TRACK
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[AC]
```

## Scope and remaining work

Full build/runtime transcripts are retained under `build/edit-1um/`. The
coordinator needs to rerun IPC and display-dependent suites with socket access,
repeat the concurrent-save oracle, and assess the journal-kill replay failure.
The notice change and its regression are complete; broader validation is not
fully green. No outside-scope code was changed.

The referenced `build/design/ret-dirty-external-prompt.png` is absent in this
worktree. Placement follows the supplied Sublime baseline and local drive
report; fresh visual comparison needs the coordinator's display. This retains
the existing transient bottom-row status model rather than reserving additional
viewport chrome. No prohibited tracking, handoff or worklog file was edited;
no `bd` or `git` command was run.
