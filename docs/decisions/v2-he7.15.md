# W0.6h · X3 large delete + undo on the stock v1 oracle

**Outcome.** On the pinned oracle the over-cap undo is the expected failure recorded in amendment
A8. After Select All, Delete and Ctrl+Z, v1 prints `sublimite: error -105` and exits with status 1.
The destination keeps its bytes and its inode metadata. The twin round-trips exactly (PASS), and
all eight controls pass. The case reports `X3.over.roundtrip` as XFAIL only for that exact
signature, on that exact binary digest. A relaunch in the same profile restores the large tab
**empty** (see "Diagnostic (one run)"). The slow saves after Ctrl+A + Delete are v1 UI-thread CPU
in wrapped-view caret following, not the save's I/O (see "Where the 49.4 s goes").

## Derivation of the sizes (Law 4)

- `LINEIDX_CHUNK = 65,536` (P, v1 `src/lineidx/lineidx.h:35`) and `LINEIDX_EDIT_MAX_CHUNKS = 512`
  (P, `lineidx.h:38`), both at 6fec2e8.
- After Select All + Delete, `lineidx_edit` leaves one empty built chunk: `pieces = 0` and
  `kept = 0` give `count = 1` (v1 `lineidx.c:880-883`). The inverse insert of L bytes at offset 0
  then plans `first = last =` that chunk (`off == len`, `locate_byte` line 272). This gives
  `newlen = 0 − 0 − 0 + L = L` and `pieces = ceil(L / 65,536)` (lines 853-855). The veto
  `pieces > 512` fires iff L > 512 × 65,536 = **33,554,432 B** (P). The capacity term cannot
  refuse first: `cap = ceil(L / 65,536) + 64` (`lineidx_create`, line 585) and `kept = 0`.
- **Twin** (largest admitted): 512 × 65,536 = 33,554,432 B, i.e. 524,288 lines of 64 B.
- **Over**: (512 + 1) × 65,536 = 33,619,968 B, i.e. 525,312 lines. This matches the design value
  in `wave0-contract-design.md` X3. The smallest vetoed size would be 33,554,433 B. The design
  goes one full chunk past it, so both sizes are whole 64-byte lines (64 divides 65,536).
- Both sizes are below v1's COPY threshold of `min(FILE_COPY_CAP, RAM/32)` (`file.c:137`). The
  open path is the same for both.

`tests/contract/large_undo_fixture.py` generates both files. Line n is the zero-padded 10-digit
number, a space, a 52-character rotation of `[a-zA-Z0-9]` starting at n mod 62, and LF. Content
depends only on n. Each file's `.json` manifest records the generator's own SHA-256 next to a
re-read of the file on disk:

| variant | bytes | lines | chunks | sha256 |
|---|---|---|---|---|
| twin | 33,554,432 | 524,288 | 512 | `f3d8ccc6a575bb8c88884f583d1e46f5348f66ba2217d74f76eac83adfd39a46` |
| over | 33,619,968 | 525,312 | 513 | `7b5afe68bc3d7257923dd352789ef38ee788013263db54331006f0926948855e` |

Oracle: `/home/tobias/Projects/sublimite-v1-oracle/build/sublimite`, commit 6fec2e8, sha256
`64c27b03f20f51abbfc512fc0b5441883e2d2b8f1ca916b9061c6eb18ca71115`.

## Case design

`tests/contract/cases/large_undo.py` does one run per invocation. Every wait is woken by an event.
The events are an inotify watch armed before the keys whose effect it observes, or the editor's
pidfd. The only timers are hang detectors, and their expiry is ERROR.

1. **Display.** A private Xvfb on an explicit `:140`..`:149` with `-noreset -nolisten tcp`, a
   fresh MIT cookie in `build/large-undo/runs/<run>/xauthority` (0600) and `-displayfd`
   readiness. The server is stopped by its own Popen and pid.
