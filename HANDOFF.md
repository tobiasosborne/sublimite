# HANDOFF — sublimité, session 7 opened (updated 2026-10-09 21:30)

Read: this → `CLAUDE.md` → `PLAN.md` §1 and §5 → `bd ready --type task` and `bd ready -n 40`. Session details: `docs/worklog/2026-10-09.md` (sessions 4, 5 and the crash recovery), `docs/worklog/2026-10-08.md` (sessions 2, 3). Session 1's long handoff is in git history (commit 20e9887); its §2 settled decisions still bind.

## Method (session 4–5, keep)
**Public repo (17:15, 2026-10-09):** https://github.com/tobiasosborne/sublimite (AGPL-3.0-or-later), remote `origin`, main tracks origin/main. `git push origin main` after each coordinator status check-in and at session end (not per bead; the beads pre-push hook runs). Only main goes up; `wt/*` stay local. Nothing in the tree may contain secrets or private data (worker reports and decision docs are public). `bd` stays local: no federation/sync daemon.
Every worker gets its own git worktree `.wt/<bead>` on branch `wt/<bead>` (`.wt/` is in `.git/info/exclude`), created from main. The coordinator verifies there (`make check`, **`make all`** (release test builds catch gcc-only warnings), the module bench), commits on the branch and cherry-picks onto main. Workers never run git. **Display: nothing opens a window on :0** (P0.2c guard, 6a39d6d; Xvfb :99 must be running; binding until ~16:30 on 2026-10-09 and a good default after). Codex launch line: `env DISPLAY=:99 EDIT_DISPLAY=:99 timeout 5400 codex exec -m gpt-6.1-sol -c model_reasoning_effort=<high|xhigh> --approve-for-me --skip-git-repo-check -C .wt/<bead> -o docs/worker-reports/<bead>.md "<brief>"`.

