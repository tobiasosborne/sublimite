# edit-e6x.26 — session 8 worker report

Scope: gl-1 sections 1–6, 8, 12 and 13. Continued WIP commits "'`797cab9` and
`2829889`; baseline main was `81db552d441551bc16b9299397dc70df5a219d41`.
All git operations were read-only. No benchmark source or out-of-scope module
implementation was edited. Display access used only Xvfb `:99`.

## Per-finding result

| Review section | Result and test |
|---|---|
| 1 | Done: unconditional context binding, owned-resource teardown and shared-display isolation. Alternating real EGL draw/readback/shutdown checks distinct pixels and a surviving backend. |
| 2 | Done: immutable native handles plus X geometry snapshot; startup draws do not read mutable UI height. Poisoned-height pixel test and worker draw concurrent with actual ConfigureNotify; TSan passes. |
| 3 | Done: upload dirty-state commit after success; pre-swap errors/fences; continuation reserved before swap; single committed swap; terminal failures. Injected upload/fence failures, owned-fence deletion, retry, failed native swap and no subsequent swap/submit. Session 8 also fixed a red case where a failed persistent backend still returned a writable lease. |
| 4 | Done: shared startup/runtime display-mode policy rejects Skip, accepts Copy/Flip/SuboptimalCopy. Typed completion tests verify no Skip T6. |
| 5 | Done: private PIXMAP event owns MSC; untyped notices before/after it cannot change that value or strand a delayed fence. |
| 6 | Done: bound worker mailbox receiver automatically continues bounded private-event/fence polls; deadline/reset/wait failures latch. Eventfd-sleeping test drains stale events and a delayed fence without manufacturing polls. Cleanup cancels/unbinds worker leases. |
| 8 | Done: capacity admission before adapter descriptor scans; bounded referenced-pixel work. Tests exercise a large unused descriptor table, zero damage, runtime atlas pixel-limit boundary, rejection ownership and allocation guard. |
| 12 | Done: removed raw native-serial T6 in actual X11 dispatcher. Startup serials plus original editor frames produce exactly one correctly correlated renderer T6 each. |
| 13 | Done: shared native contracts inspect X window pixels across actual grow/shrink, changed cell dimensions, fractional margins and repeated native-surface reuse. No-op resize is rejected. Close/destroy pending, repeated cleanup and fresh initialization pass. Native diagnostics now skip cleanly when EGL is absent. |

Design choices are recorded in `docs/decisions/edit-e6x.26.md`.

## Red evidence

The named tests and most implementations were already present in the inherited
WIP. Their original session red logs were not included. This session reconstructed
the baseline in an isolated `build/s8-red` tree using read-only `git show` of
main'"'s "'`variants/P2.4b/renderer.inc` and X11 dispatcher, then ran the inherited
regressions. Extra state fields and diagnostic declarations were compatibility
scaffolding only; original production function bodies were retained. The old
readback binding reset was restored in the fixture, and its mocked connection
was routed through the old platform field for the continuation test. These are
baseline regression reds, not a claim to have recovered historical TDD ordering.
Each targeted run exited nonzero (M) [AC]:

```text
'"
---

## Coordinator note (s8)

The worker's final handwritten report (with pasted red/green runs) was overwritten at exit by `codex exec -o`, which writes the model's final message to the same path. The text above is the report body recovered from the transcript before the worker's last edits (which inserted the filtered red log); the red/green evidence survives in the worker's STATUS.md and in docs/decisions/edit-e6x.26.md. Final message:

Completed scoped implementation and [worker report](docs/worker-reports/edit-e6x.26-s8.md).

`make all` and `make check` pass. TSan passes; both fuzzers ran 61 seconds clean (M) [AC]. Typing allocations remain zero (M) [AC].

Xvfb’s Present/MSC lane skips; LeakSanitizer was disabled as requested. The report distinguishes reconstructed baseline reds from this session’s red-green fixes. No git writes or benchmark edits.