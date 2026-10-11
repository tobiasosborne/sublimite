# HANDOFF — sublimité, end of session 10 (2026-10-11 ~15:45)

## Read first
**Main (pushed) has, all landed today after serial targeted runs on :99:** process reset + golden-master rule; Astra drive 1 report (VERDICT no) + Opus design research; flash fix; minimap composed from pixels (8m3 + d5o); shared inotify service (er2); wrap input-window fix; findui s3 (lez); Ctrl+Y + panel-chord guard (whl/2gx); clipboard (cde); Ctrl+S + startup restore (ck4); 1 GiB near-end stall (2ty); handoff limit (jmn); scroll s3 (ovu); **GL init 10 s timeout bug** (40t); Unicode glyphs (08z); tree-sitter runtime + src/hl (v0o.2); benches exit 0 on timing (lwq); startup stages (2ts); DPI-derived font size + zoom (m3g). Last main-level sanity (font, trace, editor, p4, cli) green at 0381fed; later commits are docs/reports only.

**Known visible defect on main:** at 243 DPI the window opens at 1728x1260 but the raster surface still paints 960x600 (build/design/main-dpi243-half.png) → **edit-a6v (P0)**, Codex worker was running at the cut (.wt/edit-a6v). Nothing goes to Tobias before a6v lands and Astra drive 2 says yes.

**edit-a6v LANDED (evening):** the whole 1728x1260 window paints on :98 (build/design/a6v-dpi243.png); only the sub-cell remainder strip (8 px right, 20 px bottom) stays black.

**Verified or resolved, not landed (worktrees .wt/<bead>, WIP-snapshotted):**
- edit-3kk scroll wiring: all suites GREEN in its worktree (scroll, render, raster, minimap, editor, p4, cli) but the cherry-pick onto main conflicts with the config landing: rebase once more, resolve, land.
- edit-v0o.7 config: LANDED after the wind-down note (all suites green, cherry-picked clean).
- edit-457.8 hot exit (+9ma): I renamed its window fields to e->font.window_*; its new editor_hot_exit_test:174 then fails at editor_open → needs a look (probably my rename changed the restored-size semantics, or EMFILE); journal_test green.
- edit-5ih font selection: 8-hunk conflicts with m3g/08z; Codex resolver was running at the cut.
- edit-q0g partial (x11; fix unimplemented; headless red test only) — low value, leave.
**Codex workers still running at the cut (reports land in docs/worker-reports/<bead>-s*-final.md):** edit-a6v, edit-73j (mouse/wheel/find panel), edit-v0o.1 (Wayland), edit-a2g (large-file remainder), edit-d50 (Sublime keymap parity), edit-5ih resolver. Land each the same way: snapshot, rebase onto main (resolver for semantic hunks, keep-both only for tests/STATUS/docs), serial targeted run with a private XDG_RUNTIME_DIR on :99, cherry-pick, close bead.

**Astra next:** drive 2 brief at build/astra/brief2.md targets **:98** (Xvfb 2880x1800 **-dpi 243**, running; Sublime launched there) so the DPI path is exercised; run it after a6v lands and a rebuild, Tobias launches it (sandbox denies the display); then the Sublime golden-master catalogue (build/astra/brief-sublime-catalogue.md). Nothing to Tobias before a yes.

