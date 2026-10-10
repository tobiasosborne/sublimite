# edit-9lf — session 9 worker report

Scope: P4-3 sections 2, 11, 12, 13, 14, 16, 22, 27, 41, 40, worked in that order. This bead is **incomplete**; do not close it. Git was read-only; no bd, HANDOFF.md or docs/worklog edits. No production globals or typing-path heap allocation were added. No large.c/open.c or other workers' save/journal hunks were edited.

Design choices are in [edit-9lf](../decisions/edit-9lf.md). Regression fixtures and logs are under build/. Measurements are (M)[AC]; BAT0 reported Not charging. The box is shared; no timing gate verdict is claimed.

## P4-3 section 2

Done: shared replay guard on public activation/navigation and committed snapshot reads. The journal/frame boundary and every public tab/open/close entry are asserted.

Red, written/run before the production fix:

```text
editor_test:705: FAIL editor_length(e) == 4096
review 11: first undo turn longest_slice_ns=29662 (M)[AC], slice limit=500000 ns (G)
```

Green after the fix:

```text
review 11: first undo turn longest_slice_ns=24038 (M)[AC], slice limit=500000 ns (G)
review 11: multi-record burst undo/redo yields across turns passed
```

## P4-3 section 11

Done: decoded file errors stay with their owner and generation. Regression holds deferred COPY acquisition and injects ENOSPC into an inactive save; unrelated typing succeeds.

Red, written/run before the production fix:

```text
editor_test:805: FAIL editor_step(e, 10) >= 0
```

Green after the fix:

```text
P4-3 11: deferred COPY and inactive ENOSPC errors preserve unrelated editing passed
```

Additional owner-local adoption regression, also red before its fix:

```text
editor_test:1011: FAIL editor_adopt_open(e, owner) == 0
```

Green (release, followed by ASan/UBSan):

```text
P4-3 11: failed full-source identity adoption stays owner-local passed
P4-3 11: failed full-source identity adoption stays owner-local passed
```

## P4-3 section 12

Done: controls bypass parked COPY edits. Parked edits retain ID/source generation and cannot be redirected to the newly selected tab.

Red, written/run before the production fix:

```text
editor_test:829: FAIL tabs_active_index(&e->tabs) == 0 && !e->focused
```

Green after the fix:

```text
P4-3 12: pending COPY admits controls and retains edit owner passed
```

## P4-3 section 13

Done: stale-source suspension preserves tab shortcuts and tab-strip pointer controls; source-dependent text stays frozen.

Red, written/run before the production fix:

```text
editor_test:820: FAIL tabs_active_index(&e->tabs) == 0
```

Green after the fix:

```text
P4-3 13: suspended source preserves tab keys and tab pointer controls passed
```

## P4-3 section 14

Done: recovery keys require a submitted prompt generation and fresh ingress/time, with no modifier or repeat. Explicit choices also retain owner/generation.

Red, written/run before the production fix:

```text
editor_test:820: FAIL e->source_choice == 0
```

Green after the fix:

```text
P4-3 14: pre-prompt, modified, repeated and aged recovery keys ignored passed
```

The same finding also covers returning to a previously prompted stale tab.
Activation now invalidates its old admission time, so text queued while another
tab was visible cannot resolve it. This was corrected after the implementation
cutoff within the existing section 14 scope. Additional red and release/sanitizer
green:

```text
editor_test:975: FAIL e->source_choice == 0
P4-3 14: pre-prompt, modified, repeated and aged recovery keys ignored passed
P4-3 14: pre-prompt, modified, repeated and aged recovery keys ignored passed
```

## P4-3 section 16

Partial: logical tab ID/order/MRU, clamped view and wait membership survive explicit reload. Real launcher primitive remains blocked until closure. Controller revision checks/silent clean reload and journal reconciliation remain missing; journalled reload stays suspended. Whole-source replacement preparation is synchronous.

Red, written/run before the production fix:

```text
editor_test:834: FAIL tabs_count(&e->tabs) == 2 && tabs_active_index(&e->tabs) == 1 && e->buffer->id == id
```

Green after the fix:

```text
P4-3 16: reload preserves logical tab, view and real launcher wait passed
```

## P4-3 section 22

Partial: evicted buffers no longer synchronously wait in UI maintenance. Known worker leases/index cancel and poll; file buffers remain leased until shutdown. Missing: public file logical-retirement/physical-ready API and prompt physical reclamation. Retention can exhaust the fixed buffer reservation; new admission then refuses before preparation/publication.

Red, written/run before the production fix:

```text
editor_test:836: FAIL !atomic_load(&hold.expired)
```

Green after the fix:

```text
P4-3 22: evicted buffer retains held save lease during unrelated typing passed
```

## P4-3 section 27

Remaining: opt-in regression is red. dc4015e `undo_get_history` contains groups/serial/evictions but no retained record count or memory accounting. The caller needs that bounded API; scope permits undo API use only. No production dependency/private-state workaround was made. `EDITOR_REVIEW_ONLY=p4-27` is intentionally excluded from the default green suite.

Red, written/run before the production fix:

```text
editor_test:853: FAIL bounded
```

Green: missing; no fix made.

## P4-3 section 41

Done: tab capture owns a stable ID, resolves current order before reordering, and cancels on activation/closure/focus/resize. Minimap capture also owns source generation. Both middle/last closure schedules are checked.

