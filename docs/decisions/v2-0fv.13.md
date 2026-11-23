# v2-0fv.13 · os local stream endpoint and the one wait primitive; ipc moved onto them

Closes the exception v2-0fv.6 reported ("os has no socket surface"). `ipc` now makes no
kernel call of its own: its 29 grants are deleted from `tools/system-symbols.txt`, and `os`
is the only module that imports socket, epoll or timerfd symbols. Both disciplines
(close-on-exec, nonblocking, EINTR, no SIGPIPE) and the error mapping live in one place
and are fault-injected once (`tests/os_endpoint_test.c`, `-Wl,--wrap` beneath os).

## The API

There are two families. Each is sized for what its clients need, not as one function per
syscall.

**Local streams.** `os_stream` and `os_listener` (each `{int fd}`), plus these calls:
`os_listen_local(dir|NULL, name, backlog, &bound)`, `os_listener_accept`,
`os_connect_local(dir|NULL, name)`, `os_stream_pair`, `os_stream_adopt`,
`os_stream_read/peek/write`, `os_stream_peer_uid`, `os_stream_close`, and fd accessors.
`os_user_id()` returns the real uid. A name is either one component inside a pinned
`os_dir` or, with `dir == NULL`, a Linux abstract name.

**Wait set.** `os_waitset` (`{fd, alarm_fd, alarm_ns}`) has these calls: `init/close`,
`add/modify/remove(fd, interest, tag)`, `os_waitset_alarm(deadline)`,
`os_waitset_wait(deadline, events, cap, &n)` and `os_waitset_fd`. Deadlines are absolute
on the `os_now_ns` clock. 0 and any past value poll, and `OS_DEADLINE_NEVER` never
expires. The tag `OS_WAIT_ALARM` (UINT64_MAX) is reserved for the alarm.

Error mapping additions:

- `ECONNREFUSED` maps to the new `OS_E_REFUSED`, which marks a stale socket.
- `EADDRINUSE` maps to `OS_E_EXIST`.
- `EPIPE` and `ECONNRESET` stay `OS_E_IO`.
- `OS_KIND_SOCKET` is new. Before, a socket was `OS_KIND_OTHER`, so a FIFO at the
  endpoint name could not be told apart from a stale socket (see "ipc deltas").

## Invariants

| Object | Invariant |
|---|---|
| `os_stream` | Owns one connected `AF_UNIX` stream descriptor that is close-on-exec and nonblocking, or -1. No call on it waits. `OS_E_AGAIN` means "register and retry". Reads and writes restart EINTR, and writes pass `MSG_NOSIGNAL`, so a closed peer is `OS_E_IO` with native `EPIPE` and never a signal. |
| `os_listener` | Owns one bound and listening socket that is close-on-exec and nonblocking. With a directory name, the inode at the name was pinned with `O_PATH|O_NOFOLLOW`, checked to be a socket owned by `os_user_id()`, and set to 0600 through `/proc/self/fd/<pin>`. Its stat is returned, so the owner unlinks only that inode. A failure after bind unlinks the name only if it still names that inode. |
| `os_stream_adopt` | On every result, `*out` owns the fd. Success means the fd is a socket that is now nonblocking and close-on-exec. A non-socket fails before any flag changes. |
| `os_waitset` | A live set owns an epoll fd and one timerfd (the alarm), registered under the reserved tag. `alarm_ns` is the armed absolute deadline, or `NEVER`. A wait that sees the alarm fire reads its expirations, resets `alarm_ns` to `NEVER` and reports it once. The set's fd is readable exactly when a member is ready or the alarm has fired, so nesting carries both. One thread owns a set. |
| Waiting | `os_waitset_wait` does no I/O on any member. It reports readiness, and the member's owner reads. It returns with `n == 0` only once the deadline has passed. EINTR and early wakes go round with the time left, and finite waits round up to whole ms. |

**The UI-thread invariant this bead fixes in os:** *the UI thread blocks in exactly one
place, `os_waitset_wait` on the loop's root set.* Every other set on that thread is nested
and is only polled with deadline 0 by its owner. `ipc_server_drain` now passes deadline 0,
which is visible in the source. The ipc client and launcher waits run at startup or in the
launcher parent, never in the UI loop. The minimal-L5 bead turns this into a test (below).

## The three clients, designed for now

