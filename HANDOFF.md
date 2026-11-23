# HANDOFF — sublimité v2

Read order: this file → `CLAUDE.md` → `docs/PLAN_V2.md`. Beads: `bd` in this worktree resolves to the v2 database
(prefix `v2`; docs/decisions/wave0-beads-and-oracle-pin.md §1). Push branch `v2` only, after every verified landing.

## Session 1 (2026-11-22 night → 2026-11-23 morning, ThinkPad) — state as of the last commit

### Wave 0 exit, first half: DONE — scaffold blockers closed
W0.1 vendor (c0e9e2c) · W0.3a include DAG from compiler dependency files (865095b) · W0.3b nm symbol DAG (45921fb)
· W0.3c fail-closed lanes, suite manifest `tests/lanes.mk`, tools registry (3f26948) · W0.4 app identity guard +
isolation lint (10cec71). `make check` green.

### Wave 0 exit, second half: contract suite landed; W0.7 review says HOLD (state at 20:3x UTC 2026-11-23)
All W0.6 beads landed (sandbox, X driver, wire client, v1 inspector patch, R1/R6, R3/R5, recovery pre/post-ACK,
X3, runner + gate record, R4 with survivor identity). Astra's whole-wave review docs/reviews/wave0-code-astra.md
found 12 false-PASS risks; triage (advisor): phase 1 beads B1 runner v2-he7.27 (0.79), B5 files+adapter .31 and
B3 navigation .29 (0.80), B4 capacity .30 (0.81), B2 X3 .28 (0.82), B7 scaffold .32 (0.83) — all landed; B2 has
one more round open (advisor ruling 2: journal-commit fence between Delete and Ctrl+Z, re-measure, amend A8; if v1
survives Undo after the fence, STOP and report). Phase 2 B6 cross-cutting .33 (brief carries ruling 1: R6/R1 count
IDs required on both targets — v1 instrument health, v2 count == 0 in the manifest; fresh X server per scored
launch; settle CONTRACT_DIGESTS env vs argument) starts after the B2 round lands. Phase 3 Astra reads
run.py/records.py; THEN the gate run ([AC], quiet box incl. the Paris-talk thread's jobs paused; slot after 05:00
CET Tue unless phase 2 + Astra finish for 23:00–03:00; state load and power in the gate record). Finding 10
(fatal UBSan) done by the coordinator (0.77). Backlog .34. Fault epoch: plain increment + ASSERT (1.15).

### Wave 1 (A10: L0/L1 now; L2/L5 after the Wave 0 gate)
Landed L0: os (1.1), base (1.2), trace (1.3), work (1.4), ipc (1.5), os stream + one wait set (1.8, 1.11),
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
