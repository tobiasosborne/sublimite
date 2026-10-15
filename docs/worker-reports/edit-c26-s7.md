# edit-c26 worker report (s7, 2026-10-16)

Bead: pin Astra drive 5's verdict blocker (task 37, dense64 hot-exit relaunch
error -103) with a red test, name the fixing commit, explain the mechanism.
Design and arithmetic: docs/decisions/edit-c26.md.

## Files touched

- `tests/editor_session_test.c`: block `/* edit-c26 begin */ … /* edit-c26 end */`
  (`c26_dense_hot_exit`, cases A and B), called from `main` after `large_orphans`.
- `tests/fixtures/drive5-task37-session.jnl`: copy of the drive's journal
  (12,288 B, sha256 18442215be092729305cfb814e80f792597442e0fb2381e3a3328eea444090ff).
- `docs/decisions/edit-c26.md` (new), this report (new).
- No src change. Evidence logs and scratch harnesses: `build/c26-evidence/` (git-ignored).

## Mechanism of the -103 (2c48f9c)

Two defects in series, measured with an instrumented copy of the 2c48f9c-era
source (`.wt/edit-mmp-s7` copied to the scratchpad; instrumented files kept as
`build/c26-evidence/recovery_dbg.c`, `open_dbg.c`; driver `c26drive.c`):

```
DBG session_restore lease ok skip=0
DBG strict replay rc=0
DBG add_recovered rc=0
DBG adopt rc=0 mode=1 arena=134217728        # FILE_MODE_COPY; 64 MiB reserve + 2^26 content
DBG piece_delete rc=0 len=67108866           # copies 2^26 ORIGINAL bytes into the add buffer
DBG piece_insert rc=2                        # PIECE_ERR_NOMEM: 2^26 + 67,108,866 > 134,217,728
DBG install_buffer rc=-101                   # EDITOR_ERR_MEMORY
DBG install_session rc=-101
DBG open fail line 229 rc=-103               # restore_source: set aside, recovery_retry
DBG lease check failed lock=11 errno=2       # retry: .session-X.lock was unlinked by the failed attempt's teardown
DBG open fail line 229 rc=-103
DBG retry rc=-103
editor_open rc=-103
```

1. The session is under the fm4 bound (12,288 + 67,108,864 <= 105,000,000, P),
   so it is installed eagerly; strict replay passes.
2. Install forces COPY; the arena is 64 MiB reserve + file size; the
   whole-tree replace (`piece_delete` all, which copies deleted ORIGINAL bytes
   into the add buffer, then `piece_insert` of the full recovered text) needs
   2^26 + 67,108,866 = 134,217,730 B of add space > 134,217,728 B (P). Any
   startup-restored text longer than about the 64 MiB reserve failed, dirty or clean.
3. The edit-btj fallback (open empty with "session recovery failed" notice)
   then fails too: the first attempt's `editor_close` discards the empty
   launch journal and `journal_session_release` unlinks its lease file; the
   retry's lease identity check `stat(lock_path)` gets ENOENT -> -103.

edit-28u (c62d560) exposed it: the 12 KiB referencing journal put a 64 MiB
session under fm4's deferral bound and into the eager install.

## Fixing commit

edit-f3i, 9b5ee41. Coordinator bisect (M, coordinator-run): 2c48f9c FAIL -103,
ba2beeb (52o) FAIL, f09cfa3 (qct) FAIL, 87b0f5f (n95) FAIL, 9b5ee41 (f3i) PASS.
Code reason: f3i's `install_buffer` replays the logged ops onto the opened
BASE when `base_id_equal(&b->origin, &r->base_id)`, so the install is two
one-byte inserts, no whole-tree delete/insert. Same instrumentation on main:
`ops_valid=1 … apply_ops rc=0 len=67108866 want=67108866`, `install_buffer rc=0`.

## Red runs

(a) Binary-driven, coordinator's steps (`build/c26-evidence/drive_repro.sh`,
log `build/c26-evidence/red-binary.txt`; BAT0 Not charging, load1 about 4-5):

```
### build/astra5/sublimite (2c48f9c)
== exit 1
sublimite: error -103; journal: /tmp/tmp.DUGo37NU7j/sublimite/session-nJG54j
-rw------- 1 tobias tobias 12288 Oct 16 10:49 session-c26abc.set-aside-1792108169712615750
### build/astra6/sublimite (9b5ee41)
== exit 124            # timeout 8 s after the window came up (still running)
-rw------- 1 tobias tobias 8192 Oct 16 10:49 session-PhpFwy   # source consumed, restored
```

(b) In process: the c26 block extracted verbatim with the file's helpers
(`build/c26-evidence/c26_harness.c`; `c26_harness_b.c` runs case B alone),
linked against the 2c48f9c-era source copy. Line numbers are the harness's.
Logs `red-inprocess.txt`, `red-inprocess-b.txt`. BAT0 Not charging, load1 3.88.

