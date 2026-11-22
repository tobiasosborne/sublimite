# sublimité v2

A clean-slate C11 rebuild of the sublimité text editor: v1 feature parity minus v1's architectural debt. No GUI toolkit, no Windows MVP, AGPL-3.0-or-later. Full plan: [`docs/PLAN_V2.md`](docs/PLAN_V2.md).

## Scope

- **Linux is first-class:** X11 (GL 3.3 + XShm CPU raster) and Wayland (EGL-only).
- **Windows is second-class:** only the zero-cost portability seams (`os.h`, `plat.h`) are kept; no Windows builds in v2 MVP.
- Typing path does no `malloc` (Law 2), enforced live on release builds.

## Architecture (5-layer acyclic DAG)

```
L5  src/app/       app_loop, app_buffers, app_modal, app_status, app_save,
                   app_session, app_paint, app_panels, main.c
 │
L4  libui.a        viewport, layout, tabs, minimap, find_session/panel, fif,
                   hl, cmd, config, dnd
 │
L3  libgfx_plat.a  plat (X11, Wayland), render, raster, gl, font
 │
L2  libpersist.a   file, journal, savectl, edit
 │
L1  libtext.a      scan, utf8, indent, piece, undo, lineidx, find
 │
L0  libcore.a      os, base, trace, work, ipc
```

A layer depends only on lower layers; `tools/check_dag.py` also enforces the per-module include order inside each layer.

## Build and check

```
make all           # build everything
make check         # ASan/UBSan lane + release Law-2/G1 lane + structural lints
make check-layers  # isolated --whole-archive link per layer
make check-lints   # include DAG, no #include "*.c", namespace isolation, generator --check
make check-tsan    # TSan lane on threaded suites
make fuzz-smoke    # bounded fuzz runs over committed seed corpora
```

## Namespace isolation

v2's app identity is `sublimite-v2` in every data, config, and IPC path, so v2 can never read or write v1's profile, forward into a running v1, or touch v1 journals. Enforced by `tools/check_isolation.py`. v1 oracle runs use disposable `XDG_*` directories.

## Layout

| Path | Contents |
|---|---|
| `src/` | one directory per module |
| `tests/`, `tests/contract/` | per-module suites; the v1 behavioral contract suite |
| `fuzz/`, `bench/` | fuzz targets and tracked benches |
| `tools/` | registered scripts (see `tools/README.md`) |
| `docs/` | plan, decisions, reviews, worklog |

Agents: start with [`CLAUDE.md`](CLAUDE.md) and [`HANDOFF.md`](HANDOFF.md).
