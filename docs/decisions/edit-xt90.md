# edit-xt90 — journal on the frozen os.h contract

## Inventory before implementation

The pre-edit call-site scan is `build/worker-logs/inventory-before.txt` (M:
`rg` over every journal C/header file). No implementation was edited before
this inventory. The brief's POSIX-site count is background, not a new count.

| File | Call family | Frozen entry | Migration rule |
|---|---|---|---|
| journal.c | pread; worker pwrite | os_file_pread; os_file_pwrite | Loop over short transfers off path; hooks retain POSIX return semantics. |
| journal.c/cache_write | single pwrite | os_file_pwrite_once | §9.4: one attempt; INTR/error/short/zero -> sticky IO; capture native error immediately. Hook signature stays. |
| journal.c | fdatasync; fsync retained inode | os_file_sync_data; os_file_sync | §9.12: own RDWR transport/copy handles only; no read descriptor is synced. |
| journal.c | parent fsync | os_dir_sync | §3.5: Windows directory durability gap is the contract's documented no-op. |
| journal.c | open/openat, close, ftruncate, fchmod | os_file_open[_at], os_file_close, os_file_truncate, os_file_set_mode | §9.1: directory-as-file follows existing nonregular classification; §9.9: exclusive temp creation. |
| journal.c | fstat/stat, realpath | os_file_stat, os_stat_path, os_path_canonical | §9.3: signed ns; reject negative BASE mtime; retain existing wire identity. |
| journal.c | renameat/renameat2; unlink/unlinkat | os_rename_at(REPLACE/NOREPLACE), os_rename; os_unlink[_at] | §3.2–3.3: open generations survive replacement/unlink. |
| journal.c | directory open, flock(SH\|NB), fcntl dup directory | os_dir_open[_at], os_dir_try_lock(SHARED), separate os_dir_open_at(".") | §9.10: hold object lock from before temp creation through cleanup; no upgrade. |
| journal.c | ioctl(FICLONE) | os_file_clone | NOTSUP -> existing bounded worker copy; keep cancellation and validation. |
| journal.c | mkostemp | os_random + os_file_open(CREATE\|EXCL\|RDWR) | §9.9: retain six-character private temp suffix and exclusive creation. |
| journal.c | clock_gettime, nanosleep, getpid | os_now_ns, os_sleep_ns, os_proc_id | Keep deadlines and inherited delay; name-only PID. |
| retire.c | opendir/fdopendir/readdir/rewinddir/closedir/dirfd | os_dir + os_dir_iter_open/next/close | Pin parent independently of each enumeration. Rescan pinned object. |
| retire.c | dup-then-flock(EX\|NB) directory | os_dir_try_lock(EXCLUSIVE) | §9.10: identity-keyed Windows guard; no lock upgrade. |
| retire.c | openat/flock file; stat/fstatat/lstat | os_file_open_at, os_file_try_lock(EXCLUSIVE), os_file_stat/os_stat_at/os_stat_path | §3.4 lifetime; verify held/named identities before deletion. |
| retire.c | renameat2 NOREPLACE, unlink[_at], fsync | os_rename[_at](NOREPLACE), os_unlink[_at], os_dir_sync | Preserve sweep ordering, set-aside names, and barriers. |
| retire.c | /proc/self/fd anchored scan | journal scan through borrowed os_file | Remove Linux path reconstruction; scan pinned file directly. |
| retire.c | realpath, realtime clock, pread | os_path_canonical, os_wall_ns, os_file_pread | Preserve nanosecond name schema and metadata-only rotation sweeps. |
| session.c | mkostemps, open EXCL, flock, fstat/lstat, unlink | os_random + exclusive open; os_file_try_lock/stat; os_stat_path; os_unlink | Lease held and identity checked before journal appears. |
| session.c | open directory, openat/fdopendir/readdir | os_dir_open + os_dir_iter_* | Absence includes dotted variants; preserve public Linux fd adapters. |
| manual.c | read/write, open/close, stat, fchmod/fsync | os_file_read/write/open/close/stat/set_mode/sync | §9.12: sync only write temporary; §9.1 nonregular checks. |
| manual.c | mkstemp, rename, unlink, parent fsync | exclusive os_file_open, os_rename(REPLACE), os_unlink, os_dir_sync | §9.9 atomic replacement; retain full pre/post identity checks. |
| path.c | getenv, absolute-path test | os_env_get / os_app_dir(OS_APP_DATA) | Preserve XDG/HOME policy on Linux; Windows user-data directory. |
| journal.h | int lease/directory boundary; int callback descriptors | portable handle APIs plus Linux compatibility adapters | §9.10: portable lease duplication uses os_file_dup; editor/savectl callers outside scope are reported. |

Inventory confirms every lock acquisition is nonblocking shared or exclusive
on a newly opened object; no lock upgrade is present (`rg flock` in the raw
scan). Existing crash-loop PAINT, grouped record and checkpoint formats stay
unchanged on Linux. Rotation/retention decisions: edit-52o, edit-dr9,
edit-7cj8; checkpoints: edit-f3i, edit-ze59; crash loop: edit-dzk1.

## Implementation decisions

- Production transport and retained-copy state store `os_file`; parent state
  stores `os_dir`. File and directory synchronization are separate helpers,
  making it impossible to send the directory through `os_file_sync_data`.
  Every data/metadata barrier uses a writable transport or temporary handle.
  No existing read-handle file sync needed to be dropped or reopened (§9.12).
- `cache_write` calls `os_file_pwrite_once` directly when no hook is installed.
  It saves `os_native_error()` immediately on failure, then follows the same
  sticky-IO path for interruption, other failure, zero, short, or invalid hook
  transfer counts. There is no retry, allocation, lock, or worker submission.
  Hook failures still use `errno`; successful short/zero writes still use EIO.
