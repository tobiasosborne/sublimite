# HANDOFF — editor project, session 4 interrupted (updated 2026-10-09 10:10)

Read: this → `CLAUDE.md` → `PLAN.md` §1 and §5 → `bd ready --type task` and `bd ready -n 40`. Session details: `docs/worklog/2026-10-09.md` (session 4), `docs/worklog/2026-10-08.md` (sessions 2, 3). Session 1's long handoff is in git history (commit 20e9887); its §2 settled decisions still bind.

## Method (session 4, keep)
Every worker gets its own git worktree `.wt/<bead>` on branch `wt/<bead>` (`.wt/` is in `.git/info/exclude`), created from main. The coordinator verifies there (`make check`, **`make all`** (release test builds catch gcc-only warnings), the module bench), commits on the branch and cherry-picks onto main. Workers never run git. Run X11 tests only under `xvfb-run -a` (tests that own selections must never touch Tobias's display).

## Interrupted state (read first)
Tobias paused the machine at 10:05. All workers were killed (Codex) or told to stop (Claude). Every partial edit is committed as WIP on its `wt/*` branch (worktree still on disk under `.wt/`); nothing WIP is on main. Main is clean, `make check` green as of 09:50; **`make all` on main is red** (gcc `-Werror=misleading-indentation` in `tests/file_test.c`), fixed on `wt/edit-4w1.24`.

| Bead | Branch / worktree | Report | State |
|---|---|---|---|
| edit-4w1.24 P1.7b SIGBUS | wt/edit-4w1.24 (9f1f93b) | worklog | **finished** (worker: red/green, make all + check green, G5/G8s pass); verify + cherry-pick first (fixes main's make all) |
| edit-4w1.30 utf8 budgeted cluster width | wt/edit-4w1.30 (21c8232) | worklog | **finished**; coordinator ran make check green; cherry-pick after 4w1.24 |
| edit-4w1.21 P1.4b piece synthesis (critical path) | wt/edit-4w1.21 | none (killed after 59 min) | unknown progress; inspect diff vs variants/P1.4/bptree, then relaunch Codex xhigh with "continue in this worktree" |
| edit-4w1.32 P1.9d journal save transaction | wt/edit-4w1.32 | none (killed after 18 min) | unknown; relaunch Codex high |
| edit-e6x.5 P2.5 cpu raster | wt/edit-e6x.5 | worklog (Sonnet part) | backend + tests + fuzz + bench exist; G3 MISS 7.6/10.6 ms (--quick, loaded); Codex perf continuation killed after 18 min (unknown edits) |
| edit-e6x.4 P2.4 gl (par) | wt/edit-e6x.4-egl, wt/edit-e6x.4-glx | none (killed after 45 min) | unknown; relaunch both with "continue" |
| edit-4w1.10 P1.10 find (par) | wt/edit-4w1.10-simd-filter-verify, wt/edit-4w1.10-twoway-only | worklog | both pass the frozen suite at an earlier point; LAST EDIT in each is unverified (simd: AVX2 scan uncompiled; twoway: scan_pair regression, revert); all G6/G6v MISS on a loaded box (408–643 ms) |
| edit-e6x.11 P2.2b x11 | wt/edit-e6x.11 | worklog | input MINORs done; clip.c INCR/MULTIPLE/manager save written (Codex, aborted by provider safety filter); line-663 test fixed; `plat_init` fails intermittently (5/30) in x11_clip_test (cause unknown); new tests/x11_xvfb.h private Xvfb; P2.2b.md, G4a row, fuzz 300 s not done. Route to Claude, not Codex |
| edit-e6x.15 P2.3e font CFF | wt/edit-e6x.15 | none | Sonnet stopped mid-work; unknown |
| edit-4w1.6 P1.6 lineidx | on main | worklog | landed; OPEN only for a quiet-box bench (G7/G7j missed on a loaded box at 5.4 GB/s scan) |

`git stash list` stash@{0} "session3-interrupted": all six beads it belonged to are now closed (edit-4w1.20, e6x.14, 4w1.18, 4w1.23, e6x.10, yy5.9); it can be dropped (`git stash drop`) — not dropped yet.
An older worktree from session 3 is registered at `/tmp/claude-1000/.../b66f98cd.../scratchpad/wt` (detached 93498e5); `git worktree remove` it if not needed.

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
1. Verify + cherry-pick wt/edit-4w1.24, then wt/edit-4w1.30 (`make all` and `make check` on main green again); `git stash drop`.
2. Relaunch **edit-4w1.21** synthesis (Codex sol xhigh, same worktree, brief in the session-4 scratchpad or rewrite from the bead + HANDOFF item) — critical path; then edit-4w1.22 review, edit-4w1.25/.26 re-benches, edit-4w1.35 undo fixes.
3. Relaunch edit-4w1.32 (journal, Codex), edit-e6x.4 egl/glx (Codex), edit-e6x.5 raster perf (Codex), edit-e6x.11 finish (Sonnet), edit-e6x.15 (Sonnet), edit-4w1.10 variants (Sonnet or Codex; fix the unverified last edits first).
4. When the box is quiet: bench lineidx (edit-4w1.6), find variants, raster, gl, layout log_1g row (thin margin), utf8; power stamp each.
5. Then edit-4w1.33 (journal part 2), edit-zzj.9 (layout long lines + unicode cost), P3.2 view, P3.3 editor loop when its deps close.

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
