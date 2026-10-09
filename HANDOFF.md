# HANDOFF — editor project, session 6 running (updated 2026-10-09 13:15)

Read: this → `CLAUDE.md` → `PLAN.md` §1 and §5 → `bd ready --type task` and `bd ready -n 40`. Session details: `docs/worklog/2026-10-09.md` (sessions 4, 5 and the crash recovery), `docs/worklog/2026-10-08.md` (sessions 2, 3). Session 1's long handoff is in git history (commit 20e9887); its §2 settled decisions still bind.

## Method (session 4–5, keep)
Every worker gets its own git worktree `.wt/<bead>` on branch `wt/<bead>` (`.wt/` is in `.git/info/exclude`), created from main. The coordinator verifies there (`make check`, **`make all`** (release test builds catch gcc-only warnings), the module bench), commits on the branch and cherry-picks onto main. Workers never run git. **Display: nothing opens a window on :0** (P0.2c guard, 6a39d6d; Xvfb :99 must be running; binding until ~16:30 on 2026-10-09 and a good default after). Codex launch line: `env DISPLAY=:99 EDIT_DISPLAY=:99 timeout 5400 codex exec -m gpt-6.1-sol -c model_reasoning_effort=<high|xhigh> --approve-for-me --skip-git-repo-check -C .wt/<bead> -o docs/worker-reports/<bead>.md "<brief>"`.

## Session 6 live state (coordinator, updated 13:15; read first)
Session 6 coordinator (Opus) running since 12:28. Session 5 crash recovery is done (worklog "Session 6"). Workers run in `.wt/<bead>` worktrees; briefs and logs are in the session-6 scratchpad (lost on reboot: rebuild briefs from the bead + the worker's STATUS.md/decision doc). If this session dies, every live worktree's edits are on disk: WIP-commit them on their `wt/*` branch and relaunch "continue".

| Bead | Worker | Worktree | State |
|---|---|---|---|
| edit-zzj.3 P3.3 editor loop (M0, top priority) | Codex xhigh 12:40 | .wt/edit-zzj.3 | running; deps rewired e6x.4 → e6x.5 (raster backend behind render.h) |
| edit-4w1.10 find simd-filter-verify | Codex xhigh 12:34 | .wt/edit-4w1.10-simd-filter-verify | running; twoway-only done on wt/edit-4w1.10-twoway-only; pick = one quiet run |
| edit-4w1.33 journal P1.9e (+ edit-4w1.31 folded in) | Codex xhigh 12:55 | .wt/edit-4w1.33 | running; worktree cut from the damaged main (6257fe9): rebase before verifying |
| edit-4w1.35 undo fixes | done 13:12 | .wt/edit-4w1.35 | verifying; findings 2-8 fixed; BLOCKER 1 needs the piece.h checkpoint amendment (consultant decision pending) |
| edit-4w1.36 piece BLOCKER 1-2 + MAJOR 11 | Codex xhigh 13:12 | .wt/edit-4w1.36 | running; .37 (G10f) and .38 (perf) chained after it |
| edit-457.1 word wrap | Codex high 13:03 | .wt/edit-457.1 | running |
| edit-457.2 / .4 / .5 / .9 indent, tabs, minimap, ipc | Codex high 13:08 | .wt/edit-457.{2,4,5,9} | running as standalone modules; integration bead edit-457.16 |
| reviews x11, raster, view | Codex xhigh read-only 13:08-13:13 | main | → docs/reviews/x11-1.md, P2.5-1.md, view-1.md |
| edit-e6x.4 GL egl/glx | done | .wt/edit-e6x.4-{egl,glx} + variants/P2.4 | pick from a real-display bench (announce to the consultant first; present rows SKIP under Xvfb) |
| edit-zzj.9, edit-e6x.5, edit-4w1.6, edit-457.11 | landed | main | close after ONE quiet investigation run (lineidx G7/G7j, layout log_1g + unicode, raster G3/G3z, scan_count) when load < 2 |

Bench policy (13:15, docs/decisions/bench-policy.md): one quiet [AC] run per module when it lands; never run or read gates on a loaded box.

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
