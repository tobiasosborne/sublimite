# sublimité

A tiny, very fast text editor. Editing, tabs, a minimap, nothing else.

„Man sah einander tief in die Augen und rief: »Quelle sublimité!«“
— Wolfgang Hildesheimer, *Meine Erlebnisse im Zeitalter der Ausrufe*

**Status: pre-alpha, under construction.** It opens files and you can type in
them. It is being built by a fleet of coding agents against hard latency gates
(`perf/01-perf-target.md`), coordinated from `PLAN.md`, with every decision
recorded under `docs/decisions/`. Expect churn.

- C11, no toolkit: raw xcb/xkbcommon (Wayland later), GL 3.3 via EGL with a
  CPU raster fallback, static link.
- Piece tree + chunked add buffer, no `malloc` on the typing path.
- `make all` (gcc release), `make check` (clang ASan/UBSan), `make fuzz`,
  `make bench`.

Requirements: `PRD.md`. Plan and workflow: `PLAN.md`. Current state: `HANDOFF.md`.

## Install and run

```sh
make -j3 # (P) shared-box parallelism specified by the install bead.
make install
sublimite file.txt
```

Installs the release editor, `tracedump`, desktop entry and icon under
`~/.local`, and makes sublimité the default for plain text files in the file
manager. Keep `~/.local/bin` on your shell's `PATH` (start a new login session
if your desktop has not picked it up yet). No display or profile variables are
needed. Use `make install PREFIX=/path/to/prefix` for another destination.

Remove the installed files with `make uninstall`, using the same `PREFIX` if
overridden. Your journals and preferences stay in their existing user folders.

## Licence

AGPL-3.0-or-later, see `LICENSE`. Vendored third-party code and data keep their
own licences: `vendor/stb_truetype.h` (public domain / MIT),
`vendor/DejaVuSansMono.ttf` (`vendor/DejaVu-LICENSE`), `vendor/ucd/`
(Unicode licence, see `vendor/ucd/README.md`).
