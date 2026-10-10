# Scroll — P4.3b review fixes (edit-ovu)

Implemented in this worktree: source failures preserve the viewport; bounded,
caller-owned resolve/follow continuations share the byte/callback/wall deadline
across all stages; a resident-source bridge copies immutable snapshot windows
on the bulk worker and adopts them through work mailboxes; the jump benchmark
uses asynchronous lineidx seeking, with waiting/adoption/correct submission
charged and cancellation/timeout assertions retained. No new globals or
allocation on the typing path.

Use `scroll_resolve_slice` / `scroll_follow_cursor_slice` for resident sources.
Keep the same zero-initialized `scroll_resolver` across MORE slices and check
input between them. Positive budgets are literal; zero selects the default
64 KiB (G). The shared callback limit is 256 (G), the default wall deadline is
0.5 ms (G), and each source span is capped at 4 KiB (G). Source callbacks must
be bounded and nonblocking. Compatibility calls perform one slice and discard
adapter progress; fragmented sources need the continuation API.

For faultable/mapped snapshots, initialize a `scroll_resident` outside the
input path and use the resident wrappers. Only its bulk worker reads raw
snapshot pointers. Missing windows keep navigation pending, failed partial
windows are discarded, and physical completion gates window reuse/source
retirement. The source/bridge/pool must outlive close returning MORE.

Integration remains incomplete: editor wiring is a separate host change;
renderer origin/clip/overscan and shared hit-test/damage transforms are absent;
wrapped visual-row scrolling has no adapter; actual displayed-frame G3z is
unmeasured. The null work proxy is descriptive and cannot close displayed G3z.
The worker report records precisely which review findings remain open.

Commands and red/green evidence: `docs/worker-reports/edit-ovu-s9.md`.
Design/ownership choices: `docs/decisions/edit-ovu.md`.
All display checks use Xvfb :99. Sanitizers run with `detect_leaks=0` in this
worker environment; the coordinator must rerun LeakSanitizer enabled.

Final verification: forced release `make -B all` and Clang ASan/UBSan
`make -B check` exited 0 after an environment clock rewind. The full suite
passed 59 binaries (M)[AC] plus replay CLI checks. Both unseeded and seeded
module fuzz runs completed 61 seconds (M)[AC] clean. Release allocation guards
remain active and report zero allocations (M)[AC]; sanitizer runs disable
LeakSanitizer for the coordinator's leak-enabled rerun. No gate verdict is
claimed for the loaded-box TRACK benchmark.
