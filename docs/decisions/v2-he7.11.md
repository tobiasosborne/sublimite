# W0.6d pinned v1 inspector

The oracle patch targets detached v1 commit `6fec2e8c41220e284066ae8479d8f1b573febdb2`. The build recipe checks that pin, the index and the complete worktree diff, including the newly added inspector source. It checks the patch against the clean preimage before applying it. It builds stock and patched release binaries from the same pin, with private build outputs, and records the patch SHA-256 and executable digests in `build/contract-oracle/manifest.json`. The runner checks those digests before passing the executables to the sandbox. The patch SHA-256 is `54bcf05f3e1277190b418fc36cfa6da082f33e94cd0c5622e45f7bc1127fda1a`.

## Patch size and touched files

(M, source line count) The reduced patch changes 11 files with **383 additions and 6 deletions**, or 389 changed lines. The tracked diff adds 82 and deletes 6 lines. The untracked new `src/editor/inspect.c` adds 301 lines. That is 71 fewer changed lines than the disconnect-only revision (460 minus 389).

The original patch changed 458 lines (M, 452 additions plus 6 deletions) against about 320 (E, 166 source anchors plus 153 comparable lines, rounded). Its larger surfaces were descriptor preflight and recovery-safe ownership, journal/foreground and successful-submit fences, checked wire and final-chrome projection, and live guard accounting. T2 removed the anticipated minimap palette and pixel projection. The reduced patch is 69 lines above the rounded estimate (389 minus 320). Startup validation, the full cells/tabs/bottom wire projection and the task's live foreground measurement are still required.

| Original v1 files | Audit |
|---|---|
| `src/editor/open.c` | Removed. POSIX main owns the mapping and inherited socket around `editor_main`. It attaches the optional callbacks after ordinary `editor_open`, so an internal recovery retry has no inspector storage to duplicate or unwind. |
| `src/x11/private.h` | Removed. The inspector interns and owns the marker atom through the existing native accessor. X11 forwards a ClientMessage addressed to its window through a single optional generic callback in the raw dispatch. |
| `src/main.c`, `src/editor/editor.h`, `src/editor/private.h`, `src/editor/editor.c` | Keep leading option validation, forwarding refusal, the private borrowed inspector pointer and the narrow lifecycle seams. The public header exposes only the opaque startup/attach/close interface. The editor uses its existing `on_ingress`, `on_io` and `on_submit` callbacks. Its new call sites deliver markers, service one repaint, and end measurement before the close flush. |
| `src/x11/plat.h`, `src/x11/x11.c` | Keep the optional callback and its ordered dispatch. A separate socket event or input-ring slot would lose the X server's ordering or risk dropping the marker. |
| `src/font/async.c`, `src/font/font.h` | Keep one read-only lookup. The submitted page-zero rectangle identifies baked ASCII, including replacement aliases. Only the font owner can identify the worker's explicit inked U+FFFD result on a runtime page. |
| `src/editor/inspect.c` | Keep launch capability, transaction state and serialization together. |
| `src/base/base.h`, `src/base/malloc_guard.c` | Added to give the existing release interposer a calling-thread scope. The existing process-wide measurement API remains available for the pinned tests. This excludes concurrent save and checkpoint workers without teaching those subsystems about the inspector. |

## State and paths retained

| State or path | Invariant and failure prevented |
|---|---|
| `fd`, `mapping`, `capacity` | Main owns one connected nonblocking UNIX seqpacket endpoint from its same-UID parent and one grow/shrink-sealed, writable memfd mapping. Startup checks the descriptors and marks them close-on-exec. The mapping is prefaulted, the memfd descriptor can close once mapped, and allocation and file work stay out of snapshot production. An invalid launch or one that would forward fails before opening a profile. |
| Marker atom and dispatch | The atom is interned only on attach. The X11 callback receives candidates for the owned window after earlier translated input has been delivered. It checks the atom and format and ends the platform turn for a valid envelope. It consumes only the adapter's own messages. |
| `ticket` and ticket state | The latest admitted ticket also identifies the pending reply. The one-outstanding runner contract makes separate current and previous tickets unnecessary. IDLE rejects malformed envelopes and reserves a single transaction. WAIT_NATURAL requires a submit when NEXT equals the current serial. WAIT_PAINT permits one repaint once the lower bound has passed or SNAP needs current pixels. WAIT_SUBMIT consumes that repaint allowance and still requires backend success. |
| Existing `active_frame.id` and foreground fence | The adapter uses v1's last successful frame ID, including permissible gaps for dropped attempts, instead of maintaining a second serial. It snapshots through the existing submit hook after the prefix's queued input, journal staging, clipboard, command, find selection, resize and layout/scroll continuations have settled. Those can otherwise expose pixels from before an accepted action. The stock submission gates still apply, including backend readiness. A full-damage repaint uses the normal frame path. |
| Socket retirement | One nonblocking peek at marker receipt catches EOF or unexpected input, and one nonblocking reply send catches peer closure or saturation while a transaction is running. The fd sentinel retires transport and resets the ticket state. The editor continues and the observer still measures admitted input. |
| Writer and grid projection | The mapping offset and capacity enforce the wire bound, producing SIZE for shortage. v1's successful submit already validates grid storage and glyph descriptor bounds. The final grid supplies the gutter-excluding document rectangle, Q8 transform and translated overscan row, caret/underline/wide attributes, and frame-local color equality classes. Tabs use v1's composed strip and its partial last slot. A dropdown covers that row. The bottom section describes the final last row. This prevents invented text, hidden tabs or clipped geometry from passing a fixture. |
| Measurement flag, counts and hooks | v1's admitted-input hook opens the calling UI thread's scope before mutation. The existing I/O hook excludes native transport and between-slice services, and the successful typed-frame hook closes and counts the scope. Clean close ends an unfinished count before shutdown I/O. The completed-scope count lets the runner reject a vacuous typing check. The allocation count only feeds the shutdown report. A link-time fault control induces exactly one allocation, verifies further editing and SNAP, and expects a clean exit with the nonzero report. |