2. **Sessions.** Every session gets a fresh sandbox and a fresh copy of the fixture, checked by
   SHA-256. For each variant, twin first:
   - **Control session**, in order:
     - type `x`, then the load fence, then Ctrl+S; the bytes must equal `"x"` + fixture
       (`ctl.load_save`);
     - BackSpace, then Ctrl+S; the bytes must equal the fixture (`ctl.restore`);
     - Ctrl+A, Delete, Ctrl+S; the file must be empty (`ctl.delete`);
     - Ctrl+Q; the editor must exit 0 (`ctl.exit`).
   - **Scored session.** Ctrl+A, Delete, Ctrl+Z, then the load fence, then Ctrl+S, then Ctrl+Q
     (`roundtrip`).
3. **Load fence.** v1 parks non-control keys while a file is opening, but Ctrl+S overtakes them
   (`editor.c:1064-1086`, `queue_pick`). Saving a `pending_open` buffer is a silent no-op
   (`session.c:591`).

   *Invariant:* an idle v1 writes nothing in `$XDG_DATA_HOME/sublimite`. Evidence: a 6 s idle
   probe saw only the runtime socket and lock. So an `IN_MOVED_TO session-*` commit after the
   first edit keys of a session implies an applied edit, which implies an adopted buffer. The
   fence is used only for a session's first edit, because a save leaves a trailing commit behind
   it.

   Untitled-tab edits do not produce this rename commit; the diagnostic below needed no fence
   there. Amendment A9 makes "events apply in order across an open" a v2 contract, so v2 needs no
   such fence.
4. **Save fence.** The case waits for `IN_MOVED_TO <name>`. v1 renames a temporary file over the
   destination (`file.c:1006`).
5. **Scored outcomes** (`classify`):

   | Outcome | Status |
   |---|---|
   | saved, bytes equal, Ctrl+Q exits 0 | **PASS** |
   | saved, bytes differ (including a 0 B save) | **FAIL** |
   | pinned digest (`EXPECTATIONS`, A8), editor exits with status 1, stderr contains `error -105`, destination bytes and inode metadata unchanged (dev, ino, mode, nlink, uid, gid, size, mtime_ns, ctime_ns; atime excluded because reading may advance it) | **XFAIL** |
   | pinned digest, bytes round-trip | **XPASS** (fails the run) |
   | any other editor exit (another status, signal or error, or a touched destination), a changed input fixture, or an expired hang detector | **ERROR** |

   The XFAIL signature is bound to the digest and to `X3.over.roundtrip` only.
6. **Controls.** A control that is not exactly as expected, or an editor exit during a control,
   is ERROR. It is final, and the run stops.
7. **Records.** Each record holds the case and assertion IDs, the status, the predicate, observed
   and expected values, the run ID, the target SHA-256, power stamps and steps. Each session
   keeps its stdout and stderr in separate files, its inotify transcript, `*.final.json` and a
   copy of the destination.
8. **Exit status:** 0 when everything is PASS or XFAIL, 1 for a FAIL or XPASS, 2 for an ERROR.
9. **Target.** Only v1 is driven: the fences above are v1's journal layout. v2 executes the same
   assertion when it has GUI input (W2), with PASS expected.

`make check-contract-large-undo ORACLE=/abs/.../sublimite CONTRACT_DISPLAY=:14N` runs the
self-test and then the case. It is not part of `make check`.

**Self-test** (`tests/contract/test_large_undo.py`):
- `ClassifyTests`. The exact signature is XFAIL. Each neighbour of it is not:
  - status 2, a signal, `error -104` or no stderr gives ERROR;
  - a changed inode or a changed ctime gives ERROR;
  - a 0 B save gives FAIL;
  - a round trip gives XPASS, and a round trip with a bad quit gives ERROR.

  The same exit on another digest, or on the twin assertion, is ERROR. A changed input is ERROR.
- `StubControlTest`. The stub is `build/xdriver-test-client window` behind a bash wrapper
  (`build/large-undo/selftest/stub-editor`, written by the test). On each key press it renames
  wrong bytes over the opened path and commits a `session-*` file. The run must exit 2 with
  exactly one record: `X3.twin.ctl.load_save` ERROR, "saved bytes differ from the control
  expectation".

**Red, then green (Law 9).**
- **Red:** the new self-test against the committed case
  (`build/large-undo/red/test_large_undo.py`, with `cases/large_undo.py` from 20b6ebd): all
  `ClassifyTests` error, 12 errors in total, because the old `classify` has no exit outcome.
