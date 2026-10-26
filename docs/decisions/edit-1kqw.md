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

## Round 2: review findings → change → test

Finding numbers refer to `docs/reviews/edit-1kqw.md`; evidence and the remaining
dependency blockers are in `docs/worker-reports/edit-1kqw-s12-r2.md`.
The generation-publication protocol below supersedes the earlier fixed-name
Windows transport description.

| Finding | Change | Test |
| --- | --- | --- |
| 1 — startup publication | The service thread owns the SID/namespace mutex until shutdown. A contender reports EXISTS only after opening an authenticated published listener; otherwise it waits within the existing lock deadline. | `ipc_transport_test --startup`: separate processes, pre-listener barrier, handoff and killed-initializer takeover. Red then green. |
| 2 — stale client-held pipe | A protected mapping publishes a random generation-specific name and owner PID. A replacement creates its first listener under a fresh name instead of reusing the client-held name. | `ipc_transport_test --orphan`: retain a raw client across owner kill, rebind and ACK a fresh request before closing it. Round 1 already passed on this host; no native red is claimed. |
| 3 — launcher handoff | `ipc_launcher_prepare` returns a non-inheritable listener and endpoint string. Pass the string in argv; `ipc_server_connect_wait` adopts a child-local endpoint. Frozen process inheritance stays unchanged. | `ipc_win_test --launcher-cases`: separate launcher/UI processes, primary and isolated ownership, UI disconnect. Missing-symbol red, native green. |
| 4 — CRT descriptors | Scoped thread-local invalid-parameter handlers guard handle lookup, stdin mode and reads, restoring the caller's handler. | `ipc_win_test --invalid-cases`; `ipc_descriptor_test` also injects the documented callback, which this host's CRT does not consistently invoke. Round-1 callback red, guarded green; stdin mode restored after I/O failure and LIMIT. |
| 5 — client impersonation | Every production client open requests `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`. | `ipc_transport_test --sqos`: inspect primary and launcher client opens. Red then green. |
| 6 — launcher ACL | Both launcher constructors use the SID-only protected descriptor and reject remote clients, with non-inheritable handles and peer authentication. | `ipc_transport_test --security`: inspect actual ACLs and refuse restricted-token read and duplex opens. Red then green. |
| 7 — trailing requests | Peek buffered bytes before READY and again immediately before the callback. Reply IPC_PROTOCOL without a callback; no pending extra-byte read consumes the evidence first. | `ipc_transport_test --trailers`: frame plus garbage/second frame, service mutex held while UI drains. Red then green. |
| 8 — reload permissions | On EACCES a POSIX directory reference may use O_PATH for relative anonymous creation. Sync/lock must reopen the pinned directory readably before use. | `savectl_test --r2-access`: unprivileged write/search-only source parent, unwritable cache and old O_TMPFILE positive control. Red then green. |
| 9 — temporary ownership | Exclusive creation avoids fallible post-create classification on either OS. A named reload creation retains its unlink obligation; failed unlink retries on worker maintenance and prevents premature destruction. | `savectl_test --r2-post-create`, `--r2-unlink`, `--r2-collision`: injected failures, no owned residue, foreign collision preserved. First two red then green; collision already green. |
| 10 — production save coverage | Conditional real-dependency cases assert sharing/BUSY preservation and cleanup, then pause at the fsynced-before-rename seam and replace an open/mapped target. `check-savectl-production` refuses a dependency SKIP. | Body compiles with REAL_DEPS. Integration target remains red: this worktree lacks Windows file/journal. Production green awaits dependency integration. |
| 11 — complete identity | Reload compares intact `os_stat.id` values at descriptor/path and before/after boundaries. The legacy public baseline cannot store the high word. | `savectl_identity_test`: high-word-only changes, red then green. Complete baseline/converter integration awaits W2.1; finding only partially closed. |

### Publication and crash recovery protocol

The named mutex supplies exclusion through actual ownership, not existence. The
service thread acquires it, including WAIT_ABANDONED after a crash. While another
thread owns it, an initializer tries the published generation and returns EXISTS
only after SID and published-PID authentication. If no listener opens, it retries
against the existing absolute lock deadline, or acquires the abandoned mutex and
initializes a replacement.

Only the owner writes the SID-protected named mapping. It clears readiness,
generates a random pipe suffix, creates the first overlapped listener with the
SID-only ACL and remote rejection, then publishes name/PID and readiness last.
Clients read readiness around their snapshot, open with restricted SQOS, and
authenticate the server SID and PID before sending. Missing or busy publication
during startup is retried within the caller's absolute deadline.

A crashed owner's old pipe can remain through a surviving client and return
BUSY or ACCESS_DENIED if reused. Replacements do not classify that as a live
owner or attempt to reclaim that name: the authenticated rendezvous selects a
fresh generation under exclusive ownership. Old generations disappear at last
handle close. Normal teardown clears readiness before releasing the mutex.
No arbitrary handle inheritance is added.

### OS edits and identity integration boundary

The inherited `os_posix.c` edits address findings 8 and 9; the inherited
`os_win.c` edit addresses finding 9. Retention requires the OS suites to remain
green, as recorded in the report. `os.h` stays frozen. Reopening an O_PATH
reference for sync/lock can still fail with EACCES; it never reports successful
durability on failure or substitutes a pathname for the pinned object.

This checkout's `file_id` has only `dev` and `ino`; it cannot hold a volume plus
the entire Windows FileId. This worker does not change `src/file`. W2.1's shared
conversion must preserve `os_stat.id.volume`, `index[0]` and `index[1]` without
hashing or dropping a word, and use the same timestamp/metadata encoding for
descriptor and path identities. Controller baselines, rebases, source validation
and the fixture adapter must then use that representation. The native reload
guards prevent high-word-only substitution during reload; complete baseline and
production save acceptance remain open until the dependency is integrated.

## Coordinator decisions after round 2 (session 22 wind-up, 2026-10-26)

1. The round-2 edits to `src/os/os_posix.c` and `os_win.c` stay: the frozen header is untouched and `tests/os_test.c` (17/17) and `tests/win/os_win_test.c` pass in the same `make TARGET=win check`; the Linux os suite passes in the full check.
2. Finding 2's crash coverage (a crashed server's pipe name held by a surviving client) is accepted on the deterministic test without a reproduced native red; noted on backlog edit-l3qd.
3. Landing order: after edit-pf3l (W2.1) lands, rebase this tree over it, align the Windows file-identity conversion with W2.1's, run the production-save gate the report names, verify, land.