Red, written/run before the production fix:

```text
editor_test:831: FAIL tabs_at(&e->tabs, 0)->id == first && tabs_at(&e->tabs, 1)->id == second
```

Green after the fix:

```text
P4-3 41: closed drag owner and switched minimap capture cancel safely passed
```

## P4-3 section 40

Done: compose final tab chrome in existing aligned scratch, apply stale marker before comparing, and damage only changed final cells. Cursor/text-only frames assert no unchanged tab-row damage.

Red, written/run before the production fix:

```text
editor_test:830: FAIL !probe.damaged
```

Green after the fix:

```text
P4-3 40: cursor/text frames omit unchanged final tab chrome passed
```

## Validation and limitations

Final-source GCC 13 release `make all` exited zero (M)[AC] with `-Wall -Wextra -Werror -Wshadow -Wconversion`. Clang is version 18; the final owner-local adoption regression also passes under ASan/UBSan. The first sanitizer build found a signed enum conversion in the new launcher test; it was corrected using `ipc_result`. Two full `make check` runs were started with `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0`; the display lane is shared. Their final outcome is recorded below, and no full-suite success is assumed. LeakSanitizer is disabled because it cannot run in this sandbox; the coordinator must rerun with leaks enabled.

Final-source editor fuzz after the return-to-tab correction: 26294 runs in 61 seconds (M)[AC], BAT0 Not charging, load1 1.22 (M)[AC]. Tabs fuzz: 131275 runs in 61 seconds (M)[AC], each clean against the required 60 seconds (G). Earlier independent editor/tabs campaigns also passed. Leak detection was disabled.

The initial reload test reached preserved identity/view and blocked launcher assertions, then sandbox socket send returned EPERM. Re-running with authorized local IPC access passed. No IPC implementation was changed.

The replay test checks public reads, tab/open/close/reopen/cycle/cursor/jump refusal, and unchanged submitted sequence/journal record count between slices. The ENOSPC seam is test-only syscall interposition controlled by a fixture-directory marker, with no global injection state.

Out-of-scope observation: large-file public save/find/navigation entry points in large.c retain their existing replay admission behavior; that file is assigned to another worker and was not changed. Save/journal controller integration belongs to edit-ck4. Required remaining work is bounded undo record accounting for section 27, complete file retirement/reclamation for section 22, and controller/journal/silent reload integration for section 16. The launcher regression uses real pair/adopt/wait/closed primitives and a child process; it does not launch the CLI executable.

Paired hot-path diagnostic: build-only null-backend benchmark variants restore
only unconditional chrome versus final comparison; production bench code is
unchanged. Same-second, back-to-back runs on log_1g.txt, BAT0 Not charging and
load1 2.97 (M)[AC]. Baseline injection-to-T4 p50/p99 130385/167413 ns (M)[AC];
final chrome 134162/144132 ns (M)[AC], each 128 samples (M)[AC], below 10000
required (G), TRACK. Both completed all samples with zero allocations (M)[AC]
and structural OK. No single timing, native upload result, or G1 gate verdict
is inferred. Exact rows and rationale are in the decision record.

The first full release editor run failed its existing source-watch detection
deadline at `editor_test:748: FAIL editor_get_stats(e).source_stale`. Its isolated
`EDITOR_REVIEW_ONLY=16` retry passed (M)[AC]. A full release retry was scheduled; its final result is recorded below.
The cause of the one failed watch deadline is uninvestigated. No file watch
implementation or timeout was changed to hide this observation. The release null typing allocation test reports:

```text
editor_test: null 10000 keys mallocs=0 guard=active
```

All implemented focused sanitizer regressions passed (M)[AC]; their output is
in build/s9-focused-san.log. The additional adoption regression passes in both
release and sanitizer configurations. `git diff --check` is clean.

Final release editor retry: exit zero (M)[AC], all default editor regressions
and native integration passed. The initial source-watch deadline failure above
is retained; no cause is claimed. The active guard output includes:

```text
editor_test: requested=gl actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: requested=raster actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: all passed
```

## Final acceptance state

The bead remains incomplete: sections 16/22 are partial and section 27 has no
fix. All implemented focused regressions are green in release/ASan/UBSan,
including the final return-to-tab prompt schedule. Final GCC 13 `make all`
exits zero with strict warnings. Editor and tabs fuzz each ran clean beyond
60 seconds (G), with the final editor campaign recorded above. The full release
editor retry passes with active zero-allocation guards. No timing gate or full
retired-file memory gate is certified.

Full sanitizer completion remains an acceptance gap while the shared :99
lock is unavailable. The initial run reached x11_display_guard_test; the
final-source rerun reached editor_large_test, both waiting for that lock.
At the wall-clock budget, only this worktree's outstanding make-check processes
and descendants were terminated. Neither full run exited zero; full sanitizer
acceptance is NOT met. No test assertion was loosened to obtain a green result.
The coordinator must complete full `make check`, then repeat with leaks enabled.
No external worker or display owner was interrupted.

Final build confirmation (exit zero, M)[AC]:

```text
make: Nothing to be done for 'all'.
```

Required red/green evidence, design decisions and remaining work are complete in
this report. This worker is stopped; the coordinator owns completion/commit.