- Legacy `journal_io` signatures remain unchanged. `journal_posix.c` supplies
  borrowed native descriptors for Linux hooks. `journal_win.c` supplies the
  sentinel -1: it is an injection seam, never a truncated HANDLE. Portable
  forwarding hooks keep their own `os_file` in the existing context, as the
  new append-error suite demonstrates. Directory hooks can inject barriers;
  normal directory barriers always use `os_dir_sync`.
- New portable session boundaries are `journal_session_create_file`,
  `journal_session_release_file`, and `journal_session_absent_at`. Linux's
  original descriptor APIs remain adapters in `journal_posix.c`, so existing
  editor/main callers compile and keep their descriptor ownership. A portable
  consumer duplicates the lease with `os_file_dup`; the integration suite
  proves the lock survives closing the creator's handle. The actual
  `F_DUPFD_CLOEXEC` in editor/recovery.c is outside this bead and remains for
  the editor worker, rather than changing editor's public descriptor ABI here.
- Rotation opens independent directory objects for the shared guard and next
  generation's parent. Retirement locks its pinned `os_dir` exclusively;
  directory iteration owns a separate cursor. There are no upgrades. Windows
  uses the frozen identity-keyed external lock-file implementation. The guard
  covers creation through final temp cleanup, including failed publication.
- Acknowledgment scans the already held `os_file` with `journal_scan_open`.
  Its metadata/name rechecks remain on the pinned parent. This replaces the
  Linux `/proc/self/fd` pathname construction and avoids a path-based reopen
  on Windows.
- The private temp helper draws random suffix bytes through `os_random` and
  creates with `OS_OPEN_CREATE | OS_OPEN_EXCL | OS_OPEN_RDWR`. Suffix lengths
  and reserved prefixes retain the existing mkstemp schema (P: six literal
  X characters), including session lock suffixes. It never opens an existing
  collision or truncates somebody else's temporary. All these calls are off
  the typing path. The existing bounded checkpoint collision loop is unchanged.
- `os_file_clone` NOTSUP takes the existing bounded worker-copy path. The
  pre-existing copy fallback on other clone failures (including cross-device)
  is also preserved, rather than changing Linux error behavior. Source,
  retained inode, prefix and final path validations and barriers are intact.
- Journal canonical paths use forward slashes on Windows so existing leaf,
  retention-prefix and persisted-path operations work for drive and UNC paths.
  Input backslashes are normalized only on Windows. Parent splitting preserves
  drive roots; absolute-path validation uses `os_path_is_absolute`. Linux's
  path bytes and journal wire encoding are unchanged.
- Directory-as-file errors retain their previous classification (§9.1):
  ordinary journal open/checkpoint and manual target => INVALID; BASE capture
  => BASE_CHANGED; acknowledgment's verified-source check => BUSY. Replay's
  unclassified open error remains IO. A new regression pins the manual target
  case and existing Linux tests pin open/capture behavior.
- The existing BASE schema contains a volume and a file index of uint64_t
  each (P: existing wire fields). On filesystems with a nonzero high index
  word, capture refuses BASE_CHANGED instead of silently truncating identity.
  All in-memory held/named comparisons use the full `os_identity`. Rotation
  uses its conservative legacy/set-aside spelling for an unrepresentable
  source identity. Ordinary NTFS fixtures pass. A wider persistent BASE schema
  is a separate compatibility decision, outside this mechanical migration.
- The frozen `os_file_close` has no error result. Manual replay historically
  checks its final write-handle close before publishing. It therefore transfers
  that handle with `os_file_to_stdio("r+b")` and checks C11 `fclose`; no bytes
  are buffered through this setup-only bridge. Bridge failure returns IO and
  closes the still-owned handle. This preserves the checked close without
  bypassing os.h or changing its frozen contract; the bridge may allocate off
  path.

## Windows verification shape

`mk/win.mk` includes journal and its required portable dependency modules,
with release and ASan/UBSan objects. The W0.5 worker's concurrent additions
must be merged as a union. The frozen header and OS implementations are not
edited.

`journal_os_test` runs the journal-only file/transport/rotation/retention/
manual-replay/lease/allocator cases on both platforms. Its allocator positive
control requires an active guard outside sanitizers, and its counting campaign
reuses the existing journal_test append count (P: existing fixture, no new
budget). `tests/win/journal_ack_test.c` re-executes the embedded recovery CLI,
checks directory/file contention, invalid and corrupt sources, manual replay,
reserved-source release and orphan BASE reclamation. Hard-link/symlink creation
has no frozen os.h entry; those fixtures print an explicit SKIP.

`journal_hot_exit_test` re-executes itself and is killed by `os_proc_kill`
(SIGKILL / TerminateProcess). It covers cache-only and flushed appends, grouped
BEGIN/DELETE/pre-COMMIT/post-COMMIT with an internal PAD, PAINT, live and dead
rotation guards, failed/missing original set-asides, and mid-copy staging
cleanup. Windows' journal_kill_test runs those boundaries and the inherited
seeded campaign with an independent byte oracle and piece-tree replay. Its
progress instrumentation is a separately opened scratch file instead of
POSIX shared anonymous memory; issued, cached and acknowledged fields are
published separately, and the parent rereads all fields after termination.
It checks `cached <= replayed <= issued` and `acknowledged <= replayed`.

The original editor-dependent group, retirement, editor session/recovery,
and data-loss suites remain Linux tests; Windows check prints their SKIP
reason. The original journal_test also calls the unported file-save module;
Windows runs journal_os_test for its portable journal coverage and states the
file-save fixture skip explicitly. End-to-end editor save/recovery on Windows
belongs to the editor/file/savectl integration beads.