The T1–T3 trims still apply. SNAP and NEXT emit one settled frame, ARM returns INVALID, and the provided-sections word includes cells, tabs and the bottom row. The minimap bit stays clear. Baked ASCII and known replacement artwork are resolved from the submitted binding, and other runtime artwork is unresolved.

## Disconnect follow-up

The editor exited through the allocation guard after the reader went away. The socket path detected EOF on peek or a failed nonblocking send and set `DISCONNECTED`. The old guard still began another scope for dirty frames or queued input in that state. The process-wide interposer then counted allocations from ordinary work overlapping the foreground slice. A temporary allocator caller trace resolved one reproduced allocation to `editor_records_fill` in `src/editor/checkpoint.c`, whose `calloc(c->count ? c->count : 1, sizeof *c->records)` allocates the `journal_record` array used in save preparation. That checkpoint worker is outside Law 2's input-to-submit scope. The guard translated the nonzero count to `EDITOR_ERR_IO` and the executable exited with status 1. The temporary trace is absent from the patch.

The disconnect-only revision stopped opening scopes at disconnect. The reduced patch removes that workaround because measurement is passive. The test drives both transport paths on separate editor launches. Closing the peer before the marker produces EOF. Shutting down only its receive half leaves the editor's peek live and causes the reply send to return `EPIPE`. The test waits for `POLLHUP` in the second case to observe the editor closing its endpoint before editing. Both cases type, save, copy and quit cleanly, and their destination and clipboard bytes agree with the stock and patched ordinary controls.

For the disconnect-only revision, the isolated run count was ten, one per permitted display from `:130` through `:139` inclusive (139 − 130 + 1). The fixed test therefore exercises twenty disconnects, with EOF and EPIPE on each display. Each command is a separate first attempt on a fresh Xvfb. Results below are measured on battery. The host's one-minute load was sampled at 8.70 before the original run and 3.88 before the fixed run.

| Display | Original patch | Fixed patch |
|---|---|---|
| `:130` | Fail, 3 counted allocations | Pass |
| `:131` | Fail, 3 counted allocations | Pass |
| `:132` | Pass | Pass |
| `:133` | Fail, 1 counted allocation | Pass |
| `:134` | Fail, 1 counted allocation | Pass |
| `:135` | Pass | Pass |
| `:136` | Fail, 1 counted allocation | Pass |
| `:137` | Pass | Pass |
| `:138` | Pass | Pass |
| `:139` | Fail, 1 counted allocation | Pass |

The temporary caller-trace binary passed on `:130` and `:131` with the original EOF-only test. With both disconnect paths, it failed on `:132` and exposed the allocator caller, then passed on `:133`, `:134` and `:136`. All runs of that revision reported zero allocations in guarded slices. Its full gate ran once on `:138` and all eight tests passed. Both v2 make commands exited successfully.

```sh
oracle=/home/tobias/Projects/sublimite-v1-inspect
python3 -B tests/contract/build_oracle.py --jobs 6 "$oracle"
for display in :130 :131 :132 :133 :134 :135 :136 :137 :138 :139; do
  python3 -B tests/contract/test_oracle_inspector.py --build-dir "$oracle/build/contract-oracle" --display "$display" test_disconnect_and_stock_disk_clipboard
  printf '%s exit %s\n' "$display" "$?"
done
make check-contract-oracle ORACLE_TREE="$oracle" ORACLE_DISPLAY=:138 ORACLE_JOBS=6
```

## Reduced-patch verification

The first calling-thread probe still reported nine allocations on `:137` after a deliberate quit. Its temporary trace passed a standalone run on `:137`. Across the ten reserved displays it failed on `:132` and passed on `:130`, `:131` and `:133` through `:139`. The diagnostic printed after normal shutdown and resolved the last allocation to glibc's `__alloc_dir`. The unfinished input scope had survived into cleanup. The final patch closes that scope at the start of v1's normal close path.

