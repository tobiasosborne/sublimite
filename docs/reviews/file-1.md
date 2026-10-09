32 findings: **8 BLOCKER, 24 MAJOR, 0 MINOR**.

No files changed. Clang `-fsyntax-only` passed for the implementation, tests, fuzzer, and bench. Functional tests and benches were not run because they create and modify filesystem fixtures. Findings below are based on code and contract analysis.

## 1. BLOCKER — SIGBUS registry races can overwrite unrelated mappings

Location: [src/file/file.c:70](/home/tobias/Projects/editor/src/file/file.c:70), registration at lines 108–111 and retirement at line 120.

The handler accepts every nonzero `addr`, including the reservation sentinel `1`. Registration writes `len` before publishing the real address. A handler running in that interval can interpret the slot as mapping `[1, 1+len)`.

Concrete interleaving: register a 10 GiB mapping; pause after storing its length; another thread faults on an unrelated, truncated `MAP_32BIT` mapping. Its address matches the sentinel range. The handler then uses `MAP_FIXED` to zero-map from that address toward 10 GiB, overwriting unrelated mappings instead of chaining. Slot reuse also allows a handler to combine an old address with a new length. `MAP_FIXED` removes overlapping mappings. [GNU mmap documentation](https://sourceware.org/glibc/manual/latest/html_node/Memory_002dmapped-I_002fO.html)

**Fix:** distinguish reserved slots from published slots; publish a coherent generation; pin that generation throughout recovery; defer retirement/reuse until handler readers finish. Test sentinel publication and reuse interleavings with adjacent foreign mappings.

## 2. BLOCKER — Previous SIGBUS dispositions are chained incorrectly

Location: [src/file/file.c:48](/home/tobias/Projects/editor/src/file/file.c:48).

The `SA_SIGINFO` branch checks the function pointer before checking `SIG_IGN`. On this platform, a previous action containing `SIG_IGN` and `SA_SIGINFO` produces a nonzero union member representing address `1`; line 51 calls it and crashes.

The default branch also assumes an instruction will fault again. For `raise(SIGBUS)` or `pthread_kill`, no instruction re-faults: the signal is swallowed and the guard is replaced permanently by `SIG_DFL`. A subsequent registered-mapping truncation then kills the process. Previous masks and `SA_RESETHAND` semantics are also discarded. [Linux sigaction documentation](https://man7.org/linux/man-pages/man2/sigaction.2.html)

**Fix:** handle special dispositions before interpreting the union as a callback; preserve the previous action’s delivery semantics; explicitly redeliver a default-action signal to the faulting thread. Test ignored, default, masked, one-shot, and three-argument actions.

## 3. BLOCKER — Worker results race with permitted UI calls

Location: [src/file/file.c:321](/home/tobias/Projects/editor/src/file/file.c:321), readers at lines 463, 493, and 518; watch updates at lines 537–539 and 686–688.

`open_job` writes `f->map` without `f->mu`, while `file_check` and `file_changed` read it under that mutex. The reader’s mutex does not synchronize an unlocked writer. Calling either public UI function while an asynchronous mapped open completes creates a C data race.

`f->err_no` is likewise written without synchronization. Its write occurs **after** publishing `ready`, so acquiring readiness does not protect a following `file_errno` call. Starting an inotify watch during a save also races with the worker’s accesses to watch fields.

**Fix:** publish immutable worker results through mailboxes and install them on the UI thread. Keep watch management UI-owned. Add TSan tests that poll change/error APIs during open and start watches during save.

## 4. BLOCKER — A detected late mapping fault can still produce a successful corrupted save

Location: [src/file/file.c:625](/home/tobias/Projects/editor/src/file/file.c:625), save validation setup at lines 668–680.

The final save check validates only the current pathname identity. It does not validate the snapshot’s original mapping, its fault flag, or an already-set `f->changed`.

Concrete repro:

1. Open mapped file A while another process retains a writable descriptor to its inode.
2. Save once; the pathname now names replacement B, while the tree still references A.
3. Start a second save.
4. During writing, truncate A through the retained descriptor.
5. A UI snapshot read faults and zero-fills the remaining mapping; `file_check` detects truncation.
6. Resume save. B’s pathname identity still matches `expect`, so the worker renames a file containing zero-filled source bytes and reports `FILE_OK`.

This is outside the documented last-check-to-rename race: the change was detected before commit.

**Fix:** bind save jobs to a source generation and reject invalidated/faulted generations immediately before rename. Cancellation and validation must cover the actual snapshot backing, including retired original inodes.

## 5. BLOCKER — “Keep” leaves cached newline counts inconsistent with mapped bytes

Location: [src/file/file.c:523](/home/tobias/Projects/editor/src/file/file.c:523); caching in [src/piece/piece.c:344](/home/tobias/Projects/editor/src/piece/piece.c:344).

`file_resolve_keep` changes only the disk identity and flag. It does not rebuild the tree or invalidate derived counts when mapped original bytes change.

Repro: map 64 KiB of repeated `x\n`; call `piece_line_count` to cache the count; externally overwrite the inode with 64 KiB of `x`; detect the change and choose keep. Reads now contain no newlines, but `piece_line_count` returns the old cached count. Existing snapshots and indexes can retain the same inconsistency.

Perf §2.14 permits edits over a changed original; it does not permit incorrect line results after accepting that original.

**Fix:** rebase into a fresh original generation and rebuild/invalidate all derived metadata before clearing the changed state. Resolve the conflict with the frozen piece contract explicitly if rebasing needs additional support.

## 6. BLOCKER — Journal’s retained hard link does not freeze recovery data

Location: [src/journal/journal.c:502](/home/tobias/Projects/editor/src/journal/journal.c:502).

`journal_save_prepare` retains the previous generation with a hard link, then rotates recovery BASE records to that link. A hard link preserves inode lifetime, not its contents.

Repro: another process opens the original for writing; prepare and complete the file replacement; append post-save edits; the other process truncates or overwrites its still-open old inode; crash before `journal_save_finish`. The retained BASE changed too. `journal_check_base` rejects recovery, and the journal cannot restore the promised session through its normal replay path.

The same problem exists through another hard-link name.

**Fix:** retain an independent, validated generation using a reflink or bounded worker copy, with a copy fallback. Sync that generation and its directory before publishing the prepared checkpoint. Test external writes through both old descriptors and hard-link aliases.

## 7. BLOCKER — Save can fsync a different directory and falsely acknowledge durability

Location: [src/file/file.c:642](/home/tobias/Projects/editor/src/file/file.c:642).

The parent directory is reopened by pathname **after** rename. It is not necessarily the directory containing the replacement.

Repro using `FILE_STEP_RENAMED`: rename the target’s parent directory elsewhere and create another directory at the original path. The worker opens and fsyncs the new directory, returns `FILE_OK`, and never performs the namespace barrier on the directory where its replacement occurred. Power loss can therefore lose a replacement reported as durable.

**Fix:** open and retain the parent directory before creating the temp. Use `openat`, `fstatat`, `renameat`, and `unlinkat` against that descriptor, and fsync that same descriptor. Test directory moves/exchanges at commit boundaries.

## 8. BLOCKER — Earlier save completions can reference a freed file

Location: [src/file/file.c:727](/home/tobias/Projects/editor/src/file/file.c:727); close at line 447.

Each new save overwrites `save_h`. Completed messages can remain pending in earlier slots, but `file_close` cancels only the latest handle.

Repro: finish save 1 without draining its completion; start and finish save 2; close the file; drain the mailbox. Save 1’s slot was never cancelled, so its message remains deliverable with `m.f` pointing to freed storage. A normal completion handler using `file_path(m.f)` or another file API accesses freed memory.

**Fix:** retain/cancel every outstanding file-job handle until its messages are consumed, or introduce completion ownership that keeps the object alive. Before freeing, invalidate all pending messages belonging to the file. Add this exact two-save/close/drain test under ASan.

## 9. MAJOR — SIGBUS handler calls an API without an async-signal-safety guarantee

Location: [src/file/file.c:74](/home/tobias/Projects/editor/src/file/file.c:74).

The decision says the handler uses only mmap, atomics, and errno handling. It additionally calls `sysconf`. GNU libc documents `sysconf` as AS-Unsafe because of lock/heap use; POSIX removed its async-signal-safety requirement. The implementation relies on an undocumented argument-specific exception. This is a contract defect, not a claimed observed deadlock. [GNU sysconf documentation](https://sourceware.org/glibc/manual/latest/html_node/Sysconf-Definition.html), [Linux signal-safety documentation](https://man7.org/linux/man-pages/man7/signal-safety.7.html)

**Fix:** calculate and validate page size before registration, store it in immutable registration metadata, and remove `sysconf` from the handler. Verify off-path that the atomics used by the handler are lock-free. Document the precise supported libc/sanitizer recovery path.

## 10. MAJOR — P1.7b violates the project’s mutable-global rule

Location: [src/file/file.c:43](/home/tobias/Projects/editor/src/file/file.c:43); rule at [CLAUDE.md:22](/home/tobias/Projects/editor/CLAUDE.md:22).

The registry, saved action, once control, and installation state are four mutable file-scope globals. `CLAUDE.md` permits only the trace ring and allocator hook. P1.7’s decision document claims an exception, but the binding rule was not amended.

**Fix:** either remove the need for this registry through stable backing generations, or formally amend the project contract for a narrowly specified process-wide signal service with explicit ownership, initialization, and retirement rules. Keep implementation and binding rules consistent.

## 11. MAJOR — Open performs blocking I/O and unsliced work on the UI thread

Location: [src/file/file.c:360](/home/tobias/Projects/editor/src/file/file.c:360), prefix reading at lines 388–400.

`realpath`, `open`, `fstat`, and repeated `pread` execute synchronously on the UI thread. Cold storage, FUSE, or network filesystem delays have no UI-time bound. The prefix also receives an unsliced scan of up to 1 MiB, and repeated interruptions can extend the read loop indefinitely.

The decision explicitly accepts this split, but it conflicts with the requested nonblocking UI behavior and the ≤0.5 ms foreground slice contract.

**Fix:** move canonicalization, opening, and prefix acquisition into an interactive worker stage; publish the prefix before bulk copy/index work. Make prefix readiness asynchronous and measure request-to-correct-submit, including the handoff.

## 12. MAJOR — Opening a FIFO hangs before the non-regular-file check

Location: [src/file/file.c:361](/home/tobias/Projects/editor/src/file/file.c:361).

The code opens the path with blocking `O_RDONLY` before checking its type. Opening a FIFO without a writer blocks forever, so the promised `FILE_ERR_NOTREG` result is unreachable and the UI hangs.

**Repro:** create a FIFO and pass its path to `file_open_begin` while no writer is present.

**Fix:** open with `O_NONBLOCK`, check the descriptor’s type, and reject non-regular files before reads. Do this on the worker as well. Add FIFO and device rejection tests.

## 13. MAJOR — Save acknowledgement and status queries can block on worker I/O

Location: [src/file/file.c:718](/home/tobias/Projects/editor/src/file/file.c:718); lock-held I/O at lines 623–634.

`file_save_begin` synchronously calls `stat` and mapped-inode `fstat`. The worker also holds `f->mu` across stat, fstat, and rename; UI calls such as `file_changed` wait on that mutex. Watch removal/addition performs further filesystem operations under the same lock.

A slow filesystem operation can therefore delay both G8s acknowledgement and unrelated UI status checks well beyond 5 ms.

**Fix:** acknowledge from cached UI-owned state plus snapshot/enqueue. Perform authoritative checks on the worker and return results through mailboxes. Never hold a UI-visible mutex across filesystem operations.

## 14. MAJOR — Journal preparation makes the actual save request synchronous

Location: [src/journal/journal.h:87](/home/tobias/Projects/editor/src/journal/journal.h:87); implementation at [src/journal/journal.c:491](/home/tobias/Projects/editor/src/journal/journal.c:491).

The required transaction calls `journal_save_prepare` before `file_save_begin`, with no intervening UI mutation. Preparation blocks in flush, retained-inode sync, directory sync, and complete checkpoint rotation. Journal access is UI-thread-only.

Thus the supported transaction cannot meet G8s’s request → snapshot/enqueue/“saving” endpoint when preparation stalls. The file bench excludes this preparation entirely.

**Fix:** capture immutable snapshots and the journal cutoff on the UI thread, acknowledge enqueue, then execute preparation and file replacement as worker transaction phases. Preserve subsequent edits through the cutoff protocol without freezing the UI during checkpoint I/O.

## 15. MAJOR — Closing a file waits for physical worker cleanup on the UI thread

Location: [src/file/file.c:428](/home/tobias/Projects/editor/src/file/file.c:428).

`file_close` sleeps until queued/running jobs finish or are popped. Cancelling a queued job does not remove it from the queue. A save or open behind another bulk job therefore blocks closing until that unrelated job completes.

The existing cancellation test deliberately delays release by 50 ms, demonstrating a UI stall already far beyond the slice contract. An in-flight synchronous filesystem call can delay it much longer.

**Fix:** make close logical and immediate: cancel publication, retire the UI object, and defer destruction until job/message ownership ends. Physical cleanup must not be awaited by the UI.

## 16. MAJOR — Attach performs the supposedly asynchronous bulk initialization on the UI

Location: [src/file/file.c:474](/home/tobias/Projects/editor/src/file/file.c:474).

Copy mode’s worker only reads into `f->data`. UI attachment then calls `piece_init_copy`, which allocates another original, copies all bytes, scans newline counts, and builds the tree.

For a file just below the 256 MiB threshold, hundreds of MiB of work execute without an input check. Mapped attachment also builds metadata proportional to file size; a 10 GiB original creates roughly 163,840 initial pieces on the UI thread.

**Fix:** hand the worker-owned private copy to the tree through lifetime hooks rather than copying it again. Make mapped initialization lazy or otherwise bounded within the frozen piece API. Any required initialization-contract change needs an explicit reviewed amendment.

## 17. MAJOR — Copy-mode storage exceeds G10f both permanently and at peak

Location: [src/file/file.c:391](/home/tobias/Projects/editor/src/file/file.c:391), attachment at lines 474–480.

For a 1 MiB file, the prefix is the whole file and remains allocated until close. `piece_init_copy` allocates another 1 MiB original. Payload alone is 2,097,152 bytes, exceeding G10f’s `1.25*S + 64 KiB` limit of 1,376,256 bytes.

For larger copy files, attachment temporarily holds the worker’s full copy, the piece tree’s full copy, and the prefix. Freeing `f->data` afterward does not repair a peak-memory violation.

**Fix:** use one owned private original shared through lifetime hooks; retire redundant prefix storage at a defined handoff. Add allocation accounting across open, attach, snapshots, and close—not just inside the piece allocator.

## 18. MAJOR — Open publishes success after an unvalidated source change

Location: [src/file/file.c:334](/home/tobias/Projects/editor/src/file/file.c:334), publication at lines 342–348.

Premature EOF during the copy job is treated as success. Neither copy nor mapping completion verifies source identity again before publishing readiness.

Repro: block the bulk worker, open a 3 MiB copy-mode file and publish its prefix, truncate it to 8 KiB, then release the worker. The job publishes `OPEN_READY` for the shorter data while `f->size` and prefix describe the old source and `file_changed` remains false. Concurrent rewriting across chunks can similarly produce a hybrid copy.

**Fix:** validate descriptor and pathname identity around acquisition. Treat premature EOF or identity changes as `FILE_ERR_CHANGED`; invalidate the prefix/full-content generation and surface a consistent change state.

## 19. MAJOR — Keep cannot resolve truncated or deleted originals

Location: [src/file/file.c:523](/home/tobias/Projects/editor/src/file/file.c:523); comparison at line 282.

Keep clears only `f->changed`. It does not clear or replace a faulted mapping generation, and every later check still sees the old inode shorter than its mapping. Even a successful forced save leaves the guard fault flag active, so normal saves remain refused.

Deletion is also unresolved: after accepting a missing pathname, both baseline and current identity have `exists == 0`, but `id_diff` still returns `FILE_CHG_GONE`. A normal save cannot recreate the file after keep.

Stat failures additionally produce a zero identity and unconditional `FILE_OK`.

**Fix:** define accepted generations for keep, detach compromised backing, distinguish “both absent” from a new disappearance, and return I/O errors without replacing the baseline when stat failed unexpectedly.

## 20. MAJOR — `file_check` does not return its documented sticky state

Location: [src/file/file.c:510](/home/tobias/Projects/editor/src/file/file.c:510); contract at [src/file/file.h:147](/home/tobias/Projects/editor/src/file/file.h:147).

The header promises a return value of 1 while the source-changed state is sticky. The implementation returns whether this particular comparison produced new reason bits.

Repro: record the original mtime; touch the file; call `file_check` to set changed; restore the original mtime. The next check returns 0 while `file_changed` remains 1. Callers using the documented return value can suppress an unresolved change prompt.

**Fix:** return the sticky state captured under the mutex, while leaving `reasons` as the current comparison’s bits. Add a test where metadata returns to baseline without a keep/save resolution.

## 21. MAJOR — External replacement followed by keep leaves inotify watching the old inode

Location: [src/file/file.c:539](/home/tobias/Projects/editor/src/file/file.c:539); keep at line 523.

The watch is on an inode, not the pathname’s future replacements. External rename-over can remove the watch or leave it attached to the retired inode. Keep adopts the new pathname identity but never rearms the watch.

Repro: start watching; rename another file over the target; poll and choose keep; append to the replacement. The replacement produces no event on the old watch, and `file_watch_poll` reports unchanged until another explicit stat check.

**Fix:** watch the parent/name or rearm on replacement, `IN_IGNORED`, and keep. Handle failed rearming explicitly; `file_watch_start` must not return an existing fd as though an absent watch were active.

## 22. MAJOR — Saves lose metadata and can undo permission tightening

Location: [src/file/file.c:618](/home/tobias/Projects/editor/src/file/file.c:618), identity comparison at lines 279–286.

Only cached permission bits are applied to the replacement. Ownership, group, ACLs, xattrs, and security labels are not copied.

A more immediate repro needs no exotic metadata: open a 0644 file, externally chmod it to 0600, then save normally. chmod changes neither size nor mtime; the comparison ignores mode/ctime, so the save restores cached 0644 permissions. An original mode of 0000 also becomes 0644 because zero means “use default.”

**Fix:** capture and validate metadata on the worker, preserve supported ownership/ACL/xattr state before data fsync, and distinguish an unspecified mode from mode 0000. Report preservation failures instead of silently changing access semantics.

## 23. MAJOR — Canonical-path checks miss a replacement symlink to the same inode

Location: [src/file/file.c:627](/home/tobias/Projects/editor/src/file/file.c:627), rename at line 633.

`stat` follows symlinks, while rename replaces the directory entry itself.

Repro: open regular file A; make hard link B; replace A with a symlink to B. Stat of A still returns the same device, inode, size, and mtime, so both checks pass. Saving replaces the symlink A with a regular file and leaves B unchanged, violating the documented link-preserving save policy.

**Fix:** validate the canonical target’s directory entry with `fstatat(..., AT_SYMLINK_NOFOLLOW)` against the pinned parent directory. Detect replacement by a symlink as an external change, or explicitly resolve and validate a new referent according to a documented policy.

## 24. MAJOR — Terminal open/save results can be silently dropped

Location: [src/file/file.c:698](/home/tobias/Projects/editor/src/file/file.c:698), open results at lines 346–348; drop behavior at [src/work/work.c:169](/home/tobias/Projects/editor/src/work/work.c:169).

Every terminal publication ignores `work_publish`’s result. A full mailbox drops the message, but the job marks itself finished anyway. Save status and errno then disappear; an open failure can leave callers waiting for a message that will never arrive.

**Repro:** have an earlier bulk job fill its mailbox with 256 messages, do not drain, then submit a file job in another available slot.

**Fix:** reserve reliable terminal-completion capacity per accepted job or provide a guaranteed completion channel. Progress messages may be lossy; terminal results cannot be. Add saturation tests for successful and failed open/save jobs.

## 25. MAJOR — Zero-progress writes loop forever without cancellation checks

Location: [src/file/file.c:562](/home/tobias/Projects/editor/src/file/file.c:562).

If `write` returns zero for a nonzero request, neither pointer nor remaining length changes. The loop spins forever. Repeated `EINTR` retries also bypass the stop callback indefinitely.

A regular file supplied by a filesystem returning zero-progress writes can therefore monopolize the bulk worker and make `file_close` wait forever. A syscall seam returning zero reproduces the control-flow failure directly.

**Fix:** treat zero progress as `EIO`; check cancellation inside short-write/interruption retry loops; propagate cancellation distinctly. Test zero, short, interrupted, and failed writes.

## 26. MAJOR — G5 rows do not measure or verify the G5 endpoint

Location: [bench/file_bench.c:40](/home/tobias/Projects/editor/bench/file_bench.c:40).

All four G5 rows stop timing when `file_open_begin` returns. None submits a viewport, validates prefix bytes, or even checks the returned prefix length. A blank prefix or delayed rendering can pass every row.

Missing fixtures return success. An existing incorrectly sized `sparse_10g.bin` is accepted without validating its 10 GiB size. There are no verified cold or required concurrent-worker scenarios.

**Fix:** retain prefix acquisition as a separately named primitive row, then enforce request-to-correct-viewport-submit with a byte/render oracle. Validate fixture size/content/sparsity, fail unavailable required scenarios, and include cold and queued-worker conditions.

## 27. MAJOR — G8s bench predominantly measures cached snapshot retention

Location: [bench/file_bench.c:96](/home/tobias/Projects/editor/bench/file_bench.c:96).

The tree is edited once before 400 saves. There are no intervening mutations. After the first save, `piece_snapshot_take` returns the cached snapshot through `piece_snapshot_retain`.

Thus a regression in fresh snapshot creation affects only one sample and can disappear from p50/p99. The fixture also omits large fragmented histories, busy bulk-worker conditions, journal preparation, and showing “saving.”

**Fix:** mutate outside the timed interval before each representative save, use substantial edit histories and queued-worker scenarios, and time the actual acknowledgement endpoint. Keep unchanged-repeat-save timing as a separate primitive.

## 28. MAJOR — G8d rows cannot fail on gate misses or failed saves

Location: [bench/file_bench.c:107](/home/tobias/Projects/editor/bench/file_bench.c:107), 1 GB row at line 135.

The 1 MB row only prints `TRACK`; the 1 GB row prints one elapsed value. Neither enforces its gate, and the cold-source row is absent.

Both wait for `file_save_busy` to clear and discard completion messages. ENOSPC, fsync failure, or rename failure can look like a fast completion. The 1 GB row also returns success after missing fixtures, insufficient space, or failed attachment.

**Fix:** require a matching `SAVE_DONE` with `FILE_OK`, validate output bytes, collect completion distributions, and enforce A’s 10/50 ms, 1500/2500 ms, and 2200/3000 ms rows. Required failures/skips must not produce a passing bench.

## 29. MAJOR — Durability tests cannot detect missing or reordered fsync barriers

Location: [tests/file_test.c:381](/home/tobias/Projects/editor/tests/file_test.c:381).

The test kills a process and reads the file through the surviving kernel/page cache. Removing both fsync calls while leaving step hooks intact preserves every whole-old/whole-new assertion. Moving data fsync after rename can likewise remain green.

The crash implications differ:

- Before rename, the named target remains old.
- After rename but before directory sync, restart can see new, but power loss has no durable-new guarantee.
- After directory sync, new must be durable.
- Before journal finish, recovery must still work from the prepared retained base.
- After finish, the durable checkpoint must reference the saved base plus later edits.

Only live-kernel visibility is tested for the file replacement.

**Fix:** add syscall-order assertions and a durable-image fault model for file data and namespace barriers. Compose file save with journal prepare/finish at each crash boundary, including barrier failures.

## 30. MAJOR — Error contracts and file operations lack an effective test/fuzz oracle

Location: [tests/file_test.c:433](/home/tobias/Projects/editor/tests/file_test.c:433); [fuzz/file_fuzz.c:6](/home/tobias/Projects/editor/fuzz/file_fuzz.c:6).

Cancellation coverage cancels a queued job before temp creation. There is no injected ENOSPC/EIO, short/zero write, fchmod/fsync/rename/EXDEV/directory-sync failure coverage. Removing failure-path `unlink(tmp)` can escape the current cleanup assertions.

The fuzzer exercises only the EOL/BOM scanner; it cannot catch file state, lifetime, save, or recovery defects. Tests also lack a real allocation guard around warmed save acknowledgement; default piece allocators and sanitizer builds do not enforce that contract.

**Fix:** add a syscall seam and stateful operation model covering target preservation, cleanup, status delivery, cancellation after temp creation, source generations, and mapping lifetime. Add a release-build allocation guard with pooled hooks, plus thread-race coverage.

## 31. MAJOR — `file_errno` never records save failures

Location: [src/file/file.c:463](/home/tobias/Projects/editor/src/file/file.c:463); completion at line 698.

The header describes this getter as the errno of the last `FILE_ERR_IO`, and the error definition directs callers to it. Save failures put errno only in the message payload; they never update `f->err_no`.

Repro: cause temp creation to fail with EACCES or writing to fail with ENOSPC. `SAVE_DONE` reports `FILE_ERR_IO` and the correct payload errno, but `file_errno(f)` remains the open job’s value, commonly zero.

**Fix:** install the received completion error in UI-owned file state before exposing completion. Add a test comparing getter and payload after each injected I/O failure.

## 32. MAJOR — Inotify draining has no UI work bound

Location: [src/file/file.c:549](/home/tobias/Projects/editor/src/file/file.c:549).

`file_watch_poll` repeatedly reads until the queue becomes empty. A producer alternating modification and attribute events can keep the queue nonempty; coalescing identical events does not bound this workload. There is no byte, event, or time budget and no input check.

The UI can remain in this loop indefinitely, and the subsequent identity check is never reached.

**Fix:** drain a bounded amount within the UI slice budget, coalesce the request for an identity check, and return while readiness remains pending. Perform the filesystem check asynchronously. Test with a sustained alternating event producer.

| # | Severity | Location | One line |
|---|---|---|---|
| 1 | BLOCKER | src/file/file.c:70 | Registry publication/reuse can drive MAP_FIXED over unrelated mappings. |
| 2 | BLOCKER | src/file/file.c:48 | Signal chaining can call SIG_IGN or disable truncation recovery. |
| 3 | BLOCKER | src/file/file.c:321 | Worker-written file fields race with permitted UI calls. |
| 4 | BLOCKER | src/file/file.c:625 | Late source faults can commit zero-filled bytes with FILE_OK. |
| 5 | BLOCKER | src/file/file.c:523 | Keep accepts changed bytes with stale cached line counts. |
| 6 | BLOCKER | src/journal/journal.c:502 | Retained hard links remain externally writable recovery bases. |
| 7 | BLOCKER | src/file/file.c:642 | Path-based directory reopening can acknowledge the wrong durability barrier. |
| 8 | BLOCKER | src/file/file.c:727 | Older completions can outlive and reference a freed file. |
| 9 | MAJOR | src/file/file.c:74 | Handler calls sysconf without an async-signal-safety guarantee. |
| 10 | MAJOR | src/file/file.c:43 | Four mutable globals violate the binding project rule. |
| 11 | MAJOR | src/file/file.c:360 | Open blocks the UI on filesystem I/O and unsliced prefix work. |
| 12 | MAJOR | src/file/file.c:361 | FIFO open hangs before type rejection. |
| 13 | MAJOR | src/file/file.c:718 | Save ack/status calls block on I/O and worker-held locks. |
| 14 | MAJOR | src/journal/journal.h:87 | Required journal preparation blocks before save acknowledgement. |
| 15 | MAJOR | src/file/file.c:428 | Close waits synchronously for physical job cleanup. |
| 16 | MAJOR | src/file/file.c:474 | Attach copies/scans/builds bulk content on the UI. |
| 17 | MAJOR | src/file/file.c:391 | Duplicate original/prefix storage exceeds G10f. |
| 18 | MAJOR | src/file/file.c:334 | Open accepts premature EOF and unvalidated source versions. |
| 19 | MAJOR | src/file/file.c:523 | Keep cannot resolve truncated/deleted originals reliably. |
| 20 | MAJOR | src/file/file.c:510 | file_check returns current differences instead of sticky state. |
| 21 | MAJOR | src/file/file.c:539 | Keep after replacement leaves the watch on the old inode. |
| 22 | MAJOR | src/file/file.c:618 | Saves lose metadata and can restore broader permissions. |
| 23 | MAJOR | src/file/file.c:627 | Following stat misses a replacement symlink to the same inode. |
| 24 | MAJOR | src/file/file.c:698 | Full mailboxes silently discard terminal results. |
| 25 | MAJOR | src/file/file.c:562 | Zero-progress/interrupted writes lack termination/cancellation checks. |
| 26 | MAJOR | bench/file_bench.c:40 | G5 rows omit viewport submission, correctness, and required conditions. |
| 27 | MAJOR | bench/file_bench.c:96 | G8s mostly measures cached snapshot retention. |
| 28 | MAJOR | bench/file_bench.c:107 | G8d cannot fail on latency misses or failed saves. |
| 29 | MAJOR | tests/file_test.c:381 | Process-kill tests cannot verify durability barriers or ordering. |
| 30 | MAJOR | fuzz/file_fuzz.c:6 | File/error/allocation contracts lack effective test and fuzz coverage. |
| 31 | MAJOR | src/file/file.c:463 | file_errno stays stale after save I/O failures. |
| 32 | MAJOR | src/file/file.c:549 | Continuous inotify events can monopolize the UI. |