**Beads filed today still open:** edit-cx8 (P1: editor_bench raster typing path reports allocations: law 2 check), edit-2cs (P3: stale zygote variant; long benches), edit-b0j design spec (sent to Tobias with mocks; Opus report docs/design/state-of-design-2026.md disagrees on typeface/minimap; Tobias decides per section), edit-pi6 (Tobias's real-display use, blocked on edit-w34.4 yes).

**Lessons (binding; see also the earlier list below):** land editor beads one at a time and rebase the next immediately; verify serially; `git cherry-pick` has no -q; a `cd` in a compound command moves the session cwd; a worker's branch snapshot can contain conflict markers if a rebase was left mid-way, always `git status` before building; two Xvfb now: :99 (96 dpi) and :98 (243 dpi).

---


## State now (read first)
**Landed on main today (all verified by serial targeted runs on :99: editor, p4, module suites, cli):** process reset + golden-master rule (PRD §3/§9, CLAUDE.md laws 1/4/8, PLAN §1 loop); Astra drive 1 report (VERDICT no) and the Opus design research; edit-g11 flash fix (+P4 test correction); edit-8m3 + edit-d5o minimap (per-character pixels composed into the frame, translucent viewport band); edit-er2 shared inotify service (200 opens accepted); the wrap input-window fix; edit-lez s3; edit-whl/2gx (Ctrl+Y redo, panel chords consumed with a notice); edit-cde clipboard (all paths); edit-ck4 (durable async Ctrl+S, startup restores unsaved session); edit-2ty (1 GiB near-end stall); edit-jmn (tab reservation, readable IPC errors); edit-ovu s3; edit-40t (**GL init completion message had generation 0 since de7fb22: every GL init waited 10 s then failed**; fixed); edit-08z (Unicode glyphs via the runtime font owner); edit-v0o.2 (tree-sitter v0.27.1 vendored + src/hl parser). Pushed through f9de9b2; later landings (08z, hl) await the sanity run then push.

**Verified, waiting to land (editor-file conflicts, one resolver each):** edit-m3g (DPI font size + zoom; resolver 4 after 08z), edit-2ts (startup stages; fixer for the EGL-fallback test after 40t), edit-v0o.7 (config module; re-resolve after m3g). **In flight (Codex, wave 3/4):** edit-73j mouse/wheel/find panel, edit-3kk scroll wiring, edit-v0o.1 Wayland, edit-457.8 hot exit (+9ma), edit-5ih font selection, edit-a2g large-file remainder, edit-d50 Sublime keymap parity. Codex weekly quota reset at ~14:00; Tobias: scale back as things land, no new waves.

**Astra:** drive 2 brief ready at build/astra/brief2.md (same 14 tasks + fixed/still-present column); launch after m3g + 2ts land and a rebuild, with the same launch line as drive 1 (Tobias runs it; the sandbox denies the display). Sublime golden-master catalogue brief at build/astra/brief-sublime-catalogue.md (not yet launched).

**Lessons today (binding):** (1) every editor bead conflicts with the previous landing in editor.c/open.c/private.h/main.c/editor_test.c: land editor beads one at a time, rebase the next immediately, use a Codex resolver for semantic hunks and keep-both only for test dispatch lines and STATUS.md; (2) verify serially, never >2 full checks in parallel: at load > 30 the 5 s settle() deadline, the find/savectl fixture deadlines and cli_test's load-scaled window deadline all fail spuriously; (3) pre-er2 bases exhaust inotify instances under parallel runs (editor_open/editor_add_buffer EMFILE); (4) `git cherry-pick` has no -q; a `cd` inside a compound command moves the session cwd.

---


## Read first: state at the cut
**Landed on main today:** process reset (0eeb4a4, f02934c), Astra-before-Tobias rule + flashscan (2baf8c5), Astra drive 1 report (51d840a, VERDICT no), PRD golden-master rule + PLAN loop (5160ea8), Opus design research docs/design/state-of-design-2026.md (56d7319), **edit-g11 flash fix 691ceb1** (verified: full `make check` in its worktree green except cli_test under load; cli_test re-run serially green, exit 0). **Main has NOT had a main-level clean check since 691ceb1 and was pushed as-is at the cut: run `make all && make check` on main first.**

**Wave 2 worktrees (`.wt/<bead>`, branches `wt/<bead>-s2`), each snapshotted as a WIP commit at the cut; Codex workers may still have been running (reports land in docs/worker-reports/<bead>-s2-final.md in main and <bead>.md in the worktree):**
- edit-8m3 minimap slice 1: DONE, verified (editor_test + cli_test green on serial rerun; the parallel make check failed only from inotify exhaustion and load). **Conflicts with g11 in src/minimap/minimap.c minimap_fill**: rebase wt/edit-8m3-s2 onto main, resolve, re-run minimap_test + editor_test, land. Follow-up edit-d5o (compose pixel minimap into the frame) filed P0.
- edit-gdj: Astra withdrew the anchor finding; worker added selection-chord regression tests only. A Codex continuation (edit-whl Ctrl+Y + edit-2gx Ctrl+F) was running in the same worktree: land tests + whatever it finished after `make check`.
- edit-b0j design spec: DONE (docs/design/visual-spec.md + 16 SVG mocks). Sent to Tobias with 3 PNG renders (build/design/). Conservative (keeps DejaVu Sans Mono); the Opus report disagrees on typeface (Geist Mono / JetBrains Mono) and minimap. **Tobias decides per section**; do not start chrome work before that.
- edit-jmn: partial (tab reservation + readable IPC errors). Root cause is per-file inotify_init1 in src/file (file.c:1027) exhausting max_user_instances=128 → new bead **edit-er2** (P0) dispatched to Codex in .wt/edit-er2. Land jmn after er2.
- edit-ck4 (Ctrl+S + startup restore), edit-cde (clipboard), edit-08z (Unicode glyphs), edit-m3g (DPI size + zoom), edit-2ty (1 GiB stall), edit-2ts (startup stages): in flight at the cut; check their -s2-final.md reports, verify each worktree (`rm -rf build && make all && make check` with a private XDG_RUNTIME_DIR, DISPLAY=:99), land in blocker order: ck4, cde, 08z, m3g, 2ty, 2ts.
- edit-lwq (benches exit 0): Sonnet did the harness + most benches; a Sonnet continuation was auditing the remaining 7 benches and running make bench; the session's subagents died at the cut. Finish by hand or one more Sonnet: see docs/worker-reports/edit-lwq.md in .wt/edit-lwq.

**Verification gotchas found today:** (1) 16 concurrent worker builds/tests exhaust the per-user inotify instance limit (128) because the file module opens one instance per file → editor_test:809 / editor_add_buffer EMFILE and cli_test:165/233 failures that are NOT the worktree's fault; re-run the failing suite serially. (2) cli_test prints two FAIL lines from its own forced-timeout child (§36) on every run; only the exit code counts. (3) Under load1 > 10 the display suites are slow; the Makefile's per-test display lock serializes them.

**Then:** rebuild, Tobias launches the Astra drive again (same brief build/astra/brief.md, launch line in docs/worklog/2026-10-11.md), file deviations, repeat. Nothing goes to Tobias before a yes.

---


## Session 10 (read first; supersedes every "gate", "bench run" and "review slice" instruction below)
Tobias, 2026-10-11: **"Perf is a goal, not a progress blocker. Milestones are the priority. If landing milestones is slowed by perf then the process is a failure."** Decision doc: `docs/decisions/2026-10-11-perf-is-a-goal.md`. CLAUDE.md laws 1 and 8 are rewritten, PRD §9 milestones now exit by use, PLAN §1.4 amended, `perf/01-perf-target.md` is a reference goal sheet. No code changed.

**Astra drive 1 (09:00–09:45, docs/reviews/astra-vs-sublime-1.md, 52 videos under build/astra/video, trace dumps under build/astra/traces): VERDICT no.** Ranked blockers: (1) Ctrl+S inert (edit-ck4, P0); (2) clipboard inert in every path (Tobias bead, P0); (3) mouse caret/drag/wheel inert, minimap click works (edit-73j, P1); (4) Ctrl+F types the query into the document (edit-2gx, P0); (5) non-ASCII renders as '?' (edit-08z / tkw §29, P0) and 10 px ink on 2880x1800 (Tobias bead, P0); (6) selection anchor lost across Shift chords (edit-gdj, P0), Ctrl+Y not redo (edit-whl, P1); (7) 1 GiB log stalls after Ctrl+End + Page Up (edit-2ty, P0); (8) orange flash = minimap stale overlay '!' in 0xbf853d painted for 1–2 frames per keystroke, 88/95 keys (Tobias bead, cause attached) and the blocky minimap (Tobias bead); (9) CLI handoff error 8 after 64 opens (edit-jmn, P1), hot exit does not replay (edit-9ma / 457.8, P1), 2.3 s to first text on raster (edit-2ts, P1). Working: ASCII typing (G1 1.2 ms p50 / 45 ms p99 (M)[bat] on raster), keyboard navigation, undo, tabs, CLI handoff to 64, external-change prompt, idle quiet. Limits: raster backend only (EGL fails on Xvfb), no WM on :99, 60 fps capture.

**Drive mechanics (keep):** Codex sandbox denies the X socket; Astra must be launched by Tobias (or with a Bash permission rule) as `DISPLAY=:99 ... codex exec -m gpt-6-astra ... --approve-for-me ... "$(cat build/astra/brief.md)"`. Brief in build/astra/brief.md. Record with ffmpeg x11grab 60 fps; `tools/flashscan.py` with the **rendered** colour (#6b5030 tol 24 for the stale overlay; #bf853d gave a false zero). Re-run the drive after each batch of blocker fixes until the verdict is yes; Tobias sees nothing before then.

**Next coordinator, in this order:**
1. **edit-pi6 is blocked on edit-w34.4's verdict turning yes.** Do not launch anything on :0 for Tobias. After the blocker batch lands: rebuild, re-run the Astra drive (same brief), then hand over only on a yes. Capture the trace ring. Every lag or wrong-text report becomes a bead with a repro. M0 (edit-zzj) closes when Tobias declares it, no duration attached. Nothing else outranks this. **Law 4 now forbids undenominated numbers anywhere** (I had written "an hour" and "a day"; Tobias struck them).
2. **One dedicated editor worker** (Codex sol high), queue in blocker order: ck4 slice 2 (Ctrl+S), clipboard wiring, edit-2gx (Ctrl+F/H consume), edit-gdj (selection anchor) + edit-whl (Ctrl+Y), edit-73j (mouse, wheel, find/replace), edit-457.8 hot exit. **Non-editor modules in parallel** (own worker each): tkw §29 Unicode (font), the flash fix in paint.c (minimap stale overlay), minimap rendering, font size/HiDPI default, edit-2ty large-file stall (file/lineidx/scroll), edit-jmn handoff error 8 (ipc), edit-2ts startup (measure on :0 first). Everyone else builds in their own modules toward M1/M2 (edit-5ih font selection, v0o.1 Wayland, v0o.2 tree-sitter runtime, v0o.7 config).
3. **Do not** open beads from bench rows, re-run M0/M1 bench campaigns, or convert review MAJORs into beads. P3 review beads are backlog; pull one only when Tobias, a fuzzer or the trace points at it. edit-lwq (benches exit 0) is a mechanical P3 bead for a Haiku/Codex worker when convenient.
4. Push after each main-level clean `make all && make check`.

Everything below is session 9's state, kept for reference. Its "Needs Tobias" gate decisions (G7j, G8d, undo reserve, raster defaults) are **withdrawn**: tracked numbers, no decision required.

---

# Session 9 wound down (2026-10-11 07:20)

Read: this → `CLAUDE.md` → `PLAN.md` §1 and §5 → `bd ready --type task` and `bd ready -n 40`. Session details: `docs/worklog/2026-10-10.md` (sessions 8 and 9), `docs/worklog/2026-10-09.md` (sessions 4–7), `docs/worklog/2026-10-08.md` (sessions 2, 3). Session 1's long handoff is in git history (commit 20e9887); its §2 settled decisions still bind.

## Method (session 4–5, keep)
**Public repo (17:15, 2026-10-09):** https://github.com/tobiasosborne/sublimite (AGPL-3.0-or-later), remote `origin`, main tracks origin/main. `git push origin main` after each coordinator status check-in and at session end (not per bead; the beads pre-push hook runs). Only main goes up; `wt/*` stay local. Nothing in the tree may contain secrets or private data (worker reports and decision docs are public). `bd` stays local: no federation/sync daemon.
Every worker gets its own git worktree `.wt/<bead>` on branch `wt/<bead>` (`.wt/` is in `.git/info/exclude`), created from main. The coordinator verifies there (`make check`, **`make all`** (release test builds catch gcc-only warnings), the module bench), commits on the branch and cherry-picks onto main. Workers never run git. **Display: nothing opens a window on :0** (P0.2c guard, 6a39d6d; Xvfb :99 must be running; binding until ~16:30 on 2026-10-09 and a good default after). Codex launch line: `env DISPLAY=:99 EDIT_DISPLAY=:99 timeout 5400 codex exec -m gpt-6.1-sol -c model_reasoning_effort=<high|xhigh> --approve-for-me --skip-git-repo-check -C .wt/<bead> -o docs/worker-reports/<bead>.md "<brief>"`.

## Session 9 state (2026-10-10 20:00 → 2026-10-11 07:20; read first)
Fable as coordinator, Tobias: "go wide", then "keep orchestrating over the next hours, keep Claude on pace, freely use codex; stop using Codex at 10 % remaining weekly quota". Codex Weekly went 51 % → 65 % (stop line 90 %); Claude Weekly 45 % → 50 %, 9 % behind pace throughout. One Sonnet worker (edit-2vs; Codex's safety filter refuses lifetime-bug wording), everything else Codex sol high; three P2/P4 reviews and resolvers on Codex. **47 commits on main since 0eb1dcc** (45 linear plus two review docs), every code landing verified after rebase with a clean `rm -rf build && make all` and leaks-on `make check` in its worktree; main itself re-checked clean and pushed at aa96fd0, f6fbce4, ab20118, bd24f18, fcc7386, 6c1ffea, 54b0e26, 046203a and (final) after 63eda51.

**Landed (bead → commit):** 4w1.55 58bb8ce (closed), 4w1.57 1247762 (closed), zzj.14 fd1df83 (closed), yqu slices 2+3 f5aabb4/61ea537, zzj.13 slices 2+3 687492f/dc4015e, 2vs 6f40d16 (closed), ovu slices 1+2 aa96fd0/dc8b345, 457.10 port 3492b4e, mdv slices 2+3 a89b73a/ab20118, zzj.12 a3242fb, lez f6fbce4, czn c0ae480 (closed), 9yd 08804e3 (closed), zzj.5 M0 run 94ff91a (docs/bench/m0.md), 9jo bd24f18 (closed), 5cv display lock e54f918 (closed), pyo d3a3c4e (closed), 6pc clipboard a914a05 (closed), 40t gl de7fb22, zzj.16 da1b195 (closed), tkw font 126cb97, 5o0 raster/render fcc7386, k60 cff1a7d (closed), wvf 5ba5f77, o4o c755c6b, og2 272c978, fff e80bd51, 2hc 6f9a9a6 (closed), dnc 6c1ffea, q0g a941909, 685 097b63b (closed). Reviews: P2-1 e272d7b (5 BLOCKER/31 MAJOR/3 MINOR → beads 6pc, tkw, 40t, 5o0, q0g; all five landed at least one slice), P4-3 c255b69 (7 BLOCKER/31 MAJOR/3 MINOR → beads ck4 journal P1, a2g large-file P1, 9lf loop/tabs P1, 73j wiring, apl raster memory, 2j9 bench/fuzz).

**Landed after 04:45:** dnc2 54b0e26 (G5 first viewport 8.5 → 4.2 ms loaded), apl 225448a (raster memory census, red on defaults), ck4 bcbffb5 (journal/recovery slice 1: BASE v2 with ctime, Keep/Reload reconciliation partial; Ctrl+S save path and startup session restore still OPEN with opt-in red probes), 9lf 046203a (loop/tabs §2, 11-14, 16, 22, 27, 40, 41; plus a test-fixture mtime fix), 2j9 08bf94c (bench/fuzz honesty §34-39; the savectl fuzzer's mtime-restoring rewrite now waits for the coarse ctime clock, Sonnet fix), a2g 63eda51 (large-file/find §1, 15, 28, 31 fixed; §17, 18, 20, 21 partial; §19, §29 red opt-in). **Nothing is in flight.** Worktrees under .wt/ are all landed and can be removed (`git worktree remove .wt/<bead>`; branches wt/* stay local).

**Infrastructure fixed this session (keep):** (1) the IPC socket under XDG_RUNTIME_DIR made cli_test's child hand off to another suite's editor (edit-og2: per-process isolation, display-specific hand-off refusal); (2) concurrent suites on Xvfb :99 broke refwin/x11 tests via XTest injection (edit-5cv: `make check` wraps display tests in tools/with_display_lock.sh; the tools take the same lock). Coordinator rule: never hold that lock around a whole suite (self-deadlock); serialize coordinator suites on a private lock only. (3) The system clock jumped back 19 h mid-session; after any clock change, touch sources and build from clean.

**Needs Tobias (decisions surfaced this session):** G7j jump gate (30/50 ms) is below the raw scan floor on this box (16 GB/s: touching the 966 MB prefix costs ~60 ms; edit-wvf) → restate the gate or redesign the jump (sampled/estimated line starts); G8d durable 4 KiB save 12.4 ms vs 10 ms (fsync-bound; bead filed); tabs+caller undo memory census 1.83 MB vs 1 MB (undo safety reserves; edit-fff); raster production defaults vs G10: census red at target-A, proposed fix lowers default max_rows 300 → 120 (edit-apl, docs/decisions/edit-apl.md); the P1 Core epic (edit-4w1) auto-closed in bd when its last two children closed.

**Next coordinator:** (1) `make all && make check` on main, `quota` (Codex stop line 90 % weekly); (2) P4-3 follow-up slices in this order (editor files are the bottleneck, one editor worker at a time or accept a resolver): ck4 slice 2 (§3 recoverable save path = Ctrl+S, §6 startup session restore; the opt-in probes --ck4-save/--ck4-startup are the red tests), a2g slice 2 (§19, §29, then §17/18/20/21), 9lf caveats in its report, 73j wiring (absorbs the scroll wiring bead, edit-5q0 minimap, edit-tkw §29 hook), apl default change once Tobias decides; (3) older slices: zzj.13 (8 mapped files, 11 bounded validation), mdv §18 editor wiring (bead filed), lez §22/25/29/30, ovu §8, 40t integration findings, q0g §21, zzj.12 G11 CPU, o4o reproduction, dnc early index launch; (4) real-display batch (EGL default, G2c pairs, prewake, zzj.8) and the M1 bench (457.12) toward P5.1b; (5) push after each main-level clean check.

## Needs Tobias (older)
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
- Codex safety filter refuses briefs that mention "use-after-free", "memory safety", "data races", fuzz "crash/abort/reproducer" wording: phrase as lifetime/ownership/ordering and "model mismatch"; relaunch as a continuation on the same worktree (it keeps the WIP).
- `codex exec -o` points at `<bead>-s<n>-final.md`; the worker writes `<bead>-s<n>.md`. `git cherry-pick` has no `-q`. A wait loop whose command line contains the literal path it pgreps for matches itself: hide the path in a variable.
- Landing policy: a verified bead lands without re-verify when main moved only in disjoint module dirs/files; otherwise re-verify after rebase (bench-only overlaps: rebase + `make all` suffice, `make check` cannot see bench files). Run a main-level clean check before every push.
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
