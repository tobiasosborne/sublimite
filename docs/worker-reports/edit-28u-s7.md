# edit-28u (s7) worker report

Bead: editor_checkpoint rewrote every buffer's whole text on every checkpoint
(1 GiB open file => 1 GiB journal per checkpoint). Design and derivations:
docs/decisions/edit-28u.md.

## Files touched

- src/piece/piece.h, src/piece/piece.c: additive amendment
  `piece_iter_next_source` (+ `PIECE_ADD`): span's ORIGINAL offset or add.
- src/editor/private.h: `editor_base_id`; `editor_buffer.origin`,
  `saved_text`, `saved_text_revision`; encoder declarations.
- src/editor/checkpoint.c: `editor_text_ops` (diff-from-BASE encoder),
  `editor_checkpoint_references`, base-id helpers; `editor_checkpoint`
  rewritten as two passes into one record array + one arena.
- src/editor/buffers.c: bind `origin` at prepare (same generation as BASE).
- src/editor/session.c: save-worker checkpoint uses the shared encoder;
  finish of the saved revision writes BASE only; SAVED binds `saved_text`.
- src/editor/commands.c: Save As binds `saved_text` when the captured BASE is
  the generation the write reported.
- src/editor/recovery.c: gap-buffer replay (linear for checkpoint op order).
- tests/editor_checkpoint_test.c: cases (a) (b) (b2) (c) (d) (e); all cases
  now run and report individually; private XDG root; fixtures removed on
  every path (`remove_tree`).
- tests/editor_session_test.c: fm4 large-orphan fixture unbinds the original
  so its child still writes the pre-28u 1 GiB whole-text orphan; new
  `clean_tab_set` relaunch test.
- src/journal/retire.c, tests/journal_retire_test.c: clean tab sets kept
  (follow-up section).
- docs/decisions/edit-28u.md (new), this report.

## Red run (pre-change sources, same test file)

Built in a scratch copy of this worktree with main's (c98734c) versions of the
eight touched src files. BAT0 `Full`, load 4.94 4.03 3.87 (before) / 4.70 (after).

```
editor_checkpoint_test: (a) journal 1086427136 B > 4096 B
editor_checkpoint_test:429: FAIL shape.deletes == 1 && shape.deleted == 3 && shape.inserted == 4
editor_checkpoint_test:465: FAIL shape.deletes == 1 && shape.inserted == 4
editor_checkpoint_test: (a) 1 GiB unchanged checkpoint 1: journal=1086427136 B bound=4096 B (P) time=4208.164 ms (M) allocations=262151 power=Full load1=4.94
editor_checkpoint_test: FAIL (a)/(b) 1 GiB checkpoints
editor_checkpoint_test: (b2) 2 MiB edited checkpoint: journal=2125824 B deletes=1 deleted=2097159 inserts=513 inserted=2097160 B
editor_checkpoint_test: FAIL (b2)
editor_checkpoint_test: FAIL (c)
editor_checkpoint_test: (d) legacy whole-text checkpoint journal still recovers
editor_checkpoint_test: 3 case(s) failed
exit=1
```
(b) is gated on (a) and did not run; (d) passes on old code as expected (the
wire format is unchanged). Second red run with (e) added (BAT0 Full, load1
5.62): `(e) saved+unchanged checkpoint: journal=2125824 B deletes=1
inserts=513` FAIL; (a) again 1,086,427,136 B, 5179 ms, 262,151 allocations.

## Green run

BAT0 `Full`, load 3.77 3.38 3.72 (before) / 3.71 (after), release:
```
(a) 1 GiB unchanged checkpoint 1: journal=4096 B bound=4096 B (P) time=11.216 ms (M) allocations=2 power=Full load1=3.71
(a) 1 GiB unchanged checkpoint 2: journal=4096 B growth=0 B time=8.216 ms (M) allocations=2 power=Full load1=3.71
(b) 1 GiB + 10 B checkpoint: journal=4096 B bound=4096 B (P) pieces=16385 deletes=0 inserts=1 inserted=10 B time=11.231 ms (M) allocations=2 power=Full load1=3.71
(b) replay onto base: edit at middle, base bytes at head/quarter/after edit/tail
(a)+(b) wall=479.199 ms (M) power=Full load1=3.71
(b2) 2 MiB edited checkpoint: journal=4096 B deletes=1 deleted=3 inserts=2 inserted=4 B
(c) changed BASE refuses the referencing journal; disk text opens, journal set aside
(d) legacy whole-text checkpoint journal still recovers
(e) saved+unchanged checkpoint: journal=4096 B deletes=0 inserts=0
exit=0
```
Bound (P): the test derives it from record sizes (BASE v2 32+40+path+1+8,
TARGET 32+path+1, VIEW 32+64, TABS 32+16+8, WINDOW 32+12, PAD header 32,
rounded up to JOURNAL_PAGE) with the canonical path; for this fixture one
page, 4096 B. Growth between unchanged checkpoints: 0 B (rotation replaces
the file). Time: 4208 ms -> 8 to 11 ms per checkpoint (M) [AC Full]; tracked,
not asserted. Allocation counts (release malloc guard, all threads):
262,151 -> 2. Under ASan the guard is compiled out (prints `uncounted`).
(b) pieces: 16,384 64 KiB original chunks + the typed add piece (the original
chunk at the edit point is not split: the edit lands on a chunk boundary);
the checkpoint writes 1 INSERT (10 bytes) and no DELETE because the original
halves stay contiguous.

