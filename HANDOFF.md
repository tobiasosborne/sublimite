# HANDOFF — editor project, session 5 crashed (updated 2026-10-09 12:20)

Read: this → `CLAUDE.md` → `PLAN.md` §1 and §5 → `bd ready --type task` and `bd ready -n 40`. Session details: `docs/worklog/2026-10-09.md` (sessions 4, 5 and the crash recovery), `docs/worklog/2026-10-08.md` (sessions 2, 3). Session 1's long handoff is in git history (commit 20e9887); its §2 settled decisions still bind.

## Method (session 4–5, keep)
Every worker gets its own git worktree `.wt/<bead>` on branch `wt/<bead>` (`.wt/` is in `.git/info/exclude`), created from main. The coordinator verifies there (`make check`, **`make all`** (release test builds catch gcc-only warnings), the module bench), commits on the branch and cherry-picks onto main. Workers never run git. **Display: nothing opens a window on :0** (P0.2c guard, 6a39d6d; Xvfb :99 must be running; binding until ~16:30 on 2026-10-09 and a good default after). Codex launch line: `env DISPLAY=:99 EDIT_DISPLAY=:99 timeout 5400 codex exec -m gpt-6.1-sol -c model_reasoning_effort=<high|xhigh> --approve-for-me --skip-git-repo-check -C .wt/<bead> -o docs/worker-reports/<bead>.md "<brief>"`.

## Crash state (read first)
The laptop hard-reset at ~12:09 on 2026-10-09 (nordvpn); the coordinator died at 12:07 inside the piece-matrix AC run. Main is clean at 6a39d6d (+ this checkpoint commit); `make all` and `make check` were green on main at 11:58. Every worktree's on-disk edits are WIP-committed on its `wt/*` branch ("WIP s5 (crash checkpoint 12:07)"). Full per-bead detail: worklog "Session 5 crash". Nothing is running.

**Before anything else:** (1) `make all && ./build/tools/mkcorpus` (`/tmp/edit-corpus` was lost with /tmp; 2.4 GB, disk at 93 %); (2) start `Xvfb :99 -screen 0 2880x1800x24 -nolisten tcp -noreset`; (3) `quota`.

| Bead | Worktree | State | Action |
|---|---|---|---|
| edit-e6x.4 gl egl / glx | .wt/edit-e6x.4-egl, -glx | done, reports `docs/worker-reports/edit-e6x.4-{egl,glx}-s5.md` | verify (make all + check + fuzz build), cherry-pick, AC bench rows on :99 (present rows SKIP under Xvfb) |
| edit-e6x.5 raster | .wt/edit-e6x.5 | done, report `-s5.md`; G3/G3z need quiet AC | verify, cherry-pick, AC bench |
| edit-zzj.2 view | .wt/edit-zzj.2 | done, report, `src/view/STATUS.md` | verify, cherry-pick |
| edit-zzj.9 layout long lines | .wt/edit-zzj.9 | done, report, `src/layout/STATUS.md`; 150 us unicode verdict needs quiet AC | verify, cherry-pick, AC bench |
| edit-4w1.10 twoway-only | .wt/edit-4w1.10-twoway-only | done, report | verify; AC bench; variant decision with simd |
| edit-4w1.10 simd-filter-verify | .wt/edit-4w1.10-simd-filter-verify | worker killed while rerunning check + 300 s fuzz after a last Two-Way cancellation boundary fix; no report | relaunch Codex xhigh "continue: rerun make check, 300 s fuzz, bench, write report" |
| edit-4w1.32 journal save txn | .wt/edit-4w1.32 | code + tests done (22 tests, 1000 SIGKILL trials, fuzz clean), report never written | relaunch Codex xhigh "continue: verify state, write P1.9.md + report only" |
| edit-4w1.35 undo review fixes | .wt/edit-4w1.35 | red regression tests written, no fixes yet | relaunch Codex xhigh "continue" (BLOCKER-1 may need a piece.h amendment → consultant OK) |
| edit-4w1.22 piece deep review | none (read-only on main) | killed at 8 min, no output | relaunch Codex xhigh `-s read-only -o docs/reviews/P1.4-1.md`; brief from the bead |
| edit-e6x.11 x11 finish | .wt/edit-e6x.11 | P2.2b.md + STATUS written, -noreset fix, fuzz was running; no report | relaunch Claude Sonnet "finish: fuzz 300 s result, G4a row, report" (not Codex: safety filter) |
| edit-4w1.6 lineidx | main | AC run 12:01: G7 warm 117/207 ms vs 80/125, G7j 53.7 ms vs 3 ms MISS on a loaded box | re-bench quiet; if G7j still misses by >10x, open a bug |
| edit-4w1.26 undo bench gate | main | AC 12:01: in-tree 10k 1.78/3.50 ms (G9 63/84) | make the row gated, close |

