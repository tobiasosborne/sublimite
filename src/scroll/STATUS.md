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
the renderer must consume the new scroll-owned Q8 origin/clip/overscan and
inverse hit-test contract; wrapped visual-row scrolling now has a bounded
adapter, but the host must supply exact resident wrap metadata and wire it;
actual displayed-frame G3z is unmeasured. The null work proxy is descriptive and cannot close displayed G3z.
The worker report records precisely which review findings remain open.

Commands and red/green evidence: `docs/worker-reports/edit-ovu-s9.md`.
Design/ownership choices: `docs/decisions/edit-ovu.md`.
All display checks use Xvfb :99. Sanitizers run with `detect_leaks=0` in this
worker environment; the coordinator must rerun LeakSanitizer enabled.

Prior slice verification: forced release `make -B all` and Clang ASan/UBSan
`make -B check` exited 0 after an environment clock rewind. The full suite
passed 59 binaries (M)[AC] plus replay CLI checks. Both unseeded and seeded
module fuzz runs completed 61 seconds (M)[AC] clean. Release allocation guards
remain active and report zero allocations (M)[AC]; sanitizer runs disable
LeakSanitizer for the coordinator's leak-enabled rerun. No gate verdict is
claimed for the loaded-box TRACK benchmark.

## edit-ovu2 review slice 2

`scroll_plan_frame` exports the Q8 drawing origin, clip, overscan reservation
and full-viewport damage decision; `scroll_frame_hit` is its inverse. Reserve
one extra layout row at setup without enlarging the visible surface. Actual
renderer translation remains outside this module.

`scroll_visual` maps wheel/page/follow through visual ordinals and exact
geometry descriptors, preserving trailing cursor affinity and leading
viewport seeds. A caller-owned source may expose an estimated total and lazy
resident metadata; bounded callback continuations discover EOF and publish
only a complete viewport. Reflow maps the old byte anchor in the new source
generation, retaining fractional displacement and saturating row carries.
The host/layout metadata producer and editor wiring remain required.

The public editor/null probe confirms submit/present ID correlation, but
wheel input has no frame and present submission is not displayed refresh.
The intentionally failing `scroll_test --require-editor-scroll` is retained
for the editor owner. P3.4 specifies the missing public refresh observer.
The ordinary suite labels displayed G3z UNAVAILABLE, never a gate pass.

Report: `docs/worker-reports/edit-ovu2-s9.md`.
Decisions: `docs/decisions/edit-ovu2.md` and `docs/decisions/P3.4.md`.

Slice 2 final verification: GCC 13 `make all` and Clang 18 ASan/UBSan
`make check` exit 0 (M)[AC]; all 59 test binaries (M)[AC] and replay CLI
checks pass. Sanitizers used `detect_leaks=0`; coordinator reruns leaks on.
The final mutating module fuzz run completed 62 seconds (M)[AC], 233 inputs
(M)[AC], clean. The release wrapped-path guard records zero allocations
(M)[AC]. Full display checks required running outside the sandbox's isolated
socket namespace, using only Xvfb :99. An intermittent refwin CSV failure
passed both standalone and full-suite retries; no unrelated fix was made.
Displayed G3z remains UNAVAILABLE.
