# Implementation plan — editor v1 (Linux)

Status: v1, 2026-10-08. Owner: Tobias. Consultant: Fable (main session). Companion documents: `PRD.md` (what), `perf/01-perf-target.md` (gates, binding), `CLAUDE.md` (rules for every agent), `HANDOFF.md`.

Perf measurements (battery rerun, GPU wake probe, photodiode) are **deferred** by decision on 2026-10-08. The plan below builds against the v3.4 gates as written; the instrumentation spine makes every module measurable the day the measurements are wanted.

## 1. Workflow

### 1.1 Roles

| Role | Model | Lifetime | Does |
|---|---|---|---|
| Consultant | Fable, main session | always | discusses with Tobias, writes/updates this plan, adjudicates design disputes, never codes and never spawns Fable subagents |
| Coordinator | Opus subagent | one per session | reads `bd ready`, picks the next bead, spawns one worker per bead, checks the result against the bead's acceptance criteria, closes or reopens, raises new beads for problems found, writes `docs/worklog/` entries |
| Sub-coordinator | Opus subagent | per epic when the coordinator judges it worthwhile | same as coordinator for one epic (e.g. the renderer, the piece tree best-of) |
| Worker | Haiku (default) or Sonnet | **one bead, then terminated** | implements exactly the bead, runs its tests and benchmark, updates the bead with results, closes it |
| Codex worker | `codex exec -m gpt-6.1-sol -c model_reasoning_effort=high` (xhigh for synthesis) `--approve-for-me --skip-git-repo-check -C <dir> -o <report.md> "<brief>"` | one bead, like any worker | **Opus-grade** (Tobias, 2026-10-08 evening). Takes any bead an Opus or Sonnet worker would: synthesis, mode:best implementations, kernel designs, hard debugging. Preferred over Opus/Sonnet while Codex quota is behind pace. `gpt-5.6-luna` is below Haiku grade: never for code. Same brief rules: one bead, no `bd`/`git`/Makefile, red-green report, STATUS.md on budget. |
| Synthesiser | Opus or Codex sol xhigh | one per best-of bead | reads 2–3 independent implementations, writes the best-of implementation, documents what was taken from where |
| Quick reviewer | Sonnet | one per review | bug hunt on a diff; findings become beads |
| Deep reviewer | `codex exec -m gpt-6.1-sol -c model_reasoning_effort=xhigh -s read-only --skip-git-repo-check -o <out.md>` | one per review | relentless review of a module; findings become beads |
| Test driver | `codex exec -m gpt-6-astra` with computer use | after the MVP lands | drives the built editor, files every edge case, crash, mis-render as a bead |

### 1.2 Token pacing

A standard bead is sized for **about 100K tokens of a Haiku worker**: roughly 150–400 lines of C plus its tests, in files the worker can read in full. A worker that reaches 150K tokens stops, writes what it has and what is missing into the bead, and the coordinator splits the bead. Beads tagged `model:sonnet` may go to 200K.

### 1.3 Three implementation modes

Every bead carries a `mode:` label.

- **`mode:std`** — one Haiku (or Sonnet) worker, write, test, close. Review: Sonnet bug hunt on the diff before close for anything that touches the typing path; otherwise batched at epic end.
- **`mode:par`** — a performance decision point. The coordinator spawns N workers at once, one per candidate design listed in the bead, each in its own directory `variants/<bead>/<name>/`. Each variant ships the **same** benchmark interface (`bench/<module>.c` calls a fixed API). The coordinator runs all variants on the laptop, tabulates p50/p99 and binary size, picks the Pareto front, Tobias (via the consultant) picks the point if more than one survives; the loser directories are deleted, the winner moves to `src/`. The decision and the table go into the bead and `docs/decisions/<bead>.md`.
- **`mode:best`** — correctness-critical code. 2–3 Sonnet workers implement independently from the same spec into `variants/<bead>/<n>/`; none sees the others. An Opus synthesiser then reads all, writes `src/<module>` taking the best structure, the union of the test cases, and the union of edge cases handled; it records the comparison in `docs/decisions/<bead>.md`. Then a Codex deep review before close.

