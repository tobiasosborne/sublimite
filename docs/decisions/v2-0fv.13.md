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
- **Absolute deadlines (ns), with `NEVER` = UINT64_MAX**, unlike the relative
  `os_wake_wait` and `os_proc_wait` timeouts. *Counter-example to relative:* a wait
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
  policy (which uid, the abstract-namespace test hook, the squatter fallback) stays in
  ipc.** *Counter-example to moving the policy down:* the test hook
  `EDIT_IPC_TEST_PEER_UID` and the fallback directory are ipc's endpoint policy, and
  `os` must not read app environment variables on a client's behalf.
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
- `EDIT_IPC_TEST_PEER_UID` is parsed as leading decimal digits that wrap at 32 bits.
  This narrows the test hook: no whitespace or sign prefix. No production path reads it.

## Grants (M)

Counted with `grep -v '^#' tools/system-symbols.txt | awk 'NF{print $2}' | sort | uniq -c`:

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

## Left for other beads (reported, not done)

- **Minimal L5:** make the invariant executable. Ideally the loop is the only UI-thread
  caller of `os_waitset_wait` with a nonzero deadline. A wrap-counting test in the loop
  suite would assert that one step reaches `epoll_wait` with a nonzero timeout exactly
  once, and only from the root set.
- **`os_wake_wait` / `os_proc_wait`** are still separate poll-based waits. Today neither
  has a caller outside os (`work.h` only mentions `os_wake_wait`). The UI loop must
  register `os_wake_fd` in its root set instead. Whether to delete `os_wake_wait` is a
  call for the wave-end deletion review.
- `docs/decisions/v2-he7.22.md`'s counts table and v2-0fv.6's "Reported, not fixed: os has
  no socket surface" are superseded by this file. Neither was edited here.
