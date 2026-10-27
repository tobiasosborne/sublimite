# Whole-project assessment — 2026-10-27 (Muse Code, session 25 side-review)

At Tobias's request ("review this entire project and give your assessment").
Method: 8 parallel read-only scopes (docs/intent, architecture, core
correctness, tests/build — the only scope that ran code, perf claims,
product vs PRD, process/debt, portability/security), then an adversarial
critic that re-opened cited bodies to falsify the 5 most load-bearing
positive claims, then synthesis. Refuted claims are dropped below, not
repeated. Single-source and INFERRED items are labelled as such.
Triaged per law 8: trial-affecting findings (data loss, crash, hang,
wrong text, typing lag) get their own beads; everything else goes into
one P4 backlog bead. Bead IDs are listed at the end.

## Verdict

Defensively built, honestly tracked pre-alpha, fit for continued daily
trial use (which is what M0-by-use means). No demonstrated data-loss,
crash, or wrong-text bug was found on inspected trial paths. Material
risks: invariant fragility in lineidx/journal bookkeeping, a gate that
cannot enforce the two perf laws it claims to, and a stale installed
binary. Not fit to claim v1, Linux static link, or measured GPU/B/C
perf status.

## State snapshot (observed)

- 37 module dirs + `src/main.c`; 141 `.c` / 86 `.h` = 93,337 lines.
  Tests 113 files / 64,010 lines, fuzz 38, bench 36 (counted twice, agree).
- Architecture as settled: UI thread owns `piece_tree`/grid/platform;
  workers get immutable `piece_snapshot`, reply via `work_pool` mailboxes
  (`src/work/work.h:1-11`); GL via dlopen, no `-lGL`
  (`src/gl/renderer.inc:202-203`), init on worker, submit/present on UI
  (`src/render/render.h:258-269`, second half single-source).
- M0 exited by use 2026-10-26 (edit-pi6 CLOSED). Main 9dcdf6f / 79cc880;
  installed binary still older 6667453 (`HANDOFF.md:6,8`) — the trial
  currently tests old code. First action stays: `make check` + install.
- Windows is core+tests only: `mk/win.mk` WIN_MODULES excludes editor;
  `src/main.c:8-14` is POSIX unguarded. No Windows editor binary yet.

## Confirmed strengths (critic re-opened bodies)

1. Piece/undo atomicity: reserve pool+add before mutating
   (`src/piece/piece.c:1305-1312`, fast path `:837-839` checked); undo
   reserves before the piece call (`src/undo/undo.c:349-354`); replay
   under a checkpoint aborted wholesale (`src/undo/undo.c:179-198`).
   Sole malloc in piece is the test allocator (`piece.c:165`).
2. Atomic identity-validated save: O_EXCL temp, fsync, NOFOLLOW +
   symlink refusal + identity (incl. ctime) revalidation, rename,
   dirsync (`src/file/file.c:944-1018`). Open fails closed on
   FIFO/device/dir (`file.c:305-317`).
3. UTF-8 strict Table 3-7, never overreads, invalid bytes round-trip
   as 1-byte units (`src/utf8/utf8.h:32-70`); oracle-tested over every
   scalar and 1,187 Unicode grapheme cases, observed PASS.
4. Test quality where sampled is model-based: `tests/piece_test.c`
   independent oracle + 10^5 random ops + per-alloc fault injection;
   `tests/find_test.c` exhaustive vs naive + guard pages. All three
   sampled suites observed PASS.
5. Perf numbers genuinely derived (P)/(M)/(E)/(G) with arithmetic
   (`perf/01-perf-target.md` §0, §2); benches exit 0 on timing miss
   per law 1 (`bench/harness.h`).
6. Hostile-input surfaces fail closed (single-source body reads):
   IPC wire validated/zeroed (`src/ipc/wire.c:21-47`), config TOML
   validated + atomically published (`src/config/parser.c:77-325`,
   `runtime.c:97-110`), dnd per-entry rejection (`src/dnd/dnd.c:29-114`),
   font tables pre-validated before stb_truetype (`src/font/font.c:549-653`).
