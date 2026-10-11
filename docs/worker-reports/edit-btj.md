# edit-btj worker report: recovery journal no longer blocks launch

Branch `wt/edit-btj`. Decision: `docs/decisions/edit-btj.md`.

## Root cause (both sequences, same cause)

`src/editor/recovery.c` `editor_session_restore` (main @ dfeef7e, line 285):
`journal_replay_file(previous, ...)` returns `JOURNAL_BASE_CHANGED` (5) from
`src/journal/journal.c:242` (`check_base_io` before visiting each BASE; identity
= size, mtime, ctime, inode, device, prefix CRC, `journal.c:306`). Restore then
returns `rc ? EDITOR_ERR_IO : 0` (recovery.c:313), `editor_open` fails at
`open.c:214` (`rc = editor_session_restore(e); if (rc) goto fail;`), and the
source is kept on purpose, so the newest journal fails every launch. The
resize, VIEW_STATE/WINDOW v2, DPI sizes and inotify paths were not involved
(the journal never reaches `install_session`).

* **Task 12 (external replacement):** the BASE file's size/bytes changed.
* **Ordinary profile:** dumped `~/.local/share/sublimite/session-tXZIzU` (a copy;
  last write 21:20:15, failing launch 21:20:34). BASEs: id 1 `/tmp/shot-probe.md`
  check 0, id 2 `build/astra2/files/open-fresh.c` check 0, id 3
  `build/astra2/files/typing.txt` (22 B) **check 5 = BASE_CHANGED**. The drive
  harness (`build/astra2/tasks_extra.py:7`, `tasks.py`) rewrites the fixture
  with `write_text` before each task: same 22 bytes, new mtime/ctime. The
  failed launches used the real profile (no XDG_DATA_HOME in the early
  harness; `logs/failed-02-paced-sublimite.editor.log` names
  `/home/tobias/.local/share/sublimite/session-Nl7G2j`).

## Tobias's profile

Copied `~/.local/share/sublimite` (33 session journals) and `~/.config/sublimite`
into a scratch XDG tree (real profile not modified).
* main build (= branch base): `DISPLAY=:99 ... ./build/sublimite --new-instance`
  → `sublimite: error -103`, **exit 1**, no window. **His profile currently
  reproduces the failure.**
* fixed build: window up after 4 s (killed by TERM, 143), three tabs restored
  (shot-probe.md, open-fresh.c, typing.txt), typing.txt shows its disk text,
  bottom row "file changed on disk since this session; its unsaved edits were
  set aside"; `session-tXZIzU.set-aside-1791721671530921647` holds the source.
  Screenshot: scratchpad `real_launch.png`.

## Fix (startup only)

Discovered journals: on replay failure, a tolerant second pass
(`journal_replay_bytes` over the file's bytes) marks buffers with a changed
BASE as `conflict`, skips their INSERT/DELETE, installs them from disk clean;
other buffers restore normally. The source journal is renamed to
`<name>.set-aside-<ns>` (RENAME_NOREPLACE, never deleted) instead of unlinked.
Any other replay error: set aside, open with no restored session. Install
failure: set aside, `editor_open` closes and reopens once without discovery.
Notice on the status row until the first mutation. Typing path: only a
`stats.mutations` compare in compose/overlay; no allocation.

## Tests (tests/editor_session_test.c; it already owns crash-recovery fixtures)

* `changed_base(false)`: type "MK" + second buffer "x", child `_exit`, replace
  the base with different bytes, relaunch: `editor_open == 0`, disk text in tab 0
  clean, tab 1 "xkeep" dirty, notice set, source gone, set-aside contains "MK";
  third launch restores normally with no notice.
* `changed_base(true)`: same with an identical-bytes rewrite (new mtime), the
  ordinary-profile cause.
* `install_failure`: two journalled tabs, relaunch with `tab_capacity = 1`;
  opens with no tabs, notice, set-aside contains "MK". Verified it exercises the
  retry: with the retry disabled the test fails at `editor_open`.
* `session_artifacts` previously asserted the -103 (`editor_open ==
  EDITOR_ERR_IO`); now asserts open succeeds and the source is set aside.

Red (fields added, behaviour unchanged): `session_artifacts` FAIL
`editor_open(...) == 0 && e`, exit 1; `changed_base(false)`/`(true)` both FAIL
at `editor_open(...) == 0 && e` (exit 1).
Green: see final run below.

## Final serial run (release, DISPLAY=:99 via with_display_lock)

Load 7.92 at start (box as is). All exit 0: editor_hot_exit_test, journal_test,
editor_session_test, window_size_test, editor_test, editor_p4_test, cli_test.
Sanitizer (clang ASan/UBSan) editor_session_test and journal_test: exit 0.
Full `make check` was not run.

## Out of scope (not fixed)

* Journals accumulate: Tobias's profile has 32 `session-*` journals from
  2026-10-09/11 (4 KiB to 1.5 MiB). Discovery restores only the newest; the
  rest are never replayed or retired.
* `recover_existing` (journal path is an existing nonempty journal; main.c never
  does this) still fails strictly on a changed base.
* Retained `.sublimite-base-*` generations referenced by a set-aside journal
  are kept indefinitely; there is no tool yet to replay a set-aside journal.
* Same-bytes rewrite (touch, fixture rewrite) is treated as a change, so the
  unsaved edits go aside rather than being reapplied; a full-content check would
  need the base bytes or a whole-file hash in BASE.
* SIGTERM exits 143 without a clean close (seen in the launch probe).
