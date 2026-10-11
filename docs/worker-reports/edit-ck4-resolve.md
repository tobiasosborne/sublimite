# edit-ck4 conflict resolution onto main

Resolved the three conflict hunks in `src/editor/buffers.c` and
`tests/editor_test.c`. No `git` or `bd` commands were run; rebase bookkeeping
is left to the caller.

The brief's `docs/worker-reports/edit-ck4.md` is absent from this worktree.
Reviewed the available `edit-ck4-s9.md`, `edit-ck4-s2-final.md`,
`edit-ck4-fix-final.md`, the continuation in `docs/decisions/edit-ck4.md`, and
the save/session/recovery implementation instead.

## Resolved hunks

1. `src/editor/buffers.c`, `editor_add_buffer()` (original conflict at line 375):
   retain branch save-controller initialization, per-buffer save error/errno
   reporting, and session metadata publication after successful adoption.
   Retain main's `unpoll_source(e, b)` before destroying an unpublished buffer
   on failure.
2. `src/editor/buffers.c`, `editor_check_sources()` reload commit (original
   conflict at line 779): retain branch removal of the retired generation from
   the session graph and save-controller initialization for the replacement.
   Use main's `unpoll_source(e, b)` for old-buffer watch cleanup. The helper
   preserves the shared inotify fd's epoll registration while another buffer
   uses it, including the newly installed replacement.
3. `tests/editor_test.c`, default suite (original conflict at line 1998): run
   main's `minimap_typing_flash()` and branch `ck4_recovery(0)` /
   `ck4_startup()`. Preserve `--minimap-flash`, all ck4 argument dispatch, and
   the other main/branch test additions already merged outside the markers.

No production files besides `src/editor/buffers.c` were edited.

## Validation

`make all` exited **0**. The requested sanitizer target build also exited **0**:

```sh
make build/san/tests/editor_test build/san/tests/journal_test build/san/tests/savectl_test build/san/tests/file_test
```

Both configurations compile with `-std=c11 -Wall -Wextra -Werror -Wshadow
-Wconversion`. Release uses GCC; sanitizer uses Clang with
`-fsanitize=address,undefined -fno-omit-frame-pointer`. Neither build emitted
warnings or errors. Raw build transcripts:
[make-all.log](../../build/ck4-resolve-logs/make-all.log),
[make-san.log](../../build/ck4-resolve-logs/make-san.log).

Actual compilation output for the resolved files:

```text
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc  -MMD -MP -c src/editor/buffers.c -o build/rel/src/editor/buffers.o
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -include tests/display_guard.h -MMD -MP -c tests/editor_test.c -o build/rel/tests/editor_test.o
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -include tests/display_guard.h -MMD -MP -c tests/editor_test.c -o build/san/tests/editor_test.o
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc  -MMD -MP -c src/editor/buffers.c -o build/san/src/editor/buffers.o
```

All tests below used `ASAN_OPTIONS=detect_leaks=0`. No ASan/UBSan diagnostic
appeared in their transcripts.

| Command | Exit | Result |
| --- | --- | --- |
| `DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test` | 1 | Full suite invoked; fails in `review_suite(NULL)` |
| `build/san/tests/editor_test --ck4-save` | 0 | Passed |
| `build/san/tests/editor_test --ck4-startup` | 0 | Passed |
| `build/san/tests/editor_test --minimap-flash` | 0 | Passed |
| `build/san/tests/journal_test` | 0 | Passed |
| `build/san/tests/savectl_test` | 0 | Passed |
| `build/san/tests/file_test` | 0 | Passed |

The focused editor probes use the null backend and ran directly in fresh
processes. An initial duplicate focused invocation was interrupted while waiting
for the shared display lock; the direct probes above completed. The full suite
used the repository display lock on `:99`, preserving real-display protection.

### Full editor output (exit 1)