- **Green:** the same tests against the new case, 5 tests OK. The stub run's record is
  `build/large-undo/selftest/runs/20261123T092643Z-144100/record.json`.

## Runs

Round 1 (2026-11-23, battery [bat]) is the stop-condition witness, kept as recorded.
- **Run `20261123T074710Z-107549`, load 17.18** (red): the harness had no load fence. Ctrl+S
  overtook a parked `x`, and `X3.twin.ctl.load_save` was ERROR (no save before the deadline).
- **Run `20261123T075412Z-114350`, load 13.28 → 10.45:**
  - all eight controls and the twin passed;
  - over: exit 1, `error -105`, destination `7b5afe68…48855e`, unchanged;
  - recorded as ERROR under the old empty-output expectation.
- **Attribution runs** (`diag-20261123T075649Z-117393` and `-117509`, separate run IDs): with
  Ctrl+Z as the last key, over exited 1 after 0.115 s (M). Twin was still running 60 s later and
  then saved the exact original.

Round 2 (A8 signature). Before each timed run, `pgrep -a Xvfb` showed no display in `:130`..`:139`.

```sh
make check-contract-large-undo ORACLE=/home/tobias/Projects/sublimite-v1-oracle/build/sublimite CONTRACT_DISPLAY=:140
```

Run `20261123T092647Z-144204`. BAT0 `Charging` [AC], load 4.60 → 6.98. make exit 0, case exit 0.

| assertion | status | observed | save (s) (M) |
|---|---|---|---|
| X3.twin.ctl.load_save | PASS | 33,554,433 B `b47a594d…80c3b3` | 0.290 |
| X3.twin.ctl.restore | PASS | 33,554,432 B `f3d8ccc6…d39a46` | 0.348 |
| X3.twin.ctl.delete | PASS | 0 B `e3b0c442…7852b855` | 43.524 |
| X3.twin.ctl.exit | PASS | exit 0 | |
| **X3.twin.roundtrip** | **PASS** | 33,554,432 B `f3d8ccc6…d39a46`, input intact, Ctrl+Q exit 0 | 0.536 |
| X3.over.ctl.load_save | PASS | 33,619,969 B `e95191e2…5ab811` | 0.145 |
| X3.over.ctl.restore | PASS | 33,619,968 B `7b5afe68…48855e` | 0.207 |
| X3.over.ctl.delete | PASS | 0 B | 5.649 |
| X3.over.ctl.exit | PASS | exit 0 | |
| **X3.over.roundtrip** | **XFAIL** | exit 1 0.273 s after the keys; stderr `sublimite: error -105; journal: …`; destination identity before == after (ino 80130206, ctime_ns 1795426070499140819, sha256 `7b5afe68…48855e`); input intact | none |

**Final green run on the final code.** This is the same command after `SAVE_RENAME_S` was raised
to 400 s, which affects hang detection only. Run `20261123T093029Z-147102`, [AC], load 6.00.

- The self-test passed: 5 tests, and the stub record is ERROR at `X3.twin.ctl.load_save`, with
  11 B `stub bytes\n` observed.
- The case exited 0. All eight controls PASS, `X3.twin.roundtrip` PASS (save 0.463 s), and
  `X3.over.roundtrip` XFAIL.
- The `ctl.delete` saves took 0.793 s (twin) and 0.614 s (over) this time, against 43.5 s and
  5.6 s in the previous run: the spread analysed below.

## Diagnostic (one run)

Script: `build/large-undo/explore/recovery_diag.py :143`, not a suite case. Run
`recovery-diag-20261123T092606Z`, BAT0 `Charging` [AC], load 3.62 at start.

The sandbox profile held:
- tab 1: the over file;
- tab 2: `second.txt` (`second file line\n`) with unsaved typed `typed`;
- tab 3: an untitled tab containing `untitled`.

Before the exit, Select All + Copy read 22 B `typedsecond file line\n` from tab 2 and 8 B
`untitled` from tab 3.

On tab 1: Ctrl+A, Delete, then the journal commit, then Ctrl+Z. The editor exited with status 1
and `error -105`.

