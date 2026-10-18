# edit-fi2 (s8) worker report

Bead: recovery must survive an external rewrite of the original file.
Design and numbers: docs/decisions/edit-fi2.md.

## Result

Mechanism (2), asynchronous BASE retention after the first modification,
bounded by BASE <= JOURNAL_STARTUP_SCAN_BYTES (105,000,000 B). The copy is one
WORK_BULK job on the journal pool (the worker creates the temp; the UI only
forms the name, submits, and later polls `work_handle_finished`). Once READY,
the next complete checkpoint (edit-ret refresh gate) names the copy as BASE
and the real file as TARGET, with the 28u/f3i differences. Recovery replays
from the copy: if the file still has the copy's bytes the ops go onto it (f3i
install), else the whole text becomes a modified tab against the changed disk
file with the notice "file changed on disk since this session; its unsaved
edits were recovered". Mechanism (1) is not implemented (reasons and the
>105 MB gap in the decision doc).

## Files touched

- src/journal/journal.h, journal.c: `journal_retain_start`, `journal_retain`,
  JOURNAL_RETAIN_MESSAGE; `retention_name` factored out of save_prepare.
- src/editor/retain.c (new): tick, checkpointed hook, teardown.
- src/editor/private.h: buffer retention fields, `retain_limit`,
  `retain_retry`, prototypes.
- src/editor/checkpoint.c: BASE = copy / TARGET when READY; post-rotate hook.
- src/editor/session.c: nodes carry the copy; worker checkpoints use it.
- src/editor/recovery.c: `same_file`, from-copy install, notice.
- src/editor/buffers.c (tick call, teardown), editor.c (wake message),
  open.c (`retain_limit` default).
- tests/dataloss_test.c: `external_rewrite` (rename-over, in-place),
  `retain_lifecycle`, `--open-gib`, `--retain-bound`; btj child pins
  `retain_limit = 0`; shared_save_as waits for retention before
  `journal_set_io`.
- tests/editor_checkpoint_test.c: (c) child pins `retain_limit = 0`.
- tests/editor_test.c, tests/editor_session_test.c: fixtures remove the
  hot-exit `.sublimite-base-*` before rmdir.
- docs/decisions/edit-fi2.md, this report.

## Red run (before implementation; build/san/tests/dataloss_test)

