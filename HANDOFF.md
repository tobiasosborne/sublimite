# HANDOFF — sublimité, session 8 wound down (updated 2026-10-10 15:35)

Read: this → `CLAUDE.md` → `PLAN.md` §1 and §5 → `bd ready --type task` and `bd ready -n 40`. Session details: `docs/worklog/2026-10-10.md` (session 8), `docs/worklog/2026-10-09.md` (sessions 4–7), `docs/worklog/2026-10-08.md` (sessions 2, 3). Session 1's long handoff is in git history (commit 20e9887); its §2 settled decisions still bind.

## Method (session 4–5, keep)
**Public repo (17:15, 2026-10-09):** https://github.com/tobiasosborne/sublimite (AGPL-3.0-or-later), remote `origin`, main tracks origin/main. `git push origin main` after each coordinator status check-in and at session end (not per bead; the beads pre-push hook runs). Only main goes up; `wt/*` stay local. Nothing in the tree may contain secrets or private data (worker reports and decision docs are public). `bd` stays local: no federation/sync daemon.
Every worker gets its own git worktree `.wt/<bead>` on branch `wt/<bead>` (`.wt/` is in `.git/info/exclude`), created from main. The coordinator verifies there (`make check`, **`make all`** (release test builds catch gcc-only warnings), the module bench), commits on the branch and cherry-picks onto main. Workers never run git. **Display: nothing opens a window on :0** (P0.2c guard, 6a39d6d; Xvfb :99 must be running; binding until ~16:30 on 2026-10-09 and a good default after). Codex launch line: `env DISPLAY=:99 EDIT_DISPLAY=:99 timeout 5400 codex exec -m gpt-6.1-sol -c model_reasoning_effort=<high|xhigh> --approve-for-me --skip-git-repo-check -C .wt/<bead> -o docs/worker-reports/<bead>.md "<brief>"`.

## Session 8 state (2026-10-10 15:35; read first)
Fable as coordinator, ~75 min, Tobias: "go wide, use codex hard, claude up to pace". Machine had rebooted: Xvfb :99 restarted, corpus regenerated (`mkcorpus` + `truncate -s 10G sparse_10g.bin` + a 1 GiB 'a' stream for `all_a_1g.txt`), disk 95 %. Wave: 11 Codex sol high continuations (`timeout 2700`, 40-min budget, mandatory report at minute 30), the P4-modules-2 review (xhigh, read-only), 2 Sonnet workers. **Seventeen beads/slices landed on main and pushed** (round 2 added zzj.15, 63v, mdv §1 BLOCKER slice, yqu §31/32/38 slice; see worklog "Round 2") (each verified in its worktree with `make all` 0 and `make check` 0 with leaks on): 457.16 P4.I wiring (1c47575, unblocked five beads), e6x.27, 4w1.47, 457.19, 4w1.54, zzj.8 (experiment code; bead stays open), e6x.28, e6x.26, 4w1.43, 4w1.58, 4w1.59, the first zzj.13 slice, 4w1.56 — see the worklog table for commits and caveats. **P4.I is landed: the M1 modules are wired into the loop.**

**Not landed (WIP on `wt/*`, nothing lives only in a working tree):**
- **edit-457.10 large-file path** (Sonnet, green on base 81db552, wt/edit-457.10 8ed7b61): written against the single-buffer editor; P4.I made it multi-buffer (`editor_buffer`, `e->buffers[i]`). Needs a Codex port of `src/editor/large.c` onto `editor_buffer`, then land. Its findings are filed: **edit-czn (P1)** G1 typing row at 0.9 × lines on log_1g already fails on unmodified HEAD (each edit keeps layout busy 1–30 s), edit-9yd gutter marks estimated numbers.
- ~~edit-4w1.56~~ **landed** after a Codex conflict resolver (15 min) rebased it over 4w1.47 and P4.I in the mid-rebase worktree (docs/worker-reports/edit-4w1.56-s8b.md): the pattern to reuse for zzj.15. Its first leaks-on check flaked on cli_test under load 14 (filed edit-63v); re-run on a quiet box was green.
- ~~edit-zzj.15~~ **landed in round 2** (284f372) after a Codex resolver; **EGL is now the default backend** (raster fallback logged once). First real-display EGL check is the next batch.
- **edit-zzj.13 editor review fixes: first slice landed** (c1bd548; verified leaks on); bead stays in_progress; the report lists done vs remaining (finding 14 withdrawn; one inherited failure preserved and documented).
- **P4-modules-2 review landed**: `docs/reviews/P4-modules-2.md` (Codex xhigh, 55 min, read-only, reviewed through e9ce63a): 39 findings, 1 BLOCKER (savectl reload installs torn bytes when an external writer restores mtime), 36 MAJOR, 2 MINOR. Filed as four beads: **edit-mdv (P1)** savectl §1, 9-20; edit-ovu scroll §2-8; edit-lez findui §21-30; edit-yqu bench/test/fuzz honesty §31-39.

