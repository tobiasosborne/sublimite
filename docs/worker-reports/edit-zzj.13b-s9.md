# edit-zzj.13b slice 3 / session 9 worker report

Completed findings: 13, 9, 16, 21. Partial findings: 8 and 11. This bead must
remain open for the missing portions listed below. No new globals, no git
writes, no bd, and no HANDOFF/worklog edits. All native test runs use DISPLAY=:99
and EDIT_DISPLAY=:99. Xvfb/socket test commands use the authorized execution
outside the sandbox because its Xvfb socket is hidden inside the sandbox.

The requested regressions for prefix open and burst grouping were present as
opt-in tests. Both were run RED before implementation, then promoted to the
default suite. New bounded-accounting, stale-source, replay-continuation and
native-attribution regressions were written and run RED before their fixes.

Measurements are (M)[AC]; BAT0 reports Not charging and the user confirms AC.
The shared box stays loaded; no quiet-box gate verdict is claimed. The paired
replay runs below were executed back to back in the same minute. Release uses
gcc 13.3.0 and sanitizers use clang 18.1.3. Build flags retain C11, -Wall,
-Wextra, -Werror, -Wshadow and -Wconversion.

## Finding 8: Copy prefix publication and adoption

Status: partial: copy path fixed; mapped acquisition still waits for OPEN_READY.

RED, before its implementation change:

```text
editor_test:660: FAIL first_before_remainder
```

GREEN:

```text
review 8: first viewport precedes stalled remainder work passed
```

## Finding 13: Automatic burst grouping

Status: done.

RED, before its implementation change:

```text
editor_test:619: FAIL editor_length(e) == 0
```

GREEN:

```text
review 13: typing/repeat bursts, movement/focus/timeout boundaries passed
```

## Finding 9: Bounded undo history accounting

Status: done.

RED, before its implementation change:

```text
undo_test:550: FAIL WIFEXITED(status) && WEXITSTATUS(status)==0
```

GREEN:

```text
review editor 9: bounded group query, bursts, replay, eviction and clear passed
```

## Finding 16: Mapped source detection and suspension

Status: done, with the minimal reload/keep behavior described below.

RED, before its implementation change:

```text
editor_test:653: FAIL editor_get_stats(e).source_stale
```

GREEN:

```text
review 16: mapped watch overwrite, focus truncate, edit suspension and reload/keep passed
```

## Finding 11: Resumable replay

Status: partial: multi-record replay fixed; other indivisible work remains.

RED, before its implementation change:

```text
editor_test:701: FAIL undo_replay_snapshot(e->undo) != NULL
review 11: first undo turn longest_slice_ns=6237393 (M)[AC], slice limit=500000 ns (G)
```

GREEN:

```text
review 11: first undo turn longest_slice_ns=37211 (M)[AC], slice limit=500000 ns (G)
review 11: multi-record burst undo/redo yields across turns passed
```

## Finding 21: Native allocation attribution

Status: done for the native core-key translation path.

RED, before its implementation change:

```text
editor_test:659: FAIL !edit_malloc_guard_active() || calibrated == 1
```

GREEN:

```text
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=active (M)[AC]
```
## Implementation and limits

8: Copy files exceeding FILE_PREFIX_MAX now reserve a separate preview tree
and a stable full-document tree. Prefix bytes are copied only to the bounded
preview. Tabs and undo retain the full tree's stable identity. The active view
binds the preview and keys stay queued until adoption. After the first frame
settles and the full file completion is decoded, the UI attaches the full
storage, validates identity/BASE, starts snapshot indexing/warming, rebinds the
view and releases the preview. No full copy or whole-source newline scan is
introduced on the UI. The default stalled-bulk test also checks queued typing,
full-source adoption and the resulting bytes. `open_pending` exposes preview
state separately from frame `pending`.

Missing for 8: mapped files retain the landed full-mapping acquisition
contract and still wait for FILE_MSG_OPEN_READY. The mapped newline scan was
already absent in main. Extending prefix ownership to mapping acquisition
requires reconciling the existing large-file startup contract (immediate deep
byte jump, prepared warm snapshot and full tree identity). This report does
not claim a complete finding-8 fix. Nontrivial IPC positions in a pending copy
return CAPACITY before tab publication; deferred positional adoption remains.
No whole-open warm/cold corpus gate verdict is supplied.

