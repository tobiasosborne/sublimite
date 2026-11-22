# CLAUDE.md — rules for every agent in sublimité v2

Read order, every session: `HANDOFF.md` → this file → `docs/PLAN_V2.md`. Workers read only their bead plus the headers it names.

v2 is a clean-slate rebuild on branch `v2`. v1 (`main`, `~/.local/bin/sublimite`) is Tobias's daily editor: never touch it, never read or write its profile.

## Laws (v1 laws, preserved)

1. **Perf is a goal, not a progress blocker.** Every module ships `tests/<m>_test.c` and, if it parses or mutates, `fuzz/<m>_fuzz.c`; `make check` must pass before a bead closes. Benches are tracked numbers: `bench/<m>_bench.c` prints p50/p99 and exits 0; a miss is a note in the bead, never a blocker. The single enforced perf guard is typing latency **G1** (input → submit) as a regression test. Perf beads open only from lag Tobias observes, with a repro, or from a trace-ring profile naming the hot module. Check `/sys/class/power_supply/BAT0/status` before measuring and stamp it. Milestones exit by use: Tobias declares them done. Docs-only changes (`docs/`, `*.md`, `.beads/`) need `make all` to build, not the suites.
2. **No malloc on the typing path** (input → mutation → layout → submit). Pools and arenas from `src/base` only. Enforced **live on release builds**: the Law-2 + G1 lane (`gcc -O2`) fails closed if `edit_malloc_guard` reports inert (`active && count == 0` required).
3. **One bead per worker, then stop.** Do exactly the bead's scope; report anything else, do not fix it. Workers never run `bd` or `git`; the coordinator does. When the budget is reached, make the tree build, write `STATUS.md` in your module, report what is missing. Briefs carry scope and acceptance, never wall-clock deadlines.
4. **Numbers carry evidence tags and a derivation**: (P) physics, (M) measured (stamped [bat]/[AC] and load), (E) estimate, (G) gate. No number enters a brief, bead, bench, test or doc without its derivation beside it. A (G) bounded only by (E) is an aspiration, not an enforced limit. Underived numbers are struck on sight; agents do not invent budgets, durations or counts.
5. **Settled decisions stay settled.** C11 (`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wswitch-enum`); Linux is first-class (X11 GL 3.3 + XShm raster; Wayland EGL-only); Windows is second-class (keep only `os.h` / `plat.h` portability seams; no Windows builds in MVP); no GUI toolkit; raw xcb/xkbcommon/wayland-client; GL 3.3 via dlopen on a worker; piece tree + chunked add buffer; static link; AGPL-3.0-or-later. Do not re-ask. Speculative features listed in `docs/PLAN_V2.md` §1 stay out until a real repro opens a post-cutover bead.
6. **Docs in lockstep.** A design choice made while coding goes into `docs/decisions/<bead>.md`; a review into `docs/reviews/`; each session ends with a `docs/worklog/YYYY-MM-DD.md` entry and an updated `HANDOFF.md`.
7. **Tracking is beads only** (`bd`). No TODO files, no issue lists in markdown.
8. **Reviews are triaged, not transcribed.** A finding becomes its own bead only if it would affect trial use (data loss, crash, hang, wrong text on screen, typing lag). Everything else goes into one backlog bead per review, pulled only when a user, fuzzer or trace points at it.
9. **Red-green TDD.** Write the failing test (or fuzz op / bench) first, see it fail, implement to green, refactor. The bead report shows the red run and the green run.

## v2 mechanical anti-decay rules (script-enforced in `make check-lints`)

- **5-layer DAG.** L0 `core` < L1 `text` < L2 `persist` < L3 `gfx_plat` < L4 `ui` < L5 `src/app/`. A layer includes and links only lower layers. The per-module include partial orders in `docs/PLAN_V2.md` §2 are enforced by `tools/check_dag.py`; no cross-coupling inside a layer (the v1 `x11/clip.c` ↔ `wayland/dispatch.c` `private.h` cycle must never recur).
- **`--whole-archive` isolated link per layer.** Each layer links a verification executable with `-Wl,--whole-archive libL<n>.a -Wl,--no-whole-archive` against lower layers only, so unselected objects cannot hide upward references.
- **L5 isolation.** `src/app/` never includes `x11/`, `wayland/`, `gl/` or `raster/` headers; it talks to L3 only through `plat.h` / `plat_native.h` / `render` public headers.
- **No `#include "*.c"` in `tests/` or `fuzz/`** (`tools/check_no_c_includes.py`). Test against public headers and link the layer archive; inject syscall/fault behavior with `-Wl,--wrap=...`.
- **Explicit FSMs.** Every multi-mode lifecycle is an enum handled by `switch` under `-Wswitch-enum` — no coordinating booleans, no `default:` that hides a new state. Multi-axis state (e.g. `savectl`) is modelled as orthogonal enums.
- **`sublimite-v2` namespace isolation** (`tools/check_isolation.py`). Every app-identity path (`OS_APP_DATA`, `OS_APP_CONFIG`, `config/runtime.c`, `ipc_posix.c` abstract + filesystem leaf) uses `sublimite-v2`. v2 must never read v1's `config.toml`, forward into v1, or touch v1 journals. v1-oracle test runs use disposable `XDG_DATA_HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`.
- **No fossils.** No `variants/` directory, no multi-generation variants of one module, no env-var feature matrices. No unindexed `tools/` scripts: every tool is registered in `tools/README.md`.
- **Fail closed.** A lane that silently passes on a missing mock, inert guard, or skipped suite is a bug. No flake-forgiving retries.

## Code conventions

- C11, gcc 13 release, clang 18 sanitizers. One directory per module under `src/`, one public header, `snake_case`, `module_` prefix on every exported symbol.
- Globals only where `docs/PLAN_V2.md` names them (trace ring, allocator hook, pinned `os_fault_service`). Anything else needs a `docs/decisions/` entry.
- Fixed-width types, `size_t` for sizes, byte offsets `uint64_t`.
- Errors are return codes; no `abort()` outside `ASSERT`. No `printf` on the UI thread.
- Threads: the UI thread owns the buffer; workers get snapshots. Shared state crosses only through `src/work` mailboxes.
- Commit per bead: `<wave>.<n>: <summary> (bd-xyz)`, ending with the attribution line the harness provides.

## Where things are

| | |
|---|---|
| Architecture + waves | `docs/PLAN_V2.md` |
| Session state | `HANDOFF.md` |
| Tool registry | `tools/README.md` |
| Decisions / reviews / worklog | `docs/decisions/`, `docs/reviews/`, `docs/worklog/` |
| v1 behavioral contract suite | `tests/contract/` |
