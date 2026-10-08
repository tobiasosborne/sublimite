# HANDOFF — editor project, end of session 2 (2026-10-08 14:28)

Read: this → `CLAUDE.md` → `PLAN.md` §1 → `bd ready --type task`. Previous session's long handoff is preserved in git history (commit 20e9887) and summarised in PLAN.md; its §2 settled decisions still bind.

## State
- Git repo on `main`, 10 commits. `make check` green (7 test binaries), `make all`, `make fuzz` green. Beads: 75 issues, 11 closed, snapshot in `.beads/issues.jsonl` (re-export with `bd export -o .beads/issues.jsonl` each session).
- Done (15 beads): P0 entirely, P1.2 scan (sse2), P1.3 + P1.3b piece spec/stub with coalescing and reinsert-by-reference, P1.8 work pool, P1.1 utf8 variants (unsynthesised, `variants/P1.1/`) + P1.1a generated UCD tables, P2.1 xcb window (0.9 ms to map request, 0 idle wakeups), P2.3 font tooling (partial). `make check`: 8 test binaries green.
- Perf measurements (battery rerun, GPU wake, photodiode) remain deferred by decision.

## Workflow in force (PLAN.md §1, memory `editor-agent-workflow`)
Fable = consultant, never codes, no Fable subagents. Opus coordinator spawns one Haiku 5.5 (default) or Sonnet worker per bead, ~100K tokens, then terminates it. Workers never run `bd`, `git`, or edit the Makefile (it globs). `mode:par` = implement every candidate, measure, Pareto; `mode:best` = 2–3 independent impls → Opus best-of; Codex gpt-6.1-sol xhigh for deep review; gpt-6-astra test drive after MVP. Tobias: "create possibilities, measure results, don't just theorise" and is happy to reimplement the kernel several ways and pick Pareto-optimal over use cases.

## Next (in order, all `bd ready --type task`)
0. **Token-budget hook** (haiku, new bead to create): PostToolUse hook that sums usage from the transcript and injects a stop instruction at 80K (Haiku) / 160K (Sonnet); use the update-config skill. Two Haikus declined beads today because briefs carried a wall-clock deadline; never do that again (CLAUDE.md law 3).
1. **P1.4a** frozen bench matrix (sonnet) → **P1.4** four kernel designs in parallel (4 sonnets) → Pareto → possible Opus synthesis → Codex review.
2. In parallel: P1.1b utf8 synthesis (opus); P1.2b scan_count recheck on an idle box (haiku); P0.6 record/replay (haiku, reopened untouched); P2.3b font fallback + no-malloc raster (haiku, reopened untouched); P2.2 x11 input (sonnet).
3. Then P1.5 undo, P1.6 lineidx, P1.7 file, P1.9 journal, P1.10 find, P2.4 gl (par, incl. P2.4b zero-copy).

## Gotchas
- CLAUDE.md law 9: red-green TDD; reports show the red run then the green run.
- Per-module link flags: one-line `src/<m>/LDLIBS` file, picked up by the Makefile. `libxcb-xinput` has no dev symlink here (XInput2 smooth scroll in P2.2 needs `libxcb-xinput-dev` or a dlopen).
- `bd` is single-writer (embedded Dolt); only the coordinator writes. `bd ready` lists epics unless `--type task`.
- clang 18 needs `--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/13` for libFuzzer; the Makefile does this, hand builds must too.
- Benchmarks taken while other workers run are unreliable (scan_count halved under load). Run gate benches on an idle machine; stamp power status.
- Gates written into a bead brief must come from `perf/01-perf-target.md`; two invented ones (trace 20 ns, utf8 2 GB/s) were wrong and were reset with recorded decisions.
- Stubs that satisfy a frozen API go in `src/`, not `variants/`, or `make check` breaks for everyone.
- `/tmp/edit-corpus` (2.4 GB) exists; `oneline_10g.txt` needs `--huge` and 10 GB free (disk at 93 %).
