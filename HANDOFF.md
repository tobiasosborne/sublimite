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

### Wave 1 (L2/L5 unblocked by the Wave 0 gate, A10)
Landed L0: os (1.1), base (1.2), trace (1.3), work (1.4), ipc (1.5), os stream + one wait set (1.8, 1.11), fault ids
(1.10, 1.12, 1.15), ASSERT migration (1.14), no test env vars + dead exports (1.20). Landed L1: scan (1.6), utf8 (1.7),
indent (1.9, 1.13), piece 7a = frozen piece.h incl. Lines + flat model + model-run suite (1.16–1.18, erratum d028acf,
piece_span_nl_join with undo), find (1.19), undo (1.21). lineidx removed (A19). Early leaves (A21): dnd (4.1).
In flight (state at 23:45 CET 2026-11-23):
- 7b real piece tree (v2-0fv.17) on Opus xhigh, continuing the Sol wip (build/review/7b-sol-wip.diff); landing gate =
  piece/find/undo suites green on the real tree; exact memory gate 25,950 slots (A20); landing read split (Astra on the
  production source only, a fresh Claude/Muse reviewer runs the suites and fault traces).
- L2-0a os watch object + metadata/xattr copy (Opus): green, one send-back round for a fuzz harness (Law 1).
- LF-2 TOML parser (Muse), no pool (A21).
- Q2 save protocol: advisor rules 1–7 (no retention; synced PREPARED with the temp's identity and S; by-value restores
  from snapshot capture; peer and kept-state self-contained checkpoints before rename; one buffer per file identity);
  Astra's pass: T1–T4 hold, T5 (foreign rename after our rename, then crash before the conflict checkpoint) breaks;
  advisor ruling on T5 pending (option: keep the new file reachable by a private hard link until FINISHED, or accept
  as product contract). Then Q2/Q4/Q11 rewritten, L2-2/L2-3 briefs.
L2/L5 plan: docs/decisions/wave1-L2-L5-harvest.md (beads L2-0..L2-6, L5-1..L5-3), derivations wave1-L2-derivations.md,
wave1-L2-q2-crash-consistency.md, wave1-L2-q2-protocol.md; amendments A19–A22. Provisional brief for v2-0fv.15 (edit):
build/briefs/v2-0fv.15-edit.md (untracked; rewrite after undo landed: done — undo_begin/replace/commit/abort, join flag,
burst policy in edit, one maintenance function incl. undo_maintain). Filed: v2-0fv.21 os export deletion + nm gate.

### Next session, in order
1. Land whatever lanes left (each worktree is committed on its branch as "wip ..." or reported with a path).
2. 7b landing (split read), then L2-1 file (after L2-0a) and the L2 briefs from the Q2 ruling; v2-0fv.21.
3. Wave 1 exit numbers recorded in A22 (Q6 G1 under writeback; Q3 10 GiB recovery measurement in L2-2).

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
- Routing (advisor, 2026-11-23, after five Codex cyber-filter stops: v2-he7.7, .15, .14, 7b on Sol, Astra's undo
  re-read): any Codex job, implementation OR review, whose work includes running or writing tests that inject faults,
  truncate mapped files or kill processes goes to Claude or Muse from the start. A filtered job is never resent to Codex
  in other words. Codex keeps derivation and source reading without execution (Astra design reads, production-source
  derivation checks) and lint/tool/patch beads (Sol). Landing reads of fault-heavy code are split: Astra reads the
  production source as a derivation check (runs nothing); a fresh Claude/Muse reviewer reads and runs the suites; if
  Astra is filtered, the Claude/Muse reviewer takes the source too. Muse and Gemini: up to pace only; Tobias rates Muse
  for review. Both Claude and Codex up to pace (Tobias, 2026-11-23); Opus stop line 2; four Opus lanes while Claude
  room > 4, two below.
- Worker briefs carry: no rm -rf, no mktemp with a clean-up trap, nothing outside the checkout is created or removed,
  HOME for any tool that writes caches is under the checkout's build/, build under build/, report instead of running
  anything that needs approval; never profile the oracle (perf record -N if ever). T3 thread timestamps are UTC.
- Battery: Tobias could not plug in on 2026-11-23; stop line 20 % (advisor rule, worklog).
- v1 findings for Tobias (advisor reports them): post-rename save conflict and >32 MiB delete+undo (known-bugs doc),
  editor_session_test ivv_save race, test_install_omarchy EDITOR export.
