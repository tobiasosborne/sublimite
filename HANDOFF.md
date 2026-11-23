# HANDOFF — sublimité v2

Read order: this file → `CLAUDE.md` → `docs/PLAN_V2.md`. Beads: `bd` in this worktree resolves to the v2 database
(prefix `v2`; docs/decisions/wave0-beads-and-oracle-pin.md §1). Push branch `v2` only, after every verified landing.

## Session 1 (2026-11-22 night → 2026-11-23 morning, ThinkPad) — state as of the last commit

### Wave 0 exit, first half: DONE — scaffold blockers closed
W0.1 vendor (c0e9e2c) · W0.3a include DAG from compiler dependency files (865095b) · W0.3b nm symbol DAG (45921fb)
· W0.3c fail-closed lanes, suite manifest `tests/lanes.mk`, tools registry (3f26948) · W0.4 app identity guard +
isolation lint (10cec71). `make check` green.

### Wave 0: GATE PASSED (2026-11-23 22:50 CET; advisor acknowledged)
`make contract-gate` exit 0, run v1-20261123T215001Z-843898: PASS 96, XFAIL 1 (X3.over.roundtrip, A8 signature, dies in
Undo after a journaled delete). Commit c16bfc2, [AC] Charging 86 %, load 0.84. Evidence committed under
docs/gates/wave0/v1-20261123T215001Z-843898/ (gate.json, summary.txt, record.txt). Epic v2-he7 closed; backlogs .20,
.25, .34 remain backlog.
- What it means: the contract instrument is proven against v1 at the pin, with one reviewed expected failure.
- What it does not mean: nothing yet about v2. The per-target predicates (the zero Law-2 counts on R1, R3, R4, R5, R6;
  X3 as PASS) are what v2 will be held to when Wave 1 exits.
- Timing for the next announcement: the run took 137 s wall (M), because the runner runs the cases in parallel on
  displays :190–:195 (capacity, 115.6 s, is the critical path). Ask the Paris-talk thread for about five minutes, not
  fifty. Use tools/run_gate.sh (start time, give-up time, derived quiet condition: no build/test processes, no foreign
  job, 1-min load <= 2.8 = the regime the watchdogs were measured in).
- W0.7 path: phase 1 B1–B5, B7 (0.79–0.83), X3 fences (0.84), phase 2 B6 (0.85, 0.86), phase 3 Astra runner read
  GATE-READY (docs/reviews/wave0-runner-astra.md).

### Wave 1 (A10: L0/L1 now; L2/L5 after the Wave 0 gate)
Landed piece 7a (1.16): frozen piece.h except its Lines section (second Astra read pending; A19 and
docs/decisions/wave1-piece-lines.md), flat reference model tests/piece_model.h, model-run suite + fuzzer. Running on
the model: find (.19, Sol), undo (.8, Sol; Astra reads its diff before landing). 7b and lineidx (.18, rescoped by
A19) wait for the Lines freeze. Landed L0: os (1.1), base (1.2), trace (1.3), work (1.4), ipc (1.5), os stream + one wait set (1.8, 1.11),
fault ids (1.10) + A15 follow-up (1.12), ASSERT migration (1.14). Landed L1: scan (1.6), utf8 (1.7), indent (1.9,
1.13). In flight: piece 7a (v2-0fv.7: frozen piece.h + reference model + suite; FREEZE GATE: advisor reads piece.h
before the suite is finished). Filed, waiting on 7a: 7b implementation (.17), undo (.8), lineidx (.18), find (.19).
Held: env hooks (.20). L2 edit placeholder (.15) carries acceptance lines. Specs: docs/decisions/wave1-piece-pool.md
(signed), wave1-os-fault-lease.md (+A18). Amendments A1–A18 in docs/decisions/wave0-plan-amendments.md.

### Next session, in order
1. Land whatever lanes left (each worktree is committed on its branch: "wip ..." commits).
2. W0.7 phase 1 → phase 2 (B6) → phase 3 (Astra read) → Wave 0 gate run (`make contract-gate`, [AC], quiet box).
3. piece 7a freeze → 7b, undo, lineidx, find in parallel lanes.

### Facts the next session needs
- Quality bar (Tobias, 2026-11-23): genius-level engineering; push back on sloppy work. Owner-read every landing,
  one send-back round; designs state invariant, simplest alternative, defeating counter-example (worklog).
- Oracle: /home/tobias/Projects/sublimite-v1-oracle, detached 6fec2e8, built; `build/sysroot` holds
  wayland-egl-backend.h (export CPATH=$PWD/build/sysroot/usr/include). Stock editor: build/sublimite.
  Baseline: 120/120 sanitizer suites on a quiet box, one named v1 test race (pin decision §9).
- Xvfb: explicit display only, `-noreset -nolisten tcp`; lanes were given disjoint ranges :120–:169.
- PLAN_V2 is kept true by amendments A1–A5 (docs/decisions/wave0-plan-amendments.md).
- Reading quota: when t3quota's AGE for a provider is older than your last dispatch on it, the row does not include that
  dispatch: read the system `quota` tool instead (it also reports Muse).
- Quota routing (advisor, 2026-11-23 12:42: Codex is reserved for Astra reviews — fault-id diff, Wave 0 whole-wave review,
  piece/undo diffs; Sol writes no code unless Tobias relaxes the pace rule; code goes to Opus, two workers): Codex weekly reset ~11:56; up to pace only (≈0.6 pt/h): Astra first, at most
  one Sol lane when t3quota shows room > 0. New code beads go to Opus (≤ 2 workers, stop at Claude room 3). Muse one lane;
  Gemini prose only. L0 beads count as critical path under the battery plan.
- Routing: Codex's cyber filter refused test-case beads that describe a defect or a process kill (v2-he7.7, .15, .14;
  worklog). Those go to Opus or Muse from the start; Sol does lint, tool and patch beads. Muse and Gemini: up to pace only.
- Worker briefs carry: no rm -rf, no mktemp with a clean-up trap, nothing outside the checkout is created or removed,
  HOME for any tool that writes caches is under the checkout's build/, build under build/, report instead of running
  anything that needs approval; never profile the oracle (perf record -N if ever). T3 thread timestamps are UTC.
- Battery: Tobias could not plug in on 2026-11-23; stop line 20 % (advisor rule, worklog).
- v1 findings for Tobias (advisor reports them): post-rename save conflict and >32 MiB delete+undo (known-bugs doc),
  editor_session_test ivv_save race, test_install_omarchy EDITOR export.
