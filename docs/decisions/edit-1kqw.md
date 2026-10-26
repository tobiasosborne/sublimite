# edit-1kqw — savectl and IPC Windows migration

## Inventory before implementation

(M) Read savectl and every IPC source, their headers and the named suites before
editing production code. `build/worker-logs/inventory-before.txt` records the
pre-edit call sites. The frozen `src/os/os.h` is unchanged.

| Module / call family | Destination |
| --- | --- |
| savectl `open`, `close`, `fstat`, `pread`, `pwrite` | `os_file_open`, `os_file_close`, `os_file_stat`, `os_file_pread`, `os_file_pwrite`; loop on short transfers, internal EINTR retries |
| savectl `openat(O_TMPFILE)` and fallback `getrandom`, `openat(CREATE EXCL NOFOLLOW)`, `unlinkat` | Try `os_file_open_anon` on a pinned `os_dir`; retain the existing fallback through `os_random`, `os_file_open_at(CREATE \| EXCL \| NOFOLLOW \| RDWR)` and `os_unlink_at`. No read access is added to save temporaries (which belong to file). |
| savectl `ioctl(FICLONE)` | `os_file_clone`; unsuccessful clone retains today's copy fallback, including `OS_E_NOTSUP` on Windows |
| savectl `mmap`, `munmap` | owned `os_map`, `os_map_file`, `os_unmap` |
| savectl directory `open`, `fsync`, `close` | `os_dir_open`, `os_dir_sync`, `os_dir_close`; Windows directory sync is the frozen documented no-op; no read-only file is flushed (§9.12) |
| savectl `unlink` retained journal copy | `os_unlink` |
| savectl `nanosleep` | `os_sleep_ns` with the existing journal retry interval |
| savectl `getenv(TMPDIR)` / `/tmp` | `os_temp_dir`; explicit cache directory remains pinned at setup |
| savectl absolute-path and parent parsing | `os_path_is_absolute`; recognize native separators for Windows |
| ipc socket, bind, listen, accept, connect, send/recv, poll, socketpair, fcntl | Linux `ipc_posix.c` verbatim inside its platform guard; Windows named-pipe transport in `ipc_win.c` |
| ipc epoll, timerfd, clock, sleep | Linux platform file; Windows overlapped completion events and request deadlines in one service thread, delivered to the loop through `os_wake` |
| ipc flock, lock/socket file open/stat/chmod/unlink, uid/peer credentials, environment/runtime/display naming | Linux platform file; Windows SID-scoped named mutex and named pipe with user-only ACL and remote clients refused |
| ipc args realpath/stat/lstat/getcwd | Existing Linux path semantics retained; portable `os_path_canonical`, `os_stat_path`, `os_cwd` plus native Windows root/separator parsing where required |
| ipc args read(stdin fd) | POSIX `read` / Windows CRT `_read`: the public contract takes a CRT descriptor, not an `os_file` |
| ipc wire/result | C11, same wire bytes and error values; absolute path validation must accept native Windows absolute paths |

## Integration boundaries discovered

The starting Windows module list has neither file nor journal. `tests/win/core_file.c`
is an explicit W0 backing-refusal shim. This worker does not migrate those modules;
their W2 owners supply the production implementations. Windows savectl tests must
identify cases blocked by these dependencies explicitly rather than claim a save
against a simulated file implementation.

`src/editor/open.c` registers `ipc_server_fd` in epoll, and `src/main.c` closes
`server.lock_fd` after fork. These remain W2.4 work, as required by the brief.

## Transport choice

One service thread owns overlapped pipe I/O for all bounded peers. The UI thread
only drains completed requests and queues replies under a short mutex; it never
waits for pipe I/O. A coalescing `os_wake` announces work to the platform loop.
Callbacks remain on the UI/loop thread. Request deadlines use the existing IPC
constants; close replies have no client deadline after the accepted reply.

Names use the process token's user SID, without lossy hashing. It stays stable
when profile paths or environment variables change. The SID is also
the only trustee in the named objects' ACL. Display and test namespaces are
losslessly encoded with bounded lengths. Windows runtime paths do not select a
filesystem socket. Kernel pipe/mutex objects disappear at the last close,
including process death; there is no stale lock file or socket to unlink.
The first pipe instance refuses an already occupied name; no blind takeover.

## Completion and ownership details

The service uses an I/O completion port so every pipe can have a pending read
and write without exceeding a native wait-handle array. Peer storage is bounded
by the existing client limit plus a listener. Receive slabs retain the existing
small/big pool limits. Cancelling/closing a pipe does not release its slot or
buffer until all completion packets have been reaped. Teardown joins the service
before releasing its arena, mutex, wake and completion port.

The loop drains the wake before locking shared state and re-signals it when its
existing byte/callback budget leaves work. Callbacks run under a recursive mutex
so immediate close reporting works; no pipe wait occurs under this mutex.
Wait-disconnect counters are published to the public server on drain, avoiding
cross-thread access to the public counter. Deadline test settings likewise enter
the service on drain. Complete requests get the unchanged `EDIR` ACK; an accepted
wait request gets the unchanged close result, without a client deadline.

Windows CRT stdin descriptors are temporarily switched to binary mode and their
original mode is restored, including failure paths. This preserves CRLF, NUL and
the CRT text EOF byte. Path parsing uses native absolute drive/UNC roots and
canonicalizes existing prefixes before processing later parent components.
The wire header/version/layout and result values are unchanged; path validation
also admits native absolute Windows strings. The Linux path parser stays intact.

## Test and merge boundary

Windows controller unit fixtures include the production savectl source and
real OS/work/piece code. Their file identity adapter mirrors the current public
`file_id`; source/save/journal operations fail immediately if accidentally used.
They exercise actual NTFS reload copy/mapping/install, lifecycle and Keep.
The full save/journal and Linux syscall-race cases explicitly print SKIP.
When both `file` and `journal` join `WIN_MODULES`, make automatically disables
these adapters and links the same controller tests to production dependencies.
The OS white-box suite excludes the savectl object from its direct object link;
it does not need controller code or its unlanded dependencies.

Coordinator integration: keep all parallel additions to `mk/win.mk`. Align
savectl's `os_stat` → `file_id` conversion with W2.1's shared converter and final
identity shape (this checkout's public ID has volume and the low index only).
Then run production Windows save/journal integration. This worker cannot verify
the parallel implementation from another worktree. W2.4 consumes
`ipc_server_wake` and removes main/editor's descriptor and fork assumptions;
the launcher channel primitives are tested here, the Windows main flow is not.

Linux socket/lock files retain their original lifecycle: inode-checked unlink
on clean shutdown, a lock file that remains, and refused-connect stale socket
recovery on restart. An abstract socket disappears with its last descriptor.
Windows pipe/mutex state is kernel-owned and disappears when the last handle
closes; the process-kill test verifies a new server can bind afterward.

## Coordinator decision (session 22, 2026-10-26)

The file- and journal-dependent cases that SKIP on Windows until edit-pf3l (W2.1) and edit-xt90 (W2.2) land are accepted as such; the shared file-identity conversion is aligned at landing, when this tree rebases over those two, and the production integration is verified by the coordinator's full checks then. The Codex review (docs/reviews/edit-1kqw.md) is triaged before landing.