1. **ipc server (this bead).** It owns a nested set holding the listener (tag 0), one
   peer per token, and its alarm, which is the earliest pre-ACK request deadline (review
   §11). `ipc_server_fd` is the set's fd. The deadline makes that fd readable by itself
   (`t_slot_exhaust`'s "the timer wakes the epoll fd by itself" still holds), so the outer
   loop needs no knowledge of ipc's deadlines.
2. **Wave 1 minimal L5 loop.** It owns the root set. Its members are
   `os_wake_fd(work_pool_wake(..))`, `ipc_server_fd(..)` and, from Wave 2, the platform
   connection fd. One step is: `os_waitset_wait(root, app_next_deadline_ns(), ..)`, then
   the ordered `app_step`. The absolute deadline is PLAN §4.7's single
   `app_next_deadline_ns` without conversion. L5 imports no kernel symbol.
3. **Wave 2 X11 connection.** The x11 module registers `xcb_get_file_descriptor(c)`, an
   int owned by libxcb, in the root set through `os_waitset_add`, which is why `add` takes
   a borrowed int rather than an os handle type. On readiness it drains with
   `xcb_poll_for_event`. The wait never reads the connection. Together with A12 ("no
   UI-thread read from a server connection between ingress and submit"), the only
   UI-thread reads happen at ingress, after the one wait. L3 needs no epoll or poll grant.
   Wayland key repeat is client-side, so it needs a timer. It either nests its own set with
   an alarm, or folds into `app_next_deadline_ns`. Both are covered with no new API.

## Design decisions and the counter-examples that settled them

- **One wait set with one alarm per set, not separate timer objects, and not deadlines only
  as a wait argument.**
  - *Simplest alternative:* no timers at all. Every waiter passes its next deadline to
    `wait`. *Counter-example:* ipc's set is nested in the L5 loop. A deadline passed to
    the inner set is never waited on, because inner sets are only polled, and the outer
    loop would have to know and fold every nested owner's deadlines. Forgetting one means
    requests never expire (review §11 regresses silently). The existing `ipc_test` case
    that waits on `ipc_server_fd` alone for the deadline also defeats it.
  - *Second alternative:* a general `os_timer` object that the caller registers like any
    fd. *Counter-example:* nothing needs two timers in one set. ipc arms the earliest of
    its peers' deadlines, as v1 did, and L5 has one next deadline by PLAN §4.7. Separate
    timers add a type, a registration and an acknowledgement call per client, plus a way
    to forget the acknowledgement, which leaves a timer reported forever (red run 7).
- **Absolute deadlines (ns), with `NEVER` = UINT64_MAX**, unlike the relative timeouts
  of `os_wake_wait` and `os_proc_wait`, which the send-back round deleted. *Counter-example to relative:* a wait
  restarted after EINTR or a stale alarm must recompute the time left from a fixed point.
  The L5 loop also derives an absolute next deadline. Here 0 simply means "already past",
  so polling needs no special value, and `alarm(NEVER)` disarms with the same constant.
- **`os_waitset_add` takes a borrowed int fd, not typed handles.** *Counter-example to
  typed-only registration:* the X11 connection fd belongs to libxcb and has no os handle.
  Typed overloads (`add_stream`, `add_wake`, `add_set`, ...) would need an int escape hatch
  anyway, so there is one entry point.
- **Connect never waits.** v1 and v2-0fv.6 did a nonblocking connect, polled for
  `EINPROGRESS` and read `SO_ERROR`. *(P)* On Linux, a nonblocking `AF_UNIX` stream
  connect either completes or fails inside the call: `unix_stream_connect` returns
  `-EAGAIN` when the backlog is full and never `-EINPROGRESS`. The poll path was dead, so
  `os_connect_local` has no deadline and returns `OS_E_AGAIN` for a full backlog. An
  interrupted connect is retried, and the retry's `EISCONN` counts as success, which is
  the POSIX-correct restart.
- **The filesystem listen pins first, then checks and chmods the pinned inode.** v1 ran
  `fstatat(name)`, then `openat(O_PATH)`, compared the two and chmodded the pin. Pinning
  first removes the stat-to-open window rather than detecting it, and the stat is of the
  object being chmodded. *Simplest alternative:* `fchmodat(dirfd, name)`. *Counter-
  example:* it resolves the name again, so a swap after bind redirects the mode change.
  `fchmod` on an `O_PATH` fd is `EBADF`, so the `/proc/self/fd` path is the way to change
  the mode of a pinned inode.
- **Peer credentials live in os as a primitive (`os_stream_peer_uid`). The ownership
  policy (the peer must be our uid, plus the squatter fallback) stays in ipc.**
  *Counter-example to moving the policy down:* the fallback directory is ipc's endpoint
  policy, and `os` must not choose endpoints on a client's behalf. The policy has no
  override in shipped code (send-back round, below).
- **`os_stream_adopt` owns the fd on every result.** *Counter-example to "on failure the
  fd stays the caller's":* `ipc_launcher_wait` must always consume its fd, and with only
  that contract ipc would need its own `close` import to do it. With unconditional
  ownership, the launcher closes through `os_stream_close`, and `ipc_server_adopt_wait`
  keeps its documented "fd stays the caller's on failure" by forgetting the handle
  without closing it.

## ipc deltas (behaviour, not text)

- Every `errno`, `snprintf`, `strtoul`, `open`, `openat`, `fstat`, `fstatat`, `flock`,
  `unlinkat`, `chmod`, `socket`, `epoll` and `timerfd` call is gone.
  - Names are built by a bounded appender. The `text` helper has the same limits as the
    old `snprintf` checks: a 64-byte leaf and 107 bytes of abstract name with the NUL.
  - The runtime directory is now `os_dir_open` plus `os_stat_at(".")`. It is readable,
    because the directory is ours with mode 0700. Before, it was `O_PATH`.
  - The lock is `os_file_open_at` plus `os_file_try_lock`. The probe distinguishes a stale
    socket by `OS_E_REFUSED`.
- An existing endpoint name must now be `OS_KIND_SOCKET`. v2-0fv.6 had the
  `S_ISSOCK` check, and a plain `OS_KIND_OTHER` test would have let a FIFO at the name
  reach the probe, get `ECONNREFUSED`, and be unlinked.
- The `ipc_server` fields `fd`, `listener`, `lock_fd`, `dir_fd`, `timer_fd`,
  `socket_device` and `socket_inode` became `set`, `listener`, `lock`, `dir` (os handles)
  and `socket_id`. `ipc_rx.timer_ms` was removed, because the set's `alarm_ns` is the one
  copy. Peers hold an `os_stream`.
- **Tests that named a removed internal:** `tests/ipc_test.c` read the raw int
  `s.listener` in three places: two `getsockname` calls and one `< 0` check. Those three
  lines now call `os_listener_fd(&s.listener)`. No assertion changed, and nothing else in
  ipc's test, fuzzer or bench changed.
- An EINTR on the answered-peer peek used to mean "keep the peer". It is now restarted
  inside os. The client's deadline check now happens in the wait, so data already queued
  at the deadline is still read. Both are visible only in races.
- The `ipc_launcher_pair` ends are now nonblocking. The `--wait` launcher waits through
  its own one-member set. The header documents this.
- The send-back round removed `EDIT_IPC_TEST_PEER_UID` (below).

## Grants (M)

These are the counts at the first landing. The send-back section below has the current
ones. Counted with `grep -v '^#' tools/system-symbols.txt | awk 'NF{print $2}' | sort | uniq -c`:

| Scope | Before | After |
|---|---|---|
| `*` | 21 | 21 |
| `os` | 75 | 91 |
| `hl` | 15 | 15 |
| `base` | 5 | 5 |
| `ipc` | 29 | **0** (block deleted) |
| Total lines | 145 | 132 |

`os` gained 16 imports: `accept4 bind chmod connect epoll_create1 epoll_ctl epoll_wait
getsockopt getuid listen recv send socket socketpair timerfd_create timerfd_settime`. The
other 13 deleted ipc imports were either already os grants (`close fcntl flock fstat
fstatat openat read unlinkat poll __errno_location`) or are no longer imported by anyone
(`open __isoc23_strtoul __snprintf_chk`).

`nm -u -P build/rel/src/ipc/*.o` shows the ipc objects import only os, base and `*`
symbols. For `ipc_posix.o` those are `memcpy __memcpy_chk __stack_chk_fail strlen`. The
restricted-symbol fixture's expected diagnostic goes back to "granted only to os"
(`tests/symbols/run.py`).

## Red and green

The suite was written against the API, then each discipline was broken in a build-local
copy of `os_posix.c` (`build/os-endpoint-evidence/red.sh <tag> <sed> <case>`). That copy
is compiled with gcc and the release flags, plus the same wraps.

```text
== red 1: EINTR not retried on recv
os_endpoint_test.c:125: FAIL os_stream_peek(&s, buf, 1, &n) -> OS_E_INTR (native 4), expected OS_OK
== red 2: SIGPIPE raised (no MSG_NOSIGNAL)
os_endpoint_test.c:169: FAIL sigpipes == 0
== red 3: CLOEXEC missing on accept4
fd 5 is not close-on-exec
os_endpoint_test.c:225: FAIL cloexec(fd)
== red 4: wait set does not restart EINTR
os_endpoint_test.c:139: FAIL os_waitset_wait(...) -> OS_E_INTR (native 4), expected OS_OK
== red 5: wait-set alarm timerfd without CLOEXEC
fd 9 is not close-on-exec
== red 6: finite wait rounds down (ends early)
os_endpoint_test.c:356: FAIL n == 0 && os_now_ns() >= deadline
== red 7: alarm not consumed (reported forever)
os_endpoint_test.c:378: FAIL n == 0
```

All seven exited 1. Green is `build/rel/tests/os_endpoint_test` (and `build/san/...`),
which exits 0 with five cases: eintr, sigpipe, cloexec, errors and waitset.

The ipc suite passed unchanged in behaviour on the first build after the port:
`build/san/tests/ipc_test`, and `build/rel/tests/ipc_test` with the live Law-2 guard
inside `drain_ready`, so the drain path through the wait set allocates nothing.

Gates, run on battery (`Discharging`, load 3.12), all exit 0:

```text
make all
make check        check_symbols: ok (6 archives plus LDLIBS) · check-san: 15 test binaries passed ·
                  check-law2: 3 release suites passed · check: ok
make check-tsan   check-tsan: 5 threaded suites passed
make fuzz-smoke   fuzz-smoke: 6 fuzzers replayed their seeds
```

`tests/os_endpoint_test.c` is registered in `SAN_SUITES`, which also builds the release
binary. Its wraps are `OS_ENDPOINT_TEST_WRAPS` in the Makefile. It is not in TSan because
it starts no threads. The Law-1 fuzz exemption of v2-0fv.1 still holds, because the
endpoint layer parses nothing.

## Send-back round (advisor, after landing as fcb1f9c)

The advisor raised three items that round 1 had left as notes. Notes are not tracking
(Law 7), so all three are now done.

### 1. One wait primitive: `os_wake_wait` and child processes deleted

No landed module and no filed bead calls `os_wake_wait`, `os_proc_spawn`,
`os_proc_kill` or `os_proc_wait`. Callers before this round:

- `os_wake_wait`: `tests/os_test.c`, `tests/work_test.c` and `bench/work_bench.c`.
- The process calls: `tests/os_test.c` and the landed `tests/os_fault_test.c`.
- `work.h` named `os_wake_wait` only in a comment.

The process calls all go, because spawn and kill without a reaper would leave a
zombie-producing API with no client. The process types (`os_proc`,
`os_proc_how`, `os_proc_status`) go with them. `os_proc_id` and `os_proc_self_path`
stay, because they are not waits and are outside this round's scope. No module outside os
calls them either: only the os suites do, and the fault suite needs the second to re-exec
itself. That is reported below for the deletion review.

The bead that needs one of these adds it together with its client. Candidates:

- Wave 2 font discovery.
- The minimal-L5 bead's `--wait` launcher `fork`. `ipc.h` documents that main forks.
- Its subprocess tests of the built binary (wave1-L0-harvest.md §5.3). Those can spawn
  from the test, as the os suites now do.

Where each former caller went:

| Former caller | Now |
|---|---|
| `work_test.c` (6 calls) | Its own `wake_wait`: a one-member `os_waitset` over `os_wake_fd(work_pool_wake(..))`. |
| `work_bench.c` (3 calls) | The same wait, but on one set kept for the bench's lifetime, so the foreground sample does not include creating a set. |
| `os_test.c` `case_wake` | Checks the same contract through the wait set: starts clear, finite wait lasts its timeout, signals coalesce, waiting does not drain, one drain clears, another thread wakes a blocked waiter, a closed wake is refused (`OS_E_INVAL` on add). |
| `os_test.c` `case_proc` | Keeps self-path, process id, "a lock dies with its holder" and the descriptor-limit raise. The children are forked by the harness. The exit-code, kill, timeout, argv-bytes and descriptor-inheritance cases tested the deleted spawn and went with it. |
| `os_fault_test.c` | Spawns its case children with `posix_spawn` and `addclosefrom_np(3)` in the test, which is the same environment `os_proc_spawn` gave. |
| `work.h` | Now says: add `os_wake_fd` to your `os_waitset`. |

Neither work nor ipc ever called `os_wake_wait`. The L5 loop gets the pool's wake through
its root set.

**Grants:** os falls from 101 to 93 on the current tree. It was 91 at fcb1f9c, then
v2-0fv.2's fault service added 10. The 8 grants removed are `environ kill poll
posix_spawn posix_spawn_file_actions_{init,destroy,addclosefrom_np} waitpid`. `nm -u
-P` of the release `os_posix.o` imports none of them, and no grant is left without a
matching import. `os` now imports no `poll`.

### 2. `EDIT_IPC_TEST_PEER_UID` removed from shipped code; test-only seam

`own_peer` is now `os_stream_peer_uid(s) == os_user_id()` on every endpoint, with no
environment read. The tests reach that decision through a link-time seam:

- `tests/ipc_peer_seam.h` defines `__wrap_os_stream_peer_uid`.
- The Makefile adds `-Wl,--wrap=os_stream_peer_uid` (`IPC_PEER_SEAM`) only to the
  `ipc_test` links (rel, san, tsan) and to `ipc_fuzz`.
- While `ipc_seam_foreign_abstract` is set, a stream whose endpoint is an abstract name
  reports uid + 1. The wrap identifies that from its own `getpeername` and `getsockname`.

This keeps the old hook's abstract-only scope, so the squatter case still falls back to
the private directory. No `EDIT_*` name was added.

Shipped-code evidence (`build/os-endpoint-evidence/r2-shipped.log`):

```text
strings -a <f> | grep -c PEER_UID
  build/rel/src/ipc/{args,ipc_posix,result,wire}.o: 0   build/rel/libcore.a: 0   build/sublimite-v2: 0
nm -u -P build/rel/src/ipc/ipc_posix.o  (non-project)   memcpy __memcpy_chk __stack_chk_fail strlen
nm build/sublimite-v2 | grep -c '__wrap_\|__real_'     0
nm build/rel/tests/ipc_test                             T __wrap_os_stream_peer_uid
```

`build/sublimite-v2` does not link ipc yet, because the minimal L5 does not exist, so
the object and `libcore.a` lines are the ones that decide. Two `EDIT_IPC_*` strings
remain in `ipc_posix.o`. `EDIT_IPC_NAMESPACE` changes only the abstract name.
`EDIT_IPC_FALLBACK_DIR` changes only where the fallback endpoint lives. Neither one
changes whom the server accepts. Both were reported to the coordinator for triage and
were not changed here.

Red and green:

| Run | Build | Result |
|---|---|---|
| Red, shipped read removed first | The old test, still setting the env var | `client foreign-server rc=6` / `FAIL tests/ipc_test.c:525: rc==IPC_REJECTED`, exit 1 |
| Red, seam forced foreign | Client accepts any uid (`own_peer` without `uid==os_user_id()`) | `FAIL tests/ipc_test.c:525: rc==IPC_REJECTED`, exit 1 |
| Red, seam forced foreign | Server accept skips `own_peer` | `FAIL tests/ipc_test.c:529: WIFEXITED(st) && WEXITSTATUS(st)==0` (the foreign client got an answer), exit 1 |
| Green | Current tree: `build/san/tests/ipc_test foreign_cred` and the full suite | exit 0 |

The two seam reds are build-local copies of `ipc_posix.c` linked against
`build/rel/tests/ipc_test.o` and `libcore.a`, with the wrap. The fuzzer's foreign
part 4 uses the same seam.

### 3. Stale decision files corrected

- `v2-he7.22.md`: the counts table now has the at-landing and current columns, with the
  changing bead per row. The kernel-granted module list now names `base`.
- `v2-0fv.6.md`: "os has no socket surface" is marked resolved by this bead, and the
  peer-uid hook text is marked superseded.
- Also corrected, in the same spirit:
  - `v2-0fv.5.md` pointed clients at `os_wake_wait`.
  - `v2-0fv.1.md` gained an amendment note for the deleted process and wake-wait rows.

## Reported for the coordinator (not done here; to file or triage)

- **Minimal-L5 acceptance line:** one `app_step` reaches a blocking wait exactly once,
  and only on the root set. Test it by wrapping `epoll_wait` in the loop suite and
  counting nonzero timeouts.
- **Shipped env hooks** `EDIT_IPC_NAMESPACE` and `EDIT_IPC_FALLBACK_DIR` (above).
- **`os_proc_id` and `os_proc_self_path`** have no caller outside os (wave-end deletion
  review).
