# sublimité v2 — Clean-Slate C11 Architecture & Re-Engineering Plan (Final — Post-Council Round 2)

> Forged through two independent rounds of the 3-member Architecture Council (**Claude Fable 5.2**, **GPT-6.2 Astra**, **Muse Wonder Full 1.0**). Council mean rose from **2.22/5 (all Rework)** in Round 1 to **3.83/5** in Round 2 (Fable & Muse: **Approve with Minor Revisions**, 0 Wave 0 blockers; Astra: 2 surgical Wave 0/§4.1 additions), and **every single Round 2 blocker and sharpening item is integrated below**.

---

## 1. Core Principles & Non-Negotiables

- **Quality *and* speed — no tradeoff thinking:** Data-oriented elegance in C11 eliminates both bugs and overhead simultaneously: zero redundant state copies, zero reconciliation glue, prepaid arenas, single measured GL 3.3 path, straight-line hot paths.
- **Clean-slate v2 alongside live v1:** Built on a clean branch/worktree while v1 `main` and `~/.local/bin/sublimite` remain untouched for daily use until v2 passes every cutover gate.
- **Hard runtime isolation for BOTH v2 and the Wave 0 v1 oracle:**
  - v2 uses a distinct app identity (`sublimite-v2`) across **every** path (`OS_APP_DATA`, `OS_APP_CONFIG`, `config/runtime.c`, and `ipc_posix.c` abstract + filesystem socket leaf names — audited by grep in Wave 0 so v2 can never read/write v1's `config.toml`, forward into v1, or retire live v1 hot-exit journals).
  - Wave 0 runs the **v1 baseline oracle itself** under disposable `XDG_DATA_HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`, and fixture files (`tests/ipc_isolation.h:56`) so v1 test launches never forward into Tobias's running daily editor.
- **Compiler & law discipline:** C11 (`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wswitch-enum`), zero GUI toolkit, Law 2 (zero `malloc` on input → mutation → layout → submit, enforced live on release builds), Law 4 (every numeric bound carries a `(P)` physics or `(M)` measured derivation — zero invented thresholds), AGPL-3.0-or-later.
- **Platform priority:** **Linux is strictly first-class** (X11 GL 3.3 + XShm CPU raster; Wayland EGL-only, honestly diagnosed if EGL/presentation-feedback is unavailable). **Windows is second-class:** preserve only zero-cost portability seams (`os.h` syscall boundary + platform-neutral `plat.h` types); drop Windows builds, renderers, and Windows-only format churn from v2 MVP.
- **Zero second-system creep:** v2 targets **v1 feature parity minus v1's architectural debt and fossils**. Speculative features (IME preedit, `wl_shm`, extensible user themes, T6 speculative resync, generic highlight predicates, eager open hashing) stay out of v2 MVP until a real user repro or trace opens a post-cutover bead (Law 1 / Law 8).

---

## 2. Verified 5-Layer Acyclic DAG (Per-Module Include Order + `--whole-archive` Link Checks)

Every layer links an isolated verification executable with `-Wl,--whole-archive libL<n>.a -Wl,--no-whole-archive` against **only lower layers** (so unselected objects cannot hide upward references). In addition, a **per-module include-DAG checker** enforces intra-layer partial orders so modules cannot cross-couple inside a layer (preventing regressions like v1's `x11/clip.c:5` ↔ `wayland/dispatch.c:4` mutual `private.h` includes):

```
L0  libcore.a      os (POSIX vm_reserve/commit, clock, file/dir/mmap, os_fault_service (fixed global) + generation-validated fault ids,
                       wake/poll, namespaced app_dir CONFIG+DATA),
                   base (arenas, slabs, live counting malloc guard, atomic/pthread_once cpu-detect),
                   trace (with ring release on pool teardown),
                   work (single-producer mailboxes + physical lease retirement),
                   ipc (POSIX Unix socket with namespaced leaf + abstract name)
                   [Internal order: os < base < trace < work; base < ipc — amended, docs/decisions/wave0-plan-amendments.md]
        │
L1  libtext.a      scan, utf8 (+ fix ©/® UCD_EXT_PICT), indent,
                   piece (harvested piece_snapshot + piece_checkpoint + boundary-privatization-safe
                          shared add-dir + L0 fault-id token hook + root grow/collapse regression test),
                   undo (burst coalescing, cap eviction, sliced replay, shared add-dir boundary),
                   lineidx (resumable query continuations + degraded off-path regrowth protocol),
                   find (unified streaming visit + native ASCII fold & whole-word in one literal engine)
                   [Internal order: scan/utf8 < piece < undo/lineidx/indent/find]
        │
L2  libpersist.a   file (NOREPLACE absent targets, unified L0 fault-registered reload mappings,
                         existing file_id with ctime_ns + ino_hi),
                   journal (checked arena allocations, versioned wire header, CRC + committed-group replay),
                   savectl (orthogonal 4-axis product-state machine + fd-captured BASE + conflict cancel),
                   edit (layout-free edit kernel harvested from view/edits.c: selection, piece_checkpoint
                         + undo reserve, journal staging preflight & emission — used directly by Wave 1!)
                   [Internal order: file < journal < savectl; journal < edit]
        │
L3  libgfx_plat.a  plat (src/plat/plat.h + plat_native.h + DPI helpers relocated from editor/runtime.h;
                         X11 + Wayland EGL-only; atomic inline PLAT_EV_KEY; PLAT_EV_SURFACE with DPI;
                         bounded owned plat_transfer_buf for clip/DnD; NO x11↔wayland private.h includes!),
                   render (20-byte render_cell + render_types.h + depth-1 T5/T6 + shared render_submit_prep
                           + Q8 fractional scroll parity; NO render_grid_repair),
                   raster (X11 XShm + harvested scalar/SSE2 raster_kernel.c),
                   gl (single measured GL 3.3-compatible direct-cell/TBO path),
                   font (stb hostile-input validator + GSUB + baked ASCII cold-start +
                         single live font_async owner with explicit T5-ordered pin tokens)
                   [Internal order: plat_types < x11 | wayland; render_types < render < raster | gl;
                    render_types < font; plat_native < raster | gl]
        │
L4  libui.a        viewport (persistent byte anchor + full certified visual-row descriptor +
                             64-bit hscroll + view motion/wrap affinity — NO mutation/edit ownership),
                   layout (opaque struct, unified clip+wrap emitter, incremental wrap_edit/relayout,
                           long-line checkpoints, local upward row-start cache, const views over prepaid storage),
                   tabs (buffer_id + viewport state; zero borrowed tree*/undo* pointers),
                   minimap (windowed-only + explicit bind generation, reading piece/lineidx directly),
                   find_session + find_panel + replace_txn (explicit FSM, os_temp_file),
                   fif (harvested 748-LOC demand-committed arena + O(1) hash inode dedup + shared find meter),
                   hl (opaque hl_session, budgeted hl_query_run, pinned query predicates, enum lang IDs),
                   cmd (actions.inc moved under cmd/; unified keys/menu/palette + sortedness check; no x11/plat.h),
                   config (harvested transactional TOML parser + private pool; settings glue rewritten on cmd/layout),
                   dnd (harvested verbatim)
                   [Internal order: cmd < config; viewport < layout < tabs; find_session < find_panel]
        │
L5  src/app/       app_loop (straight-line ordered app_step, single app_work_pending(), assert(!in_step),
                             zero sched_yield spins, physical work-lease retirement),
                   app_buffers, app_modal, app_status, app_save,
                   app_session (copies {path, slot, origin, view_state} into immutable leaves on UI;
                                workers never dereference live app_buffer*),
                   app_paint, app_panels, main.c
                   [Rule: L5 never includes x11/, wayland/, gl/, or raster/ internal headers.]
```

---

## 3. Complete Module Disposition

| Disposition | Modules | Key Notes |
|---|---|---|
| **Harvest verbatim** | `scan`, `indent`, `dnd`, `raster_kernel.c`, `work`, `vendor/` (13 tree-sitter grammars + UCD + `stb_truetype`) | Proven, bounded, fuzzed. Adapt only to `L0`/`L1` header paths. |
| **Harvest + targeted fix** | `os` (app-id namespace + generation-validated `os_fault` ids), `ipc` (app-id socket namespace), `base` (atomic/`pthread_once` CPU detect), `trace` (ring release on teardown), `utf8` (fix ©/® `UCD_EXT_PICT`), `piece` (write failing tests first for concurrent-snapshot checkpoint abort; keep the root grow/collapse probe as a regression test — the 17-leaf root collapse is not reproduced at 6fec2e8, docs/decisions/wave0-plan-amendments.md; fix staged-init cache invalidation, boundary-privatization-safe shared add-dir, L0 fault-pin hook), `undo` (preserve burst coalescing, cap eviction, sliced replay + shared add-dir boundary), `lineidx` (resumable continuations + off-path regrowth), `find` (promote `visit` + native ASCII fold & whole-word), `file` (`NOREPLACE` absent target, unified fault-pinned reload), `journal` (checked arena allocs + versioned header), `edit` (harvested from `view/edits.c` — only 5 layout refs vs 56 in `view.c` — plus journal preflight/staging), `tabs` (store `buffer_id`, drop `tree*`/`undo*`), `minimap` (windowed-only + explicit bind gen), `fif` (748-LOC arena + O(1) inode hash + shared find meter), `config` (harvest TOML parser + private pool; rewrite settings glue on `cmd`), `font` validator/GSUB/baked ASCII fast-start, `x11` (relocate DPI helpers out of `editor/runtime.h`, adapt to `src/plat/plat.h` + owned transfer buffers, cut `wayland/private.h` include) | Preserves hard-won v1 edge cases (selection semantics, EOL/encoding, symlinks/ACLs, stdin buffers, X11 selection exit, tree-sitter incremental edits, trace/replay). |
| **Rewrite cleanly** | `savectl` (orthogonal 4-axis FSM + fd-captured BASE), `viewport` + `layout` (collapse `view.c` + `scroll` + `scrollctl.c` + dual `layout.c`/`wrap.c`), `find_session`/`find_panel`/`replace_txn` (split `findui`, delete `findui/literal.c`), `hl` (opaque `hl_session`, budgeted `hl_query_run`), `cmd` (unify `keys`/`menu`/`palette`/`actions.inc`), `render` (shared `render_submit_prep` + Q8 scroll parity, delete `render_grid_repair`), `gl` (single measured GL 3.3 direct-cell/TBO path), `font_async` (sole runtime glyph owner; delete `font_cache` shelf, `font_runtime`, 2-slot fallback), `plat` (`src/plat/plat.h` + Wayland EGL-only with transfer deadlines, control retention, typed frame-discard outcome), `src/app/` + `main.c` | Eliminates all god structs, boolean soups, fossilized multi-generation variants, and cross-boundary leaks. |
| **Delete / do not carry over** | `variants/`, `scroll_visual`, `findui/literal.c`, `font_cache` (shelf), `font_runtime`, 2-slot font fallback, legacy GL instanced/env-var matrix, `render_grid_repair`, unindexed one-off `tools/`, Windows `mk/win.mk`/`win32/`/`d3d11/`/`dwrite_win.c` | Clean slate, zero dead colonies. |

---

## 4. Exact Specifications for the Hard Seams

### 4.1 L0 Fault-Epoch Ids & L1 Snapshot Budget
- **Fault-epoch ids (eliminates slot-reuse ABA):** `os_fault_service` (the one fixed global) issues `{slot, gen}` ids whose generations never repeat; `os_fault_poll` is lock-free and validates the generation on both sides of the epoch load, so a stale id gets NOENT and never reads another registration's epoch. No pins: every reader of mapped bytes already holds a backing reference, so the mapping's owner retires the id only at final backing release, invalidating the generation before reuse and draining admitted handlers (one shared semaphore). `L1` holds a mapping-hook reference through `piece_snapshot` plus the id — preserving `lineidx.h`'s token-only safety contract with zero upward includes into `L2 file`. Spec: docs/decisions/wave1-os-fault-lease.md (amended from pin/unpin, docs/decisions/wave0-plan-amendments.md A5).
- **Snapshot prepayment derivation (Law 2 & Law 4):** Size the buffer's prepaid COW node pool by counting distinct live revisions (active UI tree, session checkpoint leaf `saved_snapshot`, in-flight save `save_snapshot`, active background services — these have different lifetimes), their actual path/reserve classes, and the private ADD boundary prepayment (`piece.c:checkpoint_snapshot_storage`). v1's 1,920 B/edit with a live snapshot (`piece.h` MEMORY BOUND) is a `(G)` allowance that a deeper path exceeds, not a `(P)` bound to multiply (amended, docs/decisions/wave0-plan-amendments.md A4).
- **Checkpoint abort invariant:** O(1) shared add-chunk directories preserve v1's private boundary-chunk prepayment (`piece.c:441`) whenever a live snapshot can observe uncommitted checkpoint bytes — gated by an OOM + concurrent-reader test in Wave 1.

### 4.2 L2 `edit` Kernel, Journal Publish Point & `SIGKILL` Kill Oracle
- **Ownership:** `L2 edit` owns selection state, atomic forward replacement/insertion, `piece_checkpoint` + `undo` reservation, and journal-staging preflight & emission (harvested from `view/edits.c` + `editor/input.c:108,404`). Wave 1's headless editor uses this exact production path.
- **Staging preflight & frame fence:**
  1. Preflight journal staging capacity and `piece_checkpoint` + `undo` inverse reservation **before** mutating (`input.c:404`).
  2. Commit tree + selection + undo in memory and stage the journal group; retain group progress across turns (`journal.h:274`).
  3. **Fence frame submission and command acknowledgment** while staged journal operations remain (`editor.c:430`).
  4. Never roll back the tree after journal RAM acceptance (`journal.h:255`); on unrecoverable journal I/O failure, latch visibly suspended recovery status without rewinding or duplicating the edit.
- **`SIGKILL` oracle (`dataloss_test.c:436`):** Before command/frame acknowledgment, restart must recover either **pre-edit** or **whole-edit** (never torn); after acknowledgment, restart must recover **whole-edit**. Failed fsync/dirsync and suspended recovery carry distinct expected outcomes.

### 4.3 Non-Vetoing `lineidx` Degraded Mode & Off-Path Regrowth
- When an edit or undo exceeds `LINEIDX_EDIT_MAX_CHUNKS` (`512` chunks × `64 KiB` = `32 MiB` `(P)` incremental cap, `lineidx.h:37`) or total reserved index capacity (`lineidx.c:858`):
  1. `lineidx` transitions immediately to `LINEIDX_DEGRADED` and stops returning exact geometry from the stale table, falling back to v1's existing `{value, exact=false}` estimate contract (`lineidx.h:57`) for gutter, minimap, and byte-proportional scrollbar, and budgeted resumable seek for goto-line.
  2. An async rebuild runs on `WORK_BULK` against a post-edit `piece_snapshot`, **allocating a replacement table sized to post-edit file length off the typing path** (still zero typing-path `malloc`, Law 2!), and swaps it in only if `(doc_revision, fault_epoch)` still match.
  3. Foreground navigation makes bounded progress even while `WORK_BULK` is saturated (`editor_large_test.c:1119`), and growth beyond initial reserved capacity is explicitly tested.

### 4.4 Orthogonal 4-Axis Save & Hot-Exit State Machine (`L2 savectl`)
Model the four independent axes explicitly under `-Wswitch-enum`:
1. **`save_phase`:** `IDLE | PREPARING | WRITING | VERIFYING_FD | FINISHING | DIRSYNC_RETRY | CONFLICT_CANCEL`
2. **`external_state`:** `CLEAN | CHANGED_ON_DISK | RELOADING`
3. **`journal_lease`:** `UI_OWNED | SAVE_OWNED`
4. **`close_state`:** `OPEN | CLOSE_PENDING | RETIRED`

- **FD-captured BASE:** Capture `file_id` from the open temp/renamed fd written by `savectl`. Typing resumes as soon as `PREPARING` returns the journal lease (`savectl.h:99`). If post-rename pathname verification detects an external writer (`FILE_ERR_CHANGED`), transition strictly to `CONFLICT_CANCEL` (never `FINISHING`), keeping the retained original and checkpointing a self-contained snapshot. `FINISHING` updates every buffer sharing the path and preserves post-cutoff edits.

### 4.5 Complete Viewport Certificate & Opaque Incremental Layout (`L4`)
- **Persistent anchor + full certified visual-row descriptor** (retaining every field v1 proved necessary in `scroll.h:84` and `layout.h:258`):
  - `viewport_anchor`: `{ uint64_t byte; uint64_t hscroll_cols; uint32_t subrow_q8; bool visual_end; }` (keeping v1's pixel-Q8 scroll origin semantics cleanly distinct from row fractions).
  - `viewport_row_cert`: `{ uint64_t byte, end, next, line_start, col, end_column, indent, ordinal, exact_line; bool has_exact_line, newline, continuation, approximate; }`
- **Opaque `layout`:** One row-breaking emitter (`wrap_cols == 0` clip vs `> 0` wrap), retaining incremental `wrap_edit` / `wrap_relayout`, long-line checkpoints, and a small local upward row-start cache (never a global visual-row index). Exposes generation-stamped `const` views over prepaid cell/hit storage (zero per-keystroke copy). `minimap` reads `piece`/`lineidx` directly.

### 4.6 Atomic `PLAT_EV_KEY`, Wayland EGL-Only & Single GL 3.3 Path (`L3`)
- **Keep `PLAT_EV_KEY` atomic with inline UTF-8** (like v1 `plat.h:54` — zero `key_seq` focus-pairing hazard since IME preedit is out of MVP!). Add `PLAT_EV_SURFACE` (`{w, h, dpi/scale}`) and bounded owned `plat_transfer_buf` for clipboard & DnD (eliminating the Wayland unchecked `x11_plat*` cast and queued drop-overwrite race).
- **Wayland:** EGL-only for v2 MVP; bounded transfer deadlines + cancellation; terminal control-event retention under queue pressure; typed presentation outcomes (`PRESENTED | DISCARDED | LOST`) so discarded frames retire cleanly without calling `gl_fail()`. Commit protocol XMLs with `--check`.
- **Render & GL & Font:** Keep depth-1 T5/T6 + quiescent backend restart on lost feedback; unify `render_submit_prep` (page repack, unified frame caps, Q8 fractional scroll parity across GL and XShm raster). Benchmark the non-legacy GL upload modes (`SUBDATA` vs `ORPHAN` vs `PERSISTENT`) **subject to strict GL 3.3 compatibility** (no mandatory `ARB_buffer_storage`, `renderer.inc:848`) and keep the single winning direct-cell/TBO path. Keep baked ASCII atlases for cold-start speed + explicit high-DPI frame-capacity admission + `font_async` as sole runtime glyph owner with explicit T5-ordered pin tokens.

### 4.7 Straight-Line Non-Reentrant `src/app/` (`L5`)
- Straight-line ordered `app_step()` calling named cluster steps in fixed correctness order + one shared `app_work_pending()` / `app_next_deadline_ns()` derived from the same cluster states.
- `assert(!app->in_step)` non-reentrancy guard; zero `sched_yield` spins; large mmap opens, reloads, and IPC `file:line:col` jumps run as `BUF_OPENING` async states.
- Physical work-lease retirement (`work.h:185`) owns resource cleanup; `app_session` copies buffer metadata into immutable leaves on the UI thread (workers never touch live `app_buffer*`). Every multi-mode lifecycle uses an explicit `-Wswitch-enum` enum rather than coordinating booleans.

---

## 5. Build & Battery-Aware Tiered Verification

- **GNU Make** (`Makefile` + `mk/layers.mk` + `mk/grammars.mk` — single grammar table, zero duplication, isolated `--whole-archive` per-layer link checks).
- **Zero `#include "*.c"` in `tests/` and `fuzz/`** (script-enforced). Syscall/fault injection uses zero-runtime-cost `-Wl,--wrap=...` (`Makefile:156`).
- **Tiered check lanes:**
  - **Local / per-commit `make check`:**
    1. **ASan + UBSan lane** (explicit display-lane tagging, per-task timeouts, zero silent flake-forgiving retries, zero silent pass-on-missing-mock like `wayland_gl_test.c:82`).
    2. **Release Law-2 + G1 lane (`gcc -O2`):** Runs allocation-guard & G1 suites, **failing closed** if `edit_malloc_guard` reports inert!
    3. **Structural lints:** per-module include-DAG check, `#include "*.c"` == 0, isolated `--whole-archive` layer links, generator `--check` (UCD, keysyms, Wayland XMLs, font hashes), Wave 0 namespace isolation grep.
  - **Per-touched-module & wave-end / AC gates:**
    4. **`make check-tsan`:** TSan lane on threaded suites (`work`, `piece`, `journal`, `savectl`, `font_async`, `hl`, `find_session`, `app_session`), including explicit Wave 1 budget to verify/annotate pools and signal handlers cleanly.
    5. **`make fuzz-smoke`:** Bounded fuzz runs over committed minimal seed corpora.

---

## 6. Vertical-Slice Execution Waves (Runnable Editor by Wave 1)

### Wave 0 — Full Isolation, Verified DAG & Executable v1 Behavioral Contract
- Create clean `v2` branch/worktree; implement and **grep-audit** `sublimite-v2` namespace isolation across `OS_APP_DATA`, `OS_APP_CONFIG`, `config/runtime.c`, and `ipc_posix.c` (abstract + filesystem leaf).
- Stand up 5-layer GNU Make (`mk/layers.mk`, `mk/grammars.mk`), `--whole-archive` per-layer link checks, per-module include-DAG checker, `#include "*.c"` ban, and tiered `make check` skeleton.
- **Build & run the Executable v1 Behavioral Contract Suite** under a **disposable isolated v1 profile/IPC dir** (`XDG_DATA_HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`, `ipc_isolation.h:56`) using v1's existing tools (`keyinject`, `journal_replay`, `replay`, save-kill hook, Wayland clipboard fixture, plus a tiny headless cell inspector if needed):
  - Covers session 16–27 daily-use regressions (wrapped Return, Ctrl+S with Find Results open, over-capacity restore prioritizing unsaved tabs, PDF blank-glyph, occupied-bulk large navigation) **GREEN on v1**.
  - Records explicit expected-failure specs for known v1 bugs that v2 must pass (post-rename save conflict, `LINEIDX_EDIT_MAX_CHUNKS` >32 MiB delete+undo). Wayland clipboard copy is green on v1 since `73beeb4` (mock-verified; real check in Wave 2); the 17-leaf `piece` root collapse is not reproduced (docs/decisions/wave0-plan-amendments.md).

### Wave 1 — Walking Skeleton Vertical Slice #1 (Headless Crash-Safe Editor)
- **Order (amended A10):** `L0`/`L1` harvest beads start once the Wave 0 scaffold blockers are closed; `L2`, minimal `L5` and the Wave 1 exit wait for the Wave 0 gate (docs/decisions/wave0-plan-amendments.md A10).
- Land `L0 libcore.a` (including generation-validated `os_fault` ids & namespaced `ipc`), `L1 libtext.a` (`scan`, `utf8`, `indent`, `piece`, `undo`, `lineidx`), `L2 libpersist.a` (`file`, `journal`, orthogonal `savectl`, and the real **`edit`** kernel), and minimal non-reentrant `L5 src/app/` (`app_loop`, `app_buffers`, `app_save`, `app_session`).
- **Wave 1 Exit Gate (real running headless editor!):**
  - Open file → edit via production `L2 edit` under **live release malloc guard** (`active && count == 0`) → save while typing continues → post-rename foreign-file conflict test → `SIGKILL` at every save step + failed fsync/dirsync → restart and verify exact pre-ack/post-ack recovery oracle.
  - Failing-then-passing regression tests for concurrent-snapshot checkpoint abort under OOM, and >32 MiB delete+undo with `lineidx` degraded off-path regrowth past reserved capacity.
  - `make check-tsan` green on `work`, `piece`, `journal`, `savectl`, `app_session`.

### Wave 2 — Walking Skeleton Vertical Slice #2 (10 GiB Wrapped GUI + Real Presentation)
- Land `L3 libgfx_plat.a` (`src/plat/plat.h` X11 + Wayland EGL-only + relocated DPI helpers, `render` + shared `render_submit_prep`, `raster` XShm, single measured GL 3.3-compatible direct-cell/TBO path, `font_async` keeping baked ASCII cold-start) and core `L4 libui.a` (`viewport`, unified opaque `layout` with incremental wrap relayout & checkpoints, `tabs`, `minimap`, `app_paint`).
- **Wave 2 Exit Gate (usable GUI editor!):**
  - Open a 10 GiB lazy-mapped file in wrapped mode → cold seek + wrapped Return + typing under G1 & live release malloc guard + occupied-bulk navigation (`editor_large_test:1119` equivalent).
  - Real X11 (GL + XShm raster) and Wayland (EGL) rendering + Wayland clipboard copy/paste + Wayland frame-discard resilience.

### Wave 3 — Parallel Feature Verticals (Against Proven Wave 1–2 Contracts)
- Fan out against stabilized Wave 1–2 headers (respecting intra-L4 order `cmd < config`, `find_session < find_panel`):
  - Unified `find` + `find_session` / `find_panel` / `replace_txn`
  - `fif` (harvested + O(1) hash dedup + shared meter)
  - Opaque `hl_session` (budgeted `hl_query_run` + pinned predicates)
  - `cmd` (`keys`/`menu`/`palette`/`actions.inc`), `config` (parser harvest + settings glue), `dnd`, `app_modal`, `app_status`, full session restore/recovery UI.

### Wave 4 — Full Parity, Torture, Migration Drill & Cutover
- Run **100% of the Wave 0 Behavioral Contract Suite** + `modal_matrix_test` + `dataloss_test` + wrapped randomized torture + `make check-tsan` + `make fuzz-smoke` + `tools/design_shots.sh` visual parity check.
- A/B measurements vs v1 (`(M)` stamped with power/load): G1 p50/p99, RSS, binary size, cold start, `make check` duration.
- **One-way v1→v2 journal import & rollback drill** on a copy of Tobias's live profile (verifying unsaved tabs survive and v1 backup stays untouched).
- Switch `~/.local/bin/sublimite` to v2 only on Tobias's explicit sign-off after live trial use.
