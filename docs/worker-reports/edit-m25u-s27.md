# edit-m25u, session 27 worker report (Opus, ThinkPad, display :98)

Decision: `docs/decisions/edit-m25u.md`.

## 1. The second per-file descriptor

It is the save controller's pinned **reload-cache directory**: `os_dir_open($TMPDIR or /tmp)` in `savectl_create`, one per tab (M: `/proc/PID/fd` showed `-> /tmp`; fdinfo flags 02300000 = O_RDONLY|O_DIRECTORY|O_LARGEFILE|O_CLOEXEC, same inode as /tmp). It exists for edit-ck4's reload fallback: when the source's parent cannot hold an O_TMPFILE, reload backing goes through a cache directory descriptor retained at setup, so create and cleanup use one directory object and the path is never resolved again.

The descriptor can be shared safely. It is only read (`const os_dir *` openat-family calls), and the setup pin is kept. Save temporaries (file module, target directory) and watches (`file_watches`, one inotify fd) are not involved.

**Changed:** I added `savectl_cache`, a reference-counted pinned directory (`savectl_cache_open` / `savectl_cache_release`), and an optional `savectl_options.reload_cache`. The editor opens one in `open_editor`, passes it at all three `savectl_create` sites and releases its reference in `editor_close`. Each controller holds its own reference, so teardown order does not matter. If the editor's open fails, or a caller has no cache, the controller pins its own, as before.

Result (M): 31 opens held 31 fds, not 62. A 600-file instance held 631 fds, one of them on /tmp.

## 2. rlimit

I added `os_fd_limit_raise(uint64_t *soft)`. It is POSIX-only in os.h (the `os_dir_fd` pattern), implemented in os_posix.c and called once at the top of `src/main.c` with the result ignored and no output.

- It raises the soft limit to the hard limit and never lowers it.
- **Bound:** if the hard limit is RLIM_INFINITY or above `/proc/sys/fs/nr_open`, the target is nr_open, because setrlimit gives EPERM above it. If that file is unreadable, the target is 1048576 = 1024*1024 (P: the kernel default `sysctl_nr_open`, fs/file.c).
- No other cap: nothing in src/ or vendor/ uses select/FD_SET or loops to _SC_OPEN_MAX (grep).
- Windows needs nothing and gets nothing.

## 3. EMFILE handling (state before the change, measured at soft 64)

- Open 17 was accepted with only one fd: `savectl_create` hit EMFILE, `editor_save_setup`'s error went to `b->save_error`, and the tab installed anyway, so it **could not save**. That is half state.
- Open 18 and later were refused whole (no tab, no fd kept). The client printed "running instance rejected the open request", and the **editor showed nothing**, so a desktop launch failed silently.

**Fixed:**
- Sharing the cache takes the descriptor out of controller setup, so the boundary open no longer half-installs.
- `editor_buffer_prepare` records EMFILE/ENFILE, taken from the file worker's errno or the synchronous errno.
- `editor_open_request`, after its whole-request rollback, sets the notice "open refused: too many open files" once. Repeated refusals see the same pointer and change nothing.
- The IPC client still gets IPC_REJECTED.

## Files touched

- src/os/os.h
- src/os/os_posix.c
- src/main.c
- src/savectl/savectl.h
- src/savectl/savectl.c
- src/editor/private.h
- src/editor/open.c
- src/editor/buffers.c
- src/editor/session.c
- src/editor/commands.c
- tests/ipc_test.c (new cases fd_limit_startup, fd_per_file, fd_exhausted)
- docs/decisions/edit-m25u.md
- this report

## Red (before the implementation; `DISPLAY=:98 ASAN_OPTIONS=detect_leaks=1 build/san/tests/ipc_test <case>`)

```
FAIL tests/ipc_test.c:431: state.nofile_soft == state.nofile_hard
ipc_test: edit-m25u startup soft RLIMIT_NOFILE 256 -> 256 (hard 1048576)
fd_limit_startup exit 1
FAIL tests/ipc_test.c:468: after - before == count - 1u
ipc_test: edit-m25u 31 opens held 62 descriptors
fd_per_file exit 1
FAIL tests/ipc_test.c:505: e->buffer->save_error == 0 && editor_get_stats(e).tabs == tabs + 1u
FAIL tests/ipc_test.c:347: waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0
fd_exhausted exit 1
```

fd_limit_startup runs the real `src/main.c` through ipc_test's `ipc_cli_main`, in a forked child whose soft limit is lowered to 256. The child exits, so the parent's limit is never changed. fd_exhausted's red is the half-state tab.

## Green

```
ipc_test: edit-m25u startup soft RLIMIT_NOFILE 256 -> 1048576 (hard 1048576)
fd_limit_startup exit 0
ipc_test: edit-m25u 31 opens held 31 descriptors
fd_per_file exit 0
ipc_test: edit-m25u limit 15: 6 accepted, 18 refused, notice "open refused: too many open files"
exit 0
```

Full suites with ASAN_OPTIONS=detect_leaks=1, all exit 0:
- ipc_test, savectl_test, savectl_reload_test, os_test (17 cases, 0 failed)
- on :98 via with_display_lock: editor_session_test, cli_test, editor_large_test

## Manual check ([AC] Charging, load avg 4.45)

I started build/sublimite on :98 with EDIT_REFUSE_REAL_DISPLAY=1 and private XDG dirs under `mktemp -d`, from a shell with `ulimit -Sn 1024`.

- `/proc/PID/limits`: `Max open files 1048576 1048576`.
- 600 sequential client opens of small files: **ok=600 bad=0**. fds went from 31 idle to 631, with exactly one `-> /tmp`.
- I stopped only my own pid.

Extra run with `ulimit -n 64` (hard limit too, so it cannot be raised): 32 accepted, 28 rejected with the existing client message, instance alive at 63 fds.

## Verification

- `make -j4 all`: exit 0.
- `DISPLAY=:98 make -j4 check`: **exit 0**. Summary line: `check: 117 test binaries passed in 162.4 s (parallel, 12 jobs, 6 displays)` ([AC] Charging; uptime load 4.45/6.68/5.99 at start).
- One `check: FLAKY editor_large_test`: :1172 `trace_now_ns() < deadline`, a 5 s first-frame deadline under three-lane load. It passed on the rerun and again alone on :98.
- `tools/test_display_lock.sh contract` printed SKIP: it uses :99, which another lane held.

## Found outside scope (not fixed)

1. **ipc_posix.c `ipc_server_drain`**: `accept4` failing with EMFILE/ENFILE returns IPC_IO, which `editor_poll_sources` turns into EDITOR_ERR_IO, which ends the editor loop. Found by reading the code; not reproduced (10 concurrent clients at the limit were all rejected cleanly). Simply ignoring the error would spin on the level-triggered listener; the fix needs a reserve fd or a backoff.
2. **`editor_open_request` still installs a tab when `editor_save_setup` fails** for reasons other than descriptors (calloc, canonical path, or a per-controller cache open when the editor's shared cache failed at startup). That half state is still possible, just no longer from fd exhaustion on the boundary open.
3. **commands.c:113 `(void)savectl_destroy(a->writer)`** ignores BUSY on the Save As error path. The controller would leak; this predates the bead.
4. **Windows not built here** (no mingw on this box). The savectl and editor changes are portable C11 with stdatomic; `make TARGET=win` should be run on Spiral.
5. The bead's admission-control idea (coalescing IPC open bursts beyond tab capacity) was not pursued. The existing IPC_LIMIT on capacity stands.