Loop check: 25 consecutive release runs clean after the index wait (below);
3 consecutive ASan runs clean.

## Test commands (all from the worktree; ASAN_OPTIONS=detect_leaks=1 for san)

| command | exit |
|---|---|
| make -j4 all | 0 |
| build/san/tests/editor_checkpoint_test | 0 (x3) |
| build/tests/editor_checkpoint_test | 0 (x26) |
| build/san/tests/journal_test | 0 |
| build/san/tests/journal_group_test | 0 |
| build/san/tests/journal_kill_test | 0 |
| build/san/tests/journal_retire_test | 0 |
| build/san/tests/dataloss_test | 0 |
| build/san/tests/editor_hot_exit_test | 0 |
| build/san/tests/editor_session_test | 0 (large_orphans launch_ms=30.567 (M) power=Full load1=6.22) |
| build/san/tests/piece_test, piece_bounds_test, piece_mt_test | 0, 0, 0 |
| DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test | 0 |
| DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_large_test | 0 |
| DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_close_test | 0 |

Full `make check` was not run (shared box); fuzzers not rebuilt (journal
module unchanged; piece gained one read-only accessor).

## What remains

- After a save, further edits of a large file checkpoint the whole text again
  (ORIGINAL is the pre-save generation). Needs a rebase-after-save or a
  retained-generation reference; separate bead.
- Recovery installs with copy_threshold = UINT64_MAX and materialises BASE
  (edit-qct); recovered buffers checkpoint whole text afterwards.
- `session.c` save checkpoints still calloc per record (count now O(pieces)).

## Follow-up: retirement keeps clean tab sets (coordinator decision)

Change: `src/journal/retire.c` record(): a BASE in a discoverable session
journal (`session-*`, no dot) makes it not clean. Dotted set-aside/.retired
journals keep the edit-rfq rule. Tests: `tests/editor_session_test.c`
`clean_tab_set` (launch 1: open, type, Ctrl+S, checkpoint, clean quit;
launches 2 and 3: start empty), `tests/journal_retire_test.c` `clean_tab_set`
(BASE-only session kept, BASE-free session retired). Retire-test fixtures that
encoded the old rule were moved: measurement's 32 empty sessions now hold no
BASE (still one sealed page each, journal_bytes=4096); save_metadata and
targets use `.retired` names (not discovered, not capped) so they still check
FINISHED-is-not-an-edit and TARGET identity.

Red (retire.c BASE line removed, rest of today's tree; BAT0 Full, load1 6.36):
```
journal_retire_test:120 FAIL access(tabset,F_OK)==0                      exit=1
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 1 session journals=1 [.session-0q0DF2.lock session-0q0DF2 ]
clean_tab_set: launch tabs=0 title= clean=no
editor_session_test:848: FAIL tabs == 1 && !strcmp(title, "source") && clean   exit=1
```
Green (BAT0 Full, load1 6.20 -> 3.95):
```
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 1 session journals=1 [.session-pNblsg.lock session-pNblsg ]
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 2 session journals=1 [.session-Db6uCv.lock session-Db6uCv ]
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 3 session journals=1 [session-cMe5ls .session-cMe5ls.lock ]
(M)[AC] BAT0=Full empty_fixture_count=32 journal_bytes=4096 base_bytes=4 discovery_before_ns=516931 discovery_after_ns=37365 load1=5.46
journal_retire_test: passed
```
Journal count: exactly 1 `session-*` journal after every launch (the last
launch's own; the adopted one is unlinked after the recovered checkpoint is
durable) plus its stable lease file; nothing else.

Follow-up commands (ASan, detect_leaks=1): make -j4 all 0;
journal_retire_test 0; dataloss_test 0; editor_session_test 0;
editor_checkpoint_test 0; editor_hot_exit_test (DISPLAY=:99 via
with_display_lock.sh) 0.

## Found, not fixed

- **Pre-existing intermittent `EDITOR_ERR_ARG` from `editor_step` when
  editing during a background line-index build.** Typing right after
  `editor_set_cursor` on a just-adopted 2 MiB COPY file returned -100 from
  `editor_step` in about 1 of 20 child runs; reproduced on the pre-change code
  3/40 (case (c) child looped on the red build). Separately, typing into a
  just-adopted 1 GiB mapped file can keep `pending` past EDITOR_IDLE_NS while
  the index builds. The tests now wait for `editor_index_complete` before
  editing (25/25 clean). Root cause not investigated; it may affect real
  typing right after opening a file (wrong-input/lost-keystroke risk).
- `editor_session_test` large_orphans still writes 1 GiB + a 1 GiB copy per
  run (unchanged fixture size; now produced deliberately).
- `editor_session_test` leaves a `build/editor-session-XXXXXX/` directory
  holding a 4-byte `.sublimite-base-*` retained original per run (pre-existing:
  the main checkout's build/ holds seven such directories from earlier runs).
  I removed the one my run left.
