# HANDOFF — editor project, end of session 1 (2026-10-08)

For the next agent. You are starting the **planning of M0 ("glass")**. Nothing has been coded. Read this, then `PRD.md`, then `perf/01-perf-target.md` §0–§3. Do not re-derive or re-ask anything in §2 below.

## 1. What exists

| Path | Content | Status |
|---|---|---|
| `PRD.md` | Product requirements, 1.7k words, Socratic-elicited from Tobias | draft v1, decisions settled |
| `perf/01-perf-target.md` | Ship gates (p50/p99) for Targets A/B/C, planning bounds, measurement method, chosen architecture, changelog | **v3.4, binding.** Reached a fixed point: the last two review rounds would change no gate number |
| `perf/00-hardware-profile.md` | Measured profile of Target A + 3 addenda of follow-up measurements | all addenda measured **on AC** |
| `perf/02-review-*.md` | 3 angle reviews of v1, verification rounds of v2, v3.2, v3.3 (Codex gpt-6.1-sol xhigh) | reference; findings all dispositioned in the target doc's changelog |
| `perf/bench/` | C sources of every microbenchmark behind the addenda + the Codex review harness (`review.sh`, `angle-*.txt`) | runnable; see its README |
| `perf/README.md` | Index of the above + outstanding measurements | current |
| Memory (`~/.claude/projects/-home-tobias-Projects-editor/memory/`) | Tobias's preferences, PRD decisions, codex invocation | loaded automatically each session |

No git repo yet. No CLAUDE.md yet. No code.

## 2. Settled decisions (do not reopen)

- **Scope:** editing, tabs, right-hand minimap, find/replace in file, word wrap, auto-indent, bracket match, line numbers, hot exit, Sublime Text default keys, full mouse/touchpad. Full Unicode without bidi/IME. Progressive tree-sitter highlighting that never blocks typing.
- **Forever out:** plugins/scripting, file tree/project search/git, split panes/multiple windows. Deferred (not forbidden): multi-cursor, LSP/autocomplete.
- **Language: C11.** No toolkit (raw xcb/xkbcommon, raw wayland-client, raw Win32 later). Static link; `dlopen` only for GL. Tobias chose C over a Rust recommendation; mitigations (ASan/UBSan CI, fuzzing, no malloc on the typing path) are in PRD §8/§10.
- **Platforms:** v1 Linux X11 + Wayland over one renderer; v2 Windows.
- **Rendering:** GPU (OpenGL 3.3) is the chosen path on A; multi-threaded SSE2 CPU raster is the warm-only fallback; single-thread CPU is out on A (measured, perf §2.2).
- **Buffer:** piece tree over immutable original (copy < 256 MB, mmap above) + chunked add buffer; sparse 64 KiB chunk index built in background; byte-offset scrolling until indexed.
- **Done = Sublime uninstalled after 2 weeks of exclusive daily use**, all Target A gates green on battery.
- **Pessimism is wanted.** Measured battery numbers are binding; estimates take the pessimistic end. Every number carries an evidence tag: (P) physics, (M) measured, (E) estimate, (G) gate; measured ones also carry [bat] or [AC].

## 3. Where the thinking is fragile (know these before planning)

1. **Everything in the three addenda was measured while charging.** Battery rerun is the first task of M0; the scan-dependent gates (G6/G7/G7j) are marked provisional for exactly this reason. Recipe: `perf/bench/README.md`.
2. **First frame after ≥ 15 s idle** is the dominant risk. CPU path: raster 3.4× slower, X server upload 4.5× slower after idle → MT4 CPU misses a full refresh (12.1 ms > 11.1). The GPU path's own wake cost is **unmeasured**. G3i on A (8 / 11.1 ms) is provisional on it. M0 must measure this before the renderer is designed around it.
3. **The window manager costs ~60 ms to map a window** on this Cinnamon/Muffin desktop (desktop effects on). That is outside the app and is why startup has two gates: G4a (software, enforced) and G4b (on glass, tracked only).
4. **Compositor "+1 frame"** in the keystroke chain is a modelling scenario, not measured. G2c (paired difference vs a bare reference window, ≤ 1/3 ms) is the gate the app actually owns. Nobody has put a photodiode on this laptop yet.
5. **Target B is a proxy.** All "B" raster numbers are this laptop driving a 1920x1080 HDMI monitor. The real fixture is specified in perf §1 but does not exist yet.
6. **Target C has k < 1 on typing** under its (E) model and is provisional/not CI-enforced. It constrains design ("raster only changed cells or use the GPU"), not release.
7. **mmap is not a snapshot** (perf §2.14). External modification of a mapped original cannot be undone by copying later; the policy is detect → cancel in-flight jobs → user chooses. Deleted-original bytes are copied into the add buffer at delete time so undo stays byte-exact.
8. **Windows save durability** has no directory-flush equivalent; B's G8d is "data-flushed atomic replacement acknowledged", not durable. Open design gap.

## 4. What M0 has to produce (from PRD §9)

X11 window, GL text, piece tree, typing, scrolling, one file. Exit: G1, G3, G3i, G4a green on A **on battery**, keystroke-to-photon measured once with a photodiode.

Suggested planning order:
1. Battery rerun of `perf/bench/` (scanbench, rasterbench, rasterbench2) → Addendum 4. If scan rate or raster change by > 20 %, update the planning bounds in `01-perf-target.md` (not the gates) and note it in the changelog.
2. GPU first-frame-after-idle probe: minimal EGL/GLX program, glyph quads from an atlas into a 2880x1800 window, fence-timed, after 15 s idle and warm. Decides G3i.
3. Instrumentation first: the T0–T6 timestamp spine from perf §4 (ingress → dequeue → mutation → render done → submit → present complete) and the trace ring buffer, before any editor feature. Every later module is benchmarked against its gate.
4. Then the piece tree with its fuzzer, then the window/input backend, then the renderer, then typing end-to-end.
5. Photodiode rig: Tobias has not been asked whether he has one; it is a hardware purchase (photodiode + microcontroller or sound-card input). Ask before planning on it.

## 5. Conventions used so far

- Documents are Markdown in the repo, not external docs. Tables for numbers; prose kept short.
- Reviews: Codex, read-only, one angle per reviewer, severity BLOCKER/MAJOR/MINOR, recompute everything in python, end with a yes/no verdict. Harness: `perf/bench/review.sh <name> <angle-file>` writes `perf/02-review-<name>.md`. Runs take 25–40 min at xhigh and survive network outages.
- Derivation/revision was done by an Opus subagent from a consolidated adjudication brief (accept/reject per finding, reasons given); rejected points are listed in the target doc §6.
- Versions: `01-perf-target-v1.md`, `-v2.md` archived; later versions edited in place with a changelog block per round.
- Tobias's tone: direct, impatient with features, wants numbers. Say what was measured vs estimated. He plugs/unplugs the laptop during work; check `/sys/class/power_supply/BAT0/status` before any measurement.

## 6. Open items handed over

- Product/command name; embedded font (DejaVu Sans Mono vs JetBrains Mono); scratch-buffer persistence; final grammar list (PRD §11).
- Photodiode availability (see §4.5).
- `git init` + CLAUDE.md + worklog have not been created; do this at the start of M0 planning so `/orient` works next time.
- Whether Tobias wants the PRD and perf target published as shareable docs (offered, not answered).