### 1.4 Review and test policy

- Every module ships with: unit tests (`tests/<module>_test.c`), a fuzzer entry point where it parses or mutates (`fuzz/<module>_fuzz.c`, libFuzzer via clang), and a benchmark against its gate (`bench/<module>_bench.c`, prints p50/p99 and the gate, exits non-zero on miss).
- `make check` = build with `-fsanitize=address,undefined` + run tests; must pass before a bead closes. `make bench` = release build + run benches; run once per module on a quiet [AC] box when it lands, re-run only by beads that touch the hot path or when a row was within 20 % of its gate (CLAUDE.md law 1 bench policy, 2026-10-09).
- Codex deep review is mandatory at the end of each epic and for every `mode:best` module; the prompt is "relentlessly find all problems: memory safety, data races, gate regressions, spec deviations, missing tests; severity BLOCKER/MAJOR/MINOR; concrete fix for each". Output goes to `docs/reviews/<epic>-<n>.md`; each BLOCKER/MAJOR becomes a bead before the epic closes.
- No `malloc` on the typing path (input → mutation → layout → submit). Enforced by a test that installs a counting allocator hook and types 10 000 keys.

### 1.5 Bead conventions

- One epic per phase section below (P0 … P6). Beads are children of epics; `bd dep` records the arrows written in the "deps" column.
- Labels: `mode:std|par|best`, `model:haiku|sonnet`, `review:sonnet|codex|none`, `gate:G1` etc. where a gate applies.
- Bead description = the scope line plus the acceptance list below, verbatim. Workers do not read this plan; the bead must be self-contained.
- The coordinator closes a bead only after reading the test output pasted into the bead (and the bench output when the bead's module landed or its hot path changed).

## 2. Repository layout and build

```
edit/
  Makefile            single GNU make file, C11, -O2 -g, static link of our code; targets: all check bench fuzz clean
  src/                one directory per module (below), one public header each
  vendor/             tree-sitter runtime + grammars (P6), stb_truetype or freetype glue, DejaVu Sans Mono TTF
  tests/ bench/ fuzz/ one file per module
  tools/              trace dump, atlas baker, evdev tracer, variant runner
  variants/           transient; mode:par and mode:best work in progress, deleted after decision
  docs/decisions/ docs/reviews/ docs/worklog/
```

Compiler: gcc 13 (release), clang 18 (sanitizers, fuzzing). No CMake, no meson: the Makefile is small enough for Haiku to read whole.

Dependencies on A: libxcb 1.15, xkbcommon 1.6, EGL 1.5 / GL, wayland-client 1.22, freetype 26.1 (atlas baking only, not linked into the editor). **Missing on this machine and needed by P2:** `libxcb-xkb-dev`, `libxkbcommon-x11-dev`, `libxcb-present-dev`; tree-sitter is vendored, not a package.

Working command name: `edit` (PRD §11 open question; rename is a one-line change in the Makefile). Embedded font: DejaVu Sans Mono (on this machine, licence permits embedding; swap later if JetBrains Mono is wanted).

## 3. Module map

| Module | Header | Owns | Mode | Model |
|---|---|---|---|---|
| `base` | `base.h` | fixed-width types, arena, pool, assert, likely/unlikely, SIMD dispatch macro | std | haiku |
| `trace` | `trace.h` | T0–T6 timestamp ring (perf §4), lock-free single-writer per thread, dump tool | std | haiku |
| `utf8` | `utf8.h` | decode with invalid-byte preservation, cell width (CJK wide, combining zero, emoji 2), grapheme step | best | sonnet×3 |
| `scan` | `scan.h` | SSE2 newline/ASCII counting, memchr-style needle filter, popcount | par | haiku×3 |
| `piece` | `piece.h` | piece tree over original + chunked add buffer, byte+newline counts, snapshots, iterators | best | sonnet×3 |
| `undo` | `undo.h` | inverse-op undo log, burst grouping, bounded memory (G10f) | best | sonnet×2 |
| `lineidx` | `lineidx.h` | sparse 64 KiB chunk index, background build, exact/estimated line↔byte | std | haiku |
| `file` | `file.h` | open (copy < 256 MiB / mmap above), bounded-prefix first view, change detection (perf §2.14), atomic save worker | std | sonnet |
| `journal` | `journal.h` | hot-exit append-only journal + replay | best | sonnet×2 |
| `work` | `work.h` | worker thread pool, cancellable jobs with generation counters (G6c), UI-thread mailbox | std | sonnet |
| `font` | `font.h` | pre-baked ASCII atlas (tool), runtime glyph rasteriser for non-ASCII (stb_truetype), atlas page allocator | std | haiku |
| `gl` | `gl.h` | dlopen GL on a worker, EGL/GLX context, glyph-quad instanced renderer, fence timing | par | haiku×2 (EGL vs GLX; persistent-mapped vs orphaned VBO) |
| `raster` | `raster.h` | SSE2 CPU raster of glyph cells, MT4 strips, XShm upload (warm fallback) | std | sonnet |
| `x11` | `plat.h` | xcb window, xkbcommon-x11 input, Present extension timing, clipboard, event loop | std | sonnet |
| `wayland` | `plat.h` | same interface over wayland-client (P6) | std | sonnet |
| `layout` | `layout.h` | viewport → cell grid: lines, wrap, tabs, combining marks, line numbers, dirty-strip tracking | std | sonnet |
| `view` | `view.h` | cursor, selection, scroll state, per-tab view state, Sublime key table | std | haiku (several beads) |
| `editor` | `editor.h` | the UI-thread loop: input → mutation → layout → submit, ≤ 0.5 ms slices | std | sonnet |
| `find` | `find.h` | incremental search worker, regex-lite, bounded publish, verifier with Two-Way fallback | par+best | sonnet |
| `tabs` | `tabs.h` | tab strip, MRU, reopen closed | std | haiku |
| `minimap` | `minimap.h` | downscaled region from index, viewport indicator, drag | std | haiku |
| `ipc` | `ipc.h` | single instance over Unix socket, CLI parsing, `--wait`, stdin | std | haiku |
| `config` | `config.h` | TOML subset, hot reload, keymap overrides, themes | std | haiku |
| `hl` | `hl.h` | tree-sitter worker, viewport-first, per-frame budget | std | sonnet |

## 4. Phases and beads

Each bead: **id · title** — scope. *Accept:* what must be true to close. *Deps.* Mode/model in brackets when not `std/haiku`.

### P0 Bootstrap (epic)

- **P0.1 · Toolchain and skeleton** — `git init`, `.gitignore`, Makefile with `all check bench fuzz clean`, `src/base`, a `hello` test, CI script `tools/ci.sh` that runs check+bench. Install the three missing dev packages (list in §2); record versions in `docs/toolchain.md`. *Accept:* `make check` green with ASan/UBSan under clang and release under gcc.
- **P0.2 · base** — arena (bump, reset), fixed pool (free list), `ASSERT`, `TRACE_SCOPE`, SIMD dispatch (`cpu_has_avx2()`), counting-allocator hook for the no-malloc test. *Accept:* tests; no libc malloc after `base_init` in a 10 000-op pool test. *Deps:* P0.1.
- **P0.3 · trace** — per-thread ring of (event id, frame id, CLOCK_MONOTONIC ns), 64 K entries, zero allocation after init; `tools/tracedump` prints p50/p99 per gate mapping (G1 = T4−T0 etc.). *Accept:* bench shows < 20 ns per record; dump reproduces a synthetic distribution. *Deps:* P0.2.
- **P0.4 · Bench harness** — `bench/harness.h`: sample ring, nearest-rank p50/p99, CI bootstrap, gate compare, machine-readable output line, battery-status stamp from `/sys/class/power_supply/BAT0/status`. *Accept:* self-test. *Deps:* P0.2.
- **P0.5 · Corpus generator** — `tools/mkcorpus` writes the perf §4 corpus files into `/tmp/edit-corpus` (ASCII code, Unicode, malformed UTF-8, dense short lines, 1 GB and 10 GB single-line, periodic patterns). *Accept:* files produced with sizes checked. *Deps:* P0.1.

### P1 Core, no GUI (epic) — all pure C, headless, fuzzable

- **P1.1 · utf8** [best, sonnet×3] — decoder returning (codepoint | invalid byte, length), forward/backward step, cell width per PRD §6.2, grapheme boundary for combining marks. *Accept:* tests from the Unicode width tables for the BMP + emoji; fuzz 10 min clean; 1 GB decode bench ≥ 2 GB/s. *Deps:* P0.2.
- **P1.2 · scan** [par, haiku×3: plain SSE2 / SSE2+popcount / AVX2 dispatch] — count newlines and non-ASCII bytes in a range; find first newline; needle first-byte filter. *Accept:* each variant ≥ 12 GB/s on 1 GB cached; Pareto pick on (throughput, code size). *Deps:* P0.4.
- **P1.3 · piece tree spec** [sonnet] — `piece.h` API frozen before the best-of: insert/delete by byte offset, read range, byte↔line via node counts, snapshot (O(1), refcounted original + add chunks), iterator; node layout target from perf §3 (B+ SoA 384 B nodes); memory bound G10f. Also the test suite and fuzzer that all variants must pass (random edit script vs a reference `char*` model). *Accept:* header + tests compile against a stub. *Deps:* P0.2, P1.1.
- **P1.4 · piece tree impl** [best, sonnet×3 → opus] — three independent implementations of P1.3. *Accept:* each passes the P1.3 suite + 30 min fuzz; synthesiser's `src/piece` passes everything and meets: 10⁵ random edits in a 1 GB mmap in < 1 s, snapshot < 1 µs; Codex deep review done. *Deps:* P1.3.
- **P1.5 · undo** [best, sonnet×2 → opus] — inverse-op log over piece ops, burst grouping (≤ 300 ms between keys, same kind), redo stack, memory ≤ 64 B/undo + typed bytes (G10f); deleted-original bytes copied at delete time (HANDOFF §3.7). *Accept:* 10 k-step undo bench ≤ 63 ms p50 (G9); fuzz: random edit/undo/redo vs model. *Deps:* P1.4.
- **P1.6 · lineidx** — sparse index: 16 B per 64 KiB chunk (newline count, non-ASCII flag), built by a cancellable worker job in 64 KiB slices, exact line↔byte when complete, estimate (byte proportion) before; invalidated per chunk on edit. *Accept:* 1 GB build ≤ 80 ms p50 warm (G7, AC-provisional); jump to line 10⁷ via partial index ≤ 30 ms. *Deps:* P1.2, P1.4, P1.8.
- **P1.7 · file** [sonnet] — open: stat, bounded-prefix read (first 1 MiB) published immediately, then copy (< min(256 MiB, RAM/32)) or mmap + worker prefetch; LF/CRLF detection; change detection by (mtime, size, inode) + optional inotify; policy of perf §2.14 (detect → cancel jobs → surface choice). Save: snapshot → worker writes temp, fsync, rename, fsync dir; ack event immediately (G8s), done event on completion. *Accept:* G5 bench ≤ 6 ms to first viewport on the 10 GB file; G8s ≤ 2 ms; durability test with a kill between steps. *Deps:* P1.4, P1.8.
- **P1.8 · work** [sonnet] — fixed pool (1 bulk + N raster workers), job = (fn, ctx, generation); cancel = bump generation, worker checks every ≤ 5 ms of CPU (G6c); results to UI via SPSC mailbox + eventfd for the X loop. *Accept:* G6c bench: cancel ack ≤ 1 ms p50 / 5 ms p99 under a running 1 GB scan; TSan clean. *Deps:* P0.2.
- **P1.9 · journal** [best, sonnet×2 → opus] — hot-exit: append-only records (buffer id, piece op, view state, tab set), 4 KiB-aligned writes, `fdatasync` every 1 s or 64 KiB, replay with CRC per record, truncate-at-corruption. *Accept:* kill -9 at random points in a 10⁵-edit script then replay ≡ model, 1 000 trials; write cost ≤ 20 µs per edit on the UI thread (the sync is on a worker). *Deps:* P1.4, P1.8.
- **P1.10 · find core** [par, sonnet×2: SIMD filter+verify / Two-Way only; then best-of] — literal search with bounded output (count + first 4096 offsets), `a`×31+`b` adversarial case, cancellable. Regex-lite (ERE subset: classes, `*+?`, alternation, anchors, groups for replace) compiled to a byte-class NFA with a literal-prefix filter. *Accept:* G6 1 GB ≤ 80 ms p50; G6v ≤ 160 ms; regex tests. *Deps:* P1.2, P1.8.
- **P1.R · Codex deep review of P1** — one review per `best` module already done; this one covers scan/lineidx/file/work/find together. *Accept:* every BLOCKER/MAJOR is a bead. *Deps:* all P1.

### P2 Platform and rendering (epic)

- **P2.1 · x11 window** [sonnet] — xcb connect, window with `_NET_WM` hints, ARGB visual, map, Expose/Configure/Focus, event loop over `poll` on {xcb fd, eventfd, timerfd}; no toolkit. *Accept:* G4a bench: exec → map requested ≤ 10 ms of our own time (WM cost excluded, HANDOFF §3.3); idle wakeups 0/s with blink off (G11 test). *Deps:* P0.3, P1.8.
- **P2.2 · x11 input** [sonnet] — xkbcommon-x11 keymap, key → (keysym, utf8, mods), repeat handled by us (X autorepeat off), mouse buttons/motion/wheel incl. smooth-scroll valuators via XInput2, clipboard (PRIMARY + CLIPBOARD, UTF8_STRING) via selections. *Accept:* key table test with a replayed event log; T0 ingestion from X timestamp + optional `tools/evtrace` (evdev tracer, `EVIOCSCLOCKID`). *Deps:* P2.1.
- **P2.3 · font tooling** — `tools/atlasbake`: DejaVu Sans Mono at the sizes in config → ASCII atlas PNG-less raw + metrics header generated at build time (≤ 64 KB); runtime stb_truetype rasteriser for non-ASCII into an atlas page allocator (shelf packing, 1024² pages). *Accept:* baked atlas committed as `.h`; runtime glyph ≤ 50 µs each; fallback font discovery via fontconfig on a worker, never on the UI thread. *Deps:* P0.2.
- **P2.4 · gl renderer** [par, haiku×2: EGL vs GLX; sub-variants persistent-mapped VBO vs orphaning] — dlopen `libGL`/`libEGL` on a worker during startup, GL 3.3 core, one instanced quad per cell (pos, glyph uv, fg, bg) from a per-frame cell buffer, scissor to dirty strips, `glFenceSync` for T5, Present extension `PresentCompleteNotify` for T6. *Accept:* G3 bench: full 2880×1800 frame ≤ 5.0 ms p50 warm; G3z: 10 000-frame scroll 0 misses; after-idle number recorded (not gated yet). Pareto on (G3 p99, G3i, startup cost). *Deps:* P2.1, P2.3, P0.3.
- **P2.5 · cpu raster** [sonnet] — SSE2 cell blit from the atlas, MT4 horizontal strips on the raster pool, XShm `PutImage` with `xcb_sync` fence for T5; selected when GL init fails or by `--cpu`. *Accept:* ≤ 5.56 ms p99 warm full frame (G3); same cell-buffer input as P2.4 (one `render.h` interface). *Deps:* P2.3, P1.8, P2.1.
- **P2.6 · reference window** — `tools/refwin`: bare xcb window that draws one glyph through the same present path, for the G2c paired difference; plus `tools/keyinject` (XTest, phase-locked to Present MSC). *Accept:* runs; emits per-pair timestamps. *Deps:* P2.4.
- **P2.R · Codex deep review of P2.** *Deps:* all P2.

### P3 Glass = M0 (epic) — typing in one file

- **P3.1 · layout** [sonnet] — viewport (first line byte, cols, rows) → cell grid; tab expansion, combining marks merged into cells, wide cells, line numbers gutter, no wrap yet; dirty-strip tracking by line; ≤ 500 shaped chars per 0.5 ms slice (perf §3). *Accept:* layout of a 300-row viewport ≤ 150 µs; tests on the corpus. *Deps:* P1.1, P1.4.
- **P3.2 · view: cursor + selection** — cursor as (byte offset, preferred col), movement by char/word/line/page/document per Sublime keys, selection anchor, shift-extend. *Accept:* tests with a scripted key log vs expected text/cursor. *Deps:* P3.1.
- **P3.3 · editor loop** [sonnet] — the UI thread: poll → drain input (T1) → apply mutation via piece+undo (T2) → layout dirty strips → render submit (T4) → journal; render-on-input, queue depth 1; 0.5 ms slice budget with input checks. *Accept:* G1 bench ≤ 1.0 / 2 ms with 10 000 injected keys into the 1 GB file; no-malloc test on the typing path; G11 idle test. *Deps:* P3.2, P2.2, P2.4, P1.5, P1.9.
- **P3.4 · scrolling** — wheel/touchpad pixel-smooth scroll, page keys, byte-offset scroll before the index lands, cursor follow. *Accept:* G3z 0 misses in 10 000 refreshes; G7j jump to line 10⁷ ≤ 30 ms on the 1 GB file. *Deps:* P3.3, P1.6.
- **P3.5 · M0 bench run** — run every gate bench that exists on the laptop, record AC/battery status, write `docs/bench/m0.md`. *Accept:* G1, G3, G4a, G11 green at least on AC; misses become beads. *Deps:* P3.4.
- **P3.R · Codex deep review of P3** + Sonnet quick hunts on the editor loop. *Deps:* P3.5.

### P4 Editor = M1 (epic)

- **P4.1 · word wrap** [sonnet] — wrap at window width, indent-aware continuation, visual-line navigation, wrap on by file type. *Accept:* layout bench ≤ 300 µs wrapped; cursor tests on wrapped lines. *Deps:* P3.1, P3.2.
- **P4.2 · auto-indent, bracket match, whitespace, tabs/spaces** — keep-previous-indent on Enter, dedent on `}`, matching-bracket highlight within the viewport, trailing-whitespace display, tab-vs-space detection on open. *Accept:* tests. *Deps:* P3.3.
- **P4.3 · Sublime key table** — complete default keymap as data (`keys.c`): editing, selection, line ops (move/duplicate/join/delete), case, comment toggle (per grammar later), goto line. *Accept:* every binding has a test line. *Deps:* P3.2.
- **P4.4 · tabs** — tab strip, open/close/reorder-by-drag, Ctrl+Tab MRU, Ctrl+W, Ctrl+Shift+T, modified marker; tab switch = redraw only. *Accept:* G3 holds on tab switch with 100 tabs; 100-tab memory within G10. *Deps:* P3.3.
- **P4.5 · minimap** — right-hand strip rendered from the line index (density per chunk, not glyphs) for large files and from cells for small; viewport indicator; click/drag. *Accept:* included in the G3 frame budget; stale-minimap check in the bench. *Deps:* P4.4, P1.6.
- **P4.6 · find/replace UI** — bottom panel, incremental highlight of all visible matches as you type, regex/case/word toggles, wrap-around, replace one/all as one undo group; results streamed from the P1.10 worker. *Accept:* UI thread never scans (trace shows no scan on UI thread); G6c cancel on each keystroke. *Deps:* P1.10, P3.3.
- **P4.7 · save + external change** — Ctrl+S to the P1.7 worker, "saving"/"saved" status, banner for external change (reload/keep), silent reload when unmodified keeping scroll/cursor. *Accept:* G8s/G8d bench; race test from perf §2.14. *Deps:* P1.7, P4.4.
- **P4.8 · hot exit** — journal integration: every edit/view change journaled, restore on start (tabs, scroll, cursor, selection, window size), crash test. *Accept:* kill -9 during typing then restart ≡ state, 100 trials. *Deps:* P1.9, P4.4.
- **P4.9 · CLI + single instance + xdg** — `edit path[:line[:col]]...`, `-`, `--wait`, Unix socket hand-off to the running instance, `.desktop` + MIME registration script. *Accept:* second invocation opens a tab in ≤ 10 ms and exits; `--wait` blocks until the tab closes. *Deps:* P4.4.
- **P4.10 · large-file path end to end** — open the 10 GB single-line file and a 1 GB log: first viewport ≤ 6 ms, estimated line numbers, index arrival swaps to exact, find and save on 1 GB with the foreground gates holding (perf §2.15 concurrency). *Accept:* G5, G7, G6 benches with one and three workers active. *Deps:* P4.5, P4.6, P4.7.
- **P4.11 · Unicode rendering completeness** — combining marks, CJK, emoji monochrome, invalid-byte placeholders, through atlas fallback pages and fontconfig fallback. *Accept:* corpus Unicode file renders with no tofu for the fallback-covered set; screenshot diff test. *Deps:* P2.3, P3.1.
- **P4.12 · M1 bench run** — all enforced A gates; `docs/bench/m1.md`. *Deps:* all P4.
- **P4.R · Codex deep review of P4.** *Deps:* P4.12.

### P5 MVP test drive (epic)

- **P5.1 · Astra test drive** — `codex exec -m gpt-6-astra` with computer use drives the built `edit` through a script: open/edit/save every corpus file, every key in the table, every mouse interaction, resize, external modification, kill and restart, 100 tabs. Instruction: find all edge cases, crashes, mis-renders, latency stutters; report each as a bead with a repro. *Accept:* report filed; every finding is a bead with a label `src:astra`. *Deps:* P4.12.
- **P5.2 · Fix wave** — beads from P5.1 worked in priority order by Haiku/Sonnet, each closed with a regression test. *Accept:* no open BLOCKER from P5.1. *Deps:* P5.1.
- **P5.3 · Daily-use start** — Tobias uses it for a day; findings become beads. Marks the end of M1.

### P6 Daily = M2 (epic)

- **P6.1 · wayland backend** [sonnet] — wl_compositor/xdg_shell/wl_seat/wl_keyboard (xkbcommon)/wl_pointer, EGL via wayland-egl, frame callbacks for T5/T6 (`wp_presentation`), clipboard via wl_data_device. *Accept:* same `plat.h` interface; G1/G3 benches on Wayland recorded. *Deps:* P2.4, P3.3.
- **P6.2 · tree-sitter runtime vendored** — vendor `lib/src/lib.c`, build static; worker-side parse with a 1-frame budget per slice, viewport-first ranges, incremental edits via `ts_tree_edit`. *Accept:* G1/G3 unaffected with highlighting on (bench with hl worker busy); plain text shown until the viewport parse lands. *Deps:* P1.8, P3.3.
- **P6.3 · grammars + queries** — C, Rust, Python, Julia, JS/TS, JSON, TOML, YAML, Markdown, LaTeX, shell; highlight queries → theme scopes. One bead per 2–3 grammars. *Accept:* each grammar's `highlights.scm` mapped; binary ≤ 2 MB total. *Deps:* P6.2.
- **P6.4 · config + themes** — TOML subset parser, `$XDG_CONFIG_HOME/edit/config.toml`, hot reload via inotify on a worker, keymap overrides, dark + light themes, font size. *Accept:* tests; reload ≤ one frame. *Deps:* P4.3.
- **P6.5 · polish wave** — cursor blink stop after 10 s, unfocused zero wakeups (G11), window title, modified markers, status line. *Deps:* P6.4.
- **P6.6 · M2 bench run + Astra second drive.** *Deps:* all P6.
- **P6.R · Codex deep review of P6.**
- **Trial** — two weeks of exclusive use; done = Sublime uninstalled (PRD §5).

### Deferred (not planned here)
Battery rerun / GPU wake probe / photodiode rig (HANDOFF §4); Windows (M3); neural highlighting spike (R1); multi-cursor; LSP.

## 5. Ordering and parallelism

**Velocity rules (Tobias, 2026-10-08: "I want velocity").** Measured 2026-10-08: 15 beads in 27 min at 11 workers, ≈ 73K tokens per bead. The critical path (bench matrix → kernel variants → Pareto/synthesis → review → undo → editor loop → M0 bench; then tabs → save → hot exit → Astra fixes) is ≈ 10–12 h of the ≈ 25–30 h estimate; width beyond the graph's ≈ 12 concurrent beads buys nothing. To approach the floor:
1. **Reviews never gate.** Codex reviews are read-only: start the next epic the moment code lands; findings arrive as beads.
2. **Pipeline against frozen interfaces.** Layout, view, GL variants, X11 input, CPU raster build against `piece.h` and the cell-grid interface, not the winning kernel; run them during the kernel competition. Freeze `render.h` and `plat.h` before fan-out, as `piece.h` was.
3. **Builds wide, benches quiet.** Any number of concurrent compiles/tests; gate benches queue on an idle machine (concurrent builds halved a scan number today) and stamp power status.
4. **Coordinator owns `bd` and `git`** (single-writer tracker); workers never wait on it. Spawn up to 12 workers at once.
5. **Width costs tokens linearly; only widen competitions where the Pareto question is open** (kernel, GL present path, after-idle experiments). Elsewhere one worker.
6. **Pace ceiling (Tobias, 2026-10-08 evening).** Claude usage may run at most **2 % ahead of uniform pace** on every window the `quota` CLI reports (5-hour session, Weekly, Fable weekly; PACE column, `+X% ahead`). The coordinator runs `quota` before every dispatch wave and records the line in the worklog. If any Claude window is more than +2 % ahead, no new Claude workers (Haiku, Sonnet, Opus) are spawned; beads go to Codex sol until the delta is back under. Pace is defined in `~/Projects/quota-app/README.md` ("How pacing works": `u* = elapsed / L`, delta = used − u*).
7. **Codex sol is a worker pool, not only a reviewer.** While Codex Weekly is behind pace, route Opus-grade beads (synthesis, mode:best, kernel designs, hard debugging) to `gpt-6.1-sol` high/xhigh via `codex exec --approve-for-me`; keep Claude quota for the coordinator and Haiku standard beads. When Codex reaches pace, balance the two by their pace deltas.


Critical path: P0 → P1.3 → P1.4 (best-of, the longest single item) → P1.5 → P3.3 → P3.5. Everything in P1 except the piece tree and P2 entirely can run in parallel with P1.4. Suggested coordinator schedule:

1. P0.1–P0.5 sequentially (small).
2. In parallel: P1.1 (best-of), P1.2 (par), P1.3, P1.8, P2.1, P2.3.
3. When P1.3 lands: P1.4 (three Sonnets + Opus). Meanwhile P2.2, P2.4 (par), P2.5, P1.10.
4. P1.5, P1.6, P1.7, P1.9, then P3.
5. P4 in dependency order; P4.1–P4.3 can start as soon as P3.3 is in.

Expected bead count: about 60 including per-grammar and review beads; roughly 80 worker runs counting variants.

## 6. Decisions taken in this plan (reversible, say so if wrong)

- Build: plain GNU Makefile, no CMake/meson.
- Command name `edit`; font DejaVu Sans Mono; non-ASCII glyphs via stb_truetype at runtime, ASCII atlas baked at build.
- X11 presentation timing via the Present extension (`libxcb-present-dev` to install); T0 from X timestamps by default, evdev tracer optional.
- Regex engine: own ERE-subset NFA with literal-prefix filter, not PCRE.
- The best-of modules are: utf8, piece tree, undo, journal, find verifier. The par modules are: scan, GL present path, find filter.