13: Ordinary adjacent same-kind insertions/backspace/delete use automatic
undo groups and event timestamps. Selection replacements, Enter and brace
indentation keep explicit atomic groups. Movement, focus, tab operations,
timeouts and the end of a repeat sequence establish boundaries. The editor
ring stores group deltas and repaired before/after selection blobs. This
preserves selection repair without importing undo-private records. The default
regression covers typing, repeated backspace, undo/redo, movement, focus and
(timeout) separation. The fuzzer's one-edit history model now supplies event
timestamps beyond the burst timeout; the default editor regression exercises
same-time bursts. Native dequeue/trace timestamps retain their existing meaning.

9: `undo_get_history` reads only counters in the log control block, returning
applied/redo groups, oldest-group eviction count and a new-group serial. Counts
change at edit/group/replay boundaries; provisional replay retains the previous
boundary counts. The editor merges a burst by serial and drops evicted ring
entries arithmetically in constant time. The test makes the complete record
mapping inaccessible in a child, then queries counts. It also compares the new
query with the existing statistics through burst/replay/eviction/clear cases.
The two-line undo benchmark adapter renames the new exported symbol alongside
its existing mock exports; otherwise make all reports a duplicate definition.

16: Per-file inotify descriptors are registered with the editor's existing
poll descriptor. Notifications schedule file-layer bulk checks, completions
route through file_msg_decode, focus checks identity, and setup/adoption checks
before bulk indexing. Sticky file/SIGBUS change state suspends source use,
cancels index/warm/find jobs and suppresses stale result adoption. The cached
grid displays `Source changed: R reload / K keep`. R opens the current generation
and closes/retains the old tab; K delegates to file_resolve_keep. Unsafe mapped
keep is refused and leaves the prompt. Choices run during maintenance, outside
the typing guard. A source change during partial replay aborts via undo_clear
and discards that log, preserving the pre-replay tree; the old generation stays
suspended. There is no attempt to reconstruct overwritten mapped original bytes.
The test covers watch-driven overwrite, focus/truncate, edit rejection and
reload/unsafe-keep. Deterministic live indexing/change interleaving and complete
modified-buffer keep/reload policy remain unverified acceptance details.

11: Undo/redo invokes the existing slice API with at most 8 operations (G) and
an absolute 0.1 ms deadline (G) per invocation. The editor retains the logical
group delta across turns, checks input between slices, and journals/publishes
only at completion. Blink/resize/submit cannot consume the provisional tree.
The regression uses a multi-record typing group and proves that the group is
still checkpointed after the first turn, then verifies full undo and redo.
The pasted paired measurements show longest_slice_ns before/after; a single
loaded-box timing is not a hard gate verdict.

Missing for 11: resumable deletion/newline counting, large individual piece
operations, replay journal staging/counting, resize initialization and submit
validation. The frozen piece calls themselves cannot yield within an individual
mutation. The 0.5 ms maximum (G) is not certified. Existing full-segment slice
accounting is retained.

21: A new default native allocation test dequeues actual XCB key events,
starts a separate allocator guard before calling the same x11_input_key used by
native dispatch, then injects the translated event through the editor guard.
An intentional allocation probe before translation calibrates that attribution:
the old ingress-only placement misses it (RED), the new placement counts it
(GREEN). Release prints separate native and mutation-to-submit counts. XCB
packet allocation stays outside the translation guard under the existing
library exemption. ASan's counting interposer is explicitly inert. This does
not introduce a new production X11 dispatch hook or cover compose/keymap rebuild
and XI2 allocation paths; those are not claimed by this core-key regression.

## Decisions and merge notes

