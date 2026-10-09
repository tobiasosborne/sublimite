# HANDOFF — editor project, session 3 (updated 2026-10-09 08:30)

Read: this → `CLAUDE.md` → `PLAN.md` §1 and §5 → `bd ready --type task`. Session details: `docs/worklog/2026-10-08.md` (sessions 2, 3). Session 1's long handoff is in git history (commit 20e9887); its §2 settled decisions still bind.

## State
- `main`, `make check` green (14 test binaries), `make all` / `make fuzz` green. Beads snapshot in `.beads/issues.jsonl` (re-export each session).
- Landed in session 3: P1.2b/P1.2c scan (G6 re-demonstrated on AC, 16 GB/s), P0.7 token-budget hook, P0.6 input record/replay (+P0.6b flake fix), P0.2b TSan-safe malloc guard, P1.1b utf8 best-of, P1.4a frozen piece bench matrix, P2.2 x11 input (+P2.2c loop drain fix), P2.3b font fallback/no-malloc raster (+P2.3d NOMEM fix).
- **P1.4 kernel decided** (Tobias): **bptree is the base**; four variants measured in `docs/decisions/P1.4.md` (variants in `variants/P1.4/`, git-ignored, only on disk). Next: fix bench row edit-4w1.20, then Codex sol xhigh synthesis edit-4w1.21 into `src/piece/`, then Codex review edit-4w1.22 (non-gating), then delete losing variant dirs and add a "Decision" section to P1.4.md.

## Binding rules added 2026-10-08 evening (PLAN §1.1, §5.6–5.7, CLAUDE.md laws 10–11)
1. **Pace ceiling.** Run `quota` before every dispatch wave and before any single new Claude worker. If any Claude row (5-hour session, Weekly, Fable weekly) shows PACE more than `+2.0% ahead`, spawn no new Claude workers until it is back under; route work to Codex. Paste the `quota` lines you act on into the worklog. Respect the 5-hour TODAY column (a 12-worker Haiku/Sonnet wave ≈ 1M tokens).
2. **Codex sol is a worker pool.** Opus-grade and Sonnet-grade beads (synthesis, mode:best, kernel designs, hard debugging, reviews) go to `gpt-6.1-sol` (high; xhigh for synthesis/reviews). Claude Haiku only for small standard beads while pace allows; Opus is coordinator only. `gpt-5.6-luna` never writes code. Invocation:
   `timeout 5400 codex exec -m gpt-6.1-sol -c model_reasoning_effort=high --approve-for-me --skip-git-repo-check -C /home/tobias/Projects/editor -o docs/worker-reports/<bead-id>.md "<self-contained brief>"` run with Bash `run_in_background`, one per bead. **60 s after launch, check a new file appeared under `~/.codex/sessions/$(date +%Y/%m/%d)/`; if not, kill and relaunch** (a worker hung 9 h at startup on 2026-10-08). Never block the session on one worker.

## Next (in order)
1. **edit-4w1.20** P1.4a-b: retarget line_jump row to a line inside the file (G7j on a warm/indexed query, cold query TRACK), piece.h rule "no threads inside the kernel", bench_variant.sh stops `rm -rf`-ing worker outputs. (Codex sol high.)
2. **edit-4w1.21** P1.4b synthesis from bptree (Codex sol xhigh): drop in-kernel pthreads, borrow flat's cursor cache (typing), hybrid's edit-window gap blocks (delete/undo), rope's compact nodes (snapshot memory); frozen suite + fuzz 120 s + TSan + every gate on an idle box; `docs/decisions/P1.4-synthesis.md`.
3. Then in parallel: **edit-4w1.22** Codex review of src/piece; **P1.5 undo** (against piece.h, as soon as the synthesis lands); P1.6 lineidx, P1.7 file, P1.9 journal, P1.10 find, P2.4 gl (par), P2.5 cpu raster; review-fix beads P1.1c, P1.1d, P2.3c, P2.2b, P0.6c.
4. Freeze `render.h` before fanning out P2.4/P2.5 (PLAN §5.2).

## Gotchas
- Law 9 red-green: put "paste the red run" in every brief's acceptance (two P1.4 workers skipped it).
- Token hook (`.claude/settings.json` → `tools/hooks/token_budget.py`) stops Haiku at 80K, Sonnet at 160K; `EDIT_BUDGET_OFF=1` disables. 80K is too tight for Haiku module beads (they stop before writing code): give Haiku only narrow beads.
- Verify a worker's bead in an isolated `git worktree` with only its files copied when others are mid-edit.
- `make bench` uses one-line `bench/<name>.args` (piece_bench: `--quick`); the full piece matrix is `tools/bench_variant.sh <dir>` on an idle box. scan_bench misses on battery (powersave ~1.2 GHz): stamp power before reading any bench as a regression.
- TSan builds: the malloc guard is now inert under TSan (P0.2b), so `src/base` can be linked.
- Per-module link flags: one-line `src/<m>/LDLIBS`. `bd` is single-writer; `bd ready --type task`.
- clang 18 needs `--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/13` for libFuzzer (Makefile does it).
- Gates in briefs must come from `perf/01-perf-target.md` §0.2 *and* be sanity-checked against the fixture (the line_jump row asked for a line past EOF).
- `/tmp/edit-corpus` (2.4 GB) exists; disk 93 %, no `--huge`.
