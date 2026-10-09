# PRD — a text editor that is just fast

Status: draft v1, 2026-10-08. Owner: Tobias. Companion documents: `perf/01-perf-target.md` (ship gates, binding), `perf/00-hardware-profile.md` (measurements).

## 1. Problem

Every editor tried (vi, emacs, Sublime, VS Code) is slower than word processors were on a 286, and each one carries features that are never used. Agents now do most of the coding; what remains is quick, frequent editing of small files, occasional huge logs, and a lot of prose. Sublime Text does this acceptably but nags for upgrades after being paid for. The gap is an editor that opens instantly from anywhere, edits text with Sublime's muscle memory, and does nothing else.

## 2. User

One user, on this ThinkPad (Target A in the perf target) today and on a commodity Windows office desktop later (Target B). No other personas. Anything that would only matter to a different user is out.

## 3. Goals

1. **Instant.** Every enforced gate in `perf/01-perf-target.md` §0.2 passes on Target A on battery. The headline ones: typing input-to-present 1 ms p50 / 2 ms p99; full frame in half a refresh; start to first frame 25 ms p50; open any file to first viewport 6 ms, independent of size.
2. **Everywhere.** Reachable by every route: `edit path[:line[:col]]` from a shell, double-click via xdg-open, from an agent pointing at a file, and a running instance that takes new files into tabs.
3. **Sublime's hands.** Default keybindings are Sublime Text's. No relearning.
4. **Three kinds of text, one editor.** Small code and config files; multi-gigabyte logs; prose with word wrap. All first-class, none degrading the others.
5. **Nothing lost.** Hot exit: quit or crash with unsaved buffers, reopen, everything is back.

## 4. Non-goals (forever)

- No plugins, scripting, extension API or package manager. Features ship in the binary or not at all.
- No file tree, project search, git integration.
- No split panes, no multiple windows. One window, tabs.
- No bidi text, no input method editors. (Not forever on principle, but not planned.)

Deferred, not forbidden: multiple cursors / column select (v1.x); LSP, autocomplete, linting (agents cover this today; revisit only if the gates can hold).

## 5. Definition of done (v1)

Sublime Text is uninstalled after **two weeks of exclusive daily use** on Target A, with every enforced Target A gate green in the benchmark harness on battery, and no fallback to another editor during the trial. Usage is part of the criterion, not just measurements.

## 6. Functional requirements

### 6.1 Launch and routing
- CLI `edit [path[:line[:col]]]...`; `-` reads stdin into a new buffer; `--wait` blocks until the tab closes (for `$EDITOR`/`git commit`).
- Single instance per session: later invocations hand their files to the running window over a Unix socket and exit; `--new-window` is not offered (one window).
- Desktop entry + MIME registration for `text/*` and common code types; `xdg-open` on a text file opens a tab.
- Reopen on external change: a tab whose file changed on disk shows a non-blocking banner (reload / keep mine); if the buffer is unmodified it reloads silently and keeps scroll and cursor. Policy per perf target §2.14.

### 6.2 Buffer and files
- UTF-8 in, byte-exact out. Invalid sequences preserved and shown as placeholders. Line endings detected (LF/CRLF) and preserved. Optional trailing-newline enforcement off by default.
- Files below the copy threshold are loaded into memory; above it (≥ 256 MB on A) they are mapped and indexed in the background; the first viewport never waits for the index (perf §2.4). Line numbers show as estimates until the index completes.
- Full Unicode rendering: combining marks, CJK double width, emoji monochrome. No bidi, no IME.
- Save: atomic replace (write temp, fsync, rename, fsync dir). Save acknowledgement is instant; durability completes on a worker (perf §2.8). "Saved" shows only on completion.
- Hot exit: unsaved buffer contents, tab set, per-tab scroll and cursor and selection, window size are journaled continuously (append-only, fsync-light) and restored on next start. Crash-safe.

