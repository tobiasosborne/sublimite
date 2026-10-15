# edit-f3i (s7) worker report

Bead: edit-f3i, checkpoints after a save (and while one is in flight) and after
startup recovery write the whole text. Decision record:
docs/decisions/edit-f3i.md (mechanism (b): encode against the saved revision's
retained snapshot, indexed by span address).

## Files touched

- src/editor/private.h: `editor_ref_map`/`editor_ref_span`; `editor_buffer`
  gains `save_snapshot`, `saved_snapshot`, `saved_map`; `editor_text_ops` takes
  a `ref` map; `editor_ref_map_build/free`, `editor_saved_reference`; shared
  two-pass record builder `editor_records_*`.
- src/editor/checkpoint.c: reference map build and lookup, `editor_text_ops`
  generalised (kept spans by ORIGINAL offset or by saved offset, partial spans
  split), builder moved to `editor_records_*`, `build_checkpoint` encodes
  against `saved_snapshot` when BASE is the saved generation.
- src/editor/session.c: `session_node.reference` (+ identity) retained at
  publication; finish capture retains the save's snapshot; `encode_leaf` picks
  the reference (`leaf_reference`), per-slot maps in the capture; `prepare`
  uses the two-pass builder (no per-record calloc); save snapshot lifecycle
  (request retains, SAVED moves to `saved_snapshot` and republishes, every
  other end drops).
- src/editor/buffers.c: `origin` also recorded while `e->recovering`; destroy
  releases the snapshots and map before the arena goes.
- src/editor/commands.c: Save As keeps its written snapshot as the reference
  when 28u sets `saved_text`.
- src/editor/recovery.c: replay keeps an op log per buffer since its last
  BASE; install applies it to the opened generation when that is the
  journalled BASE (else the old whole-text replacement).
- tests/editor_checkpoint_test.c: f3i (a), (b) steps 1-3, (c) (in the b2 case);
  `settle_until`, `metadata_bound_with`, record-size macros; optional case-name
  arguments (e.g. `editor_checkpoint_test f3i-b b2`).
- docs/decisions/edit-f3i.md (new); docs/decisions/edit-28u.md (pointer line
  in "Not addressed").

Not touched: src/editor/restore.c, HANDOFF.md, docs/worklog, CLAUDE.md,
PLAN.md, PRD.md.

## Red run (before the implementation; release binary)

`df -h /` before: `/dev/nvme0n1p5 480G 374G 82G 83% /`; after: same.
Stamp: BAT0 "Not charging" (AC), load1 4.57 (the test's own stamp truncates
the status to "Not": pre-existing `%31s` in stamp()).

```
editor_checkpoint_test:463: FAIL after.bases == 1 && after.deletes == 1 && after.deleted == 3 && after.inserted == 4 && (uint64_t)st_after.st_size <= bound
editor_checkpoint_test:774: FAIL got_len == n && !memcmp(got, want, n)
editor_checkpoint_test:780: FAIL inflight_step(root, base, want, got, step) == 0
editor_checkpoint_test: f3i (c) recovered 2 MiB buffer, install checkpoint: journal before=4096 B after=2109440 B bound=4096 B (P) deletes=1 deleted=2097159 inserts=129 inserted=2097160 B
editor_checkpoint_test: FAIL (b2)
editor_checkpoint_test: f3i (a) 1 GiB, save, +1 B checkpoint: journal=1077936128 B bound=4096 B (P) pieces=16385 deletes=1 deleted=1073741825 inserts=65537 inserted=1073741826 B save=1733.256 ms checkpoint=4558.395 ms (M) allocations=2 power=Not load1=4.52
editor_checkpoint_test: FAIL f3i (a) 1 GiB checkpoint after save
editor_checkpoint_test: f3i (b) step 1: journal=4096 B bound=4096 B (P) bases=1 deletes=0 deleted=0 inserts=2 inserted=4 B
editor_checkpoint_test: f3i (b) step 2: journal=2105344 B bound=4096 B (P) bases=1 deletes=1 deleted=2097165 inserts=133 inserted=2097167 B
editor_checkpoint_test: FAIL f3i (b)
```