```
--- red A+B (2c48f9c-era src)
c26: first launch mode=copy
c26: hot-exit journal bytes=8192 (M) base_bytes=67108864
c26: relaunch editor_open=-103 tabs=0 text=no dirty=no notice=none restore_ms=151.173 (M) bound=tracked power=Not charging load1=3.88
editor_session_test:184: FAIL opened == 0
editor_session_test:203: FAIL c26_relaunch(dir, orphan, "xy", "dense_short.txt") == 0
editor_session_test:297: FAIL c26_dense_hot_exit() == 0
exit 1
--- red B only
c26: fixture record INSERT offset=0 size=9
c26: fixture record INSERT offset=1 size=9
c26: fixture records=9 bases=1 targets=1 inserted=2 base=/tmp/edit-corpus/dense_short.txt head=xy
c26: relaunch editor_open=-103 tabs=0 text=no dirty=no notice=none restore_ms=150.762 (M) bound=tracked power=Not charging load1=3.88
editor_session_test:184: FAIL opened == 0
editor_session_test:271: FAIL c26_relaunch(dir, orphan, head, "dense_short.txt") == 0
exit 1
```

## Green runs (this tree, main 9b5ee41 + test)

Release `build/tests/editor_session_test`, exit 0 (log `green-release.txt`;
BAT0 Not charging, load1 4.74; df -h / 135G free before and after):

```
c26: first launch mode=copy
c26: hot-exit journal bytes=8192 (M) base_bytes=67108864
c26: relaunch editor_open=0 tabs=1 text=yes dirty=yes notice=none restore_ms=123.051 (M) bound=tracked power=Not charging load1=4.78
c26: relaunch editor_open=0 tabs=1 text=yes dirty=yes notice=none restore_ms=161.379 (M) bound=tracked power=Not charging load1=4.78
c26: fixture records=9 bases=1 targets=1 inserted=2 base=/tmp/edit-corpus/dense_short.txt head=xy
c26: relaunch editor_open=0 tabs=1 text=yes dirty=yes notice=none restore_ms=154.832 (M) bound=tracked power=Not charging load1=4.78
c26: dense64 hot-exit session and the drive-5 task-37 journal relaunch with the dirty tab restored
editor_session_test: headless save/repeated/new-target, … allocation guard passed
```

restore_ms values are tracked numbers, no bound asserted (law 1).

## Commands and exit codes

| command | exit |
|---|---|
| `make -j4 all` (gcc release) | 0 |
| `build/tests/editor_session_test` (private XDG_RUNTIME_DIR) | 0 |
| `DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_session_test` (private XDG_RUNTIME_DIR, ASAN detect_leaks=1) | 0 |
| same for `build/san/tests/editor_hot_exit_test` | 0 |
| same for `build/san/tests/editor_checkpoint_test` | 0 |
| `build/san/tests/journal_test` (direct) | 0 |
| harness vs 2c48f9c-era source (red, A+B / B only) | 1 / 1 |
| harness vs this tree | 0 |
| astra5 binary drive / astra6 binary drive | 1 (-103) / 124 (window up, killed by timeout) |

Full `make check` not run (only the four suites the brief names). The release
editor_test G1 guard was not run: no src change.

## What remains

Nothing for the bead. The coordinator may want the in-process red reproduced
from git (`git worktree add` at 2c48f9c, build, link `c26_harness.c`); I used
the mmp worktree's files as the 2c48f9c-era source without running git.

## Out of scope findings (reported, not fixed)

1. **edit-btj retry is broken for every leased launch (main too).** When an
   install fails, `open_editor`'s teardown (`editor_close`:
   `journal_close_discard_empty` + `journal_session_release`) deletes the
   launch's empty journal and its `.session-X.lock`; the retry's
   `editor_session_restore` lease check (`stat(lock_path)` vs the caller's
   lease fd) fails with ENOENT, so `editor_open` returns -103 and main.c exits
   1 instead of opening empty with "session recovery failed; its journal was
   set aside". main.c always leases, so on main any install failure is a
   launch failure (the next launch then opens without the session).
   `install_failure` in editor_session_test does not lease, so it does not
   catch this. Measured on main (instrumented copy): two "open fail line 229
   rc=-103" for one launch. Possible minimal fix: on the `recovery_retry`
   failure path, keep the lease file (e.g. clear `session_lock_owned` before
   `editor_close` so the empty journal and lock survive for the retry). It
   would affect trial use (launch fails once) if any install failure is reachable.
2. **Main's whole-text install fallback still has the 64 MiB ceiling.** When
   the f3i op replay is not taken (`ops_valid` false after an op-log malloc
   failure, origin identity != BASE identity, or an op fails),
   `install_buffer` still does `piece_delete` all + `piece_insert` all into
   a COPY arena of reserve + size, and fails for texts longer than about the
   64 MiB reserve, then hits finding 1. Reached during this bead: a journal
   whose TARGET names a different file generation than its BASE (my first
   case-B rewrite left TARGET on the corpus) gave `editor_open = -103` on main.
   Real-use reachability looks low (TARGET and BASE normally name the same
   file); backlog unless a trace points at it.
3. **Test litter (existing case, not c26):** each `editor_session_test` run
   leaves `build/editor-session-XXXXXX/.sublimite-base-*` (4 B) behind
   (seen after the release run and the sanitizer run; I removed them).
4. Not a defect: a 64 MiB file opens in COPY mode on this box
   (`file_default_copy_threshold()` = min(256 MiB, RAM/32)), so "opened in
   place" in the drive means opened by path, not mmap.