### 6.3 Editing
- Sublime Text default keymap for everything implemented. Undo/redo unlimited within a session, grouped by typing bursts.
- Selection: shift+arrows, word/line/document, mouse drag, double/triple click, Alt+drag column selection is **not** in v1.
- Find/replace in file: incremental, highlights all matches as you type, regex (ERE/PCRE-lite), case, whole word, wrap-around, replace one/all. Results stream; the UI thread never scans (perf §2.7).
- Go to line (Ctrl+G) and to byte offset / percentage for unindexed large files.
- Word wrap toggle (default on for prose types, off for code), wraps at window width, indent-aware; soft-wrapped lines navigate visually.
- Auto-indent (keep previous indentation; language-aware indent/dedent only where a grammar is loaded), bracket matching highlight, line numbers, trailing-whitespace display toggle, tab/space setting with detection.

### 6.4 Tabs and minimap
- Tabs across the top: open, close, reorder by drag, Ctrl+Tab MRU, Ctrl+W, Ctrl+Shift+T reopen closed. Tab switch is a redraw, nothing else (perf §2.10).
- Minimap on the right: a downscaled view of the surrounding region, viewport indicator, click to jump, drag to scroll. Rendered from the sparse index for large files, never by rasterising the file (perf §2.11).

### 6.5 Pointer
- Click places cursor, drag selects, wheel and two-finger scroll are pixel-smooth at the panel refresh rate with zero dropped frames (gate G3z). Scrollbar is the minimap.

### 6.6 Syntax highlighting (progressive)
- Grammar-correct highlighting via embedded tree-sitter grammars for the languages actually used (initial set: C, Rust, Python, Julia, JavaScript/TypeScript, JSON, TOML/YAML, Markdown, LaTeX, shell). Grammars statically linked; no runtime loading.
- Highlighting is an overlay computed on a worker, viewport-first, incremental on edit. Text is editable and visible before any highlighting exists; highlight arrival must never cause a dropped frame or touch the typing gate. If parsing of the visible region exceeds one frame budget, the viewport shows plain text until it lands.
- Post-v1 research spike (R1, separate go/no-go): a tiny on-device predictor that speculatively colours the viewport within one frame, replaced by the grammar result when it arrives. It ships only if it fits G1/G3 and is visibly better than "plain then correct".

### 6.7 Configuration and look
- One TOML file in `$XDG_CONFIG_HOME/edit/config.toml`, hot-reloaded. Keymap overrides in the same file. No settings UI.
- Dark theme default, one light theme, both shipped. Embedded default monospace font with fallback to system fonts for missing glyphs (fontconfig discovery asynchronous, never on the typing path).
- Cursor blink stops after 10 s idle; zero wakeups when idle (gate G11).

## 7. Non-functional requirements

- **Performance:** `perf/01-perf-target.md` §0.2 is binding. Target A and B gates are release gates; Target C provisional. CI runs the harness in §4 of that document on the real laptop, on battery.
- **Footprint:** binary ≤ 2 MB, ≤ 640 KiB touched before first frame, baseline memory ≤ 87 MB on A (gate G10). Static linking of our code and grammars; `dlopen` only for the GL driver, off the critical path.
- **Rendering:** GPU path (OpenGL 3.3 via EGL/GLX) is the chosen path on A; multi-threaded CPU raster is the warm fallback and the path for machines without usable GL. Both must exist (perf §3).
- **Platforms:** v1 Linux, X11 and Wayland backends over one renderer. v2 Windows 11 (Win32, D3D11 or GL), same codebase. 64-bit only.
- **Reliability:** no data loss on crash (hot exit journal); external-modification race documented and detected (perf §2.14).
- **Accessibility, i18n of the UI:** none in v1. English UI strings only.

## 8. Technical constraints (decided)