```text
waiting for display :99 (held by unknown)
reload launcher result=4
editor_test:1031: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:422: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:448: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:64: FAIL false
editor_test:770: FAIL settle(e) == 0
editor_test:64: FAIL false
editor_test:1215: FAIL press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0
editor_test:730: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:1386: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:2002: FAIL review_suite(NULL) == 0
edit-g11 null typing: keys=95 frames=96 orange_frames=0 stale_frames=95 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=13980
P4-3 section 3: mode=COPY choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
ck4 startup: abrupt exit, same-file discovery, recovered bytes/dirty state before first paint passed
P4-3 section 8: complete-record credit bounded; independent durable image exact with dirty frames and occupied bulk worker
P4-3 section 7: healthy flush survives abrupt child exit with exact selection/tab order/active/window metadata
P4-3 section 7: missing named target association persisted independently of empty content BASE
P4-3 section 10: journal-enabled editor rejects FIFO without a writer; child completes before alarm
P4-3 section 4: mode=COPY choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
P4-3 section 4: mode=COPY choice=Reload child abrupt exit; selected/unrelated/closed recovery exact
P4-3 section 4: mode=mapped choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
P4-3 section 4: mode=mapped choice=Reload child abrupt exit; selected/unrelated/closed recovery exact
P4-3 40: cursor/text frames omit unchanged final tab chrome passed
P4-3 41: closed drag owner and switched minimap capture cancel safely passed
P4-3 22: evicted buffer retains held save lease during unrelated typing passed
P4-3 14: pre-prompt, modified, repeated and aged recovery keys ignored passed
P4-3 13: suspended source preserves tab keys and tab pointer controls passed
P4-3 12: pending COPY admits controls and retains edit owner passed
P4-3 11: failed full-source identity adoption stays owner-local passed
P4-3 11: deferred COPY and inactive ENOSPC errors preserve unrelated editing passed
review 3/P1.9-2.2: SIGKILL point=0 recovery=x passed
review 3/P1.9-2.2: SIGKILL point=1 recovery=x passed
review 3/P1.9-2.2: SIGKILL point=2 recovery=x passed
review 3/P1.9-2.2: SIGKILL point=3 recovery=x passed
review 4: FULL/append IO retains unaccepted suffix without duplicate accepted records passed
review 4: FULL/append IO retains unaccepted suffix without duplicate accepted records passed
review 4: exit checkpoint after FULL/append IO/sync IO mode=0 restores current tree passed
review 4: exit checkpoint after FULL/append IO/sync IO mode=1 restores current tree passed
review 4: exit checkpoint after FULL/append IO/sync IO mode=2 restores current tree passed
review 4: failure checkpoint preserves named/untitled contents, views and active tab order passed
review 4: failure checkpoint preserves named/untitled contents, views and active tab order passed
review 5/P1.9-2.3: source changed between attachment and BASE registration rejected passed
review 6: submitted mid-row caret/selection/blink/focus cells passed
review 7: multiple long clipped lines preserve subsequent rows in first frame and edit/undo passed
review 8: first viewport precedes stalled remainder work passed
review 10: 10 GiB beginning edits metadata visits max=618 (M)[AC], bound=4096 (G)
review 10: mutation cancels completed backlog without adopting it passed
review 11: indivisible submit observed=2085462 reported=2191654 ns (M)[AC]
review 12: journaled large deletion and undo passed
review 13: typing/repeat bursts, movement/focus/timeout boundaries passed
review 16: mapped watch overwrite, focus truncate, edit suspension and reload/keep passed
review 17: active job idle polls=2 (M)[AC], requested-timeout bound=4 (G)
review 17: queued job idle polls=3 (M)[AC], requested-timeout bound=4 (G)
review 22: allocator positions=21 failures=20; tree/view/history/journal rollback passed (M)[AC]
review 23: backend init success/failure executes on work pool and adopts mailbox result passed
```

The minimap regression, Ctrl+S recovery, startup discovery, journal credit and
metadata, and COPY/mapped Keep/Reload checks pass in the default run. Its
failures include launcher wait completion (`reload launcher result=4`), four
native editor-open assertions, and settle timeouts in replay and piece recycling.
The top-level `review_suite(NULL)` assertion exits before later default checks.
A complete editor-suite pass is therefore **not established**. The native
failures are consistent with this environment's X11/socket restrictions; the
headless timeouts are separate and their cause is not established by this
resolution. No pre-resolution baseline run is claimed.

A direct AF_UNIX connection to `/tmp/.X11-unix/X99` confirms that the current
sandbox cannot connect to the display:

