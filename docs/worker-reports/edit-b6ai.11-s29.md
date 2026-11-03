# edit-b6ai.11 worker report (s29)

Design and trade-offs: `docs/decisions/edit-b6ai.11.md`. Summary: `defaults/editor`
gets the path of a wait-aware wrapper named `fresh` (a basename Omarchy's launcher
runs in place with `--inline`, so `$EDITOR` can wait). The wrapper does
`sublimite --wait` for callers that wait and detaches when Omarchy's own terminal
window (`org.omarchy.fresh` on the parent command line) launched it. `EDITOR`
is not changed.

## Files

- `tools/install-omarchy.sh` (install/uninstall, backups under `$XDG_STATE_HOME/sublimite/omarchy`)
- `tools/omarchy/sublimite-editor.in` (wrapper template)
- `Makefile`: `install-omarchy`, `uninstall-omarchy`; test hooked into `check` and `check-serial`
- `tools/test_install_omarchy.sh` (fixture-based, 5 s, no display), `tools/test_install_omarchy_real.sh` (opt-in real round trip)
- `README.md`, `docs/config.md`, `docs/decisions/edit-b6ai.11.md`

## Red run (test written before the scripts existed)

```
ok   plain 'make install' names no omarchy step
ok   plain 'make uninstall' names no omarchy step
FAIL make install-omarchy is a target
FAIL make uninstall-omarchy is a target
ok   install-omarchy not a prerequisite of install
FAIL no Omarchy: exit 127
FAIL no Omarchy: message was: sh: /home/tobias/sublimite/.wt/edit-b6ai.11-s29/tools/install-omarchy.sh: No such file or directory
ok   no Omarchy: nothing written
FAIL no Omarchy: uninstall exit 127
FAIL install: exit 127
test_install_omarchy: install failed; behaviour sections skipped      (4 ok, 6 FAIL, exit 1)
```

(A first red run, before the guard against continuing after a failed install,
hung in the behaviour section. The fixtures now call `uwsm-app` and the
terminal by absolute path and stub every stock TUI editor name.)

## Green run

```
ok   no sublimite under PREFIX: refuses
ok   no sublimite under PREFIX: says to run make install
ok   refused install changed nothing
ok   launcher without the inline name: refuses
ok   launcher without the inline name: nothing changed
test_install_omarchy: install, wait semantics, uninstall and guards passed
(59 checks, 3 consecutive passes, 4.8 s wall [AC, desktop monolith])
```

Real round trip, private Xvfb :96 (explicit number), release `build/sublimite`,
wrapper installed into a temporary prefix and HOME:

```
ok   real: wrapper blocks while the tab is open
ok   real: wrapper returns 0 after the tab is closed
```

`make all` green; `make TARGET=win all` green; `make check`:
`check: 118 test binaries passed in 126.6 s (parallel, 36 jobs, 6 displays)`, then
`test_replay_cli: all passed`, `test_install_omarchy: install, wait semantics, uninstall and guards passed`,
display lock contract PASS. Real HOME checked after the runs: `~/.local/state/omarchy/defaults`
still absent; no `make install`/`install-omarchy` run against the real HOME.

## Found / open

- Non-inline opens from Omarchy (Super+Shift+N, Setup > Config rows) flash an empty
  `foot` window while the wrapper starts sublimite and exits. Not measured (no
  Hyprland session for a worker). The alternative (EDITOR=`sublimite --wait` via
  uwsm env.d) is described in the decision note.
- `xdg-mime query default` consults system files too, so the test checks
  `mimeapps.list` itself.
- The UI process stays alive after the last `--wait` tab closes (empty window);
  the real-display test kills its own copy by `/proc/*/exe`.
- The xdotool shim in `~/.local/bin` resolves libraries via `$HOME`; scripts that
  override HOME must restore it for xdotool.