After the exit:
- both files on disk were unchanged (identity equal);
- the data folder held `session-*` (4,096 B), a 33,619,968 B `.sublimite-base-*` (the retained
  original), a 17 B `.sublimite-base-*` (`second.txt`), and the session lock;
- the runtime folder held only the IPC lock: v1 removed its socket.

The relaunch used no arguments, in the same profile, through `sandbox.restart`:

| tab | restored | how observed |
|---|---|---|
| 1 (over file) | **empty buffer**: Save As to a fresh path wrote **0 B** (sha `e3b0c442…`). Screenshot `relaunch-tab1.png`: tab 1 empty, now titled `recovered-large…` | Ctrl+Shift+S, then `IN_MOVED_TO` of the new name |
| 2 (`second.txt`) | 22 B `typedsecond file line\n`, unsaved (•), equal to before the exit | Select All + Copy, read through the driver |
| 3 (untitled) | 8 B `untitled`, unsaved (•), equal to before the exit | Select All + Copy, read through the driver |

- Both files on disk were still unchanged after the relaunch. Ctrl+Q on the relaunched editor
  exited 0.
- The first complete run (`recovery-diag-20261123T092337Z`) copied tabs 2 and 3 with the same
  bytes. In that run, tab 1 gave no clipboard owner within 120 s, which is what an empty
  selection does.

**What it means.** Hot-exit recovery restores the delete, which was journaled before the Ctrl+Z,
and not the original content. The file on disk is intact until the user saves the recovered
tab. A Ctrl+S on that tab writes 0 B over the original. So data loss is one ordinary keystroke
away after the crash.

The diagnostic committed the delete to the journal before pressing Ctrl+Z. Where an immediate
Ctrl+Z exits before any commit (as in the round-1 scored run), whether the delete is in the
journal at exit was not tested. The redo and paste/replace paths stay code reading only.

## Where the 49.4 s goes (v1)

These are diagnostic runs (`build/large-undo/explore/timing.py <variant> :141 control`). They run
the control sequence on the over file: `x`, the fence, Ctrl+S, BackSpace, Ctrl+S, then a measured
step of Ctrl+A, Delete, Ctrl+S sent back to back, as in `ctl.delete`. The script timestamps the
key sends and inotify events in the work and journal folders. It samples every thread's
utime/stime/state/wchan and `/proc/pid/io` every 50 ms.

- strace was not available: `ptrace_scope = 1` and the editor is not a descendant of strace.
- The kernel stack and delay accounting were unavailable.
- `perf record -p` worked for user space (`perf_event_paranoid = 1`).

| run | power, load | Ctrl+S → rename | main-thread user CPU | read syscalls | bytes written |
|---|---|---|---|---|---|
| `…080723Z` quiet | [bat] 1.11 | 0.776 s | 0.61 s | 10,692 | 67,248,128 |
| `…080820Z` + background 8,000 MiB buffered `dd` | [bat] 0.71 → 1.78 | 1.547 s | 1.42 s | 21,410 | 67,260,416 |
| `…080852Z` + 12 busy loops (12 CPUs) | [bat] 3.34 → 8.11 | 32.562 s | 27.78 s | 389,363 | 67,252,224 |
| `…091129Z` + 12 busy loops, perf | [AC] 5.53 → 12.42 | 49.027 s | 35.14 s | 463,666 | 67,256,320 |
| `…091257Z` + 12 busy loops, perf, other worker's Xvfb `:137` | [AC] 17.26 → 34.06 | 188.582 s | 49.77 s | 586,193 | 67,256,320 |

Timeline of the quiet run, relative to Ctrl+S:

| t (s) | what | where | thread |
|---|---|---|---|
| −0.004 | Ctrl+A and Delete sent | | |
| 0 → 0.632 | no file activity | | main thread runs (0.61 s user) |
| 0.632 → 0.694 | `.sublimite-copy-*` written, then renamed to `.sublimite-base-*` | `editor/retain.c` → `journal_retain_start` → `retain_async_worker` | worker |
| 0.697 → 0.763 | a second `.sublimite-base-*` | savectl → `journal_save_prepare` → `retain_base` → `retention_worker` | worker; UI thread polls with `delay()` |
| 0.763 → 0.773 | journal record and `session-*` commit | | |
| 0.775 | temporary file created | work folder | |
| 0.776 | renamed over the destination | work folder | |

