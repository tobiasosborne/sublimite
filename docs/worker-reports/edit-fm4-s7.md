# edit-fm4 worker report (session s7)

Bead: edit-fm4 [BUG] P1. Launching on a profile with GB-scale orphan journals
fails and adds a 1 GiB set-aside on every attempt. Design and derivations:
docs/decisions/edit-fm4.md.

## Files touched

- src/journal/journal.h: `JOURNAL_STARTUP_SCAN_BYTES`, derived as (G) G4a warm p50 25 ms x (M)[AC] page-cache read 4.2 GB/s = 105,000,000 B. It is a tracked aspiration.
- src/journal/retire.c: an artifact larger than the bound is pinned without being scanned, and the inventory is marked uncertain.
- src/editor/recovery.c: `cost_record` / `restore_deferred` add the journal size to the BASE sizes. The journal size is checked by fstat before anything is scanned. Discovery defers any session over the bound: it is not replayed, copied or renamed, its lease is released, a notice is shown and `recovery_deferred` is incremented.
- src/editor/private.h: new field `size_t recovery_deferred`.
- tests/editor_session_test.c: new red/green case `large_orphans` (fixture helpers `fm4_*`), run first in main.
- src/editor/STATUS.md: edit-fm4 section prepended (partial: point 2 missing).
- docs/decisions/edit-fm4.md

## Test (red first)

