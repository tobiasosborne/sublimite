17 findings: 5 BLOCKER, 12 MAJOR, 0 MINOR.

Read-only verification: GCC syntax checks passed for all four modules and their tests, fuzzers, and benches; installer shell syntax passed. Existing release and ASan/UBSan binaries for tabs, indent, and minimap passed, with leak detection disabled for sanitizer runs. Release allocation guards reported zero allocations. No files were changed. IPC execution, rebuilds, and benchmark measurements were not performed.

No concrete failure was found in tabs’ close/reopen/MRU/drag invariants or configured metadata bound. Its borrowed-buffer retention policy remains an integration responsibility.

## 1. BLOCKER — IPC encoding can overrun a correctly sized output buffer

Location: [src/ipc/wire.c:55](/home/tobias/Projects/editor/src/ipc/wire.c:55), particularly the second `strlen` at line 59.

The encoder validates string lengths, writes the header, then measures paths again. Input strings may alias `wire`; the public contract does not prohibit this.

Concrete repro:

```c
uint8_t wire[44] = "/a";
ipc_request r = {.cwd = "/tmp", .count = 1};
r.paths[0] = (ipc_path){(const char *)wire, 1, 1};
size_t n;
ipc_wire_encode(&r, wire, sizeof wire, &n);
```

Validation calculates exactly 44 bytes: header 24, cwd 5, record header 12, path 3. Header writing changes the aliased path to `"EDIP\x01\0"`. The second `strlen` therefore produces length 6, and `memcpy` writes bytes 41–46 into a 44-byte array.

Fix: establish and enforce a non-overlap contract before writing, or stage overlapping inputs safely. Retain validated lengths instead of remeasuring strings. Add this exact guarded-buffer case and decode/re-encode overlap cases to the suite.

## 2. BLOCKER — Runtime-directory races permit chmod of an attacker-selected file

Location: [src/ipc/socket.c:32](/home/tobias/Projects/editor/src/ipc/socket.c:32), with subsequent pathname operations at lines 261, 266, 279–282 and 246.

Runtime validation follows symlinks and checks only the directory found by one `stat`. Later lock creation, binding, chmod and unlink resolve the pathname again.

Concrete argument: place a victim-owned 0700 runtime directory beneath an attacker-controlled writable parent. After validation, the parent owner can rename that directory and substitute a link to a writable directory. The editor creates its lock/socket there. After socket `lstat`, the attacker can replace the socket name with a symlink to a victim-owned file; `chmod(path,0600)` follows that link. Peer credentials do not protect these filesystem operations.

Fix: retain a verified directory fd, perform lifecycle operations relative to that directory, and bind through a pinned directory path. Prevent replacement through untrusted ancestors and prevent chmod from following a substituted symlink. Add synchronized directory/socket replacement tests.

## 3. BLOCKER — Canonicalization redirects invalid paths to unrelated files

Location: [src/ipc/args.c:37](/home/tobias/Projects/editor/src/ipc/args.c:37), and the acceptance of `ENOTDIR` at line 47.

The resolver removes `..` without checking whether the preceding existing component is a directory. It also treats `ENOTDIR` as an acceptable missing suffix.

Concrete repro: create regular files `leaf` and `victim`, then parse `leaf/../victim`. Kernel pathname resolution rejects this with `ENOTDIR`. This resolver successfully resolves `leaf`, pops it at `..`, and returns the path to `victim`. Similarly, a trailing slash on a regular file is discarded.

This violates the promised preservation of pathname semantics and can lead the user to edit or save the wrong file.

Fix: require existing intermediate components to be directories, propagate `ENOTDIR`, and preserve trailing-directory requirements. Keep lexical normalization only for genuinely missing components. Test regular-file prefixes followed by `/`, `/.`, `/..`, and missing children.

## 4. BLOCKER — Suffix parsing mistakes filesystem errors for absent filenames

Location: [src/ipc/args.c:95](/home/tobias/Projects/editor/src/ipc/args.c:95).

Every failed `stat` enables coordinate parsing. A dangling symlink remains an existing filename, but `stat` follows it and reports `ENOENT`.

Concrete repro: create `a:1` as a dangling symlink and invoke `edit a:1`. The parser strips `:1` and requests `/cwd/a`. A stronger error case is a self-referencing symlink named `loop:1` alongside a regular file `loop`: `stat` returns `ELOOP`, yet the parser opens `loop`.

Fix: use directory-entry existence to establish literal-filename precedence, and distinguish absence from resolution errors. Existing symlink names must retain their literal identity; cyclic or inaccessible resolution must return an error. Add dangling-link and `ELOOP` precedence tests.

