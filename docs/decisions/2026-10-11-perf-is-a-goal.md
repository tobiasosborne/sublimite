# Perf is a goal, not a progress blocker (2026-10-11, Tobias)

## What Tobias said
"Something has become twisted in this project. Perf is a goal, not a progress blocker. ... Milestones are the priority, perf is the goal. If landing milestones is *slowed* by perf then the process is a failure." He also asked where the photodiode and the G-gates came from (he never asked for them), said Codex always produces too many findings and the orchestrator exists to exercise judgment, and asked why `src/editor` is so large.

## What the evidence showed
- The session 1 planning commit (20e9887, 2026-10-08) wrote about sixty p50/p99 budgets (G1–G11 × A/B/C hardware classes) as k × planning-bound with k chosen by the planning agent, then made them binding through CLAUDE.md law 1 ("Gates are binding") and PRD §9 ("M1: all enforced A gates green"; "M0: photodiode once"). The 2026-10-09 bench-policy note softened practice but never changed those two texts, so every coordinator kept reading the binding version.
- Six beads this week (P0.3 trace 20 ns, edit-fff, edit-2hc, edit-tle, edit-wvf, edit-o4o) were caused by the number rather than the code: load artefacts, an invented 1 MB undo budget, a fsync floor, a fixture mismatch (G7j assumed a 400 MB prefix; the fixture has 8.9M lines so the bench scans 966 MB, about 60 ms at 16 GB/s), and a bench assertion bug. Two further beads (edit-yqu, edit-2j9) and about 17 review findings were about bench machinery (sample counts, exit codes, verdict enums). Harness code is 52k lines against 36k lines of product.
- Law 8 ("every BLOCKER/MAJOR becomes a bead") converted three Codex reviews into 15 mandatory beads and most of session 9.
- `src/editor` is 3330 lines over eight files, longest function 73 lines, 23 exports, eight dependencies: a hub, not a god function. Its cost is dispatch: every feature and fix lands there and the coordinator ran one editor worker at a time, so the project serialized through it while the fleet polished benches.
- M0's own exit was never attempted: the real-display run has been "next batch" since 2026-10-09.

## Decisions (settled; law 5 applies)
1. **PRD §9**: M0 exits when Tobias does his own work in it on the real display and declares it done or lists blockers (bead edit-pi6, P0). M1 exits when he declares it fit for the trial. No duration: my first draft said "an hour" and "a day", which were undenominated numbers of exactly the kind this decision removes; Tobias struck them. M3 is "usable on the B machine". The photodiode criterion is deleted.
2. **CLAUDE.md law 1** rewritten: perf is a goal. Benches print rows against `perf/01-perf-target.md` §0.2 and exit 0 (code change: bead edit-lwq). A miss is a note in the bead, never a blocker, never a bead by itself. The only enforced perf guard is the G1 typing-latency regression test. Perf beads come from observed lag with a repro or a trace-ring profile.
3. **CLAUDE.md law 8** rewritten: the coordinator triages reviews. A finding becomes a bead only if it would affect trial use (data loss, crash, hang, wrong text, typing lag). Everything else: one backlog bead per review. Reviews never block an epic from closing.
4. **PLAN §1.4** amended to match; **perf/01-perf-target.md** headed as a reference goal sheet.
5. **Dispatch**: one dedicated editor worker continuously; all other workers build features in their own modules. The editor's queue: ck4 slice 2 (Ctrl+S recoverable save, startup restore), edit-73j (clicks place the cursor, wheel scroll, find/replace in the editor, Unicode glyphs: these are missing M1 features, not hardening), edit-457.8 hot exit.

## Bead changes
- Created: edit-pi6 (M0 exit by use, P0), edit-lwq (benches exit 0, P3).
- Promoted to P1: edit-73j, edit-457.8. ck4 stays P1 for §3/§6 only.
- Closed as tracked: edit-tle, edit-wvf, edit-fff, edit-o4o, edit-apl, edit-zzj.5, edit-yqu, edit-2j9, edit-457.12.
- Downgraded to P3 (backlog, pull on evidence): edit-40t, 5o0, q0g, lez, ovu, mdv, zzj.13, a2g, 9lf, wib, dnc, zzj.12, 3kk. edit-tkw P2 for §29 Unicode rendering only. edit-zzj.6 (P3 review) P4.

6. **CLAUDE.md law 4** strengthened: every number carries its derivation; a (G) resting on an (E) planning bound is tracked, never enforced; undenominated numbers are struck on sight. This is the perf doc's own §0 rule, now applied to briefs and beads, where the module guards (indent 20 us, utf8 5 us, trace 50 ns, view 1 ms, tabs 1 MB) and the G8d 10 ms row ((E) flush × chosen k, against a measured 12.4 ms fsync) entered without one.

## Open questions left for Tobias
- G7j, G8d, the 1 MB undo reserve and the raster default rows no longer need a decision; they are tracked. If any shows up as lag while Tobias uses it, it gets a bead with the repro.
- Splitting `src/editor/buffers.c` (722 lines) and `large.c` (455) into their own modules would let two editor workers run in parallel. Not done; file a bead if the single-worker queue becomes the bottleneck again.
