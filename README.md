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

## Licence

AGPL-3.0-or-later, see `LICENSE`. Vendored third-party code and data keep their
own licences: `vendor/stb_truetype.h` (public domain / MIT),
`vendor/DejaVuSansMono.ttf` (`vendor/DejaVu-LICENSE`), `vendor/ucd/`
(Unicode licence, see `vendor/ucd/README.md`).