The text failure at line 774 (step 2) in this first red run was the test, not
the code: the fixture journal names started with `session-`, so discovery
adopted the previous step's journal and its edits landed in the new session. Renamed to `journal-%d`; the
corrected red run of (b) and (c) (BAT0 "Not charging", load1 2.59):

```
editor_checkpoint_test:463: FAIL after.bases == 1 && after.deletes == 1 && after.deleted == 3 && after.inserted == 4 && (uint64_t)st_after.st_size <= bound
editor_checkpoint_test:780: FAIL shape.bases == 1 && !shape.deletes && shape.inserted == inserted && (uint64_t)st.st_size <= bound
editor_checkpoint_test:780: FAIL shape.bases == 1 && !shape.deletes && shape.inserted == inserted && (uint64_t)st.st_size <= bound
editor_checkpoint_test:787: FAIL !failed
editor_checkpoint_test: f3i (c) recovered 2 MiB buffer, install checkpoint: journal before=4096 B after=2109440 B bound=4096 B (P) deletes=1 deleted=2097159 inserts=129 inserted=2097160 B
editor_checkpoint_test: FAIL (b2) / f3i (c)
editor_checkpoint_test: f3i (b) step 1: journal=4096 B bound=4096 B (P) bases=1 deletes=0 deleted=0 inserts=2 inserted=4 B
editor_checkpoint_test: f3i (b) step 2: journal=2105344 B bound=4096 B (P) bases=1 deletes=1 deleted=2097161 inserts=133 inserted=2097163 B
editor_checkpoint_test: f3i (b) step 3: journal=2105344 B bound=4096 B (P) bases=1 deletes=1 deleted=2097161 inserts=134 inserted=2097165 B
editor_checkpoint_test: FAIL f3i (b)
```

## Green run (release binary, all cases)

`df -h /` before and after: `/dev/nvme0n1p5 480G 374G 82G 83% /`.
Stamp: BAT0 "Not charging" (AC), load1 7.56.

```
editor_checkpoint_test: inactive pending COPY recovered=1048590 expected=1048590 clean=yes disk_intact=yes
editor_checkpoint_test: active pending COPY recovered=1048590 expected=1048590 clean=yes disk_intact=yes
editor_checkpoint_test: adopted deletion to empty survives checkpoint/crash/recovery
editor_checkpoint_test: (a) 1 GiB unchanged checkpoint 1: journal=4096 B bound=4096 B (P) time=5.281 ms (M) allocations=2 power=Not load1=7.56
editor_checkpoint_test: (a) 1 GiB unchanged checkpoint 2: journal=4096 B growth=0 B time=4.990 ms (M) allocations=2 power=Not load1=7.56
editor_checkpoint_test: (b) 1 GiB + 10 B checkpoint: journal=4096 B bound=4096 B (P) pieces=16385 deletes=0 inserts=1 inserted=10 B time=5.193 ms (M) allocations=2 power=Not load1=7.56
editor_checkpoint_test: (b) replay onto base: edit at middle, base bytes at head/quarter/after edit/tail
editor_checkpoint_test: (a)+(b) wall=539.603 ms (M) power=Not load1=7.56
editor_checkpoint_test: (b2) 2 MiB edited checkpoint: journal=4096 B deletes=1 deleted=3 inserts=2 inserted=4 B
editor_checkpoint_test: (b2) referencing checkpoint of an edited COPY file recovers the edited text
editor_checkpoint_test: f3i (c) recovered 2 MiB buffer, install checkpoint: journal before=4096 B after=4292 B bound=4292 B (P) deletes=1 deleted=3 inserts=2 inserted=4 B
editor_checkpoint_test: (c) changed BASE refuses the referencing journal; disk text opens, journal set aside
editor_checkpoint_test: (d) legacy whole-text checkpoint journal still recovers
editor_checkpoint_test: (e) saved+unchanged checkpoint: journal=4096 B deletes=0 inserts=0
editor_checkpoint_test: (e) saved revision checkpoints as BASE only and recovers the saved text clean
editor_checkpoint_test: f3i (a) 1 GiB, save, +1 B checkpoint: journal=4096 B bound=4096 B (P) pieces=16385 deletes=0 deleted=0 inserts=1 inserted=1 B save=1505.770 ms checkpoint=6.649 ms (M) allocations=3 power=Not load1=7.56
editor_checkpoint_test: f3i (a) replay onto saved file: xy at middle, head/tail intact
editor_checkpoint_test: f3i (b) step 1: journal=4096 B bound=4096 B (P) bases=1 deletes=0 deleted=0 inserts=2 inserted=4 B
editor_checkpoint_test: f3i (b) step 2: journal=4096 B bound=4096 B (P) bases=1 deletes=0 deleted=0 inserts=1 inserted=2 B
editor_checkpoint_test: f3i (b) step 3: journal=4096 B bound=4096 B (P) bases=1 deletes=0 deleted=0 inserts=2 inserted=4 B
editor_checkpoint_test: f3i (b) save in flight: pre-save BASE while held, saved file after; recovered at each step
editor_checkpoint_test: pending COPY checkpoint/crash/recovery/save, edit-28u referencing checkpoints, edit-f3i post-save checkpoints passed
```