## 5. BLOCKER — Minimap cache reuse can return another buffer’s “exact” offsets

Location: [src/minimap/minimap.c:25](/home/tobias/Projects/editor/src/minimap/minimap.c:25), cache selection at line 196, and hit construction at line 248.

The cache key contains revision, byte length, line count and height, but no buffer identity. The integration contract specifies per-buffer revision counters without explicitly requiring a distinct minimap object or reinitialization on every source switch.

Concrete repro: fill one object from `"aaa\n"` using length 4, lines 2, revision 1 and height 2. Switch its input to `"\nxxx"` with the same values. Fill reuses the first buffer’s summaries. A row-1 hit returns byte 4 with `exact=true`; the second buffer’s row-1 line starts at byte 1.

Fix: include a stable buffer identity in cache validation, or explicitly require and wire persistent, distinct minimap objects per buffer. Add a tab-switch test with equal-sized, equal-line-count buffers whose revisions match.

## 6. MAJOR — Enter and brace handling scan arbitrarily long lines on the UI thread

Location: [src/indent/indent.c:36](/home/tobias/Projects/editor/src/indent/indent.c:36), `line_end` at line 48, and callers at lines 68–71 and 87–88.

Both typing queries scan backward to the previous newline and forward to the next newline/EOF without a work bound. Enter performs the forward scan even when insertion scratch is too small. Brace handling performs it before determining that the line is nonblank.

Concrete repro: open a permitted 10 GB single-line file and press Enter or `}`. At cursor zero, forward scanning alone examines the entire file. The APIs expose no budget or continuation, and their mutable-tree calls are UI-thread-only. Wiring cannot make that call fit G1’s 1/2 ms limits or the 0.5 ms UI slice.

The bench measures ordinary source lines; tests reach 4 KiB lines and fuzzing caps input at 8 KiB.

Fix: supply bounded cached line/EOL information or introduce bounded, resumable query APIs and a defined long-line policy. Add long-line work-bound tests and benchmark cases, including insufficient-output-capacity calls.

## 7. MAJOR — Every minimap fill turns strip damage into full-viewport damage

Location: [src/minimap/minimap.c:238](/home/tobias/Projects/editor/src/minimap/minimap.c:238).

Fill marks every grid row dirty, including cached fills whose cells did not change. Frozen `render_strip` represents full-width row runs, so a narrow sidebar update becomes one full-height, full-width submission.

Concrete repro: begin a frame, damage one typing row, then perform the integration contract’s minimap fill. `render_dirty_strips` now returns the entire viewport. The CPU backend copies and rasters every text row. Cached fills on cursor/blink frames cause the same expansion.

This conflicts with G1, G11, and the minimap requirement that an edit redraw a small strip. The bench times cell filling without submitting through the backend, so it cannot detect this regression.

Fix: avoid writes and damage for unchanged cells, and provide column/rectangle damage or a separately composited minimap surface for strip-wide changes. Test submitted cells/pixels and integrated typing/blink cost.

## 8. MAJOR — Pending indexing disables the minimap scrollbar and violates sidebar gates

Location: [src/minimap/minimap.c:197](/home/tobias/Projects/editor/src/minimap/minimap.c:197), and stale-hit rejection at line 245.

A newly opened buffer has no retained summaries. While `index_ready=false`, fill paints a blank stale strip and all hits fail. Readiness requires a complete current index.

Concrete repro: open a cold 10 GB file. The viewport may open promptly, but the scrollbar remains unusable throughout indexing. The performance contract explicitly requires byte-based scrolling before index publication and says a stale or blank sidebar fails G3 (§2.11).

The bench completes indexing before its measured fills; its stale check expects the behavior that the gate rejects.

Fix: provide a current byte-based approximate map and drag target while indexing, then transition to exact line mapping. Add an incomplete-index navigation test and include sidebar correctness in the G3 benchmark.

## 9. MAJOR — Bounded minimap sampling still permits foreground major faults

Location: [src/minimap/minimap.c:99](/home/tobias/Projects/editor/src/minimap/minimap.c:99), sampled dereferences at lines 121–126, and regeneration at line 206.

A nonblocking span callback can return a pointer into a mapping without reading its pages. The module subsequently dereferences up to 256 dispersed samples synchronously on the UI thread. Bounded bytes do not bound storage-fault latency.

Concrete repro: build an index, evict the mapped sample pages, then make an edit and refill. Index readiness remains true, but the fill can incur many major faults. The integration contract neither requires sample residency nor supplies a worker-produced sample cache.

The bench builds the index immediately before measuring and repeatedly samples the same warmed locations.

Fix: prepare resident sample summaries from snapshots on a worker and publish through `src/work`, with revision checks. Specify residency and deferred-fill behavior explicitly. Add a cold-sample verification that records foreground major faults.