7. Process discipline is lived: 323 decision notes, 39 reviews, worklogs
   with SHAs/suites matching the bead/close commit alternation; review
   triage verified end to end. TODO markers ~0 by design. No secrets in
   the tracked tree (history not scanned).

## Findings — own beads (trial-affecting, law 8)

### F1. The gate cannot enforce G1 or law 2 as claimed — REFUTED claim
`CLAUDE.md:7-8` says G1 is a regression test in `tests/editor_test.c`
and "the counting-allocator test enforces" law 2. In fact:
`editor_test.c` has no G1 assert (only G11 `:1804`; slice check
`:2369-2370` printfs only), and the counting allocator is compiled
out under ASan (`src/base/base.h:74-76`) while `make check` runs only
sanitizer binaries (`Makefile:245-246`). Real asserts are release-only
and conditional (`tests/editor_large_test.c:970,1114`,
`tests/dataloss_test.c:1362` "release required"). Typing lag can
regress through a green gate. Same bead: correct the static-link
claim (`CLAUDE.md:11`): Linux links dynamically (`Makefile:34-36`);
`-static` exists only in `mk/win.mk:137,146`.

### F2. lineidx trusts unchecked allocation results (crash if invariant breaks)
`take_block`/`take_entry` results used unchecked
(`src/lineidx/lineidx.c:209-210,882,886`); a 0 corrupts sentinel
block 0. Safety rests solely on `plan_edit` preflight (`:859-860`).
`scan_find_nth_newline` dereferenced unchecked (`:513-515,990-991,
1034-1035`) vs the checked path (`:365-366`). INFERRED reachability;
no failure demonstrated.

### F3. Post-mutation bookkeeping has no rollback (wrong text / lost recovery)
Mutations apply, then `lineidx_edit` and journal staging run
(`src/editor/input.c:63,390-444`); journal staging can return
`EDITOR_ERR_MEMORY` (`input.c:88,94`). Typing pre-checks identically
(`:376-381`), but find-replace preflight is aggregate
(`src/editor/findpanel.c:17`), so a divergence desyncs index vs tree
until reopen (`src/editor/buffers.c:321-325`) or silently drops a
recovery record for already-applied text. INFERRED; fences currently
make both unreachable by construction.

### F4. File-durability edges report success they did not achieve
Dirsync failure treated as clean on legacy/large save
(`src/file/file.c:796-798`); `check_now` masks stat IO errors as
"no change" (`file.c:643-644`, save still revalidates); false
`FILE_ERR_IO` after a durable rename if only the observer fstat
fails (`file.c:1023`). No loss demonstrated; durability/reporting
gaps in the module that is otherwise the strongest in the tree.

## Findings — backlog bead (registered, not queued)

- Perf: GPU frame 1.009 ms is (E), unmeasured, yet bases G3/G3i/G5;
  B fixture never measured; C is a scaling model whose PB exceeds its
  G1; G7j 30/50 ms is below its own scan floor (`HANDOFF.md:412`) and
  needs restating; `bench/scan_bench.c:11-12` gate constants lack the
  law-4 derivation next to them; small-n bench p99s cannot evidence
  §0.2 p99 gates (`bench/find_bench.c:280` n=31).
- Memory: `piece_delete` with `ref==NULL` copies deleted spans into
  the append-only add buffer (`piece.c:1413-1419`) — unbounded growth
  in delete-heavy sessions.
- API contracts: NULL-arg UB in public piece/undo/lineidx APIs
  (`piece.c:1111,1195,1454`; `undo.c:235,248`); in-repo callers
  pre-guard, so contract risk only.
- Product vs PRD (single-source scope): 117 `KEYS_ARG_UNIMPLEMENTED`
  chords incl. Ctrl+P/F12/Ctrl+Space (`src/keys/keys.c`, grep count);
  goto line-only (`input.c:837-853,915-916`); undo capped
  (`src/undo/undo.h:12-14`) vs PRD "unlimited" (`PRD.md:51`); regex
  replace has no `$1` expansion (`src/findui/findui.h:133`); multiple
  cursors stubbed (`input.c:608-610`); find-in-files violates the
  forever non-goal (`PRD.md:25` vs `src/fif/fif.h:1-3`) — needs a
  decision recorded, not a fix.