Piece matrix r1 on AC (12:05): 36 PASS, 0 MISS. r2/r3 + bptree baseline were killed; rerun (`tools/bench_variant.sh src/piece`, ~95 s each) on a quiet box and record under docs/decisions/P1.4-runs/ before closing edit-4w1.25.

## State
- Landed in session 4 (main): P1.4 decision (bptree base), P1.4a-b bench row fix, P0.6c, P0.4b harness power, P1.7 file, P1.1c + P1.1d utf8, P2.3c font, P2.0 **render.h frozen**, P1.5 undo, P1.9 journal, P1.8b work fix, P1.10a **find.h frozen**, P3.1 layout, P1.6 lineidx (bead open for bench).
- Reviews: docs/reviews/P1.5-1.md (undo: 1 BLOCKER, 5 MAJOR → edit-4w1.35, waits on synthesis), docs/reviews/P1.9-1.md (journal: 2 BLOCKER, 7 MAJOR → edit-4w1.32, edit-4w1.33; contract decision edit-4w1.34).
- `make bench` on main is red until the synthesis lands (stub kernel misses piece rows); find/lineidx/raster gate rows miss on the stub or a loaded box.

## Needs Tobias
- **edit-4w1.34 (label human)**: hot-exit loss window vs PRD "no data loss on crash" (options in the bead; coordinator recommends write() to the page cache per edit, fdatasync every 1 s).
- edit-e6x.11 items: join group `input` for tools/evtrace (EVIOCSCLOCKID), and a real-touchpad check of XI2 smooth scroll / no doubled wheel events.
- Law-2 scope question (from P2.5): libxcb mallocs per reply/event, so the frozen render_test's "0 allocations over 10k frames" cannot hold on an X backend's present/completion path; workers assert 0 allocations up to submit only. Decide whether law 2 is "input → submit" (as written) and the conformance suite should say so.
- Power: the battery was "Discharging" for part of the session; several numbers are (M)[bat] or loaded-box.

## Next (in order)
1. Corpus + Xvfb :99 + quota (above). Commit the six `docs/worker-reports/*-s5.md` if not yet committed.
2. Verify + cherry-pick the five finished worktrees (egl, glx, raster, view, layout) and twoway-only; close or re-bench each.
3. Relaunch the four Codex continuations (simd, journal, undo fixes, piece review) and the Sonnet x11 finisher; check `~/.codex/sessions/$(date +%Y/%m/%d)/` 60 s after each launch.
4. Quiet-box AC benches: piece matrix r2/r3 + bptree baseline, lineidx, layout log_1g, find variants, raster G3, gl rows; power stamp each.
5. Then edit-4w1.33 (journal part 2), edit-4w1.25/.26, edit-e6x.16 vsync row, P3.3 editor loop when its deps close.

## Binding rules added 2026-10-08 evening (PLAN §1.1, §5.6–5.7, CLAUDE.md laws 10–11)
1. **Pace ceiling.** Run `quota` before every dispatch wave and before any single new Claude worker. If any Claude row (5-hour session, Weekly, Fable weekly) shows PACE more than `+2.0% ahead`, spawn no new Claude workers until it is back under; route work to Codex. Paste the `quota` lines you act on into the worklog. Respect the 5-hour TODAY column (a 12-worker Haiku/Sonnet wave ≈ 1M tokens).
2. **Codex sol is a worker pool.** Opus-grade and Sonnet-grade beads (synthesis, mode:best, kernel designs, hard debugging, reviews) go to `gpt-6.1-sol` (high; xhigh for synthesis/reviews). Claude Haiku only for small standard beads while pace allows; Opus is coordinator only. `gpt-5.6-luna` never writes code. Invocation:
   `timeout 5400 codex exec -m gpt-6.1-sol -c model_reasoning_effort=high --approve-for-me --skip-git-repo-check -C /home/tobias/Projects/editor -o docs/worker-reports/<bead-id>.md "<self-contained brief>"` run with Bash `run_in_background`, one per bead. **60 s after launch, check a new file appeared under `~/.codex/sessions/$(date +%Y/%m/%d)/`; if not, kill and relaunch** (a worker hung 9 h at startup on 2026-10-08). Never block the session on one worker.

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
- Gate fixtures: `log_1g.txt` has 8,947,842 lines, so "line 10^7" does not exist; benches derive floor(0.9 x lines) = 8,053,057. periodic.txt matches a×31+b every 32 bytes (dense, not near-miss); the G6v near-miss fixture is `/tmp/edit-corpus/all_a_1g.txt`; `sparse_10g.bin` is the sparse 10 GiB G5 fixture.
- Codex safety filter aborted the clipboard (X selection protocol) worker: route such beads to Claude.
- Codex/LSan: LeakSanitizer cannot run inside the Codex sandbox (ptrace); workers use detect_leaks=0, so the coordinator must re-run `make check` with leaks on.
- Verify with `make all` too: gcc release builds of tests catch warnings clang `make check` misses.
