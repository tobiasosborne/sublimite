# HANDOFF — editor project, session 6 running (updated 2026-10-09 15:40)

Read: this → `CLAUDE.md` → `PLAN.md` §1 and §5 → `bd ready --type task` and `bd ready -n 40`. Session details: `docs/worklog/2026-10-09.md` (sessions 4, 5 and the crash recovery), `docs/worklog/2026-10-08.md` (sessions 2, 3). Session 1's long handoff is in git history (commit 20e9887); its §2 settled decisions still bind.

## Method (session 4–5, keep)
**Public repo (17:15, 2026-10-09):** https://github.com/tobiasosborne/sublimite (AGPL-3.0-or-later), remote `origin`, main tracks origin/main. `git push origin main` after each coordinator status check-in and at session end (not per bead; the beads pre-push hook runs). Only main goes up; `wt/*` stay local. Nothing in the tree may contain secrets or private data (worker reports and decision docs are public). `bd` stays local: no federation/sync daemon.
Every worker gets its own git worktree `.wt/<bead>` on branch `wt/<bead>` (`.wt/` is in `.git/info/exclude`), created from main. The coordinator verifies there (`make check`, **`make all`** (release test builds catch gcc-only warnings), the module bench), commits on the branch and cherry-picks onto main. Workers never run git. **Display: nothing opens a window on :0** (P0.2c guard, 6a39d6d; Xvfb :99 must be running; binding until ~16:30 on 2026-10-09 and a good default after). Codex launch line: `env DISPLAY=:99 EDIT_DISPLAY=:99 timeout 5400 codex exec -m gpt-6.1-sol -c model_reasoning_effort=<high|xhigh> --approve-for-me --skip-git-repo-check -C .wt/<bead> -o docs/worker-reports/<bead>.md "<brief>"`.

## Session 6 live state (coordinator, updated 15:40; read first)
Session 6 coordinator (Opus) since 12:28. M0 is on main: editor loop (65990f5), binary `sublimite` (2589a9e), first human drive on :0 OK (docs/decisions/zzj.3.md). GL = EGL (confirmed, docs/decisions/P2.4.md); find = simd-filter-verify (docs/decisions/P1.10.md). Bench policy: CLAUDE.md law 1 (no quiet-box runs; picks interleaved on the box as it is; perf beads only from observed lag). Routing (Tobias 15:25): Codex sol default for everything; Claude Sonnet only for Codex-refused classes (x11/clipboard/parser/security wording) and chores; Claude +2 % ceiling binds. Codex launch: `timeout 9000` (5400 killed three xhigh workers mid-work).
If this session dies: every live worktree's edits are on disk: WIP-commit on its `wt/*` branch and relaunch "continue" (briefs in the dead session's scratchpad are lost; rebuild from the bead + the worker's STATUS.md/decision doc). Squash only on the merge-base (`git reset --soft $(git merge-base main HEAD)`), never `reset --soft main` (that reverted four landings once: ea67346). Remove `docs/worker-reports/<bead>-s6.md` from main's working tree before cherry-picking a commit that adds it.

| Bead | Worker | State |
|---|---|---|
| edit-457.16 P4.I wire tabs/indent/minimap/ipc/keys into the loop | Codex xhigh 14:42 | running; chokepoint for zzj.12, zzj.13, zzj.14, 457.8 |
| edit-zzj.10c view review fixes (port of a timed-out run's patch) | Codex xhigh 15:04 | running |
| edit-zzj.4 scrolling as src/scroll + integration contract | Codex xhigh 15:19 | running |
| edit-4w1.53 journal option (b) + PRD §7 | Codex xhigh 15:19 | running; then edit-457.8 hot exit |
| edit-4w1.42 file UI-thread blocking; edit-4w1.44 file bench honesty | Codex | .44 done 15:30 (verify); .42 running |
| edit-457.7 save/external change as src/savectl | Codex high 15:19 | running |
| edit-zzj.7 / zzj.8 / 457.15 experiments (variants/, :99) | Codex high 15:19 | running |
| edit-4w1.52 find best-of fold-in of twoway | Codex xhigh 15:21 | running |
| edit-4w1.49 work fixes; edit-457.22 minimap/indent fixes | done | verifying |
| edit-e6x.24 clipboard off the UI thread | Sonnet done (budget) | needs make check/fuzz by the coordinator |
| edit-457.21b ipc fixes finisher | Sonnet 15:30 | running |
| edit-zzj.11 layout CRLF / >4 GiB | brief written | launch once edit-457.1 closes |

Open chains: piece review perf edit-4w1.38 (after .37 landed 1f029d9) → P1.5e undo flip edit-4w1.40 (checkpoint now on main); work selective receive edit-4w1.51; raster batch enqueue edit-e6x.25 (after .49); editor review fixes edit-zzj.13/.14 (after 457.16); font fixes edit-457.19 (after zzj.11); lineidx slices edit-4w1.47; file semantics edit-4w1.43 (after .42).
GLX loser cleanup: delete variants/P2.4 and wt/edit-e6x.4-{egl,glx}, wt/edit-4w1.10-twoway-only after edit-4w1.52.

## Needs Tobias
- **edit-w34.4 P5.1b Astra vs Sublime comparison gate (M1→M2)**: every P6 bead, P5.2 and P5.3 depend on it; its verdict's blockers are Tobias's call (waive or fix).
- Law-2 scope (from P2.5): libxcb mallocs per reply/event; tests assert 0 allocations input → submit only (P2.0 addendum). Confirm that is the law.
- Decided today (no action): hot exit option (b) (edit-4w1.53 implements it), GL = EGL, third global (file SIGBUS service), name sublimité (lower case), title face provisional (wordmark bead edit-457.20 deferred behind P5.1b), evdev T0 / input group deferred (edit-e6x.17).

## Next (coordinator)
1. Verify + land what finishes (worktree: make all, make check with leaks on, make fuzz; prep script squashes on the merge-base; then main make all + check).
2. When edit-457.16 lands: stack edit-zzj.13 (editor review fixes), edit-zzj.12 (raster G11 idle), then edit-457.8 (hot exit, after 4w1.53).
3. M0 bench run edit-zzj.5 once zzj.4 scrolling is wired; real-display runs need the consultant's go.
4. Keep 12–16 workers live, mostly Codex; reviews for each new module (law 8).

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