## Session 7 state (2026-10-09 21:30; read first, then the session 6 table below)
Short evening session (Tobias had 15 min; Fable as consultant did the coordinator's restoration work itself). **Repo moved** from `~/Projects/editor` to `~/Projects/sublimite`: every `.wt/<bead>` worktree registration was repaired by hand (`git worktree repair` alone did not fix it: both `.wt/<b>/.git` and `.git/worktrees/<b>/gitdir` still pointed at the old path). Xvfb :99 restarted; `/tmp/edit-corpus` was gone and was regenerated (2.4 GB; disk 95 %, no `--huge`). Main 13e5b12 re-verified: `make all` 0, `make check` 0 (4m51 wall, ASan/UBSan with leaks on). Quota 21:08: Claude 5-hour 0 %, Weekly 42.0 % (+0.3 % ahead of pace), Fable 39.0 %.

**All WIP branches rebased onto main** (each was one squashed WIP commit; ten rebased clean; 457.16 had three conflicts, resolved: `src/editor/open.c` keeps main's `aligned_alloc`+`memset` and adds the WIP's `poll_fd = -1; drag_tab = SIZE_MAX` init plus its epoll/unistd includes; `src/editor/STATUS.md` took the WIP version; `src/layout/STATUS.md` is the union). 457.16 after rebase: `make all` 0, `make check` has exactly one red (`editor_p4_test` tabs_script(raster): `editor_add_buffer("/tmp/edit-457.16-new.c")` fails, null backend passes). `wt/edit-457.16-snap` is now redundant (delete after 457.16 lands). zzj.13 and zzj.15 were NOT rebased (they sit on the snapshot; rebase them onto 457.16's landed state).

**Ten Codex sol high workers launched 21:23, detached (`setsid nohup`, `timeout 9000`), reports to `docs/worker-reports/<bead>-s7.md`, logs in the session scratchpad:** 457.16 finisher (verify WIP, fix the one red, acceptance through the loop, docs/decisions/P4.I.md, no new scope); continuations 4w1.54, 457.19, 4w1.43, 4w1.47, e6x.26, e6x.28, 4w1.56, 4w1.58, 4w1.59. Plus the **P4-modules-2 read-only review rerun** (xhigh, on main, → docs/reviews/P4-modules-2.md). Briefs: bead description + review doc pointer + CLAUDE.md rules + red/green + make all/check/fuzz. Not relaunched: zzj.8 (rebased, needs collect/verify, no worker), zzj.13/zzj.15 (wait for 457.16).

**Next coordinator:** (1) check which of the eleven reports exist and which processes still run (`pgrep -fa 'codex exec'`); (2) for each finished worktree: `make all`, `make check` with leaks on, module bench if the hot path moved, then squash on the merge-base, cherry-pick to main; land 457.16 first; (3) rebase zzj.13/zzj.15 onto main after that and relaunch; (4) file beads from P4-modules-2; (5) push main.

## Session 6 wind-down state (2026-10-09 17:40; superseded where the session 7 block says so)
Session 6 coordinator (Opus) 12:28–17:45, stopped by Tobias at 17:30. Main is green at the last verified point (17:20: make all, make check with leaks on, 48 test binaries, make fuzz) and pushed. **M0 is on main** (editor loop + `sublimite` binary + first human drive on :0); the M1 standalone modules (tabs, indent, minimap, ipc, keys, savectl, findui, scroll) are on main, their wiring (edit-457.16) is not. Every live worker was killed at 17:37 and every dirty worktree WIP-committed on its `wt/*` branch ("WIP s6 (wind-down 17:40)"); nothing lives only in a working tree. Codex launch line now uses `timeout 9000` (5400 killed long xhigh runs mid-work).

**Before anything:** Xvfb :99 (`Xvfb :99 -screen 0 2880x1800x24 -nolisten tcp -noreset`) if gone; `/tmp/edit-corpus` survives unless the machine rebooted (`./build/tools/mkcorpus`, never `--huge`).

Prep rule for every branch (learned today): squash on the merge-base (`git reset --soft $(git merge-base main HEAD)`), `git rebase main` and LOOK for CONFLICT; never `reset --soft main`; never amend while a rebase is in progress; remove `docs/worker-reports/<bead>-s6.md` from main's working tree before cherry-picking a commit that adds it; never commit a bench that needs uncommitted variants/.

| Bead | Branch (last commit) | State | Next action |
|---|---|---|---|
| edit-457.16 P4.I wiring (chokepoint) | wt/edit-457.16 (301a7d1 WIP) | substantively done per its rollout (suites green, 0 allocs over 10k keys with 100 tabs + IPC); killed while polishing; no report | rebase (known conflict src/editor/open.c, resolved in wt/edit-457.16-snap 90919f7), verify, Codex high finisher "verify, fix red, write report, no new scope", land |
| edit-zzj.13 editor review fixes (+P1.9-2 §2/3/5, WM_DELETE_WINDOW → exit within a frame) | wt/edit-zzj.13 (1efc6e1 WIP, on the 457.16 snapshot) | started 17:25, early | continue after 457.16 lands |
| edit-zzj.15 EGL default backend, raster fallback | wt/edit-zzj.15 (19b4672 WIP, on the snapshot) | started 17:25, early | continue after 457.16 |
| edit-4w1.54 journal re-review fixes | wt/edit-4w1.54 (e251eba WIP) | ~1 h in | continue |
| edit-4w1.47 lineidx UI slices | wt/edit-4w1.47 (03688a4 WIP) | ~45 min in | continue |
| edit-4w1.43 file keep/check/save semantics | wt/edit-4w1.43 (57b42ad WIP) | ~15 min in | continue |
| edit-457.19 font review fixes | wt/edit-457.19 (543bbe7 WIP) | ~45 min in | continue |
| edit-e6x.26 GL review fixes | wt/edit-e6x.26 (e8bcd57 WIP) | ~30 min in | continue |
| edit-e6x.28 G2c tools vs real Present clock (P3) | wt/edit-e6x.28 (883143a WIP) | ~30 min in | continue |
| edit-zzj.8 prewake experiment | wt/edit-zzj.8 (bb45fe0 WIP) | ~2 h, no report | collect/verify, decide on a real-display run |
| edit-4w1.56 work foreground lane / edit-4w1.58 find+findui P1-1 / edit-4w1.59 piece reclamation lock | wt/edit-4w1.{56,58,59} (WIP) | started 17:26, early | continue |
| review P4-modules-2 (scroll/savectl/findui) | read-only | see worklog (killed at 17:40 if unwritten: output lost) | rerun if lost |
| edit-457.15 zygote | main (29db433, harness eb0c614) | needs the real-display run | next real-display batch |

Held for Claude quota (weekly +1.2 % ahead at 17:07): edit-e6x.27 GL bench honesty (Sonnet). Blocked behind 457.16: zzj.12 (raster G11), zzj.13, zzj.14, zzj.15, 457.8 (hot exit), zzj.16. Other open: 4w1.55 (after 4w1.43), 4w1.57 (after 4w1.47), 457.20 wordmark (deferred behind P5.1b), e6x.17 (deferred).

**Cancelled real-display batch** (not yet announced): zygote run (`tools/zygote_bench.sh --real-display`, docs/decisions/P4.14.md "P4.14b harness"), G2c pairs (after e6x.28), prewake (zzj.8) if needed. Real-display rules: one announcement per batch via the consultant; mute Cinnamon notifications with a restoring trap; put windows on eDP-1 (internal 2880x1800 @ 90 Hz; HDMI-1 1920x1080 @ 60 Hz may be connected); record xrandr.

## Needs Tobias
- **edit-w34.4 P5.1b Astra vs Sublime comparison gate (M1→M2)**: every P6 bead, P5.2 and P5.3 depend on it; its verdict's blockers are Tobias's call (waive or fix).
- Law-2 scope (from P2.5): libxcb mallocs per reply/event; tests assert 0 allocations input → submit only (P2.0 addendum). Confirm that is the law.
- Decided today (no action): hot exit option (b) (edit-4w1.53 implements it), GL = EGL, third global (file SIGBUS service), name sublimité (lower case), title face provisional (wordmark bead edit-457.20 deferred behind P5.1b), evdev T0 / input group deferred (edit-e6x.17).

## Next (in order)
1. Xvfb :99; `make all && make check` on main.
2. edit-457.16: verify the WIP as is, finisher, land (unblocks five beads).
3. Rebase + relaunch the WIP continuations above (Codex sol, `timeout 9000`); zzj.13 and zzj.15 onto 457.16's landed state.
4. Land zzj.15 (EGL default), then a real-display check of the editor on EGL (announce).
5. M0 bench edit-zzj.5 (needs the scroll wiring), then M1 beads (457.8 hot exit, 457.10 large files, 457.12 M1 bench) toward the P5.1b gate.
6. Push main after each status check-in and at session end.

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
