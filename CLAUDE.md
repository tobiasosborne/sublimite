# CLAUDE.md — rules for every agent in this repo

Read order, every session: `HANDOFF.md` → this file → `PLAN.md` §1 (workflow) → the bead you were given. Workers read only their bead plus the headers it names.

## Laws

1. **Perf is a goal, not a progress blocker** (2026-10-11, Tobias; replaces "Gates are binding"). `perf/01-perf-target.md` §0.2 is the goal sheet. Every module still ships `tests/<m>_test.c` and, if it parses or mutates, `fuzz/<m>_fuzz.c`; `make check` (ASan/UBSan, clang) must pass before a bead closes. `make check` runs suites in parallel (tools/check_parallel.py); `make check-serial` is the old loop. A change that touches only documentation (`docs/`, `*.md`, `.beads/`) needs `make all` to build, not the suites (Tobias, 2026-10-28: a fast merge path). Benches are **tracked numbers**: `bench/<m>_bench.c` prints p50/p99 against its row and exits 0 regardless; a miss is a note in the bead, never a blocker, never a bead by itself. The single enforced perf guard is typing latency G1 (input → submit) as a regression test in `tests/editor_test.c`. Perf beads are opened only from lag Tobias observes, with a repro, or from a trace-ring profile that names the hot module. Benches run when a bead touches that module, on the box as it is, stamped with power and load; variant picks compare back to back in the same minute. **Milestones exit by use** (PRD §9): Tobias uses it and declares the milestone done or lists blockers. **The Astra vs Sublime drive is retired as a hand-over gate** (Tobias, 2026-10-26: "it was a bootstrap loop to get the first versions above the buggy floor"; it was binding 2026-10-11 to 2026-10-25, six drives filed under docs/reviews/astra-vs-sublime-*.md). Hand-over is now `make check` green plus the bead's own verification; Tobias's findings from use are the loop. A drive may still be run on request as a test tool, never as a gate. A module that performs poorly is fixed independently afterwards.
2. **No malloc on the typing path** (input → mutation → layout → submit). Pools and arenas from `src/base` only. The counting-allocator test enforces it.
3. **One bead per worker, then stop.** Do exactly the bead's scope. Found something else? report it, do not fix it (workers never run `bd` or `git`; the coordinator does). Budget ~100K tokens (Haiku) / 200K (Sonnet): when reached, make the tree build, write STATUS.md in your module, report what is missing. Briefs carry scope and acceptance, never wall-clock deadlines.
4. **Numbers carry evidence tags and a derivation**: (P) physics, (M) measured, (E) estimate, (G) gate; measured also [bat] or [AC]. Check `/sys/class/power_supply/BAT0/status` before any measurement and stamp it. **No number enters a brief, bead, bench, test or doc without its derivation written next to it** (the (P)/(M) inputs and the arithmetic, as `perf/01-perf-target.md` §0 does). A (G) whose planning bound is only (E) is a tracked aspiration, never an enforced limit. A number with no derivation ("20 us", "1 MB", "an hour") is struck on sight by whoever reads it; agents do not invent budgets, durations or counts (Tobias, 2026-10-11: the whole point of the perf doc was to ground numbers in physics).
5. **Settled decisions stay settled** (HANDOFF §2, PRD §8, PLAN §6). C11, no toolkit, raw xcb/xkbcommon/wayland-client, GL 3.3 via dlopen on a worker, piece tree + chunked add buffer, static link. Do not re-ask.
6. **Docs in lockstep.** A design choice made while coding goes into `docs/decisions/<bead>.md`; a review goes to `docs/reviews/`; each session ends with a `docs/worklog/YYYY-MM-DD.md` entry and an updated `HANDOFF.md`.
7. **Tracking is beads only** (`bd`). No TODO files, no issue lists in markdown.
8. **Reviews are triaged, not transcribed** (2026-10-11, Tobias). Sonnet for quick bug hunts on diffs; Codex (`codex exec -m gpt-6.1-sol -c model_reasoning_effort=xhigh -s read-only --skip-git-repo-check -o docs/reviews/<name>.md "<prompt>"`) at epic end. The coordinator reads every finding and exercises judgment: a finding becomes its own bead only if it would affect Tobias's trial use (data loss, crash, hang, wrong text on screen, typing lag). Everything else goes into **one backlog bead per review**, pulled only when a user, a fuzzer or the trace points at it. Hardening against conditions the trial will not hit (FIFOs, restored-mtime rewrites, adversarial fixtures) is backlog by default. Codex finding counts are not a work queue.
9. **Red-green TDD where possible.** Write the failing test (or fuzz op / bench) first, see it fail, then implement until green, then refactor. A bead's report shows the red run and the green run. Frozen specs (P1.3-style header + suite before implementations) are this rule applied at module scale.
10. **No Fable subagents.** The main session is the consultant; Opus coordinates; Haiku/Sonnet implement; Opus synthesises best-of; Codex reviews and test-drives. **Codex `gpt-6.1-sol` (high/xhigh) is Opus-grade** and may implement, synthesise and review via `codex exec --approve-for-me -C <dir>`; prefer it over Opus/Sonnet workers while Codex is behind pace. Run Codex jobs through `tools/codex_run.py` (2026-10-28): it records the thread and resumes it after capacity errors or dropped streams (`--resume <thread>` adopts a job started without it). `gpt-5.6-luna` is below Haiku grade: never for code.
11. **Pace ceiling.** Claude usage stays within +2 % of uniform pace on every `quota` window; the coordinator checks `quota` before each dispatch wave and routes to Codex sol when over (PLAN §5.6–5.7).

## Code conventions

- C11, `-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`, gcc 13 release, clang 18 sanitizers.
- One directory per module under `src/`, one public header, `snake_case`, `module_` prefix on every exported symbol, no globals except the trace ring, the allocator hook, the OS layer's single process-wide fault service (`os_fault_service`, edit-177r.2 / edit-pf3l: Linux SIGBUS and Windows EXCEPTION_IN_PAGE_ERROR need a fixed registry because the native fault ABI has no file-object context), and the file module's single UI-owned notification service (`file_watches`, shared parent watches and one native wake source; edit-er2 / edit-pf3l). `file_bus` and its independent installer are deleted. The Windows OS layer adds one write-once cache object, `os_win_state` (QPC frequency, CPU-clock resolution, `SYSTEM_INFO`, api-set function pointers; edit-177r.3, 2026-10-26). `src/file/check_signal_globals.py` enforces it.
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