## 10. MAJOR — IPC drain bounds events but not UI work

Location: [src/ipc/socket.c:159](/home/tobias/Projects/editor/src/ipc/socket.c:159), and draining at lines 203–218.

Each peer-processing call receives its entire available frame, up to 1 MiB, before yielding. A listener event processes up to 32 accepted clients; two batches can process further arrivals. There is no byte, syscall, callback-count or elapsed-time budget.

Concrete repro: queue 32 clients sending maximum-sized frames while an input event is pending. One drain can receive roughly 32 MiB, fault in receive pages, decode requests and invoke all callbacks before returning. Completed non-wait peers free slots, permitting more work in the next batch.

The one-small-request benchmark cannot exercise this path.

Fix: impose a small total drain budget, preserve partial progress, and return to input handling between slices. Budget accepts and decoding too. Add concurrent large-frame and connection-flood cases with a drain-work assertion.

## 11. MAJOR — Incomplete clients permanently exhaust all IPC slots

Location: [src/ipc/socket.c:164](/home/tobias/Projects/editor/src/ipc/socket.c:164), and slot rejection at line 214.

Incomplete peers have no expiration. Returning on `EAGAIN` leaves their slots occupied indefinitely.

Concrete repro: establish 32 same-uid connections and send no data, or send valid large-frame headers followed by incomplete bodies. Every subsequent legitimate handoff is accepted and closed without reaching the callback. Recovery requires those clients to disconnect or the editor to restart.

Fix: add absolute pre-ACK request deadlines and bounded pressure eviction/admission. A timerfd armed only while incomplete requests exist can preserve event-driven idle behavior. Exempt legitimate post-ACK `--wait` clients. Test all-slot exhaustion followed by successful recovery.

## 12. MAJOR — Lifecycle locking can block startup and shutdown indefinitely

Location: [src/ipc/socket.c:265](/home/tobias/Projects/editor/src/ipc/socket.c:265), and shutdown locking at line 236.

Both functions use blocking `flock(LOCK_EX)` with unlimited retries. The documented “brief” blocking has no bound, and the client handoff timeout does not cover server initialization.

Concrete repro: a same-uid process opens the persistent lock file and holds an exclusive flock. Editor startup never reaches its handoff timeout; an existing editor’s shutdown can also hang while retaining wait clients.

Fix: use nonblocking lock attempts with a bounded lifecycle deadline and a returned timeout/error. Make cleanup preserve endpoint ownership safely if locking fails. Add a controlled lock-holder test for startup and finalization.

## 13. MAJOR — Predictable abstract socket names can be squatted by another uid

Location: [src/ipc/socket.c:41](/home/tobias/Projects/editor/src/ipc/socket.c:41), and `EADDRINUSE` handling at line 276.

Linux abstract Unix socket names have no filesystem ownership protection. Any local uid can bind the predictable `edit-<victim-uid>` name before the victim starts.

Concrete repro: with `XDG_RUNTIME_DIR` unset, another uid binds that name. Victim initialization returns `IPC_EXISTS`; handoff then rejects the foreign peer credentials. The supplied startup contract propagates the error and exits. Credential checks prevent spoofed acceptance but do not restore availability.

Fix: use a trusted private filesystem endpoint for the production fallback, or another discovery mechanism protected by per-user filesystem permissions. Keep isolated abstract namespaces for tests. Add a foreign-uid incumbent case.

## 14. MAJOR — IPC reserves 33.56 MB outside the stated memory plan

Location: [src/ipc/socket.c:286](/home/tobias/Projects/editor/src/ipc/socket.c:286).

On the checked 64-bit ABI, each peer is 80 bytes. The arena therefore owns:

`32 × (1,048,576 + 80) + 4,096 = 33,561,088 bytes`.

G10 counts owned allocations; untouched arena pages do not establish compliance. Even under touched-page accounting, 32 almost-complete frames can touch nearly the entire arena. Receive storage is also retained after successful ACK for waiting clients.

The documented handoff peaks are 78.4 MB on A and 37.4 MB on B (E), against 87/44 MB caps (G). Adding this arena exceeds both planned peaks. The IPC bench has no memory assertion.

Fix: budget IPC explicitly, use a smaller shared receive pool with bounded admission, and release receive storage after decoding/callback completion. Consider fd-backed stdin transport if large simultaneous payloads are required. Add peak-owned-memory checks under maximum concurrent input.

## 15. MAJOR — Disconnect cleanup for wait associations is missing from the API

Location: [src/ipc/socket.c:125](/home/tobias/Projects/editor/src/ipc/socket.c:125), and [docs/decisions/P4.9.md:128](/home/tobias/Projects/editor/docs/decisions/P4.9.md:128).

