# HANDOFF — sublimité v2

## Session 0 (2026-11-22): scaffold

### Scaffolded & Verified
- `LICENSE` (AGPL-3.0, copied from v1 `main`), `.gitignore`, `README.md`, `CLAUDE.md` (v2 rules), `docs/PLAN_V2.md` (final council-approved plan).
- Build system & 5-layer DAG: `Makefile`, `mk/layers.mk` (`L0`–`L5` archives + `--whole-archive` isolated layer link checks), `mk/grammars.mk` (unified 13-grammar table, gated on `vendor/`).
- Structural linters (`tools/README.md`): `tools/check_dag.py`, `tools/check_no_c_includes.py`, `tools/check_isolation.py`, `tools/clex.py`.
- Full module directory skeleton under `src/` with compiling L0–L5 smoke stubs (`os`, `piece`, `edit`, `render`, `viewport`, `app`, `main.c`) and `tests/layer_L0_test.c`..`layer_L4_test.c`.
- Empty structure with `.gitkeep`: `tests/contract/`, `fuzz/`, `bench/`, `docs/decisions/`, `docs/reviews/`, `docs/worklog/`.
- Verified green from clean tree: `make all` (`build/sublimite-v2`) and `make check` (`check-lints`, `check-layers`, `check-san` exit 0 in <1 s).

### Not done
No `vendor/` checked out yet, no harvested modules yet, no Wave 0 behavioral contract suite, no beads yet. `check-law2`, `check-tsan`, and `fuzz-smoke` currently emit `NOTE` until Wave 1 lands their first suites (switch to fail-closed as suites arrive). v1 `main` and `~/.local/bin/sublimite` are untouched.

## Next: Wave 0 (`docs/PLAN_V2.md` §6)

1. **Vendor.** Check out `vendor/` from v1: `git checkout main -- vendor/` (13 tree-sitter grammars, UCD, `stb_truetype`). Do not copy anything else from `main` yet.
2. **Namespace isolation.** Implement the `sublimite-v2` app identity for `OS_APP_DATA`, `OS_APP_CONFIG`, `config/runtime.c`, and `ipc_posix.c` (abstract + filesystem leaf) as modules land; `python3 tools/check_isolation.py` must be green and grep-audits all four.
3. **Build skeleton.** Confirm `make all`, `make check-layers`, `make check-lints` run on the empty tree (DAG, `#include "*.c"` ban, isolation).
4. **Contract suite.** Build the v1 behavioral contract suite under `tests/contract/`, running v1's tools (`keyinject`, `journal_replay`, `replay`, save-kill hook, Wayland clipboard fixture, headless cell inspector if needed) under disposable `XDG_DATA_HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and fixture files (see v1 `tests/ipc_isolation.h:56`) so it never forwards into the running daily editor.
   - **Green on v1:** session 16–27 regressions (wrapped Return, Ctrl+S with Find Results open, over-capacity restore prioritising unsaved tabs, PDF blank-glyph, occupied-bulk large navigation).
   - **Expected-failure specs** for known v1 bugs v2 must pass: 17-leaf `piece` root collapse, post-rename save conflict, `LINEIDX_EDIT_MAX_CHUNKS` >32 MiB delete+undo, Wayland clipboard copy.
5. **Exit:** `make check` green, namespace isolation verified, contract suite recorded. Then write `docs/worklog/2026-11-22.md` (or the session date), update this file, and open Wave 1 beads.

Read `CLAUDE.md` Law 3 before dispatching: one bead per worker.