Design choices are in docs/decisions/edit-zzj.13b.md. Editor.c edits are small
state/dispatch guards; input.c edits concentrate on group accounting/replay.
Paint.c gains only an early stale-source status branch, to limit overlap with
the estimated-gutter worker. No out-of-scope module repair was made. The
fuzz model timestamp adjustment and undo benchmark symbol adapter are necessary
companions to the scoped behavior/API changes.

## Final verification

Current final source checks:

- `DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all`: exit 0 (M)[AC].
- Final release `build/tests/editor_test`: exit 0 (M)[AC], active allocation
  guards on null/raster and selected backends, including native attribution.
- `git diff --check`: exit 0 (M)[AC].
- Final editor fuzz: exit 0 (M)[AC], 61 seconds and 10015 executions (M)[AC],
  against 60 seconds (G), ASAN_OPTIONS=detect_leaks=0.

Pasted final release/fuzz summaries:

```text
review 8: first viewport precedes stalled remainder work passed
review 13: typing/repeat bursts, movement/focus/timeout boundaries passed
review 16: mapped watch overwrite, focus truncate, edit suspension and reload/keep passed
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=active (M)[AC]
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: all passed
#10015 DONE cov: 12747 ft: 35169 corp: 368/2645b lim: 14 exec/s: 164 rss: 369Mb
Done 10015 runs in 61 second(s)
```

A full ASan/UBSan check earlier in the session exited zero (M)[AC]. The
final-source single full run exits 2 (M)[AC]: all editor and native raster suites
pass, then the unchanged refwin_test returns 1 (M)[AC] during preflight before
its paired run. The immediate standalone retry exits 0 (M)[AC]. No refwin or
raster source was changed. This is an unresolved final full-check acceptance
failure, even though the failing binary passes when isolated; the coordinator
must rerun the complete suite. Do not close the bead.

Pasted final full-run tail and isolated retry:

```text
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
== build/san/tests/refwin_test
keyinject: --pairs must be nonzero
refwin_test: measured period arithmetic and 90/60 Hz pair rejection PASS
refwin_test: exact NotifyMSC X error, event timeout and zero-clock diagnostics PASS
make: *** [Makefile:101: check] Error 1
refwin_test: PASS 12 editor/reference pairs, complete monotonic CSV, incomplete/wrong-frame runs rejected (Xvfb phase approximate)
```

The native raster suite was slow in the full run but completed successfully.
A separate unchanged raster retry reached its 90 second timeout (M)[AC], exit
124 (M)[AC]; this was a diagnostic run, not a replacement for its full-suite
pass. Earlier overlapping checks hit an IPC readiness deadline; duplicate
runs were stopped and only this worktree's test processes were terminated.
The stale-source fixture owns a private directory so unrelated build/log
events do not flood its watch queue. Remaining suites after refwin and the
replay CLI were then run separately and all exited zero (M)[AC], including
the updated undo sanitizer suite. This completes test-binary coverage, but
still does not supply a final single make check exit zero.

```text
review editor 9: bounded group query, bursts, replay, eviction and clear passed
undo_test: P1.5e all passed
x11_clip_test: ok
x11_identity_test: WM_CLASS, UTF8_STRING _NET_WM_NAME and WM_NAME readback passed on :99
x11_input_test: ok
x11_live_test: ok
x11_order_test: all ok
x11_xi2_test: ok
test_replay_cli: all passed
```

LeakSanitizer is disabled with ASAN_OPTIONS=detect_leaks=0 because it cannot run
inside this sandbox; the coordinator must rerun with leaks enabled. All native
windows are confined to :99. The undo benchmark was run once on the loaded box because its bookkeeping
hot path changed. Its real-piece and mock-piece rows run back to back; there is
no comparison against a separately sampled quiet baseline and no timing gate
certification. Exit 0 (M)[AC], load1 6.46 (M)[AC]:

```text
undo_10k_in_tree_TRACK n=31 p50=20936191 ns p99=38519449 ns (M)[AC]
undo_10k_bookkeeping_mock_G n=31 p50=765929 ns p99=1979554 ns (M)[AC]
```

The benchmark's reference limits are 58/78 ms for real piece work (G) and
6.3/6.3 ms for bookkeeping (G). No other module bench/gate verdict is claimed.