Numbers (M)[AC]: (a) journal 1,077,936,128 B -> 4096 B; checkpoint 4558.395 ms
-> 6.649 ms; allocations 3 (scratch, record array, reference map) vs 2 before
(scratch, record array; the old path built no map). Bounds are (P), derived in
the test and in the decision record (metadata records + 32 B header + payload
per text record, page rounded; (c) adds the one VIEW+TABS+WINDOW metadata
append that follows an install, 96 + 56 + 44 B).

## Test commands and exit codes

- `make -j4 all build/san/tests/{editor_checkpoint,editor_session,dataloss,journal,editor_hot_exit,editor}_test`: 0 (no warnings).
- `build/san/tests/journal_test`: 0. `build/san/tests/dataloss_test`: 0 (first run; no two_process_save flake).
- `build/san/tests/journal_kill_test`: 0. `build/san/tests/journal_group_test`: 0.
- Display lane, each `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh <bin>`, one at a time
  (the X socket was available here, BAT0 "Not charging", load1 1.4-1.8):
  - `build/san/tests/editor_checkpoint_test`: 0 (all cases, incl. the 1 GiB ones; `df -h /` 81G avail before and after).
  - `build/san/tests/editor_session_test`: 0. `build/san/tests/editor_hot_exit_test`: 0 (trials=100).
  - `build/san/tests/editor_test`: 0. `build/san/tests/editor_large_test`: 0.
    `build/san/tests/editor_undo_branch_test`: 0. `build/san/tests/cli_test`: 0.
  - Release `build/tests/editor_test` (malloc guard): 0; every guard line `mallocs=0 guard=active`
    (null/raster/gl-requested 10000 keys, hl-on, g11 typing, clipboard, review 21, P1-1 s6).
  - Release `build/tests/editor_session_test`: 0, including the Ctrl+S request
    allocation guard (the request now retains the save snapshot: an atomic
    refcount, no allocation).
- ASan leak detection was on (default); no leak reports.

## What remains

Nothing inside the bead's acceptance. Limits recorded in the decision doc:
shared-target buffers (another buffer on the saved path) still checkpoint the
whole text after the save; the kept (evicted, save in flight) shape is
unchanged by design; recovery still materialises BASE in memory for the flat
text and the clean check, and the op log holds the journalled insert bytes once
more (bounded by the journal size); external modification of the old
generation's inode through another hard link after a mapped save is not
detected by the reference (trial-irrelevant hardening).

## Out of scope (reported, not fixed)

- editor_session_test leaves `build/editor-session-XXXXXX/.sublimite-base-*`
  (4 B) behind on every run: `close_fixture` removes only the source, journal
  and lock, so `rmdir` fails on a retained base copy. Pre-existing: the same
  leftovers exist in .wt/edit-52o-s7, .wt/edit-n95-s7 and .wt/edit-qct-s7.
  I removed the ones my runs created.
- tests/editor_checkpoint_test.c `stamp()` reads the battery status with
  `%31s`, so "Not charging" prints as "Not" (pre-existing since 28u).
- Out-of-scope items named in the brief (edit-dr9, edit-n95, restore.c idle
  trigger) were not touched.
