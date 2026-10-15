# edit-c26: Astra drive 5 task 37, dense64 hot-exit relaunch error -103

Bead: edit-c26 (P1, drive 5's only verdict blocker). Worker session s7, 2026-10-16.

## The case

`/tmp/edit-corpus/dense_short.txt` (67,108,864 B = 2^26 (M), fixture stat in
`build/fixtures/37-restart-fixture.json`) opened in place by path, one
character typed, idle checkpoint, a second character, clean close. The
journal left behind is 12,288 B (M) (`tests/fixtures/drive5-task37-session.jnl`,
sha256 18442215be092729…): BASE (the corpus, edit-28u referencing shape),
TARGET (same path), INSERT 'x' at 0, VIEW/TABS/WINDOW, then the live INSERT
'y' at 1 (this test's dump: 9 records, 1 BASE, 1 TARGET, 2 one-byte INSERTs).
No-argument relaunch on build/astra5 (2c48f9c): exit 1, `error -103`, journal
renamed `.set-aside-*`. On build/astra6 (9b5ee41): window, tab restored.

## Mechanism on 2c48f9c (measured, instrumented copy of the 2c48f9c-era source)

Headless driver over the drive's journal, with fprintf at each step of
`recovery.c`/`open.c` (trace in the worker report):

1. Discovery: journal 12,288 B + BASE 67,108,864 B = 67,121,152 B
   <= `JOURNAL_STARTUP_SCAN_BYTES` 105,000,000 (P, arithmetic) so the session is
   not deferred (edit-fm4). Strict replay succeeds (BASE identity matches).
2. `install_session` forces COPY acquisition (`copy_threshold = UINT64_MAX`).
   `editor_buffer_prepare` sizes the buffer arena as reserve + file size =
   64 MiB (default, `buffers.c`, cfg.arena_bytes 0 as in main.c) + 67,108,864
   = 134,217,728 B (M: printed arena size).
3. `install_buffer` replaces the whole tree: `piece_delete(0, 2^26)` returns 0
   but copies every deleted ORIGINAL byte into the add buffer (piece.c
   `add_reserve(t, cb)` + `add_append`), 67,108,864 B; then
   `piece_insert(r->bytes, 67,108,866)` needs another 67,108,866 B. Total
   134,217,730 B > 134,217,728 B arena before any other arena use (history,
   minimap rows, nodes) (P, arithmetic): `PIECE_ERR_NOMEM` (2) ->
   `EDITOR_ERR_MEMORY` (-101). Measured: `piece_delete rc=0`, `piece_insert rc=2`.
   The ceiling is the reserve: any startup-restored text longer than about
   64 MiB minus the arena's other use failed, dirty or clean.
4. `restore_source` sets the source aside, sets `recovery_retry`, returns
   `EDITOR_ERR_IO`. `editor_open`'s edit-btj retry should then open empty with
   the notice "session recovery failed; its journal was set aside". It does
   not: the failed attempt's `editor_close` discarded the launch's empty
   journal and `journal_session_release` unlinked its lease file
   `.session-X.lock`; the retry's lease check in `editor_session_restore`
   (`stat(lock_path)` vs the caller's lease) fails with ENOENT (M: errno=2)
   -> `EDITOR_ERR_IO` -> `editor_open` returns -103 -> main.c exits 1.

Why it surfaced only now: before edit-28u (c62d560) a 64 MiB dirty file had a
>= 64 MiB whole-text journal, so journal + BASE > 105,000,000 and edit-fm4
deferred it. 28u made the journal 12 KiB, which brought the session under the
fm4 bound into the eager COPY install, which could not hold it.

## Fixing commit

Coordinator bisect (M, coordinator-run, release builds, same copied journal
and BASE, launch on :99 with no file argument):

| commit | bead | result |
|---|---|---|
| 2c48f9c | edit-mmp (drive 5 build) | FAIL -103 |
| ba2beeb | edit-52o | FAIL |
| f09cfa3 | edit-qct | FAIL |
| 87b0f5f | edit-n95 | FAIL |
| 9b5ee41 | edit-f3i | PASS (window, journal consumed) |

edit-f3i (9b5ee41) fixed it, agreeing with the code: `install_buffer` now
replays the logged ops onto the opened file when its origin identity equals
the journalled BASE (`base_id_equal`), so the install is two one-byte
`piece_insert`s and no whole-tree delete/insert. Measured on main with the
same instrumentation: `apply_ops rc=0 len=67108866`. 52o, qct, dz0, n95 do not
touch the startup install (qct's lazy, non-COPY install is restore.c, the
reopen of a deferred placeholder, not this path).

## Decisions

1. **Test location**: `tests/editor_session_test.c`, block `edit-c26 begin/end`,
   `c26_dense_hot_exit()`; its large_restore harness (private XDG_DATA_HOME,
   leased launches as main.c) fits; editor_hot_exit_test kills children and
   never closes cleanly.
2. **Size 2^26, real bytes, not sparse**: the drive's file size (M). The
   failure is arithmetic on the reserve (above), so a smaller file passes on
   2c48f9c; 2^26 is the drive's size and exceeds the reserve by the typed bytes.
   Content follows mkcorpus gen_dense (0-3 chars per line) so line indexing
   sees the drive's shape. One 64 MiB file is written once per run under
   build/editor-c26-*, shared by both cases, removed on every exit path; df
   before/after runs showed no growth (135 G free both sides).
3. **Case A (from scratch)**: leased launch with the path, wait for the async
   open, type 'x', `journal_flush` + `editor_checkpoint`, type 'y', clean
   `editor_close`, release the lease. Asserts the journal is the referencing
   shape and inside the fm4 bound (so it is replayed, not deferred). Then two
   no-argument leased relaunches: `editor_open` 0, one dirty tab
   "dense_short.txt", length 2^26 + 2, head "xy", no notice, no set-aside,
   adopted journal retired, one session journal left. Two relaunches because
   the restored session must itself restore (drive 6 may relaunch twice).
   The mode is printed, not asserted: below `file_default_copy_threshold()`
   (min(256 MiB, RAM/32)) the file is COPY, on the drive's box too.
4. **Case B (the drive's journal)**: the fixture is copied to
   `tests/fixtures/` (build/ is ignored by git). Its BASE identity (inode
   6437225) cannot be reproduced, so the test **rewrites the BASE record**
   (fresh `journal_capture_base` of the regenerated file, `journal_encode_base`)
   and the TARGET record (recovery opens TARGET; leaving it on the corpus path
   opens a different generation; docs/worker-reports/edit-c26-s7.md finding 2) and writes the drive's other
   records unchanged through `journal_open` + `journal_rotate`. It asserts the
   fixture's shape (1 BASE naming /tmp/edit-corpus/dense_short.txt of 2^26 B,
   1 TARGET, two one-byte INSERTs at 0 and 1) and the same relaunch outcome.
   Never skipped; never reads or writes /tmp/edit-corpus.
5. **Red evidence**: (a) binary-driven, build/astra5 vs build/astra6, the
   coordinator's steps (`build/c26-evidence/drive_repro.sh`); (b) in process:
   the c26 block extracted verbatim with the file's helpers into a harness and
   linked against a copy of the 2c48f9c-era source (`.wt/edit-mmp-s7`, which
   holds the 28u and mmp decisions and none of 52o/qct/dz0/n95/f3i; read, not
   built in place): both cases fail with `editor_open=-103`. The same harness
   linked against this tree passes.
6. **No behaviour change on main.** Main passes the drive's case through the
   f3i op replay, not by accident. The two defects behind the -103 that remain
   on main (whole-text fallback ceiling, btj retry under a lease) are reported
   as findings, not fixed: neither is reached by the drive's shape on main.