The fixture uses a private `XDG_DATA_HOME` = realpath(build/editor-fm4-XXXXXX). The test asserts that `journal_default_dir` resolves inside it, so it never touches the real profile. It contains:
- `corpus`: a sparse 1 GiB file ((P) 2^30, the brief's size).
- `sublimite/session-*`: a real orphan journal. A forked editor opens the corpus, types `x`, runs `editor_checkpoint` and calls `_exit`. The result is 1086427136 B (M) of written records. This is the same size as the profile's `session-1aAWBt.set-aside-*`. The sparse source does not shrink the work: the checkpoint reads every byte, and so do the replay, the copy and the next checkpoint.
- `sublimite/session-OLDSET.set-aside-1`: a copy of the orphan made with copy_file_range.
- The corpus is then changed (pwrite at offset 0), so the BASE no longer matches.

The launch mirrors main.c: `journal_session_create` plus `start_empty`, with the null backend. The test asserts:
- rc 0;
- `recovery_notice` set and `recovery_deferred == 1`;
- a key press is accepted;
- launch time printed as a tracked number against G4a warm p99 40 ms (G), not enforced (Law 1: G1 is the only enforced perf guard; coordinator change, see "Revision" below);
- every pre-existing non-lock file keeps the same name, inode, size and mtime;
- no new `*.set-aside-*` appears.

The fixture is removed on success and on every failure return. A kill by timeout leaves it behind; I removed the leftovers by hand after the red runs.

### Red (before the fix)

- 2026-10-15 23:53, BAT0 Charging, load 6.63/6.44/3.47: `timeout 300 build/san/tests/editor_session_test` gave **exit 124 after 300.1 s (M)**. `editor_open` never returned.
- 2026-10-15 23:59, BAT0 Charging, load 5.83/6.34/4.32: the release binary `build/tests/editor_session_test` gave **exit 124 after 300.06 s (M)**. Profile afterwards:
  ```
  1086427136 session-6uf5L8.set-aside-1792069172438657465   <- orphan renamed aside
  1086427136 session-DHEX2d                                 <- launch's own journal: new 1 GiB copy
  1086427136 session-OLDSET.set-aside-1
  ```
  This is the trial bug: the orphan is set aside and a new 1 GiB checkpoint copy is written. On the real display the coordinator saw `-103` after 10.06 s. Headless, `editor_open` was still spinning on two threads after the copy (R state, wchan 0); ptrace_scope=1 blocked a stack capture.
- A third run at 00:07 (60 s timeout, after debug prints were added) showed the same picture: exit 124, and "open" was never reached after the orphan was written.

### Green (after the fix)

- 2026-10-16 00:09:56, BAT0 Full, load 5.13/4.98/4.67, release: exit 0, `fm4 launch rc 0 in 27.557 ms`, whole suite 7.87 s wall.
- 00:10:11, BAT0 Full, load 4.95/4.94/4.67, ASan/UBSan `build/san/tests/editor_session_test`: exit 0, **28.040 ms** launch, 7.45 s wall.
- 00:13:06, BAT0 Full, load 3.55/4.37/4.51, ASan/UBSan under `DISPLAY=:99 sh tools/with_display_lock.sh`: passed (final suite line printed), **32.343 ms** launch.

(The three green runs above were made with the 40 ms assertion still enforced.)

### Revision (coordinator, after the first report)

The elapsed < 40 ms assertion became a tracked print. Every functional assertion is unchanged (rc 0, `recovery_deferred == 1`, key accepted, inode/size/mtime unchanged, no new set-aside). Re-run once each:

- 2026-10-16 00:17:39, BAT0 Full, load 4.65/3.78/4.15: release `build/tests/editor_session_test` **exit 0**.
  `large_orphans: launch_ms=36.150 (M) G4a_p99=40 bound=tracked power=Full load1=4.76`
- 00:17:47, BAT0 Full, load 5.02/3.89/4.18: ASan `DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_session_test` **exit 0**.
  `large_orphans: launch_ms=44.213 (M) G4a_p99=40 bound=tracked power=Full load1=5.42`

The ASan run came in over 40 ms at load1 5.42, so the enforced version would have flaked. G4a stays a tracked number here.

## Verification commands

- `make -j4 all` (gcc 13 release, -Werror): exit 0, 2026-10-16 00:13.
- ASan/UBSan, run individually from build/san/tests (not the whole `make check`). Stamped 00:10:40, BAT0 Full, load 5.02/4.95/4.68. Display-lane binaries ran under `DISPLAY=:99 sh tools/with_display_lock.sh`. All exit 0:

  | suite | wall time (M) |
  |---|---|
  | journal_retire_test | 0.62 s |
  | journal_test | 10.0 s |
  | journal_group_test | 1.97 s |
  | journal_kill_test | 20.7 s |
  | dataloss_test | 68.2 s |
  | editor_hot_exit_test | 9.39 s |
  | editor_close_test | 0.12 s |
  | editor_checkpoint_test | 0.36 s |
  | editor_session_test | see Green above |

- I did not run the other display suites (gl, x11, raster, and so on): this bead does not touch them.

## Brief points

1. **Done.** Startup no longer copies or replays a multi-GB journal: retirement skips artifacts over the bound and discovery defers them. Small changed-BASE sources are still set aside by rename only, as before (edit-btj). Large ones are no longer touched at all.
2. **Not done.** Deferred orphans are not yet shown as recoverable closed tabs and are not restored lazily. They stay discoverable, a notice says so, and `tools/journal_replay` recovers them by hand. The follow-up needs are in the decision doc ("Not done"), mainly placeholder tabs and a checkpoint that does not rewrite unmodified text.
3. **Done by construction.** There is no 10 s timer in src/ (decision doc, "The 10 s limit"). Nothing slow runs before the first frame, so nothing can time out into -103.
4. **Done.** A launch with GB-scale orphans leaves every pre-existing file unchanged (asserted). Because nothing is installed, the retry/set-aside path (open.c) cannot run for them.

## Found, not fixed

- **`editor_checkpoint` rewrites every buffer's whole text.** It writes DELETE of the base plus INSERT of the full text in 4 KiB records, with one calloc per record (checkpoint.c), even when the buffer is unmodified and equal to BASE. Any checkpoint with a 1 GiB file open writes a 1 GiB journal. This is where the GB orphans came from.
- **Recovery install forces COPY acquisition.** recovery.c `install_session` sets `copy_threshold = UINT64_MAX`, so a restored 1 GiB buffer loses the mapped large-file path. In the red run, editor_open was still spinning after 300 s (M) once the copy was done.
- **Killed rotations leave litter that is never removed.** `.sublimite-journal-*` checkpoint temporaries have the JNL1 magic but no generated name, so retire never treats them as clean. retire's `lease()` also creates `..sublimite-journal-*.lock` files for them. The fixture profile holds three of these, about 2.2 GB. Even unscanned, they are never removed.
- **A pinned artifact blocks base retirement.** With any artifact over the bound present, `inv.uncertain` stops `.sublimite-base-*` originals from being retired. This is conservative and costs only litter.
- **The fixture profile has changed since the brief.** It now holds three 1 GiB-class set-asides (6pu6de, 1aAWBt, BawHhJ at 17:20) and no `session-bjn9D6`. I only listed it with `ls -la`; I did not read, change or launch against it.
- **Unrelated test nit.** `editor_session_test` is matched by the display-lane filter (`editor%`) but uses only the null backend.
