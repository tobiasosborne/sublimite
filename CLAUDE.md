# CLAUDE.md — rules for every agent in this repo

Read order, every session: `HANDOFF.md` → this file → `PLAN.md` §1 (workflow) → the bead you were given. Workers read only their bead plus the headers it names.

## Laws

1. **Gates are binding.** `perf/01-perf-target.md` §0.2 is the contract. Every module ships with `tests/<m>_test.c`, `bench/<m>_bench.c` (prints p50/p99 vs its gate, non-zero exit on miss) and, if it parses or mutates, `fuzz/<m>_fuzz.c`. `make check` (ASan/UBSan, clang) must pass before a bead closes. **Bench policy (2026-10-09, Tobias):** a module's bench runs **once** on a quiet [AC] box when the module lands and the numbers are recorded in `docs/decisions/<bead>.md`; it is re-run only by a bead that touches that module's hot path, or when a row sat within 20 % of its gate. Rows with > 5x headroom become a loose assert in the test, not a measurement ritual. Nobody reads a gate on a loaded box or on battery: record TRACK or wait. Benches exist to make a decision (variant pick, regression on a hot path), not to produce evidence. From M0 on, the trace ring's end-to-end keystroke-to-pixel number on the real display is the primary gate; module gates are regression guards for what the trace shows is hot.
2. **No malloc on the typing path** (input → mutation → layout → submit). Pools and arenas from `src/base` only. The counting-allocator test enforces it.
3. **One bead per worker, then stop.** Do exactly the bead's scope. Found something else? report it, do not fix it (workers never run `bd` or `git`; the coordinator does). Budget ~100K tokens (Haiku) / 200K (Sonnet): when reached, make the tree build, write STATUS.md in your module, report what is missing. Briefs carry scope and acceptance, never wall-clock deadlines.
4. **Numbers carry evidence tags**: (P) physics, (M) measured, (E) estimate, (G) gate; measured also [bat] or [AC]. Check `/sys/class/power_supply/BAT0/status` before any measurement and stamp it.
5. **Settled decisions stay settled** (HANDOFF §2, PRD §8, PLAN §6). C11, no toolkit, raw xcb/xkbcommon/wayland-client, GL 3.3 via dlopen on a worker, piece tree + chunked add buffer, static link. Do not re-ask.
6. **Docs in lockstep.** A design choice made while coding goes into `docs/decisions/<bead>.md`; a review goes to `docs/reviews/`; each session ends with a `docs/worklog/YYYY-MM-DD.md` entry and an updated `HANDOFF.md`.
7. **Tracking is beads only** (`bd`). No TODO files, no issue lists in markdown.
8. **Reviews**: Sonnet for quick bug hunts on diffs; Codex (`codex exec -m gpt-6.1-sol -c model_reasoning_effort=xhigh -s read-only --skip-git-repo-check -o docs/reviews/<name>.md "<prompt>"`) for epic-end and best-of modules. Prompt it to relentlessly find all problems with severity and a concrete fix. Every BLOCKER/MAJOR becomes a bead.
9. **Red-green TDD where possible.** Write the failing test (or fuzz op / bench) first, see it fail, then implement until green, then refactor. A bead's report shows the red run and the green run. Frozen specs (P1.3-style header + suite before implementations) are this rule applied at module scale.
10. **No Fable subagents.** The main session is the consultant; Opus coordinates; Haiku/Sonnet implement; Opus synthesises best-of; Codex reviews and test-drives. **Codex `gpt-6.1-sol` (high/xhigh) is Opus-grade** and may implement, synthesise and review via `codex exec --approve-for-me -C <dir>`; prefer it over Opus/Sonnet workers while Codex is behind pace. `gpt-5.6-luna` is below Haiku grade: never for code.
11. **Pace ceiling.** Claude usage stays within +2 % of uniform pace on every `quota` window; the coordinator checks `quota` before each dispatch wave and routes to Codex sol when over (PLAN §5.6–5.7).

## Code conventions

- C11, `-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`, gcc 13 release, clang 18 sanitizers.
- One directory per module under `src/`, one public header, `snake_case`, `module_` prefix on every exported symbol, no globals except the trace ring and the allocator hook.
- Fixed-width types, `size_t` for sizes, byte offsets are `uint64_t`.
- Errors are return codes; no `abort()` outside `ASSERT`. No `printf` on the UI thread.
- Threads: UI thread owns the buffer; workers get snapshots. Shared state crosses only through `src/work` mailboxes.
- Commit per bead: `P1.4: piece tree best-of (bd-xyz)`, ending with the attribution line the harness provides.

## Where things are

| | |
|---|---|
| Requirements | `PRD.md` |
| Gates and architecture | `perf/01-perf-target.md` §0.2, §3, §4 |
| Measurements so far | `perf/00-hardware-profile.md` (all on AC) |
| Plan, roles, modes | `PLAN.md` |
| Microbenchmarks from planning | `perf/bench/` |
| Corpus | `tools/mkcorpus` → `/tmp/edit-corpus` |