I repeated the commands above against the reduced patch, with the same derivation of one first attempt per reserved display. All isolated commands exited zero on `:130`, `:131`, `:132`, `:133`, `:134`, `:135`, `:136`, `:137`, `:138`, and `:139`. Each covered EOF and EPIPE in separate launches and measured zero allocations after disconnect. The full file ran once on `:138` and passed all nine tests (M, unittest summary). The injected allocation control measured exactly one call, then confirmed further typing, a fresh SNAP and exit zero. Ordinary guarded cases measured zero. These reduced-patch runs used AC power with one-minute load sampled at 33.60 before the isolated series. `make all` and `make check` passed in v2.

## Reproduction

The test display uses `640 × 480 × 24` (E fixture geometry). v1's launch policy takes 60% of the screen width and 70% of its height. That gives a 384 × 336 pixel window, with room for complete and partial 16-cell tab slots. The fixture confirms that the last visible slot is narrower than a complete slot. The mapping uses the screen's pixel dimensions as upper bounds on cells, one overscan row, at most one tab per column and a maximum of three UTF-8 bytes for the reported replacement scalar. The test borrows the X driver watchdog and requires a free, authenticated display in the task's reserved range.

Red, run before editing the v1 source, used the copied unpatched release binary from `make -j6 all` in the prepared tree with the supplied `CPATH`:

```sh
make xdriver
python3 -B tests/contract/test_oracle_inspector.py --editor build/sublimite-v1-stock --display :137 --expect-unpatched
python3 -B tests/contract/test_oracle_inspector.py --editor build/sublimite-v1-stock --display :137 test_idle
```

Stock printed its ordinary usage and exited 2, the argument-parser refusal status. The refusal control passed and the idle SNAP test failed at launch with that exit.

Initial green:

```sh
make check-contract-oracle ORACLE_TREE=/home/tobias/Projects/sublimite-v1-inspect ORACLE_DISPLAY=:137 ORACLE_JOBS=6
make all
make check
```

The suite distinguishes stale replies, a dropped submit, an overwide clipped tab, ARM, and a missing minimap. It also covers idle and typed SNAP, one repaint for a no-op, NEXT after a later edit, clipboard paste, SIZE, and disconnect followed by editing, saving and copying. Stock, patched ordinary and patched inspected runs compare identical destination bytes and clipboard bytes. All guarded runs must report an active interposer with zero allocations (G, Law 2).

The green runner reported eight passing tests (M, unittest summary). Every guarded editor reported `active=1 allocations=0` (M, shutdown counts, [bat], one-minute host load sampled after the run at 10.19). Both v2 make commands exited successfully.

## W0.7-B5 (2026-11-23): adapter waits end on their own events

Wave 0 review [finding 15](../reviews/wave0-code-astra.md) found a shared
deadline in `disk_clipboard`. It busy-polled the clipboard owner, then polled
the disk every 1 ms for the *correct* bytes. Expiry fell through to the final
comparison, so a delayed save became FAIL. An incorrect first save also
passed if a later write corrected it.

- **Save.** An `IN_MOVED_TO` watch on the destination is armed before Ctrl+S
  (`directory_watch` / `wait_for_file_event`, moved here unchanged from
  `cases/files.py`). The case waits for the rename, or the editor's pidfd,
  under the 15 s watchdog, then reads the first committed bytes once. A
  missing rename is `TimeoutError` (ERROR), and an editor exit is `Refused`
  (ERROR).
- **Owner.** Select All and Copy follow the committed save. Ownership has
  its own 15 s deadline and the 10 ms observation cadence of
  `cases/recovery.py` ([finding 28](../reviews/wave0-code-astra.md)). A
  sentinel or zero owner keeps waiting. A third owner raises `RuntimeError`
  and expiry raises `TimeoutError` (both ERROR). Copy is not reissued.
- **Compare.** The saved bytes and the clipboard bytes are compared only
  after both are ready. A mismatch is the only FAIL.
- **Dropped submit.** `next_event()` waits under a single deadline for the
  first fault-control byte that is not `P`. A stream of repaints until expiry
  is ERROR. Previously each `P` renewed the full wait.
- **HUP.** If the `POLLHUP` wait in the EPIPE disconnect path expires, the
  result is `TimeoutError` (ERROR), not a failed assertion.

**Proof** (scratch harness: fake driver, `sleep` stand-in child, adapter
`WATCHDOG` patched to 3 s (E, above the 1 s late delay), scored by
`records.run_suite`):

| Scenario | Before | After |
|---|---|---|
| owner and save each 1 s late | PASS | PASS |
| no rename | FAIL | ERROR |
| completed wrong bytes | FAIL (after the watchdog) | FAIL |
| wrong first save, corrected by a second | PASS | FAIL |
| foreign owner | ERROR (at the read, after the watchdog) | ERROR (immediately) |
| owner never transfers | ERROR (at the read) | ERROR (`TimeoutError`) |
| `P` stream past the deadline | not testable (the inline loop is unbounded) | ERROR |

The full adapter suite then passed 9/9 once on `:130` against the existing
`build/contract-oracle`, without a rebuild (AC, one-minute load 10.72 at
start, M).

```sh
python3 -B tests/contract/test_oracle_inspector.py \
    --build-dir /home/tobias/Projects/sublimite-v1-inspect/build/contract-oracle \
    --driver "$PWD/build/xdriver" --display :130
```
