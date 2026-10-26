# edit-177r.7 — editor filesystem sites on os.h

## Inventory before implementation

(M) Source scan saved in `build/worker-logs/edit-177r.7-inventory.txt`; inspected the named units and their lock consumers before editing production code.

| Unit | Existing calls | os.h entry / disposition |
|---|---|---|
| recovery.c | open, read, pread, close | os_file_open/open_at, os_file_read/pread, os_file_close; retain READ, NOFOLLOW and NONBLOCK flags at the same sites |
| recovery.c | fstat, stat, S_ISREG | os_file_stat, os_stat_path, OS_KIND_FILE; directory-open rejection follows edit-177r.2 §9 |
| recovery.c | flock, fcntl(F_DUPFD_CLOEXEC) | os_file_try_lock(EXCLUSIVE), os_file_dup, preserving shared lease lifetime and named identity validation |
| recovery.c | opendir, readdir, closedir | os_dir_open, os_dir_iter_open/next/close, os_dir_close; discovery opens entries and leases relative to the pinned directory |
| recovery.c | unlink, fsync(directory), realpath, renameat2(NOREPLACE) | os_unlink, os_dir_sync, os_path_canonical, os_rename(NOREPLACE) |
| recovery.c | clock_gettime(REALTIME) | os_wall_ns; preserve the seconds/nanoseconds set-aside suffix |
| recovery.c | poll | Standalone acquisition wait uses os_wake_wait(work_pool_wake), retaining mailbox drain ordering; no event-loop file edited |
| restore.c | open, fstat, stat, pread, close, unlink, fsync(directory) | os_file_open/stat/pread/close, os_stat_path, os_unlink, os_dir_open/sync/close |
| runtime.c | getenv(XDG_CONFIG_HOME/HOME) | os_app_dir(OS_APP_CONFIG), preserving Linux config location |
| runtime.c | open/openat(directory), mkdirat, fsync, close | os_dir_open/open_at, os_mkdir_at, os_dir_sync/close; preserve Linux callback fault seam |
| retain.c | unlink, sched_yield | os_unlink, os_yield |
| session.c | realpath, sched_yield | os_path_canonical, os_yield |

| session.c | pthread_mutex_init/lock/unlock/destroy | os_mutex_init/lock/unlock/destroy; local session graph mutex |

No getpid call exists in these units (M: inventory). The journal resolver `journal_default_dir` already uses os_app_dir(OS_APP_DATA); do not introduce another profile resolver.

## Pending boundaries

The requested os_file fields also have integer consumers in excluded open.c, editor.h and tests. Scope clarification requested before changing those consumers. The runtime sync callback takes a native descriptor; preserve its Linux seam without exposing Windows handles as integers.

## Implementation and integration boundary

- Discovery uses a pinned os_dir plus its independent os_dir_iter; candidate and lock opens are relative to that directory. Retained BASE cleanup validates the canonical parent and unlinks the leaf through an os_dir. File leases retain exclusive flock semantics through os_file_try_lock, and Linux creator leases duplicate with os_file_dup and compare full os_identity.
- Signed mtime_ns replaces timespec ordering with the same pathname tie breaker. Journal path strings retain the journal module's slash schema, including drive and UNC paths; canonical results are normalized only on Windows. Parent splitting preserves drive roots.
- Directory creation opens each component relative to its parent and syncs every parent, including pre-existing names. The native-descriptor callback remains a Linux-only test seam; Windows production uses os_dir_sync (the contract's documented directory-flush no-op) and refuses a non-NULL descriptor callback. Config resolution uses os_app_dir(OS_APP_CONFIG); profile data continues to use journal_default_dir / os_app_dir(OS_APP_DATA), with no second profile resolver.
- The brief prohibits edits to open.c/editor.h and changes to private.h beyond its two fields. Therefore those fields use os_file on Windows but retain int on Linux for existing lifecycle consumers/tests. os_file_fd/from_fd occur only in this explicit Linux compatibility boundary. Windows non-NULL editor_config.session_lease remains unavailable pending its API migration; reject it rather than reinterpret an int as a HANDLE. This is a partial boundary, not a completed creator-lease port.
- editor_restore stores an os_file lease; editor_restore_defer_file transfers its ownership. The old Linux int entry remains a wrapper. Its portable declaration is local in recovery.c until the coordinator can update private.h.
- Windows-only pthread.h includes supply the unrelated unported private.h storage types. The scoped units themselves use os_mutex and os_yield exclusively. The Windows object gate does not link the editor or prove its runtime lifecycle.

## Coordinator handoff needed to remove compatibility boundary

Update editor_config.session_lease to the portable lease API at the same time as main.c's journal_session_create_file handoff. Then make the two private fields os_file on Linux too; open.c must initialize with OS_FILE_INIT, test os_file_is_open, release with journal_session_release_file, and close with os_file_close. Adapt the existing session tests to os_file_stat and os_file_is_open without removing their identity/exclusion assertions. In recovery.c replace the conditional creator branch with os_file_dup from the portable config lease and keep its named-identity validation; remove the Linux field fd conversions. Declare editor_restore_defer_file in private.h, then retire the legacy int wrapper once its consumers migrate. These changes are outside this worker's allowed file list and were not made.

## Final validation and disposition

(M) Linux make all exited 0; full `DISPLAY=:98 DEBUGINFOD_URLS= make -j64 check` exited 0 with the Makefile's `110 test binaries passed` and both shell contracts passed. All sanitizers, including leak detection, remain enabled. The scoped Windows object gate went red (exit 2) then green (exit 0) under the unchanged warning flags. Complete runs and the measured desktop/load stamps are in `docs/worker-reports/edit-177r.7-s13.md`.

Verdict remains partial because the strict file boundary prevents finishing the unconditional field/API migration above. Windows full check separately reports a release clipboard-helper timeout; sanitizer counterpart and isolated release retry pass. No platform or OS implementation was edited to address verification environment failures.