- Language: **C11**. No toolkit; raw xcb + xkbcommon, raw wayland-client + xkbcommon, raw Win32 later. Hand SIMD (SSE2 baseline with AVX2 dispatch) for scans and the CPU raster.
- Text buffer: piece tree over an immutable original (copy or mmap) plus a chunked append-only add buffer, per-node byte and newline counts; sparse 64 KiB chunk index (perf §2.6, §2.5).
- Threading: UI thread does input, edit, layout, submit; workers do index, find, save, highlight; all cancellable within 5 ms logical ack (G6c).
- Grammar engine: tree-sitter (C library, statically linked). Shaping: HarfBuzz is **not** linked in v1; monospace cell layout with a combining-mark pass; revisit if a prose font is wanted.
- Agent-built codebase: every module ships with its benchmark against the relevant gate; a gate regression fails CI.

## 9. Milestones (each gated, none dated yet)

| | Scope | Exit criterion |
|---|---|---|
| **M0 glass** | X11 window, GL text, piece tree, typing, scrolling, one file | G1, G3, G3i, G4a green on A on battery; keystroke-to-photon measured with a photodiode once |
| **M1 editor** | tabs, minimap, find/replace, undo, wrap, auto-indent, save, hot exit, CLI, single instance, xdg integration, large-file path | all enforced A gates green; a day of real use possible |
| **M2 daily** | Wayland backend, tree-sitter highlighting (async), config, themes, polish | the two-week trial starts |
| **v1 done** | | Sublime uninstalled |
| **M3 Windows** | Win32 + D3D11/GL backend, B fixture measured | B gates green on the real B machine |
| **R1 spike** | speculative neural highlighting | go/no-go against G1/G3 |

## 10. Risks

1. **After-idle penalty on A.** Measured CPU first-frame-after-idle misses a whole refresh; the GPU path's own wake cost is unmeasured. If GPU wake is also slow, G3i cannot be met and the aspiration drops to "one-frame slip allowed" permanently. First measurement in M0.
2. **All measurements were on AC.** Battery rerun is the first task of M0; scan-dependent gates are provisional until then.
3. **Wayland compositor latency is unknown** and not controllable from the app; Mutter/Muffin frame scheduling may add a frame. Mitigation: measure both backends with the same photodiode rig; the X11 compositor-bypass fullscreen mode has no Wayland equivalent.
4. **Tree-sitter against the frame gate** on large files and pathological grammars. Mitigation: viewport-first, hard per-frame budget, plain-text fallback; grammar set is small and fixed.
5. **Unicode without HarfBuzz**: monospace cell layout breaks on some combining sequences and all ligatures. Accepted for v1; prose is still readable.
6. **Agent-written C**: memory safety. Mitigation: ASan/UBSan in CI, fuzzing of the buffer, parser and journal replay; no `malloc` on the typing path.

## 11. Open questions

- ~~Product and command name~~ **Settled 2026-10-09: the product is "sublimité", always lower case** (command and binary `sublimite`, ASCII, no accent; window title and `_NET_WM_NAME` "sublimité", `WM_CLASS` `sublimite`/`sublimite`; journal/config dir `~/.local/share/sublimite`, `~/.config/sublimite`). Sublime, lite, and *sublimité*. The reference is Wolfgang Hildesheimer, "Meine Erlebnisse im Zeitalter der Ausrufe" (*Lieblose Legenden*): „Man sah einander tief in die Augen und rief: »Quelle sublimité!«" — the hollow exclamation of an age that only exclaims. A tool named after the exclamation, with nothing in it to exclaim about. `edit` stays as the development shorthand in beads and docs until the rename lands. **Title face (Tobias, 2026-10-09): Bodoni Moda Italic** (Owen Earl 2020, OFL, no reserved name), lower case, the wordmark baked at build time as outlines/bitmap from `docs/design/title-font/`; runner-up CAT Eckmann. Research and renders: `docs/design/title-font/README.md`.
- Default font to embed (licence must allow embedding; DejaVu Sans Mono or JetBrains Mono).
- Whether the hot-exit journal should also back a cross-session "scratch" buffer (Sublime's untitled tabs that never get saved).
- Initial grammar list: confirm the ten above against what actually gets edited in the trial.