```text
X11 socket connect: PermissionError, errno=1, [Errno 1] Operation not permitted
```

Launcher result 4 is `IPC_IO`; its underlying cause was not isolated here.

### Focused editor outputs

Ctrl+S save, exit 0:

```text
P4-3 section 3: mode=COPY choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
```

Startup restore, exit 0:

```text
ck4 startup: abrupt exit, same-file discovery, recovered bytes/dirty state before first paint passed
```

Minimap flash, exit 0:

```text
edit-g11 null typing: keys=95 frames=96 orange_frames=0 stale_frames=88 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=19286
```

The replay timeout also reproduces in an independent headless process:

```sh
ASAN_OPTIONS=detect_leaks=0 EDITOR_REVIEW_ONLY=11-replay build/san/tests/editor_test
```

Exit 1:

```text
editor_test:64: FAIL false
editor_test:770: FAIL settle(e) == 0
```

The separate piece-recycling check completed successfully after the builds
finished; its earlier full-suite timeout did not reproduce in this run:

```sh
ASAN_OPTIONS=detect_leaks=0 EDITOR_REVIEW_ONLY=15 build/san/tests/editor_test
```

Exit 0:

```text
review 15: replay arena growth=0 bytes (M)[AC], bound=65536 bytes (G)
```

### Module test outputs (each exit 0)

`ASAN_OPTIONS=detect_leaks=0 build/san/tests/journal_test`:

```text
P4-3 section 5: abrupt child exit, beyond-prefix rewrite/restored mtime rejected before callbacks
journal_test: insert credit counts UI cache bytes, bounds blocked-worker loss including PAD, resumes after durable fence
journal_test: insert continuations bound one record/step, protect every prefix, retain IO progress, allocate zero bytes
journal_test: CRC32C wire/BASE vectors match independent oracle (tails, unaligned replay, maximum record)
journal_test: overlapping saves preserve both targets/cutoffs/deltas in both finish orders and publication/retirement failures
journal_test: session values exact after replay/rotation/power image; CRC-valid semantic mutants rejected
journal_test: writeback loss requires a fresh checkpoint (released unsynced batch included)
journal_test: retained identity has a full metadata barrier (empty BASE mtime survives)
journal_test: retention completion crosses the routed work mailbox
journal_test: PAD IO outranks submission BUSY and retries only on worker
journal_test: valid 4095-byte BASE retains in bounded recovery directory
journal_test: page cache ok (before pump, blocked worker, PAD gap, completion preserves UI prefix)
journal_test: append failures ok (EAGAIN/ENOSPC/EIO/EINTR/short, single attempt, retained worker retry, sticky IO, allocations=0)
journal_test: append gaps ok (PAD failure surfaced, split INSERT suffix retained, retry blocks later offsets)
journal_test: invalid setup options create no journal
journal_test: 4095-byte journal path rotates via retained directory fd
journal_test: journal rotation survives parent directory rename
journal_test: exact scripted corruption/truncation prefix (first page, 64 KiB, PAD, byte contents)
journal_test: FULL flush reports suspension; complete checkpoint clears it
journal_test: split 1 MB paste accepted, replay bytes exact, allocations=0
journal_test: atomic 1 MiB paste accepted, replay bytes exact, allocations=0
journal_test: untitled checkpoint >64 MiB restores exact content and resumes appends
journal_test: canonical base and journal paths survive cwd changes
journal_test: session IDs rejected by writer
journal_test: session IDs rejected by CRC-repaired parser
journal_test: unreadable/short/missing bases are conflicts; replay leaves log intact
journal_test: 255-byte journal basename rotates with independent temporary name
journal_test: retry ok (write failures retain progress; sync EIO requires fresh checkpoint)
journal_test: barriers ok (durable image, creation, 64KiB straddle, sync/rename/dir failures)
journal_test: save ok (file_save_begin, retained generation, post-save edits, two buffers, marker/rename/dir failures)
journal_test: file mailbox ok (copy, mmap, empty, open failure, unread close, independent save wait)
journal_test: worker returns with undrained completion
journal_test: cadence parameter ok (byte boundary, timer, force, rotation)
journal_test: save transaction accepts a 250-byte base filename
journal_test: save retains a named base shared by two buffers
journal_test: retained recovery survives old-fd truncate/overwrite (replacement, acknowledged post-save edits, restart before finish)
journal_test: retained recovery survives hard-link alias overwrite (replacement, acknowledged post-save edits, restart before finish)
journal_test: retained worker copy ok (short writes, exact multi-chunk bytes, mutation rejection, data/directory failures before publication)
journal_test: malloc_guard=ASan-inactive (release run required) append_allocations=0
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
```

`ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test`:

```text
P4-3 section 32: failed ambiguous PREPARE retains artifact until complete CURRENT recovery publishes durably
P4-3 section 33: read-only source parent and unsupported O_TMPFILE reload through private writable backing
P4-3 section 32: post-rename directory barrier failure reconciles through public recovery, no manual token deletion
P4-3 section 32: post-rename identity failure remains ambiguous, explicit complete-current cancellation releases retention and stays dirty
savectl prepare handoff/other-buffer crash replay: ok
savectl finish writeback/fresh-inode retry/retained-token reconciliation: ok
savectl bounded session request/worker preparation/submitted-status hook: ok
savectl bounded session request/worker preparation/submitted-status hook: ok
savectl terminal completion requires mailbox and yields on saturation: ok
savectl every reload construction allocation: rollback/retry: ok
reload first UI slice=140379 ns (M)[AC], allocations=12 (M)[AC]
reload UI slices=16, max=140379 ns (M)[AC]
savectl reload UI construction yields bounded slices: ok
reload anonymous ownership delta=1286144 bytes (M)[AC], bound=2097152 bytes (G)
savectl reload mapping budget/overlapping snapshots: ok
savectl destroy ignores unrelated reused work slot: ok
savectl undo clean identity/branch/eviction/save cutoff: ok
savectl reload regular-to-FIFO rejects without writer: ok
savectl journal BASE_CHANGED exposes external recovery: ok
savectl journal BASE_CHANGED exposes external recovery: ok
savectl normal-build I/O and held-publication guard: ok
savectl creation permissions/default/umask: ok
savectl ack never waits for worker/I/O: ok
savectl save states/snapshot isolation/self event: ok
savectl failed save preserves old file and modified flag: ok
savectl §2.14 save/external-change races (notified, unnotified, restored identity): ok
savectl external banner/silent reload/latest offsets/edit protection: ok
P4-3 section 32: pre-rename failure cancels through complete CURRENT checkpoint, no manual token deletion
savectl journal prepare/finish/replay/failures/retained generation: ok
savectl full-mailbox terminal publication retry: ok
savectl_test: ok
```

`ASAN_OPTIONS=detect_leaks=0 build/san/tests/file_test`:

```text
astra_watch_many: requested=200 watching=200 inotify_instances=1
P1-1 section 2: ok
P1-1 section 3: ok
finding1: ok
file_test: ok
file_test: ok
file_test: ok
file_test: ok
finding2: ok
finding3: ok
finding4: ok
finding4 source identity: ok
finding4 invalidated generation: ok
finding4 forced late fault: ok
finding5: ok
finding7: ok
finding8: ok
finding9: ok
finding11: ok
finding11 scan boundaries: ok
finding12: ok
finding13: ok
finding13 queued commit invalidation: ok
finding16 copy: ok
finding24: ok
finding25: ok
finding31: ok
finding32: ok
finding17: ok
finding18: ok
finding18 injected reads: ok
finding18 entry race: ok
finding19 missing: ok
finding19 I/O: ok
finding19 intact generations: ok
finding19 backing I/O: ok
finding19 restored version: ok
finding20: ok
finding20 exact baseline: ok
finding21: ok
finding21 refresh failure: ok
finding22: ok
finding22 explicit 0000: ok
finding22 ACL: ok
finding22 preservation error: ok
finding23: ok
finding23 FORCE: ok
file_test: ok
```

Raw test transcripts and per-test exit files are retained under
`build/ck4-resolve-logs/`. The build directory is not a durable source report;
relevant outputs are pasted above.

A source/test marker scan (`rg -n '^(<<<<<<<|=======|>>>>>>>)($| )' src tests`)
returned no matches after resolution. No rebase continuation or index operation
was attempted.