The loop must associate wait tokens with tabs, but `drop_peer` provides no disconnect notification. There is also no non-destructive token-validity query.

Concrete repro: repeatedly request `--wait` for one existing tab, receive ACK, then disconnect without closing that tab. Server slots recycle, while a loop following the contract retains every association until tab closure. Dynamic storage grows indefinitely; fixed storage eventually rejects legitimate clients despite free server slots.

`report_closed` cannot safely serve as a liveness probe because it completes live wait requests.

Fix: expose bounded disconnect events or a token-validity query, and specify association retirement in the integration contract. Add repeated ACK/disconnect cycles against a permanently open tab. This requires a module interface change.

## 16. MAJOR — The primary-launch contract does not implement `--wait`

Location: [docs/decisions/P4.9.md:114](/home/tobias/Projects/editor/docs/decisions/P4.9.md:114).

The incumbent branch waits through `ipc_client_send`. The primary branch opens its local request, frees args and becomes the UI process. It specifies no launcher lifetime or local completion association.

Concrete repro: start `edit --wait commit-message` with no incumbent, open another tab, then close the commit-message tab. The original process still runs the editor loop, so the invoking Git process remains blocked until the whole editor exits. `--new-instance --wait` has the same unresolved lifetime.

Fix: specify a launcher/UI-process split or equivalent local completion channel, preserving the initial request’s association until its final tab closes. Cover primary, incumbent and isolated launches with contract tests.

## 17. MAJOR — IPC tests and fuzzing cannot detect several protocol/security regressions

Location: [tests/ipc_test.c:59](/home/tobias/Projects/editor/tests/ipc_test.c:59), and [fuzz/ipc_fuzz.c:24](/home/tobias/Projects/editor/fuzz/ipc_fuzz.c:24).

Unit wire tests assert rejection for truncation, surplus bytes and corrupted magic, but not the other promised malformed fields. Structural fuzz mutations accept either `IPC_OK` or `IPC_PROTOCOL`, without establishing which result is required. All transport peers inherit the same uid.

Concrete regression arguments: removing the reserved-byte rejection leaves these assertions satisfied when a mutated frame is accepted. Removing peer-credential checks also leaves every existing transport test using an accepted uid. Sanitizers cannot detect either semantic regression.

Fix: add table-driven, known-invalid envelopes with required `IPC_PROTOCOL` and cleared outputs, plus exact valid round trips and maximum-size cases. Add controlled foreign-credential rejection tests. Extend transport coverage to the bounded-work, expiration and disconnect cases above.

| # | Severity | Location | One line |
|---|---|---|---|
| 1 | BLOCKER | src/ipc/wire.c:55 | Aliased strings cause an output-buffer overrun. |
| 2 | BLOCKER | src/ipc/socket.c:32 | Runtime-path replacement permits attacker-directed chmod. |
| 3 | BLOCKER | src/ipc/args.c:37 | Non-directory prefixes can redirect requests to unrelated files. |
| 4 | BLOCKER | src/ipc/args.c:95 | Stat errors incorrectly enable coordinate suffix parsing. |
| 5 | BLOCKER | src/minimap/minimap.c:25 | Cache identity collisions return another buffer’s exact offsets. |
| 6 | MAJOR | src/indent/indent.c:36 | Typing queries synchronously scan unbounded lines. |
| 7 | MAJOR | src/minimap/minimap.c:238 | Sidebar fills damage the entire viewport. |
| 8 | MAJOR | src/minimap/minimap.c:197 | Pending indexing leaves the scrollbar blank and unusable. |
| 9 | MAJOR | src/minimap/minimap.c:99 | Dispersed samples can fault synchronously on the UI thread. |
| 10 | MAJOR | src/ipc/socket.c:159 | Drain has no effective UI work budget. |
| 11 | MAJOR | src/ipc/socket.c:164 | Incomplete clients permanently occupy every slot. |
| 12 | MAJOR | src/ipc/socket.c:265 | Lifecycle flock waits have no deadline. |
| 13 | MAJOR | src/ipc/socket.c:41 | Another uid can squat the production abstract endpoint. |
| 14 | MAJOR | src/ipc/socket.c:286 | Receive reservations exceed the available planned G10 margin. |
| 15 | MAJOR | docs/decisions/P4.9.md:128 | Wait associations cannot observe client disconnection. |
| 16 | MAJOR | docs/decisions/P4.9.md:114 | Primary and isolated `--wait` launches lack completion semantics. |
| 17 | MAJOR | fuzz/ipc_fuzz.c:24 | Protocol and credential regressions can pass the suite. |