```
== --rewrite-rename
dataloss_test: external_rewrite kind=rename-over tabs=1 len=2097158 (want 2097161) head=[ABCDEFGH] modified=0 notice=[file changed on disk since this session; its unsaved edits were set aside] aside=[build/dataloss-Zhw9Wv/session-vhojrr.set-aside-1792281342847627545]
dataloss_test:816: FAIL len == FI2_BYTES + 2u
== --rewrite-in-place
dataloss_test: external_rewrite kind=in-place tabs=1 len=2097158 (want 2097161) head=[ABCDEFGH] modified=0 notice=[file changed on disk since this session; its unsaved edits were set aside] aside=[build/dataloss-TfJ6zS/session-C0xgJq.set-aside-1792281353057084273]
dataloss_test:816: FAIL len == FI2_BYTES + 2u
== --retain-lifecycle
dataloss_test: retain_lifecycle: retained after first edit=0
dataloss_test:848: FAIL fi2_retained(root)
```
(The red run's failed fixtures under build/ were removed by hand afterwards.)

## Green run

```
== --rewrite-rename
dataloss_test: external_rewrite kind=rename-over tabs=1 len=2097161 (want 2097161) head=[XYabcdef] modified=1 notice=[file changed on disk since this session; its unsaved edits were recovered] aside=[]
exit=0
== --rewrite-in-place
dataloss_test: external_rewrite kind=in-place tabs=1 len=2097161 (want 2097161) head=[XYabcdef] modified=1 notice=[file changed on disk since this session; its unsaved edits were recovered] aside=[]
exit=0
== --retain-lifecycle
dataloss_test: retain_lifecycle: retained after first edit=1
dataloss_test: retain_lifecycle: retained after save=0
exit=0
```
(c) a clean open (cursor moves, maintenance turns) writes no artifact; the
copy appears after the first edit and is gone after Ctrl+S and after close.

## (d) 1 GiB sparse open, before/after (M), provisional under load

Fixture 2^30 B (P), sparse (ftruncate), in build/dataloss-*, removed on every
exit path (wrapper). `df -h /`: before 67G avail (390G used), after 67G;
later runs 64G -> 64G (the drop between is build output, no fixture left).

| build | editor_open ms | settle ms | first edit ms | copy | stamp |
|---|---|---|---|---|---|
| san, before | 44.2 / 69.4 / 43.7 | 0.38 / 0.54 / 3.38 | 0.93 / 2.25 / 1.36 | 0 | [bat] "Discharging", load1 14.1 |
| san, after | 88.3 / 87.2 / 66.4, then 47.7 / 58.9 / 82.6 / 80.2 / 81.9 / 67.3 | 0.37-1.25 | 0.79-2.67 | 0 | [AC] "Not charging", load1 9.3-9.9 |
| release, after | 52.8 / 41.5 / 48.6 | 0.16-0.19 | 0.45-0.47 | 0 | [AC] "Not charging", load1 9.9 |

The two san series overlap (44-88 ms) and were not taken back to back (no
"before" binary was kept), so this is no regression proof, only no visible
change. For a 1 GiB BASE nothing new runs on open: retention starts only
after a modification and never above the bound; the per-maintenance tick is
a loop over buffer slots.

## Retention numbers (M)

- Standalone 64 KiB copy + fsync (scratchpad copybench.c, mirrors
  retain_copy), [AC] "Not charging", load1 25.4: 256 MiB 723-924 ms
  (290-371 MB/s); 105,000,000 B 268-425 ms (247-392 MB/s).
- In editor, release, sparse 105,000,000 B BASE, [AC] "Not charging", load1
  9.9: first edit -> copy READY 104.4 / 162.1 / 127.4 ms; longest UI turn
  (includes the reference checkpoint: flush + rotate + fsync) 7.9 / 8.5 /
  21.2 ms.

## Verification (exit codes)

- `make -j3 all` (gcc, -Werror): 0 (run twice, last after all edits).
- build/san/tests/dataloss_test (ASAN_OPTIONS=detect_leaks=1): 0. One earlier
  full run failed two_process_save(false) with B err=5 (the known edit-1wc
  race); the next two full runs passed it.
- build/san/tests/editor_checkpoint_test: 0
- build/san/tests/journal_test: 0
- build/san/tests/journal_retire_test: 0
- editor_session_test on :99 under the display lock
  (EDIT_DISPLAY_LOCK_TIMEOUT=1800, leaks on): 0
- editor_test san on :99 under the lock: 0
- editor_test release (malloc guard) on :99: 0, typing-path mallocs=0
  ("edit-g11 null typing ... mallocs=0", "review 21 ... mutation-to-submit=0").
- Extra: journal_kill_test 0, journal_group_test 0, editor_hot_exit_test
  (:99) 0.
- No fixture directories left under build/ after the final runs.

## Deviation to flag

Constraint (a) says nothing on the UI thread waits for I/O. The copy honours
that; referencing the copy does not quite: it is one complete checkpoint
(rotate with fsync) on the UI thread under the edit-ret refresh gate,
7.9-21.2 ms measured, the same mechanism as the checkpoint open/reload/Keep
already run. An append-only alternative (BASE(copy)+TARGET+differences as
one journal group, no rotation) is named in the decision doc as the
follow-up if it is ever felt.

## Remaining

- BASEs above 105,000,000 B: no protection (mechanism (1) not implemented).
- Edits before the copy is READY + referenced (up to 162 ms measured at the
  bound) still depend on the original.
- Failed/cancelled save: its cancel checkpoint drops the copy reference for
  the saved buffer until the next checkpoint (state not reset).
- Crash mid-copy leaves a partial unreferenced temp; existing retirement
  removes unreferenced copies only above its byte cap.

## Out of scope (found, not fixed)

- Hot exit now leaves `.sublimite-base-*` beside the journal by design;
  any other test or tool that rmdirs a journal directory after an unclean or
  hot exit and asserts success will need the same cleanup as o20/ck4/session
  fixtures (I only ran the suites listed above plus three extra).
- The dataloss --probe "changed_base deleted" finding (file deleted
  externally) is unchanged by this bead.
