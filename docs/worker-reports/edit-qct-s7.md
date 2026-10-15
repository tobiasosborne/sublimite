# edit-qct worker report (session s7)

Bead: edit-qct [BUG] P2, edit-fm4 part 2. Orphan journals that startup defers
now appear as recoverable closed tabs and are restored lazily on reopen.
Design: docs/decisions/edit-qct.md.

## Files touched

- `src/editor/restore.c` (new)
  - placeholder creation (`editor_restore_defer`);
  - WORK_BULK scan into a BASE-relative program, with whole-text trim;
  - mailbox completion;
  - apply at attach;
  - install (tab swap, checkpoint, source retirement);
  - failure path.
- `src/editor/private.h`: buffer fields `placeholder`, `restore`, `restore_apply`; editor fields `recovery_placeholder_ids`, `restore_active`, `restore_prepare`; the restore prototypes; `editor_install_buffer`, `editor_unpoll_buffer`, `editor_tab_title`.
- `src/editor/buffers.c`
  - `editor_tab_title` factored out of `install` (behaviour unchanged);
  - apply hooks in `editor_buffer_prepare` (non-pending) and `editor_adopt_open` (pending);
  - the reopen hook in `editor_reopen_tab`;
  - `editor_restore_free` in `editor_buffer_destroy`;
  - two exported wrappers.
- `src/editor/editor.c`: placeholder keys are parked (`editor_inject`, `queue_pick`); `editor_restore_receive` in `editor_route_work`; `editor_restore_service` in `editor_step`.
- `src/editor/checkpoint.c`
  - placeholders are skipped in `build_checkpoint` and in the metadata VIEW records;
  - `recorded_tabs` writes TABS without placeholders (checkpoint and metadata);
  - the guard that writes nothing before the first install counts placeholder ids.
- `src/editor/session.c`: `publish` skips placeholders.
- `src/editor/input.c`: Ctrl+S on a placeholder is ignored.
- `src/editor/recovery.c`: fm4's deferral branch creates the placeholder and hands it the lease. When the pool is full, the remainder stays counted only and the notice says so.
- `tests/editor_session_test.c`:
  - new `large_restore` (with `qct_*` helpers);
  - `large_orphans` now asserts exactly one closed tab named "corpus";
  - a `qct_conflict` reopen phase (changed BASE: disk text, clean, journal set aside).
- `src/editor/STATUS.md`, `docs/decisions/edit-qct.md`, this report.

## Red run (test first, before any implementation)

The only implementation present was the `bool placeholder` stub field, so the test could compile.

`build/san/tests/editor_session_test`, 2026-10-16 05:44 AEDT, BAT0 "Not charging", load 8.12/7.85/4.95. `df -h /` before and after: 97G free.

```
large_restore: orphan journal bytes=4096 (M)
editor_session_test:961: FAIL tabs_count(&e->tabs) == 0 && tabs_closed_count(&e->tabs) == 1
editor_session_test:990: FAIL rc == 0
editor_session_test:1057: FAIL large_restore() == 0
exit=1
```

The fixture was removed on the failure path: no `build/editor-qct-*` was left behind.

## Green runs

