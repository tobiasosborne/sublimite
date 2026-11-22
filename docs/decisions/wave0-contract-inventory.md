# Wave 0 — v1 behavioural contract inventory (lane M2)

Sources read for this file: v2 `CLAUDE.md`, `docs/PLAN_V2.md` §6 (Wave 0);
v1-oracle `HANDOFF.md` (sessions 16–30 sections) and
`docs/worklog/2026-10-18.md` … `2026-11-03.md`; v1 test/tool sources cited
by path + symbol below. All v1 paths are under
`/home/tobias/Projects/sublimite-v1-oracle/` (prefix `v1:` elided in tables).
No v1 code was built or run; no `bd`/`git` was run (per brief). The pinned
commit `6fec2e8` is taken from the brief, not re-verified.

Contract-suite principle (brief + PLAN §6): drive a binary named by a
variable (`$SUBLIMITE_BIN`; v1 today, v2 later) under a disposable isolated
profile (`XDG_DATA_HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`); observe
only file bytes, recovery after SIGKILL + restart, exit codes, IPC
behaviour, and screen content through one inspector.

**BLACK-BOX** = drives the built binary via CLI / X11 input / files /
journal / kill+restart, asserts on externally visible outcomes.
**WHITE-BOX** = `#include`s internals (`*.c`, `*/private.h`) or calls
in-process symbols (`editor_inject`, `editor_step`, module APIs) — even if
it also forks and SIGKILLs. By this definition almost every v1 editor-level
suite is white-box: counted with
`grep -l '#include.*private\.h\|#include.*\.c"' tests/*.c | wc -l` →
**63 of 120** `tests/*.c` files (counted this run). The remaining files are
mostly module unit tests (public-header only, e.g. `tests/minimap_test.c`,
`tests/view_test.c`, `tests/wrap_test.c`, `tests/dnd_test.c`) — still
white-box for contract purposes because they link internal objects instead
of driving `$SUBLIMITE_BIN`. Genuinely black-box v1 harnesses are listed in
§3 (chiefly `tools/design_shots.sh` and the `tools/test_*.sh` CLI scripts).

Counting method (Law 4): rows `R1…R35` + expected-failure rows `X1…X4` are
enumerated as written; every other count states its `grep`/`ls` derivation.
Sizes/latencies are copied from v1 worklog/HANDOFF and keep their `(M)` tag.
Session↔date mapping is from the worklog/HANDOFF headings themselves
(s16 = 2026-10-16, s17 = 10-18, s19 = 10-20, s20 = 10-21/23, s21 = 10-25,
s22/23 = 10-26, s24 = 10-27, s25/26 = 10-27 eve/28, s27 = 10-29;
s28 = 11-01, s29 = 11-02, s30 = 11-03 — skimmed, marked †).
Scope note: "daily-use regression" = a defect Tobias hit in use, a trial /
Astra-drive blocker, or a recovery-review finding fixed in the same wave
because it would bite in use (Law 8 triage). Pure feature work (palette,
menu bar, Windows port W0–W2, design rounds) is excluded except where the
worklog frames it as a use finding (drag-drop, find parity).

Harness abbreviations: `inj` = in-process `editor_inject`/`editor_step`
(null or raster backend, no real window needed for most); `Xvfb` = needs a
private X server (display lock, §3); `corpus` = `/tmp/edit-corpus`
(`tools/mkcorpus.c`, e.g. `log_1g.txt`); `kill` = fork + SIGKILL + restart;
`jnl` = private journal dir; `IPC` = `tests/ipc_isolation.h`; `Wl-mock` =
`tests/wayland_mock.h` socketpair compositor; `font` = real font files.

## 1. Daily-use regressions, sessions 16–27 († = sessions 28–30 skim)