**New beads this session:** edit-5ih P6.4b user font selection (Tobias likes Iosevka; DejaVu stays default; blocked on P6.4), edit-czn (above), edit-9yd (above), **edit-2vs (P1)** raster `fence_job_fn` use-after-free seen once in file_kill_test under ASan (intermittent; freed by `file_test_queued_save_child`).

**Process lessons (binding for the next wave):**
1. `codex exec -o <path>` writes the model's FINAL MESSAGE to `<path>` at exit and **clobbers a report the worker wrote there earlier**. Use `-o docs/worker-reports/<bead>-s<n>-final.md` and tell the worker to write `docs/worker-reports/<bead>-s<n>.md`. This session's reports were recovered from the captured stdout transcripts (`recover_report.py` in the session scratchpad; recovered files carry some `+`-prefixed patch noise); e6x.26's handwritten report was only partially recoverable.
2. Land in dependency order and **rebase each remaining WIP branch immediately after every landing** that touches its files; two branches (457.10, 4w1.56) went from clean to semantic conflicts within the session because P4.I and 4w1.47 landed under them.
3. Workers add a "worker addendum" to HANDOFF.md and a `docs/worklog/<date>.md` of their own: the land script discards both (`git checkout HANDOFF.md; rm docs/worklog/<today>.md`) before squashing. Tell workers not to touch either.
4. Close a bead only after the land script prints `LANDED` (two beads were closed before a rebase conflict surfaced and had to be reopened).
5. **Verify AFTER rebasing, never before.** The s8 land script verified each worktree on its own base and only then rebased + cherry-picked; the zzj.13 slice rebased cleanly over e6x.28 yet broke e6x.28's new `editor_close_test`, so main was red from c1bd548 until the zzj.15 landing restored it. Order for every landing: squash → rebase onto current main → `make all` + `make check` (leaks on) on the rebased tree → cherry-pick → re-rebase every remaining WIP.
6. Workers cannot run LeakSanitizer; two of twelve worktrees that were "green" for the worker failed the leaks-on check. Keep the coordinator rerun.

**Next coordinator:** (1) Xvfb :99, `make all && make check` on main, `quota`; (2) continue zzj.13 from its report (fresh worktree from main); edit-2vs on Sonnet (Codex filter refuses it); next slices of edit-mdv (§9-20) and edit-yqu (§33-37, raise sample counts, drop the --track args files); (3) Codex port of 457.10 onto `editor_buffer`, land; (4) edit-mdv §9-20; (5) edit-czn before any G1 claim; (6) then the HANDOFF "Next" list below from item 4 (EGL real-display check, M0 bench edit-zzj.5, M1 beads toward P5.1b); (7) file the inherited P4.I contract gaps from docs/worker-reports/edit-457.16-s8.md (IPC wait-token sweep, minimap cached publication, indent LIMIT no-mutation for selections) as beads.

## Needs Tobias
- **edit-w34.4 P5.1b Astra vs Sublime comparison gate (M1→M2)**: every P6 bead, P5.2 and P5.3 depend on it; its verdict's blockers are Tobias's call (waive or fix).
- Law-2 scope (from P2.5): libxcb mallocs per reply/event; tests assert 0 allocations input → submit only (P2.0 addendum). Confirm that is the law.
- Decided today (no action): hot exit option (b) (edit-4w1.53 implements it), GL = EGL, third global (file SIGBUS service), name sublimité (lower case), title face provisional (wordmark bead edit-457.20 deferred behind P5.1b), evdev T0 / input group deferred (edit-e6x.17).

## Next (in order; after the session 8 items above)
1. Land zzj.15 (EGL default), then a real-display check of the editor on EGL (announce; Present/MSC lanes of e6x.26 and the G2c pairs of e6x.28 run there too).
2. M0 bench edit-zzj.5 (scroll wiring is in via P4.I), then M1 beads (457.8 hot exit, 457.10 large files, 457.12 M1 bench) toward the P5.1b gate.
3. Real-display batch: zygote run (`tools/zygote_bench.sh --real-display`), G2c pairs, prewake variants (docs/decisions/edit-zzj.8.md DRAFT has the command line and trial plan).
4. Push main after each status check-in and at session end.

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