The fixture is a 1 GiB sparse corpus ((P) 2^30, fm4's size) and a post-28u orphan of 4096 B (M). The `large_orphans` fixture is unchanged: a 1 GiB whole-text orphan plus a set-aside copy, both real bytes.

`df -h /`, before → after each GB run:

| time | before | after |
|---|---|---|
| 05:46 | 97G | 97G |
| 05:47 | 97G | 97G |
| 05:57 | 90G | 90G |
| 05:59 | 90G | 90G |

The drop between 05:47 and 05:57 is not from this worktree: `du -sh build` = 882M, and no fixture directory was left.

| run | stamp | result | tracked prints (M) |
|---|---|---|---|
| ASan direct | 05:47, Not charging, load1 9.40 | exit 0, 12.16 s | restore_ms=19.807, steps=25, worst_step_ms=15.007; conflict reopen 971 ms |
| ASan, `DISPLAY=:99 sh tools/with_display_lock.sh` | 05:57, Not charging, load1 4.01 | exit 0, 12.86 s | restore_ms=37.200, worst_step_ms=30.352; conflict 669 ms |
| release `build/tests/editor_session_test` | 05:58, load1 4.37 | exit 0, 11.88 s | restore_ms=30.748, worst_step_ms=11.124; conflict 587 ms |
| ASan with display lock (after the final restore.c edits) | 05:59, load1 12.21 | exit 0, 18.01 s | restore_ms=34.876, worst_step_ms=18.549; conflict 1816 ms |

Every green run asserts:
- exactly one closed tab, title `corpus`, path ending in `/corpus`;
- the placeholder writes no BASE into a checkpoint (`journal_scan_file` counts 0);
- after a clean exit the orphan keeps the same inode, size and mtime;
- after reopen, a single tab `corpus`, modified, with `piece_len == 2^30 + 1` and bytes `'x', 0`;
- no copy of BASE size: `file_open_mode == FILE_MODE_MMAP` and buffer arena = 67108864 B, the configured reserve. COPY acquisition would add the 1 GiB file size (buffers.c prepare);
- the launch journal after checkpoint is 4096 B (M), i.e. 28u differences;
- the orphan and its lock name are gone.

UI blocking is reported as worst_step_ms against EDITOR_IDLE_NS = 10000 ms. It is a tracked print, not an assertion. The worst step is the install step: the tab swap, `journal_flush`, the checkpoint rotate and the directory fsync.

Conflict phase (`large_orphans`, changed BASE): reopen gives disk text starting `changed`, `piece_len == 2^30`, clean, the "set aside" notice, and the orphan renamed aside.

## Other suites (all exit 0)

Built with `make -j4 all` (gcc 13 release, -Werror): exit 0 (re-run after the final edits: exit 0).

ASan/UBSan, 05:49-05:52, BAT0 Not charging, load1 15.1 → 7.5:

| suite | how it was run | wall (M) |
|---|---|---|
| editor_checkpoint_test | direct | 4.09 s |
| journal_test | direct | 22.09 s |
| journal_retire_test | direct | 1.18 s |
| editor_hot_exit_test | display lock | 20.94 s |
| editor_close_test | display lock | 0.19 s |
| editor_test | display lock | 107.69 s |

Release editor_test malloc guard, as tracked before/after prints. "Before" is the main checkout's binary `/home/tobias/Projects/sublimite/build/tests/editor_test`, built 01:54 from main. Both ran under the display lock, Not charging, load1 7.49 / 4.10. Both exit 0, with identical guard lines:

```
edit-g11 null typing: ... mallocs=0
clipboard malloc guard: 0 allocations (active)
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=active
editor_test: requested=gl actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: requested=raster actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: null 10000 keys mallocs=0 guard=active
```

The final restore.c-only edits (the in-flight counter and the VLA turned into calloc) came after the editor_test runs. They are not on the typing path.

Not run: dataloss_test (built, not run, to keep the shared box free) and the other display suites (this bead does not touch gl, x11 or raster).

## What remains

See the decision record, "Known limits":
- A whole-text (pre-28u) journal whose text differs from BASE at both ends is still copied in its differing middle on the UI thread. The trim path has no test: a whole-text fixture with an unchanged BASE costs another 1 GiB written journal.
- Large untitled buffers in a deferred source are inserted on the UI thread.
- There is no idle-time trigger, by choice.
- Retained `.sublimite-base-*` originals named by a restored source are not retired.
- If a restore job cannot be submitted (work pool full), the placeholder stays inert until it is closed and reopened.

## Found, not fixed (out of scope)

- `editor_session_test` leaves one `build/editor-session-XXXXXX/` directory holding a 4-byte `.sublimite-base-*` per run. The main checkout's own binary (built 01:54) does the same (verified 05:58), so it is not from this bead. Some case leaks a retained base and its fixture directory.
- edit-f3i was not needed for the lazy path: the restored buffer's origin is the BASE generation, so its checkpoints are differences. The eager path (`editor_add_recovered_buffer` in recovery.c) still leaves origin invalid, as f3i says.
- The fm4 tracked print and my new one read BAT0 with `%31s`, so "Not charging" prints as `power=Not`. Cosmetic.