- Each retained copy runs `retain_generation`. That is `retain_copy` of the 33.6 MB on-disk file,
  then `metadata_sync` (fsync) and `directory_sync`. Together the two copies are the 67.2 MB
  written. They took 0.13 s quiet and 0.075 s under the 8,000 MiB writer.
- The disk (NVMe, ext4) is not where the time goes. Under CPU contention the copies still
  started only at 32.41 s, and took 0.14 s.
- The time before the copies is the **UI thread spinning**: state R in every sample, workers idle
  in `futex_do_wait`, about 12,000 empty reads per second (input polling), no writes.

The DWARF profile (run `…091257Z`, 4,578 samples) puts 78% of samples under this chain:

`editor_step` → `editor_continue_action` → `view_continue` → `advance` → `finish_move` →
`fresh_follow` → `follow_rows` → `layout_wrap_follow_edit_anchored` (`row_above` walk) →
`layout_visual_row_fresh` → `scan_row` → `layout_wrap_run`

Self time:

| symbol | share |
|---|---|
| libc `memset` | 35% |
| `layout_wrap_run` | 25% |
| `next_seed` | 7% |
| `end_row` | 5% |
| `scan_row` | 3% |

The memset is `scan_row`'s `memset(&q,0,offsetof(layout,win))` (`wrap.c:548`), which zeroes a whole
`layout` struct for every one-row query.

So the 49.4 s is v1, not the save. Select All moves the caret to the end of a 33 MB wrapped
buffer. The wrapped-view caret follow then walks visual rows upward, and each step builds a
fresh one-row `layout` (`scan_row`).

**Not settled.** A single `finish_move` completes in one call (`view.c:706`, `busy=false`), but the
total CPU grows with contention: 0.61 s quiet, 27.8-49.8 s under contention. The quiet-box
results vary just as widely: `ctl.delete` took 43.5 s at load 4.6 [AC] and 5.6 s in the same run.
So the follow must be re-run a timing-dependent number of times. Hypothesis (E), unconfirmed: it
re-runs once per editor slice while the wrap layout is not current
(`fresh = v->edited || layout_wrap_current_rows < n`, `view.c:672`), and the progressive layout
gets less done per wall-clock slice when the thread is preempted.

**v2 datum.** Avoid any caret-follow or layout query whose cost is O(rows × full layout reset)
and that is repeated per slice. Such work makes wall-clock slicing degrade super-linearly
under load. It is also a datum for the journal: a save of an empty buffer copied 2 × 33.6 MB
with fsync before writing 0 B.

## Deadlines

These are a hang detector, not a gate. Expiry is ERROR (the run is invalid) and never PASS, FAIL
or XFAIL. There is one named constant per class of wait in `large_undo.py`. Each is (E): at
least 2× the slowest measurement (M) above, on the ThinkPad with 12 CPUs.

| constant | value | measured (M) |
|---|---|---|
| `XVFB_READY_S` | 15 s | 0.18-0.26 s |
| `WINDOW_MAP_MS` | 30 s | 0.12-0.29 s from launch to mapped window |
| `JOURNAL_COMMIT_S` | 120 s | 0.97-4.18 s from keys to `session-*` commit, load 4-13 |
| `SAVE_RENAME_S` | 400 s | 0.15-0.93 s for ordinary saves. After Ctrl+A + Delete: 0.78-49.4 s at load 1-13 [bat]/[AC], and 188.6 s under synthetic contention at load 34. |
| `EDITOR_EXIT_S` | 60 s | 0.12-0.18 s after Ctrl+Q, or 0.273 s after Ctrl+Z on the pin |
| `DRIVER_PIPE_S` | 60 s | Pipe watchdog. It must exceed `WINDOW_MAP_MS`, xdriver's `find` deadline (v2-he7.9). |

## Watchdogs in the other contract cases (3a; read-only review of v2 `d61aded`)

**This case.**
- Every hang detector raises `HarnessError`, which is ERROR.
- xdriver deadlines raise `XDriverProtocolError`, which is ERROR.
- An expired `child.wait` (a TimeoutExpired) is ERROR.
- No sleep remains: waits are woken by inotify and the pidfd.

