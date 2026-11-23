# HANDOFF — sublimité v2

Read order: this file → `CLAUDE.md` → `docs/PLAN_V2.md`. Beads: `bd` in this worktree resolves to the v2 database
(prefix `v2`; docs/decisions/wave0-beads-and-oracle-pin.md §1). Push branch `v2` only, after every verified landing.

## Session 1 (2026-11-22 night → 2026-11-23 morning, ThinkPad) — state as of the last commit

### Wave 0 exit, first half: DONE — scaffold blockers closed
W0.1 vendor (c0e9e2c) · W0.3a include DAG from compiler dependency files (865095b) · W0.3b nm symbol DAG (45921fb)
· W0.3c fail-closed lanes, suite manifest `tests/lanes.mk`, tools registry (3f26948) · W0.4 app identity guard +
isolation lint (10cec71). `make check` green.

### Wave 0 exit, second half: contract suite (epic v2-he7, design docs/decisions/wave0-contract-design.md + "Advisor review" trims T1–T5)
Landed: W0.6a sandbox (7dfb7bb) · W0.6b X driver (28976cf) · W0.6c wire client `snapwire.py` (5a8f29f) ·
W0.6g pre-ACK recovery controls (996f3da; bead stays open for post-ACK) · W0.6f R3 + R5 Tier A, 8 PASS (0.30; bead open for 5 screen assertions).
In flight at the time of this commit (worktrees under ~/.t3/worktrees/sublimite-v2/, one branch each):
- v2-he7.11 W0.6d v1 adapter patch (Sol xhigh) — worker output on its branch (1c55dcf; patch applied in /home/tobias/Projects/sublimite-v1-inspect, disposable detached 6fec2e8). NOT landed: coordinator run on :138 failed test_disconnect_and_stock_disk_clipboard (editor exited 1 after reader disconnect); sent back to the same worker. Patch 452+/6− vs 320 (E).
- v2-he7.15 W0.6h large delete+undo X3 on the stock v1 build (Opus) — first running-v1 reproduction; stop if v1 does not show it.
- Wave 1 prep: piece pool derivation landed (e0f176f, docs/decisions/wave1-piece-pool.md), advisor review pending.
Open, not started: v2-he7.12 W0.6e navigation R1 + R6 (needs the adapter) · W0.6g post-ACK (needs the adapter) ·
v2-he7.16 W0.6i runner + gate (wire all contract self-tests into make check; registry slash-row fixture) ·
v2-he7.17 W0.6j R4 over-capacity restore (last, with its stop condition).

### Next session, in order
1. Land whatever the in-flight lanes left (each worker's worktree is committed on its branch; see the list above or
   the final session-end note below if one was written).
2. W0.6e and W0.6g post-ACK on the landed adapter, then W0.6i, then W0.6j.
3. **The Wave 0 gate run is the first full-suite run of the next session**: no other lane running, stamped with
   power state and load at start (AC preferred; [bat] acceptable on a quiet box). Then report to the advisor (checkpoint d).
4. Then open Wave 1 beads under epic v2-0fv (its description carries the exit criteria and acceptance items).

### Facts the next session needs
- Oracle: /home/tobias/Projects/sublimite-v1-oracle, detached 6fec2e8, built; `build/sysroot` holds
  wayland-egl-backend.h (export CPATH=$PWD/build/sysroot/usr/include). Stock editor: build/sublimite.
  Baseline: 120/120 sanitizer suites on a quiet box, one named v1 test race (pin decision §9).
- Xvfb: explicit display only, `-noreset -nolisten tcp`; lanes were given disjoint ranges :120–:169.
- PLAN_V2 is kept true by amendments A1–A5 (docs/decisions/wave0-plan-amendments.md).
- Routing: Codex's cyber filter refused test-case beads that describe a defect or a process kill (v2-he7.7, .15, .14;
  worklog). Those go to Opus or Muse from the start; Sol does lint, tool and patch beads. Muse and Gemini: up to pace only.
- Worker briefs carry: no rm -rf, no mktemp with a clean-up trap, nothing outside the checkout, build under build/,
  report instead of running anything that needs approval. T3 thread timestamps are UTC.
- Battery: Tobias could not plug in on 2026-11-23; stop line 20 % (advisor rule, worklog).
- v1 findings for Tobias (advisor reports them): post-rename save conflict and >32 MiB delete+undo (known-bugs doc),
  editor_session_test ivv_save race, test_install_omarchy EDITOR export.
