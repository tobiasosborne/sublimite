# tools/ registry

**Rule: every tool in `tools/` must be registered in this file** (purpose, invocation, make target). An unregistered tool is a fossil and is deleted on sight; `make check-lints` may fail on it. Do not add one-off scripts here — keep those out of the repo.

| Tool | Purpose | Invocation | Make target |
|---|---|---|---|
| `check_dag.py` | Reads compiler `.d` files to enforce the layer and module orders in `docs/PLAN_V2.md` §2, including L5's ban on reaching x11, wayland, gl, and raster. | `python3 tools/check_dag.py` after compiling | `check-lints` |
| `check_no_c_includes.py` | Rejects tests and fuzzers whose compiler `.d` files list a C implementation file other than their own source. | `python3 tools/check_no_c_includes.py` after compiling | `check-lints` |
| `check_isolation.py` | Grep-audits the `sublimite-v2` app identity across `OS_APP_DATA`, `OS_APP_CONFIG`, `config/runtime.c`, `ipc_posix.c` so v2 never touches v1's profile or sockets. | `python3 tools/check_isolation.py` | `check-lints` |
| `clex.py` | Minimal C lexer for the source-level identity string check. | Imported by `tools/check_isolation.py` | `check-lints` |
| `tests/lint/run.py` | Compiles fixture translation units and verifies both dependency lints' verdicts. | `python3 tests/lint/run.py` | `check-lints` |
| `t3quota.py` | Quota and pace per provider (Claude, Codex, Gemini), read from T3 Code's caches (`~/.t3/caches/<provider>.json`, `usageLimits`). The coordinator runs it before each dispatch wave; ROOM is the points left before uniform pace. Muse and Pi report no data there. | `python3 tools/t3quota.py [--json]` | none (coordinator tool) |
