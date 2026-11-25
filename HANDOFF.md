# HANDOFF — sublimité v2

Read order: this file → `CLAUDE.md` → `docs/PLAN_V2.md`. Beads: `bd` in this worktree resolves to the v2 database
(prefix `v2`; docs/decisions/wave0-beads-and-oracle-pin.md §1). Push branch `v2` after every verified landing. Wip
branches: push each at every pause or session end; delete the remote branch once its bead lands on v2 (Tobias, 2026-11-24).

## Session 2 (2026-11-24 evening) — IN PROGRESS (worklog 2026-11-24 "Session 2")
- BINDING RULE (Tobias, 22:00 CET, verbatim): "work up to but not exceeding pace on any provider. coordinate serial
  work." The advisor's reading, which replaces every earlier lane count:
  - At most one running job per provider. Landings go one at a time, in dependency order.
  - No Claude worker starts until a reading taken at an event shows room >= 1.0. Claude-bound beads then run one after
    the other. The coordinator's and the advisor's turns are the whole Claude spend: act only on landings, dispatches
    and decisions; send one message per event that needs the advisor; no acknowledgements.
  - Muse's 5-hour window counts. If it is ahead of pace at a Muse event, wip-commit the worker, push the wip branch,
    and resume it at the first event after the window is at or behind pace. No polling.
  - Routing (advisor, 2026-11-24): a source-only Astra read of the pure journal reader (N2: journal corruption, witness
    CRCs) was cut at its last line by the Codex filter. The subject is enough; execution is not needed. Reads of journal
    recovery, repair and file fault paths go to Claude or Muse from the start.
  - Current jobs:
    - MUSE QUOTA (Tobias 22:19 CET: "watch muse wonder full quota carefully"). Measured rate: about 8 weekly points
      per Muse lane-hour (M, 2026-11-24: 18 % at 21:40 -> 23 % at 22:20 with ~35 min of lane time). This replaces the
      earlier 1.7 estimate. One lane runs at about 1.5x the 5-hour window's pace.
    - Muse: the N2 fix job (v2-c42/o0t/kbj/23u) was stopped at 22:21 before any edit. Its worktree is untouched at
      9235a3a, so there is no wip. It may restart only when ONE quota reading shows the Muse 5-hour window at or behind
      pace AND Muse weekly >= 3.0 behind. Its brief then adds a self-check: run `muse usage` before each build-and-test
      cycle; stop at a compiling state when Weekly >= 26 %, or when Window % > (minutes since 21:29 CET) / 3. Scope is
      the four fixes and their tests only. No further Muse dispatch tonight without asking the advisor.
    - L2-1's adversarial source read: a fresh Opus reviewer in the Claude queue, after v2-40h and before 2b-1, each at
      Claude room >= 1.0. Then the advisor's owner read, and L2-1 lands (review/l2-1 1ce753a, lanes green).
    - L2-1 Opus review question (advisor): does file take messages from the shared mailbox by selective receive
      (work_mailbox_receive)? If so, how is that consistent with one UI dispatcher (Q7)? A second consumer is a
      finding against L2-1. After L2-1 lands, the export gate decides work_mailbox_receive's temporary reservation.
    - L2-1 MERGE ITEMS (advisor, 2026-11-24):
      (1) the file-suite permission assertion (v2-677 landed in os as 1.28);
      (2) a new destination gets 0666 & ~umask, by fchmod after the temp's private 0600 creation, so no step is wider
          than the final mode. It is a deliberate deviation from v1's 0644-regardless-of-umask (v1 file.c:908/919 at
          6fec2e8; the advisor adds it to v1's backlog), recorded in docs/decisions/L2-1-file.md with the reason: it
          agrees with v1 under umask 022 and never widens past the user's umask. Assertions: umask 022 gives 0644,
          umask 077 gives 0600, and the temp is never wider than the result.
    - Codex (serial): v2-677 permission bits (Sol, running; fix in os_file_copy_metadata, test in os_persist_test; the
      file-suite assertion is added by the L2-1 merge). Then Astra answers her ten L5 questions as errata (section
      "Errata proposed by the L5 spec" in wave1-app-spec.md). The advisor reads the L5 spec and the errata together.
  - L5 RULING (advisor, 2026-11-25 01:30): Wave 1 gets a MINIMAL L5. A save whose displaced generation has any
    dependent other than the saver is REFUSED, visibly, and tested. Session-level peer protection, closed-kept and
    imported drafts, eviction and reopen move to the Wave 3 session vertical. The L2 peer mechanism stays as signed.
    - Signed L5 errata: 1 (release_fence settlement; EDIT_SAVE_FORGOTTEN), 2 (journal_new_owner, journal_source_key),
      3 (file_target as a value; savectl_identity deleted), 4 (file_temp_verify/recover, HELD), 5 (pending cutoff on a
      recovered buffer; backing-owned validation context), 8 (runnable queries; completion published before the wake),
      9 (JOURNAL_SELF_CONTAINED_LIMIT as a constant).
    - Not signed: 6 (deferred) and 7 (Astra to answer).
    - Astra is redrawing the spec. When the redraw is signed:
      - errata 3/4/5 become ONE follow-up file bead; L2-1 lands as reviewed, without them;
      - errata 2/8/9 go into the 2b-1 brief;
      - erratum 1 and the edit event go into the savectl and EDIT-CORE briefs.
  - RULE FOR EVERY CODEX BRIEF (advisor, 2026-11-25): the worker writes its decision file and its report BEFORE it
    runs the final lanes, and afterwards updates only the lane results. Two Codex filter cuts (the N2 read and
    EDIT-CORE) both came at the last step with the work intact.
  - EDIT-CORE (v2-4ht): the Sol run was cut by the filter at its last step. Wip dd58ba7 is pushed as
    t3/implement-edit-core. After 2b-1 lands, a fresh Opus finishes it against the real writer: it owns every line it
    keeps, writes v2-4ht.md, shows red/green, lists the EDIT-INTEGRATION cases, compares the double with the real
    journal, and judges the two out-of-brief edits (undo's static maintain_retired, the render smoke placeholder).
    original_generation in edit_options is SIGNED and in the edit spec.
  - L2-1 STATE (2026-11-25):
    - round 1 (Muse) got DO NOT LAND from a fresh Opus review (10 MUST-FIX);
    - round 2 (Opus) fixed all ten (re-check: 23/23 mutants caught), but 4 new MUST-FIX and 14 new-code survivors;
    - ROUND 3 runs on the round-2 thread: MF-B, MF-D, the 14 survivors, the EIO/EISDIR, dirsync and relative-path
      NOTEs, and the umask read from /proc/self/status (077 fallback);
    - then the reviewer re-checks again, then the advisor's owner read, then L2-1 LANDS with MF-A and MF-C stated OPEN.
    Branch review/l2-1c (2108374).
    - MF-C is accepted as a stated limit (same class as N1).
    - MF-A: source-witness design (d50a3a3). Its CORE is signed; the contract waits for Astra's pass 2 (W1 start at
      the first committed edit, W2 five states, W3 tickets in the validator context;
      docs/briefs/source-witness-pass2.md), which runs after Sol's v2-sve. Bead v2-hy8 (Claude) blocks the WAVE 2
      exit and is implemented after savectl lands. No direct Save As from IDLE_CHANGED now.
    - Tobias questions: v2-jh7 (chown), v2-b4y (export-to-new-name).
    - Sol: v2-sve (work wake-on-drain) after Astra's job.
    - The savectl brief carries custody hand-over (GAP 2) as an explicit open item.
    - The deletion review table gets file 2,159 vs v1 1,664, with the reason, and asks whether the custody states can
      be fewer.
    - Publish-by-exchange BREAKS (wave1-publish-by-exchange.md); rename-over with its stated window stays.
  - L2-1 ROUND 4 (round-2 Opus worker), before landing:
    - MF-E: copy buffers no longer validate the lent source at prepare. file.h rule: source validation protects bytes
      read from the source; destination expectation protects what is overwritten.
    - R3-A: no unlink unless the destination still matches.
    - R3-C: recover classifies EACCES/NOENT as a mismatch.
    Then a re-check limited to MF-E, R3-A, R3-C and a rerun of all mutants; then the advisor's single owner read;
    then the landing.
  - v2-4xk (P2, Claude, after L2-1, blocks the Wave 1 exit and savectl): the changed side named, plus named tests for
    the 15 round-3 survivors and X12.
  - Source witness: SIGNED at 9968034 as v2-hy8's contract.
  - New bead v2-4xk (Claude): file's publish/verify results name the changed side (source vs destination). It blocks the
    savectl bead.
  - CLAUDE QUEUE, each at room >= 1.0, one at a time:
    1. L2-1 round 3's re-check, then the advisor's owner read, then the landing with wake-on-drain (1.32) adopted at
       the merge;
    2. v2-4xk;
    3. 2b-1 (after the advisor's L5 read);
    4. EDIT-CORE's finishing pass;
    5. later, v2-hy8 (after savectl).
    Codex is idle (the Wave 2 harvest exists, 907cbef).
  - Codex: Astra on the Wave 2 harvest (docs/decisions/wave2-L3-L4-harvest.md), document only. Muse: no dispatch while
    unreadable.
  - Serial chains:
    - Codex: N2 (LANDED 1.27), then Astra's N2 landing read (docs/reviews/wave1-N2-astra.md), then Astra's minimal
      L5 spec (docs/decisions/wave1-app-spec.md; PLAN 4.7: app_loop, app_buffers, app_save, app_session, the headless
      command surface), then EDIT-CORE (developed after N2, v2-bkz and the piece reader land; it lands after 2b-1,
      with reads on the wip).
    - Claude, when room allows: the piece reader bead (reader + erratum (i) + the D1' deletion + the undo_commit
      change; after the advisor signs Astra's D1' text), then 2b-1, 2b-2, 2b-3.
    - Muse: L2-1 (running), then L2-3 once its dependencies exist.
    - Later: EDIT-INTEGRATION, savectl, minimal L5, the Wave 1 exit list.
  - Landed in session 2: 1.22–1.26. Signed: A27, A28, the wire + reader, the journal runtime section, edit.h, savectl.h,
    all L2 briefs (L2-1, L2-2b1/2/3, L2-3, N2, edit-core, edit-integration), and S3 (reader + erratum (i)).
    D1' is signed in principle; its text awaits the advisor.
- Rules (advisor): one Opus lane on the critical path (second only at a dispatch-time Claude room >= 3; stop line 0.5);
  act on events only, never poll quota or children; Codex carries all non-fault/kill/truncation work (filter routing
  unchanged); 7b round 4 was its last design round — a send-back needs a correctness counter-example, taste and size go
  to v2-0fv.27; landing reports carry the module's comment-free line count (`gcc -fpreprocessed -dD -E -P f | grep -c
  '[^[:space:]]'`) beside v1's and one quota reading per account.
- Environment faults: a lane failure naming a /tmp path, a missing corpus or an Xvfb display (e.g. after a reboot) is
  rebuilt and rerun, never sent to a worker as a code defect.
- Landed: 1.22 (cd8e357, v2-0fv.21 os export deletion + nm gate tools/check_exports.py; remote t3/audit-os-exports deleted).
- Landed: 1.23 (077910a, v2-0fv.17 piece 7b real tree; remote review/7b2 deleted). Bound erratum rule: a header bound
  naming 2^47 or 2^48 is struck unless an ASSERT enforces it.
- NEXT for the Opus lane: L2-2b (thread "L2-2b journal runtime (Opus)", wip a94af15 on t3/journal-runtime). Order:
  move from the piece model to the real tree FIRST, then M4–M6, append, transport, recovery, repair. Rotation is HELD
  until the advisor signs Q7 amended (wave1-L2-derivations.md; E2: an exclusive grant covers no sync and no
  content-sized write). Not started at 21:07: Claude room 0.4 < stop line 0.5.
- Specs awaiting the advisor's joint read: edit (5d57762 + Astra's send-back round running) and savectl revision
  (62c1441; 8 seam GAPs become amendments to the L2-1 and L2-3 briefs before either is dispatched). Undo erratum bead
  (E1: replay stops uncommitted; read-only iterator) is filed only once the shape is signed.
- Filed: v2-ebd (Wave 1 exit export gate: 498 would-fail exports outside os).
- 7b: LANDED as 1.23 (see above).
- Specs for the advisor's read: docs/decisions/wave1-savectl-spec.md (Astra); Astra's edit spec (v2-0fv.15) running.
- Tobias's decisions 2026-11-24: A23's large-file external-replacement contract ACCEPTED (L2-3 proceeds on it); v1
  findings live in v1's tracker — edit-hagx (undo above 32 MiB terminates the editor), edit-v1f5 (post-rename save
  conflict), edit-j4fp (Ctrl+S during an open), edit-9t6j (backlog) — cite these ids; pace rule: up to pace on every
  account, not beyond.

## Session 1 (2026-11-22 night → 2026-11-24 morning, ThinkPad) — ENDED on Tobias's "time to pause work now" (2026-11-24)

### Wave 0: GATE PASSED (2026-11-23 22:50 CET; advisor acknowledged)
`make contract-gate` exit 0, run v1-20261123T215001Z-843898: PASS 96, XFAIL 1 (X3.over.roundtrip: v1 dies in Undo after
a journaled delete). Commit c16bfc2, [AC], load 0.84; evidence in docs/gates/wave0/v1-20261123T215001Z-843898/. Epic
v2-he7 closed; backlogs .20, .25, .34 stay backlog. It proves the instrument against v1 at the pin; it says nothing yet
about v2 (the per-target predicates — zero Law-2 counts on R1/R3/R4/R5/R6, X3 as PASS — are what Wave 1's exit holds v2
to). The run takes ~137 s (parallel displays :190–:195): ask the Paris-talk thread for ~5 minutes. Use tools/run_gate.sh.

### Landed on v2 (all pushed)
- L0: os 1.1, base 1.2, trace 1.3, work 1.4, ipc 1.5, os stream + wait set 1.8/1.11, fault ids 1.10/1.12/1.15, ASSERT 1.14,
  no test env vars + dead exports 1.20 (82c3cc0), archive member-list fix 0.87 (010b69b).
- L1: scan 1.6, utf8 1.7, indent 1.9/1.13, piece 7a frozen header + model + suite 1.16–1.18 (Lines frozen 6be1cb3, erratum
  d028acf), find 1.19 (d11349c), undo 1.21 (e59461d). lineidx removed (A19).
- L2: L2-0a os watch + metadata copy 2.0 (47701d6), L2-2a journal wire + pure codec 2.1 (3f0d196).
- Early leaves (A21): dnd 4.1 (34cc2e8), TOML parser 4.2 (d121784).

### Paused, with where and what remains (resume each on its own thread; each checkout is a wip commit on its branch)
1. 7b real piece tree (v2-0fv.17), Opus thread "W1 L1 piece 7b real tree (Opus, continues Sol wip)", worktree
   /home/tobias/.t3/worktrees/sublimite-v2/t3-02ae434f-077e470154366fbf, wip e0b96ff (round 4). Rounds 1–3 merged and
   lane-green on review/7b2 (5633ba1, my merged tree). Round 4 (A26: admission exact at quiescence, four-step sequence):
   fixpoint deleted, piece_admit = steps 2–4, fast path needs companion storage, compound edits planned once (shadow
   result), STORAGE contract in piece.h, new tests J/K1/K2 red->green, I and A(1) as retry-success contract tests; san/tsan
   piece/undo suites green on the worker's tree. NOT done: decision §8.1 still says A25 (rewrite to A26; drop three round-3
   mutants that target deleted code); check/check-tsan/fuzz-smoke not run; piece.c grew 2,811 -> 2,819 (the worker offers
   two-stage compound planning to shrink it — an advisor call). Then: merge round 4's files onto review/7b2 (copy files;
   git merge-file for any that v2 changed since), lanes on the merged tree, Astra reads the admission section only
   (source, no execution), land.
2. L2-2b journal runtime (v2-0fv.24), Opus thread "L2-2b journal runtime (Opus)", worktree
   /home/tobias/.t3/worktrees/sublimite-v2/t3-de932866-2c152938cbe90882, wip a94af15. Compiles; next item: its mutation
   reds M4–M6. Wire unchanged (no amendment). Built on the piece model; after 7b lands it moves to the real tree
   (tests/piece_model.c is deleted in 7b round 2: the harness drives the pm_* model side by side).
3. (LANDED in session 2 as 1.22) v2-0fv.21 os export deletion + nm gate (Sol thread "v2-0fv.21 os export deletion + nm gate (Sol)", worktree
   /home/tobias/.t3/worktrees/sublimite-v2/t3-f672f5f8-c90c59b8e9d757d9, branch t3/audit-os-exports, wip 17bb383): paused; see its
   decision file docs/decisions/v2-0fv.21.md in that checkout for done/missing (gate check_exports: ok on its tree).

### Owed, in order
1. 7b round 4 finish -> Astra's admission read -> 7b lands.  2. L2-2b (M4–M6, then the real tree, the two measurements).
3. The advisor reads L2-3's brief (docs/briefs/L2-3-journal-save.md) after L2-2b lands; then L2-3.  4. L2-1 file
(docs/briefs/L2-1-file.md).  5. v2-0fv.21 finish and land.  6. Then edit (v2-0fv.15, provisional brief), savectl, minimal
L5, and the Wave 1 exit list (PLAN §6 + amendments A13, A19, A22–A26: the Law-2/G1 guard, the 131,072 B read gate, exact
line queries after any edit, Q6's G1-under-writeback number, Q3's 10 GiB recovery measurement, the SIGKILL-at-every-save-
step oracle, X3 as PASS on v2), then the whole-wave Astra deletion read before the gate.
Filed and waiting: v2-0fv.22 empty-tab footprint, v2-0fv.27 54-bit node-prefix packing (deletion review).

### Open questions for Tobias
- None open (the three session-1 questions were decided 2026-11-24; see Session 2).

### Quota at session end (one reading, 2026-11-24 06:45 CET)
Claude weekly 92.0 % (1.9 behind pace, reset 17:00 CET), Fable weekly 68.0 %; Codex weekly 7.0 % (4.2 behind); Muse
unreadable by `quota` ("server_sent_no_usage"; last read weekly 16 %, ~1.5 over pace at 01:30). Claude's used figure rose
89 -> 91 between 02:30 and 05:05 with all our lanes idle: the account is shared with another consumer.

### Facts the next session needs
- Quality bar (Tobias, 2026-11-23): genius-level engineering; push back on sloppy work. Owner-read every landing,
  one send-back round; designs state invariant, simplest alternative, defeating counter-example (worklog).
- Oracle: /home/tobias/Projects/sublimite-v1-oracle, detached 6fec2e8, built; `build/sysroot` holds
  wayland-egl-backend.h (export CPATH=$PWD/build/sysroot/usr/include). Stock editor: build/sublimite.
  Baseline: 120/120 sanitizer suites on a quiet box, one named v1 test race (pin decision §9).
- Xvfb: explicit display only, `-noreset -nolisten tcp`; lanes were given disjoint ranges :120–:169.
- PLAN_V2 is kept true by amendments A1–A5 (docs/decisions/wave0-plan-amendments.md).
- Reading quota: when t3quota's AGE for a provider is older than your last dispatch on it, the row does not include that
  dispatch: read the system `quota` tool instead (it also reports Muse).
- Quota routing (advisor, 2026-11-23 12:42: Codex is reserved for Astra reviews — fault-id diff, Wave 0 whole-wave review,
  piece/undo diffs; Sol writes no code unless Tobias relaxes the pace rule; code goes to Opus, two workers): Codex weekly reset ~11:56; up to pace only (≈0.6 pt/h): Astra first, at most
  one Sol lane when t3quota shows room > 0. New code beads go to Opus (≤ 2 workers, stop at Claude room 3). Muse one lane;
  Gemini prose only. L0 beads count as critical path under the battery plan.
- Routing (advisor, 2026-11-23, after five Codex cyber-filter stops: v2-he7.7, .15, .14, 7b on Sol, Astra's undo
  re-read): any Codex job, implementation OR review, whose work includes running or writing tests that inject faults,
  truncate mapped files or kill processes goes to Claude or Muse from the start. A filtered job is never resent to Codex
  in other words. Codex keeps derivation and source reading without execution (Astra design reads, production-source
  derivation checks) and lint/tool/patch beads (Sol). Landing reads of fault-heavy code are split: Astra reads the
  production source as a derivation check (runs nothing); a fresh Claude/Muse reviewer reads and runs the suites; if
  Astra is filtered, the Claude/Muse reviewer takes the source too. Muse and Gemini: up to pace only; Tobias rates Muse
  for review. Both Claude and Codex up to pace (Tobias, 2026-11-23); Opus stop line 2; four Opus lanes while Claude
  room > 4, two below.
- Routing as of session end: Codex jobs that run or write fault/kill/truncation tests go to Claude or Muse (filtered
  twice tonight: 7b on Sol, Astra's undo re-read); Muse ~1.5 over weekly pace at last reading, no Muse dispatch until it
  is at pace; the Claude account is shared with another consumer, and the coordinator's own turns use about the accrual
  rate — never poll quota or children; act on events.
- Worker briefs carry: no rm -rf, no mktemp with a clean-up trap, nothing outside the checkout is created or removed,
  HOME for any tool that writes caches is under the checkout's build/, build under build/, report instead of running
  anything that needs approval; never profile the oracle (perf record -N if ever). T3 thread timestamps are UTC.
- Battery: Tobias could not plug in on 2026-11-23; stop line 20 % (advisor rule, worklog).