**`tests/contract/cases/files.py`.**
- `wait_for_file_event` (140-170) is woken by inotify and the pidfd. Expiry is `TimeoutError`,
  which is ERROR. `quit_editor` (300-307) and the display readiness checks also time out to
  ERROR. Fine.
- **Timing can decide a FAIL.** `search_copy` (247-271) re-sends Ctrl+A/Ctrl+C until the
  clipboard equals the expected Find Results. A sampled intermediate copy that is not a prefix of
  the final text raises `Mismatch`, which is FAIL. Which intermediate state is sampled depends on
  timing. A correct editor that emits results in a non-prefix order (for example a changing count
  line) can FAIL depending on when the poll looked. Expiry is ERROR. The `owner == sentinel`
  branch loops with no pause, re-sending both chords at full rate.
- **Timing can decide a spurious ERROR (the A9 hazard).** In R3, `save-b` (326-334): Return opens
  `nested/b.c` from Find Results, then `b` is typed, then `save()` sends Ctrl+S. If `b.c` is still
  opening, v1 lets Ctrl+S overtake the parked `b`, and the save is a no-op. The `IN_MOVED_TO` wait
  then expires, which is ERROR. The race is small-file open speed against driver speed. It never
  gives a wrong PASS or FAIL.
- R5 `open-alive` and `reopen-alive` take an instantaneous `child.poll()` sample, which is not a
  fence. A crash right afterwards surfaces as ERROR at `quit_editor`.
- **Outside the timing question.** `main` returns 1 for every non-PASS, so FAIL and ERROR share an
  exit status. An exception type outside `run_case`'s list exits 1 with a traceback.

**`tests/contract/cases/recovery.py`.**
- `copied_text` (141-155) polls the clipboard owner with `time.sleep(0.01)`. It paces a readiness
  poll only, and expiry is `TimeoutError`, which is ERROR. XFixes selection events would make it
  causal.
- `wait_readable(pidfd)`, and `child.wait(timeout=WATCHDOG)` after the hot exit, time out to
  ERROR. Fine.
- `paste-and-sigkill` kills right after the paste key. Timing picks which of the two legal
  outcomes is reported (`pre-edit` or `whole-edit`), never PASS against FAIL. Fine.
- **Outside the timing question.** An exception outside `(OSError, Refused, XDriverError,
  SubprocessError)` escapes `run_case`. Python then exits 1, the same status as FAIL.

## Sandbox change (needed for the diagnostic's relaunch)

`sandbox.restart` used to accept only a reaped SIGKILL or a clean exit 0. v1 exits 1 after a fatal
editor error, having removed its socket (`main.c:317` runs `ipc_server_fini` before the exit status is returned).

The new rule: any exit with a status (≥ 0) gets the clean-exit proof, meaning the socket is gone
and the recorded lock is unchanged. A death by any signal other than SIGKILL is still refused.

- Test `test_restart_after_error_exit_that_removed_its_socket`: exit 1 without a socket may
  restart; exit 1 with a stale socket may not.
- Red: 1 error ("restart requires SIGKILL or a clean exit"). Green: 21 tests OK.
- `docs/decisions/v2-he7.8.md` is updated.

## Incident: perf's build-id cache hard-linked the oracle

My first two `perf record` runs (10:03 local) let perf add the profiled binary to its build-id
cache. The cache **hard-linked** the oracle into
`~/.debug/home/tobias/Projects/sublimite-v1-oracle/build/sublimite/0f399af…/elf`. That raised the
oracle's link count to 2. `sandbox.py` then correctly refused every oracle launch ("must be a
singly linked executable file").

I removed exactly the entries those runs added:
- the oracle's `elf` and `probes`;
- the `libxcb` `elf` and `probes`;
- the two `.build-id` symlinks;
- the directories created at 10:03.

These were individual `rm` and `rmdir` calls, with no recursive delete. Afterwards the oracle was
back at link count 1, with the same inode 78948476 and the same sha256 `64c27b03…71115`. Later
profiles used `perf record -N` (no build-id cache), and the link count stayed at 1. **Rule for
contract workers:** never profile the oracle without `-N`.