- Portability parity (partly already beaded): file drop X11-only,
  Wayland/Win32 stubs return NULL (`src/wayland/dispatch.c:104-109`,
  `src/win32/win32_plat.c:535-537`) — already edit-cxho.19/.20.
  Windows large files fail without placeholder APIs
  (`src/os/os_win.c:1353-1356` → `file.c:401-410`); config runtime is
  POSIX-only (`src/config/runtime.c:25-68`); IPC endpoint/peer is
  env-steerable in production builds (`src/ipc/ipc_posix.c:54-62`,
  same-user only, INFERRED low impact).
- Coupling: editor god-module (12,993 C lines, `private.h` 865 lines /
  ~19 headers); platform ABI misfiled as `src/x11/plat.h` consumed by
  wayland/win32/editor; `work_pool` internals published
  (`src/work/work.h:70-125`); glob build makes boundaries convention,
  not linker-enforced.
- Docs layering: PRD/PLAN/README headers still say binding gates
  (`PRD.md:3,78`), `edit` name/paths (`PRD.md:17,71`), Astra
  golden-master (`PRD.md:15`), "(Wayland later)" (`README.md:13`),
  while current truth lives in `CLAUDE.md:7`, `PRD.md:91,95,117` and
  HANDOFF top ~20 lines. PLAN role text contradicts CLAUDE on who
  closes beads. README understates shipped features.
- Repo hygiene: 42G merged `.wt/` worktrees excluded only per-clone;
  tracked unstripped binaries under gitignored `variants/` (49 files,
  eb0c614); sole P0 edit-b0j stale since 2026-10-10 while design work
  runs under cxho.21-.24; `vendor/README.md` silent on 14 tree-sitter
  grammars (111M, no recorded pins); stale
  `tools/test_runtime_identity.sh` can open the real display (already
  edit-8yer).

## Build/test reality (observed this review)

Release + all 113 sanitizer binaries compiled clean under `-Werror`.
Full green `make check` NOT observed: sandbox LSan/ptrace, then no
Xvfb `:99` for the 33 display-lane binaries. 77/80 non-display
binaries passed individually (`detect_leaks=0`); 3 failures INFERRED
sandbox environment (`file_kill_test`, `ipc_test`, `wayland_test`).
Power stamp: battery "Not charging", capacity 100. Until a full green
run is observed on a clean box, "make check green" hand-over is
unevidenced — see F1.

## Not verified by this review

Full green `make check`; release-run G1/law-2; any bench value; any
fuzzer execution (`make fuzz` is build-only); TSan (no
`-fsanitize=thread` anywhere, despite snapshot/mailbox concurrency);
journal/crash-recovery bodies end to end (no scope opened
`src/journal/`); Windows build/binary, Spiral box, installed binary;
vendored tree-sitter bodies; real-display pixels, CJK/emoji
byte-exactness, photodiode G2/G4b; `file_posix`/`file_win` bodies,
`clip.c` paste path, gsub/discovery, x11 selection transfers, ipc_win
past `:120`; git-history secret scan; accessibility/i18n (declared
none in v1, `PRD.md:83`, unassessed as risk).

## Recommended order

1. Install main (HANDOFF first action) and confirm the live Nemo drag
   (edit-cxho.6) — validates already-landed crash fixes in real use.
2. F1 bead: release enforcement lane + one observed full green
   `make check` + doc corrections.
3. F2/F3 beads: fail-safes at the two invariant-only seams, with
   fault-injection tests in the `piece_test` style.
4. F4 bead when the file module is next touched.

## Beads filed from this review (law 8 triage)

Filled in at filing time; see the commit that adds this file.

- F1 (P1): edit-hxa7
- F2 (P2): edit-cmpo
- F3 (P2): edit-i4mz (depends on edit-cmpo)
- F4 (P3): edit-j988
- Backlog (P4, all remaining findings above): edit-if82

No P0 filed: no finding is a demonstrated data-loss/crash in trial
use; F2–F4 are invariant-fragility with INFERRED reachability. If any
reproduces in use, promote on sight.
