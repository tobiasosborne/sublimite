# tools/ registry

**Rule: every tool in `tools/` must be registered in this file** (purpose, invocation, make target). An unregistered tool is a fossil and is deleted on sight; `make check-lints` may fail on it. Do not add one-off scripts here — keep those out of the repo.

| Tool | Purpose | Invocation | Make target |
|---|---|---|---|
| `check_dag.py` | Enforces the 5-layer DAG and the per-module include partial order from `docs/PLAN_V2.md` §2 (no upward or cross-module includes inside a layer; L5 never includes `x11/`, `wayland/`, `gl/`, `raster/`). | `python3 tools/check_dag.py` | `check-lints` |
| `check_no_c_includes.py` | Fails if any file in `tests/` or `fuzz/` contains `#include "*.c"`. | `python3 tools/check_no_c_includes.py` | `check-lints` |
| `check_isolation.py` | Grep-audits the `sublimite-v2` app identity across `OS_APP_DATA`, `OS_APP_CONFIG`, `config/runtime.c`, `ipc_posix.c` so v2 never touches v1's profile or sockets. | `python3 tools/check_isolation.py` | `check-lints` |
| `clex.py` | Shared minimal C comment/string lexer used by `check_dag.py`, `check_no_c_includes.py`, and `check_isolation.py`. | Imported by `tools/check_*.py` | `check-lints` |
| `t3quota.py` | Quota and pace per provider (Claude, Codex, Gemini), read from T3 Code's caches (`~/.t3/caches/<provider>.json`, `usageLimits`). The coordinator runs it before each dispatch wave; ROOM is the points left before uniform pace. Muse and Pi report no data there. | `python3 tools/t3quota.py [--json]` | none (coordinator tool) |