| # | Regression | Sess. | v1 fix (as stated) | v1 test (file, function) | Harness | Box |
|---|---|---|---|---|---|---|
| R1 | Wrapped Return on bottom visual row takes far-jump re-anchor; view jumps to last line every 1–5 min while typing (PLAN §6 "wrapped Return") | s19, 2026-10-20 | edit-6090, landed `4011fef`; fix in scroll-follow, cf. `src/editor/input.c:277` comment | `tests/editor_test.c`, `n6090_typing_top` (`--6090-typing`) + `n6090_case` | inj | WHITE |
| R2 | Same family, unwrapped: Return on bottom row leaves caret row off-frame for a frame | s19 | edit-vp4m, landed `e4fa53c` (stale `row_byte`) | `tests/editor_test.c`, vp4m case at :6590 ("unwrapped Return on the bottom row…") | inj | WHITE |
| R3 | Ctrl+S fails on ordinary files while a Find Results tab is open (PLAN §6; recovery-3 F1) | s17, 2026-10-18 | edit-3f2k, landed `f436eb3`; `editor_recorded_rank` in `src/editor/session.c` | `tests/editor_test.c`, `n3f2k_save_with_results` (`--3f2k-save`) | inj, files | WHITE |
| R4 | Over-capacity session restore drops unsaved buffers / unlinks its source journal (PLAN §6) | s27, 2026-10-29 | edit-zizz, landed `8b11df6`/`34699c5`/`e2b3284` (3 review rounds) | `tests/editor_session_test.c`, `zizz_partial`/`zizz_deferred`/`zizz_changed`/`zizz_vanished`; classifier in `tests/editor_recovery_test.c:82` | inj, jnl, fork child (`zizz_child`) | WHITE |
| R5 | Opening/activating a PDF (binary) tab kills editor, error −2: zero-sized glyph entries from blank fallback glyph → `RENDER_ERR_BOUNDS` (PLAN §6 "PDF blank-glyph") | s17 | edit-2ghs, landed `86d268a`; `src/font/async.c:481` (`inked` flag) | `tests/editor_test.c`, `binary_open_contract` (`--2ghs-binary`); glyph side in `tests/unicode_render_test.c:434,591` | inj, font | WHITE |
| R6 | Large-file navigation (Ctrl+End/PageUp/PageDown/Home/End) stalls behind occupied bulk lane (index/warm/find) (PLAN §6 "occupied-bulk large navigation", cites `editor_large_test:1119`) | pre-s16, re-verified s16–17 | edit-91c foreground resident lane; hold model in test header at `tests/editor_large_test.c:1129` | `tests/editor_large_test.c`, `starved_follows` (:1150; hold/after-index/no-hold variants :2275) | inj, corpus (`log_1g.txt`, 1 GiB asserted) | WHITE |
| R7 | Wrapped Ctrl+End on lazy 1 GiB file queries lines from byte 0: 7025 ms → 0.061 ms (M) after fix | s17 | edit-wv7, landed `f6a4cf0` | `tests/editor_large_test.c` + `tests/view_test.c` (grep `wv7`, both files) | inj, corpus | WHITE |
| R8 | First wrapped PageUp on lazy file stalls in `piece_byte_to_line`: 217 ms → 0.1 ms (M) | s17 | edit-1vun, landed `dd17081` (layout upward row query) | `tests/editor_large_test.c` + `tests/wrap_test.c` (grep `1vun`) | inj, corpus | WHITE |
| R9 | Dirty hot-exit session on 64 MiB file fails to relaunch, exit −103 (Astra drive-5 task 37, the drive-5 blocker; drive 6 = s16 verified FIXED) | s15–16, 2026-10-16 | edit-f3i `9b5ee41` (recovery origin + op-log install) + edit-c26 test `fd3db33` | `tests/editor_session_test.c`, `c26_dense_hot_exit_body` + `c26_drive_journal_body` over fixture `tests/fixtures/drive5-task37-session.jnl` | inj, jnl, fork launches | WHITE |
| R10 | Every second save of an mmap-acquired file refused `FILE_ERR_CHANGED` (recovery-3 F5) | s17 | edit-fkpk, landed `48b7c7c`; symbol `file_source_adopt_retired` | `tests/editor_session_test.c`, `select_edit_fkpk` (`--edit-fkpk`, case at :129) | inj, files | WHITE |
| R11 | Mouse click puts caret at line end; drag selects whole lines; syntax colour missing — all in wrapped buffers (ASCII fast path recorded no hit entries) | s19 | edit-a7fo, landed `67f3158`; `src/layout/wrap.c` hit entries | `tests/editor_test.c`, `a7fo_click_contract` / `select_a7fo_click` (`--a7fo-click`) | inj (synthetic pointer events) | WHITE |
| R12 | Minimap viewport grab jumps its top to the pointer instead of keeping grab offset | s19 | edit-2mhd, landed `3286018` | `tests/editor_test.c` minimap-grab case at :577–613 | inj | WHITE |
| R13 | Text area one column too wide under the minimap after gutter widens (stale view column count) | s19 | edit-9f63, landed `3620e55` | `tests/editor_test.c`, `minimap_text_columns` (`--9f63-columns`) | inj | WHITE |
| R14 | Closing an unsaved tab gives no warning; a modified closed tab is silently evicted after 16 further closes (closed-stack cap, `src/editor/open.c:95` in worklog) | s20, 2026-10-23 | edit-2ye3, landed `4a71e27` (S/D/Esc status-row prompt; eviction prefers unmodified, refuses at full-modified) | `tests/editor_test.c` cases at :7254/:7367/:7415 (round-2 1–5); stack policy in `tests/tabs_test.c:422`; callers updated in `editor_p4/session/large/dataloss` tests | inj | WHITE |
| R15 | After ~1000-request open burst: idle 100 % CPU spin and journal durability frozen (`journal_waiting` shadow flag drained by `journal_flush`; `runnable()` miscounted gated file command); round 2: Save As + queued Ctrl+N gate deadlock | s27 | edit-duuw, landed `977818e`/`190aa1f`; `journal_in_flight()` replaces flag | `tests/editor_session_test.c`, `duuw_burst`/`duuw_gated`/`duuw_save_as_queue` (`duuw_all`, `--edit-duuw-burst`) | inj, jnl, work-pool hold probe | WHITE |
| R16 | Open burst exhausts fds (multiple fds per file; soft `RLIMIT_NOFILE`); EMFILE mid-open left partial state | s27 | edit-m25u, landed `6838d56` (one fd/file, raise soft limit at startup, rollback + one notice) | `tests/ipc_test.c` cases at :398 (limit raise), :450 (one fd/open), :477 (exhaustion refusal) — note: housed in IPC suite, drives `../src/main.c` via `#include` with null backend | inj-ish (real `main` TU), IPC, rlimit | WHITE |
| R17 | Undo history lost after Save As then plain Ctrl+S (save's own rename reloads the buffer) | s19 | edit-dxas, landed `aea979e` (P2) | `tests/editor_session_test.c`, `dxas_all` (`--edit-dxas`, 8 variants) | inj, files, jnl | WHITE |
| R18 | Save refused while another tab is still loading | s17, landed s19 | edit-uzrb, landed `1c1cd35`; single predicate `editor_tab_recorded` | `tests/editor_session_test.c` (grep `uzrb`) | inj, jnl | WHITE |
| R19 | A render-rule violation ends the UI loop (editor exits on bad grid); a restored tab can crash-loop every launch | s17, landed s19 | edit-dzk1, landed `3b23f0a`; `JOURNAL_PAINT` record + crash-loop guard (journal format change) | `tests/render_test.c:175` (repair after rejected grid), `tests/cli_test.c` hot-snapshot `paint` field, `tests/editor_test.c:7094` grid-validate case | inj, jnl; cli_test forks | WHITE |
| R20 | Typing frame waits on a retiring scroll window when index prefix grows after seek (typing lag; root cause of `editor_scroll_test:117` failures) | s17 | edit-63p, landed `8f4c7de` | `tests/editor_scroll_test.c` (grep `63p`; "5/5, 0 BLOCKED" in worklog) | inj | WHITE |
| R21 | Minimap on an empty tab shows garbage bands | s24, 2026-10-27 | edit-cxho.1, landed `7a72141` | No bead-tagged case found (`grep cxho.1 tests/*.c` hits only cxho.10/.12 comments); covered indirectly by `tests/minimap_test.c` + `editor_test` minimap cases — see §4 | inj / module | WHITE |
| R22 | Find panel lets window commands through (find then Ctrl+W etc. misbehave); whole mode×action matrix inconsistent across find/go-to/palette/menu/prompts | s24 | edit-cxho.2 `8d7ad17`; matrix edit-cxho.16 `43f608c` (256 PASS / 32 XFAIL), consistency edit-cxho.17 `5ff16b2` (307 PASS / 1 XFAIL, the remainder = cxho.18) | `tests/modal_matrix_test.c` (mode×action enums in header; witnesses via editor input); `tests/editor_test.c:4402,7432` | inj | WHITE |
| R23 | Find field vs document focus wrong; panel toggles not clickable; caret/Tab/Escape semantics off Sublime parity | s24 | edit-cxho.3, landed `836f41f` | `tests/editor_test.c` focus case :4754–4834; `tests/findui_test.c`, cases at :1199 (cell clicks/toggles) and :1265+ (gutter/minimap focus) | inj | WHITE |
| R24 | Jump-to-top while typing returns in wrapped mode (multi-key frames, soft-wrap growth, paste) — R1 regressed in wider shapes | s24 | edit-cxho.25, landed `d9c0ff4` (P1; seeded random typing, worklog: 0/15012 violations) | `tests/editor_test.c`, `cxho25_typing_jump` (`--cxho25-typing`) | inj | WHITE |
| R25 | Busy layout restart leaves zeroed rows that readers answer from (root cause under R24; also large lazy files) | s24 | edit-cxho.26, landed `3a4b7a4` (rows marked void; `relative_wrap_place`) | `tests/wrap_test.c` cases :485/:509/:537; `tests/view_test.c` case :902 | module (layout/view APIs) | WHITE |
| R26 | Dragging a file from the file manager onto the window does not open it | s24 | edit-cxho.4 `3c7524b` (URI parser), .5 `51b69dc` (X11 XDND target), .6 `ca691fb` (editor opens drops; live-Nemo confirmation only in s27) | `tests/dnd_test.c` (parser, module); `tests/x11_dnd_test.c` (XDND over xcb, `x11_xvfb.h`); `tests/editor_test.c` drop cases :7532–7682 via headless drop seam | module; Xvfb+xcb (protocol half); inj (editor half) | WHITE (XDND half is protocol-black toward the X server, but still links `x11/plat` internals, not `$SUBLIMITE_BIN`) |
| R27 | Minimap on big files: viewport is a thin line, drag does not track mouse 1:1, stripes instead of real lines | s24 | edit-cxho.8 `1097f08` (fixed-scale mapping), .9 `ed1f81e` (real window lines), .10 `6667453` (constant box, 1:1 drag; worklog: 0 px off at 10 positions) | `tests/minimap_test.c` :538 (cxho.8), :673+ (cxho.9), :878 (cxho.10); `tests/editor_test.c` :344+ drag-tracking cases | module; inj | WHITE |
| R28 | Find is not incremental and does not prefill from selection/caret word; Ctrl+E/F3/Shift+F3 family missing (Sublime parity, from use) | s24 | edit-cxho.12 `96ae682`, edit-cxho.13 `7b162a9` | `tests/editor_test.c` :4838 (cxho.12), :4936+ `find_seed_*` (`--cxho13-*`); `tests/findui_test.c:129` worker origin seek | inj | WHITE |
| R29 | Two crashes: input flood parked behind a pending open; Ctrl+W (busy tab commands) while busy | s24 | edit-cxho.7, landed `69e8a06` (both reproduced red first) | `tests/editor_test.c` cases :7684 (1100-event burst), :7798/:7817 (busy actions / busy Ctrl+W), :7858 (ring order) | inj | WHITE |
| R30 | Extension-less files (README, notes) open unwrapped | s24 | edit-cxho.27, landed `4a5fb13` (wrap default on basename) | `tests/view_test.c`, `view_wrap_default` table at :975 | module | WHITE |
| R31 | Recovery-3 wave (would bite in use; fixed s17–s19): F2 use-after-free at close during retention; F3 retired buffers leak slots/fds; BUSY not retried on all checkpoint paths; F10 checkpoint publish on UI thread; scroll_slice recoverable errors reach `fail()` | s17–19 | edit-9ev6 `53eed9e`, edit-qcuh `ee913f0`, edit-ivv `f9d23dc`, edit-ze59 `3e2fcae`, edit-bt1 `ff69193` | `tests/dataloss_test.c` (9ev6/ze59/ivv cases), `tests/editor_session_test.c` (qcuh/ivv), `tests/editor_scroll_test.c`+`editor_test.c` (bt1) — by bead grep | inj, jnl, kill (dataloss forks) | WHITE |
| R32† | Windows: mouse drag/snap/resize broken — editor ended pump turn with `plat_quit`, `modal_service` posted `WM_CANCELMODE`. "Found by Tobias's use, not by any suite" (worklog 2026-11-01) | s28† | edit-177r.31 (landed on main per worklog; no SHA stated there) | None at the time; later live-gated by edit-177r.33 source guard (HANDOFF s28). No automated regression case named | real desktop only | — (gap, §4) |
| R33† | Minimap stalls on biggish files, minitext goes blank; "minimap preview not the right size" | s28† | edit-177r.34 — explicitly "not investigated" (HANDOFF s28) | None | — | — (gap, §4) |
| R34† | Wayland clipboard copy called X11 code (copy broken on Wayland); primary selection missing | s30† (bug pre-dates; PLAN §6 lists "Wayland clipboard copy" as known v1 bug) | edit-b6ai.9 (landed s30); `wayland_plat_clip_set_buf` in `src/wayland/private.h:35` | `tests/wayland_test.c` :579+ primary-selection cases, clipboard send/receive :656/:955 — against `tests/wayland_mock.h` only; worklog: "verified against the headless mock only", real-compositor checklist on edit-b6ai.5 | Wl-mock | WHITE |
| R35† | Monolith/Hyprland daily-use batch: test Xvfb auto-pick deleted live `/tmp/.X11-unix/X0` 3× (b6ai.19, P0); XWayland text size from physical mm (b6ai.15); GL vendor check Mesa-only under XWayland/NVIDIA (b6ai.14); window grid capped at 2880 px (b6ai.16); minimap box overflow on short docs (b6ai.17); wrapped Ctrl+End over-reveals (b6ai.18) | s30† | edit-b6ai.14/.15/.16/.17/.18/.19, all landed s30 (SHAs not stated in worklog) | b6ai.19: `tests/x11_xvfb.h` `xvfb_explicit_display` (header comment carries the repro); others: no bead-tagged test found by grep in this pass — see §4 | Xvfb / XWayland; inj | WHITE / mixed |

Expected-failure specs (PLAN §6 Wave 0: "known v1 bugs that v2 must pass" —
contract suite should record these as XFAIL on v1, PASS required on v2):

| # | Known v1 bug | Evidence in v1 tree | v1 test |
|---|---|---|---|
| X1 | 17-leaf `piece` root collapse | No test or source comment found (`grep -rn "17.*leaf\|root collapse" tests/ src/` → only unrelated checkpoint "leaf" rows in `docs/reviews/recovery-3.md:92`). Cannot cite a v1 symbol; treat as new-work spec from PLAN only | None found |
| X2 | Post-rename save conflict (foreign file after rename not detected / mishandled) | No bead-tagged case found; nearest machinery: `tests/file_test.c` save-conflict cases around `FILE_ERR_CHANGED` (:480s) and `tests/savectl_test.c` / `tests/savectl_reload_test.c` (module level) | None at editor/binary level found |
| X3 | `LINEIDX_EDIT_MAX_CHUNKS` (512, `src/lineidx/lineidx.h:38`; admission check `src/lineidx/lineidx.c:858`) rejects >32 MiB-class delete, breaking delete+undo on large replacements | Limit + comment at `lineidx.h:131` ("Larger replacements need an index recreated"); `tests/editor_large_test.c:873` covers only estimated-newline delete/undo/redo | Partial (module/large suite, WHITE); no over-limit editor case found |
| X4 | Wayland clipboard copy | See R34†: at the pinned tree this is already fixed (s30) but mock-verified only. The contract suite must decide GREEN-vs-XFAIL against the actual v1 binary it runs, not from PLAN's older list | `tests/wayland_test.c` (Wl-mock, WHITE) |

Reading of §1 for suite design: **0 of R1–R31 is covered black-box today**
(counted: every cited test either `#include`s `editor/private.h` /
`../src/main.c` or links module objects and injects `plat_event`s
in-process). The behaviours are well pinned, but every pin must be
re-expressed through `$SUBLIMITE_BIN` (§2). Closest to black-box already:
`tests/cli_test.c` and `tests/ipc_test.c` (fork/exec children, real IPC,
SIGKILL) — still white by the `#include "../src/main.c"` + `private.h`
route; `tests/dataloss_test.c` (fork child editor, SIGKILL from parent or
worker hook, restart, byte-oracle compare — header comment) is the best
*shape* to copy, minus its in-process editor.

## 2. Black-box formulations (one per white-box row)

Common fixture for every case unless stated: fresh temp dirs for
`XDG_DATA_HOME`/`XDG_CONFIG_HOME`/`XDG_RUNTIME_DIR` (mode 0700 runtime,
cf. `test_ipc_isolate`), private Xvfb display via explicit number (never
`-displayfd` alone, R35†), `EDIT_REFUSE_REAL_DISPLAY=1`, launch
`$SUBLIMITE_BIN --new-instance <files…>`, drive keys/pointer with XTest
(the `tools/keyinject.c` mechanism, §3) or, where only text outcome
matters, with a second launch (IPC forwarding) + file edits; read the
screen through **one** inspector (proposed: root-window capture + OCR-free
cell readback is *not* available black-box — use the `design_shots.sh`
precedent: settled-frame `xwd` grab + pixel assertions on named regions,
plus journal/file bytes and exit codes; caret/top assertions below are
phrased as "line N's text is/isn't in the captured viewport rows", which
the pixel inspector answers by template-matching rendered fixture text —
fixtures use a monospace cell grid, so row bands are computable exactly as
`design_shots.sh` computes `cw`/`ch` from DPI). Settle = frame unchanged
for the blink-safe quiet window `design_shots.sh` already derives.
"Save and read file" always asserts exact bytes on disk. Where a v1
white-box test asserts an internal counter (malloc guard, lane held), the
black-box form asserts only the user-visible consequence and says so.

| # | Inputs → steps → observable outcome |
|---|---|
| R1/R2/R24/R25 (typing-top family) | Fixture: wrapped `.md` (and unwrapped `.c` twin) with numbered lines `L0000…L2999`, some lines longer than the viewport. Steps: open, go to a mid file line, then inject a long typing burst (letters + Returns, incl. multi-key bursts and a long paste via clipboard) at the bottom visible row. Outcome: after settle, captured viewport still contains the caret's line text and the first viewport row has advanced by at most the number of inserted visual rows (never jumped to EOF / top); then Ctrl+S and disk bytes equal the independently computed expected text. R25's void-row unit cases have no separate black-box form — R24's burst (second key before first frame) is their observable shadow; keep module tests for the mechanism in v2's own suites, not the contract suite. |
| R3 | Two small files A, B containing `cat` matches. Steps: open both, Ctrl+Shift+F find-in-files for `cat` so a Find Results tab exists, switch to A (tab before results) and to B (after results), type a marker in each, Ctrl+S each. Outcome: both files' disk bytes contain their markers; editor still alive (no exit); results tab content unchanged on screen. |
| R4 | Build an over-capacity session black-box: launch, open many clean files plus untitled/modified buffers with known texts (U1, M, U2, U3 pattern from `zizz_child`), settle until journal checkpoint exists, SIGKILL. Outcome on relaunch (same profile): every unsaved text is present in some tab (inspector + save-each-tab-to-disk via Save As to prove bytes), clean tabs beyond capacity may be absent, the source journal file still exists on disk (not unlinked) after a partial restore, and a second SIGKILL+relaunch loses no byte. Capacity itself is discovered, not assumed: binary-search the tab count at which clean tabs start being dropped — do not hard-code v1's number into the contract. |
| R5 | Fixture: binary file with NUL/high/invalid-UTF-8 bytes (the `binary_open_contract` shape) plus a real small PDF if the suite may carry one. Steps: open it next to a text file, activate its tab, scroll, switch away and back, close and reopen. Outcome: process alive throughout (exit code only on deliberate quit = 0), window paints (non-blank capture), text file still editable and saves exact bytes. |
| R6/R7/R8 (large navigation) | Corpus-scale file (generate ≥1 GiB sparse-ish numbered-line file at suite setup; `mkcorpus` `--small` variant for smoke runs, full size for the gate run). Steps: open, *immediately* (before settle — this replaces the white-box bulk-lane hold, whose internal "lane still held" assertion has no black-box equivalent) inject Ctrl+End, PageUp×n, Home, End, and a typed character. Outcome: each navigation's target line text appears in the viewport within the suite watchdog (watchdog value is a suite (G) to be set by the Wave-0 implementer from a v1 (M) run — none is invented here), typed char lands at the right place (save to a *copy* path via Save As… a 1 GiB save is expensive; cheaper oracle: SIGKILL, relaunch, assert recovered text at caret via inspector, then discard profile). R7/R8 are the wrapped (`.md` name) repetitions of Ctrl+End / first PageUp. |
| R9 | Reuse the drive-5 shape, not its bytes: 64 MiB-class dense-short-lines file; launch, type a 2-byte marker at head, wait for checkpoint, SIGKILL; relaunch ×2. Outcome: both relaunches exit the startup path alive (no −103-class failure: window appears), marker text present, buffer shown modified (unsaved dot in tab, inspector), source file on disk unchanged. The committed fixture `tests/fixtures/drive5-task37-session.jnl` is v1-format-specific — usable only for the v1-oracle run, never as a v2 input (v2 journal import is Wave 4, a separate case). |
| R10 | File large enough / named so v1 mmaps it is implementation detail — black-box form must not care: open file, type marker 1, Ctrl+S, assert disk; type marker 2, Ctrl+S, assert disk has both markers and no error notice on screen. Run for a small file and a corpus file. (If v2 never mmaps, the case still passes — correct: the contract is "second save works".) |
| R11 | Wrapped file with long lines of known cell layout. Steps: XTest-click at computed pixel centre of a chosen mid-line character cell; type `X`. Outcome: after Ctrl+S, `X` sits at exactly that byte offset in the file. Drag variant: drag across part of one visual row, Ctrl+C, paste into a scratch tab, save scratch, assert pasted bytes = the partial-row substring (not whole lines). |
| R12/R13/R27 (minimap geometry) | Tall numbered-lines file. Steps: compute minimap strip + viewport-box pixels from a capture; XTest-drag the box by a known dy starting from a grab point offset inside the box; assert (a) first viewport line changes by the amount the *box* moved (grab offset preserved: box does not jump its top to the pointer), (b) box height in pixels is constant across file sizes (repeat with short file: box clipped to strip, R35-b6ai.17), (c) text never renders underneath the minimap strip (rightmost text column pixels belong to minimap background at every gutter width — widen gutter by opening a ≥1000-line file). Drag-tracking tolerance is a suite (G) from a v1 (M) run; v1's internal claim was 0 px (M) in-process. |
| R14 | Open file, type marker (modified), Ctrl+W. Outcome: prompt row visible in capture (inspector finds the prompt text region non-blank / template), file tab still present; press Esc → tab + text intact; Ctrl+W again, press the Don't-save key → tab gone, disk unchanged; repeat with Save key → disk has marker. Eviction half (closed-tabs stack, reopened via Ctrl+Shift+T): close many tabs — modified ones only via the prompt — past the discovered stack capacity, then reopen repeatedly; contract: every modified text either reopens intact or its close was explicitly refused with a visible notice at close time; never a silent vanish. |
| R15/R16 (open burst) | Generate ~150 small files; launch editor, then forward all of them via repeated second launches (`$SUBLIMITE_BIN file…` without `--new-instance`, exercising IPC) — the s27 trigger shape. Then Ctrl+N, type marker, wait, SIGKILL, relaunch. Outcome: no 100 %-CPU idle spin (sample `/proc/<pid>/stat` utime over an idle window — threshold is a suite (G) from v1 (M)), journal file mtime/bytes advance after the marker edit (durability not frozen), recovered untitled text present after relaunch, every accepted open either produced a tab or a visible refusal notice, and process fd count (`/proc/<pid>/fd`) grows by ≤1 per opened file on average (R16's one-fd contract, read from outside). Deadlock sub-case: start Save As on a large file, immediately Ctrl+N + type; both complete (disk bytes + new tab text). |
| R17 | Open A, type m1, Save As to B, type m2, Ctrl+S, Ctrl+Z. Outcome: screen text lacks m2 but keeps m1; Ctrl+S again; B's disk bytes = original+m1. (Undo surviving Save As + save is the whole contract.) |
| R18 | Open a corpus-large file (slow load) in tab 2 while tab 1 (small, modified) waits; immediately Ctrl+S on tab 1. Outcome: tab 1's disk bytes updated; no refusal notice; large tab eventually displays. |
| R19 | No black-box trigger exists for "render-rule violation" (it needs a malformed grid). Observable shadow: R5's binary file + R29's input flood + resize storm during typing; contract = editor never exits except on quit (exit code 0), and after SIGKILL mid-storm the relaunch does not crash-loop (3 consecutive launches reach a painted window — the JOURNAL_PAINT guard's user-visible half). The grid-repair unit case stays a v2-internal test. |
| R20 | Typing-latency consequence only: large file, seek mid-file (go-to-line), then type continuously while the index prefix grows; assert every typed char appears (save + byte compare) and (optionally, release lane) input→paint latency via the keyinject/Present-MSC method (§3) stays within the G1 gate the suite inherits — the internal "didn't wait for retiring window" mechanism is not observable and is not contracted. |
| R21 | New empty tab next to a text tab. Outcome: minimap strip pixels for the empty tab equal the strip's background (no bands) in a settled capture; switching tabs toggles strip content accordingly. (Weak oracle without a cell inspector — pixel-band assertion on the strip region is the honest black-box form.) |
| R22 | Subset of the modal matrix that is user-observable (the full 307-cell matrix stays v2-internal): for each overlay in {find panel, go-to, palette, menu, close prompt}: open overlay, send Ctrl+W / Ctrl+N / Ctrl+S / Ctrl+Tab / typing, and assert from capture + disk that (a) window commands do not close/create tabs behind the overlay, (b) typing goes where focus claims (query field vs document, by saved bytes), (c) Escape returns focus to the document with text intact. |
| R23 | Click the find panel's toggle cells and query field via XTest at inspector-located positions; type in field vs document after clicking each; assert via saved bytes (document typing) and via match-highlight movement in capture (field typing). Tab cycles focus field→document observably (typing lands in document). |
| R26 | Real XDND against the real binary: a small X11 helper client (new suite code, modelled on `tests/x11_dnd_test.c` but targeting `$SUBLIMITE_BIN`'s window found via `_NET_CLIENT_LIST`/title) performs an XDND drop of `text/uri-list` with two files and one directory. Outcome: two new tabs with the files' contents (save-copy byte check), directory skipped with a visible notice, last dropped file active. Wayland/Win32 drop = later lanes (v1: cxho.19/.20 backlog). |
| R28 | Type a query in the find field one char at a time; after each char the viewport must already show the first match beyond the origin (capture contains match line) without pressing Enter; Escape restores origin line. Prefill: select a word (double-click), Ctrl+F → field shows it (capture template) and F3 jumps to next occurrence (viewport line advances); Ctrl+E with panel closed seeds the same word. |
| R29 | Flood: while a large open is pending, inject ≥1100 events (keys + wheel + button) via XTest, then Ctrl+W repeatedly. Outcome: process alive, no crash exit, queued text input is not lost wholesale (saved bytes contain the typed suffix at minimum — exact prefix-drop policy is *not* contracted black-box; v1's counted-no-op semantics stay internal), editor still saves and quits cleanly (exit 0). |
| R30 | Open file named `README` (no extension) containing one very long line. Outcome: capture shows the line wrapped (its tail text visible on a continuation row) with default config; the `.c`-named twin shows horizontal-scroll behaviour (tail not visible). Config-file override: same with a config that forces the opposite, proving it's the default and not hard-wired. |
| R31 (recovery wave) | The user-visible core is one torture loop, already the `dataloss_test` shape: scripted edits + saves + Save As + tab closes interleaved with SIGKILL at randomised points (seeded), relaunch after each, oracle = for every file: disk bytes ∈ {last acked save, last acked save + journaled edits} and never partial/torn; untitled texts always recoverable; fd count and tab count do not grow across loops (F3 leak shadow, via `/proc`); a save racing an external rewrite of the target (suite rewrites file between Ctrl+S keypress and settle, repeated) ends in either a visible conflict notice or the external bytes intact — never silent clobber (also covers X2's observable half). F10's "off UI thread" becomes: typing stays responsive (chars land) while a large checkpoint/save is in flight. |
| R34†/X4 | Needs a real (headless) compositor, not the mock: run `$SUBLIMITE_BIN` under a headless Wayland compositor the suite launches (e.g. weston headless if present — availability is an implementer check, not asserted here), select text, Ctrl+C, then read the selection with an independent Wayland client and paste into a second editor instance; assert bytes round-trip both directions + primary-selection middle-click paste. Until such a compositor lane exists this row is XFAIL-by-environment on both binaries (fail-closed: report as not-run, never as pass — the v1 mock pass at `wayland_gl_test.c:82`-style silent skip is exactly what PLAN §5 bans). X11 clipboard twin (XTest + `xcb` selection owner helper, cf. `tests/x11_clip_test.c` shape) runs today. |
| R35† | b6ai.19 is harness, not editor: contract = the *suite's own* Xvfb allocator never returns :0 and never unlinks a foreign socket (unit-test the allocator; already the `xvfb_explicit_display` design). b6ai.15/.16: launch on an Xvfb with synthetic physical size (Xvfb `-dpi` / screen mm) and assert cell pixel size / grid columns from capture geometry match the 96-dpi×scale rule, not the mm rule. b6ai.14: GL-vendor acceptance is not black-box distinguishable from "GL works here" — contract = editor paints and types on the suite's GL-capable Xvfb (llvmpipe) without falling back to an error exit; the NVIDIA/XWayland specifics stay manual (b6ai.5 checklist). b6ai.18 = R7's wrapped Ctrl+End with the extra assertion that the caret row is the *bottom* row (minimal reveal), not centred. |
| X1 | New black-box spec (no v1 oracle behaviour to copy): construct, via scripted typing/paste/undo on a plain file, an edit history that fragments the buffer into many small pieces (interleaved inserts at spread offsets), then select-all + delete + type + save + SIGKILL + relaunch. Contract on v2: exact bytes at every stage; on v1: record actual (expected FAIL per PLAN). The precise "17-leaf" trigger is internal — the implementer must confirm the scripted history actually reaches the collapse on v1 (red run) or the case proves nothing. |
| X3 | File >32 MiB (number from PLAN's phrasing; the v1 limit symbol is `LINEIDX_EDIT_MAX_CHUNKS 512` — the byte size at which it bites depends on chunking, so the case must be tuned until v1 goes red): open, select-all, delete, Ctrl+Z, Ctrl+S to a copy. Contract: copy bytes = original bytes; editor alive; on v1 expected FAIL per PLAN. |

## 3. Reusable v1 tools & fixtures

Counted inventory basis: `ls tools/` (59 entries incl. dirs, this run) and
`ls tests/*.h` (12 headers). Only items a black-box contract suite can
reuse are listed; build/codegen/orchestration tools (`codex_run.py`,
`council_run.py`, `check_parallel.py`, generators, installers) are omitted
deliberately.

| Tool | Path (v1:) | What it does | Interface | Needs v1 internals? |
|---|---|---|---|---|
| keyinject | `tools/keyinject.c` (+ `tools/refwin_protocol.h`) | XTest key injection scheduled from measured per-window Present NotifyMSC/UST; pairs with a reference window for latency (G1) measurement; CSV output | `keyinject --window ID [--dry-run\|--csv PATH] [--pairs N] [--first-pair N] [--synthetic-clock] [--send-event] [--wait-reference]` (usage at `keyinject.c:137`) | No editor internals — speaks X11/Present + its own atoms to any window; reuses `trace_now_ns` via refproto. **Reusable as-is against `$SUBLIMITE_BIN`** for R20/XTest driving (needs a general keysym/text mode added for contract use) |
| refwin | `tools/refwin.c` | Bare reference renderer (one baked glyph) on the editor's CPU backend/Present path; the latency oracle keyinject pairs against | CLI driven by `tests/refwin_test.c` harness (`--dry-run --window …`) | **Yes** — `#include "x11/private.h"`, links raster/font/work. Not reusable against v2; the *protocol* (`refwin_protocol.h`, `tools/refwin_pairs.py`, `tests/refwin_pairs_test.py`) is the reusable part |
| replay | `tools/replay.c` | Re-injects a trace dump's input log with original timing; default sink prints one line/event | `replay [--fast] [--speed=x] dump`; exit 1 malformed, 2 usage (header comment). CLI contract pinned by `tools/test_replay_cli.sh` (bad speeds → rc 2 naming `--speed`) — itself a black-box test worth copying | Depends on v1 trace format (`src/trace/trace_fmt.h`). Reusable only if v2 keeps the dump format; otherwise re-implement the sink idea. Note: prints, does not drive a window — contract driving still needs XTest |
| journal_replay | `tools/journal_replay.c` | Manual recovery: replay a journal onto a file, optionally `--apply`, per-`--buffer ID`; `--acknowledge` discards a set-aside explicitly (F12) | `journal_replay [--apply] [--buffer ID] JOURNAL FILE` / `--acknowledge RESERVED_SET_ASIDE`; prints `ACKNOWLEDGED …`; rc = journal error | **Yes** — links `journal/journal.h`, v1 journal format. For the v1-oracle run it is the recovery oracle (s27 used `--apply` to rescue Tobias's buffers); for v2 it only works if v2 reads v1 journals (Wave-4 import). Contract cases must not *require* it — recovery must be observable through the editor itself |
| tracedump | `tools/tracedump.c` | Per-gate latency p50/p99 from a trace dump (G1 = T4−T0 etc.) | `tracedump <dump.bin>` | v1 trace format only; reusable for v1-side (M) numbers when setting suite (G) watchdogs |
| mkcorpus | `tools/mkcorpus.c` | Deterministic perf corpus (seeded xorshift64): `log_1g.txt` 1 GiB, `dense_short.txt` 64 MiB, `oneline_1g/10g`, malformed/unicode/CRLF files… (`tools/README.md`) | `mkcorpus [--small] [--huge] [DIR]` (default `/tmp/edit-corpus`); standard C only, no project headers | **No. Reusable verbatim** — the contract suite's large-file generator; `--small` (÷64) for smoke runs |
| save-kill hook | `src/file/file.h:333,351` `file_set_step_hook` + `FILE_STEP_*`; used by `tests/file_test.c:497` `kill_hook`/`t_durability` and `tests/file_kill_test.c` `file_test_kill` | Fires a callback at each save step (temp created → renamed → dir synced); tests SIGKILL the child at each step and assert old-xor-new bytes, never partial | In-process C callback only — no CLI/env | **Yes, by design.** Black-box replacement: external SIGKILL racing real saves (R31 loop) + strace-free timing via many iterations; exact step coverage is a v2-internal (`--wrap`) test per PLAN §5, not contract. PLAN §6's phrase "save-kill hook" as a contract tool is therefore only true for the v1-oracle's own test binary lane — flag for Wave-0 implementer |
| Wayland clipboard fixture (mock compositor) | `tests/wayland_mock.h` (used by `tests/wayland_test.c`, `wayland_gl_test.c`, `wayland_startup_test.c`) | Headless `wl_display` server on a socketpair in a forked child: xdg-shell, presentation, viewporter, fractional-scale, xdg-activation, primary-selection; serves `"mock clipboard"`/`"owned clipboard"` (:381/:413), tracks wl_shm damage into a shared screen buffer the parent inspects | In-process C fixture (`mock_start_options` fork); no CLI | **Yes** — drives the `plat` API in-process, and overrides `getsockopt` for the sandbox. The *screen-buffer inspector idea* is exactly the "one inspector" the contract needs on Wayland, but it must be re-pointed at a real compositor + real binary (R34) |
| IPC isolation | `tests/ipc_isolation.h` | `test_ipc_isolate()`: mkdtemp `/tmp/sublimite-ipc-XXXXXX` (0700), sets `XDG_RUNTIME_DIR` + `EDIT_IPC_NAMESPACE`, `on_exit` cleanup that forked clients cannot trigger for the parent | Header-only C; env out: `XDG_RUNTIME_DIR`, `EDIT_IPC_NAMESPACE` | No editor internals (pure env/dir discipline). **The pattern is reusable verbatim** in shell/Python form; `EDIT_IPC_NAMESPACE` itself is a v1 env name — v2 must provide its own (PLAN: `sublimite-v2` namespace) |
| Display guard | `tests/display_guard.h` (force-included by Makefile) + `tools/with_display_lock.sh` + `tests/x11_xvfb.h` | Refuses real display :0 (redirects to `EDIT_DISPLAY`, default :99; clears inherited Wayland; exports `EDIT_REFUSE_REAL_DISPLAY` for children); display lock serialises suites per Xvfb via flock (`EDIT_DISPLAY_LOCK_TIMEOUT`, default 600); `xvfb_start()`/`xvfb_explicit_display` allocate an explicit free display by bind-probing (post-R35 design) | env: `DISPLAY`, `EDIT_DISPLAY`, `EDIT_ALLOW_REAL_DISPLAY=1`, `EDIT_WAYLAND_DISPLAY`, `EDIT_DISPLAY_LOCK_TIMEOUT` | Guard header calls production policy (`editor/runtime.h`) — reimplement the env discipline in the suite runner; `with_display_lock.sh` is standalone Python-in-shell and **reusable as-is** |
| design_shots | `tools/design_shots.sh` + `tools/design_fixtures/` (`hello.c`, `notes.txt`, `readme.md`, `util.py`) | **The one true black-box driver in v1**: runs a given binary on private Xvfbs (:91/:92), 11 scenes × 2 themes, settles by identical-frame rule, captures PNGs + `manifest.json` with hashes; byte-identical repeat runs asserted | `tools/design_shots.sh <binary> <outdir>`; env `EDIT_SHOTS_DISPLAY`, `EDIT_SHOTS_THEMES`, `EDIT_SHOTS_KEEP_WORK`; needs Xvfb, xdotool, ImageMagick | **No.** Takes the binary as argv[1] — this is the template (and settle rule, and cell-geometry derivation) for the contract suite's inspector |
| flashscan | `tools/flashscan.py` | Scans captured frames/recordings for orange-flash artefacts (Astra-drive era detector; drive-2: 0 flashes in 84 runs (M)) | Python CLI over image files | No internals; niche — reusable only if the contract grows a visual-artefact lane |
| CLI/IPC surface (the thing under test) | `src/main.c:64` usage; `tests/cli_test.c`, `tests/ipc_test.c`, `tools/test_runtime_identity.sh`, `tools/test_install.sh` | `sublimite [--wait] [--new-instance] [--font-family P] [--font-file P[#I]] [--font-fallbacks A,B] [--] [path[:line[:col]] …] [-]`; second launch forwards via IPC; `--help` = argc==2 special case | Exit codes are editor error numbers (e.g. −103 seen as 153-class shell codes in drives — the suite should assert zero/non-zero + window-appears, not v1's numeric codes, except on the v1-oracle run where they may be pinned) | cli/ipc tests are WHITE (`#include "../src/main.c"`); the two `test_*.sh` scripts are black-box and reusable as patterns |
| dataloss shape | `tests/dataloss_test.c` (header) | Fork child editor → SIGKILL (parent or worker hook) or two-instance race → restart → compare bytes/modified/caret vs child's published oracle over a pipe | Selector args; `--probe` runs known-failing findings without reddening `make check` | White (in-process child), but the *protocol* (child publishes oracle, parent kills, restart compares) is the contract suite's R31/R4/R9 skeleton with `$SUBLIMITE_BIN` as the child |

## 4. Gaps — regressions with no covering test

Counted basis: rows above whose "v1 test" cell says none/not found, plus
explicit worklog/HANDOFF statements.

1. **R32† Windows drag/snap/resize (`plat_quit`/`WM_CANCELMODE`)** — "Found
   by Tobias's use, not by any suite" (worklog 2026-11-01); only a later
   live-gate/source guard (edit-177r.33). Out of MVP scope for v2 (Windows
   second-class, PLAN/CLAUDE law 5) — record, do not contract.
2. **R33† minimap stall/blank on biggish files (edit-177r.34)** — never
   investigated; no repro, no test. Needs Tobias's repro before it can
   become a contract case (Law 1/8).
3. **R21 empty-tab minimap bands (cxho.1)** — fix landed with suite-level
   verification ("worker full check 110 passed") but no bead-tagged
   regression case was found by grep; first contract case to write, since
   the black-box form (§2) is cheap.
4. **R35† b6ai.14/.15/.16/.17/.18** — no bead-tagged tests found in this
   pass (greps per bead over `tests/*.c` returned no case symbols; the s30
   verification was full-`make check` + live use). b6ai.18 folds into R7's
   black-box case; .15/.16 into the geometry case; .14/.17 need new cases
   or an explicit manual-checklist disposition.
5. **X1 17-leaf piece collapse, X2 post-rename conflict** — no v1 test or
   symbol found at all. They enter the contract suite as unproven XFAILs:
   each needs a demonstrated red run on the v1 binary before it can mean
   anything on v2 (a never-red XFAIL is a silent pass, banned by PLAN §5
   fail-closed).
6. **Real-compositor Wayland everything** — clipboard (R34†), raster,
   fractional scale, activation raise (b6ai.6/.7/.8/.9): "Every Wayland
   item is mock-verified only: edit-b6ai.5 carries the real-Hyprland
   checklist" (HANDOFF s30). The gap is environmental, not editorial: no
   headless-compositor lane exists in v1 to copy.
7. **Live file-manager drag (cxho.6)** was closed on Tobias's manual
   confirmation (s27) — the XDND protocol half is tested, the end-to-end
   drop onto the real binary is not (see R26 black-box form).
8. **edit-cxho.18 find-in-files lifecycle** — the modal matrix's last
   XFAIL after cxho.17 (307 PASS / 1 XFAIL); still open at s24 HANDOFF.
   Becomes an XFAIL row in the R22 subset until fixed.
9. **Flakes with no repro**: edit-mvro (`dataloss_test` two_process_save
   `EDITOR_ERR_IO`, twice under load, 15 reruns passed, s27) and the s24
   `editor_test:2360` settle flake — not contractible until reproduced;
   listed so Wave 0 does not mistake them for coverage.

## Verdict

VERDICT: 31 session-16–27 regression rows (R1–R31) + 4 skimmed session-28–30 rows (R32†–R35†) + 4 PLAN expected-failure rows (X1–X4) inventoried; of R1–R31, 30 have a named v1 regression test and 1 (R21) only suite-level cover, but 0 are black-box — every pin is in-process (`editor_inject` / module link / `#include` of `private.h` or `main.c`; 63/120 test files include internals, counted by grep) — so the Wave-0 work is re-expression, not discovery: §2 gives a `$SUBLIMITE_BIN` form for every row (2 rows, R19/R25-mechanism, honestly reduce to observable shadows); reusable as-is are `design_shots.sh` (the inspector template), `mkcorpus`, `with_display_lock.sh` and the IPC-isolation/display-guard discipline, while `keyinject` needs a general driving mode and `journal_replay`/save-kill hook/Wayland mock are v1-internal oracles that must not become contract dependencies; 9 gaps (§4) have no test at all, of which X1/X2 must be proven red on v1 before they can serve as XFAILs.

